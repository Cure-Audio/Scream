#pragma once
#include "imgui.h"
#include "plugin.h"
#include <nanovg2.h>
#include <sokol_gfx.h>

#include "libs/texteditor.h"
#include "xhl/vector.h"

typedef struct
{
    float   x, y;
    int16_t u, v;
} vertex_t;

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
    float param_positions_cx[NUM_AUTOMATABLE_PARAMS];
    float param_scale;

    imgui_pt knobs_pos[3]; // cx/cy
    float    knob_radius;
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
    sg_buffer   knob_vbo;
    sg_buffer   knob_ibo;

    sg_image logo_id;
    int      logo_width;
    int      logo_height;

    bool      lfo_points_dirty;
    imgui_pt* lfo_points;
    imgui_pt* lfo_skew_points;
    // TODO: rather than cache a line of points, cache the vertices from NanoVG.
    // This will require some editing of NanoVG.
    bool      lfo_cached_path_dirty;
    imgui_pt* lfo_cached_path;

    xvec2f    selection_start;
    xvec2f    selection_end;
    int*      selected_point_indexes;
    imgui_pt* points_copy;

    uint64_t frame_start_time;
    uint64_t frame_end_time;

    uint64_t gui_create_time;
    uint64_t last_resize_time;

    SGNVGframebuffer main_framebuffer;
} GUI;

static const NVGcolour COLOUR_TEXT = nvgHexColour(0x828A91FF);

static const NVGcolour COLOUR_BG_LIGHT = nvgHexColour(0xC9D3DDFF);
static const NVGcolour COLOUR_BG_DARK  = nvgHexColour(0x151B32FF);

static const NVGcolour COLOUR_GREY_1 = nvgHexColour(0xB5BEC7FF);
static const NVGcolour COLOUR_GREY_2 = nvgHexColour(0x636A78FF);
static const NVGcolour COLOUR_GREY_3 = nvgHexColour(0x353940FF);

static const NVGcolour COLOUR_WHITE = nvgHexColour(0xffffffff);

static const NVGcolour COLOUR_BLUE_SECONDARY = nvgHexColour(0x459DB5FF);

// For snapping to certain pixel boundaries
#define snapf(val, interval) (roundf((val) / (interval)) * (interval))