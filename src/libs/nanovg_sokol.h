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

#include <nanovg2.h>
#include <sokol_gfx.h>

#include "linked_arena.h"

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

NVGcontext* nvgCreateSokol(int flags);
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

enum SGNVGshaderType
{
    NSVG_SHADER_FILLGRAD,
    NSVG_SHADER_FILLIMG,
    NSVG_SHADER_SIMPLE,
    NSVG_SHADER_IMG
};

typedef struct SGNVGtexture
{
    int        id;
    sg_image   img;
    sg_sampler smp;
    int        width, height;
    int        type;
    int        flags;
    uint8_t*   imgData;
} SGNVGtexture;

typedef struct SGNVGblend
{
    sg_blend_factor srcRGB;
    sg_blend_factor dstRGB;
    sg_blend_factor srcAlpha;
    sg_blend_factor dstAlpha;
} SGNVGblend;

enum SGNVGcallType
{
    SGNVG_NONE = 0,
    SGNVG_FILL,
    SGNVG_CONVEXFILL,
    SGNVG_STROKE,
    SGNVG_TRIANGLES,
};

typedef struct SGNVGpath
{
    int fillOffset;
    int fillCount;
    int strokeOffset;
    int strokeCount;
} SGNVGpath;

typedef struct SGNVGattribute
{
    float vertex[2];
    float tcoord[2];
} SGNVGattribute;

typedef struct SGNVGvertUniforms
{
    float viewSize[4];
} SGNVGvertUniforms;

typedef struct SGNVGfragUniforms
{
#define NANOVG_SG_UNIFORMARRAY_SIZE 11
    union
    {
        struct
        {
            float           scissorMat[12]; // matrices are actually 3 vec4s
            float           paintMat[12];
            struct NVGcolor innerCol;
            struct NVGcolor outerCol;
            float           scissorExt[2];
            float           scissorScale[2];
            float           extent[2];
            float           radius;
            float           feather;
            float           strokeMult;
            float           strokeThr;
            float           texType;
            float           type;
        };
        float uniformArray[NANOVG_SG_UNIFORMARRAY_SIZE][4];
    };
} SGNVGfragUniforms;

// LRU cache; keep its size relatively small, as items are accessed via a linear search
#define NANOVG_SG_PIPELINE_CACHE_SIZE 32

typedef struct SGNVGpipelineCacheKey
{
    uint16_t blend;   // cached as `src_factor_rgb | (dst_factor_rgb << 4) | (src_factor_alpha << 8) | (dst_factor_alpha
                      // << 12)`
    uint16_t lastUse; // updated on each read
} SGNVGpipelineCacheKey;

enum SGNVGpipelineType
{
    // used by sgnvg__convexFill, sgnvg__stroke, sgnvg__triangles
    SGNVG_PIP_BASE = 0,

    // used by sgnvg__fill
    SGNVG_PIP_FILL_STENCIL,
    SGNVG_PIP_FILL_ANTIALIAS, // only used if sg->flags & NVG_ANTIALIAS
    SGNVG_PIP_FILL_DRAW,

    // used by sgnvg__stroke
    SGNVG_PIP_STROKE_STENCIL_DRAW,      // only used if sg->flags & NVG_STENCIL_STROKES
    SGNVG_PIP_STROKE_STENCIL_ANTIALIAS, // only used if sg->flags & NVG_STENCIL_STROKES
    SGNVG_PIP_STROKE_STENCIL_CLEAR,     // only used if sg->flags & NVG_STENCIL_STROKES

    SGNVG_PIP_NUM_
};

typedef struct SGNVGpipelineCache
{
    // keys are stored as a separate array for search performance
    SGNVGpipelineCacheKey keys[NANOVG_SG_PIPELINE_CACHE_SIZE];
    sg_pipeline           pipelines[NANOVG_SG_PIPELINE_CACHE_SIZE][SGNVG_PIP_NUM_];
    uint8_t               pipelinesActive[NANOVG_SG_PIPELINE_CACHE_SIZE];
    uint16_t              currentUse; // incremented on each overwrite
} SGNVGpipelineCache;

typedef struct SGNVGcall
{
    int        type;
    int        image;
    int        pathOffset;
    int        pathCount;
    int        triangleOffset;
    int        triangleCount;
    int        uniformOffset;
    SGNVGblend blendFunc;

    SGNVGfragUniforms* uniforms_1;
    SGNVGfragUniforms* uniforms_2;
} SGNVGcall;

typedef struct SGNVGPass
{
    sg_pass pass;

    int width, height;

    int draw_start_ncalls;
} SGNVGPass;

typedef struct SGNVGcontext
{
    sg_shader          shader;
    SGNVGtexture*      textures;
    SGNVGvertUniforms  view;
    int                ntextures;
    int                ctextures;
    int                textureId;
    sg_buffer          vertBuf;
    sg_buffer          indexBuf;
    SGNVGpipelineCache pipelineCache;
    int                fragSize;
    int                flags;

    // Per frame buffers
    SGNVGcall*      calls;
    int             ccalls;
    int             ncalls;
    SGNVGpath*      paths;
    int             cpaths;
    int             npaths;
    SGNVGattribute* verts;
    int             cverts;
    int             nverts;
    int             cverts_gpu;
    uint32_t*       indexes;
    int             cindexes;
    int             nindexes;
    int             cindexes_gpu;

    SGNVGPass* passes;
    int        cpasses;
    int        npasses;

    LinkedArena* frame_arena;

    // state
    int            pipelineCacheIndex;
    sg_blend_state blend;

    int dummyTex;
} SGNVGcontext;

void snvg_new_pass(NVGcontext* ctx, const sg_pass*, int width, int height);

void snvg_process_draws(NVGcontext* ctx, int start_idx, int end_idx, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* NANOVG_SOKOL_H */