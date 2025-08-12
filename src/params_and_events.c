#include "plugin.h"
#include <stdio.h>
#include <xhl/maths.h>

struct
{
    union
    {
        void*         _lock_aligner;
        xt_spinlock_t lock;
    };
    xt_atomic_uint32_t head;
    unsigned           tail;
    size_t             events[EVENT_QUEUE_SIZE];
} g_event_queue = {0};

void dequeue_global_events()
{
    if (!is_main_thread())
    {
        println("[WARNING] Called dequeue_global_events off of the main thread");
    }
    CPLUG_LOG_ASSERT(is_main_thread());
    unsigned head = xt_atomic_load_u32(&g_event_queue.head);
    while (g_event_queue.tail != head)
    {
        size_t compressed = g_event_queue.events[g_event_queue.tail];

        enum GlobalEvent type = (enum GlobalEvent)(compressed & 0xff);
        void*            ptr  = (void*)(compressed >> 8);
        CPLUG_LOG_ASSERT(ptr != NULL);
        if (ptr)
        {
            switch (type)
            {
            case GLOBAL_EVENT_DEQUEUE_MAIN:
                main_dequeue_events((Plugin*)ptr);
                break;
            case GLOBAL_EVENT_GARBAGE_COLLECT_FREE:
                xfree(ptr);
                break;
            default:
                println("[WARNING] Unhanled global event: %u", type);
                break;
            }
        }

        g_event_queue.tail++;
        g_event_queue.tail &= EVENT_QUEUE_MASK;

        head = xt_atomic_load_u32(&g_event_queue.head) & EVENT_QUEUE_MASK;
    }
}

void send_to_global_event_queue(enum GlobalEvent type, void* ptr)
{
    // NOTE: x86 64 processors use a 48 bit address space. New ARMv8.2 chips use 52 bits.
    // This gives us a max of 12 bits to reliably play with. We'll only use 8
    size_t compressed = (size_t)type | ((size_t)ptr << 8ULL);
    xt_spinlock_lock(&g_event_queue.lock);

    unsigned head              = xt_atomic_load_u32(&g_event_queue.head) & EVENT_QUEUE_MASK;
    g_event_queue.events[head] = compressed;
    xt_atomic_fetch_add_u32(&g_event_queue.head, 1);
    xt_atomic_fetch_and_u32(&g_event_queue.head, EVENT_QUEUE_MASK);

    xt_spinlock_unlock(&g_event_queue.lock);
}

void send_to_audio_event_queue(Plugin* plugin, const CplugEvent* event)
{
    CPLUG_LOG_ASSERT(is_main_thread());
    int head                         = cplug_atomic_load_i32(&plugin->queue_audio_head) & EVENT_QUEUE_MASK;
    plugin->queue_audio_events[head] = *event;
    cplug_atomic_fetch_add_i32(&plugin->queue_audio_head, 1);
    cplug_atomic_fetch_and_i32(&plugin->queue_audio_head, EVENT_QUEUE_MASK);
}

void send_to_main_event_queue(Plugin* p, const CplugEvent event)
{
    xt_spinlock_lock(&p->queue_main_spinlock);

    unsigned head              = xt_atomic_load_u32(&p->queue_main_head) & EVENT_QUEUE_MASK;
    p->queue_main_events[head] = event;
    xt_atomic_fetch_add_u32(&p->queue_main_head, 1);
    xt_atomic_fetch_and_u32(&p->queue_main_head, EVENT_QUEUE_MASK);

    xt_spinlock_unlock(&p->queue_main_spinlock);
}

void param_change_begin(Plugin* p, ParamID id)
{
    // println("%s %s", __FUNCTION__, PARAM_STR[id]);
    CPLUG_LOG_ASSERT(is_main_thread());
    CplugEvent e     = {0};
    e.parameter.type = CPLUG_EVENT_PARAM_CHANGE_BEGIN;
    e.parameter.id   = id;

    if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_CLAP)
        send_to_audio_event_queue(p, &e);
    else
        p->cplug_ctx->sendParamEvent(p->cplug_ctx, &e);
}

void param_change_end(Plugin* p, ParamID id)
{
    // println("%s %s", __FUNCTION__, PARAM_STR[id]);
    CPLUG_LOG_ASSERT(is_main_thread());
    CplugEvent e     = {0};
    e.parameter.type = CPLUG_EVENT_PARAM_CHANGE_END;
    e.parameter.id   = id;
    if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_CLAP)
        send_to_audio_event_queue(p, &e);
    else
        p->cplug_ctx->sendParamEvent(p->cplug_ctx, &e);
}

