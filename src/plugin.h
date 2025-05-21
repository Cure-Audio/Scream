#pragma once
#include "common.h"
#include <cplug.h>
#include <xhl/thread.h>

#include "param_smoothing.h"

typedef enum ParamID
{
    PARAM_CUTOFF,
    PARAM_SCREAM,
    PARAM_RESONANCE,
    PARAM_INPUT_GAIN,
    PARAM_WET,
} ParamID;

#define RANGE_INPUT_GAIN_MIN -72.0
#define RANGE_INPUT_GAIN_MAX 24.0

enum
{
    NUM_PARAMS = PARAM_WET + 1,

    GUI_INIT_WIDTH  = 900,
    GUI_INIT_HEIGHT = GUI_INIT_WIDTH / 2,

    GUI_RATIO_X = 2,
    GUI_RATIO_Y = 1,

    GUI_MIN_WIDTH  = GUI_RATIO_X * 100,
    GUI_MIN_HEIGHT = GUI_RATIO_Y * 100,

    EVENT_QUEUE_SIZE = 256,
    EVENT_QUEUE_MASK = 255,
};

typedef enum EventType
{
    EVENT_SET_PARAMETER = 16,
    EVENT_SET_PARAMETER_NOTIFYING_HOST,

    // Helps to stop self oscillation
    EVENT_PANIC_BUTTON_PRESSED,
} EventType;

typedef struct Plugin
{
    CplugHostContext* cplug_ctx;

    // Retained data for GUI
    void* gui;
    int   width, height;

    // two floats, stored as a u64
    xt_atomic_uint64_t gui_input_peak_gain;

    xt_atomic_float gui_output_peak_gain;
    xt_atomic_float gui_osc_phase;
    xt_atomic_float gui_osc_midi;

    double main_params[NUM_PARAMS];
    double audio_params[NUM_PARAMS];

    // Plugin data
    double   sample_rate;
    uint32_t max_block_size;

    struct FilterState
    {
        SmoothedValue values[NUM_PARAMS];

        float fb_yn_1;
        float peak_xn_1;

        float lp[2];
        float hp[2];
    } state[2];

    // Event stuff

    // SPSC queue
    cplug_atomic_i32 queue_audio_head;
    cplug_atomic_i32 queue_audio_tail;
    union CplugEvent queue_audio_events[EVENT_QUEUE_SIZE];

    // MPSC queue
    union
    {
        void*         _queue_main_spinlock_aligner;
        xt_spinlock_t queue_main_spinlock;
    };
    xt_atomic_uint32_t queue_main_head;
    uint32_t           queue_main_tail;
    union CplugEvent   queue_main_events[EVENT_QUEUE_SIZE];
} Plugin;

enum GlobalEvent
{
    GLOBAL_EVENT_DEQUEUE_MAIN,
};
// [Any thread] Post enum to MPSC queue.
void send_to_global_event_queue(enum GlobalEvent, Plugin*);
// [Main thread]
void dequeue_global_events();
void main_dequeue_events(Plugin* p);

bool is_main_thread();

// [Main thread]
void send_to_audio_event_queue(Plugin* p, const CplugEvent event);
// [Any thread]
void send_to_main_event_queue(Plugin* p, const CplugEvent event);

// [Main thread]
void param_change_begin(Plugin* p, ParamID id);
void param_change_end(Plugin* p, ParamID id);
void param_change_update(Plugin* p, ParamID id, double value);
// Calls begin > update > end
void param_set(Plugin* p, ParamID id, double value);

#ifndef NDEBUG
static const char* PARAM_STR[] = {
    "PARAM_CUTOFF",
    "PARAM_SCREAM",
    "PARAM_RESONANCE",
    "PARAM_INPUT_GAIN",
    "PARAM_WET",
};
_Static_assert(ARRLEN(PARAM_STR) == NUM_PARAMS, "");
#endif