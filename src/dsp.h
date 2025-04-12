#pragma once
#include <math.h>
#include <xhl/debug.h>
#include <xhl/maths.h>

// SvfLinearTrapOptimised2
// https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
typedef union Coeffs
{
    struct
    {
        float a1, a2, a3, m0, m1, m2;
    };
    void* _align;
} Coeffs;

static inline float calcG(float fc, float fs_inv /*sampleRateInv*/)
{
    return xm_fasttan_normalised(XM_HALF_PIf * 1.27f * fc * fs_inv);
}

static Coeffs filter_LP(float fc, float Q, float fs_inv)
{
    float g = calcG(fc, fs_inv);
    float k = 1.0f / Q;
    // float k = XM_SQRT2f; // Butterworth

    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    float m0 = 0.0f;
    float m1 = 0.0f;
    float m2 = 1.0f;
    return (Coeffs){a1, a2, a3, m0, m1, m2};
}

static Coeffs filter_HP(float fc, float Q, float fs_inv)
{
    float g = calcG(fc, fs_inv);
    float k = 1.0f / Q;
    // float k  = XM_SQRT2f; // Butterworth
    float a1 = 1 / (1 + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;
    float m0 = 1;
    float m1 = -k;
    float m2 = -1;
    return (Coeffs){a1, a2, a3, m0, m1, m2};
}

static inline float filter_process(float v0 /*xn*/, Coeffs* c, float* s)
{
    float v3 = v0 - s[1];
    float v1 = c->a1 * s[0] + c->a2 * v3;
    float v2 = s[1] + c->a2 * s[0] + c->a3 * v3;
    s[0]     = 2 * v1 - s[0];
    s[1]     = 2 * v2 - s[1];
    return c->m0 * v0 + c->m1 * v1 + c->m2 * v2;
}

// Source: Will Pirkle - Designing Audio Effect Plugins in C++, somewhere in fxobjects.cpp
static double convert_compressor_time(double num_samples)
{
    static const double TLD_AUDIO_ENVELOPE_ANALOG_TC = -0.99967234081320612357829304641019;
    return exp(TLD_AUDIO_ENVELOPE_ANALOG_TC / num_samples);
}

// Giannoulis, Massberg, Reiss compressor
// http://eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf
static inline float hard_knee_expander(float x_dB, float threshold_dB, float ratio)
{
    float y_dB;
    if (x_dB >= threshold_dB)
        y_dB = x_dB;
    else
        y_dB = threshold_dB + ratio * (x_dB - threshold_dB);
    return y_dB;
}

static inline float hard_knee_compress(float x_dB, float threshold_dB, float ratio_inv)
{
    float y_dB, hard_knee;
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;

    y_dB = x_dB > threshold_dB ? hard_knee : x_dB;
    // protect against inf, which may occur in the division above if 'knee_dB' is 0
    xassert(y_dB == y_dB);

    return y_dB;
}

static inline float soft_knee_compress(float x_dB, float threshold_dB, float ratio_inv, float knee_dB)
{
    float y_dB, hard_knee, soft_knee, v, cond;
    v         = x_dB - threshold_dB + knee_dB / 2.0f;
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;
    soft_knee = x_dB + ((ratio_inv - 1.0f) * v * v) / (2 * knee_dB);

    cond = 2 * (x_dB - threshold_dB);
    y_dB = cond <= -knee_dB ? x_dB : cond > knee_dB ? hard_knee : soft_knee;
    // protect against inf, which may occur in the division above if 'knee_dB' is 0
    xassert(y_dB == y_dB);

    return y_dB;
}

static inline float detect_peak(float x, float yn_1, float attack, float release)
{
    float y = 0.0f;

    if (x > yn_1)
        y = attack * yn_1 + (1 - attack) * x;
    else
        y = release * yn_1;

    if (y < 1.0e-6f)
        y = 0;

    return y;
}

static inline float detect_rms(float x, float z1, float attack, float release)
{
    float y = 0.0f;

    if (x > z1)
        y = sqrtf(attack * z1 * z1 + (1 - attack) * x * x);
    else
        y = release * z1;

    if (y < 1.0e-8f)
        y = 0;

    return y;
}

static inline float hard_knee_upwards_compress(float x_dB, float threshold_dB, float ratio_inv)
{
    float y_dB, hard_knee, soft_knee, v, cond;
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;

    y_dB = x_dB > threshold_dB ? x_dB : hard_knee;
    xassert(y_dB == y_dB);

    return y_dB;
}

static inline float soft_knee_upwards_compress(float x_dB, float threshold_dB, float ratio_inv, float knee_dB)
{
    float y_dB, hard_knee, soft_knee, v, cond;
    v         = x_dB - threshold_dB - knee_dB / 2.0f;
    soft_knee = x_dB + (1.0f - ratio_inv) * v * v / (2 * knee_dB);
    hard_knee = threshold_dB + (x_dB - threshold_dB) * ratio_inv;

    cond = 2 * (x_dB - threshold_dB);
    y_dB = cond <= -knee_dB ? hard_knee : cond > knee_dB ? x_dB : soft_knee;
    xassert(y_dB == y_dB);

    return y_dB;
}