void param_change_update(Plugin* p, ParamID id, double value)
{
    // println("%s %s %f", __FUNCTION__, PARAM_STR[id], value);
    CPLUG_LOG_ASSERT(is_main_thread());

    if (value < 0)
        value = 0;
    if (value > 1)
        value = 1;
    p->main_params[id] = value;

    CplugEvent e;
    e.parameter.type  = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
    e.parameter.id    = id;
    e.parameter.value = value;

    if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_VST3 || p->cplug_ctx->type == CPLUG_PLUGIN_IS_AUV2)
    {
        p->cplug_ctx->sendParamEvent(p->cplug_ctx, &e);
    }

    if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_CLAP || p->cplug_ctx->type == CPLUG_PLUGIN_IS_STANDALONE ||
        p->cplug_ctx->type == CPLUG_PLUGIN_IS_AUV2)
    {
        if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_STANDALONE || p->cplug_ctx->type == CPLUG_PLUGIN_IS_AUV2)
            e.type = EVENT_SET_PARAMETER;
        send_to_audio_event_queue(p, &e);
    }
}

void param_set(Plugin* p, ParamID id, double value)
{
    if (value < 0)
        value = 0;
    if (value > 1)
        value = 1;
    // println("%s %s %f", __FUNCTION__, PARAM_STR[id], value);
    if (p->main_params[id] != value)
    {
        param_change_begin(p, id);
        param_change_update(p, id, value);
        param_change_end(p, id);
    }
}

void main_set_param(Plugin* p, ParamID id, double value)
{
    // println("%s %s %f", __FUNCTION__, PARAM_STR[id], value);
    CPLUG_LOG_ASSERT(is_main_thread());
    CPLUG_LOG_ASSERT(id >= 0 && id < NUM_PARAMS);
    p->main_params[id] = value;
}

double main_get_param(Plugin* p, ParamID id)
{
    // println("%s %s", __FUNCTION__, PARAM_STR[id]);
    CPLUG_LOG_ASSERT(is_main_thread());
    CPLUG_LOG_ASSERT(id >= 0 && id < NUM_PARAMS);
    return p->main_params[id];
}

void audio_set_param(Plugin* p, ParamID id, double value)
{
    // println("%s %s %f", __FUNCTION__, PARAM_STR[id], value);
    CPLUG_LOG_ASSERT(!is_main_thread());
    CPLUG_LOG_ASSERT(id >= 0 && id < NUM_PARAMS);
    p->audio_params[id] = value;
}

void main_notify_host_param_change(Plugin* p, ParamID id, double value)
{
    CPLUG_LOG_ASSERT(is_main_thread());
    CplugEvent event = {.parameter.id = id, .parameter.value = value};
    if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_CLAP)
    {
        event.type = EVENT_SET_PARAMETER_NOTIFYING_HOST;
        send_to_audio_event_queue(p, &event);
    }
    else // Standalone, VST3, AUv2
    {
        event.type = CPLUG_EVENT_PARAM_CHANGE_BEGIN;
        p->cplug_ctx->sendParamEvent(p->cplug_ctx, &event);

        event.type = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
        p->cplug_ctx->sendParamEvent(p->cplug_ctx, &event);

        event.type = CPLUG_EVENT_PARAM_CHANGE_END;
        p->cplug_ctx->sendParamEvent(p->cplug_ctx, &event);
    }
}

void main_dequeue_events(Plugin* p)
{
    CPLUG_LOG_ASSERT(is_main_thread());
    uint32_t head = xt_atomic_load_u32(&p->queue_main_head) & EVENT_QUEUE_MASK;
    uint32_t tail = p->queue_main_tail;

    while (tail != head)
    {
        CplugEvent* event = &p->queue_main_events[tail];

        switch (event->type)
        {
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
        case EVENT_SET_PARAMETER:
        case EVENT_SET_PARAMETER_NOTIFYING_HOST:
            main_set_param(p, event->parameter.id, event->parameter.value);
            if (event->type == EVENT_SET_PARAMETER_NOTIFYING_HOST)
                main_notify_host_param_change(p, event->parameter.id, event->parameter.value);

            if (p->gui)
            {
                extern void gui_handle_param_change(void* gui, ParamID paramid);
                gui_handle_param_change(p->gui, event->parameter.id);
            }
            break;

        default:
            println("[MAIN] Unhandled event in main queue: %u", event->type);
            break;
        }

        tail++;
        tail &= EVENT_QUEUE_MASK;
    }
    p->queue_main_tail = tail;
}

