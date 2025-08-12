#include "plugin.h"
#include <stdio.h>
#include <string.h>
#include <xhl/maths.h>
#include <xhl/vector.h>

typedef xvecu plugin_version;

typedef struct StateHeader
{
    plugin_version version;
    uint32_t       size;
} StateHeader;

typedef struct PluginStatev0_0_1
{
    double params[3];
} PluginStatev0_0_1;

typedef struct PluginStatev0_0_3
{
    double params[4];
} PluginStatev0_0_3;

typedef struct PluginStatev0_0_4
{
    double params[5];
} PluginStatev0_0_4;

typedef struct PluginStatev0_2_0
{
    double params[7];
} PluginState;
_Static_assert(NUM_PARAMS == 7, "If params change, update state");

plugin_version get_plugin_version()
{
    plugin_version version = {0};
    int            major, minor, patch;
    if (3 == sscanf(CPLUG_PLUGIN_VERSION, "%d.%d.%d", &major, &minor, &patch))
    {
        version.major = major;
        version.minor = minor;
        version.patch = patch;
    }
    return version;
}

void cplug_saveState(void* _p, const void* stateCtx, cplug_writeProc writeProc)
{
    Plugin* p = _p;

    StateHeader header;
    header.version = get_plugin_version();
    header.size    = sizeof(PluginState);
    writeProc(stateCtx, &header, sizeof(header));

    _Static_assert(sizeof(PluginState) == sizeof(p->main_params), "Must match");
    writeProc(stateCtx, &p->main_params, sizeof(p->main_params));
}

void state_update_params(Plugin* p, double* state_params, size_t num_params)
{
    for (int i = 0; i < NUM_PARAMS; i++)
    {
        double v;

        if (i < num_params)
            v = state_params[i];
        else
            v = cplug_getDefaultParameterValue(p, i);

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
    }

    memcpy(p->audio_params, p->main_params, sizeof(p->main_params));
    p->cplug_ctx->rescan(p->cplug_ctx, CPLUG_FLAG_RESCAN_PARAM_VALUES);
}

void cplug_loadState(void* _p, const void* stateCtx, cplug_readProc readProc)
{
    Plugin* p = _p;

    StateHeader header = {0};
    int64_t     ret    = readProc(stateCtx, &header, sizeof(header));

    if (ret != 0 && ret != sizeof(header))
    {
        println("Error: Unexpected state version. Ret %lld", ret);
    }
    else
    {
        const plugin_version v0_0_3 = {.patch = 3};
        if (header.version.u32 < v0_0_3.u32)
        {
            PluginStatev0_0_1 state;
            readProc(stateCtx, &state, sizeof(state));

            state_update_params(p, state.params, ARRLEN(state.params));
        }
        else if (header.version.u32 == v0_0_3.u32)
        {
            PluginStatev0_0_3 state;
            xassert(header.size == sizeof(state));
            readProc(stateCtx, &state, sizeof(state));
            state_update_params(p, state.params, ARRLEN(state.params));
        }
        else
        {
            PluginState state;
            xassert(header.size == sizeof(state));
            readProc(stateCtx, &state, sizeof(state));
            state_update_params(p, state.params, ARRLEN(state.params));
        }
    }
}
