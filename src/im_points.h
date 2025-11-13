#ifndef IM_POINTS_H
#define IM_POINTS_H

#include <xhl/vector.h>

enum
{
    IMP_DEFAULT_POINT_CLICK_RADIUS = 12,
    IMP_DEFAULT_POINT_RADIUS       = 4,
    IMP_DEFAULT_SKEW_POINT_RADIUS  = 3,
    IMP_DEFAULT_SKEW_DRAG_RANGE    = 250,

    IMP_DEFAULT_POINT_DRAG_ERASE_DISTANCE = 24,
};

typedef enum IMPShapeType
{
    IMP_SHAPE_POINT,
    IMP_SHAPE_FLAT,
    IMP_SHAPE_LINEAR_ASC,
    IMP_SHAPE_LINEAR_DESC,
    IMP_SHAPE_CONVEX_ASC,
    IMP_SHAPE_CONCAVE_DESC,
    IMP_SHAPE_CONCAVE_ASC,
    IMP_SHAPE_CONVEX_DESC,
    IMP_SHAPE_COSINE_ASC,
    IMP_SHAPE_COSINE_DESC,
    IMP_SHAPE_TRIANGLE_UP,
    IMP_SHAPE_TRIANGLE_DOWN,
    IMP_SHAPE_COUNT,
} IMPShapeType;

typedef struct IMPointsArea
{
    float x, y, r, b;
} IMPointsArea;

typedef struct IMPointsData
{
    IMPointsArea area;

    // If false, should copy over the points array from the audio thread to the main thread
    bool main_points_valid;
    // Used to queue changes made to points on the audio thread
    xvec3f* main_points;

    // Draggable points (widgets)
    // Cordinates are in window space
    bool    points_valid; // Set to false to copy main_points > points
    xvec2f* points;
    xvec2f* skew_points;
    // Used as backup while doing non-destructive preview editing of points
    xvec2f* points_copy;
    xvec2f* skew_points_copy;

    // Point multiselect
    xvec2f selection_start;
    xvec2f selection_end;
    int*   selected_point_indexes;
    // Used for hacks to make the current selection & hover work properly when previewing edits to points with the
    // drag-auto-erase feature
    int selected_point_idx;

    xvec2f* path_cache;

    struct
    {
        uint32_t col_line;
        float    line_stroke_width;

        uint32_t col_point_hover_bg;

        uint32_t col_skewpoint_inner;
        uint32_t col_skewpoint_outer;
        float    skewpoint_stroke_width;

        uint32_t col_point;
        uint32_t col_point_selected;
        uint32_t col_selection_box;
    } theme;
} IMPointsData;

typedef struct IMPointsFrameContext
{
    IMPointsData*         imp;   // not owned
    struct NVGcontext*    nvg;   // not owned
    struct imgui_context* im;    // not owned
    struct LinkedArena*   arena; // not owned
    void*                 pw;    // not owned

    bool should_update_cached_path;
    bool should_update_main_points_with_points;
    bool should_update_audio_lfo_points_with_main_points;
    int  pt_hover_idx;
    int  pt_hover_skew_idx;
    int  delete_pt_idx;
} IMPointsFrameContext;

static IMPointsFrameContext imp_frame_context_new(
    IMPointsData*         imp,
    struct NVGcontext*    nvg,
    struct imgui_context* im,
    struct LinkedArena*   arena,
    void*                 pw)
{
    IMPointsFrameContext framestate = {0};
    framestate.imp                  = imp;
    framestate.nvg                  = nvg;
    framestate.im                   = im;
    framestate.arena                = arena;
    framestate.pw                   = pw;

    framestate.pt_hover_idx      = -1;
    framestate.pt_hover_skew_idx = -1;
    framestate.delete_pt_idx     = -1;
    return framestate;
}

void imp_deinit(IMPointsData*);

void imp_clear_selection(IMPointsData* imp);

void imp_handle_point_events(IMPointsFrameContext* fstate, int num_grid_x, int num_grid_y);
void imp_handle_grid_events(
    IMPointsFrameContext* fstate,
    IMPointsArea          selection_area,
    int                   num_grid_x,
    int                   num_grid_y,
    IMPShapeType          selected_shape);

void imp_draw_points(IMPointsFrameContext* fstate);

#endif // IM_POINTS_H

/*
██╗███╗   ███╗██████╗ ██╗     ███████╗███╗   ███╗███████╗███╗   ██╗████████╗ █████╗ ████████╗██╗ ██████╗ ███╗   ██╗
██║████╗ ████║██╔══██╗██║     ██╔════╝████╗ ████║██╔════╝████╗  ██║╚══██╔══╝██╔══██╗╚══██╔══╝██║██╔═══██╗████╗  ██║
██║██╔████╔██║██████╔╝██║     █████╗  ██╔████╔██║█████╗  ██╔██╗ ██║   ██║   ███████║   ██║   ██║██║   ██║██╔██╗ ██║
██║██║╚██╔╝██║██╔═══╝ ██║     ██╔══╝  ██║╚██╔╝██║██╔══╝  ██║╚██╗██║   ██║   ██╔══██║   ██║   ██║██║   ██║██║╚██╗██║
██║██║ ╚═╝ ██║██║     ███████╗███████╗██║ ╚═╝ ██║███████╗██║ ╚████║   ██║   ██║  ██║   ██║   ██║╚██████╔╝██║ ╚████║
╚═╝╚═╝     ╚═╝╚═╝     ╚══════╝╚══════╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═══╝   ╚═╝   ╚═╝  ╚═╝   ╚═╝   ╚═╝ ╚═════╝ ╚═╝  ╚═══╝
*/

#ifdef IM_POINTS_IMPL
#undef IM_POINTS_IMPL

#include "dsp.h"
#include <imgui.h>
#include <nanovg2.h>
#include <sort.h>
#include <xhl/array.h>
#include <xhl/maths.h>

