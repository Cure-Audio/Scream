#pragma once
#include "xhl/debug.h"
#include <cplug_extensions/window.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

typedef struct imgui_rect
{
    float x, y, r, b;
} imgui_rect;

typedef struct imgui_pt
{
    float x, y;
} imgui_pt;

static imgui_pt imgui_centre(const imgui_rect* rect)
{
    imgui_pt pt;
    pt.x = (rect->x + rect->r) * 0.5f;
    pt.y = (rect->y + rect->b) * 0.5f;
    return pt;
}

enum ImguiMouseButtonType
{
    IMGUI_MOUSE_BUTTON_NONE,
    IMGUI_MOUSE_BUTTON_LEFT,
    IMGUI_MOUSE_BUTTON_RIGHT,
    IMGUI_MOUSE_BUTTON_MIDDLE,
};

enum // Event flags
{
    IMGUI_EVENT_MOUSE_ENTER = 1 << 0,
    IMGUI_EVENT_MOUSE_EXIT  = 1 << 1,
    IMGUI_EVENT_MOUSE_HOVER = 1 << 2,

    IMGUI_EVENT_MOUSE_LEFT_DOWN = 1 << 3, // For single/multiple clicks acting on mouse down
    IMGUI_EVENT_MOUSE_LEFT_HOLD = 1 << 4, // For animating widgets while button is held
    IMGUI_EVENT_MOUSE_LEFT_UP   = 1 << 5, // For single clicks acting on mouse up

    IMGUI_EVENT_MOUSE_RIGHT_DOWN = 1 << 6,
    IMGUI_EVENT_MOUSE_RIGHT_HOLD = 1 << 7,
    IMGUI_EVENT_MOUSE_RIGHT_UP   = 1 << 8,

    IMGUI_EVENT_MOUSE_MIDDLE_DOWN = 1 << 9,
    IMGUI_EVENT_MOUSE_MIDDLE_HOLD = 1 << 10,
    IMGUI_EVENT_MOUSE_MIDDLE_UP   = 1 << 11,

    IMGUI_EVENT_MOUSE_WHEEL    = 1 << 12,
    IMGUI_EVENT_TOUCHPAD_BEGIN = 1 << 13, // For MacBooks touchpad
    IMGUI_EVENT_TOUCHPAD_MOVE  = 1 << 14,
    IMGUI_EVENT_TOUCHPAD_END   = 1 << 15,

    IMGUI_EVENT_DRAG_BEGIN = 1 << 16, // Drag source
    IMGUI_EVENT_DRAG_END   = 1 << 17,
    IMGUI_EVENT_DRAG_MOVE  = 1 << 18,

    IMGUI_EVENT_DRAG_ENTER = 1 << 19, // Drag target
    IMGUI_EVENT_DRAG_EXIT  = 1 << 20,
    IMGUI_EVENT_DRAG_OVER  = 1 << 21,
    IMGUI_EVENT_DRAG_DROP  = 1 << 22,

    // TODO: file drag & drop, import/export
    // TODO: keyboard events

    IMGUI_FLAGS_PW_MOUSE_DOWN_EVENTS =
        (1 << PW_EVENT_MOUSE_LEFT_DOWN) | (1 << PW_EVENT_MOUSE_RIGHT_DOWN) | (1 << PW_EVENT_MOUSE_MIDDLE_DOWN),
    IMGUI_FLAGS_PW_MOUSE_UP_EVENTS =
        (1 << PW_EVENT_MOUSE_LEFT_UP) | (1 << PW_EVENT_MOUSE_RIGHT_UP) | (1 << PW_EVENT_MOUSE_MIDDLE_UP),
};

