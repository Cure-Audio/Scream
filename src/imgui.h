#pragma once
#include "common.h"
#include <cplug_extensions/window.h>
#include <math.h>

struct imgui_widget
{
    float x, y, r, b;
};

typedef struct imgui_context
{
    float mouse_x, mouse_y;
    float mouse_down_x, mouse_down_y;
    float mouse_last_drag_x, mouse_last_drag_y;

    bool mouse_left_down;
    bool mouse_left_down_frame;
    bool mouse_left_up_frame;
} imgui_context;

bool imgui_hittest(imgui_context* ctx, struct imgui_widget* widget)
{
    return ctx->mouse_down_x >= widget->x && ctx->mouse_down_y >= widget->y && ctx->mouse_down_x <= widget->r &&
           ctx->mouse_down_y <= widget->b;
}

bool imgui_button(imgui_context* ctx, struct imgui_widget* widget)
{
    return ctx->mouse_left_down_frame && imgui_hittest(ctx, widget);
}

void imgui_slider(imgui_context* ctx, struct imgui_widget* widget, float* value, float vmin, float vmax)
{
    xassert(vmin < vmax);
    if (ctx->mouse_left_down && imgui_hittest(ctx, widget))
    {
        if (ctx->mouse_left_down_frame)
        {
            ctx->mouse_last_drag_x = ctx->mouse_down_x;
            ctx->mouse_last_drag_y = ctx->mouse_down_y;
        }

        float delta_x = ctx->mouse_x - ctx->mouse_last_drag_x;
        float delta_y = ctx->mouse_last_drag_y - ctx->mouse_y;

        ctx->mouse_last_drag_x = ctx->mouse_x;
        ctx->mouse_last_drag_y = ctx->mouse_y;

        // float delta_px   = fabsf(delta_x) > fabsf(delta_y) ? delta_x : delta_y; // Vertical/horizontal drag
        float delta_px   = delta_x;
        float delta_norm = delta_px / 300;

        float delta_value  = vmin + delta_norm * (vmax - vmin);
        float next_value   = *value;
        next_value        += delta_value;
        if (next_value > vmax)
            next_value = vmax;
        if (next_value < vmin)
            next_value = vmin;

        *value = next_value;
    }
}

void imgui_end_frame(imgui_context* ctx) { ctx->mouse_left_down_frame = false; }

void imgui_send_event(imgui_context* ctx, const PWEvent* e)
{
    if (e->type == PW_EVENT_MOUSE_LEFT_DOWN)
    {
        ctx->mouse_left_down       = true;
        ctx->mouse_left_down_frame = true;
        ctx->mouse_x               = e->mouse.x;
        ctx->mouse_y               = e->mouse.y;
        ctx->mouse_down_x          = e->mouse.x;
        ctx->mouse_down_y          = e->mouse.y;
    }
    else if (e->type == PW_EVENT_MOUSE_LEFT_UP)
    {
        ctx->mouse_left_down     = false;
        ctx->mouse_left_up_frame = true;
    }
    else if (e->type == PW_EVENT_MOUSE_MOVE)
    {
        ctx->mouse_x = e->mouse.x;
        ctx->mouse_y = e->mouse.y;
    }
}