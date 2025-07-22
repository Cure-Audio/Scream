
#include "common.h"
#include "dsp.h"
#include "imgui.h"
#include "plugin.h"

#ifdef CPLUG_BUILD_STANDALONE
#include "plot_dsp.h"
#endif

#include <xhl/array.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/time.h>
#include <xhl/vector.h>

#include <cplug_extensions/window.h>
#include <nanovg.h>
#include <nanovg_sokol.h>
#include <stb_image.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <knob.glsl.h>
#include <texquad.glsl.h>

#include "gui.h"

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

// Source: https://github.com/floooh/sokol/issues/102
sg_image sg_make_image_with_mipmaps(const sg_image_desc* desc_)
{
    sg_image_desc desc = *desc_;
    xassert(
        desc.pixel_format == SG_PIXELFORMAT_RGBA8 || desc.pixel_format == SG_PIXELFORMAT_BGRA8 ||
        desc.pixel_format == SG_PIXELFORMAT_R8);

    unsigned num_channels = 1;
    if (desc.pixel_format == SG_PIXELFORMAT_RGBA8 || desc.pixel_format == SG_PIXELFORMAT_BGRA8)
        num_channels = 4;

    int w          = desc.width;
    int h          = desc.height * desc.num_slices;
    int total_size = 0;

    int max_mipmap_levels = desc.num_mipmaps;
    if (max_mipmap_levels < 1)
        max_mipmap_levels = 1;
    if (max_mipmap_levels > SG_MAX_MIPMAPS)
        max_mipmap_levels = SG_MAX_MIPMAPS;

    for (int level = 1; level < max_mipmap_levels; ++level)
    {
        w /= 2;
        h /= 2;

        if (w < 1 && h < 1)
            break;

        total_size += (w * h * num_channels);
    }

    int cube_faces = 0;
    for (; cube_faces < SG_CUBEFACE_NUM; ++cube_faces)
    {
        if (!desc.data.subimage[cube_faces][0].ptr)
            break;
    }

    total_size                *= (cube_faces + 1);
    unsigned char* big_target  = MY_MALLOC(total_size);
    unsigned char* target      = big_target;

    for (int cube_face = 0; cube_face < cube_faces; ++cube_face)
    {
        int target_width  = desc.width;
        int target_height = desc.height;
        int dst_height    = target_height * desc.num_slices;

        for (int level = 1; level < max_mipmap_levels; ++level)
        {
            unsigned char* src = (unsigned char*)desc.data.subimage[cube_face][level - 1].ptr;
            if (!src)
                break;

            int src_w      = target_width;
            int src_h      = target_height;
            target_width  /= 2;
            target_height /= 2;
            if (target_width < 1 && target_height < 1)
                break;

            if (target_width < 1)
                target_width = 1;

            if (target_height < 1)
                target_height = 1;

            dst_height               /= 2;
            unsigned       img_size   = target_width * dst_height * num_channels;
            unsigned char* miptarget  = target;

            for (int slice = 0; slice < desc.num_slices; ++slice)
            {
                for (int x = 0; x < target_width; ++x)
                {
                    for (int y = 0; y < target_height; ++y)
                    {
                        for (int ch = 0; ch < num_channels; ++ch)
                        {
                            int col = 0;
                            int sx  = x * 2;
                            int sy  = y * 2;

                            col += src[(sy * src_w + sx) * num_channels + ch];
                            col += src[(sy * src_w + (sx + 1)) * num_channels + ch];
                            col += src[((sy + 1) * src_w + (sx + 1)) * num_channels + ch];
                            col += src[((sy + 1) * src_w + sx) * num_channels + ch];
                            col /= 4;
                            miptarget[(y * target_width + x) * num_channels + ch] = (uint8_t)col;
                        }
                    }
                }

                src       += (src_w * src_h * num_channels);
                miptarget += (target_width * target_height * num_channels);
            }
            desc.data.subimage[cube_face][level].ptr   = target;
            desc.data.subimage[cube_face][level].size  = img_size;
            target                                    += img_size;
            if (desc.num_mipmaps <= level)
                desc.num_mipmaps = level + 1;
        }
    }

    sg_image img = sg_make_image(&desc);
    MY_FREE(big_target);
    return img;
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

    gui->nvg = nvgCreateSokol(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    CPLUG_LOG_ASSERT(gui->nvg);

    // Load font
    {
        char path[1024];
        xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_APPDATA);
        int         len = strlen(path);
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

        int font_id = -1;
        int i       = 0;

        do
        {
            font_id = nvgCreateFont(gui->nvg, "default", font_paths[i]);
            if (font_id == -1)
            {
                println("[CRITICAL] Failed to open fallback font at path %s", path);
            }
            i++;
        }
        while (font_id == -1 && i < ARRLEN(font_paths));

        gui->font_id = font_id;
    }

    gui->scale = (float)gui->plugin->width / (float)GUI_INIT_WIDTH;

    // Knob shader
    {
        gui->knob_vbo = sg_make_buffer(&(sg_buffer_desc){
            .usage.vertex_buffer = true,
            .usage.stream_update = true,
            .size                = sizeof(vertex_t) * 4 * 3,
            .label               = "knob-vertices"});

        // clang-format off
        static const uint16_t KNOB_INDICES[] = {
            0, 1, 2,  0, 2,  3,
            4, 5, 6,  4, 6,  7,
            8, 9, 10, 8, 10, 11,
        };
        _Static_assert(ARRLEN(KNOB_INDICES) == (3 * 6), "");
        // clang-format on

        gui->knob_ibo = sg_make_buffer(&(sg_buffer_desc){
            .usage.index_buffer = true,
            .usage.immutable    = true,
            .data               = SG_RANGE(KNOB_INDICES),
            .size               = sizeof(KNOB_INDICES),
            .label              = "knob-indices"});

        sg_shader shd = sg_make_shader(knob_shader_desc(sg_query_backend()));
        gui->knob_pip = sg_make_pipeline(&(sg_pipeline_desc){
            .shader     = shd,
            .index_type = SG_INDEXTYPE_UINT16,
            .layout =
                {.attrs =
                     {[ATTR_knob_position].format = SG_VERTEXFORMAT_FLOAT2,
                      [ATTR_knob_coord].format    = SG_VERTEXFORMAT_SHORT2N}},
            .colors[0] =
                {.write_mask = SG_COLORMASK_RGBA,
                 .blend =
                     {
                         .enabled          = true,
                         .src_factor_rgb   = SG_BLENDFACTOR_ONE,
                         .src_factor_alpha = SG_BLENDFACTOR_ONE,
                         .dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                         .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                     }},
            .label = "knob-pipeline"});
    }

    // Logo shader
    {
        static const uint16_t indices[] = {0, 1, 2, 0, 2, 3};

        gui->img_vbo = sg_make_buffer(&(sg_buffer_desc){
            .usage.vertex_buffer = true,
            .usage.stream_update = true,
            .size                = sizeof(vertex_t) * 4,
            .label               = "logo-vertices"});

        gui->img_ibo = sg_make_buffer(
            &(sg_buffer_desc){.usage.index_buffer = true, .data = SG_RANGE(indices), .label = "logo-indices"});

        sg_shader shd = sg_make_shader(texquad_shader_desc(sg_query_backend()));
        gui->img_pip  = sg_make_pipeline(&(sg_pipeline_desc){
             .shader     = shd,
             .index_type = SG_INDEXTYPE_UINT16,
             .layout =
                {.attrs =
                      {[ATTR_texquad_position].format  = SG_VERTEXFORMAT_FLOAT2,
                       [ATTR_texquad_texcoord0].format = SG_VERTEXFORMAT_SHORT2N}},
             .colors[0] =
                {.write_mask = SG_COLORMASK_RGBA,
                  .blend =
                      {
                          .enabled          = true,
                          .src_factor_rgb   = SG_BLENDFACTOR_ONE,
                          .src_factor_alpha = SG_BLENDFACTOR_ONE,
                          .dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                          .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                     }},
             .label = "logo-pipeline"});

        // a sampler object
        gui->img_smp = sg_make_sampler(&(sg_sampler_desc){
            .min_filter    = SG_FILTER_LINEAR,
            .mag_filter    = SG_FILTER_LINEAR,
            .mipmap_filter = SG_FILTER_LINEAR,
            .wrap_u        = SG_WRAP_CLAMP_TO_EDGE,
            .wrap_v        = SG_WRAP_CLAMP_TO_EDGE,
        });

        void*  file_data     = NULL;
        size_t file_data_len = 0;
        bool   ok            = false;
        {
            char path[1024];
            xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_APPDATA);
            int         len = strlen(path);
            const char* cat = XFILES_DIR_STR "Cure Audio" XFILES_DIR_STR "Scream" XFILES_DIR_STR "cureaudio.png";
            snprintf(path + len, sizeof(path) - len, "%s", cat);
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
                // TODO: mip maps
                // gui->logo_img_id = nvgCreateImageRGBA(gui->nvg, x, y, NVG_IMAGE_GENERATE_MIPMAPS, img_buf);
                // gui->logo_img_id = nvgCreateImageRGBA(gui->nvg, x, y, 0, img_buf);
                // xassert(gui->logo_img_id);

                gui->img_logo_id = sg_make_image_with_mipmaps(&(sg_image_desc){
                    .width        = x,
                    .height       = y,
                    .num_mipmaps  = 5,
                    .num_slices   = 1,
                    .pixel_format = SG_PIXELFORMAT_RGBA8,

                    .data.subimage[0][0] = {
                        .ptr  = img_buf,
                        .size = x * y * comp,
                    }});
                stbi_image_free(img_buf);

                gui->img_logo_width  = x;
                gui->img_logo_height = y;
            }

            XFILES_FREE(file_data);
        }
    }

    ted_init(&gui->texteditor);

    gui->gui_create_time = gui->frame_end_time = xtime_now_ns();

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

    sg_set_global(gui->sg);
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

    // TODO: save last used width & height to settings file
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

        gui->last_resize_time = xtime_now_ns();
    }
    imgui_send_event(&gui->imgui, event);

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
        bool changed = value_f != next_value;
        if (changed)
        {
            value_d = value_f = next_value;
            param_change_update(gui->plugin, param_id, value_d);
        }
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
        if (changed)
        {
            value_d = value_f = next_value;
            param_change_update(gui->plugin, param_id, value_d);
        }
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

