#include "common.h"

#include "plugin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <xhl/maths.h>
#include <xhl/time.h>

// Apparently denormals aren't a problem on ARM & M1?
// https://en.wikipedia.org/wiki/Subnormal_number
// https://www.kvraudio.com/forum/viewtopic.php?t=575799
#if __arm64__
#define DISABLE_DENORMALS
#define RESTORE_DENORMALS
#elif defined(_WIN32)
// https://softwareengineering.stackexchange.com/a/337251
#include <immintrin.h>
#define DISABLE_DENORMALS                                                                                              \
    unsigned int oldMXCSR = _mm_getcsr();       /*read the old MXCSR setting  */                                       \
    unsigned int newMXCSR = oldMXCSR |= 0x8040; /* set DAZ and FZ bits        */                                       \
    _mm_setcsr(newMXCSR);                       /* write the new MXCSR setting to the MXCSR */
#define RESTORE_DENORMALS _mm_setcsr(oldMXCSR);
#else
#include <fenv.h>
#define DISABLE_DENORMALS                                                                                              \
    fenv_t _fenv;                                                                                                      \
    fegetenv(&_fenv);                                                                                                  \
    fesetenv(FE_DFL_DISABLE_SSE_DENORMS_ENV);
#define RESTORE_DENORMALS fesetenv(&_fenv);
#endif

// Used for midi note maths
#define MIDI_NOTE_NUM_20Hz  15.486820576352429f // getMidiNoteFromHertz(20)
#define MIDI_NOTE_NUM_20kHz 135.0762319922975f  // getMidiNoteFromHertz(20000)
// getMidiNoteFromHertz(20000) - getMidiNoteFromHertz(20)
#define MIDI_NOTE_NUM_RANGE 119.58941141594507f

void cplug_libraryLoad()
{
    xtime_init();
    xalloc_init();
}
void cplug_libraryUnload() { xalloc_shutdown(); }

void* cplug_createPlugin(CplugHostContext* ctx)
{
    struct Plugin* p = xcalloc(1, sizeof(*p));
    p->cplug_ctx     = ctx;

    p->width  = GUI_INIT_WIDTH;
    p->height = GUI_INIT_HEIGHT;

    for (int i = 0; i < ARRLEN(p->params); i++)
        p->params[i] = cplug_getDefaultParameterValue(p, i);

    return p;
}
void cplug_destroyPlugin(void* p)
{
    xassert(p != NULL);
    xfree(p);
}

uint32_t cplug_getNumInputBusses(void* ptr) { return 1; }
uint32_t cplug_getNumOutputBusses(void* ptr) { return 1; }
uint32_t cplug_getInputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }
uint32_t cplug_getOutputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }

