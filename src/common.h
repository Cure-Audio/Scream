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

#define CPLUG_CLAP_ID          "com.cureaudio.skibidiscream"
#define CPLUG_CLAP_DESCRIPTION "skibidiscream"
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

#define XFILES_ASSERT xassert

#define PW_MALLOC xmalloc
#define PW_FREE   xfree

enum
{
    GUI_INIT_WIDTH  = 800,
    GUI_INIT_HEIGHT = GUI_INIT_WIDTH / 2,

    GUI_RATIO_X = 2,
    GUI_RATIO_Y = 1,
};

#endif // PLUGIN_CONFIG_H
