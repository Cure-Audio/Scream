#pragma once
#include "dsp.h"
#include "imgui.h"
#include <math.h>
#include <nanovg.h>
#include <stdio.h>
#include <string.h>

void im_slider(
    NVGcontext*    nvg,
    imgui_context* im,
    imgui_rect     rect,
    float*         pValue,
    float          vmin,
    float          vmax,
    const char*    fmt_value,
    const char*    name)
{
    xassert((*pValue) >= vmin && (*pValue) <= vmax);
    uint32_t events        = imgui_get_events_rect(im, &rect);
    float    slider_width  = rect.r - rect.x;
    float    slider_height = rect.b - rect.y;
    float    knob_radius   = slider_height * 0.5f;

    float slider_bg_width = slider_width - knob_radius * 2;

    if (events & IMGUI_EVENT_DRAG_MOVE)
    {
        imgui_drag_value(im, pValue, vmin, vmax, slider_bg_width, IMGUI_DRAG_HORIZONTAL);
    }

    nvgBeginPath(nvg);
    nvgRect(nvg, rect.x, rect.y, slider_width, slider_height);
    nvgFillColor(nvg, nvgRGBAf(0.2, 0.2, 0.25, 0.5));
    nvgFill(nvg);

    float val_x = xm_mapf(*pValue, vmin, vmax, rect.x, rect.r - slider_height);
    nvgBeginPath(nvg);
    nvgRect(nvg, val_x, rect.y, slider_height, slider_height);
    nvgFillColor(nvg, nvgRGBAf(0.2, 0.2, 0.8, 1.0));
    nvgFill(nvg);

    imgui_pt c = imgui_centre(&rect);
    char     label[16];
    snprintf(label, sizeof(label), fmt_value, *pValue);
    nvgFillColor(nvg, (NVGcolor){1, 1, 1, 1});
    nvgFontSize(nvg, slider_height * 0.75);
    nvgTextAlign(nvg, NVG_ALIGN_MIDDLE | NVG_ALIGN_CENTER);
    nvgText(nvg, c.x, c.y, label, NULL);

    nvgTextAlign(nvg, NVG_ALIGN_MIDDLE | NVG_ALIGN_LEFT);
    nvgFillColor(nvg, (NVGcolor){0, 0, 0, 1});
    nvgText(nvg, rect.r + 10, c.y, name, NULL);
}

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

    float peak_dB  = xm_fast_gain_to_dB(fabsf(x));
    float comp_dB  = soft_knee_compress(peak_dB, threshold, 1.0f / ratio, knee);
    comp_dB       -= peak_dB;
    float gain     = xm_fast_dB_to_gain(comp_dB);
    float y        = gain * x;
    return y;
}

