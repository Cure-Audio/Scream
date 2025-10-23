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

    xvec2f    lfo_mod_amounts[6];
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

// [main thread]
void cplug_saveState(void* _p, const void* stateCtx, cplug_writeProc writeProc)
{
    Plugin* p = _p;

    size_t requried_blob_size = 0;
    for (int lfo_idx = 0; lfo_idx < ARRLEN(p->lfos); lfo_idx++)
    {
        LFO* lfo = p->lfos + lfo_idx;

        for (int pattern_idx = 0; pattern_idx < ARRLEN(lfo->points); pattern_idx++)
        {
            size_t npoints       = xarr_len(lfo->points[pattern_idx]);
            size_t arrsize_bytes = npoints * sizeof(lfo->points[pattern_idx][0]);
            // Round up to 16 bytes alignment
            arrsize_bytes = (arrsize_bytes + 0xf) & ~0xf;
            xassert((arrsize_bytes & 0xf) == 0);

            requried_blob_size += arrsize_bytes;
        }
    }

    size_t       state_size = sizeof(PluginState) + requried_blob_size;
    PluginState* state      = xcalloc(1, state_size);

    _Static_assert(sizeof(state->params) == sizeof(p->main_params), "");
    memcpy(state->params, p->main_params, sizeof(p->main_params));

    _Static_assert(sizeof(state->lfo_mod_amounts) == sizeof(p->lfo_mod_amounts), "");
    _Static_assert(ARRLEN(state->lfo_mod_amounts) == ARRLEN(p->lfo_mod_amounts), "");
    memcpy(state->lfo_mod_amounts, p->lfo_mod_amounts, sizeof(p->lfo_mod_amounts));

    size_t blob_write_pos = 0;
    for (int i = 0; i < ARRLEN(state->lfos); i++)
    {
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(state->lfos[0].grid_y), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(state->lfos[0].patterns), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(p->lfos[0].grid_x), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(p->lfos[0].grid_y), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(p->lfos[0].points), "");
        _Static_assert(sizeof(state->lfos[0].grid_x) == sizeof(p->lfos[0].grid_x), "");
        _Static_assert(sizeof(state->lfos[0].grid_x) == sizeof(p->lfos[0].grid_y), "");

        memcpy(state->lfos[i].grid_x, p->lfos[i].grid_x, sizeof(p->lfos[i].grid_x));
        memcpy(state->lfos[i].grid_y, p->lfos[i].grid_y, sizeof(p->lfos[i].grid_y));

        for (int k = 0; k < ARRLEN(state->lfos[i].patterns); k++)
        {
            size_t npoints              = xarr_len(p->lfos[i].points[k]);
            size_t arrsize_bytes        = npoints * sizeof(p->lfos[i].points[k][0]);
            size_t arrsize_bytes_padded = (arrsize_bytes + 0xf) & ~0xf;

            bool will_not_overflow =
                (offsetof(PluginState, blob) + blob_write_pos + arrsize_bytes_padded) <= state_size;
            xassert(will_not_overflow);
            if (will_not_overflow)
            {
                state->lfos[i].patterns[k].array_length = npoints;
                state->lfos[i].patterns[k].blob_offset  = blob_write_pos;
                void* write_pos                         = state->blob + blob_write_pos;
                memcpy(write_pos, p->lfos[i].points[k], arrsize_bytes);

                blob_write_pos += arrsize_bytes_padded;
            }
        }
    }
    xassert(blob_write_pos == requried_blob_size);

    state->blob_length = blob_write_pos;

    StateHeader header;
    header.version = get_plugin_version();
    header.size    = state_size;
    writeProc(stateCtx, &header, sizeof(header));
    writeProc(stateCtx, state, state_size);

    xfree(state);
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

