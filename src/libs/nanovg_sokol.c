#include "nanovg_sokol.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/*
    Embedded source code compiled with:

    sokol-shdc --input shd.glsl --output shd.glsl.h --slang glsl410:glsl300es:hlsl4:metal_macos:metal_ios:metal_sim:wgsl
--ifdef sokol-shdc --input shd.aa.glsl --output shd.aa.glsl.h --defines=EDGE_AA --slang
glsl410:glsl300es:hlsl4:metal_macos:metal_ios:metal_sim:wgsl --ifdef

    shd.glsl and shd.aa.glsl both @include the internal_shd.fs.glsl and internal_shd.vs.glsl

    All four files are included below for convenience.

    To extract the shader source code and get back the 4 files use the following
    awk command:

    awk '/^8< / {out_file=$2; print "// " out_file > out_file; next} /^>8/ {out_file=""; next} {if (out_file) print $0
>> out_file}' nanovg_sokol.h

8< internal_shd.fs.glsl

precision highp float;
#if defined(_HLSL5_) && !defined(USE_SOKOL)
    uniform frag {
        mat4 _scissorMat;
        vec4 _scissorExt;
        vec4 _scissorScale;
        mat4 _paintMat;
        vec4 _extent;
        vec4 _radius;
        vec4 _feather;
        vec4 innerCol;
        vec4 outerCol;
        vec4 _strokeMult;
        int texType;
        int type;
    };
    #define scissorMat mat3(_scissorMat)
    #define scissorExt _scissorExt.xy
    #define scissorScale _scissorScale.xy
    #define paintMat mat3(_paintMat)
    #define extent _extent.xy
    #define radius _radius.x
    #define feather _feather.x
    #define strokeMult _strokeMult.x
    #define strokeThr _strokeMult.y
#else
    #ifdef USE_UNIFORMBUFFER
        layout(std140,binding=1) uniform frag {
            mat3 scissorMat;
            mat3 paintMat;
            vec4 innerCol;
            vec4 outerCol;
            vec2 scissorExt;
            vec2 scissorScale;
            vec2 extent;
            float radius;
            float feather;
            float strokeMult;
            float strokeThr;
            int texType;
            int type;
        };
    #else
        layout(std140,binding=1) uniform frag {
            vec4 dummy[11];
        };
        #define scissorMat mat3(dummy[0].xyz, dummy[1].xyz, dummy[2].xyz)
        #define paintMat mat3(dummy[3].xyz, dummy[4].xyz, dummy[5].xyz)
        #define innerCol dummy[6]
        #define outerCol dummy[7]
        #define scissorExt dummy[8].xy
        #define scissorScale dummy[8].zw
        #define extent dummy[9].xy
        #define radius dummy[9].z
        #define feather dummy[9].w
        #define strokeMult dummy[10].x
        #define strokeThr dummy[10].y
        #define texType int(dummy[10].z)
        #define type int(dummy[10].w)
    #endif
#endif

layout(binding=2) uniform texture2D tex;
layout(binding=3) uniform sampler smp;
layout(location = 0) in vec2 ftcoord;
layout(location = 1) in vec2 fpos;
layout(location = 0) out vec4 outColor;

float sdroundrect(vec2 pt, vec2 ext, float rad) {
    vec2 ext2 = ext - vec2(rad,rad);
    vec2 d = abs(pt) - ext2;
    return min(max(d.x,d.y),0.0) + length(max(d,0.0)) - rad;
}

// Scissoring
float scissorMask(vec2 p) {
    vec2 sc = (abs((scissorMat * vec3(p,1.0)).xy) - scissorExt);
    sc = vec2(0.5,0.5) - sc * scissorScale;
    return clamp(sc.x,0.0,1.0) * clamp(sc.y,0.0,1.0);
}

#ifdef EDGE_AA
// Stroke - from [0..1] to clipped pyramid, where the slope is 1px.
float strokeMask() {
    return min(1.0, (1.0-abs(ftcoord.x*2.0-1.0))*strokeMult) * min(1.0, ftcoord.y);
}
#endif

void main(void) {
    vec4 result;

#ifdef EDGE_AA
    float strokeAlpha = strokeMask();
// #ifdef _HLSL5_
// #endif
    if (strokeAlpha < strokeThr) discard;
#else
    float strokeAlpha = 1.0;
#endif
    float scissor = scissorMask(fpos);

    if (scissor == 0) {
        return;
    }

    if (type == 0) {    // Gradient
        // Calculate gradient color using box gradient
        vec2 pt = (paintMat * vec3(fpos,1.0)).xy;
        float d = clamp((sdroundrect(pt, extent, radius) + feather*0.5) / feather, 0.0, 1.0);
        vec4 color = mix(innerCol,outerCol,d);
        // Combine alpha
        color *= strokeAlpha * scissor;
        result = color;
    } else if (type == 1) {// Image
        // Calculate color fron texture
        vec2 pt = (paintMat * vec3(fpos,1.0)).xy / extent;
        vec4 color = texture(sampler2D(tex, smp), pt);
        if (texType == 1) color = vec4(color.xyz*color.w,color.w);
        if (texType == 2) color = vec4(color.x);
        // stencil support
        if (texType == 3 && color.a == 1.0) discard;
        // Apply color tint and alpha.
        color *= innerCol;
        // Combine alpha
        color *= strokeAlpha * scissor;
        result = color;
    } else if (type == 2) {// Stencil fill
        result = vec4(1,1,1,1);
    } else if (type == 3) {// Textured tris
        vec4 color = texture(sampler2D(tex, smp), ftcoord);
        if (texType == 1) color = vec4(color.xyz*color.w,color.w);
        if (texType == 2) color = vec4(color.x);
        result = color * scissor * innerCol;
    }
    outColor = result;
}
>8

8< internal_shd.vs.glsl

layout (binding = 0) uniform viewSize {
#if defined(_HLSL5_) && !defined(USE_SOKOL)
    mat4 dummy;
#endif
    vec4 _viewSize;
};
layout (location = 0) in vec2 vertex;
layout (location = 1) in vec2 tcoord;
layout (location = 0) out vec2 ftcoord;
layout (location = 1) out vec2 fpos;

void main(void) {
        ftcoord = tcoord;
        fpos = vertex;
    float x = 2.0 * vertex.x / _viewSize.x - 1.0;
    float y = 1.0 - 2.0 * vertex.y / _viewSize.y;
        gl_Position = vec4(
        x,
        y,
        0,
        1
    );
}
>8

8< shd.glsl

@module nanovg

@vs vs
@include internal_shd.vs.glsl
@end

@fs fs
@include internal_shd.fs.glsl
@end

@program sg vs fs
>8

8< shd.aa.glsl

@module nanovg_aa

@vs vs_aa
@include internal_shd.vs.glsl
@end

@fs fs_aa
@include internal_shd.fs.glsl
@end

@program sg vs_aa fs_aa

>8

*/

#include "nanovg_sokol.glsl.h"

#if !defined(SGNVG_MALLOC) || !defined(SGNVG_REALLOC) || !defined(SGNVG_FREE)
#include <stdlib.h>
#define SGNVG_MALLOC(sz)       malloc(sz)
#define SGNVG_REALLOC(ptr, sz) realloc(ptr, sz)
#define SGNVG_FREE(ptr)        free(ptr)
#endif

#if !defined(SGNVG_ASSERT)
#include <assert.h>
#define SGNVG_ASSERT(cond) assert((cond))
#endif

enum SGNVGshaderType
{
    NSVG_SHADER_FILLGRAD,
    NSVG_SHADER_FILLIMG,
    NSVG_SHADER_SIMPLE,
    NSVG_SHADER_IMG
};

struct SGNVGtexture
{
    int        id;
    sg_image   img;
    sg_sampler smp;
    int        width, height;
    int        type;
    int        flags;
    uint8_t*   imgData;
};
typedef struct SGNVGtexture SGNVGtexture;

enum
{
    NVG_IMAGE_DIRTY = 1 << 31, // Should send img buffer to GPU on render pass
};

struct SGNVGblend
{
    sg_blend_factor srcRGB;
    sg_blend_factor dstRGB;
    sg_blend_factor srcAlpha;
    sg_blend_factor dstAlpha;
};
typedef struct SGNVGblend SGNVGblend;

enum SGNVGcallType
{
    SGNVG_NONE = 0,
    SGNVG_FILL,
    SGNVG_CONVEXFILL,
    SGNVG_STROKE,
    SGNVG_TRIANGLES,
};

struct SGNVGcall
{
    int        type;
    int        image;
    int        pathOffset;
    int        pathCount;
    int        triangleOffset;
    int        triangleCount;
    int        uniformOffset;
    SGNVGblend blendFunc;
};
typedef struct SGNVGcall SGNVGcall;

struct SGNVGpath
{
    int fillOffset;
    int fillCount;
    int strokeOffset;
    int strokeCount;
};
typedef struct SGNVGpath SGNVGpath;

struct SGNVGattribute
{
    float vertex[2];
    float tcoord[2];
};
typedef struct SGNVGattribute SGNVGattribute;

struct SGNVGvertUniforms
{
    float viewSize[4];
};
typedef struct SGNVGvertUniforms SGNVGvertUniforms;

struct SGNVGfragUniforms
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
};
typedef struct SGNVGfragUniforms SGNVGfragUniforms;

