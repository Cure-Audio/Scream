
#include "common.h"
#include "plugin.h"

#include <xhl/component.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/time.h>
#include <xhl/vector.h>

#include <cplug_extensions/window.h>
#include <nanovg.h>
#include <nanovg_sokol.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct GUI
{
    Plugin*     plugin;
    void*       pw;
    void*       sg;
    NVGcontext* nvg;
    int         font_id;
    float       scale;

    xcomp_root       root;      // root comp state
    xcomp_component  component; // root level component
    xcomp_component* children[3];
    xcomp_component  sliders[3];

    int    slider_drag_idx;
    xvec2f drag_last;
    double drag_val_normalised;
} GUI;

static const float SLIDER_POSITIONS[3] = {0.2, 0.5, 0.8};
// Relative to GUI width
#define SLIDER_RADIUS 0.1f
// Angle radians
// 120deg
#define SLIDER_START_RAD 2.0943951023931953f
// 120deg + 360deg * 0.8333
#define SLIDER_END_RAD 7.330173418865945f
// end - start
#define SLIDER_LENGTH_RAD 5.23577831647275f

static void my_sg_logger(
    const char* tag,              // always "sapp"
    uint32_t    log_level,        // 0=panic, 1=error, 2=warning, 3=info
    uint32_t    log_item_id,      // SAPP_LOGITEM_*
    const char* message_or_null,  // a message string, may be nullptr in release mode
    uint32_t    line_nr,          // line number in sokol_app.h
    const char* filename_or_null, // source filename, may be nullptr in release mode
    void*       user_data)
{
    static char* LOG_LEVEL[] = {
        "PANIC",
        "ERROR",
        "WARNING",
        "INFO",
    };
    xassert(log_level > 1);
    xassert(log_level < ARRLEN(LOG_LEVEL));
    if (!message_or_null)
        message_or_null = "";
    println("[%s] %s %u:%s", LOG_LEVEL[log_level], message_or_null, line_nr, filename_or_null);
}

void pw_get_info(struct PWGetInfo* info)
{
    if (info->type == PW_INFO_INIT_SIZE)
    {
        Plugin* p              = info->init_size.plugin;
        info->init_size.width  = p->width;
        info->init_size.height = p->height;
    }
    else if (info->type == PW_INFO_CONSTRAIN_SIZE)
    {
        uint32_t width  = info->constrain_size.width;
        uint32_t height = info->constrain_size.height;

        if (width < GUI_MIN_WIDTH)
            width = GUI_MIN_WIDTH;
        if (height < GUI_MIN_HEIGHT)
            height = GUI_MIN_HEIGHT;

        switch (info->constrain_size.direction)
        {
        case PW_RESIZE_UNKNOWN:
        case PW_RESIZE_TOPLEFT:
        case PW_RESIZE_TOPRIGHT:
        case PW_RESIZE_BOTTOMLEFT:
        case PW_RESIZE_BOTTOMRIGHT:
        {
            uint32_t numX = width / GUI_RATIO_X;
            uint32_t numY = height / GUI_RATIO_Y;
            uint32_t num  = numX > numY ? numX : numY;

            uint32_t nextW = num * GUI_RATIO_X;
            uint32_t nextH = num * GUI_RATIO_Y;

            if (nextW > width || nextH > height)
            {
                num = num == numX ? numY : numX;

                nextW = num * GUI_RATIO_X;
                nextH = num * GUI_RATIO_Y;
            }

            width  = nextW;
            height = nextH;
            break;
        }
        case PW_RESIZE_LEFT:
        case PW_RESIZE_RIGHT:
        {
            uint32_t num = width / GUI_RATIO_X;
            width        = num * GUI_RATIO_X;
            height       = num * GUI_RATIO_Y;
            break;
        }
        case PW_RESIZE_TOP:
        case PW_RESIZE_BOTTOM:
        {
            uint32_t num = height / GUI_RATIO_Y;
            width        = num * GUI_RATIO_X;
            height       = num * GUI_RATIO_Y;
            break;
        }
        }

        info->constrain_size.width  = width;
        info->constrain_size.height = height;
    }
}

