#pragma once
#include "common.h"
#include <cplug_extensions/window.h>
#include <math.h>
#include <xhl/vector.h>

typedef struct imgui_widget
{
    float x, y, r, b;
} imgui_widget;

typedef struct imgui_context
{
    // Set to false whenever there are new events
    // Set to 0 on init
    bool has_redrawn;

    bool mouse_left_down;
    bool mouse_left_down_frame;
    bool mouse_left_up_frame;

    xvec2f mouse_down;
    xvec2f mouse_up;
    xvec2f mouse_move;
    xvec2f mouse_last_drag;
} imgui_context;

bool imgui_hittest(xvec2f pos, imgui_widget* widget)
{
    return pos.x >= widget->x && pos.y >= widget->y && pos.x <= widget->r && pos.y <= widget->b;
}

bool imgui_check_press(imgui_context* ctx, imgui_widget* widget)
{
    return ctx->mouse_left_down && imgui_hittest(ctx->mouse_down, widget);
}

bool imgui_check_release(imgui_context* ctx, imgui_widget* widget)
{
    return ctx->mouse_left_up_frame && imgui_hittest(ctx->mouse_up, widget);
}

bool imgui_check_hover(imgui_context* ctx, imgui_widget* widget) { return imgui_hittest(ctx->mouse_move, widget); }

enum ImguiDragType
{
    IMGUI_DRAG_HORIZONTAL_VERTICAL,
    IMGUI_DRAG_HORIZONTAL,
    IMGUI_DRAG_VERTICAL,
};

void imgui_drag_value(imgui_context* ctx, float* value, float vmin, float vmax, enum ImguiDragType drag_type)
{
    if (ctx->mouse_left_down_frame)
        ctx->mouse_last_drag = ctx->mouse_down;

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
    ctx->mouse_left_down_frame = false;
    if (ctx->mouse_left_up_frame)
        ctx->mouse_left_down = false;
    ctx->mouse_left_up_frame = false;
    ctx->has_redrawn         = true;
}

void imgui_send_event(imgui_context* ctx, const PWEvent* e)
{
    ctx->has_redrawn = false;

    if (e->type == PW_EVENT_MOUSE_LEFT_DOWN)
    {
        ctx->mouse_left_down       = true;
        ctx->mouse_left_down_frame = true;
        ctx->mouse_move.x          = e->mouse.x;
        ctx->mouse_move.y          = e->mouse.y;
        ctx->mouse_down.x          = e->mouse.x;
        ctx->mouse_down.y          = e->mouse.y;
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