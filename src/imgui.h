#pragma once
#include <cplug_extensions/window.h>
#include <math.h>
#include <stdbool.h>

typedef struct imgui_rect
{
    float x, y, r, b;
} imgui_rect;

typedef struct imgui_pt
{
    float x, y;
} imgui_pt;

imgui_pt imgui_centre(const imgui_rect* rect)
{
    imgui_pt pt;
    pt.x = (rect->x + rect->r) * 0.5f;
    pt.y = (rect->y + rect->b) * 0.5f;
    return pt;
}

typedef struct imgui_context
{
    // For tracking widgets ownership over events
    // NOTE: Responding to mouse enter/exit events is tricky in IMGUIs
    // For example, two widgets, A & B change the mouse cursor on enter & exit. If Widget A receives a mouse enter event
    // and changes the cursor before Widget B responds to its mouse exit event, where it also updates the cursor, then
    // the cursor will be incorrectly set to Widget Bs cursor. In this case, extra care will need to be taken responsing
    // to mouse exit events.
    // Although everything else in this library appears to be working, requiring this kind of caution from users should
    // be considered a design flaw and future improvements to the design should be made. Great design makes mistakes
    // difficult. Similar problems may exist when responding to drag drop + end events.
    // For now, you will need to be prepared to program like a ninja handling out of order events!
    unsigned id;
    unsigned mouse_over_id;
    unsigned mouse_over_last_frame_id;
    unsigned mouse_left_down_id;
    unsigned mouse_drag_id;
    unsigned mouse_drag_over_id;

    // Roughly tracks how many duplicate frames this library produces
    // If you app receives new events that affect your widgets/display, you should set this number to 0
    // Every call to imgui_end_frame() increments this number
    // It can be used in event driven rendering. If you are, be warned: depending on your platform and swapchain
    // settings, you may need to draw duplicate frames to all your backbuffers. If your app has stopped redrawing and
    // one of your backbuffers is not a duplicate of the other, then you may get stuck with a flickering screen, which
    // is caused by your driver cycling through backbuffers.
    unsigned num_duplicate_backbuffers;

    unsigned left_click_counter;
    unsigned last_left_click_time;

    unsigned frame_events;

    bool mouse_left_down;
    bool mouse_left_down_frame;
    bool mouse_left_up_frame;
    bool mouse_inside_window;

    imgui_pt mouse_down;
    unsigned mouse_down_mods;

    unsigned mouse_up_mods;
    imgui_pt mouse_up;

    imgui_pt mouse_move;
    unsigned mouse_move_mods;

    unsigned mouse_touchpad_mods;
    imgui_pt mouse_touchpad;

    unsigned mouse_wheel_mods;
    int      mouse_wheel;

    imgui_pt mouse_last_drag;
} imgui_context;

bool imgui_hittest_rect(imgui_pt pos, const imgui_rect* rect)
{
    return pos.x >= rect->x && pos.y >= rect->y && pos.x <= rect->r && pos.y <= rect->b;
}

bool imgui_hittest_circle(imgui_pt pos, imgui_pt centre, float radius)
{
    float diff_x   = pos.x - centre.x;
    float diff_y   = pos.y - centre.y;
    float distance = hypotf(fabsf(diff_x), fabsf(diff_y));
    return distance < radius;
}

enum
{
    IMGUI_EVENT_MOUSE_ENTER = 1 << 0,
    IMGUI_EVENT_MOUSE_EXIT  = 1 << 1,
    IMGUI_EVENT_MOUSE_HOVER = 1 << 2,

    IMGUI_EVENT_MOUSE_LEFT_DOWN = 1 << 3, // For single clicks acting on mouse down
    IMGUI_EVENT_MOUSE_LEFT_HOLD = 1 << 4, // For animating widgets while button is held
    IMGUI_EVENT_MOUSE_LEFT_UP   = 1 << 5, // For single clicks acting on mouse up

    IMGUI_EVENT_DRAG_BEGIN = 1 << 6, // Drag source
    IMGUI_EVENT_DRAG_END   = 1 << 7,
    IMGUI_EVENT_DRAG_MOVE  = 1 << 8,

    IMGUI_EVENT_MOUSE_WHEEL = 1 << 9,

    IMGUI_EVENT_TOUCHPAD_BEGIN = 1 << 10,
    IMGUI_EVENT_TOUCHPAD_MOVE  = 1 << 11,
    IMGUI_EVENT_TOUCHPAD_END   = 1 << 12,

