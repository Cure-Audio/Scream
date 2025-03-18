
#include "common.h"
#include "dsp.h"
#include "imgui.h"
#include "plot_dsp.h"
#include "plugin.h"

#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/time.h>
#include <xhl/vector.h>

#include <cplug_extensions/window.h>
#include <nanovg.h>
#include <nanovg_sokol.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NVG_ALIGN_TL (NVG_ALIGN_TOP | NVG_ALIGN_LEFT)
#define NVG_ALIGN_TC (NVG_ALIGN_TOP | NVG_ALIGN_CENTER)
#define NVG_ALIGN_TR (NVG_ALIGN_TOP | NVG_ALIGN_RIGHT)

#define NVG_ALIGN_CL (NVG_ALIGN_MIDDLE | NVG_ALIGN_LEFT)
#define NVG_ALIGN_CC (NVG_ALIGN_MIDDLE | NVG_ALIGN_CENTER)
#define NVG_ALIGN_CR (NVG_ALIGN_MIDDLE | NVG_ALIGN_RIGHT)

#define NVG_ALIGN_BL (NVG_ALIGN_BOTTOM | NVG_ALIGN_LEFT)
#define NVG_ALIGN_BC (NVG_ALIGN_BOTTOM | NVG_ALIGN_CENTER)
#define NVG_ALIGN_BR (NVG_ALIGN_BOTTOM | NVG_ALIGN_RIGHT)

typedef struct GUI
{
    Plugin*     plugin;
    void*       pw;
    void*       sg;
    NVGcontext* nvg;
    int         font_id;
    float       scale;

    struct imgui_context imgui;

    bool hover_params[NUM_PARAMS];
    bool drag_params[NUM_PARAMS];
    bool hover_panic_btn;
} GUI;

// Relative to GUI width
#define SLIDER_RADIUS 0.075f
// Angle radians
// 120deg
#define SLIDER_START_RAD 2.0943951023931953f
// 120deg + 360deg * 0.8333
#define SLIDER_END_RAD 7.330173418865945f
// end - start
#define SLIDER_LENGTH_RAD 5.23577831647275f

void main_dequeue_events(Plugin* p)
{
    CPLUG_LOG_ASSERT(is_main_thread());
    uint32_t head = xt_atomic_load_u32(&p->queue_main_head) & EVENT_QUEUE_MASK;
    uint32_t tail = p->queue_main_tail;

    if (p->gui)
    {
        GUI* gui = p->gui;
        if (head != tail)
            gui->imgui.num_duplicate_backbuffers = 0;
    }

    while (tail != head)
    {
        CplugEvent* event = &p->queue_main_events[tail];

        switch (event->type)
        {
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
        case EVENT_SET_PARAMETER:
        case EVENT_SET_PARAMETER_NOTIFYING_HOST:
            main_set_param(p, event->parameter.id, event->parameter.value);
            if (event->type == EVENT_SET_PARAMETER_NOTIFYING_HOST)
                main_notify_host_param_change(p, event->parameter.id, event->parameter.value);
            break;

        default:
            println("[MAIN] Unhandled event in main queue: %u", event->type);
            break;
        }

        tail++;
        tail &= EVENT_QUEUE_MASK;
    }
    p->queue_main_tail = tail;
}

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
    CPLUG_LOG_ASSERT(log_level > 1);
    CPLUG_LOG_ASSERT(log_level < ARRLEN(LOG_LEVEL));
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

/*
void cb_xcomp_slider(xcomp_component* comp, uint32_t event, xcomp_event_data data)
{
    GUI* gui        = comp->data;
    int  slider_idx = 0;
    for (; slider_idx < ARRLEN(gui->sliders); slider_idx++)
    {
        if (comp == &gui->sliders[slider_idx])
            break;
    }
    CPLUG_LOG_ASSERT(slider_idx != ARRLEN(gui->sliders));

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
        if (distance_px <= radius)
        {
            if (!(comp->flags & FLAG_HIT_TEST))
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);
            comp->flags |= FLAG_HIT_TEST;
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

        double val = gui->plugin->main_params[slider_idx];
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
    else if (event == XCOMP_EVENT_MOUSE_LEFT_DOUBLE_CLICK)
    {
        gui->root.left_click_counter = 0;

        double v = cplug_getDefaultParameterValue(gui->plugin, slider_idx);
        param_set(gui->plugin, slider_idx, v);
    }
    else if (event == XCOMP_EVENT_MOUSE_SCROLL_WHEEL)
    {
        double delta  = data.y / 120.0;
        delta        *= 0.01;

        const bool fine_increment = data.modifiers & (XCOMP_MOD_PLATFORM_KEY_CTRL | XCOMP_MOD_KEY_SHIFT);
        if (fine_increment)
            delta *= 0.01;

        double v  = gui->plugin->main_params[slider_idx];
        v        += delta;

        param_set(gui->plugin, slider_idx, v);
    }
}
*/

