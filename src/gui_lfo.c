#include "dsp.h"
#include "gui.h"
#include "xhl/debug.h"

#include <sort.h>

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
    int           lfo_idx = p->selected_lfo_idx;
    extern double main_get_param(Plugin * p, ParamID id);
    double        v  = main_get_param(p, PARAM_PATTERN_LFO_1 + lfo_idx);
    v               *= NUM_LFO_PATTERNS - 1;
    return xm_droundi(v);
}

float calculate_point_skew(GUI* gui, int idx)
{
    xassert(idx >= 0 && idx < xarr_len(gui->lfo_points));

    const imgui_pt* pt      = gui->lfo_points + idx;
    const imgui_pt* next_pt = gui->lfo_points + idx + 1;
    const imgui_pt* skew_pt = gui->lfo_skew_points + idx;

    float skew = 0.5f;
    if (pt->y != next_pt->y)
        skew = xm_normf(skew_pt->y, next_pt->y, pt->y);
    if (pt->y > next_pt->y)
        skew = 1 - skew;

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

void send_points_to_lfo(GUI* gui, const imgui_rect* area)
{
    const int lfo_idx     = gui->plugin->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

    const int pattern_length = gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx];

    LFOPoint* points     = NULL;
    int       num_points = xarr_len(gui->lfo_points) - 1;
    xarr_setlen(points, num_points);

    for (int i = 0; i < num_points; i++)
    {
        LFOPoint* pt = points + i;
        pt->x        = pattern_length * snap_point(xm_normd(gui->lfo_points[i].x, area->x, area->r));
        pt->y        = snap_point(xm_normd(gui->lfo_points[i].y, area->b, area->y));
        pt->skew     = calculate_point_skew(gui, i);
        xassert(pt->x >= 0 && pt->x <= num_points);
        xassert(pt->y >= 0 && pt->y <= 1);
        xassert(pt->skew >= 0 && pt->skew <= 1);
    }
    points->x = 0;
#ifndef NDEBUG
    for (int i = 0; i < num_points - 1; i++)
    {
        LFOPoint* p1 = points + i;
        LFOPoint* p2 = points + i + 1;
        xassert(p1->x <= p2->x);
    }
#endif

    LFOEvent e;
    e.set_points.type           = EVENT_SET_LFO_POINTS;
    e.set_points.lfo_idx        = lfo_idx;
    e.set_points.pattern_idx    = pattern_idx;
    e.set_points.pattern_length = pattern_length;
    e.set_points.array          = points;
    send_to_audio_event_queue(gui->plugin, (const CplugEvent*)&e);
}

void lfo_points_add_selected(GUI* gui, int idx)
{
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
    xassert(i < xarr_len(gui->lfo_skew_points));
    xassert(i < xarr_len(gui->lfo_points) - 1);
    if (gui->lfo_points[i].x == gui->lfo_points[i + 1].x) // the line between point & next point is vertical
    {
        gui->lfo_skew_points[i].x = gui->lfo_points[i].x;
        // display skew point vertically, halfway between points
        // skew amount not considered
        gui->lfo_skew_points[i].y = (gui->lfo_points[i].y + gui->lfo_points[i + 1].y) * 0.5f;
    }
    else
    {
        // x is always halfway between points
        gui->lfo_skew_points[i].x = (gui->lfo_points[i].x + gui->lfo_points[i + 1].x) * 0.5f;
        // skew amount controls y coord
        const imgui_pt* pt1 = gui->lfo_points + i;
        const imgui_pt* pt2 = gui->lfo_points + i + 1;
        const float     y   = interp_points(0.5f, 1 - skew, pt1->y, pt2->y);

        gui->lfo_skew_points[i].y = y;
    }

    const int lfo_idx     = gui->plugin->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);
    LFOPoint* lfo_points  = gui->plugin->lfos[lfo_idx].points[pattern_idx];
    lfo_points[i].skew    = skew;

    gui->lfo_cached_path_dirty = true;
}

// Clamps target_pos to boundaries. Updates relevant skew points. Updates LFO points on audio thread
void update_lfo_point(GUI* gui, const imgui_rect* area, imgui_pt pos, int idx)
{
    const int lfo_idx     = gui->plugin->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

    const LFOPoint* lfopoints  = gui->plugin->lfos[lfo_idx].points[pattern_idx];
    const size_t    num_points = xarr_len(gui->lfo_points);

    xvec2f range_horizontal;

    if (idx == 0)
        range_horizontal.left = area->x;
    else
        range_horizontal.left = gui->lfo_points[idx - 1].x;

    if (idx == 0)
        range_horizontal.right = area->x;
    else if (idx == num_points - 1)
        range_horizontal.right = area->r;
    else
        range_horizontal.right = gui->lfo_points[idx + 1].x;

    imgui_pt* pt = &gui->lfo_points[idx];
    pt->x        = xm_clampf(pos.x, range_horizontal.l, range_horizontal.r);
    pt->y        = xm_clampf(pos.y, area->y, area->b);

    if (idx == 0)
        gui->lfo_points[num_points - 1].y = pt->y;

    update_skew_point(gui, idx, lfopoints[idx].skew);
    if (idx > 0)
        update_skew_point(gui, idx - 1, lfopoints[idx - 1].skew);

    if (idx == 0)
    {
        const int lastSkewIdx = (int)xarr_len(gui->lfo_skew_points) - 1;
        update_skew_point(gui, lastSkewIdx, lfopoints[lastSkewIdx].skew);
    }
}

