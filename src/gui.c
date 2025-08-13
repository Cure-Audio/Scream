
#include "common.h"

#include "gui.h"
#include "plugin.h"

#include "dsp.h"
#include "imgui.h"
#include "sort.h"
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

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <knob.glsl.h>

enum
{
    LFO_POINT_CLICK_RADIUS = 12,
    LFO_POINT_RADIUS       = 4,
    LFO_SKEW_POINT_RADIUS  = 3,
    LFO_SKEW_DRAG_RANGE    = 250,

    LFO_POINT_DRAG_ERASE_DISTANCE = 24,
};

extern double main_get_param(Plugin* p, ParamID id);

static inline int main_get_lfo_pattern_idx(Plugin* p)
{
    int    lfo_idx  = p->selected_lfo_idx;
    double v        = main_get_param(p, PARAM_PATTERN_LFO_1 + lfo_idx);
    v              *= NUM_LFO_PATTERNS - 1;
    return xm_droundi(v);
}

void gui_handle_param_change(void* _gui, ParamID param_id)
{
    GUI* gui = _gui;
    if (param_id == PARAM_PATTERN_LFO_1 || param_id == PARAM_PATTERN_LFO_2)
        gui->lfo_points_dirty = true;
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

    // Knob shader
    {
        static const sg_buffer_desc knob_vbo_desc = {
            .usage.vertex_buffer = true,
            .usage.stream_update = true,
            .size                = sizeof(vertex_t) * 4 * 3,
            .label               = NVG_LABEL("knob vertices")};
        gui->knob_vbo = sg_make_buffer(&knob_vbo_desc);

        // clang-format off
        static const uint16_t KNOB_INDICES[] = {
            0, 1, 2,  0, 2,  3,
            4, 5, 6,  4, 6,  7,
            8, 9, 10, 8, 10, 11,
        };
        _Static_assert(ARRLEN(KNOB_INDICES) == (3 * 6), "");
        // clang-format on

        static const sg_buffer_desc knob_ibo_desc = {
            .usage.index_buffer = true,
            .usage.immutable    = true,
            .data               = SG_RANGE(KNOB_INDICES),
            .size               = sizeof(KNOB_INDICES),
            .label              = NVG_LABEL("knob indices")};
        gui->knob_ibo = sg_make_buffer(&knob_ibo_desc);

        sg_shader              shd      = sg_make_shader(knob_shader_desc(sg_query_backend()));
        const sg_pipeline_desc pip_desc = {
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
            .label = NVG_LABEL("knob pipeline")};
        gui->knob_pip = sg_make_pipeline(&pip_desc);
    }

    // Logo shader
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
                    .usage.immutable = true,
                    .width           = x,
                    .height          = y,
                    .num_mipmaps     = 5,
                    .num_slices      = 1,
                    .pixel_format    = SG_PIXELFORMAT_RGBA8,

                    .data.subimage[0][0] = {
                        .ptr  = img_buf,
                        .size = x * y * comp,
                    }};
                gui->logo_id = sg_make_image_with_mipmaps(&img_desc);
                stbi_image_free(img_buf);

                gui->logo_width  = x;
                gui->logo_height = y;

                snvgCreateImageFromHandleSokol(
                    gui->nvg,
                    gui->logo_id,
                    NVG_TEXTURE_RGBA,
                    gui->logo_width,
                    gui->logo_height,
                    NVG_IMAGE_IMMUTABLE);
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
    xarr_free(gui->lfo_points);
    xarr_free(gui->lfo_skew_points);
    xarr_free(gui->lfo_cached_path);
    xarr_free(gui->selected_point_indexes);
    xarr_free(gui->points_copy);
    xarr_free(gui->skew_points_copy);

    sg_set_global(gui->sg);

    snvgDestroyFramebuffer(gui->nvg, &gui->main_framebuffer);

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

    // TODO: replace this with better grid
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
        // TODO: snap to Y grid
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
        xassert(false); // TODO!
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

            // TODO: draw text and icon

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
                if (gui->selected_point_idx != -1)
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

                        range_l = xm_maxf(range_l, grid_x);
                        range_r = xm_minf(range_r, grid_r);

                        float* skew_amts = linked_arena_alloc(gui->arena, sizeof(*skew_amts) * num_points);

                        int npoints                = 0;
                        gui->lfo_points[npoints++] = gui->points_copy[0];
                        gui->selected_point_idx    = -1;
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
                            else if (
                                p2->x <= (range_l + LFO_POINT_DRAG_ERASE_DISTANCE) ||
                                p2->x >= (range_r - LFO_POINT_DRAG_ERASE_DISTANCE))
                            {
                                gui->lfo_points[npoints++] = *p2;

                                // Clamp dragged point to nearby point
                                if (p2->x >= range_l && p2->x <= (range_l + LFO_POINT_DRAG_ERASE_DISTANCE))
                                    drag_pos.x = p2->x;
                                else if (p2->x >= (range_r - LFO_POINT_DRAG_ERASE_DISTANCE) && p2->x <= range_r)
                                    drag_pos.x = p2->x;
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

                        imgui_pt(*view_pts)[512]      = (void*)gui->lfo_points;
                        imgui_pt(*view_skew_pts)[512] = (void*)gui->lfo_skew_points;

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

void do_knob_shader(void* uptr)
{
    GUI*           gui = uptr;
    LayoutMetrics* lm  = &gui->layout;

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
    gui->frame_start_time = xtime_now_ns();
    // #endif

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

        const float dpi = pw_get_dpi(gui->pw);
#ifdef __APPLE__
        lm->content_scale    = dpi * 0.5;
        lm->devicePixelRatio = dpi; // required for text to render properly...
#else
        lm->content_scale    = dpi;
        lm->devicePixelRatio = 1;
#endif

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

        imgui_rect lfo_btn;
        lfo_btn.x              = (lm->width / 2) - 20;
        lfo_btn.y              = lm->top_content_bottom - 20;
        lfo_btn.r              = lfo_btn.x + 40;
        lfo_btn.b              = lm->top_content_bottom;
        gui->lfo_toggle_button = lfo_btn;

        snvgDestroyFramebuffer(nvg, &gui->main_framebuffer);
        gui->main_framebuffer = snvgCreateFramebuffer(nvg, lm->width, lm->height);

        gui->lfo_points_dirty = true;
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

    snvg_command_begin_pass(
        nvg,
        &(sg_pass){
            .action      = {.colors[0] = {.load_action = SG_LOADACTION_DONTCARE}},
            .attachments = gui->main_framebuffer.att,
            .label       = NVG_LABEL("main_framebuffer"),
        },
        gui->main_framebuffer.width,
        gui->main_framebuffer.height,
        NVG_LABEL("main framebuffer begin pass"));
    snvg_command_draw_nvg(nvg, NVG_LABEL("main framebuffer"));

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
        nvgSetColour(nvg, COLOUR_BG_LIGHT);
        nvgSetTextAlign(nvg, NVG_ALIGN_CC);
        nvgText(nvg, lm->width * 0.5f, lm->height_header * 0.5f + 4, "SCREAM", NULL);

        // Logo
        float x, y, w, h, img_scale;
        y         = 4;
        h         = lm->height_header - 4;
        img_scale = h / (float)gui->logo_height;
        w         = (float)gui->logo_width * img_scale;
        x         = lm->width - 16 - w;
        nvgBeginPath(nvg);
        nvgRect(nvg, x, y, w, h);
        nvgSetPaint(nvg, nvgImagePattern(nvg, x, y, w, h, 0, gui->logo_id, 1, nvg->sampler_linear));
        nvgFill(nvg);
    }

    // Main content background
    {
        float height = lm->content_b - lm->content_y;
        nvgBeginPath(nvg);
        nvgRoundedRect(nvg, 8, lm->content_y, lm->width - 16, height, 8);
        nvgSetColour(nvg, COLOUR_BG_LIGHT);
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

                float value_norm  = cplug_normaliseParameterValue(gui->plugin, i, value_d);
                float angle_value = SLIDER_START_RAD + value_norm * SLIDER_LENGTH_RAD;

                float angle_x = cosf(angle_value);
                float angle_y = sinf(angle_value);

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
                const float arc_radius = roundf(RADIUS_VALUE_ARC * lm->param_scale);
                stroke_w               = roundf(lm->param_scale * 4);
                nvgBeginPath(nvg);
                nvgArc(nvg, pt.x, pt.y, arc_radius, SLIDER_START_RAD, SLIDER_END_RAD, NVG_CW);
                nvgSetColour(nvg, COLOUR_GREY_1);
                nvgStroke(nvg, stroke_w);

                nvgBeginPath(nvg);
                nvgArc(nvg, pt.x, pt.y, arc_radius, SLIDER_START_RAD, angle_value, NVG_CW);
                nvgSetColour(nvg, COLOUR_GREY_2);
                nvgStroke(nvg, stroke_w);
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
                        nvgSetPaint(nvg, paint);
                        nvgFill(nvg);

                        nvgBeginPath(nvg);
                        nvgMoveTo(nvg, icon_x, icon_y);
                        nvgLineTo(nvg, icon_r, icon_y - 8);
                        nvgLineTo(nvg, icon_r, icon_y + 8);
                        nvgClosePath(nvg);
                        nvgSetColour(nvg, COLOUR_GREY_2);
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
                        nvgSetColour(nvg, COLOUR_BLUE_SECONDARY);
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
                    float zero_dB_y   = rect.b - padding - zero_dB_pos * ch_h;
                    nvgBeginPath(nvg);
                    nvgMoveTo(nvg, rect.x, zero_dB_y);
                    nvgLineTo(nvg, rect.r, zero_dB_y);
                    nvgSetPaint(nvg, bg_paint);
                    nvgStroke(nvg, 1);
                }
                else // param_id == PARAM_WET
                {
                    // BG colour
                    nvgBeginPath(nvg);
                    nvgRoundedRect(nvg, rect.x, rect.y, meter_width, meter_height, 4 * lm->param_scale);
                    nvgSetColour(nvg, COLOUR_BG_LIGHT);
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

                    nvgSetColour(nvg, COLOUR_GREY_1);
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
                }
                break;
            }
            default:
                xassert(false);
                break;
            }
        }

        nvgSetColour(nvg, COLOUR_TEXT);
        const float param_font_size = 14 * lm->content_scale;
        nvgSetFontSize(nvg, param_font_size);

        const float content_cy  = lm->content_y + lm->top_content_height * 0.5f;
        const float text_offset = lm->knob_radius + 40 * lm->scale_y;
        const float value_y     = content_cy - text_offset;
        const float label_b     = content_cy + text_offset;

        static const char* NAMES[] = {"CUTOFF", "SCREAM", "RESONANCE", "INPUT", "WET"};
        _Static_assert(ARRLEN(NAMES) == ARRLEN(lm->param_positions_cx));
        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            const ParamID param_id = i;
            const float   param_cx = lm->param_positions_cx[i];

            nvgSetTextAlign(nvg, NVG_ALIGN_BC);
            nvgSetColour(nvg, COLOUR_TEXT);
            nvgText(nvg, param_cx, label_b, NAMES[param_id], NULL);

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

                nvgSetColour(nvg, COLOUR_TEXT);
                nvgSetTextAlign(nvg, NVG_ALIGN_TC);
                nvgText(nvg, param_cx, value_y, label, NULL);
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
    imgui_rect rect = gui->lfo_toggle_button;
    snvg_command_draw_nvg(nvg, NVG_LABEL("ayy lmao"));
    nvgBeginPath(nvg);
    // nvgRect(nvg, rect.x, rect.y, rect.r - rect.x, rect.b - rect.y);
    // nvgSetColour(nvg, nvgHexColour(0xff0000ff));
    // nvgFill(nvg);
    nvgSetTextAlign(nvg, NVG_ALIGN_CC);
    nvgText(nvg, (rect.x + rect.r) * 0.5f, (rect.y + rect.b) * 0.5f, "LFO", 0);

    if (gui->plugin->lfo_section_open)
    {
        draw_lfo_section(gui);
    }

    snvg_command_end_pass(nvg, NVG_LABEL("end main framebuffer"));

    snvg_command_begin_pass(
        gui->nvg,
        &(sg_pass){
            .action    = {.colors[0] = {.load_action = SG_LOADACTION_DONTCARE}},
            .swapchain = gui->swapchain,
            .label     = NVG_LABEL("swapchain / main"),
        },
        lm->width,
        lm->height,
        0);
    snvg_command_draw_nvg(nvg, NVG_LABEL("swapchain"));

    sg_image bgimg = gui->main_framebuffer.img;
    nvgSetPaint(nvg, nvgImagePattern(nvg, 0, 0, lm->width, lm->height, 0, bgimg, 1, nvg->sampler_nearest));
    nvgBeginPath(nvg);
    nvgRect(nvg, 0, 0, lm->width, lm->height);
    nvgFill(nvg);

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

        nvgSetFontSize(nvg, 12 * lm->content_scale);
        NVGcolour footer_col = COLOUR_BG_LIGHT;
        footer_col.a         = 0.5f;
        nvgSetColour(nvg, footer_col);
        char text[64] = {0};
        int  len      = snprintf(
            text,
            sizeof(text),
            "GUI CPU: %.2lf%% Frame Time: %.3lfms. FPS: %.lf",
            (cpu_amt * 100),
            frame_time_ms,
            actual_fps);
        nvgSetTextAlign(nvg, NVG_ALIGN_TL);
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
        nvgSetTextAlign(nvg, NVG_ALIGN_BL);
        nvgText(nvg, 8, lm->height - 8, text, text + len);

        // Show window dimensions w/h
        uint64_t time_since_creation_ns = gui->frame_start_time - gui->gui_create_time;
        uint64_t time_since_resize_ns   = gui->frame_start_time - gui->last_resize_time;
        uint64_t threshold_1sec         = 1000000000;
        uint64_t threshold_1_2sec       = 1200000000;
        if (time_since_resize_ns < threshold_1sec && time_since_creation_ns > threshold_1_2sec)
        {
            len = snprintf(text, sizeof(text), "%dx%d", lm->width, lm->height);
            nvgSetTextAlign(nvg, NVG_ALIGN_BR);
            nvgText(nvg, lm->width - 8, lm->height - 8, text, text + len);
        }
    }

    unsigned bg_events = imgui_get_events_rect(im, 'bg', &(imgui_rect){0, 0, lm->width, lm->height});
    if (bg_events & IMGUI_EVENT_MOUSE_ENTER)
    {
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_DEFAULT);
    }

    snvg_command_end_pass(nvg, NVG_LABEL("end swapchain"));
    nvgEndFrame(gui->nvg);
    sg_commit();
    sg_set_global(NULL);

    imgui_end_frame(&gui->imgui);

    LINKED_ARENA_LEAK_DETECT_END(gui->arena);
}