void draw_lfo_section(GUI* gui)
{
    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;
    LayoutMetrics* lm  = &gui->layout;

    static const NVGcolour c_display_bg = nvgHexColour(0x090E20FF);
    static const NVGcolour c_light_blue = nvgHexColour(0x97E6FCFF);

    float bot_content_height = lm->content_b - lm->top_content_bottom;

    const float display_y = lm->top_content_bottom + 8;
    const float display_w = (lm->content_r - lm->content_x) - 2 * 8;
    const float display_h = bot_content_height - 2 * 8;
    const float display_b = display_y + display_h;
    nvgBeginPath(nvg);
    nvgRoundedRect(nvg, lm->content_x + 8, display_y, display_w, display_h, 6);
    nvgFillColour(nvg, c_display_bg);
    nvgFill(nvg);

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

        SHAPES_WIDTH = 40, // LFO shape buttons are square

        CONTENT_PADDING_X = 32,
        CONTENT_PADDING_Y = 16,

        PATTERN_WIDTH                = 256,
        PATTERN_NUMBER_LABEL_PADDING = 32,
        PATTERN_SLIDER_WIDTH         = PATTERN_WIDTH - 2 * PATTERN_NUMBER_LABEL_PADDING,
        PATTERN_TRIANGLE_HEIGHT      = 12,

        DISPLAY_PADDING_TOP    = 48,
        DISPLAY_PADDING_BOTTOM = 32,
    };

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

    const float top_text_cy = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT * 0.5f;

    static int active_lfo_idx = 0;

    for (int i = 0; i < ARRLEN(lfo_tabs); i++)
    {
        const imgui_rect* rect   = &lfo_tabs[i];
        const unsigned    wid    = 'tlfo' + i;
        const unsigned    events = imgui_get_events_rect(im, wid, rect);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            println("TODO: LFO TAB %d", i);
            active_lfo_idx = i;
        }

        NVGcolour  col1, col2;
        const bool is_active = active_lfo_idx == i;
        if (is_active)
        {
            col1 = c_light_blue;
            col2 = c_display_bg;
        }
        else
        {
            col1 = c_display_bg;
            col2 = c_light_blue;
        }

        nvgBeginPath(nvg);
        if (is_active)
        {
            NVGcolour glow_col = nvgHexColour(0x459DB5FF);
            float     width    = rect->r - rect->x;
            float     height   = rect->b - rect->y;
            nvgRoundedRect(nvg, rect->x, rect->y, width, height, 4);
            nvgFillColour(nvg, col1);
            nvgFill(nvg);
        }
        else
        {
            nvgRoundedRect(nvg, rect->x + 0.5, rect->y + 0.5, rect->r - rect->x, rect->b - rect->y, 4);
            nvgStrokeColour(nvg, col2);
            nvgStrokeWidth(nvg, 1.1);
            nvgStroke(nvg);
        }

        // TODO: draw text and icon

        // snap half pixel
        float icon_x = floorf(rect->x + LFO_TAB_ICON_PADDING) + 0.5f;
        float icon_y = floorf(rect->y + LFO_TAB_ICON_PADDING) + 0.5f;
        float icon_r = icon_x + LFO_TAB_ICON_WIDTH - 1;
        float icon_b = icon_y + LFO_TAB_ICON_WIDTH - 1;

        // nvgLineCap(nvg, NVG_ROUND); // Doesn't look great when lines are so small and thin
        nvgLineCap(nvg, NVG_BUTT);
        nvgStrokeWidth(nvg, 1);
        nvgStrokeColour(nvg, col2);
        nvgFillColour(nvg, col2);

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

        nvgStroke(nvg);

        char label[]  = "LFO 1";
        label[4]     += i;

        nvgTextAlign(nvg, NVG_ALIGN_CR);
        nvgText(nvg, rect->r - LFO_TAB_ICON_PADDING, top_text_cy, label, label + 5);
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

        nvgFontSize(nvg, 14);
        nvgFillColour(nvg, COLOUR_TEXT);
        nvgTextAlign(nvg, NVG_ALIGN_CL);

        nvgTextGlyphPositions(nvg, 0, 0, label_grid, label_grid + label_grid_len, glyphs, label_length_len);
        const float label_grid_width = glyphs[label_grid_len - 1].maxx;

        nvgTextGlyphPositions(nvg, 0, 0, label_length, label_length + label_length_len, glyphs, label_length_len);
        const float label_length_width = glyphs[label_length_len - 1].maxx;

        nvgTextAlign(nvg, NVG_ALIGN_CL);
        nvgText(nvg, content_x, top_text_cy, label_grid, label_grid + label_grid_len);

        nvgTextAlign(nvg, NVG_ALIGN_CR);
        float label_length_r = content_r - GRID_BUTTON_WIDTH * 2 - GRID_BUTTON_BUTTON_GAP - GRID_BUTTON_TEXT_GAP;
        nvgText(nvg, label_length_r, top_text_cy, label_length, label_length + label_length_len);

        nvgTextAlign(nvg, NVG_ALIGN_CL);

        const float button_top    = top_text_cy - GRID_BUTTON_HEIGHT * 0.5f;
        const float button_bottom = top_text_cy + GRID_BUTTON_HEIGHT * 0.5f;
        enum
        {
            BUTTON_GRID_HALF,
            BUTTON_GRID_DOUBLE,
            BUTTON_LENGTH_HALF,
            BUTTON_LENGTH_DOUBLE,
            BUTTON_COUNT,
        };
        imgui_rect buttons[BUTTON_COUNT];

        buttons[BUTTON_GRID_HALF].x     = content_x + label_grid_width + GRID_BUTTON_TEXT_GAP;
        buttons[BUTTON_GRID_DOUBLE].x   = buttons[BUTTON_GRID_HALF].x + GRID_BUTTON_WIDTH + GRID_BUTTON_BUTTON_GAP;
        buttons[BUTTON_LENGTH_HALF].x   = content_r - 2 * GRID_BUTTON_WIDTH - GRID_BUTTON_BUTTON_GAP;
        buttons[BUTTON_LENGTH_DOUBLE].x = content_r - GRID_BUTTON_WIDTH;

        static const char* btn_labels[] = {"÷2", "×2"};

        for (int i = 0; i < BUTTON_COUNT; i++)
        {
            imgui_rect* rect = buttons + i;

            rect->y = button_top;
            rect->r = rect->x + GRID_BUTTON_WIDTH;
            rect->b = button_bottom;

            unsigned wid    = 'gbtn' + i;
            unsigned events = imgui_get_events_rect(im, wid, rect);

            if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            {
                println("CLICKED BUTTON: %d", i);
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
            nvgFillColour(nvg, COLOUR_GREY_3);
            nvgFill(nvg);

            nvgFillColour(nvg, COLOUR_GREY_1);
            int txt_idx = i & 1;
            nvgTextAlign(nvg, NVG_ALIGN_CC);
            nvgText(nvg, btn_cx, text_cy, btn_labels[txt_idx], NULL);
        }
    }

    enum ShapeButtonType
    {
        SHAPE_FLAT,
        SHAPE_LINEAR_ASC,
        SHAPE_LINEAR_DESC,
        SHAPE_CONVEX_ASC,
        SHAPE_CONCAVE_DESC,
        SHAPE_CONCAVE_ASC,
        SHAPE_CONVEX_DESC,
        SHAPE_TRIANGLE_UP,
        SHAPE_TRIANGLE_DOWN,
        SHAPE_COUNT,
    };

    float shape_x       = content_x;
    float shape_y       = display_b - CONTENT_PADDING_Y - SHAPES_WIDTH;
    float shape_inner_y = shape_y + 8;
    float shape_inner_b = shape_y + SHAPES_WIDTH - 8;
    float shape_inner_x = shape_x + 8;
    float shape_inner_r = shape_x + SHAPES_WIDTH - 8;

    nvgBeginPath(nvg);
    for (int i = 0; i < SHAPE_COUNT; i++)
    {
        imgui_rect rect = {shape_x, shape_y, shape_x + SHAPES_WIDTH, shape_y + SHAPES_WIDTH};

        unsigned wid    = 'lshp' + i;
        unsigned events = imgui_get_events_rect(im, wid, &rect);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            println("TODO: handle changing draw LFO shapes: %d", i);
        }

        // nvgBeginPath(nvg);
        // nvgFillColour(nvg, COLOUR_BG_LIGHT);
        // nvgRect(nvg, shape_x, shape_y, SHAPES_WIDTH, SHAPES_WIDTH);
        // nvgFill(nvg);

        const enum ShapeButtonType type = i;
        switch (type)
        {
        case SHAPE_FLAT:
        {
            float cy = floorf(shape_y + SHAPES_WIDTH * 0.5f) + 0.5f;
            nvgMoveTo(nvg, shape_inner_x, cy);
            nvgLineTo(nvg, shape_inner_r, cy);
            break;
        }
        case SHAPE_LINEAR_ASC:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_b);
            nvgLineTo(nvg, shape_inner_r, shape_inner_y);
            break;
        case SHAPE_LINEAR_DESC:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_y);
            nvgLineTo(nvg, shape_inner_r, shape_inner_b);
            break;
        case SHAPE_CONCAVE_ASC:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_b);
            nvgQuadTo(nvg, shape_inner_r, shape_inner_b, shape_inner_r, shape_inner_y);
            break;
        case SHAPE_CONVEX_ASC:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_b);
            nvgQuadTo(nvg, shape_inner_x, shape_inner_y, shape_inner_r, shape_inner_y);
            break;
        case SHAPE_CONCAVE_DESC:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_y);
            nvgQuadTo(nvg, shape_inner_x, shape_inner_b, shape_inner_r, shape_inner_b);
            break;
        case SHAPE_CONVEX_DESC:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_y);
            nvgQuadTo(nvg, shape_inner_r, shape_inner_y, shape_inner_r, shape_inner_b);
            break;
        case SHAPE_TRIANGLE_UP:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_b);
            nvgLineTo(nvg, shape_x + SHAPES_WIDTH * 0.5f, shape_inner_y);
            nvgLineTo(nvg, shape_inner_r, shape_inner_b);
            break;
        case SHAPE_TRIANGLE_DOWN:
            nvgMoveTo(nvg, shape_inner_x, shape_inner_y);
            nvgLineTo(nvg, shape_x + SHAPES_WIDTH * 0.5f, shape_inner_b);
            nvgLineTo(nvg, shape_inner_r, shape_inner_y);
            break;
        case SHAPE_COUNT:
            break;
        }

        shape_x       += SHAPES_WIDTH;
        shape_inner_x += SHAPES_WIDTH;
        shape_inner_r += SHAPES_WIDTH;
    }
    nvgStrokeColour(nvg, nvgHexColour(0xffffffff));
    nvgStrokeWidth(nvg, 1.2f);
    nvgStroke(nvg);

    float pattern_r  = content_r;
    float pattern_x  = xm_maxf(pattern_r - PATTERN_WIDTH, shape_x);
    float pattern_cx = 0.5f * (pattern_x + pattern_r);
    float pattern_cy = shape_y + SHAPES_WIDTH * 0.5f;
    float pattern_b  = display_b - CONTENT_PADDING_Y;
    {
        const imgui_rect rect   = {pattern_x, shape_y, pattern_r, pattern_b};
        const unsigned   events = imgui_get_events_rect(im, 'lptn', &rect);
        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
        {
            println("TODO: handle changing patterns");
        }

        nvgTextAlign(nvg, NVG_ALIGN_BC);
        nvgFillColour(nvg, COLOUR_TEXT);
        nvgText(nvg, pattern_cx, pattern_b, "PATTERN", NULL);

        nvgTextAlign(nvg, NVG_ALIGN_CL);
        nvgText(nvg, pattern_x, pattern_cy, "1", NULL);
        nvgTextAlign(nvg, NVG_ALIGN_CR);
        nvgText(nvg, pattern_r, pattern_cy, "8", NULL);

        float pattern_line_y = floorf(pattern_cy) + 0.5f;
        float pattern_line_x = pattern_x + PATTERN_NUMBER_LABEL_PADDING;
        float pattern_line_r = pattern_r - PATTERN_NUMBER_LABEL_PADDING;
        nvgBeginPath(nvg);
        nvgMoveTo(nvg, pattern_line_x, pattern_line_y);
        nvgLineTo(nvg, pattern_line_r, pattern_line_y);
        nvgStrokeColour(nvg, COLOUR_TEXT);
        nvgStroke(nvg);

        const int   pattern_num   = 1;
        const float pattern_pos_x = xm_mapf(pattern_num, 1, 8, pattern_line_x, pattern_line_r);

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

    float grid_y = display_y + CONTENT_PADDING_Y + LFO_TAB_HEIGHT + DISPLAY_PADDING_TOP;
    float grid_b = shape_y - DISPLAY_PADDING_BOTTOM;
    float grid_x = lm->content_x + CONTENT_PADDING_X + 8;
    float grid_r = lm->content_r - CONTENT_PADDING_X - 8;

    NVGcolour c_grid_1 = nvgHexColour(0x7E8795FF);
    NVGcolour c_grid_2 = nvgHexColour(0x292D32FF);

    nvgBeginPath(nvg);
    nvgRect(nvg, grid_x + 0.5f, grid_y + 0.5f, grid_r - grid_x - 1, grid_b - grid_y - 1);
    nvgStrokeWidth(nvg, 1);
    nvgStrokeColour(nvg, c_grid_1);
    nvgStroke(nvg);
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

    // #ifndef NDEBUG
    gui->frame_start_time = xtime_now_ns();
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

    const float dpi = pw_get_dpi(gui->pw);
#ifdef __APPLE__
    const float content_scale = dpi * 0.5;
#else
    const float    content_scale            = dpi;
#endif

    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;
    LayoutMetrics* lm  = &gui->layout;

    enum
    {
        HEIGHT_HEADER  = 32,
        HEIGHT_FOOTER  = 20,
        BORDER_PADDING = 8,

        CONTENT_HEIGHT = GUI_INIT_HEIGHT - HEIGHT_HEADER - HEIGHT_FOOTER - 2 * BORDER_PADDING,

        PARAMS_BOUNDARY_LEFT  = 32,
        VERTICAL_SLIDER_WIDTH = 60,
        METER_WIDTH           = 24,
        METER_HEIGHT          = 146,
        ROTARY_PARAM_DIAMETER = 160,

        _MINIMUM_WIDTH = PARAMS_BOUNDARY_LEFT * 2 + VERTICAL_SLIDER_WIDTH * 2 + ROTARY_PARAM_DIAMETER * 3,
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

        lm->height_header = floorf(HEIGHT_HEADER * content_scale * lm->scale_y);
        lm->height_footer = floorf(HEIGHT_FOOTER * content_scale * lm->scale_y);

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
        const float knob_diameter         = snapf(ROTARY_PARAM_DIAMETER * lm->param_scale, 2);

        const float total_param_width = veritcal_slider_width * 2 + knob_diameter * 3;
        const float param_padding     = (PARAMS_WIDTH - total_param_width) / 4;

        _Static_assert(
            ARRLEN(lm->param_positions_cx) == 5,
            "You've changed the number of params and we assumed there were only 5");
        lm->param_positions_cx[PARAM_INPUT_GAIN] = param_boundary_left + veritcal_slider_width * 0.5f;
        lm->param_positions_cx[PARAM_CUTOFF] =
            param_boundary_left + veritcal_slider_width + param_padding + knob_diameter * 0.5f;
        lm->param_positions_cx[PARAM_SCREAM] =
            param_boundary_left + veritcal_slider_width + param_padding * 2 + knob_diameter * 1.5f;
        lm->param_positions_cx[PARAM_RESONANCE] =
            param_boundary_left + veritcal_slider_width + param_padding * 3 + knob_diameter * 2.5f;
        lm->param_positions_cx[PARAM_WET] = param_boundary_right - veritcal_slider_width * 0.5f;

        lm->knob_radius = knob_diameter * 0.5f;

        _Static_assert(
            ARRLEN(lm->knobs_pos) == 3,
            "You've changed the number of rotary params and we assumed there were only 3");
        for (int i = 0; i < ARRLEN(lm->knobs_pos); i++)
        {
            lm->knobs_pos[i].x = lm->param_positions_cx[i];
            lm->knobs_pos[i].y = roundf(lm->content_y + lm->top_content_height * 0.5f);
        }
    }

    // Begin frame
    {
        sg_pass_action pass_action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0, 0, 0, 1.0f}}};

        sg_swapchain swapchain;
        memset(&swapchain, 0, sizeof(swapchain));
        swapchain.width        = lm->width;
        swapchain.height       = lm->height;
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
        sg_set_global(gui->sg);
        sg_begin_pass(&(sg_pass){.action = pass_action, .swapchain = swapchain});
    }

    imgui_begin_frame(im);