static void insert_lfo_point(GUI* gui, imgui_pt pos, int idx, const imgui_rect* area)
{
    const int prev_idx = idx - 1;

#ifndef NDEBUG
    size_t gui_pt_len = xarr_len(gui->lfo_points);
    xassert(idx > 0);
    xassert(idx < gui_pt_len);
    xassert(prev_idx >= 0);
    imgui_pt prev_pt = gui->lfo_points[prev_idx];
    xassert(prev_pt.x <= pos.x);
#endif

    float skew = calculate_point_skew(gui, prev_idx);

    // add points locally
    xarr_insert(gui->lfo_points, idx, pos);
    xarr_insert(gui->lfo_skew_points, prev_idx, pos);

    update_skew_point(gui, prev_idx, skew);
    if (idx < xarr_len(gui->lfo_skew_points))
        update_skew_point(gui, idx, 0.5f);

    gui->lfo_cached_path_dirty = true;
}

static void delete_lfo_point(GUI* gui, int idx)
{
    xassert(idx > 0);
    xassert(idx != xarr_len(gui->lfo_skew_points));
    xassert(xarr_len(gui->lfo_skew_points) == (xarr_len(gui->lfo_points) - 1));

    xarr_delete(gui->lfo_points, idx);
    xarr_delete(gui->lfo_skew_points, idx - 1);
    // when user clears the "last point", reset neighbouring skew amounts
    update_skew_point(gui, idx - 1, 0.5f);
}