// LRU cache; keep its size relatively small, as items are accessed via a linear search
#define NANOVG_SG_PIPELINE_CACHE_SIZE 32

struct SGNVGpipelineCacheKey
{
    uint16_t blend;   // cached as `src_factor_rgb | (dst_factor_rgb << 4) | (src_factor_alpha << 8) | (dst_factor_alpha
                      // << 12)`
    uint16_t lastUse; // updated on each read
};
typedef struct SGNVGpipelineCacheKey SGNVGpipelineCacheKey;

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
typedef enum SGNVGpipelineType SGNVGpipelineType;

struct SGNVGpipelineCache
{
    // keys are stored as a separate array for search performance
    SGNVGpipelineCacheKey keys[NANOVG_SG_PIPELINE_CACHE_SIZE];
    sg_pipeline           pipelines[NANOVG_SG_PIPELINE_CACHE_SIZE][SGNVG_PIP_NUM_];
    uint8_t               pipelinesActive[NANOVG_SG_PIPELINE_CACHE_SIZE];
    uint16_t              currentUse; // incremented on each overwrite
};
typedef struct SGNVGpipelineCache SGNVGpipelineCache;

struct SGNVGcontext
{
    _sg_state_t*       _sg;
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
    unsigned char*  uniforms;
    int             cuniforms;
    int             nuniforms;

    // state
    int            pipelineCacheIndex;
    sg_blend_state blend;

    int dummyTex;
};
typedef struct SGNVGcontext SGNVGcontext;

static int sgnvg__maxi(int a, int b) { return a > b ? a : b; }

#ifdef SOKOL_GLES2
static unsigned int sgnvg__nearestPow2(unsigned int num)
{
    unsigned n  = num > 0 ? num - 1 : 0;
    n          |= n >> 1;
    n          |= n >> 2;
    n          |= n >> 4;
    n          |= n >> 8;
    n          |= n >> 16;
    n++;
    return n;
}
#endif

// you probably always want to have that one disabled for performance
// unless you want to trace all functions in here which is very verbose
// #define SGNVG_DEBUG_LOG

#ifdef SGNVG_DEBUG_LOG
#define SGNVG_EXTLOG(...) fprintf(stderr, "- " __VA_ARGS__);
#define SGNVG_INTLOG(...) fprintf(stderr, "  - " __VA_ARGS__);
#else
#define SGNVG_EXTLOG(...)
#define SGNVG_INTLOG(...)
#endif

static SGNVGtexture* sgnvg__allocTexture(SGNVGcontext* sg)
{
    SGNVG_INTLOG("sgnvg__allocTexture(%p)\n", sg);
    SGNVGtexture* tex = NULL;
    int           i;

    for (i = 0; i < sg->ntextures; i++)
    {
        if (sg->textures[i].id == 0)
        {
            tex = &sg->textures[i];
            break;
        }
    }
    if (tex == NULL)
    {
        if (sg->ntextures + 1 > sg->ctextures)
        {
            SGNVGtexture* textures;
            int           ctextures = sgnvg__maxi(sg->ntextures + 1, 4) + sg->ctextures / 2; // 1.5x Overallocate
            textures                = (SGNVGtexture*)SGNVG_REALLOC(sg->textures, sizeof(SGNVGtexture) * ctextures);
            if (textures == NULL)
                return NULL;
            sg->textures  = textures;
            sg->ctextures = ctextures;
        }
        tex = &sg->textures[sg->ntextures++];
    }

    memset(tex, 0, sizeof(*tex));
    tex->id = ++sg->textureId;
    SGNVG_INTLOG("sgnvg__allocTexture(%p) -> %d\n", sg, tex->id);
    return tex;
}

static SGNVGtexture* sgnvg__findTexture(SGNVGcontext* sg, int id)
{
    SGNVG_INTLOG("sgnvg__findTexture(sg: %p, id: %d)\n", sg, id);
    int i;
    for (i = 0; i < sg->ntextures; i++)
        if (sg->textures[i].id == id)
            return &sg->textures[i];
    return NULL;
}

static int sgnvg__deleteTexture(SGNVGcontext* sg, int id)
{
    SGNVG_INTLOG("sgnvg__deleteTexture(sg: %p, id: %d)\n", sg, id);
    int i;
    for (i = 0; i < sg->ntextures; i++)
    {
        SGNVGtexture* tex = &sg->textures[i];
        if (tex->id == id)
        {
            if (tex->img.id != 0 && (tex->flags & NVG_IMAGE_NODELETE) == 0)
                sg_destroy_image(sg->_sg, tex->img);
            SGNVG_FREE(tex->imgData);
            memset(tex, 0, sizeof(*tex));
            return 1;
        }
    }
    SGNVG_ASSERT(0);
    return 0;
}

static uint16_t sgnvg__getCombinedBlendNumber(sg_blend_state blend)
{
    SGNVG_INTLOG(
        "sgnvg__getCombinedBlendNumber(%d, %d, %d, %d)\n",
        blend.src_factor_rgb,
        blend.dst_factor_rgb,
        blend.src_factor_alpha,
        blend.dst_factor_alpha);
#if __STDC_VERSION__ >= 201112L
    _Static_assert(_SG_BLENDFACTOR_NUM <= 17, "too many blend factors for 16-bit blend number");
#else
    SGNVG_ASSERT(_SG_BLENDFACTOR_NUM <= 17); // can be a _Static_assert
#endif
    return blend.src_factor_rgb | (blend.dst_factor_rgb << 4) | (blend.src_factor_alpha << 8) |
           (blend.dst_factor_alpha << 12);
}

static void sgnvg__initPipeline(
    SGNVGcontext*           sg,
    sg_pipeline             pip,
    const sg_stencil_state* stencil,
    sg_color_mask           write_mask,
    sg_cull_mode            cull_mode)
{
    SGNVG_INTLOG(
        "sgnvg__initPipeline(sg: %p, pip: %d, stencil: %p, write_mask: %d, cull_mode: %d)\n",
        sg,
        pip.id,
        stencil,
        write_mask,
        cull_mode);
    sg_init_pipeline(
        sg->_sg,
        pip,
        &(sg_pipeline_desc){
            .shader = sg->shader,
            .layout =
                {
                    // .buffers[0] = {.stride = sizeof(SGNVGattribute)},
                    .attrs =
                        {
                            [ATTR_nanovg_sg_vertex].format = SG_VERTEXFORMAT_FLOAT2,
                            [ATTR_nanovg_sg_tcoord].format = SG_VERTEXFORMAT_FLOAT2,
                        },
                },
            .stencil = *stencil,
            .colors[0] =
                {
                    .write_mask = write_mask,
                    .blend      = sg->blend,
                },
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type     = SG_INDEXTYPE_UINT32,
            .cull_mode      = cull_mode,
            .face_winding   = SG_FACEWINDING_CCW,
            .label          = "nanovg.pipeline",
        });
}

static bool sgnvg__pipelineTypeIsInUse(SGNVGcontext* sg, SGNVGpipelineType type)
{
    SGNVG_INTLOG("sgnvg__pipelineTypeIsInUse(sg: %p, type: %d)\n", sg, type);
    switch (type)
    {
    case SGNVG_PIP_BASE:
    case SGNVG_PIP_FILL_STENCIL:
    case SGNVG_PIP_FILL_DRAW:
        return true;
    case SGNVG_PIP_FILL_ANTIALIAS:
        return !!(sg->flags & NVG_ANTIALIAS);
    case SGNVG_PIP_STROKE_STENCIL_DRAW:
    case SGNVG_PIP_STROKE_STENCIL_ANTIALIAS:
    case SGNVG_PIP_STROKE_STENCIL_CLEAR:
        return !!(sg->flags & NVG_STENCIL_STROKES);

    case SGNVG_PIP_NUM_: // to avoid warnings
        break;           /* fall through to assert */
    }
    SGNVG_ASSERT(0);
    return false;
}

static int sgnvg__getIndexFromCache(SGNVGcontext* sg, uint16_t blendNumber)
{
    SGNVG_INTLOG("sgnvg__getIndexFromCache(sg: %p, blendNumber: %d)\n", sg, blendNumber);
    uint16_t currentUse = sg->pipelineCache.currentUse;

    int maxAge      = 0;
    int maxAgeIndex = 0;

    // find the correct cache entry for `blend_number`
    for (unsigned int i = 0; i < NANOVG_SG_PIPELINE_CACHE_SIZE; i++)
    {
        if (sg->pipelineCache.keys[i].blend == blendNumber)
        {
            sg->pipelineCache.keys[i].lastUse = sg->pipelineCache.currentUse;
            return i;
        }
        int age = (uint16_t)(currentUse - sg->pipelineCache.keys[i].lastUse);
        if (age > maxAge)
        {
            maxAge      = age;
            maxAgeIndex = i;
        }
    }

    // not found; reuse an old one
    sg->pipelineCache.currentUse                = ++currentUse;
    sg->pipelineCache.keys[maxAgeIndex].blend   = blendNumber;
    sg->pipelineCache.keys[maxAgeIndex].lastUse = currentUse;

    sg_pipeline* pipelines       = sg->pipelineCache.pipelines[maxAgeIndex];
    uint8_t      pipelinesActive = sg->pipelineCache.pipelinesActive[maxAgeIndex];
    // we may have had data already initialized; deinit those
    for (uint32_t type = SGNVG_PIP_BASE; type < SGNVG_PIP_NUM_; type++)
        if (pipelinesActive & (1 << type))
            sg_uninit_pipeline(sg->_sg, pipelines[type]);
    // mark all as inactive
    sg->pipelineCache.pipelinesActive[maxAgeIndex] = 0;
    return maxAgeIndex;
}

