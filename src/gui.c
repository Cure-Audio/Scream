
#include "common.h"

#include "gui.h"
#include "plugin.h"

#include "dsp.h"
#include "widgets.h"

#include <stdint.h>
#include <xhl/array.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/time.h>
#include <xhl/vector.h>

#include <cplug_extensions/window.h>
#include <nanovg2.h>
#include <stb_image.h>

#include <imgui.h>
#include <layout.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <knob.glsl.h>
#include <lfo.glsl.h>

#if defined(CPLUG_BUILD_STANDALONE)
// #define SYNTH_HUD
#endif

#if defined(SYNTH_HUD) && defined(CPLUG_BUILD_STANDALONE)
#include "libs/synth.h"
extern Synth g_synth;
#endif

void gui_handle_param_change(void* _gui, ParamID param_id)
{
    GUI* gui = _gui;
    if (param_id == PARAM_PATTERN_LFO_1 || param_id == PARAM_PATTERN_LFO_2)
        gui->imp.main_points_valid = false;
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

void* my_sg_allocator_alloc(size_t size, void* user_data)
{
    void* ptr = MY_MALLOC(size);
    return ptr;
}
void my_sg_allocator_free(void* ptr, void* user_data) { MY_FREE(ptr); }

void* pw_create_gui(void* _plugin, void* _pw)
{
    CPLUG_LOG_ASSERT(_plugin);
    CPLUG_LOG_ASSERT(_pw);

    LinkedArena* arena = linked_arena_create(1024 * 64);
    GUI*         gui   = linked_arena_alloc(arena, sizeof(*gui));
    gui->arena         = arena;
    gui->pw            = _pw;

    Plugin* p   = _plugin;
    gui->plugin = p;
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
        .allocator =
            {
                .alloc_fn  = my_sg_allocator_alloc,
                .free_fn   = my_sg_allocator_free,
                .user_data = gui,
            },
        .environment        = env,
        .logger             = my_sg_logger,
        .pipeline_pool_size = 512,
    });

    gui->nvg = nvgCreateContext(NVG_ANTIALIAS);
    xassert(gui->nvg);

    resources_init(&gui->resource_manager, 4096);

    // Load font
    {
        char path[1024];
        int  len = xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_APPDATA);

        const char* cat = XFILES_DIR_STR "Cure Audio" XFILES_DIR_STR "Scream" XFILES_DIR_STR "Tomorrow-SemiBold.ttf";
        snprintf(path + len, sizeof(path) - len, "%s", cat);

        const char* font_paths[] = {
            path,
#ifdef _WIN32
            "C:\\Windows\\Fonts\\arial.ttf",
#elif defined(__APPLE__)
            "/Library/Fonts/Arial Unicode.ttf",
#endif
        };

        int font_id  = -1;
        int font_idx = 0;

        do
        {
            font_id = nvgCreateFont(gui->nvg, "default", font_paths[font_idx]);
            if (font_id == -1)
            {
                println("[CRITICAL] Failed to open fallback font at path %s", path);
            }
            font_idx++;
        }
        while (font_id == -1 && font_idx < ARRLEN(font_paths));

        gui->font_id = font_id;
    }

    // Logo
    {
        void*  file_data     = NULL;
        size_t file_data_len = 0;
        bool   ok            = false;
        {
            char path[1024];
            xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_APPDATA);
            int         len = strlen(path);
            const char* cat = XFILES_DIR_STR "Cure Audio" XFILES_DIR_STR "Scream" XFILES_DIR_STR "cureaudio.png";
            snprintf(path + len, sizeof(path) - len, "%s", cat);
            xassert(xfiles_exists(path));
            ok = xfiles_read(path, &file_data, &file_data_len);
            xassert(ok);
        }
        if (ok)
        {
            int      x = 0, y = 0, comp = 0;
            stbi_uc* img_buf = stbi_load_from_memory(file_data, file_data_len, &x, &y, &comp, 4);
            xassert(img_buf);
            xassert(comp == 4);

            if (img_buf)
            {
                sg_image_desc img_desc = {
                    .width              = x,
                    .height             = y,
                    .pixel_format       = SG_PIXELFORMAT_RGBA8,
                    .data.mip_levels[0] = {
                        .ptr  = img_buf,
                        .size = x * y * comp,
                    }};
                gui->logo_id      = sg_make_image_with_mipmaps(&img_desc);
                gui->logo_texview = sg_make_view(&(sg_view_desc){.texture = gui->logo_id});
                stbi_image_free(img_buf);

                gui->logo_width  = x;
                gui->logo_height = y;
            }

            XFILES_FREE(file_data);
        }
    }

    ted_init(&gui->texteditor);

    gui->gui_create_time = gui->last_frame_start_time = gui->frame_start_time = gui->frame_end_time = xtime_now_ns();

    gui->imgui.frame.events = 1 << PW_EVENT_RESIZE;

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;

    // if (gui->logo_img_id)
    // {
    //     nvgDeleteImage(gui->nvg, gui->logo_img_id);
    //     gui->logo_img_id = 0;
    // }

    ted_deinit(&gui->texteditor);

    imp_deinit(&gui->imp);
    xarr_free(gui->lfo_ybuffer);
    xarr_free(gui->lfo_playhead_trail);

    sg_set_global(gui->sg);

    resources_deinit(&gui->resource_manager, gui->nvg);

    nvgDestroyContext(gui->nvg);
    sg_shutdown(gui->sg);

    gui->plugin->gui = NULL;

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

    // TODO: save last used width & height to settings file

    linked_arena_destroy(gui->arena);
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
        GUI* gui = info->constrain_size.gui;

        uint32_t width  = info->constrain_size.width;
        uint32_t height = info->constrain_size.height;

        uint32_t min_height = GUI_MIN_HEIGHT;
        if (gui->plugin->lfo_section_open)
        {
            min_height += gui->layout.top_content_height;
        }

        if (width < GUI_MIN_WIDTH)
            width = GUI_MIN_WIDTH;
        if (height < min_height)
            height = min_height;

        info->constrain_size.width  = width;
        info->constrain_size.height = height;
    }
}

bool pw_event(const PWEvent* event)
{
    GUI* gui = event->gui;

    if (!gui || !gui->plugin)
        return false;

    imgui_send_event(&gui->imgui, event);

    if (event->type == PW_EVENT_RESIZE)
    {
        // Retain size info for when the GUI is destroyed / reopened
        gui->plugin->width  = event->resize.width;
        gui->plugin->height = event->resize.height;

        gui->last_resize_time = xtime_now_ns();
    }

    if (gui->texteditor.active_param != -1)
    {
        TextEditor* ted = &gui->texteditor;

        if (event->type == PW_EVENT_MOUSE_LEFT_DOWN || event->type == PW_EVENT_MOUSE_RIGHT_DOWN ||
            event->type == PW_EVENT_MOUSE_MIDDLE_DOWN)
        {
            imgui_pt   pos = {event->mouse.x, event->mouse.y};
            imgui_rect rect;
            rect.x = ted->dimensions.x;
            rect.y = ted->dimensions.y;
            rect.r = ted->dimensions.x + ted->dimensions.width;
            rect.b = ted->dimensions.y + ted->dimensions.height;
            if (false == imgui_hittest_rect(pos, &rect))
            {
                ted->active_param = -1;
                pw_release_keyboard_focus(gui->pw);
            }
        }
        else if (event->type == PW_EVENT_TEXT)
        {
            xassert(event->text.codepoint != 0x7f);

            bool ret = false;

            if (event->text.codepoint > 31)
            {
                ret = true;
                ted_handle_text(ted, event->text.codepoint);
            }
            else if (event->text.codepoint == 13) // Enter (win) Return (mac)
            {
                ret = true;

                char text[256];
                ted_get_text(ted, text, sizeof(text));

                extern bool param_string_to_value(uint32_t param_id, const char* str, double* val);

                double val = 0;
                if (param_string_to_value(ted->active_param, text, &val))
                {
                    param_set(gui->plugin, ted->active_param, val);
                }

                ted_deactivate(ted);
            }
            else if (event->text.codepoint == 27) // ESC
            {
                // This is not how Chrome behaves, it just feels intuitive to me...
                ted_deactivate(ted);
                ret = true;
            }
            else if (event->text.codepoint == 9) // TAB
            {
                // Ignored
            }
            xassert(ted->ibeam_idx >= 0 && ted->ibeam_idx <= xarr_len(ted->codepoints));
            return ret;
        }
        else if (event->type == PW_EVENT_KEY_DOWN)
        {
            bool ret = ted_handle_key_down(ted, event);
            xassert(ted->ibeam_idx >= 0 && ted->ibeam_idx <= xarr_len(ted->codepoints));
            return ret;
        }
        else if (event->type == PW_EVENT_RESIZE)
        {
            ted_deactivate(ted);
        }
        else if (event->type == PW_EVENT_KEY_FOCUS_LOST)
        {
            ted_deactivate(ted);
        }
    }

    if (event->type == PW_EVENT_MOUSE_LEFT_DOWN)
    {
        if (imgui_hittest_rect((imgui_pt){event->mouse.x, event->mouse.y}, &gui->lfo_toggle_button))
        {
            LayoutMetrics* lm = &gui->layout;

            gui->plugin->lfo_section_open = !gui->plugin->lfo_section_open;

            int next_height    = lm->height;
            int content_height = lm->content_b - lm->content_y;
            if (gui->plugin->lfo_section_open)
                next_height += content_height;
            else
                next_height -= content_height - lm->top_content_height;
            xassert(next_height >= 0);

            gui->plugin->cplug_ctx->requestResize(gui->plugin->cplug_ctx, lm->width, next_height);
        }
    }

    return false;
}