void cb_xcomp_slider(struct xcomp_component* comp, uint32_t event, xcomp_event_data data)
{
    GUI* gui        = comp->data;
    int  slider_idx = 0;
    for (; slider_idx < ARRLEN(gui->sliders); slider_idx++)
    {
        if (comp == &gui->sliders[slider_idx])
            break;
    }
    xassert(slider_idx != ARRLEN(gui->sliders));

    // The xcomp lib only supports these drag callbacks for rectangles, and we are drawing a really large round rotary
    // knob, and not using all vailable pixels within the square. The hit test flag helps us skip any click events that
    // occur without the flag.
    static const uint64_t FLAG_HIT_TEST = 1 << 16;

    if (event == XCOMP_EVENT_MOUSE_MOVE)
    {
        // hit test
        float cx = comp->dimensions.x + 0.5f * comp->dimensions.width;
        float cy = comp->dimensions.y + 0.5f * comp->dimensions.height;

        float diff_x      = data.x - cx;
        float diff_y      = data.y - cy;
        float distance_px = hypotf(fabsf(diff_x), fabsf(diff_y));

        float radius = SLIDER_RADIUS * gui->component.dimensions.width;
        // println("x: %f y: %f px: %f", diff_x, diff_y, distance_px);
        if (distance_px <= radius)
        {
            if (!(comp->flags & FLAG_HIT_TEST))
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);
            comp->flags |= FLAG_HIT_TEST;
            // do thing
        }
        else
        {
            if ((comp->flags & FLAG_HIT_TEST))
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_ARROW);
            comp->flags &= ~FLAG_HIT_TEST;
        }
    }
    else if (event == XCOMP_EVENT_MOUSE_EXIT)
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_ARROW);
    else if (event == XCOMP_EVENT_MOUSE_LEFT_DOWN)
    {
        if (!(comp->flags & FLAG_HIT_TEST))
            return;

        double val = cplug_getParameterValue(gui->plugin, slider_idx);
        cplug_normaliseParameterValue(gui->plugin, slider_idx, val);
        gui->slider_drag_idx     = slider_idx;
        gui->drag_last.x         = data.x;
        gui->drag_last.y         = data.y;
        gui->drag_val_normalised = val;

        param_change_begin(gui->plugin, slider_idx);
    }
    else if (event == XCOMP_EVENT_DRAG_MOVE)
    {
        if (gui->slider_drag_idx < 0)
            return;

        /*
        void drag_value(GUI* gui, const xcomp_event_data* data, uint64_t drag_flags, float drag_range_pixels)
        {
            float next_val;
            float diff;
            if (DRAG_HORIZONTALVERTICAL(drag_flags))
                diff = (data->x - gui->drag_last.x) + (gui->drag_last.y - data->y);
            else if (DRAG_HORIZONTAL(drag_flags))
                diff = data->x - gui->drag_last.x;
            else // DRAG_VERTICAL
                diff = gui->drag_last.y - data->y;

            if (data->modifiers & (XCOMP_MOD_KEY_SHIFT | XCOMP_MOD_PLATFORM_KEY_CTRL))
                diff *= 0.1f;

            next_val = gui->v0 + diff * (1.0f / drag_range_pixels);
            next_val = xm_clampf(next_val, 0.0f, 1.0f);

            gui->drag_last.x = data->x;
            gui->drag_last.y = data->y;
            gui->v0          = next_val;
        }
        */
        const bool fine_increment = data.modifiers & (XCOMP_MOD_PLATFORM_KEY_CTRL | XCOMP_MOD_KEY_SHIFT);

        float diff = (data.x - gui->drag_last.x) + (gui->drag_last.y - data.y);
        if (fine_increment)
            diff *= 0.1f;

        double       next_val;
        const double drag_range_px = 400.0; // drag sensitivity

        next_val = gui->drag_val_normalised + diff * (1.0f / drag_range_px);
        if (next_val < 0)
            next_val = 0;
        if (next_val > 1)
            next_val = 1;

        gui->drag_last.x         = data.x;
        gui->drag_last.y         = data.y;
        gui->drag_val_normalised = next_val;

        next_val = cplug_denormaliseParameterValue(gui->plugin, slider_idx, next_val);
        param_change_update(gui->plugin, slider_idx, next_val);
    }
    else if (event == XCOMP_EVENT_DRAG_END)
    {
        if (gui->slider_drag_idx < 0)
            return;

        param_change_end(gui->plugin, slider_idx);
        gui->slider_drag_idx = -1;
    }
}

