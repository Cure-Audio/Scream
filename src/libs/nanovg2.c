//
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

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
layout(location = 0) out vec4 outColour;

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
        // Calculate gradient colour using box gradient
        vec2 pt = (paintMat * vec3(fpos,1.0)).xy;
        float d = clamp((sdroundrect(pt, extent, radius) + feather*0.5) / feather, 0.0, 1.0);
        vec4 colour = mix(innerCol,outerCol,d);
        // Combine alpha
        colour *= strokeAlpha * scissor;
        result = colour;
    } else if (type == 1) {// Image
        // Calculate colour fron texture
        vec2 pt = (paintMat * vec3(fpos,1.0)).xy / extent;
        vec4 colour = texture(sampler2D(tex, smp), pt);
        if (texType == 1) colour = vec4(colour.xyz*colour.w,colour.w);
        if (texType == 2) colour = vec4(colour.x);
        // stencil support
        if (texType == 3 && colour.a == 1.0) discard;
        // Apply colour tint and alpha.
        colour *= innerCol;
        // Combine alpha
        colour *= strokeAlpha * scissor;
        result = colour;
    } else if (type == 2) {// Stencil fill
        result = vec4(1,1,1,1);
    } else if (type == 3) {// Textured tris
        vec4 colour = texture(sampler2D(tex, smp), ftcoord);
        if (texType == 1) colour = vec4(colour.xyz*colour.w,colour.w);
        if (texType == 2) colour = vec4(colour.x);
        result = colour * scissor * innerCol;
    }
    outColour = result;
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

@program ctx vs fs
>8

8< shd.aa.glsl

@module nanovg_aa

@vs vs_aa
@include internal_shd.vs.glsl
@end

@fs fs_aa
@include internal_shd.fs.glsl
@end

@program ctx vs_aa fs_aa

>8

*/

#include "nanovg2.h"

#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include <nanovg_sokol.glsl.h>

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#ifndef NVG_NO_STB
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4100) // unreferenced formal parameter
#pragma warning(disable : 4127) // conditional expression is constant
#pragma warning(disable : 4204) // nonstandard extension used : non-constant aggregate initializer
#pragma warning(disable : 4706) // assignment within conditional expression
#endif

#define NVG_INIT_FONTIMAGE_SIZE 512
#define NVG_MAX_FONTIMAGE_SIZE  2048

#define NVG_INIT_COMMANDS_SIZE 256
#define NVG_INIT_POINTS_SIZE   128
#define NVG_INIT_PATHS_SIZE    16
#define NVG_INIT_VERTS_SIZE    256

#define NVG_KAPPA90 0.5522847493f // Length proportional to radius of a cubic bezier handle for 90deg arcs.

#define NVG_COUNTOF(arr) (sizeof(arr) / sizeof(0 [arr]))

#define NVG_ASSERT_GOTO(cond, label)                                                                                   \
    NVG_ASSERT(cond);                                                                                                  \
    if (!(cond))                                                                                                       \
        goto label;

void nvg__renderText(NVGcontext* ctx, NVGvertex* verts, int nverts);

static float nvg__sqrtf(float a) { return sqrtf(a); }
static float nvg__modf(float a, float b) { return fmodf(a, b); }
static float nvg__sinf(float a) { return sinf(a); }
static float nvg__cosf(float a) { return cosf(a); }
static float nvg__tanf(float a) { return tanf(a); }
static float nvg__atan2f(float a, float b) { return atan2f(a, b); }
static float nvg__acosf(float a) { return acosf(a); }

static int   nvg__mini(int a, int b) { return a < b ? a : b; }
static int   nvg__maxi(int a, int b) { return a > b ? a : b; }
static int   nvg__clampi(int a, int mn, int mx) { return a < mn ? mn : (a > mx ? mx : a); }
static float nvg__minf(float a, float b) { return a < b ? a : b; }
static float nvg__maxf(float a, float b) { return a > b ? a : b; }
static float nvg__absf(float a) { return a >= 0.0f ? a : -a; }
static float nvg__signf(float a) { return a >= 0.0f ? 1.0f : -1.0f; }
static float nvg__clampf(float a, float mn, float mx) { return a < mn ? mn : (a > mx ? mx : a); }
static float nvg__cross(float dx0, float dy0, float dx1, float dy1) { return dx1 * dy0 - dx0 * dy1; }

static float nvg__normalize(float* x, float* y)
{
    float d = nvg__sqrtf((*x) * (*x) + (*y) * (*y));
    if (d > 1e-6f)
    {
        float id  = 1.0f / d;
        *x       *= id;
        *y       *= id;
    }
    return d;
}

static void nvg__setDevicePixelRatio(NVGcontext* ctx, float ratio)
{
    ctx->tessTol       = 0.25f / ratio;
    ctx->distTol       = 0.01f / ratio;
    ctx->fringeWidth   = 1.0f / ratio;
    ctx->devicePxRatio = ratio;
}

static NVGcompositeOperationState nvg__compositeOperationState(int op)
{
    int sfactor, dfactor;

    if (op == NVG_SOURCE_OVER)
    {
        sfactor = NVG_ONE;
        dfactor = NVG_ONE_MINUS_SRC_ALPHA;
    }
    else if (op == NVG_SOURCE_IN)
    {
        sfactor = NVG_DST_ALPHA;
        dfactor = NVG_ZERO;
    }
    else if (op == NVG_SOURCE_OUT)
    {
        sfactor = NVG_ONE_MINUS_DST_ALPHA;
        dfactor = NVG_ZERO;
    }
    else if (op == NVG_ATOP)
    {
        sfactor = NVG_DST_ALPHA;
        dfactor = NVG_ONE_MINUS_SRC_ALPHA;
    }
    else if (op == NVG_DESTINATION_OVER)
    {
        sfactor = NVG_ONE_MINUS_DST_ALPHA;
        dfactor = NVG_ONE;
    }
    else if (op == NVG_DESTINATION_IN)
    {
        sfactor = NVG_ZERO;
        dfactor = NVG_SRC_ALPHA;
    }
    else if (op == NVG_DESTINATION_OUT)
    {
        sfactor = NVG_ZERO;
        dfactor = NVG_ONE_MINUS_SRC_ALPHA;
    }
    else if (op == NVG_DESTINATION_ATOP)
    {
        sfactor = NVG_ONE_MINUS_DST_ALPHA;
        dfactor = NVG_SRC_ALPHA;
    }
    else if (op == NVG_LIGHTER)
    {
        sfactor = NVG_ONE;
        dfactor = NVG_ONE;
    }
    else if (op == NVG_COPY)
    {
        sfactor = NVG_ONE;
        dfactor = NVG_ZERO;
    }
    else if (op == NVG_XOR)
    {
        sfactor = NVG_ONE_MINUS_DST_ALPHA;
        dfactor = NVG_ONE_MINUS_SRC_ALPHA;
    }
    else
    {
        sfactor = NVG_ONE;
        dfactor = NVG_ZERO;
    }

    NVGcompositeOperationState state;
    state.srcRGB   = sfactor;
    state.dstRGB   = dfactor;
    state.srcAlpha = sfactor;
    state.dstAlpha = dfactor;
    return state;
}

void nvgReset(NVGcontext* ctx)
{
    NVGstate* state = &ctx->state;
    memset(state, 0, sizeof(*state));

    nvgSetColour(ctx, nvgRGBA(0, 0, 0, 255));
    state->compositeOperation = nvg__compositeOperationState(NVG_SOURCE_OVER);
    state->shapeAntiAlias     = 1;
    state->strokeWidth        = 1.0f;
    state->miterLimit         = 10.0f;
    state->lineCap            = NVG_BUTT;
    state->lineJoin           = NVG_MITER;
    nvgTransformIdentity(state->xform);

    state->scissor.extent[0] = -1.0f;
    state->scissor.extent[1] = -1.0f;

    state->fontSize      = 16.0f;
    state->letterSpacing = 0.0f;
    state->lineHeight    = 1.0f;
    state->fontBlur      = 0.0f;
    state->textAlign     = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
    state->fontId        = 0;
}

NVGcolour nvgRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    NVGcolour colour;
    // Use longer initialization to suppress warning.
    colour.r = r / 255.0f;
    colour.g = g / 255.0f;
    colour.b = b / 255.0f;
    colour.a = a / 255.0f;
    return colour;
}

NVGcolour nvgRGBAf(float r, float g, float b, float a)
{
    NVGcolour colour;
    // Use longer initialization to suppress warning.
    colour.r = r;
    colour.g = g;
    colour.b = b;
    colour.a = a;
    return colour;
}

NVGcolour nvgLerpRGBA(NVGcolour c0, NVGcolour c1, float u)
{
    int       i;
    float     oneminu;
    NVGcolour cint = {{{0}}};

    u       = nvg__clampf(u, 0.0f, 1.0f);
    oneminu = 1.0f - u;
    for (i = 0; i < 4; i++)
    {
        cint.rgba[i] = c0.rgba[i] * oneminu + c1.rgba[i] * u;
    }

    return cint;
}

NVGcolour nvgHSL(float h, float s, float l) { return nvgHSLA(h, s, l, 255); }

static float nvg__hue(float h, float m1, float m2)
{
    if (h < 0)
        h += 1;
    if (h > 1)
        h -= 1;
    if (h < 1.0f / 6.0f)
        return m1 + (m2 - m1) * h * 6.0f;
    else if (h < 3.0f / 6.0f)
        return m2;
    else if (h < 4.0f / 6.0f)
        return m1 + (m2 - m1) * (2.0f / 3.0f - h) * 6.0f;
    return m1;
}

NVGcolour nvgHSLA(float h, float s, float l, unsigned char a)
{
    float     m1, m2;
    NVGcolour col;
    h = nvg__modf(h, 1.0f);
    if (h < 0.0f)
        h += 1.0f;
    s     = nvg__clampf(s, 0.0f, 1.0f);
    l     = nvg__clampf(l, 0.0f, 1.0f);
    m2    = l <= 0.5f ? (l * (1 + s)) : (l + s - l * s);
    m1    = 2 * l - m2;
    col.r = nvg__clampf(nvg__hue(h + 1.0f / 3.0f, m1, m2), 0.0f, 1.0f);
    col.g = nvg__clampf(nvg__hue(h, m1, m2), 0.0f, 1.0f);
    col.b = nvg__clampf(nvg__hue(h - 1.0f / 3.0f, m1, m2), 0.0f, 1.0f);
    col.a = a / 255.0f;
    return col;
}

