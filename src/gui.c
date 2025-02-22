
#include "common.h"
#include "plugin.h"

#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/time.h>

#include <cplug_extensions/window.h>
#include <nanovg.h>
#include <nanovg_sokol.h>
#include <stdio.h>
#include <string.h>

typedef struct GUI
{
    Plugin*     plugin;
    void*       pw;
    void*       sg;
    NVGcontext* nvg;
    int         font_id;
} GUI;

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
#ifndef NDEBUG
        static char* names[] = {
            "PW_RESIZE_UNKNOWN",
            "PW_RESIZE_LEFT",
            "PW_RESIZE_RIGHT",
            "PW_RESIZE_TOP",
            "PW_RESIZE_TOPLEFT",
            "PW_RESIZE_TOPRIGHT",
            "PW_RESIZE_BOTTOM",
            "PW_RESIZE_BOTTOMLEFT",
            "PW_RESIZE_BOTTOMRIGHT",
        };
        println("resize direction: %s", names[info->constrain_size.direction]);
#endif
        uint32_t width  = info->constrain_size.width;
        uint32_t height = info->constrain_size.height;
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

void* pw_create_gui(void* _plugin, void* _pw)
{
    xassert(_plugin);
    xassert(_pw);
    Plugin* p   = _plugin;
    GUI*    gui = calloc(1, sizeof(*gui));
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

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;

    sg_shutdown(gui->sg);

    gui->plugin->gui = NULL;
    free(gui);
}

void pw_tick(void* _gui)
{
    println("tick");
    GUI* gui = _gui;
    xassert(gui->nvg);
    if (!gui->nvg)
        return;

    // Begin frame
    {
        int width  = gui->plugin->width;
        int height = gui->plugin->height;

        sg_pass_action pass_action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0.0f, 0.0f, 0.0f, 1.0f}}};

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

    nvgBeginPath(gui->nvg);
    nvgRect(gui->nvg, 20, 20, 20, 20);
    nvgFillColor(gui->nvg, nvgRGBAf(1, 0, 0, 1));
    nvgFill(gui->nvg);

    // Timer
    {
        uint64_t now  = xtime_now_ns();
        now          /= 1000000;
        double sec    = (double)now / 1000.0;
        nvgFillColor(gui->nvg, (NVGcolor){1, 1, 1, 1});
        nvgFontSize(gui->nvg, 16);
        nvgTextAlign(gui->nvg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        int  height = gui->plugin->height;
        char str[24];
        snprintf(str, sizeof(str), "%.3fsec", sec);
        nvgText(gui->nvg, 20, height - 20, str, NULL);
    }

    // End frame
    nvgEndFrame(gui->nvg);
    sg_end_pass(gui->sg);
    sg_commit(gui->sg);
}

void cb_choose_files(void* user, const char* const* paths, uint32_t num_paths)
{
    println("num_paths: %u", num_paths);
    if (num_paths)
    {
        for (int i = 0; i < num_paths; i++)
        {
            println(paths[i]);
        }
    }
}

