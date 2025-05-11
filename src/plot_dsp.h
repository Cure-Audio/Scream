#pragma once

#ifndef CPLUG_BUILD_STANDALONE
#error This is for testing only
#endif

#include "dsp.h"
#include "imgui.h"
#include <math.h>
#include <nanovg.h>
#include <stdio.h>
#include <string.h>

#ifdef CPLUG_BUILD_STANDALONE
extern float g_input_gain_dB;
extern float g_output_gain_dB;
extern float g_attack_ms;
extern float g_release_ms;
extern float g_lp_Q;
extern float g_hp_Q;
#endif

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

#ifdef CPLUG_BUILD_STANDALONE
float* buffer_audio     = NULL;
float* buffer_processed = NULL;
size_t buffer_audio_len = 0;
// E1 = 28 midi = 41.2Hz
void make_saw(float Hz, float sample_rate)
{
    const int NUM_SAWS = 3;

    const int samples_per_cycle = (int)(sample_rate / Hz);

    const int next_buffer_len = samples_per_cycle * NUM_SAWS;

    if (next_buffer_len != buffer_audio_len)
    {
        buffer_audio_len = next_buffer_len;
        buffer_audio     = MY_REALLOC(buffer_audio, buffer_audio_len * sizeof(float));
        buffer_processed = MY_REALLOC(buffer_processed, buffer_audio_len * sizeof(float));

        // Fill saw
        const float inc = 2.0f / samples_per_cycle;
        float       saw = -1;
        for (int i = 0; i < samples_per_cycle; i++)
        {
            buffer_audio[i]  = tanhf(saw * 2);
            saw             += inc;
        }
        for (int i = 1; i < NUM_SAWS; i++)
        {
            memcpy(buffer_audio + (i * samples_per_cycle), buffer_audio, sizeof(float) * samples_per_cycle);
        }
    }
}
// E1 = 28 midi = 41.2Hz
void make_sine(float Hz, float sample_rate)
{
    const int NUM_SINES = 3;

    const int samples_per_cycle = (int)(sample_rate / Hz);

    const int next_buffer_len = samples_per_cycle * NUM_SINES;

    if (next_buffer_len != buffer_audio_len)
    {
        buffer_audio_len = next_buffer_len;
        buffer_audio     = MY_REALLOC(buffer_audio, buffer_audio_len * sizeof(float));
        buffer_processed = MY_REALLOC(buffer_processed, buffer_audio_len * sizeof(float));

        // Fill saw
        const float inc   = XM_TAUf / samples_per_cycle;
        float       phase = 0;
        for (int i = 0; i < samples_per_cycle; i++)
        {
            float sample     = sinf(phase);
            sample          *= 0.25; // -12dB gain
            buffer_audio[i]  = sample;
            phase           += inc;
        }
        for (int i = 1; i < NUM_SINES; i++)
        {
            memcpy(buffer_audio + (i * samples_per_cycle), buffer_audio, sizeof(float) * samples_per_cycle);
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

        float pt_x = xm_mapf(i, 0, buffer_audio_len, x, right);
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
    float x, y, width, height;

    x      = gui_width * 0.3;
    y      = 2;
    width  = gui_width * 0.7;
    height = gui_height - 4;

    const float half_height = height * 0.5f;

    const float cy = y + half_height;

    nvgBeginPath(nvg);

    nvgMoveTo(nvg, x + width * 0.5f, y);
    nvgLineTo(nvg, x + width * 0.5f, y + gui_height);
    nvgMoveTo(nvg, x, y + gui_height * 0.5f);
    nvgLineTo(nvg, x + width, y + gui_height * 0.5f);
    nvgStrokeWidth(nvg, 1.2);
    nvgStrokeColor(nvg, nvgRGBAf(0.2, 0.2, 0.3, 1.0f));
    nvgStroke(nvg);

    {
        enum
        {
            SAMPLE_RATE = 48000,
        };

        imgui_rect rect = {20, 20, 180, 40};

        // Params
        static float pos_input_gain_dB = 12;
        static float pos_threshold_dB  = -9;
        static float pos_ratio         = 100;
        static float neg_input_gain_dB = 12;
        static float neg_threshold_dB  = -14;
        static float neg_ratio         = 5;
        // static float g_attack_ms         = 0.0;
        // static float g_release_ms        = 0.86;
        // static float g_output_gain_dB    = 8;
        im_slider(nvg, im, rect, &pos_input_gain_dB, 0, 60, "%.2f dB", "+ Input gain");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &pos_threshold_dB, -60, 0, "%.2f dB", "+ Threshold");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &pos_ratio, 1, 100, "1:%.2f", "+ Ratio");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &neg_input_gain_dB, 0, 60, "%.2f dB", "- Input Gain");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &neg_threshold_dB, -60, 0, "%.2f dB", "- Threshold");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &neg_ratio, 1, 100, "1:%.2f", "- Ratio");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &g_attack_ms, 0, 10, "%.3f ms", "Attack");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &g_release_ms, 0, 100, "%.3f ms", "Release");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &g_output_gain_dB, 0, 60, "%.2f dB", "Output gain");
        rect.y += 40;
        rect.b += 40;

        make_saw(41.2f, SAMPLE_RATE);
        plot_line(nvg, x, y, width, height, buffer_audio, buffer_audio_len, nvgRGBA(0, 127, 127, 255));

        float atk_samples = SAMPLE_RATE * g_attack_ms * 0.001;
        float rel_samples = SAMPLE_RATE * g_release_ms * 0.001;
        atk_samples       = floorf(atk_samples);
        rel_samples       = floorf(rel_samples);
        if (atk_samples < 1)
            atk_samples = 1;
        if (rel_samples < 1)
            rel_samples = 1;

        // const float atk = convert_compressor_time(atk_samples);
        // const float rel = convert_compressor_time(rel_samples);
        const float atk = atk_samples > 0 ? xm_fast_dB_to_gain(-1 / atk_samples) : 1;
        const float rel = xm_fast_dB_to_gain(-1 / rel_samples);

        const float pos_ratio_inv    = 1 / pos_ratio;
        const float neg_ratio_inv    = 1 / neg_ratio;
        const float pos_input_gain_G = xm_fast_dB_to_gain(pos_input_gain_dB);
        const float neg_input_gain_G = xm_fast_dB_to_gain(neg_input_gain_dB);
        const float output_gain_G    = xm_fast_dB_to_gain(g_output_gain_dB);

        float xn_1 = 0;
        for (int i = 0; i < buffer_audio_len; i++)
        {
            float input = buffer_audio[i];

            float input_gain_G = input > 0 ? pos_input_gain_G : neg_input_gain_G;
            float threshold_dB = input > 0 ? pos_threshold_dB : neg_threshold_dB;
            float ratio_inv    = input > 0 ? pos_ratio_inv : neg_ratio_inv;

            input *= input_gain_G;

            xn_1          = detect_peak(fabsf(input), xn_1, atk, rel);
            float peak_dB = xm_fast_gain_to_dB(xn_1);
            // float reduction_dB  = soft_knee_compress(peak_dB, threshold, ratio_inv, knee);
            float reduction_dB  = hard_knee_compress(peak_dB, threshold_dB, ratio_inv);
            reduction_dB       -= peak_dB;
            float reduction_G   = xm_fast_dB_to_gain(reduction_dB);

            float output = input * reduction_G * output_gain_G;

            buffer_processed[i] = output;
        }
        plot_line(nvg, x, y, width, height, buffer_processed, buffer_audio_len, nvgRGBA(255, 0, 127, 255));

        nvgFillColor(nvg, nvgRGBAf(0, 0, 0, 1));
        nvgTextAlign(nvg, NVG_ALIGN_BOTTOM | NVG_ALIGN_RIGHT);
        char  label[64];
        float label_y = height - 60;
        snprintf(label, sizeof(label), "Attack samples: %.lf", atk_samples);
        nvgText(nvg, x + width - 5, label_y, label, NULL);

        label_y += 15;
        snprintf(label, sizeof(label), "Release samples: %.lf", rel_samples);
        nvgText(nvg, x + width - 5, label_y, label, NULL);

        label_y += 15;
        snprintf(label, sizeof(label), "Attack: %.10lf", atk);
        nvgText(nvg, x + width - 5, label_y, label, NULL);
        label_y += 15;
        snprintf(label, sizeof(label), "Release: %.10lf", rel);
        nvgText(nvg, x + width - 5, label_y, label, NULL);
    }

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
    /*
    {
        enum
        {
            MA_LENGTH = 500
        };

        imgui_rect rect = {20, 20, 180, 40};
        // Pos params
        static float pos_input_gain = 12;
        static float pos_ma_range   = 10;
        im_slider(nvg, im, rect, &pos_input_gain, 0, 60, "%.2f dB", "+ Input gain");
        rect.y += 40;
        rect.b += 40;
        im_slider(nvg, im, rect, &pos_ma_range, 1, MA_LENGTH, "%.f", "+ MA range samples");
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

        make_saw(41.2f, 48000.0f);

        plot_line(nvg, x, y, width, height, buffer_audio, buffer_audio_len, nvgRGBA(0, 127, 127, 255));

        float ma_buf[MA_LENGTH];
        int   ma_write_idx   = 0;
        float ma_range_sum   = 0;
        float ma_running_sum = 0;
        int   ma_idx_offset  = (int)pos_ma_range;

        int ma_read_idx = MA_LENGTH - ma_idx_offset;

        memset(ma_buf, 0, sizeof(ma_buf));

        float pos_input_gain_G = xm_fast_dB_to_gain(pos_input_gain);
        for (int i = 0; i < buffer_audio_len; i++)
        {
            float saw = buffer_audio[i];

            saw = xm_clampf(saw * pos_input_gain_G, -1, 1);

            //
    https://signalsmith-audio.co.uk/writing/2021/box-sum-cumulative/#avoiding-floating-point-errors-variable-length-box-sum-example-code
            // MA stuff

            // Pop()
            xassert(ma_read_idx >= 0 && ma_read_idx < MA_LENGTH);
            float ma_prev   = ma_buf[ma_read_idx];
            ma_running_sum -= ma_prev;

            ma_read_idx++;
            if (ma_read_idx == MA_LENGTH)
            {
                ma_read_idx    = 0;
                ma_running_sum = ma_range_sum;
            }

            // Push()
            ma_running_sum       += saw;
            ma_range_sum         += saw;
            ma_buf[ma_write_idx]  = saw;

            ma_write_idx++;
            if (ma_write_idx == MA_LENGTH)
            {
                ma_write_idx = 0;
                ma_range_sum = 0;
            }

            float ma_sample     = ma_running_sum / ma_idx_offset;
            buffer_processed[i] = ma_sample;
        }
        plot_line(nvg, x, y, width, height, buffer_processed, buffer_audio_len, nvgRGBA(255, 0, 127, 255));
    }
    */
}