// There is no official init function for this object. Just set the whole thing to 0.
// If your app responds to resizes at draw time, consider adding 'frame.id |= 1 << PW_EVENT_RESIZE;' on init.
typedef struct imgui_context
{
    unsigned uid_last_frame_mouse_over;
    unsigned uid_last_frame_drag_over;

    unsigned frame_id_mouse_over;
    unsigned frame_id_drag_over;
    unsigned uid_mouse_over;
    unsigned uid_drag_over;
    unsigned uid_mouse_hold;
    unsigned uid_drag;
    unsigned uid_touchpad;

    unsigned left_click_counter;
    unsigned last_left_click_time;

    // Needed for tracking drags made by a specific mouse button
    enum ImguiMouseButtonType mouse_hold_type;

    imgui_pt pos_mouse_down;
    imgui_pt pos_mouse_up;
    imgui_pt pos_mouse_move;

    imgui_pt mouse_last_drag;

    bool mouse_inside_window;

    // Roughly tracks how many duplicate frames this library produces
    // If you app receives new events that affect your widgets/display, you should set this number to 0
    // Every call to imgui_end_frame() increments this number
    // It can be used in event driven rendering. If you are, be warned: depending on your platform and swapchain
    // settings, you may need to draw duplicate frames to all your backbuffers. If your app has stopped redrawing and
    // one of your backbuffers is not a duplicate of the other, then you may get stuck with a flickering screen, which
    // is caused by your driver cycling through backbuffers.
    unsigned num_duplicate_backbuffers;

    int last_width, last_height;
    int next_width, next_height;

    struct
    {
        // For tracking widgets ownership over events
        // NOTE: Responding to mouse enter/exit events is tricky in IMGUIs
        // For example, two widgets, A & B change the mouse cursor on enter & exit. If Widget A receives a mouse enter
        // event and changes the cursor before Widget B responds to its mouse exit event, where it also updates the
        // cursor, then the cursor will be incorrectly set to Widget Bs cursor. In this case, extra care will need to be
        // taken responsing to mouse exit events. Although everything else in this library appears to be working,
        // requiring this kind of caution from users should be considered a design flaw and future improvements to the
        // design should be made. Great design makes mistakes difficult. Similar problems may exist when responding to
        // drag drop + end events. For now, you will need to be prepared to program like a ninja handling out of order
        // events!
        unsigned id; // frame.id

        unsigned events; // |= (1 << PW_EVENT_XXX);

        enum ImguiMouseButtonType type_mouse_down; // Needed in case multiple clicks are sent in between frames
        enum ImguiMouseButtonType type_mouse_up;

        unsigned uid_mouse_down;
        unsigned uid_mouse_up;

        unsigned uid_drag_over_end;
        unsigned uid_drag_end;
        unsigned uid_touchpad_end;

        unsigned modifiers_mouse_down;
        unsigned modifiers_mouse_up;
        unsigned modifiers_mouse_move;
        unsigned modifiders_mouse_wheel; // This probably needs to have its own way to handle ownership of events
        unsigned modifiers_touchpad;

        imgui_pt delta_touchpad;
        int      delta_mouse_wheel;
    } frame;

#ifndef NDEBUG
    struct
    {
        size_t    length;
        size_t    capacity;
        unsigned* data;
    } duplicate_uid_detector;
#endif
} imgui_context;

static void imgui_clear_widget(imgui_context* ctx)
{
    ctx->uid_last_frame_mouse_over = 0;
    ctx->uid_last_frame_drag_over  = 0;
    ctx->uid_mouse_over            = 0;
    ctx->uid_drag_over             = 0;
    ctx->uid_mouse_hold            = 0;
    ctx->uid_drag                  = 0;
    ctx->uid_touchpad              = 0;
    ctx->left_click_counter        = 0;

    ctx->frame.uid_mouse_down    = 0;
    ctx->frame.uid_mouse_up      = 0;
    ctx->frame.uid_drag_over_end = 0;
    ctx->frame.uid_drag_end      = 0;
    ctx->frame.uid_touchpad_end  = 0;
}

static unsigned imgui_hash(const char* str)
{
    // MurmurHash. Apparently very fast with few collisions: https://stackoverflow.com/a/69812981
    // Source: https://github.com/aappleby/smhasher/blob/master/src/Hashes.cpp
    // "All MurmurHash versions are public domain software, and the author disclaims all copyright to their code."

    unsigned int h = 0x12345678; // Seed
    for (; *str; ++str)
    {
        h ^= *str;
        h *= 0x5bd1e995;
        h ^= h >> 15;
    }
    return h;
}