float waveshape_softknee(float x, float threshold, float ratio, float knee)
{
    float peak_dB  = xm_fast_gain_to_dB(fabsf(x));
    float comp_dB  = soft_knee_compress(peak_dB, threshold, 1.0f / ratio, knee);
    comp_dB       -= peak_dB;
    float gain     = xm_fast_dB_to_gain(comp_dB);
    float y        = gain * x;
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

static float* buffer_saw       = NULL;
static float* buffer_processed = NULL;
static size_t buffer_saw_len   = 0;
// E1 = 28 midi = 41.2Hz
void make_saw(float Hz, float sample_rate)
{
    const int NUM_SAWS = 2;

    const int samples_per_cycle = (int)(sample_rate / Hz);

    const int next_buffer_len = samples_per_cycle * NUM_SAWS;

    if (next_buffer_len != buffer_saw_len)
    {
        buffer_saw_len   = next_buffer_len;
        buffer_saw       = realloc(buffer_saw, buffer_saw_len * sizeof(float));
        buffer_processed = realloc(buffer_processed, buffer_saw_len * sizeof(float));

        // Fill saw
        const float inc = 2.0f / samples_per_cycle;
        float       saw = -1;
        for (int i = 0; i < samples_per_cycle; i++)
        {
            buffer_saw[i]  = saw;
            saw           += inc;
        }
        for (int i = 1; i < NUM_SAWS; i++)
        {
            memcpy(buffer_saw + (i * samples_per_cycle), buffer_saw, sizeof(float) * samples_per_cycle);
        }
    }
}

void plot_line(
    NVGcontext*  nvg,
    float        x,
    float        y,
    float        width,
    float        height,
    const float* buf,
    size_t       buflen,
    NVGcolor     col)
{
    const float half_height = height * 0.5f;
    const float cy          = y + half_height;
    const float right       = x + width;
    nvgBeginPath(nvg);
    for (int i = 0; i < buflen; i++)
    {
        float sample = buf[i];

        float pt_x = xm_mapf(i, 0, buffer_saw_len, x, right);
        float pt_y = cy - sample * half_height;
        if (i == 0)
            nvgMoveTo(nvg, pt_x, pt_y);
        else
            nvgLineTo(nvg, pt_x, pt_y);
    }

    nvgStrokeColor(nvg, col);
    nvgStrokeWidth(nvg, 2);
    nvgStroke(nvg);
}

void plot_peak_distortion(NVGcontext* nvg, imgui_context* im, float gui_width, float gui_height)
{
    imgui_rect rect = {20, 20, 180, 40};
    // Pos params
    static float pos_input_gain = 12;
    im_slider(nvg, im, rect, &pos_input_gain, 0, 60, "%.2f dB", "+ Input gain");
    rect.y += 40;
    rect.b += 40;

    // Neg params
    static float threshold  = -20;
    static float ratio      = 3.5;
    static float knee       = 12;
    static float input_gain = 12;
    static float drive      = 0;
    im_slider(nvg, im, rect, &threshold, -60, 0, "%.2f dB", "- Threshold");
    rect.y += 40;
    rect.b += 40;
    im_slider(nvg, im, rect, &ratio, 1, 10, "1:%.2f", "- Ratio");
    rect.y += 40;
    rect.b += 40;
    im_slider(nvg, im, rect, &knee, 1, 60, "%.2f dB", "- Knee");
    rect.y += 40;
    rect.b += 40;
    im_slider(nvg, im, rect, &input_gain, 0, 60, "%.2f dB", "- Input gain");
    rect.y += 40;
    rect.b += 40;
    im_slider(nvg, im, rect, &drive, 0, 1, "%.2f", "Drive");

    float x, y, width, height;

    y     = 2;
    width = height = gui_height - 4;

    const float half_height = height * 0.5f;

    x = gui_width * 0.5f - half_height;

    const float cy = y + half_height;

    nvgBeginPath(nvg);

    nvgMoveTo(nvg, x + width * 0.5f, y);
    nvgLineTo(nvg, x + width * 0.5f, y + gui_height);
    nvgMoveTo(nvg, x, y + gui_height * 0.5f);
    nvgLineTo(nvg, x + width, y + gui_height * 0.5f);
    nvgStrokeWidth(nvg, 1.2);
    nvgStrokeColor(nvg, nvgRGBAf(0.2, 0.2, 0.3, 1.0f));
    nvgStroke(nvg);

    // {
    //     float       sample_inc = 2 / width;
    //     float       in_sample  = -1;
    //     float       pt_x       = x;
    //     const float right      = x + width;

    //     nvgBeginPath(nvg);
    //     const float amt              = xm_fast_gain_to_dB(0.3);
    //     float       pos_input_gain_G = xm_fast_dB_to_gain(pos_input_gain);
    //     float       neg_input_gain_G = xm_fast_dB_to_gain(input_gain);
    //     while (pt_x < right)
    //     {
    //         float pos        = tanhf(in_sample * pos_input_gain_G);
    //         float neg        = waveshape_softknee(in_sample * neg_input_gain_G, threshold, ratio, knee);
    //         float out_sample = in_sample >= 0 ? pos : neg;

    //         float pt_y = cy - out_sample * half_height;
    //         if (pt_x == x)
    //             nvgMoveTo(nvg, pt_x, pt_y);
    //         else
    //             nvgLineTo(nvg, pt_x, pt_y);

    //         in_sample += sample_inc;
    //         pt_x      += 1;
    //     }
    // }
    {
        make_saw(41.2f, 48000.0f);

        plot_line(nvg, x, y, width, height, buffer_saw, buffer_saw_len, nvgRGBA(0, 127, 127, 255));

        enum
        {
            MA_LENGTH = 50
        };
        float ma_buf[MA_LENGTH];
        int   ma_idx         = 0;
        float ma_range_sum   = 0;
        float ma_running_sum = 0;

        memset(ma_buf, 0, sizeof(ma_buf));

        float pos_input_gain_G = xm_fast_dB_to_gain(pos_input_gain);
        for (int i = 0; i < buffer_saw_len; i++)
        {
            float saw = buffer_saw[i];

            saw = xm_clampf(saw * pos_input_gain_G, -1, 1);

            // MA stuff
            float ma_prev  = ma_buf[ma_idx];
            ma_buf[ma_idx] = saw;

            ma_idx++;
            if (ma_idx == MA_LENGTH)
            {
                ma_running_sum = ma_range_sum;
                ma_idx         = 0;
                ma_range_sum   = 0;
            }
            ma_running_sum += saw - ma_prev;
            ma_range_sum   += saw;

            float ma_sample     = ma_running_sum / MA_LENGTH;
            buffer_processed[i] = ma_sample;
        }
        plot_line(nvg, x, y, width, height, buffer_processed, buffer_saw_len, nvgRGBA(255, 0, 127, 255));
    }
}