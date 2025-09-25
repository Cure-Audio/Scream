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
#include "xhl/array.h"
#include "xhl/debug.h"
#include "xhl/vector.h"

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

extern void audio_set_param(Plugin* p, ParamID id, double value);

void cplug_libraryLoad()
{
    xtime_init();
    xalloc_init();
}
void cplug_libraryUnload()
{
    // Triggers leak detector
    xalloc_shutdown();
}

extern void library_load_platform();
extern void library_unload_platform();

void* cplug_createPlugin(CplugHostContext* ctx)
{
    g_is_main_thread = true;
    library_load_platform();

    struct Plugin* p                   = NULL;
    size_t         expected_max_memory = sizeof(*p);

    // For audio
    // max block size * max sample rate * num LFOs
    expected_max_memory += sizeof(float) * 2048 * 9600 * 2;
    // just to be safe
    expected_max_memory += 1024 * 1024; // 1mb

    LinkedArena* arena = linked_arena_create(expected_max_memory);
    p                  = linked_arena_alloc(arena, sizeof(*p));
    p->audio_arena     = arena;
    p->cplug_ctx       = ctx;

    p->width  = GUI_INIT_WIDTH;
    p->height = GUI_INIT_HEIGHT * 2;

    p->lfo_section_open = true;

    for (int i = 0; i < ARRLEN(p->main_params); i++)
        p->main_params[i] = cplug_getDefaultParameterValue(p, i);
#ifndef NDEBUG
    p->main_params[PARAM_CUTOFF]    = 0.85;
    p->main_params[PARAM_SCREAM]    = 0.465f;
    p->main_params[PARAM_RESONANCE] = 1;
#endif

    memcpy(p->audio_params, p->main_params, sizeof(p->main_params));
    _Static_assert(sizeof(p->main_params) == sizeof(p->audio_params));

    for (int i = 0; i < ARRLEN(p->lfos); i++)
    {
        LFO* lfo = p->lfos + i;
        for (int j = 0; j < ARRLEN(lfo->points); j++)
        {
            xarr_setcap(lfo->points[j], (4 * MAX_PATTERN_LENGTH_PATTERNS));

            lfo->grid_x[j] = 4;
            lfo->grid_y[j] = 4;

            // lfo->pattern_length[j] = 4;
            lfo->pattern_length[j] = 1;

            // for (int k = 0; k < lfo->pattern_length[j]; k++)
            // {
            // float    x1  = k;
            float    x1  = 0;
            float    x2  = x1 + 0.5;
            LFOPoint pt1 = {x1, 0, 0.5};
            LFOPoint pt2 = {x2, 1, 0.5};
            xarr_push(lfo->points[j], pt1);
            xarr_push(lfo->points[j], pt2);
            // }
            // LFOPoint(*points_view)[32] = (void*)(lfo->points[j]);
            // xassert(false);
        }
    }

    p->bpm = 120;

    // TODO: uncomment this
    p->lfo_mod_amounts[PARAM_CUTOFF].left = 0.25f;

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

    linked_arena_destroy(p->audio_arena);
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

        for (int i = 0; i < ARRLEN(s->values); i++)
            smoothvalue_reset(&s->values[i], p->audio_params[i]);
    }
}

#ifdef CPLUG_BUILD_STANDALONE
char g_osc_midi = -1;
// char  g_osc_midi  = 28; // E1
float g_osc_phase = 0;

// float g_output_gain_dB = 0;
// float g_output_gain_dB = -6;
float       g_output_gain_dB = -12;
const float g_attack_ms      = 5;
const float g_release_ms     = 5.0;

// float g_lp_Q = XM_SQRT1_2f;
// float g_lp_Q = XM_SQRT2f;
// float g_hp_Q = XM_SQRT1_2f;
// float g_hp_Q = XM_SQRT2f;
#endif

