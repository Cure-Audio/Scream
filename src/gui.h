#pragma once
#include "imgui.h"
#include "plugin.h"
#include <nanovg2.h>
#include <sokol_gfx.h>

#include "libs/texteditor.h"
#include "libs/tooltip.h"
#include <xhl/vector.h>

#include "resource_manager.h"

#include "im_points.h"

// #define SHOW_FPS

typedef struct
{
    float   x, y;
    int16_t u, v;
} vertex_t;

// Assortment of cached lengths of things in the GUI
typedef struct LayoutMetrics
{
    int width, height;

    float scale_x;
    float scale_y;

    float content_scale;
    float devicePixelRatio; // Some nonsense value for NVG which I still don't totally understand

    float height_header;
    float height_footer;

    // top section
    float content_x;
    float content_r;
    float content_y;
    float content_b;
    float top_content_height;
    float top_content_bottom;

    // Params
    float param_positions_cx[5];
    float param_scale;

    float knob_outer_radius;
    float knob_inner_radius;

    float cy_param_value;
    float cy_param;
    float cy_param_mod_amount;
    float cy_param_title;

    float last_lfo_playhead;
    float current_lfo_playhead;
} LayoutMetrics;

typedef struct GUI
{
    LinkedArena* arena;

    Plugin*     plugin;
    void*       pw;
    void*       sg;
    NVGcontext* nvg;
    int         font_id;
    float       scale;

    ResourceManager resource_manager;

    TextEditor texteditor;

    struct imgui_context imgui;

    LayoutMetrics layout;
    imgui_rect    lfo_toggle_button;

    float input_gain_peaks_slow[2];
    float input_gain_peaks_fast[2];

    sg_swapchain swapchain;

    sg_buffer lfo_ybuffer_obj;
    sg_view   lfo_ybuffer_view;
    float*    lfo_ybuffer;
    sg_buffer lfo_playhead_trail_obj;
    sg_view   lfo_playhead_trail_view;
    float*    lfo_playhead_trail;

    sg_image   logo_id;
    sg_view    logo_texview;
    int        logo_width;
    int        logo_height;
    unsigned   logo_events;
    imgui_rect logo_area;

    IMPointsData imp;

    Tooltip tooltip;

    uint64_t gui_create_time;
    uint64_t last_resize_time;

    uint64_t last_frame_start_time;
    uint64_t frame_start_time;
#ifdef SHOW_FPS
    uint64_t frame_end_time;

    uint64_t frame_diff_idx;
    uint64_t frame_diff_running_sum;
    uint64_t frame_diff_arr[64];
#endif // SHOW_FPS
} GUI;

static const NVGcolour C_WHITE         = nvgHexColour(0xffffffff);
static const NVGcolour C_TEXT_LIGHT_BG = nvgHexColour(0x707880FF);
static const NVGcolour C_TEXT_DARK_BG  = nvgHexColour(0x828A91FF);

static const NVGcolour C_BG_LIGHT = nvgHexColour(0xC9D3DDFF);
static const NVGcolour C_BG_DARK  = nvgHexColour(0x151B32FF);
static const NVGcolour C_BG_LFO   = nvgHexColour(0x090E20FF);

static const NVGcolour C_GREY_1 = nvgHexColour(0xB5BEC7FF);
static const NVGcolour C_GREY_2 = nvgHexColour(0x636A78FF);
static const NVGcolour C_GREY_3 = nvgHexColour(0x353940FF);

static const NVGcolour C_DARK_BLUE    = nvgHexColour(0x459CB4FF);
static const NVGcolour C_LIGHT_BLUE   = nvgHexColour(0xACDEECFF);
static const NVGcolour C_LIGHT_BLUE_2 = nvgHexColour(0x97E6FCFF);
static const NVGcolour C_GREEN        = nvgHexColour(0x62E32BFF);
static const NVGcolour C_RED          = nvgHexColour(0xFF4757FF);

static const NVGcolour C_GRID_PRIMARY   = nvgHexColour(0x515762FF);
static const NVGcolour C_GRID_SECONDARY = nvgHexColour(0x40464FFF);
static const NVGcolour C_GRID_TERTIARY  = C_GRID_SECONDARY;
// static const NVGcolour C_GRID_TERTIARY  = nvgHexColour(0x2C2F35FF);

// For snapping to certain pixel boundaries
#define snapf(val, interval) (roundf((val) / (interval)) * (interval))

typedef imgui_rect  Rect;
static inline float rect_cx(const Rect* r) { return (r->r + r->x) * 0.5f; }
static inline float rect_cy(const Rect* r) { return (r->b + r->y) * 0.5f; }
