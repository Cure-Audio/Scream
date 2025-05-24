
#include "common.h"
#include "dsp.h"
#include "imgui.h"
#include "plugin.h"

#ifdef CPLUG_BUILD_STANDALONE
#include "plot_dsp.h"
#endif

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

typedef struct GUI
{
    Plugin*     plugin;
    void*       pw;
    void*       sg;
    NVGcontext* nvg;
    int         font_id;
    float       scale;

    struct imgui_context imgui;

    float input_gain_peaks_slow[2];
    float input_gain_peaks_fast[2];
} GUI;

// Relative to GUI width
#define SLIDER_RADIUS 0.085f
// Angle radians
// 120deg
#define SLIDER_START_RAD 2.0943951023931953f
// 120deg + 360deg * 0.8333
#define SLIDER_END_RAD 7.330173418865945f
// end - start
#define SLIDER_LENGTH_RAD 5.23577831647275f

// Nanovg helpers
// clang-format off
#define NVG_ALIGN_TL (NVG_ALIGN_TOP | NVG_ALIGN_LEFT)
#define NVG_ALIGN_TC (NVG_ALIGN_TOP | NVG_ALIGN_CENTER)
#define NVG_ALIGN_TR (NVG_ALIGN_TOP | NVG_ALIGN_RIGHT)

#define NVG_ALIGN_CL (NVG_ALIGN_MIDDLE | NVG_ALIGN_LEFT)
#define NVG_ALIGN_CC (NVG_ALIGN_MIDDLE | NVG_ALIGN_CENTER)
#define NVG_ALIGN_CR (NVG_ALIGN_MIDDLE | NVG_ALIGN_RIGHT)

#define NVG_ALIGN_BL (NVG_ALIGN_BOTTOM | NVG_ALIGN_LEFT)
#define NVG_ALIGN_BC (NVG_ALIGN_BOTTOM | NVG_ALIGN_CENTER)
#define NVG_ALIGN_BR (NVG_ALIGN_BOTTOM | NVG_ALIGN_RIGHT)

// Fake english to real english helpers
typedef struct NVGcolor NVGcolour;
static inline void nvgFillColour(NVGcontext* ctx, NVGcolour col) { nvgFillColor(ctx, col); }
static inline void nvgStrokeColour(NVGcontext* ctx, NVGcolour col) { nvgStrokeColor(ctx, col); }
#define nvgHexColour(hex) (NVGcolour){( hex >> 24)         / 255.0f,\
                                     ((hex >> 16) & 0xff) / 255.0f,\
                                     ((hex >>  8) & 0xff) / 255.0f,\
                                     ( hex        & 0xff) / 255.0f}
// clang-format on

static const NVGcolour COLOUR_TEXT = nvgHexColour(0x8D949BFF);

static const NVGcolour COLOUR_BG_LIGHT = nvgHexColour(0xC9D3DDFF);
static const NVGcolour COLOUR_BG_DARK  = nvgHexColour(0x151B32FF);

static const NVGcolour COLOUR_GREY_1 = nvgHexColour(0xB5BEC7FF);
static const NVGcolour COLOUR_GREY_2 = nvgHexColour(0x636A78FF);

void main_set_param(Plugin* p, ParamID id, double value);
void main_notify_host_param_change(Plugin* p, ParamID id, double value);
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
    CPLUG_LOG_ASSERT(log_level > 1 && log_level < ARRLEN(LOG_LEVEL));
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

        info->constrain_size.width  = width;
        info->constrain_size.height = height;
    }
}

