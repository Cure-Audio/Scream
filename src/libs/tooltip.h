#pragma once
#include <imgui.h>
#include <linked_arena.h>
#include <string.h>
#include <xvg.h>

// clang-format off
enum
{
    TOOLTIP_HIDE_FLAGS = IMGUI_EVENT_MOUSE_EXIT
                       | IMGUI_EVENT_MOUSE_LEFT_DOWN
                       | IMGUI_EVENT_MOUSE_RIGHT_DOWN
                       | IMGUI_EVENT_MOUSE_MIDDLE_DOWN
                       | IMGUI_EVENT_MOUSE_WHEEL
                       | IMGUI_EVENT_TOUCHPAD_BEGIN
                       | IMGUI_EVENT_TOUCHPAD_MOVE
                       | IMGUI_EVENT_DRAG_BEGIN
                       | IMGUI_EVENT_DRAG_MOVE,

    TOOLTIP_SHOW_FLAGS = IMGUI_EVENT_MOUSE_ENTER
                       | IMGUI_EVENT_MOUSE_MOVE
                       | IMGUI_EVENT_MOUSE_LEFT_UP
                       | IMGUI_EVENT_MOUSE_RIGHT_UP
                       | IMGUI_EVENT_MOUSE_MIDDLE_UP
                       | IMGUI_EVENT_TOUCHPAD_END
                       | IMGUI_EVENT_DRAG_END,
};
// clang-format on

typedef struct Tooltip
{
    struct
    {
        const char* text;

        imgui_rect target_widget_dimensions;

        // Tracks when last call to tooltip_show() was made.
        uint64_t time_shown_ns;
    } state;

    struct
    {
        float box_height;
        float window_boundary;
        float arrow_length;
        float arrow_width;
        float text_padding_x;
        float text_padding_y;
        float gap;
        float font_size;

        unsigned colour_bg;
        unsigned colour_border;
        unsigned colour_text;
    } settings;
} Tooltip;

// Sets defaults
static void tooltip_init(Tooltip* tt)
{
    tt->settings.box_height      = 20;
    tt->settings.window_boundary = 8;
    tt->settings.arrow_length    = 12;
    tt->settings.arrow_width     = 12;
    tt->settings.text_padding_x  = 10;
    tt->settings.text_padding_y  = 6;
    tt->settings.gap             = 4;
    tt->settings.font_size       = 14;

    tt->settings.colour_bg     = 0x000000ff;
    tt->settings.colour_border = 0xffffffff;
    tt->settings.colour_text   = 0xffffffff;
}

static void tooltip_show(Tooltip* tt, imgui_rect d, const char* text, uint64_t time_ns)
{
    tt->state.text                     = text;
    tt->state.target_widget_dimensions = d;
    tt->state.time_shown_ns            = time_ns;
}

static void tooltip_hide(Tooltip* tt) { memset(&tt->state, 0, sizeof(tt->state)); }

static void tooltip_handle_events(Tooltip* tt, imgui_rect d, const char* txt, uint64_t time_ns, uint32_t events)
{
    xassert(strlen(txt));
    if (events & TOOLTIP_HIDE_FLAGS)
        tooltip_hide(tt);
    else if (events & TOOLTIP_SHOW_FLAGS)
        tooltip_show(tt, d, txt, time_ns);
}

void tooltip_draw(
    Tooltip*        tt,
    XVGCommandList* xvg,
    LinkedArena*    arena,
    uint64_t        time_ns,
    float           gui_width,
    float           gui_height,
    float           font_size);