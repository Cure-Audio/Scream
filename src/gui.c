
#include "common.h"

#include "gui.h"
#include "plugin.h"

#include "dsp.h"
// #include "widgets.h"

#include <stdint.h>
#include <xhl/array2.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/string.h>
#include <xhl/time.h>
#include <xhl/vector.h>

#include <cplug_extensions/window.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include <imgui.h>
#include <layout.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <shaders.glsl.h>

#if defined(CPLUG_BUILD_STANDALONE)
// #define SYNTH_HUD
#endif

#if defined(SYNTH_HUD) && defined(CPLUG_BUILD_STANDALONE)
#include "libs/synth.h"
extern Synth g_synth;
#endif

enum
{
    HEIGHT_HEADER  = 32,
    HEIGHT_FOOTER  = 20,
    BORDER_PADDING = 8,

    CONTENT_HEIGHT = GUI_INIT_HEIGHT - HEIGHT_HEADER - HEIGHT_FOOTER - 2 * BORDER_PADDING,
};

void gui_handle_param_change(void* _gui, ParamID param_id)
{
    GUI* gui = _gui;
    if (param_id == PARAM_PATTERN_LFO_1 || param_id == PARAM_PATTERN_LFO_2)
    {
        imp_reload(&gui->imp);
    }
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
// Modified to use STBIR
sg_image make_icon_mipmaps(const sg_image_desc* desc_)
{
    sg_image_desc desc = *desc_;
    // TODO: support floats
    XVG_ASSERT(
        desc.pixel_format == SG_PIXELFORMAT_RGBA8 || desc.pixel_format == SG_PIXELFORMAT_BGRA8 ||
        desc.pixel_format == SG_PIXELFORMAT_R8);

    unsigned num_channels = 1;
    if (desc.pixel_format == SG_PIXELFORMAT_RGBA8 || desc.pixel_format == SG_PIXELFORMAT_BGRA8)
        num_channels = 4;

    stbir_pixel_layout layout_type = desc.pixel_format == SG_PIXELFORMAT_RGBA8   ? STBIR_RGBA
                                     : desc.pixel_format == SG_PIXELFORMAT_BGRA8 ? STBIR_BGRA
                                                                                 : STBIR_1CHANNEL;

    int max_slices = desc.num_slices;
    if (max_slices < 1)
        max_slices = 1;

    int w          = desc.width;
    int h          = desc.height * max_slices;
    int total_size = 0;

    int target_max_mipmap_levels = desc.num_mipmaps;
    if (target_max_mipmap_levels <= 0)
        target_max_mipmap_levels = SG_MAX_MIPMAPS;
    if (target_max_mipmap_levels > SG_MAX_MIPMAPS)
        target_max_mipmap_levels = SG_MAX_MIPMAPS;

    int max_mipmap_levels;
    for (max_mipmap_levels = 1; max_mipmap_levels < target_max_mipmap_levels; ++max_mipmap_levels)
    {
        w /= 2;
        h /= 2;

        if (w < 1 || h < 1)
            break;

        total_size += (w * h * num_channels);
    }

    unsigned char* big_target = XVG_MALLOC(total_size);
    unsigned char* target     = big_target;
    XVG_ASSERT(big_target);

    int target_width  = desc.width;
    int target_height = desc.height;
    int dst_height    = target_height * max_slices;

    for (int level = 1; level < max_mipmap_levels; ++level)
    {
        unsigned char* src = (unsigned char*)desc.data.mip_levels[level - 1].ptr;
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

        dst_height              /= 2;
        unsigned       img_size  = target_width * dst_height * num_channels;
        unsigned char* dst       = target;

        XVG_ASSERT(dst < big_target + total_size);

        for (int slice = 0; slice < max_slices; ++slice)
        {
            // stbir_resize_uint8_srgb(
            //     src,
            //     src_w,
            //     src_h,
            //     src_w * num_channels,
            //     dst,
            //     target_width,
            //     target_height,
            //     target_width * num_channels,
            //     layout_type);
            // Hack to get icons looking good
            int mitchell_offset = ICON_EXACOUSTICS_COLOUR + 1;
            int src_h_mitchell  = src_w * mitchell_offset;
            int src_h_points    = src_h - src_h_mitchell;

            unsigned char* src_mitchell = src;
            unsigned char* src_points   = src + src_h_mitchell * src_w * num_channels;

            int dst_h_mitchell = target_width * mitchell_offset;
            int dst_h_points   = target_height - dst_h_mitchell;

            unsigned char* dst_mitchell = dst;
            unsigned char* dst_points   = dst + dst_h_mitchell * target_width * num_channels;

            stbir_resize(
                src_mitchell,
                src_w,
                src_h_mitchell,
                src_w * num_channels,
                dst_mitchell,
                target_width,
                dst_h_mitchell,
                target_width * num_channels,
                layout_type,
                STBIR_TYPE_UINT8_SRGB,
                STBIR_EDGE_CLAMP,
                STBIR_FILTER_MITCHELL);
            stbir_resize(
                src_points,
                src_w,
                src_h_points,
                src_w * num_channels,
                dst_points,
                target_width,
                dst_h_points,
                target_width * num_channels,
                layout_type,
                STBIR_TYPE_UINT8_SRGB,
                STBIR_EDGE_CLAMP,
                STBIR_FILTER_POINT_SAMPLE);

            src += (src_w * src_h * num_channels);
            dst += (target_width * target_height * num_channels);
        }
        desc.data.mip_levels[level].ptr   = target;
        desc.data.mip_levels[level].size  = img_size;
        target                           += img_size;
        desc.num_mipmaps                  = level + 1;
    }
    XVG_ASSERT(desc.num_mipmaps == max_mipmap_levels);

    sg_image img = sg_make_image(&desc);
    XVG_FREE(big_target);
    return img;
}

extern unsigned long long SVG_DATA_Cure_Audio[];
extern unsigned long long SVG_DATA_Exacoustics_white[];
extern unsigned long long SVG_DATA_Exacoustics_colour[];
extern unsigned long long SVG_DATA_PT_Select[];
extern unsigned long long SVG_DATA_PT_Flat[];
extern unsigned long long SVG_DATA_PT_Ramp_Up[];
extern unsigned long long SVG_DATA_PT_Ramp_Down[];
extern unsigned long long SVG_DATA_PT_Convex_Up[];
extern unsigned long long SVG_DATA_PT_Convex_Down[];
extern unsigned long long SVG_DATA_PT_Concave_Up[];
extern unsigned long long SVG_DATA_PT_Concave_Down[];
extern unsigned long long SVG_DATA_PT_Cosine_Up[];
extern unsigned long long SVG_DATA_PT_Cosine_Down[];
extern unsigned long long SVG_DATA_PT_Tri_Up[];
extern unsigned long long SVG_DATA_PT_Tri_Down[];
extern unsigned long long SVG_DATA_Loop_icon[];
extern unsigned long long SVG_DATA_Retrig_icon[];
extern unsigned long long SVG_DATA_One_shot_icon[];
extern unsigned long long SVG_DATA_Crotchet[];

static const unsigned long long* SVGS[] = {
    SVG_DATA_Cure_Audio,      SVG_DATA_Exacoustics_white, SVG_DATA_Exacoustics_colour, SVG_DATA_PT_Select,
    SVG_DATA_PT_Flat,         SVG_DATA_PT_Ramp_Up,        SVG_DATA_PT_Ramp_Down,       SVG_DATA_PT_Concave_Up,
    SVG_DATA_PT_Concave_Down, SVG_DATA_PT_Convex_Up,      SVG_DATA_PT_Convex_Down,     SVG_DATA_PT_Cosine_Up,
    SVG_DATA_PT_Cosine_Down,  SVG_DATA_PT_Tri_Up,         SVG_DATA_PT_Tri_Down,        SVG_DATA_Loop_icon,
    SVG_DATA_Retrig_icon,     SVG_DATA_One_shot_icon,     SVG_DATA_Crotchet,
};
xstatic_assert(ARRLEN(SVGS) == ICON_COUNT, "");

void icons_init_size(IconMap* m, int size)
{
    if (size > m->width || size > m->height)
    {
        sg_destroy_view(m->view);
        sg_destroy_image(m->img);
        xfree(m->buffer);

        m->width  = size;
        m->height = size;
        m->buffer = xmalloc(m->width * m->height * 4);

        m->img  = sg_make_image(&(sg_image_desc){
             .width                = m->width,
             .height               = m->height,
             .pixel_format         = SG_PIXELFORMAT_RGBA8,
             .usage.dynamic_update = true,
        });
        m->view = sg_make_view(&(sg_view_desc){.texture = m->img});
    }

    // Reset icons
    memset(m->buffer, 0, m->width * m->height * 4);
    memset(&m->atlas, 0, sizeof(m->atlas));
    stbrp_init_target(&m->atlas.ctx, m->width - 1, m->height - 1, m->atlas.nodes, ARRLEN(m->atlas.nodes));
}

xvec4i icon_get_coords(GUI* gui, enum IconID id, float w, float h)
{
    xvec4i c = {0};

    w *= gui->xvg.backingScaleFactor;
    h *= gui->xvg.backingScaleFactor;

    bool ok = id >= 0 && id < ARRLEN(gui->icons.atlas.rects);
    xassert(ok);
    if (!ok)
        return c;
    // Check if coords are in cache
    stbrp_rect* rect = &gui->icons.atlas.rects[id];
    if (rect->was_packed == 0)
    {
        // If not, use stbrp to find place in atlas
        rect->w        = w;
        rect->h        = h;
        int num_packed = stbrp_pack_rects(&gui->icons.atlas.ctx, rect, 1);

        if (num_packed)
        {
            // Render to atlas
            NSVGimage2* image = (NSVGimage2*)SVGS[id];

            float scale = (float)rect->h / image->height;

            int stride = gui->icons.width * 4;

            unsigned char* dst  = gui->icons.buffer;
            dst                += rect->y * stride;
            dst                += rect->x * 4;

            nsvgRasterise(&gui->svg_rasteriser, image, 0, 0, scale, dst, rect->w, rect->h, stride, gui->arena);

            gui->icons.dirty = true;
        }
    }
    // Return coords

    xassert(rect->w == (int)w);
    xassert(rect->h == (int)h);

    c.x      = rect->x;
    c.y      = rect->y;
    c.width  = rect->w;
    c.height = rect->h;

    return c;
}

void icons_end_frame(IconMap* m)
{
    if (m->dirty)
    {
        m->dirty                        = false;
        size_t              update_size = m->width * m->height * 4;
        const sg_image_data data        = {
                   .mip_levels[0] = {
                       .ptr  = m->buffer,
                       .size = update_size,
            }};
        sg_update_image(m->img, &data);
    }
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

    extern float system_get_content_scale();
    gui->layout.content_scale = system_get_content_scale();

    sg_environment env;
    memset(&env, 0, sizeof(env));
    env.defaults.sample_count = 1;
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
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

    tooltip_init(&gui->tooltip);
    gui->tooltip.settings.colour_border = 0x8A94A8ff;
    gui->tooltip.settings.colour_text   = 0x5D636Aff;
    gui->tooltip.settings.colour_bg     = 0xD4D7DEff;

    xvg_init(&gui->xvg);
    gui->_xvg_bg0 = xvg_command_list_create(&gui->xvg);
    gui->_xvg_bg1 = xvg_command_list_create(&gui->xvg);
    gui->xvg_anim = xvg_command_list_create(&gui->xvg);

    imp_init(&gui->imp, 'lpt\0', 'lsp\0', 'lbg\0');

    // Load assets
    {
        // Font
        char path[1024];
#ifdef NDEBUG
        // Requires installer putting assets in correct folder
        int         len             = xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_APPDATA);
        const char* relpath_plugin  = XFILES_DIR_STR "Cure Audio" XFILES_DIR_STR "Scream" XFILES_DIR_STR;
        len                        += xfmt(path, len, "%s", relpath_plugin);
#else
        // Load from assets dir
        int         len             = xfmt(path, 0, "%s", SRC_DIR);
        const char* relpath_plugin  = XFILES_DIR_STR "assets" XFILES_DIR_STR;
        len                        += xfmt(path, len, "%s", relpath_plugin);
#endif

        // Don't increment path len
        xfmt(path, len, "%s", "Tomorrow-SemiBold.ttf");

        const char* font_paths[] = {
            path,
#ifdef _WIN32
            "C:\\Windows\\Fonts\\arial.ttf",
#elif defined(__APPLE__)
            "/Library/Fonts/Arial Unicode.ttf",
#endif
        };

        XVGFont font     = {0};
        int     font_idx = 0;

        do
        {
            font = xvg_add_font_from_path(&gui->xvg, font_paths[font_idx]);
            if (font.id == 0)
            {
                println("[CRITICAL] Failed to open fallback font at path %s", path);
            }
            font_idx++;
        }
        while (font.id == 0 && font_idx < ARRLEN(font_paths));

        gui->font = font;
    }
    resources_init(&gui->resource_manager, 4096);
    gui->active_param_text_input = -1;
    ted_init(&gui->texteditor, gui->xvg_anim);

    uint64_t now_ns            = xtime_now_ns();
    gui->gui_create_time       = now_ns;
    gui->last_resize_time      = now_ns;
    gui->last_frame_start_time = now_ns;
    gui->frame_start_time      = now_ns;
#ifdef SHOW_FPS
    gui->frame_end_time = now_ns;
#endif // SHOW_FPS
    gui->imgui.frame.events = 1 << PW_EVENT_RESIZE_UPDATE;

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;
    sg_set_global(gui->sg);

    xarr_free(gui->lfo_ybuffer);
    xarr_free(gui->lfo_playhead_trail);
    ted_deinit(&gui->texteditor);
    resources_deinit(&gui->resource_manager, &gui->xvg);
    xfree(gui->icons.buffer);

    imp_deinit(&gui->imp);
    xvg_deinit(&gui->xvg);
    sg_shutdown(gui->sg);

    gui->plugin->gui = NULL;

    // #ifdef CPLUG_BUILD_STANDALONE
    //     if (buffer_audio)
    //     {
    //         MY_FREE(buffer_audio);
    //         buffer_audio = NULL;
    //     }
    //     if (buffer_processed)
    //     {
    //         MY_FREE(buffer_processed);
    //         buffer_processed = NULL;
    //     }
    //     if (oscilloscope_ringbuf)
    //     {
    //         MY_FREE(oscilloscope_ringbuf);
    //         oscilloscope_ringbuf = NULL;
    //     }
    // #endif // CPLUG_BUILD_STANDALONE

    // TODO: save last used width & height to settings file

    linked_arena_destroy(gui->arena);
}