void* pw_create_gui(void* _plugin, void* _pw)
{
    CPLUG_LOG_ASSERT(_plugin);
    CPLUG_LOG_ASSERT(_pw);
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
    CPLUG_LOG_ASSERT(gui->nvg);

#ifdef _WIN32
    static const char* font_path = "C:\\Windows\\Fonts\\arial.ttf";
#elif defined(__APPLE__)
    static const char* font_path = "/Library/Fonts/Arial Unicode.ttf";
#endif
    int font_id = nvgCreateFont(gui->nvg, "Arial", font_path);
    CPLUG_LOG_ASSERT(font_id != -1);
    if (font_id == -1)
    {
        println("[CRITICAL] Failed to open font at path %s", font_path);
    }

    gui->font_id = font_id;
    gui->scale   = (float)gui->plugin->width / (float)GUI_INIT_WIDTH;

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
    CPLUG_LOG_ASSERT(gui->plugin);
    CPLUG_LOG_ASSERT(gui->nvg);
    if (!gui || !gui->plugin || !gui->nvg)
        return;

    main_dequeue_events(gui->plugin);

#if defined(_WIN32)
    // Using the CPLUG window extension, we have configured our DXGI backbuffer count to a maximum of 2
    const uint32_t MAX_DUP_BACKBUFFER_COUNT = 2;
#elif defined(__APPLE__)
    // Using the CPLUG window extension, we use the default settings in MTKView, which appears to work fine with
    const uint32_t MAX_DUP_BACKBUFFER_COUNT = 1;
#endif

    if (gui->imgui.num_duplicate_backbuffers >= MAX_DUP_BACKBUFFER_COUNT)
        return;

    int width  = gui->plugin->width;
    int height = gui->plugin->height;

    // Begin frame
    {
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

    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;

    const NVGcolor col_text = nvgRGBA(143, 150, 160, 255);

    // Main parameters
    static const float SLIDER_POSITIONS[] = {0.1666, 0.3333, 0.5, 0.6666, 0.8333};
    _Static_assert(ARRLEN(SLIDER_POSITIONS) == NUM_PARAMS);
    const float slider_radius = SLIDER_RADIUS * width;
    for (int i = 0; i < ARRLEN(SLIDER_POSITIONS); i++)
    {
        xvec2f pt;
        pt.x = SLIDER_POSITIONS[i] * width;
        pt.y = height * 0.5f;

        double value_d = gui->plugin->main_params[i];
        float  value_f = value_d;

        uint32_t events = imgui_get_events_circle(im, pt, slider_radius);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);
        if ((events & IMGUI_EVENT_MOUSE_EXIT) && im->mouse_over_id == 0)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_ARROW);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            if (im->left_click_counter == 2)
            {
                im->left_click_counter = 0; // single and triple click not supported

                value_d = value_f = cplug_getDefaultParameterValue(gui->plugin, i);
                param_set(gui->plugin, i, value_d);
            }
        }

        if (events & IMGUI_EVENT_DRAG_BEGIN)
            param_change_begin(gui->plugin, i);
        if (events & IMGUI_EVENT_DRAG_MOVE)
        {
            float next_value = value_f;
            imgui_drag_value(im, &next_value, 0, 1, IMGUI_DRAG_VERTICAL);
            bool changed = value_f != next_value;
            if (changed)
            {
                value_d = value_f = next_value;
                param_change_update(gui->plugin, i, value_d);
            }
        }
        if (events & IMGUI_EVENT_DRAG_END)
            param_change_end(gui->plugin, i);

        // Knob
        nvgBeginPath(nvg);
        nvgCircle(nvg, pt.x, pt.y, slider_radius);
        nvgFillColor(nvg, nvgRGBA(91, 100, 109, 255));
        nvgFill(nvg);

        // Labels
        nvgFillColor(nvg, col_text);
        nvgTextAlign(nvg, NVG_ALIGN_CC);
        nvgFontSize(nvg, gui->scale * 16);

        char label[24];
        cplug_getParameterName(gui->plugin, i, label, sizeof(label));
        nvgText(nvg, pt.x, pt.y + slider_radius * 1.4, label, NULL);

        cplug_parameterValueToString(gui->plugin, i, label, sizeof(label), value_d);
        nvgText(nvg, pt.x, pt.y - slider_radius * 1.2, label, NULL);

        // Slider Tick/Notch
        float value_norm  = cplug_normaliseParameterValue(gui->plugin, i, value_d);
        float angle_value = SLIDER_START_RAD + value_norm * SLIDER_LENGTH_RAD;

        float angle_x = cosf(angle_value);
        float angle_y = sinf(angle_value);

        float tick_radius_start = slider_radius * 0.8f;
        float tick_radius_end   = slider_radius * 0.4f;

        nvgBeginPath(nvg);
        nvgMoveTo(nvg, pt.x + tick_radius_start * angle_x, pt.y + tick_radius_start * angle_y);
        nvgLineTo(nvg, pt.x + tick_radius_end * angle_x, pt.y + tick_radius_end * angle_y);
        nvgStrokeWidth(nvg, gui->scale * 8);
        nvgStrokeColor(nvg, nvgRGBA(40, 47, 83, 255));
        nvgLineCap(nvg, NVG_ROUND);
        nvgStroke(nvg);
    }

    if (gui->plugin->is_clipping)
    {
        nvgTextAlign(nvg, NVG_ALIGN_BR);
        nvgFillColor(nvg, nvgRGBAf(1, 0.1, 0.1, 1));
        float dB = xm_fast_gain_to_dB(gui->plugin->peak_gain);
        char  label[48];
        snprintf(label, sizeof(label), "[WARNING] Auto hardclipper: ON. %.2fdB", dB);
        nvgText(nvg, width - 20, height - 20, label, NULL);
    }

    // Panic button
    {
        imgui_widget d = {width - 100, 0, width, 40};

        uint32_t events = imgui_get_events_rect(im, &d);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
        if ((events & IMGUI_EVENT_MOUSE_EXIT) && im->mouse_over_id == 0)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_ARROW);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            CplugEvent e = {0};
            e.type       = EVENT_PANIC_BUTTON_PRESSED;
            send_to_audio_event_queue(gui->plugin, e);
            println("PANIC!");
        }

        nvgBeginPath(nvg);
        nvgRect(nvg, d.x, d.y, d.r - d.x, d.b - d.y);
        const bool hover = events & IMGUI_EVENT_MOUSE_HOVER;
        NVGcolor   bgcol = hover ? (NVGcolor){1.0f, 0.15f, 0.3f, 1.0f} : (NVGcolor){0.8f, 0.1f, 0.2f, 1.0f};
        nvgFillColor(nvg, bgcol);
        nvgFill(nvg);

        nvgFillColor(nvg, (NVGcolor){0.9f, 0.9f, 0.2f, 1.0f});
        nvgTextAlign(nvg, NVG_ALIGN_CC);
        if (events & IMGUI_EVENT_MOUSE_LEFT_HOLD)
        {
            d.y += 1.0f;
            d.b += 1.0f;
        }
        float cx = (d.x + d.r) * 0.5f;
        float cy = (d.y + d.b) * 0.5f;
        nvgText(nvg, cx, cy, "PANIC!", NULL);
    }

    {
        // plot_expander(nvg, width, height);
        // plot_peak_detection(nvg, width, height);
        // plot_peak_distortion(nvg, 0, 2, height - 4, height - 4, gui->plugin->main_params[PARAM_FEEDBACK_GAIN]);
    }

    // End frame
    nvgEndFrame(gui->nvg);
    sg_end_pass(gui->sg);
    sg_commit(gui->sg);

    imgui_end_frame(&gui->imgui);
}

bool pw_event(const PWEvent* event)
{
    GUI* gui = event->gui;

    if (!gui || !gui->plugin)
        return false;

    if (PW_EVENT_RESIZE)
    {
        // Retain size info for when the GUI is destroyed / reopened
        gui->plugin->width  = event->resize.width;
        gui->plugin->height = event->resize.height;
        gui->scale          = (float)event->resize.width / (float)GUI_INIT_WIDTH;
    }
    imgui_send_event(&gui->imgui, event);

    return false;
}
