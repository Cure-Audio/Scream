#include "common.h"

#include "dsp.h"
#include "plugin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <xhl/maths.h>
#include <xhl/time.h>

#include "params_and_events.c"
#include "state.c"

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

void cplug_libraryLoad()
{
    xtime_init();
    xalloc_init();
}
void cplug_libraryUnload() { xalloc_shutdown(); }

extern void library_load_platform();
extern void library_unload_platform();

void* cplug_createPlugin(CplugHostContext* ctx)
{
    g_is_main_thread = true;
    library_load_platform();

    struct Plugin* p = MY_CALLOC(1, sizeof(*p));
    p->cplug_ctx     = ctx;

    p->width  = GUI_INIT_WIDTH;
    p->height = GUI_INIT_HEIGHT * 2;

    p->lfo_section_open = true;

    for (int i = 0; i < ARRLEN(p->main_params); i++)
        p->main_params[i] = cplug_getDefaultParameterValue(p, i);
    memcpy(p->audio_params, p->main_params, sizeof(p->main_params));
    _Static_assert(sizeof(p->main_params) == sizeof(p->audio_params));

    for (int i = 0; i < ARRLEN(p->lfos); i++)
    {
        LFO* lfo = p->lfos + i;
        for (int j = 0; j < ARRLEN(lfo->points); j++)
        {
            xarr_setcap(lfo->points[j], (2 * MAX_PATTERN_LENGTH_PATTERNS));

            lfo->pattern_length[j] = 4;

            for (int k = 0; k < lfo->pattern_length[j]; k++)
            {
                float    x1  = k;
                float    x2  = x1 + 0.5;
                LFOPoint pt1 = {x1, 0, 0.5};
                LFOPoint pt2 = {x2, 1, 0.5};
                xarr_push(lfo->points[j], pt1);
                xarr_push(lfo->points[j], pt2);
            }
        }
    }

    return p;
}

void cplug_destroyPlugin(void* _p)
{
    CPLUG_LOG_ASSERT(_p != NULL);

    Plugin* p = _p;
    for (int i = 0; i < ARRLEN(p->lfos); i++)
    {
        LFO* lfo = p->lfos + i;
        for (int j = 0; j < ARRLEN(lfo->points); j++)
        {
            xarr_free(lfo->points[j]);
        }
    }

    library_unload_platform();

    MY_FREE(p);
}

uint32_t cplug_getNumInputBusses(void* ptr) { return 1; }
uint32_t cplug_getNumOutputBusses(void* ptr) { return 1; }
uint32_t cplug_getInputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }
uint32_t cplug_getOutputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }

