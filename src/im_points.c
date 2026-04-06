#include "im_points.h"

#include <cplug_extensions/window.h>
#include <imgui.h>
#include <sort.h>
#include <xhl/debug.h>
#include <xhl/maths.h>
#include <xvg.h>

// TODO: a block allocator would be really nice for all of these points arrays.
// They're all a similar size in bytes, except for the path cache
void imp_deinit(IMPointsData* imp)
{
    xarr_free(imp->main_points);
    xarr_free(imp->points);
    xarr_free(imp->skew_points);
    xarr_free(imp->selected_point_indexes);
    xarr_free(imp->selected_point_indexes_copy);
    xarr_free(imp->points_copy);
    xarr_free(imp->skew_points_copy);
    xfree(imp->line_cache);
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

// v - value between 0-1
// a - skew amount between 0-1 where lower values push x closer to 0 and vice versa
static float _imp_skewf(float v, float a)
{
    // va/(1-a-v+2va)
    if (a <= 0)
        return 0;
    else if (a >= 1.0f)
        return 1.0f;

    float va = v * a;
    return va / (1.0f - a - v + 2 * va);
}

// looks about as good as we can get on Godbolt
static float _imp_interp_points(float norm_pos, float skew_amt, float point1_y, float point2_y)
{
    if (point1_y == point2_y)
        return point2_y;

    if (point1_y > point2_y)
        norm_pos = (1.0f - norm_pos);
    float skewed_pos = _imp_skewf(norm_pos, skew_amt);
    if (point1_y > point2_y)
        skewed_pos = (1.0f - skewed_pos);

    return xm_lerpf(skewed_pos, point1_y, point2_y);
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
        const float   y   = _imp_interp_points(0.5f, 1 - skew, pt1->y, pt2->y);

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

void _imp_save_points_to_copy(IMPointsData* imp) { xarr_copy(imp->points, imp->points_copy); }
void _imp_save_skew_points_to_copy(IMPointsData* imp) { xarr_copy(imp->skew_points, imp->skew_points_copy); }
void _imp_save_selection_to_copy(IMPointsData* imp)
{
    xarr_copy(imp->selected_point_indexes, imp->selected_point_indexes_copy);
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
            if (snap_y - imp->theme.point_click_radius <= pos.y && pos.y <= snap_y + imp->theme.point_click_radius)
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
            interp_y_right = _imp_interp_points(amt, 1 - skew, pt->y, next_pt->y);
        }

        if (num_points_at_left_boundary == 0)
        {
            float skew = _imp_calculate_point_skew(imp, left_idx);

            const xvec2f* pt      = imp->points + left_idx;
            const xvec2f* next_pt = imp->points + left_idx + 1;
            const xvec2f* skew_pt = imp->skew_points + left_idx;

            float amt     = xm_normf(boundary_left, pt->x, next_pt->x);
            interp_y_left = _imp_interp_points(amt, 1 - skew, pt->y, next_pt->y);
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
            unsigned uid = imp->uid_points + pt_idx;

            // const xvec2f pt        = imp->points_copy[pt_idx];

            // Properly track point position
            // We're reading out of an array of points cached at the beginning of a drag, and not the points that are
            // actually displayed
            bool was_dragged_last_frame = im->uid_mouse_hold == uid || im->frame.uid_mouse_up == uid;

            imgui_pt pt = was_dragged_last_frame ? im->pos_mouse_move
                                                 : (imgui_pt){imp->points_copy[pt_idx].x, imp->points_copy[pt_idx].y};

            const unsigned pt_events = imgui_get_events_circle(im, uid, pt, imp->theme.point_click_radius);

            if (pt_events == 0)
                continue;

            if (pt_events & IMGUI_EVENT_MOUSE_HOVER)
            {
                fstate->pt_hover_idx = pt_idx;
                bool hold            = !!(pt_events & IMGUI_EVENT_MOUSE_LEFT_HOLD);
                bool is_dragging     = im->uid_drag != 0;
                if (imp->selected_point_idx != -1 && hold && is_dragging)
                    fstate->pt_hover_idx = imp->selected_point_idx;
            }

            if (pt_events & IMGUI_EVENT_MOUSE_ENTER)
            {
                pw_set_mouse_cursor(fstate->pw, PW_CURSOR_HAND_DRAGGABLE);
            }

            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                // wrap
                const int pt_idx_2 = (pt_idx + 1) == num_points ? 0 : pt_idx;

                bool shift_key_down = !!(PW_MOD_KEY_SHIFT & im->frame.modifiers_mouse_down);
                if (im->left_click_counter == 1 || shift_key_down)
                {
                    int sel_idx      = 0;
                    int num_selected = xarr_len(imp->selected_point_indexes);
                    for (sel_idx = 0; sel_idx < num_selected; sel_idx++)
                    {
                        if (pt_idx_2 == imp->selected_point_indexes[sel_idx])
                            break;
                    }
                    bool is_selected = sel_idx < num_selected;
                    if (shift_key_down == false && is_selected == false)
                    {
                        imp_clear_selection(imp);
                    }

                    if (is_selected && shift_key_down)
                    {
                        xarr_delete(imp->selected_point_indexes, sel_idx);
                        if (num_selected == 2)
                            imp->selected_point_idx = imp->selected_point_indexes[0];
                        else
                            imp->selected_point_idx = -1;
                    }
                    else if (!is_selected || shift_key_down)
                    {
                        _imp_add_to_selection(imp, pt_idx_2);
                        int num_selected_pts = xarr_len(imp->selected_point_indexes);
                        xassert(num_selected_pts > 0);
                    }
                    fstate->pt_hover_idx = pt_idx_2;
                    pw_set_mouse_cursor(fstate->pw, PW_CURSOR_HAND_DRAGGING);
                }
                else if (im->left_click_counter == 2 && pt_idx_2 > 0)
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
                            if (x - imp->theme.point_click_radius <= drag_pos.x &&
                                drag_pos.x <= x + imp->theme.point_click_radius)
                            {
                                drag_pos.x = x;
                                break;
                            }
                        }
                        for (int j = 0; j <= num_grid_y; j++)
                        {
                            float y = imp->area.y + j * y_inc;
                            if (y - imp->theme.point_click_radius <= drag_pos.y &&
                                drag_pos.y <= y + imp->theme.point_click_radius)
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
                        if (imp->selected_point_indexes >= 0 && imp->selected_point_idx != -1)
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
                                float y = _imp_interp_points(0.5f, 1 - skew, p1->y, p2->y);

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

            fstate->delete_pt_idx     = -1;
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
            unsigned       uid = imp->uid_skew_points + pt_idx;
            const xvec2f   pt  = imp->skew_points[pt_idx];
            const unsigned pt_events =
                imgui_get_events_circle(im, uid, (imgui_pt){pt.x, pt.y}, imp->theme.point_click_radius);
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
    imgui_rect            selection_area,
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

    const unsigned events = imgui_get_events_rect(im, imp->uid_grid, &sel_area);

    if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
    {
        imgui_pt pos;
        pos.x = floorf(im->pos_mouse_down.x);
        pos.y = floorf(im->pos_mouse_down.y);

        // bool should_draw_shape = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_CTRL;
        bool should_draw_shape = selected_shape != IMP_SHAPE_POINT;
        bool shift_key_down    = !!(PW_MOD_KEY_SHIFT & im->frame.modifiers_mouse_down);
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
            if (shift_key_down == false)
            {
                imp_clear_selection(imp);
                xarr_setlen(imp->selected_point_indexes_copy, 0);
            }
            else
            {
                _imp_save_selection_to_copy(imp);
            }

            imp->selection_start.x = pos.x;
            imp->selection_start.y = pos.y;
            imp->selection_end.u64 = imp->selection_start.u64;

            imp->area_last_click_pos.x = im->pos_mouse_down.x;
            imp->area_last_click_pos.y = im->pos_mouse_down.y;
        }
        else if (im->left_click_counter >= 2 && !shift_key_down)
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
    if (events & IMGUI_EVENT_MOUSE_LEFT_UP)
    {
        imp->selection_start.u64 = 0;
        imp->selection_end.u64   = 0;
    }
    if (events & IMGUI_EVENT_DRAG_MOVE)
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

                const int num_points = xarr_len(imp->points);
                xarr_setcap(imp->selected_point_indexes, num_points);

                imp_clear_selection(imp);

                for (int i = 0; i < num_points; i++)
                {
                    const xvec2f pt = imp->points[i];
                    if (imgui_hittest_rect((imgui_pt){pt.x, pt.y}, &area))
                        _imp_add_to_selection(imp, i);
                }

                const int num_prev_selected = xarr_len(imp->selected_point_indexes_copy);
                for (int i = 0; i < num_prev_selected; i++)
                {
                    int idx = imp->selected_point_indexes_copy[i];
                    _imp_add_to_selection(imp, idx);
                }
            }
        }
    }

    bool should_track_mouse  = events & IMGUI_EVENT_MOUSE_HOVER;
    should_track_mouse      &= imp->area_last_click_pos.u64 != 0;
    should_track_mouse      &= !!(im->frame.events & (1 << PW_EVENT_MOUSE_MOVE));
    if (should_track_mouse)
    {
        float distance_x = im->pos_mouse_move.x - imp->area_last_click_pos.x;
        float distance_y = im->pos_mouse_move.y - imp->area_last_click_pos.y;
        float distance   = sqrtf(distance_x * distance_x + distance_y * distance_y);
        if (distance > 16) // move threshold in pixels
        {
            im->left_click_counter       = 0;
            imp->area_last_click_pos.u64 = 0;
        }
    }

    if (events & IMGUI_EVENT_MOUSE_ENTER)
    {
        pw_set_mouse_cursor(fstate->pw, PW_CURSOR_DEFAULT);
    }

    if (fstate->should_update_main_points_with_points)
    {
        fstate->should_update_audio_points_with_main_points = true;
        // Queue IM points
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
void imp_draw(IMPointsFrameContext* fstate)
{
    const IMPointsData* imp = fstate->imp;
    XVGCommandList*     xcl = fstate->xcl;
    xassert(xcl);

    // Draw path
    {
        const IMPointsLineCache* lc = imp->line_cache;
        for (int i = 0; i < lc->num_lines; i++)
        {
            IMPointsLineCacheStraight* l = lc->lines + i;
            xvg_draw_line_round(xcl, l->x1, l->y1, l->x2, l->y2, imp->theme.line_stroke_width, imp->theme.col_line);
        }
        for (int i = 0; i < lc->num_plots; i++)
        {
            IMPointsLineCachePlot* plot = lc->plots + i;

            xvg_draw_line_plot(
                xcl,
                plot->x,
                plot->y,
                plot->w,
                plot->h,
                lc->y_values + plot->begin_idx,
                0,
                imp->theme.line_stroke_width,
                imp->theme.col_line);
        }
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
            unsigned col = imp->theme.col_point_hover_bg;
            xvg_draw_circle(xcl, hover_pt->x, hover_pt->y, imp->theme.point_click_radius, 0, col);
        }
    }

    // Skew points
    {
        float stroke = imp->theme.skewpoint_stroke_width;
        float radius = imp->theme.skew_point_radius;
        for (int i = 0; i < xarr_len(imp->skew_points); i++)
        {
            xvec2f pt = imp->skew_points[i];
            xvg_draw_circle(xcl, pt.x, pt.y, radius, 0, imp->theme.col_skewpoint_inner);
            xvg_draw_circle(xcl, pt.x, pt.y, radius, stroke, imp->theme.col_skewpoint_outer);
        }

        for (int i = 0; i < xarr_len(imp->skew_points); i++)
        {
            xvec2f pt = imp->skew_points[i];
        }
    }

    // Regular points
    {
        uint64_t  selected_points_flags = 0;
        const int num_slected_points    = xarr_len(imp->selected_point_indexes);
        if (num_slected_points == 1 && imp->selected_point_idx != -1)
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

        size_t   num_points   = xarr_len(imp->points);
        unsigned col_selected = imp->theme.col_point_selected;
        unsigned col_normal   = imp->theme.col_point;
        for (uint64_t i = 0; i < num_points; i++)
        {
            xvec2f pt = imp->points[i];

            // First and last point are the same. Show both as selected
            uint64_t pt_idx = i;
            if (i == num_points - 1)
                pt_idx = 0;

            const bool is_selected = !!(selected_points_flags & (1llu << pt_idx));
            unsigned   col         = is_selected ? col_selected : col_normal;
            xvg_draw_circle(xcl, pt.x, pt.y, imp->theme.point_radius, 0, col);
        }
    }

    if (imp->selection_start.u64 != 0 && imp->selection_end.u64 != 0)
    {
        const unsigned   col  = imp->theme.col_selection_box;
        const imgui_rect area = {
            xm_minf(imp->selection_start.x, imp->selection_end.x),
            xm_minf(imp->selection_start.y, imp->selection_end.y),
            xm_maxf(imp->selection_start.x, imp->selection_end.x),
            xm_maxf(imp->selection_start.y, imp->selection_end.y)};

        unsigned bg_col = (col & 0xffffff00) | 0x40; // 25% opacity

        float stroke = 1;
        xvg_draw_rectangle(xcl, area.x, area.y, area.r - area.x, area.b - area.y, 0, 0, bg_col);
        xvg_draw_rectangle(xcl, area.x, area.y, area.r - area.x, area.b - area.y, 0, stroke, col);
    }
}

