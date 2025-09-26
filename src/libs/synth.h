#pragma once
#include <math.h>
#include <pffft.h>
#include <stdbool.h>
#include <string.h>
#include <xhl/maths.h>

#include "../dsp.h"
#include "adsr.h"
#include "linked_arena.h"

#define SYNTH_MIDI_20Hz  15.486820576352429f
#define SYNTH_MIDI_20kHz 135.0762319922975f

enum SynthParamIds
{
    kEnvAttack,
    kEnvDecay,
    kEnvSustain,
    kEnvRelease,

    kUnisonVoices,
    kUnisonDetune,

    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,
    kFilterEnvAttack,
    kFilterEnvDecay,

    kNumSynthParams,
};
typedef float SynthParams[kNumSynthParams];

enum
{
    UNISON_VOICES_MAX = 16,
    WT_LEN            = 2048,
    WT_MASK           = 2047,
};

typedef struct
{
    float time_domain[WT_LEN];
    float freq_domain[WT_LEN];
} Wavetable;

static void wavetable_bandlimit(Wavetable* wt, PFFFT_Setup* fft, float* dst, float Hz, float sample_rate)
{
    const float    bandlimit_bin_idx_f = xm_clampf(sample_rate / Hz + 1, 1, WT_LEN);
    const unsigned bandlimit_bin_idx   = (unsigned)bandlimit_bin_idx_f >> 1;

    {
        float wt_freq_domain[2048];
        memcpy(wt_freq_domain, wt->freq_domain, bandlimit_bin_idx * 2 * sizeof(float));
        memset(wt_freq_domain + bandlimit_bin_idx * 2, 0, (WT_LEN - bandlimit_bin_idx * 2) * sizeof(float));
        pffft_transform_ordered(fft, wt_freq_domain, dst, NULL, PFFFT_BACKWARD);

        const float gain = 0.25f / WT_LEN;
        for (int i = 0; i < WT_LEN; i++)
        {
            float v_in  = dst[i];
            float v_out = v_in * gain;
            xassert(v_out >= -1.5 && v_out <= 1.5);
            dst[i] = v_out;
        }
    }
}

static void wavetable_set_saw(Wavetable* wt, PFFFT_Setup* fft)
{
    const float inc = 2.0f / WT_LEN;
    for (int i = 0; i < WT_LEN; i++)
    {
        float sample = -1 + i * inc;
        xassert(sample >= -1 && sample <= 1);
        wt->time_domain[i] = sample;
    }

    pffft_transform_ordered(fft, wt->time_domain, wt->freq_domain, NULL, PFFFT_FORWARD);
}

static void wavetable_set_square(Wavetable* wt, PFFFT_Setup* fft)
{
    for (int i = 0; i < WT_LEN / 2; i++)
    {
        wt->time_domain[i] = -1;
    }
    for (int i = WT_LEN / 2; i < WT_LEN; i++)
    {
        wt->time_domain[i] = 1;
    }

    pffft_transform_ordered(fft, wt->time_domain, wt->freq_domain, NULL, PFFFT_FORWARD);
}

static float wavetable_get_sample(const float* wt, float phase)
{
    float(*data)[2048] = (void*)wt;

    float idxf = phase * WT_LEN;
    int   idx0 = idxf;
    int   idx1 = idx0 + 1;
    float diff = idxf - idx0;

    idx0 &= WT_MASK;
    idx1 &= WT_MASK;
    xassert(idx0 < WT_LEN);
    xassert(idx1 < WT_LEN);

    float v0 = wt[idx0];
    float v1 = wt[idx1];
    xassert(v0 >= -1.5 && v0 <= 1.5);
    xassert(v1 >= -1.5 && v1 <= 1.5);

    float sample = xm_lerpf(diff, v0, v1);
    return sample;
}

typedef struct
{
    ADSR adsr;

    struct
    {
        uint64_t seed;
        int      length;
        float    scale;
        float    phases[UNISON_VOICES_MAX];
        float    increment[UNISON_VOICES_MAX];
    } unison;

    float* wavetable;

    ADSR  filter_env;
    float sample_rate_inv;
    float s_filter[2];
    float midi_cutoff_min;
    float midi_cutoff_max;
    float resonance;
} Voice;

static void voice_init(Voice* voc, LinkedArena* arena)
{
    // Randomise phase
    if (voc->unison.seed == 0)
    {
        voc->unison.seed = (uint64_t)voc;
        voc->unison.seed = xm_xorshift64(voc->unison.seed);
        voc->unison.seed = xm_xorshift64(voc->unison.seed);
        voc->unison.seed = xm_xorshift64(voc->unison.seed);
    }

    xassert(voc->wavetable == NULL);
    voc->wavetable = linked_arena_alloc(arena, sizeof(float) * WT_LEN);
}

static void voice_stop(Voice* voc) { adsr_set_stage(&voc->adsr, ADSR_RELEASE); }