void pw_get_info(struct PWGetInfo* info)
{
    if (info->type == PW_INFO_INIT_SIZE)
    {
        Plugin* p = info->init_size.plugin;

        uint32_t screen_width  = 0;
        uint32_t screen_height = 0;
        pw_get_screen_size(&screen_width, &screen_height);

        uint32_t max_width  = (uint32_t)((float)screen_width * 0.8f);
        uint32_t max_height = (uint32_t)((float)screen_height * 0.8f);

        p->width  = xm_minu(max_width, p->width);
        p->height = xm_minu(max_height, p->height);

        info->init_size.width  = p->width;
        info->init_size.height = p->height;
    }
    else if (info->type == PW_INFO_CONSTRAIN_SIZE)
    {
        GUI* gui = info->constrain_size.gui;

        uint32_t width  = info->constrain_size.width;
        uint32_t height = info->constrain_size.height;

        float    content_scale  = xm_maxf(1, gui->layout.content_scale);
        uint32_t min_width      = (uint32_t)(GUI_MIN_WIDTH * content_scale);
        uint32_t min_height     = (uint32_t)(GUI_MIN_HEIGHT * content_scale);
        uint32_t content_height = (uint32_t)(CONTENT_HEIGHT * content_scale);

        if (gui->plugin->lfo_section_open)
        {
            min_height += content_height;
        }

        if (width < min_width)
            width = min_width;
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

    if (event->type == PW_EVENT_RESIZE_UPDATE)
    {
        // Retain size info for when the GUI is destroyed / reopened
        gui->plugin->width  = event->resize.width;
        gui->plugin->height = event->resize.height;

        gui->last_resize_time = xtime_now_ns();
    }

    if (gui->active_param_text_input != -1)
    {
        TextEditor* ted = &gui->texteditor;

        bool ret               = false;
        bool should_deactivate = false;

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
                should_deactivate = true;
            }
        }
        else if (event->type == PW_EVENT_TEXT)
        {
            xassert(event->text.codepoint != 0x7f);

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
                if (param_string_to_value(gui->active_param_text_input, text, &val))
                {
                    param_set(gui->plugin, gui->active_param_text_input, val);
                }

                should_deactivate = true;
            }
            else if (event->text.codepoint == 27) // ESC
            {
                // This is not how Chrome behaves, it just feels intuitive to me...
                should_deactivate = true;
                ret               = true;
            }
            else if (event->text.codepoint == 9) // TAB
            {
                // Ignored
            }

            xassert(ted->ibeam_idx >= 0 && ted->ibeam_idx <= xarr_len(ted->codepoints));
        }
        else if (event->type == PW_EVENT_KEY_DOWN)
        {
            ret = ted_handle_key_down(ted, gui->pw, event);
            xassert(ted->ibeam_idx >= 0 && ted->ibeam_idx <= xarr_len(ted->codepoints));
        }
        else if (event->type == PW_EVENT_RESIZE_UPDATE)
        {
            should_deactivate = true;
        }
        else if (event->type == PW_EVENT_KEY_FOCUS_LOST)
        {
            should_deactivate = true;
        }

        if (should_deactivate)
        {
            gui->active_param_text_input = -1;
            ted_deactivate(ted);

            if (gui->plugin->cplug_ctx->type != CPLUG_PLUGIN_IS_STANDALONE && event->type != PW_EVENT_KEY_FOCUS_LOST)
            {
                pw_release_keyboard_focus(gui->pw);
            }
        }
        if (ret)
            return ret;
    }
    else if (gui->plugin->cplug_ctx->type == CPLUG_PLUGIN_IS_STANDALONE)
    {
        if ((event->type == PW_EVENT_MOUSE_LEFT_DOWN || event->type == PW_EVENT_MOUSE_RIGHT_DOWN ||
             event->type == PW_EVENT_MOUSE_MIDDLE_DOWN))
        {
            pw_get_keyboard_focus(gui->pw);
        }

        if (event->type == PW_EVENT_KEY_DOWN || event->type == PW_EVENT_KEY_UP)
        {
            bool is_repeat = (event->key.modifiers & PW_MOD_KEY_REPEAT) == PW_MOD_KEY_REPEAT;
            if (is_repeat && event->type == PW_EVENT_KEY_DOWN)
                return true;

            // clang-format off
            static const unsigned char KEYBOARD_CHARACTERS[] = {
                PW_KEY_A, PW_KEY_W, PW_KEY_S, PW_KEY_E, PW_KEY_D, PW_KEY_F, PW_KEY_T, PW_KEY_G, PW_KEY_Y, PW_KEY_H, PW_KEY_U,
                PW_KEY_J, PW_KEY_K, PW_KEY_O, PW_KEY_L, PW_KEY_P, PW_KEY_OEM_1, PW_KEY_OEM_7, PW_KEY_OEM_6
            };
            // clang-format on
            static uint32_t keyboard_state = 0;

            static int octave = 4;
            if (event->type == PW_EVENT_KEY_DOWN &&
                (event->key.virtual_key == PW_KEY_Z || event->key.virtual_key == PW_KEY_X))
            {
                int prev_octave = octave;
                int next_octave = octave;
                if (event->key.virtual_key == PW_KEY_Z)
                    next_octave = xm_clampi(octave - 1, 0, 8);
                if (event->key.virtual_key == PW_KEY_X)
                    next_octave = xm_clampi(octave + 1, 0, 8);

                if (prev_octave != next_octave)
                {
                    int diff = next_octave - octave;
                    octave   = next_octave;
                    // transpose currently playing keys

                    for (int i = 0; i < ARRLEN(KEYBOARD_CHARACTERS); i++)
                    {
                        if (keyboard_state & (1 << i))
                        {
                            CplugEvent e;
                            e.type        = CPLUG_EVENT_MIDI;
                            e.midi.status = 128; // note off
                            e.midi.data1  = xm_clampu(prev_octave * 12 + i, 0, 127);
                            e.midi.data2  = 127;
                            send_to_audio_event_queue(gui->plugin, &e);

                            e.midi.status = 144; // note on
                            e.midi.data1  = xm_clampu(next_octave * 12 + i, 0, 127);
                            send_to_audio_event_queue(gui->plugin, &e);
                        }
                    }
                }
                return true;
            }

            for (int i = 0; i < ARRLEN(KEYBOARD_CHARACTERS); i++)
            {
                if (KEYBOARD_CHARACTERS[i] == event->key.virtual_key)
                {
                    CplugEvent e;
                    e.type       = CPLUG_EVENT_MIDI;
                    e.midi.data1 = xm_clampu(octave * 12 + i, 0, 127);
                    e.midi.data2 = 127;
                    if (event->type == PW_EVENT_KEY_DOWN)
                    {
                        e.midi.status   = 144; // note on
                        keyboard_state |= 1 << i;
                    }
                    else
                    {
                        e.midi.status   = 128; // note off
                        keyboard_state &= ~(1 << i);
                    }

                    send_to_audio_event_queue(gui->plugin, &e);

                    return true;
                }
            }
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

    if (event->type == PW_EVENT_CONTENT_SCALE_FACTOR_CHANGED)
    {
        gui->layout.content_scale = event->content_scale_factor;
    }

    return false;
}

double handle_param_events(GUI* gui, ParamID param_id, uint32_t events, float drag_range_px)
{
    imgui_context* im      = &gui->imgui;
    double         value_d = main_get_param(gui->plugin, param_id);
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
        float delta = im->frame.delta_scroll.y / drag_range_px;
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
        float delta = im->frame.delta_mouse_wheel * 0.1f;
        if (im->frame.modifiers_mouse_wheel & PW_MOD_INVERTED_SCROLL)
            delta = -delta;
        if (im->frame.modifiers_mouse_wheel & PW_MOD_PLATFORM_KEY_CTRL)
            delta *= 0.1f;
        if (im->frame.modifiers_mouse_wheel & PW_MOD_KEY_SHIFT)
            delta *= 0.1f;

        float next_value = xm_clampf(value_f + delta, 0, 1);
        bool  changed    = value_f != next_value;
        value_d = value_f = next_value;

        param_set(gui->plugin, param_id, value_d);
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
        float feather = 1.0f / radius;

        fs_knob_uniforms_t fs_uniforms = {
            .u_colour     = {0.7098039215686275, 0.7450980392156863, 0.7803921568627451, 1},
            .fan_feather  = 0.15,
            .ring_feather = 0.002,
        };

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

            sg_apply_uniforms(UB_vs_knob_uniforms, &SG_RANGE(vs_uniforms));
            sg_apply_uniforms(UB_fs_knob_uniforms, &SG_RANGE(fs_uniforms));

            sg_draw(0, 6, 1);
        }

        xassert(sg_isvalid());
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
    xfmt(buf, 0, "open %s", url);
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

void draw_checkbox(XVGCommandList* xvg, float width, float cy, float r, float scale, bool on)
{
    imgui_rect box;
    box.x = floorf(r - width);
    box.r = ceilf(r);
    box.y = floorf(cy - width * 0.5f);
    box.b = ceilf(cy + width * 0.5f);

    float stroke_width  = ceilf(scale);
    float inner_padding = ceilf(scale * 3);

    unsigned col = on ? C_LIGHT_BLUE_2 : C_GRID_SECONDARY;

    xvg_draw_rectangle(xvg, box.x, box.y, box.r - box.x, box.b - box.y, 4, stroke_width, col);

    if (on)
    {
        xvg_draw_rectangle(
            xvg,
            box.x + inner_padding,
            box.y + inner_padding,
            box.r - box.x - 2 * inner_padding,
            box.b - box.y - 2 * inner_padding,
            2,
            0,
            col);
    }
}

void do_background_shader(void* _gui)
{
    GUI*           gui = _gui;
    LayoutMetrics* lm  = &gui->layout;
    sg_pipeline    pip = {0};
    bool           ok  = resource_get_pipeline(&gui->resource_manager, &pip, bg_shader_desc, 0);

    if (ok)
    {
        sg_apply_pipeline(pip);

        vs_bg_uniforms_t vs_uniforms = {
            .u_size                   = {lm->width, lm->height},
            .u_bg_colour_top          = 0x151B33FF,
            .u_bg_colour_bottom       = 0x090E1FFF,
            .u_bg_colour_content      = C_BG_LIGHT,
            .u_padding_x              = 8,
            .u_height_header          = lm->content_y,
            .u_height_footer          = lm->height - lm->content_b,
            .u_border_radius          = 8,
            .u_radial_gradient_y      = lm->content_y + lm->top_content_height * 0.2,
            .u_radial_gradient_radius = {lm->width * 0.6f, lm->top_content_height * 0.75f},
        };
        sg_apply_uniforms(UB_vs_bg_uniforms, &SG_RANGE(vs_uniforms));

        sg_draw(0, 3, 1);
    }
}

void draw_background(GUI* gui, bool bg)
{
    XVGCommandList* xvg = gui->xvg_bg;
    LayoutMetrics*  lm  = &gui->layout;

    if (bg)
    {
        XVGGradient bg_grad = xvg_make_linear_gradient(0x151B33FF, 0x090E1FFF, 0, 0, 0, lm->height);
        xvg_draw_solid_rectangle_with_gradient(xvg, 0, 0, lm->width, lm->height, bg_grad);

        float width  = lm->width - 16;
        float height = lm->content_b - lm->content_y;
        xvg_draw_rectangle(xvg, 8, lm->content_y, width, height, 8, 0, C_BG_LIGHT);

        float cx = lm->width * 0.5f;
        float h  = lm->top_content_height;
        // Mimic light reflections
        XVGGradient lighting =
            xvg_make_radial_gradient(0xffffff7f, 0xffffff00, cx, lm->content_y + h * 0.2, lm->width * 0.6f, h * 0.75f);
        xvg_draw_rectangle_with_gradient(xvg, 8, lm->content_y, width, h, 8, 0, lighting);

        // Gentle reds and greens to add depth
        // XVGGradient metallic =
        //     xvg_make_linear_gradient(0xAC48000c, 0xBCEB2008, 8, lm->content_y, width, lm->top_content_bottom);
        // xvg_draw_rectangle_with_gradient(xvg, 8, lm->content_y, width, height, 8, 0, metallic);

        // Inner shadows
        const float blur_radius = 8;
        float       grad_x      = lm->content_x - blur_radius * 0.5f;
        float       grad_r      = lm->content_r + blur_radius * 0.5f;
        float       grad_w      = grad_r - grad_x;

        // Top inner shadow (light)
        XVGGradient top_shadow = xvg_make_shadow(0xffffffdf, 0xffffff00, 0, 10, 4, -8, true);
        xvg_draw_rectangle_with_gradient(xvg, 8, lm->content_y, width, 16, 8, 0, top_shadow);

        // Bottom inner shadow (dark)
        XVGGradient bot_shadow = xvg_make_shadow(0x7f, 0, 0, -10, 4, -8, true);
        xvg_draw_rectangle_with_gradient(xvg, 8, lm->content_b - 16, width, 16, 8, 0, bot_shadow);
    }

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
    for (int i = 0; i < ARRLEN(points); i++)
    {
        xvg_draw_circle(xvg, points[i].x, points[i].y - 1, DOT_RADIUS, 0, 0x0);        // fake bevelled edge
        xvg_draw_circle(xvg, points[i].x, points[i].y + 1, DOT_RADIUS, 0, 0xffffffff); // fake bevelled edge
        xvg_draw_circle(xvg, points[i].x, points[i].y, DOT_RADIUS, 0, 0x111629FF);
    }
}

bool do_bg_command_lists_match(GUI* gui)
{
    XVGCommandList* l1 = gui->_xvg_bg0;
    XVGCommandList* l2 = gui->_xvg_bg1;

    bool match = true;
    // Shapes
    match &= l1->frame.num_shapes == l2->frame.num_shapes;
    if (!match)
        return match;
    match &= 0 == memcmp(l1->shapes, l2->shapes, sizeof(l2->shapes[0]) * l2->frame.num_shapes);
    if (!match)
        return match;

    // Lines
    match &= l1->frame.num_line_segments == l2->frame.num_line_segments;
    if (!match)
        return match;
    match &=
        0 == memcmp(l1->line_segments, l2->line_segments, sizeof(l2->line_segments[0]) * l2->frame.num_line_segments);
    if (!match)
        return match;
    // Text
    match &= l1->frame.num_text == l2->frame.num_text;
    if (!match)
        return match;
    match &= 0 == memcmp(l1->text, l2->text, sizeof(l2->text[0]) * l2->frame.num_text);

    return match;
}

void pw_tick(void* _gui)
{
    GUI*    gui = _gui;
    Plugin* p   = gui->plugin;

#ifdef SHOW_FPS
    uint64_t frame_duration_last_frame = gui->frame_end_time - gui->frame_start_time;
#endif
    gui->last_frame_start_time = gui->frame_start_time;
    gui->frame_start_time      = xtime_now_ns();

    gui->xvg_bg        = (gui->frame_counter & 1) ? gui->_xvg_bg1 : gui->_xvg_bg0;
    XVGCommandList* bg = gui->xvg_bg;
    gui->frame_counter++;

    {
        uint32_t head = xt_atomic_load_u32(&p->queue_main_head) & EVENT_QUEUE_MASK;
        uint32_t tail = p->queue_main_tail;
        if (head != tail)
            gui->imgui.num_duplicate_backbuffers = 0;
        main_dequeue_events(p);
    }

    bool click_curelogo = false;
    bool click_exaclogo = false;

#if defined(_WIN32)
    // Using the CPLUG window extension, we have configured our DXGI backbuffer count to a maximum of 2
    const uint32_t MAX_DUP_BACKBUFFER_COUNT = 2 + 2;
#elif defined(__APPLE__)
    // Using the CPLUG window extension, we use the default settings in MTKView, which appears to work fine with
    const uint32_t MAX_DUP_BACKBUFFER_COUNT = 1;
#endif

    // #ifdef CPLUG_BUILD_STANDALONE
    //     if (oscilloscope_ringbuf)
    //         gui->imgui.num_duplicate_backbuffers = 0;
    // #endif

    // Uncomment to enable event driven redrawing
    // if (gui->imgui.num_duplicate_backbuffers >= MAX_DUP_BACKBUFFER_COUNT)
    //     return;

    LINKED_ARENA_LEAK_DETECT_BEGIN(gui->arena);

    const uint64_t time_since_last_frame = gui->frame_start_time - gui->last_frame_start_time;

    XVGCommandList* xvg = gui->xvg_anim;
    imgui_context*  im  = &gui->imgui;
    LayoutMetrics*  lm  = &gui->layout;

    sg_set_global(gui->sg);

    enum
    {
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
    // 75% min width is the new minimum scale
    const int min_width = ((_MINIMUM_WIDTH * 3) / 4);
    _Static_assert(((_MINIMUM_WIDTH * 3) / 4) < GUI_MIN_WIDTH, "");

    // Recalculate layout metrics
    if (im->frame.events & ((1 << PW_EVENT_RESIZE_UPDATE) | (1 << PW_EVENT_CONTENT_SCALE_FACTOR_CHANGED)))
    {
        lm->width  = p->width;
        lm->height = p->height;

        int init_height = GUI_INIT_HEIGHT;
        int top_height  = lm->height;
        if (p->lfo_section_open)
        {
            init_height = HEIGHT_HEADER + HEIGHT_FOOTER + 2 * CONTENT_HEIGHT + 2 * BORDER_PADDING;
        }

        lm->scale_x = (float)lm->width / (float)GUI_INIT_WIDTH;
        lm->scale_y = (float)top_height / (float)init_height;

        gui->xvg.backingScaleFactor = pw_get_backing_scale_factor(gui->pw);

        lm->param_scale = xm_maxf(0.75, xm_minf(lm->scale_x, lm->scale_y));

        lm->height_header = floorf(HEIGHT_HEADER * lm->param_scale);
        lm->height_footer = floorf(HEIGHT_FOOTER * lm->param_scale);

        lm->content_x = BORDER_PADDING;
        lm->content_r = lm->width - BORDER_PADDING;
        lm->content_y = lm->height_header + BORDER_PADDING;
        lm->content_b = lm->height - lm->height_footer - BORDER_PADDING;

        const bool lfo_open = p->lfo_section_open;

        float content_height = lm->content_b - lm->content_y;
        if (lfo_open)
            lm->top_content_height = floorf(content_height * 0.5f);
        else
            lm->top_content_height = content_height;
        lm->top_content_bottom = lm->content_y + lm->top_content_height;

        const float param_boundary_left  = lm->scale_x * PARAMS_BOUNDARY_LEFT;
        const float param_boundary_right = lm->width - lm->scale_x * PARAMS_BOUNDARY_LEFT;
        const float PARAMS_WIDTH         = param_boundary_right - param_boundary_left;

        const float veritcal_slider_width = snapf(VERTICAL_SLIDER_WIDTH * lm->param_scale, 2);
        const float knob_diameter         = snapf(ROTARY_PARAM_OUTER_DIAMETER * lm->param_scale, 2);

        {
            _Static_assert(
                ARRLEN(lm->param_positions_cx) == 5,
                "You've changed the number of params and we assumed there were only 5");
            imgui_rect rects[ARRLEN(lm->param_positions_cx)] = {0};

            rects[0].r = veritcal_slider_width;
            rects[1].r = knob_diameter;
            rects[2].r = knob_diameter;
            rects[3].r = knob_diameter;
            rects[4].r = veritcal_slider_width;
            layout_horizontal_fill(
                rects,
                ARRLEN(rects),
                LAYOUT_SPACE_BETWEEN,
                &(imgui_rect){param_boundary_left, 0, param_boundary_right, 0});
            for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
                lm->param_positions_cx[i] = 0.5f * (rects[i].x + rects[i].r);
        }

        lm->knob_outer_radius = knob_diameter * 0.5f;
        lm->knob_inner_radius = lm->param_scale * 0.5f * ROTARY_PARAM_INNER_DIAMETER;

        {
            imgui_rect rects[4] = {0};
            rects[0].b          = lm->param_scale * 20;                          // param value
            rects[1].b          = lm->knob_outer_radius * 2;                     // param
            rects[2].b          = lm->param_scale * PARAM_MOD_AMOUNT_RADIUS * 2; // mod amount
            rects[3].b          = lm->param_scale * 20;                          // Param title

            imgui_rect box  = {0, lm->content_y, 0, lm->content_y + lm->top_content_height};
            box.y          += 36 + lm->param_scale * 10;
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
                        .label                = XVG_LABEL("lfo_ybuffer"),
            });
            gui->lfo_playhead_trail_obj  = sg_make_buffer(&(sg_buffer_desc){
                 .usage.storage_buffer = true,
                 .usage.stream_update  = true,
                 .size                 = lfo_buffer_cap * sizeof(*gui->lfo_playhead_trail),
                 .label                = XVG_LABEL("lfo_playhead_trail"),
            });
            gui->lfo_ybuffer_view        = sg_make_view(&(sg_view_desc){.storage_buffer = gui->lfo_ybuffer_obj});
            gui->lfo_playhead_trail_view = sg_make_view(&(sg_view_desc){.storage_buffer = gui->lfo_playhead_trail_obj});
        }
        const int lfo_idx        = p->selected_lfo_idx;
        float     playhead       = (float)p->lfos[lfo_idx].phase;
        lm->current_lfo_playhead = lm->last_lfo_playhead = playhead;

        float      lfo_btn_width = 64 * lm->param_scale;
        imgui_rect lfo_btn;
        lfo_btn.x              = (lm->width / 2) - lfo_btn_width * 0.5f;
        lfo_btn.y              = lm->top_content_bottom - 20 * lm->param_scale;
        lfo_btn.r              = (lm->width / 2) + lfo_btn_width * 0.5f;
        lfo_btn.b              = lm->top_content_bottom;
        gui->lfo_toggle_button = lfo_btn;

        // Framebuffer
        int fb_width  = gui->plugin->width * gui->xvg.backingScaleFactor;
        int fb_height = gui->plugin->width * gui->xvg.backingScaleFactor;
        if (fb_width != gui->bg_framebuffer.width || fb_height != gui->bg_framebuffer.height)
        {
            sg_destroy_view(gui->bg_framebuffer.img_texview);
            sg_destroy_view(gui->bg_framebuffer.img_colview);
            sg_destroy_image(gui->bg_framebuffer.img);
            gui->bg_framebuffer.width  = gui->plugin->width * gui->xvg.backingScaleFactor;
            gui->bg_framebuffer.height = gui->plugin->height * gui->xvg.backingScaleFactor;
            gui->bg_framebuffer.img    = sg_make_image(&(sg_image_desc){
                   .usage.color_attachment = true,
                   .width                  = gui->bg_framebuffer.width,
                   .height                 = gui->bg_framebuffer.height,
                   .pixel_format           = SG_PIXELFORMAT_RGBA8,
                   .sample_count           = 1,
            });
            gui->bg_framebuffer.img_colview =
                sg_make_view(&(sg_view_desc){.color_attachment = gui->bg_framebuffer.img});
            gui->bg_framebuffer.img_texview = sg_make_view(&(sg_view_desc){.texture = gui->bg_framebuffer.img});
            xassert(gui->bg_framebuffer.width);
            xassert(gui->bg_framebuffer.height);
            xassert(gui->bg_framebuffer.img_texview.id);
            gui->_xvg_bg0->frame.num_shapes = 0; // This will force the BG to redraw
            gui->_xvg_bg0->frame.num_text   = 0;
            gui->_xvg_bg1->frame.num_shapes = 0;
            gui->_xvg_bg1->frame.num_text   = 0;
        }

        // Remake icons ?
        icons_init_size(&gui->icons, ceilf(lm->param_scale * 320));
    }

    // Note: The 'id<CAMetalDrawable>' pointer can change every frame.
    // New calls to get this pointer must be issued every frame
    gui->swapchain = (sg_swapchain){
        .width        = gui->layout.width,
        .height       = gui->layout.height,
        .sample_count = 1,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_NONE,

#if __APPLE__
        .metal.current_drawable = pw_get_metal_drawable(gui->pw),
#endif
#if _WIN32
        .d3d11.render_view = pw_get_dx11_render_target_view(gui->pw),
#endif
    };

    imgui_begin_frame(im, gui->layout.width, gui->layout.height);

    xvg_begin_frame(&gui->xvg);
    xvg_command_list_begin_frame(gui->xvg_anim);
    xvg_command_list_begin_frame(bg);

    xvg_command_begin_pass(
        bg,
        &(sg_pass){
            .attachments.colors[0] = gui->bg_framebuffer.img_colview,
            .action = {.colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0.0f, 0.0f, 0.0f, 1.0}}},
            .label  = XVG_LABEL("swapchain-pass-begin")},
        XVG_LABEL("swapchain-pass-begin"));

    xvg_command_begin_pass(
        gui->xvg_anim,
        &(sg_pass){
            .swapchain = gui->swapchain,
            .action    = {.colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0.0f, 0.0f, 0.0f, 1.0}}},
            .label     = XVG_LABEL("swapchain-pass-begin")},
        XVG_LABEL("swapchain-pass-begin"));

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
    // NOTE: this is the heaviest draw call in the entire GUI, likely due to the high pixel coverage
    // Currently its 50% of all consumed GPU usage by the GUI
    // We use a custom shader to draw all gradients in one pass. It costs half as much GPU%
    bool draw_bg = true;
    if (draw_bg == false)
    {
        xvg_command_custom(bg, gui, do_background_shader, XVG_LABEL("BG Shader"));
    }
    draw_background(gui, draw_bg);

    xvg_draw_solid_rectangle_with_gradient(
        xvg,
        0,
        0,
        lm->width,
        lm->height,
        xvg_make_image_fill(
            gui->bg_framebuffer.img_texview,
            gui->xvg.smp_linear,
            0,
            0,
            gui->bg_framebuffer.width,
            gui->bg_framebuffer.height,
            0xffffffff));

    // Header
    {
        float cx = lm->width * 0.5f;
        float cy = lm->height_header * 0.5f + 4;
        xvg_draw_text(bg, cx, cy, "SCREAM", 0, 24 * lm->param_scale, XVG_ALIGN_CC, C_BG_LIGHT);

        // Output gain
        const int  output_gain_with = 120 * lm->param_scale;
        float      fsize            = 12 * lm->param_scale;
        imgui_rect rect             = {0, 0, lm->width - 16, lm->height_header + 8};
        rect.x                      = floorf(rect.r - output_gain_with);
        uint32_t events             = imgui_get_events_rect(im, 'outg', &rect);
        handle_param_events(gui, PARAM_OUTPUT_GAIN, events, 200);

        extern int param_value_to_string(ParamID paramId, char* buf, size_t bufsize, double value);

        double value = main_get_param(p, PARAM_OUTPUT_GAIN);
        char   label[16];
        int    label_len = param_value_to_string(PARAM_OUTPUT_GAIN, label, sizeof(label), value);

        xvg_draw_text(xvg, rect.r, (rect.b - rect.y) * 0.5f, label, label + label_len, fsize, XVG_ALIGN_CR, C_GREY_1);
        xvg_draw_text(bg, rect.x, (rect.b - rect.y) * 0.5f, "OUTPUT", NULL, fsize, XVG_ALIGN_CL, C_TEXT_DARK_BG);
    }

    // Params
    // / *
    {
        static const ParamID param_ids[] = {PARAM_INPUT_GAIN, PARAM_CUTOFF, PARAM_SCREAM, PARAM_RESONANCE, PARAM_WET};
        _Static_assert(ARRLEN(param_ids) == ARRLEN(lm->param_positions_cx), "");

        // Param labels
        const float fsize = 14 * lm->param_scale;

        static const char* NAMES[] = {"INPUT", "CUTOFF", "SCREAM", "RESONANCE", "WET"};
        _Static_assert(ARRLEN(NAMES) == ARRLEN(lm->param_positions_cx));
        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            const ParamID param_id = param_ids[i];
            const float   param_cx = lm->param_positions_cx[i];

            xvg_draw_text(bg, param_cx, lm->cy_param_title, NAMES[i], NULL, fsize, XVG_ALIGN_CC, C_TEXT_LIGHT_BG);

            imgui_rect rect;
            rect.x = param_cx - 50;
            rect.r = param_cx + 50;
            rect.y = lm->cy_param_value - lm->param_scale * 10;
            rect.b = rect.y + lm->param_scale * 20;

            unsigned wid    = 'txt' + i;
            uint32_t events = imgui_get_events_rect(im, wid, &rect);

            if (events & IMGUI_EVENT_MOUSE_ENTER)
                pw_set_mouse_cursor(gui->pw, PW_CURSOR_IBEAM);

            // Handle events
            if (gui->active_param_text_input == param_id)
            {
                TextEditor* ted = &gui->texteditor;
                // Text editor stuff
                if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
                {
                    ted_handle_mouse_down(ted, &gui->imgui);
                }
                if (events & IMGUI_EVENT_DRAG_MOVE)
                {
                    ted_handle_mouse_drag(ted, &gui->imgui);
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

                    const TextEditorTheme theme = {
                        .font_size            = fsize,
                        .col_text_active      = C_TEXT_LIGHT_BG,
                        .col_text_inactive    = C_TEXT_LIGHT_BG,
                        .col_text_placeholder = C_TEXT_LIGHT_BG,
                        .col_selection_bg     = 0x8BD1E47F,
                        .col_ibeam            = 0xff,
                        .is_centre_aligned    = true,
                    };

                    gui->active_param_text_input = param_id;
                    pw_get_keyboard_focus(gui->pw);

                    char   text[24];
                    double value = main_get_param(gui->plugin, param_id);
                    cplug_parameterValueToString(gui->plugin, param_id, text, sizeof(text), value);

                    ted_activate(&gui->texteditor, &theme, dimensions, &pos, text);
                }
            }

            // Draw
            if (gui->active_param_text_input == param_id)
            {
                ted_draw(&gui->texteditor, gui->frame_start_time, "", true);
            }
            else
            {
                char   label[24];
                double value = main_get_param(p, param_id);
                cplug_parameterValueToString(p, param_id, label, sizeof(label), value);

                xvg_draw_text(xvg, param_cx, lm->cy_param_value, label, NULL, fsize, XVG_ALIGN_CC, C_TEXT_LIGHT_BG);
            }
        }

        // Mod amount controls
        const float mod_handle_offset = lm->knob_outer_radius + 10 * lm->scale_y;

        unsigned    mod_amt_events[ARRLEN(lm->param_positions_cx)][2] = {0};
        const float mod_amt_radius                                    = lm->param_scale * PARAM_MOD_AMOUNT_RADIUS;
        const float mod_amt_gap                                       = lm->param_scale * 10;
        const float mod_amt_stroke_width                              = lm->param_scale * 4;
        const float mod_amt_cx_delta                                  = 0.5f * mod_amt_gap + mod_amt_radius;

        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            float         param_cx = lm->param_positions_cx[i];
            const ParamID param_id = param_ids[i];

            const float mod_amt_cx[2] = {
                lm->param_positions_cx[i] - mod_amt_cx_delta,
                lm->param_positions_cx[i] + mod_amt_cx_delta,
            };

            const xvec2f modamt = p->lfo_mod_amounts[param_id];

            for (int j = 0; j < ARRLEN(mod_amt_cx); j++)
            {
                const unsigned uid    = 'pmod' + (i * ARRLEN(mod_amt_cx)) + j;
                const imgui_pt c      = {mod_amt_cx[j], lm->cy_param_mod_amount};
                const unsigned events = imgui_get_events_circle(im, uid, c, mod_amt_radius);

                xassert(param_id < ARRLEN(mod_amt_events));
                xassert(j < ARRLEN(mod_amt_events[0]));
                mod_amt_events[param_id][j] = events;

                float* pValue = &p->lfo_mod_amounts[param_id].data[j];

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
                    float delta = im->frame.delta_scroll.y / 300;
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
                    if (im->frame.modifiers_mouse_wheel & PW_MOD_PLATFORM_KEY_CTRL)
                        delta *= 0.1;
                    if (im->frame.modifiers_mouse_wheel & PW_MOD_KEY_SHIFT)
                        delta *= 0.1;

                    *pValue = xm_clampf(*pValue + delta, -1, 1);
                }

                char label  = '1';
                label      += j;
                xvg_draw_text(bg, c.x, c.y, &label, &label + 1, fsize, XVG_ALIGN_CC_TIGHT, C_TEXT_LIGHT_BG);
                xvg_draw_circle(bg, c.x, c.y, mod_amt_radius, mod_amt_stroke_width, C_GREY_1);

                float amt = modamt.data[j];
                if (fabsf(amt) != 0)
                    xvg_draw_arc(xvg, c.x, c.y, mod_amt_radius, 0, amt, mod_amt_stroke_width, true, C_DARK_BLUE);
            }
        }

        // Parameter control
        for (int i = 0; i < ARRLEN(lm->param_positions_cx); i++)
        {
            const ParamID  param_id = param_ids[i];
            const float    param_cx = lm->param_positions_cx[i];
            const unsigned wid      = 'prm' + i;

            const xvec2f modamts = p->lfo_mod_amounts[param_id];
            const xvec2f lfo_amt = p->last_lfo_amount;

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
                    RADIUS_INNER_VALUE_ARC = 124 / 2,
                    RADIUS_OUTER_VALUE_ARC = 140 / 2,
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

                    static const unsigned stop100 = 0x40454AFF;
                    static const unsigned stop90  = 0xB7C7D7FF;
                    static const unsigned stop80  = C_BG_LIGHT;
                    float                 blur1   = r100 - r90;
                    float                 blur2   = r90 - r80;

                    XVGGradient grad_100_90 = xvg_make_shadow(stop100, stop90, 0, 0, blur1, -blur1 * 0.5, true);
                    xvg_draw_circle_with_gradient(bg, pt.x, pt.y, r100, 0, grad_100_90);

                    XVGGradient grad_90_80 = xvg_make_shadow(stop90, stop80, 0, 0, blur2, 0, true);
                    xvg_draw_circle_with_gradient(bg, pt.x, pt.y, r90, 0, grad_90_80);
                }

                // Outer knob
                {
                    const float radius_outer = roundf(RADIUS_OUTER * lm->param_scale);
                    const float outer_y      = pt.y - radius_outer;
                    const float outer_h      = radius_outer * 2;

                    const float y         = pt.y + 16 * lm->param_scale;
                    const float drop_blur = 8 * lm->param_scale;
                    XVGGradient drop      = xvg_make_shadow(0x0, 0x40, 0, 0, drop_blur, 0, false);
                    xvg_draw_circle_with_gradient(bg, pt.x, y, radius_outer + drop_blur, 0, drop);

                    const float top     = outer_y + outer_h * 0.13f;
                    const float bottom  = outer_y + outer_h * 0.84f;
                    XVGGradient lingrad = xvg_make_linear_gradient(0xD4DFEAFF, 0xB5BFC8FF, 0, top, 0, bottom);
                    xvg_draw_circle_with_gradient(bg, pt.x, pt.y, radius_outer, 0, lingrad);

                    float       y_offset = 1;
                    float       in_blur  = 2;
                    XVGGradient inner =
                        xvg_make_shadow(0xffffffcc, 0xffffff00, 0, y_offset + in_blur, in_blur, -in_blur, true);
                    xvg_draw_circle_with_gradient(bg, pt.x, pt.y, radius_outer, 0, inner);
                }

                // Inner
                const float radius_inner = RADIUS_INNER * lm->param_scale;
                const float inner_y      = pt.y - radius_inner;
                const float inner_h      = radius_inner * 2;
                const float inner_s0_y   = inner_y + inner_h * 0.16f;
                const float inner_s1_y   = inner_y + inner_h * 0.87f;

                XVGGradient inner_grad = xvg_make_linear_gradient(0xB5BFC8FF, 0xD4DFEAFF, 0, inner_s0_y, 0, inner_s1_y);
                xvg_draw_circle_with_gradient(bg, pt.x, pt.y, radius_inner, 0, inner_grad);

