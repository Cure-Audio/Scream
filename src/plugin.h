#pragma once
#include "common.h"
#include <cplug.h>
#include <linked_arena.h>
#include <stdint.h>
#include <xhl/array.h>
#include <xhl/thread.h>
#include <xhl/vector.h>

#include "libs/adsr.h"
#include <param_smoothing.h>

#include "libs/ADAA.h"

typedef enum ParamID
{
    PARAM_CUTOFF,
    PARAM_SCREAM,
    PARAM_RESONANCE,
    PARAM_INPUT_GAIN,
    PARAM_WET,
    PARAM_OUTPUT_GAIN,

    PARAM_PATTERN_LFO_1,
    PARAM_PATTERN_LFO_2,
    PARAM_RATE_TYPE_LFO_1,
    PARAM_RATE_TYPE_LFO_2,
    PARAM_SYNC_RATE_LFO_1,
    PARAM_SYNC_RATE_LFO_2,
    PARAM_SEC_RATE_LFO_1,
    PARAM_SEC_RATE_LFO_2,
    PARAM_RETRIG_LFO_1,
    PARAM_RETRIG_LFO_2,
    PARAM_COUNT,
} ParamID;

#ifndef NDEBUG
static const char* PARAM_STR[] = {
    "PARAM_CUTOFF",
    "PARAM_SCREAM",
    "PARAM_RESONANCE",
    "PARAM_INPUT_GAIN",
    "PARAM_WET",
    "PARAM_OUTPUT_GAIN",
    "PARAM_PATTERN_LFO_1",
    "PARAM_PATTERN_LFO_2",
    "PARAM_RATE_TYPE_LFO_1",
    "PARAM_RATE_TYPE_LFO_2",
    "PARAM_SYNC_RATE_LFO_1",
    "PARAM_SYNC_RATE_LFO_2",
    "PARAM_SEC_RATE_LFO_1",
    "PARAM_SEC_RATE_LFO_2",
    "PARAM_RETRIG_LFO_1",
    "PARAM_RETRIG_LFO_2",
};
_Static_assert(ARRLEN(PARAM_STR) == PARAM_COUNT, "");
#endif

#define RANGE_INPUT_GAIN_MIN -72.0
#define RANGE_INPUT_GAIN_MAX 24.0

#define RANGE_OUTPUT_GAIN_MIN -24.0
#define RANGE_OUTPUT_GAIN_MAX 0

enum
{
    NUM_AUTOMATABLE_PARAMS = PARAM_OUTPUT_GAIN + 1,

    NUM_LFO_PATTERNS = 8,

    MAX_PATTERN_LENGTH_PATTERNS = 16,

    GUI_INIT_WIDTH  = 960,
    GUI_INIT_HEIGHT = 420,

    GUI_MIN_WIDTH  = 900,
    GUI_MIN_HEIGHT = GUI_INIT_HEIGHT,

    EVENT_QUEUE_SIZE = 256,
    EVENT_QUEUE_MASK = 255,
};

typedef enum EventType
{
    EVENT_SET_PARAMETER = 16,
    EVENT_SET_PARAMETER_NOTIFYING_HOST,
} EventType;

typedef enum LFORate
{
    LFO_RATE_4_BARS,
    LFO_RATE_2_BARS,
    LFO_RATE_1_BAR,
    LFO_RATE_3_4,
    LFO_RATE_2_3,
    LFO_RATE_1_2,
    LFO_RATE_3_8,
    LFO_RATE_1_3,
    LFO_RATE_1_4,
    LFO_RATE_3_16,
    LFO_RATE_1_6,
    LFO_RATE_1_8,
    LFO_RATE_1_12,
    LFO_RATE_1_16,
    LFO_RATE_1_24,
    LFO_RATE_1_32,
    LFO_RATE_1_48,
    LFO_RATE_1_64,
    LFO_RATE_COUNT,
} LFORate;
static const char* LFO_RATE_NAMES[] = {
    "4 Bars",
    "2 Bars",
    "1 Bar",
    "3 / 4",
    "2 / 3",
    "1 / 2",
    "3 / 8",
    "1 / 3",
    "1 / 4",
    "3 / 16",
    "1 / 6",
    "1 / 8",
    "1 / 12",
    "1 / 16",
    "1 / 24",
    "1 / 32",
    "1 / 48",
    "1 / 64",
};
_Static_assert(ARRLEN(LFO_RATE_NAMES) == LFO_RATE_COUNT, "");

