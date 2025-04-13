#include "common.h"

#include "dsp.h"
#include "plugin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <xhl/maths.h>
#include <xhl/thread.h>
#include <xhl/time.h>

// Apparently denormals aren't a problem on ARM & M1?
// https://en.wikipedia.org/wiki/Subnormal_number
// https://www.kvraudio.com/forum/viewtopic.php?t=575799
#if __arm64__
#define DISABLE_DENORMALS
#define RESTORE_DENORMALS
#elif defined(_WIN32)
// https://softwareengineering.stackexchange.com/a/337251
#include <immintrin.h>
#define DISABLE_DENORMALS                                                                                              \
    unsigned int oldMXCSR = _mm_getcsr();       /*read the old MXCSR setting  */                                       \
    unsigned int newMXCSR = oldMXCSR |= 0x8040; /* set DAZ and FZ bits        */                                       \
    _mm_setcsr(newMXCSR);                       /* write the new MXCSR setting to the MXCSR */
#define RESTORE_DENORMALS _mm_setcsr(oldMXCSR);
#else
#include <fenv.h>
#define DISABLE_DENORMALS                                                                                              \
    fenv_t _fenv;                                                                                                      \
    fegetenv(&_fenv);                                                                                                  \
    fesetenv(FE_DFL_DISABLE_SSE_DENORMS_ENV);
#define RESTORE_DENORMALS fesetenv(&_fenv);
#endif

// Used for midi note maths
#define MIDI_NOTE_NUM_20Hz  15.486820576352429f // getMidiNoteFromHertz(20)
#define MIDI_NOTE_NUM_20kHz 135.0762319922975f  // getMidiNoteFromHertz(20000)
// getMidiNoteFromHertz(20000) - getMidiNoteFromHertz(20)
#define MIDI_NOTE_NUM_RANGE 119.58941141594507f

XTHREAD_LOCAL bool g_is_main_thread = false;
bool               is_main_thread() { return g_is_main_thread; }

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
} g_event_queue;

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

void dequeue_global_events()
{
    if (!g_is_main_thread)
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

void cplug_libraryLoad()
{
    xtime_init();
    xalloc_init();
    memset(&g_event_queue, 0, sizeof(g_event_queue));
}
void cplug_libraryUnload() { xalloc_shutdown(); }

extern void library_load_platform();
extern void library_unload_platform();
void*       cplug_createPlugin(CplugHostContext* ctx)
{
    g_is_main_thread = true;
    library_load_platform();

    struct Plugin* p = MY_CALLOC(1, sizeof(*p));
    p->cplug_ctx     = ctx;

    p->width  = GUI_INIT_WIDTH;
    p->height = GUI_INIT_HEIGHT;

    for (int i = 0; i < ARRLEN(p->main_params); i++)
        p->main_params[i] = cplug_getDefaultParameterValue(p, i);
    memcpy(p->audio_params, p->main_params, sizeof(p->main_params));
    _Static_assert(sizeof(p->main_params) == sizeof(p->audio_params));

    return p;
}
void cplug_destroyPlugin(void* p)
{
    CPLUG_LOG_ASSERT(p != NULL);
    MY_FREE(p);

    library_unload_platform();
}

uint32_t cplug_getNumInputBusses(void* ptr) { return 1; }
uint32_t cplug_getNumOutputBusses(void* ptr) { return 1; }
uint32_t cplug_getInputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }
uint32_t cplug_getOutputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }

void cplug_getInputBusName(void*, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Input"); }
void cplug_getOutputBusName(void*, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Output"); }

uint32_t cplug_getLatencyInSamples(void* p) { return 0; }
uint32_t cplug_getTailInSamples(void* p) { return 0; }

#pragma mark -Params

uint32_t cplug_getNumParameters(void*) { return NUM_PARAMS; }
uint32_t cplug_getParameterID(void* p, uint32_t paramIndex) { return paramIndex; }
uint32_t cplug_getParameterFlags(void* p, uint32_t paramId) { return CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE; }

// NOTE: AUv2 supports a max length of 52 bytes, VST3 128, CLAP 256
void cplug_getParameterName(void*, uint32_t paramId, char* buf, size_t buflen)
{
    const char*        str     = "";
    static const char* NAMES[] = {"LP Freq", "LP Res", "HP Freq", "HP Res", "Feedback Gain"};
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
    case PARAM_LP_CUTOFF:
        v = 0.5;
        break;
    case PARAM_LP_RESONANCE:
    case PARAM_HP_RESONANCE:
        v = xm_normd(XM_SQRT1_2f, 0.1, 20);
        break;
    case PARAM_FEEDBACK_GAIN:
    case PARAM_HP_CUTOFF:
    default:
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
    if (g_is_main_thread)
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
    switch (paramId)
    {
    case PARAM_LP_CUTOFF:
    case PARAM_HP_CUTOFF:
    {
        scanf(str, "%fHz", &val);
        val = xm_fast_normalise_Hz1(val);
        break;
    }
    case PARAM_LP_RESONANCE:
    case PARAM_HP_RESONANCE:
    {
        scanf(str, "%f", &val);
        val = xm_normd(val, 0.1, 20);
        break;
    }
    case PARAM_FEEDBACK_GAIN:
    {
        scanf(str, "%lf%%", &val);
        val *= 0.01;
        break;
    }
    default:
        scanf(str, "%f", &val);
        break;
    }
    return val;
}

void cplug_parameterValueToString(void*, uint32_t paramId, char* buf, size_t bufsize, double value)
{
    switch (paramId)
    {
    case PARAM_LP_CUTOFF:
    case PARAM_HP_CUTOFF:
    {
        float Hz = xm_fast_denomalise_Hz(value);
        snprintf(buf, bufsize, "%.2fHz", Hz);
        break;
    }
    case PARAM_LP_RESONANCE:
    case PARAM_HP_RESONANCE:
    {
        float Q = xm_lerpf(value, 0.1, 20);
        snprintf(buf, bufsize, "%.3f", Q);
        break;
    }
    case PARAM_FEEDBACK_GAIN:
        snprintf(buf, bufsize, "%.2f%%", value * 100);
        break;
    default:
        snprintf(buf, bufsize, "%f", value);
        break;
    }
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

#pragma mark -Audio

void cplug_setSampleRateAndBlockSize(void* _p, double sampleRate, uint32_t maxBlockSize)
{
    Plugin* p         = _p;
    p->sample_rate    = sampleRate;
    p->max_block_size = maxBlockSize;

    memset(&p->state, 0, sizeof(p->state));
}

#ifdef CPLUG_BUILD_STANDALONE
// static char note_down_midi = -1;
char  osc_midi  = 28; // E1
float osc_phase = 0;
#endif

void cplug_process(void* _p, CplugProcessContext* ctx)
{
    DISABLE_DENORMALS
    Plugin* p                     = _p;
    bool    should_post_to_global = false;
    bool    panic_btn_pressed     = false;

    // Dequeue events sent from main thread
    {
        // Audio thread has chance to respond to incoming GUI events before being sent to the host
        int head = cplug_atomic_load_i32(&p->queue_audio_head) & EVENT_QUEUE_MASK;
        int tail = cplug_atomic_load_i32(&p->queue_audio_tail);

        while (tail != head)
        {
            CplugEvent event = p->queue_audio_events[tail];

            switch (event.type)
            {
            case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            case EVENT_SET_PARAMETER:
            case EVENT_SET_PARAMETER_NOTIFYING_HOST:
            {
                audio_set_param(p, event.parameter.id, event.parameter.value);
                // println("Dequeue audio (%u) - %u %f", tail, event.type, event.parameter.value);

                if (event.type == CPLUG_EVENT_PARAM_CHANGE_UPDATE)
                {
                    bool ok              = true;
                    event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
                    ok                   = ok && ctx->enqueueEvent(ctx, &event, 0);
                    if (!ok)
                    {
                        println(
                            "[AUDIO] Failed to notify host of parameter change. Context: Event (%u)",
                            CPLUG_EVENT_PARAM_CHANGE_UPDATE);
                    }
                }
                else if (event.type == EVENT_SET_PARAMETER_NOTIFYING_HOST)
                {
                    bool ok              = true;
                    event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_BEGIN;
                    ok                   = ok && ctx->enqueueEvent(ctx, &event, 0);
                    event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
                    ok                   = ok && ctx->enqueueEvent(ctx, &event, 0);
                    event.parameter.type = CPLUG_EVENT_PARAM_CHANGE_END;
                    ok                   = ok && ctx->enqueueEvent(ctx, &event, 0);
                    if (!ok)
                    {
                        println(
                            "[AUDIO] Failed to notify host of parameter change. Context: Event: (%u)",
                            EVENT_SET_PARAMETER_NOTIFYING_HOST);
                    }
                }
                break;
            }
            case CPLUG_EVENT_PARAM_CHANGE_BEGIN:
            case CPLUG_EVENT_PARAM_CHANGE_END:
            {
                bool ok = ctx->enqueueEvent(ctx, &event, 0);
                CPLUG_LOG_ASSERT(ok);
                break;
            }
            case EVENT_PANIC_BUTTON_PRESSED:
                panic_btn_pressed = true;
                break;
            }

            tail++;
            tail &= EVENT_QUEUE_MASK;
        }
        cplug_atomic_exchange_i32(&p->queue_audio_tail, tail);
    }

    float** output = ctx->getAudioOutput(ctx, 0);
    CPLUG_LOG_ASSERT(output != NULL);
    CPLUG_LOG_ASSERT(output[0] != NULL);
    CPLUG_LOG_ASSERT(output[1] != NULL);

    // Force "in place processing"
    {
        float** input = ctx->getAudioInput(ctx, 0);
        if (input && input[0] != output[0])
        {
            memcpy(output[0], input[0], sizeof(float) * ctx->numFrames);
            memcpy(output[1], input[1], sizeof(float) * ctx->numFrames);
        }
    }

    const float fs_inv      = 1.0f / p->sample_rate;
    bool        is_clipping = false;
    float       peak_gain   = 0;

    CplugEvent event;
    uint32_t   frame = 0;

#ifdef CPLUG_BUILD_STANDALONE
    float phase = osc_phase;
#endif

    while (ctx->dequeueEvent(ctx, &event, frame))
    {
        switch (event.type)
        {
        case CPLUG_EVENT_UNHANDLED_EVENT:
            break;
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            send_to_main_event_queue(p, event);
            audio_set_param(p, event.parameter.id, event.parameter.value);
            should_post_to_global = true;
            break;

#ifdef CPLUG_BUILD_STANDALONE

        case CPLUG_EVENT_MIDI:
        {
            if (event.midi.status == 144)
                osc_midi = event.midi.data1;
            else if (event.midi.status == 128 && event.midi.data1 == osc_midi)
                osc_midi = -1;
            break;
        }

#endif

        case CPLUG_EVENT_PROCESS_AUDIO:
        {

            int num_frames = event.processAudio.endFrame - frame;

#ifdef CPLUG_BUILD_STANDALONE
            // Saw wave oscillator for testing
            if (osc_midi != -1)
            {
                float       freq = xm_midi_to_Hz((float)osc_midi);
                const float inc  = freq * fs_inv; // ~A1
                for (int i = frame; i < event.processAudio.endFrame; i++)
                {
                    float saw_wave = -1 + phase * 2;
                    // saw_wave       *= 0.25; // volume

                    output[0][i] = saw_wave;

                    phase += inc;
                    if (phase >= 1)
                        phase -= 1;
                }
                memcpy(output[1] + frame, output[0] + frame, sizeof(float) * num_frames);
            }
            else
            {
                memset(output[0] + frame, 0, sizeof(float) * num_frames);
                memset(output[1] + frame, 0, sizeof(float) * num_frames);
            }
#endif

            // Setup params
            float lp_cutoff = p->audio_params[PARAM_LP_CUTOFF];
            float lp_Q      = p->audio_params[PARAM_LP_RESONANCE];
            float hp_cutoff = p->audio_params[PARAM_HP_CUTOFF];
            float hp_Q      = p->audio_params[PARAM_HP_RESONANCE];
            float drive     = p->audio_params[PARAM_FEEDBACK_GAIN];

            lp_cutoff = xm_fast_denomalise_Hz(lp_cutoff);
            hp_cutoff = xm_fast_denomalise_Hz(hp_cutoff);
            lp_Q      = xm_lerpf(lp_Q, 0.1, 20);
            hp_Q      = xm_lerpf(hp_Q, 0.1, 20);

            // const float ratio     = powf(100, drive);
            // const float inv_ratio = 1 / ratio;
            float feedback_gain = xm_lerpf(drive, -18, 24);
            feedback_gain       = xm_fast_dB_to_gain(feedback_gain);

            // Process
            Coeffs lp_c = filter_LP(lp_cutoff, lp_Q, fs_inv);
            Coeffs hp_c = filter_HP(hp_cutoff, hp_Q, fs_inv);

            float release         = 0.005; // 5 ms
            float attack_samples  = 1;
            float release_samples = p->sample_rate * release;

            float atk = convert_compressor_time(attack_samples);
            float rel = convert_compressor_time(release_samples);

            for (int ch = 0; ch < 2; ch++)
            {
                float*             it  = output[ch] + frame;
                const float* const end = output[ch] + event.processAudio.endFrame;
                struct FilterState s   = p->state[ch];
                for (; it != end; it++)
                {
                    const float x = *it;

                    // Feedforward
                    float y = tanhf(x + s.fb_yn_1);
                    y       = filter_process(y, &lp_c, s.lp);

                    CPLUG_LOG_ASSERT(y == y);
                    if (y != y) // NaN protection. TODO: remove when filter algo is solid
                        y = 0;
                    // Hard clip protection. Remove later
                    if (y < -1)
                    {
                        is_clipping = true;
                        if (fabsf(y) > peak_gain)
                            peak_gain = fabsf(y);
                        y = -1;
                    }
                    if (y > 1)
                    {
                        y = 1;
                        if (y > peak_gain)
                            peak_gain = y;
                        is_clipping = true;
                    }
                    *it = y;

                    // Feedback
                    float feed = filter_process(y, &hp_c, s.hp);
                    feed       = tanhf(feed * feedback_gain);

                    // Feedback gate, triggered by input
                    s.peak_xn_1        = detect_peak(fabsf(x), s.peak_xn_1, atk, rel);
                    float peak_dB      = xm_fast_gain_to_dB(s.peak_xn_1);
                    float reduction_dB = hard_knee_expander(peak_dB, -60.0f, 10);
                    xassert(reduction_dB == reduction_dB);
                    reduction_dB         -= peak_dB;
                    float reduction_gain  = xm_fast_dB_to_gain(reduction_dB);
                    if (reduction_dB < -140) // The above xm_fast_dB_to_gain function uses approximations that become
                                             // unstable is dB is too low. This protects against INF
                        reduction_gain = 0;
                    feed *= reduction_gain;
                    xassert(feed == feed);

                    s.fb_yn_1 = feed;
                }
#define ROUND_STATE_TO_ZERO(n)                                                                                         \
    if (fabsf(n) < 1.0e-8f)                                                                                            \
        n = 0;

                ROUND_STATE_TO_ZERO(s.lp[0])
                ROUND_STATE_TO_ZERO(s.lp[1])
                ROUND_STATE_TO_ZERO(s.hp[0])
                ROUND_STATE_TO_ZERO(s.hp[1])
                ROUND_STATE_TO_ZERO(s.fb_yn_1)

                p->state[ch] = s;
            }

            frame = event.processAudio.endFrame;
            break;
        }
        default:
            println("[WARNING] Unhandled audio event: %u", event.type);
            break;
        }
    }

#ifndef NDEBUG
    if (p->gui)
    {
        extern void oscilloscope_push(float* const* buf, int buflen, int nchannels);
        oscilloscope_push(output, ctx->numFrames, 2);
    }
#endif
#ifdef CPLUG_BUILD_STANDALONE
    xt_atomic_store_f32(&p->gui_osc_midi, osc_midi);
    xt_atomic_store_f32(&p->gui_osc_phase, phase);
    osc_phase = phase;
#endif

    if (panic_btn_pressed)
    {
        // Self oscillation is caused by existing feedback & filter state
        memset(&p->state, 0, sizeof(p->state));

        // Smooth audio fade out. Linear dB
        const float target_dB   = -100;
        const float dB_inc      = target_dB / ctx->numFrames;
        const float scalar_gain = xm_fast_dB_to_gain(dB_inc);

        for (int ch = 0; ch < 2; ch++)
        {
            float              gain = scalar_gain;
            float*             it   = output[ch];
            const float* const end  = output[ch] + ctx->numFrames;
            for (; it != end; it++)
            {
                *it  *= gain;
                gain *= gain;
            }
        }
    }

    p->gui_is_clipping = is_clipping;
    p->gui_peak_gain   = peak_gain;
    RESTORE_DENORMALS

    if (should_post_to_global && p->cplug_ctx->type != CPLUG_PLUGIN_IS_STANDALONE)
    {
        send_to_global_event_queue(GLOBAL_EVENT_DEQUEUE_MAIN, p);
    }
}

#pragma mark -State

typedef struct PluginStatev0_0_1
{
    union
    {
        struct
        {
            uint8_t tweak;
            uint8_t patch;
            uint8_t minor;
            uint8_t major;
        };
        uint32_t number;
    } version;
    double params[NUM_PARAMS];
} PluginState;

void cplug_saveState(void* _p, const void* stateCtx, cplug_writeProc writeProc)
{
    Plugin* p = _p;

    PluginState state   = {0};
    state.version.major = 0;
    state.version.minor = 0;
    state.version.patch = 1;
    state.version.tweak = 0;

    _Static_assert(sizeof(state.params) == sizeof(p->main_params), "Must match");
    memcpy(state.params, p->main_params, sizeof(p->main_params));

    writeProc(stateCtx, &state, sizeof(state));
}

void cplug_loadState(void* _p, const void* stateCtx, cplug_readProc readProc)
{
    Plugin*     p     = _p;
    PluginState state = {0};

    readProc(stateCtx, &state, sizeof(state));

    _Static_assert(sizeof(state.params) == sizeof(p->main_params), "Must match");
    memcpy(p->main_params, state.params, sizeof(p->main_params));
    memcpy(p->audio_params, state.params, sizeof(p->main_params));

    for (int i = 0; i < ARRLEN(state.params); i++)
    {
        double v = state.params[i];
        if (v < 0)
            v = 0;
        if (v > 1)
            v = 1;
        if (v != p->main_params[i] || v != p->audio_params[i])
        {
            p->main_params[i]  = v;
            p->audio_params[i] = v;
            param_change_begin(p, i);
            param_change_update(p, i, v);
            param_change_end(p, i);
        }
    }
}