static sg_pipeline sgnvg__getPipelineFromCache(SGNVGcontext* sg, SGNVGpipelineType type)
{
    SGNVG_INTLOG("sgnvg__getPipelineFromCache(sg: %p, type: %d)\n", sg, type);
    SGNVG_ASSERT(sgnvg__pipelineTypeIsInUse(sg, type));

    int         pipelineCacheIndex = sg->pipelineCacheIndex;
    sg_pipeline pipeline           = sg->pipelineCache.pipelines[pipelineCacheIndex][type];
    uint8_t     typeMask           = 1 << type;
    if (!(sg->pipelineCache.pipelinesActive[pipelineCacheIndex] & typeMask))
    {
        sg->pipelineCache.pipelinesActive[pipelineCacheIndex] |= typeMask;
        switch (type)
        {
        case SGNVG_PIP_BASE:
            sgnvg__initPipeline(
                sg,
                pipeline,
                &(sg_stencil_state){
                    .enabled = false,
                },
                SG_COLORMASK_RGBA,
                SG_CULLMODE_BACK);
            break;

        case SGNVG_PIP_FILL_STENCIL:
            sgnvg__initPipeline(
                sg,
                pipeline,
                &(sg_stencil_state){
                    .enabled = true,
                    .front =
                        {.compare       = SG_COMPAREFUNC_ALWAYS,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_INCR_WRAP},
                    .back =
                        {.compare       = SG_COMPAREFUNC_ALWAYS,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_DECR_WRAP},
                    .read_mask  = 0xFF,
                    .write_mask = 0xFF,
                    .ref        = 0,
                },
                SG_COLORMASK_NONE,
                SG_CULLMODE_NONE);
            break;
        case SGNVG_PIP_FILL_ANTIALIAS:
            sgnvg__initPipeline(
                sg,
                pipeline,
                &(sg_stencil_state){
                    .enabled = true,
                    .front =
                        {.compare       = SG_COMPAREFUNC_EQUAL,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_KEEP},
                    .back =
                        {.compare       = SG_COMPAREFUNC_EQUAL,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_KEEP},
                    .read_mask  = 0xFF,
                    .write_mask = 0xFF,
                    .ref        = 0,
                },
                SG_COLORMASK_RGBA,
                SG_CULLMODE_BACK);
            break;
        case SGNVG_PIP_FILL_DRAW:
            sgnvg__initPipeline(
                sg,
                pipeline,
                &(sg_stencil_state){
                    .enabled = true,
                    .front =
                        {.compare       = SG_COMPAREFUNC_NOT_EQUAL,
                         .fail_op       = SG_STENCILOP_ZERO,
                         .depth_fail_op = SG_STENCILOP_ZERO,
                         .pass_op       = SG_STENCILOP_ZERO},
                    .back =
                        {.compare       = SG_COMPAREFUNC_NOT_EQUAL,
                         .fail_op       = SG_STENCILOP_ZERO,
                         .depth_fail_op = SG_STENCILOP_ZERO,
                         .pass_op       = SG_STENCILOP_ZERO},
                    .read_mask  = 0xFF,
                    .write_mask = 0xFF,
                    .ref        = 0,
                },
                SG_COLORMASK_RGBA,
                SG_CULLMODE_BACK);
            break;

        case SGNVG_PIP_STROKE_STENCIL_DRAW:
            sgnvg__initPipeline(
                sg,
                pipeline,
                &(sg_stencil_state){
                    .enabled = true,
                    .front =
                        {.compare       = SG_COMPAREFUNC_EQUAL,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_INCR_CLAMP},
                    .back =
                        {.compare       = SG_COMPAREFUNC_EQUAL,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_INCR_CLAMP},
                    .read_mask  = 0xFF,
                    .write_mask = 0xFF,
                    .ref        = 0,
                },
                SG_COLORMASK_RGBA,
                SG_CULLMODE_BACK);
            break;
        case SGNVG_PIP_STROKE_STENCIL_ANTIALIAS:
            sgnvg__initPipeline(
                sg,
                pipeline,
                &(sg_stencil_state){
                    .enabled = true,
                    .front =
                        {.compare       = SG_COMPAREFUNC_EQUAL,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_KEEP},
                    .back =
                        {.compare       = SG_COMPAREFUNC_EQUAL,
                         .fail_op       = SG_STENCILOP_KEEP,
                         .depth_fail_op = SG_STENCILOP_KEEP,
                         .pass_op       = SG_STENCILOP_KEEP},
                    .read_mask  = 0xFF,
                    .write_mask = 0xFF,
                    .ref        = 0,
                },
                SG_COLORMASK_RGBA,
                SG_CULLMODE_BACK);
            break;
        case SGNVG_PIP_STROKE_STENCIL_CLEAR:
            sgnvg__initPipeline(
                sg,
                pipeline,
                &(sg_stencil_state){
                    .enabled = true,
                    .front =
                        {.compare       = SG_COMPAREFUNC_ALWAYS,
                         .fail_op       = SG_STENCILOP_ZERO,
                         .depth_fail_op = SG_STENCILOP_ZERO,
                         .pass_op       = SG_STENCILOP_ZERO},
                    .back =
                        {.compare       = SG_COMPAREFUNC_ALWAYS,
                         .fail_op       = SG_STENCILOP_ZERO,
                         .depth_fail_op = SG_STENCILOP_ZERO,
                         .pass_op       = SG_STENCILOP_ZERO},
                    .read_mask  = 0xFF,
                    .write_mask = 0xFF,
                    .ref        = 0,
                },
                SG_COLORMASK_NONE,
                SG_CULLMODE_BACK);
            break;

        default:
            SGNVG_ASSERT(0);
        }
    }
    return pipeline;
}

static SGNVGfragUniforms* nvg__fragUniformPtr(SGNVGcontext* gl, int i);

static void sgnvg__setUniforms(SGNVGcontext* sg, int uniformOffset, int image)
{
    SGNVG_INTLOG("sgnvg__setUniforms(sg: %p, uniformOffset: %d, image: %d)\n", sg, uniformOffset, image);
    SGNVGtexture*      tex  = NULL;
    SGNVGfragUniforms* frag = nvg__fragUniformPtr(sg, uniformOffset);
    sg_apply_uniforms(sg->_sg, UB_nanovg_viewSize, &(sg_range){&sg->view, sizeof(sg->view)});
    sg_apply_uniforms(sg->_sg, UB_nanovg_frag, &(sg_range){frag, sizeof(*frag)});

    if (image != 0)
    {
        tex = sgnvg__findTexture(sg, image);
    }
    // If no image is set, use empty texture
    if (tex == NULL)
    {
        tex = sgnvg__findTexture(sg, sg->dummyTex);
    }
    sg_apply_bindings(
        sg->_sg,
        &(sg_bindings){
            .vertex_buffers[0]        = sg->vertBuf,
            .index_buffer             = sg->indexBuf,
            .images[IMG_nanovg_tex]   = tex ? tex->img : (sg_image){0},
            .samplers[SMP_nanovg_smp] = tex ? tex->smp : (sg_sampler){0},
        });
}

static void
sgnvg__preparePipelineUniforms(SGNVGcontext* sg, SGNVGpipelineType pipelineType, int uniformOffset, int image)
{
    SGNVG_INTLOG(
        "sgnvg__preparePipelineUniforms(sg: %p, pipelineType: %d, uniformOffset: %d, image: %d)\n",
        sg,
        pipelineType,
        uniformOffset,
        image);
    sg_pipeline pip = sgnvg__getPipelineFromCache(sg, pipelineType);
    sg_apply_pipeline(sg->_sg, pip);
    sgnvg__setUniforms(sg, uniformOffset, image);
}

#define NANOVG_SG_TOSTRING_(X) #X
#define NANOVG_SG_TOSTRING(X)  NANOVG_SG_TOSTRING_(X)

static int sgnvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data);

static int sgnvg__renderCreate(void* uptr)
{
    SGNVG_EXTLOG("sgnvg__renderCreate(uptr: %p)\n", uptr);
    SGNVGcontext* sg    = (SGNVGcontext*)uptr;
    int           align = 4;

    // if(sg->flags & NVG_ANTIALIAS)
    sg->shader = sg_make_shader(sg->_sg, nanovg_sg_shader_desc(sg_query_backend(sg->_sg)));
    // else
    // sg->shader = sg_make_shader(sg->_sg, nanovg_sg_shader_desc(sg_query_backend()));
    for (int i = 0; i < NANOVG_SG_PIPELINE_CACHE_SIZE; i++)
    {
        for (uint32_t t = 0; t < SGNVG_PIP_NUM_; t++)
        {
            // only allocate pipelines if correct flags are set
            if (!sgnvg__pipelineTypeIsInUse(sg, (SGNVGpipelineType)t))
                continue;
            sg->pipelineCache.pipelines[i][t] = sg_alloc_pipeline(sg->_sg);
        }
    }

    sg->blend = (sg_blend_state){
        .enabled          = true,
        .src_factor_rgb   = SG_BLENDFACTOR_ZERO,
        .dst_factor_rgb   = SG_BLENDFACTOR_ZERO,
        .op_rgb           = SG_BLENDOP_ADD,
        .src_factor_alpha = SG_BLENDFACTOR_ZERO,
        .dst_factor_alpha = SG_BLENDFACTOR_ZERO,
        .op_alpha         = SG_BLENDOP_ADD,
    };

    sg->vertBuf  = sg_alloc_buffer(sg->_sg);
    sg->indexBuf = sg_alloc_buffer(sg->_sg);

    sg->fragSize = sizeof(SGNVGfragUniforms) + (align - sizeof(SGNVGfragUniforms) % align) % align;

    // Some platforms does not allow to have samples to unset textures.
    // Create empty one which is bound when there's no texture specified.
    sg->dummyTex = sgnvg__renderCreateTexture(sg, NVG_TEXTURE_ALPHA, 1, 1, 0, NULL);

    return 1;
}

