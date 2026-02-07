#include "dsp.h"
#include "gui.h"

#include <layout.h>
#include <shaders.glsl.h>
#include <sort.h>
#include <stdio.h>
#include <xhl/time.h>

#define LFO_PLAYHEAD_TRAIL_MAX_OPACITY 0.5f

const ResourceID RID_LFO_GRID_AREA = {.u32_1 = 'grid', .u32_2 = 'area'};

static inline int main_get_lfo_pattern_idx(Plugin* p)
{
    int    lfo_idx  = p->selected_lfo_idx;
    double v        = main_get_param(p, PARAM_PATTERN_LFO_1 + lfo_idx);
    v              *= NUM_LFO_PATTERNS - 1;
    return xm_droundi(v);
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
    double time_delta_sec = xtime_convert_ns_to_sec(time_delta_ns);
    // const double reduction_per_second = -48;
    const double reduction_per_second = -60;
    // const double reduction_per_second = -72;
    double reduction_dB = time_delta_sec * reduction_per_second;
    float  scale        = xm_fast_dB_to_gain(reduction_dB);

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
    float  start_y       = gui->lfo_playhead_trail[last_playhead_idx];
    size_t playhead_diff = current_playhead_idx - last_playhead_idx;
    float  inc           = (LFO_PLAYHEAD_TRAIL_MAX_OPACITY - start_y) / (float)playhead_diff;

    float(*view_trail)[512]  = (void*)gui->lfo_playhead_trail;
    view_trail              += 0;

    for (size_t i = 1; i <= playhead_diff; i++)
    {
        size_t idx = last_playhead_idx + i;
        // wrap idx
        if (idx >= len)
            idx -= len;

        float y = start_y + inc * i;
        // float y = LFO_PLAYHEAD_TRAIL_MAX_OPACITY;
        if (y > 1)
            y = 0;
        gui->lfo_playhead_trail[idx] = y;
    }

    sg_pipeline   pip;
    IMPointsArea* grid_area          = &gui->imp.area;
    bool          have_all_resources = true;
    have_all_resources &= resource_get_pipeline(&gui->resource_manager, &pip, lfo_vertical_grad_shader_desc, 0);
    // imgui_rect* grid_area          = NULL;
    // have_all_resources &=
    //     resource_get_data_fixed(&gui->resource_manager, RID_LFO_GRID_AREA, (void**)&grid_area, sizeof(*grid_area),
    //     0);
    if (have_all_resources)
    {
        xassert(grid_area);
        xassert(grid_area->r > grid_area->x);
        xassert(grid_area->b > grid_area->y);

        sg_range range_ybuf     = {.ptr = gui->lfo_ybuffer, .size = len * sizeof(*gui->lfo_ybuffer)};
        sg_range range_playhead = {.ptr = gui->lfo_playhead_trail, .size = len * sizeof(*gui->lfo_playhead_trail)};

        sg_bindings bind = {0};
        xassert(gui->lfo_ybuffer_view.id); // TODO: make view, and udpate it on resize
        bind.views[VIEW_lfo_line_storage_buffer]  = gui->lfo_ybuffer_view;
        bind.views[VIEW_lfo_trail_storage_buffer] = gui->lfo_playhead_trail_view;

        vs_lfo_uniforms_t vs_uniforms = {
            .topleft     = {grid_area->x, grid_area->y},
            .bottomright = {grid_area->r, grid_area->b},
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

        sg_apply_pipeline(pip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(UB_vs_lfo_uniforms, &SG_RANGE(vs_uniforms));
        sg_apply_uniforms(UB_fs_lfo_uniforms, &SG_RANGE(fs_uniforms));

        sg_draw(0, 6, 1);
    }
}

void add_up_down_triangles(XVG* xvg, imgui_rect rect)
{
    float w  = rect.r - rect.x;
    float cy = (rect.b + rect.y) * 0.5f;
    xvg_draw_triangle(xvg, rect.x, cy - w * 0.5f, w, w, 0.0f, 0, C_GREY_1);
    xvg_draw_triangle(xvg, rect.x, cy + w * 0.5f, w, w, 0.5f, 0, C_GREY_1);
}

typedef struct ButtonStripIndexes
{
    int hover_idx;
    int mouse_down_idx;
    int mouse_hold_idx;
} ButtonStripIndexes;
ButtonStripIndexes handle_button_strip(
    GUI*         gui,
    imgui_rect   area,
    unsigned     num_buttons,
    float        width,
    float        padding,
    unsigned     events,
    const char** descriptions,
    unsigned     num_descriptions)
{
    xassert(num_buttons > 1);
    xassert(num_descriptions > 0);
    xassert(descriptions != NULL);
    ButtonStripIndexes data;

    data.hover_idx      = -1;
    data.mouse_down_idx = -1;
    data.mouse_hold_idx = -1;

    float btn_stride = width + padding;

    if (events & IMGUI_EVENT_MOUSE_HOVER)
    {
        float rel_x     = gui->imgui.pos_mouse_move.x - area.x;
        int   idx       = xm_clampi(rel_x / btn_stride, 0, num_buttons - 1);
        float btn_right = area.x + idx * btn_stride + width;
        if (gui->imgui.pos_mouse_move.x < btn_right)
            data.hover_idx = idx;
    }
    if (events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
    {
        float rel_x     = gui->imgui.pos_mouse_down.x - area.x;
        int   idx       = xm_clampi(rel_x / btn_stride, 0, num_buttons - 1);
        float btn_right = area.x + idx * btn_stride + width;
        if (gui->imgui.pos_mouse_down.x < btn_right)
            data.mouse_hold_idx = idx;
    }
    if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        data.mouse_down_idx = data.mouse_hold_idx;

    if (events & IMGUI_EVENT_MOUSE_ENTER)
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);

    int tt_offset_idx = xm_clampi(data.hover_idx, 0, num_buttons - 1);
    int tt_desc_idx   = xm_clampi(data.hover_idx, 0, num_descriptions - 1);

    imgui_rect btn;
    btn.x = area.x + btn_stride * tt_offset_idx;
    btn.y = area.y;
    btn.r = area.x + btn_stride * tt_offset_idx + width;
    btn.b = area.b;
    tooltip_handle_events(&gui->tooltip, btn, descriptions[tt_desc_idx], gui->frame_start_time, events);

    return data;
}

int format_time(double ms, char* buf, size_t buflen)
{
    int ms_rounded = xm_droundi(ms);
    if (ms_rounded < 10) // 10ms
        return snprintf(buf, buflen, "%dms", ms_rounded);
    else if (ms_rounded < 1000) // 100ms
        return snprintf(buf, buflen, "%dms", ms_rounded);
    else if (ms_rounded < 1000)
        return snprintf(buf, buflen, "%dms", ms_rounded);
    else
        return snprintf(buf, buflen, "%.1fs", ms * 0.001);
}

void draw_lfo_section(GUI* gui)
{
    LINKED_ARENA_LEAK_DETECT_BEGIN(gui->arena);

    Plugin*        p   = gui->plugin;
    XVG*           xvg = &gui->xvg;
    imgui_context* im  = &gui->imgui;
    LayoutMetrics* lm  = &gui->layout;
    IMPointsData*  imp = &gui->imp;

    const float SCALE          = lm->param_scale;
    const float PADDING        = floorf(8 * lm->content_scale);
    const float LFO_TAB_WIDTH  = floorf(64 * SCALE);
    const float LFO_TAB_HEIGHT = floorf(28 * SCALE);

    const float GRID_BUTTON_WIDTH      = floorf(32 * SCALE);
    const float GRID_BUTTON_HEIGHT     = floorf(28 * SCALE);
    const float GRID_BUTTON_BUTTON_GAP = floorf(8 * SCALE);
    const float GRID_BUTTON_TEXT_GAP   = floorf(16 * SCALE);

    const float CHECKBOX_HEIGHT = floorf(12 * SCALE);

    // const float SHAPES_WIDTH         = floorf(40 * SCALE); // LFO shape buttons are square
    const float SHAPES_WIDTH = snapf(LFO_TAB_HEIGHT, 2); // LFO shape buttons are square

    const float CONTENT_PADDING_X = floorf(32 * lm->content_scale);
    const float CONTENT_PADDING_Y = floorf(16 * lm->content_scale);

    const float PATTERN_WIDTH                = floorf(200 * SCALE);
    const float PATTERN_NUMBER_LABEL_PADDING = floorf(32 * SCALE);
    const float PATTERN_SLIDER_WIDTH         = PATTERN_WIDTH - 2 * PATTERN_NUMBER_LABEL_PADDING;
    const float PATTERN_TRIANGLE_HEIGHT      = floorf(12 * SCALE);

    const float    DISPLAY_PADDING_TOP    = floorf(48 * lm->content_scale);
    const float    DISPLAY_PADDING_BOTTOM = floorf(32 * lm->content_scale);
    const unsigned FONT_SIZE              = (unsigned)(14 * SCALE);

    if (im->frame.events & ((1 << PW_EVENT_RESIZE_UPDATE) | (1 << PW_EVENT_CONTENT_SCALE_FACTOR_CHANGED)))
    {
        imp->theme.col_line           = C_LIGHT_BLUE_2;
        imp->theme.line_stroke_width  = 2;
        imp->theme.point_click_radius = 12 * SCALE;
        imp->theme.point_radius       = 4 * SCALE;
        imp->theme.skew_point_radius  = 3 * SCALE;

        imp->theme.col_point_hover_bg = 0xffffff33;

        imp->theme.col_skewpoint_inner    = C_BG_LFO;
        imp->theme.col_skewpoint_outer    = C_LIGHT_BLUE_2;
        imp->theme.skewpoint_stroke_width = 1.5f * SCALE;

        imp->theme.col_point          = C_LIGHT_BLUE_2;
        imp->theme.col_point_selected = 0xffff00ff;

        imp->theme.col_selection_box = 0x007fffff;

        imp->points_valid = false;
    }

    IMPointsFrameContext fstate                 = imp_frame_context_new(imp, xvg, im, gui->arena, gui->pw);
    bool                 should_clear_lfo_trail = false;
    // int  next_pattern_length                                = 0;

    const float bot_content_height = lm->content_b - lm->top_content_bottom;

    const float display_y   = lm->top_content_bottom + PADDING;
    const float display_w   = (lm->content_r - lm->content_x) - 2 * PADDING;
    const float display_h   = bot_content_height - 2 * PADDING;
    const float display_b   = display_y + display_h;
    const float top_text_cy = display_y + (CONTENT_PADDING_Y + LFO_TAB_HEIGHT * 0.5f);

    XVGGradient g_bg = xvg_make_linear_gradient(0x242838FF, 0x0C101DFF, 0, display_y, 0, display_b);
    xvg_draw_rectangle_with_gradient(xvg, lm->content_x + PADDING, display_y, display_w, display_h, 6, 0, g_bg);

    // LFO tabs
    {
        float      gui_cx = lm->width * 0.5f;
        imgui_rect lfo_tabs;
        lfo_tabs.x = gui_cx - LFO_TAB_WIDTH - PADDING * 0.5f;
        lfo_tabs.r = gui_cx + LFO_TAB_WIDTH + PADDING * 0.5f;
        lfo_tabs.y = display_y + CONTENT_PADDING_Y;
        lfo_tabs.b = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT;

        const unsigned     events        = imgui_get_events_rect(im, 'ltab', &lfo_tabs);
        static const char* DESCRIPTION[] = {"Displays tabbed contents for the selected LFO"};
        ButtonStripIndexes data =
            handle_button_strip(gui, lfo_tabs, 2, LFO_TAB_WIDTH, PADDING, events, DESCRIPTION, ARRLEN(DESCRIPTION));

        if (data.mouse_down_idx != -1)
        {
            xassert(events & IMGUI_EVENT_MOUSE_LEFT_DOWN);
            p->selected_lfo_idx                          = data.mouse_down_idx;
            imp->main_points_valid                       = false;
            fstate.should_update_main_points_with_points = true;
            should_clear_lfo_trail                       = true;

            lm->current_lfo_playhead = lm->last_lfo_playhead = p->lfos[data.mouse_hold_idx].phase;
        }

        for (int i = 0; i < 2; i++)
        {
            float x = floorf(lfo_tabs.x + i * (LFO_TAB_WIDTH + PADDING));
            float r = floorf(lfo_tabs.x + i * (LFO_TAB_WIDTH + PADDING) + LFO_TAB_WIDTH);
            float y = lfo_tabs.y;
            if (data.mouse_hold_idx == i)
                y += 1;

            bool is_selected = i == p->selected_lfo_idx;
            bool is_hovering = i == data.hover_idx && data.mouse_hold_idx == -1;

            unsigned bg_col   = is_selected ? C_LIGHT_BLUE_2 : C_BTN_HOVER;
            unsigned text_col = is_selected ? C_BG_LFO : is_hovering ? C_LIGHT_BLUE_2 : C_TEXT_DARK_BG;
            // unsigned text_col = is_selected ? C_BG_LFO : is_hovering ? C_WHITE : C_TEXT_DARK_BG;

            if (is_selected)
            {
                unsigned glow_icol   = C_DARK_BLUE & 0xffffff60;
                unsigned glow_ocol   = C_DARK_BLUE & 0xffffff00;
                float    glow_radius = 12;
                // glow
                XVGGradient tab_shadow = xvg_make_shadow(glow_ocol, glow_icol, 0, 0, glow_radius, 0, false);
                xvg_draw_rectangle_with_gradient(
                    xvg,
                    x - glow_radius,
                    y - glow_radius,
                    LFO_TAB_WIDTH + 2 * glow_radius,
                    LFO_TAB_HEIGHT + 2 * glow_radius,
                    4,
                    0,
                    tab_shadow);

                // tab
                xvg_draw_rectangle(xvg, x, y, LFO_TAB_WIDTH, LFO_TAB_HEIGHT, 4, 0, bg_col);
            }
            else
            {
                xvg_draw_rectangle(xvg, x, y, LFO_TAB_WIDTH, LFO_TAB_HEIGHT, 4, 1.1, bg_col);
            }

            char label[]  = "LFO 1";
            label[4]     += i;
            float cx      = x + LFO_TAB_WIDTH * 0.5f;
            float cy      = y + LFO_TAB_HEIGHT * 0.5f;
            xvg_draw_text(xvg, cx, cy, label, label + 5, FONT_SIZE, XVG_ALIGN_CC, text_col);
        }
    }

    const float content_x     = lm->content_x + CONTENT_PADDING_X;
    const float content_r     = lm->content_r - CONTENT_PADDING_X;
    const float button_top    = top_text_cy - GRID_BUTTON_HEIGHT * 0.5f;
    const float button_bottom = top_text_cy + GRID_BUTTON_HEIGHT * 0.5f;

    // Grid slider
    {
        imgui_rect rect;
        rect.x = content_r - ceilf(76 * SCALE);
        rect.r = content_r;
        rect.y = button_top;
        rect.b = button_bottom;

        unsigned events = imgui_get_events_rect(im, 'grid', &rect);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);

        int lfo_idx     = p->selected_lfo_idx;
        int pattern_idx = main_get_lfo_pattern_idx(p);
        int ngrid       = p->lfos[lfo_idx].grid_x[pattern_idx];

        if (events &
            (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_DRAG_MOVE | IMGUI_EVENT_TOUCHPAD_BEGIN | IMGUI_EVENT_TOUCHPAD_MOVE))
        {
            static float last_drag_val = 0;
            if (events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_TOUCHPAD_BEGIN))
            {
                last_drag_val = (float)ngrid;
            }

            float range_min = 1;
            float range_max = 32;
            if (events & IMGUI_EVENT_DRAG_MOVE)
            {
                imgui_drag_value(im, &last_drag_val, range_min, range_max, 200, IMGUI_DRAG_VERTICAL);
            }
            else if (events & IMGUI_EVENT_TOUCHPAD_MOVE)
            {
                float delta = im->frame.delta_touchpad.y / 200;
                if (im->frame.modifiers_touchpad & PW_MOD_INVERTED_SCROLL)
                    delta = -delta;
                if (im->frame.modifiers_touchpad & PW_MOD_PLATFORM_KEY_CTRL)
                    delta *= 0.1f;
                if (im->frame.modifiers_touchpad & PW_MOD_KEY_SHIFT)
                    delta *= 0.1f;
                last_drag_val = xm_clampd(last_drag_val + delta, range_min, range_max);
            }
            ngrid = (int)last_drag_val;

            p->lfos[lfo_idx].grid_x[pattern_idx] = ngrid;
        }
        if (events & IMGUI_EVENT_MOUSE_WHEEL)
        {
            ngrid = xm_clampi(ngrid + im->frame.delta_mouse_wheel, 1, 32);

            p->lfos[lfo_idx].grid_x[pattern_idx] = ngrid;
        }
        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            if (im->left_click_counter >= 2)
            {
                im->left_click_counter -= 2;
                ngrid                   = 4;

                p->lfos[lfo_idx].grid_x[pattern_idx] = ngrid;
            }
        }

        xvg_draw_text(xvg, rect.x, top_text_cy, "GRID", NULL, FONT_SIZE, XVG_ALIGN_CL, C_TEXT_DARK_BG);

        // Up & down "buttons"
        float btn_left = floor(rect.r - 6 * SCALE);
        float btn_top  = floor(top_text_cy - FONT_SIZE * 0.75f);
        float btn_bot  = floor(top_text_cy + FONT_SIZE * 0.4f);

        add_up_down_triangles(xvg, (imgui_rect){btn_left, btn_top, rect.r, btn_bot});
        char label[8];
        snprintf(label, sizeof(label), "%d", ngrid);
        xvg_draw_text(xvg, rect.r - 9 * SCALE, top_text_cy, label, 0, FONT_SIZE, XVG_ALIGN_CR, C_GREY_1);
    }

    // Rate
    imgui_rect sl_rate;
    sl_rate.x = content_r - 128 * SCALE;
    sl_rate.r = content_r;
    sl_rate.b = display_b - CONTENT_PADDING_Y;
    sl_rate.y = sl_rate.b - GRID_BUTTON_HEIGHT;
    imgui_rect btn_rate_type;
    btn_rate_type.y = sl_rate.y;
    btn_rate_type.b = sl_rate.b;
    btn_rate_type.r = sl_rate.x - 20 * SCALE;
    btn_rate_type.x = btn_rate_type.r - 2 * (sl_rate.b - sl_rate.y);
    imgui_rect btn_loop_type;
    btn_loop_type.y = sl_rate.y;
    btn_loop_type.b = sl_rate.b;
    btn_loop_type.r = btn_rate_type.x - 20 * SCALE;
    btn_loop_type.x = btn_loop_type.r - NUM_LOOP_TYPES * (sl_rate.b - sl_rate.y);

    // Rate type buttons
    {
        int     lfo_idx  = p->selected_lfo_idx;
        ParamID param_id = PARAM_RATE_TYPE_LFO_1 + lfo_idx;

        float height = btn_rate_type.b - btn_rate_type.y;

        unsigned events = imgui_get_events_rect(im, 'rtyp', &btn_rate_type);

        static const char* DESCRIPTIONS[] = {
            "Sync rate mode\n"
            "\n"
            "Syncs the rate of the LFO playhead to the DAWs BPM clock",

            "Time rate mode\n"
            "\n"
            "Change the rate of the LFO playhead in seconds/milliseconds",
        };
        static_assert(ARRLEN(DESCRIPTIONS) == 2, "Should be binary/boolean");

        ButtonStripIndexes data =
            handle_button_strip(gui, btn_rate_type, 2, height, 0, events, DESCRIPTIONS, ARRLEN(DESCRIPTIONS));

        if (data.mouse_down_idx != -1)
        {
            xassert(events & IMGUI_EVENT_MOUSE_LEFT_DOWN);
            bool is_ms = data.mouse_down_idx == 1;
            param_set(p, param_id, (double)is_ms);
        }

        bool is_ms        = main_get_param(p, param_id) >= 0.5f;
        int  selected_idx = is_ms ? 1 : 0;

        for (int i = 0; i < 2; i++)
        {
            float x = btn_rate_type.x + height * i;
            float y = btn_rate_type.y;
            if (data.mouse_hold_idx == i)
                y += 1;

            bool is_selected = i == selected_idx;
            bool is_hovering = i == data.hover_idx && data.mouse_hold_idx == -1;

            if (is_selected || is_hovering)
            {
                unsigned bg_col = is_selected ? C_LIGHT_BLUE_2 : C_BTN_HOVER;
                xvg_draw_rectangle(xvg, x, y, height, height, 4 * SCALE, 0, bg_col);
            }

            unsigned icon_col = is_selected ? C_BG_LFO : is_hovering ? C_LIGHT_BLUE_2 : C_TEXT_DARK_BG;

            float cx = x + height * 0.5;
            float cy = y + height * 0.5;
            if (i == 0)
            {
                float crotchet_x = cx - 5 * SCALE;
                float crotchet_y = cy - 7 * SCALE;

                xvec4i      c     = icon_get_coords(&gui->icons, ICON_CROTCHET);
                XVGGradient ifill = (XVGGradient){.colour1 = icon_col};
                xvg_gradient_apply_image(&ifill, gui->icons.view, xvg->smp_linear, c.x, c.y, c.width, c.height);
                xvg_draw_solid_rectangle_with_gradient(xvg, x, y, height, height, ifill);
            }
            else
            {
                xvg_draw_text(xvg, cx, cy, "MS", NULL, 12 * SCALE, XVG_ALIGN_CC, icon_col);
            }
        }
    }

    // Loop type buttons
    {
        int lfo_idx = p->selected_lfo_idx;

        unsigned events = imgui_get_events_rect(im, 'loop', &btn_loop_type);

        static const char* DESCRIPTIONS[] = {
            "Loop mode\n"
            "\n"
            "Causes the LFO playhead to return to 0\% after it reaches 100%.",

            "Retrig mode\n"
            "\n"
            "Uses clever audio peak detection to reset the LFO phase to 0%. Send MIDI to the plugin while MIDI "
            "keytracking is on for the most accurate results.\n"
            "\n"
            "The LFO playhead will return to 0\% after it reaches 100%.",

            "One shot mode\n"
            "\n"
            "Uses clever audio peak detection to reset the LFO phase to 0%. Send MIDI to the plugin while MIDI "
            "keytracking is on for the most accurate results.\n"
            "\n"
            "When the LFO playhead reaches 100\% it will hang there until audio or MIDI causes it to reset to 0%."};
        static_assert(ARRLEN(DESCRIPTIONS) == NUM_LOOP_TYPES, "");

        float height = btn_loop_type.b - btn_loop_type.y;

        ButtonStripIndexes data = handle_button_strip(
            gui,
            btn_loop_type,
            NUM_LOOP_TYPES,
            height,
            0,
            events,
            DESCRIPTIONS,
            ARRLEN(DESCRIPTIONS));

        if (data.mouse_down_idx != -1)
        {
            xassert(events & IMGUI_EVENT_MOUSE_LEFT_DOWN);
            LFOLoopType loop_type     = data.mouse_down_idx;
            p->lfo_loop_type[lfo_idx] = loop_type;
        }

        const LFOLoopType current_loop_type = p->lfo_loop_type[lfo_idx];
        for (int i = 0; i < NUM_LOOP_TYPES; i++)
        {
            LFOLoopType type = i;
            float       x    = btn_loop_type.x + height * i;
            float       y    = btn_loop_type.y;
            if (data.mouse_hold_idx == i)
                y += 1;

            bool is_selected = type == current_loop_type;
            bool is_hovering = i == data.hover_idx && data.mouse_hold_idx == -1;

            if (is_selected || is_hovering)
            {
                unsigned bg_col = is_selected ? C_LIGHT_BLUE_2 : C_BTN_HOVER;
                xvg_draw_rectangle(xvg, x, y, height, height, 4 * SCALE, 0, bg_col);
            }

            unsigned icon_col = is_selected ? C_BG_LFO : is_hovering ? C_LIGHT_BLUE_2 : C_TEXT_DARK_BG;
            int      icon_id  = ICON_LFO_LOOP + type;
            xassert(type >= 0 && type <= 2);

            XVGGradient ifill = {.colour1 = icon_col};
            xvec4i      icon  = icon_get_coords(&gui->icons, icon_id);
            xvg_gradient_apply_image(&ifill, gui->icons.view, xvg->smp_linear, icon.x, icon.y, icon.width, icon.height);
            xvg_draw_solid_rectangle_with_gradient(xvg, x, y, height, height, ifill);
        }
    }

    // Rate slider
    {
        int     lfo_idx  = p->selected_lfo_idx;
        ParamID param_id = 0;

        bool is_sync = false;
        // Find rate type
        {
            ParamID rate_type_id = PARAM_RATE_TYPE_LFO_1 + lfo_idx;
            double  rate_type_d  = main_get_param(p, rate_type_id);

            is_sync = rate_type_d < 0.5;
        }
        if (is_sync)
            param_id = PARAM_SYNC_RATE_LFO_1 + lfo_idx;
        else
            param_id = PARAM_SEC_RATE_LFO_1 + lfo_idx;

        xassert(param_id);
        double val = main_get_param(p, param_id);

        unsigned events = imgui_get_events_rect(im, 'rate', &sl_rate);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            if (im->left_click_counter >= 2)
            {
                im->left_click_counter = 0;

                val = cplug_getDefaultParameterValue(p, param_id);
                param_set(p, param_id, val);
            }
        }

        if (events &
            (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_DRAG_MOVE | IMGUI_EVENT_TOUCHPAD_BEGIN | IMGUI_EVENT_TOUCHPAD_MOVE))
        {
            static float last_drag_val = 0;
            if (events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_TOUCHPAD_BEGIN))
            {
                last_drag_val = (float)val;
                if (!is_sync)
                    last_drag_val = 1 - last_drag_val;
                param_change_begin(p, param_id);
            }
            double range_min, range_max;
            cplug_getParameterRange(p, param_id, &range_min, &range_max);
            if (events & IMGUI_EVENT_DRAG_MOVE)
            {
                imgui_drag_value(im, &last_drag_val, range_min, range_max, 200, IMGUI_DRAG_VERTICAL);
            }
            else if (events & IMGUI_EVENT_TOUCHPAD_MOVE)
            {
                float delta = im->frame.delta_touchpad.y / 200;
                if (im->frame.modifiers_touchpad & PW_MOD_INVERTED_SCROLL)
                    delta = -delta;
                if (im->frame.modifiers_touchpad & PW_MOD_PLATFORM_KEY_CTRL)
                    delta *= 0.1f;
                if (im->frame.modifiers_touchpad & PW_MOD_KEY_SHIFT)
                    delta *= 0.1f;
                last_drag_val = xm_clampd(last_drag_val + delta, range_min, range_max);
            }

            if (is_sync)
            {
                double next_val = xm_droundi(last_drag_val);
                val             = next_val;
            }
            else
                val = 1.0 - last_drag_val; // invert
            param_change_update(p, param_id, val);
            val += 0;
        }
        if (events & (IMGUI_EVENT_DRAG_END | IMGUI_EVENT_TOUCHPAD_END))
            param_change_end(p, param_id);

        if (events & IMGUI_EVENT_MOUSE_WHEEL)
        {
            float delta = 0;
            if (is_sync)
            {
                delta = im->frame.delta_mouse_wheel;
            }
            else
            {
                delta = im->frame.delta_mouse_wheel * -0.1; // this parameter is inverted
                if (im->frame.modifiers_mouse_wheel & PW_MOD_PLATFORM_KEY_CTRL)
                    delta *= 0.1;
                if (im->frame.modifiers_mouse_wheel & PW_MOD_KEY_SHIFT)
                    delta *= 0.1;
            }
            if (im->frame.modifiers_mouse_wheel & PW_MOD_INVERTED_SCROLL)
                delta = -delta;

            double range_min = 0, range_max = 1;
            cplug_getParameterRange(p, param_id, &range_min, &range_max);
            float next_val = xm_clampf(val + delta, range_min, range_max);
            if (next_val != val)
            {
                val = next_val;
                param_set(p, param_id, next_val);
            }
        }

        float cy = rect_cy(&sl_rate);
        xvg_draw_text(xvg, sl_rate.x, cy, "RATE", NULL, FONT_SIZE, XVG_ALIGN_CL, C_TEXT_DARK_BG);

        char label[16] = {0};

        cplug_parameterValueToString(p, param_id, label, sizeof(label), val);

        xvg_draw_text(xvg, sl_rate.r - 12, cy, label, NULL, FONT_SIZE, XVG_ALIGN_CR, C_GREY_1);

        // Up & down "buttons"
        float btn_left = floor(sl_rate.r - 6 * SCALE);
        float btn_top  = floor(cy - FONT_SIZE * 0.75f);
        float btn_bot  = ceilf(cy + FONT_SIZE * 0.4f);
        add_up_down_triangles(xvg, (imgui_rect){btn_left, btn_top, sl_rate.r, btn_bot});
    }

    // LFO Draw shapes
    float shape_x = content_x;
    float shape_y = display_b - CONTENT_PADDING_Y - SHAPES_WIDTH;
    {
        imgui_rect btns;
        btns.x = content_x;
        btns.r = content_x + IMP_SHAPE_COUNT * SHAPES_WIDTH;
        btns.y = shape_y;
        btns.b = shape_y + SHAPES_WIDTH;

        unsigned events = imgui_get_events_rect(im, 'lshp', &btns);

        static const char* DESCRIPTIONS[] = {
            "Select a draw mode and drag your mouse inside empty space on the LFO grid to paint the currently "
            "selected shape to the grid."};
        ButtonStripIndexes data = handle_button_strip(
            gui,
            btns,
            IMP_SHAPE_COUNT,
            SHAPES_WIDTH,
            0,
            events,
            DESCRIPTIONS,
            ARRLEN(DESCRIPTIONS));

        if (data.mouse_down_idx != -1)
        {
            xassert(events & IMGUI_EVENT_MOUSE_LEFT_DOWN);
            // It was observed that some users clicked the active shape type button hoping to "reset" the shape back to
            // default, and turn off drawing mode. They were confused when this behaviour didn't happen
            // So here we're trying it and hoping that it's good UX
            IMPShapeType next_type = data.mouse_down_idx;
            if (data.mouse_down_idx == p->lfo_shape_idx)
                next_type = 0;
            p->lfo_shape_idx = next_type;
        }

        const IMPShapeType current_shape_type = p->lfo_shape_idx;

        for (int i = 0; i < IMP_SHAPE_COUNT; i++)
        {
            imgui_rect btn;
            btn.x = btns.x + i * SHAPES_WIDTH;
            btn.r = btns.x + (i + 1) * SHAPES_WIDTH;
            btn.y = btns.y;
            btn.b = btns.b;

            if (data.mouse_hold_idx == i)
            {
                btn.y += 1;
                btn.b += 1;
            }

            bool is_selected = i == current_shape_type;
            bool is_hovering = i == data.hover_idx;

            if (is_selected || is_hovering)
            {
                unsigned bg_col = is_selected ? C_LIGHT_BLUE_2 : C_BTN_HOVER;
                xvg_draw_rectangle(xvg, btn.x, btn.y, SHAPES_WIDTH, SHAPES_WIDTH, 4 * SCALE, 0, bg_col);
            }

            int      icon_id  = ICON_IMP_POINTS + i;
            unsigned icon_col = is_selected ? C_BG_LFO : is_hovering ? C_LIGHT_BLUE_2 : C_TEXT_DARK_BG;

            XVGGradient ifill = {.colour1 = icon_col};
            xvec4i      icon  = icon_get_coords(&gui->icons, icon_id);
            xvg_gradient_apply_image(&ifill, gui->icons.view, xvg->smp_linear, icon.x, icon.y, icon.width, icon.height);
            xvg_draw_solid_rectangle_with_gradient(xvg, btn.x, btn.y, SHAPES_WIDTH, SHAPES_WIDTH, ifill);
        }
    }

    // LFO pattern selector
    {
        imgui_rect btn_pattern;

        btn_pattern.x = lm->content_x + CONTENT_PADDING_X + SCALE * 80;
        btn_pattern.y = display_y + CONTENT_PADDING_Y;
        btn_pattern.b = btn_pattern.y + LFO_TAB_HEIGHT;
        btn_pattern.r = btn_pattern.x + SCALE * 8 * 28;

        const ParamID  param_id = PARAM_PATTERN_LFO_1 + p->selected_lfo_idx;
        const unsigned uid      = 'prm' + param_id;
        const unsigned events   = imgui_get_events_rect(im, uid + 'btn', &btn_pattern);

        float w  = btn_pattern.r - btn_pattern.x;
        float w8 = w / NUM_LFO_PATTERNS;

        static const char* DESCRIPTIONS[] = {"Switch between custom LFO shapes for this LFO\n"
                                             "\n"
                                             "Try automating this parameter in your DAW"};

        ButtonStripIndexes data =
            handle_button_strip(gui, btn_pattern, NUM_LFO_PATTERNS, w8, 0, events, DESCRIPTIONS, ARRLEN(DESCRIPTIONS));

        float pattern_cx = 0.5f * (btn_pattern.x + btn_pattern.r);
        float pattern_cy = 0.5f * (btn_pattern.y + btn_pattern.b);

        float value_f = (float)main_get_param(p, param_id);

        float next_value = value_f;

        if (data.mouse_down_idx != -1)
        {
            xassert(events & IMGUI_EVENT_MOUSE_LEFT_DOWN);
            next_value = (float)data.mouse_down_idx / (NUM_LFO_PATTERNS - 1);
        }
        bool changed = value_f != next_value;
        if (changed)
        {
            value_f = next_value;
            param_change_update(p, param_id, value_f);
            imp->main_points_valid                       = false;
            fstate.should_update_main_points_with_points = true;
            should_clear_lfo_trail                       = true;
        }

        xvg_draw_text(xvg, content_x, rect_cy(&btn_pattern), "PATTERN", NULL, FONT_SIZE, XVG_ALIGN_CL, C_TEXT_DARK_BG);

        const int pattern_idx = main_get_lfo_pattern_idx(p);

        for (int i = 0; i < NUM_LFO_PATTERNS; i++)
        {
            char label[4]  = {'1', 0, 0, 0};
            label[0]      += i;
            xassert(label[0] < '9'); // oops you might be incrementing "1" past 10
            xassert(i < 8);          // oops you might be incrementing "1" past 10

            float x = btn_pattern.x + i * w8;
            float y = btn_pattern.y;
            if (i == data.mouse_hold_idx)
                y += 1;
            bool is_selected = i == pattern_idx;
            bool is_hovering = i == data.hover_idx && data.mouse_hold_idx == -1;

            if (is_selected || is_hovering)
            {
                unsigned bg_col = is_selected ? C_LIGHT_BLUE_2 : C_BTN_HOVER;
                xvg_draw_rectangle(xvg, x, y, w8, w8, 4 * SCALE, 0, bg_col);
            }

            unsigned col = is_selected ? C_BG_LFO : is_hovering ? C_LIGHT_BLUE_2 : C_TEXT_DARK_BG;
            xvg_draw_text(xvg, x + w8 * 0.5f, y + w8 * 0.5f, label, label + 1, FONT_SIZE, XVG_ALIGN_CC, col);
        }
    }

    // Display grid

    // const float grid_y = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT + DISPLAY_PADDING_TOP;
    const float grid_y = button_bottom + (CONTENT_PADDING_Y + LFO_TAB_HEIGHT);
    const float grid_b = shape_y - DISPLAY_PADDING_BOTTOM;
    const float grid_x = lm->content_x + CONTENT_PADDING_X;
    const float grid_r = lm->content_r - CONTENT_PADDING_X;
    const float grid_w = ceilf(grid_r - grid_x);
    const float grid_h = ceilf(grid_b - grid_y);

    imp->area.x = grid_x;
    imp->area.y = grid_y;
    imp->area.r = grid_r;
    imp->area.b = grid_b;

    const int lfo_idx     = p->selected_lfo_idx;
    const int pattern_idx = main_get_lfo_pattern_idx(p);

    // const float pattern_length = p->lfos[lfo_idx].pattern_length[pattern_idx];
    const float pattern_length = 1;
    const int   num_grid_x     = pattern_length * p->lfos[lfo_idx].grid_x[pattern_idx];
    const int   num_grid_y     = num_grid_x;

    const enum IMPShapeType current_shape = p->lfo_shape_idx;

    const IMPointsArea selection_area = {lm->content_x + 16, grid_y - 32, lm->content_r - 16, grid_b + 24};

    imp_run(
        &fstate,
        selection_area,
        num_grid_x,
        num_grid_y,
        current_shape,

        &p->lfos[lfo_idx].points[pattern_idx],
        &p->lfos[lfo_idx].spinlocks[pattern_idx]);

    float playhead = (float)p->lfos[lfo_idx].phase;
    playhead       = fmodf(playhead, pattern_length);

    lm->last_lfo_playhead    = lm->current_lfo_playhead;
    lm->current_lfo_playhead = playhead;

    bool       retrigger_flag = xt_atomic_exchange_u8(&p->gui_retrig_flag, 0);
    const bool has_resized    = !!(im->frame.events & (1 << PW_EVENT_RESIZE_UPDATE));

    // Clear trail on resize
    should_clear_lfo_trail |= has_resized;
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

    if (retrigger_flag)
    {
        size_t len          = xarr_cap(gui->lfo_playhead_trail);
        int    playhead_idx = (float)len * lm->current_lfo_playhead;
        playhead_idx        = xm_mini(playhead_idx, len - 1);
        for (int i = 0; i <= playhead_idx; i++)
            gui->lfo_playhead_trail[i] = LFO_PLAYHEAD_TRAIL_MAX_OPACITY;
    }

    if (has_resized || fstate.should_update_audio_points_with_main_points)
    {
        size_t len = xarr_len(gui->lfo_ybuffer);
        size_t cap = xarr_cap(gui->lfo_ybuffer);
        xassert(len <= cap);
        size_t new_len = grid_w;
        xarr_setlen(gui->lfo_ybuffer, new_len);
        imp_render_y_values(&gui->imp, gui->lfo_ybuffer, new_len, 0, 1);
    }

    // Draw grid
    {
        bool mouse_is_inside_grid =
            imgui_hittest_rect(im->pos_mouse_move, &(imgui_rect){grid_x, grid_y, grid_r, grid_b});
        if (current_shape != IMP_SHAPE_POINT && mouse_is_inside_grid)
        {
            float grid_x_inc = grid_w / num_grid_x;
            float grid_y_inc = grid_h / num_grid_y;

            float grid_rel_x = im->pos_mouse_move.x - grid_x;
            float grid_rel_y = im->pos_mouse_move.y - grid_y;

            float grid_index_x          = floorf(grid_rel_x / grid_x_inc);
            float grid_index_y          = num_grid_y - floorf(grid_rel_y / grid_y_inc);
            float grid_highlight_left   = grid_x + grid_index_x * grid_x_inc;
            float grid_highlight_height = grid_index_y * grid_y_inc;
            float grid_highlight_top    = grid_b - grid_highlight_height;

            xvg_draw_solid_rectangle(
                xvg,
                grid_highlight_left,
                grid_highlight_top,
                grid_x_inc,
                grid_highlight_height,
                0x10);
        }

        xvg_draw_rectangle(xvg, grid_x, grid_y, grid_r - grid_x, grid_b - grid_y, 0, 1, C_GRID_PRIMARY);

        // Horizontal subdivisions
        for (int k = 0; k < 2; k++)
        {
            unsigned col = k == 0 ? C_GRID_TERTIARY : C_GRID_SECONDARY;
            for (int i = 1 + k; i < num_grid_x; i += 2)
            {
                float x = xm_mapf(i, 0, num_grid_x, grid_x, grid_r);
                x       = floorf(x);
                xvg_draw_solid_rectangle(xvg, x, grid_y + 1, 1, grid_h - 2, col);
            }
            // Vertical subdivisions
            for (int i = 1 + k; i < num_grid_y; i += 2)
            {
                float y = xm_mapf(i, 0, num_grid_y, grid_y, grid_b);
                y       = floorf(y);
                xvg_draw_solid_rectangle(xvg, grid_x + 1, y, grid_w - 2, 1, col);
            }
        }

        // Horiztonal beats (obsolete)
        // for (int i = 1; i < pattern_length; i++)
        // {
        //     float x = xm_mapf(i, 0, pattern_length, grid_x, grid_r);
        //     x       = floorf(x);
        //     xvg_draw_solid_rectangle(xvg, x, grid_y, 1, grid_h - 2, C_GRID_PRIMARY);
        // }

        ParamID rate_type_param_id = PARAM_RATE_TYPE_LFO_1 + lfo_idx;
        bool    is_ms              = main_get_param(p, rate_type_param_id) >= 0.5;

        if (is_ms)
        {
            ParamID sec_param_id = PARAM_SEC_RATE_LFO_1 + lfo_idx;
            double  sec          = main_get_param(p, sec_param_id);

            extern double denormalise_sec(double v);
            sec = denormalise_sec(sec);

            double max_ms = sec * 1000;
            double ms_inc = 0;

            if (max_ms < 13.0)
                ms_inc = 2;
            else if (max_ms < 23.0)
                ms_inc = 5.0;
            else if (max_ms < 50.0)
                ms_inc = 10.0;
            else if (max_ms < 125.0)
                ms_inc = 25.0;
            else if (max_ms < 250.0)
                ms_inc = 50.0;
            else if (max_ms < 550.0)
                ms_inc = 100.0;
            else if (max_ms <= 1250.0) // 1sec
                ms_inc = 250.0;
            else if (max_ms <= 3200.0)
                ms_inc = 500.0;
            else // > 1sec
                ms_inc = 1000.0;

            xvg_draw_text(xvg, grid_x, grid_y - 8, "0 ms", 0, FONT_SIZE, XVG_ALIGN_BL, C_GREY_2);

            float  num_subdivisions = ms_inc / max_ms;
            float  x_inc            = grid_w * num_subdivisions;
            float  x                = grid_x + x_inc;
            double ms               = ms_inc;
            char   label[16];
            int    label_len;
            while (x < (grid_r - 100))
            {
                label_len = format_time(ms, label, sizeof(label));

                xvg_draw_text(xvg, x, grid_y - 8, label, label + label_len, FONT_SIZE, XVG_ALIGN_BC, C_GREY_2);

                x  += x_inc;
                ms += ms_inc;
            }
            label_len = format_time(max_ms, label, sizeof(label));
            xvg_draw_text(xvg, grid_r, grid_y - 8, label, label + label_len, FONT_SIZE, XVG_ALIGN_BR, C_GREY_2);
        }
        else // is_sync
        {
            const char** labels_arr = NULL;
            int          num_labels = 0;

            ParamID sync_param_id = PARAM_SYNC_RATE_LFO_1 + lfo_idx;
            LFORate sync_type     = (int)main_get_param(p, sync_param_id);
            switch (sync_type)
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
                labels_arr                      = labels_1_2;
                num_labels                      = ARRLEN(labels_1_2);
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
                for (int i = 0; i < num_labels; i++)
                {
                    int alignment = XVG_ALIGN_BC;
                    if (i == 0)
                        alignment = XVG_ALIGN_BL;
                    if (i == num_labels - 1)
                        alignment = XVG_ALIGN_BR;

                    const char* txt = labels_arr[i];

                    float x = xm_mapf(i, 0, num_labels - 1, grid_x, grid_r);
                    xvg_draw_text(xvg, x, grid_y - 8, txt, 0, FONT_SIZE, alignment, C_GREY_2);
                }
            }
        }
    }

    xvg_command_custom(xvg, gui, do_lfo_shaders, XVG_LABEL("LFO shaders"));

    imp_draw(&fstate);

    if (playhead < pattern_length)
    {
        float        x   = xm_lerpf(playhead, grid_x, grid_r);
        size_t       idx = (size_t)(x - grid_x);
        const size_t len = xarr_len(gui->lfo_ybuffer);
        idx              = xm_minull(idx, len - 1);
        float y_norm     = gui->lfo_ybuffer[idx];
        float y          = xm_lerpf(y_norm, grid_b, grid_y);
        xvg_draw_circle(xvg, x, y, 3 * SCALE, 0, C_WHITE);
    }

    LINKED_ARENA_LEAK_DETECT_END(gui->arena);
}
