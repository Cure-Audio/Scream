#pragma once
#include <xhl/debug.h>
#include <xhl/maths.h>

typedef enum ADSRStage
{
    ADSR_IDLE,
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE,
    ADSR_NUM_STAGES,
} ADSRStage;

#define ADSR_MIN_LEVEL 0.0001

typedef struct ADSR
{
    ADSRStage current_stage;
    float     current_level;
    float     multiplier;
    uint32_t  current_idx;
    uint32_t  next_stage_idx;

    uint32_t attack_length_samples;
    uint32_t decay_length_samples;
    uint32_t release_length_samples;
    float    sustain;
} ADSR;

// https://www.martin-finke.de/articles/audio-plugins-011-envelopes/
static void adsr_set_params(ADSR* env, float attack, float decay, float sustain, float release, float sample_rate)
{
    xassert(sample_rate > 0);
    env->attack_length_samples  = attack * sample_rate;
    env->decay_length_samples   = decay * sample_rate;
    env->release_length_samples = release * sample_rate;
    env->sustain                = sustain;
}

static void adsr_calc_multiplier(ADSR* env, float startLevel, float endLevel, uint32_t lengthInSamples)
{
    float v = 0;
    if (lengthInSamples)
        v = 1.0f + (xm_fastlog(endLevel) - xm_fastlog(startLevel)) / (float)(lengthInSamples);
    xassert(v == v);
    env->multiplier = v;
}

static void adsr_set_stage(ADSR* env, ADSRStage new_stage)
{
    env->current_stage = new_stage;
    env->current_idx   = 0;

    switch (new_stage)
    {
    case ADSR_IDLE:
        env->current_level = 0.0f;
        env->multiplier    = 1.0f;
        break;
    case ADSR_ATTACK:
        env->next_stage_idx = env->attack_length_samples;
        if (ADSR_MIN_LEVEL > env->current_level) // Legato voice steal
            env->current_level = ADSR_MIN_LEVEL;
        adsr_calc_multiplier(env, env->current_level, 1.0f, env->next_stage_idx);
        break;
    case ADSR_DECAY:
        env->next_stage_idx = env->decay_length_samples;
        env->current_level  = 1.0f;
        adsr_calc_multiplier(env, env->current_level, xm_maxf(env->sustain, ADSR_MIN_LEVEL), env->next_stage_idx);
        break;
    case ADSR_SUSTAIN:
        env->current_level = env->sustain;
        env->multiplier    = 1.0f;
        break;
    case ADSR_RELEASE:
        env->next_stage_idx = env->release_length_samples;
        // We could go from ATTACK/DECAY to RELEASE,
        // so we're not changing current_level here.
        adsr_calc_multiplier(env, env->current_level, ADSR_MIN_LEVEL, env->next_stage_idx);
        break;
    default:
        break;
    }
}

static float adsr_tick(struct ADSR* env)
{
    if (env->current_stage != ADSR_IDLE && env->current_stage != ADSR_SUSTAIN)
    {
        if (env->current_idx == env->next_stage_idx)
        {
            ADSRStage new_stage = (env->current_stage + 1);
            if (new_stage >= ADSR_NUM_STAGES)
                new_stage = ADSR_IDLE;
            adsr_set_stage(env, new_stage);
        }
        env->current_level *= env->multiplier;
        env->current_idx++;
    }
    // xassert(env->current_level >= 0 && env->current_level <= 1);
    return xm_clampf(env->current_level, 0, 1);
}