static int sgnvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data)
{
    SGNVG_EXTLOG(
        "sgnvg__renderCreateTexture(uptr: %p, type: %d, w: %d, h: %d, imageFlags: %d, data: %p)\n",
        uptr,
        type,
        w,
        h,
        imageFlags,
        data);
    SGNVGcontext* sg  = (SGNVGcontext*)uptr;
    SGNVGtexture* tex = sgnvg__allocTexture(sg);

    if (tex == NULL)
        return 0;

#ifdef SOKOL_GLES2
    // Check for non-power of 2.
    if (sgnvg__nearestPow2(w) != (unsigned int)w || sgnvg__nearestPow2(h) != (unsigned int)h)
    {
        // No repeat
        if ((imageFlags & NVG_IMAGE_REPEATX) != 0 || (imageFlags & NVG_IMAGE_REPEATY) != 0)
        {
            printf("Repeat X/Y is not supported for non power-of-two textures (%d x %d)\n", w, h);
            imageFlags &= ~(NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
        }
        // No mips.
        if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS)
        {
            printf("Mip-maps is not support for non power-of-two textures (%d x %d)\n", w, h);
            imageFlags &= ~NVG_IMAGE_GENERATE_MIPMAPS;
        }
    }
#endif
    SGNVG_ASSERT(!(imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) && "TODO mipmaps");

    // if we have mipmaps, we forbid updating
    bool immutable = !!(imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) && data;

    tex->width              = w;
    tex->height             = h;
    tex->type               = type;
    tex->flags              = imageFlags;
    sg_image_data imageData = {
        // TODO mipmaps
        .subimage[0][0] = {data, w * h * (type == NVG_TEXTURE_RGBA ? 4 : 1)},
    };
    tex->img = sg_make_image(
        sg->_sg,
        &(sg_image_desc){
            .type = SG_IMAGETYPE_2D,
            //.render_target
            .width        = w,
            .height       = h,
            .num_mipmaps  = 1, // TODO mipmaps
            .usage        = immutable ? SG_USAGE_IMMUTABLE : SG_USAGE_DYNAMIC,
            .pixel_format = type == NVG_TEXTURE_RGBA ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8,
            .data         = ((imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) && data) ? imageData
                                                                                : (sg_image_data){.subimage[0][0] = {NULL, 0}},
            .label        = "nanovg.image[]",
        });
    tex->imgData = SGNVG_MALLOC(w * h * (type == NVG_TEXTURE_RGBA ? 4 : 1));
    if (data != NULL)
    {
        memcpy(tex->imgData, data, w * h * (type == NVG_TEXTURE_RGBA ? 4 : 1));
    }
    tex->smp = sg_make_sampler(
        sg->_sg,
        &(sg_sampler_desc){
            .min_filter    = imageFlags & NVG_IMAGE_GENERATE_MIPMAPS
                                 ? _SG_FILTER_DEFAULT
                                 : (imageFlags & NVG_IMAGE_NEAREST ? SG_FILTER_NEAREST : SG_FILTER_LINEAR),
            .mipmap_filter = imageFlags & NVG_IMAGE_GENERATE_MIPMAPS
                                 ? (imageFlags & NVG_IMAGE_NEAREST ? SG_FILTER_NEAREST : SG_FILTER_LINEAR)
                                 : _SG_FILTER_DEFAULT,
            .mag_filter    = imageFlags & NVG_IMAGE_NEAREST ? SG_FILTER_NEAREST : SG_FILTER_LINEAR,
            .wrap_u        = imageFlags & NVG_IMAGE_REPEATX ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE,
            .wrap_v        = imageFlags & NVG_IMAGE_REPEATY ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE,
        });

    return tex->id;
}

static int sgnvg__renderDeleteTexture(void* uptr, int image)
{
    SGNVG_EXTLOG("sgnvg__renderDeleteTexture(uptr: %p, image: %d)\n", uptr, image);
    SGNVGcontext* sg = (SGNVGcontext*)uptr;
    return sgnvg__deleteTexture(sg, image);
}

static int sgnvg__renderUpdateTexture(void* uptr, int image, int x0, int y0, int w, int h, const unsigned char* data)
{
    SGNVG_EXTLOG(
        "sgnvg__renderUpdateTexture(uptr: %p, image: %d, x0: %d, y0: %d, w: %d, h: %d, data: %p)\n",
        uptr,
        image,
        x0,
        y0,
        w,
        h,
        data);
    SGNVGcontext* sg  = (SGNVGcontext*)uptr;
    SGNVGtexture* tex = sgnvg__findTexture(sg, image);

    if (tex == NULL)
        return 0;

    if (tex->imgData)
    {
        // this is really weird but nanogl_gl.h is doing the same
        // somehow we always get a whole row or so? o_O
        x0 = 0;
        w  = tex->width;

        size_t bytePerPixel = 1;
        if (tex->type == NVG_TEXTURE_RGBA)
        {
            bytePerPixel = 4;
        }
        size_t               srcLineInBytes = w * bytePerPixel;
        size_t               dstLineInBytes = tex->width * bytePerPixel;
        const unsigned char* src            = data + y0 * srcLineInBytes;
        unsigned char*       dst            = tex->imgData + y0 * dstLineInBytes + x0 * bytePerPixel;

        for (int y = 0; y < h; y++)
        {
            memcpy(dst, src, srcLineInBytes);
            src += srcLineInBytes;
            dst += dstLineInBytes;
        }

        tex->flags |= NVG_IMAGE_DIRTY;
    }

    return 1;
}

static int sgnvg__renderGetTextureSize(void* uptr, int image, int* w, int* h)
{
    SGNVG_EXTLOG("sgnvg__renderGetTextureSize(uptr: %p, image: %d, w: %d, h: %d)\n", uptr, image, *w, *h);
    SGNVGcontext* sg  = (SGNVGcontext*)uptr;
    SGNVGtexture* tex = sgnvg__findTexture(sg, image);
    if (tex == NULL)
        return 0;
    *w = tex->width;
    *h = tex->height;
    return 1;
}

static void sgnvg__xformToMat3x4(float* m3, float* t)
{
    SGNVG_INTLOG("sgnvg__xformToMat3x4(m3: %p, t: %p)\n", m3, t);
    m3[0]  = t[0];
    m3[1]  = t[1];
    m3[2]  = 0.0f;
    m3[3]  = 0.0f;
    m3[4]  = t[2];
    m3[5]  = t[3];
    m3[6]  = 0.0f;
    m3[7]  = 0.0f;
    m3[8]  = t[4];
    m3[9]  = t[5];
    m3[10] = 1.0f;
    m3[11] = 0.0f;
}

static NVGcolor sgnvg__premulColor(NVGcolor c)
{
    SGNVG_INTLOG("sgnvg__premulColor(c: %f, %f, %f, %f)\n", c.r, c.g, c.b, c.a);
    c.r *= c.a;
    c.g *= c.a;
    c.b *= c.a;
    return c;
}