double handle_param_events(GUI* gui, ParamID param_id, uint32_t events, float drag_range_px)
{
    imgui_context* im      = &gui->imgui;
    double         value_d = gui->plugin->main_params[param_id];
    float          value_f = value_d;

    if (events & IMGUI_EVENT_MOUSE_ENTER)
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);

    if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
    {
        if (im->left_click_counter >= 2)
        {
            im->left_click_counter = 0;

            value_d = value_f = cplug_getDefaultParameterValue(gui->plugin, param_id);
            param_set(gui->plugin, param_id, value_d);
        }
    }

    if (events & (IMGUI_EVENT_DRAG_BEGIN | IMGUI_EVENT_TOUCHPAD_BEGIN))
    {
        param_change_begin(gui->plugin, param_id);
    }
    if (events & IMGUI_EVENT_DRAG_MOVE)
    {
        float next_value = value_f;
        imgui_drag_value(im, &next_value, 0, 1, drag_range_px, IMGUI_DRAG_VERTICAL);
        value_d = value_f = next_value;
        param_change_update(gui->plugin, param_id, value_d);
    }
    if (events & IMGUI_EVENT_TOUCHPAD_MOVE)
    {
        float delta = im->frame.delta_touchpad.y / drag_range_px;
        if (im->frame.modifiers_touchpad & PW_MOD_INVERTED_SCROLL)
            delta = -delta;
        if (im->frame.modifiers_touchpad & PW_MOD_PLATFORM_KEY_CTRL)
            delta *= 0.1f;
        if (im->frame.modifiers_touchpad & PW_MOD_KEY_SHIFT)
            delta *= 0.1f;

        float next_value = xm_clampf(value_f + delta, 0, 1);

        bool changed = value_f != next_value;
        value_d = value_f = next_value;
        param_change_update(gui->plugin, param_id, value_d);
    }
    if (events & (IMGUI_EVENT_DRAG_END | IMGUI_EVENT_TOUCHPAD_END))
    {
        param_change_end(gui->plugin, param_id);
    }
    if (events & IMGUI_EVENT_MOUSE_WHEEL)
    {
        double delta = im->frame.delta_mouse_wheel * 0.1;
        if (im->frame.modifiders_mouse_wheel & PW_MOD_PLATFORM_KEY_CTRL)
            delta *= 0.1;
        if (im->frame.modifiders_mouse_wheel & PW_MOD_KEY_SHIFT)
            delta *= 0.1;

        double v  = gui->plugin->main_params[param_id];
        v        += delta;
        param_set(gui->plugin, param_id, v);
    }
    return value_d;
}

void do_knob_shader(void* uptr)
{
    GUI*           gui = uptr;
    LayoutMetrics* lm  = &gui->layout;

    sg_pipeline pip;
    bool ok = resource_get_pipeline(&gui->resource_manager, &pip, knob_shader_desc, RESOURCE_FLAG_NODESTROY_ENDFRAME);
    xassert(ok);
    if (ok)
    {
        sg_apply_pipeline(pip);

        float radius = lm->knob_outer_radius;
        xassert(radius != 0);

        for (int i = 0; i < 3; i++)
        {
            int cx_idx = 1 + i;
            xassert(cx_idx < ARRLEN(lm->param_positions_cx));
            float cx = lm->param_positions_cx[cx_idx];

            vs_knob_uniforms_t vs_uniforms = {
                .topleft     = {cx - radius, lm->cy_param - radius},
                .bottomright = {cx + radius, lm->cy_param + radius},
                .size        = {lm->width, lm->height},
            };

            fs_knob_uniforms_t fs_uniforms = {
                .u_colour = {0.7098039215686275, 0.7450980392156863, 0.7803921568627451, 1}};

            sg_apply_uniforms(UB_vs_knob_uniforms, &SG_RANGE(vs_uniforms));
            sg_apply_uniforms(UB_fs_knob_uniforms, &SG_RANGE(fs_uniforms));

            sg_draw(0, 6, 1);
        }

        xassert(sg_isvalid());
    }
}

// This should be a nvgImagePattern draw call but somehow I keep breaking nanovg...
void do_logo_shader(void* uptr)
{
    GUI*           gui = uptr;
    LayoutMetrics* lm  = &gui->layout;

    sg_pipeline pip;
    bool ok = resource_get_pipeline(&gui->resource_manager, &pip, logo_shader_desc, RESOURCE_FLAG_NODESTROY_ENDFRAME);
    xassert(ok);
    if (ok)
    {
        vs_logo_uniforms_t vs_uniforms = {
            .topleft     = {gui->logo_area.x, gui->logo_area.y},
            .bottomright = {gui->logo_area.r, gui->logo_area.b},
            .size        = {lm->width, lm->height},
        };

        const bool hover = gui->logo_events & IMGUI_EVENT_MOUSE_HOVER;

        fs_logo_uniforms_t fs_uniforms = {.u_col = {1, 1, 1, 1}};
        if (hover)
            memcpy(fs_uniforms.u_col, &C_WHITE, sizeof(C_WHITE));
        else
            memcpy(fs_uniforms.u_col, &C_TEXT, sizeof(C_TEXT));
        _Static_assert(sizeof(fs_uniforms.u_col) == sizeof(C_TEXT), "");

        sg_apply_pipeline(pip);
        sg_apply_bindings(&(sg_bindings){
            .views[VIEW_logo_tex]   = gui->logo_texview,
            .samplers[SMP_logo_smp] = gui->nvg->sampler_linear,
        });
        sg_apply_uniforms(UB_vs_logo_uniforms, &SG_RANGE(vs_uniforms));
        sg_apply_uniforms(UB_fs_logo_uniforms, &SG_RANGE(fs_uniforms));
        sg_draw(0, 6, 1);
    }
}

#ifdef _WIN32
// #include <Windows.h>
struct HWND__;
struct HINSTANCE__;
typedef struct HWND__*       HWND;
typedef struct HINSTANCE__*  HINSTANCE;
extern __declspec(dllimport) HINSTANCE __stdcall ShellExecuteA(
    _In_opt_ HWND        hwnd,
    _In_opt_ const char* lpOperation,
    _In_ const char*     lpFile,
    _In_opt_ const char* lpParameters,
    _In_opt_ const char* lpDirectory,
    _In_ int             nShowCmd);
#endif

int thread_run_hyperlink(void* data)
{
    const char* url = data;

#if defined(_WIN32)
    ShellExecuteA(NULL, "open", url, NULL, NULL, 1);
#elif defined(__APPLE__)
    char buf[256];
    snprintf(buf, sizeof(buf), "open %s", url);
    system(buf);
#endif

    return 0;
}

void open_hyperlink(const char* url)
{
    // non blocking
    xt_thread_ptr_t thread = xthread_create(thread_run_hyperlink, (void*)url, XTHREAD_STACK_SIZE_DEFAULT);
    xthread_detach(thread); // auto-destroy resources when thread ends
}

