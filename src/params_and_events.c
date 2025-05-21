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
        Plugin*          p    = (Plugin*)(compressed >> 8);
        CPLUG_LOG_ASSERT(p != NULL);
        if (p)
        {
            switch (type)
            {
            case GLOBAL_EVENT_DEQUEUE_MAIN:
                main_dequeue_events(p);
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

void send_to_global_event_queue(enum GlobalEvent type, Plugin* ptr)
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

void send_to_audio_event_queue(Plugin* plugin, const CplugEvent event)
{
    CPLUG_LOG_ASSERT(is_main_thread());
    int head                         = cplug_atomic_load_i32(&plugin->queue_audio_head) & EVENT_QUEUE_MASK;
    plugin->queue_audio_events[head] = event;
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
        send_to_audio_event_queue(p, e);
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
        send_to_audio_event_queue(p, e);
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
    if (p->cplug_ctx->type == CPLUG_PLUGIN_IS_CLAP)
    {
        send_to_audio_event_queue(p, e);
    }
    else
    {
        p->cplug_ctx->sendParamEvent(p->cplug_ctx, &e);
        e.parameter.type = EVENT_SET_PARAMETER;
        send_to_audio_event_queue(p, e);
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
        send_to_audio_event_queue(p, event);
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

uint32_t cplug_getNumParameters(void*) { return NUM_PARAMS; }
uint32_t cplug_getParameterID(void* p, uint32_t paramIndex) { return paramIndex; }
uint32_t cplug_getParameterFlags(void* p, uint32_t paramId) { return CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE; }

// NOTE: AUv2 supports a max length of 52 bytes, VST3 128, CLAP 256
void cplug_getParameterName(void*, uint32_t paramId, char* buf, size_t buflen)
{
    const char*        str     = "";
    static const char* NAMES[] = {"Cutoff", "Scream", "Resonance", "Input", "Wet"};
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
    // println("%s %s %f", __FUNCTION__, PARAM_STR[paramId], value);
    Plugin* p = _p;
    if (value < 0)
        value = 0;
    if (value > 1)
        value = 1;

    CplugEvent e;
    e.parameter.id    = paramId;
    e.parameter.value = value;
    if (is_main_thread())
    {
        main_set_param(p, paramId, value);
        e.type = EVENT_SET_PARAMETER;
        send_to_audio_event_queue(p, e);
    }
    else
    {
        audio_set_param(p, paramId, value);
        e.type = EVENT_SET_PARAMETER;
        send_to_main_event_queue(p, e);
    }
}
// VST3 only
double cplug_denormaliseParameterValue(void*, uint32_t paramId, double value) { return value; }
double cplug_normaliseParameterValue(void*, uint32_t paramId, double value) { return value; }

double cplug_parameterStringToValue(void*, uint32_t paramId, const char* str)
{
    double val = 0;
    switch ((ParamID)paramId)
    {
    case PARAM_CUTOFF:
        if (1 == sscanf(str, "%lfHz", &val))
            val = xm_fast_normalise_Hz1(val);
        break;
    case PARAM_SCREAM:
    case PARAM_RESONANCE:
    case PARAM_WET:
        if (1 == sscanf(str, "%lf%%", &val))
            val *= 0.01;
        break;
    case PARAM_INPUT_GAIN:
        if (1 == sscanf(str, "%lfdB", &val))
            val = xm_normd(val, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
        break;
    }
    return val;
}

void cplug_parameterValueToString(void*, uint32_t paramId, char* buf, size_t bufsize, double value)
{
    switch ((ParamID)paramId)
    {
    case PARAM_CUTOFF:
    {
        float Hz = xm_fast_denomalise_Hz(value);
        Hz       = xm_minf(Hz, 20000);
        snprintf(buf, bufsize, "%.2fHz", Hz);
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
    }
}