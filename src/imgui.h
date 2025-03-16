#pragma once
#include "common.h"
#include <cplug_extensions/window.h>
#include <math.h>
#include <xhl/vector.h>

typedef struct imgui_widget
{
    float x, y, r, b;
} imgui_widget;

xvec2f imgui_centre(const imgui_widget* widget)
{
    xvec2f pt;
    pt.x = (widget->x + widget->r) * 0.5f;
    pt.y = (widget->y + widget->b) * 0.5f;
    return pt;
}

typedef struct imgui_context
{
    // Roughly tracks how many duplicate frames this library produces
    // If you app receives new events that affect your widgets/display, you should set this number to 0
    // Every call to imgui_end_frame() increments this number
    // It can be used in event driven rendering. If you are, be warned: depending on your platform and swapchain
    // settings, you may need to draw duplicate frames to all your backbuffers. If your app has stopped redrawing and
    // one of your backbuffers is not a duplicate of the other, then you may get stuck with a flickering screen, which
    // is caused by your driver cycling through backbuffers.
    uint32_t num_duplicate_backbuffers;

    bool mouse_left_down;
    bool mouse_left_down_frame;
    bool mouse_left_up_frame;

    // mouse down left
    uint32_t last_left_click_time;
    uint32_t left_click_counter;
    xvec2f   mouse_down;

    xvec2f mouse_up;
    xvec2f mouse_move;
    xvec2f mouse_last_drag;
} imgui_context;

bool imgui_hittest_rect(xvec2f pos, imgui_widget* widget)
{
    return pos.x >= widget->x && pos.y >= widget->y && pos.x <= widget->r && pos.y <= widget->b;
}

bool imgui_hittest_circle(xvec2f pos, xvec2f centre, float radius)
{
    float diff_x   = pos.x - centre.x;
    float diff_y   = pos.y - centre.y;
    float distance = hypotf(fabsf(diff_x), fabsf(diff_y));
    return distance < radius;
}

bool imgui_check_press(imgui_context* ctx, imgui_widget* widget)
{
    return ctx->mouse_left_down && imgui_hittest_rect(ctx->mouse_down, widget);
}

bool imgui_check_release(imgui_context* ctx, imgui_widget* widget)
{
    return ctx->mouse_left_up_frame && imgui_hittest_rect(ctx->mouse_up, widget);
}

// Returns true if hover state toggled
bool imgui_check_hover(imgui_context* ctx, imgui_widget* widget, bool* last_hover)
{
    bool hover  = imgui_hittest_rect(ctx->mouse_move, widget);
    bool toggle = *last_hover != hover;
    *last_hover = hover;
    return toggle;
}

// Returns true if drag state toggled
bool imgui_check_drag(imgui_context* ctx, bool press, bool* last_drag)
{
    bool toggle = false;
    if (*last_drag)
    {
        toggle = ctx->mouse_left_up_frame;
    }
    else if (press) // && !last_drag
    {
        float distance_x = fabsf(ctx->mouse_down.x - ctx->mouse_move.x);
        float distance_y = fabsf(ctx->mouse_down.y - ctx->mouse_move.y);
        float distance_r = hypotf(distance_x, distance_y);
        toggle           = distance_r > 5;
        if (toggle)
            ctx->left_click_counter = 0;
    }
    if (toggle)
        *last_drag = !(*last_drag);
    return toggle;
}

enum ImguiDragType
{
    IMGUI_DRAG_HORIZONTAL_VERTICAL,
    IMGUI_DRAG_HORIZONTAL,
    IMGUI_DRAG_VERTICAL,
};

void imgui_drag_value(imgui_context* ctx, float* value, float vmin, float vmax, enum ImguiDragType drag_type)
{
    xassert(ctx->mouse_left_down); // Are you really dragging right now? Or has your drag ended?
    float delta_x = ctx->mouse_move.x - ctx->mouse_last_drag.x;
    float delta_y = ctx->mouse_last_drag.y - ctx->mouse_move.y;

    ctx->mouse_last_drag.x = ctx->mouse_move.x;
    ctx->mouse_last_drag.y = ctx->mouse_move.y;

    float delta_px = 0;
    switch (drag_type)
    {
    case IMGUI_DRAG_HORIZONTAL_VERTICAL:
        delta_px = fabsf(delta_x) > fabsf(delta_y) ? delta_x : delta_y;
        break;
    case IMGUI_DRAG_HORIZONTAL:
        delta_px = delta_x;
        break;
    case IMGUI_DRAG_VERTICAL:
        delta_px = delta_y;
        break;
    }
    float delta_norm = delta_px / 300;

    float delta_value  = vmin + delta_norm * (vmax - vmin); // lerp
    float next_value   = *value;
    next_value        += delta_value;
    if (next_value > vmax)
        next_value = vmax;
    if (next_value < vmin)
        next_value = vmin;

    *value = next_value;
}

// Act on press
bool imgui_button(imgui_context* ctx, imgui_widget* widget)
{
    return ctx->mouse_left_down_frame && imgui_check_press(ctx, widget);
}
// Act on release
bool imgui_button_mouse_up(imgui_context* ctx, imgui_widget* widget)
{
    return imgui_check_release(ctx, widget) && imgui_check_press(ctx, widget);
}

void imgui_slider(imgui_context* ctx, imgui_widget* widget, float* value, float vmin, float vmax)
{
    xassert(vmin < vmax);
    if (imgui_check_press(ctx, widget))
    {
        imgui_drag_value(ctx, value, vmin, vmax, IMGUI_DRAG_VERTICAL);
    }
}

// Call at the end of every frame after all events have been processed
void imgui_end_frame(imgui_context* ctx)
{
    ctx->num_duplicate_backbuffers++;
    ctx->mouse_left_down_frame = false;
    if (ctx->mouse_left_up_frame)
    {
        ctx->num_duplicate_backbuffers = 0;
        ctx->mouse_left_down           = false;
    }
    ctx->mouse_left_up_frame = false;
}

void imgui_send_event(imgui_context* ctx, const PWEvent* e)
{
    ctx->num_duplicate_backbuffers = 0;

    if (e->type == PW_EVENT_MOUSE_LEFT_DOWN)
    {
        ctx->mouse_left_down       = true;
        ctx->mouse_left_down_frame = true;
        ctx->mouse_move.x          = e->mouse.x;
        ctx->mouse_move.y          = e->mouse.y;
        ctx->mouse_down.x          = e->mouse.x;
        ctx->mouse_down.y          = e->mouse.y;
        ctx->mouse_last_drag.x     = e->mouse.x;
        ctx->mouse_last_drag.y     = e->mouse.y;

        uint32_t diff = e->mouse.time_ms - ctx->last_left_click_time;
        if (diff > e->mouse.double_click_interval_ms)
            ctx->left_click_counter = 0;
        if (ctx->left_click_counter >= 3)
            ctx->left_click_counter = 0;

        ctx->left_click_counter++;
        ctx->last_left_click_time = e->mouse.time_ms;
    }
    else if (e->type == PW_EVENT_MOUSE_LEFT_UP)
    {
        ctx->mouse_left_up_frame = true;
        ctx->mouse_move.x        = e->mouse.x;
        ctx->mouse_move.y        = e->mouse.y;
        ctx->mouse_up.x          = e->mouse.x;
        ctx->mouse_up.y          = e->mouse.y;
    }
    else if (e->type == PW_EVENT_MOUSE_MOVE)
    {
        ctx->mouse_move.x = e->mouse.x;
        ctx->mouse_move.y = e->mouse.y;
    }
}