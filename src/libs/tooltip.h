#pragma once
#include <imgui.h>
#include <linked_arena.h>
#include <nanovg2.h>
#include <string.h>

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
                       | IMGUI_EVENT_MOUSE_LEFT_UP
                       | IMGUI_EVENT_MOUSE_RIGHT_UP
                       | IMGUI_EVENT_MOUSE_MIDDLE_UP
                       | IMGUI_EVENT_TOUCHPAD_END
                       | IMGUI_EVENT_DRAG_END,
};
// clang-format on

typedef struct Tooltip
{
    const char* text;

    imgui_rect target_widget_dimensions;

    // Tracks when last call to tooltip_show() was made.
    uint64_t time_shown_ns;
} Tooltip;

static void tooltip_show(Tooltip* tt, imgui_rect d, const char* text, uint64_t time_ns)
{
    tt->text                     = text;
    tt->target_widget_dimensions = d;
    tt->time_shown_ns            = time_ns;
}

static void tooltip_hide(Tooltip* tt) { memset(tt, 0, sizeof(*tt)); }

static void tooltip_handle_events(Tooltip* tt, imgui_rect d, const char* txt, uint64_t time_ns, uint32_t events)
{
    if (events & TOOLTIP_HIDE_FLAGS)
        tooltip_hide(tt);
    else if (events & TOOLTIP_SHOW_FLAGS)
        tooltip_show(tt, d, txt, time_ns);
}

void tooltip_draw(
    Tooltip*            tt,
    struct NVGcontext*  nvg,
    struct LinkedArena* arena,
    uint64_t            time_ns,
    float               gui_width,
    float               gui_height,
    float               font_size);