void plot_peak_upwards_compression(NVGcontext* nvg, imgui_context* im, float gui_width, float gui_height)
{
    float x, y, width, height;

    x      = gui_width * 0.3;
    y      = 2;
    width  = gui_width * 0.7;
    height = gui_height - 4;

    const float half_height = height * 0.5f;

    const float cy = y + half_height;

    nvgBeginPath(nvg);

    nvgMoveTo(nvg, x + width * 0.5f, y);
    nvgLineTo(nvg, x + width * 0.5f, y + gui_height);
    nvgMoveTo(nvg, x, y + gui_height * 0.5f);
    nvgLineTo(nvg, x + width, y + gui_height * 0.5f);
    nvgStrokeWidth(nvg, 1.2);
    nvgStrokeColor(nvg, nvgRGBAf(0.2, 0.2, 0.3, 1.0f));
    nvgStroke(nvg);

    {
        enum
        {
            SAMPLE_RATE = 48000,
        };

        imgui_rect rect = {20, 20, 180, 40};

        // Params
        static float threshold_dB = 0;
        static float ratio        = 50;
        static float knee_dB      = 6;
        // static float g_attack_ms    = 5.0;
        // static float g_release_ms   = 5.0;
        static float drive = 1.0;
        // im_slider(nvg, im, rect, &threshold_dB, -60, 0, "%.2f dB", "Threshold");
        // rect.y += 40;
        // rect.b += 40;
        // im_slider(nvg, im, rect, &ratio, 1, 100, "1:%.2f", "Ratio");
        // rect.y += 40;
        // rect.b += 40;
        // im_slider(nvg, im, rect, &knee_dB, 1, 100, "%.2f dB", "Knee");
        // rect.y += 40;
        // rect.b += 40;
        // im_slider(nvg, im, rect, &g_attack_ms, 0, 50, "%.3f ms", "Attack");
        // rect.y += 40;
        // rect.b += 40;
        // im_slider(nvg, im, rect, &g_release_ms, 0, 50, "%.3f ms", "Release");
        // rect.y += 40;
        // rect.b += 40;
        im_slider(nvg, im, rect, &drive, 0, 1, "%.3f", "Drive");
        rect.y += 40;
        rect.b += 40;

        // make_sine(41.2f, SAMPLE_RATE);
        make_saw(41.2f, SAMPLE_RATE);
        plot_line(nvg, x, y, width, height, buffer_audio, buffer_audio_len, nvgRGBA(0, 127, 127, 255));

        float atk_samples = SAMPLE_RATE * g_attack_ms * 0.001;
        float rel_samples = SAMPLE_RATE * g_release_ms * 0.001;
        atk_samples       = floorf(atk_samples);
        rel_samples       = floorf(rel_samples);
        if (atk_samples < 1)
            atk_samples = 1;
        if (rel_samples < 1)
            rel_samples = 1;

        const float atk = convert_compressor_time(atk_samples);
        const float rel = convert_compressor_time(rel_samples);
        // const float ratio_inv = 1 / ratio;

        // denormalise drive (0-1) to ratio (1-100), exponential curve
        float       skewed_ratio = powf(100, drive);
        const float ratio_inv    = 1 / skewed_ratio;

        // Test upwards compression
        /*
        float xn_1 = 0;
        for (int i = 0; i < buffer_audio_len; i++)
        {
            float input = buffer_audio[i];

            float processed_input = fabsf(input);
            float peak_dB         = xm_fast_gain_to_dB(processed_input);

            float dynamic_knee  = fabsf(threshold_dB);
            float boost_dB      = soft_knee_upwards_compress(peak_dB, threshold_dB, ratio_inv, knee_dB);
            boost_dB           -= peak_dB;
            float reduction_G   = xm_fast_dB_to_gain(boost_dB);

            float output        = input * reduction_G;
            buffer_processed[i] = output;
        }
        */
        // Test upwards -> downwards compression
        float xn_1 = 0;
        for (int i = 0; i < buffer_audio_len; i++)
        {
            float input = buffer_audio[i];

            // up comp waveshaping
            float in_dB     = xm_fast_gain_to_dB(fabsf(input));
            float boost_dB  = hard_knee_upwards_compress(in_dB, 0, ratio_inv);
            boost_dB       -= in_dB;
            float boost_G   = xm_fast_dB_to_gain(boost_dB);

            // down comp
            xn_1           = detect_peak(fabsf(input * boost_G), xn_1, atk, rel);
            float peak_dB  = xm_fast_gain_to_dB(xn_1);
            float cut_dB   = soft_knee_compress(peak_dB, threshold_dB, ratio_inv, knee_dB);
            cut_dB        -= peak_dB;
            float cut_G    = xm_fast_dB_to_gain(cut_dB);

            float output        = input * boost_G * cut_G;
            buffer_processed[i] = output;
        }
        plot_line(nvg, x, y, width, height, buffer_processed, buffer_audio_len, nvgRGBA(255, 0, 127, 255));

        nvgFillColor(nvg, nvgRGBAf(0, 0, 0, 1));
        nvgTextAlign(nvg, NVG_ALIGN_BOTTOM | NVG_ALIGN_RIGHT);
        char  label[64];
        float label_y = height - 76;
        snprintf(label, sizeof(label), "Buf length: %zu", buffer_audio_len);
        nvgText(nvg, x + width - 5, label_y, label, NULL);

        label_y += 15;
        snprintf(label, sizeof(label), "Attack samples: %.lf", atk_samples);
        nvgText(nvg, x + width - 5, label_y, label, NULL);
        label_y += 15;
        snprintf(label, sizeof(label), "Release samples: %.lf", rel_samples);
        nvgText(nvg, x + width - 5, label_y, label, NULL);

        label_y += 15;
        snprintf(label, sizeof(label), "Attack: %.10f", atk);
        nvgText(nvg, x + width - 5, label_y, label, NULL);
        label_y += 15;
        snprintf(label, sizeof(label), "Release: %.10f", rel);
        nvgText(nvg, x + width - 5, label_y, label, NULL);

        nvgTextAlign(nvg, NVG_ALIGN_MIDDLE | NVG_ALIGN_LEFT);
        snprintf(label, sizeof(label), "Ratio: 1:%.1f", skewed_ratio);
        nvgText(nvg, rect.x, rect.y, label, NULL);

        // Upwards compression plot
        /*
        {
            float chart_x      = 20;
            float chart_r      = gui_width * 0.3;
            float chart_width  = chart_r - chart_x;
            float chart_y      = cy;
            float chart_b      = gui_height - 20;
            float chart_cy     = (chart_y + chart_b) * 0.5f;
            float chart_height = (chart_b - chart_y);

            for (int i = 0; i <= 5; i++)
            {
                float dB    = -60 + i * 12;
                float x_pos = xm_mapf(dB, -60, 0, chart_x, chart_r);
                float y_pos = xm_mapf(dB, -60, 0, chart_b, chart_y);

                nvgBeginPath(nvg);
                nvgMoveTo(nvg, chart_x, y_pos);
                nvgLineTo(nvg, chart_r, y_pos);
                nvgMoveTo(nvg, x_pos, chart_y);
                nvgLineTo(nvg, x_pos, chart_b);
                nvgStrokeWidth(nvg, 1.2);
                nvgStrokeColor(nvg, (NVGcolor){0, 0, 0, 0.1});
                nvgStroke(nvg);

                // nvgTextAlign(nvg, NVG)
            }

            float chart_x_pos = chart_x;
            bool  first       = true;
            nvgBeginPath(nvg);
            float threshold_G = xm_fast_dB_to_gain(threshold_dB);
            while (chart_x_pos < chart_r)
            {
                float x_dB        = xm_mapf(chart_x_pos, chart_x, chart_r, -60, 0);
                float y_dB        = soft_knee_upwards_compress(x_dB, threshold_dB, ratio_inv, knee_dB);
                float chart_y_pos = xm_mapf(y_dB, -60, 0, chart_b, chart_y);
                if (first)
                {
                    nvgMoveTo(nvg, chart_x_pos, chart_y_pos);
                    first = false;
                }
                else
                {
                    nvgLineTo(nvg, chart_x_pos, chart_y_pos);
                }
                chart_x_pos += 1.0f;
            }
            nvgStrokeWidth(nvg, 1.2);
            nvgStrokeColor(nvg, (NVGcolor){0, 0, 0, 1});
            nvgStroke(nvg);
        }
        */
    }
}

