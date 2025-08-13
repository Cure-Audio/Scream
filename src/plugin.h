#pragma once
#include "common.h"
#include <cplug.h>
#include <stdint.h>
#include <xhl/array.h>
#include <xhl/thread.h>
#include <xhl/vector.h>

#include "param_smoothing.h"

#include "libs/ADAA.h"

typedef enum ParamID
{
    PARAM_CUTOFF,
    PARAM_SCREAM,
    PARAM_RESONANCE,
    PARAM_INPUT_GAIN,
    PARAM_WET,

    PARAM_PATTERN_LFO_1,
    PARAM_PATTERN_LFO_2,
} ParamID;

#define RANGE_INPUT_GAIN_MIN -72.0
#define RANGE_INPUT_GAIN_MAX 24.0

enum
{
    NUM_AUTOMATABLE_PARAMS = PARAM_WET + 1,
    NUM_PARAMS             = PARAM_PATTERN_LFO_2 + 1,

    NUM_LFO_PATTERNS = 8,

    MAX_PATTERN_LENGTH_PATTERNS = 16,

    GUI_INIT_WIDTH  = 960,
    GUI_INIT_HEIGHT = 400,

    GUI_MIN_WIDTH  = (GUI_INIT_WIDTH * 3) / 4,
    GUI_MIN_HEIGHT = (GUI_INIT_HEIGHT * 7) / 8,

    EVENT_QUEUE_SIZE = 256,
    EVENT_QUEUE_MASK = 255,
};

enum ShapeButtonType
{
    SHAPE_FLAT,
    SHAPE_LINEAR_ASC,
    SHAPE_LINEAR_DESC,
    SHAPE_CONVEX_ASC,
    SHAPE_CONCAVE_DESC,
    SHAPE_CONCAVE_ASC,
    SHAPE_CONVEX_DESC,
    SHAPE_TRIANGLE_UP,
    SHAPE_TRIANGLE_DOWN,
    SHAPE_COUNT,
};

typedef enum EventType
{
    EVENT_SET_PARAMETER = 16,
    EVENT_SET_PARAMETER_NOTIFYING_HOST,

    EVENT_SET_LFO_POINTS,
    EVENT_SET_LFO_XY,
    EVENT_SET_LFO_SKEW,
} EventType;

typedef struct LFOPoint
{
    float x;    // 0-pattern_length. Values are in beat time
    float y;    // 0,1
    float skew; // 0,1, default 0.5
} LFOPoint;

typedef struct LFO
{
    // NOTE: the GUI will display a point on the right edge. This does not represent the final point in this array. We
    // calculate that point at runtime based off of the first point in these arrays
    LFOPoint* points[NUM_LFO_PATTERNS];

    // Length in beats
    int pattern_length[NUM_LFO_PATTERNS];
} LFO;

typedef union LFOEvent
{
    struct
    {
        enum EventType type;
        uint8_t        lfo_idx;
        uint8_t        pattern_idx;
        LFOPoint*      array;
    } set_points;

    struct
    {
        enum EventType type;
        uint8_t        lfo_idx;
        uint8_t        pattern_idx;
        uint16_t       point_idx;
        float          x, y;
    } set_xy;

    struct
    {
        enum EventType type;
        uint8_t        lfo_idx;
        uint8_t        pattern_idx;
        uint16_t       point_idx;
        float          skew;
    } set_skew;
} LFOEvent;
_Static_assert(sizeof(LFOEvent) == sizeof(CplugEvent), "");

typedef struct Plugin
{
    CplugHostContext* cplug_ctx;

    // Retained data for GUI
    void*                gui;
    int                  width, height;
    bool                 lfo_section_open;
    uint8_t              selected_lfo_idx;
    enum ShapeButtonType lfo_shape_idx;

    // two floats, stored as a u64
    xt_atomic_uint64_t gui_input_peak_gain;

    xvec2f _gui_input_last_peak;
    int    _gui_input_read_count[2];

    xt_atomic_float gui_osc_phase;
    xt_atomic_float gui_osc_midi;

    double main_params[NUM_PARAMS];
    double audio_params[NUM_PARAMS];

    // Plugin data
    double   sample_rate;
    uint32_t max_block_size;

    LFO lfos[2];

    xvec2f* mod_buffer;
    double  bpm;
    double  beat_position;
    double  beat_inc;

    // left channel is lfo 1, right is lfo 2
    xvec2f lfo_mod_amounts[NUM_AUTOMATABLE_PARAMS];

    struct FilterState
    {
        SmoothedValue values[NUM_AUTOMATABLE_PARAMS];

        Tanh_ADAA2 tanh_1;
        Tanh_ADAA2 tanh_2;

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
    GLOBAL_EVENT_GARBAGE_COLLECT_FREE,
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

#ifndef NDEBUG
static const char* PARAM_STR[] = {
    "PARAM_CUTOFF",
    "PARAM_SCREAM",
    "PARAM_RESONANCE",
    "PARAM_INPUT_GAIN",
    "PARAM_WET",
    "PARAM_PATTERN_LFO_1",
    "PARAM_PATTERN_LFO_2",
};
_Static_assert(ARRLEN(PARAM_STR) == NUM_PARAMS, "");
#endif