static int sgnvg__convertPaint(
    SGNVGcontext*      sg,
    SGNVGfragUniforms* frag,
    NVGpaint*          paint,
    NVGscissor*        scissor,
    float              width,
    float              fringe,
    float              strokeThr)
{
    SGNVG_INTLOG(
        "sgnvg__convertPaint(sg: %p, frag: %p, paint: %p, scissor: %p, width: %f, fringe: %f, strokeThr: %f)\n",
        sg,
        frag,
        paint,
        scissor,
        width,
        fringe,
        strokeThr);
    SGNVGtexture* tex = NULL;
    float         invxform[6];

    memset(frag, 0, sizeof(*frag));

    frag->innerCol = sgnvg__premulColor(paint->innerColor);
    frag->outerCol = sgnvg__premulColor(paint->outerColor);

    if (scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f)
    {
        memset(frag->scissorMat, 0, sizeof(frag->scissorMat));
        frag->scissorExt[0]   = 1.0f;
        frag->scissorExt[1]   = 1.0f;
        frag->scissorScale[0] = 1.0f;
        frag->scissorScale[1] = 1.0f;
    }
    else
    {
        nvgTransformInverse(invxform, scissor->xform);
        sgnvg__xformToMat3x4(frag->scissorMat, invxform);
        frag->scissorExt[0] = scissor->extent[0];
        frag->scissorExt[1] = scissor->extent[1];
        frag->scissorScale[0] =
            sqrtf(scissor->xform[0] * scissor->xform[0] + scissor->xform[2] * scissor->xform[2]) / fringe;
        frag->scissorScale[1] =
            sqrtf(scissor->xform[1] * scissor->xform[1] + scissor->xform[3] * scissor->xform[3]) / fringe;
    }

    memcpy(frag->extent, paint->extent, sizeof(frag->extent));
    frag->strokeMult = (width * 0.5f + fringe * 0.5f) / fringe;
    frag->strokeThr  = strokeThr;

    if (paint->image != 0)
    {
        tex = sgnvg__findTexture(sg, paint->image);
        if (tex == NULL)
            return 0;
        if ((tex->flags & NVG_IMAGE_FLIPY) != 0)
        {
            float m1[6], m2[6];
            nvgTransformTranslate(m1, 0.0f, frag->extent[1] * 0.5f);
            nvgTransformMultiply(m1, paint->xform);
            nvgTransformScale(m2, 1.0f, -1.0f);
            nvgTransformMultiply(m2, m1);
            nvgTransformTranslate(m1, 0.0f, -frag->extent[1] * 0.5f);
            nvgTransformMultiply(m1, m2);
            nvgTransformInverse(invxform, m1);
        }
        else
        {
            nvgTransformInverse(invxform, paint->xform);
        }
        frag->type = NSVG_SHADER_FILLIMG;

#if NANOVG_GL_USE_UNIFORMBUFFER
        if (tex->type == NVG_TEXTURE_RGBA)
            frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0 : 1;
        else
            frag->texType = 2;
#else
        if (tex->type == NVG_TEXTURE_RGBA)
            frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0.0f : 1.0f;
        else
            frag->texType = 2.0f;
#endif
        //		printf("frag->texType = %d\n", frag->texType);
    }
    else
    {
        frag->type    = NSVG_SHADER_FILLGRAD;
        frag->radius  = paint->radius;
        frag->feather = paint->feather;
        nvgTransformInverse(invxform, paint->xform);
    }

    sgnvg__xformToMat3x4(frag->paintMat, invxform);

    return 1;
}

static void sgnvg__renderViewport(void* uptr, float width, float height, float devicePixelRatio)
{
    SGNVG_EXTLOG(
        "sgnvg__renderViewport(uptr: %p, width: %f, height: %f, devicePixelRatio: %f)\n",
        uptr,
        width,
        height,
        devicePixelRatio);
    NVG_NOTUSED(devicePixelRatio);
    SGNVGcontext* sg     = (SGNVGcontext*)uptr;
    sg->view.viewSize[0] = width;
    sg->view.viewSize[1] = height;
}

static void sgnvg__fill(SGNVGcontext* sg, SGNVGcall* call)
{
    SGNVG_INTLOG("sgnvg__fill(sg: %p, call: %p)\n", sg, call);
    SGNVGpath* paths = &sg->paths[call->pathOffset];
    int        i, npaths = call->pathCount;

    sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_FILL_STENCIL, call->uniformOffset, 0);
    for (i = 0; i < npaths; i++)
        sg_draw(sg->_sg, paths[i].fillOffset, paths[i].fillCount, 1);

    // if (sg->flags & NVG_ANTIALIAS) {
    sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_FILL_ANTIALIAS, call->uniformOffset + sg->fragSize, call->image);
    // Draw fringes
    for (i = 0; i < npaths; i++)
        sg_draw(sg->_sg, paths[i].strokeOffset, paths[i].strokeCount, 1);
    // }

    // Draw fill
    sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_FILL_DRAW, call->uniformOffset + sg->fragSize, call->image);
    sg_draw(sg->_sg, call->triangleOffset, call->triangleCount, 1);
}

static void sgnvg__convexFill(SGNVGcontext* sg, SGNVGcall* call)
{
    SGNVG_INTLOG("sgnvg__convexFill(sg: %p, call: %p)\n", sg, call);
    SGNVGpath* paths = &sg->paths[call->pathOffset];
    int        i, npaths = call->pathCount;

    sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_BASE, call->uniformOffset, call->image);
    for (i = 0; i < npaths; i++)
    {
        sg_draw(sg->_sg, paths[i].fillOffset, paths[i].fillCount, 1);
        // Draw fringes
        if (paths[i].strokeCount > 0)
        {
            sg_draw(sg->_sg, paths[i].strokeOffset, paths[i].strokeCount, 1);
        }
    }
}

static void sgnvg__stroke(SGNVGcontext* sg, SGNVGcall* call)
{
    SGNVG_INTLOG("sgnvg__stroke(sg: %p, call: %p)\n", sg, call);
    SGNVGpath* paths  = &sg->paths[call->pathOffset];
    int        npaths = call->pathCount, i;

    if (sg->flags & NVG_STENCIL_STROKES)
    {
        sgnvg__preparePipelineUniforms(
            sg,
            SGNVG_PIP_STROKE_STENCIL_DRAW,
            call->uniformOffset + sg->fragSize,
            call->image);
        for (i = 0; i < npaths; i++)
            sg_draw(sg->_sg, paths[i].strokeOffset, paths[i].strokeCount, 1);

        // Draw anti-aliased pixels.
        sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_STROKE_STENCIL_ANTIALIAS, call->uniformOffset, call->image);
        for (i = 0; i < npaths; i++)
            sg_draw(sg->_sg, paths[i].strokeOffset, paths[i].strokeCount, 1);

        // Clear stencil buffer.
        sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_STROKE_STENCIL_CLEAR, call->uniformOffset, 0);
        for (i = 0; i < npaths; i++)
            sg_draw(sg->_sg, paths[i].strokeOffset, paths[i].strokeCount, 1);

        //		sgnvg__convertPaint(sg, nvg__fragUniformPtr(sg, call->uniformOffset + sg->fragSize), paint,
        // scissor, strokeWidth, fringe, 1.0f - 0.5f/255.0f);
    }
    else
    {
        sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_BASE, call->uniformOffset, call->image);
        // Draw Strokes
        for (i = 0; i < npaths; i++)
            sg_draw(sg->_sg, paths[i].strokeOffset, paths[i].strokeCount, 1);
    }
}

static void sgnvg__triangles(SGNVGcontext* sg, SGNVGcall* call)
{
    SGNVG_INTLOG("sgnvg__triangles(sg: %p, call: %p)\n", sg, call);
    sgnvg__preparePipelineUniforms(sg, SGNVG_PIP_BASE, call->uniformOffset, call->image);
    sg_draw(sg->_sg, call->triangleOffset, call->triangleCount, 1);
}

static void sgnvg__renderCancel(void* uptr)
{
    SGNVG_EXTLOG("sgnvg__renderCancel(uptr: %p)\n", uptr);
    SGNVGcontext* sg = (SGNVGcontext*)uptr;
    sg->nverts       = 0;
    sg->npaths       = 0;
    sg->ncalls       = 0;
    sg->nuniforms    = 0;
}

static sg_blend_factor sgnvg_convertBlendFuncFactor(int factor)
{
    if (factor == NVG_ZERO)
        return SG_BLENDFACTOR_ZERO;
    if (factor == NVG_ONE)
        return SG_BLENDFACTOR_ONE;
    if (factor == NVG_SRC_COLOR)
        return SG_BLENDFACTOR_SRC_COLOR;
    if (factor == NVG_ONE_MINUS_SRC_COLOR)
        return SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    if (factor == NVG_DST_COLOR)
        return SG_BLENDFACTOR_DST_COLOR;
    if (factor == NVG_ONE_MINUS_DST_COLOR)
        return SG_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    if (factor == NVG_SRC_ALPHA)
        return SG_BLENDFACTOR_SRC_ALPHA;
    if (factor == NVG_ONE_MINUS_SRC_ALPHA)
        return SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    if (factor == NVG_DST_ALPHA)
        return SG_BLENDFACTOR_DST_ALPHA;
    if (factor == NVG_ONE_MINUS_DST_ALPHA)
        return SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    if (factor == NVG_SRC_ALPHA_SATURATE)
        return SG_BLENDFACTOR_SRC_ALPHA_SATURATED;
    return _SG_BLENDFACTOR_DEFAULT;
}

static SGNVGblend sgnvg__blendCompositeOperation(NVGcompositeOperationState op)
{
    SGNVG_INTLOG(
        "sgnvg__blendCompositeOperation(op: (%d, %d, %d, %d))\n",
        op.srcRGB,
        op.dstRGB,
        op.srcAlpha,
        op.dstAlpha);
    SGNVGblend blend;
    blend.srcRGB   = sgnvg_convertBlendFuncFactor(op.srcRGB);
    blend.dstRGB   = sgnvg_convertBlendFuncFactor(op.dstRGB);
    blend.srcAlpha = sgnvg_convertBlendFuncFactor(op.srcAlpha);
    blend.dstAlpha = sgnvg_convertBlendFuncFactor(op.dstAlpha);
    if (blend.srcRGB == _SG_BLENDFACTOR_DEFAULT || blend.dstRGB == _SG_BLENDFACTOR_DEFAULT ||
        blend.srcAlpha == _SG_BLENDFACTOR_DEFAULT || blend.dstAlpha == _SG_BLENDFACTOR_DEFAULT)
    {
        blend.srcRGB   = SG_BLENDFACTOR_ONE;
        blend.dstRGB   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.srcAlpha = SG_BLENDFACTOR_ONE;
        blend.dstAlpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    }
    return blend;
}