bool param_string_to_value(uint32_t param_id, const char* str, double* val)
{
    int ok = 0;

    switch ((ParamID)param_id)
    {
    case PARAM_CUTOFF:
        if ((ok = sscanf(str, "%lfHz", val)))
            *val = log(*val / 20.0) / log(2.0) / 10.0; // Normalise Hz
        break;
    case PARAM_SCREAM:
    case PARAM_RESONANCE:
    case PARAM_WET:
        if ((ok = sscanf(str, "%lf%%", val)))
            *val *= 0.01;
        break;
    case PARAM_INPUT_GAIN:
        if ((ok = sscanf(str, "%lfdB", val)))
            *val = xm_normd(*val, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
        break;
    case PARAM_PATTERN_LFO_1:
    case PARAM_PATTERN_LFO_2:
        if ((ok = sscanf(str, "%lf", val)))
            *val = xm_normd(*val, 1, NUM_LFO_PATTERNS);
        break;
    }
    if (ok)
        *val = xm_clampd(*val, 0, 1);
    return ok;
}

//=====================================================================================

uint32_t cplug_getNumParameters(void*) { return NUM_PARAMS; }
uint32_t cplug_getParameterID(void* p, uint32_t paramIndex) { return paramIndex; }
uint32_t cplug_getParameterFlags(void* p, uint32_t paramId) { return CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE; }

// NOTE: AUv2 supports a max length of 52 bytes, VST3 128, CLAP 256
void cplug_getParameterName(void*, uint32_t paramId, char* buf, size_t buflen)
{
    const char*        str     = "";
    static const char* NAMES[] = {"Cutoff", "Scream", "Resonance", "Input", "Wet", "LFO 1 Pattern", "LFO 2 Pattern"};
    _Static_assert(ARRLEN(NAMES) == NUM_PARAMS);
    if (paramId < NUM_PARAMS)
    {
        str = NAMES[paramId];
    }
    snprintf(buf, buflen, "%s", str);
}

void cplug_getParameterRange(void*, uint32_t paramId, double* min, double* max)
{
    *min = 0;
    *max = 1;
}

double cplug_getDefaultParameterValue(void* _p, uint32_t paramId)
{
    double v = 0.0;
    switch ((ParamID)paramId)
    {
    case PARAM_CUTOFF:
        v = 1;
        // v = xm_fast_normalise_Hz2(5000);
        break;
    case PARAM_SCREAM:
        // v = 0.25;
        break;
    case PARAM_RESONANCE:
        // v = 0.5;
        break;
    case PARAM_INPUT_GAIN:
        v = xm_normd(0, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
        break;
    case PARAM_WET:
        v = 1;
        break;
    case PARAM_PATTERN_LFO_1:
    case PARAM_PATTERN_LFO_2:
        // v = 0;
        break;
    }
    return v;
}

double cplug_getParameterValue(void* _p, uint32_t paramId)
{
    Plugin* p = _p;
    double  value;
    if (is_main_thread())
    {
        value = p->main_params[paramId];
        // println("[main] %s %s %f", __FUNCTION__, PARAM_STR[paramId], value);
    }
    else
    {
        value = p->audio_params[paramId];
        // println("[audio] %s %s %f", __FUNCTION__, PARAM_STR[paramId], value);
    }

    return value;
}
// [hopefully audio thread] VST3 & AU only
void cplug_setParameterValue(void* _p, uint32_t paramId, double value)
{
    Plugin* p = _p;
    if (value < 0)
        value = 0;
    if (value > 1)
        value = 1;

    const bool is_main = is_main_thread();
    if (is_main)
    {
        // println("[main] %s %s %f", __FUNCTION__, PARAM_STR[paramId], value);
        main_set_param(p, paramId, value);
    }
    else
    {
        // println("[audio] %s %s %f", __FUNCTION__, PARAM_STR[paramId], value);
        audio_set_param(p, paramId, value);
    }

    if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_AUV2)
    {
        CplugEvent e;
        e.type            = EVENT_SET_PARAMETER;
        e.parameter.id    = paramId;
        e.parameter.value = value;
        if (is_main)
            send_to_audio_event_queue(p, &e);
        else
            send_to_main_event_queue(p, e);
    }
}
// VST3 only
double cplug_denormaliseParameterValue(void*, uint32_t paramId, double value) { return value; }
double cplug_normaliseParameterValue(void*, uint32_t paramId, double value) { return value; }

double cplug_parameterStringToValue(void*, uint32_t paramId, const char* str)
{
    double val = 0;
    param_string_to_value(paramId, str, &val);
    return val;
}

void cplug_parameterValueToString(void*, uint32_t paramId, char* buf, size_t bufsize, double value)
{
    switch ((ParamID)paramId)
    {
    case PARAM_CUTOFF:
    {
        double Hz = 20 * pow(2, value * 10); // Denormalise Hz
        Hz        = xm_mind(Hz, 20000);
        snprintf(buf, bufsize, "%.2lfHz", Hz);
        break;
    }
    case PARAM_SCREAM:
    case PARAM_RESONANCE:
    case PARAM_WET:
        snprintf(buf, bufsize, "%.2f%%", value * 100);
        break;
    case PARAM_INPUT_GAIN:
    {
        double dB = xm_lerpd(value, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
        snprintf(buf, bufsize, "%.2fdB", dB);
        break;
    }
    case PARAM_PATTERN_LFO_1:
    case PARAM_PATTERN_LFO_2:
    {
        int num = xm_droundi(xm_lerpd(value, 1, NUM_LFO_PATTERNS));
        snprintf(buf, bufsize, "%d", num);
        break;
    }
    }
}