typedef struct LFO
{
    xt_spinlock_t spinlocks[NUM_LFO_PATTERNS];

    // struct
    // {
    //     float x;    // 0-pattern_length. Values are in beat time
    //     float y;    // 0,1
    //     float skew; // 0,1, default 0.5
    // } points;
    // NOTE: the GUI will display a point on the right edge. This does not represent the final point in this array. We
    // calculate that point at runtime based off of the first point in these arrays
    xvec3f* points[NUM_LFO_PATTERNS];

    double phase;

    // (deprecated. Might remove later)
    // Length in beats
    int pattern_length[NUM_LFO_PATTERNS];

    int grid_x[NUM_LFO_PATTERNS];
    // (deprecated. Might remove later)
    int grid_y[NUM_LFO_PATTERNS];
} LFO;

typedef struct Plugin
{
    LinkedArena*      audio_arena;
    CplugHostContext* cplug_ctx;

#ifndef NDEBUG
    uint64_t num_process_callbacks;
#endif // DEBUG

    // Retained data for GUI
    void*             gui;
    int               width, height;
    bool              lfo_section_open;
    uint8_t           selected_lfo_idx;
    enum IMPShapeType lfo_shape_idx;
    xvec2f            last_lfo_amount;

    // two floats, stored as a u64
    xt_atomic_uint64_t gui_input_peak_gain;

    xvec2f _gui_input_last_peak;
    int    _gui_input_read_count[2];

    // xt_atomic_float gui_osc_phase;
    // xt_atomic_float gui_osc_midi;

    double main_params[PARAM_COUNT];
    double audio_params[PARAM_COUNT];

    // Plugin data
    double   sample_rate;
    uint32_t max_block_size;

    bool   playhead_was_playing;
    double last_playhead_beats;

    float             retrig_detection;
    xt_atomic_uint8_t gui_retrig_flag;

    LFO lfos[2];

    double bpm;
    double beat_position;
    double beat_inc;

    // left channel is lfo 1, right is lfo 2
    xvec2f lfo_mod_amounts[NUM_AUTOMATABLE_PARAMS];

    struct FilterState
    {
        float autogain_last_peak;  // used for peak detector. lazy way to smooth out the 'average volume' of a signal
        SmoothedValue autogain_dB; // applied to signal
        ADSR autogain_adsr; // rushed hackjob to help smooth out pops when new signals start playing at a low gain, but
                            // then become loud. eg. a kick sound have a loud attack, but always has a few samples
                            // before its at maximum volume. For an autogain looking to boost the "average loudness" of
                            // signal, the average loudness will begin way too loud for signals like a kick. So we need
                            // an "attack"

        SmoothedValue values[NUM_AUTOMATABLE_PARAMS];

        Tanh_ADAA2 tanh_1;
        Tanh_ADAA2 tanh_2;

        float fb_yn_1;   // y n-1. Used in feedback loop to get overdriven sound
        float peak_xn_1; // x n-1. Used by peak detector to gate feedback and prevent ringing

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
void send_to_global_event_queue(enum GlobalEvent, void*);
// [Main thread]
void dequeue_global_events();
void main_dequeue_events(Plugin* p);

bool is_main_thread();

// [Main thread]
void send_to_audio_event_queue(Plugin* p, const CplugEvent* event);
// [Any thread]
void send_to_main_event_queue(Plugin* p, const CplugEvent event);

// [Main thread]
void param_change_begin(Plugin* p, ParamID id);
void param_change_end(Plugin* p, ParamID id);
void param_change_update(Plugin* p, ParamID id, double value);
// Calls begin > update > end
void param_set(Plugin* p, ParamID id, double value);
// [main thread]
double main_get_param(Plugin* p, ParamID id);