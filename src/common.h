#ifndef PLUGIN_CONFIG_H
#define PLUGIN_CONFIG_H

#if !defined(_WIN32) && !defined(__APPLE__)
#error Unsupported OS
#endif

#define CPLUG_IS_INSTRUMENT 0

#define CPLUG_NUM_INPUT_BUSSES  1
#define CPLUG_NUM_OUTPUT_BUSSES 1
#define CPLUG_WANT_MIDI_INPUT   1
#define CPLUG_WANT_MIDI_OUTPUT  0

#define CPLUG_WANT_GUI      1
#define CPLUG_GUI_RESIZABLE 1

// See list of categories here: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/group__plugType.html
#define CPLUG_VST3_CATEGORIES "Fx|Filter"

#define CPLUG_VST3_TUID_COMPONENT  'Cure', 'comp', 'SCRM', 0
#define CPLUG_VST3_TUID_CONTROLLER 'Cure', 'edit', 'SCRM', 0

#define CPLUG_AUV2_VIEW_CLASS     ScreamView
#define CPLUG_AUV2_VIEW_CLASS_STR "ScreamView"

#define CPLUG_CLAP_ID          "com.cureaudio.scream"
#define CPLUG_CLAP_DESCRIPTION "Scream Filter"
#define CPLUG_CLAP_FEATURES    CLAP_PLUGIN_FEATURE_FILTER

#include <xhl/alloc.h>
#include <xhl/debug.h>

#define ARRLEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define STRLEN(str) (ARRLEN(str) - 1)

#ifndef NDEBUG
void println(const char* const fmt, ...);
#define cplug_log println
#else
#define println(...)
#endif

#if defined(_MSC_VER)
#define XALIGN(a) __declspec(align(a))
#else
#define XALIGN(a) __attribute__((aligned(a)))
#endif

#ifdef CPLUG_BUILD_STANDALONE
#define XFILES_ASSERT          xassert
#define CPLUG_LOG_ASSERT(cond) xassert((cond));
#else // !CPLUG_BUILD_STANDALONE
#define XFILES_ASSERT(cond) CPLUG_LOG_ASSERT((cond))
#endif // CPLUG_BUILD_STANDALONE

#define LOG_MALLOC(sz)    (println("malloc(%s) (%llu) - %s:%d", #sz, (sz), __FILE__, __LINE__), xmalloc(sz))
#define LOG_CALLOC(n, sz) (println("calloc(%s, %s) (%llu) - %s:%d", #n, #sz, (sz), __FILE__, __LINE__), xcalloc(n, sz))
#define LOG_REALLOC(ptr, sz)                                                                                           \
    (println("realloc(%s, %s) (%llu) - %s:%d", #ptr, #sz, (sz), __FILE__, __LINE__), xrealloc(ptr, sz))
#define LOG_FREE(ptr) (println("free(%s) (0x%p) - %s:%d", #ptr, (ptr), __FILE__, __LINE__), xfree(ptr))

#define MY_MALLOC(sz)       xmalloc(sz)
#define MY_CALLOC(n, sz)    xcalloc(n, sz)
#define MY_REALLOC(ptr, sz) xrealloc(ptr, sz)
#define MY_FREE(ptr)        xfree(ptr)

#define PW_MALLOC(sz) MY_MALLOC(sz)
#define PW_FREE(ptr)  MY_FREE(ptr)

#define NVG_MALLOC(sz)       MY_MALLOC(sz)
#define NVG_REALLOC(ptr, sz) MY_REALLOC(ptr, sz)
#define NVG_FREE(ptr)        MY_FREE(ptr)
#define NVG_ASSERT           xassert

#define STBI_MALLOC(sz)       MY_MALLOC(sz)
#define STBI_REALLOC(ptr, sz) MY_REALLOC(ptr, sz)
#define STBI_FREE(ptr)        MY_FREE(ptr)
#define STBI_ASSERT           xassert

#define XFILES_MALLOC(sz)       MY_MALLOC(sz)
#define XFILES_REALLOC(ptr, sz) MY_REALLOC(ptr, sz)
#define XFILES_FREE(ptr)        MY_FREE(ptr)

#define XARR_REALLOC(ptr, sz) MY_REALLOC(ptr, sz)
#define XARR_FREE(ptr)        MY_FREE(ptr)

// clang-format off
#define hexcol(hex) {( hex >> 24)         / 255.0f,\
                     ((hex >> 16) & 0xff) / 255.0f,\
                     ((hex >>  8) & 0xff) / 255.0f,\
                     ( hex        & 0xff) / 255.0f}
// clang-format on

#include <cplug.h>

#endif // PLUGIN_CONFIG_H
