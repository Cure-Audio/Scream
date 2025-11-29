#include "tooltip.h"
#include <string.h>

void tooltip_draw(
    Tooltip*     tt,
    NVGcontext*  nvg,
    LinkedArena* arena,
    uint64_t     time_ns,
    float        gui_width,
    float        gui_height,
    float        scale)
{
    LINKED_ARENA_LEAK_DETECT_BEGIN(arena);

    xassert(tt->text != NULL);

    const uint64_t duration_ns         = time_ns - tt->time_shown_ns;
    const uint64_t one_second          = 1000000000LLU;
    const bool     should_show_tooltip = duration_ns > one_second;

    if (!should_show_tooltip)
        return;

    const float TOOLTIP_BOX_HEIGHT     = scale * 20;
    const float TOOLTIP_ARROW_LENGTH   = scale * 8;
    const float TOOLTIP_ARROW_WIDTH    = scale * 8;
    const float TOOLTIP_TEXT_PADDING_X = scale * 10;
    const float TOOLTIP_TEXT_PADDING_Y = scale * 6;
    const float TOOLTIP_GAP            = scale * 4;
    const float FONT_SIZE              = scale * 14;
    const float LINE_HEIGHT            = FONT_SIZE * 1.5;

    int         numrows = 69;
    NVGtextRow* rows    = linked_arena_alloc(arena, sizeof(*rows) * numrows);

    nvgSetTextAlign(nvg, NVG_ALIGN_CL);
    nvgSetFontSize(nvg, FONT_SIZE);

    // measure dimensions
    int text_len = strlen(tt->text);
    xassert(text_len > 0);
    numrows = nvgTextBreakLines(nvg, tt->text, tt->text + text_len, gui_width * 0.25, rows, numrows);
    xassert(numrows >= 1);
    float width = 1;
    for (int i = 0; i < numrows; i++)
    {
        if (rows[i].width > width)
            width = rows[i].width;
        if (rows[i].maxx > width)
            width = rows[i].maxx;
    }
    width += TOOLTIP_TEXT_PADDING_X * 2;

    imgui_rect  d             = tt->target_widget_dimensions;
    const float widget_cx     = (d.x + d.r) * 0.5f;
    const float widget_top    = d.y;
    const float widget_bottom = d.b;

    float tt_height  = numrows * LINE_HEIGHT + TOOLTIP_TEXT_PADDING_Y * 2;
    d.x              = widget_cx - width * 0.5f;
    d.r              = d.x + width;
    d.y             -= tt_height + TOOLTIP_ARROW_LENGTH + TOOLTIP_GAP;
    d.b              = d.y + tt_height;

    const float boundary_padding = scale * 8;
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

    xassert(d.y >= 0);
    xassert(d.b >= d.y);

    imgui_rect shadow_area  = d;
    shadow_area.x          += 2;
    shadow_area.r          += 2;
    shadow_area.y          += 4;
    shadow_area.b          += 4;
    nvgBeginPath(nvg);
    nvgRect2(nvg, shadow_area.x, shadow_area.y, shadow_area.r, shadow_area.b);
    NVGpaint shadow_paint = nvgBoxGradient(
        nvg,
        shadow_area.x,
        shadow_area.y,
        shadow_area.r - shadow_area.x,
        shadow_area.b - shadow_area.y,
        0,
        4,
        (NVGcolour){0, 0, 0, 0.25},
        (NVGcolour){0, 0, 0, 0});
    nvgSetPaint(nvg, shadow_paint);
    nvgFill(nvg);

    nvgBeginPath(nvg);
    nvgMoveTo(nvg, d.x, d.y);
    nvgLineTo(nvg, d.x, d.b);
    if (d.b < widget_top) // tooltip above widget
    {
        nvgLineTo(nvg, widget_cx - TOOLTIP_ARROW_WIDTH * 0.5f, d.b);
        nvgLineTo(nvg, widget_cx, d.b + TOOLTIP_ARROW_LENGTH);
        nvgLineTo(nvg, widget_cx + TOOLTIP_ARROW_WIDTH * 0.5f, d.b);
    }
    nvgLineTo(nvg, d.r, d.b);
    nvgLineTo(nvg, d.r, d.y);
    if (d.y > widget_bottom) // tooltip below widget
    {
        nvgLineTo(nvg, widget_cx + TOOLTIP_ARROW_WIDTH * 0.5f, d.y);
        nvgLineTo(nvg, widget_cx, d.y - TOOLTIP_ARROW_LENGTH);
        nvgLineTo(nvg, widget_cx - TOOLTIP_ARROW_WIDTH * 0.5f, d.y);
    }
    nvgClosePath(nvg);

    static const NVGcolour TOOLTIP_BG_COLOUR     = nvgHexColour(0xD4D7DEff);
    static const NVGcolour TOOLTIP_BORDER_COLOUR = nvgHexColour(0x8A94A8ff);
    static const NVGcolour TOOLTIP_TEXT_COLOUR   = nvgHexColour(0x5D636Aff);

    nvgSetColour(nvg, TOOLTIP_BG_COLOUR);
    nvgFill(nvg);
    nvgSetColour(nvg, TOOLTIP_BORDER_COLOUR);
    nvgStroke(nvg, 1);

    nvgSetColour(nvg, TOOLTIP_TEXT_COLOUR);

    float text_x = d.x + TOOLTIP_TEXT_PADDING_X;
    float text_y = d.y + TOOLTIP_TEXT_PADDING_Y + LINE_HEIGHT * 0.5f;
    for (int i = 0; i < numrows; i++)
    {
        NVGtextRow* row = &rows[i];
        nvgText(nvg, text_x, text_y, row->start, row->end);

        text_y += LINE_HEIGHT;
    }

    linked_arena_release(arena, rows);

    LINKED_ARENA_LEAK_DETECT_END(arena);
}
