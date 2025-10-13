#include "dsp.h"
#include "gui.h"

#include <layout.h>
#include <lfo.glsl.h>
#include <sort.h>
#include <stdio.h>
#include <xhl/time.h>

enum
{
    LFO_POINT_CLICK_RADIUS = 12,
    LFO_POINT_RADIUS       = 4,
    LFO_SKEW_POINT_RADIUS  = 3,
    LFO_SKEW_DRAG_RANGE    = 250,

    LFO_POINT_DRAG_ERASE_DISTANCE = 24,
};

static inline int main_get_lfo_pattern_idx(Plugin* p)
{
    int    lfo_idx  = p->selected_lfo_idx;
    double v        = main_get_param(p, PARAM_PATTERN_LFO_1 + lfo_idx);
    v              *= NUM_LFO_PATTERNS - 1;
    return xm_droundi(v);
}

float calculate_point_skew(GUI* gui, int idx)
{
    size_t num_points      = xarr_len(gui->points);
    size_t num_skew_points = xarr_len(gui->skew_points);
    size_t next_idx        = idx + 1;

    xassert(idx >= 0 && idx < num_points);
    xassert(idx >= 0 && next_idx < num_points);
    xassert(idx >= 0 && idx < num_skew_points);

    if (next_idx >= num_points)
        next_idx = 0;

    const imgui_pt* pt      = gui->points + idx;
    const imgui_pt* next_pt = gui->points + next_idx;
    const imgui_pt* skew_pt = gui->skew_points + idx;

    float skew = 0.5f;
    if (pt->y != next_pt->y)
        skew = xm_normf(skew_pt->y, next_pt->y, pt->y);
    if (pt->y > next_pt->y)
        skew = 1 - skew;

    skew = xm_clampf(skew, 0, 1);
    return skew;
}

static inline double snap_point(double x)
{
    if (x < 0.000001)
        x = 0;
    if (x > 0.999999)
        x = 1;
    return x;
}

void lfo_points_add_selected(GUI* gui, int idx)
{
    int num_points = xarr_len(gui->points);
    if (idx == num_points - 1)
        idx = 0;

    int       i;
    const int N = xarr_len(gui->selected_point_indexes);
    for (i = 0; i < N; i++)
    {
        if (gui->selected_point_indexes[i] == idx)
            break;
    }
    if (i == N) // idx not in array
    {
        xarr_push(gui->selected_point_indexes, idx);
        sort_int(gui->selected_point_indexes, N + 1);

        int num_selected_pts = xarr_len(gui->selected_point_indexes);
        xassert(num_selected_pts > 0);
        if (num_selected_pts == 1)
            gui->selected_point_idx = idx;
        else
            gui->selected_point_idx = -1;
    }
}

void update_skew_point(GUI* gui, int i, float skew)
{
    xassert(i < xarr_len(gui->skew_points));
    xassert(i < xarr_len(gui->points) - 1);
    if (gui->points[i].x == gui->points[i + 1].x) // the line between point & next point is vertical
    {
        gui->skew_points[i].x = gui->points[i].x;
        // display skew point vertically, halfway between points
        // skew amount not considered
        gui->skew_points[i].y = (gui->points[i].y + gui->points[i + 1].y) * 0.5f;
    }
    else
    {
        // x is always halfway between points
        gui->skew_points[i].x = (gui->points[i].x + gui->points[i + 1].x) * 0.5f;
        // skew amount controls y coord
        const imgui_pt* pt1 = gui->points + i;
        const imgui_pt* pt2 = gui->points + i + 1;
        const float     y   = interp_points(0.5f, 1 - skew, pt1->y, pt2->y);

        gui->skew_points[i].y = y;
    }
}

// Clamps target_pos to boundaries. Updates relevant skew points. Updates LFO points on audio thread
void update_lfo_point(GUI* gui, const imgui_rect* area, imgui_pt pos, int idx)
{
    const int lfo_idx     = gui->plugin->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

    // const LFOPoint* lfopoints  = gui->main_lfo_points;
    const size_t num_points = xarr_len(gui->points);

    xvec2f range_horizontal;

    if (idx == 0)
        range_horizontal.left = area->x;
    else
        range_horizontal.left = gui->points[idx - 1].x;

    if (idx == 0)
        range_horizontal.right = area->x;
    else if (idx == num_points - 1)
        range_horizontal.right = area->r;
    else
        range_horizontal.right = gui->points[idx + 1].x;

    float i_skew        = calculate_point_skew(gui, idx);
    float prev_skew     = 0;
    float last_skew     = 0;
    int   last_skew_idx = (int)xarr_len(gui->skew_points) - 1;
    if (idx > 0)
        prev_skew = calculate_point_skew(gui, idx - 1);
    if (idx == 0)
        last_skew = calculate_point_skew(gui, last_skew_idx);

    imgui_pt* pt = &gui->points[idx];
    pt->x        = xm_clampf(pos.x, range_horizontal.l, range_horizontal.r);
    pt->y        = xm_clampf(pos.y, area->y, area->b);

    if (idx == 0)
        gui->points[num_points - 1].y = pt->y;

    update_skew_point(gui, idx, i_skew);
    if (idx > 0)
        update_skew_point(gui, idx - 1, prev_skew);

    if (idx == 0)
    {
        update_skew_point(gui, last_skew_idx, last_skew);
    }
}

static void insert_lfo_point(GUI* gui, imgui_pt pos, int idx, const imgui_rect* area)
{
    const int prev_idx = idx - 1;

#ifndef NDEBUG
    size_t gui_pt_len = xarr_len(gui->points);
    xassert(idx > 0);
    xassert(idx < gui_pt_len);
    xassert(prev_idx >= 0);
    imgui_pt prev_pt = gui->points[prev_idx];
    xassert(prev_pt.x <= pos.x);
#endif

    float skew = calculate_point_skew(gui, prev_idx);

    // add points locally
    xarr_insert(gui->points, idx, pos);
    xarr_insert(gui->skew_points, prev_idx, pos);

    update_skew_point(gui, prev_idx, skew);
    if (idx < xarr_len(gui->skew_points))
        update_skew_point(gui, idx, 0.5f);
}

static void delete_lfo_point(GUI* gui, int idx)
{
    xassert(idx > 0);
    xassert(idx != xarr_len(gui->skew_points));
    xassert(xarr_len(gui->skew_points) == (xarr_len(gui->points) - 1));

    xarr_delete(gui->points, idx);
    xarr_delete(gui->skew_points, idx - 1);
    // when user clears the "last point", reset neighbouring skew amounts
    update_skew_point(gui, idx - 1, 0.5f);
}

static void copy_points(GUI* gui)
{
    int N = xarr_len(gui->points);
    xarr_setlen(gui->points_copy, N);
    memcpy(gui->points_copy, gui->points, N * sizeof(*gui->points_copy));
}
static void copy_skew_points(GUI* gui)
{
    int N = xarr_len(gui->skew_points);
    xarr_setlen(gui->skew_points_copy, N);
    memcpy(gui->skew_points_copy, gui->skew_points, N * sizeof(*gui->skew_points_copy));
}

static void clear_selection(GUI* gui)
{
    xarr_setlen(gui->selected_point_indexes, 0);
    gui->selected_point_idx = -1;
}