void* pw_create_gui(void* _plugin, void* _pw)
{
    xassert(_plugin);
    xassert(_pw);
    Plugin* p   = _plugin;
    GUI*    gui = xcalloc(1, sizeof(*gui));
    gui->plugin = p;
    gui->pw     = _pw;
    p->gui      = gui;

    sg_environment env;
    memset(&env, 0, sizeof(env));
    env.defaults.sample_count = 1;
    env.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
#if __APPLE__
    env.metal.device = pw_get_metal_device(gui->pw);
#elif _WIN32
    env.d3d11.device         = pw_get_dx11_device(gui->pw);
    env.d3d11.device_context = pw_get_dx11_device_context(gui->pw);
#endif
    gui->sg = sg_setup(&(sg_desc){
        .environment        = env,
        .logger             = my_sg_logger,
        .pipeline_pool_size = 512,
    });

    gui->nvg = nvgCreateSokol(gui->sg, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    xassert(gui->nvg);

#ifdef _WIN32
    static const char* font_path = "C:\\Windows\\Fonts\\arial.ttf";
#elif defined(__APPLE__)
    static const char* font_path = "/Library/Fonts/Arial Unicode.ttf";
#endif
    int font_id = nvgCreateFont(gui->nvg, "Arial", font_path);
    xassert(font_id != -1);
    if (font_id == -1)
    {
        println("[CRITICAL] Failed to open font at path %s", font_path);
    }

    gui->font_id = font_id;
    gui->scale   = 1.0f;

    gui->root.main               = &gui->component;
    gui->component.children      = gui->children;
    gui->component.event_handler = xcomp_empty_event_cb;
    gui->component.data          = gui;

    for (int i = 0; i < ARRLEN(gui->children); i++)
    {
        xcomp_component* comp = &gui->sliders[i];
        xcomp_add_child(&gui->component, comp);
        comp->data          = gui;
        comp->event_handler = cb_xcomp_slider;
    }

    gui->slider_drag_idx = -1;

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;

    sg_shutdown(gui->sg);

    gui->plugin->gui = NULL;
    xfree(gui);
}

void pw_tick(void* _gui)
{
    GUI* gui = _gui;
    xassert(gui->plugin);
    xassert(gui->nvg);
    if (!gui || !gui->plugin || !gui->nvg)
        return;

    // Begin frame
    {
        int width  = gui->plugin->width;
        int height = gui->plugin->height;

        static const float r = 202.0f / 255.0f;
        static const float g = 211.0f / 255.0f;
        static const float b = 220.0f / 255.0f;

        sg_pass_action pass_action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {r, g, b, 1.0f}}};

        sg_swapchain swapchain;
        memset(&swapchain, 0, sizeof(swapchain));
        swapchain.width        = width;
        swapchain.height       = height;
        swapchain.sample_count = 1;
        swapchain.color_format = SG_PIXELFORMAT_BGRA8;
        swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;

#if __APPLE__
        swapchain.metal.current_drawable      = pw_get_metal_drawable(gui->pw);
        swapchain.metal.depth_stencil_texture = pw_get_metal_depth_stencil_texture(gui->pw);
#endif
#if _WIN32
        swapchain.d3d11.render_view        = pw_get_dx11_render_target_view(gui->pw);
        swapchain.d3d11.depth_stencil_view = pw_get_dx11_depth_stencil_view(gui->pw);
#endif
        sg_begin_pass(gui->sg, &(sg_pass){.action = pass_action, .swapchain = swapchain});

        nvgBeginFrame(gui->nvg, width, height, pw_get_dpi(gui->pw));
    }

    NVGcontext* nvg = gui->nvg;

    const NVGcolor col_text = nvgRGBA(143, 150, 160, 255);

    for (int i = 0; i < ARRLEN(SLIDER_POSITIONS); i++)
    {
        xcomp_dimensions* d = &gui->sliders[i].dimensions;

        float cx     = d->x + 0.5f * d->width;
        float cy     = d->y + 0.5f * d->height;
        float radius = d->width * 0.5f;

        // Knob
        nvgBeginPath(nvg);
        nvgCircle(nvg, cx, cy, radius);
        nvgFillColor(nvg, nvgRGBA(91, 100, 109, 255));
        nvgFill(nvg);

        // Labels
        nvgFillColor(nvg, col_text);
        nvgTextAlign(nvg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(nvg, gui->scale * 16);

        static const char* NAMES[] = {"CUTOFF", "SCREAM", "RESONANCE"};
        _Static_assert(ARRLEN(NAMES) == ARRLEN(SLIDER_POSITIONS));
        _Static_assert(ARRLEN(NAMES) == ARRLEN(gui->sliders));
        nvgText(nvg, cx, cy + radius * 1.4, NAMES[i], NULL);

        char   label[24];
        double value = cplug_getParameterValue(gui->plugin, i);
        cplug_parameterValueToString(gui->plugin, i, label, sizeof(label), value);
        nvgText(nvg, cx, cy - radius * 1.2, label, NULL);

        // Slider Tick/Notch
        float value_norm  = cplug_normaliseParameterValue(gui->plugin, i, value);
        float angle_value = SLIDER_START_RAD + value_norm * SLIDER_LENGTH_RAD;

        float angle_x = cosf(angle_value);
        float angle_y = sinf(angle_value);

        float tick_radius_start = radius * 0.8f;
        float tick_radius_end   = radius * 0.4f;

        nvgBeginPath(nvg);
        nvgMoveTo(nvg, cx + tick_radius_start * angle_x, cy + tick_radius_start * angle_y);
        nvgLineTo(nvg, cx + tick_radius_end * angle_x, cy + tick_radius_end * angle_y);
        nvgStrokeWidth(nvg, gui->scale * 8);
        nvgStrokeColor(nvg, nvgRGBA(40, 47, 83, 255));
        nvgLineCap(nvg, NVG_ROUND);
        nvgStroke(nvg);
    }

    // Timer
    {
        uint64_t now  = xtime_now_ns();
        now          /= 1000000;
        double sec    = (double)now / 1000.0;
        nvgFillColor(gui->nvg, col_text);
        nvgFontSize(gui->nvg, gui->scale * 16);
        nvgTextAlign(gui->nvg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        char str[24];
        snprintf(str, sizeof(str), "%.3fsec", sec);
        const float height = gui->plugin->height;
        nvgText(gui->nvg, 20, height - 20, str, NULL);
    }

    // End frame
    nvgEndFrame(gui->nvg);
    sg_end_pass(gui->sg);
    sg_commit(gui->sg);
}

bool pw_event(const PWEvent* event)
{
    GUI* gui = event->gui;

    if (!gui || !gui->plugin)
        return false;

    switch (event->type)
    {
    case PW_EVENT_RESIZE:
    {
        // Retain size info for when the GUI is destroyed / reopened
        gui->plugin->width  = event->resize.width;
        gui->plugin->height = event->resize.height;

        gui->scale = (float)event->resize.width / (float)GUI_INIT_WIDTH;

        xcomp_dimensions dimensions = {0, 0, event->resize.width, event->resize.height};
        xcomp_set_dimensions(&gui->component, dimensions);

        float radius = SLIDER_RADIUS * dimensions.width;
        for (int i = 0; i < ARRLEN(SLIDER_POSITIONS); i++)
        {
            float pos = SLIDER_POSITIONS[i];
            float x   = pos * dimensions.width;
            float y   = 0.5f * dimensions.height;

            xcomp_dimensions d = {x - radius, y - radius, 2 * radius, 2 * radius};
            xcomp_set_dimensions(&gui->sliders[i], d);
        }
        break;
    }
    case PW_EVENT_MOUSE_EXIT:
    {
        xcomp_event_data data = {
            .x         = event->mouse.x,
            .y         = event->mouse.y,
            .modifiers = event->mouse.modifiers,
        };
        xcomp_send_mouse_position(&gui->root, data);
        break;
    }
    case PW_EVENT_MOUSE_ENTER:
    case PW_EVENT_MOUSE_MOVE:
    {
        xcomp_event_data data = {
            .x         = event->mouse.x,
            .y         = event->mouse.y,
            .modifiers = event->mouse.modifiers,
        };
        xcomp_send_mouse_position(&gui->root, data);
        break;
    }
    case PW_EVENT_MOUSE_SCROLL_WHEEL:
    case PW_EVENT_MOUSE_SCROLL_TOUCHPAD:
    {
        if (gui->root.mouse_over)
        {
            xcomp_event_data data = {
                .x         = event->mouse.x,
                .y         = event->mouse.y,
                .modifiers = event->mouse.modifiers,
            };
            bool     is_wheel = event->type == PW_EVENT_MOUSE_SCROLL_WHEEL;
            uint32_t ev_type  = is_wheel ? XCOMP_EVENT_MOUSE_SCROLL_WHEEL : XCOMP_EVENT_MOUSE_SCROLL_TOUCHPAD;

            gui->root.mouse_over->event_handler(gui->root.mouse_over, ev_type, data);
        }
        break;
    }
    case PW_EVENT_MOUSE_LEFT_DOWN:
    case PW_EVENT_MOUSE_RIGHT_DOWN:
    case PW_EVENT_MOUSE_MIDDLE_DOWN:
    {
        xcomp_event_data data = {
            .x         = event->mouse.x,
            .y         = event->mouse.y,
            .modifiers = event->mouse.modifiers,
        };
        xcomp_send_mouse_down(&gui->root, data);
        break;
    }
    case PW_EVENT_MOUSE_LEFT_UP:
    case PW_EVENT_MOUSE_RIGHT_UP:
    case PW_EVENT_MOUSE_MIDDLE_UP:
    {
        xcomp_event_data data = {
            .x         = event->mouse.x,
            .y         = event->mouse.y,
            .modifiers = event->mouse.modifiers,
        };
        xcomp_send_mouse_up(&gui->root, data, event->mouse.time_ms, event->mouse.double_click_interval_ms);
        break;
    }

    case PW_EVENT_KEY_FOCUS_LOST:
        xcomp_root_give_keyboard_focus(&gui->root, NULL);
        break;

    case PW_EVENT_DPI_CHANGED:
    case PW_EVENT_KEY_DOWN:
    case PW_EVENT_KEY_UP:
    case PW_EVENT_TEXT:
    case PW_EVENT_FILE_ENTER:
    case PW_EVENT_FILE_MOVE:
    case PW_EVENT_FILE_DROP:
    case PW_EVENT_FILE_EXIT:
        break;
    }

    return false;
}