void imp_sync_main_points(IMPointsData* imp, const xvec3f* ap)
{
    if (!imp->main_points_valid)
    {
        imp->main_points_valid = !imp->main_points_valid;
        _Static_assert(sizeof(*imp->main_points) == sizeof(*ap), "");
        xarr_copy(ap, imp->main_points);
        imp->points_valid = false;
    }
}

void imp_run(
    IMPointsFrameContext* fstate,
    imgui_rect            selection_area,
    int                   num_grid_x,
    int                   num_grid_y,
    IMPShapeType          current_shape,

    xvec3f**       p_audio_points,
    xt_spinlock_t* p_lock)
{
    IMPointsData*  imp = fstate->imp;
    imgui_context* im  = fstate->im;

    // Handle clicks outside of the
    bool should_clear = false;
    enum
    {
        IMGUI_FLAGS_PW_MOUSE_DOWN_EVENTS =
            (1 << PW_EVENT_MOUSE_LEFT_DOWN) | (1 << PW_EVENT_MOUSE_RIGHT_DOWN) | (1 << PW_EVENT_MOUSE_MIDDLE_DOWN),

    };
    if (im->frame.events & IMGUI_FLAGS_PW_MOUSE_DOWN_EVENTS)
    {
        bool hit =
            imgui_hittest_rect(im->pos_mouse_down, &(imgui_rect){imp->area.x, imp->area.y, imp->area.r, imp->area.b});
        if (!hit)
            should_clear = true;
    }
    should_clear |= !!(im->frame.events & (1 << PW_EVENT_RESIZE_UPDATE));
    if (should_clear)
        imp_clear_selection(imp);

    fstate->did_reload = imp->main_points_valid == false;
    imp_sync_main_points(imp, *p_audio_points);

    if (!imp->points_valid)
    {
        imp->points_valid                 = !imp->points_valid;
        fstate->should_update_cached_path = true;

        imp_clear_selection(imp);

        const int N = xarr_len(imp->main_points);

        const xvec3f* it  = imp->main_points;
        const xvec3f* end = it + N;

        xarr_setlen(imp->points, (N + 1));
        xarr_setlen(imp->skew_points, N);

        xvec2f* p = imp->points;

        while (it != end)
        {
            p->x = xm_lerpf(it->x, imp->area.x, imp->area.r);
            p->y = xm_lerpf(it->y, imp->area.b, imp->area.y);
            it++;
            p++;
        }
        // last Y point matches first point
        p->x = imp->area.r;
        p->y = imp->points->y;

        it         = imp->main_points;
        p          = imp->points;
        xvec2f* sp = imp->skew_points;

        while (it != end)
        {
            xvec2f* next_p = p + 1;

            if (p->x == next_p->x) // the line between point & next_p is vertical
            {
                sp->x = p->x;
                // display skew point vertically, halfway between points
                // skew amount not considered
                sp->y = (p->y + next_p->y) * 0.5f;
            }
            else
            {
                float y = _imp_interp_points(0.5f, 1 - it->skew, p->y, next_p->y);

                // x is always halfway between points
                sp->x = (p->x + next_p->x) * 0.5f;
                // skew amount controls y coord
                sp->y = y;
            }

            it++;
            p++;
            sp++;
        }

        xarr_copy(imp->points, imp->points_copy);
        xarr_copy(imp->skew_points, imp->skew_points_copy);
    }

    if (current_shape == IMP_SHAPE_POINT)
        imp_handle_point_events(fstate, num_grid_x, num_grid_y);

    imp_handle_grid_events(fstate, selection_area, num_grid_x, num_grid_y, current_shape);

    if (fstate->should_update_cached_path)
    {
        const size_t N          = xarr_len(imp->points);
        float        area_w     = imp->area.r - imp->area.x;
        const int    points_cap = area_w + N;

        // Path cache 2
        {
            size_t lines_cap = (N * 2) * sizeof(*imp->line_cache->lines);
            size_t plots_cap = (N - 1) * sizeof(*imp->line_cache->plots);
            size_t y_cap     = ((int)area_w + N * 2) * sizeof(*imp->line_cache->y_values);
            size_t total_cap = sizeof(*imp->line_cache) + lines_cap + plots_cap + y_cap;
            if (total_cap > imp->line_cache_cap_bytes)
            {
                imp->line_cache    = xrealloc(imp->line_cache, total_cap);
                unsigned char* ptr = (unsigned char*)imp->line_cache;

                imp->line_cache->plots = (IMPointsLineCachePlot*)(ptr + total_cap - plots_cap);
                imp->line_cache->lines = (IMPointsLineCacheStraight*)(ptr + total_cap - plots_cap - lines_cap);
            }
            imp->line_cache->num_lines    = 0;
            imp->line_cache->num_plots    = 0;
            imp->line_cache->num_y_values = 0;
        }

        IMPointsLineCache* lc = imp->line_cache;

        const xvec2f* pt      = imp->points;
        const xvec2f* next_pt = imp->points + 1;
        const xvec2f* end     = imp->points + N - 1;
        const xvec2f* skew_pt = imp->skew_points;

        while (pt != end)
        {
            /*
            if (pos.x >= next_pt->x)
            {
                points[npoints++] = *next_pt;
                pt++;
                next_pt++;
                skew_pt++;
            }
            else
            {
                float skew_amt = 0.5f;
                if (pt->y != next_pt->y)
                    skew_amt = xm_normf(skew_pt->y, next_pt->y, pt->y);
                if (pt->y < next_pt->y)
                    skew_amt = 1 - skew_amt;

                float norm_pos = xm_normf(pos.x, pt->x, next_pt->x);

                // A smart person could turn this into a bezier curve with only a few points (destination point +
                // control points). I am not a smart person who can do that.
                pos.y = _imp_interp_points(norm_pos, skew_amt, pt->y, next_pt->y);

                points[npoints++] = pos;

                pos.x += 1.0f;
            }
            */
            bool add_straight_line = false;

            float x1 = floorf(pt->x);
            float y1 = floorf(pt->y);
            float x2 = floorf(next_pt->x);
            float y2 = floorf(next_pt->y);
            xassert(pt->x <= next_pt->x);

            if (x1 == x2 || y1 == y2)
            {
                add_straight_line = true;
            }
            else
            {
                float skew_amt = 0.5f;
                if (pt->y != next_pt->y)
                    skew_amt = xm_normf(skew_pt->y, next_pt->y, pt->y);
                if (pt->y > next_pt->y)
                    skew_amt = 1 - skew_amt;

                if (skew_amt == 0.5f)
                {
                    add_straight_line = true;
                }
                else if (skew_amt == 0 || skew_amt == 1)
                {
                    float x3 = y1 > y2 ? x1 : x2;
                    float x4 = y1 > y2 ? x2 : x1;
                    float y3 = y1 > y2 ? y2 : y1;
                    float y4 = y1 > y2 ? y1 : y2;

                    float x = skew_amt == 0 ? x4 : x3;
                    float y = skew_amt == 0 ? y4 : y3;

                    IMPointsLineCacheStraight l1 = {x, y3, x, y4};
                    IMPointsLineCacheStraight l2 = {x3, y, x4, y};

                    imp->line_cache->lines[imp->line_cache->num_lines++] = l1;
                    imp->line_cache->lines[imp->line_cache->num_lines++] = l2;
                }
                else // Make plot
                {
                    float w = x2 - x1;
                    float h = fabsf(y2 - y1);
                    xassert(w >= 1);

                    const unsigned y_begin = lc->num_y_values;

                    float*      data     = lc->y_values + y_begin;
                    const int   plot_len = w + 1; // include the right most point
                    const float inc      = 1.0f / w;

                    const float plot_y1 = y1 > y2 ? 0 : 1;
                    const float plot_y2 = y1 > y2 ? 1 : 0;

                    for (int i = 0; i < plot_len; i++)
                    {
                        float norm_pos = i * inc;
                        data[i]        = _imp_interp_points(norm_pos, skew_amt, plot_y1, plot_y2);
                    }
                    lc->num_y_values += plot_len;

                    lc->plots[lc->num_plots++] = (IMPointsLineCachePlot){
                        .x         = x1,
                        .y         = xm_minf(y1, y2),
                        .w         = plot_len,
                        .h         = h,
                        .begin_idx = y_begin,
                        .end_idx   = lc->num_y_values,
                    };
                }
            }

            if (add_straight_line)
            {
                imp->line_cache->lines[imp->line_cache->num_lines++] = (IMPointsLineCacheStraight){
                    .x1 = x1,
                    .y1 = y1,
                    .x2 = x2,
                    .y2 = y2,
                };
            }

            pt++;
            next_pt++;
            skew_pt++;
        }
    }

    if (fstate->should_update_audio_points_with_main_points)
    {
        xvec3f* old_array  = NULL;
        size_t  num_points = xarr_len(imp->main_points);

        // !!!
        {
            if (p_lock)
                xt_spinlock_lock(p_lock);

            old_array = xt_atomic_exchange_ptr((xt_atomic_ptr_t*)p_audio_points, imp->main_points);

            if (p_lock)
                xt_spinlock_unlock(p_lock);
        }

        // Deep copy
        xarr_setlen(old_array, num_points);
        memcpy(old_array, *p_audio_points, sizeof(*old_array) * num_points);

        imp->main_points = old_array;
    }
}

