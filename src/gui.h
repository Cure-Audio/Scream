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
    float param_positions_cx[NUM_PARAMS];
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

    bool    lfo_points_dirty;
    bool    lfo_cached_path_dirty;
    xvec2f* lfo_points;
    xvec2f* lfo_skew_points;
    xvec2f* lfo_cached_path;

    uint64_t frame_start_time;
    uint64_t frame_end_time;

    uint64_t gui_create_time;
    uint64_t last_resize_time;

    SGNVGframebuffer main_framebuffer;
} GUI;

// Nanovg helpers
#define NVG_ALIGN_TL (NVG_ALIGN_TOP | NVG_ALIGN_LEFT)
#define NVG_ALIGN_TC (NVG_ALIGN_TOP | NVG_ALIGN_CENTER)
#define NVG_ALIGN_TR (NVG_ALIGN_TOP | NVG_ALIGN_RIGHT)

#define NVG_ALIGN_CL (NVG_ALIGN_MIDDLE | NVG_ALIGN_LEFT)
#define NVG_ALIGN_CC (NVG_ALIGN_MIDDLE | NVG_ALIGN_CENTER)
#define NVG_ALIGN_CR (NVG_ALIGN_MIDDLE | NVG_ALIGN_RIGHT)

#define NVG_ALIGN_BL (NVG_ALIGN_BOTTOM | NVG_ALIGN_LEFT)
#define NVG_ALIGN_BC (NVG_ALIGN_BOTTOM | NVG_ALIGN_CENTER)
#define NVG_ALIGN_BR (NVG_ALIGN_BOTTOM | NVG_ALIGN_RIGHT)

static const NVGcolour COLOUR_TEXT = nvgHexColour(0x828A91FF);

static const NVGcolour COLOUR_BG_LIGHT = nvgHexColour(0xC9D3DDFF);
static const NVGcolour COLOUR_BG_DARK  = nvgHexColour(0x151B32FF);

static const NVGcolour COLOUR_GREY_1 = nvgHexColour(0xB5BEC7FF);
static const NVGcolour COLOUR_GREY_2 = nvgHexColour(0x636A78FF);
static const NVGcolour COLOUR_GREY_3 = nvgHexColour(0x353940FF);

// For snapping to certain pixel boundaries
#define snapf(val, interval) (roundf((val) / (interval)) * (interval))