void cplug_getInputBusName(void*, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Input"); }
void cplug_getOutputBusName(void*, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Output"); }

uint32_t cplug_getLatencyInSamples(void* p) { return 0; }
uint32_t cplug_getTailInSamples(void* p) { return 0; }

uint32_t cplug_getParameterID(void* p, uint32_t paramIndex) { return paramIndex; }
uint32_t cplug_getParameterFlags(void* p, uint32_t paramId) { return CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE; }

void cplug_getParameterRange(void*, uint32_t paramId, double* min, double* max)
{
    *min = 0;
    *max = 1;
}

uint32_t cplug_getNumParameters(void*) { return NUM_PARAMS; }
// NOTE: AUv2 supports a max length of 52 bytes, VST3 128, CLAP 256
void cplug_getParameterName(void*, uint32_t paramId, char* buf, size_t buflen)
{
    const char* str = "";
    if (paramId == kCutoff)
        str = "Cutoff";
    else if (paramId == kScream)
        str = "Scream";
    else if (paramId == kResonance)
        str = "Resonance";
    snprintf(buf, buflen, "%s", str);
}

double cplug_getParameterValue(void* _p, uint32_t paramId)
{
    Plugin* p = _p;
    return p->params[paramId];
}
double cplug_getDefaultParameterValue(void*, uint32_t paramId)
{
    double v = 0.5;
    if (paramId == kResonance)
        v = xm_normd(-2.0, -18.0, 6);
    return v;
}
// [hopefully audio thread] VST3 & AU only
void cplug_setParameterValue(void* _p, uint32_t paramId, double value) { Plugin* p = _p; }
// VST3 only
double cplug_denormaliseParameterValue(void*, uint32_t paramId, double value) { return value; }
double cplug_normaliseParameterValue(void*, uint32_t paramId, double value) { return value; }

double cplug_parameterStringToValue(void*, uint32_t paramId, const char* str)
{
    double val = 0;
    scanf(str, "%f", &val);
    return val;
}

void cplug_parameterValueToString(void*, uint32_t paramId, char* buf, size_t bufsize, double value)
{
    switch (paramId)
    {
    case kCutoff:
    case kScream:
    {
        float Hz = xm_fast_denomalise_Hz(value);
        snprintf(buf, bufsize, "%.2fHz", Hz);
        break;
    }
    case kResonance:
    {
        float dB = xm_lerpf(value, -18, 6);
        snprintf(buf, bufsize, "%.2fdB", dB);
        break;
    }
    default:
        snprintf(buf, bufsize, "%f", value);
        break;
    }
}

void param_change_begin(Plugin* p, uint32_t param_idx)
{
    CplugEvent e     = {0};
    e.parameter.type = CPLUG_EVENT_PARAM_CHANGE_BEGIN;
    e.parameter.id   = param_idx;
    p->cplug_ctx->sendParamEvent(p->cplug_ctx, &e);
}

void param_change_end(Plugin* p, uint32_t param_idx)
{
    CplugEvent e     = {0};
    e.parameter.type = CPLUG_EVENT_PARAM_CHANGE_END;
    e.parameter.id   = param_idx;
    p->cplug_ctx->sendParamEvent(p->cplug_ctx, &e);
}

void param_change_update(Plugin* p, uint32_t param_idx, double value)
{
    CplugEvent e     = {0};
    e.parameter.type = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
    e.parameter.id   = param_idx;
    p->cplug_ctx->sendParamEvent(p->cplug_ctx, &e);

    p->params[param_idx] = value;
}

void cplug_setSampleRateAndBlockSize(void* _p, double sampleRate, uint32_t maxBlockSize)
{
    Plugin* p         = _p;
    p->sample_rate    = sampleRate;
    p->max_block_size = maxBlockSize;

    memset(&p->state, 0, sizeof(p->state));
}

// SvfLinearTrapOptimised2
// https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
typedef union Coeffs
{
    struct
    {
        float a1, a2, a3, m0, m1, m2;
    };
    void* _align;
} Coeffs;

static inline float calcG(float fc, float fs_inv /*sampleRateInv*/)
{
    return xm_fasttan_normalised(XM_HALF_PIf * 1.27f * fc * fs_inv);
}
Coeffs filter_LP(float fc, float fs_inv)
{
    float g = calcG(fc, fs_inv);
    float k = 1.0f / 0.707f;
    // float k = XM_SQRT2f; // Butterworth

    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    float m0 = 0.0f;
    float m1 = 0.0f;
    float m2 = 1.0f;
    return (Coeffs){a1, a2, a3, m0, m1, m2};
}

Coeffs filter_HP(float fc, float fs_inv)
{
    float g = calcG(fc, fs_inv);
    float k = 1.0f / 0.707f;
    // float k  = XM_SQRT2f; // Butterworth
    float a1 = 1 / (1 + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;
    float m0 = 1;
    float m1 = -k;
    float m2 = -1;
    return (Coeffs){a1, a2, a3, m0, m1, m2};
}

static inline float filter_process(float v0 /*xn*/, Coeffs* c, float* s)
{
    float v3 = v0 - s[1];
    float v1 = c->a1 * s[0] + c->a2 * v3;
    float v2 = s[1] + c->a2 * s[0] + c->a3 * v3;
    s[0]     = 2 * v1 - s[0];
    s[1]     = 2 * v2 - s[1];
    return c->m0 * v0 + c->m1 * v1 + c->m2 * v2;
}

void cplug_process(void* _p, CplugProcessContext* ctx)
{
    DISABLE_DENORMALS
    Plugin* p = _p;

    const float fs_inv = 1.0f / p->sample_rate;

    CplugEvent event;
    uint32_t   frame = 0;
    while (ctx->dequeueEvent(ctx, &event, frame))
    {
        switch (event.type)
        {
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            cplug_setParameterValue(p, event.parameter.id, event.parameter.value);
            break;
        case CPLUG_EVENT_PROCESS_AUDIO:
        {
            float** output = ctx->getAudioOutput(ctx, 0);
            CPLUG_LOG_ASSERT(output != NULL)
            CPLUG_LOG_ASSERT(output[0] != NULL);
            CPLUG_LOG_ASSERT(output[1] != NULL);

            int num_frames = event.processAudio.endFrame - frame;
#ifdef CPLUG_BUILD_STANDALONE
            // Saw wave oscillator for testing
            static float phase = 0;
            const float  inc   = 55.0f * fs_inv; // ~A1
            for (int i = frame; i < event.processAudio.endFrame; i++)
            {
                float saw_wave  = -1 + phase * 2;
                saw_wave       *= 0.25; // volume

                output[0][i] = saw_wave;

                phase += inc;
                if (phase >= 1)
                    phase -= 1;
            }
            memcpy(output[1] + frame, output[0] + frame, sizeof(float) * num_frames);
#endif
            // Setup params
            float lowpass       = p->params[kCutoff];
            float highpass      = p->params[kScream];
            float feedback_gain = p->params[kResonance];

            lowpass       = xm_fast_denomalise_Hz(lowpass);
            highpass      = xm_fast_denomalise_Hz(highpass);
            feedback_gain = xm_lerpf(feedback_gain, -18, 6);
            feedback_gain = xm_fast_dB_to_gain(feedback_gain);

            // Process
            Coeffs lp_c = filter_LP(lowpass, fs_inv);
            Coeffs hp_c = filter_HP(highpass, fs_inv);
            for (int ch = 0; ch < 2; ch++)
            {
                float*             it  = output[ch] + frame;
                const float* const end = output[ch] + event.processAudio.endFrame;
                struct FilterState s   = p->state[ch];
                for (; it != end; it++)
                {
                    const float x = *it;

                    // Feedforward
                    float y = tanhf(x + s.prev_sample);
                    y       = filter_process(y, &lp_c, s.lp);

                    xassert(y == y);
                    xassert(y >= -1 && y <= 1);
                    *it = y;

                    // Feedback
                    float feed    = filter_process(y, &hp_c, s.hp);
                    feed          = tanhf(feed * feedback_gain);
                    s.prev_sample = feed;
                }
                p->state[ch] = s;
            }

            frame = event.processAudio.endFrame;
            break;
        }
        default:
            break;
        }
    }
    RESTORE_DENORMALS
}

typedef struct PluginStatev0_0_1
{
    union
    {
        struct
        {
            uint8_t tweak;
            uint8_t patch;
            uint8_t minor;
            uint8_t major;
        };
        uint32_t number;
    } version;
    double params[NUM_PARAMS];
} PluginState;

void cplug_saveState(void* _p, const void* stateCtx, cplug_writeProc writeProc)
{
    Plugin* p = _p;

    PluginState state   = {0};
    state.version.major = 0;
    state.version.minor = 0;
    state.version.patch = 1;
    state.version.tweak = 0;

    _Static_assert(sizeof(state.params) == sizeof(p->params), "Must match");
    memcpy(state.params, p->params, sizeof(p->params));

    writeProc(stateCtx, &state, sizeof(state));
}

void cplug_loadState(void* _p, const void* stateCtx, cplug_readProc readProc)
{
    Plugin*     p     = _p;
    PluginState state = {0};

    readProc(stateCtx, &state, sizeof(state));

    _Static_assert(sizeof(state.params) == sizeof(p->params), "Must match");
    memcpy(p->params, state.params, sizeof(p->params));
}