void imp_render_y_values(const IMPointsData* imp, float* buffer, size_t bufferlen, float y_range_min, float y_range_max)
{
    float y_scale = y_range_max - y_range_min;

    const float x_inc  = 1.0f / bufferlen;
    int         bufidx = 0;

    xassert(imp->main_points_valid); // oh dear, your points are out of sync
    const int num_main_points = xarr_len(imp->main_points);
    int       pt_idx          = 0;

    xassert(num_main_points > 0);

    const xvec3f* it  = imp->main_points;
    const xvec3f* end = imp->main_points + num_main_points;

    float pt_x      = imp->main_points[0].x;
    float pt_y      = imp->main_points[0].y;
    float next_pt_x = (it + 1 == end) ? 1.0f : (it + 1)->x;
    float next_pt_y = (it + 1 == end) ? it->y : (it + 1)->y;
    float skew_amt  = imp->main_points[0].skew;

    while (it != end && bufidx < bufferlen)
    {
        float x = bufidx * x_inc;
        if (x >= next_pt_x)
        {
            it++;
            pt_idx++;

            pt_x      = it->x;
            pt_y      = it->y;
            next_pt_x = (it + 1 == end) ? 1.0f : (it + 1)->x;
            next_pt_y = (it + 1 == end) ? imp->main_points[0].y : (it + 1)->y;
            skew_amt  = it->skew;
        }
        else
        {
            float norm_pos = xm_normf(x, pt_x, next_pt_x);

            // A smart person could turn this into a bezier curve with only a few points (destination point +
            // control points). I am not a smart person who can do that.
            float y_norm = _imp_interp_points(norm_pos, skew_amt, pt_y, next_pt_y);

            float y_rescaled = y_range_min + y_norm * y_scale;
            y_rescaled       = xm_clampf(y_rescaled, y_range_min, y_range_max);

            buffer[bufidx++] = y_rescaled;
        }
    }
    xassert(bufidx == bufferlen);
}