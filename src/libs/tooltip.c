#include "tooltip.h"
#include <string.h>
#include <xhl/maths.h>

void tooltip_draw(
    Tooltip*     tt,
    XVG*         xvg,
    LinkedArena* arena,
    uint64_t     time_ns,
    float        gui_width,
    float        gui_height,
    float        scale)
{
    LINKED_ARENA_TAGGED_LEAK_DETECT_BEGIN(arena, _param_arena);
    LINKED_ARENA_TAGGED_LEAK_DETECT_BEGIN(xvg->arena, _nvg_arena);

    xassert(tt->state.text != NULL);

    const uint64_t duration_ns         = time_ns - tt->state.time_shown_ns;
    const uint64_t one_second          = 1000000000LLU;
    const bool     should_show_tooltip = duration_ns > one_second;

    if (!should_show_tooltip)
        return;

    xassert(tt->settings.font_size);

    const float TOOLTIP_BOX_HEIGHT     = scale * tt->settings.box_height;
    const float TOOLTIP_ARROW_LENGTH   = scale * tt->settings.arrow_length;
    const float TOOLTIP_ARROW_WIDTH    = scale * tt->settings.arrow_width;
    const float TOOLTIP_TEXT_PADDING_X = scale * tt->settings.text_padding_x;
    const float TOOLTIP_TEXT_PADDING_Y = scale * tt->settings.text_padding_y;
    const float TOOLTIP_GAP            = scale * tt->settings.gap;
    const float FONT_SIZE              = scale * tt->settings.font_size;

    // nvgSetTextAlign(nvg, NVG_ALIGN_TL);
    // nvgSetFontSize(nvg, FONT_SIZE);
    // nvgSetTextLineHeight(nvg, 1.5);

    // measure dimensions
    float line_height      = 1.5;
    float line_break_width = gui_width * 0.25f;
    line_break_width       = xm_maxf(line_break_width, 500);
    line_break_width       = xm_minf(line_break_width, gui_width);
    const XVGTextLayout* layout =
        xvg_create_text_layout(xvg, tt->state.text, 0, FONT_SIZE, line_break_width, line_height);
    xassert(layout->num_rows >= 1);

    float width  = layout->xmax / xvg->backingScaleFactor;
    width       += TOOLTIP_TEXT_PADDING_X * 2;

    imgui_rect  d             = tt->state.target_widget_dimensions;
    const float widget_cx     = (d.x + d.r) * 0.5f;
    const float widget_top    = d.y;
    const float widget_bottom = d.b;

    float tt_height  = layout->total_height / xvg->backingScaleFactor;
    tt_height       += TOOLTIP_TEXT_PADDING_Y * 2;
    d.x              = widget_cx - width * 0.5f;
    d.r              = d.x + width;
    d.y             -= tt_height + TOOLTIP_ARROW_LENGTH + TOOLTIP_GAP;
    d.b              = d.y + tt_height;

    const float boundary_padding = scale * scale * tt->settings.window_boundary;
    const float boundary_right   = gui_width - boundary_padding;
    const float boundary_bottom  = gui_height - boundary_padding;
    if (d.x < boundary_padding)
    {
        float delta  = boundary_padding - d.x;
        d.x         += delta;
        d.r         += delta;
    }
    if (d.y < boundary_padding)
    {
        float delta  = boundary_padding - d.y;
        d.y         += delta;
        d.b         += delta;

        // Default the tooltip displays above the widget.
        // In the rarer case we run out of room above the widget, display the tooltip below the widget
        if (d.b > widget_top)
        {
            d.y = widget_bottom + TOOLTIP_ARROW_LENGTH + TOOLTIP_GAP;
            d.b = d.y + tt_height;
        }
    }
    if (d.r > boundary_right)
    {
        float delta  = d.r - boundary_right;
        d.x         -= delta;
        d.r         -= delta;
    }
    if (d.b > boundary_bottom)
    {
        float delta  = d.b - boundary_bottom;
        d.y         -= delta;
        d.b         -= delta;
    }

    d.x = floorf(d.x);
    d.y = floorf(d.y);
    d.r = ceilf(d.r);
    d.b = ceilf(d.b);

    xassert(d.y >= 0);
    xassert(d.b >= d.y);

    float shadow_offset = 2;
    float shadow_blur   = 4;
    xvg_draw_rectangle_with_gradient(
        xvg,
        d.x + shadow_offset - shadow_blur,
        d.y + shadow_offset - shadow_blur,
        d.r - d.x + shadow_blur * 2,
        d.b - d.y + shadow_blur * 2,
        0,
        0,
        xvg_make_shadow(0x0, 0x40, 0, 0, shadow_blur, 0, false));

    float w = d.r - d.x;
    float h = d.b - d.y;

    unsigned col_tt = tt->settings.colour_bg;
    float    offset = TOOLTIP_ARROW_LENGTH * 0.33;
    xvg_draw_rectangle(xvg, d.x, d.y, w, h, 0, 0, col_tt);

    xvg_draw_solid_rectangle(xvg, d.x, d.y, 1, h, tt->settings.colour_border);
    xvg_draw_solid_rectangle(xvg, d.r - 1, d.y, 1, h, tt->settings.colour_border);
    xvg_draw_solid_rectangle(xvg, d.x, d.y, w, 1, tt->settings.colour_border);
    xvg_draw_solid_rectangle(xvg, d.x, d.b - 1, w, 1, tt->settings.colour_border);

    float arrow_left     = widget_cx - TOOLTIP_ARROW_WIDTH * 0.5f;
    float arrow_right    = widget_cx + TOOLTIP_ARROW_WIDTH * 0.5f;
    float arrow_border_h = TOOLTIP_ARROW_LENGTH * 0.66;

    if (d.b < widget_top) // tooltip above widget
    {
        xvg_draw_triangle(
            xvg,
            arrow_left - 2,
            d.b - offset - 2,
            TOOLTIP_ARROW_WIDTH + 4,
            TOOLTIP_ARROW_LENGTH + 2,
            0.5f,
            0,
            col_tt);
        xvg_draw_line_round(xvg, arrow_left, d.b, widget_cx, d.b + arrow_border_h, 1, tt->settings.colour_border);
        xvg_draw_line_round(xvg, arrow_right, d.b, widget_cx, d.b + arrow_border_h, 1, tt->settings.colour_border);
    }
    else // tooltip below widget
    {
        xvg_draw_triangle(
            xvg,
            arrow_left - 2,
            d.y + offset - TOOLTIP_ARROW_LENGTH,
            TOOLTIP_ARROW_WIDTH + 4,
            TOOLTIP_ARROW_LENGTH + 2,
            0.0f,
            0,
            col_tt);
        xvg_draw_line_round(xvg, arrow_left, d.y, widget_cx, d.y - arrow_border_h, 1, tt->settings.colour_border);
        xvg_draw_line_round(xvg, arrow_right, d.y, widget_cx, d.y - arrow_border_h, 1, tt->settings.colour_border);
    }

    // nvgSetColour(nvg, tt->settings.colour_bg);
    // nvgFill(nvg);
    // nvgSetColour(nvg, tt->settings.colour_border);
    // nvgStroke(nvg, STROKE_WIDTH);

    // nvgSetColour(nvg, tt->settings.colour_text);

    float text_x = d.x + TOOLTIP_TEXT_PADDING_X;
    float text_y = (d.b + d.y) * 0.5f;
    xvg_draw_text_layout(xvg, layout, text_x, text_y, XVG_ALIGN_CL, tt->settings.colour_text);
    xvg_release_text_layout(xvg, layout);

    LINKED_ARENA_TAGGED_LEAK_DETECT_END(xvg->arena, _nvg_arena);
    LINKED_ARENA_TAGGED_LEAK_DETECT_END(arena, _param_arena);
}