float* oscilloscope_ringbuf      = NULL;
int    oscilloscope_ringbuf_len  = 0;
int    oscilloscope_ringbuf_head = 0;
int    oscilloscope_ringbuf_tail = 0;

void oscilloscope_push(float* const* buf, int buflen, int nchannels)
{
    if (!oscilloscope_ringbuf)
    {
        oscilloscope_ringbuf_len = 1024 * 16;
        oscilloscope_ringbuf     = MY_CALLOC(1, oscilloscope_ringbuf_len * sizeof(float));
    }
    xassert(buflen >= 0 && buflen < oscilloscope_ringbuf_len);

    int          remaining = buflen;
    int          head      = oscilloscope_ringbuf_head;
    const float* src       = buf[0]; // mono
    while (remaining)
    {
        int end = head + remaining;
        if (end > oscilloscope_ringbuf_len)
            end = oscilloscope_ringbuf_len;

        int    worklen = end - head;
        float* dst     = oscilloscope_ringbuf + head;
        memcpy(dst, src, sizeof(*dst) * worklen);

        remaining -= worklen;
        head      += worklen;
        src       += worklen;
        if (head == oscilloscope_ringbuf_len)
            head = 0;
        xassert(remaining >= 0);
        xassert(head >= 0 && head < oscilloscope_ringbuf_len);
    }

    // TODO: sum right channel. for now this only pushes the left channel

    oscilloscope_ringbuf_head = head;
}