// [main thread]
void cplug_loadState(void* _p, const void* stateCtx, cplug_readProc readProc)
{
    Plugin* p = _p;

    StateHeader header = {0};
    int64_t     ret    = readProc(stateCtx, &header, sizeof(header));

    // TODO: init LFO points
    // xassert(false);

    extern void reset_state(Plugin * p);

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
        if (header.version.u32 < v0_2_4.u32)
        {
            // Reset all of the new state between v0.0.3 and v0.2.4
            memset(p->lfo_mod_amounts, 0, sizeof(p->lfo_mod_amounts));

            float    x1  = 0;
            float    x2  = x1 + 0.5;
            LFOPoint pt1 = {x1, 0, 0.5};
            LFOPoint pt2 = {x2, 1, 0.5};

            for (int i = 0; i < ARRLEN(p->lfos); i++)
            {
                for (int k = 0; k < ARRLEN(p->lfos[0].grid_x); k++)
                    p->lfos[i].grid_x[k] = 4;
                for (int k = 0; k < ARRLEN(p->lfos[0].grid_y); k++)
                    p->lfos[i].grid_y[k] = 4;
                for (int k = 0; k < ARRLEN(p->lfos[0].pattern_length); k++)
                    p->lfos[i].pattern_length[k] = 1;

                // !!!
                for (int k = 0; k < ARRLEN(p->lfos[0].pattern_length); k++)
                {
                    xt_spinlock_lock(&p->lfos[i].spinlocks[k]);

                    xarr_setlen(p->lfos[i].points[k], 2);
                    p->lfos[i].points[k][0] = pt1;
                    p->lfos[i].points[k][1] = pt2;

                    xt_spinlock_unlock(&p->lfos[i].spinlocks[k]);
                }
            }
        }
        else // if (header.version.u32 >= v0_2_4.u32)
        {
            PluginState* state = xmalloc(header.size);

            int64_t bytes_read = readProc(stateCtx, state, header.size);

            if (bytes_read != header.size)
            {
                // TODO: log error
            }
            else
            {
                state_update_params(p, state->params, ARRLEN(state->params));

                _Static_assert(sizeof(state->lfo_mod_amounts) == sizeof(p->lfo_mod_amounts), "");
                _Static_assert(ARRLEN(state->lfo_mod_amounts) == ARRLEN(p->lfo_mod_amounts), "");
                memcpy(state->lfo_mod_amounts, p->lfo_mod_amounts, sizeof(p->lfo_mod_amounts));

                // spare array
                LFOPoint* dst_points = NULL;

                for (int lfo_idx = 0; lfo_idx < ARRLEN(state->lfos); lfo_idx++)
                {
                    LFO* lfo = p->lfos + lfo_idx;

                    _Static_assert(sizeof(lfo->grid_x) == sizeof(state->lfos[lfo_idx].grid_x), "");
                    _Static_assert(sizeof(lfo->grid_y) == sizeof(state->lfos[lfo_idx].grid_y), "");
                    memcpy(lfo->grid_x, state->lfos[lfo_idx].grid_x, sizeof(lfo->grid_x));
                    memcpy(lfo->grid_y, state->lfos[lfo_idx].grid_y, sizeof(lfo->grid_y));

                    for (int pattern_idx = 0; pattern_idx < ARRLEN(state->lfos[lfo_idx].patterns); pattern_idx++)
                    {
                        LFOPointArrayHeaderv0_2_4* arrheader = &state->lfos[lfo_idx].patterns[pattern_idx];

                        size_t    src_npoints = arrheader->array_length;
                        LFOPoint* src_points  = (LFOPoint*)(state->blob + arrheader->blob_offset);

                        xarr_setlen(dst_points, src_npoints);

                        size_t num_bytes = sizeof(*dst_points) * src_npoints;
                        xassert(arrheader->blob_offset + num_bytes <= state->blob_length);

                        memcpy(dst_points, src_points, num_bytes);

                        // !!! Audio is still running when running cplug_loadState()
                        {
                            xt_spinlock_lock(&lfo->spinlocks[pattern_idx]);

                            dst_points =
                                xt_atomic_exchange_ptr((xt_atomic_ptr_t*)&lfo->points[pattern_idx], dst_points);

                            xt_spinlock_unlock(&lfo->spinlocks[pattern_idx]);
                        }
                    }
                }

                xarr_free(dst_points);
            }

            xfree(state);
        }
    }

    if (p->gui)
    {
        GUI* gui                  = p->gui;
        gui->gui_lfo_points_valid = false;
    }
}