static void
voice_start(Voice* voc, Wavetable* wt, PFFFT_Setup* fft, double sample_rate, float midi_note_num, SynthParams params)
{
    // Tune oscillator
    {
        const float midi_detune = params[kUnisonDetune];
        voc->unison.length      = params[kUnisonVoices];
        voc->unison.scale       = sqrtf(1.0f / voc->unison.length);

        xassert(voc->unison.length > 0 && voc->unison.length <= ARRLEN(voc->unison.phases));
        const float Hz = xm_midi_to_Hz(midi_note_num);

        wavetable_bandlimit(wt, fft, voc->wavetable, Hz, sample_rate);

        // Set unison detune
        const int   channel_unison  = voc->unison.length >> 1;
        const float detune_inc      = midi_detune / (channel_unison + 1);
        const float sample_rate_inv = 1.0f / sample_rate;
        for (int i = 0; i < voc->unison.length; i += 2)
        {
            const int detune_multiplier = i >> 1;

            float offset = midi_detune - detune_multiplier * detune_inc;
            xassert(offset >= 0);

            float midi_l = midi_note_num + offset;
            float midi_r = midi_note_num - offset;

            float Hz_l = xm_midi_to_Hz(midi_l);
            float Hz_r = xm_midi_to_Hz(midi_r);

            voc->unison.increment[i]     = Hz_l * sample_rate_inv;
            voc->unison.increment[i + 1] = Hz_r * sample_rate_inv;
        }
    }

    if (voc->adsr.current_stage == ADSR_IDLE)
    {
        xassert(voc->unison.seed != 0);
        if (voc->unison.length > 1)
        {
            for (int i = 0; i < voc->unison.length; i++)
            {
                voc->unison.seed = xm_xorshift64(voc->unison.seed);
                float phase      = (float)(voc->unison.seed & 63) / 64;

                voc->unison.phases[i] = phase;
            }
        }
        else
        {
            voc->unison.phases[0] = 0;
        }

        memset(voc->s_filter, 0, sizeof(voc->s_filter));
    }

    adsr_set_params(
        &voc->adsr,
        params[kEnvAttack],
        params[kEnvDecay],
        params[kEnvSustain],
        params[kEnvRelease],
        sample_rate);
    adsr_set_stage(&voc->adsr, ADSR_ATTACK);

    voc->sample_rate_inv = 1.0f / sample_rate;

    float midi_cutoff_min = midi_note_num + params[kFilterCutoff];
    float midi_cutoff_max = midi_cutoff_min + params[kFilterEnvAmount];
    voc->midi_cutoff_min  = xm_clampf(midi_cutoff_min, SYNTH_MIDI_20Hz, SYNTH_MIDI_20kHz);
    voc->midi_cutoff_max  = xm_clampf(midi_cutoff_max, SYNTH_MIDI_20Hz, SYNTH_MIDI_20kHz);
    adsr_set_params(
        &voc->filter_env,
        params[kFilterEnvAttack],
        params[kFilterEnvDecay],
        0.0,
        params[kEnvRelease],
        sample_rate);
    adsr_set_stage(&voc->filter_env, ADSR_ATTACK);
    voc->resonance = params[kFilterResonance];
}

static void voice_process(Voice* restrict voc, float* out, int num_samples, const bool filter_on)
{
    if (voc->adsr.current_stage == ADSR_IDLE)
        return;

    xassert(voc->unison.length > 0);
    xassert(voc->wavetable != NULL);
    xassert(voc->sample_rate_inv > 0);
    xassert(voc->midi_cutoff_min > 0);
    xassert(voc->midi_cutoff_max > 0);

    for (int i = 0; i < num_samples; i++)
    {
        float vol = adsr_tick(&voc->adsr);

        float sample = 0;

        for (int j = 0; j < voc->unison.length; j++)
        {
            sample += wavetable_get_sample(voc->wavetable, voc->unison.phases[j]);
        }

        for (int j = 0; j < voc->unison.length; j++)
            voc->unison.phases[j] += voc->unison.increment[j];

        sample *= voc->unison.scale;
        xassert(sample >= -1.5 && sample <= 1.5);

        if (filter_on)
        {
            float filter_env  = adsr_tick(&voc->filter_env);
            float cutoff_midi = xm_lerpf(filter_env, voc->midi_cutoff_min, voc->midi_cutoff_max);
            float cutoff_hz   = xm_midi_to_Hz(cutoff_midi);

            Coeffs c = filter_LP(cutoff_hz, voc->resonance, voc->sample_rate_inv);
            sample   = filter_process(sample, &c, voc->s_filter);
        }
        out[i] += sample * vol;
        xassert(out[i] == out[i]);
    }

    for (int i = 0; i < ARRLEN(voc->unison.phases); i++)
        voc->unison.phases[i] -= (int)voc->unison.phases[i];
}

enum
{
    MAX_POLYPHONY = 16,
};

