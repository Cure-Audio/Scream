#define SOKOL_METAL
#define SOKOL_GFX_IMPL
#define XHL_ALLOC_IMPL
#define XHL_FILES_IMPL
#define XHL_MATHS_IMPL
#define XHL_THREAD_IMPL
#define XHL_TIME_IMPL
#define STB_IMAGE_IMPLEMENTATION

#ifndef NDEBUG
#define SOKOL_ASSERT(cond) (cond) ? (void)0 : __builtin_debugtrap()
#endif

#include "common.h"

#include "libs/sokol_gfx.h"
#include <cplug_extensions/window_osx.m>
#include <stb_image.h>

#include <stdarg.h>
#include <stdio.h>
#include <xhl/alloc.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/thread.h>
#include <xhl/time.h>

CFRunLoopTimerRef g_timer;
int               g_platform_init_counter = 0;

#ifndef NDEBUG
void println(const char* const fmt, ...)
{
    char    buf[256] = {0};
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n)
    {
        if (n < sizeof(buf) && buf[n - 1] != '\n')
        {
            buf[n] = '\n';
            n++;
        }

#ifdef CPLUG_BUILD_STANDALONE
        fwrite(buf, 1, n, stdout);
#else
        char path[1024];
        xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_DESKTOP);
        strcat(path, "/log.txt");

        FILE* f = fopen(path, "a");
        if (f)
        {
            fwrite(buf, 1, n, f);
            fclose(f);
        }
#endif
    }
}
#endif // NDEBUG

extern void dequeue_global_events();
void        g_timer_cb(CFRunLoopTimerRef timer, void* info) { dequeue_global_events(); }

void library_load_platform()
{
    g_platform_init_counter++;

    if (g_timer == NULL)
    {
        g_timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.2, 0.2, 0, 0, g_timer_cb, NULL);
        xassert(g_timer != NULL);
        if (g_timer)
            CFRunLoopAddTimer(CFRunLoopGetCurrent(), g_timer, kCFRunLoopCommonModes);
    }
}
void library_unload_platform()
{
    dequeue_global_events();

    g_platform_init_counter--;
    if (g_platform_init_counter == 0)
    {
        if (g_timer)
        {
            CFRunLoopTimerInvalidate(g_timer);
            CFRelease(g_timer);
        }
        g_timer = NULL;
    }
}