double handle_param_events(GUI* gui, ParamID param_id, uint32_t events, float drag_range_px)
{
    imgui_context* im      = &gui->imgui;
    double         value_d = gui->plugin->main_params[param_id];
    float          value_f = value_d;

    if (events & IMGUI_EVENT_MOUSE_ENTER)
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);
    if ((events & IMGUI_EVENT_MOUSE_EXIT) && im->mouse_over_id == 0)
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_ARROW);

    if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
    {
        if (im->left_click_counter == 2)
        {
            im->left_click_counter = 0; // single and triple click not supported

            value_d = value_f = cplug_getDefaultParameterValue(gui->plugin, param_id);
            param_set(gui->plugin, param_id, value_d);
        }
    }

    if (events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_TOUCHPAD_BEGIN))
        param_change_begin(gui->plugin, param_id);
    if (events & IMGUI_EVENT_DRAG_MOVE)
    {
        float next_value = value_f;
        imgui_drag_value(im, &next_value, 0, 1, drag_range_px, IMGUI_DRAG_VERTICAL);
        bool changed = value_f != next_value;
        if (changed)
        {
            value_d = value_f = next_value;
            param_change_update(gui->plugin, param_id, value_d);
        }
    }
    if (events & IMGUI_EVENT_TOUCHPAD_MOVE)
    {
        float delta = im->mouse_touchpad.y / drag_range_px;
        if (im->mouse_touchpad_mods & PW_MOD_INVERTED_SCROLL)
            delta = -delta;
        if (im->mouse_touchpad_mods & PW_MOD_PLATFORM_KEY_CTRL)
            delta *= 0.1f;
        if (im->mouse_touchpad_mods & PW_MOD_KEY_SHIFT)
            delta *= 0.1f;

        float next_value = xm_clampf(value_f + delta, 0, 1);

        bool changed = value_f != next_value;
        if (changed)
        {
            value_d = value_f = next_value;
            param_change_update(gui->plugin, param_id, value_d);
        }
    }
    if (events & (IMGUI_EVENT_DRAG_END | IMGUI_EVENT_TOUCHPAD_END))
        param_change_end(gui->plugin, param_id);
    if (events & IMGUI_EVENT_MOUSE_WHEEL)
    {
        double delta = im->mouse_wheel * 0.1;
        if (im->mouse_wheel_mods & PW_MOD_PLATFORM_KEY_CTRL)
            delta *= 0.1;
        if (im->mouse_wheel_mods & PW_MOD_KEY_SHIFT)
            delta *= 0.1;

        double v  = gui->plugin->main_params[param_id];
        v        += delta;
        param_set(gui->plugin, param_id, v);
    }
    return value_d;
}

void* pw_create_gui(void* _plugin, void* _pw)
{
    CPLUG_LOG_ASSERT(_plugin);
    CPLUG_LOG_ASSERT(_pw);
    Plugin* p   = _plugin;
    GUI*    gui = MY_CALLOC(1, sizeof(*gui));
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
    env.d3d11.device             = pw_get_dx11_device(gui->pw);
    env.d3d11.device_context     = pw_get_dx11_device_context(gui->pw);
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

    nvgDeleteSokol(gui->nvg);
    sg_shutdown(gui->sg);

    gui->plugin->gui = NULL;
    MY_FREE(gui);

#ifdef CPLUG_BUILD_STANDALONE
    if (buffer_audio)
    {
        MY_FREE(buffer_audio);
        buffer_audio = NULL;
    }
    if (buffer_processed)
    {
        MY_FREE(buffer_processed);
        buffer_processed = NULL;
    }
    if (oscilloscope_ringbuf)
    {
        MY_FREE(oscilloscope_ringbuf);
        oscilloscope_ringbuf = NULL;
    }
#endif // CPLUG_BUILD_STANDALONE
}

bool pw_event(const PWEvent* event)
{
    GUI* gui = event->gui;

    if (!gui || !gui->plugin)
        return false;

    if (event->type == PW_EVENT_RESIZE)
    {
        // Retain size info for when the GUI is destroyed / reopened
        gui->plugin->width  = event->resize.width;
        gui->plugin->height = event->resize.height;
        gui->scale          = (float)event->resize.width / (float)GUI_INIT_WIDTH;
    }
    imgui_send_event(&gui->imgui, event);

    return false;
}