void imp_deinit(IMPointsData* imp)
{
    xarr_free(imp->main_points);
    xarr_free(imp->points);
    xarr_free(imp->skew_points);
    xarr_free(imp->path_cache);
    xarr_free(imp->selected_point_indexes);
    xarr_free(imp->points_copy);
    xarr_free(imp->skew_points_copy);
}

void imp_clear_selection(IMPointsData* imp)
{
    xarr_setlen(imp->selected_point_indexes, 0);
    imp->selected_point_idx = -1;
}

float _imp_calculate_point_skew(IMPointsData* imp, int idx)
{
    size_t num_points      = xarr_len(imp->points);
    size_t num_skew_points = xarr_len(imp->skew_points);
    size_t next_idx        = idx + 1;

    xassert(idx >= 0 && idx < num_points);
    xassert(idx >= 0 && next_idx < num_points);
    xassert(idx >= 0 && idx < num_skew_points);

    if (next_idx >= num_points)
        next_idx = 0;

    const xvec2f* pt      = imp->points + idx;
    const xvec2f* next_pt = imp->points + next_idx;
    const xvec2f* skew_pt = imp->skew_points + idx;

    float skew = 0.5f;
    if (pt->y != next_pt->y)
        skew = xm_normf(skew_pt->y, next_pt->y, pt->y);
    if (pt->y > next_pt->y)
        skew = 1 - skew;

    skew = xm_clampf(skew, 0, 1);
    return skew;
}

void _imp_add_to_selection(IMPointsData* imp, int idx)
{
    int num_points = xarr_len(imp->points);
    if (idx == num_points - 1)
        idx = 0;

    int       i;
    const int N = xarr_len(imp->selected_point_indexes);
    for (i = 0; i < N; i++)
    {
        if (imp->selected_point_indexes[i] == idx)
            break;
    }
    if (i == N) // idx not in array
    {
        xarr_push(imp->selected_point_indexes, idx);
        sort_int(imp->selected_point_indexes, N + 1);

        int num_selected_pts = xarr_len(imp->selected_point_indexes);
        xassert(num_selected_pts > 0);
        if (num_selected_pts == 1)
            imp->selected_point_idx = idx;
        else
            imp->selected_point_idx = -1;
    }
}

void _imp_update_skew_point(IMPointsData* imp, int i, float skew)
{
    xassert(i < xarr_len(imp->skew_points));
    xassert(i < xarr_len(imp->points) - 1);
    if (imp->points[i].x == imp->points[i + 1].x) // the line between point & next point is vertical
    {
        imp->skew_points[i].x = imp->points[i].x;
        // display skew point vertically, halfway between points
        // skew amount not considered
        imp->skew_points[i].y = (imp->points[i].y + imp->points[i + 1].y) * 0.5f;
    }
    else
    {
        // x is always halfway between points
        imp->skew_points[i].x = (imp->points[i].x + imp->points[i + 1].x) * 0.5f;
        // skew amount controls y coord
        const xvec2f* pt1 = imp->points + i;
        const xvec2f* pt2 = imp->points + i + 1;
        const float   y   = interp_points(0.5f, 1 - skew, pt1->y, pt2->y);

        imp->skew_points[i].y = y;
    }
}

// Clamps target_pos to boundaries. Updates relevant skew points
void _imp_update_point(IMPointsData* imp, xvec2f pos, int idx)
{
    const size_t num_points = xarr_len(imp->points);

    xvec2f range_horizontal;

    if (idx == 0)
        range_horizontal.left = imp->area.x;
    else
        range_horizontal.left = imp->points[idx - 1].x;

    if (idx == 0)
        range_horizontal.right = imp->area.x;
    else if (idx == num_points - 1)
        range_horizontal.right = imp->area.r;
    else
        range_horizontal.right = imp->points[idx + 1].x;

    float i_skew        = _imp_calculate_point_skew(imp, idx);
    float prev_skew     = 0;
    float last_skew     = 0;
    int   last_skew_idx = (int)xarr_len(imp->skew_points) - 1;
    if (idx > 0)
        prev_skew = _imp_calculate_point_skew(imp, idx - 1);
    if (idx == 0)
        last_skew = _imp_calculate_point_skew(imp, last_skew_idx);

    xvec2f* pt = &imp->points[idx];
    pt->x      = xm_clampf(pos.x, range_horizontal.l, range_horizontal.r);
    pt->y      = xm_clampf(pos.y, imp->area.y, imp->area.b);

    if (idx == 0)
        imp->points[num_points - 1].y = pt->y;

    _imp_update_skew_point(imp, idx, i_skew);
    if (idx > 0)
        _imp_update_skew_point(imp, idx - 1, prev_skew);

    if (idx == 0)
    {
        _imp_update_skew_point(imp, last_skew_idx, last_skew);
    }
}

void _imp_insert_point(IMPointsData* imp, xvec2f pos, int idx)
{
    const int prev_idx = idx - 1;

#ifndef NDEBUG
    size_t gui_pt_len = xarr_len(imp->points);
    xassert(idx > 0);
    xassert(idx < gui_pt_len);
    xassert(prev_idx >= 0);
    xvec2f prev_pt = imp->points[prev_idx];
    xassert(prev_pt.x <= pos.x);
#endif

    float skew = _imp_calculate_point_skew(imp, prev_idx);

    // add points locally
    xarr_insert(imp->points, idx, pos);
    xarr_insert(imp->skew_points, prev_idx, pos);

    _imp_update_skew_point(imp, prev_idx, skew);
    if (idx < xarr_len(imp->skew_points))
        _imp_update_skew_point(imp, idx, 0.5f);
}

