#pragma once
#include "dsp.h"
#include <math.h>
#include <nanovg.h>

void plot_expander(NVGcontext* nvg, float width, float height)
{
    nvgBeginPath(nvg);
    float y_inc = height / 5.0f;
    float x_inc = width / 5.0f;
    for (int i = 1; i < 5; i++)
    {
        float y = i * y_inc;
        nvgMoveTo(nvg, 0, y);
        nvgLineTo(nvg, width, y);
    }
    for (int i = 1; i <= 5; i++)
    {
        float x = i * x_inc;
        nvgMoveTo(nvg, x, 0);
        nvgLineTo(nvg, x, height);
    }
    nvgStrokeColor(nvg, (NVGcolor){0, 0, 0, 0.2});
    nvgStrokeWidth(nvg, 1.0f);
    nvgStroke(nvg);

    nvgTextAlign(nvg, NVG_ALIGN_TOP | NVG_ALIGN_LEFT);
    nvgFillColor(nvg, (NVGcolor){0, 0, 0, 0.4});
    nvgFontSize(nvg, 12);
    nvgText(nvg, 0, 0, "0dB", NULL);
    nvgTextAlign(nvg, NVG_ALIGN_MIDDLE | NVG_ALIGN_LEFT);
    nvgText(nvg, 0, y_inc, "-12dB", NULL);
    nvgText(nvg, 0, y_inc * 2, "-24dB", NULL);
    nvgText(nvg, 0, y_inc * 3, "-36dB", NULL);
    nvgText(nvg, 0, y_inc * 4, "-48dB", NULL);
    nvgTextAlign(nvg, NVG_ALIGN_BOTTOM | NVG_ALIGN_LEFT);
    nvgText(nvg, 0, height, "-60dB", NULL);

    nvgTextAlign(nvg, NVG_ALIGN_BOTTOM | NVG_ALIGN_CENTER);
    nvgText(nvg, x_inc, height, "-48dB", NULL);
    nvgText(nvg, x_inc * 2, height, "-36dB", NULL);
    nvgText(nvg, x_inc * 3, height, "-24dB", NULL);
    nvgText(nvg, x_inc * 4, height, "-12dB", NULL);
    nvgText(nvg, x_inc * 5, height, "0dB", NULL);

    nvgBeginPath(nvg);
    float y_dB_inc = height / 60.0f;
    float x_dB_inc = width / 60.0f;
    for (int i = 0; i < 60; i++)
    {
        float x_dB = (float)(-60 + i);
        float y_dB = hard_knee_expander(x_dB, -30, 10);

        float x = i * x_dB_inc;
        float y = fabsf(y_dB * y_dB_inc);
        if (i == 0)
            nvgMoveTo(nvg, x, y);
        else
            nvgLineTo(nvg, x, y);
    }
    nvgStroke(nvg);
}

void plot_peak_detection(NVGcontext* nvg, float width, float height)
{
    float sample_rate = width;
    float attack      = 0.002; // 50 ms
    float release     = 0.05;  // 5 ms

    // float attack_samples  = sample_rate * attack;
    float attack_samples  = 1;
    float release_samples = sample_rate * release;

    float atk = convert_compressor_time(attack_samples);
    float rel = convert_compressor_time(release_samples);

    float x      = 0;
    float last_y = 0;
    nvgMoveTo(nvg, 0, height);
    while (x < width)
    {
        float in = x < (width * 0.5) ? 1 : 0;
        last_y   = detect_peak(in, last_y, atk, rel);

        float y = height - last_y * height;
        nvgLineTo(nvg, x, y);

        x += 1.0f;
    }
    nvgStrokeColor(nvg, (NVGcolor){0, 0, 0, 1.0});
    nvgStrokeWidth(nvg, 1.0f);
    nvgStroke(nvg);
}

float distort_compress(float x, float drive)
{
    x = x * (1 + drive * 3);

    float threshold = 0 - drive * 12;
    float knee      = 60 - drive * 50;
    // float knee  = 24 - drive * 18;
    float ratio = 1.4 + drive * 2.6;
    // float ratio = 1.4 + drive * 0.6;
    // float ratio = 1.01;

    float dB   = xm_fast_gain_to_dB(fabsf(x));
    dB         = soft_knee_compress(dB, threshold, 1.0f / ratio, knee);
    float gain = xm_fast_dB_to_gain(dB);
    float y    = copysignf(gain, x);
    return y;
}

float distort(float x, float drive)
{
    // float y = tanhf(x * (1 + drive * 3));
    // float y = x;

    float tanh_y = tanhf(x * (1 + drive * 4));
    float comp_y = distort_compress(x, drive);

    // float mix = fabsf(comp_y);
    float mix = fabsf(x);
    if (mix > 1)
        mix = 1;

    // float y = comp_y * (1 - mix) + tanh_y * mix; // mix wet/dry
    // float y = comp_y * (1 - 0.5 * drive) + tanh_y * drive * 0.5; // mix wet/dry
    float y = comp_y;

    xassert(y == y);
    if (y != y)
        y = 0;
    if (y > 1)
        y = 1;
    if (y < -1)
        y = -1;

    return y;
}

void plot_peak_distortion(NVGcontext* nvg, float x, float y, float width, float height, float drive)
{
    const float half_height = height * 0.5f;
    const float cy          = y + half_height;

    nvgBeginPath(nvg);

    nvgMoveTo(nvg, x + width * 0.5f, y);
    nvgLineTo(nvg, x + width * 0.5f, y + height);
    nvgMoveTo(nvg, x, y + height * 0.5f);
    nvgLineTo(nvg, x + width, y + height * 0.5f);
    nvgStrokeWidth(nvg, 1.2);
    nvgStrokeColor(nvg, nvgRGBAf(0.2, 0.2, 0.3, 1.0f));
    nvgStroke(nvg);

    float       sample_inc = 2 / width;
    float       in_sample  = -1;
    float       pt_x       = x;
    const float right      = x + width;

    nvgBeginPath(nvg);
    while (pt_x < right)
    {
        float out_sample = distort(in_sample, drive);

        float pt_y = cy - out_sample * half_height;
        if (pt_x == x)
            nvgMoveTo(nvg, pt_x, pt_y);
        else
            nvgLineTo(nvg, pt_x, pt_y);

        in_sample += sample_inc;
        pt_x      += 1;
    }

    nvgStrokeColor(nvg, nvgRGBA(255, 0, 127, 255));
    nvgStrokeWidth(nvg, 4);
    nvgStroke(nvg);
}