void pw_tick(void* _gui)
{
    GUI* gui = _gui;
    CPLUG_LOG_ASSERT(gui->plugin);
    CPLUG_LOG_ASSERT(gui->nvg);
    if (!gui || !gui->plugin)
        return;

    main_dequeue_events(gui->plugin);

    // #ifndef NDEBUG
    uint64_t frame_time_start = xtime_now_ns();
    // #endif
    if (!gui->nvg)
        return;

#if defined(_WIN32)
    // Using the CPLUG window extension, we have configured our DXGI backbuffer count to a maximum of 2
    const uint32_t MAX_DUP_BACKBUFFER_COUNT = 2 + 2;
#elif defined(__APPLE__)
    // Using the CPLUG window extension, we use the default settings in MTKView, which appears to work fine with
    const uint32_t MAX_DUP_BACKBUFFER_COUNT = 1;
#endif

#ifdef CPLUG_BUILD_STANDALONE
    if (oscilloscope_ringbuf)
        gui->imgui.num_duplicate_backbuffers = 0;
#endif

    // Uncomment to enable event driven redrawing
    // if (gui->imgui.num_duplicate_backbuffers >= MAX_DUP_BACKBUFFER_COUNT)
    //     return;

    const int   gui_width  = gui->plugin->width;
    const int   gui_height = gui->plugin->height;
    const float dpi        = pw_get_dpi(gui->pw);

    // Begin frame
    {
        sg_pass_action pass_action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0, 0, 0, 1.0f}}};

        sg_swapchain swapchain;
        memset(&swapchain, 0, sizeof(swapchain));
        swapchain.width        = gui_width;
        swapchain.height       = gui_height;
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

        nvgBeginFrame(gui->nvg, gui_width, gui_height, dpi);
    }

    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;

    // Layout
    enum
    {
        HEIGHT_HEADER = 32,
        HEIGHT_FOOTER = 20,
    };

    const float height_header  = HEIGHT_HEADER * dpi;
    const float height_footer  = HEIGHT_FOOTER * dpi;
    const float height_content = gui_height - height_header - height_footer;

    // Background
    {
        nvgBeginPath(nvg);
        nvgRect(nvg, 0, 0, gui_width, gui_height);
        static const NVGcolour stop0 = nvgHexColour(0x151B33FF);
        static const NVGcolour stop1 = nvgHexColour(0x090E1FFF);
        nvgFillPaint(nvg, nvgLinearGradient(nvg, 0, 0, 0, gui_height, stop0, stop1));
        nvgFill(nvg);
    }

    // Header
    {
        nvgFontSize(nvg, dpi * 24);
        nvgFillColour(nvg, COLOUR_BG_LIGHT);
        nvgTextAlign(nvg, NVG_ALIGN_CC);
        nvgText(nvg, gui_width * 0.5f, height_header * 0.5f + 4, "SCREAM", NULL);
    }

    // Main content background
    {
        const float content_x      = 8;
        const float content_r      = gui_width - 8;
        const float content_y      = floorf(height_header + 8);
        const float content_b      = floorf(gui_height - height_footer - 16);
        const float content_height = content_b - content_y;

        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, 8, content_y, gui_width - 16, content_height, 8);
        nvgFillColour(nvg, COLOUR_BG_LIGHT);
        nvgFill(nvg);

        // Inner shadows
        const float blur_radius = 8;
        float       grad_x      = content_x - blur_radius * 0.5f;
        float       grad_r      = content_r + blur_radius * 0.5f;
        float       grad_w      = grad_r - grad_x;

        NVGcolour icol  = (NVGcolour){1, 1, 1, 0};
        NVGcolour ocol  = (NVGcolour){1, 1, 1, 0.75};
        NVGpaint  paint = nvgBoxGradient(nvg, grad_x, content_y, grad_w, content_height, 16, blur_radius, icol, ocol);

        // Top inner shadow (light)
        nvgBeginPath(nvg);
        nvgRoundedRectVarying(nvg, 8, content_y, gui_width - 16, blur_radius * 2, 8, 8, 0, 0);
        nvgFillPaint(nvg, paint);
        nvgFill(nvg);

        // Bottom inner shadow (dark)
        paint.innerColor = (NVGcolour){0, 0, 0, 0};
        paint.outerColor = (NVGcolour){0, 0, 0, 0.75f};
        nvgBeginPath(nvg);
        nvgRoundedRectVarying(nvg, 8, content_b - blur_radius * 2, gui_width - 16, blur_radius * 2, 0, 0, 8, 8);
        nvgFillPaint(nvg, paint);
        nvgFill(nvg);

        // Dots
        const float DOT_DIAMETER = 6;
        const float DOT_RADIUS   = DOT_DIAMETER / 2;
        const float DOT_PADDING  = 6;

        const float left_dot_cx  = roundf(content_x + 8 + DOT_RADIUS);
        const float right_dot_cx = roundf(content_r - 8 - DOT_RADIUS);
        const float top_dot_cy   = roundf(content_y + 8 + DOT_RADIUS);
        const float dot_offset   = roundf(DOT_DIAMETER + DOT_PADDING);

        const xvec2f points[] = {
            // Left
            {left_dot_cx, top_dot_cy},
            {left_dot_cx + dot_offset, top_dot_cy},
            {left_dot_cx + dot_offset + dot_offset, top_dot_cy},
            {left_dot_cx, top_dot_cy + dot_offset},
            {left_dot_cx, top_dot_cy + dot_offset + dot_offset},
            // Right
            {right_dot_cx, top_dot_cy},
            {right_dot_cx - dot_offset, top_dot_cy},
            {right_dot_cx - dot_offset - dot_offset, top_dot_cy},
            {right_dot_cx, top_dot_cy + dot_offset},
            {right_dot_cx, top_dot_cy + dot_offset + dot_offset},
        };
        _Static_assert(ARRLEN(points) == 10, "Should be 5 dots each side");

        nvgBeginPath(nvg);
        for (int i = 0; i < ARRLEN(points); i++)
        {
            nvgCircle(nvg, points[i].x, points[i].y, DOT_RADIUS);
        }
        nvgFillColor(nvg, (NVGcolour){1, 1, 1, 1});

        nvgFill(nvg);
        nvgBeginPath(nvg);
        for (int i = 0; i < ARRLEN(points); i++)
        {
            nvgCircle(nvg, points[i].x, points[i].y - 1, DOT_RADIUS);
        }
        nvgFillColor(nvg, nvgHexColour(0x111629FF));
        nvgFill(nvg);
    }

    // Params
    {
        enum
        {
            PARAMS_BOUNDARY_LEFT  = 32,
            VERTICAL_SLIDER_WIDTH = 60,
            METER_WIDTH           = 24,
            METER_HEIGHT          = 146,
            ROTARY_PARAM_DIAMETER = 160,

            _MINIMUM_WIDTH = PARAMS_BOUNDARY_LEFT * 2 + VERTICAL_SLIDER_WIDTH * 2 + ROTARY_PARAM_DIAMETER * 3,
        };
        _Static_assert(_MINIMUM_WIDTH < GUI_MIN_WIDTH, "");

        const float PARAMS_BOUNDARY_RIGHT = gui_width - 32;
        const float PARAMS_WIDTH          = PARAMS_BOUNDARY_RIGHT - PARAMS_BOUNDARY_LEFT;

        const float scale_x = (float)gui_width / (float)GUI_INIT_WIDTH;
        const float scale_y = (float)gui_height / (float)GUI_INIT_HEIGHT;

        const float param_scale = xm_minf(1, xm_minf(scale_x, scale_y));

#define snapf(val, interval) (roundf((val) / (interval)) * (interval))

        const float veritcal_slider_width = snapf(VERTICAL_SLIDER_WIDTH * param_scale, 2);
        const float rotary_param_diameter = snapf(ROTARY_PARAM_DIAMETER * param_scale, 2);

        const float total_param_width = veritcal_slider_width * 2 + rotary_param_diameter * 3;
        const float param_padding     = (PARAMS_WIDTH - total_param_width) / 4;

        const struct
        {
            ParamID param_id;
            float   cx;
        } param_positions[] = {
            {PARAM_CUTOFF, PARAMS_BOUNDARY_LEFT + veritcal_slider_width + param_padding + rotary_param_diameter * 0.5f},
            {PARAM_SCREAM,
             PARAMS_BOUNDARY_LEFT + veritcal_slider_width + param_padding * 2 + rotary_param_diameter * 1.5f},
            {PARAM_RESONANCE,
             PARAMS_BOUNDARY_LEFT + veritcal_slider_width + param_padding * 3 + rotary_param_diameter * 2.5f},
            {PARAM_INPUT_GAIN, PARAMS_BOUNDARY_LEFT + veritcal_slider_width * 0.5f},
            {PARAM_WET, PARAMS_BOUNDARY_RIGHT - veritcal_slider_width * 0.5f},
        };

        for (int i = 0; i < ARRLEN(param_positions); i++)
        {
            const ParamID param_id = param_positions[i].param_id;
            const float   param_cx = param_positions[i].cx;

            switch (param_id)
            {
            case PARAM_CUTOFF:
            case PARAM_SCREAM:
            case PARAM_RESONANCE:
                break;
            case PARAM_INPUT_GAIN:
            {
                const float meter_width  = snapf(METER_WIDTH * param_scale, 2);
                const float meter_height = snapf(METER_HEIGHT * param_scale, 2);

                imgui_rect rect;

                rect.x = roundf(param_cx - meter_width * 0.5);
                rect.r = roundf(param_cx + meter_width * 0.5);
                rect.y = roundf(gui_height * 0.5 - meter_height * 0.5f);
                rect.b = roundf(gui_height * 0.5 + meter_height * 0.5f);

                // Shadows
                {
                    // Top
                    imgui_rect shadow_rect = rect;
                    // Translate NW 4px
                    shadow_rect.x -= 4;
                    shadow_rect.y -= 4;
                    shadow_rect.r -= 4;
                    shadow_rect.b -= 4;
                    // Expand 4px
                    shadow_rect.x    -= 4;
                    shadow_rect.y    -= 4;
                    shadow_rect.r    += 4;
                    shadow_rect.b    += 4;
                    const float blur  = 12;

                    float w = shadow_rect.r - shadow_rect.x;
                    float h = shadow_rect.b - shadow_rect.y;

                    // NVGcolour top_iol = nvgHexColour(0xE9EDF1E0);
                    static const NVGcolour top_iol  = nvgHexColour(0xE9EDF1BF);
                    static const NVGcolour top_ocol = nvgHexColour(0xE9EDF100);

                    NVGpaint paint =
                        nvgBoxGradient(nvg, shadow_rect.x, shadow_rect.y, w, h, blur, blur, top_iol, top_ocol);

                    // Expand
                    shadow_rect.x -= blur;
                    shadow_rect.y -= blur;
                    shadow_rect.r += blur;
                    shadow_rect.b += blur;
                    w              = shadow_rect.r - shadow_rect.x;
                    h              = shadow_rect.b - shadow_rect.y;
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, shadow_rect.x, shadow_rect.y, w, h, blur);
                    nvgFillPaint(nvg, paint);
                    nvgFill(nvg);

                    // Bottom
                    shadow_rect = rect;
                    // Translate NW 4px
                    shadow_rect.x += 4;
                    shadow_rect.y += 4;
                    shadow_rect.r += 4;
                    shadow_rect.b += 4;
                    // Expand 4px
                    shadow_rect.x -= 4;
                    shadow_rect.y -= 4;
                    shadow_rect.r += 4;
                    shadow_rect.b += 4;

                    w = shadow_rect.r - shadow_rect.x;
                    h = shadow_rect.b - shadow_rect.y;

                    static const NVGcolour bot_iol  = nvgHexColour(0xABB2BABF);
                    static const NVGcolour bot_ocol = nvgHexColour(0xABB2BA00);

                    paint = nvgBoxGradient(nvg, shadow_rect.x, shadow_rect.y, w, h, blur, blur, bot_iol, bot_ocol);

                    // Expand
                    shadow_rect.x -= blur;
                    shadow_rect.y -= blur;
                    shadow_rect.r += blur;
                    shadow_rect.b += blur;
                    w              = shadow_rect.r - shadow_rect.x;
                    h              = shadow_rect.b - shadow_rect.y;
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, shadow_rect.x, shadow_rect.y, w, h, blur);
                    nvgFillPaint(nvg, paint);
                    nvgFill(nvg);
                }

                float       rect_r     = rect.r;
                const float icon_width = 10;
                float       icon_r     = rect.r + icon_width + 4;
                rect.r                 = icon_r;
                uint32_t events        = imgui_get_events_rect(im, &rect);
                rect.r                 = rect_r;

                double value_d = handle_param_events(gui, PARAM_INPUT_GAIN, events, rect.b - rect.y);

                nvgBeginPath(nvg);
                nvgRoundedRect(nvg, rect.x, rect.y, rect.r - rect.x, rect.b - rect.y, 4);
                nvgFillColour(nvg, nvgHexColour(0x2C2F35FF));

                static const NVGcolour bg_grad_stop0 = nvgHexColour(0x2C2F35FF);
                static const NVGcolour bg_grad_stop1 = nvgHexColour(0x585E6AFF);
                const NVGpaint bg_paint = nvgLinearGradient(nvg, 0, rect.y, 0, rect.b, bg_grad_stop0, bg_grad_stop1);
                nvgFillPaint(nvg, bg_paint);
                nvgFill(nvg);

                // Value icon
                {
                    float icon_x = icon_r - icon_width;

                    float shadow_radius = 8;

                    float icon_y = xm_lerpf(value_d, rect.b, rect.y);

                    static const NVGcolour icol      = {0, 0, 0, 0.3};
                    static const NVGcolour ocol      = {0, 0, 0, 0};
                    float                  shadow_cx = icon_r - icon_width * 0.3f;
                    NVGpaint paint = nvgRadialGradient(nvg, shadow_cx, icon_y + 2, 0, shadow_radius, icol, ocol);

                    nvgBeginPath(nvg);
                    nvgCircle(nvg, shadow_cx, icon_y + 2, shadow_radius);
                    nvgFillPaint(nvg, paint);
                    nvgFill(nvg);

                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, icon_x, icon_y);
                    nvgLineTo(nvg, icon_r, icon_y - 8);
                    nvgLineTo(nvg, icon_r, icon_y + 8);
                    nvgClosePath(nvg);
                    nvgFillColour(nvg, COLOUR_GREY_2);
                    nvgFill(nvg);
                }

                xvec2f peaks;
                peaks.u64 = xt_atomic_load_u64(&gui->plugin->gui_input_peak_gain);

                float ch_w    = roundf(meter_width * 0.25);
                float padding = roundf(meter_width / 6.0f);

                const float ch_y    = rect.y + padding;
                const float ch_b    = rect.b - padding;
                const float ch_h    = ch_b - ch_y;
                const float ch_x[2] = {
                    roundf(rect.x + padding),
                    roundf(rect.r - padding - ch_w),
                };

                // Background
                nvgBeginPath(nvg);
                for (int ch = 0; ch < 2; ch++)
                {
                    nvgRoundedRect(nvg, ch_x[ch], ch_y, ch_w, ch_h, 2);
                }

                static const NVGcolour ch_grad_stop0 = nvgHexColour(0x6C7483FF);
                static const NVGcolour ch_grad_stop1 = nvgHexColour(0x7C8493FF);

                const NVGpaint ch_bg_grad = nvgLinearGradient(nvg, 0, ch_y, 0, ch_b, ch_grad_stop0, ch_grad_stop1);
                nvgFillPaint(nvg, ch_bg_grad);
                nvgFill(nvg);

                const double release_time_slow =
                    xm_fast_dB_to_gain((RANGE_INPUT_GAIN_MIN - RANGE_INPUT_GAIN_MAX) / (60 * 2));
                const double release_time_fast = xm_fast_dB_to_gain((RANGE_INPUT_GAIN_MIN - RANGE_INPUT_GAIN_MAX) / 30);

                for (int ch = 0; ch < 2; ch++)
                {
                    // double release_time_slow = convert_compressor_time(6);
                    // double release_time_fast = convert_compressor_time(1);
                    gui->input_gain_peaks_slow[ch] =
                        detect_peak(peaks.data[ch], gui->input_gain_peaks_slow[ch], 0, release_time_slow);
                    gui->input_gain_peaks_fast[ch] =
                        detect_peak(peaks.data[ch], gui->input_gain_peaks_fast[ch], 0, release_time_fast);
                }

                // Decaying Peak
                nvgBeginPath(nvg);
                bool has_peaks = false;
                for (int ch = 0; ch < 2; ch++)
                {
                    float peak_dB_1 = xm_fast_gain_to_dB(gui->input_gain_peaks_slow[ch]);
                    if (peak_dB_1 > RANGE_INPUT_GAIN_MIN)
                    {
                        float norm        = xm_normf(peak_dB_1, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                        float peak_height = norm * ch_h;
                        nvgRoundedRect(nvg, ch_x[ch], ch_b - peak_height, ch_w, peak_height, 2);
                        has_peaks = true;
                    }
                }
                if (has_peaks)
                {
                    nvgFillColour(nvg, nvgHexColour(0x459DB5FF));
                    nvgFill(nvg);
                }

                // Realtime Peak
                float rt_peak_dB[2] = {
                    xm_fast_gain_to_dB(peaks.data[0]),
                    xm_fast_gain_to_dB(peaks.data[1]),
                };
                float rt_peak_h[2] = {gui_height, gui_height};
                float rt_peak_y[2] = {gui_height, gui_height};

                for (int ch = 0; ch < 2; ch++)
                {
                    float norm        = xm_normf(rt_peak_dB[ch], RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                    float peak_height = norm * ch_h;
                    rt_peak_h[ch]     = peak_height;
                    rt_peak_y[ch]     = ch_b - peak_height;
                }
                // Peak glow/shadow
                // Shadows need to be drawn first to prevent spilling onto the realtime peak in the foreground
                for (int ch = 0; ch < 2; ch++)
                {
                    if (rt_peak_dB[ch] > RANGE_INPUT_GAIN_MIN)
                    {
                        static const float blur = 4;

                        float gx = ch_x[ch] - blur;
                        float gy = rt_peak_y[ch] - blur;
                        float gw = ch_w + blur * 2;
                        float gh = rt_peak_h[ch] + blur * 2;

                        static const NVGcolour icol = nvgHexColour(0x4DB9FC72);
                        static const NVGcolour ocol = nvgHexColour(0x4DB9FC00);

                        NVGpaint paint = nvgBoxGradient(
                            nvg,
                            gx + blur * 0.5f,
                            gy + blur * 0.5,
                            gw - blur,
                            gh - blur,
                            blur,
                            blur,
                            icol,
                            ocol);

                        nvgBeginPath(nvg);
                        nvgRoundedRect(nvg, gx, gy, gw, gh, blur);
                        nvgFillPaint(nvg, paint);
                        nvgFill(nvg);
                    }
                }

                // Foreground realtime peak
                for (int ch = 0; ch < 2; ch++)
                {
                    if (rt_peak_dB[ch] > RANGE_INPUT_GAIN_MIN)
                    {
                        nvgBeginPath(nvg);
                        nvgFillColour(nvg, nvgHexColour(0xACDEECFF));
                        nvgRoundedRect(nvg, ch_x[ch], rt_peak_y[ch], ch_w, rt_peak_h[ch], 2);
                        nvgFill(nvg);
                    }
                }

                // 0dB notch
                float zero_dB_pos = xm_normf(0, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                float zero_dB_y   = rect.b - padding - zero_dB_pos * ch_h;
                nvgBeginPath(nvg);
                nvgMoveTo(nvg, rect.x, zero_dB_y);
                nvgLineTo(nvg, rect.r, zero_dB_y);
                nvgStrokePaint(nvg, bg_paint);
                nvgStrokeWidth(nvg, 1);
                nvgStroke(nvg);
                break;
            }
            case PARAM_WET:
                break;
            }
        }

        nvgFillColour(nvg, COLOUR_TEXT);
        nvgTextAlign(nvg, NVG_ALIGN_CC);
        nvgFontSize(nvg, dpi * 14);

        static const char* NAMES[] = {"CUTOFF", "SCREAM", "RESONANCE", "INPUT", "WET"};
        _Static_assert(ARRLEN(NAMES) == NUM_PARAMS);
        for (int i = 0; i < ARRLEN(param_positions); i++)
        {
            const ParamID param_id = param_positions[i].param_id;
            const float   param_cx = param_positions[i].cx;

            nvgText(nvg, param_cx, gui_height * 0.75, NAMES[param_id], NULL);

            char   label[24];
            double value = cplug_getParameterValue(gui->plugin, param_id);
            cplug_parameterValueToString(gui->plugin, param_id, label, sizeof(label), value);
            nvgText(nvg, param_cx, gui_height * 0.25, label, NULL);
        }
    }

    /*
    // Main parameters
    static const float SLIDER_POSITIONS[] = {0.3, 0.5, 0.7};
    const float        slider_radius      = SLIDER_RADIUS * gui_width;
    for (int i = 0; i < ARRLEN(SLIDER_POSITIONS); i++)
    {
        imgui_pt pt;
        pt.x = SLIDER_POSITIONS[i] * gui_width;
        pt.y = gui_height * 0.5f;

        uint32_t events  = imgui_get_events_circle(im, pt, slider_radius);
        double   value_d = handle_param_events(gui, i, events, 300);

        // Knob
        nvgBeginPath(nvg);
        nvgCircle(nvg, pt.x, pt.y, slider_radius);
        nvgFillColour(nvg, nvgRGBA(91, 100, 109, 255));
        nvgFill(nvg);

        // Labels
        nvgFillColour(nvg, COLOUR_TEXT);
        nvgTextAlign(nvg, NVG_ALIGN_CC);
        nvgFontSize(nvg, gui->scale * 16);

        char label[24];
        cplug_getParameterName(gui->plugin, i, label, sizeof(label));
        nvgText(nvg, pt.x, gui_height * 0.75, label, NULL);

        cplug_parameterValueToString(gui->plugin, i, label, sizeof(label), value_d);
        nvgText(nvg, pt.x, gui_height * 0.25, label, NULL);

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
        nvgStrokeColour(nvg, COLOUR_BG_DARK);
        nvgLineCap(nvg, NVG_ROUND);
        nvgStroke(nvg);
    }

    // Wet/dry
    {
        imgui_rect rect;
        rect.x = gui_width * 0.885;
        rect.r = gui_width * 0.925;
        rect.y = gui_height * 0.5 - slider_radius;
        rect.b = gui_height * 0.5 + slider_radius;

        imgui_rect slider_dimensions  = rect;
        slider_dimensions.x          += 4; // padding
        slider_dimensions.y          += 4;
        slider_dimensions.r          -= 4;
        slider_dimensions.b          -= 4;
        float handle_width            = slider_dimensions.r - slider_dimensions.x;
        float drag_y                  = slider_dimensions.y + handle_width * 0.5f;
        float drag_b                  = slider_dimensions.b - handle_width * 0.5f;
        float drag_height             = drag_b - drag_y;

        uint32_t events  = imgui_get_events_rect(im, &rect);
        double   value_d = handle_param_events(gui, PARAM_WET, events, drag_height);

        // Draw BG
        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, rect.x, rect.y, rect.r - rect.x, rect.b - rect.y, 4);
        nvgFillColour(nvg, nvgHexColour(0x2C2F35FF));
        nvgFill(nvg);

        // Draw BG notches
        enum
        {
            NOTCH_COUNT = 16
        };
        const float y_inc   = drag_height / (float)NOTCH_COUNT;
        const float notch_x = slider_dimensions.x + handle_width * 0.25;
        const float notch_r = slider_dimensions.x + handle_width * 0.75;
        nvgBeginPath(nvg);
        for (int i = 0; i < NOTCH_COUNT; i++)
        {
            float y = roundf(drag_y + i * y_inc) + 0.5f;
            nvgMoveTo(nvg, notch_x, y);
            nvgLineTo(nvg, notch_r, y);
        }
        nvgStrokeColour(nvg, COLOUR_GREY_2);
        nvgStrokeWidth(nvg, 1);
        nvgStroke(nvg);

        // Draw handle
        float handle_cy = xm_lerpf(value_d, drag_b, drag_y);
        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, slider_dimensions.x, handle_cy - handle_width * 0.5f, handle_width, handle_width, 2);
        nvgFillColour(nvg, COLOUR_GREY_1);
        nvgFill(nvg);
        // Handle notch
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, notch_x, handle_cy);
        nvgLineTo(nvg, notch_r, handle_cy);
        nvgStrokeColour(nvg, nvgHexColour(0x2C2F35FF));
        nvgStroke(nvg);

        nvgFillColour(nvg, COLOUR_TEXT);
        char  label[24];
        float cx = (rect.x + rect.r) * 0.5f;
        cplug_getParameterName(gui->plugin, PARAM_WET, label, sizeof(label));
        nvgText(nvg, cx, gui_height * 0.75, label, NULL);

        cplug_parameterValueToString(gui->plugin, PARAM_WET, label, sizeof(label), value_d);
        nvgText(nvg, cx, gui_height * 0.25, label, NULL);
    }
    */

    /*
    const float peak_gain = gui->plugin->gui_output_peak_gain;
    if (peak_gain > 1)
    {
        nvgTextAlign(nvg, NVG_ALIGN_BR);
        nvgFillColour(nvg, nvgRGBAf(1, 0.1, 0.1, 1));
        float dB = xm_fast_gain_to_dB(peak_gain);
        char  label[48];
        snprintf(label, sizeof(label), "[WARNING] Auto hardclipper: ON. %.2fdB", dB);
        nvgText(nvg, gui_width - 20, gui_height - 20, label, NULL);
    }
    */