bool pw_event(const PWEvent* event)
{
    GUI* gui = event->gui;

    if (!gui || !gui->plugin)
        return false;

    if (event->type == PW_EVENT_RESIZE)
    {
        gui->plugin->width  = event->resize.width;
        gui->plugin->height = event->resize.height;
    }
    else if (event->type == PW_EVENT_MOUSE_LEFT_DOWN)
    {
        println("click!");
        static int times_clicked = 0;
        times_clicked++;

        /* SET CLIPBOARD */
        char msg[32];
        snprintf(msg, sizeof(msg), "times_clicked: %d", times_clicked);
        pw_set_clipboard_text(gui->pw, msg);

        /* GET CLIPBOARD */
        char*  text    = NULL;
        size_t textlen = 0;
        pw_get_clipboard_text(gui->pw, &text, &textlen);
        println("Updated clipboard:\n%.*s", (int)textlen, text);
        pw_free_clipboard_text(text);

        /* BEEP */
        pw_beep();

        /* GET DPI */
        float dpi = pw_get_dpi(gui->pw);
        println("DPI: %f", dpi);

        /* GET SCREEN_SIZE */
        uint32_t screen_width = 0, screen_height = 0;
        pw_get_screen_size(&screen_width, &screen_height);
        println("Screen size: %ux%u", screen_width, screen_height);

        /* FILE SAVE */
        PWChooseFileArgs   args        = {0};
        static const char* ext_types[] = {"txt", "doc"};
        static const char* ext_names[] = {"Text Document (.txt)", "Word Document (.doc)"};
        _Static_assert(ARRLEN(ext_types) == ARRLEN(ext_names), "");

        char desktop_path[256];
        xfiles_get_user_directory(desktop_path, sizeof(desktop_path), XFILES_USER_DIRECTORY_DESKTOP);

        // args.pw              = gui->pw;
        // args.callback_data   = gui;
        // args.callback        = cb_choose_files;
        // args.is_save         = true;
        // args.num_extensions  = ARRLEN(ext_types);
        // args.extension_types = ext_types;
        // args.extension_names = ext_names;
        // args.title           = "Save my file!";
        // args.folder          = desktop_path;
        // args.filename        = "test";
        // pw_choose_file(&args);

        /* FILE OPEN */
        // memset(&args, 0, sizeof(args));
        // args.pw              = gui->pw;
        // args.callback_data   = gui;
        // args.callback        = cb_choose_files;
        // args.multiselect     = true;
        // args.num_extensions  = ARRLEN(ext_types);
        // args.extension_types = ext_types;
        // args.extension_names = ext_names;
        // args.title           = "Open my file!";
        // args.folder          = desktop_path;
        // pw_choose_file(&args);

        /* FOLDER OPEN */
        // memset(&args, 0, sizeof(args));
        // args.pw            = gui->pw;
        // args.callback_data = gui;
        // args.callback      = cb_choose_files;
        // args.is_folder     = true;
        // // args.multiselect   = true;
        // args.title  = "Open Folder!";
        // args.folder = desktop_path;
        // pw_choose_file(&args);

        pw_get_keyboard_focus(gui->pw);
        // pw_check_keyboard_focus
        // pw_release_keyboard_focus
    }
    else if (event->type == PW_EVENT_MOUSE_ENTER)
    {
        static enum PWCursorType cursor_type = 0;
        static char*             names[]     = {
            "PW_CURSOR_ARROW", // Default cursor
            "PW_CURSOR_BEAM",  // 'I' used for hovering over text
            "PW_CURSOR_NO",    // Circle with diagonal strike through
            "PW_CURSOR_CROSS", // Precision select/crosshair

            "PW_CURSOR_ARROW_DRAG", // Default cursor with copy box
            "PW_CURSOR_HAND_POINT",
            "PW_CURSOR_HAND_DRAGGABLE",
            "PW_CURSOR_HAND_DRAGGING",

            "PW_CURSOR_RESIZE_WE",
            "PW_CURSOR_RESIZE_NS",
            "PW_CURSOR_RESIZE_NESW",
            "PW_CURSOR_RESIZE_NWSE",
        };
        println("cursor: %s", names[cursor_type]);
        pw_set_mouse_cursor(gui->pw, cursor_type);

        cursor_type++;
        if (cursor_type >= ARRLEN(names))
            cursor_type = 0;
    }
    else if (event->type == PW_EVENT_KEY_FOCUS_LOST)
    {
        println("focus lost");
        xassert(pw_check_keyboard_focus(gui->pw) == false);
    }
    else if (event->type == PW_EVENT_TEXT)
    {
        int string[2] = {event->text.codepoint, 0};

        println("text %s", (char*)&string);
    }
    else if (event->type == PW_EVENT_FILE_ENTER)
    {
        for (int i = 0; i < event->file.num_paths; i++)
        {
            println("Enter %s", event->file.paths[i]);
        }
        return true;
    }
    else if (event->type == PW_EVENT_FILE_MOVE)
    {
        for (int i = 0; i < event->file.num_paths; i++)
        {
            println("Move %s", event->file.paths[i]);
        }
        return true;
    }
    else if (event->type == PW_EVENT_FILE_DROP)
    {
        for (int i = 0; i < event->file.num_paths; i++)
        {
            println("Drop %s", event->file.paths[i]);
        }
        return true;
    }
    return false;
}

bool cplug_getResizeHints(
    void*     userGUI,
    bool*     resizableX,
    bool*     resizableY,
    bool*     preserveAspectRatio,
    uint32_t* aspectRatioX,
    uint32_t* aspectRatioY)
{
    *resizableX          = true;
    *resizableY          = true;
    *preserveAspectRatio = false;

    return true;
}