// First draft for painting shapes on the LFO grid.
void drag_and_draw_lfo_points(GUI* gui, imgui_pt pos, const imgui_rect* area)
{
    gui->selection_start.u64 = 0;
    gui->selection_end.u64   = 0;
    clear_selection(gui);

    const enum ShapeButtonType shape_type = gui->plugin->lfo_shape_idx;
    xassert(shape_type >= 0 && shape_type < SHAPE_COUNT);

    const int lfo_idx     = gui->plugin->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

    // const float pattern_length = (float)gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx];
    const float pattern_length = 1;

    const float area_width  = area->r - area->x;
    const float area_height = area->b - area->y;

    const int lfo_grid_x = pattern_length * gui->plugin->lfos[lfo_idx].grid_x[pattern_idx];
    const int lfo_grid_y = gui->plugin->lfos[lfo_idx].grid_y[pattern_idx];

    const float x_inc = area_width / (float)lfo_grid_x;
    const float y_inc = area_height / (float)lfo_grid_y;

    int grid_idx_left    = (int)((pos.x - area->x) / x_inc);
    grid_idx_left        = xm_clampi(grid_idx_left, 0, lfo_grid_x - 1);
    float boundary_left  = area->x + grid_idx_left * x_inc;
    float boundary_right = area->x + (grid_idx_left + 1) * x_inc;

    const bool snap_to_grid = gui->imgui.frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_ALT;
    float      y            = pos.y;
    if (snap_to_grid)
    {
        for (int j = 0; j <= lfo_grid_y; j++)
        {
            float snap_y = area->y + j * y_inc;
            if (snap_y - LFO_POINT_CLICK_RADIUS <= pos.y && pos.y <= snap_y + LFO_POINT_CLICK_RADIUS)
            {
                y = snap_y;
                break;
            }
        }
    }

    y = xm_clampf(y, area->y, area->b);

    // New points at grid boundary
    float pt_y_left = y, pt_y_right = y;
    switch (shape_type)
    {
    case SHAPE_FLAT:
        break;
    case SHAPE_LINEAR_ASC:
    case SHAPE_CONVEX_ASC:
    case SHAPE_CONCAVE_ASC:
    case SHAPE_COSINE_ASC:
        pt_y_left = area->b;
        break;
    case SHAPE_LINEAR_DESC:
    case SHAPE_CONVEX_DESC:
    case SHAPE_CONCAVE_DESC:
    case SHAPE_COSINE_DESC:
        pt_y_right = area->b;
        break;

    case SHAPE_TRIANGLE_UP:
        pt_y_left  = area->b;
        pt_y_right = area->b;
        break;
    case SHAPE_TRIANGLE_DOWN:
        pt_y_left  = area->y;
        pt_y_right = area->y;
        break;
    case SHAPE_COUNT:
        xassert(false);
        break;
    }

    // Delete points inside boundary range
    {
        const int num_points = xarr_len(gui->points); // note, len(gui->points) is the same as len(lfo->skew_points) + 1
        for (int i = num_points - 1; i-- != 1;)
        {
            xassert(i > 0);
            bool between  = boundary_left < gui->points[i].x;
            between      &= gui->points[i].x < boundary_right;

            if (between)
                delete_lfo_point(gui, i);
        }
    }

    // Count points at boundaries
    int num_points                   = xarr_len(gui->points);
    int right_idx                    = -1;
    int num_points_at_right_boundary = 0;
    int left_idx                     = -1;
    int num_points_at_left_boundary  = 0;

    // Count right
    {
        for (int i = num_points; i-- != 0;)
        {
            if (gui->points[i].x == boundary_right)
            {
                num_points_at_right_boundary++;
            }
            if (gui->points[i].x < boundary_right)
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
            if (gui->points[i].x == boundary_left)
            {
                num_points_at_left_boundary++;
            }
            if (gui->points[i].x > boundary_left)
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
            float skew = calculate_point_skew(gui, right_idx - 1);

            const imgui_pt* pt      = gui->points + right_idx - 1;
            const imgui_pt* next_pt = gui->points + right_idx;
            const imgui_pt* skew_pt = gui->skew_points + right_idx - 1;

            float amt      = xm_normf(boundary_right, pt->x, next_pt->x);
            interp_y_right = interp_points(amt, 1 - skew, pt->y, next_pt->y);
        }

        if (num_points_at_left_boundary == 0)
        {
            float skew = calculate_point_skew(gui, left_idx);

            const imgui_pt* pt      = gui->points + left_idx;
            const imgui_pt* next_pt = gui->points + left_idx + 1;
            const imgui_pt* skew_pt = gui->skew_points + left_idx;

            float amt     = xm_normf(boundary_left, pt->x, next_pt->x);
            interp_y_left = interp_points(amt, 1 - skew, pt->y, next_pt->y);
        }

        if (num_points_at_right_boundary == 0)
        {
            imgui_pt pt = {boundary_right, interp_y_right};
            insert_lfo_point(gui, pt, right_idx, area);
            num_points++;
            num_points_at_right_boundary++;
        }
        if (num_points_at_left_boundary == 0)
        {
            imgui_pt pt = {boundary_left, interp_y_left};
            insert_lfo_point(gui, pt, left_idx + 1, area);
            num_points++;
            num_points_at_left_boundary++;
            left_idx++;
            right_idx++;
        }
    }

    if (num_points_at_right_boundary == 1)
    {
        imgui_pt pt = {boundary_right, pt_y_right};
        insert_lfo_point(gui, pt, right_idx, area);
        num_points++;
        num_points_at_right_boundary++;
    }
    else
    {
        xassert(num_points_at_right_boundary >= 2);
        imgui_pt pt = {boundary_right, pt_y_right};
        update_lfo_point(gui, area, pt, right_idx);
    }

    if (num_points_at_left_boundary == 1)
    {
        imgui_pt prev_pt = gui->points[left_idx];
        imgui_pt pt      = {boundary_left, pt_y_left};
        if (prev_pt.x != pt.x || prev_pt.y != pt.y)
        {
            insert_lfo_point(gui, pt, left_idx + 1, area);
            num_points++;
            num_points_at_left_boundary++;
            left_idx++;
        }
    }
    else
    {
        xassert(num_points_at_left_boundary >= 2);
        imgui_pt pt = {boundary_left, pt_y_left};
        update_lfo_point(gui, area, pt, left_idx);
    }
    // xassert(num_points_at_right_boundary == 2);

    if (shape_type == SHAPE_LINEAR_ASC || shape_type == SHAPE_LINEAR_DESC)
    {
        update_skew_point(gui, left_idx, 0.5);
    }
    if (shape_type == SHAPE_CONVEX_ASC || shape_type == SHAPE_CONVEX_DESC)
    {
        update_skew_point(gui, left_idx, 0.85);
    }
    if (shape_type == SHAPE_CONCAVE_ASC || shape_type == SHAPE_CONCAVE_DESC)
    {
        update_skew_point(gui, left_idx, 0.15);
    }

    if (shape_type == SHAPE_TRIANGLE_UP || shape_type == SHAPE_TRIANGLE_DOWN)
    {
        imgui_pt pt = {boundary_left + x_inc * 0.5f, y};
        insert_lfo_point(gui, pt, left_idx + 1, area);
        num_points++;
        left_idx++;
    }

    if (shape_type == SHAPE_COSINE_ASC || shape_type == SHAPE_COSINE_DESC)
    {
        // Approximation for a descening cosing shape
        // x = (1 / 7), y = 0.95, skew = 0.731994
        // x = 0.3333,  y = 0.75, skew = 0.56
        // x = 0.6666,  y = 0.25, skew = 0
        // x = (6 / 7), y = 0.05, skew = 0.44
        // x = 1,       y = 0,    skew = 1 - 0.731994
        imgui_pt pts[] = {
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
        if (shape_type == SHAPE_COSINE_ASC)
        {
            for (int i = 0; i < ARRLEN(pts); i++)
            {
                imgui_pt* pt = pts + i;
                pt->y        = 1 - pt->y;
            }
            for (int i = 0; i < ARRLEN(skews); i++)
                skews[i] = 1 - skews[i];
        }

        for (int i = 0; i < ARRLEN(pts); i++)
        {
            imgui_pt* pt = pts + i;
            pt->x        = xm_lerpf(pt->x, boundary_left, boundary_right);
            pt->y        = xm_lerpf(pt->y, area->b, y);
            insert_lfo_point(gui, *pt, left_idx + 1, area);
            update_skew_point(gui, left_idx, skews[i]);
            num_points++;
            left_idx++;
        }
        update_skew_point(gui, left_idx, skews[ARRLEN(skews) - 1]);
    }

    copy_points(gui);
    copy_skew_points(gui);
}

void do_lfo_shaders(void* uptr)
{
    GUI*           gui = uptr;
    LayoutMetrics* lm  = &gui->layout;

    const size_t len = xarr_len(gui->lfo_ybuffer);
    xassert(len > 0);

    // Slow decay on the LFO playhead tail
    // Scale in decibels
    uint64_t time_delta_ns = gui->frame_start_time - gui->last_frame_start_time;
    if (gui->last_frame_start_time == gui->gui_create_time)
        time_delta_ns = 0;
    double       time_delta_sec       = xtime_convert_ns_to_sec(time_delta_ns);
    const double reduction_per_second = -48;
    double       reduction_dB         = time_delta_sec * reduction_per_second;
    float        scale                = xm_fast_dB_to_gain(reduction_dB);

    for (int i = 0; i < len; i++)
    {
        float y = gui->lfo_playhead_trail[i] * scale;
        if (y < 0.0001) // snap to 0
            y = 0;
        gui->lfo_playhead_trail[i] = y;
    }

    // Apply new trail
    size_t last_playhead_idx    = lm->last_lfo_playhead * len;
    size_t current_playhead_idx = lm->current_lfo_playhead * len;
    last_playhead_idx           = xm_minull(last_playhead_idx, len - 1);
    current_playhead_idx        = xm_minull(current_playhead_idx, len - 1);
    if (current_playhead_idx < last_playhead_idx) // unwrap
        current_playhead_idx += len;
    xassert(current_playhead_idx >= last_playhead_idx);

    // target playhead y is 1
    float  target_playhead_y = 0.5f;
    float  start_y           = gui->lfo_playhead_trail[last_playhead_idx];
    size_t playhead_diff     = current_playhead_idx - last_playhead_idx;
    float  inc               = (target_playhead_y - start_y) / (float)playhead_diff;

    float(*view_trail)[512] = (void*)gui->lfo_playhead_trail;

    view_trail += 0;

    for (size_t i = 1; i <= playhead_diff; i++)
    {
        size_t idx = last_playhead_idx + i;
        // wrap idx
        if (idx >= len)
            idx -= len;

        float y = start_y + inc * i;
        // float y = target_playhead_y;
        if (y > 1)
            y = 0;
        gui->lfo_playhead_trail[idx] = y;
    }

    sg_range range_ybuf     = {.ptr = gui->lfo_ybuffer, .size = len * sizeof(*gui->lfo_ybuffer)};
    sg_range range_playhead = {.ptr = gui->lfo_playhead_trail, .size = len * sizeof(*gui->lfo_playhead_trail)};

    sg_bindings bind                                    = {0};
    bind.storage_buffers[SBUF_lfo_line_storage_buffer]  = gui->lfo_ybuffer_obj;
    bind.storage_buffers[SBUF_lfo_trail_storage_buffer] = gui->lfo_playhead_trail_obj;

    vs_lfo_uniforms_t vs_uniforms = {
        .topleft     = {gui->lfo_grid_area.x, gui->lfo_grid_area.y},
        .bottomright = {gui->lfo_grid_area.r, gui->lfo_grid_area.b},
        .size        = {lm->width, lm->height},
    };

    fs_lfo_uniforms_t fs_uniforms = {
        .colour1      = hexcol(0xBDEBF754),
        .colour2      = hexcol(0x92C6D400),
        .colour_trail = hexcol(0xACDEECFF),
        .buffer_len   = len,
    };

    sg_update_buffer(gui->lfo_ybuffer_obj, &range_ybuf);
    sg_update_buffer(gui->lfo_playhead_trail_obj, &range_playhead);

    sg_apply_pipeline(gui->lfo_vertical_grad_pip);
    sg_apply_bindings(&bind);
    sg_apply_uniforms(UB_vs_lfo_uniforms, &SG_RANGE(vs_uniforms));
    sg_apply_uniforms(UB_fs_lfo_uniforms, &SG_RANGE(fs_uniforms));

    sg_draw(0, 6, 1);
}

// Autogenerated with assets/svg_to_nvg.py
// WxH = 8x14
void draw_crotchet_svg(NVGcontext* nvg, const float scale, float x, float y)
{
    // clang-format off
    nvgBeginPath(nvg);
    nvgMoveTo(nvg, x + scale * 5.01502f, y + scale * 12.8449f);
    nvgBezierTo(nvg, x + scale * 2.98149f, y + scale * 13.7081f, x + scale * 0.983125f, y + scale * 13.5836f, x + scale * 0.551536f, y + scale * 12.5669f);
    nvgBezierTo(nvg, x + scale * 0.119946f, y + scale * 11.5501f, x + scale * 1.41857f, y + scale * 10.0261f, x + scale * 3.45209f, y + scale * 9.16292f);
    nvgBezierTo(nvg, x + scale * 4.35265f, y + scale * 8.78066f, x + scale * 5.24631f, y + scale * 8.5921f, x + scale * 6.00005f, y + scale * 8.58896f);
    nvgLineTo(nvg, x + scale * 6.00005f, y + scale * 0.0f);
    nvgLineTo(nvg, x + scale * 8.00016f, y + scale * 0.0f);
    nvgLineTo(nvg, x + scale * 7.99796f, y + scale * 9.89037f);
    nvgBezierTo(nvg, x + scale * 7.99702f, y + scale * 9.92657f, x + scale * 7.99446f, y + scale * 9.96313f, x + scale * 7.99032f, y + scale * 10.0f);
    nvgBezierTo(nvg, x + scale * 7.88258f, y + scale * 10.9586f, x + scale * 6.70155f, y + scale * 12.1291f, x + scale * 5.01502f, y + scale * 12.8449f);
    nvgClosePath(nvg);
    nvgFill(nvg);
    // clang-format on
}

void draw_lfo_section(GUI* gui)
{
    LINKED_ARENA_LEAK_DETECT_BEGIN(gui->arena);

    enum
    {
        LFO_TAB_HEIGHT           = 28,
        LFO_TAB_WIDTH            = 80,
        LFO_TAB_ICON_PADDING     = 8,
        LFO_TAB_ICON_WIDTH       = 12, // Square icon
        LFO_TAB_ARROWHEAD_LENGTH = 3,
        LFO_TAB_ARROWBODY_LENGTH = 4,

        GRID_BUTTON_WIDTH      = 32,
        GRID_BUTTON_HEIGHT     = 28,
        GRID_BUTTON_BUTTON_GAP = 8,
        GRID_BUTTON_TEXT_GAP   = 16,

        CHECKBOX_HEIGHT = 12,

        SHAPES_WIDTH         = 40, // LFO shape buttons are square
        SHAPES_INNER_PADDING = 8,

        CONTENT_PADDING_X = 32,
        CONTENT_PADDING_Y = 16,

        PATTERN_WIDTH                = 200,
        PATTERN_NUMBER_LABEL_PADDING = 32,
        PATTERN_SLIDER_WIDTH         = PATTERN_WIDTH - 2 * PATTERN_NUMBER_LABEL_PADDING,
        PATTERN_TRIANGLE_HEIGHT      = 12,

        DISPLAY_PADDING_TOP    = 48,
        DISPLAY_PADDING_BOTTOM = 32,
    };

    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;
    LayoutMetrics* lm  = &gui->layout;

    // TODO: rather than cache a line of points, cache the vertices from NanoVG.
    bool should_update_cached_path                          = false;
    bool should_update_gui_lfo_points_with_points           = false;
    bool should_update_audio_lfo_points_with_gui_lfo_points = false;
    bool should_clear_lfo_trail                             = false;
    int  next_pattern_length                                = 0;

    const float bot_content_height = lm->content_b - lm->top_content_bottom;
    const float font_size          = lm->content_scale * 14;

    const float display_y   = lm->top_content_bottom + 8;
    const float display_w   = (lm->content_r - lm->content_x) - 2 * 8;
    const float display_h   = bot_content_height - 2 * 8;
    const float display_b   = display_y + display_h;
    const float top_text_cy = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT * 0.5f;

    nvgBeginPath(nvg);
    nvgRoundedRect(nvg, lm->content_x + 8, display_y, display_w, display_h, 6);
    nvgSetPaint(
        nvg,
        nvgLinearGradient(nvg, 0, display_y, 0, display_b, nvgHexColour(0x242838FF), nvgHexColour(0x0C101DFF)));
    nvgFill(nvg);

    // LFO tabs
    {
        imgui_rect lfo_tabs[2];
        float      gui_cx = lm->width / 2.0f;

        lfo_tabs[0].r = gui_cx - 4;
        lfo_tabs[0].x = lfo_tabs[0].r - LFO_TAB_WIDTH;
        lfo_tabs[1].x = gui_cx + 4;
        lfo_tabs[1].r = lfo_tabs[1].x + LFO_TAB_WIDTH;

        // float top_padding = CONTENT_PADDING_Y;
        lfo_tabs[0].y = display_y + CONTENT_PADDING_Y;
        lfo_tabs[1].y = display_y + CONTENT_PADDING_Y;
        lfo_tabs[0].b = lfo_tabs[0].y + LFO_TAB_HEIGHT;
        lfo_tabs[1].b = lfo_tabs[1].y + LFO_TAB_HEIGHT;

        for (int i = 0; i < ARRLEN(lfo_tabs); i++)
        {
            const imgui_rect* rect   = &lfo_tabs[i];
            const unsigned    wid    = 'tlfo' + i;
            const unsigned    events = imgui_get_events_rect(im, wid, rect);

            tooltip_handle_events(
                &gui->tooltip,
                *rect,
                "Toggle between controls for LFO 1 & 2",
                gui->frame_start_time,
                events);

            if (events & IMGUI_EVENT_MOUSE_ENTER)
            {
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
            }
            if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                gui->plugin->selected_lfo_idx = i;
                gui->gui_lfo_points_valid     = false;
                should_clear_lfo_trail        = true;
            }

            NVGcolour  col1, col2;
            const bool is_active = gui->plugin->selected_lfo_idx == i;
            if (is_active)
            {
                col1 = C_LIGHT_BLUE_2;
                col2 = C_BG_LFO;
            }
            else
            {
                col1 = C_BG_LFO;
                col2 = C_LIGHT_BLUE_2;
            }

            if (is_active)
            {
                NVGcolour glow_icol = C_DARK_BLUE;
                NVGcolour glow_ocol = glow_icol;
                glow_ocol.a         = 0;
                float width         = rect->r - rect->x;
                float height        = rect->b - rect->y;
                float glow_radius   = 12;
                // glow
                float gx = rect->x - glow_radius;
                float gy = rect->y - glow_radius;
                float gw = width + 2 * glow_radius;
                float gh = height + 2 * glow_radius;
                nvgBeginPath(nvg);
                nvgRect(nvg, gx, gy, gw, gh);
                NVGpaint paint =
                    nvgBoxGradient(nvg, rect->x, rect->y, width, height, 4, glow_radius, glow_icol, glow_ocol);
                nvgSetPaint(nvg, paint);
                nvgFill(nvg);

                // tab
                nvgBeginPath(nvg);
                nvgRoundedRect(nvg, rect->x, rect->y, width, height, 4);
                nvgSetColour(nvg, col1);
                nvgFill(nvg);
            }
            else
            {
                nvgBeginPath(nvg);
                nvgRoundedRect(nvg, rect->x + 0.5, rect->y + 0.5, rect->r - rect->x, rect->b - rect->y, 4);
                nvgSetColour(nvg, col2);
                nvgStroke(nvg, 1.1); // rounded edges looks better when stroke width >1
            }

            // snap half pixel
            float icon_x = floorf(rect->x + LFO_TAB_ICON_PADDING) + 0.5f;
            float icon_y = floorf(rect->y + LFO_TAB_ICON_PADDING) + 0.5f;
            float icon_r = icon_x + LFO_TAB_ICON_WIDTH - 1;
            float icon_b = icon_y + LFO_TAB_ICON_WIDTH - 1;

            // nvgSetLineCap(nvg, NVG_ROUND); // Doesn't look great when lines are so small and thin
            nvgSetLineCap(nvg, NVG_BUTT);
            nvgSetColour(nvg, col2);

            nvgBeginPath(nvg);
            // Top left arrow head
            nvgMoveTo(nvg, icon_x, icon_y + LFO_TAB_ARROWHEAD_LENGTH);
            nvgLineTo(nvg, icon_x, icon_y);
            nvgLineTo(nvg, icon_x + LFO_TAB_ARROWHEAD_LENGTH, icon_y);
            // Top right
            nvgMoveTo(nvg, icon_r - LFO_TAB_ARROWHEAD_LENGTH, icon_y);
            nvgLineTo(nvg, icon_r, icon_y);
            nvgLineTo(nvg, icon_r, icon_y + LFO_TAB_ARROWHEAD_LENGTH);
            // Bottom left
            nvgMoveTo(nvg, icon_x, icon_b - LFO_TAB_ARROWHEAD_LENGTH);
            nvgLineTo(nvg, icon_x, icon_b);
            nvgLineTo(nvg, icon_x + LFO_TAB_ARROWHEAD_LENGTH, icon_b);
            // Bottom right
            nvgMoveTo(nvg, icon_r - LFO_TAB_ARROWHEAD_LENGTH, icon_b);
            nvgLineTo(nvg, icon_r, icon_b);
            nvgLineTo(nvg, icon_r, icon_b - LFO_TAB_ARROWHEAD_LENGTH);

            // Arrow bodies
            nvgMoveTo(nvg, icon_x, icon_y);
            nvgLineTo(nvg, icon_x + LFO_TAB_ARROWBODY_LENGTH, icon_y + LFO_TAB_ARROWBODY_LENGTH);
            nvgMoveTo(nvg, icon_r, icon_y);
            nvgLineTo(nvg, icon_r - LFO_TAB_ARROWBODY_LENGTH, icon_y + LFO_TAB_ARROWBODY_LENGTH);
            nvgMoveTo(nvg, icon_x, icon_b);
            nvgLineTo(nvg, icon_x + LFO_TAB_ARROWBODY_LENGTH, icon_b - LFO_TAB_ARROWBODY_LENGTH);
            nvgMoveTo(nvg, icon_r, icon_b);
            nvgLineTo(nvg, icon_r - LFO_TAB_ARROWBODY_LENGTH, icon_b - LFO_TAB_ARROWBODY_LENGTH);

            // icon/text seperator
            nvgMoveTo(nvg, icon_r + LFO_TAB_ICON_PADDING + 1, icon_y - 2.5f);
            nvgLineTo(nvg, icon_r + LFO_TAB_ICON_PADDING + 1, icon_b + 2.5f);

            nvgStroke(nvg, 1);

            char label[]  = "LFO 1";
            label[4]     += i;

            nvgSetFontSize(nvg, lm->content_scale * 14);
            nvgSetTextAlign(nvg, NVG_ALIGN_CR);
            nvgText(nvg, rect->r - LFO_TAB_ICON_PADDING, top_text_cy, label, label + 5);
        }
    }

    const float content_x     = lm->content_x + CONTENT_PADDING_X;
    const float content_r     = lm->content_r - CONTENT_PADDING_X;
    const float button_top    = top_text_cy - GRID_BUTTON_HEIGHT * 0.5f;
    const float button_bottom = top_text_cy + GRID_BUTTON_HEIGHT * 0.5f;

    // Grid slider
    {
        imgui_rect rect;
        rect.x = content_x;
        rect.r = ceilf(content_x + lm->content_scale * 76);
        rect.y = button_top;
        rect.b = button_bottom;

        unsigned events = imgui_get_events_rect(im, 'grid', &rect);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);

        int lfo_idx     = gui->plugin->selected_lfo_idx;
        int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);
        int ngrid       = gui->plugin->lfos[lfo_idx].grid_x[pattern_idx];

        if (events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_DRAG_MOVE))
        {
            static float last_drag_val = 0;
            if (events & IMGUI_EVENT_DRAG_BEGIN)
            {
                last_drag_val = (float)ngrid;
            }

            imgui_drag_value(im, &last_drag_val, 1, 32, 200, IMGUI_DRAG_VERTICAL);
            ngrid = (int)last_drag_val;

            gui->plugin->lfos[lfo_idx].grid_x[pattern_idx] = ngrid;
        }

        nvgSetFontSize(nvg, font_size);
        nvgSetColour(nvg, C_TEXT);
        nvgSetTextAlign(nvg, NVG_ALIGN_CL);
        nvgText(nvg, content_x, top_text_cy, "GRID", NULL);

        // Up & down "buttons"
        float btn_top = floor(top_text_cy - font_size * 0.4f);
        float btn_bot = ceilf(top_text_cy + font_size * 0.35f);

        nvgBeginPath(nvg);
        nvgMoveTo(nvg, rect.r, btn_top + 3.8);
        nvgLineTo(nvg, rect.r - 2.5, btn_top);
        nvgLineTo(nvg, rect.r - 5, btn_top + 3.8);
        nvgClosePath(nvg);

        nvgMoveTo(nvg, rect.r, btn_bot - 3.8);
        nvgLineTo(nvg, rect.r - 2.5, btn_bot);
        nvgLineTo(nvg, rect.r - 5, btn_bot - 3.8);
        nvgClosePath(nvg);

        nvgSetColour(nvg, C_GREY_1);
        nvgFill(nvg);

        nvgSetTextAlign(nvg, NVG_ALIGN_CR);
        char label[8];
        snprintf(label, sizeof(label), "%d", ngrid);
        nvgText(nvg, rect.r - 9, top_text_cy, label, 0);
    }

    // Pattern Length
    /*
    {
        static const char   label_length[]   = "LENGTH";
        static const size_t label_length_len = ARRLEN(label_length) - 1;

        NVGglyphPosition glyphs[label_length_len];

        nvgSetFontSize(nvg, lm->content_scale * 14);
        nvgSetColour(nvg, C_TEXT);
        nvgSetTextAlign(nvg, NVG_ALIGN_CL);

        nvgTextGlyphPositions(nvg, 0, 0, label_length, label_length + label_length_len, glyphs, label_length_len);
        const float label_length_width = glyphs[label_length_len - 1].maxx;

        nvgSetTextAlign(nvg, NVG_ALIGN_CR);
        float label_length_r = content_r - GRID_BUTTON_WIDTH * 2 - GRID_BUTTON_BUTTON_GAP - GRID_BUTTON_TEXT_GAP;
        nvgText(nvg, label_length_r, top_text_cy, label_length, label_length + label_length_len);

        nvgSetTextAlign(nvg, NVG_ALIGN_CL);

        enum
        {
            BUTTON_LENGTH_HALF,
            BUTTON_LENGTH_DOUBLE,
            BUTTON_COUNT,
        };
        imgui_rect buttons[BUTTON_COUNT];

        buttons[BUTTON_LENGTH_HALF].x   = content_r - 2 * GRID_BUTTON_WIDTH - GRID_BUTTON_BUTTON_GAP;
        buttons[BUTTON_LENGTH_DOUBLE].x = content_r - GRID_BUTTON_WIDTH;

        static const char* btn_labels[] = {"÷2", "×2"};

        for (int btn_idx = 0; btn_idx < ARRLEN(buttons); btn_idx++)
        {
            imgui_rect* rect = buttons + btn_idx;

            rect->y = button_top;
            rect->r = rect->x + GRID_BUTTON_WIDTH;
            rect->b = button_bottom;

            unsigned wid    = 'gbtn' + btn_idx;
            unsigned events = imgui_get_events_rect(im, wid, rect);

            // Updates
            if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                int lfo_idx     = gui->plugin->selected_lfo_idx;
                int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

                int pattern_length = gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx];

                if (btn_idx == BUTTON_LENGTH_HALF)
                    next_pattern_length = pattern_length >> 1;
                if (btn_idx == BUTTON_LENGTH_DOUBLE)
                    next_pattern_length = pattern_length << 1;
                next_pattern_length = xm_clampi(next_pattern_length, 1, MAX_PATTERN_LENGTH_PATTERNS);

                // Modifies the normalised LFOPoints stored on the main thread
                // The denormalised 'widget' points
                if (next_pattern_length != pattern_length)
                {
                    const LFOPoint* current_points = gui->plugin->lfos[lfo_idx].points[pattern_idx];
                    int             N              = xarr_len(current_points);

                    xarr_setcap(gui->main_lfo_points, (N * 2));
                    xarr_setlen(gui->main_lfo_points, 0);

                    if (btn_idx == BUTTON_LENGTH_HALF)
                    {
                        // Crop points
                        for (int i = 0; i < N; i++)
                        {
                            LFOPoint pt = current_points[i];
                            if (pt.x <= next_pattern_length)
                            {
                                xarr_push(gui->main_lfo_points, pt);
                            }
                            if (pt.x >= next_pattern_length)
                                break;
                        }
                    }
                    else if (btn_idx == BUTTON_LENGTH_DOUBLE)
                    {
                        // Duplicate pattern

                        // Deep copy
                        memcpy(gui->main_lfo_points, current_points, sizeof(*gui->main_lfo_points) * N);
                        xarr_header(gui->main_lfo_points)->length = N;

                        // Copy & translate points
                        float delta_x = next_pattern_length >> 1;
                        for (int i = 0; i < N; i++)
                        {
                            LFOPoint pt  = current_points[i];
                            pt.x        += delta_x;
                            xarr_push(gui->main_lfo_points, pt);
                        }
                    }

                    // Coalesce duplicate points
                    N = xarr_len(gui->main_lfo_points);
                    for (int i = N - 1; i-- > 0;)
                    {
                        xassert(i >= 0);
                        xassert((i + 1) < xarr_len(gui->main_lfo_points));
                        LFOPoint* pt1 = gui->main_lfo_points + i;
                        LFOPoint* pt2 = gui->main_lfo_points + i + 1;
                        int       cmp = memcmp(pt1, pt2, sizeof(*pt1));
                        if (cmp == 0)
                        {
                            xarr_delete(gui->main_lfo_points, i);
                        }
                    }

                    gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx] = next_pattern_length;

                    gui->should_update_points = false;

                    should_update_audio_lfo_points_with_gui_lfo_points = true;
                }
            }

            float btn_y   = rect->y;
            float text_cy = top_text_cy;
            float btn_cx  = 0.5f * (rect->r + rect->x);

            if (events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
            {
                btn_y   += 1;
                text_cy += 1;
            }
            nvgBeginPath(nvg);
            nvgRoundedRect(nvg, rect->x, btn_y, rect->r - rect->x, rect->b - rect->y, 2);
            nvgSetColour(nvg, C_GREY_3);
            nvgFill(nvg);

            nvgSetColour(nvg, C_GREY_1);
            nvgSetTextAlign(nvg, NVG_ALIGN_CC);
            nvgText(nvg, btn_cx, text_cy, btn_labels[btn_idx], NULL);
        }
    }
    */
    // LFO Rate

    // Rate
    imgui_rect sl_rate = {content_r - 128 * lm->content_scale, button_top, content_r, button_bottom};
    imgui_rect btn_rate_type;
    btn_rate_type.y = sl_rate.y;
    btn_rate_type.b = sl_rate.b;
    btn_rate_type.r = sl_rate.x - 20;
    btn_rate_type.x = btn_rate_type.r - 2 * (sl_rate.b - sl_rate.y);
    imgui_rect btn_retrig;
    btn_retrig.r = btn_rate_type.x - 20;
    btn_retrig.x = btn_retrig.r - 80;
    btn_retrig.y = btn_rate_type.y;
    btn_retrig.b = btn_rate_type.b;
    // Rate type buttons
    {
        int     lfo_idx  = gui->plugin->selected_lfo_idx;
        ParamID param_id = PARAM_RATE_TYPE_LFO_1 + lfo_idx;

        float x      = btn_rate_type.x;
        float height = btn_rate_type.b - btn_rate_type.y;

        unsigned events = imgui_get_events_rect(im, 'rtyp', &btn_rate_type);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            imgui_rect btn_ms  = btn_rate_type;
            btn_ms.x          += height;
            bool is_ms         = imgui_hittest_rect(im->pos_mouse_down, &btn_ms);
            param_set(gui->plugin, param_id, (double)is_ms);
        }

        bool is_ms = gui->plugin->main_params[param_id] >= 0.5f;

        if (is_ms)
            x += height;

        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, x, btn_rate_type.y, height, height, 4);
        nvgSetColour(nvg, C_LIGHT_BLUE_2);
        nvgFill(nvg);

        // Crotchet
        if (is_ms)
            nvgSetColour(nvg, C_LIGHT_BLUE_2);
        else
            nvgSetColour(nvg, C_BG_LFO);
        float cx         = btn_rate_type.x + height * 0.5;
        float cy         = btn_rate_type.y + height * 0.5;
        float crotchet_x = cx - 5 * lm->content_scale;
        float crotchet_y = cy - 7 * lm->content_scale;
        draw_crotchet_svg(nvg, lm->content_scale, crotchet_x, crotchet_y);

        // Label
        nvgSetTextAlign(nvg, NVG_ALIGN_CC);
        if (is_ms)
            nvgSetColour(nvg, C_BG_LFO);
        else
            nvgSetColour(nvg, C_LIGHT_BLUE_2);
        nvgSetFontSize(nvg, lm->content_scale * 12);
        nvgText(nvg, btn_rate_type.x + height * 1.5, btn_rate_type.y + height * 0.5, "MS", NULL);
    }

    // Rate slider
    {
        int     lfo_idx  = gui->plugin->selected_lfo_idx;
        ParamID param_id = 0;

        bool is_sync = false;
        // Find rate type
        {
            ParamID rate_type_id = PARAM_RATE_TYPE_LFO_1 + lfo_idx;
            double  rate_type_d  = main_get_param(gui->plugin, rate_type_id);

            is_sync = rate_type_d < 0.5;
        }
        if (is_sync)
            param_id = PARAM_SYNC_RATE_LFO_1 + lfo_idx;
        else
            param_id = PARAM_SEC_RATE_LFO_1 + lfo_idx;

        xassert(param_id);
        double val = main_get_param(gui->plugin, param_id);

        unsigned events = imgui_get_events_rect(im, 'rate', &sl_rate);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            if (im->left_click_counter >= 2)
            {
                im->left_click_counter = 0;

                val = cplug_getDefaultParameterValue(gui->plugin, param_id);
                param_set(gui->plugin, param_id, val);
            }
        }

        if (events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_DRAG_MOVE))
        {
            static float last_drag_val = 0;
            if (events & IMGUI_EVENT_DRAG_BEGIN)
            {
                last_drag_val = (float)val;
                param_change_begin(gui->plugin, param_id);
            }
            double range_min, range_max;
            cplug_getParameterRange(gui->plugin, param_id, &range_min, &range_max);
            imgui_drag_value(im, &last_drag_val, range_min, range_max, 200, IMGUI_DRAG_VERTICAL);
            if (is_sync)
            {
                double next_val = xm_droundi(last_drag_val);
                val             = next_val;
            }
            else
                val = last_drag_val;
            param_change_update(gui->plugin, param_id, val);
            val += 0;
        }
        if (events & IMGUI_EVENT_DRAG_END)
            param_change_end(gui->plugin, param_id);

        nvgSetFontSize(nvg, font_size);
        nvgSetTextAlign(nvg, NVG_ALIGN_CL);
        nvgSetColour(nvg, C_TEXT);
        nvgText(nvg, sl_rate.x, top_text_cy, "RATE", NULL);

        char label[16] = {0};

        cplug_parameterValueToString(gui->plugin, param_id, label, sizeof(label), val);

        nvgSetTextAlign(nvg, NVG_ALIGN_CR);
        nvgSetColour(nvg, C_GREY_1);
        nvgText(nvg, sl_rate.r - 12, top_text_cy, label, 0);

        // nvgBeginPath(nvg);
        // nvgRect2(nvg, content_r - 80, button_top, content_r, button_bottom);
        // nvgSetColour(nvg, C_WHITE);
        // nvgFill(nvg);

        // Up & down "buttons"
        float btn_top = floor(top_text_cy - font_size * 0.4f);
        float btn_bot = ceilf(top_text_cy + font_size * 0.35f);

        nvgBeginPath(nvg);
        nvgMoveTo(nvg, sl_rate.r, btn_top + 3.8);
        nvgLineTo(nvg, sl_rate.r - 2.5, btn_top);
        nvgLineTo(nvg, sl_rate.r - 5, btn_top + 3.8);
        nvgClosePath(nvg);

        nvgMoveTo(nvg, sl_rate.r, btn_bot - 3.8);
        nvgLineTo(nvg, sl_rate.r - 2.5, btn_bot);
        nvgLineTo(nvg, sl_rate.r - 5, btn_bot - 3.8);
        nvgClosePath(nvg);

        nvgSetColour(nvg, C_GREY_1);
        nvgFill(nvg);
    }

    // Retrig Button
    {
        int     lfo_idx   = gui->plugin->selected_lfo_idx;
        ParamID param_id  = PARAM_RETRIG_LFO_1 + lfo_idx;
        bool    retrig_on = gui->plugin->main_params[param_id] >= 0.5;

        unsigned events = imgui_get_events_rect(im, 'rtrg', &btn_retrig);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            retrig_on = !retrig_on;
            param_set(gui->plugin, param_id, retrig_on);
        }
        // if (events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
        // {
        //     btn_retrig.y += 1;
        //     btn_retrig.b += 1;
        // }
        float cy = (btn_retrig.y + btn_retrig.b) * 0.5f;

        nvgSetTextAlign(nvg, NVG_ALIGN_CL);
        nvgSetColour(nvg, C_TEXT);
        nvgText(nvg, btn_retrig.x, cy, "RETRIG", 0);

        float       checkbox_height   = snapf(lm->content_scale * CHECKBOX_HEIGHT, 2);
        const float stroke_width      = 1;
        const float half_stroke_width = stroke_width * 0.5f;

        NVGcolour checkbox_col = retrig_on ? C_LIGHT_BLUE_2 : C_GRID_SECONDARY;

        imgui_rect checkbox = {
            btn_retrig.r - checkbox_height,
            cy - checkbox_height * 0.5f - 1, // -1 to sit well with text vertical alignment
            btn_retrig.r,
            cy + checkbox_height * 0.5f - 1};

        nvgBeginPath(nvg);
        nvgRect2(
            nvg,
            checkbox.x + half_stroke_width,
            checkbox.y + half_stroke_width,
            checkbox.r - half_stroke_width,
            checkbox.b - half_stroke_width);
        nvgSetColour(nvg, checkbox_col);
        nvgStroke(nvg, stroke_width);

        if (retrig_on)
        {
            nvgBeginPath(nvg);
            nvgRect2(nvg, checkbox.x + 3, checkbox.y + 3, checkbox.r - 3, checkbox.b - 3);
            nvgFill(nvg);
        }
    }

    // LFO Draw shapes
    float shape_x = content_x;
    float shape_y = display_b - CONTENT_PADDING_Y - SHAPES_WIDTH;
    {
        imgui_rect btns[SHAPE_COUNT];
        unsigned   events[SHAPE_COUNT];

        layout_uniform_horizontal(btns, ARRLEN(btns), content_x, shape_y, SHAPES_WIDTH, SHAPES_WIDTH, LAYOUT_START, 0);

        for (int i = 0; i < ARRLEN(btns); i++)
        {
            imgui_rect* rect = btns + i;

            unsigned wid = 'lshp' + i;
            events[i]    = imgui_get_events_rect(im, wid, rect);

#if defined(_WIN32)
#define PAINT_KEY "Ctrl"
#elif defined(__APPLE__)
#define PAINT_KEY "Cmd"
#endif

            tooltip_handle_events(
                &gui->tooltip,
                *rect,
                "Hold the " PAINT_KEY " key on your keyboard while dragging your mouse inside empty space on the LFO "
                "grid to paint the currently selected shape to the grid",
                gui->frame_start_time,
                events[i]);

            if (events[i] & IMGUI_EVENT_MOUSE_ENTER)
            {
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
            }
            if (events[i] & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                gui->plugin->lfo_shape_idx = i;
            }

            if (events[i] & IMGUI_EVENT_MOUSE_LEFT_HOLD)
            {
                rect->y += 1;
                rect->b += 1;
            }

            if (events[i] & IMGUI_EVENT_MOUSE_HOVER)
            {
                NVGcolour col = C_GREY_2;
                col.a         = 0.5f;
                nvgBeginPath(nvg);
                nvgSetColour(nvg, col);
                nvgRoundedRect2(nvg, rect->x, rect->y, rect->r, rect->b, 4);
                nvgFill(nvg);
            }
            else if (i == gui->plugin->lfo_shape_idx)
            {
                nvgBeginPath(nvg);
                nvgSetColour(nvg, C_GREY_3);
                nvgRoundedRect2(nvg, rect->x, rect->y, rect->r, rect->b, 4);
                nvgFill(nvg);
            }
        }

        nvgBeginPath(nvg);
        for (int i = 0; i < ARRLEN(btns); i++)
        {
            const imgui_rect* rect = btns + i;

            imgui_rect inner  = *rect;
            inner.x          += SHAPES_INNER_PADDING;
            inner.y          += SHAPES_INNER_PADDING;
            inner.r          -= SHAPES_INNER_PADDING;
            inner.b          -= SHAPES_INNER_PADDING;

            const enum ShapeButtonType type = i;
            switch (type)
            {
            case SHAPE_FLAT:
            {
                float cy = floorf((inner.y + inner.b) * 0.5f) + 0.5f;
                nvgMoveTo(nvg, inner.x, cy);
                nvgLineTo(nvg, inner.r, cy);
                break;
            }
            case SHAPE_LINEAR_ASC:
                nvgMoveTo(nvg, inner.x, inner.b);
                nvgLineTo(nvg, inner.r, inner.y);
                break;
            case SHAPE_LINEAR_DESC:
                nvgMoveTo(nvg, inner.x, inner.y);
                nvgLineTo(nvg, inner.r, inner.b);
                break;
            case SHAPE_CONCAVE_ASC:
                nvgMoveTo(nvg, inner.x, inner.b);
                nvgQuadTo(nvg, inner.r, inner.b, inner.r, inner.y);
                break;
            case SHAPE_CONVEX_ASC:
                nvgMoveTo(nvg, inner.x, inner.b);
                nvgQuadTo(nvg, inner.x, inner.y, inner.r, inner.y);
                break;
            case SHAPE_CONCAVE_DESC:
                nvgMoveTo(nvg, inner.x, inner.y);
                nvgQuadTo(nvg, inner.x, inner.b, inner.r, inner.b);
                break;
            case SHAPE_CONVEX_DESC:
                nvgMoveTo(nvg, inner.x, inner.y);
                nvgQuadTo(nvg, inner.r, inner.y, inner.r, inner.b);
                break;
            case SHAPE_COSINE_ASC:
            case SHAPE_COSINE_DESC:
            {
                float w = inner.r - inner.x;
                float h = inner.b - inner.y;
                // imgui_pt c = imgui_centre(&inner);
                float cx = inner.x + w * 0.5f;
                float cy = inner.y + h * 0.5f;

                float y1 = type == SHAPE_COSINE_ASC ? inner.y : inner.b;
                float y2 = type == SHAPE_COSINE_ASC ? inner.b : inner.y;
                // More like a cosine
                nvgMoveTo(nvg, inner.x, y2);
                nvgQuadTo(nvg, inner.x + w * 0.25f, y2, cx, cy);
                nvgQuadTo(nvg, inner.x + w * 0.75f, y1, inner.r, y1);

                // more like a circle
                // nvgMoveTo(nvg, inner.x, inner.b);
                // nvgQuadTo(nvg, cx, inner.b, cx, cy);
                // nvgQuadTo(nvg, cx, inner.y, inner.r, inner.y);
                break;
            }
            case SHAPE_TRIANGLE_UP:
                nvgMoveTo(nvg, inner.x, inner.b);
                nvgLineTo(nvg, (inner.x + inner.r) * 0.5f, inner.y);
                nvgLineTo(nvg, inner.r, inner.b);
                break;
            case SHAPE_TRIANGLE_DOWN:
                nvgMoveTo(nvg, inner.x, inner.y);
                nvgLineTo(nvg, (inner.x + inner.r) * 0.5f, inner.b);
                nvgLineTo(nvg, inner.r, inner.y);
                break;
            case SHAPE_COUNT:
                break;
            }
        }
        nvgSetColour(nvg, C_WHITE);
        nvgStroke(nvg, 1.2f);
    }

    // LFO pattern selector
    const imgui_rect pattern_area = {
        .x = xm_maxf(content_r - PATTERN_WIDTH - 4, shape_x),
        .y = shape_y - 16,
        .r = content_r - 4,
        .b = display_b - CONTENT_PADDING_Y};
    {
        imgui_rect sl_pattern  = pattern_area;
        imgui_rect btn_pattern = pattern_area;

        float pattern_area_height = pattern_area.b - pattern_area.y;
        float third_height        = pattern_area_height / 3;
        sl_pattern.b              = sl_pattern.y + third_height;

        btn_pattern.y = sl_pattern.b;
        btn_pattern.b = btn_pattern.y + third_height;

        const ParamID  param_id   = PARAM_PATTERN_LFO_1 + gui->plugin->selected_lfo_idx;
        const unsigned uid        = 'prm' + param_id;
        const unsigned sl_events  = imgui_get_events_rect(im, uid, &sl_pattern);
        const unsigned btn_events = imgui_get_events_rect(im, uid + 'btn', &btn_pattern);

        float w  = sl_pattern.r - sl_pattern.x;
        float w8 = w / NUM_LFO_PATTERNS;

        tooltip_handle_events(
            &gui->tooltip,
            pattern_area,
            "Switch between custom LFO shapes for this LFO",
            gui->frame_start_time,
            sl_events);

        float pattern_cx = 0.5f * (pattern_area.x + pattern_area.r);
        float pattern_cy = (pattern_area.y + pattern_area.b) * 0.5f;

        float value_f = (float)gui->plugin->main_params[param_id];

        float next_value = value_f;

        int btn_idx = -1;
        if (btn_events & IMGUI_EVENT_MOUSE_HOVER)
        {
            float diff = im->pos_mouse_move.x - sl_pattern.x;
            btn_idx    = diff / w8;
        }

        if (sl_events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_WE);
        if (btn_events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);

        if (sl_events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_TOUCHPAD_BEGIN))
        {
            param_change_begin(gui->plugin, param_id);
        }
        if (sl_events & IMGUI_EVENT_DRAG_MOVE)
            imgui_drag_value(im, &next_value, 0, 1, pattern_area.r - pattern_area.x, IMGUI_DRAG_HORIZONTAL);

        if (sl_events & IMGUI_EVENT_TOUCHPAD_MOVE)
        {
            float delta = im->frame.delta_touchpad.x / PATTERN_WIDTH;
            if (im->frame.modifiers_touchpad & PW_MOD_INVERTED_SCROLL)
                delta = -delta;
            if (im->frame.modifiers_touchpad & PW_MOD_PLATFORM_KEY_CTRL)
                delta *= 0.1f;
            if (im->frame.modifiers_touchpad & PW_MOD_KEY_SHIFT)
                delta *= 0.1f;

            next_value = xm_clampf(value_f + delta, 0, 1);
        }

        if (btn_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            xassert(btn_idx > -1);
            next_value = (float)btn_idx / (NUM_LFO_PATTERNS - 1);
        }
        bool changed = value_f != next_value;
        if (changed)
        {
            value_f = next_value;
            param_change_update(gui->plugin, param_id, value_f);
            gui->gui_lfo_points_valid = false;
            should_clear_lfo_trail    = true;
        }

        if (sl_events & (IMGUI_EVENT_DRAG_END | IMGUI_EVENT_TOUCHPAD_END))
        {
            int vi  = xm_droundi(xm_lerpd(value_f, 1, NUM_LFO_PATTERNS));
            value_f = xm_normf(vi, 1, NUM_LFO_PATTERNS);

            param_change_update(gui->plugin, param_id, value_f);
            param_change_end(gui->plugin, param_id);
            gui->gui_lfo_points_valid = false;
        }

        if (sl_events & IMGUI_EVENT_MOUSE_WHEEL)
        {
            int vi  = xm_droundi(xm_lerpd(value_f, 1, NUM_LFO_PATTERNS));
            vi     += im->frame.delta_mouse_wheel;
            vi      = xm_clampi(vi, 1, NUM_LFO_PATTERNS);

            value_f = xm_normd(vi, 1, NUM_LFO_PATTERNS);

            if (sl_events & IMGUI_EVENT_MOUSE_WHEEL)
                param_set(gui->plugin, param_id, value_f);

            gui->gui_lfo_points_valid = false;
        }

        int vi  = xm_droundi(xm_lerpd(value_f, 1, NUM_LFO_PATTERNS));
        value_f = xm_normd(vi, 1, NUM_LFO_PATTERNS);

        if (btn_idx > -1)
        {
            nvgBeginPath(nvg);
            float y = btn_pattern.y;
            if (btn_events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
                y += 1;
            nvgRoundedRect(nvg, btn_pattern.x + w8 * btn_idx, y, w8, third_height, 4);
            nvgSetColour(nvg, (NVGcolour){1, 1, 1, 0.1});
            nvgFill(nvg);
        }

        nvgSetTextAlign(nvg, NVG_ALIGN_BC);
        nvgSetColour(nvg, C_TEXT);
        nvgText(nvg, pattern_cx, pattern_area.b, "PATTERN", NULL);

        nvgSetTextAlign(nvg, NVG_ALIGN_CC);
        float btn_text_x         = pattern_area.x + w8 * 0.5f;
        int   btn_mouse_down_idx = -1;
        if (btn_events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
            btn_mouse_down_idx = btn_idx;
        for (int i = 0; i < NUM_LFO_PATTERNS; i++)
        {
            char label[4]  = {'1', 0, 0, 0};
            label[0]      += i;
            xassert(i < 8); // oops you might be incrementing "1" past 10
            float y = pattern_cy + 1;
            if (i == btn_mouse_down_idx)
                y += 1;
            nvgText(nvg, btn_text_x + i * w8, y, label, label + 1);
        }

        const float pattern_pos_x = xm_lerpf(value_f, pattern_area.x + w8 * 0.5f, pattern_area.r - w8 * 0.5f);

        float tri_y = floorf(pattern_area.y);
        float tri_b = tri_y + PATTERN_TRIANGLE_HEIGHT;

        nvgBeginPath(nvg);
        nvgMoveTo(nvg, pattern_pos_x, tri_b);
        nvgLineTo(nvg, pattern_pos_x - PATTERN_TRIANGLE_HEIGHT + 2, tri_y);
        nvgLineTo(nvg, pattern_pos_x + PATTERN_TRIANGLE_HEIGHT - 2, tri_y);
        nvgClosePath(nvg);
        nvgFill(nvg);
    }

    // Display grid

    // const float grid_y = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT + DISPLAY_PADDING_TOP;
    const float grid_y = button_bottom + CONTENT_PADDING_Y + LFO_TAB_HEIGHT;
    const float grid_b = shape_y - DISPLAY_PADDING_BOTTOM;
    const float grid_x = lm->content_x + CONTENT_PADDING_X;
    const float grid_r = lm->content_r - CONTENT_PADDING_X;
    const float grid_w = ceilf(grid_r - grid_x);

    imgui_rect grid_bg = {grid_x, grid_y, grid_r, grid_b};

    const int lfo_idx     = gui->plugin->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

    // const float pattern_length = gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx];
    const float pattern_length = 1;
    const int   num_grid_x     = pattern_length * gui->plugin->lfos[lfo_idx].grid_x[pattern_idx];
    // const int   num_grid_y     = gui->plugin->lfos[lfo_idx].grid_y[pattern_idx];
    const int num_grid_y = num_grid_x;

    bool should_clear = false;
    if (im->frame.events & IMGUI_FLAGS_PW_MOUSE_DOWN_EVENTS)
    {
        bool hit = imgui_hittest_rect(im->pos_mouse_down, &grid_bg);
        if (!hit)
            should_clear = true;
    }
    should_clear |= !!(im->frame.events & (1 << PW_EVENT_RESIZE));
    if (should_clear)
        clear_selection(gui);

    if (!gui->gui_lfo_points_valid)
    {
        gui->gui_lfo_points_valid = !gui->gui_lfo_points_valid;
        LFOPoint* lfo_points      = gui->plugin->lfos[lfo_idx].points[pattern_idx];

        // deep copy audio lfo points array to gui
        size_t N = xarr_len(lfo_points);
        xarr_setlen(gui->main_lfo_points, N);
        _Static_assert(sizeof(*gui->main_lfo_points) == sizeof(*lfo_points), "");
        memcpy(gui->main_lfo_points, lfo_points, sizeof(*lfo_points) * N);

        gui->should_update_points = false;
    }

    if (!gui->should_update_points)
    {
        gui->should_update_points = !gui->should_update_points;
        should_update_cached_path = true;

        clear_selection(gui);

        const int N = xarr_len(gui->main_lfo_points);

        const LFOPoint* it  = gui->main_lfo_points;
        const LFOPoint* end = it + N;

        xarr_setlen(gui->points, (N + 1));
        xarr_setlen(gui->skew_points, N);

        imgui_pt* p = gui->points;

        // scale beat time to px with one multiply
        const float beattime_scale = grid_w / pattern_length;

        while (it != end)
        {
            p->x = grid_x + it->x * beattime_scale;
            p->y = xm_lerpf(it->y, grid_b, grid_y);
            it++;
            p++;
        }
        // last Y point matches first point
        p->x = grid_r;
        p->y = gui->points->y;

        it           = gui->main_lfo_points;
        p            = gui->points;
        imgui_pt* sp = gui->skew_points;

        while (it != end)
        {
            imgui_pt* next_p = p + 1;

            if (p->x == next_p->x) // the line between point & next_p is vertical
            {
                sp->x = p->x;
                // display skew point vertically, halfway between points
                // skew amount not considered
                sp->y = (p->y + next_p->y) * 0.5f;
            }
            else
            {
                float y = interp_points(0.5f, 1 - it->skew, p->y, next_p->y);

                // x is always halfway between points
                sp->x = (p->x + next_p->x) * 0.5f;
                // skew amount controls y coord
                sp->y = y;
            }

            it++;
            p++;
            sp++;
        }

        xarr_setlen(gui->points_copy, (N + 1));
        xarr_setlen(gui->skew_points_copy, N);
        memcpy(gui->points_copy, gui->points, sizeof(*gui->points) * (N + 1));
        memcpy(gui->skew_points_copy, gui->skew_points, sizeof(*gui->skew_points_copy) * N);
    }

    int pt_hover_idx      = -1;
    int pt_hover_skew_idx = -1;
    int delete_pt_idx     = -1;

    // Point events
    {
        const int num_points    = xarr_len(gui->points_copy);
        bool      backup_points = false;

        for (int pt_idx = 0; pt_idx < num_points; pt_idx++)
        {
            unsigned       uid       = 'lfop' + pt_idx;
            const imgui_pt pt        = gui->points_copy[pt_idx];
            const unsigned pt_events = imgui_get_events_circle(im, uid, pt, LFO_POINT_CLICK_RADIUS);

            if (pt_events == 0)
                continue;

            if (pt_events & IMGUI_EVENT_MOUSE_HOVER)
            {
                pt_hover_idx = pt_idx;
                if (gui->selected_point_idx != -1 && pt_events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
                    pt_hover_idx = gui->selected_point_idx;
            }

            if (pt_events & IMGUI_EVENT_MOUSE_ENTER)
            {
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_DRAGGABLE);
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
                    for (int i = 0; i < xarr_len(gui->selected_point_indexes); i++)
                    {
                        if (select_idx == gui->selected_point_indexes[i])
                        {
                            idx_is_selected = true;
                            break;
                        }
                    }
                    if (shift_click == false && idx_is_selected == false)
                    {
                        clear_selection(gui);
                    }

                    lfo_points_add_selected(gui, select_idx);
                    int num_selected_pts = xarr_len(gui->selected_point_indexes);
                    xassert(num_selected_pts > 0);
                    pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_DRAGGING);
                }
                else if (im->left_click_counter == 2 && select_idx > 0)
                {
                    delete_pt_idx = pt_idx;
                    imgui_clear_widget(im);
                }
            }
            if (pt_events & IMGUI_EVENT_DRAG_BEGIN)
            {
                xarr_setlen(gui->points_copy, num_points);
                xarr_setlen(gui->skew_points_copy, (num_points - 1));
                xassert(0 == memcmp(gui->points_copy, gui->points, sizeof(*gui->points_copy) * num_points));
                xassert(
                    0 ==
                    memcmp(gui->skew_points_copy, gui->skew_points, sizeof(*gui->skew_points_copy) * (num_points - 1)));
            }
            if (pt_events & IMGUI_EVENT_DRAG_MOVE)
            {
                const int num_selected = xarr_len(gui->selected_point_indexes);

                if (num_selected == 1)
                {
                    const bool alt_drag     = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_ALT;
                    const bool snap_to_grid = alt_drag && (num_selected == 1);

                    imgui_pt drag_pos = im->pos_mouse_move;
                    drag_pos.y        = xm_clampf(drag_pos.y, grid_y, grid_b);
                    drag_pos.x        = xm_clampf(drag_pos.x, grid_x, grid_r);
                    if (snap_to_grid)
                    {
                        float x_inc = grid_w / num_grid_x;
                        float y_inc = (grid_b - grid_y) / num_grid_y;
                        for (int j = 0; j <= num_grid_x; j++)
                        {
                            float x = grid_x + j * x_inc;
                            if (x - LFO_POINT_CLICK_RADIUS <= drag_pos.x && drag_pos.x <= x + LFO_POINT_CLICK_RADIUS)
                            {
                                drag_pos.x = x;
                                break;
                            }
                        }
                        for (int j = 0; j <= num_grid_y; j++)
                        {
                            float y = grid_y + j * y_inc;
                            if (y - LFO_POINT_CLICK_RADIUS <= drag_pos.y && drag_pos.y <= y + LFO_POINT_CLICK_RADIUS)
                            {
                                drag_pos.y = y;
                                break;
                            }
                        }
                    }

                    // Points on edges cannot be dragged past other points
                    const int sel_idx = gui->selected_point_indexes[0];
                    if (sel_idx == 0 || sel_idx == num_points - 1)
                    {
                        update_lfo_point(gui, &grid_bg, drag_pos, sel_idx);
                    }
                    else
                    {
                        // Rebuid points array, skipping any points between the beginning and current drag position
                        float range_l = xm_minf(drag_pos.x, im->pos_mouse_down.x);
                        float range_r = xm_maxf(drag_pos.x, im->pos_mouse_down.x);
                        range_l       = xm_maxf(range_l, grid_x);
                        range_r       = xm_minf(range_r, grid_r);

                        const float clamp_range_l = range_l + LFO_POINT_DRAG_ERASE_DISTANCE;
                        const float clamp_range_r = range_r - LFO_POINT_DRAG_ERASE_DISTANCE;

                        float* skew_amts = linked_arena_alloc(gui->arena, sizeof(*skew_amts) * num_points);

                        int npoints             = 0;
                        gui->points[npoints++]  = gui->points_copy[0];
                        gui->selected_point_idx = -1;

                        imgui_pt(*view_pts)[512]      = (void*)gui->points;
                        imgui_pt(*view_skew_pts)[512] = (void*)gui->skew_points;
                        imgui_pt(*view_src_pts)[512]  = (void*)gui->points_copy;

                        for (int j = 1; j < num_points; j++)
                        {
                            float skew = 0.5f;
                            // Calc skew amount from cached points
                            const imgui_pt* p1 = gui->points_copy + j - 1;
                            const imgui_pt* p2 = gui->points_copy + j;
                            const imgui_pt* sp = gui->skew_points_copy + j - 1;
                            if (p1->y != p2->y)
                                skew = xm_normf(sp->y, p2->y, p1->y);
                            if (p1->y > p2->y)
                                skew = 1 - skew;

                            // Update displayed points
                            if (j == sel_idx)
                            {
                                // Defer adding this point so we can give it an opportunity to get clamped
                                gui->selected_point_idx = npoints;
                                npoints++;
                            }
                            else if (j < sel_idx && p2->x <= clamp_range_l)
                            {
                                gui->points[npoints++] = *p2;

                                if (p2->x >= range_l) // Clamp dragged point to nearby point
                                    drag_pos.x = xm_maxf(p2->x, drag_pos.x);
                                xassert(drag_pos.x >= p2->x);
                            }
                            else if (j > sel_idx && p2->x >= clamp_range_r)
                            {
                                gui->points[npoints++] = *p2;
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
                        xassert(gui->selected_point_idx != -1);
                        if (gui->selected_point_indexes >= 0)
                        {
                            pt_hover_idx = gui->selected_point_idx;

                            gui->points[gui->selected_point_idx] = drag_pos;
                        }

                        // Update displayed skew point
                        for (int j = 0; j < npoints - 1; j++)
                        {
                            xassert((j + 1) < npoints);
                            const imgui_pt* p1   = gui->points + j;
                            const imgui_pt* p2   = gui->points + j + 1;
                            imgui_pt*       sp   = gui->skew_points + j;
                            float           skew = skew_amts[j];

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

                        xarr_header(gui->points)->length      = npoints;
                        xarr_header(gui->skew_points)->length = npoints - 1;

                        linked_arena_release(gui->arena, skew_amts);
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
                        int idx = gui->selected_point_indexes[i];

                        xassert(idx != (num_points - 1));

                        bool is_first_point = idx == 0 || idx == (num_points - 1);
                        if (is_first_point && has_moved_first_point)
                            continue;

                        has_moved_first_point |= is_first_point;

                        imgui_pt translate_pos  = gui->points_copy[idx];
                        translate_pos.x        += delta.x;
                        translate_pos.y        += delta.y;

                        update_lfo_point(gui, &grid_bg, translate_pos, idx);
                    }
                }

                should_update_cached_path                = true;
                should_update_gui_lfo_points_with_points = true;
            } // IMGUI_EVENT_DRAG_MOVE

            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_UP)
            {
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_DRAGGABLE);
                backup_points = true;
            }
        } // end loop points

        if (delete_pt_idx > 0)
        {
            delete_lfo_point(gui, delete_pt_idx);
            clear_selection(gui);

            pt_hover_idx      = -1;
            pt_hover_skew_idx = -1;
            backup_points     = true;

            should_update_gui_lfo_points_with_points = true;
            should_update_cached_path                = true;
        }

        if (backup_points)
        {
            copy_points(gui);
            copy_skew_points(gui);
        }
    }

    // Skew point events
    {
        const int num_skew_points = xarr_len(gui->skew_points);
        for (int pt_idx = 0; pt_idx < num_skew_points; pt_idx++)
        {
            unsigned       uid       = 'lskp' + pt_idx;
            const imgui_pt pt        = gui->skew_points[pt_idx];
            const unsigned pt_events = imgui_get_events_circle(im, uid, pt, LFO_POINT_CLICK_RADIUS);
            if (pt_events == 0)
                continue;

            if (pt_events & IMGUI_EVENT_MOUSE_HOVER)
                pt_hover_skew_idx = pt_idx;

            if (pt_events & IMGUI_EVENT_MOUSE_ENTER)
            {
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);
            }

            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                clear_selection(gui);

                const bool ctrl = im->frame.modifiers_mouse_down & PW_MOD_PLATFORM_KEY_CTRL;
                if (im->left_click_counter == 2 && !ctrl)
                {
                    update_skew_point(gui, pt_idx, 0.5f);
                    pt_hover_skew_idx                        = -1;
                    should_update_gui_lfo_points_with_points = true;
                    should_update_cached_path                = true;
                }
            }
            if (pt_events & IMGUI_EVENT_DRAG_MOVE)
            {
                float delta = 0;
                imgui_drag_value(im, &delta, -1, 1, LFO_SKEW_DRAG_RANGE * 2, IMGUI_DRAG_VERTICAL);

                float skew      = calculate_point_skew(gui, pt_idx);
                float next_skew = skew + delta;
                next_skew       = xm_clampf(next_skew, 0.0f, 1.0f);
                xassert(next_skew >= 0 && next_skew <= 1);

                update_skew_point(gui, pt_idx, next_skew);
                should_update_gui_lfo_points_with_points = true;
                should_update_cached_path                = true;
            }
            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_UP)
            {
                copy_skew_points(gui);
            }
        }
    }

    const imgui_rect selection_area =
        {lm->content_x + 16, display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT, lm->content_r - 16, pattern_area.y};
    const unsigned grid_events = imgui_get_events_rect(im, 'lgbg', &selection_area);
    if (grid_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
    {
        imgui_pt pos;
        pos.x = floorf(im->pos_mouse_down.x);
        pos.y = floorf(im->pos_mouse_down.y);

        bool should_draw_shape = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_CTRL;
        if (should_draw_shape)
        {
            im->left_click_counter = 0;
            drag_and_draw_lfo_points(gui, pos, &grid_bg);
            should_update_gui_lfo_points_with_points = true;
            should_update_cached_path                = true;
        }
        else if (im->left_click_counter == 1)
        {
            bool shift_click = (PW_MOD_KEY_SHIFT | PW_MOD_LEFT_BUTTON) == im->frame.modifiers_mouse_down;
            if (shift_click == false)
            {
                clear_selection(gui);
            }

            gui->selection_start.x = pos.x;
            gui->selection_start.y = pos.y;
            gui->selection_end.u64 = gui->selection_start.u64;
        }
        else if (im->left_click_counter == 2)
        {
            // add point
            imgui_pt mouse_down = im->pos_mouse_down;

            // user clicked in empty space
            for (int i = xarr_len(gui->points) - 1; i-- != 0;)
            {
                if (mouse_down.x >= gui->points[i].x)
                {
                    imgui_pt pt;
                    pt.x = xm_clampf(mouse_down.x, grid_bg.x, grid_bg.r);
                    pt.y = xm_clampf(mouse_down.y, grid_bg.y, grid_bg.b);

                    insert_lfo_point(gui, pt, i + 1, &grid_bg);

                    copy_points(gui);
                    copy_skew_points(gui);

                    clear_selection(gui);
                    lfo_points_add_selected(gui, i + 1);

                    pt_hover_idx = i + 1;

                    should_update_gui_lfo_points_with_points = true;
                    should_update_cached_path                = true;
                    break;
                }
            }
        }
    }
    if (grid_events & IMGUI_EVENT_MOUSE_LEFT_UP)
    {
        gui->selection_start.u64 = 0;
        gui->selection_end.u64   = 0;
    }
    if (grid_events & IMGUI_EVENT_DRAG_MOVE)
    {
        bool     should_draw_shape = im->frame.modifiers_mouse_move & PW_MOD_PLATFORM_KEY_CTRL;
        imgui_pt pos;
        pos.x = floorf(im->pos_mouse_move.x);
        pos.y = floorf(im->pos_mouse_move.y);

        if (should_draw_shape)
        {
            drag_and_draw_lfo_points(gui, pos, &grid_bg);
            should_update_gui_lfo_points_with_points = true;
            should_update_cached_path                = true;
        }
        else
        {
            gui->selection_end.x = xm_clampf(pos.x, selection_area.x, selection_area.r);
            gui->selection_end.y = xm_clampf(pos.y, selection_area.y, selection_area.b);

            if (gui->selection_start.u64 != 0 && gui->selection_start.u64 != gui->selection_end.u64)
            {
                const imgui_rect area = {
                    xm_minf(gui->selection_start.x, gui->selection_end.x),
                    xm_minf(gui->selection_start.y, gui->selection_end.y),
                    xm_maxf(gui->selection_start.x, gui->selection_end.x) + 1,
                    xm_maxf(gui->selection_start.y, gui->selection_end.y) + 1};

                const int N = xarr_len(gui->points);
                xarr_setcap(gui->selected_point_indexes, N);

                for (int i = 0; i < N; i++)
                {
                    const imgui_pt pt = gui->points[i];
                    if (imgui_hittest_rect(pt, &area))
                        lfo_points_add_selected(gui, i);
                }
            }
        }
    }
    if (grid_events & IMGUI_EVENT_MOUSE_ENTER)
    {
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_DEFAULT);
    }

    if (should_update_gui_lfo_points_with_points)
    {
        should_update_audio_lfo_points_with_gui_lfo_points = true;
        // Queue LFO points
        int npoints = xarr_len(gui->skew_points);
        xarr_setlen(gui->main_lfo_points, npoints);
        for (int i = 0; i < npoints; i++)
        {
            LFOPoint* p1 = gui->main_lfo_points + i;
            p1->x        = pattern_length * snap_point(xm_normd(gui->points[i].x, grid_x, grid_r));
            p1->y        = snap_point(xm_normd(gui->points[i].y, grid_b, grid_y));
            p1->skew     = calculate_point_skew(gui, i);
            xassert(p1->x >= 0 && p1->x <= pattern_length);
            xassert(p1->y >= 0 && p1->y <= 1);
            xassert(p1->skew >= 0 && p1->skew <= 1);
#ifndef NDEBUG
            // validate we didn't do anything silly
            if (i > 0)
            {
                LFOPoint* prev = gui->main_lfo_points + i - 1;
                xassert(p1->x >= prev->x);
            }
#endif

            i += 0;
        }
        gui->main_lfo_points->x = 0;
    }

    if (should_update_cached_path)
    {
        const int points_cap = grid_w + xarr_len(gui->points);
        xarr_setcap(gui->lfo_cached_path, points_cap);

        imgui_pt* points  = gui->lfo_cached_path;
        int       npoints = 0;
        int       ny      = 0;

        imgui_pt pos = {grid_x, grid_b};

        const imgui_pt* pt      = gui->points;
        const imgui_pt* next_pt = gui->points + 1;
        const imgui_pt* end     = gui->points + xarr_len(gui->points) - 1;
        const imgui_pt* skew_pt = gui->skew_points;

        points[npoints++] = *pt;
        while (pt != end)
        {
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
                pos.y = interp_points(norm_pos, skew_amt, pt->y, next_pt->y);

                xassert(ny < xarr_cap(gui->lfo_ybuffer));

                float y_norm           = xm_normf(pos.y, grid_b, grid_y);
                gui->lfo_ybuffer[ny++] = xm_clampf(y_norm, 0, 1);

                points[npoints++] = pos;

                pos.x += 1.0f;
            }

            xarr_header(gui->lfo_ybuffer)->length = ny;
        }

        imgui_pt(*view_points)[1024] = (void*)gui->lfo_cached_path;

        xarr_header(gui->lfo_cached_path)->length = npoints;

        gui->lfo_grid_area.x = grid_x;
        gui->lfo_grid_area.y = grid_y;
        gui->lfo_grid_area.r = grid_r;
        gui->lfo_grid_area.b = grid_b;
    }

    // Draw cosine shape to LFO grid. Useful for approximating cosine shape
    // nvgBeginPath(nvg);
    // for (int i = 0; i <= (int)(grid_w / 4.0f); i++)
    // {
    //     float x  = grid_x + i;
    //     float v  = cosf((float)i / floorf(grid_w / 2.0f) * XM_TAUf);
    //     v       += 1;
    //     v       *= 0.5f;
    //     float y  = xm_lerpf(v, grid_b, grid_y);

    //     if (i == 0)
    //         nvgMoveTo(nvg, x, y);
    //     else
    //         nvgLineTo(nvg, x, y);
    // }
    // nvgSetColour(nvg, C_WHITE);
    // nvgStroke(nvg, 2);
    // Skew values for 'cosine'

    // x = (1 / 7),     y = 0.05, skew = 0.731994
    // x = 0.3333,      y = 0.25, skew = 0.56
    // x = 0.6666,      y = 0.75, skew = 0.44
    // x = 1 - (1 / 7), y = 0.96, skew = 1 - 0.731994

    // Draw grid
    {
        nvgBeginPath(nvg);
        nvgRect(nvg, grid_x + 0.5f, grid_y + 0.5f, grid_r - grid_x - 1, grid_b - grid_y - 1);
        nvgSetColour(nvg, C_GRID_PRIMARY);
        nvgStroke(nvg, 1);

        // Horizontal subdivisions
        for (int k = 0; k < 2; k++)
        {
            nvgBeginPath(nvg);
            for (int i = 1 + k; i < num_grid_x; i += 2)
            {
                float x = xm_mapf(i, 0, num_grid_x, grid_x, grid_r);
                x       = floorf(x) + 0.5f;
                nvgMoveTo(nvg, x, grid_y + 1);
                nvgLineTo(nvg, x, grid_b - 1);
            }
            // Vertical subdivisions
            for (int i = 1 + k; i < num_grid_y; i += 2)
            {
                float y = xm_mapf(i, 0, num_grid_y, grid_y, grid_b);
                y       = floorf(y) + 0.5f;
                nvgMoveTo(nvg, grid_x + 1, y);
                nvgLineTo(nvg, grid_r - 1, y);
            }
            if (num_grid_x > 1 || num_grid_y > 1)
            {
                if (k == 0)
                    nvgSetColour(nvg, C_GRID_TERTIARY);
                else
                    nvgSetColour(nvg, C_GRID_SECONDARY);
                nvgStroke(nvg, 1);
            }
        }

        // Horiztonal beats
        nvgBeginPath(nvg);
        for (int i = 1; i < pattern_length; i++)
        {
            float x = xm_mapf(i, 0, pattern_length, grid_x, grid_r);
            x       = floorf(x) + 0.5f;
            nvgMoveTo(nvg, x, grid_y + 1);
            nvgLineTo(nvg, x, grid_b - 1);
        }
        nvgSetColour(nvg, C_GRID_PRIMARY);
        nvgStroke(nvg, 1);

        const char** labels_arr = NULL;
        int          num_labels = 0;
        /*
        static const char* labels_1_beat[]  = {"0", "1/4"};
        static const char* labels_2_beats[] = {"0", "1/4", "1/2"};
        static const char* labels_1_bar[]   = {"0", "1/4", "1/2", "3/4", "1 bar"};
        static const char* labels_2_bars[]  = {"0", "1/2", "1 bar", "1 1/2", "2 bar"};
        static const char* labels_4_bars[]  = {"0", "1 bar", "2 bar", "3 bar", "4 bar"};
        if (pattern_length == 1)
        {
            labels_arr = labels_1_beat;
            num_labels = ARRLEN(labels_1_beat);
        }
        else if (pattern_length == 2)
        {
            labels_arr = labels_2_beats;
            num_labels = ARRLEN(labels_2_beats);
        }
        else if (pattern_length == 4)
        {
            labels_arr = labels_1_bar;
            num_labels = ARRLEN(labels_1_bar);
        }
        else if (pattern_length == 8)
        {
            labels_arr = labels_2_bars;
            num_labels = ARRLEN(labels_2_bars);
        }
        else if (pattern_length == 16)
        {
            labels_arr = labels_4_bars;
            num_labels = ARRLEN(labels_4_bars);
        }
        */
        ParamID param_id  = PARAM_SYNC_RATE_LFO_1 + lfo_idx;
        LFORate rate_type = (int)main_get_param(gui->plugin, param_id);
        switch (rate_type)
        {
        case LFO_RATE_4_BARS:
            static const char* labels_4_bars[] = {"0", "1", "2", "3", "4 bars"};
            labels_arr                         = labels_4_bars;
            num_labels                         = ARRLEN(labels_4_bars);
            break;
        case LFO_RATE_2_BARS:
            static const char* labels_2_bars[] = {"0", "1 / 2", "1", "1 1/ 2", "2 bars"};
            labels_arr                         = labels_2_bars;
            num_labels                         = ARRLEN(labels_2_bars);
            break;
        case LFO_RATE_1_BAR:
            static const char* labels_1_bar[] = {"0", "1 / 4", "1 / 2", "3 / 4", "1 bar"};
            labels_arr                        = labels_1_bar;
            num_labels                        = ARRLEN(labels_1_bar);
            break;
        case LFO_RATE_3_4:
            static const char* labels_3_4[] = {"0", "1 / 4", "1 / 2", "3 / 4"};
            labels_arr                      = labels_3_4;
            num_labels                      = ARRLEN(labels_3_4);
            break;
        case LFO_RATE_2_3:
            static const char* labels_2_3[] = {"0", "1 / 6", "1 / 3", "1 / 2", "2 / 3"};
            labels_arr                      = labels_2_3;
            num_labels                      = ARRLEN(labels_2_3);
            break;
        case LFO_RATE_1_2:
            static const char* labels_1_2[] = {"0", "1 / 8", "1 / 4", "3 / 8", "1 / 2"};
            labels_arr                      = labels_2_3;
            num_labels                      = ARRLEN(labels_2_3);
            break;
        case LFO_RATE_3_8:
            static const char* labels_3_8[] = {"0", "1 / 8", "1 / 4", "3 / 8"};
            labels_arr                      = labels_3_8;
            num_labels                      = ARRLEN(labels_3_8);
            break;
        case LFO_RATE_1_3:
            static const char* labels_1_3[] = {"0", "1 / 12", "1 / 6", "1 / 3"};
            labels_arr                      = labels_1_3;
            num_labels                      = ARRLEN(labels_1_3);
            break;
        case LFO_RATE_1_4:
            static const char* labels_1_4[] = {"0", "1 / 16", "1 / 8", "3 / 16", "1 / 4"};
            labels_arr                      = labels_1_4;
            num_labels                      = ARRLEN(labels_1_4);
            break;
        case LFO_RATE_3_16:
            static const char* labels_3_16[] = {"0", "1 / 16", "1 / 8", "3 / 16"};
            labels_arr                       = labels_3_16;
            num_labels                       = ARRLEN(labels_3_16);
            break;
        case LFO_RATE_1_6:
            static const char* labels_1_6[] = {"0", "1 / 24", "1 / 12", "1 / 8", "1 / 6"};
            labels_arr                      = labels_1_6;
            num_labels                      = ARRLEN(labels_1_6);
            break;
        case LFO_RATE_1_8:
            static const char* labels_1_8[] = {"0", "1 / 32", "1 / 16", "3 / 32", "1 / 8"};
            labels_arr                      = labels_1_8;
            num_labels                      = ARRLEN(labels_1_8);
            break;
        case LFO_RATE_1_12:
            static const char* labels_1_12[] = {"0", "1 / 48", "1 / 24", "3 / 48", "1 / 12"};
            labels_arr                       = labels_1_12;
            num_labels                       = ARRLEN(labels_1_12);
            break;
        case LFO_RATE_1_16:
            static const char* labels_1_16[] = {"0", "1 / 64", "1 / 32", "3 / 64", "1 / 16"};
            labels_arr                       = labels_1_16;
            num_labels                       = ARRLEN(labels_1_16);
            break;
        case LFO_RATE_1_24:
            static const char* labels_1_24[] = {"0", "1 / 96", "1 / 48", "3 / 96", "1 / 24"};
            labels_arr                       = labels_1_24;
            num_labels                       = ARRLEN(labels_1_24);
            break;
        case LFO_RATE_1_32:
            static const char* labels_1_32[] = {"0", "1 / 128", "1 / 64", "3 / 128", "1 / 32"};
            labels_arr                       = labels_1_32;
            num_labels                       = ARRLEN(labels_1_32);
            break;
        case LFO_RATE_1_48:
            static const char* labels_1_48[] = {"0", "1 / 192", "1 / 96", "3 / 192", "1 / 48"};
            labels_arr                       = labels_1_48;
            num_labels                       = ARRLEN(labels_1_48);
            break;
        case LFO_RATE_1_64:
            static const char* labels_1_64[] = {"0", "1 / 256", "1 / 128", "3 / 256", "1 / 64"};
            labels_arr                       = labels_1_64;
            num_labels                       = ARRLEN(labels_1_64);
            break;
        case LFO_RATE_COUNT:
            break;
        }

        if (num_labels && labels_arr)
        {
            nvgSetColour(nvg, nvgHexColour(0x626A77FF));
            for (int i = 0; i < num_labels; i++)
            {
                int alignment = NVG_ALIGN_BC;
                if (i == 0)
                    alignment = NVG_ALIGN_BL;
                if (i == num_labels - 1)
                    alignment = NVG_ALIGN_BR;

                const char* txt = labels_arr[i];

                float x = xm_mapf(i, 0, num_labels - 1, grid_x, grid_r);
                nvgSetTextAlign(nvg, alignment);
                nvgText(nvg, x, grid_y - 8, txt, 0);
            }
        }
    }

    float playhead = (float)gui->plugin->lfos[lfo_idx].phase;
    playhead       = fmodf(playhead, pattern_length);

    lm->last_lfo_playhead    = lm->current_lfo_playhead;
    lm->current_lfo_playhead = playhead;

    bool retrigger_flag = xt_atomic_exchange_u8(&gui->plugin->gui_retrig_flag, 0);

    // Clear trail on resize
    should_clear_lfo_trail |= !!(im->frame.events & (1 << PW_EVENT_RESIZE));
    // Clear trail on retrigger
    should_clear_lfo_trail |= retrigger_flag;
    // Sync prev playhead on retrigger
    if (retrigger_flag)
        lm->last_lfo_playhead = playhead;

    if (should_clear_lfo_trail)
    {
        size_t cap = xarr_cap(gui->lfo_playhead_trail);
        xassert(cap);
        memset(gui->lfo_playhead_trail, 0, cap * sizeof(*gui->lfo_playhead_trail));
    }

    snvg_command_custom(nvg, gui, do_lfo_shaders, NVG_LABEL("LFO shaders"));
    snvg_command_draw_nvg(nvg, NVG_LABEL("LFO path"));

    // Draw path
    {
        const int       N   = xarr_len(gui->lfo_cached_path);
        const imgui_pt* it  = gui->lfo_cached_path;
        const imgui_pt* end = it + N;
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, it->x, it->y);
        while (++it != end)
            nvgLineTo(nvg, it->x, it->y);

        nvgSetColour(nvg, C_LIGHT_BLUE_2);
        nvgStroke(nvg, 2);
    }

    // Draw points
    {
        imgui_pt* hover_pt = NULL;
        if (pt_hover_skew_idx != -1)
            hover_pt = gui->skew_points + pt_hover_skew_idx;
        if (pt_hover_idx != -1)
            hover_pt = gui->points + pt_hover_idx;

        if (hover_pt)
        {
            xassert(delete_pt_idx == -1);
            nvgBeginPath(nvg);
            nvgCircle(nvg, hover_pt->x, hover_pt->y, LFO_POINT_CLICK_RADIUS);
            nvgSetColour(nvg, (NVGcolour){1, 1, 1, 0.2});
            nvgFill(nvg);
        }

        // skew points
        nvgBeginPath(nvg);
        for (int i = 0; i < xarr_len(gui->skew_points); i++)
        {
            imgui_pt pt = gui->skew_points[i];
            nvgCircle(nvg, pt.x, pt.y, LFO_SKEW_POINT_RADIUS);
        }
        nvgSetColour(nvg, C_BG_LFO);
        nvgFill(nvg);

        nvgBeginPath(nvg);
        for (int i = 0; i < xarr_len(gui->skew_points); i++)
        {
            imgui_pt pt = gui->skew_points[i];
            nvgCircle(nvg, pt.x, pt.y, LFO_SKEW_POINT_RADIUS);
        }
        nvgSetColour(nvg, C_LIGHT_BLUE_2);
        nvgStroke(nvg, 1.5);
        // regular points
        uint64_t  selected_points_flags = 0;
        const int num_slected_points    = xarr_len(gui->selected_point_indexes);
        if (num_slected_points == 1)
        {
            xassert(gui->selected_point_idx >= 0 && gui->selected_point_idx < xarr_len(gui->points));
            selected_points_flags |= 1llu << ((uint64_t)gui->selected_point_idx);
        }
        else
        {
            for (int i = 0; i < num_slected_points; i++)
            {
                uint64_t idx = gui->selected_point_indexes[i];
                if (idx < 64)
                    selected_points_flags |= 1llu << idx;
            }
        }

        static const NVGcolour col_normal   = C_LIGHT_BLUE_2;
        static const NVGcolour col_selected = nvgHexColour(0xffffff);
        size_t                 num_points   = xarr_len(gui->points);
        for (uint64_t i = 0; i < num_points; i++)
        {
            imgui_pt pt = gui->points[i];

            // First and last point are the same. Show both as selected
            uint64_t pt_idx = i;
            if (i == num_points - 1)
                pt_idx = 0;

            nvgBeginPath(nvg);
            nvgCircle(nvg, pt.x, pt.y, LFO_POINT_RADIUS);
            if (selected_points_flags & (1llu << pt_idx))
                nvgSetColour(nvg, col_selected);
            else
                nvgSetColour(nvg, col_normal);
            nvgFill(nvg);
        }
    }

    // Todo: make this a white line that follows the LFO path
    // if (playhead < pattern_length)
    // {
    //     float x = xm_mapf(playhead, 0, pattern_length, grid_x, grid_r);
    //     x       = floorf(x) + 0.5f;
    //     nvgBeginPath(nvg);
    //     nvgMoveTo(nvg, x, grid_y + 1);
    //     nvgLineTo(nvg, x, grid_b - 1);
    //     nvgSetColour(nvg, C_WHITE);
    //     nvgStroke(nvg, 1);
    // }

    if (gui->selection_start.u64 != 0 && gui->selection_end.u64 != 0)
    {
        static const NVGcolour col  = {0, 0.5, 1, 1};
        const imgui_rect       area = {
            xm_minf(gui->selection_start.x, gui->selection_end.x),
            xm_minf(gui->selection_start.y, gui->selection_end.y),
            xm_maxf(gui->selection_start.x, gui->selection_end.x),
            xm_maxf(gui->selection_start.y, gui->selection_end.y)};

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

    if (should_update_audio_lfo_points_with_gui_lfo_points)
    {
        LFO*      lfo        = &gui->plugin->lfos[lfo_idx];
        LFOPoint* old_array  = NULL;
        size_t    num_points = xarr_len(gui->main_lfo_points);

        // !!!
        {
            xt_spinlock_lock(&lfo->spinlocks[pattern_idx]);

            if (next_pattern_length)
                xt_atomic_store_i32(&lfo->pattern_length[pattern_idx], next_pattern_length);

            old_array = xt_atomic_exchange_ptr((xt_atomic_ptr_t*)&lfo->points[pattern_idx], gui->main_lfo_points);

            xt_spinlock_unlock(&lfo->spinlocks[pattern_idx]);
        }

        // Deep copy
        xarr_setlen(old_array, num_points);
        memcpy(old_array, lfo->points[pattern_idx], sizeof(*old_array) * num_points);

        gui->main_lfo_points = old_array;
    }

    LINKED_ARENA_LEAK_DETECT_END(gui->arena);
}