// clang-format off
const double SYNC_VALUES[] = {
    // LFO_RATE_4_BARS,
    (4.0),
    // LFO_RATE_2_BARS,
    (2.0),
    // LFO_RATE_1_BAR,
    (1.0),
    // LFO_RATE_3_4,
    (3.0 / 4.0),
    // LFO_RATE_2_3,
    (2.0 / 3.0),
    // LFO_RATE_1_2,
    (1.0 / 2.0),
    // LFO_RATE_3_8,
    (3.0 / 8.0),
    // LFO_RATE_1_3,
    (1.0 / 3.0),
    // LFO_RATE_1_4,
    (1.0 / 4.0),
    // LFO_RATE_3_16,
    (3.0 / 16.0),
    // LFO_RATE_1_6,
    (1.0 / 6.0),
    // LFO_RATE_1_8,
    (1.0 / 8.0),
    // LFO_RATE_1_12,
    (1.0 / 12.0),
    // LFO_RATE_1_16,
    (1.0 / 16.0),
    // LFO_RATE_1_24,
    (1.0 / 24.0),
    // LFO_RATE_1_32,
    (1.0 / 32.0),
    // LFO_RATE_1_48,
    (1.0 / 48.0),
    // LFO_RATE_1_64,
    (1.0 / 64.0),
};
_Static_assert(ARRLEN(SYNC_VALUES) == LFO_RATE_COUNT, "");
// clang-format on

void render_lfo(Plugin* p, float* buffer, int num_samples, int lfo_idx)
{
    LINKED_ARENA_LEAK_DETECT_BEGIN(p->audio_arena);
    xassert(lfo_idx >= 0 && lfo_idx < ARRLEN(p->lfos));

    LFO* lfo = p->lfos + lfo_idx;

    // Inspect in debugger
    float(*buffer_view)[512] = (void*)buffer;

    const double pattern_v   = p->audio_params[PARAM_PATTERN_LFO_1 + lfo_idx];
    const int    pattern_idx = xm_droundi(pattern_v * (NUM_LFO_PATTERNS - 1));

    // !!!
    xt_spinlock_lock(&lfo->spinlocks[pattern_idx]);

    const int num_points = xarr_len(lfo->points[pattern_idx]);
    // const double pattern_length = xt_atomic_load_i32((xt_atomic_int32_t*)&lfo->pattern_length[pattern_idx]);
    const double pattern_length = 1;
    LFOPoint*    points         = linked_arena_alloc(p->audio_arena, num_points * sizeof(*points));
    memcpy(points, lfo->points[pattern_idx], num_points * sizeof(*points));

    xt_spinlock_unlock(&lfo->spinlocks[pattern_idx]);

    const LFOPoint* points_start = points;
    const LFOPoint* points_end   = points_start + num_points;

    // double beat_position = fmod(p->beat_position, pattern_length);
    // xassert(beat_position < pattern_length);
    // const double beat_inc = p->beat_inc;

    ParamID       rate_param_idx = PARAM_RATE_LFO_1 + lfo_idx;
    const LFORate lfo_rate_idx   = (int)p->audio_params[rate_param_idx];
    xassert(lfo_rate_idx >= 0 && lfo_rate_idx < ARRLEN(SYNC_VALUES));
    const double rate_hz  = p->bpm / (SYNC_VALUES[lfo_rate_idx] * 240);
    const double beat_inc = rate_hz / p->sample_rate;

#define beat_position lfo->phase

    const LFOPoint* it = points_end;
    while (it-- != points_start)
        if (beat_position >= it->x)
            break;

    LFOPoint pt1 = *it;
    xvec2f   pt2;
    if ((it + 1) == points_end)
    {
        // last point in array (wrap)
        pt2.x = pattern_length;
        pt2.y = points_start->y;
    }
    else
    {
        // Next point
        pt2.x = it[1].x;
        pt2.y = it[1].y;
    }

    for (int i = 0; i < num_samples; i++)
    {
        xassert(beat_position >= pt1.x);
        xassert(beat_position < pt2.x);
        xassert(it >= points_start && it < points_end);

        float rel_position  = (float)beat_position - pt1.x;
        rel_position       /= pt2.x - pt1.x;

        // Calc LFO value
        float v = interp_points(rel_position, pt1.skew, pt1.y, pt2.y);

        buffer[i] = v;

        beat_position += beat_inc;

        if (beat_position >= pt2.x)
        {
            if (beat_position >= pattern_length)
                beat_position -= pattern_length;

            it = points_end;
            while (it-- != points_start)
                if (beat_position >= it->x)
                    break;

            if (it == points_end)
                it = points_start;

            pt1 = *it;
            if ((it + 1) == points_end)
            {
                // last point in array (wrap)
                pt2.x = pattern_length;
                pt2.y = points_start->y;
            }
            else
            {
                // Next point
                pt2.x = it[1].x;
                pt2.y = it[1].y;
            }
        }
    }

#undef beat_position

    linked_arena_release(p->audio_arena, points);
    LINKED_ARENA_LEAK_DETECT_END(p->audio_arena);
}