typedef struct Synth
{
    PFFFT_Setup* fft11_plan;
    Wavetable*   wt_saw;
    Wavetable*   wt_square;

    float params[kNumSynthParams];

    uint64_t filter_on;

    int   midi_note_num[MAX_POLYPHONY]; // set to -1 if not playing
    Voice voices[MAX_POLYPHONY];
} Synth;

static void synth_init(Synth* s, LinkedArena* arena)
{
    s->fft11_plan = pffft_new_setup(2048, PFFFT_REAL);
    s->wt_saw     = linked_arena_alloc(arena, sizeof(*s->wt_saw));
    s->wt_square  = linked_arena_alloc(arena, sizeof(*s->wt_square));
    xassert(((long long)s->wt_saw & 0xf) == 0);
    xassert(((long long)s->wt_square & 0xf) == 0);
    wavetable_set_saw(s->wt_saw, s->fft11_plan);
    wavetable_set_square(s->wt_square, s->fft11_plan);

    s->params[kEnvAttack]  = 0.01;
    s->params[kEnvDecay]   = 0.4;
    s->params[kEnvSustain] = 0.1;
    s->params[kEnvRelease] = 0.25;

    s->params[kUnisonVoices] = 1;
    s->params[kUnisonDetune] = 0.2;

    s->params[kFilterCutoff]    = 12 * 2;
    s->params[kFilterResonance] = 2;
    s->params[kFilterEnvAmount] = 12 * 3;
    s->params[kFilterEnvAttack] = 0.005;
    s->params[kFilterEnvDecay]  = 0.5;

    s->filter_on = true;

    for (int i = 0; i < ARRLEN(s->voices); i++)
    {
        s->midi_note_num[i] = -1;
    }
    for (int i = 0; i < ARRLEN(s->voices); i++)
    {
        voice_init(&s->voices[i], arena);
    }
}

static void synth_deinit(Synth* s) { pffft_destroy_setup(s->fft11_plan); }

static void synth_prepare(Synth* s, double sample_rate)
{
    for (int i = 0; i < ARRLEN(s->voices); i++)
    {
        Voice* voc = s->voices + i;
        memset(&voc->adsr, 0, sizeof(voc->adsr));
    }
}

static int synth_find_voice_index(Synth* s, int midi_note)
{
    for (int i = 0; i < ARRLEN(s->midi_note_num); i++)
    {
        if (s->midi_note_num[i] == midi_note)
            return i;
    }
    return -1;
}

static void synth_note_on(Synth* s, double sample_rate, int midi_note_num)
{
    xassert(midi_note_num >= 0 && midi_note_num < 128);
    // Try to find existing voice playing midi note
    int voice_idx = synth_find_voice_index(s, midi_note_num);

    if (voice_idx == -1)
    {
        // Try to find available voice
        voice_idx = synth_find_voice_index(s, -1);
        // Steal releasing voice
        if (voice_idx == -1)
        {
            for (int i = 0; i < ARRLEN(s->midi_note_num); i++)
            {
                if (s->voices[i].adsr.current_stage == ADSR_RELEASE)
                {
                    voice_idx = i;
                    break;
                }
            }

            // TODO: steal voice from the middle of a chord
            // Alternatively, track the note down order, and steal the earliest note down
            if (voice_idx == -1)
            {
            }
        }
    }

    bool is_idx_valid = voice_idx >= 0 && voice_idx < ARRLEN(s->midi_note_num);
    xassert(is_idx_valid);
    if (!is_idx_valid)
        return; // FAILED!

    s->midi_note_num[voice_idx] = midi_note_num;
    Voice* voc                  = s->voices + voice_idx;

    const bool already_triggered = voc->adsr.current_stage == ADSR_ATTACK && voc->adsr.current_idx == 0;
    if (!already_triggered)
    {
        voice_start(voc, s->wt_saw, s->fft11_plan, sample_rate, midi_note_num, s->params);
    }
}

static void synth_note_off(Synth* s, int midi_note_num)
{
    int voice_idx = synth_find_voice_index(s, midi_note_num);
    // xassert(voice_idx != -1);
    if (voice_idx != -1)
    {
        Voice* voc = s->voices + voice_idx;
        // xassert(voc->adsr.current_stage != ADSR_IDLE);
        // xassert(voc->adsr.current_stage != ADSR_RELEASE);
        if (voc->adsr.current_stage != ADSR_IDLE && voc->adsr.current_stage != ADSR_RELEASE)
        {
            voice_stop(voc);
        }
    }
}

static void synth_process(Synth* s, float* out, int num_samples)
{
    for (int i = 0; i < ARRLEN(s->voices); i++)
    {
        Voice* voc = s->voices + i;
        if (voc->adsr.current_stage != ADSR_IDLE)
        {
            voice_process(voc, out, num_samples, s->filter_on);

            if (voc->adsr.current_stage == ADSR_IDLE)
            {
                s->midi_note_num[i] = -1;
            }
        }
    }
}