void pw_tick(void* _gui)
{
    GUI* gui = _gui;
    CPLUG_LOG_ASSERT(gui->plugin);
    CPLUG_LOG_ASSERT(gui->nvg);

    if (!gui || !gui->plugin)
        return;

    {
        Plugin*  p    = gui->plugin;
        uint32_t head = xt_atomic_load_u32(&p->queue_main_head) & EVENT_QUEUE_MASK;
        uint32_t tail = p->queue_main_tail;
        if (head != tail)
            gui->imgui.num_duplicate_backbuffers = 0;
        main_dequeue_events(gui->plugin);
    }

    bool click_curelogo = false;
    bool click_devtag   = false;

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

    if (!gui->nvg)
        return;

    LINKED_ARENA_LEAK_DETECT_BEGIN(gui->arena);

    // #ifndef NDEBUG

    const uint64_t last_frame_draw_time = gui->frame_end_time - gui->frame_start_time;
    // fprintf(stderr, "%llu\n", last_frame_draw_time);

    gui->last_frame_start_time = gui->frame_start_time;
    gui->frame_start_time      = xtime_now_ns();
    // #endif

    const uint64_t time_since_last_frame = gui->frame_start_time - gui->last_frame_start_time;

    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;
    LayoutMetrics* lm  = &gui->layout;

    sg_set_global(gui->sg);

    enum
    {
        HEIGHT_HEADER  = 32,
        HEIGHT_FOOTER  = 20,
        BORDER_PADDING = 8,

        CONTENT_HEIGHT = GUI_INIT_HEIGHT - HEIGHT_HEADER - HEIGHT_FOOTER - 2 * BORDER_PADDING,

        PARAM_MOD_AMOUNT_RADIUS     = 14,
        PARAMS_BOUNDARY_LEFT        = 32,
        VERTICAL_SLIDER_WIDTH       = 60,
        INPUT_WIDTH                 = 32,
        WET_WIDTH                   = 24,
        VERTICAL_SLIDER_HEIGHT      = 146,
        ROTARY_PARAM_OUTER_DIAMETER = 160,
        ROTARY_PARAM_INNER_DIAMETER = 80,

        _MINIMUM_WIDTH = PARAMS_BOUNDARY_LEFT * 2 + VERTICAL_SLIDER_WIDTH * 2 + ROTARY_PARAM_OUTER_DIAMETER * 3,
    };
    _Static_assert(_MINIMUM_WIDTH < GUI_MIN_WIDTH, "");

    // Recalculate layout metrics
    if (im->frame.events & ((1 << PW_EVENT_RESIZE) | (1 << PW_EVENT_DPI_CHANGED)))
    {
        lm->width  = gui->plugin->width;
        lm->height = gui->plugin->height;

        int init_height = GUI_INIT_HEIGHT;
        int top_height  = lm->height;
        if (gui->plugin->lfo_section_open)
        {
            init_height = HEIGHT_HEADER + HEIGHT_FOOTER + 2 * CONTENT_HEIGHT + 2 * BORDER_PADDING;
        }

        lm->scale_x = (float)lm->width / (float)GUI_INIT_WIDTH;
        lm->scale_y = (float)top_height / (float)init_height;

        const float dpi = pw_get_dpi(gui->pw);
#ifdef __APPLE__
        lm->content_scale    = dpi * 0.5;
        lm->devicePixelRatio = 2; // required for text to render properly...
#else
        lm->content_scale    = dpi;
        lm->devicePixelRatio = 1;
#endif
        nvg->devicePxRatio = lm->devicePixelRatio;

        lm->height_header = floorf(HEIGHT_HEADER * lm->content_scale);
        lm->height_footer = floorf(HEIGHT_FOOTER * lm->content_scale);

        lm->content_x = BORDER_PADDING;
        lm->content_r = lm->width - BORDER_PADDING;
        lm->content_y = lm->height_header + BORDER_PADDING;
        lm->content_b = lm->height - lm->height_footer - BORDER_PADDING;

        const bool lfo_open = gui->plugin->lfo_section_open;

        float content_height = lm->content_b - lm->content_y;
        if (lfo_open)
            lm->top_content_height = floorf(content_height * 0.5f);
        else
            lm->top_content_height = content_height;
        lm->top_content_bottom = lm->content_y + lm->top_content_height;

        const float param_boundary_left  = lm->scale_x * PARAMS_BOUNDARY_LEFT;
        const float param_boundary_right = lm->width - lm->scale_x * PARAMS_BOUNDARY_LEFT;
        const float PARAMS_WIDTH         = param_boundary_right - param_boundary_left;

        lm->param_scale = xm_maxf(1, xm_minf(lm->scale_x, lm->scale_y));

        const float veritcal_slider_width = snapf(VERTICAL_SLIDER_WIDTH * lm->param_scale, 2);
        const float knob_diameter         = snapf(ROTARY_PARAM_OUTER_DIAMETER * lm->param_scale, 2);

        {
            _Static_assert(
                ARRLEN(lm->param_positions_cx) == 6,
                "You've changed the number of params and we assumed there were only 5");
            imgui_rect rects[ARRLEN(lm->param_positions_cx)] = {0};

            rects[0].r = veritcal_slider_width;
            rects[1].r = knob_diameter;
            rects[2].r = knob_diameter;
            rects[3].r = knob_diameter;
            rects[4].r = veritcal_slider_width;
            rects[5].r = veritcal_slider_width;
            layout_horizontal_fill(
                rects,
                ARRLEN(rects),
                LAYOUT_SPACE_BETWEEN,
                &(imgui_rect){param_boundary_left, 0, param_boundary_right, 0});
            for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
                lm->param_positions_cx[i] = 0.5f * (rects[i].x + rects[i].r);
            xassert(lm->param_positions_cx[5] < param_boundary_right);
        }

        lm->knob_outer_radius = knob_diameter * 0.5f;
        lm->knob_inner_radius = lm->param_scale * 0.5f * ROTARY_PARAM_INNER_DIAMETER;

        {
            imgui_rect rects[4] = {0};
            rects[0].b          = lm->content_scale * 20;                          // param value
            rects[1].b          = lm->knob_outer_radius * 2;                       // param
            rects[2].b          = lm->content_scale * PARAM_MOD_AMOUNT_RADIUS * 2; // mod amount
            rects[3].b          = lm->content_scale * 20;                          // Param title

            imgui_rect box  = {0, lm->content_y, 0, lm->content_y + lm->top_content_height};
            box.y          += 36 + lm->content_scale * 10;
            box.b          -= 40;
            layout_vertical_fill(rects, ARRLEN(rects), LAYOUT_SPACE_BETWEEN, &box);

            lm->cy_param_value      = (rects[0].y + rects[0].b) * 0.5f;
            lm->cy_param            = (rects[1].y + rects[1].b) * 0.5f;
            lm->cy_param_mod_amount = (rects[2].y + rects[2].b) * 0.5f;
            lm->cy_param_title      = (rects[3].y + rects[3].b) * 0.5f;
        }

        size_t lfo_buffer_cap = xarr_cap(gui->lfo_ybuffer);
        if (lm->width > lfo_buffer_cap)
        {
            lfo_buffer_cap = lm->width * 2;
            xarr_setlen(gui->lfo_ybuffer, lfo_buffer_cap);
            xarr_setlen(gui->lfo_playhead_trail, lfo_buffer_cap);

            memset(gui->lfo_playhead_trail, 0, sizeof(*gui->lfo_playhead_trail) * lfo_buffer_cap);

            if (gui->lfo_ybuffer_view.id)
                sg_destroy_view(gui->lfo_ybuffer_view);
            if (gui->lfo_playhead_trail_view.id)
                sg_destroy_view(gui->lfo_playhead_trail_view);

            if (gui->lfo_ybuffer_obj.id)
                sg_destroy_buffer(gui->lfo_ybuffer_obj);
            if (gui->lfo_playhead_trail_obj.id)
                sg_destroy_buffer(gui->lfo_playhead_trail_obj);

            gui->lfo_ybuffer_obj         = sg_make_buffer(&(sg_buffer_desc){
                        .usage.storage_buffer = true,
                        .usage.stream_update  = true,
                        .size                 = lfo_buffer_cap * sizeof(*gui->lfo_ybuffer),
                        .label                = NVG_LABEL("lfo_ybuffer"),
            });
            gui->lfo_playhead_trail_obj  = sg_make_buffer(&(sg_buffer_desc){
                 .usage.storage_buffer = true,
                 .usage.stream_update  = true,
                 .size                 = lfo_buffer_cap * sizeof(*gui->lfo_playhead_trail),
                 .label                = NVG_LABEL("lfo_playhead_trail"),
            });
            gui->lfo_ybuffer_view        = sg_make_view(&(sg_view_desc){.storage_buffer = gui->lfo_ybuffer_obj});
            gui->lfo_playhead_trail_view = sg_make_view(&(sg_view_desc){.storage_buffer = gui->lfo_playhead_trail_obj});
        }
        const int lfo_idx        = gui->plugin->selected_lfo_idx;
        float     playhead       = (float)gui->plugin->lfos[lfo_idx].phase;
        lm->current_lfo_playhead = lm->last_lfo_playhead = playhead;

        imgui_rect lfo_btn;
        lfo_btn.x              = (lm->width / 2) - 20;
        lfo_btn.y              = lm->top_content_bottom - 20;
        lfo_btn.r              = lfo_btn.x + 40;
        lfo_btn.b              = lm->top_content_bottom;
        gui->lfo_toggle_button = lfo_btn;

        gui->imp.points_valid = false;
    }

    // Note: The 'id<CAMetalDrawable>' pointer can change every frame.
    // New calls to get this pointer must be issued every frame
    gui->swapchain = (sg_swapchain){
        .width        = gui->layout.width,
        .height       = gui->layout.height,
        .sample_count = 1,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,

#if __APPLE__
        .metal.current_drawable      = pw_get_metal_drawable(gui->pw),
        .metal.depth_stencil_texture = pw_get_metal_depth_stencil_texture(gui->pw),
#endif
#if _WIN32
        .d3d11.render_view        = pw_get_dx11_render_target_view(gui->pw),
        .d3d11.depth_stencil_view = pw_get_dx11_depth_stencil_view(gui->pw),
#endif
    };

    imgui_begin_frame(im);
    nvgBeginFrame(nvg, lm->devicePixelRatio);

    SGNVGframebuffer fb_main = {0};
    bool ok = resource_get_framebuffer(&gui->resource_manager, 'main', &fb_main, nvg, lm->width, lm->height, 0);
    xassert(ok);

    snvg_command_begin_pass(
        nvg,
        &(sg_pass){
            .action                    = {.colors[0] = {.load_action = SG_LOADACTION_DONTCARE}},
            .attachments.colors[0]     = fb_main.img_colview,
            .attachments.depth_stencil = fb_main.depth_view,
            .label                     = NVG_LABEL("main_framebuffer"),
        },
        0,
        0,
        fb_main.width,
        fb_main.height,
        NVG_LABEL("main framebuffer begin pass"));
    snvg_command_draw_nvg(nvg, NVG_LABEL("main framebuffer"));

// Synth HUD
#ifdef SYNTH_HUD
    SNVGcallState calls_synth_hud = {0};
    {
        SNVGcallState calls_main    = snvg_calls_pop(nvg);
        const float   slider_height = 20;
        imgui_rect    rect;
        rect.x = 40;
        rect.y = 40;
        rect.r = 240;
        rect.b = 40 + slider_height;
        im_slider(nvg, im, rect, &g_synth.params[kSynthVolume], 0, 1, "%.3f%%", "Vol");

        calls_synth_hud = snvg_calls_pop(nvg);

        snvg_calls_set(nvg, &calls_main);
    }
#endif // SYNTH_HUD

    // Background
    {
        nvgBeginPath(nvg);
        nvgRect(nvg, 0, 0, lm->width, lm->height);
        static const NVGcolour stop0 = nvgHexColour(0x151B33FF);
        static const NVGcolour stop1 = nvgHexColour(0x090E1FFF);
        nvgSetPaint(nvg, nvgLinearGradient(nvg, 0, 0, 0, lm->height, stop0, stop1));
        nvgFill(nvg);
    }

    // Header
    {
        nvgSetFontSize(nvg, lm->content_scale * 24);
        nvgSetColour(nvg, C_BG_LIGHT);
        nvgSetTextAlign(nvg, NVG_ALIGN_CC);
        nvgText(nvg, lm->width * 0.5f, lm->height_header * 0.5f + 4, "SCREAM", NULL);

        // Logo
        snvg_command_custom(nvg, gui, do_logo_shader, NVG_LABEL("Logo shader"));
        snvg_command_draw_nvg(nvg, NVG_LABEL("main framebuffer 2"));

        float x, y, w, h, img_scale;
        y                = 4;
        h                = lm->height_header - y;
        img_scale        = h / (float)gui->logo_height;
        w                = (float)gui->logo_width * img_scale;
        x                = lm->width - 16 - w;
        gui->logo_area   = (imgui_rect){x, y, x + w, y + h};
        gui->logo_events = imgui_get_events_rect(im, 'logo', &gui->logo_area);
        if (gui->logo_events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
        if (gui->logo_events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            click_curelogo = true;
        // nvgBeginPath(nvg);
        // nvgRect(nvg, x, y, w, h);
        // nvgSetPaint(nvg, nvgImagePattern(nvg, x, y, w, h, 0, gui->logo_texview, 1, nvg->sampler_linear));
        // nvgFill(nvg);
    }

    // Main content background
    {
        float height = lm->content_b - lm->content_y;
        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, 8, lm->content_y, lm->width - 16, height, 8);
        nvgSetColour(nvg, C_BG_LIGHT);
        nvgFill(nvg);

        // Inner shadows
        const float blur_radius = 8;
        float       grad_x      = lm->content_x - blur_radius * 0.5f;
        float       grad_r      = lm->content_r + blur_radius * 0.5f;
        float       grad_w      = grad_r - grad_x;

        NVGcolour icol  = (NVGcolour){1, 1, 1, 0};
        NVGcolour ocol  = (NVGcolour){1, 1, 1, 0.75};
        NVGpaint  paint = nvgBoxGradient(nvg, grad_x, lm->content_y, grad_w, height, 16, blur_radius, icol, ocol);

        // Top inner shadow (light)
        nvgBeginPath(nvg);
        nvgRoundedRectVarying(nvg, 8, lm->content_y, lm->width - 16, blur_radius * 2, 8, 8, 0, 0);
        nvgSetPaint(nvg, paint);
        nvgFill(nvg);

        // Bottom inner shadow (dark)
        paint.innerColour = (NVGcolour){0, 0, 0, 0};
        paint.outerColour = (NVGcolour){0, 0, 0, 0.75f};
        nvgBeginPath(nvg);
        nvgRoundedRectVarying(nvg, 8, lm->content_b - blur_radius * 2, lm->width - 16, blur_radius * 2, 0, 0, 8, 8);
        nvgSetPaint(nvg, paint);
        nvgFill(nvg);

        // Dots
        const float DOT_DIAMETER = 6;
        const float DOT_RADIUS   = DOT_DIAMETER / 2;
        const float DOT_PADDING  = 6;

        const float left_dot_cx  = roundf(lm->content_x + 8 + DOT_RADIUS);
        const float right_dot_cx = roundf(lm->content_r - 8 - DOT_RADIUS);
        const float top_dot_cy   = roundf(lm->content_y + 8 + DOT_RADIUS);
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
        nvgSetColour(nvg, (NVGcolour){1, 1, 1, 1});
        nvgFill(nvg);

        nvgBeginPath(nvg);
        for (int i = 0; i < ARRLEN(points); i++)
        {
            nvgCircle(nvg, points[i].x, points[i].y - 1, DOT_RADIUS);
        }
        nvgSetColour(nvg, nvgHexColour(0x111629FF));
        nvgFill(nvg);
    }

    // Params
    {
        static const ParamID param_ids[] =
            {PARAM_INPUT_GAIN, PARAM_CUTOFF, PARAM_SCREAM, PARAM_RESONANCE, PARAM_WET, PARAM_OUTPUT_GAIN};
        _Static_assert(ARRLEN(param_ids) == ARRLEN(lm->param_positions_cx), "");

        // Param labels
        nvgSetColour(nvg, C_TEXT);
        const float param_font_size = 14 * lm->content_scale;
        nvgSetFontSize(nvg, param_font_size);

        static const char* NAMES[] = {"INPUT", "CUTOFF", "SCREAM", "RESONANCE", "WET", "OUTPUT"};
        _Static_assert(ARRLEN(NAMES) == ARRLEN(lm->param_positions_cx));
        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            const ParamID param_id = param_ids[i];
            const float   param_cx = lm->param_positions_cx[i];

            nvgSetTextAlign(nvg, NVG_ALIGN_CC);
            nvgSetColour(nvg, C_TEXT);
            nvgText(nvg, param_cx, lm->cy_param_title, NAMES[i], NULL);

            imgui_rect rect;
            rect.x = param_cx - 50;
            rect.r = param_cx + 50;
            rect.y = lm->cy_param_value - lm->content_scale * 10;
            rect.b = rect.y + lm->content_scale * 20;

            unsigned wid    = 'txt' + i;
            uint32_t events = imgui_get_events_rect(im, wid, &rect);

            if (events & IMGUI_EVENT_MOUSE_ENTER)
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_IBEAM);

            // Handle events
            if (gui->texteditor.active_param == param_id)
            {
                TextEditor* ted = &gui->texteditor;
                // Text editor stuff
                if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
                {
                    ted_handle_mouse_down(ted);
                }
                if (events & IMGUI_EVENT_DRAG_MOVE)
                {
                    ted_handle_mouse_drag(ted);
                }
                xassert(ted->ibeam_idx >= 0 && ted->ibeam_idx <= xarr_len(ted->codepoints));
            }
            else
            {
                // Not text editor
                if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
                {
                    xvec4f dimensions = {rect.x, rect.y, rect.r - rect.x, rect.b - rect.y};
                    xvec2f pos        = {im->pos_mouse_down.x, im->pos_mouse_down.y};
                    ted_activate(&gui->texteditor, dimensions, pos, param_font_size, param_id);
                }
            }

            // Draw
            if (gui->texteditor.active_param == param_id)
            {
                ted_draw(&gui->texteditor);
            }
            else
            {
                char   label[24];
                double value = main_get_param(gui->plugin, param_id);
                cplug_parameterValueToString(gui->plugin, param_id, label, sizeof(label), value);

                nvgSetColour(nvg, C_TEXT);
                nvgSetTextAlign(nvg, NVG_ALIGN_CC);
                nvgText(nvg, param_cx, lm->cy_param_value, label, NULL);
            }
        }

        // Mod amount controls
        const float mod_handle_offset = lm->knob_outer_radius + 10 * lm->scale_y;

        unsigned    mod_amt_events[ARRLEN(lm->param_positions_cx)][2] = {0};
        const float mod_amt_radius                                    = lm->param_scale * PARAM_MOD_AMOUNT_RADIUS;
        const float mod_amt_gap                                       = lm->param_scale * 10;
        const float mod_amt_cx_delta                                  = 0.5f * mod_amt_gap + mod_amt_radius;

        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            float         param_cx = lm->param_positions_cx[i];
            const ParamID param_id = param_ids[i];

            const float mod_amt_cx[2] = {
                lm->param_positions_cx[i] - mod_amt_cx_delta,
                lm->param_positions_cx[i] + mod_amt_cx_delta,
            };

            const xvec2f modamt = gui->plugin->lfo_mod_amounts[param_id];

            for (int j = 0; j < ARRLEN(mod_amt_cx); j++)
            {
                const unsigned uid    = 'pmod' + (i * ARRLEN(mod_amt_cx)) + j;
                const imgui_pt c      = {mod_amt_cx[j], lm->cy_param_mod_amount};
                const unsigned events = imgui_get_events_circle(im, uid, c, mod_amt_radius);

                xassert(param_id < ARRLEN(mod_amt_events));
                xassert(j < ARRLEN(mod_amt_events[0]));
                mod_amt_events[param_id][j] = events;

                float* pValue = &gui->plugin->lfo_mod_amounts[param_id].data[j];

                if (events & IMGUI_EVENT_MOUSE_ENTER)
                    pw_set_mouse_cursor(gui->pw, PW_CURSOR_RESIZE_NS);
                if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
                {
                    if (im->left_click_counter == 2)
                    {
                        *pValue = 0;
                    }
                }
                if (events & IMGUI_EVENT_DRAG_MOVE)
                {
                    imgui_drag_value(im, pValue, -1, 1, 300, IMGUI_DRAG_VERTICAL);
                }
                if (events & IMGUI_EVENT_TOUCHPAD_MOVE)
                {
                    float delta = im->frame.delta_touchpad.y / 300;
                    if (im->frame.modifiers_touchpad & PW_MOD_INVERTED_SCROLL)
                        delta = -delta;
                    if (im->frame.modifiers_touchpad & PW_MOD_PLATFORM_KEY_CTRL)
                        delta *= 0.1f;
                    if (im->frame.modifiers_touchpad & PW_MOD_KEY_SHIFT)
                        delta *= 0.1f;

                    *pValue = xm_clampf(*pValue + delta, -1, 1);
                }
                if (events & IMGUI_EVENT_MOUSE_WHEEL)
                {
                    double delta = im->frame.delta_mouse_wheel * 0.1;
                    if (im->frame.modifiders_mouse_wheel & PW_MOD_PLATFORM_KEY_CTRL)
                        delta *= 0.1;
                    if (im->frame.modifiders_mouse_wheel & PW_MOD_KEY_SHIFT)
                        delta *= 0.1;

                    *pValue = xm_clampf(*pValue + delta, -1, 1);
                }

                char label  = '1';
                label      += j;
                nvgSetTextAlign(nvg, NVG_ALIGN_CC);
                nvgSetColour(nvg, C_TEXT);
                nvgText(nvg, c.x, c.y + 1, &label, &label + 1);

                nvgBeginPath(nvg);
                nvgCircle(nvg, c.x, c.y, mod_amt_radius - 2);
                nvgSetColour(nvg, C_GREY_1);
                nvgStroke(nvg, 4);

                if (fabsf(modamt.data[j]) != 0)
                {
                    float start_radians = -XM_HALF_PIf;
                    float end_radians   = -XM_HALF_PIf + XM_TAUf * modamt.data[j];
                    nvgBeginPath(nvg);
                    nvgArc(
                        nvg,
                        c.x,
                        c.y,
                        mod_amt_radius - 2,
                        xm_minf(start_radians, end_radians),
                        xm_maxf(start_radians, end_radians),
                        NVG_CW);
                    nvgSetColour(nvg, C_DARK_BLUE);
                    nvgStroke(nvg, 4);
                }
            }
        }

        // Parameter control
        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            const ParamID  param_id = param_ids[i];
            const float    param_cx = lm->param_positions_cx[i];
            const unsigned wid      = 'prm' + i;

            const xvec2f modamts = gui->plugin->lfo_mod_amounts[param_id];
            const xvec2f lfo_amt = gui->plugin->last_lfo_amount;

            switch (param_id)
            {
            case PARAM_CUTOFF:
            case PARAM_SCREAM:
            case PARAM_RESONANCE:
            {
                enum
                {
                    RADIUS_INNER           = 80 / 2,
                    RADIUS_OUTER           = 88 / 2,
                    RADIUS_INLET           = 108 / 2,
                    RADIUS_INNER_VALUE_ARC = 120 / 2,
                    RADIUS_OUTER_VALUE_ARC = 136 / 2,
                };
                imgui_pt pt = {lm->param_positions_cx[i], lm->cy_param};

                uint32_t events  = imgui_get_events_circle(im, wid, pt, lm->knob_outer_radius);
                double   value_d = handle_param_events(gui, param_id, events, 300);

                // Inlet
                {
                    // 3 stop radial gradient
                    const float r100 = roundf(RADIUS_INLET * lm->param_scale);
                    const float r90  = roundf(r100 * 0.9f);
                    const float r80  = roundf(r100 * 0.8f);

                    static const NVGcolour stop100 = nvgHexColour(0x40454AFF);
                    static const NVGcolour stop90  = nvgHexColour(0xB7C7D7FF);
                    static const NVGcolour stop80  = nvgHexColour(0xC9D3DDFF);

                    NVGpaint grad_100_90 = nvgRadialGradient(nvg, pt.x, pt.y, r90, r100, stop90, stop100);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, r100);
                    nvgSetPaint(nvg, grad_100_90);
                    nvgFill(nvg);

                    NVGpaint grad_90_80 = nvgRadialGradient(nvg, pt.x, pt.y, r80, r90, stop80, stop90);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, r90);
                    nvgSetPaint(nvg, grad_90_80);
                    nvgFill(nvg);
                }

                // Outer knob
                const float radius_outer = roundf(RADIUS_OUTER * lm->param_scale);
                const float outer_y      = pt.y - radius_outer;
                const float outer_h      = radius_outer * 2;

                // Outer knob outer shadow
                {
                    const float y            = pt.y + 16 * lm->param_scale;
                    const float inner_radius = radius_outer - 8 * lm->param_scale;
                    const float outer_radius = radius_outer + 4 * lm->param_scale;

                    static const NVGcolour icol = {0, 0, 0, 0.25f};
                    static const NVGcolour ocol = {0, 0, 0, 0};
                    NVGpaint grad = nvgRadialGradient(nvg, pt.x, y, inner_radius, outer_radius, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, y, outer_radius);
                    nvgSetPaint(nvg, grad);
                    nvgFill(nvg);
                }

                {
                    static const NVGcolour stop0 = nvgHexColour(0xD4DFEAFF);
                    static const NVGcolour stop1 = nvgHexColour(0xB5BFC8FF);

                    const float top         = outer_y + outer_h * 0.13f;
                    const float bottom      = outer_y + outer_h * 0.84f;
                    NVGpaint    out_lingrad = nvgLinearGradient(nvg, 0, top, 0, bottom, stop0, stop1);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, radius_outer);
                    nvgSetPaint(nvg, out_lingrad);
                    nvgFill(nvg);
                }

                // Outer knob inner shadow
                {
                    static const NVGcolour icol = {1, 1, 1, 0};
                    static const NVGcolour ocol = {1, 1, 1, 0.8};
                    NVGpaint grad = nvgRadialGradient(nvg, pt.x, pt.y + 1, radius_outer - 1, radius_outer, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, radius_outer);
                    nvgSetPaint(nvg, grad);
                    nvgFill(nvg);
                }

                // Inner
                const float radius_inner = RADIUS_INNER * lm->param_scale;
                const float inner_y      = pt.y - radius_inner;
                const float inner_h      = radius_inner * 2;
                const float inner_s0_y   = inner_y + inner_h * 0.16f;
                const float inner_s1_y   = inner_y + inner_h * 0.87f;

                static const NVGcolour in_lin_s0 = nvgHexColour(0xB5BFC8FF);
                static const NVGcolour in_lin_s1 = nvgHexColour(0xD4DFEAFF);
                NVGpaint inner_grad = nvgLinearGradient(nvg, 0, inner_s0_y, 0, inner_s1_y, in_lin_s0, in_lin_s1);

                nvgBeginPath(nvg);
                nvgCircle(nvg, pt.x, pt.y, radius_inner);
                nvgSetPaint(nvg, inner_grad);
                nvgFill(nvg);