void _imp_delete_point(IMPointsData* imp, int idx)
{
    xassert(idx > 0);
    xassert(idx != xarr_len(imp->skew_points));
    xassert(xarr_len(imp->skew_points) == (xarr_len(imp->points) - 1));

    xarr_delete(imp->points, idx);
    xarr_delete(imp->skew_points, idx - 1);
    // when user clears the "last point", reset neighbouring skew amounts
    _imp_update_skew_point(imp, idx - 1, 0.5f);
}

void _imp_save_points_to_copy(IMPointsData* imp)
{
    int N = xarr_len(imp->points);
    xarr_setlen(imp->points_copy, N);
    memcpy(imp->points_copy, imp->points, N * sizeof(*imp->points_copy));
}

void _imp_save_skew_points_to_copy(IMPointsData* imp)
{
    int N = xarr_len(imp->skew_points);
    xarr_setlen(imp->skew_points_copy, N);
    memcpy(imp->skew_points_copy, imp->skew_points, N * sizeof(*imp->skew_points_copy));
}

void _imp_drag_and_draw(
    IMPointsData* imp,
    imgui_pt      pos,
    bool          snap_to_grid,
    IMPShapeType  selected_shape,
    int           num_grid_x,
    int           num_grid_y)
{
    xassert(selected_shape >= 0 && selected_shape < IMP_SHAPE_COUNT);

    imp->selection_start.u64 = 0;
    imp->selection_end.u64   = 0;
    imp_clear_selection(imp);

    const float area_width  = imp->area.r - imp->area.x;
    const float area_height = imp->area.b - imp->area.y;

    const float x_inc = area_width / (float)num_grid_x;
    const float y_inc = area_height / (float)num_grid_y;

    int grid_idx_left    = (int)((pos.x - imp->area.x) / x_inc);
    grid_idx_left        = xm_clampi(grid_idx_left, 0, num_grid_x - 1);
    float boundary_left  = imp->area.x + grid_idx_left * x_inc;
    float boundary_right = imp->area.x + (grid_idx_left + 1) * x_inc;

    float y = pos.y;
    if (snap_to_grid)
    {
        for (int j = 0; j <= num_grid_y; j++)
        {
            float snap_y = imp->area.y + j * y_inc;
            if (snap_y - IMP_DEFAULT_POINT_CLICK_RADIUS <= pos.y && pos.y <= snap_y + IMP_DEFAULT_POINT_CLICK_RADIUS)
            {
                y = snap_y;
                break;
            }
        }
    }

    y = xm_clampf(y, imp->area.y, imp->area.b);

    // New points at grid boundary
    float pt_y_left = y, pt_y_right = y;
    switch (selected_shape)
    {
    case IMP_SHAPE_POINT:
    case IMP_SHAPE_FLAT:
        break;
    case IMP_SHAPE_LINEAR_ASC:
    case IMP_SHAPE_CONVEX_ASC:
    case IMP_SHAPE_CONCAVE_ASC:
    case IMP_SHAPE_COSINE_ASC:
        pt_y_left = imp->area.b;
        break;
    case IMP_SHAPE_LINEAR_DESC:
    case IMP_SHAPE_CONVEX_DESC:
    case IMP_SHAPE_CONCAVE_DESC:
    case IMP_SHAPE_COSINE_DESC:
        pt_y_right = imp->area.b;
        break;

    case IMP_SHAPE_TRIANGLE_UP:
        pt_y_left  = imp->area.b;
        pt_y_right = imp->area.b;
        break;
    case IMP_SHAPE_TRIANGLE_DOWN:
        pt_y_left  = imp->area.y;
        pt_y_right = imp->area.y;
        break;
    case IMP_SHAPE_COUNT:
        xassert(false);
        break;
    }

    // Delete points inside boundary range
    {
        const int num_points = xarr_len(imp->points); // note, len(imp->points) is the same as len(lfo->skew_points) + 1
        for (int i = num_points - 1; i-- != 1;)
        {
            xassert(i > 0);
            bool between  = boundary_left < imp->points[i].x;
            between      &= imp->points[i].x < boundary_right;

            if (between)
                _imp_delete_point(imp, i);
        }
    }

    // Count points at boundaries
    int num_points                   = xarr_len(imp->points);
    int right_idx                    = -1;
    int num_points_at_right_boundary = 0;
    int left_idx                     = -1;
    int num_points_at_left_boundary  = 0;

    // Count right
    {
        for (int i = num_points; i-- != 0;)
        {
            if (imp->points[i].x == boundary_right)
            {
                num_points_at_right_boundary++;
            }
            if (imp->points[i].x < boundary_right)
            {
                right_idx = i + 1;
                break;
            }
        }
        xassert(right_idx >= 0);
    }

    // Count left
    {
        for (int i = 0; i < num_points; i++)
        {
            if (imp->points[i].x == boundary_left)
            {
                num_points_at_left_boundary++;
            }
            if (imp->points[i].x > boundary_left)
            {
                left_idx = i - 1;
                break;
            }
        }
        xassert(left_idx >= 0);
    }

    // If there are no points at the left & right boundaries, calculate the existing Y values at those points and insert
    // them as points
    {
        float interp_y_right = 0, interp_y_left = 0;
        if (num_points_at_right_boundary == 0)
        {
            float skew = _imp_calculate_point_skew(imp, right_idx - 1);

            const xvec2f* pt      = imp->points + right_idx - 1;
            const xvec2f* next_pt = imp->points + right_idx;
            const xvec2f* skew_pt = imp->skew_points + right_idx - 1;

            float amt      = xm_normf(boundary_right, pt->x, next_pt->x);
            interp_y_right = interp_points(amt, 1 - skew, pt->y, next_pt->y);
        }

        if (num_points_at_left_boundary == 0)
        {
            float skew = _imp_calculate_point_skew(imp, left_idx);

            const xvec2f* pt      = imp->points + left_idx;
            const xvec2f* next_pt = imp->points + left_idx + 1;
            const xvec2f* skew_pt = imp->skew_points + left_idx;

            float amt     = xm_normf(boundary_left, pt->x, next_pt->x);
            interp_y_left = interp_points(amt, 1 - skew, pt->y, next_pt->y);
        }

        if (num_points_at_right_boundary == 0)
        {
            xvec2f pt = {boundary_right, interp_y_right};
            _imp_insert_point(imp, pt, right_idx);
            num_points++;
            num_points_at_right_boundary++;
        }
        if (num_points_at_left_boundary == 0)
        {
            xvec2f pt = {boundary_left, interp_y_left};
            _imp_insert_point(imp, pt, left_idx + 1);
            num_points++;
            num_points_at_left_boundary++;
            left_idx++;
            right_idx++;
        }
    }

    if (num_points_at_right_boundary == 1)
    {
        xvec2f pt = {boundary_right, pt_y_right};
        _imp_insert_point(imp, pt, right_idx);
        num_points++;
        num_points_at_right_boundary++;
    }
    else
    {
        xassert(num_points_at_right_boundary >= 2);
        xvec2f pt = {boundary_right, pt_y_right};
        _imp_update_point(imp, pt, right_idx);
    }

    if (num_points_at_left_boundary == 1)
    {
        xvec2f prev_pt = imp->points[left_idx];
        xvec2f pt      = {boundary_left, pt_y_left};
        if (prev_pt.x != pt.x || prev_pt.y != pt.y)
        {
            _imp_insert_point(imp, pt, left_idx + 1);
            num_points++;
            num_points_at_left_boundary++;
            left_idx++;
        }
    }
    else
    {
        xassert(num_points_at_left_boundary >= 2);
        xvec2f pt = {boundary_left, pt_y_left};
        _imp_update_point(imp, pt, left_idx);
    }
    // xassert(num_points_at_right_boundary == 2);

    if (selected_shape == IMP_SHAPE_LINEAR_ASC || selected_shape == IMP_SHAPE_LINEAR_DESC)
    {
        _imp_update_skew_point(imp, left_idx, 0.5);
    }
    if (selected_shape == IMP_SHAPE_CONVEX_ASC || selected_shape == IMP_SHAPE_CONVEX_DESC)
    {
        _imp_update_skew_point(imp, left_idx, 0.85);
    }
    if (selected_shape == IMP_SHAPE_CONCAVE_ASC || selected_shape == IMP_SHAPE_CONCAVE_DESC)
    {
        _imp_update_skew_point(imp, left_idx, 0.15);
    }

    if (selected_shape == IMP_SHAPE_TRIANGLE_UP || selected_shape == IMP_SHAPE_TRIANGLE_DOWN)
    {
        xvec2f pt = {boundary_left + x_inc * 0.5f, y};
        _imp_insert_point(imp, pt, left_idx + 1);
        num_points++;
        left_idx++;
    }

    if (selected_shape == IMP_SHAPE_COSINE_ASC || selected_shape == IMP_SHAPE_COSINE_DESC)
    {
        // Approximation for a descening cosing shape
        // x = (1 / 7), y = 0.95, skew = 0.731994
        // x = 0.3333,  y = 0.75, skew = 0.56
        // x = 0.6666,  y = 0.25, skew = 0
        // x = (6 / 7), y = 0.05, skew = 0.44
        // x = 1,       y = 0,    skew = 1 - 0.731994
        xvec2f pts[] = {
            {(1.0f / 7.0f), 0.95f},
            {(1.0f / 3.0f), 0.75f},
            {(2.0f / 3.0f), 0.25f},
            {(6.0f / 7.0f), 0.05f},
        };
        float skews[] = {
            0.731994f,
            0.56,
            0.5,
            0.46,
            1 - 0.731994,
        };
        _Static_assert(ARRLEN(pts) == ARRLEN(skews) - 1, "");
        if (selected_shape == IMP_SHAPE_COSINE_ASC)
        {
            for (int i = 0; i < ARRLEN(pts); i++)
            {
                xvec2f* pt = pts + i;
                pt->y      = 1 - pt->y;
            }
            for (int i = 0; i < ARRLEN(skews); i++)
                skews[i] = 1 - skews[i];
        }

        for (int i = 0; i < ARRLEN(pts); i++)
        {
            xvec2f* pt = pts + i;
            pt->x      = xm_lerpf(pt->x, boundary_left, boundary_right);
            pt->y      = xm_lerpf(pt->y, imp->area.b, y);
            _imp_insert_point(imp, *pt, left_idx + 1);
            _imp_update_skew_point(imp, left_idx, skews[i]);
            num_points++;
            left_idx++;
        }
        _imp_update_skew_point(imp, left_idx, skews[ARRLEN(skews) - 1]);
    }

    _imp_save_points_to_copy(imp);
    _imp_save_skew_points_to_copy(imp);
}

