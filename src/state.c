#include "plugin.h"
#include <string.h>
#include <xhl/maths.h>

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
    uint32_t size;
    double   params[NUM_PARAMS];
} PluginState;

void cplug_saveState(void* _p, const void* stateCtx, cplug_writeProc writeProc)
{
    Plugin* p = _p;

    PluginState state   = {0};
    state.version.major = 0;
    state.version.minor = 0;
    state.version.patch = 1;
    state.version.tweak = 0;
    state.size          = sizeof(state);

    _Static_assert(sizeof(state.params) == sizeof(p->main_params), "Must match");
    memcpy(state.params, p->main_params, sizeof(p->main_params));

    writeProc(stateCtx, &state, sizeof(state));
}

void cplug_loadState(void* _p, const void* stateCtx, cplug_readProc readProc)
{
    Plugin*     p     = _p;
    PluginState state = {0};

    int64_t ret = readProc(stateCtx, &state, sizeof(state) + 8);
    if (ret != 0 && ret != 32)
    {
        println("Error: Unexpected state version. Ret %lld", ret);
    }
    else
    {
        for (int i = 0; i < ARRLEN(state.params); i++)
        {
            double   v        = state.params[i];
            double   vmin     = 0;
            double   vmax     = 1;
            uint32_t param_id = cplug_getParameterID(p, i);
            cplug_getParameterRange(p, param_id, &vmin, &vmax);
            xassert(vmax > vmin);
            if (v < vmin)
                v = vmin;
            if (v > vmax)
                v = vmax;
            p->main_params[i] = xm_clampd(v, vmin, vmax);
            _Static_assert(sizeof(state.params) == sizeof(p->main_params), "Must match");
        }
        memcpy(p->audio_params, p->main_params, sizeof(p->main_params));

        p->cplug_ctx->rescan(p->cplug_ctx, CPLUG_FLAG_RESCAN_PARAM_VALUES);
    }
}