static void sgnvg__renderFlush(void* uptr)
{
    SGNVG_EXTLOG("sgnvg__renderFlush(uptr: %p)\n", uptr);
    SGNVGcontext* sg = (SGNVGcontext*)uptr;
    int           i;

    for (i = 0; i < sg->ntextures; i++)
    {
        if (sg->textures[i].id != 0)
        {
            SGNVGtexture* tex = &sg->textures[i];

            if (tex->flags & NVG_IMAGE_DIRTY)
            {
                tex->flags ^= NVG_IMAGE_DIRTY;
                SGNVG_INTLOG("sgnvg__flushTexture(%p -> tex->id: %d, tex->img: 0x%0X)\n", tex, tex->id, tex->img.id);
                const sg_range img_range = {
                    tex->imgData,
                    tex->width * tex->height * (tex->type == NVG_TEXTURE_RGBA ? 4 : 1)};
                const sg_image_data imgdata = {.subimage[0][0] = img_range};
                sg_update_image(sg->_sg, tex->img, &imgdata);
            }
        }
    }

    if (sg->ncalls > 0 && sg->nverts && sg->nindexes)
    {
        if (sg->cverts_gpu < sg->nverts) // resize GPU vertex buffer
        {
            if (sg->cverts_gpu) // delete old buffer if necessary
                sg_uninit_buffer(sg->_sg, sg->vertBuf);
            sg->cverts_gpu = sg->cverts;
            sg_init_buffer(
                sg->_sg,
                sg->vertBuf,
                &(sg_buffer_desc){
                    .size  = sg->cverts_gpu * sizeof(*sg->verts),
                    .type  = SG_BUFFERTYPE_VERTEXBUFFER,
                    .usage = SG_USAGE_STREAM,
                    .label = "nanovg.vertBuf",
                });
        }
        // upload vertex data
        sg_update_buffer(sg->_sg, sg->vertBuf, &(sg_range){sg->verts, sg->nverts * sizeof(*sg->verts)});

        if (sg->cindexes_gpu < sg->nindexes) // resize GPU index buffer
        {
            if (sg->cindexes_gpu) // delete old buffer if necessary
                sg_uninit_buffer(sg->_sg, sg->indexBuf);
            sg->cindexes_gpu = sg->cindexes;
            sg_init_buffer(
                sg->_sg,
                sg->indexBuf,
                &(sg_buffer_desc){
                    .size  = sg->cindexes_gpu * sizeof(*sg->indexes),
                    .type  = SG_BUFFERTYPE_INDEXBUFFER,
                    .usage = SG_USAGE_STREAM,
                    .label = "nanovg.indexBuf",
                });
        }
        // upload index data
        sg_update_buffer(sg->_sg, sg->indexBuf, &(sg_range){sg->indexes, sg->nindexes * sizeof(*sg->indexes)});

        for (i = 0; i < sg->ncalls; i++)
        {
            SGNVGcall* call            = &sg->calls[i];
            sg->blend.src_factor_rgb   = call->blendFunc.srcRGB;
            sg->blend.dst_factor_rgb   = call->blendFunc.dstRGB;
            sg->blend.src_factor_alpha = call->blendFunc.srcAlpha;
            sg->blend.dst_factor_alpha = call->blendFunc.dstAlpha;
            sg->pipelineCacheIndex     = sgnvg__getIndexFromCache(sg, sgnvg__getCombinedBlendNumber(sg->blend));
            if (call->type == SGNVG_FILL)
                sgnvg__fill(sg, call);
            else if (call->type == SGNVG_CONVEXFILL)
                sgnvg__convexFill(sg, call);
            else if (call->type == SGNVG_STROKE)
                sgnvg__stroke(sg, call);
            else if (call->type == SGNVG_TRIANGLES)
                sgnvg__triangles(sg, call);
        }

        // sg_uninit_pipeline(sg->pipeline);
        // sgnvg__initPipeline(sg, &(sg_stencil_state){0}, SG_COLORMASK_RGBA, SG_CULLMODE_NONE);
    }

    // Reset calls
    sg->nverts    = 0;
    sg->nindexes  = 0;
    sg->npaths    = 0;
    sg->ncalls    = 0;
    sg->nuniforms = 0;
}

static int sgnvg__maxVertCount(const NVGpath* paths, int npaths)
{
    SGNVG_INTLOG("sgnvg__maxVertCount(paths: %p, npaths: %d)\n", paths, npaths);
    int i, count = 0;
    for (i = 0; i < npaths; i++)
    {
        count += paths[i].nfill;
        count += paths[i].nstroke;
    }
    return count;
}

static int sgnvg__maxIndexCount(const NVGpath* paths, int npaths)
{
    SGNVG_INTLOG("sgnvg__maxIndexCount(paths: %p, npaths: %d)\n", paths, npaths);
    int i, count = 0;
    for (i = 0; i < npaths; i++)
    {
        count += sgnvg__maxi(paths[i].nfill - 2, 0) * 3;   // triangle fan
        count += sgnvg__maxi(paths[i].nstroke - 2, 0) * 3; // triangle strip
    }
    return count;
}

static SGNVGcall* sgnvg__allocCall(SGNVGcontext* sg)
{
    SGNVG_INTLOG("sgnvg__allocCall(sg: %p)\n", sg);
    SGNVGcall* ret = NULL;
    if (sg->ncalls + 1 > sg->ccalls)
    {
        SGNVGcall* calls;
        int        ccalls = sgnvg__maxi(sg->ncalls + 1, 128) + sg->ccalls / 2; // 1.5x Overallocate
        calls             = (SGNVGcall*)SGNVG_REALLOC(sg->calls, sizeof(SGNVGcall) * ccalls);
        if (calls == NULL)
            return NULL;
        sg->calls  = calls;
        sg->ccalls = ccalls;
    }
    ret = &sg->calls[sg->ncalls++];
    memset(ret, 0, sizeof(SGNVGcall));
    return ret;
}

static int sgnvg__allocPaths(SGNVGcontext* sg, int n)
{
    SGNVG_INTLOG("sgnvg__allocPaths(sg: %p, n: %d)\n", sg, n);
    int ret = 0;
    if (sg->npaths + n > sg->cpaths)
    {
        SGNVGpath* paths;
        int        cpaths = sgnvg__maxi(sg->npaths + n, 128) + sg->cpaths / 2; // 1.5x Overallocate
        paths             = (SGNVGpath*)SGNVG_REALLOC(sg->paths, sizeof(SGNVGpath) * cpaths);
        if (paths == NULL)
            return -1;
        sg->paths  = paths;
        sg->cpaths = cpaths;
    }
    ret         = sg->npaths;
    sg->npaths += n;
    return ret;
}

static int sgnvg__allocVerts(SGNVGcontext* sg, int n)
{
    SGNVG_INTLOG("sgnvg__allocVerts(sg: %p, n: %d)\n", sg, n);
    int ret = 0;
    if (sg->nverts + n > sg->cverts)
    {
        SGNVGattribute* verts;
        int             cverts = sgnvg__maxi(sg->nverts + n, 4096) + sg->cverts / 2; // 1.5x Overallocate
        verts                  = (SGNVGattribute*)SGNVG_REALLOC(sg->verts, sizeof(SGNVGattribute) * cverts);
        if (verts == NULL)
            return -1;
        sg->verts  = verts;
        sg->cverts = cverts;
    }
    ret         = sg->nverts;
    sg->nverts += n;
    return ret;
}

static int sgnvg__allocIndexes(SGNVGcontext* sg, int n)
{
    SGNVG_INTLOG("sgnvg__allocIndexes(sg: %p, n: %d)\n", sg, n);
    int ret = 0;
    if (sg->nindexes + n > sg->cindexes)
    {
        uint32_t* indexes;
        int       cindexes = sgnvg__maxi(sg->nindexes + n, 4096) + sg->cindexes / 2; // 1.5x Overallocate
        indexes            = (uint32_t*)SGNVG_REALLOC(sg->indexes, sizeof(uint32_t) * cindexes);
        if (indexes == NULL)
            return -1;
        sg->indexes  = indexes;
        sg->cindexes = cindexes;
    }
    ret           = sg->nindexes;
    sg->nindexes += n;
    return ret;
}

static int sgnvg__allocFragUniforms(SGNVGcontext* sg, int n)
{
    SGNVG_INTLOG("sgnvg__allocFragUniforms(sg: %p, n: %d)\n", sg, n);
    int ret = 0, structSize = sg->fragSize;
    if (sg->nuniforms + n > sg->cuniforms)
    {
        unsigned char* uniforms;
        int            cuniforms = sgnvg__maxi(sg->nuniforms + n, 128) + sg->cuniforms / 2; // 1.5x Overallocate
        uniforms                 = (unsigned char*)SGNVG_REALLOC(sg->uniforms, structSize * cuniforms);
        if (uniforms == NULL)
            return -1;
        sg->uniforms  = uniforms;
        sg->cuniforms = cuniforms;
    }
    ret            = sg->nuniforms * structSize;
    sg->nuniforms += n;
    return ret;
}

static SGNVGfragUniforms* nvg__fragUniformPtr(SGNVGcontext* gl, int i)
{
    SGNVG_INTLOG("nvg__fragUniformPtr(gl: %p, i: %d)\n", gl, i);
    return (SGNVGfragUniforms*)&gl->uniforms[i];
}