#ifdef __APPLE__
    // required for text to render properly...
    nvgBeginFrame(gui->nvg, lm->width, lm->height, dpi);
#else
    nvgBeginFrame(gui->nvg, lm->width, lm->height, 1.0f);
#endif

    // Background
    {
        nvgBeginPath(nvg);
        nvgRect(nvg, 0, 0, lm->width, lm->height);
        static const NVGcolour stop0 = nvgHexColour(0x151B33FF);
        static const NVGcolour stop1 = nvgHexColour(0x090E1FFF);
        nvgFillPaint(nvg, nvgLinearGradient(nvg, 0, 0, 0, lm->height, stop0, stop1));
        nvgFill(nvg);
    }

    // Header
    {
        nvgFontSize(nvg, content_scale * lm->scale_y * 24);
        nvgFillColour(nvg, COLOUR_BG_LIGHT);
        nvgTextAlign(nvg, NVG_ALIGN_CC);
        nvgText(nvg, lm->width * 0.5f, lm->height_header * 0.5f + 4, "SCREAM", NULL);

        // Sokol nanovg isn't rendering this for some reason :(
        // Logo
        // float x, y, w, h, img_scale;

        // h         = height_header - 4;
        // img_scale = h / (float)gui->img_logo_height;
        // w         = (float)gui->img_logo_width * img_scale;
        // x = lm->width - 16 - w;
        // x = 16;
        // y = 4;
        // nvgBeginPath(nvg);
        // nvgRect(nvg, x, y, w, h);
        // nvgFillPaint(nvg, nvgImagePattern(nvg, x, y, w, h, 0, gui->logo_img_id, 1));
        // nvgFillColour(nvg, (NVGcolour){1, 1, 1, 1});
        // nvgRect(nvg, 0, 0, 50, 50);
        // nvgFillPaint(nvg, nvgImagePattern(nvg, 0, 0, 50, 50, 0, gui->logo_img_id, 1));
        // nvgFill(nvg);

        // Doesn't look great rendered by nanovg...
        // nvgFillColor(nvg, (NVGcolor){1, 1, 1, 1});
        // draw_cure_audio_logo_fixed_svg(nvg, (28.0f / 241.0f), lm->width - 16 - 20, 2);
    }

    // Main content background
    {
        float height = lm->content_b - lm->content_y;
        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, 8, lm->content_y, lm->width - 16, height, 8);
        nvgFillColour(nvg, COLOUR_BG_LIGHT);
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
        nvgFillPaint(nvg, paint);
        nvgFill(nvg);

        // Bottom inner shadow (dark)
        paint.innerColor = (NVGcolour){0, 0, 0, 0};
        paint.outerColor = (NVGcolour){0, 0, 0, 0.75f};
        nvgBeginPath(nvg);
        nvgRoundedRectVarying(nvg, 8, lm->content_b - blur_radius * 2, lm->width - 16, blur_radius * 2, 0, 0, 8, 8);
        nvgFillPaint(nvg, paint);
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
        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            const ParamID  param_id = i;
            const float    param_cx = lm->param_positions_cx[i];
            const unsigned wid      = 'prm' + i;

            switch (param_id)
            {
            case PARAM_CUTOFF:
            case PARAM_SCREAM:
            case PARAM_RESONANCE:
            {
                enum
                {
                    RADIUS_INNER          = 86 / 2,
                    RADIUS_OUTER          = 98 / 2,
                    RADIUS_INLET          = 122 / 2,
                    RADIUS_VALUE_ARC      = 140 / 2,
                    RADIUS_DASHED_PATTERN = 154 / 2,
                };
                xassert(param_id < ARRLEN(lm->knobs_pos));
                imgui_pt pt = lm->knobs_pos[param_id];

                uint32_t events  = imgui_get_events_circle(im, wid, pt, lm->knob_radius);
                double   value_d = handle_param_events(gui, i, events, 300);

                // Inlet
                {
                    // 3 stop radial gradient
                    const float r100 = roundf(RADIUS_INLET * lm->param_scale);
                    const float r90  = roundf(r100 * 0.9f);
                    const float r80  = roundf(r100 * 0.8f);

                    static const NVGcolor stop100 = nvgHexColour(0x40454AFF);
                    static const NVGcolor stop90  = nvgHexColour(0xB7C7D7FF);
                    static const NVGcolor stop80  = nvgHexColour(0xC9D3DDFF);

                    NVGpaint grad_100_90 = nvgRadialGradient(nvg, pt.x, pt.y, r90, r100, stop90, stop100);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, r100);
                    nvgFillPaint(nvg, grad_100_90);
                    nvgFill(nvg);

                    NVGpaint grad_90_80 = nvgRadialGradient(nvg, pt.x, pt.y, r80, r90, stop80, stop90);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, r90);
                    nvgFillPaint(nvg, grad_90_80);
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

                    static const NVGcolor icol = {0, 0, 0, 0.25f};
                    static const NVGcolor ocol = {0, 0, 0, 0};
                    NVGpaint grad = nvgRadialGradient(nvg, pt.x, y, inner_radius, outer_radius, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, y, outer_radius);
                    nvgFillPaint(nvg, grad);
                    nvgFill(nvg);
                }

                {
                    static const NVGcolor stop0 = nvgHexColour(0xD4DFEAFF);
                    static const NVGcolor stop1 = nvgHexColour(0xB5BFC8FF);

                    const float top         = outer_y + outer_h * 0.13f;
                    const float bottom      = outer_y + outer_h * 0.84f;
                    NVGpaint    out_lingrad = nvgLinearGradient(nvg, 0, top, 0, bottom, stop0, stop1);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, radius_outer);
                    nvgFillPaint(nvg, out_lingrad);
                    nvgFill(nvg);
                }

                // Outer knob inner shadow
                {
                    static const NVGcolor icol = {1, 1, 1, 0};
                    static const NVGcolor ocol = {1, 1, 1, 0.8};
                    NVGpaint grad = nvgRadialGradient(nvg, pt.x, pt.y + 1, radius_outer - 1, radius_outer, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgCircle(nvg, pt.x, pt.y, radius_outer);
                    nvgFillPaint(nvg, grad);
                    nvgFill(nvg);
                }

                // Inner
                const float radius_inner = RADIUS_INNER * lm->param_scale;
                const float inner_y      = pt.y - radius_inner;
                const float inner_h      = radius_inner * 2;
                const float inner_s0_y   = inner_y + inner_h * 0.16f;
                const float inner_s1_y   = inner_y + inner_h * 0.87f;

                static const NVGcolor in_lin_s0 = nvgHexColour(0xB5BFC8FF);
                static const NVGcolor in_lin_s1 = nvgHexColour(0xD4DFEAFF);
                NVGpaint inner_grad = nvgLinearGradient(nvg, 0, inner_s0_y, 0, inner_s1_y, in_lin_s0, in_lin_s1);

                nvgBeginPath(nvg);
                nvgCircle(nvg, pt.x, pt.y, radius_inner);
                nvgFillPaint(nvg, inner_grad);
                nvgFill(nvg);

// Slider Tick/Notch
// Angle radians
// 120deg
#define SLIDER_START_RAD 2.0943951023931953f
// 120deg + 360deg * 0.8333
#define SLIDER_END_RAD 7.330173418865945f
// end - start
#define SLIDER_LENGTH_RAD 5.23577831647275f

                float value_norm  = cplug_normaliseParameterValue(gui->plugin, i, value_d);
                float angle_value = SLIDER_START_RAD + value_norm * SLIDER_LENGTH_RAD;

                float angle_x = cosf(angle_value);
                float angle_y = sinf(angle_value);

                float tick_radius_start = radius_inner - 10 * lm->param_scale;
                float tick_radius_end   = radius_inner * 0.4f;

                const imgui_pt pt1 = {pt.x + tick_radius_start * angle_x, pt.y + tick_radius_start * angle_y};
                const imgui_pt pt2 = {pt.x + tick_radius_end * angle_x, pt.y + tick_radius_end * angle_y};
                nvgStrokeWidth(nvg, 6 * lm->param_scale);
                nvgLineCap(nvg, NVG_ROUND);

                nvgBeginPath(nvg); // Skeumorphic inner shadow
                nvgMoveTo(nvg, pt1.x, pt1.y);
                nvgLineTo(nvg, pt2.x, pt2.y);
                nvgStrokeColour(nvg, (NVGcolour){1, 1, 1, 1});
                nvgStroke(nvg);

                nvgBeginPath(nvg);
                nvgMoveTo(nvg, pt1.x, pt1.y - 1);
                nvgLineTo(nvg, pt2.x, pt2.y - 1);
                nvgStrokeColour(nvg, nvgHexColour(0x242E56FF));
                nvgStroke(nvg);

                nvgLineCap(nvg, NVG_BUTT);

                // Value arc
                const float arc_radius = roundf(RADIUS_VALUE_ARC * lm->param_scale);
                nvgStrokeWidth(nvg, roundf(lm->param_scale * 4));
                nvgBeginPath(nvg);
                nvgArc(nvg, pt.x, pt.y, arc_radius, SLIDER_START_RAD, SLIDER_END_RAD, NVG_CW);
                nvgStrokeColour(nvg, COLOUR_GREY_1);
                nvgStroke(nvg);

                nvgBeginPath(nvg);
                nvgArc(nvg, pt.x, pt.y, arc_radius, SLIDER_START_RAD, angle_value, NVG_CW);
                nvgStrokeColour(nvg, COLOUR_GREY_2);
                nvgStroke(nvg);
                break;
            }
            case PARAM_INPUT_GAIN:
            case PARAM_WET:
            {
                const float meter_width  = snapf(METER_WIDTH * lm->param_scale, 2);
                const float meter_height = snapf(METER_HEIGHT * lm->param_scale, 2);

                imgui_rect rect;

                rect.x = roundf(param_cx - meter_width * 0.5);
                rect.r = roundf(param_cx + meter_width * 0.5);
                rect.y = roundf(lm->knobs_pos[0].y - meter_height * 0.5f);
                rect.b = roundf(lm->knobs_pos[0].y + meter_height * 0.5f);

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

                if (param_id == PARAM_INPUT_GAIN)
                {
                    float       rect_r     = rect.r;
                    const float icon_width = 10;
                    float       icon_r     = rect.r + icon_width + 4;
                    rect.r                 = icon_r;
                    uint32_t events        = imgui_get_events_rect(im, wid, &rect);
                    rect.r                 = rect_r;

                    double value_d = handle_param_events(gui, PARAM_INPUT_GAIN, events, rect.b - rect.y);

                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, rect.x, rect.y, rect.r - rect.x, rect.b - rect.y, 4 * lm->param_scale);
                    nvgFillColour(nvg, nvgHexColour(0x2C2F35FF));

                    static const NVGcolour bg_grad_stop0 = nvgHexColour(0x2C2F35FF);
                    static const NVGcolour bg_grad_stop1 = nvgHexColour(0x585E6AFF);
                    const NVGpaint         bg_paint =
                        nvgLinearGradient(nvg, 0, rect.y, 0, rect.b, bg_grad_stop0, bg_grad_stop1);
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
                        nvgRoundedRect(nvg, ch_x[ch], ch_y, ch_w, ch_h, 2 * lm->param_scale);
                    }

                    static const NVGcolour ch_grad_stop0 = nvgHexColour(0x6C7483FF);
                    static const NVGcolour ch_grad_stop1 = nvgHexColour(0x7C8493FF);

                    const NVGpaint ch_bg_grad = nvgLinearGradient(nvg, 0, ch_y, 0, ch_b, ch_grad_stop0, ch_grad_stop1);
                    nvgFillPaint(nvg, ch_bg_grad);
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
                        nvgFillColour(nvg, nvgHexColour(0x459DB5FF));
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
                }
                else // param_id == PARAM_WET
                {
                    // BG colour
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, rect.x, rect.y, meter_width, meter_height, 4 * lm->param_scale);
                    nvgFillColour(nvg, COLOUR_BG_LIGHT);
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
                    nvgFillPaint(nvg, paint);
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
                    double   value_d = handle_param_events(gui, PARAM_WET, events, drag_height);

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

                    nvgStrokeColour(nvg, COLOUR_GREY_1);
                    nvgStrokeWidth(nvg, 1);
                    nvgStroke(nvg);

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
                    nvgFillPaint(nvg, paint);
                    nvgRoundedRect(nvg, sh_x - blur2, sh_y - blur2, w + blur2 * 2, w + blur2 * 2, 4 * lm->param_scale);
                    nvgFill(nvg);

                    // Handle BG
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, handle.x, handle.y, w, w, 4 * lm->param_scale);
                    NVGcolor stop1 = nvgHexColour(0xB5BFC8FF);
                    NVGcolor stop2 = nvgHexColour(0xD5DFEAFF);
                    paint = nvgLinearGradient(0, 0, handle_cy - w * 0.35, 0, handle_cy + w * 0.35, stop1, stop2);
                    nvgFillPaint(nvg, paint);
                    nvgFill(nvg);

                    // Top inner shadow
                    icol  = (NVGcolour){1, 1, 1, 0};
                    ocol  = (NVGcolour){1, 1, 1, 0.3};
                    paint = nvgBoxGradient(0, handle.x, handle.y + 2, w, w, 4 * lm->param_scale, 1, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, handle.x, handle.y, w, w, 4 * lm->param_scale);
                    nvgFillPaint(nvg, paint);
                    nvgFill(nvg);
                    // Bottom inner shadow
                    icol  = (NVGcolour){0, 0, 0, 0};
                    ocol  = (NVGcolour){0, 0, 0, 0.2};
                    paint = nvgBoxGradient(0, handle.x, handle.y - 2, w, w, 4 * lm->param_scale, 1, icol, ocol);
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, handle.x, handle.y, w, w, 4 * lm->param_scale);
                    nvgFillPaint(nvg, paint);
                    nvgFill(nvg);

                    // Handle notch
                    float snapped_y = floorf(handle_cy) - 1;
                    // float snapped_y = floorf(handle_cy) + 0.5f;
                    nvgStrokeWidth(nvg, 2);
                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, notch_x, snapped_y);
                    nvgLineTo(nvg, notch_r, snapped_y);
                    nvgStrokeColour(nvg, nvgHexColour(0x242E56FF));
                    nvgStroke(nvg);
                    nvgStrokeWidth(nvg, 1);
                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, notch_x, snapped_y - 2);
                    nvgLineTo(nvg, notch_r, snapped_y - 2);
                    nvgStrokeColour(nvg, nvgHexColour(0x9199A0FF));
                    nvgStroke(nvg);
                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, notch_x, snapped_y + 2);
                    nvgLineTo(nvg, notch_r, snapped_y + 2);
                    nvgStrokeColour(nvg, nvgHexColour(0xDCE2E9FF));
                    nvgStroke(nvg);
                }
                break;
            }
            }
        }

        nvgFillColour(nvg, COLOUR_TEXT);
        const float param_font_size = 14 * content_scale * lm->param_scale;
        nvgFontSize(nvg, 14 * content_scale * lm->param_scale);

        const float content_cy  = lm->content_y + lm->top_content_height * 0.5f;
        const float text_offset = lm->knob_radius + 40 * lm->scale_y;
        const float value_y     = content_cy - text_offset;
        const float label_b     = content_cy + text_offset;

        static const char* NAMES[] = {"CUTOFF", "SCREAM", "RESONANCE", "INPUT", "WET"};
        _Static_assert(ARRLEN(NAMES) == NUM_PARAMS);
        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            const ParamID param_id = i;
            const float   param_cx = lm->param_positions_cx[i];

            nvgTextAlign(nvg, NVG_ALIGN_BC);
            nvgFillColour(nvg, COLOUR_TEXT);
            nvgText(nvg, param_cx, label_b, NAMES[param_id], NULL);

            extern double main_get_param(Plugin * p, ParamID id);

            imgui_rect rect;
            rect.x = param_cx - 50;
            rect.r = param_cx + 50;
            rect.y = value_y;
            rect.b = value_y + param_font_size;

            unsigned wid    = 'txt' + i;
            uint32_t events = imgui_get_events_rect(im, wid, &rect);

            if (events & IMGUI_EVENT_MOUSE_ENTER)
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_IBEAM);

            // Handle events
            if (gui->texteditor.active_param == i)
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
                    ted_activate(&gui->texteditor, dimensions, pos, param_font_size, i);
                }
            }

            // Draw
            if (gui->texteditor.active_param == i)
            {
                ted_draw(&gui->texteditor);
            }
            else
            {
                char   label[24];
                double value = main_get_param(gui->plugin, param_id);
                cplug_parameterValueToString(gui->plugin, param_id, label, sizeof(label), value);

                nvgFillColour(nvg, COLOUR_TEXT);
                nvgTextAlign(nvg, NVG_ALIGN_TC);
                nvgText(nvg, param_cx, value_y, label, NULL);
            }
        }
    }

    /*
    const float peak_gain = gui->plugin->gui_output_peak_gain;
    if (peak_gain > 1)
    {
        nvgTextAlign(nvg, NVG_ALIGN_BR);
        nvgFillColour(nvg, nvgRGBAf(1, 0.1, 0.1, 1));
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

    // Footer
    {
        uint64_t frame_time_end         = xtime_now_ns();
        uint64_t frame_time_duration_ns = frame_time_end - gui->frame_start_time;

        uint64_t max_frame_time_ns = 16666666; // 1/60th of a second, in nanoseconds

        // limit accuracy from nanoseconds to approximately microseconds
        uint64_t cpu_numerator   = frame_time_duration_ns >> 10; // fast integer divide by 1024
        uint64_t cpu_denominator = max_frame_time_ns >> 10;      // fast integer divide by 1024

        double cpu_amt       = (double)cpu_numerator / (double)cpu_denominator;
        double frame_time_ms = (double)cpu_numerator * 1024e-6; // correct for 1024 int 'division'
        // double approx_fps    = 1000 / frame_time_ms; // Potential FPS

        // uint64_t actual
        uint64_t diff_last_frame = frame_time_end - gui->frame_end_time;
        gui->frame_end_time      = frame_time_end;
        double actual_fps        = 1000.0 / ((diff_last_frame >> 10) * 1024e-6);

        nvgFontSize(nvg, 12 * content_scale);
        NVGcolour footer_col = COLOUR_BG_LIGHT;
        footer_col.a         = 0.5f;
        nvgFillColour(nvg, footer_col);
        char text[64] = {0};
        int  len      = snprintf(
            text,
            sizeof(text),
            "GUI CPU: %.2lf%% Frame Time: %.3lfms. FPS: %.lf",
            (cpu_amt * 100),
            frame_time_ms,
            actual_fps);
        nvgTextAlign(nvg, NVG_ALIGN_TL);
        nvgText(nvg, 8, 8, text, text + len);

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
        nvgTextAlign(nvg, NVG_ALIGN_BL);
        nvgText(nvg, 8, lm->height - 8, text, text + len);

        // Show window dimensions w/h
        uint64_t time_since_creation_ns = gui->frame_start_time - gui->gui_create_time;
        uint64_t time_since_resize_ns   = gui->frame_start_time - gui->last_resize_time;
        uint64_t threshold_1sec         = 1000000000;
        uint64_t threshold_1_2sec       = 1200000000;
        if (time_since_resize_ns < threshold_1sec && time_since_creation_ns > threshold_1_2sec)
        {
            len = snprintf(text, sizeof(text), "%dx%d", lm->width, lm->height);
            nvgTextAlign(nvg, NVG_ALIGN_BR);
            nvgText(nvg, lm->width - 8, lm->height - 8, text, text + len);
        }
    }

    bool toggle_lfo_open = false;
    {
        imgui_rect lmao;
        lmao.x = (lm->width / 2) - 20;
        lmao.y = lm->top_content_bottom - 40;
        lmao.r = lmao.x + 40;
        lmao.b = lm->top_content_bottom;

        unsigned events = imgui_get_events_rect(im, 'size', &lmao);

        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            toggle_lfo_open = true;

        nvgBeginPath(nvg);
        nvgRect(nvg, lmao.x, lmao.y, lmao.r - lmao.x, lmao.b - lmao.y);
        nvgFillColour(nvg, nvgHexColour(0xff0000ff));
        nvgFill(nvg);
    }

    if (gui->plugin->lfo_section_open)
    {
        draw_lfo_section(gui);
    }

    // End frame
    nvgEndFrame(gui->nvg);

    // Knob shader
    {
        // clang-format off
        vertex_t verts[] = {
            {-1.0f,  1.0f, -32767,  32767},
            { 1.0f,  1.0f,  32767,  32767},
            { 1.0f, -1.0f,  32767, -32767},
            {-1.0f, -1.0f, -32767, -32767},

            {-1.0f,  1.0f, -32767,  32767},
            { 1.0f,  1.0f,  32767,  32767},
            { 1.0f, -1.0f,  32767, -32767},
            {-1.0f, -1.0f, -32767, -32767},

            {-1.0f,  1.0f, -32767,  32767},
            { 1.0f,  1.0f,  32767,  32767},
            { 1.0f, -1.0f,  32767, -32767},
            {-1.0f, -1.0f, -32767, -32767},
        };
        _Static_assert(ARRLEN(verts) == (3 * 4), "");
        _Static_assert(ARRLEN(verts) / 4 == ARRLEN(lm->knobs_pos), "");
        // clang-format on

        float radius = lm->knob_radius;
        xassert(radius != 0);
        for (int i = 0; i < ARRLEN(lm->knobs_pos); i++)
        {
            float left   = lm->knobs_pos[i].x - radius;
            float right  = lm->knobs_pos[i].x + radius;
            float top    = lm->knobs_pos[i].y - radius;
            float bottom = lm->knobs_pos[i].y + radius;

            left   = xm_mapf(left, 0, lm->width, -1, 1);
            right  = xm_mapf(right, 0, lm->width, -1, 1);
            top    = xm_mapf(top, 0, lm->height, 1, -1);
            bottom = xm_mapf(bottom, 0, lm->height, 1, -1);

            int v_idx = i * 4;

            verts[v_idx + 0].x = left;
            verts[v_idx + 0].y = top;
            verts[v_idx + 1].x = right;
            verts[v_idx + 1].y = top;
            verts[v_idx + 2].x = right;
            verts[v_idx + 2].y = bottom;
            verts[v_idx + 3].x = left;
            verts[v_idx + 3].y = bottom;
        }

        sg_update_buffer(gui->knob_vbo, &SG_RANGE(verts));
        sg_apply_pipeline(gui->knob_pip);

        sg_bindings bind       = {0};
        bind.vertex_buffers[0] = gui->knob_vbo;
        bind.index_buffer      = gui->knob_ibo;
        sg_apply_bindings(&bind);

        xassert(sg_isvalid());

        sg_draw(0, 6 * 3, 1);
    }

    // Logo shader
    if (gui->img_logo_id.id)
    {
        float x, y, w, h, img_scale;

        float padding = 4 * lm->scale_y;
        h             = lm->height_header - padding;
        img_scale     = h / (float)gui->img_logo_height;
        w             = (float)gui->img_logo_width * img_scale;
        x             = lm->width - 16 - w;
        // x         = 16;
        y = padding;

        float l = xm_mapf(x, 0, lm->width, -1, 1);
        float r = xm_mapf(x + w, 0, lm->width, -1, 1);
        float t = xm_mapf(y, 0, lm->height, 1, -1);
        float b = xm_mapf(y + h, 0, lm->height, 1, -1);

        // clang-format off
        vertex_t verts[] = {
            {l, t, 0,     0},
            {r, t, 32767, 0},
            {r, b, 32767, 32767},
            {l, b, 0,     32767},
        };

        sg_update_buffer(gui->img_vbo, &SG_RANGE(verts));
        sg_apply_pipeline(gui->img_pip);

        sg_bindings bind       = {0};
        bind.vertex_buffers[0] = gui->img_vbo;
        bind.index_buffer      = gui->img_ibo;
        bind.images[IMG_texquad_tex]   = gui->img_logo_id;
        bind.samplers[SMP_texquad_smp] = gui->img_smp;

        sg_apply_bindings(&bind);
        sg_draw(0, 6, 1);
    }

    unsigned bg_events = imgui_get_events_rect(im, 'bg', &(imgui_rect){0,0,lm->width, lm->height});
    if (bg_events & IMGUI_EVENT_MOUSE_ENTER)
    {
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_DEFAULT);
    }

    sg_end_pass();
    sg_commit();
    sg_set_global(NULL);

    imgui_end_frame(&gui->imgui);

    if (toggle_lfo_open)
    {
        gui->plugin->lfo_section_open = !gui->plugin->lfo_section_open;

        int next_height = lm->height;
        int content_height = lm->content_b - lm->content_y;
        if (gui->plugin->lfo_section_open)
            next_height += content_height;
        else
            next_height -= (content_height / 2);
        xassert(next_height >= 0);

        gui->plugin->cplug_ctx->requestResize(gui->plugin->cplug_ctx, lm->width, next_height);
    }
}