// Slider Tick/Notch
// Angle radians
// 120deg
#define SLIDER_START_RAD 2.0943951023931953f
// 120deg + 360deg * 0.8333
#define SLIDER_END_RAD 7.330173418865945f
// end - start
#define SLIDER_LENGTH_RAD 5.23577831647275f

                const float value_norm  = cplug_normaliseParameterValue(gui->plugin, i, value_d);
                const float angle_value = SLIDER_START_RAD + value_norm * SLIDER_LENGTH_RAD;

                const float angle_x = cosf(angle_value);
                const float angle_y = sinf(angle_value);

                float tick_radius_start = radius_inner - 10 * lm->param_scale;
                float tick_radius_end   = radius_inner * 0.4f;

                const imgui_pt pt1      = {pt.x + tick_radius_start * angle_x, pt.y + tick_radius_start * angle_y};
                const imgui_pt pt2      = {pt.x + tick_radius_end * angle_x, pt.y + tick_radius_end * angle_y};
                float          stroke_w = 6 * lm->param_scale;
                nvgSetLineCap(nvg, NVG_ROUND);

                nvgBeginPath(nvg); // Skeumorphic inner shadow
                nvgMoveTo(nvg, pt1.x, pt1.y);
                nvgLineTo(nvg, pt2.x, pt2.y);
                nvgSetColour(nvg, (NVGcolour){1, 1, 1, 1});
                nvgStroke(nvg, stroke_w);

                nvgBeginPath(nvg);
                nvgMoveTo(nvg, pt1.x, pt1.y - 1);
                nvgLineTo(nvg, pt2.x, pt2.y - 1);
                nvgSetColour(nvg, nvgHexColour(0x242E56FF));
                nvgStroke(nvg, stroke_w);

                nvgSetLineCap(nvg, NVG_BUTT);

                // Value arc
                float arc_radius[] = {
                    roundf(RADIUS_INNER_VALUE_ARC * lm->param_scale),
                    roundf(RADIUS_OUTER_VALUE_ARC * lm->param_scale),
                };

                stroke_w = roundf(lm->param_scale * 4);
                for (int lfo_idx = 0; lfo_idx < 2; lfo_idx++)
                {
                    const bool is_modulated = fabsf(modamts.data[lfo_idx]) != 0;

                    nvgBeginPath(nvg);
                    nvgArc(nvg, pt.x, pt.y, arc_radius[lfo_idx], SLIDER_START_RAD, SLIDER_END_RAD, NVG_CW);
                    nvgSetColour(nvg, C_GREY_1);
                    nvgStroke(nvg, stroke_w);

                    if (is_modulated)
                    {
                        xassert(param_id < ARRLEN(mod_amt_events));
                        xassert(lfo_idx < ARRLEN(mod_amt_events[0]));
                        unsigned mod_amt_event = mod_amt_events[param_id][lfo_idx];
                        if (mod_amt_event & IMGUI_EVENT_MOUSE_HOVER)
                        {
                            float mod_value_norm        = value_norm + modamts.data[lfo_idx];
                            mod_value_norm              = xm_clampf(mod_value_norm, 0, 1);
                            const float mod_angle_value = SLIDER_START_RAD + mod_value_norm * SLIDER_LENGTH_RAD;

                            float angle_start = xm_minf(angle_value, mod_angle_value);
                            float angle_end   = xm_maxf(angle_value, mod_angle_value);

                            nvgBeginPath(nvg);
                            nvgArc(nvg, pt.x, pt.y, arc_radius[lfo_idx], angle_start, angle_end, NVG_CW);
                            nvgSetColour(nvg, C_DARK_BLUE);
                            nvgStroke(nvg, stroke_w * 1.3);
                        }
                        float mod_value_norm        = value_norm + modamts.data[lfo_idx] * lfo_amt.data[lfo_idx];
                        mod_value_norm              = xm_clampf(mod_value_norm, 0, 1);
                        const float mod_angle_value = SLIDER_START_RAD + mod_value_norm * SLIDER_LENGTH_RAD;

                        float angle_start = xm_minf(angle_value, mod_angle_value);
                        float angle_end   = xm_maxf(angle_value, mod_angle_value);

                        nvgBeginPath(nvg);
                        nvgArc(nvg, pt.x, pt.y, arc_radius[lfo_idx], angle_start, angle_end, NVG_CW);
                        nvgSetColour(nvg, C_DARK_BLUE);
                        nvgStroke(nvg, stroke_w * 1.3);
                    }
                }

                if (modamts.u64 == 0)
                {
                    nvgBeginPath(nvg);
                    nvgArc(nvg, pt.x, pt.y, arc_radius[0], SLIDER_START_RAD, angle_value, NVG_CW);
                    nvgSetColour(nvg, C_GREY_2);
                    nvgStroke(nvg, stroke_w);
                }
                break;
            }
            case PARAM_INPUT_GAIN:
            case PARAM_WET:
            case PARAM_OUTPUT_GAIN:
            {
                float       param_width  = param_id == PARAM_INPUT_GAIN ? INPUT_WIDTH : WET_WIDTH;
                const float meter_width  = snapf(param_width * lm->param_scale, 2);
                const float meter_height = snapf(VERTICAL_SLIDER_HEIGHT * lm->param_scale, 2);

                imgui_rect rect;

                rect.x = roundf(param_cx - meter_width * 0.5);
                rect.r = roundf(param_cx + meter_width * 0.5);
                rect.y = roundf(lm->cy_param - meter_height * 0.5f);
                rect.b = roundf(lm->cy_param + meter_height * 0.5f);

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
                    nvgSetPaint(nvg, paint);
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
                    nvgSetPaint(nvg, paint);
                    nvgFill(nvg);
                }

                if (param_id == PARAM_INPUT_GAIN)
                {
                    float       rect_r     = rect.r;
                    const float icon_width = 10;
                    float       icon_r     = rect.r + icon_width + 4;
                    rect.r                 = icon_r;
                    uint32_t events        = imgui_get_events_rect(im, wid, &rect);
                    rect.r                 = rect_r;

                    static const char* input_gain_description =
                        "Changing the input gain drastically changes the sound. It's recommended your try and keep the "
                        "input gain close to 0dB.\n\n"
                        "If your input is detected to be within a desirable range, the peak meter will show text in "
                        "green. If the input too loud or too quiet, then the text will be red. As always, trust your "
                        "ears first.";

                    tooltip_handle_events(&gui->tooltip, rect, input_gain_description, gui->frame_start_time, events);

                    double value_d = handle_param_events(gui, PARAM_INPUT_GAIN, events, rect.b - rect.y);

                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, rect.x, rect.y, rect.r - rect.x, rect.b - rect.y, 4 * lm->param_scale);
                    nvgSetColour(nvg, nvgHexColour(0x2C2F35FF));

                    static const NVGcolour bg_grad_stop0 = nvgHexColour(0x2C2F35FF);
                    static const NVGcolour bg_grad_stop1 = nvgHexColour(0x585E6AFF);
                    const NVGpaint         bg_paint =
                        nvgLinearGradient(nvg, 0, rect.y, 0, rect.b, bg_grad_stop0, bg_grad_stop1);
                    nvgSetPaint(nvg, bg_paint);
                    nvgFill(nvg);

                    const float mod_amt_padding     = 2;
                    const float mod_amt_strokewidth = 3;

                    nvgBeginPath(nvg);
                    for (int j = 0; j < ARRLEN(modamts.data); j++)
                    {
                        if (fabsf(modamts.data[j]) != 0)
                        {
                            float mod_amt = modamts.data[j];

                            unsigned mod_amt_event = mod_amt_events[param_id][j];
                            if (!(mod_amt_event & IMGUI_EVENT_MOUSE_HOVER))
                            {
                                mod_amt *= lfo_amt.data[j];
                            }
                            float mod_end_value = xm_clampf(mod_amt + value_d, 0, 1);
                            float mod_start_y   = xm_lerpf(value_d, rect.b, rect.y);
                            float mod_end_y     = xm_lerpf(mod_end_value, rect.b, rect.y);

                            float y_top = xm_minf(mod_end_y, mod_start_y);
                            float y_bot = xm_maxf(mod_end_y, mod_start_y);
                            if (y_bot <= y_top)
                                y_bot += 1;

                            float l, r;
                            if (j == 0)
                            {
                                l = rect.x + mod_amt_padding;
                                r = rect.x + mod_amt_padding + mod_amt_strokewidth;
                            }
                            else
                            {
                                l = rect.r - mod_amt_padding - mod_amt_strokewidth;
                                r = rect.r - mod_amt_padding;
                            }

                            nvgRect2(nvg, l, y_top, r, y_bot);
                        }
                    }
                    if (modamts.u64)
                    {
                        nvgSetColour(nvg, C_DARK_BLUE);
                        nvgFill(nvg);
                    }

                    xvec2f peaks;
                    peaks.u64 = xt_atomic_load_u64(&gui->plugin->gui_input_peak_gain);

                    float ch_w = roundf(meter_width * (7.0f / 32.0f));

                    const float peak_label_height = 16 * lm->content_scale;

                    const float ch_y    = rect.y + peak_label_height;
                    const float ch_b    = rect.b - 4;
                    const float ch_h    = ch_b - ch_y;
                    const float ch_x[2] = {
                        roundf(rect.x + meter_width * (8.0f / 32.0f)),
                        roundf(rect.r - meter_width * (8.0f / 32.0f)) - ch_w,
                    };

                    // Value icon
                    {
                        float icon_x = icon_r - icon_width;

                        float shadow_radius = 8;

                        // ch_y
                        // ch_h

                        float icon_y = xm_lerpf(value_d, ch_b, ch_y);

                        static const NVGcolour icol      = {0, 0, 0, 0.3};
                        static const NVGcolour ocol      = {0, 0, 0, 0};
                        float                  shadow_cx = icon_r - icon_width * 0.3f;
                        NVGpaint paint = nvgRadialGradient(nvg, shadow_cx, icon_y + 2, 0, shadow_radius, icol, ocol);

                        nvgBeginPath(nvg);
                        nvgCircle(nvg, shadow_cx, icon_y + 2, shadow_radius);
                        nvgSetPaint(nvg, paint);
                        nvgFill(nvg);

                        nvgBeginPath(nvg);
                        nvgMoveTo(nvg, icon_x, icon_y);
                        nvgLineTo(nvg, icon_r, icon_y - 8);
                        nvgLineTo(nvg, icon_r, icon_y + 8);
                        nvgClosePath(nvg);
                        nvgSetColour(nvg, C_GREY_2);
                        nvgFill(nvg);
                    }

                    // Background
                    nvgBeginPath(nvg);
                    for (int ch = 0; ch < 2; ch++)
                    {
                        nvgRoundedRect(nvg, ch_x[ch], ch_y, ch_w, ch_h, 2 * lm->param_scale);
                    }

                    static const NVGcolour ch_grad_stop0 = nvgHexColour(0x6C7483FF);
                    static const NVGcolour ch_grad_stop1 = nvgHexColour(0x7C8493FF);

                    const NVGpaint ch_bg_grad = nvgLinearGradient(nvg, 0, ch_y, 0, ch_b, ch_grad_stop0, ch_grad_stop1);
                    nvgSetPaint(nvg, ch_bg_grad);
                    nvgFill(nvg);

                    const double release_time_slow =
                        xm_fast_dB_to_gain((RANGE_INPUT_GAIN_MIN - RANGE_INPUT_GAIN_MAX) / (60 * 2));
                    const double release_time_fast =
                        xm_fast_dB_to_gain((RANGE_INPUT_GAIN_MIN - RANGE_INPUT_GAIN_MAX) / 30);

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
                            nvgRoundedRect(nvg, ch_x[ch], ch_b - peak_height, ch_w, peak_height, 2 * lm->param_scale);
                            has_peaks = true;
                        }
                    }
                    if (has_peaks)
                    {
                        nvgSetColour(nvg, C_DARK_BLUE);
                        nvgFill(nvg);
                    }

                    // Realtime Peak
                    float rt_peak_dB[2] = {
                        xm_fast_gain_to_dB(gui->input_gain_peaks_fast[0]),
                        xm_fast_gain_to_dB(gui->input_gain_peaks_fast[1]),
                    };
                    float rt_peak_h[2] = {lm->height, lm->height};
                    float rt_peak_y[2] = {lm->height, lm->height};

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
                            const float blur = 4 * lm->param_scale;

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
                            nvgSetPaint(nvg, paint);
                            nvgFill(nvg);
                        }
                    }

                    // Foreground realtime peak
                    for (int ch = 0; ch < 2; ch++)
                    {
                        if (rt_peak_dB[ch] > RANGE_INPUT_GAIN_MIN)
                        {
                            nvgBeginPath(nvg);
                            nvgSetColour(nvg, nvgHexColour(0xACDEECFF));
                            nvgRoundedRect(nvg, ch_x[ch], rt_peak_y[ch], ch_w, rt_peak_h[ch], 2);
                            nvgFill(nvg);
                        }
                    }

                    // 0dB notch
                    float zero_dB_pos = xm_normf(0, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                    float zero_dB_y   = ch_b - zero_dB_pos * ch_h;
                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, rect.x, zero_dB_y);
                    nvgLineTo(nvg, rect.r, zero_dB_y);
                    nvgSetPaint(nvg, bg_paint);
                    nvgStroke(nvg, 1);

                    // Peak label + gain suggestion
                    float peak_dB    = xm_maxf(gui->input_gain_peaks_slow[0], gui->input_gain_peaks_slow[1]);
                    peak_dB          = xm_maxf(peak_dB, gui->input_gain_peaks_fast[0]);
                    peak_dB          = xm_maxf(peak_dB, gui->input_gain_peaks_fast[1]);
                    peak_dB          = xm_fast_gain_to_dB(peak_dB);
                    const float ninf = -(INFINITY);
                    if (peak_dB < -120)
                        peak_dB = ninf;

                    if (peak_dB >= -5 && peak_dB <= 1)
                        nvgSetColour(nvg, C_GREEN);
                    else if (peak_dB == ninf)
                        nvgSetColour(nvg, C_GREY_2);
                    else
                        nvgSetColour(nvg, C_RED);

                    nvgSetFontSize(nvg, 8);
                    char peak_label[16];
                    snprintf(peak_label, sizeof(peak_label), "%.2f", peak_dB);
                    nvgText(nvg, (rect.x + rect.r) * 0.5f, rect.y + peak_label_height * 0.5f, peak_label, NULL);
                }
                else // param_id == PARAM_WET || param_id == PARAM_OUTPUT_GAIN
                {
                    // BG colour
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, rect.x, rect.y, meter_width, meter_height, 4 * lm->param_scale);
                    nvgSetColour(nvg, C_BG_LIGHT);
                    nvgFill(nvg);

                    // Inner shadow
                    nvgBeginPath(nvg);
                    float       shadow_y = rect.y + 2;
                    NVGcolour   icol     = {0, 0, 0, 0};
                    NVGcolour   ocol     = {0, 0, 0, 0.15};
                    const float blur1    = 4; // * lm->param_scale; // Doesn't look great scaled
                    NVGpaint    paint    = nvgBoxGradient(
                        nvg,
                        rect.x + blur1 * 0.5,
                        shadow_y + blur1 * 0.5,
                        meter_width - blur1,
                        meter_height - blur1,
                        blur1,
                        blur1,
                        icol,
                        ocol);
                    nvgSetPaint(nvg, paint);
                    nvgRoundedRect(nvg, rect.x, rect.y, meter_width, meter_height, 4 * lm->param_scale);
                    nvgFill(nvg);

                    imgui_rect handle  = rect;
                    handle.x          += 2; // padding
                    handle.y          += 2;
                    handle.r          -= 2;
                    handle.b          -= 2;
                    float w            = handle.r - handle.x;
                    float drag_y       = handle.y + w * 0.5f;
                    float drag_b       = handle.b - w * 0.5f;
                    float drag_height  = drag_b - drag_y;

                    uint32_t events  = imgui_get_events_rect(im, wid, &rect);
                    double   value_d = handle_param_events(gui, param_id, events, drag_height);

                    // Draw BG notches
                    enum
                    {
                        NOTCH_COUNT = 16
                    };
                    const float y_inc = (drag_height + w * 0.5f) / (float)NOTCH_COUNT;

                    float notch_x = handle.x + w * 0.25;
                    float notch_r = handle.x + w * 0.75;
                    nvgBeginPath(nvg);
                    for (int n = 1; n < NOTCH_COUNT - 1; n++)
                    {
                        float y = roundf(drag_y + n * y_inc) + 0.5f;
                        nvgMoveTo(nvg, notch_x, y);
                        nvgLineTo(nvg, notch_r, y);
                    }
                    notch_x = handle.x + w * 0.125;
                    notch_r = handle.x + w * 0.875;

                    float top_y = roundf(drag_y) + 0.5f;
                    nvgMoveTo(nvg, notch_x, top_y);
                    nvgLineTo(nvg, notch_r, top_y);
                    float bot_y = roundf(drag_b) + 0.5f;
                    nvgMoveTo(nvg, notch_x, bot_y);
                    nvgLineTo(nvg, notch_r, bot_y);

                    nvgSetColour(nvg, C_GREY_1);
                    nvgStroke(nvg, 1);

                    // Handle drop shadow
                    float handle_cy = xm_lerpf(value_d, drag_b, drag_y);
                    handle.y        = handle_cy - w * 0.5f;

                    const float blur2 = 4 * lm->param_scale;
                    nvgBeginPath(nvg);
                    icol       = (NVGcolour){0, 0, 0, 0.3};
                    ocol       = (NVGcolour){0, 0, 0, 0};
                    float sh_x = handle.x + 1;
                    float sh_y = handle.y + 3;

                    paint = nvgBoxGradient(0, sh_x, sh_y, w, w, 4 * lm->param_scale, blur2, icol, ocol);
                    nvgSetPaint(nvg, paint);
                    nvgRoundedRect(nvg, sh_x - blur2, sh_y - blur2, w + blur2 * 2, w + blur2 * 2, 4 * lm->param_scale);
                    nvgFill(nvg);

                    // Handle BG
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, handle.x, handle.y, w, w, 4 * lm->param_scale);
                    NVGcolour stop1 = nvgHexColour(0xB5BFC8FF);
                    NVGcolour stop2 = nvgHexColour(0xD5DFEAFF);
                    paint = nvgLinearGradient(0, 0, handle_cy - w * 0.35, 0, handle_cy + w * 0.35, stop1, stop2);
                    nvgSetPaint(nvg, paint);
                    nvgFill(nvg);

                    // Top inner shadow
                    icol  = (NVGcolour){1, 1, 1, 0};
                    ocol  = (NVGcolour){1, 1, 1, 0.3};
                    paint = nvgBoxGradient(0, handle.x, handle.y + 2, w, w, 4 * lm->param_scale, 1, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, handle.x, handle.y, w, w, 4 * lm->param_scale);
                    nvgSetPaint(nvg, paint);
                    nvgFill(nvg);
                    // Bottom inner shadow
                    icol  = (NVGcolour){0, 0, 0, 0};
                    ocol  = (NVGcolour){0, 0, 0, 0.2};
                    paint = nvgBoxGradient(0, handle.x, handle.y - 2, w, w, 4 * lm->param_scale, 1, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, handle.x, handle.y, w, w, 4 * lm->param_scale);
                    nvgSetPaint(nvg, paint);
                    nvgFill(nvg);

                    // Handle notch
                    float snapped_y = floorf(handle_cy) - 1;
                    // float snapped_y = floorf(handle_cy) + 0.5f;
                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, notch_x, snapped_y);
                    nvgLineTo(nvg, notch_r, snapped_y);
                    nvgSetColour(nvg, nvgHexColour(0x242E56FF));
                    nvgStroke(nvg, 2);

                    nvgMoveTo(nvg, notch_x, snapped_y - 2);
                    nvgLineTo(nvg, notch_r, snapped_y - 2);
                    nvgSetColour(nvg, nvgHexColour(0x9199A0FF));
                    nvgStroke(nvg, 2);
                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, notch_x, snapped_y + 2);
                    nvgLineTo(nvg, notch_r, snapped_y + 2);
                    nvgSetColour(nvg, nvgHexColour(0xDCE2E9FF));
                    nvgStroke(nvg, 1);

                    const float mod_amt_padding     = 4;
                    const float mod_amt_strokewidth = 3;
                    const float mod_amt_delta       = mod_amt_padding + mod_amt_strokewidth;

                    nvgBeginPath(nvg);
                    for (int j = 0; j < ARRLEN(modamts.data); j++)
                    {
                        if (fabsf(modamts.data[j]) != 0)
                        {
                            float mod_amt = modamts.data[j];

                            unsigned mod_amt_event = mod_amt_events[param_id][j];
                            if (!(mod_amt_event & IMGUI_EVENT_MOUSE_HOVER))
                            {
                                mod_amt *= lfo_amt.data[j];
                            }
                            float mod_end_value = xm_clampf(mod_amt + value_d, 0, 1);
                            float mod_end_y     = xm_lerpf(mod_end_value, drag_b, drag_y);

                            float y_top = xm_minf(mod_end_y, handle_cy);
                            float y_bot = xm_maxf(mod_end_y, handle_cy);
                            if (y_bot <= y_top)
                                y_bot += 1;

                            float l, r;
                            if (j == 0)
                            {
                                l = rect.x - mod_amt_padding - mod_amt_strokewidth;
                                r = rect.x - mod_amt_padding;
                            }
                            else
                            {
                                l = rect.r + mod_amt_padding;
                                r = rect.r + mod_amt_padding + mod_amt_strokewidth;
                            }

                            nvgRect2(nvg, l, y_top, r, y_bot);
                        }
                    }
                    if (modamts.u64)
                    {
                        nvgSetColour(nvg, C_DARK_BLUE);
                        nvgFill(nvg);
                    }
                }
                break;
            }
            default:
                xassert(false);
                break;
            }
        }
    }

    snvg_command_custom(nvg, gui, do_knob_shader, NVG_LABEL("Knob shader"));

    /*
    const float peak_gain = gui->plugin->gui_output_peak_gain;
    if (peak_gain > 1)
    {
        nvgSetTextAlign(nvg, NVG_ALIGN_BR);
        nvgSetColour(nvg, nvgRGBAf(1, 0.1, 0.1, 1));
        float dB = xm_fast_gain_to_dB(peak_gain);
        char  label[48];
        snprintf(label, sizeof(label), "[WARNING] Auto hardclipper: ON. %.2fdB", dB);
        nvgText(nvg, lm->width - 20, gui_height - 20, label, NULL);
    }

#ifdef CPLUG_BUILD_STANDALONE
    {
        Plugin* p = gui->plugin;
        // plot_expander(nvg, lm->width, gui_height);
        // plot_peak_detection(nvg, lm->width, gui_height);
        // plot_peak_distortion(nvg, im, lm->width, gui_height);
        // plot_peak_upwards_compression(nvg, im, lm->width, gui_height);
        float midi  = xt_atomic_load_f32(&p->gui_osc_midi);
        float phase = xt_atomic_load_f32(&p->gui_osc_phase);
        plot_oscilloscope(nvg, lm->width - 230, 10, 220, 180, p->sample_rate, midi, phase);

        imgui_rect  rect   = {lm->width - 220, 10, lm->width - 60, 25};
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
    */

    // LFO toggle button
    {
        imgui_rect rect = gui->lfo_toggle_button;
        snvg_command_draw_nvg(nvg, NVG_LABEL("ayy lmao"));
        nvgBeginPath(nvg);
        nvgSetColour(nvg, C_TEXT);
        nvgSetFontSize(nvg, lm->content_scale * 14);
        nvgSetTextAlign(nvg, NVG_ALIGN_CC);
        float cx = (rect.x + rect.r) * 0.5f - 10;
        float cy = (rect.y + rect.b) * 0.5f;
        nvgText(nvg, cx, cy, "LFO", 0);

        float y1 = cy + 2;
        float y2 = cy - 4;
        if (gui->plugin->lfo_section_open)
        {
            float tmp = y1;
            y1        = y2;
            y2        = tmp;
        }
        nvgSetLineCap(nvg, NVG_ROUND);
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, cx + 22, y1);
        nvgLineTo(nvg, cx + 28, y2);
        nvgLineTo(nvg, cx + 34, y1);
        nvgStroke(nvg, 2.5);
        nvgSetLineCap(nvg, NVG_BUTT);
        unsigned events = imgui_get_events_rect(im, 'lopn', &rect);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
    }

    if (gui->plugin->lfo_section_open)
    {
        extern void draw_lfo_section(GUI*);
        draw_lfo_section(gui);
    }

    // extern float g_pd_threshold;
    // imgui_rect   threshold_rect = {20, 40, 220, 60};
    // im_slider(nvg, im, threshold_rect, &g_pd_threshold, -96, -36, "%.2fdB", "Threshold");

    snvg_command_end_pass(nvg, NVG_LABEL("end main framebuffer"));

    snvg_command_begin_pass(
        gui->nvg,
        &(sg_pass){
            .action    = {.colors[0] = {.load_action = SG_LOADACTION_DONTCARE}},
            .swapchain = gui->swapchain,
            .label     = NVG_LABEL("swapchain / main"),
        },
        0,
        0,
        lm->width,
        lm->height,
        0);
    snvg_command_draw_nvg(nvg, NVG_LABEL("swapchain"));

    nvgBeginPath(nvg);
    nvgRect(nvg, 0, 0, lm->width, lm->height);
    nvgSetPaint(
        nvg,
        nvgImagePattern(nvg, 0, 0, lm->width, lm->height, 0, fb_main.img_texview, 1, nvg->sampler_nearest));
    nvgFill(nvg);