#ifndef NDEBUG
#if !defined(IMGUI_REALLOC) || !defined(IMGUI_FREE)
#include <stdlib.h>
#define IMGUI_REALLOC(ptr, sz) realloc(ptr, sz)
#define IMGUI_FREE(ptr)        free(ptr)
#endif
static bool imgui_is_uid_valid(imgui_context* ctx, unsigned uid)
{
    PW_ASSERT(uid != 0);
    if (uid == 0)
        return false;
    const size_t N = ctx->duplicate_uid_detector.length;

    for (int i = 0; i < N; i++)
    {
        PW_ASSERT(ctx->duplicate_uid_detector.data[i] != uid);
        if (ctx->duplicate_uid_detector.data[i] == uid)
            return false;
    }

    ctx->duplicate_uid_detector.length++;
    if (ctx->duplicate_uid_detector.length > ctx->duplicate_uid_detector.capacity)
    {
        ctx->duplicate_uid_detector.capacity *= 2;
        if (ctx->duplicate_uid_detector.capacity == 0)
            ctx->duplicate_uid_detector.capacity = 256;
        ctx->duplicate_uid_detector.data = IMGUI_REALLOC(
            ctx->duplicate_uid_detector.data,
            ctx->duplicate_uid_detector.capacity * sizeof(*ctx->duplicate_uid_detector.data));
    }

    ctx->duplicate_uid_detector.data[N] = uid;
    return true;
}
#endif

static bool imgui_hittest_rect(imgui_pt pos, const imgui_rect* rect)
{
    return pos.x >= rect->x && pos.y >= rect->y && pos.x <= rect->r && pos.y <= rect->b;
}

static bool imgui_hittest_circle(imgui_pt pos, imgui_pt centre, float radius)
{
    float diff_x   = pos.x - centre.x;
    float diff_y   = pos.y - centre.y;
    float distance = hypotf(fabsf(diff_x), fabsf(diff_y));
    return distance < radius;
}

