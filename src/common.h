#ifndef PLUGIN_CONFIG_H
#define PLUGIN_CONFIG_H

#if !defined(_WIN32) && !defined(__APPLE__)
#error Unsupported OS
#endif

#define CPLUG_IS_INSTRUMENT 0

#define CPLUG_NUM_INPUT_BUSSES  1
#define CPLUG_NUM_OUTPUT_BUSSES 1
#define CPLUG_WANT_MIDI_INPUT   0
#define CPLUG_WANT_MIDI_OUTPUT  0

#define CPLUG_WANT_GUI      1
#define CPLUG_GUI_RESIZABLE 1

// See list of categories here: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/group__plugType.html
#define CPLUG_VST3_CATEGORIES "Fx|Filter"

#define CPLUG_VST3_TUID_COMPONENT  'Cure', 'comp', 'SCRM', 0
#define CPLUG_VST3_TUID_CONTROLLER 'Cure', 'edit', 'SCRM', 0

#define CPLUG_AUV2_VIEW_CLASS     ScreamTemplateView
#define CPLUG_AUV2_VIEW_CLASS_STR "ScreamTemplateView"

#define CPLUG_CLAP_ID          "com.cureaudio.scream"
#define CPLUG_CLAP_DESCRIPTION "Scream Filter"
#define CPLUG_CLAP_FEATURES    CLAP_PLUGIN_FEATURE_FILTER

#include <xhl/alloc.h>
#include <xhl/debug.h>

#define ARRLEN(arr) (sizeof(arr) / sizeof(arr[0]))

#ifndef NDEBUG
void println(const char* const fmt, ...);
#define cplug_log println
#else
#define println(...)                                                                                                   \
    {}
#endif

#ifdef CPLUG_BUILD_STANDALONE
#define XFILES_ASSERT          xassert
#define CPLUG_LOG_ASSERT(cond) xassert((cond));
#else // !CPLUG_BUILD_STANDALONE
#define XFILES_ASSERT(cond) CPLUG_LOG_ASSERT((cond))
#endif // CPLUG_BUILD_STANDALONE

#define LOG_MALLOC(sz)       (println("malloc(%s) - %s:%d", #sz, __FILE__, __LINE__), xmalloc(sz))
#define LOG_CALLOC(n, sz)    (println("calloc(%s, %s) - %s:%d", #n, #sz, __FILE__, __LINE__), xcalloc(n, sz))
#define LOG_REALLOC(ptr, sz) (println("realloc(%s, %s) - %s:%d", #ptr, #sz, __FILE__, __LINE__), xrealloc(ptr, sz))
#define LOG_FREE(ptr)        (println("free(%s) (0x%p) - %s:%d", #ptr, (ptr), __FILE__, __LINE__), xfree(ptr))

#define MY_MALLOC(sz)       xmalloc(sz)
#define MY_CALLOC(n, sz)    xcalloc(n, sz)
#define MY_REALLOC(ptr, sz) xrealloc(ptr, sz)
#define MY_FREE(ptr)        xfree(ptr)

#define PW_MALLOC(sz) MY_MALLOC(sz)
#define PW_FREE(ptr)  MY_FREE(ptr)

#define SGNVG_MALLOC(sz)       MY_MALLOC(sz)
#define SGNVG_REALLOC(ptr, sz) MY_REALLOC(ptr, sz)
#define SGNVG_FREE(ptr)        MY_FREE(ptr)
#define SGNVG_ASSERT           xassert

#define STBI_MALLOC(sz)       MY_MALLOC(sz)
#define STBI_REALLOC(ptr, sz) MY_REALLOC(ptr, sz)
#define STBI_FREE(ptr)        MY_FREE(ptr)
#define STBI_ASSERT           xassert

#define XFILES_MALLOC(sz) MY_MALLOC(sz)
#define XFILES_FREE(ptr)  MY_FREE(ptr)

#endif // PLUGIN_CONFIG_H
