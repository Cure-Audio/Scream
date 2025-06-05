
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
#include <stb_image.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <knob.glsl.h>
#include <texquad.glsl.h>

typedef struct
{
    float   x, y;
    int16_t u, v;
} vertex_t;

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

    sg_pipeline knob_pip;
    sg_buffer   knob_vbo;
    sg_buffer   knob_ibo;

    // TODO: fix whatever is wrong with NanoVG sokol so we can use that for drawing the logo...
    sg_pipeline logo_pip;
    sg_buffer   logo_vbo;
    sg_buffer   logo_ibo;
    sg_image    logo_img;
    sg_sampler  logo_smp;

    // int         logo_img_id;
    int logo_img_width;
    int logo_img_height;
} GUI;

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
sg_image sg_make_image_with_mipmaps(_sg_state_t* sg, const sg_image_desc* desc_)
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

    sg_image img = sg_make_image(sg, &desc);
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

    {
        char name[128] = {0};
        p->cplug_ctx->getHostName(p->cplug_ctx, name, sizeof(name)); // eg. "Ableton 12.1.1"
        int ret = strncmp(name, "Ableton", ARRLEN("Ableton") - 1);   // Ignore version number

        const bool is_ableton = ret == 0;
        p->is_ableton_vst3    = is_ableton && p->cplug_ctx->type == CPLUG_PLUGIN_IS_VST3;
    }

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

    gui->nvg = nvgCreateSokol(gui->sg, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
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
        }
        while (font_id == -1 && i < ARRLEN(font_paths));

        gui->font_id = font_id;
    }

    gui->scale = (float)gui->plugin->width / (float)GUI_INIT_WIDTH;

    // Knob shader
    {
        gui->knob_vbo = sg_make_buffer(
            gui->sg,
            &(sg_buffer_desc){
                .type  = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_STREAM,
                .size  = sizeof(vertex_t) * 4 * 3,
                .label = "knob-vertices"});

        // clang-format off
        static const uint16_t KNOB_INDICES[] = {
            0, 1, 2,  0, 2,  3,
            4, 5, 6,  4, 6,  7,
            8, 9, 10, 8, 10, 11,
        };
        _Static_assert(ARRLEN(KNOB_INDICES) == (3 * 6), "");
        // clang-format on

        gui->knob_ibo = sg_make_buffer(
            gui->sg,
            &(sg_buffer_desc){
                .type  = SG_BUFFERTYPE_INDEXBUFFER,
                .usage = SG_USAGE_IMMUTABLE,
                .data  = SG_RANGE(KNOB_INDICES),
                .size  = sizeof(KNOB_INDICES),
                .label = "knob-indices"});

        sg_shader shd = sg_make_shader(gui->sg, knob_shader_desc(sg_query_backend(gui->sg)));
        gui->knob_pip = sg_make_pipeline(
            gui->sg,
            &(sg_pipeline_desc){
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

        gui->logo_vbo = sg_make_buffer(
            gui->sg,
            &(sg_buffer_desc){
                .type  = SG_BUFFERTYPE_VERTEXBUFFER,
                .usage = SG_USAGE_STREAM,
                .size  = sizeof(vertex_t) * 4,
                .label = "logo-vertices"});

        gui->logo_ibo = sg_make_buffer(
            gui->sg,
            &(sg_buffer_desc){.type = SG_BUFFERTYPE_INDEXBUFFER, .data = SG_RANGE(indices), .label = "logo-indices"});

        sg_shader shd = sg_make_shader(gui->sg, texquad_shader_desc(sg_query_backend(gui->sg)));
        gui->logo_pip = sg_make_pipeline(
            gui->sg,
            &(sg_pipeline_desc){
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
        gui->logo_smp = sg_make_sampler(
            gui->sg,
            &(sg_sampler_desc){
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
            stbi_set_flip_vertically_on_load(1);

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

                gui->logo_img = sg_make_image_with_mipmaps(
                    gui->sg,
                    &(sg_image_desc){
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

                gui->logo_img_width  = x;
                gui->logo_img_height = y;
            }

            XFILES_FREE(file_data);
        }
    }

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
    }
    imgui_send_event(&gui->imgui, event);

    return false;
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
    {
        param_change_end(gui->plugin, param_id);
    }
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
    }

    NVGcontext*    nvg = gui->nvg;
    imgui_context* im  = &gui->imgui;

    // Layout
    enum
    {
        HEIGHT_HEADER = 32,
        HEIGHT_FOOTER = 20,
    };

#ifdef __APPLE__
    const float content_scale = dpi * 0.5;
#else
    const float    content_scale            = dpi;
#endif

    const float height_header = HEIGHT_HEADER * content_scale;
    const float height_footer = HEIGHT_FOOTER * content_scale;

    const float content_x      = 8;
    const float content_r      = gui_width - 8;
    const float content_y      = floorf(height_header + 8);
    const float content_b      = floorf(gui_height - height_footer - 16);
    const float content_height = content_b - content_y;

#ifdef __APPLE__
    // required for text to render properly...
    nvgBeginFrame(gui->nvg, gui_width, gui_height, dpi);
#else
    nvgBeginFrame(gui->nvg, gui_width, gui_height, 1.0f);
#endif

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
        nvgFontSize(nvg, content_scale * 24);
        nvgFillColour(nvg, COLOUR_BG_LIGHT);
        nvgTextAlign(nvg, NVG_ALIGN_CC);
        nvgText(nvg, gui_width * 0.5f, height_header * 0.5f + 4, "SCREAM", NULL);

        // Sokol nanovg isn't rendering this for some reason :(
        // Logo
        // float x, y, w, h, img_scale;

        // h         = height_header - 4;
        // img_scale = h / (float)gui->logo_img_height;
        // w         = (float)gui->logo_img_width * img_scale;
        // x = gui_width - 16 - w;
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
        // draw_cure_audio_logo_fixed_svg(nvg, (28.0f / 241.0f), gui_width - 16 - 20, 2);
    }

    // Main content background
    {
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
    imgui_pt knobs_pos[3] = {0};
    float    knob_radius  = 0;
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

        const float param_scale = xm_maxf(1, xm_minf(scale_x, scale_y));

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

        knob_radius = rotary_param_diameter * 0.5f;

        for (int i = 0; i < ARRLEN(param_positions); i++)
        {
            const ParamID param_id = param_positions[i].param_id;
            const float   param_cx = param_positions[i].cx;

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
                imgui_pt pt;
                pt.x                      = param_cx;
                pt.y                      = roundf(content_y + content_height * 0.5f);
                const float slider_radius = rotary_param_diameter * 0.5f;

                xassert(param_id < ARRLEN(knobs_pos));
                knobs_pos[param_id] = pt;

                uint32_t events  = imgui_get_events_circle(im, pt, slider_radius);
                double   value_d = handle_param_events(gui, i, events, 300);

                // Inlet
                {
                    // 3 stop radial gradient
                    const float r100 = roundf(RADIUS_INLET * param_scale);
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
                const float radius_outer = roundf(RADIUS_OUTER * param_scale);
                const float outer_y      = pt.y - radius_outer;
                const float outer_h      = radius_outer * 2;

                // Outer knob outer shadow
                {
                    const float y            = pt.y + 16 * param_scale;
                    const float inner_radius = radius_outer - 8 * param_scale;
                    const float outer_radius = radius_outer + 4 * param_scale;

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
                const float radius_inner = RADIUS_INNER * param_scale;
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

                float tick_radius_start = radius_inner - 10 * param_scale;
                float tick_radius_end   = radius_inner * 0.4f;

                const imgui_pt pt1 = {pt.x + tick_radius_start * angle_x, pt.y + tick_radius_start * angle_y};
                const imgui_pt pt2 = {pt.x + tick_radius_end * angle_x, pt.y + tick_radius_end * angle_y};
                nvgStrokeWidth(nvg, 6 * param_scale);
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
                const float arc_radius = roundf(RADIUS_VALUE_ARC * param_scale);
                nvgStrokeWidth(nvg, roundf(param_scale * 4));
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
                nvgRoundedRect(nvg, rect.x, rect.y, rect.r - rect.x, rect.b - rect.y, 4 * param_scale);
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
                    nvgRoundedRect(nvg, ch_x[ch], ch_y, ch_w, ch_h, 2 * param_scale);
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
                        nvgRoundedRect(nvg, ch_x[ch], ch_b - peak_height, ch_w, peak_height, 2 * param_scale);
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
                        const float blur = 4 * param_scale;

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
            {
                const float meter_width  = snapf(METER_WIDTH * param_scale, 2);
                const float meter_height = snapf(METER_HEIGHT * param_scale, 2);

                imgui_rect rect;

                rect.x = roundf(param_cx - meter_width * 0.5);
                rect.r = roundf(param_cx + meter_width * 0.5);
                rect.y = roundf(gui_height * 0.5 - meter_height * 0.5f);
                rect.b = roundf(gui_height * 0.5 + meter_height * 0.5f);

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
                const float y_inc = (drag_height + handle_width * 0.5f) / (float)NOTCH_COUNT;

                float notch_x = slider_dimensions.x + handle_width * 0.25;
                float notch_r = slider_dimensions.x + handle_width * 0.75;
                nvgBeginPath(nvg);
                for (int n = 1; n < NOTCH_COUNT - 1; n++)
                {
                    float y = roundf(drag_y + n * y_inc) + 0.5f;
                    nvgMoveTo(nvg, notch_x, y);
                    nvgLineTo(nvg, notch_r, y);
                }
                notch_x = slider_dimensions.x + handle_width * 0.125;
                notch_r = slider_dimensions.x + handle_width * 0.875;

                float top_y = roundf(drag_y) + 0.5f;
                nvgMoveTo(nvg, notch_x, top_y);
                nvgLineTo(nvg, notch_r, top_y);
                float bot_y = roundf(drag_b) + 0.5f;
                nvgMoveTo(nvg, notch_x, bot_y);
                nvgLineTo(nvg, notch_r, bot_y);

                nvgStrokeColour(nvg, COLOUR_GREY_2);
                nvgStrokeWidth(nvg, 1);
                nvgStroke(nvg);

                // Draw handle
                float handle_cy = xm_lerpf(value_d, drag_b, drag_y);
                nvgBeginPath(nvg);
                nvgRoundedRect(
                    nvg,
                    slider_dimensions.x,
                    handle_cy - handle_width * 0.5f,
                    handle_width,
                    handle_width,
                    2);
                nvgFillColour(nvg, COLOUR_GREY_1);
                nvgFill(nvg);
                // Handle notch
                nvgBeginPath(nvg);
                nvgMoveTo(nvg, notch_x, handle_cy);
                nvgLineTo(nvg, notch_r, handle_cy);
                nvgStrokeColour(nvg, nvgHexColour(0x2C2F35FF));
                nvgStroke(nvg);
                break;
            }
            }
        }

        nvgFillColour(nvg, COLOUR_TEXT);
        nvgFontSize(nvg, 14 * content_scale * param_scale);

        const float value_y = content_y + 40 * param_scale;
        const float label_b = content_b - 40 * param_scale;

        static const char* NAMES[] = {"CUTOFF", "SCREAM", "RESONANCE", "INPUT", "WET"};
        _Static_assert(ARRLEN(NAMES) == NUM_PARAMS);
        for (int i = 0; i < ARRLEN(param_positions); i++)
        {
            const ParamID param_id = param_positions[i].param_id;
            const float   param_cx = param_positions[i].cx;

            nvgTextAlign(nvg, NVG_ALIGN_BC);
            nvgText(nvg, param_cx, label_b, NAMES[param_id], NULL);

            extern double main_get_param(Plugin * p, ParamID id);

            char   label[24];
            double value = main_get_param(gui->plugin, param_id);
            cplug_parameterValueToString(gui->plugin, param_id, label, sizeof(label), value);
            nvgTextAlign(nvg, NVG_ALIGN_TC);
            nvgText(nvg, param_cx, value_y, label, NULL);
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
        nvgText(nvg, gui_width - 20, gui_height - 20, label, NULL);
    }

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
    */

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

        nvgFontSize(nvg, 12 * content_scale);
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
        nvgText(nvg, 8, height_header * 0.5f + 4, text, text + len);

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
        len = snprintf(text, sizeof(text), "Scream %s | %s | %s", CPLUG_PLUGIN_VERSION, plugin_type_name, os_name);
        nvgText(nvg, 8, gui_height - height_footer * 0.5f - 4, text, text + len);
    }
    // #endif

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
        _Static_assert(ARRLEN(verts) / 4 == ARRLEN(knobs_pos), "");
        // clang-format on

        xassert(knob_radius != 0);
        for (int i = 0; i < ARRLEN(knobs_pos); i++)
        {
            float left   = knobs_pos[i].x - knob_radius;
            float right  = knobs_pos[i].x + knob_radius;
            float top    = knobs_pos[i].y - knob_radius;
            float bottom = knobs_pos[i].y + knob_radius;

            left   = xm_mapf(left, 0, gui_width, -1, 1);
            right  = xm_mapf(right, 0, gui_width, -1, 1);
            top    = xm_mapf(top, 0, gui_height, 1, -1);
            bottom = xm_mapf(bottom, 0, gui_height, 1, -1);

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

        sg_update_buffer(gui->sg, gui->knob_vbo, &SG_RANGE(verts));
        sg_apply_pipeline(gui->sg, gui->knob_pip);

        sg_bindings bind       = {0};
        bind.vertex_buffers[0] = gui->knob_vbo;
        bind.index_buffer      = gui->knob_ibo;
        sg_apply_bindings(gui->sg, &bind);

        xassert(sg_isvalid(gui->sg));

        sg_draw(gui->sg, 0, 6 * 3, 1);
    }

    // Logo shader
    if (gui->logo_img.id)
    {
        float x, y, w, h, img_scale;

        h         = height_header - 4;
        img_scale = h / (float)gui->logo_img_height;
        w         = (float)gui->logo_img_width * img_scale;
        x         = gui_width - 16 - w;
        // x         = 16;
        y = 4;

        float l = xm_mapf(x, 0, gui_width, -1, 1);
        float r = xm_mapf(x + w, 0, gui_width, -1, 1);
        float t = xm_mapf(y, 0, gui_height, 1, -1);
        float b = xm_mapf(y + h, 0, gui_height, 1, -1);

        // clang-format off
        vertex_t verts[] = {
            {l, t, 0,     32767},
            {r, t, 32767, 32767},
            {r, b, 32767, 0},
            {l, b, 0,     0},
        };

        sg_update_buffer(gui->sg, gui->logo_vbo, &SG_RANGE(verts));
        sg_apply_pipeline(gui->sg, gui->logo_pip);

        sg_bindings bind       = {0};
        bind.vertex_buffers[0] = gui->logo_vbo;
        bind.index_buffer      = gui->logo_ibo;
        bind.images[IMG_texquad_tex]   = gui->logo_img;
        bind.samplers[SMP_texquad_smp] = gui->logo_smp;

        sg_apply_bindings(gui->sg, &bind);
        sg_draw(gui->sg, 0, 6, 1);
    }

    sg_end_pass(gui->sg);
    sg_commit(gui->sg);

    imgui_end_frame(&gui->imgui);
}