void cplug_process(void* _p, CplugProcessContext* ctx)
{
    DISABLE_DENORMALS
    Plugin* p                     = _p;
    bool    should_post_to_global = false;

    LINKED_ARENA_LEAK_DETECT_BEGIN(p->audio_arena);

    // Dequeue events sent from main thread
    {
        // Audio thread has chance to respond to incoming GUI events before being sent to the host
        int head = cplug_atomic_load_i32(&p->queue_audio_head) & EVENT_QUEUE_MASK;
        int tail = cplug_atomic_load_i32(&p->queue_audio_tail);

        while (tail != head)
        {
            const CplugEvent* event = &p->queue_audio_events[tail];
            tail++;
            tail &= EVENT_QUEUE_MASK;

            switch (event->type)
            {
            case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            case EVENT_SET_PARAMETER:
            case EVENT_SET_PARAMETER_NOTIFYING_HOST:
            {
                audio_set_param(p, event->parameter.id, event->parameter.value);
                // println("Dequeue audio (%u) - %u %f", tail, event->type, event->parameter.value);

                if (event->type == CPLUG_EVENT_PARAM_CHANGE_UPDATE)
                {
                    bool       ok     = true;
                    CplugEvent e2     = *event;
                    e2.parameter.type = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
                    ok                = ok && ctx->enqueueEvent(ctx, &e2, 0);
                    if (!ok)
                    {
                        println(
                            "[AUDIO] Failed to notify host of parameter change. Context: Event (%u)",
                            CPLUG_EVENT_PARAM_CHANGE_UPDATE);
                    }
                }
                else if (event->type == EVENT_SET_PARAMETER_NOTIFYING_HOST)
                {
                    bool       ok     = true;
                    CplugEvent e2     = *event;
                    e2.parameter.type = CPLUG_EVENT_PARAM_CHANGE_BEGIN;
                    ok                = ok && ctx->enqueueEvent(ctx, &e2, 0);
                    e2.parameter.type = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
                    ok                = ok && ctx->enqueueEvent(ctx, &e2, 0);
                    e2.parameter.type = CPLUG_EVENT_PARAM_CHANGE_END;
                    ok                = ok && ctx->enqueueEvent(ctx, &e2, 0);
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
                bool ok = ctx->enqueueEvent(ctx, event, 0);
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

    if (ctx->flags & CPLUG_FLAG_TRANSPORT_HAS_BPM)
    {
        p->bpm = ctx->bpm;
    }

    const double beats_per_second = p->bpm / 60.0;
    const double samples_per_beat = p->sample_rate / beats_per_second;
    p->beat_inc                   = 1.0 / samples_per_beat;

    const bool has_playhead = ctx->flags & CPLUG_FLAG_TRANSPORT_HAS_PLAYHEAD_BEATS;
    const bool is_playing   = ctx->flags & CPLUG_FLAG_TRANSPORT_IS_PLAYING;
    const bool is_looping   = (bool)(ctx->flags & CPLUG_FLAG_TRANSPORT_IS_LOOPING);

    bool   did_loop            = false;
    double next_playhead_beats = 0;

    if (has_playhead)
        next_playhead_beats = ctx->playheadBeats;

    if (has_playhead && is_playing)
        p->beat_position = fmod(ctx->playheadBeats, MAX_PATTERN_LENGTH_PATTERNS);

    if (is_looping && has_playhead)
        did_loop = next_playhead_beats < p->last_playhead_beats;

    const bool started_playing = p->playhead_was_playing == false && is_playing == true;
    p->playhead_was_playing    = is_playing;
    p->last_playhead_beats     = next_playhead_beats;

    if (p->bpm > 0 && (started_playing || did_loop))
    {
        for (int lfo_idx = 0; lfo_idx < ARRLEN(p->lfos); lfo_idx++)
        {
            ParamID rate_param_id = PARAM_RATE_LFO_1 + lfo_idx;
            LFORate rate_idx      = p->audio_params[rate_param_id];
            xassert(rate_idx >= 0 && rate_idx < ARRLEN(SYNC_VALUES));

            double phase  = p->last_playhead_beats / SYNC_VALUES[rate_idx];
            phase        -= (int)phase;

            p->lfos[lfo_idx].phase = phase;
        }
    }

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
            if (event.midi.status == 144) // Note on
            {
                g_osc_midi       = event.midi.data1;
                p->beat_position = 0; // Retrigger LFO on key press
            }
            else if (event.midi.status == 128 && event.midi.data1 == g_osc_midi) // Note off
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

                    // Simulate low input gain
                    wave *= 0.1f;

                    output[0][i] = wave;

                    phase += inc;
                    if (phase >= 1)
                        phase -= 1;
                }
                // Autogain
                const float attack  = convert_compressor_time(p->sample_rate * 0.001);
                const float release = convert_compressor_time(p->sample_rate * 0.1);

                static float last_peak          = 0;
                float        block_highest_peak = 0;
                for (int i = frame; i < event.processAudio.endFrame; i++)
                {
                    float x   = output[0][i];
                    last_peak = detect_peak(fabsf(x), last_peak, attack, release);

                    block_highest_peak = xm_maxf(block_highest_peak, last_peak);
                }
                static bool          init_autogain = false;
                static SmoothedValue sv_autogain;

                if (init_autogain == false)
                {
                    init_autogain = true;
                    smoothvalue_reset(&sv_autogain, 1);
                }

                float in_peak_dB = xm_fast_gain_to_dB(block_highest_peak);
                if (in_peak_dB < -48)
                    in_peak_dB = -48;
                // We want to bring the peak gain to approx. 0dB
                float autogain_dB            = -in_peak_dB;
                float autogain_gain          = xm_fast_dB_to_gain(autogain_dB);
                int   smoothing_time_samples = xm_droundi(p->sample_rate * 0.02);
                smoothvalue_set_target(&sv_autogain, autogain_gain, smoothing_time_samples);

                float out_peak = 0;
                for (int i = frame; i < event.processAudio.endFrame; i++)
                {
                    float gain    = smoothvalue_tick(&sv_autogain);
                    output[0][i] *= gain;

                    if (output[0][i] > out_peak)
                        out_peak = output[0][i];
                }

                // println("in_peak_dB: %.2fdB out_peak", in_peak_dB, xm_fast_gain_to_dB(out_peak));

                memcpy(output[1] + frame, output[0] + frame, sizeof(float) * num_frames);
            }
            else
            {
                memset(output[0] + frame, 0, sizeof(float) * num_frames);
                memset(output[1] + frame, 0, sizeof(float) * num_frames);
            }
#endif // CPLUG_BUILD_STANDALONE

            // These flags represent active LFO modulations on parameters
            // The index of the set bit corresponds to the parameter idx that is being modulated
            uint8_t lfo_1_mod_flags = 0;
            uint8_t lfo_2_mod_flags = 0;
            for (int i = 0; i < ARRLEN(p->lfo_mod_amounts); i++)
            {
                if (fabsf(p->lfo_mod_amounts[i].data[0]) != 0)
                    lfo_1_mod_flags |= 1 << i;
                if (fabsf(p->lfo_mod_amounts[i].data[1]) != 0)
                    lfo_2_mod_flags |= 1 << i;
            }

            xvec2f last_lfo_amount = {0};
            float* mod_buffer_1    = NULL;
            float* mod_buffer_2    = NULL;
            if (lfo_1_mod_flags)
            {
                mod_buffer_1 = linked_arena_alloc(p->audio_arena, num_frames * sizeof(mod_buffer_1));
                render_lfo(p, mod_buffer_1, num_frames, 0);
                last_lfo_amount.left = mod_buffer_1[num_frames - 1];
            }
            if (lfo_2_mod_flags)
            {
                mod_buffer_2 = linked_arena_alloc(p->audio_arena, num_frames * sizeof(mod_buffer_2));
                render_lfo(p, mod_buffer_2, num_frames, 1);
                last_lfo_amount.right = mod_buffer_2[num_frames - 1];
            }
            p->last_lfo_amount = last_lfo_amount;

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
                float* audio = output[ch] + frame;

                struct FilterState s = p->state[ch];

                for (int i = 0; i < ARRLEN(s.values); i++)
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

                const bool has_modulation_or_smoothing = lfo_1_mod_flags || lfo_2_mod_flags || smooth_params;

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
                            float x = fabsf(audio[i]);
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
                for (int i = 0; i < num_frames; i++)
                {
                    float x = audio[i];

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
                    audio[i] = wet * y + (1 - wet) * audio[i];
                    // *it = x;

#ifdef CPLUG_BUILD_STANDALONE
                    audio[i] *= xm_fast_dB_to_gain(g_output_gain_dB);
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

                    if (has_modulation_or_smoothing)
                    {
                        _Static_assert(NUM_AUTOMATABLE_PARAMS == ARRLEN(s.values), "");
                        _Static_assert(NUM_AUTOMATABLE_PARAMS == ARRLEN(p->lfo_mod_amounts), "");
                        float modvals[NUM_AUTOMATABLE_PARAMS];
                        for (int j = 0; j < ARRLEN(s.values); j++)
                        {
                            smoothvalue_tick(&s.values[j]);

                            float v = s.values[j].current;

                            // TODO: apply bidirectional algorithm?
                            if (lfo_1_mod_flags & (1 << j))
                                v += p->lfo_mod_amounts[j].data[0] * mod_buffer_1[i];
                            if (lfo_2_mod_flags & (1 << j))
                                v += p->lfo_mod_amounts[j].data[1] * mod_buffer_2[i];

                            modvals[j] = xm_clampf(v, 0, 1);
                        }

                        lp_cutoff = modvals[PARAM_CUTOFF];
                        hp_cutoff = modvals[PARAM_SCREAM];
                        resonance = modvals[PARAM_RESONANCE];
                        in_gain   = modvals[PARAM_INPUT_GAIN];
                        wet       = modvals[PARAM_WET];

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

            // Wrap beat position
            p->beat_position += num_frames * p->beat_inc;
            if (p->beat_position >= MAX_PATTERN_LENGTH_PATTERNS)
                p->beat_position -= MAX_PATTERN_LENGTH_PATTERNS;
            xassert(p->beat_position >= 0 && p->beat_position < MAX_PATTERN_LENGTH_PATTERNS);

            if (mod_buffer_2)
                linked_arena_release(p->audio_arena, mod_buffer_2);
            if (mod_buffer_1)
                linked_arena_release(p->audio_arena, mod_buffer_1);

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

    LINKED_ARENA_LEAK_DETECT_END(p->audio_arena);
}