// Returns event flags
static unsigned _imgui_get_events(imgui_context* ctx, unsigned uid, bool hover, bool mouse_down, bool mouse_up)
{
    PW_ASSERT(imgui_is_uid_valid(ctx, uid));

    unsigned events   = 0;
    unsigned frame_id = ++ctx->frame.id;

    // Hover
    const bool no_active_hover       = ctx->uid_mouse_over == 0;
    const bool should_steal_hover    = frame_id < ctx->frame_id_mouse_over;
    const bool not_dragging_anything = ctx->uid_drag == 0 && ctx->mouse_hold_type == IMGUI_MOUSE_BUTTON_NONE;
    const bool will_release_another_widget_from_drag =
        ctx->uid_drag != uid && ctx->frame.type_mouse_up != IMGUI_MOUSE_BUTTON_NONE;

    bool mouse_enter  = hover;
    mouse_enter      &= ctx->uid_mouse_over != uid;
    mouse_enter      &= (no_active_hover || should_steal_hover);
    mouse_enter      &= (not_dragging_anything || will_release_another_widget_from_drag);
    if (mouse_enter)
    {
        events                   |= IMGUI_EVENT_MOUSE_ENTER;
        ctx->frame_id_mouse_over  = frame_id;
        ctx->uid_mouse_over       = uid;
    }

    const bool has_had_hover_stolen  = ctx->frame_id_mouse_over > 0 && ctx->frame_id_mouse_over < frame_id;
    bool       is_exiting            = !hover || has_had_hover_stolen;
    is_exiting                      &= ctx->uid_last_frame_mouse_over == uid; // was hovering
    is_exiting                      &= ctx->uid_mouse_hold != uid;            // no mouse hold

    if (is_exiting)
    {
        events |= IMGUI_EVENT_MOUSE_EXIT;
        if (ctx->uid_mouse_over == uid)
        {
            ctx->uid_mouse_over      = 0;
            ctx->frame_id_mouse_over = 0;
        }
    }

    if (ctx->uid_mouse_over == uid)
        events |= IMGUI_EVENT_MOUSE_HOVER;

    // Mouse wheel & touchpad
    if (ctx->uid_mouse_over == uid && ctx->frame.delta_mouse_wheel)
    {
        events |= IMGUI_EVENT_MOUSE_WHEEL;
    }

    if (ctx->uid_mouse_over == uid && (ctx->frame.events & (1 << PW_EVENT_MOUSE_TOUCHPAD_BEGIN)))
    {
        events            |= IMGUI_EVENT_TOUCHPAD_BEGIN;
        ctx->uid_touchpad  = uid;
    }
    if (ctx->uid_touchpad == uid && (ctx->frame.events & (1 << PW_EVENT_MOUSE_TOUCHPAD_MOVE)))
        events |= IMGUI_EVENT_TOUCHPAD_MOVE;
    if (ctx->uid_touchpad == uid && (ctx->frame.events & (1 << PW_EVENT_MOUSE_TOUCHPAD_END)))
        events |= IMGUI_EVENT_TOUCHPAD_END;

    // Drag & drop
    const bool is_dragging_something_else = ctx->uid_drag != 0 && ctx->uid_drag != uid;
    const bool no_active_drag_over        = ctx->uid_drag_over == 0;
    const bool should_steal_drag_over     = frame_id < ctx->frame_id_drag_over;

    bool is_dragging_over  = hover;
    is_dragging_over      &= is_dragging_something_else;
    is_dragging_over      &= (no_active_drag_over || should_steal_drag_over);
    if (is_dragging_over)
    {
        PW_ASSERT(ctx->uid_drag_over != uid);
        events                  |= IMGUI_EVENT_DRAG_ENTER;
        ctx->frame_id_drag_over  = frame_id;
        ctx->uid_drag_over       = uid;
    }

    bool is_dropping  = hover;
    is_dropping      &= ctx->frame.uid_drag_over_end == uid;
    is_dropping      &= ctx->frame.type_mouse_up != IMGUI_MOUSE_BUTTON_NONE;
    if (is_dropping)
        events |= IMGUI_EVENT_DRAG_DROP;

    const bool was_dragging_over         = ctx->uid_last_frame_drag_over == uid;
    const bool is_not_dragged            = ctx->uid_drag != uid;
    const bool has_had_drag_over_stolen  = ctx->frame_id_drag_over > 0 && ctx->frame_id_drag_over < frame_id;
    bool       is_exiting_drag_over      = !hover || has_had_drag_over_stolen;
    is_exiting_drag_over                &= was_dragging_over;
    is_exiting_drag_over                &= is_not_dragged;
    if (is_exiting_drag_over)
    {
        events |= IMGUI_EVENT_DRAG_EXIT;
        if (ctx->uid_drag_over == uid)
        {
            ctx->uid_drag_over      = 0;
            ctx->frame_id_drag_over = 0;
        }
    }

    if (ctx->uid_drag_over == uid)
        events |= IMGUI_EVENT_DRAG_OVER;

    // Mouse down
    bool incoming_mouse_down_event  = ctx->frame.type_mouse_down != IMGUI_MOUSE_BUTTON_NONE;
    incoming_mouse_down_event      &= (bool)(ctx->frame.events & IMGUI_FLAGS_PW_MOUSE_DOWN_EVENTS);
    // incoming_mouse_down_event      &= ctx->frame.uid_mouse_down != 0;
    incoming_mouse_down_event &= ctx->frame.uid_mouse_down == uid;
    incoming_mouse_down_event &= mouse_down;

    enum ImguiMouseButtonType handle_mouse_down = IMGUI_MOUSE_BUTTON_NONE;
    if (incoming_mouse_down_event)
    {
        PW_ASSERT(ctx->frame.type_mouse_down != IMGUI_MOUSE_BUTTON_NONE);
        handle_mouse_down = ctx->frame.type_mouse_down;
    }
    switch (handle_mouse_down)
    {
    case IMGUI_MOUSE_BUTTON_NONE:
        break;
    case IMGUI_MOUSE_BUTTON_LEFT:
        events |= IMGUI_EVENT_MOUSE_LEFT_DOWN;
        break;
    case IMGUI_MOUSE_BUTTON_RIGHT:
        events |= IMGUI_EVENT_MOUSE_RIGHT_DOWN;
        break;
    case IMGUI_MOUSE_BUTTON_MIDDLE:
        events |= IMGUI_EVENT_MOUSE_MIDDLE_DOWN;
        break;
    }

    // Drag
    bool is_dragging  = ctx->uid_mouse_hold == uid;
    is_dragging      &= ctx->uid_drag == 0; // not dragging anything else
    is_dragging      &= ctx->mouse_hold_type == IMGUI_MOUSE_BUTTON_LEFT;
    if (is_dragging)
    {
        float distance_x = fabsf(ctx->pos_mouse_down.x - ctx->pos_mouse_move.x);
        float distance_y = fabsf(ctx->pos_mouse_down.y - ctx->pos_mouse_move.y);
        float distance_r = hypotf(distance_x, distance_y);
        if (distance_r > 5) // Drag threshold
        {
            PW_ASSERT(ctx->uid_drag != uid);
            events        |= IMGUI_EVENT_DRAG_BEGIN;
            ctx->uid_drag  = uid;
        }
    }

    // Only send event when mouse actually moved.
    // This helps to guarantee the assiciated mods are also correct
    if (ctx->uid_drag == uid && ctx->frame.events & (1 << PW_EVENT_MOUSE_MOVE))
        events |= IMGUI_EVENT_DRAG_MOVE;

    if (ctx->frame.uid_drag_end == uid)
        events |= IMGUI_EVENT_DRAG_END;

    // Release
    enum ImguiMouseButtonType handle_mouse_up = IMGUI_MOUSE_BUTTON_NONE;
    if (ctx->frame.uid_mouse_up == uid)
    {
        PW_ASSERT(ctx->frame.events & IMGUI_FLAGS_PW_MOUSE_UP_EVENTS);
        PW_ASSERT(ctx->frame.type_mouse_up != IMGUI_MOUSE_BUTTON_NONE);
        handle_mouse_up = ctx->frame.type_mouse_up;
    }

    switch (handle_mouse_up)
    {
    case IMGUI_MOUSE_BUTTON_NONE:
        break;
    case IMGUI_MOUSE_BUTTON_LEFT:
        events |= IMGUI_EVENT_MOUSE_LEFT_UP;
        break;
    case IMGUI_MOUSE_BUTTON_RIGHT:
        events |= IMGUI_EVENT_MOUSE_RIGHT_UP;
        break;
    case IMGUI_MOUSE_BUTTON_MIDDLE:
        events |= IMGUI_EVENT_MOUSE_MIDDLE_UP;
        break;
    }

    enum ImguiMouseButtonType handle_mouse_hold = IMGUI_MOUSE_BUTTON_NONE;
    if (ctx->uid_mouse_hold == uid)
    {
        PW_ASSERT(ctx->mouse_hold_type != IMGUI_MOUSE_BUTTON_NONE);
        handle_mouse_hold = ctx->mouse_hold_type;
    }
    switch (handle_mouse_hold)
    {
    case IMGUI_MOUSE_BUTTON_NONE:
        break;
    case IMGUI_MOUSE_BUTTON_LEFT:
        events |= IMGUI_EVENT_MOUSE_LEFT_HOLD;
        break;
    case IMGUI_MOUSE_BUTTON_RIGHT:
        events |= IMGUI_EVENT_MOUSE_RIGHT_HOLD;
        break;
    case IMGUI_MOUSE_BUTTON_MIDDLE:
        events |= IMGUI_EVENT_MOUSE_MIDDLE_HOLD;
        break;
    }

    // Assert valid states
    PW_ASSERT(
        (events & (IMGUI_EVENT_DRAG_END | IMGUI_EVENT_DRAG_MOVE)) != (IMGUI_EVENT_DRAG_END | IMGUI_EVENT_DRAG_MOVE));
    PW_ASSERT((events & (IMGUI_EVENT_DRAG_MOVE)) ? (ctx->mouse_hold_type != IMGUI_MOUSE_BUTTON_NONE) : true);
    PW_ASSERT((events & (IMGUI_EVENT_MOUSE_EXIT)) ? (events & IMGUI_EVENT_MOUSE_HOVER) == 0 : true);

    return events;
}

