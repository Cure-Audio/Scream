#pragma once
#include "imgui.h"
#include "plugin.h"
#include <nanovg2.h>
#include <sokol_gfx.h>

#include "libs/texteditor.h"
#include "libs/tooltip.h"
#include "xhl/vector.h"

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

    TextEditor texteditor;

    struct imgui_context imgui;

    LayoutMetrics layout;
    imgui_rect    lfo_toggle_button;

    float input_gain_peaks_slow[2];
    float input_gain_peaks_fast[2];

    sg_swapchain swapchain;

    sg_pipeline knob_pip;

    sg_pipeline lfo_vertical_grad_pip;
    sg_buffer   lfo_ybuffer_obj;
    sg_view     lfo_ybuffer_view;
    float*      lfo_ybuffer;
    sg_buffer   lfo_playhead_trail_obj;
    sg_view     lfo_playhead_trail_view;
    float*      lfo_playhead_trail;
    imgui_rect  lfo_grid_area;

    sg_pipeline logo_pip;
    sg_image    logo_id;
    sg_view     logo_texview;
    int         logo_width;
    int         logo_height;

    // If false, should copy over the points array from the audio thread
    bool gui_lfo_points_valid;
    // Used to queue changes made to LFO points on the audio thread
    // Coordinates are in beat time, exactly like the lfo
    LFOPoint* main_lfo_points;

    // Draggable points (widgets)
    // Cordinates are in window space
    bool      should_update_points;
    imgui_pt* points;
    imgui_pt* skew_points;
    // Used as backup while doing non-destructive preview editing of points
    imgui_pt* points_copy;
    imgui_pt* skew_points_copy;

    // Point multiselect
    xvec2f selection_start;
    xvec2f selection_end;
    int*   selected_point_indexes;
    // Used for hacks to make the current selection & hover work properly when previewing edits to points with the
    // drag-auto-erase feature
    int selected_point_idx;

    imgui_pt* lfo_cached_path;

    Tooltip tooltip;

    uint64_t last_frame_start_time;
    uint64_t frame_start_time;
    uint64_t frame_end_time;

    uint64_t gui_create_time;
    uint64_t last_resize_time;

    SGNVGframebuffer main_framebuffer;
} GUI;

static const NVGcolour C_WHITE = nvgHexColour(0xffffffff);
static const NVGcolour C_TEXT  = nvgHexColour(0x707880FF);

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

static const NVGcolour C_GRID_PRIMARY   = nvgHexColour(0x7E8795FF);
static const NVGcolour C_GRID_SECONDARY = nvgHexColour(0x535A65FF);
static const NVGcolour C_GRID_TERTIARY  = nvgHexColour(0x353941FF);

// For snapping to certain pixel boundaries
#define snapf(val, interval) (roundf((val) / (interval)) * (interval))