#ifdef CPLUG_BUILD_STANDALONE
    {
        Plugin* p = gui->plugin;
        // plot_expander(nvg, gui_width, gui_height);
        // plot_peak_detection(nvg, gui_width, gui_height);
        // plot_peak_distortion(nvg, im, gui_width, gui_height);
        // plot_peak_upwards_compression(nvg, im, gui_width, gui_height);
        float midi  = xt_atomic_load_f32(&p->gui_osc_midi);
        float phase = xt_atomic_load_f32(&p->gui_osc_phase);
        plot_oscilloscope(nvg, gui_width - 230, 10, 220, 180, p->sample_rate, midi, phase);

        imgui_rect  rect   = {gui_width - 220, 10, gui_width - 60, 25};
        const float offset = 10 + (rect.b - rect.y);
        im_slider(nvg, im, rect, &g_output_gain_dB, -24, 0, "%.2fdB", "Output");
        // rect.y += offset;
        // rect.b += offset;
        // im_slider(nvg, im, rect, &g_attack_ms, 0, 50, "%.2fms", "Attack");
        // rect.y += offset;
        // rect.b += offset;
        // im_slider(nvg, im, rect, &g_release_ms, 0, 50, "%.2fms", "Release");
        // rect.y += offset;
        // rect.b += offset;
        // im_slider(nvg, im, rect, &g_lp_Q, 0.01, 10, "%.3f", "LP Q");
        // rect.y += offset;
        // rect.b += offset;
        // im_slider(nvg, im, rect, &g_hp_Q, 0.05, 2, "%.3f", "HP Q");
    }
