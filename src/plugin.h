#pragma once
#include <cplug.h>
#include <nanovg_sokol.h>

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

    float params[NUM_PARAMS];
} Plugin;