static unsigned imgui_get_events_rect(imgui_context* ctx, unsigned uid, const imgui_rect* rect)
{
    bool hover = ctx->mouse_inside_window && imgui_hittest_rect(ctx->pos_mouse_move, rect);
    bool down  = imgui_hittest_rect(ctx->pos_mouse_down, rect);
    bool up    = imgui_hittest_rect(ctx->pos_mouse_up, rect);
    return _imgui_get_events(ctx, uid, hover, down, up);
}

static unsigned imgui_get_events_circle(imgui_context* ctx, unsigned uid, imgui_pt pt, float radius)
{
    bool hover = ctx->mouse_inside_window && imgui_hittest_circle(ctx->pos_mouse_move, pt, radius);
    bool down  = imgui_hittest_circle(ctx->pos_mouse_down, pt, radius);
    bool up    = imgui_hittest_circle(ctx->pos_mouse_up, pt, radius);
    return _imgui_get_events(ctx, uid, hover, down, up);
}

enum ImguiDragType
{
    IMGUI_DRAG_HORIZONTAL_VERTICAL,
    IMGUI_DRAG_HORIZONTAL,
    IMGUI_DRAG_VERTICAL,
};

static void
imgui_drag_value(imgui_context* ctx, float* value, float vmin, float vmax, float range_px, enum ImguiDragType drag_type)
{
    xassert(ctx->uid_mouse_hold); // Are you really dragging right now? Or has your drag ended?
    float delta_x = ctx->pos_mouse_move.x - ctx->mouse_last_drag.x;
    float delta_y = ctx->mouse_last_drag.y - ctx->pos_mouse_move.y;

    ctx->mouse_last_drag.x = ctx->pos_mouse_move.x;
    ctx->mouse_last_drag.y = ctx->pos_mouse_move.y;

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
    if (ctx->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_CTRL)
        delta_px *= 0.1f;
    if (ctx->frame.modifiers_mouse_move & PW_MOD_KEY_SHIFT)
        delta_px *= 0.1f;

    float delta_norm = delta_px / range_px;

    float delta_value  = delta_norm * (vmax - vmin);
    float next_value   = *value;
    next_value        += delta_value;
    if (next_value > vmax)
        next_value = vmax;
    if (next_value < vmin)
        next_value = vmin;

    *value = next_value;
}