static void sgnvg__vset(SGNVGattribute* vtx, float x, float y, float u, float v)
{
    SGNVG_INTLOG("sgnvg__vset(vtx: %p, x: %f, y: %f, u: %f, v: %f)\n", vtx, x, y, u, v);
    vtx->vertex[0] = x;
    vtx->vertex[1] = y;
    vtx->tcoord[0] = u;
    vtx->tcoord[1] = v;
}

static void sgnvg__generateTriangleFanIndexes(uint32_t* indexes, int offset, int nverts)
{
    SGNVG_INTLOG("sgnvg__generateTriangleFanIndexes(indexes: %p, offset: %d, nverts: %d)\n", indexes, offset, nverts);
    // following triangles all use starting vertex, previous vertex, and current vertex
    for (int i = 2; i < nverts; i++)
    {
        indexes[3 * (i - 2) + 0] = offset + 0;
        indexes[3 * (i - 2) + 1] = offset + i - 1;
        indexes[3 * (i - 2) + 2] = offset + i;
    }
}

static void sgnvg__generateTriangleStripIndexes(uint32_t* indexes, int offset, int nverts)
{
    SGNVG_INTLOG("sgnvg__generateTriangleStripIndexes(indexes: %p, offset: %d, nverts: %d)\n", indexes, offset, nverts);
    // following triangles all use previous 2 vertices, and current vertex
    // we use bit-shifts to get the sequence:
    // i  idx i(bits)   i-1(bits)   i-2(bits)
    // 2: 012 0010      0001        0000
    // 3: 213 0011      0010        0001
    // 4: 234 0100      0011        0010
    // 5: 435 0101      0100        0011
    // 6: 456 0110      0101        0100
    // 7: 657 0111      0110        0101
    //                  first index = above & ~1 = floor_to_even(i-1)
    //                              second index = above | 1 = ceil_to_even(i-2)
    // all this trickery ensures that we maintain correct (CCW) vertex order
    for (int i = 2; i < nverts; i++)
    {
        indexes[3 * (i - 2) + 0] = offset + ((i - 1) & ~1);
        indexes[3 * (i - 2) + 1] = offset + ((i - 2) | 1);
        indexes[3 * (i - 2) + 2] = offset + i;
    }
}