#endif

    // #ifndef NDEBUG
    {
        uint64_t frame_time_end         = xtime_now_ns();
        uint64_t frame_time_duration_ns = frame_time_end - frame_time_start;

        uint64_t max_frame_time_ns = 16666666; // 1/60th of a second, in nanoseconds

        // limit accuracy from nanoseconds to approximately microseconds
        uint64_t cpu_numerator   = frame_time_duration_ns >> 10; // fast integer divide by 1024
        uint64_t cpu_denominator = max_frame_time_ns >> 10;      // fast integer divide by 1024

        double cpu_amt       = (double)cpu_numerator / (double)cpu_denominator;
        double frame_time_ms = (double)cpu_numerator * 1024e-6; // correct for 1024 int 'division'
        double approx_fps    = 1000 / frame_time_ms;

        nvgFontSize(nvg, 12);
        NVGcolour footer_col = COLOUR_BG_LIGHT;
        footer_col.a         = 0.5f;
        nvgFillColour(nvg, footer_col);
        char text[64] = {0};
        int  len      = snprintf(
            text,
            sizeof(text),
            "CPU: %.2lf%% Time: %.3lfms. Max FPS: %.lf",
            (cpu_amt * 100),
            frame_time_ms,
            approx_fps);
        nvgTextAlign(nvg, NVG_ALIGN_CL);
        nvgText(nvg, 8, height_header * 0.5f + 4, text, NULL);

        // TODO: remove after release
        const char*    plugin_type_name = "";
        const uint32_t plugin_type      = gui->plugin->cplug_ctx->type;
        if (plugin_type == CPLUG_PLUGIN_IS_STANDALONE)
            plugin_type_name = "Standalone";
        if (plugin_type == CPLUG_PLUGIN_IS_VST3)
            plugin_type_name = "VST3";
        if (plugin_type == CPLUG_PLUGIN_IS_CLAP)
            plugin_type_name = "CLAP";
        if (plugin_type == CPLUG_PLUGIN_IS_AUV2)
            plugin_type_name = "Audio Unit";
#ifdef _WIN32
        const char* os_name = "Windows";
#elif __APPLE__
        const char* os_name = "macoS";
#endif
        snprintf(text, sizeof(text), "Scream %s | %s | %s", CPLUG_PLUGIN_VERSION, plugin_type_name, os_name);
        nvgText(nvg, 8, gui_height - height_footer * 0.5f - 4, text, NULL);
    }
    // #endif

    // End frame
    nvgEndFrame(gui->nvg);
    sg_end_pass(gui->sg);
    sg_commit(gui->sg);

    imgui_end_frame(&gui->imgui);
}