void imp_handle_point_events(IMPointsFrameContext* fstate, int num_grid_x, int num_grid_y)
{
    IMPointsData*  imp = fstate->imp;
    imgui_context* im  = fstate->im;
    xassert(fstate->imp);
    xassert(fstate->im);
    xassert(fstate->pw);
    xassert(fstate->pw);
    xassert(fstate->arena);
    xassert(imp->area.r > imp->area.x);
    xassert(imp->area.b > imp->area.y);
    LINKED_ARENA_LEAK_DETECT_BEGIN(fstate->arena);

    // Point events
    {
        const int num_points    = xarr_len(imp->points_copy);
        bool      backup_points = false;

        for (int pt_idx = 0; pt_idx < num_points; pt_idx++)
        {
            unsigned uid = 'lfop' + pt_idx;

            // const xvec2f pt        = imp->points_copy[pt_idx];

            // Properly track point position
            // We're reading out of an array of points cached at the beginning of a drag, and not the points that are
            // actually displayed
            imgui_pt       pt        = (im->uid_mouse_hold == uid || im->frame.uid_mouse_up == uid)
                                           ? im->pos_mouse_move
                                           : (imgui_pt){imp->points_copy[pt_idx].x, imp->points_copy[pt_idx].y};
            const unsigned pt_events = imgui_get_events_circle(im, uid, pt, IMP_DEFAULT_POINT_CLICK_RADIUS);

            if (pt_events == 0)
                continue;

            if (pt_events & IMGUI_EVENT_MOUSE_HOVER)
            {
                fstate->pt_hover_idx = pt_idx;
                if (imp->selected_point_idx != -1 && pt_events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
                    fstate->pt_hover_idx = imp->selected_point_idx;
            }

            if (pt_events & IMGUI_EVENT_MOUSE_ENTER)
            {
                pw_set_mouse_cursor(fstate->pw, PW_CURSOR_HAND_DRAGGABLE);
            }

            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                int select_idx = pt_idx;
                if ((pt_idx + 1) == num_points)
                    select_idx = 0;

                if (im->left_click_counter == 1)
                {
                    bool shift_click     = (PW_MOD_KEY_SHIFT | PW_MOD_LEFT_BUTTON) == im->frame.modifiers_mouse_down;
                    bool idx_is_selected = false;
                    for (int i = 0; i < xarr_len(imp->selected_point_indexes); i++)
                    {
                        if (select_idx == imp->selected_point_indexes[i])
                        {
                            idx_is_selected = true;
                            break;
                        }
                    }
                    if (shift_click == false && idx_is_selected == false)
                    {
                        imp_clear_selection(imp);
                    }

                    _imp_add_to_selection(imp, select_idx);
                    fstate->pt_hover_idx = select_idx;
                    int num_selected_pts = xarr_len(imp->selected_point_indexes);
                    xassert(num_selected_pts > 0);
                    pw_set_mouse_cursor(fstate->pw, PW_CURSOR_HAND_DRAGGING);
                }
                else if (im->left_click_counter == 2 && select_idx > 0)
                {
                    fstate->delete_pt_idx = pt_idx;
                    imgui_clear_widget(im);
                }
            }
            if (pt_events & IMGUI_EVENT_DRAG_BEGIN)
            {
                xarr_setlen(imp->points_copy, num_points);
                xarr_setlen(imp->skew_points_copy, (num_points - 1));
                xassert(0 == memcmp(imp->points_copy, imp->points, sizeof(*imp->points_copy) * num_points));
                xassert(
                    0 ==
                    memcmp(imp->skew_points_copy, imp->skew_points, sizeof(*imp->skew_points_copy) * (num_points - 1)));
            }
            if (pt_events & IMGUI_EVENT_DRAG_MOVE)
            {
                const int num_selected = xarr_len(imp->selected_point_indexes);

                if (num_selected == 1)
                {
                    const bool alt_drag     = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_ALT;
                    const bool snap_to_grid = alt_drag && (num_selected == 1);

                    xvec2f drag_pos = (xvec2f){im->pos_mouse_move.x, im->pos_mouse_move.y};
                    drag_pos.y      = xm_clampf(drag_pos.y, imp->area.y, imp->area.b);
                    drag_pos.x      = xm_clampf(drag_pos.x, imp->area.x, imp->area.r);
                    if (snap_to_grid)
                    {
                        float x_inc = (imp->area.r - imp->area.x) / num_grid_x;
                        float y_inc = (imp->area.b - imp->area.y) / num_grid_y;
                        for (int j = 0; j <= num_grid_x; j++)
                        {
                            float x = imp->area.x + j * x_inc;
                            if (x - IMP_DEFAULT_POINT_CLICK_RADIUS <= drag_pos.x &&
                                drag_pos.x <= x + IMP_DEFAULT_POINT_CLICK_RADIUS)
                            {
                                drag_pos.x = x;
                                break;
                            }
                        }
                        for (int j = 0; j <= num_grid_y; j++)
                        {
                            float y = imp->area.y + j * y_inc;
                            if (y - IMP_DEFAULT_POINT_CLICK_RADIUS <= drag_pos.y &&
                                drag_pos.y <= y + IMP_DEFAULT_POINT_CLICK_RADIUS)
                            {
                                drag_pos.y = y;
                                break;
                            }
                        }
                    }

                    // Points on edges cannot be dragged past other points
                    const int sel_idx = imp->selected_point_indexes[0];
                    if (sel_idx == 0 || sel_idx == num_points - 1)
                    {
                        _imp_update_point(imp, drag_pos, sel_idx);
                    }
                    else
                    {
                        // Rebuid points array, skipping any points between the beginning and current drag position
                        float range_l = xm_minf(drag_pos.x, im->pos_mouse_down.x);
                        float range_r = xm_maxf(drag_pos.x, im->pos_mouse_down.x);
                        range_l       = xm_maxf(range_l, imp->area.x);
                        range_r       = xm_minf(range_r, imp->area.r);

                        const float clamp_range_l = range_l + IMP_DEFAULT_POINT_DRAG_ERASE_DISTANCE;
                        const float clamp_range_r = range_r - IMP_DEFAULT_POINT_DRAG_ERASE_DISTANCE;

                        float* skew_amts = linked_arena_alloc(fstate->arena, sizeof(*skew_amts) * num_points);

                        int npoints             = 0;
                        imp->points[npoints++]  = imp->points_copy[0];
                        imp->selected_point_idx = -1;

                        xvec2f(*view_pts)[512]      = (void*)imp->points;
                        xvec2f(*view_skew_pts)[512] = (void*)imp->skew_points;
                        xvec2f(*view_src_pts)[512]  = (void*)imp->points_copy;

                        for (int j = 1; j < num_points; j++)
                        {
                            float skew = 0.5f;
                            // Calc skew amount from cached points
                            const xvec2f* p1 = imp->points_copy + j - 1;
                            const xvec2f* p2 = imp->points_copy + j;
                            const xvec2f* sp = imp->skew_points_copy + j - 1;
                            if (p1->y != p2->y)
                                skew = xm_normf(sp->y, p2->y, p1->y);
                            if (p1->y > p2->y)
                                skew = 1 - skew;

                            // Update displayed points
                            if (j == sel_idx)
                            {
                                // Defer adding this point so we can give it an opportunity to get clamped
                                imp->selected_point_idx = npoints;
                                npoints++;
                            }
                            else if (j < sel_idx && p2->x <= clamp_range_l)
                            {
                                imp->points[npoints++] = *p2;

                                if (p2->x >= range_l) // Clamp dragged point to nearby point
                                    drag_pos.x = xm_maxf(p2->x, drag_pos.x);
                                xassert(drag_pos.x >= p2->x);
                            }
                            else if (j > sel_idx && p2->x >= clamp_range_r)
                            {
                                imp->points[npoints++] = *p2;
                                if (p2->x <= range_r) // Clamp dragged point to nearby point
                                    drag_pos.x = xm_minf(p2->x, drag_pos.x);
                                xassert(drag_pos.x <= p2->x);
                            }
                            else
                            {
                                continue;
                            }

                            xassert((npoints - 2) >= 0);
                            skew_amts[npoints - 2] = skew;
                        }
                        xassert(imp->selected_point_idx != -1);
                        if (imp->selected_point_indexes >= 0)
                        {
                            fstate->pt_hover_idx = imp->selected_point_idx;

                            imp->points[imp->selected_point_idx] = drag_pos;
                        }

                        // Update displayed skew point
                        for (int j = 0; j < npoints - 1; j++)
                        {
                            xassert((j + 1) < npoints);
                            const xvec2f* p1   = imp->points + j;
                            const xvec2f* p2   = imp->points + j + 1;
                            xvec2f*       sp   = imp->skew_points + j;
                            float         skew = skew_amts[j];

                            xassert(p1->x <= p2->x);

                            if (p1->x == p2->x) // the line between point & next_p is vertical
                            {
                                sp->x = p1->x;
                                // display skew point vertically, halfway between points
                                // skew amount not considered
                                sp->y = (p1->y + p2->y) * 0.5f;
                            }
                            else
                            {
                                float y = interp_points(0.5f, 1 - skew, p1->y, p2->y);

                                // x is always halfway between points
                                sp->x = (p1->x + p2->x) * 0.5f;
                                // skew amount controls y coord
                                sp->y = y;
                            }
                        }

                        xarr_header(imp->points)->length      = npoints;
                        xarr_header(imp->skew_points)->length = npoints - 1;

                        linked_arena_release(fstate->arena, skew_amts);
                    }
                }
                else if (num_selected > 1)
                {
                    const xvec2f delta = {
                        im->pos_mouse_move.x - im->pos_mouse_down.x,
                        im->pos_mouse_move.y - im->pos_mouse_down.y};

                    bool has_moved_first_point = false;

                    for (int i = 0; i < num_selected; i++)
                    {
                        int idx = imp->selected_point_indexes[i];

                        xassert(idx != (num_points - 1));

                        bool is_first_point = idx == 0 || idx == (num_points - 1);
                        if (is_first_point && has_moved_first_point)
                            continue;

                        has_moved_first_point |= is_first_point;

                        xvec2f translate_pos  = imp->points_copy[idx];
                        translate_pos.x      += delta.x;
                        translate_pos.y      += delta.y;

                        _imp_update_point(imp, translate_pos, idx);
                    }
                }

                fstate->should_update_cached_path             = true;
                fstate->should_update_main_points_with_points = true;
            } // IMGUI_EVENT_DRAG_MOVE

            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_UP)
            {
                pw_set_mouse_cursor(fstate->pw, PW_CURSOR_HAND_DRAGGABLE);
                backup_points = true;
            }
        } // end loop points

        if (fstate->delete_pt_idx > 0)
        {
            _imp_delete_point(imp, fstate->delete_pt_idx);
            imp_clear_selection(imp);

            fstate->pt_hover_idx      = -1;
            fstate->pt_hover_skew_idx = -1;
            backup_points             = true;

            fstate->should_update_main_points_with_points = true;
            fstate->should_update_cached_path             = true;
        }

        if (backup_points)
        {
            _imp_save_points_to_copy(imp);
            _imp_save_skew_points_to_copy(imp);
        }
    }

    // Skew point events
    {
        const int num_skew_points = xarr_len(imp->skew_points);
        for (int pt_idx = 0; pt_idx < num_skew_points; pt_idx++)
        {
            unsigned       uid = 'lskp' + pt_idx;
            const xvec2f   pt  = imp->skew_points[pt_idx];
            const unsigned pt_events =
                imgui_get_events_circle(im, uid, (imgui_pt){pt.x, pt.y}, IMP_DEFAULT_POINT_CLICK_RADIUS);
            if (pt_events == 0)
                continue;

            if (pt_events & IMGUI_EVENT_MOUSE_HOVER)
                fstate->pt_hover_skew_idx = pt_idx;

            if (pt_events & IMGUI_EVENT_MOUSE_ENTER)
            {
                pw_set_mouse_cursor(fstate->pw, PW_CURSOR_RESIZE_NS);
            }

            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                imp_clear_selection(imp);

                const bool ctrl = im->frame.modifiers_mouse_down & PW_MOD_PLATFORM_KEY_CTRL;
                if (im->left_click_counter == 2 && !ctrl)
                {
                    _imp_update_skew_point(imp, pt_idx, 0.5f);
                    fstate->pt_hover_skew_idx                     = -1;
                    fstate->should_update_main_points_with_points = true;
                    fstate->should_update_cached_path             = true;
                }
            }
            if (pt_events & IMGUI_EVENT_DRAG_MOVE)
            {
                float delta = 0;
                imgui_drag_value(im, &delta, -1, 1, IMP_DEFAULT_SKEW_DRAG_RANGE * 2, IMGUI_DRAG_VERTICAL);

                float skew      = _imp_calculate_point_skew(imp, pt_idx);
                float next_skew = skew + delta;
                next_skew       = xm_clampf(next_skew, 0.0f, 1.0f);
                xassert(next_skew >= 0 && next_skew <= 1);

                _imp_update_skew_point(imp, pt_idx, next_skew);
                fstate->should_update_main_points_with_points = true;
                fstate->should_update_cached_path             = true;
            }
            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_UP)
            {
                _imp_save_skew_points_to_copy(imp);
            }
        }
    }

    LINKED_ARENA_LEAK_DETECT_END(fstate->arena);
}

