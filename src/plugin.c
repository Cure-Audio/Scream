#include "plugin.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
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

    p->width  = 640;
    p->height = 480;

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
    double v = 0;
    if (paramId == kCutoff)
        v = 0.5;
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
    snprintf(buf, bufsize, "%f", value);
}

void cplug_setSampleRateAndBlockSize(void* _p, double sampleRate, uint32_t maxBlockSize)
{
    Plugin* p         = _p;
    p->sample_rate    = sampleRate;
    p->max_block_size = maxBlockSize;
}
void cplug_process(void* _p, CplugProcessContext* ctx)
{
    DISABLE_DENORMALS
    Plugin* p = _p;

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
            memset(output[0] + frame, 0, sizeof(float) * num_frames);
            memset(output[1] + frame, 0, sizeof(float) * num_frames);
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
    float params[NUM_PARAMS];
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
