#define XHL_ALLOC_IMPL
#define XHL_FILES_IMPL
#define XHL_MATHS_IMPL
#define XHL_THREAD_IMPL
#define XHL_TIME_IMPL
#define XHL_STRING_IMPL
#define XHL_SYSTEM_IMPL

#ifndef NDEBUG
#define SOKOL_ASSERT(cond) (cond) ? (void)0 : __debugbreak()
#endif

#include "common.h"

#include <stdio.h>
#include <xhl/alloc.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/string.h>
#include <xhl/system.h>
#include <xhl/thread.h>
#include <xhl/time.h>

#ifdef CPLUG_BUILD_STANDALONE
#include <pffft.c>
#endif

static const UINT_PTR UNIQUE_INT_ID = (UINT_PTR)'CURE' | ((UINT_PTR)'SCRM' << 32);

static UINT_PTR g_timer                 = 0;
volatile int    g_platform_init_counter = 0;

extern void dequeue_global_events();
void        TimerFunc(HWND unnamedParam1, UINT unnamedParam2, UINT_PTR unnamedParam3, DWORD time_ms)
{
    dequeue_global_events();
}

void library_load_platform()
{
    int refcount = xt_atomic_fetch_add_i32(&g_platform_init_counter, 1);
    if (refcount == 0)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer
        g_timer = SetTimer(NULL, UNIQUE_INT_ID, 200, TimerFunc);
    }
}

void library_unload_platform()
{
    dequeue_global_events();

    int refcount = xt_atomic_fetch_add_i32(&g_platform_init_counter, 1);
    if (refcount == 1)
    {
        if (g_timer != 0)
        {
            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-killtimer
            KillTimer(NULL, g_timer);
        }
        g_timer = 0;
    }
}