#include "gui.h"
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

XALIGN(8) typedef struct LFOPointArrayHeaderv0_2_4
{
    int array_length; // num 'LFOPoint'
    int blob_offset;  // LFOPoint* array = PluginStatev0_2_4.blob + blob_offset;
} LFOPointArrayHeaderv0_2_4;

typedef struct LFOv0_2_4
{
    int grid_x[8];
    int grid_y[8];

    LFOPointArrayHeaderv0_2_4 patterns[8];
} LFOv0_2_4;

typedef struct PluginStatev0_2_4
{
    double params[16];

    LFOv0_2_4 lfos[2];

    size_t        blob_length;
    unsigned char blob[];
} PluginState;
_Static_assert(PARAM_COUNT == 16, "If params change, update state");
_Static_assert(NUM_LFO_PATTERNS == 8, "Max LFO patterns change, update state");

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

    // TODO: save state
    xassert(false);
    // _Static_assert(sizeof(PluginState) == sizeof(p->main_params), "Must match");
    // writeProc(stateCtx, &p->main_params, sizeof(p->main_params));
}

void state_update_params(Plugin* p, double* state_params, size_t num_params)
{
    for (int i = 0; i < PARAM_COUNT; i++)
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

    // TODO: init LFO points
    xassert(false);

    if (ret != 0 && ret != sizeof(header))
    {
        println("Error: Unexpected state version. Ret %lld", ret);
    }
    else
    {
        static const plugin_version v0_0_3 = {.patch = 3};
        static const plugin_version v0_2_4 = {.minor = 2, .patch = 4};
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
        // Note: between v0.0.3 and v0.2.4 we didn't support saving state
        else if (header.version.u32 >= v0_2_4.u32)
        {
            PluginState state;
            xassert(header.size == sizeof(state));
            readProc(stateCtx, &state, sizeof(state));
            state_update_params(p, state.params, ARRLEN(state.params));

            for (int lfo_idx = 0; lfo_idx < ARRLEN(state.lfos); lfo_idx++)
            {
                LFO* lfo = p->lfos + lfo_idx;

                _Static_assert(sizeof(lfo->grid_x) == sizeof(state.lfos[lfo_idx].grid_x), "");
                _Static_assert(sizeof(lfo->grid_y) == sizeof(state.lfos[lfo_idx].grid_y), "");
                memcpy(lfo->grid_x, state.lfos[lfo_idx].grid_x, sizeof(lfo->grid_x));
                memcpy(lfo->grid_y, state.lfos[lfo_idx].grid_y, sizeof(lfo->grid_y));

                for (int pattern_idx = 0; pattern_idx < ARRLEN(state.lfos[lfo_idx].patterns); pattern_idx++)
                {
                    LFOPointArrayHeaderv0_2_4* arrheader = &state.lfos[lfo_idx].patterns[pattern_idx];

                    int       num_points = arrheader->array_length;
                    LFOPoint* points     = (LFOPoint*)(state.blob + arrheader->blob_offset);

                    xarr_setlen(lfo->points[pattern_idx], num_points);

                    size_t num_bytes = sizeof(*points) * num_points;
                    xassert(arrheader->blob_offset + num_bytes <= state.blob_length);

                    memcpy(lfo->points[pattern_idx], points, num_bytes);
                }
            }
        }
    }

    if (p->gui)
    {
        GUI* gui                  = p->gui;
        gui->gui_lfo_points_valid = false;
    }
}