void nvgTransformIdentity(float* t)
{
    t[0] = 1.0f;
    t[1] = 0.0f;
    t[2] = 0.0f;
    t[3] = 1.0f;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void nvgTransformTranslate(float* t, float tx, float ty)
{
    t[0] = 1.0f;
    t[1] = 0.0f;
    t[2] = 0.0f;
    t[3] = 1.0f;
    t[4] = tx;
    t[5] = ty;
}

void nvgTransformScale(float* t, float sx, float sy)
{
    t[0] = sx;
    t[1] = 0.0f;
    t[2] = 0.0f;
    t[3] = sy;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void nvgTransformRotate(float* t, float a)
{
    float cs = nvg__cosf(a), sn = nvg__sinf(a);
    t[0] = cs;
    t[1] = sn;
    t[2] = -sn;
    t[3] = cs;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void nvgTransformSkewX(float* t, float a)
{
    t[0] = 1.0f;
    t[1] = 0.0f;
    t[2] = nvg__tanf(a);
    t[3] = 1.0f;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void nvgTransformSkewY(float* t, float a)
{
    t[0] = 1.0f;
    t[1] = nvg__tanf(a);
    t[2] = 0.0f;
    t[3] = 1.0f;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void nvgTransformMultiply(float* t, const float* s)
{
    float t0 = t[0] * s[0] + t[1] * s[2];
    float t2 = t[2] * s[0] + t[3] * s[2];
    float t4 = t[4] * s[0] + t[5] * s[2] + s[4];
    t[1]     = t[0] * s[1] + t[1] * s[3];
    t[3]     = t[2] * s[1] + t[3] * s[3];
    t[5]     = t[4] * s[1] + t[5] * s[3] + s[5];
    t[0]     = t0;
    t[2]     = t2;
    t[4]     = t4;
}

void nvgTransformPremultiply(float* t, const float* s)
{
    float s2[6];
    memcpy(s2, s, sizeof(float) * 6);
    nvgTransformMultiply(s2, t);
    memcpy(t, s2, sizeof(float) * 6);
}

int nvgTransformInverse(float* inv, const float* t)
{
    double invdet, det = (double)t[0] * t[3] - (double)t[2] * t[1];
    if (det > -1e-6 && det < 1e-6)
    {
        nvgTransformIdentity(inv);
        return 0;
    }
    invdet = 1.0 / det;
    inv[0] = (float)(t[3] * invdet);
    inv[2] = (float)(-t[2] * invdet);
    inv[4] = (float)(((double)t[2] * t[5] - (double)t[3] * t[4]) * invdet);
    inv[1] = (float)(-t[1] * invdet);
    inv[3] = (float)(t[0] * invdet);
    inv[5] = (float)(((double)t[1] * t[4] - (double)t[0] * t[5]) * invdet);
    return 1;
}

void nvgTransformPoint(float* dx, float* dy, const float* t, float sx, float sy)
{
    *dx = sx * t[0] + sy * t[2] + t[4];
    *dy = sx * t[1] + sy * t[3] + t[5];
}

float nvgDegToRad(float deg) { return deg / 180.0f * NVG_PI; }

float nvgRadToDeg(float rad) { return rad / NVG_PI * 180.0f; }

// Transforms
void nvgTransform(NVGcontext* ctx, float a, float b, float c, float d, float e, float f)
{
    NVGstate* state = &ctx->state;
    float     t[6]  = {a, b, c, d, e, f};
    nvgTransformPremultiply(state->xform, t);
}

void nvgResetTransform(NVGcontext* ctx)
{
    NVGstate* state = &ctx->state;
    nvgTransformIdentity(state->xform);
}

void nvgTranslate(NVGcontext* ctx, float x, float y)
{
    NVGstate* state = &ctx->state;
    float     t[6];
    nvgTransformTranslate(t, x, y);
    nvgTransformPremultiply(state->xform, t);
}

void nvgRotate(NVGcontext* ctx, float angle)
{
    NVGstate* state = &ctx->state;
    float     t[6];
    nvgTransformRotate(t, angle);
    nvgTransformPremultiply(state->xform, t);
}

void nvgSkewX(NVGcontext* ctx, float angle)
{
    NVGstate* state = &ctx->state;
    float     t[6];
    nvgTransformSkewX(t, angle);
    nvgTransformPremultiply(state->xform, t);
}

void nvgSkewY(NVGcontext* ctx, float angle)
{
    NVGstate* state = &ctx->state;
    float     t[6];
    nvgTransformSkewY(t, angle);
    nvgTransformPremultiply(state->xform, t);
}

void nvgScale(NVGcontext* ctx, float x, float y)
{
    NVGstate* state = &ctx->state;
    float     t[6];
    nvgTransformScale(t, x, y);
    nvgTransformPremultiply(state->xform, t);
}

void nvgCurrentTransform(NVGcontext* ctx, float* xform)
{
    NVGstate* state = &ctx->state;
    if (xform == NULL)
        return;
    memcpy(xform, state->xform, sizeof(float) * 6);
}

void nvgSetColour(NVGcontext* ctx, NVGcolour colour)
{
    NVGstate* state = &ctx->state;
    NVGpaint* p     = &state->paint;
    memset(&state->paint, 0, sizeof(*p));
    nvgTransformIdentity(p->xform);
    p->radius      = 0.0f;
    p->feather     = 1.0f;
    p->innerColour = colour;
    p->outerColour = colour;
}

#ifndef NVG_NO_STB
int nvgCreateImage(NVGcontext* ctx, const char* filename, int imageFlags)
{
    int            w, h, n, image;
    unsigned char* img;
    stbi_set_unpremultiply_on_load(1);
    stbi_convert_iphone_png_to_rgb(1);
    img = stbi_load(filename, &w, &h, &n, 4);
    if (img == NULL)
    {
        //		printf("Failed to load %s - %s\n", filename, stbi_failure_reason());
        return 0;
    }
    image = nvgCreateImageRGBA(ctx, w, h, imageFlags, img);
    stbi_image_free(img);
    return image;
}

int nvgCreateImageMem(NVGcontext* ctx, int imageFlags, unsigned char* data, int ndata)
{
    int            w, h, n, image;
    unsigned char* img = stbi_load_from_memory(data, ndata, &w, &h, &n, 4);
    if (img == NULL)
    {
        //		printf("Failed to load %s - %s\n", filename, stbi_failure_reason());
        return 0;
    }
    image = nvgCreateImageRGBA(ctx, w, h, imageFlags, img);
    stbi_image_free(img);
    return image;
}
#endif

int nvgCreateImageRGBA(NVGcontext* ctx, int w, int h, int imageFlags, const unsigned char* data)
{
    return nvgCreateTexture(ctx, NVG_TEXTURE_RGBA, w, h, imageFlags, data);
}

void nvgUpdateImage(NVGcontext* ctx, int image, const unsigned char* data)
{
    int w, h;
    nvgGetImageSize(ctx, image, &w, &h);
    nvgUpdateTexture(ctx, image, 0, 0, w, h, data);
}

void nvgDeleteImage(NVGcontext* ctx, int image)
{
    int i;
    for (i = 0; i < ctx->ntextures; i++)
    {
        SGNVGtexture* tex = &ctx->textures[i];
        if (tex->img.id == image)
        {
            if (tex->img.id != 0 && (tex->flags & NVG_IMAGE_NODELETE) == 0)
                sg_destroy_image(tex->img);
            if (tex->imgData)
            {
                NVG_FREE(tex->imgData);
            }
            memset(tex, 0, sizeof(*tex));
            return;
        }
    }
    NVG_ASSERT(0);
}

NVGpaint nvgLinearGradient(NVGcontext* ctx, float sx, float sy, float ex, float ey, NVGcolour icol, NVGcolour ocol)
{
    NVGpaint    p;
    float       dx, dy, d;
    const float large = 1e5;
    NVG_NOTUSED(ctx);
    memset(&p, 0, sizeof(p));

    // Calculate transform aligned to the line
    dx = ex - sx;
    dy = ey - sy;
    d  = sqrtf(dx * dx + dy * dy);
    if (d > 0.0001f)
    {
        dx /= d;
        dy /= d;
    }
    else
    {
        dx = 0;
        dy = 1;
    }

    p.xform[0] = dy;
    p.xform[1] = -dx;
    p.xform[2] = dx;
    p.xform[3] = dy;
    p.xform[4] = sx - dx * large;
    p.xform[5] = sy - dy * large;

    p.extent[0] = large;
    p.extent[1] = large + d * 0.5f;

    p.radius = 0.0f;

    p.feather = nvg__maxf(1.0f, d);

    p.innerColour = icol;
    p.outerColour = ocol;

    return p;
}

NVGpaint nvgRadialGradient(NVGcontext* ctx, float cx, float cy, float inr, float outr, NVGcolour icol, NVGcolour ocol)
{
    NVGpaint p;
    float    r = (inr + outr) * 0.5f;
    float    f = (outr - inr);
    NVG_NOTUSED(ctx);
    memset(&p, 0, sizeof(p));

    nvgTransformIdentity(p.xform);
    p.xform[4] = cx;
    p.xform[5] = cy;

    p.extent[0] = r;
    p.extent[1] = r;

    p.radius = r;

    p.feather = nvg__maxf(1.0f, f);

    p.innerColour = icol;
    p.outerColour = ocol;

    return p;
}

NVGpaint
nvgBoxGradient(NVGcontext* ctx, float x, float y, float w, float h, float r, float f, NVGcolour icol, NVGcolour ocol)
{
    NVGpaint p;
    NVG_NOTUSED(ctx);
    memset(&p, 0, sizeof(p));

    nvgTransformIdentity(p.xform);
    p.xform[4] = x + w * 0.5f;
    p.xform[5] = y + h * 0.5f;

    p.extent[0] = w * 0.5f;
    p.extent[1] = h * 0.5f;

    p.radius = r;

    p.feather = nvg__maxf(1.0f, f);

    p.innerColour = icol;
    p.outerColour = ocol;

    return p;
}

NVGpaint nvgImagePattern(NVGcontext* ctx, float cx, float cy, float w, float h, float angle, int image, float alpha)
{
    NVGpaint p;
    NVG_NOTUSED(ctx);
    memset(&p, 0, sizeof(p));

    nvgTransformRotate(p.xform, angle);
    p.xform[4] = cx;
    p.xform[5] = cy;

    p.extent[0] = w;
    p.extent[1] = h;

    p.image = image;

    p.innerColour = p.outerColour = nvgRGBAf(1, 1, 1, alpha);

    return p;
}

// Scissoring
void nvgSetScissor(NVGcontext* ctx, float x, float y, float w, float h)
{
    NVGstate* state = &ctx->state;

    w = nvg__maxf(0.0f, w);
    h = nvg__maxf(0.0f, h);

    nvgTransformIdentity(state->scissor.xform);
    state->scissor.xform[4] = x + w * 0.5f;
    state->scissor.xform[5] = y + h * 0.5f;
    // nvgTransformMultiply(state->scissor.xform, state->xform);

    state->scissor.extent[0] = w * 0.5f;
    state->scissor.extent[1] = h * 0.5f;
}

static void nvg__isectRects(float* dst, float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    float minx = nvg__maxf(ax, bx);
    float miny = nvg__maxf(ay, by);
    float maxx = nvg__minf(ax + aw, bx + bw);
    float maxy = nvg__minf(ay + ah, by + bh);
    dst[0]     = minx;
    dst[1]     = miny;
    dst[2]     = nvg__maxf(0.0f, maxx - minx);
    dst[3]     = nvg__maxf(0.0f, maxy - miny);
}

void nvgIntersectScissor(NVGcontext* ctx, float x, float y, float w, float h)
{
    NVGstate* state = &ctx->state;
    float     pxform[6], invxorm[6];
    float     rect[4];
    float     ex, ey, tex, tey;

    // If no previous scissor has been set, set the scissor as current scissor.
    if (state->scissor.extent[0] < 0)
    {
        nvgSetScissor(ctx, x, y, w, h);
        return;
    }

    // Transform the current scissor rect into current transform space.
    // If there is difference in rotation, this will be approximation.
    memcpy(pxform, state->scissor.xform, sizeof(float) * 6);
    ex = state->scissor.extent[0];
    ey = state->scissor.extent[1];
    nvgTransformInverse(invxorm, state->xform);
    nvgTransformMultiply(pxform, invxorm);
    tex = ex * nvg__absf(pxform[0]) + ey * nvg__absf(pxform[2]);
    tey = ex * nvg__absf(pxform[1]) + ey * nvg__absf(pxform[3]);

    // Intersect rects.
    nvg__isectRects(rect, pxform[4] - tex, pxform[5] - tey, tex * 2, tey * 2, x, y, w, h);

    nvgSetScissor(ctx, rect[0], rect[1], rect[2], rect[3]);
}

void nvgResetScissor(NVGcontext* ctx)
{
    NVGstate* state = &ctx->state;
    memset(state->scissor.xform, 0, sizeof(state->scissor.xform));
    state->scissor.extent[0] = -1.0f;
    state->scissor.extent[1] = -1.0f;
}

// Global composite operation.
void nvgSetGlobalCompositeOperation(NVGcontext* ctx, int op)
{
    NVGstate* state           = &ctx->state;
    state->compositeOperation = nvg__compositeOperationState(op);
}

void nvgSetGlobalCompositeBlendFunc(NVGcontext* ctx, int sfactor, int dfactor)
{
    nvgSetGlobalCompositeBlendFuncSeparate(ctx, sfactor, dfactor, sfactor, dfactor);
}

void nvgSetGlobalCompositeBlendFuncSeparate(NVGcontext* ctx, int srcRGB, int dstRGB, int srcAlpha, int dstAlpha)
{
    NVGcompositeOperationState op;
    op.srcRGB   = srcRGB;
    op.dstRGB   = dstRGB;
    op.srcAlpha = srcAlpha;
    op.dstAlpha = dstAlpha;

    NVGstate* state           = &ctx->state;
    state->compositeOperation = op;
}

static int nvg__ptEquals(float x1, float y1, float x2, float y2, float tol)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    return dx * dx + dy * dy < tol * tol;
}

static float nvg__distPtSeg(float x, float y, float px, float py, float qx, float qy)
{
    float pqx, pqy, dx, dy, d, t;
    pqx = qx - px;
    pqy = qy - py;
    dx  = x - px;
    dy  = y - py;
    d   = pqx * pqx + pqy * pqy;
    t   = pqx * dx + pqy * dy;
    if (d > 0)
        t /= d;
    if (t < 0)
        t = 0;
    else if (t > 1)
        t = 1;
    dx = px + t * pqx - x;
    dy = py + t * pqy - y;
    return dx * dx + dy * dy;
}

static void nvg__appendCommands(NVGcontext* ctx, float* vals, int nvals)
{
    NVGstate* state = &ctx->state;
    int       i;

    if (ctx->ncommands + nvals > ctx->ccommands)
    {
        float* commands;
        int    ccommands = ctx->ncommands + nvals + ctx->ccommands / 2;
        commands         = (float*)NVG_REALLOC(ctx->commands, sizeof(float) * ccommands);
        if (commands == NULL)
            return;
        ctx->commands  = commands;
        ctx->ccommands = ccommands;
    }

    if ((int)vals[0] != NVG_CLOSE && (int)vals[0] != NVG_WINDING)
    {
        ctx->commandx = vals[nvals - 2];
        ctx->commandy = vals[nvals - 1];
    }

    // transform commands
    i = 0;
    while (i < nvals)
    {
        int cmd = (int)vals[i];
        switch (cmd)
        {
        case NVG_MOVETO:
            nvgTransformPoint(&vals[i + 1], &vals[i + 2], state->xform, vals[i + 1], vals[i + 2]);
            i += 3;
            break;
        case NVG_LINETO:
            nvgTransformPoint(&vals[i + 1], &vals[i + 2], state->xform, vals[i + 1], vals[i + 2]);
            i += 3;
            break;
        case NVG_BEZIERTO:
            nvgTransformPoint(&vals[i + 1], &vals[i + 2], state->xform, vals[i + 1], vals[i + 2]);
            nvgTransformPoint(&vals[i + 3], &vals[i + 4], state->xform, vals[i + 3], vals[i + 4]);
            nvgTransformPoint(&vals[i + 5], &vals[i + 6], state->xform, vals[i + 5], vals[i + 6]);
            i += 7;
            break;
        case NVG_CLOSE:
            i++;
            break;
        case NVG_WINDING:
            i += 2;
            break;
        default:
            i++;
        }
    }

    memcpy(&ctx->commands[ctx->ncommands], vals, nvals * sizeof(float));

    ctx->ncommands += nvals;
}

static NVGpath* nvg__lastPath(NVGcontext* ctx)
{
    if (ctx->cache.npaths > 0)
        return &ctx->cache.paths[ctx->cache.npaths - 1];
    return NULL;
}

static void nvg__addPath(NVGcontext* ctx)
{
    NVGpath* path;
    if (ctx->cache.npaths + 1 > ctx->cache.cpaths)
    {
        NVGpath* paths;
        int      cpaths = ctx->cache.npaths + 1 + ctx->cache.cpaths / 2;
        paths           = (NVGpath*)NVG_REALLOC(ctx->cache.paths, sizeof(NVGpath) * cpaths);
        if (paths == NULL)
            return;
        ctx->cache.paths  = paths;
        ctx->cache.cpaths = cpaths;
    }
    path = &ctx->cache.paths[ctx->cache.npaths];
    memset(path, 0, sizeof(*path));
    path->first   = ctx->cache.npoints;
    path->winding = NVG_CCW;

    ctx->cache.npaths++;
}

static NVGpoint* nvg__lastPoint(NVGcontext* ctx)
{
    if (ctx->cache.npoints > 0)
        return &ctx->cache.points[ctx->cache.npoints - 1];
    return NULL;
}

static void nvg__addPoint(NVGcontext* ctx, float x, float y, int flags)
{
    NVGpath*  path = nvg__lastPath(ctx);
    NVGpoint* pt;
    if (path == NULL)
        return;

    if (path->count > 0 && ctx->cache.npoints > 0)
    {
        pt = nvg__lastPoint(ctx);
        if (nvg__ptEquals(pt->x, pt->y, x, y, ctx->distTol))
        {
            pt->flags |= flags;
            return;
        }
    }

    if (ctx->cache.npoints + 1 > ctx->cache.cpoints)
    {
        NVGpoint* points;
        int       cpoints = ctx->cache.npoints + 1 + ctx->cache.cpoints / 2;
        points            = (NVGpoint*)NVG_REALLOC(ctx->cache.points, sizeof(NVGpoint) * cpoints);
        if (points == NULL)
            return;
        ctx->cache.points  = points;
        ctx->cache.cpoints = cpoints;
    }

    pt = &ctx->cache.points[ctx->cache.npoints];
    memset(pt, 0, sizeof(*pt));
    pt->x     = x;
    pt->y     = y;
    pt->flags = (unsigned char)flags;

    ctx->cache.npoints++;
    path->count++;
}

static void nvg__closePath(NVGcontext* ctx)
{
    NVGpath* path = nvg__lastPath(ctx);
    if (path == NULL)
        return;
    path->closed = 1;
}

static void nvg__pathWinding(NVGcontext* ctx, int winding)
{
    NVGpath* path = nvg__lastPath(ctx);
    if (path == NULL)
        return;
    path->winding = winding;
}

static float nvg__getAverageScale(float* t)
{
    float sx = sqrtf(t[0] * t[0] + t[2] * t[2]);
    float sy = sqrtf(t[1] * t[1] + t[3] * t[3]);
    return (sx + sy) * 0.5f;
}

static NVGvertex* nvg__allocTempVerts(NVGcontext* ctx, int nverts)
{
    if (nverts > ctx->cache.cverts)
    {
        NVGvertex* verts;
        int cverts = (nverts + 0xff) & ~0xff; // Round up to prevent allocations when things change just slightly.
        verts      = (NVGvertex*)NVG_REALLOC(ctx->cache.verts, sizeof(NVGvertex) * cverts);
        if (verts == NULL)
            return NULL;
        ctx->cache.verts  = verts;
        ctx->cache.cverts = cverts;
    }

    return ctx->cache.verts;
}

static float nvg__triarea2(float ax, float ay, float bx, float by, float cx, float cy)
{
    float abx = bx - ax;
    float aby = by - ay;
    float acx = cx - ax;
    float acy = cy - ay;
    return acx * aby - abx * acy;
}

static float nvg__polyArea(NVGpoint* pts, int npts)
{
    int   i;
    float area = 0;
    for (i = 2; i < npts; i++)
    {
        NVGpoint* a  = &pts[0];
        NVGpoint* b  = &pts[i - 1];
        NVGpoint* c  = &pts[i];
        area        += nvg__triarea2(a->x, a->y, b->x, b->y, c->x, c->y);
    }
    return area * 0.5f;
}

static void nvg__polyReverse(NVGpoint* pts, int npts)
{
    NVGpoint tmp;
    int      i = 0, j = npts - 1;
    while (i < j)
    {
        tmp    = pts[i];
        pts[i] = pts[j];
        pts[j] = tmp;
        i++;
        j--;
    }
}

static void nvg__vset(NVGvertex* vtx, float x, float y, float u, float v)
{
    vtx->x = x;
    vtx->y = y;
    vtx->u = u;
    vtx->v = v;
}

static void nvg__tesselateBezier(
    NVGcontext* ctx,
    float       x1,
    float       y1,
    float       x2,
    float       y2,
    float       x3,
    float       y3,
    float       x4,
    float       y4,
    int         level,
    int         type)
{
    float x12, y12, x23, y23, x34, y34, x123, y123, x234, y234, x1234, y1234;
    float dx, dy, d2, d3;

    if (level > 10)
        return;

    x12  = (x1 + x2) * 0.5f;
    y12  = (y1 + y2) * 0.5f;
    x23  = (x2 + x3) * 0.5f;
    y23  = (y2 + y3) * 0.5f;
    x34  = (x3 + x4) * 0.5f;
    y34  = (y3 + y4) * 0.5f;
    x123 = (x12 + x23) * 0.5f;
    y123 = (y12 + y23) * 0.5f;

    dx = x4 - x1;
    dy = y4 - y1;
    d2 = nvg__absf(((x2 - x4) * dy - (y2 - y4) * dx));
    d3 = nvg__absf(((x3 - x4) * dy - (y3 - y4) * dx));

    if ((d2 + d3) * (d2 + d3) < ctx->tessTol * (dx * dx + dy * dy))
    {
        nvg__addPoint(ctx, x4, y4, type);
        return;
    }

    /*	if (nvg__absf(x1+x3-x2-x2) + nvg__absf(y1+y3-y2-y2) + nvg__absf(x2+x4-x3-x3) + nvg__absf(y2+y4-y3-y3) <
       ctx->tessTol) { nvg__addPoint(ctx, x4, y4, type); return;
            }*/

    x234  = (x23 + x34) * 0.5f;
    y234  = (y23 + y34) * 0.5f;
    x1234 = (x123 + x234) * 0.5f;
    y1234 = (y123 + y234) * 0.5f;

    nvg__tesselateBezier(ctx, x1, y1, x12, y12, x123, y123, x1234, y1234, level + 1, 0);
    nvg__tesselateBezier(ctx, x1234, y1234, x234, y234, x34, y34, x4, y4, level + 1, type);
}

static void nvg__flattenPaths(NVGcontext* ctx)
{
    NVGpathCache* cache = &ctx->cache;
    //	NVGstate* state = &ctx->state;
    NVGpoint* last;
    NVGpoint* p0;
    NVGpoint* p1;
    NVGpoint* pts;
    NVGpath*  path;
    int       i, j;
    float*    cp1;
    float*    cp2;
    float*    p;
    float     area;

    if (cache->npaths > 0)
        return;

    // Flatten
    i = 0;
    while (i < ctx->ncommands)
    {
        int cmd = (int)ctx->commands[i];
        switch (cmd)
        {
        case NVG_MOVETO:
            nvg__addPath(ctx);
            p = &ctx->commands[i + 1];
            nvg__addPoint(ctx, p[0], p[1], NVG_PT_CORNER);
            i += 3;
            break;
        case NVG_LINETO:
            p = &ctx->commands[i + 1];
            nvg__addPoint(ctx, p[0], p[1], NVG_PT_CORNER);
            i += 3;
            break;
        case NVG_BEZIERTO:
            last = nvg__lastPoint(ctx);
            if (last != NULL)
            {
                cp1 = &ctx->commands[i + 1];
                cp2 = &ctx->commands[i + 3];
                p   = &ctx->commands[i + 5];
                nvg__tesselateBezier(
                    ctx,
                    last->x,
                    last->y,
                    cp1[0],
                    cp1[1],
                    cp2[0],
                    cp2[1],
                    p[0],
                    p[1],
                    0,
                    NVG_PT_CORNER);
            }
            i += 7;
            break;
        case NVG_CLOSE:
            nvg__closePath(ctx);
            i++;
            break;
        case NVG_WINDING:
            nvg__pathWinding(ctx, (int)ctx->commands[i + 1]);
            i += 2;
            break;
        default:
            i++;
        }
    }

    cache->bounds[0] = cache->bounds[1] = 1e6f;
    cache->bounds[2] = cache->bounds[3] = -1e6f;

    // Calculate the direction and length of line segments.
    for (j = 0; j < cache->npaths; j++)
    {
        path = &cache->paths[j];
        pts  = &cache->points[path->first];

        // If the first and last points are the same, remove the last, mark as closed path.
        p0 = &pts[path->count - 1];
        p1 = &pts[0];
        if (nvg__ptEquals(p0->x, p0->y, p1->x, p1->y, ctx->distTol))
        {
            path->count--;
            p0           = &pts[path->count - 1];
            path->closed = 1;
        }

        // Enforce winding.
        if (path->count > 2)
        {
            area = nvg__polyArea(pts, path->count);
            if (path->winding == NVG_CCW && area < 0.0f)
                nvg__polyReverse(pts, path->count);
            if (path->winding == NVG_CW && area > 0.0f)
                nvg__polyReverse(pts, path->count);
        }

        for (i = 0; i < path->count; i++)
        {
            // Calculate segment direction and length
            p0->dx  = p1->x - p0->x;
            p0->dy  = p1->y - p0->y;
            p0->len = nvg__normalize(&p0->dx, &p0->dy);
            // Update bounds
            cache->bounds[0] = nvg__minf(cache->bounds[0], p0->x);
            cache->bounds[1] = nvg__minf(cache->bounds[1], p0->y);
            cache->bounds[2] = nvg__maxf(cache->bounds[2], p0->x);
            cache->bounds[3] = nvg__maxf(cache->bounds[3], p0->y);
            // Advance
            p0 = p1++;
        }
    }
}

static int nvg__curveDivs(float r, float arc, float tol)
{
    float da = acosf(r / (r + tol)) * 2.0f;
    return nvg__maxi(2, (int)ceilf(arc / da));
}

static void nvg__chooseBevel(int bevel, NVGpoint* p0, NVGpoint* p1, float w, float* x0, float* y0, float* x1, float* y1)
{
    if (bevel)
    {
        *x0 = p1->x + p0->dy * w;
        *y0 = p1->y - p0->dx * w;
        *x1 = p1->x + p1->dy * w;
        *y1 = p1->y - p1->dx * w;
    }
    else
    {
        *x0 = p1->x + p1->dmx * w;
        *y0 = p1->y + p1->dmy * w;
        *x1 = p1->x + p1->dmx * w;
        *y1 = p1->y + p1->dmy * w;
    }
}

static NVGvertex* nvg__roundJoin(
    NVGvertex* dst,
    NVGpoint*  p0,
    NVGpoint*  p1,
    float      lw,
    float      rw,
    float      lu,
    float      ru,
    int        ncap,
    float      fringe)
{
    int   i, n;
    float dlx0 = p0->dy;
    float dly0 = -p0->dx;
    float dlx1 = p1->dy;
    float dly1 = -p1->dx;
    NVG_NOTUSED(fringe);

    if (p1->flags & NVG_PT_LEFT)
    {
        float lx0, ly0, lx1, ly1, a0, a1;
        nvg__chooseBevel(p1->flags & NVG_PR_INNERBEVEL, p0, p1, lw, &lx0, &ly0, &lx1, &ly1);
        a0 = atan2f(-dly0, -dlx0);
        a1 = atan2f(-dly1, -dlx1);
        if (a1 > a0)
            a1 -= NVG_PI * 2;

        nvg__vset(dst, lx0, ly0, lu, 1);
        dst++;
        nvg__vset(dst, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1);
        dst++;

        n = nvg__clampi((int)ceilf(((a0 - a1) / NVG_PI) * ncap), 2, ncap);
        for (i = 0; i < n; i++)
        {
            float u  = i / (float)(n - 1);
            float a  = a0 + u * (a1 - a0);
            float rx = p1->x + cosf(a) * rw;
            float ry = p1->y + sinf(a) * rw;
            nvg__vset(dst, p1->x, p1->y, 0.5f, 1);
            dst++;
            nvg__vset(dst, rx, ry, ru, 1);
            dst++;
        }

        nvg__vset(dst, lx1, ly1, lu, 1);
        dst++;
        nvg__vset(dst, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1);
        dst++;
    }
    else
    {
        float rx0, ry0, rx1, ry1, a0, a1;
        nvg__chooseBevel(p1->flags & NVG_PR_INNERBEVEL, p0, p1, -rw, &rx0, &ry0, &rx1, &ry1);
        a0 = atan2f(dly0, dlx0);
        a1 = atan2f(dly1, dlx1);
        if (a1 < a0)
            a1 += NVG_PI * 2;

        nvg__vset(dst, p1->x + dlx0 * rw, p1->y + dly0 * rw, lu, 1);
        dst++;
        nvg__vset(dst, rx0, ry0, ru, 1);
        dst++;

        n = nvg__clampi((int)ceilf(((a1 - a0) / NVG_PI) * ncap), 2, ncap);
        for (i = 0; i < n; i++)
        {
            float u  = i / (float)(n - 1);
            float a  = a0 + u * (a1 - a0);
            float lx = p1->x + cosf(a) * lw;
            float ly = p1->y + sinf(a) * lw;
            nvg__vset(dst, lx, ly, lu, 1);
            dst++;
            nvg__vset(dst, p1->x, p1->y, 0.5f, 1);
            dst++;
        }

        nvg__vset(dst, p1->x + dlx1 * rw, p1->y + dly1 * rw, lu, 1);
        dst++;
        nvg__vset(dst, rx1, ry1, ru, 1);
        dst++;
    }
    return dst;
}

static NVGvertex*
nvg__bevelJoin(NVGvertex* dst, NVGpoint* p0, NVGpoint* p1, float lw, float rw, float lu, float ru, float fringe)
{
    float rx0, ry0, rx1, ry1;
    float lx0, ly0, lx1, ly1;
    float dlx0 = p0->dy;
    float dly0 = -p0->dx;
    float dlx1 = p1->dy;
    float dly1 = -p1->dx;
    NVG_NOTUSED(fringe);

    if (p1->flags & NVG_PT_LEFT)
    {
        nvg__chooseBevel(p1->flags & NVG_PR_INNERBEVEL, p0, p1, lw, &lx0, &ly0, &lx1, &ly1);

        nvg__vset(dst, lx0, ly0, lu, 1);
        dst++;
        nvg__vset(dst, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1);
        dst++;

        if (p1->flags & NVG_PT_BEVEL)
        {
            nvg__vset(dst, lx0, ly0, lu, 1);
            dst++;
            nvg__vset(dst, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1);
            dst++;

            nvg__vset(dst, lx1, ly1, lu, 1);
            dst++;
            nvg__vset(dst, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1);
            dst++;
        }
        else
        {
            rx0 = p1->x - p1->dmx * rw;
            ry0 = p1->y - p1->dmy * rw;

            nvg__vset(dst, p1->x, p1->y, 0.5f, 1);
            dst++;
            nvg__vset(dst, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1);
            dst++;

            nvg__vset(dst, rx0, ry0, ru, 1);
            dst++;
            nvg__vset(dst, rx0, ry0, ru, 1);
            dst++;

            nvg__vset(dst, p1->x, p1->y, 0.5f, 1);
            dst++;
            nvg__vset(dst, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1);
            dst++;
        }

        nvg__vset(dst, lx1, ly1, lu, 1);
        dst++;
        nvg__vset(dst, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1);
        dst++;
    }
    else
    {
        nvg__chooseBevel(p1->flags & NVG_PR_INNERBEVEL, p0, p1, -rw, &rx0, &ry0, &rx1, &ry1);

        nvg__vset(dst, p1->x + dlx0 * lw, p1->y + dly0 * lw, lu, 1);
        dst++;
        nvg__vset(dst, rx0, ry0, ru, 1);
        dst++;

        if (p1->flags & NVG_PT_BEVEL)
        {
            nvg__vset(dst, p1->x + dlx0 * lw, p1->y + dly0 * lw, lu, 1);
            dst++;
            nvg__vset(dst, rx0, ry0, ru, 1);
            dst++;

            nvg__vset(dst, p1->x + dlx1 * lw, p1->y + dly1 * lw, lu, 1);
            dst++;
            nvg__vset(dst, rx1, ry1, ru, 1);
            dst++;
        }
        else
        {
            lx0 = p1->x + p1->dmx * lw;
            ly0 = p1->y + p1->dmy * lw;

            nvg__vset(dst, p1->x + dlx0 * lw, p1->y + dly0 * lw, lu, 1);
            dst++;
            nvg__vset(dst, p1->x, p1->y, 0.5f, 1);
            dst++;

            nvg__vset(dst, lx0, ly0, lu, 1);
            dst++;
            nvg__vset(dst, lx0, ly0, lu, 1);
            dst++;

            nvg__vset(dst, p1->x + dlx1 * lw, p1->y + dly1 * lw, lu, 1);
            dst++;
            nvg__vset(dst, p1->x, p1->y, 0.5f, 1);
            dst++;
        }

        nvg__vset(dst, p1->x + dlx1 * lw, p1->y + dly1 * lw, lu, 1);
        dst++;
        nvg__vset(dst, rx1, ry1, ru, 1);
        dst++;
    }

    return dst;
}

static NVGvertex*
nvg__buttCapStart(NVGvertex* dst, NVGpoint* p, float dx, float dy, float w, float d, float aa, float u0, float u1)
{
    float px  = p->x - dx * d;
    float py  = p->y - dy * d;
    float dlx = dy;
    float dly = -dx;
    nvg__vset(dst, px + dlx * w - dx * aa, py + dly * w - dy * aa, u0, 0);
    dst++;
    nvg__vset(dst, px - dlx * w - dx * aa, py - dly * w - dy * aa, u1, 0);
    dst++;
    nvg__vset(dst, px + dlx * w, py + dly * w, u0, 1);
    dst++;
    nvg__vset(dst, px - dlx * w, py - dly * w, u1, 1);
    dst++;
    return dst;
}

static NVGvertex*
nvg__buttCapEnd(NVGvertex* dst, NVGpoint* p, float dx, float dy, float w, float d, float aa, float u0, float u1)
{
    float px  = p->x + dx * d;
    float py  = p->y + dy * d;
    float dlx = dy;
    float dly = -dx;
    nvg__vset(dst, px + dlx * w, py + dly * w, u0, 1);
    dst++;
    nvg__vset(dst, px - dlx * w, py - dly * w, u1, 1);
    dst++;
    nvg__vset(dst, px + dlx * w + dx * aa, py + dly * w + dy * aa, u0, 0);
    dst++;
    nvg__vset(dst, px - dlx * w + dx * aa, py - dly * w + dy * aa, u1, 0);
    dst++;
    return dst;
}

static NVGvertex*
nvg__roundCapStart(NVGvertex* dst, NVGpoint* p, float dx, float dy, float w, int ncap, float aa, float u0, float u1)
{
    int   i;
    float px  = p->x;
    float py  = p->y;
    float dlx = dy;
    float dly = -dx;
    NVG_NOTUSED(aa);
    for (i = 0; i < ncap; i++)
    {
        float a  = i / (float)(ncap - 1) * NVG_PI;
        float ax = cosf(a) * w, ay = sinf(a) * w;
        nvg__vset(dst, px - dlx * ax - dx * ay, py - dly * ax - dy * ay, u0, 1);
        dst++;
        nvg__vset(dst, px, py, 0.5f, 1);
        dst++;
    }
    nvg__vset(dst, px + dlx * w, py + dly * w, u0, 1);
    dst++;
    nvg__vset(dst, px - dlx * w, py - dly * w, u1, 1);
    dst++;
    return dst;
}

static NVGvertex*
nvg__roundCapEnd(NVGvertex* dst, NVGpoint* p, float dx, float dy, float w, int ncap, float aa, float u0, float u1)
{
    int   i;
    float px  = p->x;
    float py  = p->y;
    float dlx = dy;
    float dly = -dx;
    NVG_NOTUSED(aa);
    nvg__vset(dst, px + dlx * w, py + dly * w, u0, 1);
    dst++;
    nvg__vset(dst, px - dlx * w, py - dly * w, u1, 1);
    dst++;
    for (i = 0; i < ncap; i++)
    {
        float a  = i / (float)(ncap - 1) * NVG_PI;
        float ax = cosf(a) * w, ay = sinf(a) * w;
        nvg__vset(dst, px, py, 0.5f, 1);
        dst++;
        nvg__vset(dst, px - dlx * ax + dx * ay, py - dly * ax + dy * ay, u0, 1);
        dst++;
    }
    return dst;
}

static void nvg__calculateJoins(NVGcontext* ctx, float w, int lineJoin, float miterLimit)
{
    NVGpathCache* cache = &ctx->cache;
    int           i, j;
    float         iw = 0.0f;

    if (w > 0.0f)
        iw = 1.0f / w;

    // Calculate which joins needs extra vertices to append, and gather vertex count.
    for (i = 0; i < cache->npaths; i++)
    {
        NVGpath*  path  = &cache->paths[i];
        NVGpoint* pts   = &cache->points[path->first];
        NVGpoint* p0    = &pts[path->count - 1];
        NVGpoint* p1    = &pts[0];
        int       nleft = 0;

        path->nbevel = 0;

        for (j = 0; j < path->count; j++)
        {
            float dlx0, dly0, dlx1, dly1, dmr2, cross, limit;
            dlx0 = p0->dy;
            dly0 = -p0->dx;
            dlx1 = p1->dy;
            dly1 = -p1->dx;
            // Calculate extrusions
            p1->dmx = (dlx0 + dlx1) * 0.5f;
            p1->dmy = (dly0 + dly1) * 0.5f;
            dmr2    = p1->dmx * p1->dmx + p1->dmy * p1->dmy;
            if (dmr2 > 0.000001f)
            {
                float scale = 1.0f / dmr2;
                if (scale > 600.0f)
                {
                    scale = 600.0f;
                }
                p1->dmx *= scale;
                p1->dmy *= scale;
            }

            // Clear flags, but keep the corner.
            p1->flags = (p1->flags & NVG_PT_CORNER) ? NVG_PT_CORNER : 0;

            // Keep track of left turns.
            cross = p1->dx * p0->dy - p0->dx * p1->dy;
            if (cross > 0.0f)
            {
                nleft++;
                p1->flags |= NVG_PT_LEFT;
            }

            // Calculate if we should use bevel or miter for inner join.
            limit = nvg__maxf(1.01f, nvg__minf(p0->len, p1->len) * iw);
            if ((dmr2 * limit * limit) < 1.0f)
                p1->flags |= NVG_PR_INNERBEVEL;

            // Check to see if the corner needs to be beveled.
            if (p1->flags & NVG_PT_CORNER)
            {
                if ((dmr2 * miterLimit * miterLimit) < 1.0f || lineJoin == NVG_BEVEL || lineJoin == NVG_ROUND)
                {
                    p1->flags |= NVG_PT_BEVEL;
                }
            }

            if ((p1->flags & (NVG_PT_BEVEL | NVG_PR_INNERBEVEL)) != 0)
                path->nbevel++;

            p0 = p1++;
        }

        path->convex = (nleft == path->count) ? 1 : 0;
    }
}

static int nvg__expandStroke(NVGcontext* ctx, float w, float fringe, int lineCap, int lineJoin, float miterLimit)
{
    NVGpathCache* cache = &ctx->cache;
    NVGvertex*    verts;
    NVGvertex*    dst;
    int           cverts, i, j;
    float         aa = fringe; // ctx->fringeWidth;
    float         u0 = 0.0f, u1 = 1.0f;
    int           ncap = nvg__curveDivs(w, NVG_PI, ctx->tessTol); // Calculate divisions per half circle.

    w += aa * 0.5f;

    // Disable the gradient used for antialiasing when antialiasing is not used.
    if (aa == 0.0f)
    {
        u0 = 0.5f;
        u1 = 0.5f;
    }

    nvg__calculateJoins(ctx, w, lineJoin, miterLimit);

    // Calculate max vertex usage.
    cverts = 0;
    for (i = 0; i < cache->npaths; i++)
    {
        NVGpath* path = &cache->paths[i];
        int      loop = (path->closed == 0) ? 0 : 1;
        if (lineJoin == NVG_ROUND)
            cverts += (path->count + path->nbevel * (ncap + 2) + 1) * 2; // plus one for loop
        else
            cverts += (path->count + path->nbevel * 5 + 1) * 2; // plus one for loop
        if (loop == 0)
        {
            // space for caps
            if (lineCap == NVG_ROUND)
            {
                cverts += (ncap * 2 + 2) * 2;
            }
            else
            {
                cverts += (3 + 3) * 2;
            }
        }
    }

    verts = nvg__allocTempVerts(ctx, cverts);
    if (verts == NULL)
        return 0;

    for (i = 0; i < cache->npaths; i++)
    {
        NVGpath*  path = &cache->paths[i];
        NVGpoint* pts  = &cache->points[path->first];
        NVGpoint* p0;
        NVGpoint* p1;
        int       s, e, loop;
        float     dx, dy;

        path->fill  = 0;
        path->nfill = 0;

        // Calculate fringe or stroke
        loop         = (path->closed == 0) ? 0 : 1;
        dst          = verts;
        path->stroke = dst;

        if (loop)
        {
            // Looping
            p0 = &pts[path->count - 1];
            p1 = &pts[0];
            s  = 0;
            e  = path->count;
        }
        else
        {
            // Add cap
            p0 = &pts[0];
            p1 = &pts[1];
            s  = 1;
            e  = path->count - 1;
        }

        if (loop == 0)
        {
            // Add cap
            dx = p1->x - p0->x;
            dy = p1->y - p0->y;
            nvg__normalize(&dx, &dy);
            if (lineCap == NVG_BUTT)
                dst = nvg__buttCapStart(dst, p0, dx, dy, w, -aa * 0.5f, aa, u0, u1);
            else if (lineCap == NVG_BUTT || lineCap == NVG_SQUARE)
                dst = nvg__buttCapStart(dst, p0, dx, dy, w, w - aa, aa, u0, u1);
            else if (lineCap == NVG_ROUND)
                dst = nvg__roundCapStart(dst, p0, dx, dy, w, ncap, aa, u0, u1);
        }

        for (j = s; j < e; ++j)
        {
            if ((p1->flags & (NVG_PT_BEVEL | NVG_PR_INNERBEVEL)) != 0)
            {
                if (lineJoin == NVG_ROUND)
                {
                    dst = nvg__roundJoin(dst, p0, p1, w, w, u0, u1, ncap, aa);
                }
                else
                {
                    dst = nvg__bevelJoin(dst, p0, p1, w, w, u0, u1, aa);
                }
            }
            else
            {
                nvg__vset(dst, p1->x + (p1->dmx * w), p1->y + (p1->dmy * w), u0, 1);
                dst++;
                nvg__vset(dst, p1->x - (p1->dmx * w), p1->y - (p1->dmy * w), u1, 1);
                dst++;
            }
            p0 = p1++;
        }

        if (loop)
        {
            // Loop it
            nvg__vset(dst, verts[0].x, verts[0].y, u0, 1);
            dst++;
            nvg__vset(dst, verts[1].x, verts[1].y, u1, 1);
            dst++;
        }
        else
        {
            // Add cap
            dx = p1->x - p0->x;
            dy = p1->y - p0->y;
            nvg__normalize(&dx, &dy);
            if (lineCap == NVG_BUTT)
                dst = nvg__buttCapEnd(dst, p1, dx, dy, w, -aa * 0.5f, aa, u0, u1);
            else if (lineCap == NVG_BUTT || lineCap == NVG_SQUARE)
                dst = nvg__buttCapEnd(dst, p1, dx, dy, w, w - aa, aa, u0, u1);
            else if (lineCap == NVG_ROUND)
                dst = nvg__roundCapEnd(dst, p1, dx, dy, w, ncap, aa, u0, u1);
        }

        path->nstroke = (int)(dst - verts);

        verts = dst;
    }

    return 1;
}

static int nvg__expandFill(NVGcontext* ctx, float w, int lineJoin, float miterLimit)
{
    NVGpathCache* cache = &ctx->cache;
    NVGvertex*    verts;
    NVGvertex*    dst;
    int           cverts, convex, i, j;
    float         aa     = ctx->fringeWidth;
    int           fringe = w > 0.0f;

    nvg__calculateJoins(ctx, w, lineJoin, miterLimit);

    // Calculate max vertex usage.
    cverts = 0;
    for (i = 0; i < cache->npaths; i++)
    {
        NVGpath* path  = &cache->paths[i];
        cverts        += path->count + path->nbevel + 1;
        if (fringe)
            cverts += (path->count + path->nbevel * 5 + 1) * 2; // plus one for loop
    }

    verts = nvg__allocTempVerts(ctx, cverts);
    if (verts == NULL)
        return 0;

    convex = cache->npaths == 1 && cache->paths[0].convex;

    for (i = 0; i < cache->npaths; i++)
    {
        NVGpath*  path = &cache->paths[i];
        NVGpoint* pts  = &cache->points[path->first];
        NVGpoint* p0;
        NVGpoint* p1;
        float     rw, lw, woff;
        float     ru, lu;

        // Calculate shape vertices.
        woff       = 0.5f * aa;
        dst        = verts;
        path->fill = dst;

        if (fringe)
        {
            // Looping
            p0 = &pts[path->count - 1];
            p1 = &pts[0];
            for (j = 0; j < path->count; ++j)
            {
                if (p1->flags & NVG_PT_BEVEL)
                {
                    float dlx0 = p0->dy;
                    float dly0 = -p0->dx;
                    float dlx1 = p1->dy;
                    float dly1 = -p1->dx;
                    if (p1->flags & NVG_PT_LEFT)
                    {
                        float lx = p1->x + p1->dmx * woff;
                        float ly = p1->y + p1->dmy * woff;
                        nvg__vset(dst, lx, ly, 0.5f, 1);
                        dst++;
                    }
                    else
                    {
                        float lx0 = p1->x + dlx0 * woff;
                        float ly0 = p1->y + dly0 * woff;
                        float lx1 = p1->x + dlx1 * woff;
                        float ly1 = p1->y + dly1 * woff;
                        nvg__vset(dst, lx0, ly0, 0.5f, 1);
                        dst++;
                        nvg__vset(dst, lx1, ly1, 0.5f, 1);
                        dst++;
                    }
                }
                else
                {
                    nvg__vset(dst, p1->x + (p1->dmx * woff), p1->y + (p1->dmy * woff), 0.5f, 1);
                    dst++;
                }
                p0 = p1++;
            }
        }
        else
        {
            for (j = 0; j < path->count; ++j)
            {
                nvg__vset(dst, pts[j].x, pts[j].y, 0.5f, 1);
                dst++;
            }
        }

        path->nfill = (int)(dst - verts);
        verts       = dst;

        // Calculate fringe
        if (fringe)
        {
            lw           = w + woff;
            rw           = w - woff;
            lu           = 0;
            ru           = 1;
            dst          = verts;
            path->stroke = dst;

            // Create only half a fringe for convex shapes so that
            // the shape can be rendered without stenciling.
            if (convex)
            {
                lw = woff; // This should generate the same vertex as fill inset above.
                lu = 0.5f; // Set outline fade at middle.
            }

            // Looping
            p0 = &pts[path->count - 1];
            p1 = &pts[0];

            for (j = 0; j < path->count; ++j)
            {
                if ((p1->flags & (NVG_PT_BEVEL | NVG_PR_INNERBEVEL)) != 0)
                {
                    dst = nvg__bevelJoin(dst, p0, p1, lw, rw, lu, ru, ctx->fringeWidth);
                }
                else
                {
                    nvg__vset(dst, p1->x + (p1->dmx * lw), p1->y + (p1->dmy * lw), lu, 1);
                    dst++;
                    nvg__vset(dst, p1->x - (p1->dmx * rw), p1->y - (p1->dmy * rw), ru, 1);
                    dst++;
                }
                p0 = p1++;
            }

            // Loop it
            nvg__vset(dst, verts[0].x, verts[0].y, lu, 1);
            dst++;
            nvg__vset(dst, verts[1].x, verts[1].y, ru, 1);
            dst++;

            path->nstroke = (int)(dst - verts);
            verts         = dst;
        }
        else
        {
            path->stroke  = NULL;
            path->nstroke = 0;
        }
    }

    return 1;
}

// Draw
void nvgBeginPath(NVGcontext* ctx)
{
    ctx->ncommands     = 0;
    ctx->cache.npoints = 0;
    ctx->cache.npaths  = 0;
}

void nvgMoveTo(NVGcontext* ctx, float x, float y)
{
    float vals[] = {NVG_MOVETO, x, y};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgLineTo(NVGcontext* ctx, float x, float y)
{
    float vals[] = {NVG_LINETO, x, y};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgBezierTo(NVGcontext* ctx, float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    float vals[] = {NVG_BEZIERTO, c1x, c1y, c2x, c2y, x, y};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgQuadTo(NVGcontext* ctx, float cx, float cy, float x, float y)
{
    float x0     = ctx->commandx;
    float y0     = ctx->commandy;
    float vals[] = {
        NVG_BEZIERTO,
        x0 + 2.0f / 3.0f * (cx - x0),
        y0 + 2.0f / 3.0f * (cy - y0),
        x + 2.0f / 3.0f * (cx - x),
        y + 2.0f / 3.0f * (cy - y),
        x,
        y};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgArcTo(NVGcontext* ctx, float x1, float y1, float x2, float y2, float radius)
{
    float x0 = ctx->commandx;
    float y0 = ctx->commandy;
    float dx0, dy0, dx1, dy1, a, d, cx, cy, a0, a1;
    int   dir;

    if (ctx->ncommands == 0)
    {
        return;
    }

    // Handle degenerate cases.
    if (nvg__ptEquals(x0, y0, x1, y1, ctx->distTol) || nvg__ptEquals(x1, y1, x2, y2, ctx->distTol) ||
        nvg__distPtSeg(x1, y1, x0, y0, x2, y2) < ctx->distTol * ctx->distTol || radius < ctx->distTol)
    {
        nvgLineTo(ctx, x1, y1);
        return;
    }

    // Calculate tangential circle to lines (x0,y0)-(x1,y1) and (x1,y1)-(x2,y2).
    dx0 = x0 - x1;
    dy0 = y0 - y1;
    dx1 = x2 - x1;
    dy1 = y2 - y1;
    nvg__normalize(&dx0, &dy0);
    nvg__normalize(&dx1, &dy1);
    a = nvg__acosf(dx0 * dx1 + dy0 * dy1);
    d = radius / nvg__tanf(a / 2.0f);

    //	printf("a=%f° d=%f\n", a/NVG_PI*180.0f, d);

    if (d > 10000.0f)
    {
        nvgLineTo(ctx, x1, y1);
        return;
    }

    if (nvg__cross(dx0, dy0, dx1, dy1) > 0.0f)
    {
        cx  = x1 + dx0 * d + dy0 * radius;
        cy  = y1 + dy0 * d + -dx0 * radius;
        a0  = nvg__atan2f(dx0, -dy0);
        a1  = nvg__atan2f(-dx1, dy1);
        dir = NVG_CW;
        //		printf("CW c=(%f, %f) a0=%f° a1=%f°\n", cx, cy, a0/NVG_PI*180.0f, a1/NVG_PI*180.0f);
    }
    else
    {
        cx  = x1 + dx0 * d + -dy0 * radius;
        cy  = y1 + dy0 * d + dx0 * radius;
        a0  = nvg__atan2f(-dx0, dy0);
        a1  = nvg__atan2f(dx1, -dy1);
        dir = NVG_CCW;
        //		printf("CCW c=(%f, %f) a0=%f° a1=%f°\n", cx, cy, a0/NVG_PI*180.0f, a1/NVG_PI*180.0f);
    }

    nvgArc(ctx, cx, cy, radius, a0, a1, dir);
}

void nvgClosePath(NVGcontext* ctx)
{
    float vals[] = {NVG_CLOSE};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgSetPathWinding(NVGcontext* ctx, int dir)
{
    float vals[] = {NVG_WINDING, (float)dir};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgArc(NVGcontext* ctx, float cx, float cy, float r, float a0, float a1, int dir)
{
    float a = 0, da = 0, hda = 0, kappa = 0;
    float dx = 0, dy = 0, x = 0, y = 0, tanx = 0, tany = 0;
    float px = 0, py = 0, ptanx = 0, ptany = 0;
    float vals[3 + 5 * 7 + 100];
    int   i, ndivs, nvals;
    int   move = ctx->ncommands > 0 ? NVG_LINETO : NVG_MOVETO;

    // Clamp angles
    da = a1 - a0;
    if (dir == NVG_CW)
    {
        if (nvg__absf(da) >= NVG_PI * 2)
        {
            da = NVG_PI * 2;
        }
        else
        {
            while (da < 0.0f)
                da += NVG_PI * 2;
        }
    }
    else
    {
        if (nvg__absf(da) >= NVG_PI * 2)
        {
            da = -NVG_PI * 2;
        }
        else
        {
            while (da > 0.0f)
                da -= NVG_PI * 2;
        }
    }

    // Split arc into max 90 degree segments.
    ndivs = nvg__maxi(1, nvg__mini((int)(nvg__absf(da) / (NVG_PI * 0.5f) + 0.5f), 5));
    hda   = (da / (float)ndivs) / 2.0f;
    kappa = nvg__absf(4.0f / 3.0f * (1.0f - nvg__cosf(hda)) / nvg__sinf(hda));

    if (dir == NVG_CCW)
        kappa = -kappa;

    nvals = 0;
    for (i = 0; i <= ndivs; i++)
    {
        a    = a0 + da * (i / (float)ndivs);
        dx   = nvg__cosf(a);
        dy   = nvg__sinf(a);
        x    = cx + dx * r;
        y    = cy + dy * r;
        tanx = -dy * r * kappa;
        tany = dx * r * kappa;

        if (i == 0)
        {
            vals[nvals++] = (float)move;
            vals[nvals++] = x;
            vals[nvals++] = y;
        }
        else
        {
            vals[nvals++] = NVG_BEZIERTO;
            vals[nvals++] = px + ptanx;
            vals[nvals++] = py + ptany;
            vals[nvals++] = x - tanx;
            vals[nvals++] = y - tany;
            vals[nvals++] = x;
            vals[nvals++] = y;
        }
        px    = x;
        py    = y;
        ptanx = tanx;
        ptany = tany;
    }

    nvg__appendCommands(ctx, vals, nvals);
}

void nvgRect(NVGcontext* ctx, float x, float y, float w, float h)
{
    float vals[] = {NVG_MOVETO, x, y, NVG_LINETO, x, y + h, NVG_LINETO, x + w, y + h, NVG_LINETO, x + w, y, NVG_CLOSE};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgRoundedRect(NVGcontext* ctx, float x, float y, float w, float h, float r)
{
    nvgRoundedRectVarying(ctx, x, y, w, h, r, r, r, r);
}

void nvgRoundedRectVarying(
    NVGcontext* ctx,
    float       x,
    float       y,
    float       w,
    float       h,
    float       radTopLeft,
    float       radTopRight,
    float       radBottomRight,
    float       radBottomLeft)
{
    if (radTopLeft < 0.1f && radTopRight < 0.1f && radBottomRight < 0.1f && radBottomLeft < 0.1f)
    {
        nvgRect(ctx, x, y, w, h);
        return;
    }
    else
    {
        float halfw = nvg__absf(w) * 0.5f;
        float halfh = nvg__absf(h) * 0.5f;
        float rxBL  = nvg__minf(radBottomLeft, halfw) * nvg__signf(w),
              ryBL  = nvg__minf(radBottomLeft, halfh) * nvg__signf(h);
        float rxBR  = nvg__minf(radBottomRight, halfw) * nvg__signf(w),
              ryBR  = nvg__minf(radBottomRight, halfh) * nvg__signf(h);
        float rxTR  = nvg__minf(radTopRight, halfw) * nvg__signf(w),
              ryTR  = nvg__minf(radTopRight, halfh) * nvg__signf(h);
        float rxTL = nvg__minf(radTopLeft, halfw) * nvg__signf(w), ryTL = nvg__minf(radTopLeft, halfh) * nvg__signf(h);
        float vals[] = {
            NVG_MOVETO,
            x,
            y + ryTL,
            NVG_LINETO,
            x,
            y + h - ryBL,
            NVG_BEZIERTO,
            x,
            y + h - ryBL * (1 - NVG_KAPPA90),
            x + rxBL * (1 - NVG_KAPPA90),
            y + h,
            x + rxBL,
            y + h,
            NVG_LINETO,
            x + w - rxBR,
            y + h,
            NVG_BEZIERTO,
            x + w - rxBR * (1 - NVG_KAPPA90),
            y + h,
            x + w,
            y + h - ryBR * (1 - NVG_KAPPA90),
            x + w,
            y + h - ryBR,
            NVG_LINETO,
            x + w,
            y + ryTR,
            NVG_BEZIERTO,
            x + w,
            y + ryTR * (1 - NVG_KAPPA90),
            x + w - rxTR * (1 - NVG_KAPPA90),
            y,
            x + w - rxTR,
            y,
            NVG_LINETO,
            x + rxTL,
            y,
            NVG_BEZIERTO,
            x + rxTL * (1 - NVG_KAPPA90),
            y,
            x,
            y + ryTL * (1 - NVG_KAPPA90),
            x,
            y + ryTL,
            NVG_CLOSE};
        nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
    }
}

void nvgEllipse(NVGcontext* ctx, float cx, float cy, float rx, float ry)
{
    float vals[] = {
        NVG_MOVETO,
        cx - rx,
        cy,
        NVG_BEZIERTO,
        cx - rx,
        cy + ry * NVG_KAPPA90,
        cx - rx * NVG_KAPPA90,
        cy + ry,
        cx,
        cy + ry,
        NVG_BEZIERTO,
        cx + rx * NVG_KAPPA90,
        cy + ry,
        cx + rx,
        cy + ry * NVG_KAPPA90,
        cx + rx,
        cy,
        NVG_BEZIERTO,
        cx + rx,
        cy - ry * NVG_KAPPA90,
        cx + rx * NVG_KAPPA90,
        cy - ry,
        cx,
        cy - ry,
        NVG_BEZIERTO,
        cx - rx * NVG_KAPPA90,
        cy - ry,
        cx - rx,
        cy - ry * NVG_KAPPA90,
        cx - rx,
        cy,
        NVG_CLOSE};
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgCircle(NVGcontext* ctx, float cx, float cy, float r) { nvgEllipse(ctx, cx, cy, r, r); }

void nvgDebugDumpPathCache(NVGcontext* ctx)
{
    const NVGpath* path;
    int            i, j;

    printf("Dumping %d cached paths\n", ctx->cache.npaths);
    for (i = 0; i < ctx->cache.npaths; i++)
    {
        path = &ctx->cache.paths[i];
        printf(" - Path %d\n", i);
        if (path->nfill)
        {
            printf("   - fill: %d\n", path->nfill);
            for (j = 0; j < path->nfill; j++)
                printf("%f\t%f\n", path->fill[j].x, path->fill[j].y);
        }
        if (path->nstroke)
        {
            printf("   - stroke: %d\n", path->nstroke);
            for (j = 0; j < path->nstroke; j++)
                printf("%f\t%f\n", path->stroke[j].x, path->stroke[j].y);
        }
    }
}

// Add fonts
int nvgCreateFont(NVGcontext* ctx, const char* name, const char* filename)
{
    return fonsAddFont(ctx->fs, name, filename, 0);
}

int nvgCreateFontAtIndex(NVGcontext* ctx, const char* name, const char* filename, const int fontIndex)
{
    return fonsAddFont(ctx->fs, name, filename, fontIndex);
}

int nvgCreateFontMem(NVGcontext* ctx, const char* name, unsigned char* data, int ndata, int freeData)
{
    return fonsAddFontMem(ctx->fs, name, data, ndata, freeData, 0);
}

int nvgCreateFontMemAtIndex(
    NVGcontext*    ctx,
    const char*    name,
    unsigned char* data,
    int            ndata,
    int            freeData,
    const int      fontIndex)
{
    return fonsAddFontMem(ctx->fs, name, data, ndata, freeData, fontIndex);
}

int nvgFindFont(NVGcontext* ctx, const char* name)
{
    if (name == NULL)
        return -1;
    return fonsGetFontByName(ctx->fs, name);
}

int nvgAddFallbackFontId(NVGcontext* ctx, int baseFont, int fallbackFont)
{
    if (baseFont == -1 || fallbackFont == -1)
        return 0;
    return fonsAddFallbackFont(ctx->fs, baseFont, fallbackFont);
}

int nvgAddFallbackFont(NVGcontext* ctx, const char* baseFont, const char* fallbackFont)
{
    return nvgAddFallbackFontId(ctx, nvgFindFont(ctx, baseFont), nvgFindFont(ctx, fallbackFont));
}

void nvgResetFallbackFontsId(NVGcontext* ctx, int baseFont) { fonsResetFallbackFont(ctx->fs, baseFont); }

void nvgResetFallbackFonts(NVGcontext* ctx, const char* baseFont)
{
    nvgResetFallbackFontsId(ctx, nvgFindFont(ctx, baseFont));
}

// State setting
void nvgSetFontSize(NVGcontext* ctx, float size)
{
    NVGstate* state = &ctx->state;
    state->fontSize = size;
}

void nvgSetFontBlur(NVGcontext* ctx, float blur)
{
    NVGstate* state = &ctx->state;
    state->fontBlur = blur;
}

void nvgSetLetterSpacing(NVGcontext* ctx, float spacing)
{
    NVGstate* state      = &ctx->state;
    state->letterSpacing = spacing;
}

void nvgSetTextLineHeight(NVGcontext* ctx, float lineHeight)
{
    NVGstate* state   = &ctx->state;
    state->lineHeight = lineHeight;
}

void nvgSetTextAlign(NVGcontext* ctx, int align)
{
    NVGstate* state  = &ctx->state;
    state->textAlign = align;
}

void nvgSetFontFaceById(NVGcontext* ctx, int font)
{
    NVGstate* state = &ctx->state;
    state->fontId   = font;
}

void nvgSetFontFaceByName(NVGcontext* ctx, const char* font)
{
    NVGstate* state = &ctx->state;
    state->fontId   = fonsGetFontByName(ctx->fs, font);
}

static float nvg__quantize(float a, float d) { return ((int)(a / d + 0.5f)) * d; }

static float nvg__getFontScale(NVGstate* state)
{
    return nvg__minf(nvg__quantize(nvg__getAverageScale(state->xform), 0.01f), 4.0f);
}

static void nvg__flushTextTexture(NVGcontext* ctx)
{
    int dirty[4];

    if (fonsValidateTexture(ctx->fs, dirty))
    {
        int fontImage = ctx->fontImages[ctx->fontImageIdx];
        // Update texture
        if (fontImage != 0)
        {
            int                  iw, ih;
            const unsigned char* data = fonsGetTextureData(ctx->fs, &iw, &ih);
            int                  x    = dirty[0];
            int                  y    = dirty[1];
            int                  w    = dirty[2] - dirty[0];
            int                  h    = dirty[3] - dirty[1];
            nvgUpdateTexture(ctx, fontImage, x, y, w, h, data);
        }
    }
}

static int nvg__allocTextAtlas(NVGcontext* ctx)
{
    int iw, ih;
    nvg__flushTextTexture(ctx);
    if (ctx->fontImageIdx >= NVG_MAX_FONTIMAGES - 1)
        return 0;
    // if next fontImage already have a texture
    if (ctx->fontImages[ctx->fontImageIdx + 1] != 0)
        nvgGetImageSize(ctx, ctx->fontImages[ctx->fontImageIdx + 1], &iw, &ih);
    else
    { // calculate the new font image size and create it.
        nvgGetImageSize(ctx, ctx->fontImages[ctx->fontImageIdx], &iw, &ih);
        if (iw > ih)
            ih *= 2;
        else
            iw *= 2;
        if (iw > NVG_MAX_FONTIMAGE_SIZE || ih > NVG_MAX_FONTIMAGE_SIZE)
            iw = ih = NVG_MAX_FONTIMAGE_SIZE;
        ctx->fontImages[ctx->fontImageIdx + 1] = nvgCreateTexture(ctx, NVG_TEXTURE_ALPHA, iw, ih, 0, NULL);
    }
    ++ctx->fontImageIdx;
    fonsResetAtlas(ctx->fs, iw, ih);
    return 1;
}

static int nvg__isTransformFlipped(const float* xform)
{
    float det = xform[0] * xform[3] - xform[2] * xform[1];
    return (det < 0);
}

float nvgText(NVGcontext* ctx, float x, float y, const char* string, const char* end)
{
    NVGstate*    state = &ctx->state;
    FONStextIter iter, prevIter;
    FONSquad     q;
    NVGvertex*   verts;
    float        scale     = nvg__getFontScale(state) * ctx->devicePxRatio;
    float        invscale  = 1.0f / scale;
    int          cverts    = 0;
    int          nverts    = 0;
    int          isFlipped = nvg__isTransformFlipped(state->xform);

    if (end == NULL)
        end = string + strlen(string);

    if (state->fontId == FONS_INVALID)
        return x;

    fonsSetSize(ctx->fs, state->fontSize * scale);
    fonsSetSpacing(ctx->fs, state->letterSpacing * scale);
    fonsSetBlur(ctx->fs, state->fontBlur * scale);
    fonsSetAlign(ctx->fs, state->textAlign);
    fonsSetFont(ctx->fs, state->fontId);

    cverts = nvg__maxi(2, (int)(end - string)) * 6; // conservative estimate.
    verts  = nvg__allocTempVerts(ctx, cverts);
    if (verts == NULL)
        return x;

    fonsTextIterInit(ctx->fs, &iter, x * scale, y * scale, string, end, FONS_GLYPH_BITMAP_REQUIRED);
    prevIter = iter;
    while (fonsTextIterNext(ctx->fs, &iter, &q))
    {
        float c[4 * 2];
        if (iter.prevGlyphIndex == -1)
        { // can not retrieve glyph?
            if (nverts != 0)
            {
                nvg__renderText(ctx, verts, nverts);
                nverts = 0;
            }
            if (!nvg__allocTextAtlas(ctx))
                break; // no memory :(
            iter = prevIter;
            fonsTextIterNext(ctx->fs, &iter, &q); // try again
            if (iter.prevGlyphIndex == -1)        // still can not find glyph?
                break;
        }
        prevIter = iter;
        if (isFlipped)
        {
            float tmp;

            tmp  = q.y0;
            q.y0 = q.y1;
            q.y1 = tmp;
            tmp  = q.t0;
            q.t0 = q.t1;
            q.t1 = tmp;
        }
        // Transform corners.
        nvgTransformPoint(&c[0], &c[1], state->xform, q.x0 * invscale, q.y0 * invscale);
        nvgTransformPoint(&c[2], &c[3], state->xform, q.x1 * invscale, q.y0 * invscale);
        nvgTransformPoint(&c[4], &c[5], state->xform, q.x1 * invscale, q.y1 * invscale);
        nvgTransformPoint(&c[6], &c[7], state->xform, q.x0 * invscale, q.y1 * invscale);
        // Create triangles
        if (nverts + 6 <= cverts)
        {
            nvg__vset(&verts[nverts], c[0], c[1], q.s0, q.t0);
            nverts++;
            nvg__vset(&verts[nverts], c[4], c[5], q.s1, q.t1);
            nverts++;
            nvg__vset(&verts[nverts], c[2], c[3], q.s1, q.t0);
            nverts++;
            nvg__vset(&verts[nverts], c[0], c[1], q.s0, q.t0);
            nverts++;
            nvg__vset(&verts[nverts], c[6], c[7], q.s0, q.t1);
            nverts++;
            nvg__vset(&verts[nverts], c[4], c[5], q.s1, q.t1);
            nverts++;
        }
    }

    // TODO: add back-end bit to do this just once per frame.
    nvg__flushTextTexture(ctx);

    nvg__renderText(ctx, verts, nverts);

    return iter.nextx / scale;
}

void nvgTextBox(NVGcontext* ctx, float x, float y, float breakRowWidth, const char* string, const char* end)
{
    NVGstate*  state = &ctx->state;
    NVGtextRow rows[2];
    int        nrows    = 0, i;
    int        oldAlign = state->textAlign;
    int        halign   = state->textAlign & (NVG_ALIGN_LEFT | NVG_ALIGN_CENTER | NVG_ALIGN_RIGHT);
    int        valign   = state->textAlign & (NVG_ALIGN_TOP | NVG_ALIGN_MIDDLE | NVG_ALIGN_BOTTOM | NVG_ALIGN_BASELINE);
    float      lineh    = 0;

    if (state->fontId == FONS_INVALID)
        return;

    nvgTextMetrics(ctx, NULL, NULL, &lineh);

    state->textAlign = NVG_ALIGN_LEFT | valign;

    while ((nrows = nvgTextBreakLines(ctx, string, end, breakRowWidth, rows, 2)))
    {
        for (i = 0; i < nrows; i++)
        {
            NVGtextRow* row = &rows[i];
            if (halign & NVG_ALIGN_LEFT)
                nvgText(ctx, x, y, row->start, row->end);
            else if (halign & NVG_ALIGN_CENTER)
                nvgText(ctx, x + breakRowWidth * 0.5f - row->width * 0.5f, y, row->start, row->end);
            else if (halign & NVG_ALIGN_RIGHT)
                nvgText(ctx, x + breakRowWidth - row->width, y, row->start, row->end);
            y += lineh * state->lineHeight;
        }
        string = rows[nrows - 1].next;
    }

    state->textAlign = oldAlign;
}

int nvgTextGlyphPositions(
    NVGcontext*       ctx,
    float             x,
    float             y,
    const char*       string,
    const char*       end,
    NVGglyphPosition* positions,
    int               maxPositions)
{
    NVGstate*    state    = &ctx->state;
    float        scale    = nvg__getFontScale(state) * ctx->devicePxRatio;
    float        invscale = 1.0f / scale;
    FONStextIter iter, prevIter;
    FONSquad     q;
    int          npos = 0;

    if (state->fontId == FONS_INVALID)
        return 0;

    if (end == NULL)
        end = string + strlen(string);

    if (string == end)
        return 0;

    fonsSetSize(ctx->fs, state->fontSize * scale);
    fonsSetSpacing(ctx->fs, state->letterSpacing * scale);
    fonsSetBlur(ctx->fs, state->fontBlur * scale);
    fonsSetAlign(ctx->fs, state->textAlign);
    fonsSetFont(ctx->fs, state->fontId);

    fonsTextIterInit(ctx->fs, &iter, x * scale, y * scale, string, end, FONS_GLYPH_BITMAP_OPTIONAL);
    prevIter = iter;
    while (fonsTextIterNext(ctx->fs, &iter, &q))
    {
        if (iter.prevGlyphIndex < 0 && nvg__allocTextAtlas(ctx))
        { // can not retrieve glyph?
            iter = prevIter;
            fonsTextIterNext(ctx->fs, &iter, &q); // try again
        }
        prevIter             = iter;
        positions[npos].str  = iter.str;
        positions[npos].x    = iter.x * invscale;
        positions[npos].minx = nvg__minf(iter.x, q.x0) * invscale;
        positions[npos].maxx = nvg__maxf(iter.nextx, q.x1) * invscale;
        npos++;
        if (npos >= maxPositions)
            break;
    }

    return npos;
}

enum NVGcodepointType
{
    NVG_SPACE,
    NVG_NEWLINE,
    NVG_CHAR,
    NVG_CJK_CHAR,
};

int nvgTextBreakLines(
    NVGcontext* ctx,
    const char* string,
    const char* end,
    float       breakRowWidth,
    NVGtextRow* rows,
    int         maxRows)
{
    NVGstate*    state    = &ctx->state;
    float        scale    = nvg__getFontScale(state) * ctx->devicePxRatio;
    float        invscale = 1.0f / scale;
    FONStextIter iter, prevIter;
    FONSquad     q;
    int          nrows      = 0;
    float        rowStartX  = 0;
    float        rowWidth   = 0;
    float        rowMinX    = 0;
    float        rowMaxX    = 0;
    const char*  rowStart   = NULL;
    const char*  rowEnd     = NULL;
    const char*  wordStart  = NULL;
    float        wordStartX = 0;
    float        wordMinX   = 0;
    const char*  breakEnd   = NULL;
    float        breakWidth = 0;
    float        breakMaxX  = 0;
    int          type = NVG_SPACE, ptype = NVG_SPACE;
    unsigned int pcodepoint = 0;

    if (maxRows == 0)
        return 0;
    if (state->fontId == FONS_INVALID)
        return 0;

    if (end == NULL)
        end = string + strlen(string);

    if (string == end)
        return 0;

    fonsSetSize(ctx->fs, state->fontSize * scale);
    fonsSetSpacing(ctx->fs, state->letterSpacing * scale);
    fonsSetBlur(ctx->fs, state->fontBlur * scale);
    fonsSetAlign(ctx->fs, state->textAlign);
    fonsSetFont(ctx->fs, state->fontId);

    breakRowWidth *= scale;

    fonsTextIterInit(ctx->fs, &iter, 0, 0, string, end, FONS_GLYPH_BITMAP_OPTIONAL);
    prevIter = iter;
    while (fonsTextIterNext(ctx->fs, &iter, &q))
    {
        if (iter.prevGlyphIndex < 0 && nvg__allocTextAtlas(ctx))
        { // can not retrieve glyph?
            iter = prevIter;
            fonsTextIterNext(ctx->fs, &iter, &q); // try again
        }
        prevIter = iter;
        switch (iter.codepoint)
        {
        case 9:      // \t
        case 11:     // \v
        case 12:     // \f
        case 32:     // space
        case 0x00a0: // NBSP
            type = NVG_SPACE;
            break;
        case 10: // \n
            type = pcodepoint == 13 ? NVG_SPACE : NVG_NEWLINE;
            break;
        case 13: // \r
            type = pcodepoint == 10 ? NVG_SPACE : NVG_NEWLINE;
            break;
        case 0x0085: // NEL
            type = NVG_NEWLINE;
            break;
        default:
            if ((iter.codepoint >= 0x4E00 && iter.codepoint <= 0x9FFF) ||
                (iter.codepoint >= 0x3000 && iter.codepoint <= 0x30FF) ||
                (iter.codepoint >= 0xFF00 && iter.codepoint <= 0xFFEF) ||
                (iter.codepoint >= 0x1100 && iter.codepoint <= 0x11FF) ||
                (iter.codepoint >= 0x3130 && iter.codepoint <= 0x318F) ||
                (iter.codepoint >= 0xAC00 && iter.codepoint <= 0xD7AF))
                type = NVG_CJK_CHAR;
            else
                type = NVG_CHAR;
            break;
        }

        if (type == NVG_NEWLINE)
        {
            // Always handle new lines.
            rows[nrows].start = rowStart != NULL ? rowStart : iter.str;
            rows[nrows].end   = rowEnd != NULL ? rowEnd : iter.str;
            rows[nrows].width = rowWidth * invscale;
            rows[nrows].minx  = rowMinX * invscale;
            rows[nrows].maxx  = rowMaxX * invscale;
            rows[nrows].next  = iter.next;
            nrows++;
            if (nrows >= maxRows)
                return nrows;
            // Set null break point
            breakEnd   = rowStart;
            breakWidth = 0.0;
            breakMaxX  = 0.0;
            // Indicate to skip the white space at the beginning of the row.
            rowStart = NULL;
            rowEnd   = NULL;
            rowWidth = 0;
            rowMinX = rowMaxX = 0;
        }
        else
        {
            if (rowStart == NULL)
            {
                // Skip white space until the beginning of the line
                if (type == NVG_CHAR || type == NVG_CJK_CHAR)
                {
                    // The current char is the row so far
                    rowStartX  = iter.x;
                    rowStart   = iter.str;
                    rowEnd     = iter.next;
                    rowWidth   = iter.nextx - rowStartX;
                    rowMinX    = q.x0 - rowStartX;
                    rowMaxX    = q.x1 - rowStartX;
                    wordStart  = iter.str;
                    wordStartX = iter.x;
                    wordMinX   = q.x0 - rowStartX;
                    // Set null break point
                    breakEnd   = rowStart;
                    breakWidth = 0.0;
                    breakMaxX  = 0.0;
                }
            }
            else
            {
                float nextWidth = iter.nextx - rowStartX;

                // track last non-white space character
                if (type == NVG_CHAR || type == NVG_CJK_CHAR)
                {
                    rowEnd   = iter.next;
                    rowWidth = iter.nextx - rowStartX;
                    rowMaxX  = q.x1 - rowStartX;
                }
                // track last end of a word
                if (((ptype == NVG_CHAR || ptype == NVG_CJK_CHAR) && type == NVG_SPACE) || type == NVG_CJK_CHAR)
                {
                    breakEnd   = iter.str;
                    breakWidth = rowWidth;
                    breakMaxX  = rowMaxX;
                }
                // track last beginning of a word
                if ((ptype == NVG_SPACE && (type == NVG_CHAR || type == NVG_CJK_CHAR)) || type == NVG_CJK_CHAR)
                {
                    wordStart  = iter.str;
                    wordStartX = iter.x;
                    wordMinX   = q.x0;
                }

                // Break to new line when a character is beyond break width.
                if ((type == NVG_CHAR || type == NVG_CJK_CHAR) && nextWidth > breakRowWidth)
                {
                    // The run length is too long, need to break to new line.
                    if (breakEnd == rowStart)
                    {
                        // The current word is longer than the row length, just break it from here.
                        rows[nrows].start = rowStart;
                        rows[nrows].end   = iter.str;
                        rows[nrows].width = rowWidth * invscale;
                        rows[nrows].minx  = rowMinX * invscale;
                        rows[nrows].maxx  = rowMaxX * invscale;
                        rows[nrows].next  = iter.str;
                        nrows++;
                        if (nrows >= maxRows)
                            return nrows;
                        rowStartX  = iter.x;
                        rowStart   = iter.str;
                        rowEnd     = iter.next;
                        rowWidth   = iter.nextx - rowStartX;
                        rowMinX    = q.x0 - rowStartX;
                        rowMaxX    = q.x1 - rowStartX;
                        wordStart  = iter.str;
                        wordStartX = iter.x;
                        wordMinX   = q.x0 - rowStartX;
                    }
                    else
                    {
                        // Break the line from the end of the last word, and start new line from the beginning of the
                        // new.
                        rows[nrows].start = rowStart;
                        rows[nrows].end   = breakEnd;
                        rows[nrows].width = breakWidth * invscale;
                        rows[nrows].minx  = rowMinX * invscale;
                        rows[nrows].maxx  = breakMaxX * invscale;
                        rows[nrows].next  = wordStart;
                        nrows++;
                        if (nrows >= maxRows)
                            return nrows;
                        // Update row
                        rowStartX = wordStartX;
                        rowStart  = wordStart;
                        rowEnd    = iter.next;
                        rowWidth  = iter.nextx - rowStartX;
                        rowMinX   = wordMinX - rowStartX;
                        rowMaxX   = q.x1 - rowStartX;
                    }
                    // Set null break point
                    breakEnd   = rowStart;
                    breakWidth = 0.0;
                    breakMaxX  = 0.0;
                }
            }
        }

        pcodepoint = iter.codepoint;
        ptype      = type;
    }

    // Break the line from the end of the last word, and start new line from the beginning of the new.
    if (rowStart != NULL)
    {
        rows[nrows].start = rowStart;
        rows[nrows].end   = rowEnd;
        rows[nrows].width = rowWidth * invscale;
        rows[nrows].minx  = rowMinX * invscale;
        rows[nrows].maxx  = rowMaxX * invscale;
        rows[nrows].next  = end;
        nrows++;
    }

    return nrows;
}

float nvgTextBounds(NVGcontext* ctx, float x, float y, const char* string, const char* end, float* bounds)
{
    NVGstate* state    = &ctx->state;
    float     scale    = nvg__getFontScale(state) * ctx->devicePxRatio;
    float     invscale = 1.0f / scale;
    float     width;

    if (state->fontId == FONS_INVALID)
        return 0;

    fonsSetSize(ctx->fs, state->fontSize * scale);
    fonsSetSpacing(ctx->fs, state->letterSpacing * scale);
    fonsSetBlur(ctx->fs, state->fontBlur * scale);
    fonsSetAlign(ctx->fs, state->textAlign);
    fonsSetFont(ctx->fs, state->fontId);

    width = fonsTextBounds(ctx->fs, x * scale, y * scale, string, end, bounds);
    if (bounds != NULL)
    {
        // Use line bounds for height.
        fonsLineBounds(ctx->fs, y * scale, &bounds[1], &bounds[3]);
        bounds[0] *= invscale;
        bounds[1] *= invscale;
        bounds[2] *= invscale;
        bounds[3] *= invscale;
    }
    return width * invscale;
}

void nvgTextBoxBounds(
    NVGcontext* ctx,
    float       x,
    float       y,
    float       breakRowWidth,
    const char* string,
    const char* end,
    float*      bounds)
{
    NVGstate*  state = &ctx->state;
    NVGtextRow rows[2];
    float      scale    = nvg__getFontScale(state) * ctx->devicePxRatio;
    float      invscale = 1.0f / scale;
    int        nrows    = 0, i;
    int        oldAlign = state->textAlign;
    int        halign   = state->textAlign & (NVG_ALIGN_LEFT | NVG_ALIGN_CENTER | NVG_ALIGN_RIGHT);
    int        valign   = state->textAlign & (NVG_ALIGN_TOP | NVG_ALIGN_MIDDLE | NVG_ALIGN_BOTTOM | NVG_ALIGN_BASELINE);
    float      lineh = 0, rminy = 0, rmaxy = 0;
    float      minx, miny, maxx, maxy;

    if (state->fontId == FONS_INVALID)
    {
        if (bounds != NULL)
            bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0.0f;
        return;
    }

    nvgTextMetrics(ctx, NULL, NULL, &lineh);

    state->textAlign = NVG_ALIGN_LEFT | valign;

    minx = maxx = x;
    miny = maxy = y;

    fonsSetSize(ctx->fs, state->fontSize * scale);
    fonsSetSpacing(ctx->fs, state->letterSpacing * scale);
    fonsSetBlur(ctx->fs, state->fontBlur * scale);
    fonsSetAlign(ctx->fs, state->textAlign);
    fonsSetFont(ctx->fs, state->fontId);
    fonsLineBounds(ctx->fs, 0, &rminy, &rmaxy);
    rminy *= invscale;
    rmaxy *= invscale;

    while ((nrows = nvgTextBreakLines(ctx, string, end, breakRowWidth, rows, 2)))
    {
        for (i = 0; i < nrows; i++)
        {
            NVGtextRow* row = &rows[i];
            float       rminx, rmaxx, dx = 0;
            // Horizontal bounds
            if (halign & NVG_ALIGN_LEFT)
                dx = 0;
            else if (halign & NVG_ALIGN_CENTER)
                dx = breakRowWidth * 0.5f - row->width * 0.5f;
            else if (halign & NVG_ALIGN_RIGHT)
                dx = breakRowWidth - row->width;
            rminx = x + row->minx + dx;
            rmaxx = x + row->maxx + dx;
            minx  = nvg__minf(minx, rminx);
            maxx  = nvg__maxf(maxx, rmaxx);
            // Vertical bounds.
            miny = nvg__minf(miny, y + rminy);
            maxy = nvg__maxf(maxy, y + rmaxy);

            y += lineh * state->lineHeight;
        }
        string = rows[nrows - 1].next;
    }

    state->textAlign = oldAlign;

    if (bounds != NULL)
    {
        bounds[0] = minx;
        bounds[1] = miny;
        bounds[2] = maxx;
        bounds[3] = maxy;
    }
}

void nvgTextMetrics(NVGcontext* ctx, float* ascender, float* descender, float* lineh)
{
    NVGstate* state    = &ctx->state;
    float     scale    = nvg__getFontScale(state) * ctx->devicePxRatio;
    float     invscale = 1.0f / scale;

    if (state->fontId == FONS_INVALID)
        return;

    fonsSetSize(ctx->fs, state->fontSize * scale);
    fonsSetSpacing(ctx->fs, state->letterSpacing * scale);
    fonsSetBlur(ctx->fs, state->fontBlur * scale);
    fonsSetAlign(ctx->fs, state->textAlign);
    fonsSetFont(ctx->fs, state->fontId);

    fonsVertMetrics(ctx->fs, ascender, descender, lineh);
    if (ascender != NULL)
        *ascender *= invscale;
    if (descender != NULL)
        *descender *= invscale;
    if (lineh != NULL)
        *lineh *= invscale;
}

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

static SGNVGtexture* sgnvg__allocTexture(NVGcontext* ctx)
{
    SGNVGtexture* tex = NULL;
    int           i;

    for (i = 0; i < ctx->ntextures; i++)
    {
        if (ctx->textures[i].img.id == 0)
        {
            tex = &ctx->textures[i];
            break;
        }
    }
    if (tex == NULL)
    {
        if (ctx->ntextures + 1 > ctx->ctextures)
        {
            SGNVGtexture* textures;
            int           ctextures = nvg__maxi(ctx->ntextures + 1, 4) + ctx->ctextures / 2; // 1.5x Overallocate
            textures                = (SGNVGtexture*)NVG_REALLOC(ctx->textures, sizeof(SGNVGtexture) * ctextures);
            if (textures == NULL)
                return NULL;
            ctx->textures  = textures;
            ctx->ctextures = ctextures;
        }
        tex = &ctx->textures[ctx->ntextures++];
    }

    memset(tex, 0, sizeof(*tex));
    return tex;
}

static SGNVGtexture* sgnvg__findTexture(NVGcontext* ctx, int id)
{
    int i;
    for (i = 0; i < ctx->ntextures; i++)
        if (ctx->textures[i].img.id == id)
            return &ctx->textures[i];
    return NULL;
}

static uint16_t sgnvg__getCombinedBlendNumber(sg_blend_state blend)
{
#if __STDC_VERSION__ >= 201112L
    _Static_assert(_SG_BLENDFACTOR_NUM <= 17, "too many blend factors for 16-bit blend number");
#else
    NVG_ASSERT(_SG_BLENDFACTOR_NUM <= 17); // can be a _Static_assert
#endif
    return blend.src_factor_rgb | (blend.dst_factor_rgb << 4) | (blend.src_factor_alpha << 8) |
           (blend.dst_factor_alpha << 12);
}

static void sgnvg__initPipeline(
    NVGcontext*             ctx,
    sg_pipeline             pip,
    const sg_stencil_state* stencil,
    sg_color_mask           write_mask,
    sg_cull_mode            cull_mode)
{
    sg_init_pipeline(
        pip,
        &(sg_pipeline_desc){
            .shader = ctx->shader,
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
                    .blend      = ctx->blend,
                },
            .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
            .index_type     = SG_INDEXTYPE_UINT32,
            .cull_mode      = cull_mode,
            .face_winding   = SG_FACEWINDING_CCW,
            .label          = "nanovg.pipeline",
        });
}

static bool sgnvg__pipelineTypeIsInUse(NVGcontext* ctx, enum SGNVGpipelineType type)
{
    switch (type)
    {
    case SGNVG_PIP_BASE:
    case SGNVG_PIP_FILL_STENCIL:
    case SGNVG_PIP_FILL_DRAW:
        return true;
    case SGNVG_PIP_FILL_ANTIALIAS:
        return !!(ctx->flags & NVG_ANTIALIAS);
    case SGNVG_PIP_STROKE_STENCIL_DRAW:
    case SGNVG_PIP_STROKE_STENCIL_ANTIALIAS:
    case SGNVG_PIP_STROKE_STENCIL_CLEAR:
        return !!(ctx->flags & NVG_STENCIL_STROKES);

    case SGNVG_PIP_NUM_: // to avoid warnings
        break;           /* fall through to assert */
    }
    NVG_ASSERT(0);
    return false;
}

static int sgnvg__getIndexFromCache(NVGcontext* ctx, uint16_t blendNumber)
{
    uint16_t currentUse = ctx->pipelineCache.currentUse;

    int maxAge      = 0;
    int maxAgeIndex = 0;

    // find the correct cache entry for `blend_number`
    for (unsigned int i = 0; i < NANOVG_SG_PIPELINE_CACHE_SIZE; i++)
    {
        if (ctx->pipelineCache.keys[i].blend == blendNumber)
        {
            ctx->pipelineCache.keys[i].lastUse = ctx->pipelineCache.currentUse;
            return i;
        }
        int age = (uint16_t)(currentUse - ctx->pipelineCache.keys[i].lastUse);
        if (age > maxAge)
        {
            maxAge      = age;
            maxAgeIndex = i;
        }
    }

    // not found; reuse an old one
    ctx->pipelineCache.currentUse                = ++currentUse;
    ctx->pipelineCache.keys[maxAgeIndex].blend   = blendNumber;
    ctx->pipelineCache.keys[maxAgeIndex].lastUse = currentUse;

    sg_pipeline* pipelines       = ctx->pipelineCache.pipelines[maxAgeIndex];
    uint8_t      pipelinesActive = ctx->pipelineCache.pipelinesActive[maxAgeIndex];
    // we may have had data already initialized; deinit those
    for (uint32_t type = SGNVG_PIP_BASE; type < SGNVG_PIP_NUM_; type++)
        if (pipelinesActive & (1 << type))
            sg_uninit_pipeline(pipelines[type]);
    // mark all as inactive
    ctx->pipelineCache.pipelinesActive[maxAgeIndex] = 0;
    return maxAgeIndex;
}

static sg_pipeline sgnvg__getPipelineFromCache(NVGcontext* ctx, enum SGNVGpipelineType type)
{
    NVG_ASSERT(sgnvg__pipelineTypeIsInUse(ctx, type));

    int         pipelineCacheIndex = ctx->pipelineCacheIndex;
    sg_pipeline pipeline           = ctx->pipelineCache.pipelines[pipelineCacheIndex][type];
    uint8_t     typeMask           = 1 << type;
    if (!(ctx->pipelineCache.pipelinesActive[pipelineCacheIndex] & typeMask))
    {
        ctx->pipelineCache.pipelinesActive[pipelineCacheIndex] |= typeMask;
        switch (type)
        {
        case SGNVG_PIP_BASE:
            sgnvg__initPipeline(
                ctx,
                pipeline,
                &(sg_stencil_state){
                    .enabled = false,
                },
                SG_COLORMASK_RGBA,
                SG_CULLMODE_BACK);
            break;

        case SGNVG_PIP_FILL_STENCIL:
            sgnvg__initPipeline(
                ctx,
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
                ctx,
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
                ctx,
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
                ctx,
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
                ctx,
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
                ctx,
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
            NVG_ASSERT(0);
        }
    }
    return pipeline;
}

static void sgnvg__preparePipelineUniforms(
    NVGcontext*            ctx,
    SGNVGfragUniforms*     uniforms,
    int                    image,
    enum SGNVGpipelineType pipelineType)
{
    sg_pipeline   pip = sgnvg__getPipelineFromCache(ctx, pipelineType);
    SGNVGtexture* tex = NULL;

    sg_apply_pipeline(pip);

    sg_apply_uniforms(UB_nanovg_viewSize, &(sg_range){&ctx->view, sizeof(ctx->view)});
    sg_apply_uniforms(UB_nanovg_frag, &(sg_range){uniforms, sizeof(*uniforms)});

    if (image != 0)
    {
        tex = sgnvg__findTexture(ctx, image);
    }
    // If no image is set, use empty texture
    if (tex == NULL)
    {
        tex = sgnvg__findTexture(ctx, ctx->dummyTex);
    }
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers[0]        = ctx->vertBuf,
        .index_buffer             = ctx->indexBuf,
        .images[IMG_nanovg_tex]   = tex ? tex->img : (sg_image){0},
        .samplers[SMP_nanovg_smp] = tex ? tex->smp : (sg_sampler){0},
    });
}

int nvgCreateTexture(NVGcontext* ctx, enum NVGtexture type, int w, int h, int imageFlags, const unsigned char* data)
{
    SGNVGtexture* tex = sgnvg__allocTexture(ctx);

    NVG_ASSERT(tex != NULL);
    if (tex == NULL)
        return 0;

    NVG_ASSERT(!(imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) && "TODO mipmaps");

    // if we have mipmaps, we forbid updating
    bool immutable = !!(imageFlags & (NVG_IMAGE_GENERATE_MIPMAPS | NVG_IMAGE_IMMUTABLE)) && data;

    tex->width  = w;
    tex->height = h;
    tex->type   = type;
    tex->flags  = imageFlags;

    sg_image_data imageData = {0};
    if ((imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) && data)
    {
        imageData.subimage[0][0] = (sg_range){data, w * h * (type == NVG_TEXTURE_RGBA ? 4 : 1)};
    }
    tex->img = sg_make_image(&(sg_image_desc){
        .type = SG_IMAGETYPE_2D,
        //.render_target
        .width                = w,
        .height               = h,
        .num_mipmaps          = 1, // TODO mipmaps
        .usage.immutable      = immutable,
        .usage.dynamic_update = !immutable,
        .pixel_format         = type == NVG_TEXTURE_RGBA ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8,
        .data                 = imageData,
        .label                = "nanovg.image[]",
    });
    NVG_ASSERT(tex->img.id != 0);
    tex->imgData = NVG_MALLOC(w * h * (type == NVG_TEXTURE_RGBA ? 4 : 1));
    if (data != NULL)
    {
        memcpy(tex->imgData, data, w * h * (type == NVG_TEXTURE_RGBA ? 4 : 1));
        tex->flags |= NVG_IMAGE_DIRTY;
    }
    tex->smp = (imageFlags & NVG_IMAGE_NEAREST) ? ctx->sampler_nearest : ctx->sampler_linear;

    return tex->img.id;
}

int nvgUpdateTexture(NVGcontext* ctx, int image, int x0, int y0, int w, int h, const unsigned char* data)
{
    SGNVGtexture* tex = sgnvg__findTexture(ctx, image);

    if (tex == NULL)
        return 0;

    bool immutable = tex->flags & (NVG_IMAGE_IMMUTABLE | NVG_IMAGE_GENERATE_MIPMAPS);
    NVG_ASSERT(!immutable);
    if (immutable)
        return 0;

    if (tex->imgData)
    {
        // this is really weird but nanogl_gl.h is doing the same
        // somehow we always get a whole row or so? o_O
        x0 = 0;
        w  = tex->width;

        size_t bytePerPixel = 1;
        if (tex->type == NVG_TEXTURE_RGBA)
            bytePerPixel = 4;

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

bool nvgGetImageSize(NVGcontext* ctx, int image, int* w, int* h)
{
    SGNVGtexture* tex = sgnvg__findTexture(ctx, image);
    if (tex == NULL)
        return 0;
    *w = tex->width;
    *h = tex->height;
    return 1;
}

static void sgnvg__xformToMat3x4(float* m3, float* t)
{
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

static NVGcolour sgnvg__premulColour(NVGcolour c)
{
    c.r *= c.a;
    c.g *= c.a;
    c.b *= c.a;
    return c;
}

static int sgnvg__convertPaint(
    NVGcontext*        ctx,
    SGNVGfragUniforms* frag,
    NVGpaint*          paint,
    NVGscissor*        scissor,
    float              width,
    float              fringe,
    float              strokeThr)
{
    SGNVGtexture* tex = NULL;
    float         invxform[6];

    memset(frag, 0, sizeof(*frag));

    frag->innerCol = sgnvg__premulColour(paint->innerColour);
    frag->outerCol = sgnvg__premulColour(paint->outerColour);

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
        tex = sgnvg__findTexture(ctx, paint->image);
        if (tex == NULL)
            return 0;

        nvgTransformInverse(invxform, paint->xform);
        frag->type = NSVG_SHADER_FILLIMG;

        if (tex->type == NVG_TEXTURE_RGBA)
            frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0.0f : 1.0f;
        else
            frag->texType = 2.0f;
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

static void sgnvg__fill(NVGcontext* ctx, SGNVGcall* call)
{
    SGNVGpath* paths = call->paths;
    int        i, npaths = call->num_paths;

    sgnvg__preparePipelineUniforms(ctx, call->uniforms, 0, SGNVG_PIP_FILL_STENCIL);
    for (i = 0; i < npaths; i++)
        sg_draw(paths[i].fillOffset, paths[i].fillCount, 1);

    // if (ctx->flags & NVG_ANTIALIAS) {
    sgnvg__preparePipelineUniforms(ctx, call->uniforms + 1, call->image, SGNVG_PIP_FILL_ANTIALIAS);
    // Draw fringes
    for (i = 0; i < npaths; i++)
        sg_draw(paths[i].strokeOffset, paths[i].strokeCount, 1);
    // }

    // Draw fill
    sgnvg__preparePipelineUniforms(ctx, call->uniforms + 1, call->image, SGNVG_PIP_FILL_DRAW);
    sg_draw(call->triangleOffset, call->triangleCount, 1);
}

static void sgnvg__convexFill(NVGcontext* ctx, SGNVGcall* call)
{
    SGNVGpath* paths = call->paths;
    int        i, npaths = call->num_paths;

    sgnvg__preparePipelineUniforms(ctx, call->uniforms, call->image, SGNVG_PIP_BASE);
    for (i = 0; i < npaths; i++)
    {
        sg_draw(paths[i].fillOffset, paths[i].fillCount, 1);
        // Draw fringes
        if (paths[i].strokeCount > 0)
        {
            sg_draw(paths[i].strokeOffset, paths[i].strokeCount, 1);
        }
    }
}

static void sgnvg__stroke(NVGcontext* ctx, SGNVGcall* call)
{
    SGNVGpath* paths  = call->paths;
    int        npaths = call->num_paths, i;

    if (ctx->flags & NVG_STENCIL_STROKES)
    {
        sgnvg__preparePipelineUniforms(ctx, call->uniforms + 1, call->image, SGNVG_PIP_STROKE_STENCIL_DRAW);

        for (i = 0; i < npaths; i++)
            sg_draw(paths[i].strokeOffset, paths[i].strokeCount, 1);

        // Draw anti-aliased pixels.
        sgnvg__preparePipelineUniforms(ctx, call->uniforms, call->image, SGNVG_PIP_STROKE_STENCIL_ANTIALIAS);
        for (i = 0; i < npaths; i++)
            sg_draw(paths[i].strokeOffset, paths[i].strokeCount, 1);

        // Clear stencil buffer.
        sgnvg__preparePipelineUniforms(ctx, call->uniforms, 0, SGNVG_PIP_STROKE_STENCIL_CLEAR);
        for (i = 0; i < npaths; i++)
            sg_draw(paths[i].strokeOffset, paths[i].strokeCount, 1);
    }
    else
    {
        sgnvg__preparePipelineUniforms(ctx, call->uniforms, call->image, SGNVG_PIP_BASE);
        // Draw Strokes
        for (i = 0; i < npaths; i++)
            sg_draw(paths[i].strokeOffset, paths[i].strokeCount, 1);
    }
}

static void sgnvg__triangles(NVGcontext* ctx, SGNVGcall* call)
{
    sgnvg__preparePipelineUniforms(ctx, call->uniforms, call->image, SGNVG_PIP_BASE);
    sg_draw(call->triangleOffset, call->triangleCount, 1);
}

static sg_blend_factor sgnvg_convertBlendFuncFactor(int factor)
{
    if (factor == NVG_ZERO)
        return SG_BLENDFACTOR_ZERO;
    if (factor == NVG_ONE)
        return SG_BLENDFACTOR_ONE;
    if (factor == NVG_SRC_COLOUR)
        return SG_BLENDFACTOR_SRC_COLOR;
    if (factor == NVG_ONE_MINUS_SRC_COLOUR)
        return SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    if (factor == NVG_DST_COLOUR)
        return SG_BLENDFACTOR_DST_COLOR;
    if (factor == NVG_ONE_MINUS_DST_COLOUR)
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

void nvgBeginFrame(NVGcontext* ctx, float devicePixelRatio)
{
    /*	printf("Tris: draws:%d  fill:%d  stroke:%d  text:%d  TOT:%d",
                    ctx->drawCallCount, ctx->fillTriCount, ctx->strokeTriCount, ctx->textTriCount,
                    ctx->fillTriCount+ctx->strokeTriCount+ctx->textTriCount);*/

    ctx->nstates = 0;
    nvgReset(ctx);

    ctx->drawCallCount  = 0;
    ctx->fillTriCount   = 0;
    ctx->strokeTriCount = 0;
    ctx->textTriCount   = 0;

    // Reset calls
    ctx->nverts        = 0;
    ctx->nindexes      = 0;
    ctx->first_command = NULL;

    linked_arena_clear(ctx->frame_arena);

    nvg__setDevicePixelRatio(ctx, devicePixelRatio);
}

void nvgEndFrame(NVGcontext* ctx)
{
    if (ctx->fontImageIdx != 0)
    {
        int fontImage                      = ctx->fontImages[ctx->fontImageIdx];
        ctx->fontImages[ctx->fontImageIdx] = 0;
        int i, j, iw, ih;
        // delete images that smaller than current one
        if (fontImage == 0)
            return;
        nvgGetImageSize(ctx, fontImage, &iw, &ih);
        for (i = j = 0; i < ctx->fontImageIdx; i++)
        {
            if (ctx->fontImages[i] != 0)
            {
                int nw, nh;
                int image          = ctx->fontImages[i];
                ctx->fontImages[i] = 0;
                nvgGetImageSize(ctx, image, &nw, &nh);
                if (nw < iw || nh < ih)
                    nvgDeleteImage(ctx, image);
                else
                    ctx->fontImages[j++] = image;
            }
        }
        // make current font image to first
        ctx->fontImages[j] = ctx->fontImages[0];
        ctx->fontImages[0] = fontImage;
        ctx->fontImageIdx  = 0;
    }

    for (int i = 0; i < ctx->ntextures; i++)
    {
        if (ctx->textures[i].img.id != 0)
        {
            SGNVGtexture* tex = &ctx->textures[i];

            if (tex->flags & NVG_IMAGE_DIRTY)
            {
                tex->flags   ^= NVG_IMAGE_DIRTY;
                int channels  = tex->type == NVG_TEXTURE_RGBA ? 4 : 1;
                int nbytes    = tex->width * tex->height * channels;
                sg_update_image(tex->img, &(sg_image_data){.subimage[0][0] = {tex->imgData, nbytes}});
            }
        }
    }

    if (ctx->nverts == 0 && ctx->nindexes == 0)
        return;

    if (ctx->cverts_gpu < ctx->nverts) // resize GPU vertex buffer
    {
        if (ctx->cverts_gpu) // delete old buffer if necessary
            sg_uninit_buffer(ctx->vertBuf);
        ctx->cverts_gpu = ctx->cverts;
        sg_init_buffer(
            ctx->vertBuf,
            &(sg_buffer_desc){
                .size                = ctx->cverts_gpu * sizeof(*ctx->verts),
                .usage.vertex_buffer = true,
                .usage.stream_update = true,
                .label               = "nanovg.vertBuf",
            });
    }
    // upload vertex data
    sg_update_buffer(ctx->vertBuf, &(sg_range){ctx->verts, ctx->nverts * sizeof(*ctx->verts)});

    if (ctx->cindexes_gpu < ctx->nindexes) // resize GPU index buffer
    {
        if (ctx->cindexes_gpu) // delete old buffer if necessary
            sg_uninit_buffer(ctx->indexBuf);
        ctx->cindexes_gpu = ctx->cindexes;
        sg_init_buffer(
            ctx->indexBuf,
            &(sg_buffer_desc){
                .size                = ctx->cindexes_gpu * sizeof(*ctx->indexes),
                .usage.index_buffer  = true,
                .usage.stream_update = true,
                .label               = "nanovg.indexBuf",
            });
    }
    // upload index data
    sg_update_buffer(ctx->indexBuf, &(sg_range){ctx->indexes, ctx->nindexes * sizeof(*ctx->indexes)});

    int           ncommands = 0;
    SGNVGcommand* cmd       = ctx->first_command;
    while (cmd != NULL)
    {
        switch (cmd->type)
        {
        case SGNVG_CMD_BEGIN_PASS:
        {
            SGNVGbeginPass* p = cmd->payload.beginPass;

            sg_begin_pass(&p->pass);

            ctx->view.viewSize[0] = p->width;
            ctx->view.viewSize[1] = p->height;

            break;
        }
        case SGNVG_CMD_END_PASS:
            sg_end_pass();
            break;
        case SGNVG_CMD_DRAW_NVG:
        {
            SGNVGdrawNVG* draws = cmd->payload.drawNVG;

            SGNVGcall* call = draws->calls;

            for (int i = 0; i < draws->num_calls && call != NULL; i++)
            {
                ctx->blend.src_factor_rgb   = call->blendFunc.srcRGB;
                ctx->blend.dst_factor_rgb   = call->blendFunc.dstRGB;
                ctx->blend.src_factor_alpha = call->blendFunc.srcAlpha;
                ctx->blend.dst_factor_alpha = call->blendFunc.dstAlpha;
                ctx->pipelineCacheIndex     = sgnvg__getIndexFromCache(ctx, sgnvg__getCombinedBlendNumber(ctx->blend));
                if (call->type == SGNVG_FILL)
                    sgnvg__fill(ctx, call);
                else if (call->type == SGNVG_CONVEXFILL)
                    sgnvg__convexFill(ctx, call);
                else if (call->type == SGNVG_STROKE)
                    sgnvg__stroke(ctx, call);
                else if (call->type == SGNVG_TRIANGLES)
                    sgnvg__triangles(ctx, call);

                call = call->next;
            }
            break;
        }
        case SGNVG_CMD_CUSTOM:
        {
            SGNVGcustom* custom = cmd->payload.custom;
            custom->func(custom->uptr);
            break;
        }
        }

        cmd = cmd->next;
        ncommands++;
    }
}

static int sgnvg__maxVertCount(const NVGpath* paths, int npaths)
{
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
    int i, count = 0;
    for (i = 0; i < npaths; i++)
    {
        count += nvg__maxi(paths[i].nfill - 2, 0) * 3;   // triangle fan
        count += nvg__maxi(paths[i].nstroke - 2, 0) * 3; // triangle strip
    }
    return count;
}

static void sgnvg__addCall(NVGcontext* ctx, SGNVGcall* call)
{
    if (ctx->current_call)
        ctx->current_call->next = call;
    ctx->current_call = call;

    xassert(ctx->current_nvg_draw != NULL);
    if (ctx->current_nvg_draw)
    {
        ctx->current_nvg_draw->num_calls++;
        if (ctx->current_nvg_draw->calls == NULL)
            ctx->current_nvg_draw->calls = call;
    }
}

static SGNVGcommand* sgnvg__allocCommand(NVGcontext* ctx)
{
    SGNVGcommand* cmd = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*cmd));

    if (ctx->first_command == NULL)
        ctx->first_command = cmd;

    if (ctx->current_command)
        ctx->current_command->next = cmd;

    ctx->current_command = cmd;

    return cmd;
}

static int sgnvg__allocVerts(NVGcontext* ctx, int n)
{
    int ret = 0;
    if (ctx->nverts + n > ctx->cverts)
    {
        SGNVGattribute* verts;
        int             cverts = nvg__maxi(ctx->nverts + n, 4096) + ctx->cverts / 2; // 1.5x Overallocate
        verts                  = (SGNVGattribute*)NVG_REALLOC(ctx->verts, sizeof(SGNVGattribute) * cverts);
        if (verts == NULL)
            return -1;
        ctx->verts  = verts;
        ctx->cverts = cverts;
    }
    ret          = ctx->nverts;
    ctx->nverts += n;
    return ret;
}

static int sgnvg__allocIndexes(NVGcontext* ctx, int n)
{
    int ret = 0;
    if (ctx->nindexes + n > ctx->cindexes)
    {
        uint32_t* indexes;
        int       cindexes = nvg__maxi(ctx->nindexes + n, 4096) + ctx->cindexes / 2; // 1.5x Overallocate
        indexes            = (uint32_t*)NVG_REALLOC(ctx->indexes, sizeof(uint32_t) * cindexes);
        if (indexes == NULL)
            return -1;
        ctx->indexes  = indexes;
        ctx->cindexes = cindexes;
    }
    ret            = ctx->nindexes;
    ctx->nindexes += n;
    return ret;
}

static void sgnvg__vset(SGNVGattribute* vtx, float x, float y, float u, float v)
{
    vtx->vertex[0] = x;
    vtx->vertex[1] = y;
    vtx->tcoord[0] = u;
    vtx->tcoord[1] = v;
}

static void sgnvg__generateTriangleFanIndexes(uint32_t* indexes, int offset, int nverts)
{
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

void nvgFill(NVGcontext* ctx)
{
    NVGstate*      state = &ctx->state;
    const NVGpath* path;
    NVGpaint       paint             = state->paint;
    float          expandFringeWidth = 0;
    int            i;

    nvg__flattenPaths(ctx);
    if (ctx->edgeAntiAlias && state->shapeAntiAlias)
        expandFringeWidth = ctx->fringeWidth;
    nvg__expandFill(ctx, expandFringeWidth, NVG_MITER, 2.4f);

    NVGcompositeOperationState compositeOperation = state->compositeOperation;
    NVGscissor*                scissor            = &state->scissor;
    float                      fringe             = ctx->fringeWidth;
    const float*               bounds             = ctx->cache.bounds;
    const NVGpath*             paths              = ctx->cache.paths;
    int                        npaths             = ctx->cache.npaths;

    SGNVGcall*         call = NULL;
    SGNVGattribute*    quad = NULL;
    SGNVGfragUniforms* frag = NULL;
    int                maxverts, offset, maxindexes, ioffset;

    // Looks like you forgot to call snvg_command_draw_nvg() before issuing nvgFill()/nvgStroke()/nvgText() commands!
    // NVG_ASSERT(ctx->current_nvg_draw != NULL); // TODO: remove?
    if (ctx->current_nvg_draw == NULL)
        snvg_command_draw_nvg(ctx);

    call = linked_arena_alloc(ctx->frame_arena, sizeof(*call));

    if (call == NULL)
        return;

    call->type          = SGNVG_FILL;
    call->triangleCount = 4;
    call->paths         = linked_arena_alloc_clear(ctx->frame_arena, npaths * sizeof(*call->paths));
    if (call->paths == NULL)
        return;
    call->num_paths = npaths;
    call->image     = paint.image;
    call->blendFunc = sgnvg__blendCompositeOperation(compositeOperation);

    if (npaths == 1 && paths[0].convex)
    {
        call->type          = SGNVG_CONVEXFILL;
        call->triangleCount = 0; // Bounding box fill quad not needed for convex fill
    }

    // Allocate vertices for all the paths.
    maxverts = sgnvg__maxVertCount(paths, npaths) + call->triangleCount;
    offset   = sgnvg__allocVerts(ctx, maxverts);
    if (offset == -1)
        return;
    maxindexes = sgnvg__maxIndexCount(paths, npaths) + nvg__maxi(call->triangleCount - 2, 0) * 3;
    ioffset    = sgnvg__allocIndexes(ctx, maxindexes);
    if (ioffset == -1)
        return;

    for (i = 0; i < npaths; i++)
    {
        SGNVGpath* copy = &call->paths[i];
        path            = &paths[i];
        if (path->nfill > 0)
        {
            // fill: triangle fan
            copy->fillOffset = ioffset;
            copy->fillCount  = (path->nfill - 2) * 3;
            memcpy(&ctx->verts[offset], path->fill, sizeof(NVGvertex) * path->nfill);
            sgnvg__generateTriangleFanIndexes(&ctx->indexes[ioffset], offset, path->nfill);
            offset  += path->nfill;
            ioffset += copy->fillCount;
        }
        if (path->nstroke > 0)
        {
            // stroke: triangle strip
            copy->strokeOffset = ioffset;
            copy->strokeCount  = (path->nstroke - 2) * 3;
            memcpy(&ctx->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
            sgnvg__generateTriangleStripIndexes(&ctx->indexes[ioffset], offset, path->nstroke);
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
        quad                 = &ctx->verts[offset];
        sgnvg__vset(&quad[0], bounds[2], bounds[3], 0.5f, 1.0f);
        sgnvg__vset(&quad[1], bounds[2], bounds[1], 0.5f, 1.0f);
        sgnvg__vset(&quad[2], bounds[0], bounds[3], 0.5f, 1.0f);
        sgnvg__vset(&quad[3], bounds[0], bounds[1], 0.5f, 1.0f);
        sgnvg__generateTriangleStripIndexes(&ctx->indexes[ioffset], offset, 4);

        frag = linked_arena_alloc_clear(ctx->frame_arena, 2 * sizeof(*frag));
        if (frag == NULL)
            return;

        call->uniforms = frag;

        // Simple shader for stencil
        call->uniforms->strokeThr = -1.0f;
        call->uniforms->type      = NSVG_SHADER_SIMPLE;
        // Fill shader
        sgnvg__convertPaint(ctx, call->uniforms + 1, &paint, scissor, fringe, fringe, -1.0f);
    }
    else
    {
        frag = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*frag));
        if (frag == NULL)
            return;
        call->uniforms = frag;
        // Fill shader
        sgnvg__convertPaint(ctx, frag, &paint, scissor, fringe, fringe, -1.0f);
    }

    sgnvg__addCall(ctx, call);

    // Count triangles
    for (i = 0; i < ctx->cache.npaths; i++)
    {
        path                = &ctx->cache.paths[i];
        ctx->fillTriCount  += path->nfill - 2;
        ctx->fillTriCount  += path->nstroke - 2;
        ctx->drawCallCount += 2;
    }
}

void nvgStroke(NVGcontext* ctx)
{
    NVGstate* state             = &ctx->state;
    float     scale             = nvg__getAverageScale(state->xform);
    float     strokeWidth       = nvg__clampf(state->strokeWidth * scale, 0.0f, 200.0f);
    NVGpaint  paint             = state->paint;
    float     expandFringeWidth = 0;
    int       i;

    if (strokeWidth < ctx->fringeWidth)
    {
        // If the stroke width is less than pixel size, use alpha to emulate coverage.
        // Since coverage is area, scale by alpha*alpha.
        float alpha          = nvg__clampf(strokeWidth / ctx->fringeWidth, 0.0f, 1.0f);
        paint.innerColour.a *= alpha * alpha;
        paint.outerColour.a *= alpha * alpha;
        strokeWidth          = ctx->fringeWidth;
    }

    nvg__flattenPaths(ctx);

    if (ctx->edgeAntiAlias && state->shapeAntiAlias)
        expandFringeWidth = ctx->fringeWidth;
    nvg__expandStroke(ctx, strokeWidth * 0.5f, expandFringeWidth, state->lineCap, state->lineJoin, state->miterLimit);

    NVGcompositeOperationState compositeOperation = state->compositeOperation;
    NVGscissor*                scissor            = &state->scissor;
    float                      fringe             = ctx->fringeWidth;
    const NVGpath*             paths              = ctx->cache.paths;
    int                        npaths             = ctx->cache.npaths;

    SGNVGcall*         call  = NULL;
    SGNVGfragUniforms* frags = NULL;
    int                maxverts, offset, maxindexes, ioffset;

    // Looks like you forgot to call snvg_command_draw_nvg() before issuing nvgFill()/nvgStroke()/nvgText() commands!
    // NVG_ASSERT(ctx->current_nvg_draw != NULL); // TODO: remove?
    if (ctx->current_nvg_draw == NULL)
        snvg_command_draw_nvg(ctx);

    call = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*call));

    if (call == NULL)
        return;

    call->type  = SGNVG_STROKE;
    call->paths = linked_arena_alloc_clear(ctx->frame_arena, npaths * sizeof(*call->paths));
    if (call->paths == NULL)
        return;
    call->num_paths = npaths;
    call->image     = paint.image;
    call->blendFunc = sgnvg__blendCompositeOperation(compositeOperation);

    // Allocate vertices for all the paths.
    maxverts = sgnvg__maxVertCount(paths, npaths);
    offset   = sgnvg__allocVerts(ctx, maxverts);
    if (offset == -1)
        return;
    maxindexes = sgnvg__maxIndexCount(paths, npaths);
    ioffset    = sgnvg__allocIndexes(ctx, maxindexes);
    if (ioffset == -1)
        return;

    for (i = 0; i < npaths; i++)
    {
        SGNVGpath*     copy = &call->paths[i];
        const NVGpath* path = &paths[i];

        if (path->nstroke)
        {
            // stroke: triangle strip
            copy->strokeOffset = ioffset;
            copy->strokeCount  = (path->nstroke - 2) * 3;
            memcpy(&ctx->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
            sgnvg__generateTriangleStripIndexes(&ctx->indexes[ioffset], offset, path->nstroke);
            offset  += path->nstroke;
            ioffset += copy->strokeCount;
        }
    }

    if (ctx->flags & NVG_STENCIL_STROKES)
    {
        // Fill shader
        frags = linked_arena_alloc_clear(ctx->frame_arena, 2 * sizeof(*frags));

        if (frags == NULL)
            return;

        call->uniforms = frags;

        sgnvg__convertPaint(ctx, call->uniforms, &paint, scissor, strokeWidth, fringe, -1.0f);
        sgnvg__convertPaint(ctx, call->uniforms + 1, &paint, scissor, strokeWidth, fringe, 1.0f - 0.5f / 255.0f);
    }
    else
    {
        // Fill shader
        frags = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*frags));
        if (frags == NULL)
            return;
        call->uniforms = frags;
        sgnvg__convertPaint(ctx, call->uniforms, &paint, scissor, strokeWidth, fringe, -1.0f);
    }

    sgnvg__addCall(ctx, call);

    // Count triangles
    for (i = 0; i < ctx->cache.npaths; i++)
    {
        const NVGpath* path  = &ctx->cache.paths[i];
        ctx->strokeTriCount += path->nstroke - 2;
        ctx->drawCallCount++;
    }
}

void nvg__renderText(NVGcontext* ctx, NVGvertex* verts, int nverts)
{
    NVGstate* state = &ctx->state;
    NVGpaint  paint = state->paint;

    // Render triangles.
    paint.image = ctx->fontImages[ctx->fontImageIdx];

    SGNVGcall*         call = NULL;
    SGNVGfragUniforms* frag = NULL;

    // Looks like you forgot to call snvg_command_draw_nvg() before issuing nvgFill()/nvgStroke()/nvgText() commands!
    // NVG_ASSERT(ctx->current_nvg_draw != NULL); // TODO: remove?
    if (ctx->current_nvg_draw == NULL)
        snvg_command_draw_nvg(ctx);

    call = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*call));

    if (call == NULL)
        return;

    call->type      = SGNVG_TRIANGLES;
    call->image     = paint.image;
    call->blendFunc = sgnvg__blendCompositeOperation(state->compositeOperation);

    int offset, ioffset;
    offset = sgnvg__allocVerts(ctx, nverts);
    if (offset == -1)
        return;
    ioffset = sgnvg__allocIndexes(ctx, nverts);
    if (ioffset == -1)
        return;

    // Allocate vertices for all the paths.
    call->triangleOffset = ioffset;
    call->triangleCount  = nverts;
    memcpy(&ctx->verts[offset], verts, sizeof(NVGvertex) * nverts);
    for (int i = 0; i < nverts; i++)
        ctx->indexes[ioffset + i] = offset + i;

    // Fill shader
    frag = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*frag));
    if (frag == NULL)
        return;
    call->uniforms = frag;
    sgnvg__convertPaint(ctx, frag, &paint, &state->scissor, 1.0f, ctx->fringeWidth, -1.0f);
    frag->type = NSVG_SHADER_IMG;

    // Success
    sgnvg__addCall(ctx, call);

    ctx->drawCallCount++;
    ctx->textTriCount += nverts / 3;
}

int snvgCreateImageFromHandleSokol(
    NVGcontext*     ctx,
    sg_image        imageSokol,
    sg_sampler      samplerSokol,
    enum NVGtexture type,
    int             w,
    int             h,
    int             flags)
{
    SGNVGtexture* tex = sgnvg__allocTexture(ctx);

    if (tex == NULL)
        return 0;

    tex->type   = type;
    tex->img    = imageSokol;
    tex->smp    = samplerSokol;
    tex->flags  = flags;
    tex->width  = w;
    tex->height = h;

    return tex->img.id;
}

void snvg_command_begin_pass(NVGcontext* ctx, const sg_pass* pass, int width, int height)
{
    SGNVGcommand*   cmd = sgnvg__allocCommand(ctx);
    SGNVGbeginPass* bp  = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*bp));

    cmd->type              = SGNVG_CMD_BEGIN_PASS;
    cmd->payload.beginPass = bp;

    bp->pass   = *pass;
    bp->width  = width;
    bp->height = height;
}

void snvg_command_end_pass(NVGcontext* ctx)
{
    SGNVGcommand* cmd = sgnvg__allocCommand(ctx);
    cmd->type         = SGNVG_CMD_END_PASS;
}

void snvg_command_draw_nvg(NVGcontext* ctx)
{
    SGNVGcommand* cmd   = sgnvg__allocCommand(ctx);
    SGNVGdrawNVG* draws = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*draws));

    cmd->type            = SGNVG_CMD_DRAW_NVG;
    cmd->payload.drawNVG = draws;

    ctx->current_nvg_draw = draws;
}

void snvg_command_custom(NVGcontext* ctx, void* uptr, SGNVGcustomFunc func)
{
    // Clear cached call points
    // This will help us to enforce users are correctly calling snvg_command_draw_nvg() before issuing
    // nvgFill/Stroke/Text commands
    ctx->current_call     = NULL;
    ctx->current_nvg_draw = NULL;

    SGNVGcommand* cmd    = sgnvg__allocCommand(ctx);
    SGNVGcustom*  custom = linked_arena_alloc_clear(ctx->frame_arena, sizeof(*custom));

    cmd->type           = SGNVG_CMD_CUSTOM;
    cmd->payload.custom = custom;

    custom->uptr = uptr;
    custom->func = func;
}

NVGcontext* nvgCreateContext(int flags)
{
    NVGcontext* ctx = (NVGcontext*)NVG_MALLOC(sizeof(NVGcontext));
    FONSparams  fontParams;
    int         i;

    NVG_ASSERT_GOTO(ctx != NULL, error);
    memset(ctx, 0, sizeof(*ctx));

    ctx->frame_arena = linked_arena_create(1024 * 64);
    NVG_ASSERT_GOTO(ctx->frame_arena != NULL, error);

    ctx->edgeAntiAlias = flags & NVG_ANTIALIAS ? 1 : 0;
    ctx->flags         = flags;

    // if(ctx->flags & NVG_ANTIALIAS)
    ctx->shader = sg_make_shader(nanovg_sg_shader_desc(sg_query_backend()));
    // else
    // ctx->shader = sg_make_shader(nanovg_sg_shader_desc(sg_query_backend()));
    for (i = 0; i < NANOVG_SG_PIPELINE_CACHE_SIZE; i++)
    {
        for (enum SGNVGpipelineType t = 0; t < SGNVG_PIP_NUM_; t++)
        {
            // only allocate pipelines if correct flags are set
            if (!sgnvg__pipelineTypeIsInUse(ctx, t))
                continue;
            ctx->pipelineCache.pipelines[i][t] = sg_alloc_pipeline();
        }
    }

    ctx->sampler_linear  = sg_make_sampler(&(sg_sampler_desc){
         .min_filter    = SG_FILTER_LINEAR,
         .mag_filter    = SG_FILTER_LINEAR,
         .mipmap_filter = SG_FILTER_LINEAR,
         .wrap_u        = SG_WRAP_CLAMP_TO_EDGE,
         .wrap_v        = SG_WRAP_CLAMP_TO_EDGE,
    });
    ctx->sampler_nearest = sg_make_sampler(&(sg_sampler_desc){
        .min_filter    = SG_FILTER_NEAREST,
        .mag_filter    = SG_FILTER_NEAREST,
        .mipmap_filter = SG_FILTER_NEAREST,
        .wrap_u        = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v        = SG_WRAP_CLAMP_TO_EDGE,
    });

    ctx->blend = (sg_blend_state){
        .enabled          = true,
        .src_factor_rgb   = SG_BLENDFACTOR_ZERO,
        .dst_factor_rgb   = SG_BLENDFACTOR_ZERO,
        .op_rgb           = SG_BLENDOP_ADD,
        .src_factor_alpha = SG_BLENDFACTOR_ZERO,
        .dst_factor_alpha = SG_BLENDFACTOR_ZERO,
        .op_alpha         = SG_BLENDOP_ADD,
    };

    ctx->vertBuf  = sg_alloc_buffer();
    ctx->indexBuf = sg_alloc_buffer();

    // Some platforms does not allow to have samples to unset textures.
    // Create empty one which is bound when there's no texture specified.
    ctx->dummyTex = nvgCreateTexture(ctx, NVG_TEXTURE_ALPHA, 1, 1, 0, NULL);

    nvgReset(ctx);
    nvg__setDevicePixelRatio(ctx, 1.0f);

    ctx->commands  = (float*)NVG_MALLOC(sizeof(float) * NVG_INIT_COMMANDS_SIZE);
    ctx->ccommands = NVG_INIT_COMMANDS_SIZE;
    NVG_ASSERT_GOTO(ctx->commands != NULL, error);

    ctx->cache.points  = (NVGpoint*)NVG_MALLOC(sizeof(NVGpoint) * NVG_INIT_POINTS_SIZE);
    ctx->cache.cpoints = NVG_INIT_POINTS_SIZE;
    NVG_ASSERT_GOTO(ctx->cache.points != NULL, error);

    ctx->cache.paths  = (NVGpath*)NVG_MALLOC(sizeof(NVGpath) * NVG_INIT_PATHS_SIZE);
    ctx->cache.cpaths = NVG_INIT_PATHS_SIZE;
    NVG_ASSERT_GOTO(ctx->cache.paths != NULL, error);

    ctx->cache.verts  = (NVGvertex*)NVG_MALLOC(sizeof(NVGvertex) * NVG_INIT_VERTS_SIZE);
    ctx->cache.cverts = NVG_INIT_VERTS_SIZE;
    NVG_ASSERT_GOTO(ctx->cache.verts != NULL, error);

    // Init font rendering
    memset(&fontParams, 0, sizeof(fontParams));
    fontParams.width  = NVG_INIT_FONTIMAGE_SIZE;
    fontParams.height = NVG_INIT_FONTIMAGE_SIZE;
    fontParams.flags  = FONS_ZERO_TOPLEFT;
    ctx->fs           = fonsCreateInternal(&fontParams);
    NVG_ASSERT_GOTO(ctx->fs != NULL, error);

    // Create font texture
    ctx->fontImages[0] = nvgCreateTexture(ctx, NVG_TEXTURE_ALPHA, fontParams.width, fontParams.height, 0, NULL);
    NVG_ASSERT_GOTO(ctx->fontImages[0] != 0, error);

    return ctx;

error:
    if (ctx != NULL)
        nvgDestroyContext(ctx);
    return NULL;
}

void nvgDestroyContext(NVGcontext* ctx)
{
    int i;
    if (ctx == NULL)
        return;
    if (ctx->commands != NULL)
        NVG_FREE(ctx->commands);
    if (ctx->cache.points != NULL)
        NVG_FREE(ctx->cache.points);
    if (ctx->cache.paths != NULL)
        NVG_FREE(ctx->cache.paths);
    if (ctx->cache.verts != NULL)
        NVG_FREE(ctx->cache.verts);

    if (ctx->fs)
        fonsDeleteInternal(ctx->fs);

    for (i = 0; i < NVG_MAX_FONTIMAGES; i++)
    {
        if (ctx->fontImages[i] != 0)
        {
            nvgDeleteImage(ctx, ctx->fontImages[i]);
            ctx->fontImages[i] = 0;
        }
    }

    sg_destroy_shader(ctx->shader);

    for (i = 0; i < NANOVG_SG_PIPELINE_CACHE_SIZE; i++)
    {
        for (enum SGNVGpipelineType t = 0; t < SGNVG_PIP_NUM_; t++)
        {
            // only uninitialize if correct flags are set
            if (!sgnvg__pipelineTypeIsInUse(ctx, t))
                continue;
            if (ctx->pipelineCache.pipelinesActive[i] & (1 << t))
                sg_uninit_pipeline(ctx->pipelineCache.pipelines[i][t]);
            sg_dealloc_pipeline(ctx->pipelineCache.pipelines[i][t]);
        }
    }

    if (ctx->cverts_gpu)
        sg_uninit_buffer(ctx->vertBuf);
    sg_dealloc_buffer(ctx->vertBuf);

    if (ctx->cindexes_gpu)
        sg_uninit_buffer(ctx->indexBuf);
    sg_dealloc_buffer(ctx->indexBuf);

    nvgDeleteImage(ctx, ctx->dummyTex);
    for (i = 0; i < ctx->ntextures; i++)
    {
        bool img_exists    = ctx->textures[i].img.id != 0;
        bool should_delete = (ctx->textures[i].flags & NVG_IMAGE_NODELETE) == 0;
        if (img_exists && should_delete)
            sg_destroy_image(ctx->textures[i].img);
    }

    if (ctx->textures)
    {
        NVG_FREE(ctx->textures);
    }
    if (ctx->verts)
    {
        NVG_FREE(ctx->verts);
    }
    if (ctx->indexes)
    {
        NVG_FREE(ctx->indexes);
    }

    if (ctx->frame_arena)
    {
        linked_arena_destroy(ctx->frame_arena);
    }

    NVG_FREE(ctx);
}