static void copy_points(GUI* gui)
{
    int N = xarr_len(gui->lfo_points);
    xarr_setlen(gui->points_copy, N);
    memcpy(gui->points_copy, gui->lfo_points, N * sizeof(*gui->points_copy));
}
static void copy_skew_points(GUI* gui)
{
    int N = xarr_len(gui->lfo_skew_points);
    xarr_setlen(gui->skew_points_copy, N);
    memcpy(gui->skew_points_copy, gui->lfo_skew_points, N * sizeof(*gui->skew_points_copy));
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

    const float pattern_length = (float)gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx];

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
        pt_y_left = area->b;
        break;
    case SHAPE_LINEAR_DESC:
    case SHAPE_CONVEX_DESC:
    case SHAPE_CONCAVE_DESC:
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
        const int num_points =
            xarr_len(gui->lfo_points); // note, len(gui->lfo_points) is the same as len(lfo->skew_points) + 1
        for (int i = num_points - 1; i-- != 1;)
        {
            xassert(i > 0);
            bool between  = boundary_left < gui->lfo_points[i].x;
            between      &= gui->lfo_points[i].x < boundary_right;

            if (between)
                delete_lfo_point(gui, i);
        }
    }

    // Count points at boundaries
    int num_points                   = xarr_len(gui->lfo_points);
    int right_idx                    = -1;
    int num_points_at_right_boundary = 0;
    int left_idx                     = -1;
    int num_points_at_left_boundary  = 0;

    // Count right
    {
        for (int i = num_points; i-- != 0;)
        {
            if (gui->lfo_points[i].x == boundary_right)
            {
                num_points_at_right_boundary++;
            }
            if (gui->lfo_points[i].x < boundary_right)
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
            if (gui->lfo_points[i].x == boundary_left)
            {
                num_points_at_left_boundary++;
            }
            if (gui->lfo_points[i].x > boundary_left)
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

            const imgui_pt* pt      = gui->lfo_points + right_idx - 1;
            const imgui_pt* next_pt = gui->lfo_points + right_idx;
            const imgui_pt* skew_pt = gui->lfo_skew_points + right_idx - 1;

            float amt      = xm_normf(boundary_right, pt->x, next_pt->x);
            interp_y_right = interp_points(amt, 1 - skew, pt->y, next_pt->y);
        }

        if (num_points_at_left_boundary == 0)
        {
            float skew = calculate_point_skew(gui, left_idx);

            const imgui_pt* pt      = gui->lfo_points + left_idx;
            const imgui_pt* next_pt = gui->lfo_points + left_idx + 1;
            const imgui_pt* skew_pt = gui->lfo_skew_points + left_idx;

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
        imgui_pt prev_pt = gui->lfo_points[left_idx];
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
    xassert(num_points_at_right_boundary == 2);

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

    copy_points(gui);
    copy_skew_points(gui);

    send_points_to_lfo(gui, area);
    gui->lfo_cached_path_dirty = true;
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
        GRID_BUTTON_HEIGHT     = 24,
        GRID_BUTTON_BUTTON_GAP = 8,
        GRID_BUTTON_TEXT_GAP   = 16,

        SHAPES_WIDTH         = 40, // LFO shape buttons are square
        SHAPES_INNER_PADDING = 8,

        CONTENT_PADDING_X = 32,
        CONTENT_PADDING_Y = 16,

        PATTERN_WIDTH                = 256,
        PATTERN_NUMBER_LABEL_PADDING = 32,
        PATTERN_SLIDER_WIDTH         = PATTERN_WIDTH - 2 * PATTERN_NUMBER_LABEL_PADDING,
        PATTERN_TRIANGLE_HEIGHT      = 12,

        DISPLAY_PADDING_TOP    = 48,
        DISPLAY_PADDING_BOTTOM = 32,
    };

    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;
    LayoutMetrics* lm  = &gui->layout;

    LFOPoint* next_lfo_points     = NULL;
    int       next_pattern_length = 0;

    float bot_content_height = lm->content_b - lm->top_content_bottom;

    const float display_y   = lm->top_content_bottom + 8;
    const float display_w   = (lm->content_r - lm->content_x) - 2 * 8;
    const float display_h   = bot_content_height - 2 * 8;
    const float display_b   = display_y + display_h;
    const float top_text_cy = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT * 0.5f;

    nvgBeginPath(nvg);
    nvgRoundedRect(nvg, lm->content_x + 8, display_y, display_w, display_h, 6);
    nvgSetColour(nvg, COLOUR_BG_LFO);
    nvgFill(nvg);

    // LFO tabs
    {
        imgui_rect lfo_tabs[2];
        float      gui_cx = lm->width / 2;

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

            if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                gui->plugin->selected_lfo_idx = i;
                gui->lfo_points_dirty         = true;
            }

            NVGcolour  col1, col2;
            const bool is_active = gui->plugin->selected_lfo_idx == i;
            if (is_active)
            {
                col1 = COLOUR_LFO_LINE;
                col2 = COLOUR_BG_LFO;
            }
            else
            {
                col1 = COLOUR_BG_LFO;
                col2 = COLOUR_LFO_LINE;
            }

            if (is_active)
            {
                NVGcolour glow_icol = COLOUR_BLUE_SECONDARY;
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

            nvgSetTextAlign(nvg, NVG_ALIGN_CR);
            nvgText(nvg, rect->r - LFO_TAB_ICON_PADDING, top_text_cy, label, label + 5);
        }
    }

    const float content_x = lm->content_x + CONTENT_PADDING_X;
    const float content_r = lm->content_r - CONTENT_PADDING_X;

    // Grid labels & buttons
    {
        static const char   label_grid[]     = "GRID";
        static const char   label_length[]   = "LENGTH";
        static const size_t label_grid_len   = ARRLEN(label_grid) - 1;
        static const size_t label_length_len = ARRLEN(label_length) - 1;

        NVGglyphPosition glyphs[label_length_len];

        nvgSetFontSize(nvg, 14);
        nvgSetColour(nvg, COLOUR_TEXT);
        nvgSetTextAlign(nvg, NVG_ALIGN_CL);

        nvgTextGlyphPositions(nvg, 0, 0, label_grid, label_grid + label_grid_len, glyphs, label_length_len);
        const float label_grid_width = glyphs[label_grid_len - 1].maxx;

        nvgTextGlyphPositions(nvg, 0, 0, label_length, label_length + label_length_len, glyphs, label_length_len);
        const float label_length_width = glyphs[label_length_len - 1].maxx;

        nvgSetTextAlign(nvg, NVG_ALIGN_CL);
        nvgText(nvg, content_x, top_text_cy, label_grid, label_grid + label_grid_len);

        nvgSetTextAlign(nvg, NVG_ALIGN_CR);
        float label_length_r = content_r - GRID_BUTTON_WIDTH * 2 - GRID_BUTTON_BUTTON_GAP - GRID_BUTTON_TEXT_GAP;
        nvgText(nvg, label_length_r, top_text_cy, label_length, label_length + label_length_len);

        nvgSetTextAlign(nvg, NVG_ALIGN_CL);

        const float button_top    = top_text_cy - GRID_BUTTON_HEIGHT * 0.5f;
        const float button_bottom = top_text_cy + GRID_BUTTON_HEIGHT * 0.5f;
        enum
        {
            BUTTON_GRID_DEC,
            BUTTON_GRID_INC,
            BUTTON_LENGTH_HALF,
            BUTTON_LENGTH_DOUBLE,
            BUTTON_COUNT,
        };
        imgui_rect buttons[BUTTON_COUNT];

        buttons[BUTTON_GRID_DEC].x      = content_x + label_grid_width + GRID_BUTTON_TEXT_GAP;
        buttons[BUTTON_GRID_INC].x      = buttons[BUTTON_GRID_DEC].x + GRID_BUTTON_WIDTH + GRID_BUTTON_BUTTON_GAP;
        buttons[BUTTON_LENGTH_HALF].x   = content_r - 2 * GRID_BUTTON_WIDTH - GRID_BUTTON_BUTTON_GAP;
        buttons[BUTTON_LENGTH_DOUBLE].x = content_r - GRID_BUTTON_WIDTH;

        static const char* btn_labels[] = {"-1", "+1", "÷2", "×2"};

        for (int btn_idx = 0; btn_idx < BUTTON_COUNT; btn_idx++)
        {
            imgui_rect* rect = buttons + btn_idx;

            rect->y = button_top;
            rect->r = rect->x + GRID_BUTTON_WIDTH;
            rect->b = button_bottom;

            unsigned wid    = 'gbtn' + btn_idx;
            unsigned events = imgui_get_events_rect(im, wid, rect);

            if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                int lfo_idx     = gui->plugin->selected_lfo_idx;
                int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

                if (btn_idx == BUTTON_GRID_DEC || btn_idx == BUTTON_GRID_INC)
                {
                    int ngrid = gui->plugin->lfos[lfo_idx].grid_x[pattern_idx];
                    if (btn_idx == BUTTON_GRID_DEC)
                        ngrid--;
                    if (btn_idx == BUTTON_GRID_INC)
                        ngrid++;
                    ngrid = xm_clampi(ngrid, 1, 8);

                    gui->plugin->lfos[lfo_idx].grid_x[pattern_idx] = ngrid;
                }

                if (btn_idx == BUTTON_LENGTH_HALF || btn_idx == BUTTON_LENGTH_DOUBLE)
                {
                    int pattern_length = gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx];

                    if (btn_idx == BUTTON_LENGTH_HALF)
                        next_pattern_length = pattern_length >> 1;
                    if (btn_idx == BUTTON_LENGTH_DOUBLE)
                        next_pattern_length = pattern_length << 1;
                    next_pattern_length = xm_clampi(next_pattern_length, 1, MAX_PATTERN_LENGTH_PATTERNS);

                    if (next_pattern_length != pattern_length)
                    {
                        LFOPoint* current_points = gui->plugin->lfos[lfo_idx].points[pattern_idx];
                        int       N              = xarr_len(current_points);

                        xarr_setcap(next_lfo_points, (N * 2));
                        xarr_setlen(next_lfo_points, 0);

                        if (btn_idx == BUTTON_LENGTH_HALF)
                        {
                            // Crop points
                            for (int i = 0; i < N; i++)
                            {
                                LFOPoint pt = current_points[i];
                                if (pt.x <= next_pattern_length)
                                {
                                    xarr_push(next_lfo_points, pt);
                                }
                                if (pt.x >= next_pattern_length)
                                    break;
                            }
                        }
                        else if (btn_idx == BUTTON_LENGTH_DOUBLE)
                        {
                            // Duplicate pattern

                            // Deep copy
                            memcpy(next_lfo_points, current_points, sizeof(*next_lfo_points) * N);
                            xarr_header(next_lfo_points)->length = N;

                            // Copy & translate points
                            float delta_x = next_pattern_length >> 1;
                            for (int i = 0; i < N; i++)
                            {
                                LFOPoint pt  = current_points[i];
                                pt.x        += delta_x;
                                xarr_push(next_lfo_points, pt);
                            }
                        }

                        // Coalesce duplicate points
                        N = xarr_len(next_lfo_points);
                        for (int i = N - 1; i-- > 0;)
                        {
                            xassert(i >= 0);
                            xassert((i + 1) < xarr_len(next_lfo_points));
                            LFOPoint* pt1 = next_lfo_points + i;
                            LFOPoint* pt2 = next_lfo_points + i + 1;
                            int       cmp = memcmp(pt1, pt2, sizeof(*pt1));
                            if (cmp == 0)
                            {
                                xarr_delete(next_lfo_points, i);
                            }
                        }

                        LFOEvent e;
                        e.set_points.type           = EVENT_SET_LFO_POINTS;
                        e.set_points.lfo_idx        = lfo_idx;
                        e.set_points.pattern_idx    = pattern_idx;
                        e.set_points.pattern_length = next_pattern_length;
                        e.set_points.array          = next_lfo_points;
                        send_to_audio_event_queue(gui->plugin, (const CplugEvent*)&e);
                    }
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
            nvgSetColour(nvg, COLOUR_GREY_3);
            nvgFill(nvg);

            nvgSetColour(nvg, COLOUR_GREY_1);
            nvgSetTextAlign(nvg, NVG_ALIGN_CC);
            nvgText(nvg, btn_cx, text_cy, btn_labels[btn_idx], NULL);
        }
    }

    // LFO Draw shapes
    float shape_x = content_x;
    float shape_y = display_b - CONTENT_PADDING_Y - SHAPES_WIDTH;

    {
        imgui_rect buttons[SHAPE_COUNT];
        unsigned   events[SHAPE_COUNT];

        for (int i = 0; i < ARRLEN(buttons); i++)
        {
            imgui_rect* rect = buttons + i;
            rect->x          = content_x + i * SHAPES_WIDTH;
            rect->y          = shape_y;
            rect->r          = content_x + (i + 1) * SHAPES_WIDTH;
            rect->b          = shape_y + SHAPES_WIDTH;

            unsigned wid = 'lshp' + i;
            events[i]    = imgui_get_events_rect(im, wid, rect);

            if (events[i] & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                gui->plugin->lfo_shape_idx = i;
            }

            if (events[i] & IMGUI_EVENT_MOUSE_LEFT_HOLD)
            {
                rect->y += 1;
                rect->b += 1;
            }
        }

        imgui_rect* active_area = buttons + gui->plugin->lfo_shape_idx;
        nvgBeginPath(nvg);
        nvgSetColour(nvg, COLOUR_GREY_3);
        nvgRoundedRect2(nvg, active_area->x, active_area->y, active_area->r, active_area->b, 4);
        nvgFill(nvg);

        nvgBeginPath(nvg);
        for (int i = 0; i < ARRLEN(buttons); i++)
        {
            const imgui_rect* rect = buttons + i;

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
        nvgSetColour(nvg, COLOUR_WHITE);
        nvgStroke(nvg, 1.2f);
    }

    // LFO pattern selector
    float pattern_r  = content_r;
    float pattern_x  = xm_maxf(pattern_r - PATTERN_WIDTH, shape_x);
    float pattern_cx = 0.5f * (pattern_x + pattern_r);
    float pattern_cy = shape_y + SHAPES_WIDTH * 0.5f;
    float pattern_b  = display_b - CONTENT_PADDING_Y;
    {
        const imgui_rect rect     = {pattern_x, shape_y, pattern_r, pattern_b};
        const ParamID    param_id = PARAM_PATTERN_LFO_1 + gui->plugin->selected_lfo_idx;
        const unsigned   uid      = 'prm' + param_id;
        const unsigned   events   = imgui_get_events_rect(im, uid, &rect);

        double value_d = gui->plugin->main_params[param_id];
        float  value_f = value_d;

        float next_value = value_f;

        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_WE);

        if (events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_TOUCHPAD_BEGIN))
        {
            param_change_begin(gui->plugin, param_id);
        }
        if (events & IMGUI_EVENT_DRAG_MOVE)
            imgui_drag_value(im, &next_value, 0, 1, PATTERN_WIDTH, IMGUI_DRAG_HORIZONTAL);

        if (events & IMGUI_EVENT_TOUCHPAD_MOVE)
        {
            float delta = im->frame.delta_touchpad.y / PATTERN_WIDTH;
            if (im->frame.modifiers_touchpad & PW_MOD_INVERTED_SCROLL)
                delta = -delta;
            if (im->frame.modifiers_touchpad & PW_MOD_PLATFORM_KEY_CTRL)
                delta *= 0.1f;
            if (im->frame.modifiers_touchpad & PW_MOD_KEY_SHIFT)
                delta *= 0.1f;

            next_value = xm_clampf(value_f + delta, 0, 1);
        }
        bool changed = value_f != next_value;
        if (changed)
        {
            value_d = value_f = next_value;

            param_change_update(gui->plugin, param_id, value_d);
            gui->lfo_points_dirty = true;
        }

        if (events & (IMGUI_EVENT_DRAG_END | IMGUI_EVENT_TOUCHPAD_END))
        {
            int vi  = xm_droundi(xm_lerpd(value_d, 1, NUM_LFO_PATTERNS));
            value_f = value_d = xm_normd(vi, 1, NUM_LFO_PATTERNS);

            param_change_update(gui->plugin, param_id, value_d);
            param_change_end(gui->plugin, param_id);
            gui->lfo_points_dirty = true;
        }

        if (events & IMGUI_EVENT_MOUSE_WHEEL)
        {
            int vi  = xm_droundi(xm_lerpd(value_d, 1, NUM_LFO_PATTERNS));
            vi     += im->frame.delta_mouse_wheel;
            vi      = xm_clampi(vi, 1, NUM_LFO_PATTERNS);

            value_f = value_d = xm_normd(vi, 1, NUM_LFO_PATTERNS);

            if (events & IMGUI_EVENT_MOUSE_WHEEL)
                param_set(gui->plugin, param_id, value_d);
            gui->lfo_points_dirty = true;
        }

        int vi  = xm_droundi(xm_lerpd(value_d, 1, NUM_LFO_PATTERNS));
        value_f = value_d = xm_normd(vi, 1, NUM_LFO_PATTERNS);

        nvgSetTextAlign(nvg, NVG_ALIGN_BC);
        nvgSetColour(nvg, COLOUR_TEXT);
        nvgText(nvg, pattern_cx, pattern_b, "PATTERN", NULL);

        nvgSetTextAlign(nvg, NVG_ALIGN_CL);
        nvgText(nvg, pattern_x, pattern_cy, "1", NULL);
        nvgSetTextAlign(nvg, NVG_ALIGN_CR);
        nvgText(nvg, pattern_r, pattern_cy, "8", NULL);

        float pattern_line_y = floorf(pattern_cy) + 0.5f;
        float pattern_line_x = pattern_x + PATTERN_NUMBER_LABEL_PADDING;
        float pattern_line_r = pattern_r - PATTERN_NUMBER_LABEL_PADDING;
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, pattern_line_x, pattern_line_y);
        nvgLineTo(nvg, pattern_line_r, pattern_line_y);
        nvgSetColour(nvg, COLOUR_TEXT);
        nvgStroke(nvg, 1);

        const float pattern_pos_x = xm_lerpf(value_f, pattern_line_x, pattern_line_r);

        float tri_b = ceilf(pattern_line_y - 4);
        float tri_y = tri_b - PATTERN_TRIANGLE_HEIGHT;

        nvgBeginPath(nvg);
        nvgMoveTo(nvg, pattern_pos_x, tri_b);
        nvgLineTo(nvg, pattern_pos_x - PATTERN_TRIANGLE_HEIGHT + 2, tri_y);
        nvgLineTo(nvg, pattern_pos_x + PATTERN_TRIANGLE_HEIGHT - 2, tri_y);
        nvgClosePath(nvg);
        nvgFill(nvg);
    }

    // Display grid

    const float grid_y = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT + DISPLAY_PADDING_TOP;
    const float grid_b = shape_y - DISPLAY_PADDING_BOTTOM;
    const float grid_x = lm->content_x + CONTENT_PADDING_X + 8;
    const float grid_r = lm->content_r - CONTENT_PADDING_X - 8;
    const float grid_w = ceilf(grid_r - grid_x);

    imgui_rect grid_bg = {grid_x, grid_y, grid_r, grid_b};

    const int lfo_idx     = gui->plugin->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(gui->plugin);

    const float pattern_length =
        next_pattern_length ? next_pattern_length : (float)gui->plugin->lfos[lfo_idx].pattern_length[pattern_idx];
    const int num_grid_x = pattern_length * gui->plugin->lfos[lfo_idx].grid_x[pattern_idx];
    const int num_grid_y = gui->plugin->lfos[lfo_idx].grid_y[pattern_idx];

    if (gui->lfo_points_dirty || next_lfo_points)
    {
        gui->lfo_points_dirty      = false;
        gui->lfo_cached_path_dirty = true;

        clear_selection(gui);

        const LFOPoint* lfo_points = next_lfo_points;
        if (!lfo_points)
            lfo_points = gui->plugin->lfos[lfo_idx].points[pattern_idx];
        xassert(lfo_points);

        const int N = xarr_len(lfo_points);

        const LFOPoint* it  = lfo_points;
        const LFOPoint* end = it + N;

        xarr_setlen(gui->lfo_points, (N + 1));
        xarr_setlen(gui->lfo_skew_points, N);

        imgui_pt* p = gui->lfo_points;

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
        p->y = gui->lfo_points->y;

        it           = lfo_points;
        p            = gui->lfo_points;
        imgui_pt* sp = gui->lfo_skew_points;

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
        memcpy(gui->points_copy, gui->lfo_points, sizeof(*gui->lfo_points) * (N + 1));
        memcpy(gui->skew_points_copy, gui->lfo_skew_points, sizeof(*gui->skew_points_copy) * N);
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
                        if (pt_idx == gui->selected_point_indexes[i])
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
                xassert(0 == memcmp(gui->points_copy, gui->lfo_points, sizeof(*gui->points_copy) * num_points));
                xassert(
                    0 == memcmp(
                             gui->skew_points_copy,
                             gui->lfo_skew_points,
                             sizeof(*gui->skew_points_copy) * (num_points - 1)));
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

                        int npoints                = 0;
                        gui->lfo_points[npoints++] = gui->points_copy[0];
                        gui->selected_point_idx    = -1;

                        imgui_pt(*view_pts)[512]      = (void*)gui->lfo_points;
                        imgui_pt(*view_skew_pts)[512] = (void*)gui->lfo_skew_points;
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
                                gui->lfo_points[npoints++] = *p2;

                                if (p2->x >= range_l) // Clamp dragged point to nearby point
                                    drag_pos.x = xm_maxf(p2->x, drag_pos.x);
                                xassert(drag_pos.x >= p2->x);
                            }
                            else if (j > sel_idx && p2->x >= clamp_range_r)
                            {
                                gui->lfo_points[npoints++] = *p2;
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

                            gui->lfo_points[gui->selected_point_idx] = drag_pos;
                        }

                        // Update displayed skew point
                        for (int j = 0; j < npoints - 1; j++)
                        {
                            xassert((j + 1) < npoints);
                            const imgui_pt* p1   = gui->lfo_points + j;
                            const imgui_pt* p2   = gui->lfo_points + j + 1;
                            imgui_pt*       sp   = gui->lfo_skew_points + j;
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

                        xarr_header(gui->lfo_points)->length      = npoints;
                        xarr_header(gui->lfo_skew_points)->length = npoints - 1;

                        linked_arena_release(gui->arena, skew_amts);
                        send_points_to_lfo(gui, &grid_bg);
                        gui->lfo_cached_path_dirty = true;
                    }
                }
                else if (num_selected > 1)
                {
                    const xvec2f delta = {
                        im->pos_mouse_move.x - im->pos_mouse_down.x,
                        im->pos_mouse_move.y - im->pos_mouse_down.y};

                    for (int i = 0; i < num_selected; i++)
                    {
                        int idx = gui->selected_point_indexes[i];

                        imgui_pt translate_pos  = gui->points_copy[idx];
                        translate_pos.x        += delta.x;
                        translate_pos.y        += delta.y;

                        update_lfo_point(gui, &grid_bg, translate_pos, idx);
                    }
                }

                send_points_to_lfo(gui, &grid_bg);
                gui->lfo_cached_path_dirty = true;
            }

            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_UP)
            {
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_DRAGGABLE);
                backup_points = true;
            }
        }

        if (delete_pt_idx > 0)
        {
            delete_lfo_point(gui, delete_pt_idx);

            pt_hover_idx      = -1;
            pt_hover_skew_idx = -1;
            clear_selection(gui);

            send_points_to_lfo(gui, &grid_bg);
            backup_points = true;
        }

        if (backup_points)
        {
            copy_points(gui);
            copy_skew_points(gui);
        }
    }

    // Skew point events
    {
        const int num_skew_points = xarr_len(gui->lfo_skew_points);
        for (int pt_idx = 0; pt_idx < num_skew_points; pt_idx++)
        {
            unsigned       uid       = 'lskp' + pt_idx;
            const imgui_pt pt        = gui->lfo_skew_points[pt_idx];
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
                    pt_hover_skew_idx = -1;

                    LFOEvent e;
                    e.set_skew.type        = EVENT_SET_LFO_SKEW;
                    e.set_skew.lfo_idx     = lfo_idx;
                    e.set_skew.pattern_idx = pattern_idx;
                    e.set_skew.point_idx   = pt_idx;
                    e.set_skew.skew        = 0.5f;
                    send_to_audio_event_queue(gui->plugin, (const CplugEvent*)&e);
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

                LFOEvent e;
                e.set_skew.type        = EVENT_SET_LFO_SKEW;
                e.set_skew.lfo_idx     = lfo_idx;
                e.set_skew.pattern_idx = pattern_idx;
                e.set_skew.point_idx   = pt_idx;
                e.set_skew.skew        = next_skew;
                send_to_audio_event_queue(gui->plugin, (const CplugEvent*)&e);
            }
            if (pt_events & IMGUI_EVENT_MOUSE_LEFT_UP)
            {
                copy_skew_points(gui);
            }
        }
    }

    const unsigned grid_events = imgui_get_events_rect(im, 'lgbg', &grid_bg);
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
            for (int i = xarr_len(gui->lfo_points) - 1; i-- != 0;)
            {
                if (mouse_down.x >= gui->lfo_points[i].x)
                {
                    imgui_pt pt;
                    pt.x = xm_clampf(mouse_down.x, grid_bg.x, grid_bg.r);
                    pt.y = xm_clampf(mouse_down.y, grid_bg.y, grid_bg.b);

                    insert_lfo_point(gui, pt, i + 1, &grid_bg);
                    send_points_to_lfo(gui, &grid_bg);

                    copy_points(gui);
                    copy_skew_points(gui);

                    clear_selection(gui);
                    lfo_points_add_selected(gui, i + 1);

                    pt_hover_idx = i + 1;
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
        }
        else
        {
            gui->selection_end.x = xm_clampf(pos.x, grid_x, grid_r);
            gui->selection_end.y = xm_clampf(pos.y, grid_y, grid_b);

            if (gui->selection_start.u64 != 0 && gui->selection_start.u64 != gui->selection_end.u64)
            {
                const imgui_rect area = {
                    xm_minf(gui->selection_start.x, gui->selection_end.x),
                    xm_minf(gui->selection_start.y, gui->selection_end.y),
                    xm_maxf(gui->selection_start.x, gui->selection_end.x) + 1,
                    xm_maxf(gui->selection_start.y, gui->selection_end.y) + 1};

                const int N = xarr_len(gui->lfo_points);
                xarr_setcap(gui->selected_point_indexes, N);

                for (int i = 0; i < N; i++)
                {
                    const imgui_pt pt = gui->lfo_points[i];
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

    if (gui->lfo_cached_path_dirty)
    {
        gui->lfo_cached_path_dirty = false;

        const int points_cap = grid_w + xarr_len(gui->lfo_points);
        xarr_setcap(gui->lfo_cached_path, points_cap);

        imgui_pt* points  = gui->lfo_cached_path;
        int       npoints = 0;

        imgui_pt pos = {grid_x, grid_b};

        const imgui_pt* pt      = gui->lfo_points;
        const imgui_pt* next_pt = gui->lfo_points + 1;
        const imgui_pt* end     = gui->lfo_points + xarr_len(gui->lfo_points) - 1;
        const imgui_pt* skew_pt = gui->lfo_skew_points;

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

                points[npoints++] = pos;

                pos.x += 1.0f;
            }
        }

        imgui_pt(*view_points)[1024] = (void*)gui->lfo_cached_path;

        xarr_header(gui->lfo_cached_path)->length = npoints;
    }

    // Draw grid
    {
        nvgBeginPath(nvg);
        nvgRect(nvg, grid_x + 0.5f, grid_y + 0.5f, grid_r - grid_x - 1, grid_b - grid_y - 1);
        nvgSetColour(nvg, C_GRID_PRIMARY);
        nvgStroke(nvg, 1);

        // Horizontal subdivisions
        nvgBeginPath(nvg);
        for (int i = 1; i < num_grid_x; i++)
        {
            float x = xm_mapf(i, 0, num_grid_x, grid_x, grid_r);
            x       = floorf(x) + 0.5f;
            nvgMoveTo(nvg, x, grid_y + 1);
            nvgLineTo(nvg, x, grid_b - 1);
        }
        // Vertical subdivisions
        for (int i = 1; i < num_grid_y; i++)
        {
            float y = xm_mapf(i, 0, num_grid_y, grid_y, grid_b);
            y       = floorf(y) + 0.5f;
            nvgMoveTo(nvg, grid_x + 1, y);
            nvgLineTo(nvg, grid_r - 1, y);
        }
        if (num_grid_x > 1 || num_grid_y > 1)
        {
            nvgSetColour(nvg, C_GRID_SECONDARY);
            nvgStroke(nvg, 1);
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

        static const char* labels_1_beat[]  = {"0", "1/4"};
        static const char* labels_2_beats[] = {"0", "1/4", "1/2"};
        static const char* labels_1_bar[]   = {"0", "1/4", "1/2", "3/4", "1 bar"};
        static const char* labels_2_bars[]  = {"0", "1/2", "1 bar", "1 1/2", "2 bar"};
        static const char* labels_4_bars[]  = {"0", "1 bar", "2 bar", "3 bar", "4 bar"};

        const char** labels_arr = NULL;
        int          num_labels = 0;
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

    // Draw path
    {
        const int       N   = xarr_len(gui->lfo_cached_path);
        const imgui_pt* it  = gui->lfo_cached_path;
        const imgui_pt* end = it + N;
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, it->x, it->y);
        while (++it != end)
            nvgLineTo(nvg, it->x, it->y);

        nvgSetColour(nvg, COLOUR_LFO_LINE);
        nvgStroke(nvg, 2);
    }

    // Draw points
    {
        imgui_pt* hover_pt = NULL;
        if (pt_hover_skew_idx != -1)
            hover_pt = gui->lfo_skew_points + pt_hover_skew_idx;
        if (pt_hover_idx != -1)
            hover_pt = gui->lfo_points + pt_hover_idx;

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
        for (int i = 0; i < xarr_len(gui->lfo_skew_points); i++)
        {
            imgui_pt pt = gui->lfo_skew_points[i];
            nvgCircle(nvg, pt.x, pt.y, LFO_SKEW_POINT_RADIUS);
        }
        nvgSetColour(nvg, COLOUR_BG_LFO);
        nvgFill(nvg);

        nvgBeginPath(nvg);
        for (int i = 0; i < xarr_len(gui->lfo_skew_points); i++)
        {
            imgui_pt pt = gui->lfo_skew_points[i];
            nvgCircle(nvg, pt.x, pt.y, LFO_SKEW_POINT_RADIUS);
        }
        nvgSetColour(nvg, COLOUR_LFO_LINE);
        nvgStroke(nvg, 1.5);
        // regular points
        uint64_t  selected_points_flags = 0;
        const int num_slected_points    = xarr_len(gui->selected_point_indexes);
        if (num_slected_points == 1)
        {
            xassert(gui->selected_point_idx >= 0 && gui->selected_point_idx < xarr_len(gui->lfo_points));
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

        static const NVGcolour col_normal   = COLOUR_LFO_LINE;
        static const NVGcolour col_selected = nvgHexColour(0xffff00ff);
        for (uint64_t i = 0; i < xarr_len(gui->lfo_points); i++)
        {
            imgui_pt pt = gui->lfo_points[i];

            nvgBeginPath(nvg);
            nvgCircle(nvg, pt.x, pt.y, LFO_POINT_RADIUS);
            if (selected_points_flags & (1llu << i))
                nvgSetColour(nvg, col_selected);
            else
                nvgSetColour(nvg, col_normal);
            nvgFill(nvg);
        }
    }

    float playhead = (float)gui->plugin->beat_position;
    playhead       = fmodf(playhead, pattern_length);
    if (playhead < pattern_length)
    {
        float x = xm_mapf(playhead, 0, pattern_length, grid_x, grid_r);
        x       = floorf(x) + 0.5f;
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, x, grid_y + 1);
        nvgLineTo(nvg, x, grid_b - 1);
        nvgSetColour(nvg, COLOUR_WHITE);
        nvgStroke(nvg, 1);
    }

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

    LINKED_ARENA_LEAK_DETECT_END(gui->arena);
}