static void sgnvg__renderFill(
    void*                      uptr,
    NVGpaint*                  paint,
    NVGcompositeOperationState compositeOperation,
    NVGscissor*                scissor,
    float                      fringe,
    const float*               bounds,
    const NVGpath*             paths,
    int                        npaths)
{
    SGNVG_EXTLOG(
        "sgnvg__renderFill(uptr: %p, paint: %p, compositeOperation: (%d, %d, %d, %d), scissor: %p, fringe: %f, bounds: "
        "%f, paths: %p, npaths: %d)\n",
        uptr,
        paint,
        compositeOperation.srcRGB,
        compositeOperation.dstRGB,
        compositeOperation.srcAlpha,
        compositeOperation.dstAlpha,
        scissor,
        fringe,
        *bounds,
        paths,
        npaths);
    SGNVGcontext*      sg   = (SGNVGcontext*)uptr;
    SGNVGcall*         call = sgnvg__allocCall(sg);
    SGNVGattribute*    quad;
    SGNVGfragUniforms* frag;
    int                i, maxverts, offset, maxindexes, ioffset;

    if (call == NULL)
        return;

    call->type          = SGNVG_FILL;
    call->triangleCount = 4;
    call->pathOffset    = sgnvg__allocPaths(sg, npaths);
    if (call->pathOffset == -1)
        goto error;
    call->pathCount = npaths;
    call->image     = paint->image;
    call->blendFunc = sgnvg__blendCompositeOperation(compositeOperation);

    if (npaths == 1 && paths[0].convex)
    {
        call->type          = SGNVG_CONVEXFILL;
        call->triangleCount = 0; // Bounding box fill quad not needed for convex fill
    }

    // Allocate vertices for all the paths.
    maxverts = sgnvg__maxVertCount(paths, npaths) + call->triangleCount;
    offset   = sgnvg__allocVerts(sg, maxverts);
    if (offset == -1)
        goto error;
    maxindexes = sgnvg__maxIndexCount(paths, npaths) + sgnvg__maxi(call->triangleCount - 2, 0) * 3;
    ioffset    = sgnvg__allocIndexes(sg, maxindexes);
    if (ioffset == -1)
        goto error;

    for (i = 0; i < npaths; i++)
    {
        SGNVGpath*     copy = &sg->paths[call->pathOffset + i];
        const NVGpath* path = &paths[i];
        memset(copy, 0, sizeof(SGNVGpath));
        if (path->nfill > 0)
        {
            // fill: triangle fan
            copy->fillOffset = ioffset;
            copy->fillCount  = (path->nfill - 2) * 3;
            memcpy(&sg->verts[offset], path->fill, sizeof(NVGvertex) * path->nfill);
            sgnvg__generateTriangleFanIndexes(&sg->indexes[ioffset], offset, path->nfill);
            offset  += path->nfill;
            ioffset += copy->fillCount;
        }
        if (path->nstroke > 0)
        {
            // stroke: triangle strip
            copy->strokeOffset = ioffset;
            copy->strokeCount  = (path->nstroke - 2) * 3;
            memcpy(&sg->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
            sgnvg__generateTriangleStripIndexes(&sg->indexes[ioffset], offset, path->nstroke);
            offset  += path->nstroke;
            ioffset += copy->strokeCount;
        }
    }

    // Setup uniforms for draw calls
    if (call->type == SGNVG_FILL)
    {
        // Quad
        call->triangleOffset = ioffset;
        call->triangleCount  = (call->triangleCount - 2) * 3; // convert vertex count into index
        quad                 = &sg->verts[offset];
        sgnvg__vset(&quad[0], bounds[2], bounds[3], 0.5f, 1.0f);
        sgnvg__vset(&quad[1], bounds[2], bounds[1], 0.5f, 1.0f);
        sgnvg__vset(&quad[2], bounds[0], bounds[3], 0.5f, 1.0f);
        sgnvg__vset(&quad[3], bounds[0], bounds[1], 0.5f, 1.0f);
        sgnvg__generateTriangleStripIndexes(&sg->indexes[ioffset], offset, 4);
        call->uniformOffset = sgnvg__allocFragUniforms(sg, 2);
        if (call->uniformOffset == -1)
            goto error;
        // Simple shader for stencil
        frag = nvg__fragUniformPtr(sg, call->uniformOffset);
        memset(frag, 0, sizeof(*frag));
        frag->strokeThr = -1.0f;
        frag->type      = NSVG_SHADER_SIMPLE;
        // Fill shader
        sgnvg__convertPaint(
            sg,
            nvg__fragUniformPtr(sg, call->uniformOffset + sg->fragSize),
            paint,
            scissor,
            fringe,
            fringe,
            -1.0f);
    }
    else
    {
        call->uniformOffset = sgnvg__allocFragUniforms(sg, 1);
        if (call->uniformOffset == -1)
            goto error;
        // Fill shader
        sgnvg__convertPaint(sg, nvg__fragUniformPtr(sg, call->uniformOffset), paint, scissor, fringe, fringe, -1.0f);
    }

    return;

error:
    // We get here if call alloc was ok, but something else is not.
    // Roll back the last call to prevent drawing it.
    if (sg->ncalls > 0)
        sg->ncalls--;
}

static void sgnvg__renderStroke(
    void*                      uptr,
    NVGpaint*                  paint,
    NVGcompositeOperationState compositeOperation,
    NVGscissor*                scissor,
    float                      fringe,
    float                      strokeWidth,
    const NVGpath*             paths,
    int                        npaths)
{
    SGNVG_EXTLOG(
        "sgnvg__renderStroke(uptr: %p, paint: %p, compositeOperation: (%d, %d, %d, %d), scissor: %p, fringe: %f, "
        "strokeWidth: %f, paths: %p, npaths: %d)\n",
        uptr,
        paint,
        compositeOperation.dstAlpha,
        compositeOperation.dstRGB,
        compositeOperation.srcAlpha,
        compositeOperation.srcRGB,
        scissor,
        fringe,
        strokeWidth,
        paths,
        npaths);
    SGNVGcontext* sg   = (SGNVGcontext*)uptr;
    SGNVGcall*    call = sgnvg__allocCall(sg);
    int           i, maxverts, offset, maxindexes, ioffset;

    if (call == NULL)
        return;

    call->type       = SGNVG_STROKE;
    call->pathOffset = sgnvg__allocPaths(sg, npaths);
    if (call->pathOffset == -1)
        goto error;
    call->pathCount = npaths;
    call->image     = paint->image;
    call->blendFunc = sgnvg__blendCompositeOperation(compositeOperation);

    // Allocate vertices for all the paths.
    maxverts = sgnvg__maxVertCount(paths, npaths);
    offset   = sgnvg__allocVerts(sg, maxverts);
    if (offset == -1)
        goto error;
    maxindexes = sgnvg__maxIndexCount(paths, npaths);
    ioffset    = sgnvg__allocIndexes(sg, maxindexes);
    if (ioffset == -1)
        goto error;

    for (i = 0; i < npaths; i++)
    {
        SGNVGpath*     copy = &sg->paths[call->pathOffset + i];
        const NVGpath* path = &paths[i];
        memset(copy, 0, sizeof(SGNVGpath));
        if (path->nstroke)
        {
            // stroke: triangle strip
            copy->strokeOffset = ioffset;
            copy->strokeCount  = (path->nstroke - 2) * 3;
            memcpy(&sg->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
            sgnvg__generateTriangleStripIndexes(&sg->indexes[ioffset], offset, path->nstroke);
            offset  += path->nstroke;
            ioffset += copy->strokeCount;
        }
    }

    if (sg->flags & NVG_STENCIL_STROKES)
    {
        // Fill shader
        call->uniformOffset = sgnvg__allocFragUniforms(sg, 2);
        if (call->uniformOffset == -1)
            goto error;

        sgnvg__convertPaint(
            sg,
            nvg__fragUniformPtr(sg, call->uniformOffset),
            paint,
            scissor,
            strokeWidth,
            fringe,
            -1.0f);
        sgnvg__convertPaint(
            sg,
            nvg__fragUniformPtr(sg, call->uniformOffset + sg->fragSize),
            paint,
            scissor,
            strokeWidth,
            fringe,
            1.0f - 0.5f / 255.0f);
    }
    else
    {
        // Fill shader
        call->uniformOffset = sgnvg__allocFragUniforms(sg, 1);
        if (call->uniformOffset == -1)
            goto error;
        sgnvg__convertPaint(
            sg,
            nvg__fragUniformPtr(sg, call->uniformOffset),
            paint,
            scissor,
            strokeWidth,
            fringe,
            -1.0f);
    }

    return;

error:
    // We get here if call alloc was ok, but something else is not.
    // Roll back the last call to prevent drawing it.
    if (sg->ncalls > 0)
        sg->ncalls--;
}

static void sgnvg__renderTriangles(
    void*                      uptr,
    NVGpaint*                  paint,
    NVGcompositeOperationState compositeOperation,
    NVGscissor*                scissor,
    const NVGvertex*           verts,
    int                        nverts,
    float                      fringe)
{
    SGNVG_EXTLOG(
        "sgnvg__renderTriangles(uptr: %p, paint: %p, compositeOperation: (%d, %d, %d, %d), scissor: %p, verts: %p, "
        "nverts: %d, fringe: %f)\n",
        uptr,
        paint,
        compositeOperation.srcRGB,
        compositeOperation.dstRGB,
        compositeOperation.srcAlpha,
        compositeOperation.dstAlpha,
        scissor,
        verts,
        nverts,
        fringe);
    SGNVGcontext*      sg   = (SGNVGcontext*)uptr;
    SGNVGcall*         call = sgnvg__allocCall(sg);
    SGNVGfragUniforms* frag;

    if (call == NULL)
        return;

    call->type      = SGNVG_TRIANGLES;
    call->image     = paint->image;
    call->blendFunc = sgnvg__blendCompositeOperation(compositeOperation);

    int offset, ioffset;
    offset = sgnvg__allocVerts(sg, nverts);
    if (offset == -1)
        goto error;
    ioffset = sgnvg__allocIndexes(sg, nverts);
    if (ioffset == -1)
        goto error;

    // Allocate vertices for all the paths.
    call->triangleOffset = ioffset;
    call->triangleCount  = nverts;
    memcpy(&sg->verts[offset], verts, sizeof(NVGvertex) * nverts);
    for (int i = 0; i < nverts; i++)
        sg->indexes[ioffset + i] = offset + i;

    // Fill shader
    call->uniformOffset = sgnvg__allocFragUniforms(sg, 1);
    if (call->uniformOffset == -1)
        goto error;
    frag = nvg__fragUniformPtr(sg, call->uniformOffset);
    sgnvg__convertPaint(sg, frag, paint, scissor, 1.0f, fringe, -1.0f);
    frag->type = NSVG_SHADER_IMG;

    return;

error:
    // We get here if call alloc was ok, but something else is not.
    // Roll back the last call to prevent drawing it.
    if (sg->ncalls > 0)
        sg->ncalls--;
}

static void sgnvg__renderDelete(void* uptr)
{
    SGNVG_EXTLOG("sgnvg__renderDelete(uptr: %p)\n", uptr);
    SGNVGcontext* sg = (SGNVGcontext*)uptr;
    int           i;
    if (sg == NULL)
        return;

    sg_destroy_shader(sg->_sg, sg->shader);

    for (i = 0; i < NANOVG_SG_PIPELINE_CACHE_SIZE; i++)
    {
        for (uint32_t t = 0; t < SGNVG_PIP_NUM_; t++)
        {
            // only uninitialize if correct flags are set
            if (!sgnvg__pipelineTypeIsInUse(sg, (SGNVGpipelineType)t))
                continue;
            if (sg->pipelineCache.pipelinesActive[i] & (1 << t))
                sg_uninit_pipeline(sg->_sg, sg->pipelineCache.pipelines[i][t]);
            sg_dealloc_pipeline(sg->_sg, sg->pipelineCache.pipelines[i][t]);
        }
    }

    if (sg->cverts_gpu)
        sg_uninit_buffer(sg->_sg, sg->vertBuf);
    sg_dealloc_buffer(sg->_sg, sg->vertBuf);

    if (sg->cindexes_gpu)
        sg_uninit_buffer(sg->_sg, sg->indexBuf);
    sg_dealloc_buffer(sg->_sg, sg->indexBuf);

    sgnvg__renderDeleteTexture(sg, sg->dummyTex);
    for (i = 0; i < sg->ntextures; i++)
    {
        if (sg->textures[i].img.id != 0 && (sg->textures[i].flags & NVG_IMAGE_NODELETE) == 0)
            sg_destroy_image(sg->_sg, sg->textures[i].img);
    }

    if (sg->textures)
    {
        SGNVG_FREE(sg->textures);
    }
    if (sg->calls)
    {
        SGNVG_FREE(sg->calls);
    }
    if (sg->paths)
    {
        SGNVG_FREE(sg->paths);
    }
    if (sg->verts)
    {
        SGNVG_FREE(sg->verts);
    }
    if (sg->indexes)
    {
        SGNVG_FREE(sg->indexes);
    }
    if (sg->uniforms)
    {
        SGNVG_FREE(sg->uniforms);
    }

    SGNVG_FREE(sg);
}

NVGcontext* nvgCreateSokol(_sg_state_t* _sg, int flags)
{
    SGNVG_EXTLOG("nvgCreateSokol(flags: %d)\n", flags);
    NVGparams     params;
    NVGcontext*   ctx = NULL;
    SGNVGcontext* sg  = (SGNVGcontext*)SGNVG_MALLOC(sizeof(SGNVGcontext));
    if (sg == NULL)
        goto error;
    memset(sg, 0, sizeof(SGNVGcontext));
    sg->_sg = _sg;

    memset(&params, 0, sizeof(params));
    params.renderCreate         = sgnvg__renderCreate;
    params.renderCreateTexture  = sgnvg__renderCreateTexture;
    params.renderDeleteTexture  = sgnvg__renderDeleteTexture;
    params.renderUpdateTexture  = sgnvg__renderUpdateTexture;
    params.renderGetTextureSize = sgnvg__renderGetTextureSize;
    params.renderViewport       = sgnvg__renderViewport;
    params.renderCancel         = sgnvg__renderCancel;
    params.renderFlush          = sgnvg__renderFlush;
    params.renderFill           = sgnvg__renderFill;
    params.renderStroke         = sgnvg__renderStroke;
    params.renderTriangles      = sgnvg__renderTriangles;
    params.renderDelete         = sgnvg__renderDelete;
    params.userPtr              = sg;
    params.edgeAntiAlias        = flags & NVG_ANTIALIAS ? 1 : 0;

    sg->flags = flags;

    ctx = nvgCreateInternal(&params);
    if (ctx == NULL)
        goto error;

    return ctx;

error:
    if (ctx != NULL)
        nvgDeleteInternal(ctx);
    return NULL;
}

void nvgDeleteSokol(NVGcontext* ctx) { nvgDeleteInternal(ctx); }

int nvsgCreateImageFromHandleSokol(
    NVGcontext* ctx,
    sg_image    imageSokol,
    sg_sampler  samplerSokol,
    int         type,
    int         w,
    int         h,
    int         flags)
{
    SGNVG_INTLOG(
        "nvsgCreateImageFromHandleSokol(ctx: %p, imageSokol: %d, samplerSokol: %d, type: %d, w: %d, h: %d, flags: "
        "%d)\n",
        ctx,
        imageSokol.id,
        samplerSokol.id,
        type,
        w,
        h,
        flags);
    SGNVGcontext* sg  = (SGNVGcontext*)nvgInternalParams(ctx)->userPtr;
    SGNVGtexture* tex = sgnvg__allocTexture(sg);

    if (tex == NULL)
        return 0;

    tex->type   = type;
    tex->img    = imageSokol;
    tex->smp    = samplerSokol;
    tex->flags  = flags;
    tex->width  = w;
    tex->height = h;

    return tex->id;
}

sg_image nvsgImageHandleSokol(NVGcontext* ctx, int image)
{
    SGNVG_INTLOG("nvsgImageHandleSokol(ctx: %p, image: %d)\n", ctx, image);
    SGNVGcontext* sg  = (SGNVGcontext*)nvgInternalParams(ctx)->userPtr;
    SGNVGtexture* tex = sgnvg__findTexture(sg, image);
    return tex->img;
}
