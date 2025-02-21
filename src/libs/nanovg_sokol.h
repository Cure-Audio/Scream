// Taken from https://github.com/void256/nanovg_sokol.h
// see https://github.com/floooh/sokol/issues/633 for history
//
// @darkuranium
// - initial version
//
// @zeromake
// - sampler support
// - additional changes
//
// @void256 24.10.2024
// - inline shaders
// - fix partial texture updates (and with it text rendering)
// - optional debug trace logging
//
// @void256 25.10.2024
// - embedded original .glsl files and awk command to extract them to single files
// - clean up sgnvg__renderUpdateTexture and fix issues when x0 is not 0
//
// @void256 26.10.2024
// - don't call sg_update_image() when creating texture in sgnvg__renderCreateTexture()
// - renamed debug trace logging macros and added "SGNVG_" prefix there as well
//

#ifndef NANOVG_SOKOL_H
#define NANOVG_SOKOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <nanovg.h>
#include <sokol_gfx.h>

// struct sg_image;

// Create flags

enum NVGcreateFlags
{
    // Flag indicating if geometry based anti-aliasing is used (may not be needed when using MSAA).
    NVG_ANTIALIAS = 1 << 0,
    // Flag indicating if strokes should be drawn using stencil buffer. The rendering will be a little
    // slower, but path overlaps (i.e. self-intersecting or sharp turns) will be drawn just once.
    NVG_STENCIL_STROKES = 1 << 1,
    // Flag indicating that additional debug checks are done.
    NVG_DEBUG = 1 << 2,
};

NVGcontext* nvgCreateSokol(struct _sg_state_t* _sg, int flags);
void        nvgDeleteSokol(NVGcontext* ctx);

int nvsgCreateImageFromHandleSokol(
    NVGcontext* ctx,
    sg_image    imageSokol,
    sg_sampler  samplerSokol,
    int         type,
    int         w,
    int         h,
    int         flags);
struct sg_image nvsgImageHandleSokol(NVGcontext* ctx, int image);

// These are additional flags on top of NVGimageFlags.
enum NVGimageFlagsGL
{
    NVG_IMAGE_NODELETE = 1 << 16, // Do not delete Sokol image.
};

#ifdef __cplusplus
}
#endif

#endif /* NANOVG_SOKOL_H */