void plot_oscilloscope(
    NVGcontext* nvg,
    float       x,
    float       y,
    float       width,
    float       height,
    float       sample_rate,
    float       osc_midi,
    float       osc_phase)
{
    if (osc_midi <= 0)
        return;
    if (oscilloscope_ringbuf_len == 0)
        return;

    const float right = floorf(x + width);
    x                 = floorf(x);
    width             = right - x;

    float samples_per_cycle = 1;
    float idx_inc           = 0.1;

    if (osc_midi > 0)
    {
        float Hz          = xm_midi_to_Hz(osc_midi);
        samples_per_cycle = sample_rate / Hz;
        idx_inc           = samples_per_cycle / width;
    }

    float idx_delta = osc_phase * samples_per_cycle + samples_per_cycle;
    float idx       = (float)oscilloscope_ringbuf_head - idx_delta;

    float     line_x      = floorf(x);
    float     half_height = height * 0.5f;
    float     cy          = y + half_height;
    bool      first       = true;
    const int mask        = oscilloscope_ringbuf_len - 1;
    // test is power of 2
    xassert(xm_popcountu(oscilloscope_ringbuf_len) == 1);
    nvgBeginPath(nvg);
    while (line_x < right)
    {
        // read sample from oscilloscope
        int   idx0       = idx;
        int   idx1       = idx0 + 1;
        float remainder  = idx - idx0;
        idx0            &= mask;
        idx1            &= mask;
        xassert(idx0 >= 0 && idx0 < oscilloscope_ringbuf_len);
        xassert(idx1 >= 0 && idx1 < oscilloscope_ringbuf_len);
        float sample = xm_lerpf(remainder, oscilloscope_ringbuf[idx0], oscilloscope_ringbuf[idx1]);

        float line_y = cy - sample * half_height;
        if (first)
        {
            first = false;
            nvgMoveTo(nvg, line_x, line_y);
        }
        else
        {
            nvgLineTo(nvg, line_x, line_y);
        }

        line_x += 1.0f;
        idx    += idx_inc;
    }
    nvgRect(nvg, x, y, width, height);
    NVGcolor col = {0, 0, 0, 1};
    nvgStrokeWidth(nvg, 1.2f);
    nvgStrokeColor(nvg, col);
    nvgStroke(nvg);
}
#endif // CPLUG_BUILD_STANDALONE