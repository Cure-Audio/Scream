#pragma once
#include <cplug.h>

enum Parameter
{
    kCutoff,
    kScream,
    kResonance,
};
enum
{
    NUM_PARAMS = kResonance + 1
};

typedef struct Plugin
{
    CplugHostContext* cplug_ctx;

    void* gui;
    int   width, height;

    double   sample_rate;
    uint32_t max_block_size;

    double params[NUM_PARAMS];

    struct FilterState
    {
        float lp[2];
        float hp[2];

        float prev_sample;
    } state[2];

} Plugin;

void param_change_begin(Plugin* p, uint32_t param_idx);
void param_change_end(Plugin* p, uint32_t param_idx);
void param_change_update(Plugin* p, uint32_t param_idx, double value);