void cplug_getInputBusName(void*, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Input"); }
void cplug_getOutputBusName(void*, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Output"); }

uint32_t cplug_getLatencyInSamples(void* p) { return 0; }
uint32_t cplug_getTailInSamples(void* p) { return 0; }

#pragma mark -Audio

void cplug_setSampleRateAndBlockSize(void* _p, double sampleRate, uint32_t maxBlockSize)
{
    Plugin* p         = _p;
    p->sample_rate    = sampleRate;
    p->max_block_size = maxBlockSize;

    memset(&p->state, 0, sizeof(p->state));

    for (int ch = 0; ch < ARRLEN(p->state); ch++)
    {
        struct FilterState* s = &p->state[ch];

        for (int i = 0; i < ARRLEN(p->audio_params); i++)
            smoothvalue_reset(&s->values[i], p->audio_params[i]);
    }
}

#ifdef CPLUG_BUILD_STANDALONE
char g_osc_midi = -1;
// char  g_osc_midi  = 28; // E1
float g_osc_phase = 0;

float g_output_gain_dB = 0;
// float g_output_gain_dB = -6;
const float g_attack_ms  = 5;
const float g_release_ms = 5.0;

// float g_lp_Q = XM_SQRT1_2f;
// float g_lp_Q = XM_SQRT2f;
// float g_hp_Q = XM_SQRT1_2f;
// float g_hp_Q = XM_SQRT2f;
#endif

void audio_set_param(Plugin* p, ParamID id, double value);
void cplug_process(void* _p, CplugProcessContext* ctx)
{
    DISABLE_DENORMALS
    Plugin* p                     = _p;
    bool    should_post_to_global = false;

    // Dequeue events sent from main thread
    {
        // Audio thread has chance to respond to incoming GUI events before being sent to the host
        int head = cplug_atomic_load_i32(&p->queue_audio_head) & EVENT_QUEUE_MASK;
        int tail = cplug_atomic_load_i32(&p->queue_audio_tail);

        while (tail != head)
        {
            CplugEvent event = p->queue_audio_events[tail];
            tail++;
            tail &= EVENT_QUEUE_MASK;

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
            }
        }
        cplug_atomic_exchange_i32(&p->queue_audio_tail, tail);
    }

    // NOTE: FL Studio and Ableton may return NULL if your FX slot is bypassed
    float** output = ctx->getAudioOutput(ctx, 0);

    // Force "in place processing"
    {
        float** input = ctx->getAudioInput(ctx, 0);
        if (input && output && input[0] != output[0])
        {
            memcpy(output[0], input[0], sizeof(float) * ctx->numFrames);
            memcpy(output[1], input[1], sizeof(float) * ctx->numFrames);
        }
    }

    CplugEvent event;
    uint32_t   frame = 0;

#ifdef CPLUG_BUILD_STANDALONE
    float phase = g_osc_phase;
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
                g_osc_midi = event.midi.data1;
            else if (event.midi.status == 128 && event.midi.data1 == g_osc_midi)
                g_osc_midi = -1;
            break;
        }

#endif

        case CPLUG_EVENT_PROCESS_AUDIO:
        {
            if (output == NULL)
            {
                frame = event.processAudio.endFrame;
                continue;
            }
            const int num_frames = event.processAudio.endFrame - frame;

            const float fs_inv = 1.0f / p->sample_rate;

#ifdef CPLUG_BUILD_STANDALONE
            // Saw wave oscillator for testing
            if (g_osc_midi != -1)
            {
                float       freq = xm_midi_to_Hz((float)g_osc_midi);
                const float inc  = freq * fs_inv; // ~A1
                for (int i = frame; i < event.processAudio.endFrame; i++)
                {
                    // Shabby attempt at recreating the default Saw wave
                    float v    = 1 - (1 - phase) * (1 - phase);
                    v          = xm_lerpf(0.85, phase, v);
                    float wave = -1 + v * 2;

                    // Tri
                    // float wave = -1 + phase * 4;
                    // if (wave > 1)
                    //     wave = 2 - wave;
                    // if (wave < -1)
                    //     wave = -2 - wave;

                    output[0][i] = wave;

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

            const float expander_attack = convert_compressor_time(1);
            // const float expander_release = convert_compressor_time(p->sample_rate * 0.001 * 0.5); // 5 ms
            const float expander_release = convert_compressor_time(p->sample_rate * 0.001); // 1 ms

            // Frequency of param updates vary. In FL studio (2024), when dragging params in the GUI, FL doesn't do a
            // great job of sending these updates frequently on the audio thread. FL sends these updates much more
            // frequently when you are playing parameter automation. Ableton 12 however does a much better job in all
            // cases, and does a great job with a much shorter smoothing time like 10ms.
            // It may be possible to improve this with a little bit of DAW detection to optimise smoothing time based on
            // the DAW.
            const double smoothing_len = 0.030; // 30ms
            // const double smoothing_len        = 0.020; // 20ms NOTE: sounds too steppy in FL Studio
            int param_smoothing_time = xm_droundi(p->sample_rate * smoothing_len);

            for (int ch = 0; ch < 2; ch++)
            {
                float*             it  = output[ch] + frame;
                const float* const end = output[ch] + event.processAudio.endFrame;
                struct FilterState s   = p->state[ch];

                for (int i = 0; i < ARRLEN(p->audio_params); i++)
                    smoothvalue_set_target(&s.values[i], p->audio_params[i], param_smoothing_time);

                float lp_cutoff = s.values[PARAM_CUTOFF].current;
                float hp_cutoff = s.values[PARAM_SCREAM].current;
                float resonance = s.values[PARAM_RESONANCE].current;
                float in_gain   = s.values[PARAM_INPUT_GAIN].current;
                float wet       = s.values[PARAM_WET].current;

// #define CUTOFF_MAX    MIDI_NOTE_NUM_20kHz
#define CUTOFF_MAX (MIDI_NOTE_NUM_20kHz)
// #define HP_CUTOFF_MIN 0
#define HP_CUTOFF_MIN (MIDI_NOTE_NUM_20Hz - 12)
#define LP_Q_MIN      XM_SQRT1_2f
#define LP_Q_MAX      XM_SQRT1_2f
// #define LP_Q_MAX      XM_SQRT2f
#define HP_Q_MIN XM_SQRT1_2f
// #define HP_Q_MAX    (3 * XM_SQRT1_2f)
#define HP_Q_MAX (XM_SQRT2f)
// #define HP_Q_MAX    (XM_SQRT1_2f)
#define FB_GAIN_MIN -12.0f
#define FB_GAIN_MAX 12.0f

                float lp_Q = xm_lerpf(resonance, LP_Q_MIN, LP_Q_MAX);
                float hp_Q = xm_lerpf(resonance, HP_Q_MIN, HP_Q_MAX);

                lp_cutoff  = xm_lerpf(lp_cutoff, MIDI_NOTE_NUM_20Hz, CUTOFF_MAX);
                hp_cutoff  = xm_lerpf(hp_cutoff, HP_CUTOFF_MIN, CUTOFF_MAX);
                hp_cutoff -= CUTOFF_MAX - lp_cutoff;

                lp_cutoff = xm_midi_to_Hz(lp_cutoff);
                hp_cutoff = xm_midi_to_Hz(hp_cutoff);
                lp_cutoff = xm_clampf(lp_cutoff, 5, 20000);
                hp_cutoff = xm_clampf(hp_cutoff, 5, 20000);

                float feedback_gain = xm_lerpf(resonance, FB_GAIN_MIN, FB_GAIN_MAX);
                feedback_gain       = xm_fast_dB_to_gain(feedback_gain);

                in_gain = xm_lerpf(in_gain, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                in_gain = xm_fast_dB_to_gain(in_gain);

                Coeffs lp_c = filter_LP(lp_cutoff, lp_Q, fs_inv);
                Coeffs hp_c = filter_HP(hp_cutoff, hp_Q, fs_inv);

                const bool smooth_params = s.values[PARAM_CUTOFF].remaining | s.values[PARAM_SCREAM].remaining |
                                           s.values[PARAM_RESONANCE].remaining | s.values[PARAM_INPUT_GAIN].remaining |
                                           s.values[PARAM_WET].remaining;

                if (p->gui)
                {
                    enum
                    {
                        PEAK_FREQUENCY_SAMPLES = 1 << 12,
                        PEAK_FREQUENCY_MASK    = PEAK_FREQUENCY_SAMPLES - 1,
                    };
                    int remaining_samples = num_frames;
                    while (remaining_samples)
                    {
                        float peak_input = p->_gui_input_last_peak.data[ch];
                        int   N = xm_mini(remaining_samples, PEAK_FREQUENCY_SAMPLES - p->_gui_input_read_count[ch]);
                        for (int i = 0; i < N; i++)
                        {
                            float x = fabsf(it[i]);
                            if (x > peak_input)
                                peak_input = x;
                        }
                        remaining_samples            -= N;
                        p->_gui_input_read_count[ch] += N;
                        if (p->_gui_input_read_count[ch] == PEAK_FREQUENCY_SAMPLES)
                        {
                            p->_gui_input_read_count[ch] = 0;
                            xvec2f next                  = {.u64 = p->gui_input_peak_gain};
                            next.data[ch]                = peak_input * in_gain;

                            xt_atomic_store_u64(&p->gui_input_peak_gain, next.u64);

                            p->_gui_input_last_peak.data[ch] = 0;
                        }
                    }
                }

                // Process
                for (; it != end; it++)
                {
                    float x = *it;

                    x *= in_gain;

                    // Feedforward
                    // float y = x + s.fb_yn_1 * feedback_gain;
                    // float y = x + s.fb_yn_1;
                    float y = x + s.fb_yn_1;

                    // y = tanhf(y);
                    y = Tanh_ADAA2_process(&s.tanh_1, y);
                    // y = sinarctan2(y);
                    // y = softsine(y);
                    // y = sinarctan(y);
                    // y = xm_clampf(y, -1, 1);
                    y = filter_process(y, &lp_c, s.lp);

                    CPLUG_LOG_ASSERT(y == y);
                    if (y != y) // NaN protection. TODO: remove when filter algo is solid
                        y = 0;

                    // *it = xm_clampf(y, -1, 1);
                    *it = wet * y + (1 - wet) * (*it);
                    // *it = x;

#ifdef CPLUG_BUILD_STANDALONE
                    *it *= xm_fast_dB_to_gain(g_output_gain_dB);
#endif

                    // Feedback
                    // float feed = y;
                    float feed = y * feedback_gain;

                    // feed = sinarctan(feed);
                    feed = filter_process(feed, &hp_c, s.hp);
                    // feed = xm_clampf(feed, -1, 1);
                    // feed = tanhf(feed);
                    feed = Tanh_ADAA2_process(&s.tanh_2, feed);
                    // feed = softsine(feed);
                    // feed = softsine2(feed);

                    // Feedback gate, triggered by input
                    s.peak_xn_1        = detect_peak(fabsf(x), s.peak_xn_1, expander_attack, expander_release);
                    float peak_dB      = xm_fast_gain_to_dB(s.peak_xn_1);
                    float reduction_dB = hard_knee_expander(peak_dB, -120.0f, 2);
                    xassert(reduction_dB == reduction_dB);
                    reduction_dB         -= peak_dB;
                    float reduction_gain  = xm_fast_dB_to_gain(reduction_dB);
                    // Clamp to 0
                    if (reduction_dB < -140)
                        reduction_gain = 0;
                    feed *= reduction_gain;
                    xassert(feed == feed);

                    s.fb_yn_1 = feed;

                    if (smooth_params)
                    {
                        for (int i = 0; i < ARRLEN(s.values); i++)
                            smoothvalue_tick(&s.values[i]);

                        lp_cutoff = s.values[PARAM_CUTOFF].current;
                        hp_cutoff = s.values[PARAM_SCREAM].current;
                        resonance = s.values[PARAM_RESONANCE].current;
                        in_gain   = s.values[PARAM_INPUT_GAIN].current;
                        wet       = s.values[PARAM_WET].current;

                        lp_Q = xm_lerpf(resonance, LP_Q_MIN, LP_Q_MAX);
                        hp_Q = xm_lerpf(resonance, HP_Q_MIN, HP_Q_MAX);

                        lp_cutoff  = xm_lerpf(lp_cutoff, MIDI_NOTE_NUM_20Hz, CUTOFF_MAX);
                        hp_cutoff  = xm_lerpf(hp_cutoff, HP_CUTOFF_MIN, CUTOFF_MAX);
                        hp_cutoff -= CUTOFF_MAX - lp_cutoff;

                        lp_cutoff = xm_midi_to_Hz(lp_cutoff);
                        hp_cutoff = xm_midi_to_Hz(hp_cutoff);
                        lp_cutoff = xm_clampf(lp_cutoff, 20, 20000);
                        hp_cutoff = xm_clampf(hp_cutoff, 20, 20000);

                        feedback_gain = xm_lerpf(resonance, FB_GAIN_MIN, FB_GAIN_MAX);
                        feedback_gain = xm_fast_dB_to_gain(feedback_gain);

                        in_gain = xm_lerpf(in_gain, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                        in_gain = xm_fast_dB_to_gain(in_gain);

                        lp_c = filter_LP(lp_cutoff, lp_Q, fs_inv);
                        hp_c = filter_HP(hp_cutoff, hp_Q, fs_inv);
                    }
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

#ifdef CPLUG_BUILD_STANDALONE
    // if (p->gui)
    // {
    //     extern void oscilloscope_push(float* const* buf, int buflen, int nchannels);
    //     oscilloscope_push(output, ctx->numFrames, 2);
    // }
    xt_atomic_store_f32(&p->gui_osc_midi, g_osc_midi);
    xt_atomic_store_f32(&p->gui_osc_phase, phase);
    g_osc_phase = phase;
#endif

    RESTORE_DENORMALS

    if (should_post_to_global && p->cplug_ctx->type != CPLUG_PLUGIN_IS_STANDALONE)
    {
        send_to_global_event_queue(GLOBAL_EVENT_DEQUEUE_MAIN, p);
    }
}
