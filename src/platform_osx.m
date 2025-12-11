#define XHL_ALLOC_IMPL
#define XHL_FILES_IMPL
#define XHL_MATHS_IMPL
#define XHL_THREAD_IMPL
#define XHL_TIME_IMPL

#ifndef NDEBUG
#define SOKOL_ASSERT(cond) (cond) ? (void)0 : __builtin_debugtrap()
#endif

#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <xhl/alloc.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/thread.h>
#include <xhl/time.h>

#ifdef CPLUG_BUILD_STANDALONE
#include <pffft.c>
#endif

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
        // static char path[1024] = {0};
        // if (strlen(path) == 0)
        // {
        //     bool ok = xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_DESKTOP);
        //     xassert(ok);
        //     strcat(path, "/log.txt");
        // }
        // bool ok = xfiles_append(path, buf, n);
        // xassert(ok);
#endif
    }
}
#endif // NDEBUG

extern void dequeue_global_events();
void        g_timer_cb(CFRunLoopTimerRef timer, void* info) { dequeue_global_events(); }

#include <Appkit/AppKit.h>

float system_get_content_scale()
{
    // Algorithm to find the actual size in pixels of the current display, without any "logical" or "physical" jargon.
    // When you look at the tech specs on your macbook device or monitor, this returns the matching resolution.
    // "Logical" and "phyical" is confusing Apple jargon
    /*
    float backingScale = 1;
    float scaleX       = 1;
    float scaleY       = 1;

    // NSScreen* mainScreen = [NSScreen mainScreen];
    // CGRect frame = [mainScreen frame];
    // backingScale = [mainScreen backingScaleFactor];

    CGDirectDisplayID displayIds[4];
    uint32_t          numDisplays = 0;
    CGError           err         = CGGetActiveDisplayList(ARRLEN(displayIds), displayIds, &numDisplays);
    if (err == kCGErrorSuccess && numDisplays > 0)
    {
        // Note: Apples docs say the first item in the list is the 'main display' ie. "the one with the menu bar"
        // CGSize sizeMillimeters = CGDisplayScreenSize(displayIds[0]);
        CGRect rectLogicalPixels = CGDisplayBounds(displayIds[0]);

        //
    https://stackoverflow.com/questions/13859109/how-to-programmatically-determine-native-pixel-resolution-of-retina-macbook-pro
        CGDirectDisplayID sid = displayIds[0];

        const void* keys[]   = {kCGDisplayShowDuplicateLowResolutionModes};
        const void* values[] = {kCFBooleanTrue};

        CFDictionaryRef options = CFDictionaryCreate(0, keys, values, ARRLEN(values), 0, 0);
        CFArrayRef      ms      = CGDisplayCopyAllDisplayModes(sid, options);
        CFIndex         n       = CFArrayGetCount(ms);

        size_t highestX = 0, highestY = 0;
        for (int i = 0; i < n; ++i)
        {
            CGDisplayModeRef m = (CGDisplayModeRef)CFArrayGetValueAtIndex(ms, i);

            size_t pixelWidth  = CGDisplayModeGetPixelWidth(m);
            size_t pixelHeight = CGDisplayModeGetPixelHeight(m);
            size_t width       = CGDisplayModeGetWidth(m);
            size_t height      = CGDisplayModeGetHeight(m);

            // println("dm #%d: %zu:%zu %zu:%zu", i, pixelWidth, pixelHeight, width, height);
            // CGDisplayModeGetIOFlags(m) & kDisplayModeNativeFlag
            if (pixelWidth == width && pixelHeight == height)
            {
                if (pixelWidth > highestX && pixelHeight > highestY)
                {
                    highestX = pixelWidth;
                    highestY = pixelHeight;
                }
            }
        }

        if (highestX && highestY)
        {
            scaleX = (float)highestX / rectLogicalPixels.size.width;
            scaleY = (float)highestY / rectLogicalPixels.size.height;

            println("size: %zu %zu", highestX, highestY);
            println("scale x/y: %f %f", scaleX, scaleY);
        }
        CFRelease(ms);
        CFRelease(options);
    }
    return scaleY;
    */
    return 1;
}

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