// Slider Tick/Notch
// Angles in turns
// 7 o'clock
#define SLIDER_START_TURN -0.4166666666666667
// 5 o'clock
#define SLIDER_END_TURN 0.4166666666666667
// end - start
#define SLIDER_LENGTH_TURN 0.8333333333333334

                const float value_norm  = cplug_normaliseParameterValue(p, i, value_d);
                const float angle_value = SLIDER_START_TURN + value_norm * SLIDER_LENGTH_TURN;

                const float angle_x = cosf(angle_value * XM_TAUf - XM_HALF_PIf);
                const float angle_y = sinf(angle_value * XM_TAUf - XM_HALF_PIf);

                float tick_radius_start = radius_inner - 10 * lm->param_scale;
                float tick_radius_end   = radius_inner * 0.4f;

                const imgui_pt pt1      = {pt.x + tick_radius_start * angle_x, pt.y + tick_radius_start * angle_y};
                const imgui_pt pt2      = {pt.x + tick_radius_end * angle_x, pt.y + tick_radius_end * angle_y};
                float          stroke_w = 6 * lm->param_scale;

                xvg_draw_line_round(xvg, pt1.x, pt1.y - 1, pt2.x, pt2.y - 1, stroke_w, 0xff);
                xvg_draw_line_round(xvg, pt1.x, pt1.y + 1, pt2.x, pt2.y + 1, stroke_w, 0xffffffff);
                xvg_draw_line_round(xvg, pt1.x, pt1.y, pt2.x, pt2.y, stroke_w, 0x242E56FF);

                // Value arc
                float arc_radius[] = {
                    roundf(RADIUS_INNER_VALUE_ARC * lm->param_scale),
                    roundf(RADIUS_OUTER_VALUE_ARC * lm->param_scale),
                };

                stroke_w = roundf(lm->param_scale * 4);
                for (int lfo_idx = 0; lfo_idx < 2; lfo_idx++)
                {
                    const bool is_modulated = fabsf(modamts.data[lfo_idx]) != 0;

                    float r = arc_radius[lfo_idx];
                    xvg_draw_arc(bg, pt.x, pt.y, r, SLIDER_START_TURN, SLIDER_END_TURN, stroke_w, true, C_GREY_1);

                    if (is_modulated)
                    {
                        xassert(param_id < ARRLEN(mod_amt_events));
                        xassert(lfo_idx < ARRLEN(mod_amt_events[0]));
                        unsigned mod_amt_event = mod_amt_events[param_id][lfo_idx];
                        if (mod_amt_event & IMGUI_EVENT_MOUSE_HOVER)
                        {
                            float mod_value_norm        = value_norm + modamts.data[lfo_idx];
                            mod_value_norm              = xm_clampf(mod_value_norm, 0, 1);
                            const float mod_angle_value = SLIDER_START_TURN + mod_value_norm * SLIDER_LENGTH_TURN;

                            xvg_draw_arc(xvg, pt.x, pt.y, r, angle_value, mod_angle_value, stroke_w, true, C_DARK_BLUE);
                        }
                        float mod_value_norm        = value_norm + modamts.data[lfo_idx] * lfo_amt.data[lfo_idx];
                        mod_value_norm              = xm_clampf(mod_value_norm, 0, 1);
                        const float mod_angle_value = SLIDER_START_TURN + mod_value_norm * SLIDER_LENGTH_TURN;

                        xvg_draw_arc(xvg, pt.x, pt.y, r, angle_value, mod_angle_value, stroke_w, true, C_DARK_BLUE);
                    }
                }

                if (modamts.u64 == 0)
                {
                    float r = arc_radius[0];
                    xvg_draw_arc(xvg, pt.x, pt.y, r, SLIDER_START_TURN, angle_value, stroke_w, true, C_GREY_2);
                }
                break;
            }
            case PARAM_INPUT_GAIN:
            case PARAM_WET:
            {
                float       param_width  = param_id == PARAM_INPUT_GAIN ? INPUT_WIDTH : WET_WIDTH;
                const float meter_width  = snapf(param_width * lm->param_scale, 2);
                const float meter_height = snapf(VERTICAL_SLIDER_HEIGHT * lm->param_scale, 2);

                imgui_rect rect;

                rect.x = roundf(param_cx - meter_width * 0.5);
                rect.r = roundf(param_cx + meter_width * 0.5);
                rect.y = roundf(lm->cy_param - meter_height * 0.5f);
                rect.b = roundf(lm->cy_param + meter_height * 0.5f);

                float w = rect.r - rect.x;
                float h = rect.b - rect.y;

                // Shadows
                {
                    float blur   = 8 * lm->param_scale;
                    float spread = 2 * lm->param_scale;

                    float offset = 4;

                    // NVGcolour top_iol = nvgHexColour(0xE9EDF1E0);
                    static const unsigned top_iol  = 0xE9EDF1BF;
                    static const unsigned top_ocol = 0xE9EDF100;

                    XVGGradient tl_shadow = xvg_make_shadow(0xFFFFFF00, 0xFFFFFF7F, 0, 0, blur, -spread, false);
                    // XVGGradient tl_shadow = xvg_make_shadow(top_ocol, top_iol, 0, 0, blur, -spread, false);
                    xvg_draw_rectangle_with_gradient(
                        bg,
                        rect.x - blur - spread - offset,
                        rect.y - blur - spread - offset,
                        w + (blur + spread) * 2,
                        h + (blur + spread) * 2,
                        4 + blur,
                        0,
                        tl_shadow);

                    static const unsigned bot_iol   = 0xABB2BABF;
                    static const unsigned bot_ocol  = 0xABB2BA00;
                    XVGGradient           br_shadow = xvg_make_shadow(0x0, 0x20, 0, 0, blur, -spread, false);
                    // XVGGradient br_shadow = xvg_make_shadow(bot_ocol, bot_iol, 0, 0, blur, -spread, false);
                    xvg_draw_rectangle_with_gradient(
                        bg,
                        rect.x - blur - spread + offset,
                        rect.y - blur - spread + offset,
                        w + (blur + spread) * 2,
                        h + (blur + spread) * 2,
                        4 + blur,
                        0,
                        br_shadow);
                }

                if (param_id == PARAM_INPUT_GAIN)
                {
                    float       rect_r     = rect.r;
                    const float icon_width = 10 * lm->param_scale;
                    float       icon_r     = rect.r + icon_width + 4 * lm->param_scale;
                    rect.r                 = icon_r;
                    uint32_t events        = imgui_get_events_rect(im, wid, &rect);
                    rect.r                 = rect_r;

                    const float ch_w = roundf(meter_width * (7.0f / 32.0f));

                    const float peak_label_height = 16 * lm->param_scale;

                    const float ch_y    = rect.y + peak_label_height;
                    const float ch_b    = rect.b - 4 * lm->param_scale;
                    const float ch_h    = ch_b - ch_y;
                    const float ch_x[2] = {
                        roundf(rect.x + meter_width * (8.0f / 32.0f)),
                        roundf(rect.r - meter_width * (8.0f / 32.0f)) - ch_w,
                    };

                    static const char* input_gain_description =
                        "Changing the input gain drastically changes the sound. For the best sound, keep the input "
                        "gain close to 0dB. Use Autogain to help you.\n\n"
                        "If your input is detected to be within a desirable range, the peak meter will show text in "
                        "green. If the input too loud or too quiet, then the text will be red. As always, trust your "
                        "ears first.";

                    tooltip_handle_events(&gui->tooltip, rect, input_gain_description, gui->frame_start_time, events);

                    double value_d = handle_param_events(gui, PARAM_INPUT_GAIN, events, ch_h);

                    static const unsigned bg_grad_stop0 = 0x2C2F35FF;
                    static const unsigned bg_grad_stop1 = 0x585E6AFF;
                    XVGGradient grad = xvg_make_linear_gradient(bg_grad_stop0, bg_grad_stop1, 0, rect.y, 0, rect.b);
                    xvg_draw_rectangle_with_gradient(bg, rect.x, rect.y, w, h, 4 * lm->param_scale, 0, grad);
                    // xvg_draw_rectangle(xvg, rect.x, rect.y, w, h, 4 * lm->param_scale, 0, 0x4A4E5AFF);

                    const float mod_amt_padding     = floorf(2 * lm->param_scale);
                    const float mod_amt_strokewidth = floorf(3 * lm->param_scale);

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

                            xvg_draw_solid_rectangle(xvg, l, y_top, r - l, y_bot - y_top, C_DARK_BLUE);
                        }
                    }

                    xvec2f peaks;
                    peaks.u64 = xt_atomic_load_u64(&p->gui_input_peak_gain);

                    // Value icon
                    {
                        float shadow_offset = 2;
                        float icon_x        = icon_r - icon_width;

                        float shadow_radius = 8;

                        float       icon_cy = xm_lerpf(value_d, ch_b, ch_y);
                        float       icon_y  = icon_cy - icon_width * 0.5f;
                        float       icon_cx = icon_x + icon_width * 0.5f;
                        XVGGradient dshadow =
                            xvg_make_shadow(0x0, 0x40, 0, 0, shadow_radius, -shadow_radius * 0.5f, false);
                        xvg_draw_triangle_with_gradient(
                            xvg,
                            icon_x - shadow_radius,
                            icon_y - shadow_radius + shadow_offset,
                            icon_width + shadow_radius * 2,
                            icon_width + shadow_radius * 2,
                            0.75f,
                            0,
                            dshadow);

                        xvg_draw_triangle(xvg, icon_x, icon_y, icon_width, icon_width, 0.75f, 0, C_GREY_2);
                    }

                    // Background
                    static const unsigned ch_grad_stop0 = 0x6C7483FF;
                    static const unsigned ch_grad_stop1 = 0x7C8493FF;
                    XVGGradient ch_bg_grad = xvg_make_linear_gradient(ch_grad_stop0, ch_grad_stop1, 0, ch_y, 0, ch_b);
                    for (int ch = 0; ch < 2; ch++)
                    {
                        float br = 2 * lm->param_scale;
                        xvg_draw_rectangle_with_gradient(bg, ch_x[ch], ch_y, ch_w, ch_h, br, 0, ch_bg_grad);
                    }

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
                    for (int ch = 0; ch < 2; ch++)
                    {
                        float peak_dB_1 = xm_fast_gain_to_dB(gui->input_gain_peaks_slow[ch]);
                        if (peak_dB_1 > RANGE_INPUT_GAIN_MIN)
                        {
                            float norm        = xm_normf(peak_dB_1, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                            float peak_height = norm * ch_h;
                            float br          = 2 * lm->param_scale;
                            float bot         = ch_b - peak_height;
                            xvg_draw_rectangle(xvg, ch_x[ch], bot, ch_w, peak_height, br, 0, C_DARK_BLUE);
                        }
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

                            // static const unsigned C_DARK_BLUE    = 0x459CB4FF;
                            // static const unsigned C_LIGHT_BLUE   = 0xACDEECFF;
                            XVGGradient dshadow = xvg_make_shadow(0x459CB400, 0xACDEEC40, 0, 0, blur, 0, false);
                            xvg_draw_rectangle_with_gradient(
                                xvg,
                                gx - blur,
                                gy - blur,
                                gw + 2 * blur,
                                gh + 2 * blur,
                                blur * 2,
                                0,
                                dshadow);
                        }
                    }

                    // Foreground realtime peak
                    for (int ch = 0; ch < 2; ch++)
                    {
                        if (rt_peak_dB[ch] > RANGE_INPUT_GAIN_MIN)
                        {
                            xvg_draw_rectangle(xvg, ch_x[ch], rt_peak_y[ch], ch_w, rt_peak_h[ch], 2, 0, C_LIGHT_BLUE);
                        }
                    }

                    // 0dB notch
                    float zero_dB_pos = xm_normf(0, RANGE_INPUT_GAIN_MIN, RANGE_INPUT_GAIN_MAX);
                    float zero_dB_y   = ch_b - zero_dB_pos * ch_h;
                    xvg_draw_solid_rectangle_with_gradient(xvg, rect.x, zero_dB_y, w, 1, grad);

                    // Peak label + gain suggestion
                    float peak_dB    = xm_maxf(gui->input_gain_peaks_slow[0], gui->input_gain_peaks_slow[1]);
                    peak_dB          = xm_maxf(peak_dB, gui->input_gain_peaks_fast[0]);
                    peak_dB          = xm_maxf(peak_dB, gui->input_gain_peaks_fast[1]);
                    peak_dB          = xm_fast_gain_to_dB(peak_dB);
                    const float ninf = -(INFINITY);
                    if (peak_dB < -120)
                        peak_dB = ninf;

                    unsigned txt_col = 0;
                    if (peak_dB >= -5 && peak_dB <= 1)
                        txt_col = C_GREEN;
                    else if (peak_dB == ninf)
                        txt_col = C_GREY_2;
                    else
                        txt_col = C_RED;

                    char peak_label[16];
                    xfmt(peak_label, 0, "%.2f", peak_dB);
                    float cx    = (rect.x + rect.r) * 0.5f;
                    float txt_y = rect.y + peak_label_height * 0.5f;
                    xvg_draw_text(xvg, cx, txt_y, peak_label, NULL, 8 * lm->param_scale, XVG_ALIGN_CC, txt_col);
                }
                else // param_id == PARAM_WET || param_id == PARAM_OUTPUT_GAIN
                {
                    // BG
                    float br = 4 * lm->param_scale;
                    xvg_draw_rectangle(bg, rect.x, rect.y, meter_width, meter_height, br, 0, C_BG_LIGHT);

                    // Inner shadow
                    const float blur1   = 4; // * lm->param_scale; // Doesn't look great scaled
                    XVGGradient ishadow = xvg_make_shadow(0x70, 0x0, 0, 2, blur1, -blur1, true);
                    xvg_draw_rectangle_with_gradient(bg, rect.x, rect.y, meter_width, meter_height, br, 0, ishadow);
                    float bot_offset = -2 - blur1 * 0.5;
                    ishadow = xvg_make_shadow(0xffffff70, 0xffffff00, bot_offset, bot_offset, blur1, -blur1, true);
                    xvg_draw_rectangle_with_gradient(bg, rect.x, rect.y, meter_width, meter_height, br, 0, ishadow);

                    imgui_rect handle  = rect;
                    handle.x          += 2; // padding
                    handle.y          += 2;
                    handle.r          -= 2;
                    handle.b          -= 2;
                    w                  = handle.r - handle.x;
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

                    float notch_x = handle.x + w * 0.25f;
                    float notch_w = w * 0.5f;
                    for (int n = 1; n < NOTCH_COUNT - 1; n++)
                    {
                        float y = floorf(drag_y + n * y_inc);
                        xvg_draw_solid_rectangle(bg, notch_x, y, notch_w, 1, C_GREY_1);
                    }
                    notch_x = handle.x + w * 0.125;
                    notch_w = w * 0.75f;

                    float top_y = floorf(drag_y) + 0.5f;
                    float bot_y = floorf(drag_b) + 0.5f;
                    xvg_draw_solid_rectangle(bg, notch_x, top_y, notch_w, 1, C_GREY_1);
                    xvg_draw_solid_rectangle(bg, notch_x, bot_y, notch_w, 1, C_GREY_1);

                    // Handle drop shadow
                    float handle_cy = xm_lerpf(value_d, drag_b, drag_y);
                    handle.y        = handle_cy - w * 0.5f;

                    const float blur2        = 4 * lm->param_scale;
                    float       handle_w     = handle.r - handle.x;
                    XVGGradient handle_dshad = xvg_make_shadow(0x0, 0x99, 0, 0, blur2, 0, false);
                    xvg_draw_rectangle_with_gradient(
                        xvg,
                        handle.x - blur2 + 1,
                        handle.y - blur2 + 3,
                        handle_w + blur2 * 2,
                        handle_w + blur2 * 2,
                        blur2,
                        0,
                        handle_dshad);

                    // Handle BG
                    float s1_y = handle_cy - w * 0.35;
                    float s2_y = handle_cy + w * 0.35;

                    XVGGradient handle_bg = xvg_make_linear_gradient(0xB5BFC8FF, 0xD5DFEAFF, 0, s1_y, 0, s2_y);
                    float       hbr       = 4 * lm->param_scale;
                    xvg_draw_rectangle_with_gradient(xvg, handle.x, handle.y, handle_w, handle_w, hbr, 0, handle_bg);
                    // Top inner shadow
                    XVGGradient handle_ishad =
                        xvg_make_shadow(0xffffff7c, 0xffffff00, 0, blur2 + 2, blur2, -blur2, true);
                    xvg_draw_rectangle_with_gradient(xvg, handle.x, handle.y, handle_w, handle_w, hbr, 0, handle_ishad);
                    // Bottom inner shadow
                    handle_ishad = xvg_make_shadow(0x40, 0x0, 0, -blur2 - 2, blur2, -blur2, true);
                    xvg_draw_rectangle_with_gradient(xvg, handle.x, handle.y, handle_w, handle_w, hbr, 0, handle_ishad);

                    // Handle notch
                    int snapped_y = (int)handle_cy;

                    xvg_draw_solid_rectangle(xvg, notch_x, snapped_y, notch_w, 1, 0x242E56FF);
                    xvg_draw_solid_rectangle(xvg, notch_x, snapped_y - 1, notch_w, 1, 0x9199A0FF); // shadow
                    xvg_draw_solid_rectangle(xvg, notch_x, snapped_y + 1, notch_w, 1, 0xDCE2E9FF); // shadow

                    const float mod_amt_padding     = 4;
                    const float mod_amt_strokewidth = 3;
                    const float mod_amt_delta       = mod_amt_padding + mod_amt_strokewidth;

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

                            xvg_draw_solid_rectangle(xvg, l, y_top, r - l, y_bot - y_top, C_DARK_BLUE);
                        }
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
    xvg_command_custom(bg, gui, do_knob_shader, XVG_LABEL("Knob shader"));
    // * /

    //     const float peak_gain = p->gui_output_peak_gain;
    //     if (peak_gain > 1)
    //     {
    //         nvgSetTextAlign(nvg, NVG_ALIGN_BR);
    //         nvgSetColour(nvg, nvgRGBAf(1, 0.1, 0.1, 1));
    //         float dB = xm_fast_gain_to_dB(peak_gain);
    //         char  label[48];
    //         xfmt(label, 0, "[WARNING] Auto hardclipper: ON. %.2fdB", dB);
    //         nvgText(nvg, lm->width - 20, gui_height - 20, label, NULL);
    //     }

    // #ifdef CPLUG_BUILD_STANDALONE
    //     {
    //         Plugin* p = p;
    //         // plot_expander(nvg, lm->width, gui_height);
    //         // plot_peak_detection(nvg, lm->width, gui_height);
    //         // plot_peak_distortion(nvg, im, lm->width, gui_height);
    //         // plot_peak_upwards_compression(nvg, im, lm->width, gui_height);
    //         float midi  = xt_atomic_load_f32(&p->gui_osc_midi);
    //         float phase = xt_atomic_load_f32(&p->gui_osc_phase);
    //         plot_oscilloscope(nvg, lm->width - 230, 10, 220, 180, p->sample_rate, midi, phase);

    //         imgui_rect  rect   = {lm->width - 220, 10, lm->width - 60, 25};
    //         const float offset = 10 + (rect.b - rect.y);
    //         im_slider(nvg, im, rect, &g_output_gain_dB, -24, 0, "%.2fdB", "Output");
    //         // rect.y += offset;
    //         // rect.b += offset;
    //         // im_slider(nvg, im, rect, &g_attack_ms, 0, 50, "%.2fms", "Attack");
    //         // rect.y += offset;
    //         // rect.b += offset;
    //         // im_slider(nvg, im, rect, &g_release_ms, 0, 50, "%.2fms", "Release");
    //         // rect.y += offset;
    //         // rect.b += offset;
    //         // im_slider(nvg, im, rect, &g_lp_Q, 0.01, 10, "%.3f", "LP Q");
    //         // rect.y += offset;
    //         // rect.b += offset;
    //         // im_slider(nvg, im, rect, &g_hp_Q, 0.05, 2, "%.3f", "HP Q");
    //     }
    // #endif

    // LFO toggle button
    {
        imgui_rect rect = gui->lfo_toggle_button;
        // snvg_command_draw_nvg(nvg, XVG_LABEL("ayy lmao"));

        bool lfo_open = p->lfo_section_open;

        if (lfo_open) // section seperator
        {
            float       y = rect.b - 4;
            float       b = rect.b;
            XVGGradient g = xvg_make_linear_gradient(0x0, 0x40, 0, y, 0, b);
            xvg_draw_solid_rectangle_with_gradient(bg, lm->content_x, y, lm->content_r - lm->content_x, b - y, g);
        }

        // Inlet
        float h = rect.b - rect.y;
        float w = rect.r - rect.x;
        {
            // Note: nanovg doesn't have a great way to make a rounded rectangle that looks like this:
            //  _______
            // /       \\
            // ----------
            const float radius = lm->param_scale * 12;
            XVGGradient g      = {.colour1 = C_BG_LIGHT};
            xvg_draw_rectangle_with_gradient_ex(bg, rect.x, rect.y, w, h, radius, 0, radius, 0, 0, g);
            float blur = 6;
            g          = xvg_make_shadow(0x40, 0x0, 0, 0, blur, -blur, true);
            xvg_draw_rectangle_with_gradient_ex(bg, rect.x, rect.y, w, h, radius, 0, radius, 0, 0, g);
        }

        float cy            = (rect.y + rect.b) * 0.5f;
        float inner_padding = 12 * lm->param_scale;
        float fsize         = lm->param_scale * 12;
        xvg_draw_text(bg, rect.x + inner_padding, cy, "LFO", 0, fsize, XVG_ALIGN_CL, C_TEXT_LIGHT_BG);

        // Arrow
        float tri_half_width = 5 * lm->param_scale;
        float y1             = cy + tri_half_width * (1.0f / 3.0f);
        float y2             = cy - tri_half_width * (2.0f / 3.0f);
        if (!lfo_open)
        {
            float tmp = y1;
            y1        = y2;
            y2        = tmp;
        }
        y1           += 1;
        y2           += 1;
        float x1      = rect.r - inner_padding;
        float x2      = rect.r - inner_padding - tri_half_width;
        float x3      = rect.r - inner_padding - tri_half_width * 2;
        float stroke  = 2 * lm->param_scale;
        xvg_draw_line_round(bg, x1, y1, x2, y2, stroke, C_TEXT_LIGHT_BG);
        xvg_draw_line_round(bg, x2, y2, x3, y1, stroke, C_TEXT_LIGHT_BG);
        unsigned events = imgui_get_events_rect(im, 'lopn', &rect);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
    }

    if (p->lfo_section_open)
    {
        extern void draw_lfo_section(GUI*);
        draw_lfo_section(gui);

        // Test dithering quality on gradeints
        // {
        //     float top = lm->top_content_bottom;
        //     float bot = lm->height;

        //     float ch0 = 0.3;
        //     float ch1 = 0.1;

        //     NVGcolour col0 = {ch0, ch0, ch0, 1};
        //     NVGcolour col1 = {ch1, ch1, ch1, 1};

        //     nvgBeginPath(nvg);
        //     nvgRect2(nvg, 0, top, lm->width, bot);
        //     NVGpaint paint = nvgLinearGradient(nvg, 0, top, 0, bot, col0, col1);
        //     nvgSetPaint(nvg, paint);
        //     nvgFill(nvg);
        // }
    }

    // Footer bottom left
    {
        const float checkbox_height = floorf(12 * lm->param_scale);
        float       fsize           = checkbox_height;

        // Autogain
        imgui_rect rect;
        rect.x = 16;
        rect.y = lm->content_b;
        rect.r = rect.x + 96 * lm->param_scale;
        rect.b = lm->height;

        unsigned events = imgui_get_events_rect(im, 'auto', &rect);

        static const char* DESCRIPTION_AUTOGAIN = "When Autogain is on it adjusts the input gain to a stable level "
                                                  "that delivers a consistent sound inside Scream's "
                                                  "internal saturation and feedback loop";
        tooltip_handle_events(&gui->tooltip, rect, DESCRIPTION_AUTOGAIN, gui->frame_start_time, events);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            p->autogain_on ^= 1;

        float cy          = rect_cy(&rect);
        bool  autogain_on = p->autogain_on;
        xvg_draw_text(bg, rect.x, cy, "AUTOGAIN", NULL, fsize, XVG_ALIGN_CL, C_TEXT_DARK_BG);
        draw_checkbox(bg, checkbox_height, cy, rect.r, lm->param_scale, autogain_on);

        // Keytracking
        rect.x = rect.r + BORDER_PADDING * 4;
        rect.r = rect.x + 152 * lm->param_scale;

        events = imgui_get_events_rect(im, 'ktrk', &rect);

        static const char* DESCRIPTION_KEYTRACKING =
            "When MIDI keytracking is on, the filters cutoff position will be offset relative to the last MIDI note "
            "sent to the plugin. This feature likely requires routing MIDI to this plugin inside your DAW.";
        tooltip_handle_events(&gui->tooltip, rect, DESCRIPTION_KEYTRACKING, gui->frame_start_time, events);
        if (events & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
        if (events & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            p->midi_keytracking_on ^= 1;

        bool midi_keytracking_on = p->midi_keytracking_on;
        xvg_draw_text(bg, rect.x, cy, "MIDI KEYTRACKING", NULL, fsize, XVG_ALIGN_CL, C_TEXT_DARK_BG);
        draw_checkbox(bg, checkbox_height, cy, rect.r, lm->param_scale, midi_keytracking_on);
    }

    // Footer bottom right
    {
        char text[128] = {0};
        int  len       = 0;

        // Show window dimensions w/h on resize
        uint64_t time_since_creation_ns = gui->frame_start_time - gui->gui_create_time;
        uint64_t time_since_resize_ns   = gui->frame_start_time - gui->last_resize_time;
        uint64_t threshold_1sec         = 1000000000;
        uint64_t threshold_1_2sec       = 1200000000;
        if (time_since_resize_ns < threshold_1sec && time_since_creation_ns > threshold_1_2sec)
        {
            len = xfmt(text, len, "%dx%d", lm->width, lm->height);
        }
        else
        {
#if defined(_WIN32)
#define PLATFORM_NAME "Windows"
#elif defined(__APPLE__)
#define PLATFORM_NAME "macOS"
#endif

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
            len = xfmt(text, len, "v%s | %s | %s", CPLUG_PLUGIN_VERSION, plugin_type_name, os_name);
        }
        float fsize = 12 * lm->param_scale;
        xvg_draw_text(bg, lm->width - 8, lm->height - 8, text, text + len, fsize, XVG_ALIGN_BR, C_TEXT_DARK_BG);
    }

    // Logos
    {
        // Cure Audio logo
        float x, y, w, h, b, img_scale;
        x = BORDER_PADDING * 2;
        y = BORDER_PADDING;
        b = lm->content_y - BORDER_PADDING;
        w = h             = b - y;
        imgui_rect rect   = {x, y, x + w, y + h};
        unsigned   e_logo = imgui_get_events_rect(im, 'cure', &rect);
        if (e_logo & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
        if (e_logo & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            click_curelogo = true;

        bool hover = !!(e_logo & IMGUI_EVENT_MOUSE_HOVER);

        xvec4i      icon  = icon_get_coords(gui, ICON_CURE_AUDIO, w, h);
        XVGGradient ifill = {.colour1 = C_TEXT_DARK_BG};
        if (hover)
        {
            ifill = xvg_make_linear_gradient(C_WHITE, 0xb3b3b3ff, 0, y, 0, b);
        }
        xvg_gradient_apply_image(
            &ifill,
            gui->icons.view,
            xvg->xvg->smp_linear,
            icon.x,
            icon.y,
            icon.width,
            icon.height);
        xvg_draw_solid_rectangle_with_gradient(xvg, x, y, w, h, ifill);

        // Exacoustics logo
        rect.x += w + BORDER_PADDING;
        rect.r += w + BORDER_PADDING;
        e_logo  = imgui_get_events_rect(im, 'exac', &rect);
        if (e_logo & IMGUI_EVENT_MOUSE_ENTER)
            pw_set_mouse_cursor(gui->pw, PW_CURSOR_HAND_POINT);
        if (e_logo & IMGUI_EVENT_MOUSE_LEFT_DOWN)
            click_exaclogo = true;

        hover                  = !!(e_logo & IMGUI_EVENT_MOUSE_HOVER);
        icon                   = icon_get_coords(gui, hover ? ICON_EXACOUSTICS_COLOUR : ICON_EXACOUSTICS_WHITE, w, h);
        const int   offset_a   = 96;
        const int   width_a    = icon.width - offset_a;
        const float proportion = (float)offset_a / 128.0f;

        ifill = (XVGGradient){.colour1 = hover ? C_WHITE : C_TEXT_DARK_BG};
        xvg_gradient_apply_image(
            &ifill,
            gui->icons.view,
            xvg->xvg->smp_linear,
            icon.x,
            icon.y,
            icon.width,
            icon.height);
        xvg_draw_solid_rectangle_with_gradient(xvg, rect.x, y, w, h, ifill);
    }

    // extern float g_pd_threshold;
    // imgui_rect   threshold_rect = {20, 40, 220, 60};
    // im_slider(nvg, im, threshold_rect, &g_pd_threshold, -96, -36, "%.2fdB", "Threshold");

#ifdef SYNTH_HUD
    if (calls_synth_hud.start)
        snvg_calls_join(nvg, &calls_synth_hud);
#endif // SYNTH_HUD

    if (gui->tooltip.state.text)
    {
        tooltip_draw(&gui->tooltip, xvg, gui->arena, gui->frame_start_time, lm->width, lm->height, lm->param_scale);
    }

    // For looking at the text atlas (debugging)
    // xvg_draw_rectangle_with_gradient(xvg, 0, 0, lm->width, lm->height, 0, 0,
    // xvg_make_image_fill(xvg->text.atlases[0].img_view, xvg->smp_linear, 0, 0, 256, 256, 0xffffffff));

    // FPS HUD
#ifdef SHOW_FPS
    {
        gui->frame_diff_running_sum              -= gui->frame_diff_arr[gui->frame_diff_idx];
        gui->frame_diff_running_sum              += frame_duration_last_frame;
        gui->frame_diff_arr[gui->frame_diff_idx]  = frame_duration_last_frame;
        if (++gui->frame_diff_idx >= ARRLEN(gui->frame_diff_arr))
            gui->frame_diff_idx = 0;

        uint64_t avg_frame_time_duration_ns = gui->frame_diff_running_sum / ARRLEN(gui->frame_diff_arr);

        uint64_t max_frame_time_ns = 16666666; // 1/60th of a second, in nanoseconds

        // limit accuracy from nanoseconds to approximately microseconds
        // uint64_t cpu_numerator   = frame_time_duration_ns >> 10; // fast integer divide by 1024
        uint64_t cpu_numerator   = avg_frame_time_duration_ns >> 10; // fast integer divide by 1024
        uint64_t cpu_denominator = max_frame_time_ns >> 10;          // fast integer divide by 1024

        double cpu_amt           = (double)cpu_numerator / (double)cpu_denominator;
        double avg_frame_time_ms = (double)cpu_numerator * 1024e-6;                 // correct for 1024 int 'division'
        double frame_time_ms = (double)(frame_duration_last_frame >> 10) * 1024e-6; // correct for 1024 int 'division'
        // double approx_fps    = 1000 / frame_time_ms; // Potential FPS

        // uint64_t actual
        // uint64_t diff_last_frame = frame_time_end - gui->frame_end_time;
        // gui->frame_end_time      = frame_time_end;
        // double actual_fps = 1000.0 / ((frame_duration_last_frame >> 10) * 1024e-6);
        double actual_fps = 1000.0 / ((avg_frame_time_duration_ns >> 10) * 1024e-6);

        char text[96] = {0};
        int  len      = xfmt(
            text,
            0,
            "GUI AVG CPU: %.2lf%%\nAVG Frame Time: %.3lfms.\nFrame Time: %.3lfms.\nMax FPS: %.lf",
            (cpu_amt * 100),
            avg_frame_time_ms,
            frame_time_ms,
            actual_fps);
        xassert(len < sizeof(text) - 1);

        // if (p->audio_cpu_usage)
        // {
        //     len += xfmt(text, 0, "\nAudio CPU: %.2f%%", p->audio_cpu_usage * 100);

        //     uint64_t audio_time_ns = p->audio_process_time;
        //     double   audio_time_ms = xtime_convert_ns_to_ms(audio_time_ns);

        //     len += xfmt(text, 0, "\nAudio Time: %.3fms", audio_time_ms);
        //     len += 0;
        // }
        // nvgText(nvg, 8, 8, text, text + len);

        xvg_draw_text_ex(xvg, 8, 8, text, text + len, 14, XVG_ALIGN_TL, 0xff007fff, 0, 1.5);
    }
#endif // SHOW_FPS

    unsigned e_bg = imgui_get_events_rect(im, 'bg', &(imgui_rect){0, 0, lm->width, lm->height});
    if (e_bg & IMGUI_EVENT_MOUSE_ENTER)
    {
        pw_set_mouse_cursor(gui->pw, PW_CURSOR_DEFAULT);
    }

    xvg_command_end_pass(bg, XVG_LABEL("swapchain-pass-end"));
    xvg_command_end_pass(gui->xvg_anim, XVG_LABEL("swapchain-pass-end"));

    icons_end_frame(&gui->icons);
    xvg_end_frame(&gui->xvg);

    bool bg_match = do_bg_command_lists_match(gui);
    if (!bg_match) // redraw the background
    {
        xvg_command_list_end_frame(bg, gui->plugin->width, gui->plugin->height);
    }
    xvg_command_list_end_frame(xvg, gui->plugin->width, gui->plugin->height);

    sg_commit();
    resources_end_frame(&gui->resource_manager, &gui->xvg);
    imgui_end_frame(&gui->imgui);
    sg_set_global(NULL);
    LINKED_ARENA_LEAK_DETECT_END(gui->arena);

#ifdef SHOW_FPS
    gui->frame_end_time = xtime_now_ns();
#endif

    // TODO: d3d->present() happens after this tick() function exits, so this hyperlink stuff is still blocking call.
    //       PW either needs a pw_preset() function added to its API, or I need to finally add retained mode click
    //       callbacks to my IMGUI lib. The latter is probably the best option, since I need this for native file
    //       browser stuff and file drag stuff anyway
    if (click_curelogo)
        open_hyperlink("https://cure.audio");
    if (click_exaclogo)
        open_hyperlink("https://exacoustics.com");
}