    // IMGUI_EVENT_DRAG_ENTER = 1 << 9, // Drag target
    // IMGUI_EVENT_DRAG_EXIT  = 1 << 10,
    // IMGUI_EVENT_DRAG_OVER  = 1 << 11,
    // IMGUI_EVENT_DRAG_DROP  = 1 << 12,

    // TODO: mouse right & middle
    // TODO: file drag & drop, import/export
    // TODO: keyboard events
};

unsigned _imgui_get_events(imgui_context* ctx, bool hover, bool press, bool release)
{
    unsigned events = 0;
    unsigned id     = ++ctx->id;

    // Left mouse button
    if (press && ctx->mouse_left_down_frame)
    {
        events                  |= IMGUI_EVENT_MOUSE_LEFT_DOWN;
        ctx->mouse_left_down_id  = id;
    }
    if (press && ctx->mouse_left_down_id == id)
        events |= IMGUI_EVENT_MOUSE_LEFT_HOLD;
    if (press && ctx->mouse_left_down_id == id && ctx->mouse_left_up_frame)
    {
        events                  |= IMGUI_EVENT_MOUSE_LEFT_UP;
        ctx->mouse_left_down_id  = 0;
    }

    // Drag source
    if (ctx->mouse_left_down_id == id && ctx->mouse_drag_id == 0)
    {
        float distance_x = fabsf(ctx->mouse_down.x - ctx->mouse_move.x);
        float distance_y = fabsf(ctx->mouse_down.y - ctx->mouse_move.y);
        float distance_r = hypotf(distance_x, distance_y);
        if (distance_r > 5) // Drag threshold
        {
            events             |= IMGUI_EVENT_DRAG_BEGIN;
            ctx->mouse_drag_id  = id;
        }
    }
    if (ctx->mouse_drag_id == id && ctx->mouse_left_up_frame)
    {
        events             |= IMGUI_EVENT_DRAG_END;
        ctx->mouse_drag_id  = 0;
    }
    if (ctx->mouse_drag_id == id)
        events |= IMGUI_EVENT_DRAG_MOVE | IMGUI_EVENT_MOUSE_HOVER;

    // Hover
    const bool not_dragging_anything                 = ctx->mouse_drag_id == 0;
    const bool will_release_another_widget_from_drag = ctx->mouse_left_up_frame && ctx->mouse_drag_id != id;
    if (hover && ctx->mouse_over_id != id && (not_dragging_anything || will_release_another_widget_from_drag))
    {
        events             |= IMGUI_EVENT_MOUSE_ENTER;
        ctx->mouse_over_id  = id;
    }
    if (!hover && ctx->mouse_over_last_frame_id == id && ctx->mouse_drag_id != id)
    {
        events |= IMGUI_EVENT_MOUSE_EXIT;
        if (ctx->mouse_over_id == id)
            ctx->mouse_over_id = 0;
    }
    if (hover && ctx->mouse_over_id == id)
        events |= IMGUI_EVENT_MOUSE_HOVER;

    // Mouse wheel & touchpad
    if (ctx->mouse_over_id == id && ctx->mouse_wheel)
        events |= IMGUI_EVENT_MOUSE_WHEEL;
    if (ctx->mouse_over_id == id && (ctx->frame_events & (1 << PW_EVENT_MOUSE_TOUCHPAD_BEGIN)))
        events |= IMGUI_EVENT_TOUCHPAD_BEGIN;
    if (ctx->mouse_over_id == id && (ctx->frame_events & (1 << PW_EVENT_MOUSE_TOUCHPAD_MOVE)))
        events |= IMGUI_EVENT_TOUCHPAD_MOVE;
    if (ctx->mouse_over_id == id && (ctx->frame_events & (1 << PW_EVENT_MOUSE_TOUCHPAD_END)))
        events |= IMGUI_EVENT_TOUCHPAD_END;

    // Cleanup
    if (events & (IMGUI_EVENT_MOUSE_ENTER | IMGUI_EVENT_MOUSE_EXIT | IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_DRAG_END |
                  IMGUI_EVENT_MOUSE_WHEEL))
    {
        ctx->left_click_counter        = 0;
        ctx->num_duplicate_backbuffers = 0;
    }

    return events;
}

unsigned imgui_get_events_rect(imgui_context* ctx, const imgui_rect* rect)
{
    bool hover   = ctx->mouse_inside_window && imgui_hittest_rect(ctx->mouse_move, rect);
    bool press   = ctx->mouse_left_down && imgui_hittest_rect(ctx->mouse_down, rect);
    bool release = ctx->mouse_left_up_frame && imgui_hittest_rect(ctx->mouse_up, rect);
    return _imgui_get_events(ctx, hover, press, release);
}