#ifdef SYNTH_HUD
    if (calls_synth_hud.start)
        snvg_calls_join(nvg, &calls_synth_hud);
#endif // SYNTH_HUD

    // Footer
    {
        nvgSetFontSize(nvg, 12 * lm->content_scale);

        NVGcolour footer_col = C_BG_LIGHT;
        footer_col.a         = 0.5f;
        nvgSetColour(nvg, footer_col);
        char text[64] = {0};
        int  len      = 0;

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
        const char* os_name = "macOS";
#endif
        len = snprintf(text, sizeof(text), "Scream %s | %s | %s", CPLUG_PLUGIN_VERSION, plugin_type_name, os_name);
        nvgSetTextAlign(nvg, NVG_ALIGN_BL);
        nvgText(nvg, 8, lm->height - 8, text, text + len);

#ifndef NDEBUG
        // uint64_t frame_time_end         = xtime_now_ns();
        // uint64_t frame_time_duration_ns = frame_time_end - gui->frame_start_time;

        uint64_t max_frame_time_ns = 16666666; // 1/60th of a second, in nanoseconds

        // limit accuracy from nanoseconds to approximately microseconds
        uint64_t cpu_numerator   = last_frame_draw_time >> 10; // fast integer divide by 1024
        uint64_t cpu_denominator = max_frame_time_ns >> 10;    // fast integer divide by 1024

        double cpu_amt       = (double)cpu_numerator / (double)cpu_denominator;
        double frame_time_ms = (double)cpu_numerator * 1024e-6; // correct for 1024 int 'division'
        // double approx_fps    = 1000 / frame_time_ms; // Potential FPS

        // uint64_t actual
        // uint64_t diff_last_frame = frame_time_end - gui->frame_end_time;
        // gui->frame_end_time      = frame_time_end;
        double actual_fps = 1000.0 / ((time_since_last_frame >> 10) * 1024e-6);

        len = snprintf(
            text,
            sizeof(text),
            "GUI CPU: %.2lf%% Frame Time: %.3lfms. FPS: %.lf",
            (cpu_amt * 100),
            frame_time_ms,
            actual_fps);
        nvgSetTextAlign(nvg, NVG_ALIGN_TL);
        nvgText(nvg, 8, 8, text, text + len);
#endif // !NDEBUG

        // Show window dimensions w/h on resize
        uint64_t time_since_creation_ns = gui->frame_start_time - gui->gui_create_time;
        uint64_t time_since_resize_ns   = gui->frame_start_time - gui->last_resize_time;
        uint64_t threshold_1sec         = 1000000000;
        uint64_t threshold_1_2sec       = 1200000000;
        if (time_since_resize_ns < threshold_1sec && time_since_creation_ns > threshold_1_2sec)
        {
            len = snprintf(text, sizeof(text), "%dx%d", lm->width, lm->height);
            nvgSetTextAlign(nvg, NVG_ALIGN_BR);
            nvgText(nvg, lm->width - 8, lm->height - 8, text, text + len);
            // nvgSetTextAlign(nvg, NVG_ALIGN_TL);
            // nvgText(nvg, 8, 8, text, text + len);
        }
        else // shameless plug
        {
#define DEV_TAG "Developed by exacoustics"
            const char*       devtag             = DEV_TAG;
            const int         devtag_len         = STRLEN(DEV_TAG);
            const int         devtag_company_len = STRLEN("exacoustics");
            NVGglyphPosition* glyphs             = linked_arena_alloc(gui->arena, sizeof(*glyphs) * devtag_len);
            nvgTextGlyphPositions(nvg, 0, 0, devtag, devtag + devtag_len, glyphs, devtag_len);

            const NVGglyphPosition* name_start = &glyphs[devtag_len - devtag_company_len];
            const NVGglyphPosition* name_end   = &glyphs[devtag_len - 1];
            const float             name_width = name_end->maxx - name_start->minx;
            const float             tag_width  = name_end->maxx - glyphs[0].minx;

            imgui_rect tag_click_area = {lm->width - 8 - name_width, lm->content_b, lm->width - 8, lm->height};
            unsigned   events         = imgui_get_events_rect(im, 'dtag', &tag_click_area);
            if (events & IMGUI_EVENT_MOUSE_ENTER)
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
            if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
                click_devtag = true;

            nvgSetTextAlign(nvg, NVG_ALIGN_BL);
            nvgText(nvg, lm->width - 8 - tag_width, lm->height - 8, devtag, devtag + devtag_len - devtag_company_len);

            if (events & IMGUI_EVENT_MOUSE_HOVER)
                nvgSetColour(nvg, C_GREY_1);
            nvgText(
                nvg,
                lm->width - 8 - name_width,
                lm->height - 8,
                devtag + devtag_len - devtag_company_len,
                devtag + devtag_len);

            linked_arena_release(gui->arena, glyphs);
        }
    }

    if (gui->tooltip.text)
    {
        tooltip_draw(&gui->tooltip, nvg, gui->arena, gui->frame_start_time, lm->width, lm->height, lm->content_scale);
    }

    unsigned bg_events = imgui_get_events_rect(im, 'bg', &(imgui_rect){0, 0, lm->width, lm->height});
    if (bg_events & IMGUI_EVENT_MOUSE_ENTER)
    {
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_DEFAULT);
    }

    snvg_command_end_pass(nvg, NVG_LABEL("end swapchain"));
    nvgEndFrame(gui->nvg);
    sg_commit(); // flip swapchain
    resources_end_frame(&gui->resource_manager, gui->nvg);
    imgui_end_frame(&gui->imgui);

    println("GPU upload: %llu", gui->nvg->frame_stats.uploaded_bytes);

    sg_set_global(NULL);
    LINKED_ARENA_LEAK_DETECT_END(gui->arena);

    gui->frame_end_time = xtime_now_ns();

    if (click_curelogo)
        open_hyperlink("https://cure.audio");
    if (click_devtag)
        open_hyperlink("https://exacoustics.com");
}