static void imgui_begin_frame(imgui_context* ctx)
{
#ifndef NDEBUG
    ctx->duplicate_uid_detector.length = 0;
#endif
}

// Call at the end of every frame after all events have been processed
static void imgui_end_frame(imgui_context* ctx)
{
    ctx->num_duplicate_backbuffers++;

    unsigned events = (1 << PW_EVENT_MOUSE_SCROLL_WHEEL) | (1 << PW_EVENT_MOUSE_TOUCHPAD_BEGIN) |
                      (1 << PW_EVENT_MOUSE_TOUCHPAD_MOVE) | (1 << PW_EVENT_MOUSE_TOUCHPAD_END);
    const bool hover_changed      = ctx->uid_last_frame_mouse_over != ctx->uid_mouse_over;
    const bool drag_hover_changed = ctx->uid_last_frame_drag_over != ctx->uid_drag_over;
    if ((ctx->frame.events & events) || hover_changed || drag_hover_changed)
    {
        ctx->left_click_counter        = 0;
        ctx->num_duplicate_backbuffers = 0;
    }
    if (ctx->frame.uid_mouse_up)
        ctx->num_duplicate_backbuffers = 0;

    ctx->last_width  = ctx->next_width;
    ctx->last_height = ctx->next_height;

    ctx->uid_last_frame_mouse_over = ctx->uid_mouse_over;
    ctx->uid_last_frame_drag_over  = ctx->uid_drag_over;

    memset(&ctx->frame, 0, sizeof(ctx->frame));
}