static inline double _imp_snap_point(double x)
{
    if (x < 0.000001)
        x = 0;
    if (x > 0.999999)
        x = 1;
    return x;
}

void imp_handle_grid_events(
    IMPointsFrameContext* fstate,
    IMPointsArea          selection_area,
    int                   num_grid_x,
    int                   num_grid_y,
    IMPShapeType          selected_shape)
{
    IMPointsData*  imp = fstate->imp;
    imgui_context* im  = fstate->im;
    xassert(fstate->imp);
    xassert(fstate->im);
    xassert(fstate->pw);
    xassert(fstate->arena);
    xassert(imp->area.r > imp->area.x);
    xassert(imp->area.b > imp->area.y);

    const imgui_rect sel_area = {
        selection_area.x,
        selection_area.y,
        selection_area.r,
        selection_area.b,
    };

    const unsigned grid_events = imgui_get_events_rect(im, 'lgbg', &sel_area);

    if (grid_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
    {
        imgui_pt pos;
        pos.x = floorf(im->pos_mouse_down.x);
        pos.y = floorf(im->pos_mouse_down.y);

        // bool should_draw_shape = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_CTRL;
        bool should_draw_shape = selected_shape != IMP_SHAPE_POINT;
        if (should_draw_shape)
        {
            im->left_click_counter = 0;

            const bool snap_to_grid = im->frame.modifiers_mouse_down & PW_MOD_PLATFORM_KEY_ALT;
            _imp_drag_and_draw(imp, pos, snap_to_grid, selected_shape, num_grid_x, num_grid_y);

            fstate->should_update_main_points_with_points = true;
            fstate->should_update_cached_path             = true;
        }
        else if (im->left_click_counter == 1)
        {
            bool shift_click = (PW_MOD_KEY_SHIFT | PW_MOD_LEFT_BUTTON) == im->frame.modifiers_mouse_down;
            if (shift_click == false)
            {
                imp_clear_selection(imp);
            }

            imp->selection_start.x = pos.x;
            imp->selection_start.y = pos.y;
            imp->selection_end.u64 = imp->selection_start.u64;
        }
        else if (im->left_click_counter == 2)
        {
            // add point
            imgui_pt mouse_down = im->pos_mouse_down;

            // user clicked in empty space
            for (int i = xarr_len(imp->points) - 1; i-- != 0;)
            {
                if (mouse_down.x >= imp->points[i].x)
                {
                    xvec2f pt;
                    pt.x = xm_clampf(mouse_down.x, imp->area.x, imp->area.r);
                    pt.y = xm_clampf(mouse_down.y, imp->area.y, imp->area.b);

                    _imp_insert_point(imp, pt, i + 1);

                    _imp_save_points_to_copy(imp);
                    _imp_save_skew_points_to_copy(imp);

                    imp_clear_selection(imp);
                    _imp_add_to_selection(imp, i + 1);

                    fstate->pt_hover_idx = i + 1;

                    fstate->should_update_main_points_with_points = true;
                    fstate->should_update_cached_path             = true;
                    break;
                }
            }
        }
    }
    if (grid_events & IMGUI_EVENT_MOUSE_LEFT_UP)
    {
        imp->selection_start.u64 = 0;
        imp->selection_end.u64   = 0;
    }
    if (grid_events & IMGUI_EVENT_DRAG_MOVE)
    {
        // bool     should_draw_shape = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_CTRL;
        bool     should_draw_shape = selected_shape != IMP_SHAPE_POINT;
        imgui_pt pos;
        pos.x = floorf(im->pos_mouse_move.x);
        pos.y = floorf(im->pos_mouse_move.y);

        if (should_draw_shape)
        {
            const bool snap_to_grid = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_ALT;
            _imp_drag_and_draw(imp, pos, snap_to_grid, selected_shape, num_grid_x, num_grid_y);

            fstate->should_update_main_points_with_points = true;
            fstate->should_update_cached_path             = true;
        }
        else
        {
            imp->selection_end.x = xm_clampf(pos.x, sel_area.x, sel_area.r);
            imp->selection_end.y = xm_clampf(pos.y, sel_area.y, sel_area.b);

            if (imp->selection_start.u64 != 0 && imp->selection_start.u64 != imp->selection_end.u64)
            {
                const imgui_rect area = {
                    xm_minf(imp->selection_start.x, imp->selection_end.x),
                    xm_minf(imp->selection_start.y, imp->selection_end.y),
                    xm_maxf(imp->selection_start.x, imp->selection_end.x) + 1,
                    xm_maxf(imp->selection_start.y, imp->selection_end.y) + 1};

                const int N = xarr_len(imp->points);
                xarr_setcap(imp->selected_point_indexes, N);

                for (int i = 0; i < N; i++)
                {
                    const xvec2f pt = imp->points[i];
                    if (imgui_hittest_rect((imgui_pt){pt.x, pt.y}, &area))
                        _imp_add_to_selection(imp, i);
                }
            }
        }
    }
    if (grid_events & IMGUI_EVENT_MOUSE_ENTER)
    {
        pw_set_mouse_cursor(fstate->pw, PW_CURSOR_DEFAULT);
    }

    if (fstate->should_update_main_points_with_points)
    {
        fstate->should_update_audio_lfo_points_with_main_points = true;
        // Queue LFO points
        int npoints = xarr_len(imp->skew_points);
        xarr_setlen(imp->main_points, npoints);
        for (int i = 0; i < npoints; i++)
        {
            xvec3f* p1 = imp->main_points + i;
            p1->x      = _imp_snap_point(xm_normd(imp->points[i].x, imp->area.x, imp->area.r));
            p1->y      = _imp_snap_point(xm_normd(imp->points[i].y, imp->area.b, imp->area.y));
            p1->skew   = _imp_calculate_point_skew(imp, i);
            xassert(p1->x >= 0 && p1->x <= 1);
            xassert(p1->y >= 0 && p1->y <= 1);
            xassert(p1->skew >= 0 && p1->skew <= 1);
#ifndef NDEBUG
            // validate we didn't do anything silly
            if (i > 0)
            {
                xvec3f* prev = imp->main_points + i - 1;
                xassert(p1->x >= prev->x);
            }
#endif

            i += 0;
        }
        imp->main_points->x = 0;
    }
}

// Draw points
void imp_draw_points(IMPointsFrameContext* fstate)
{
    IMPointsData* imp = fstate->imp;
    NVGcontext*   nvg = fstate->nvg;
    xassert(nvg);

    // Draw path
    {
        const int     N   = xarr_len(imp->path_cache);
        const xvec2f* it  = imp->path_cache;
        const xvec2f* end = it + N;
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, it->x, it->y);
        while (++it != end)
            nvgLineTo(nvg, it->x, it->y);

        NVGcolour col = nvgHexColour(imp->theme.col_line);
        nvgSetColour(nvg, col);
        nvgStroke(nvg, imp->theme.line_stroke_width);
    }

    // Hover point
    {
        xvec2f* hover_pt = NULL;
        if (fstate->pt_hover_skew_idx != -1)
            hover_pt = imp->skew_points + fstate->pt_hover_skew_idx;
        if (fstate->pt_hover_idx != -1)
            hover_pt = imp->points + fstate->pt_hover_idx;

        if (hover_pt)
        {
            xassert(fstate->delete_pt_idx == -1);
            nvgBeginPath(nvg);
            nvgCircle(nvg, hover_pt->x, hover_pt->y, IMP_DEFAULT_POINT_CLICK_RADIUS);
            nvgSetColour(nvg, nvgHexColour(imp->theme.col_point_hover_bg));
            nvgFill(nvg);
        }
    }

    // Skew points
    {
        nvgBeginPath(nvg);
        for (int i = 0; i < xarr_len(imp->skew_points); i++)
        {
            xvec2f pt = imp->skew_points[i];
            nvgCircle(nvg, pt.x, pt.y, IMP_DEFAULT_SKEW_POINT_RADIUS);
        }
        nvgSetColour(nvg, nvgHexColour(imp->theme.col_skewpoint_inner));
        nvgFill(nvg);

        nvgBeginPath(nvg);
        for (int i = 0; i < xarr_len(imp->skew_points); i++)
        {
            xvec2f pt = imp->skew_points[i];
            nvgCircle(nvg, pt.x, pt.y, IMP_DEFAULT_SKEW_POINT_RADIUS);
        }
        nvgSetColour(nvg, nvgHexColour(imp->theme.col_skewpoint_outer));
        nvgStroke(nvg, imp->theme.skewpoint_stroke_width);
    }

    // Regular points
    {
        uint64_t  selected_points_flags = 0;
        const int num_slected_points    = xarr_len(imp->selected_point_indexes);
        if (num_slected_points == 1)
        {
            xassert(imp->selected_point_idx >= 0 && imp->selected_point_idx < xarr_len(imp->points));
            selected_points_flags |= 1llu << ((uint64_t)imp->selected_point_idx);
        }
        else
        {
            for (int i = 0; i < num_slected_points; i++)
            {
                uint64_t idx = imp->selected_point_indexes[i];
                if (idx < 64)
                    selected_points_flags |= 1llu << idx;
            }
        }

        size_t    num_points   = xarr_len(imp->points);
        NVGcolour col_selected = nvgHexColour(imp->theme.col_point_selected);
        NVGcolour col_normal   = nvgHexColour(imp->theme.col_point);
        for (uint64_t i = 0; i < num_points; i++)
        {
            xvec2f pt = imp->points[i];

            // First and last point are the same. Show both as selected
            uint64_t pt_idx = i;
            if (i == num_points - 1)
                pt_idx = 0;

            nvgBeginPath(nvg);
            nvgCircle(nvg, pt.x, pt.y, IMP_DEFAULT_POINT_RADIUS);
            if (selected_points_flags & (1llu << pt_idx))
                nvgSetColour(nvg, col_selected);
            else
                nvgSetColour(nvg, col_normal);
            nvgFill(nvg);
        }
    }

    if (imp->selection_start.u64 != 0 && imp->selection_end.u64 != 0)
    {
        const NVGcolour  col  = nvgHexColour(imp->theme.col_selection_box);
        const imgui_rect area = {
            xm_minf(imp->selection_start.x, imp->selection_end.x),
            xm_minf(imp->selection_start.y, imp->selection_end.y),
            xm_maxf(imp->selection_start.x, imp->selection_end.x),
            xm_maxf(imp->selection_start.y, imp->selection_end.y)};

        NVGcolour bg_col = col;
        bg_col.a         = 0.25f;

        nvgBeginPath(nvg);
        nvgRect2(nvg, area.x, area.y, area.r, area.b);
        nvgSetColour(nvg, bg_col);
        nvgFill(nvg);

        nvgBeginPath(nvg);
        nvgRect2(nvg, area.x + 0.5f, area.y + 0.5f, area.r - 0.5f, area.b - 0.5f);
        nvgSetColour(nvg, col);
        nvgStroke(nvg, 1);
    }
}

#endif // IM_POINTS_IMPL