unsigned imgui_get_events_circle(imgui_context* ctx, imgui_pt pt, float radius)
{
    bool hover   = ctx->mouse_inside_window && imgui_hittest_circle(ctx->mouse_move, pt, radius);
    bool press   = ctx->mouse_left_down && imgui_hittest_circle(ctx->mouse_down, pt, radius);
    bool release = ctx->mouse_left_up_frame && imgui_hittest_circle(ctx->mouse_up, pt, radius);
    return _imgui_get_events(ctx, hover, press, release);
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
    if (ctx->mouse_move_mods & PW_MOD_PLATFORM_KEY_CTRL)
        delta_px *= 0.1f;
    if (ctx->mouse_move_mods & PW_MOD_KEY_SHIFT)
        delta_px *= 0.1f;

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

// Call at the end of every frame after all events have been processed
void imgui_end_frame(imgui_context* ctx)
{
    ctx->num_duplicate_backbuffers++;
    ctx->mouse_left_down_frame = false;
    if (ctx->frame_events & (1 << PW_EVENT_MOUSE_LEFT_UP))
    {
        ctx->num_duplicate_backbuffers = 0;
        ctx->mouse_left_down           = false;
    }
    ctx->mouse_left_up_frame = false;

    ctx->mouse_over_last_frame_id = ctx->mouse_over_id;

    ctx->mouse_down_mods     = 0;
    ctx->mouse_up_mods       = 0;
    ctx->mouse_move_mods     = 0;
    ctx->mouse_touchpad_mods = 0;
    ctx->mouse_wheel_mods    = 0;
    ctx->mouse_wheel         = 0;
    ctx->frame_events        = 0;

    ctx->id = 0;
}

void imgui_send_event(imgui_context* ctx, const PWEvent* e)
{
    ctx->num_duplicate_backbuffers = 0;
    ctx->frame_events              = 1 << e->type;

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

        ctx->mouse_down_mods |= e->mouse.modifiers;

        unsigned diff = e->mouse.time_ms - ctx->last_left_click_time;
        if (diff > e->mouse.double_click_interval_ms)
            ctx->left_click_counter = 0;
        if (ctx->left_click_counter >= 3)
            ctx->left_click_counter = 0;

        ctx->left_click_counter++;
        ctx->last_left_click_time = e->mouse.time_ms;
    }
    else if (e->type == PW_EVENT_MOUSE_LEFT_UP)
    {
        ctx->mouse_left_up_frame  = true;
        ctx->mouse_move.x         = e->mouse.x;
        ctx->mouse_move.y         = e->mouse.y;
        ctx->mouse_up.x           = e->mouse.x;
        ctx->mouse_up.y           = e->mouse.y;
        ctx->mouse_up_mods       |= e->mouse.modifiers;
    }
    else if (e->type == PW_EVENT_MOUSE_MOVE)
    {
        ctx->mouse_move.x     = e->mouse.x;
        ctx->mouse_move.y     = e->mouse.y;
        ctx->mouse_move_mods |= e->mouse.modifiers;
    }
    else if (e->type == PW_EVENT_MOUSE_SCROLL_WHEEL)
    {
        ctx->mouse_wheel       = (int)(e->mouse.y / 120.0f);
        ctx->mouse_wheel_mods |= e->mouse.modifiers;
    }
    else if (
        e->type == PW_EVENT_MOUSE_TOUCHPAD_MOVE || e->type == PW_EVENT_MOUSE_TOUCHPAD_BEGIN ||
        e->type == PW_EVENT_MOUSE_TOUCHPAD_END)
    {
        ctx->mouse_touchpad.x     = e->mouse.x;
        ctx->mouse_touchpad.y     = e->mouse.y;
        ctx->mouse_touchpad_mods |= e->mouse.modifiers;
    }
    else if (e->type == PW_EVENT_MOUSE_ENTER)
    {
        ctx->mouse_move.x        = e->mouse.x;
        ctx->mouse_move.y        = e->mouse.y;
        ctx->mouse_over_id       = 0;
        ctx->mouse_inside_window = true;
    }
    else if (e->type == PW_EVENT_MOUSE_EXIT)
    {
        ctx->mouse_over_id       = 0;
        ctx->mouse_inside_window = false;
    }
}