static void imgui_send_event(imgui_context* ctx, const PWEvent* e)
{
    ctx->num_duplicate_backbuffers  = 0;
    ctx->frame.events              |= 1 << e->type;

    switch (e->type)
    {
    case PW_EVENT_RESIZE:
        ctx->next_width  = e->resize.width;
        ctx->next_height = e->resize.height;
        break;

    case PW_EVENT_MOUSE_EXIT:
        // ctx->mouse_over_id = 0;
        // // NOTE: When dragging files out from you GUI (eg. pw_drag_files), Windows will immediately send a
        // // WM_MOUSE_LEAVE event and your window looses its focus, even while your cursor is still directly over
        // // whichever widget initiated the drag. This means we have to clear any mouse down & drag information
        // ctx->mouse_left_down_id  = 0;
        // ctx->mouse_right_down_id = 0;
        // ctx->mouse_drag_id       = 0;

        ctx->uid_mouse_over      = 0;
        ctx->frame_id_mouse_over = 0;
        ctx->uid_mouse_hold      = 0;
        ctx->uid_drag            = 0;
        ctx->uid_drag_over       = 0;
        ctx->uid_touchpad        = 0;
        ctx->mouse_inside_window = false;
        break;
    case PW_EVENT_MOUSE_ENTER:
        ctx->pos_mouse_move.x    = e->mouse.x;
        ctx->pos_mouse_move.y    = e->mouse.y;
        ctx->frame_id_mouse_over = 0;
        ctx->uid_mouse_over      = 0;
        ctx->mouse_inside_window = true;
        break;
    case PW_EVENT_MOUSE_MOVE:
        ctx->pos_mouse_move.x            = e->mouse.x;
        ctx->pos_mouse_move.y            = e->mouse.y;
        ctx->frame.modifiers_mouse_move |= e->mouse.modifiers;
        break;
    case PW_EVENT_MOUSE_SCROLL_WHEEL:
        ctx->frame.delta_mouse_wheel      += (int)(e->mouse.y / 120.0f);
        ctx->frame.modifiders_mouse_wheel |= e->mouse.modifiers;
        break;
    case PW_EVENT_MOUSE_TOUCHPAD_BEGIN:
    case PW_EVENT_MOUSE_TOUCHPAD_MOVE:
    case PW_EVENT_MOUSE_TOUCHPAD_END:
        ctx->frame.delta_touchpad.x   += e->mouse.x;
        ctx->frame.delta_touchpad.y   += e->mouse.y;
        ctx->frame.modifiers_touchpad |= e->mouse.modifiers;
        break;
    case PW_EVENT_MOUSE_LEFT_DOWN:
    case PW_EVENT_MOUSE_RIGHT_DOWN:
    case PW_EVENT_MOUSE_MIDDLE_DOWN:
    {
        ctx->pos_mouse_move.x = e->mouse.x;
        ctx->pos_mouse_move.y = e->mouse.y;

        enum ImguiMouseButtonType btn_type = IMGUI_MOUSE_BUTTON_NONE;
        if (e->type == PW_EVENT_MOUSE_LEFT_DOWN)
            btn_type = IMGUI_MOUSE_BUTTON_LEFT;
        else if (e->type == PW_EVENT_MOUSE_RIGHT_DOWN)
            btn_type = IMGUI_MOUSE_BUTTON_RIGHT;
        else if (e->type == PW_EVENT_MOUSE_MIDDLE_DOWN)
            btn_type = IMGUI_MOUSE_BUTTON_MIDDLE;

        if (ctx->frame.type_mouse_down == IMGUI_MOUSE_BUTTON_NONE || ctx->frame.type_mouse_down == btn_type)
        {
            const bool valid_state_1 = ctx->uid_mouse_hold == 0 && ctx->mouse_hold_type == IMGUI_MOUSE_BUTTON_NONE;
            const bool valid_state_2 = ctx->uid_mouse_hold != 0 && ctx->mouse_hold_type == IMGUI_MOUSE_BUTTON_NONE;
            PW_ASSERT(valid_state_1 || valid_state_2);
            ctx->mouse_hold_type       = btn_type;
            ctx->frame.type_mouse_down = btn_type;
            ctx->uid_mouse_hold        = ctx->uid_mouse_over;
            ctx->frame.uid_mouse_down  = ctx->uid_mouse_over;

            ctx->pos_mouse_down.x  = e->mouse.x;
            ctx->pos_mouse_down.y  = e->mouse.y;
            ctx->mouse_last_drag.x = e->mouse.x;
            ctx->mouse_last_drag.y = e->mouse.y;

            ctx->frame.modifiers_mouse_down |= e->mouse.modifiers;

            if (e->type == PW_EVENT_MOUSE_LEFT_DOWN)
            {
                unsigned diff = e->mouse.time_ms - ctx->last_left_click_time;
                if (diff > e->mouse.double_click_interval_ms)
                    ctx->left_click_counter = 0;
                if (ctx->left_click_counter >= 3)
                    ctx->left_click_counter = 0;

                ctx->left_click_counter++;
                ctx->last_left_click_time = e->mouse.time_ms;
            }
        }

        break;
    }
    case PW_EVENT_MOUSE_LEFT_UP:
    case PW_EVENT_MOUSE_RIGHT_UP:
    case PW_EVENT_MOUSE_MIDDLE_UP:
    {
        ctx->pos_mouse_move.x = e->mouse.x;
        ctx->pos_mouse_move.y = e->mouse.y;

        enum ImguiMouseButtonType btn_type = IMGUI_MOUSE_BUTTON_NONE;
        if (e->type == PW_EVENT_MOUSE_LEFT_UP)
            btn_type = IMGUI_MOUSE_BUTTON_LEFT;
        else if (e->type == PW_EVENT_MOUSE_RIGHT_UP)
            btn_type = IMGUI_MOUSE_BUTTON_RIGHT;
        else if (e->type == PW_EVENT_MOUSE_MIDDLE_UP)
            btn_type = IMGUI_MOUSE_BUTTON_MIDDLE;

        if (ctx->mouse_hold_type == btn_type)
        {
            ctx->frame.type_mouse_up     = ctx->mouse_hold_type;
            ctx->frame.uid_mouse_up      = ctx->uid_mouse_hold;
            ctx->frame.uid_drag_end      = ctx->uid_drag;
            ctx->frame.uid_drag_over_end = ctx->uid_drag_over;

            ctx->uid_mouse_hold     = 0;
            ctx->uid_drag           = 0;
            ctx->uid_drag_over      = 0;
            ctx->frame_id_drag_over = 0;
            ctx->mouse_hold_type    = IMGUI_MOUSE_BUTTON_NONE;

            ctx->pos_mouse_up.x            = e->mouse.x;
            ctx->pos_mouse_up.y            = e->mouse.y;
            ctx->frame.modifiers_mouse_up |= e->mouse.modifiers;
        }
        break;
    }

    // TODO
    case PW_EVENT_DPI_CHANGED:
    case PW_EVENT_KEY_DOWN:
    case PW_EVENT_KEY_UP:
    case PW_EVENT_TEXT:
    case PW_EVENT_KEY_FOCUS_LOST:
    case PW_EVENT_FILE_ENTER:
    case PW_EVENT_FILE_MOVE:
    case PW_EVENT_FILE_DROP:
    case PW_EVENT_FILE_EXIT:
        break;
    }
}