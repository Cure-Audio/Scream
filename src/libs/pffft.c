// clang-format off
/* Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )

   Based on original fortran 77 code from FFTPACKv4 from NETLIB
   (http://www.netlib.org/fftpack), authored by Dr Paul Swarztrauber
   of NCAR, in 1985.

   As confirmed by the NCAR fftpack software curators, the following
   FFTPACKv5 license applies to FFTPACKv4 sources. My changes are
   released under the same terms.

   FFTPACK license:

   http://www.cisl.ucar.edu/css/software/fftpack5/ftpk.html

   Copyright (c) 2004 the University Corporation for Atmospheric
   Research ("UCAR"). All rights reserved. Developed by NCAR's
   Computational and Information Systems Laboratory, UCAR,
   www.cisl.ucar.edu.

   Redistribution and use of the Software in source and binary forms,
   with or without modification, is permitted provided that the
   following conditions are met:

   - Neither the names of NCAR's Computational and Information Systems
   Laboratory, the University Corporation for Atmospheric Research,
   nor the names of its sponsors or contributors may be used to
   endorse or promote products derived from this Software without
   specific prior written permission.  

   - Redistributions of source code must retain the above copyright
   notices, this list of conditions, and the disclaimer below.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions, and the disclaimer below in the
   documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
   SOFTWARE.


   PFFFT : a Pretty Fast FFT.

   This file is largerly based on the original FFTPACK implementation, modified in
   order to take advantage of SIMD instructions of modern CPUs.
*/

/*
  ChangeLog: 
  - 2011/10/02, version 1: This is the very first release of this file.
*/

#include "pffft.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* detect compiler flavour */
#if defined(__GNUC__)
#  define PFFFT_ALWAYS_INLINE(return_type) inline return_type __attribute__ ((always_inline))
#  define PFFFT_NEVER_INLINE(return_type) return_type __attribute__ ((noinline))
#  define PFFFT_RESTRICT __restrict
#  define PFFFT_VLA_ARRAY_ON_STACK(type__, varname__, size__) type__ varname__[size__];
#elif defined(_MSC_VER)
#  define PFFFT_ALWAYS_INLINE(return_type) __forceinline return_type
#  define PFFFT_NEVER_INLINE(return_type) __declspec(noinline) return_type
#  define PFFFT_RESTRICT __restrict
#  define PFFFT_VLA_ARRAY_ON_STACK(type__, varname__, size__) type__ *varname__ = (type__*)_alloca(size__ * sizeof(type__))
#endif

#if !defined(PFFFT_MALLOC) || !defined(PFFFT_FREE)
#  define PFFFT_MALLOC malloc
#  define PFFFT_FREE free
#endif

#ifndef PFFFT_ASSERT
#include <assert.h>
#  define PFFFT_ASSERT assert
#endif

/* 
   vector support macros: the rest of the code is independant of
   SSE/Altivec/NEON -- adding support for other platforms with 4-element
   vectors should be limited to these macros 
*/


// define PFFFT_SIMD_DISABLE if you want to use scalar code instead of simd code
//#define PFFFT_SIMD_DISABLE

/*
   Altivec support macros 
*/
#if !defined(PFFFT_SIMD_DISABLE) && (defined(__ppc__) || defined(__ppc64__) || defined(__powerpc__) || defined(__powerpc64__))
#include <altivec.h>
typedef vector float v4sf;
#  define PFFFT_SIMD_SZ 4
#  define PFFFT_VZERO() ((vector float) vec_splat_u8(0))
#  define PFFFT_VMUL(a,b) vec_madd(a,b, PFFFT_VZERO())
#  define PFFFT_VADD(a,b) vec_add(a,b)
#  define PFFFT_VMADD(a,b,c) vec_madd(a,b,c)
#  define PFFFT_VSUB(a,b) vec_sub(a,b)
inline v4sf ld_ps1(const float *p) { v4sf v=vec_lde(0,p); return vec_splat(vec_perm(v, v, vec_lvsl(0, p)), 0); }
#  define PFFFT_LD_PS1(p) ld_ps1(&p)
#  define PFFFT_INTERLEAVE2(in1, in2, out1, out2) { v4sf tmp__ = vec_mergeh(in1, in2); out2 = vec_mergel(in1, in2); out1 = tmp__; }
#  define PFFFT_UNINTERLEAVE2(in1, in2, out1, out2) {                           \
    vector unsigned char vperm1 =  (vector unsigned char){0,1,2,3,8,9,10,11,16,17,18,19,24,25,26,27}; \
    vector unsigned char vperm2 =  (vector unsigned char){4,5,6,7,12,13,14,15,20,21,22,23,28,29,30,31}; \
    v4sf tmp__ = vec_perm(in1, in2, vperm1); out2 = vec_perm(in1, in2, vperm2); out1 = tmp__; \
  }
#  define PFFFT_VTRANSPOSE4(x0,x1,x2,x3) {              \
    v4sf y0 = vec_mergeh(x0, x2);               \
    v4sf y1 = vec_mergel(x0, x2);               \
    v4sf y2 = vec_mergeh(x1, x3);               \
    v4sf y3 = vec_mergel(x1, x3);               \
    x0 = vec_mergeh(y0, y2);                    \
    x1 = vec_mergel(y0, y2);                    \
    x2 = vec_mergeh(y1, y3);                    \
    x3 = vec_mergel(y1, y3);                    \
  }
#  define PFFFT_VSWAPHL(a,b) vec_perm(a,b, (vector unsigned char){16,17,18,19,20,21,22,23,8,9,10,11,12,13,14,15})
#  define PFFFT_VALIGNED(ptr) ((((long long)(ptr)) & 0xF) == 0)

/*
  SSE1 support macros
*/
#elif !defined(PFFFT_SIMD_DISABLE) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(i386) || defined(_M_IX86))

#include <xmmintrin.h>
typedef __m128 v4sf;
#  define PFFFT_SIMD_SZ 4 // 4 floats by simd vector -- this is pretty much hardcoded in the preprocess/finalize functions anyway so you will have to work if you want to enable AVX with its 256-bit vectors.
#  define PFFFT_VZERO() _mm_setzero_ps()
#  define PFFFT_VMUL(a,b) _mm_mul_ps(a,b)
#  define PFFFT_VADD(a,b) _mm_add_ps(a,b)
#  define PFFFT_VMADD(a,b,c) _mm_add_ps(_mm_mul_ps(a,b), c)
#  define PFFFT_VSUB(a,b) _mm_sub_ps(a,b)
#  define PFFFT_LD_PS1(p) _mm_set1_ps(p)
#  define PFFFT_INTERLEAVE2(in1, in2, out1, out2) { v4sf tmp__ = _mm_unpacklo_ps(in1, in2); out2 = _mm_unpackhi_ps(in1, in2); out1 = tmp__; }
#  define PFFFT_UNINTERLEAVE2(in1, in2, out1, out2) { v4sf tmp__ = _mm_shuffle_ps(in1, in2, _MM_SHUFFLE(2,0,2,0)); out2 = _mm_shuffle_ps(in1, in2, _MM_SHUFFLE(3,1,3,1)); out1 = tmp__; }
#  define PFFFT_VTRANSPOSE4(x0,x1,x2,x3) _MM_TRANSPOSE4_PS(x0,x1,x2,x3)
#  define PFFFT_VSWAPHL(a,b) _mm_shuffle_ps(b, a, _MM_SHUFFLE(3,2,1,0))
#  define PFFFT_VALIGNED(ptr) ((((long long)(ptr)) & 0xF) == 0)

/*
  ARM NEON support macros
*/
#elif !defined(PFFFT_SIMD_DISABLE) && (defined(__arm__) || defined(__aarch64__) || defined(__arm64__))
#  include <arm_neon.h>
typedef float32x4_t v4sf;
#  define PFFFT_SIMD_SZ 4
#  define PFFFT_VZERO() vdupq_n_f32(0)
#  define PFFFT_VMUL(a,b) vmulq_f32(a,b)
#  define PFFFT_VADD(a,b) vaddq_f32(a,b)
#  define PFFFT_VMADD(a,b,c) vmlaq_f32(c,a,b)
#  define PFFFT_VSUB(a,b) vsubq_f32(a,b)
#  define PFFFT_LD_PS1(p) vld1q_dup_f32(&(p))
#  define PFFFT_INTERLEAVE2(in1, in2, out1, out2) { float32x4x2_t tmp__ = vzipq_f32(in1,in2); out1=tmp__.val[0]; out2=tmp__.val[1]; }
#  define PFFFT_UNINTERLEAVE2(in1, in2, out1, out2) { float32x4x2_t tmp__ = vuzpq_f32(in1,in2); out1=tmp__.val[0]; out2=tmp__.val[1]; }
#  define PFFFT_VTRANSPOSE4(x0,x1,x2,x3) {                                    \
    float32x4x2_t t0_ = vzipq_f32(x0, x2);                              \
    float32x4x2_t t1_ = vzipq_f32(x1, x3);                              \
    float32x4x2_t u0_ = vzipq_f32(t0_.val[0], t1_.val[0]);              \
    float32x4x2_t u1_ = vzipq_f32(t0_.val[1], t1_.val[1]);              \
    x0 = u0_.val[0]; x1 = u0_.val[1]; x2 = u1_.val[0]; x3 = u1_.val[1]; \
  }
// marginally faster version
//#  define PFFFT_VTRANSPOSE4(x0,x1,x2,x3) { asm("vtrn.32 %q0, %q1;\n vtrn.32 %q2,%q3\n vswp %f0,%e2\n vswp %f1,%e3" : "+w"(x0), "+w"(x1), "+w"(x2), "+w"(x3)::); }
#  define PFFFT_VSWAPHL(a,b) vcombine_f32(vget_low_f32(b), vget_high_f32(a))
#  define PFFFT_VALIGNED(ptr) ((((long long)(ptr)) & 0x3) == 0)
#else
#  if !defined(PFFFT_SIMD_DISABLE)
#    warning "building with simd disabled !\n";
#    define PFFFT_SIMD_DISABLE // fallback to scalar code
#  endif
#endif

// fallback mode for situations where SSE/Altivec are not available, use scalar mode instead
#ifdef PFFFT_SIMD_DISABLE
typedef float v4sf;
#  define PFFFT_SIMD_SZ 1
#  define PFFFT_VZERO() 0.f
#  define PFFFT_VMUL(a,b) ((a)*(b))
#  define PFFFT_VADD(a,b) ((a)+(b))
#  define PFFFT_VMADD(a,b,c) ((a)*(b)+(c))
#  define PFFFT_VSUB(a,b) ((a)-(b))
#  define PFFFT_LD_PS1(p) (p)
#  define PFFFT_VALIGNED(ptr) ((((long long)(ptr)) & 0x3) == 0)
#endif

// shortcuts for complex multiplcations
#define PFFFT_VCPLXMUL(ar,ai,br,bi) { v4sf tmp; tmp=PFFFT_VMUL(ar,bi); ar=PFFFT_VMUL(ar,br); ar=PFFFT_VSUB(ar,PFFFT_VMUL(ai,bi)); ai=PFFFT_VMUL(ai,br); ai=PFFFT_VADD(ai,tmp); }
#define PFFFT_VCPLXMULCONJ(ar,ai,br,bi) { v4sf tmp; tmp=PFFFT_VMUL(ar,bi); ar=PFFFT_VMUL(ar,br); ar=PFFFT_VADD(ar,PFFFT_VMUL(ai,bi)); ai=PFFFT_VMUL(ai,br); ai=PFFFT_VSUB(ai,tmp); }
#ifndef PFFFT_SVMUL
// multiply a scalar with a vector
#define PFFFT_SVMUL(f,v) PFFFT_VMUL(PFFFT_LD_PS1(f),v)
#endif

#if !defined(PFFFT_SIMD_DISABLE)
typedef union v4sf_union {
  v4sf  v;
  float f[4];
} v4sf_union;

#include <string.h>

#define pffft_assertv4(v,f0,f1,f2,f3) PFFFT_ASSERT(v.f[0] == (f0) && v.f[1] == (f1) && v.f[2] == (f2) && v.f[3] == (f3))

/* detect bugs with the vector support macros */
void validate_pffft_simd(void) {
  float f[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
  v4sf_union a0, a1, a2, a3, t, u; 
  memcpy(a0.f, f, 4*sizeof(float));
  memcpy(a1.f, f+4, 4*sizeof(float));
  memcpy(a2.f, f+8, 4*sizeof(float));
  memcpy(a3.f, f+12, 4*sizeof(float));

  t = a0; u = a1; t.v = PFFFT_VZERO();
  printf("PFFFT_VZERO=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); pffft_assertv4(t, 0, 0, 0, 0);
  t.v = PFFFT_VADD(a1.v, a2.v);
  printf("PFFFT_VADD(4:7,8:11)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); pffft_assertv4(t, 12, 14, 16, 18);
  t.v = PFFFT_VMUL(a1.v, a2.v);
  printf("PFFFT_VMUL(4:7,8:11)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); pffft_assertv4(t, 32, 45, 60, 77);
  t.v = PFFFT_VMADD(a1.v, a2.v,a0.v);
  printf("PFFFT_VMADD(4:7,8:11,0:3)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]); pffft_assertv4(t, 32, 46, 62, 80);

  PFFFT_INTERLEAVE2(a1.v,a2.v,t.v,u.v);
  printf("PFFFT_INTERLEAVE2(4:7,8:11)=[%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3], u.f[0], u.f[1], u.f[2], u.f[3]);
  pffft_assertv4(t, 4, 8, 5, 9); pffft_assertv4(u, 6, 10, 7, 11);
  PFFFT_UNINTERLEAVE2(a1.v,a2.v,t.v,u.v);
  printf("PFFFT_UNINTERLEAVE2(4:7,8:11)=[%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3], u.f[0], u.f[1], u.f[2], u.f[3]);
  pffft_assertv4(t, 4, 6, 8, 10); pffft_assertv4(u, 5, 7, 9, 11);

  t.v=PFFFT_LD_PS1(f[15]);
  printf("PFFFT_LD_PS1(15)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]);
  pffft_assertv4(t, 15, 15, 15, 15);
  t.v = PFFFT_VSWAPHL(a1.v, a2.v);
  printf("PFFFT_VSWAPHL(4:7,8:11)=[%2g %2g %2g %2g]\n", t.f[0], t.f[1], t.f[2], t.f[3]);
  pffft_assertv4(t, 8, 9, 6, 7);
  PFFFT_VTRANSPOSE4(a0.v, a1.v, a2.v, a3.v);
  printf("PFFFT_VTRANSPOSE4(0:3,4:7,8:11,12:15)=[%2g %2g %2g %2g] [%2g %2g %2g %2g] [%2g %2g %2g %2g] [%2g %2g %2g %2g]\n", 
         a0.f[0], a0.f[1], a0.f[2], a0.f[3], a1.f[0], a1.f[1], a1.f[2], a1.f[3], 
         a2.f[0], a2.f[1], a2.f[2], a2.f[3], a3.f[0], a3.f[1], a3.f[2], a3.f[3]); 
  pffft_assertv4(a0, 0, 4, 8, 12); pffft_assertv4(a1, 1, 5, 9, 13); pffft_assertv4(a2, 2, 6, 10, 14); pffft_assertv4(a3, 3, 7, 11, 15);
}
#else
void validate_pffft_simd() {} // allow test_pffft.c to call this function even when simd is not available..
#endif //!PFFFT_SIMD_DISABLE

/* SSE and co like 16-bytes aligned pointers */
#define MALLOC_V4SF_ALIGNMENT 64 // with a 64-byte alignment, we are even aligned on L2 cache lines...
void *pffft_aligned_malloc(size_t nb_bytes) {
  void *p, *p0 = PFFFT_MALLOC(nb_bytes + MALLOC_V4SF_ALIGNMENT);
  if (!p0) return (void *) 0;
  p = (void *) (((size_t) p0 + MALLOC_V4SF_ALIGNMENT) & (~((size_t) (MALLOC_V4SF_ALIGNMENT-1))));
  *((void **) p - 1) = p0;
  return p;
}

void pffft_aligned_free(void *p) {
  if (p) PFFFT_FREE(*((void **) p - 1));
}

int pffft_simd_size(void) { return PFFFT_SIMD_SZ; }

/*
  passf2 and passb2 has been merged here, fsign = -1 for passf2, +1 for passb2
*/
static PFFFT_NEVER_INLINE(void) pffft_passf2_ps(int ido, int l1, const v4sf *cc, v4sf *ch, const float *wa1, float fsign) {
  int k, i;
  int l1ido = l1*ido;
  if (ido <= 2) {
    for (k=0; k < l1ido; k += ido, ch += ido, cc+= 2*ido) {
      ch[0]         = PFFFT_VADD(cc[0], cc[ido+0]);
      ch[l1ido]     = PFFFT_VSUB(cc[0], cc[ido+0]);
      ch[1]         = PFFFT_VADD(cc[1], cc[ido+1]);
      ch[l1ido + 1] = PFFFT_VSUB(cc[1], cc[ido+1]);
    }
  } else {
    for (k=0; k < l1ido; k += ido, ch += ido, cc += 2*ido) {
      for (i=0; i<ido-1; i+=2) {
        v4sf tr2 = PFFFT_VSUB(cc[i+0], cc[i+ido+0]);
        v4sf ti2 = PFFFT_VSUB(cc[i+1], cc[i+ido+1]);
        v4sf wr = PFFFT_LD_PS1(wa1[i]), wi = PFFFT_VMUL(PFFFT_LD_PS1(fsign), PFFFT_LD_PS1(wa1[i+1]));
        ch[i]   = PFFFT_VADD(cc[i+0], cc[i+ido+0]);
        ch[i+1] = PFFFT_VADD(cc[i+1], cc[i+ido+1]);
        PFFFT_VCPLXMUL(tr2, ti2, wr, wi);
        ch[i+l1ido]   = tr2;
        ch[i+l1ido+1] = ti2;
      }
    }
  }
}

/*
  passf3 and passb3 has been merged here, fsign = -1 for passf3, +1 for passb3
*/
static PFFFT_NEVER_INLINE(void) pffft_passf3_ps(int ido, int l1, const v4sf *cc, v4sf *ch,
                                    const float *wa1, const float *wa2, float fsign) {
  static const float taur = -0.5f;
  float taui = 0.866025403784439f*fsign;
  int i, k;
  v4sf tr2, ti2, cr2, ci2, cr3, ci3, dr2, di2, dr3, di3;
  int l1ido = l1*ido;
  float wr1, wi1, wr2, wi2;
  PFFFT_ASSERT(ido > 2);
  for (k=0; k< l1ido; k += ido, cc+= 3*ido, ch +=ido) {
    for (i=0; i<ido-1; i+=2) {
      tr2 = PFFFT_VADD(cc[i+ido], cc[i+2*ido]);
      cr2 = PFFFT_VADD(cc[i], PFFFT_SVMUL(taur,tr2));
      ch[i]    = PFFFT_VADD(cc[i], tr2);
      ti2 = PFFFT_VADD(cc[i+ido+1], cc[i+2*ido+1]);
      ci2 = PFFFT_VADD(cc[i    +1], PFFFT_SVMUL(taur,ti2));
      ch[i+1]  = PFFFT_VADD(cc[i+1], ti2);
      cr3 = PFFFT_SVMUL(taui, PFFFT_VSUB(cc[i+ido], cc[i+2*ido]));
      ci3 = PFFFT_SVMUL(taui, PFFFT_VSUB(cc[i+ido+1], cc[i+2*ido+1]));
      dr2 = PFFFT_VSUB(cr2, ci3);
      dr3 = PFFFT_VADD(cr2, ci3);
      di2 = PFFFT_VADD(ci2, cr3);
      di3 = PFFFT_VSUB(ci2, cr3);
      wr1=wa1[i]; wi1=fsign*wa1[i+1]; wr2=wa2[i]; wi2=fsign*wa2[i+1];
      PFFFT_VCPLXMUL(dr2, di2, PFFFT_LD_PS1(wr1), PFFFT_LD_PS1(wi1));
      ch[i+l1ido] = dr2; 
      ch[i+l1ido + 1] = di2;
      PFFFT_VCPLXMUL(dr3, di3, PFFFT_LD_PS1(wr2), PFFFT_LD_PS1(wi2));
      ch[i+2*l1ido] = dr3;
      ch[i+2*l1ido+1] = di3;
    }
  }
} /* passf3 */

static PFFFT_NEVER_INLINE(void) pffft_passf4_ps(int ido, int l1, const v4sf *cc, v4sf *ch,
                                    const float *wa1, const float *wa2, const float *wa3, float fsign) {
  /* isign == -1 for forward transform and +1 for backward transform */

  int i, k;
  v4sf ci2, ci3, ci4, cr2, cr3, cr4, ti1, ti2, ti3, ti4, tr1, tr2, tr3, tr4;
  int l1ido = l1*ido;
  if (ido == 2) {
    for (k=0; k < l1ido; k += ido, ch += ido, cc += 4*ido) {
      tr1 = PFFFT_VSUB(cc[0], cc[2*ido + 0]);
      tr2 = PFFFT_VADD(cc[0], cc[2*ido + 0]);
      ti1 = PFFFT_VSUB(cc[1], cc[2*ido + 1]);
      ti2 = PFFFT_VADD(cc[1], cc[2*ido + 1]);
      ti4 = PFFFT_VMUL(PFFFT_VSUB(cc[1*ido + 0], cc[3*ido + 0]), PFFFT_LD_PS1(fsign));
      tr4 = PFFFT_VMUL(PFFFT_VSUB(cc[3*ido + 1], cc[1*ido + 1]), PFFFT_LD_PS1(fsign));
      tr3 = PFFFT_VADD(cc[ido + 0], cc[3*ido + 0]);
      ti3 = PFFFT_VADD(cc[ido + 1], cc[3*ido + 1]);

      ch[0*l1ido + 0] = PFFFT_VADD(tr2, tr3);
      ch[0*l1ido + 1] = PFFFT_VADD(ti2, ti3);
      ch[1*l1ido + 0] = PFFFT_VADD(tr1, tr4);
      ch[1*l1ido + 1] = PFFFT_VADD(ti1, ti4);
      ch[2*l1ido + 0] = PFFFT_VSUB(tr2, tr3);
      ch[2*l1ido + 1] = PFFFT_VSUB(ti2, ti3);        
      ch[3*l1ido + 0] = PFFFT_VSUB(tr1, tr4);
      ch[3*l1ido + 1] = PFFFT_VSUB(ti1, ti4);
    }
  } else {
    for (k=0; k < l1ido; k += ido, ch+=ido, cc += 4*ido) {
      for (i=0; i<ido-1; i+=2) {
        float wr1, wi1, wr2, wi2, wr3, wi3;
        tr1 = PFFFT_VSUB(cc[i + 0], cc[i + 2*ido + 0]);
        tr2 = PFFFT_VADD(cc[i + 0], cc[i + 2*ido + 0]);
        ti1 = PFFFT_VSUB(cc[i + 1], cc[i + 2*ido + 1]);
        ti2 = PFFFT_VADD(cc[i + 1], cc[i + 2*ido + 1]);
        tr4 = PFFFT_VMUL(PFFFT_VSUB(cc[i + 3*ido + 1], cc[i + 1*ido + 1]), PFFFT_LD_PS1(fsign));
        ti4 = PFFFT_VMUL(PFFFT_VSUB(cc[i + 1*ido + 0], cc[i + 3*ido + 0]), PFFFT_LD_PS1(fsign));
        tr3 = PFFFT_VADD(cc[i + ido + 0], cc[i + 3*ido + 0]);
        ti3 = PFFFT_VADD(cc[i + ido + 1], cc[i + 3*ido + 1]);

        ch[i] = PFFFT_VADD(tr2, tr3);
        cr3    = PFFFT_VSUB(tr2, tr3);
        ch[i + 1] = PFFFT_VADD(ti2, ti3);
        ci3 = PFFFT_VSUB(ti2, ti3);

        cr2 = PFFFT_VADD(tr1, tr4);
        cr4 = PFFFT_VSUB(tr1, tr4);
        ci2 = PFFFT_VADD(ti1, ti4);
        ci4 = PFFFT_VSUB(ti1, ti4);
        wr1=wa1[i]; wi1=fsign*wa1[i+1];
        PFFFT_VCPLXMUL(cr2, ci2, PFFFT_LD_PS1(wr1), PFFFT_LD_PS1(wi1));
        wr2=wa2[i]; wi2=fsign*wa2[i+1];
        ch[i + l1ido] = cr2;
        ch[i + l1ido + 1] = ci2;

        PFFFT_VCPLXMUL(cr3, ci3, PFFFT_LD_PS1(wr2), PFFFT_LD_PS1(wi2));
        wr3=wa3[i]; wi3=fsign*wa3[i+1];
        ch[i + 2*l1ido] = cr3;
        ch[i + 2*l1ido + 1] = ci3;

        PFFFT_VCPLXMUL(cr4, ci4, PFFFT_LD_PS1(wr3), PFFFT_LD_PS1(wi3));
        ch[i + 3*l1ido] = cr4;
        ch[i + 3*l1ido + 1] = ci4;
      }
    }
  }
} /* passf4 */

/*
  passf5 and passb5 has been merged here, fsign = -1 for passf5, +1 for passb5
*/
static PFFFT_NEVER_INLINE(void) pffft_passf5_ps(int ido, int l1, const v4sf *cc, v4sf *ch,
                                    const float *wa1, const float *wa2, 
                                    const float *wa3, const float *wa4, float fsign) {  
  static const float tr11 = .309016994374947f;
  const float ti11 = .951056516295154f*fsign;
  static const float tr12 = -.809016994374947f;
  const float ti12 = .587785252292473f*fsign;

  /* Local variables */
  int i, k;
  v4sf ci2, ci3, ci4, ci5, di3, di4, di5, di2, cr2, cr3, cr5, cr4, ti2, ti3,
    ti4, ti5, dr3, dr4, dr5, dr2, tr2, tr3, tr4, tr5;

  float wr1, wi1, wr2, wi2, wr3, wi3, wr4, wi4;

#define pffft_cc_ref(a_1,a_2) cc[(a_2-1)*ido + a_1 + 1]
#define pffft_ch_ref(a_1,a_3) ch[(a_3-1)*l1*ido + a_1 + 1]

  PFFFT_ASSERT(ido > 2);
  for (k = 0; k < l1; ++k, cc += 5*ido, ch += ido) {
    for (i = 0; i < ido-1; i += 2) {
      ti5 = PFFFT_VSUB(pffft_cc_ref(i  , 2), pffft_cc_ref(i  , 5));
      ti2 = PFFFT_VADD(pffft_cc_ref(i  , 2), pffft_cc_ref(i  , 5));
      ti4 = PFFFT_VSUB(pffft_cc_ref(i  , 3), pffft_cc_ref(i  , 4));
      ti3 = PFFFT_VADD(pffft_cc_ref(i  , 3), pffft_cc_ref(i  , 4));
      tr5 = PFFFT_VSUB(pffft_cc_ref(i-1, 2), pffft_cc_ref(i-1, 5));
      tr2 = PFFFT_VADD(pffft_cc_ref(i-1, 2), pffft_cc_ref(i-1, 5));
      tr4 = PFFFT_VSUB(pffft_cc_ref(i-1, 3), pffft_cc_ref(i-1, 4));
      tr3 = PFFFT_VADD(pffft_cc_ref(i-1, 3), pffft_cc_ref(i-1, 4));
      pffft_ch_ref(i-1, 1) = PFFFT_VADD(pffft_cc_ref(i-1, 1), PFFFT_VADD(tr2, tr3));
      pffft_ch_ref(i  , 1) = PFFFT_VADD(pffft_cc_ref(i  , 1), PFFFT_VADD(ti2, ti3));
      cr2 = PFFFT_VADD(pffft_cc_ref(i-1, 1), PFFFT_VADD(PFFFT_SVMUL(tr11, tr2),PFFFT_SVMUL(tr12, tr3)));
      ci2 = PFFFT_VADD(pffft_cc_ref(i  , 1), PFFFT_VADD(PFFFT_SVMUL(tr11, ti2),PFFFT_SVMUL(tr12, ti3)));
      cr3 = PFFFT_VADD(pffft_cc_ref(i-1, 1), PFFFT_VADD(PFFFT_SVMUL(tr12, tr2),PFFFT_SVMUL(tr11, tr3)));
      ci3 = PFFFT_VADD(pffft_cc_ref(i  , 1), PFFFT_VADD(PFFFT_SVMUL(tr12, ti2),PFFFT_SVMUL(tr11, ti3)));
      cr5 = PFFFT_VADD(PFFFT_SVMUL(ti11, tr5), PFFFT_SVMUL(ti12, tr4));
      ci5 = PFFFT_VADD(PFFFT_SVMUL(ti11, ti5), PFFFT_SVMUL(ti12, ti4));
      cr4 = PFFFT_VSUB(PFFFT_SVMUL(ti12, tr5), PFFFT_SVMUL(ti11, tr4));
      ci4 = PFFFT_VSUB(PFFFT_SVMUL(ti12, ti5), PFFFT_SVMUL(ti11, ti4));
      dr3 = PFFFT_VSUB(cr3, ci4);
      dr4 = PFFFT_VADD(cr3, ci4);
      di3 = PFFFT_VADD(ci3, cr4);
      di4 = PFFFT_VSUB(ci3, cr4);
      dr5 = PFFFT_VADD(cr2, ci5);
      dr2 = PFFFT_VSUB(cr2, ci5);
      di5 = PFFFT_VSUB(ci2, cr5);
      di2 = PFFFT_VADD(ci2, cr5);
      wr1=wa1[i]; wi1=fsign*wa1[i+1]; wr2=wa2[i]; wi2=fsign*wa2[i+1];
      wr3=wa3[i]; wi3=fsign*wa3[i+1]; wr4=wa4[i]; wi4=fsign*wa4[i+1];
      PFFFT_VCPLXMUL(dr2, di2, PFFFT_LD_PS1(wr1), PFFFT_LD_PS1(wi1));
      pffft_ch_ref(i - 1, 2) = dr2;
      pffft_ch_ref(i, 2)     = di2;
      PFFFT_VCPLXMUL(dr3, di3, PFFFT_LD_PS1(wr2), PFFFT_LD_PS1(wi2));
      pffft_ch_ref(i - 1, 3) = dr3;
      pffft_ch_ref(i, 3)     = di3;
      PFFFT_VCPLXMUL(dr4, di4, PFFFT_LD_PS1(wr3), PFFFT_LD_PS1(wi3));
      pffft_ch_ref(i - 1, 4) = dr4;
      pffft_ch_ref(i, 4)     = di4;
      PFFFT_VCPLXMUL(dr5, di5, PFFFT_LD_PS1(wr4), PFFFT_LD_PS1(wi4));
      pffft_ch_ref(i - 1, 5) = dr5;
      pffft_ch_ref(i, 5)     = di5;
    }
  }
#undef pffft_ch_ref
#undef pffft_cc_ref
}

static PFFFT_NEVER_INLINE(void) pffft_radf2_ps(int ido, int l1, const v4sf * PFFFT_RESTRICT cc, v4sf * PFFFT_RESTRICT ch, const float *wa1) {
  static const float minus_one = -1.f;
  int i, k, l1ido = l1*ido;
  for (k=0; k < l1ido; k += ido) {
    v4sf a = cc[k], b = cc[k + l1ido];
    ch[2*k] = PFFFT_VADD(a, b);
    ch[2*(k+ido)-1] = PFFFT_VSUB(a, b);
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k=0; k < l1ido; k += ido) {
      for (i=2; i<ido; i+=2) {
        v4sf tr2 = cc[i - 1 + k + l1ido], ti2 = cc[i + k + l1ido];
        v4sf br = cc[i - 1 + k], bi = cc[i + k];
        PFFFT_VCPLXMULCONJ(tr2, ti2, PFFFT_LD_PS1(wa1[i - 2]), PFFFT_LD_PS1(wa1[i - 1])); 
        ch[i + 2*k] = PFFFT_VADD(bi, ti2);
        ch[2*(k+ido) - i] = PFFFT_VSUB(ti2, bi);
        ch[i - 1 + 2*k] = PFFFT_VADD(br, tr2);
        ch[2*(k+ido) - i -1] = PFFFT_VSUB(br, tr2);
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k=0; k < l1ido; k += ido) {
    ch[2*k + ido] = PFFFT_SVMUL(minus_one, cc[ido-1 + k + l1ido]);
    ch[2*k + ido-1] = cc[k + ido-1];
  }
} /* radf2 */


static PFFFT_NEVER_INLINE(void) pffft_radb2_ps(int ido, int l1, const v4sf *cc, v4sf *ch, const float *wa1) {
  static const float minus_two=-2;
  int i, k, l1ido = l1*ido;
  v4sf a,b,c,d, tr2, ti2;
  for (k=0; k < l1ido; k += ido) {
    a = cc[2*k]; b = cc[2*(k+ido) - 1];
    ch[k] = PFFFT_VADD(a, b);
    ch[k + l1ido] =PFFFT_VSUB(a, b);
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k = 0; k < l1ido; k += ido) {
      for (i = 2; i < ido; i += 2) {
        a = cc[i-1 + 2*k]; b = cc[2*(k + ido) - i - 1];
        c = cc[i+0 + 2*k]; d = cc[2*(k + ido) - i + 0];
        ch[i-1 + k] = PFFFT_VADD(a, b);
        tr2 = PFFFT_VSUB(a, b);
        ch[i+0 + k] = PFFFT_VSUB(c, d);
        ti2 = PFFFT_VADD(c, d);
        PFFFT_VCPLXMUL(tr2, ti2, PFFFT_LD_PS1(wa1[i - 2]), PFFFT_LD_PS1(wa1[i - 1]));
        ch[i-1 + k + l1ido] = tr2;
        ch[i+0 + k + l1ido] = ti2;
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k = 0; k < l1ido; k += ido) {
    a = cc[2*k + ido-1]; b = cc[2*k + ido];
    ch[k + ido-1] = PFFFT_VADD(a,a);
    ch[k + ido-1 + l1ido] = PFFFT_SVMUL(minus_two, b);
  }
} /* radb2 */

static void pffft_radf3_ps(int ido, int l1, const v4sf * PFFFT_RESTRICT cc, v4sf * PFFFT_RESTRICT ch,
                     const float *wa1, const float *wa2) {
  static const float taur = -0.5f;
  static const float taui = 0.866025403784439f;
  int i, k, ic;
  v4sf ci2, di2, di3, cr2, dr2, dr3, ti2, ti3, tr2, tr3, wr1, wi1, wr2, wi2;
  for (k=0; k<l1; k++) {
    cr2 = PFFFT_VADD(cc[(k + l1)*ido], cc[(k + 2*l1)*ido]);
    ch[3*k*ido] = PFFFT_VADD(cc[k*ido], cr2);
    ch[(3*k+2)*ido] = PFFFT_SVMUL(taui, PFFFT_VSUB(cc[(k + l1*2)*ido], cc[(k + l1)*ido]));
    ch[ido-1 + (3*k + 1)*ido] = PFFFT_VADD(cc[k*ido], PFFFT_SVMUL(taur, cr2));
  }
  if (ido == 1) return;
  for (k=0; k<l1; k++) {
    for (i=2; i<ido; i+=2) {
      ic = ido - i;
      wr1 = PFFFT_LD_PS1(wa1[i - 2]); wi1 = PFFFT_LD_PS1(wa1[i - 1]);
      dr2 = cc[i - 1 + (k + l1)*ido]; di2 = cc[i + (k + l1)*ido];
      PFFFT_VCPLXMULCONJ(dr2, di2, wr1, wi1);

      wr2 = PFFFT_LD_PS1(wa2[i - 2]); wi2 = PFFFT_LD_PS1(wa2[i - 1]);
      dr3 = cc[i - 1 + (k + l1*2)*ido]; di3 = cc[i + (k + l1*2)*ido];
      PFFFT_VCPLXMULCONJ(dr3, di3, wr2, wi2);
        
      cr2 = PFFFT_VADD(dr2, dr3);
      ci2 = PFFFT_VADD(di2, di3);
      ch[i - 1 + 3*k*ido] = PFFFT_VADD(cc[i - 1 + k*ido], cr2);
      ch[i + 3*k*ido] = PFFFT_VADD(cc[i + k*ido], ci2);
      tr2 = PFFFT_VADD(cc[i - 1 + k*ido], PFFFT_SVMUL(taur, cr2));
      ti2 = PFFFT_VADD(cc[i + k*ido], PFFFT_SVMUL(taur, ci2));
      tr3 = PFFFT_SVMUL(taui, PFFFT_VSUB(di2, di3));
      ti3 = PFFFT_SVMUL(taui, PFFFT_VSUB(dr3, dr2));
      ch[i - 1 + (3*k + 2)*ido] = PFFFT_VADD(tr2, tr3);
      ch[ic - 1 + (3*k + 1)*ido] = PFFFT_VSUB(tr2, tr3);
      ch[i + (3*k + 2)*ido] = PFFFT_VADD(ti2, ti3);
      ch[ic + (3*k + 1)*ido] = PFFFT_VSUB(ti3, ti2);
    }
  }
} /* radf3 */


static void pffft_radb3_ps(int ido, int l1, const v4sf *PFFFT_RESTRICT cc, v4sf *PFFFT_RESTRICT ch,
                     const float *wa1, const float *wa2)
{
  static const float taur = -0.5f;
  static const float taui = 0.866025403784439f;
  static const float taui_2 = 0.866025403784439f*2;
  int i, k, ic;
  v4sf ci2, ci3, di2, di3, cr2, cr3, dr2, dr3, ti2, tr2;
  for (k=0; k<l1; k++) {
    tr2 = cc[ido-1 + (3*k + 1)*ido]; tr2 = PFFFT_VADD(tr2,tr2);
    cr2 = PFFFT_VMADD(PFFFT_LD_PS1(taur), tr2, cc[3*k*ido]);
    ch[k*ido] = PFFFT_VADD(cc[3*k*ido], tr2);
    ci3 = PFFFT_SVMUL(taui_2, cc[(3*k + 2)*ido]);
    ch[(k + l1)*ido] = PFFFT_VSUB(cr2, ci3);
    ch[(k + 2*l1)*ido] = PFFFT_VADD(cr2, ci3);
  }
  if (ido == 1) return;
  for (k=0; k<l1; k++) {
    for (i=2; i<ido; i+=2) {
      ic = ido - i;
      tr2 = PFFFT_VADD(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido]);
      cr2 = PFFFT_VMADD(PFFFT_LD_PS1(taur), tr2, cc[i - 1 + 3*k*ido]);
      ch[i - 1 + k*ido] = PFFFT_VADD(cc[i - 1 + 3*k*ido], tr2);
      ti2 = PFFFT_VSUB(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido]);
      ci2 = PFFFT_VMADD(PFFFT_LD_PS1(taur), ti2, cc[i + 3*k*ido]);
      ch[i + k*ido] = PFFFT_VADD(cc[i + 3*k*ido], ti2);
      cr3 = PFFFT_SVMUL(taui, PFFFT_VSUB(cc[i - 1 + (3*k + 2)*ido], cc[ic - 1 + (3*k + 1)*ido]));
      ci3 = PFFFT_SVMUL(taui, PFFFT_VADD(cc[i + (3*k + 2)*ido], cc[ic + (3*k + 1)*ido]));
      dr2 = PFFFT_VSUB(cr2, ci3);
      dr3 = PFFFT_VADD(cr2, ci3);
      di2 = PFFFT_VADD(ci2, cr3);
      di3 = PFFFT_VSUB(ci2, cr3);
      PFFFT_VCPLXMUL(dr2, di2, PFFFT_LD_PS1(wa1[i-2]), PFFFT_LD_PS1(wa1[i-1]));
      ch[i - 1 + (k + l1)*ido] = dr2;
      ch[i + (k + l1)*ido] = di2;
      PFFFT_VCPLXMUL(dr3, di3, PFFFT_LD_PS1(wa2[i-2]), PFFFT_LD_PS1(wa2[i-1]));
      ch[i - 1 + (k + 2*l1)*ido] = dr3;
      ch[i + (k + 2*l1)*ido] = di3;
    }
  }
} /* radb3 */

static PFFFT_NEVER_INLINE(void) pffft_rradf4_ps(int ido, int l1, const v4sf *PFFFT_RESTRICT cc, v4sf * PFFFT_RESTRICT ch,
                                   const float * PFFFT_RESTRICT wa1, const float * PFFFT_RESTRICT wa2, const float * PFFFT_RESTRICT wa3)
{
  static const float minus_hsqt2 = (float)-0.7071067811865475;
  int i, k, l1ido = l1*ido;
  {
    const v4sf *PFFFT_RESTRICT cc_ = cc, * PFFFT_RESTRICT cc_end = cc + l1ido; 
    v4sf * PFFFT_RESTRICT ch_ = ch;
    while (cc < cc_end) {
      // this loop represents between 25% and 40% of total pffft_rradf4_ps cost !
      v4sf a0 = cc[0], a1 = cc[l1ido];
      v4sf a2 = cc[2*l1ido], a3 = cc[3*l1ido];
      v4sf tr1 = PFFFT_VADD(a1, a3);
      v4sf tr2 = PFFFT_VADD(a0, a2);
      ch[2*ido-1] = PFFFT_VSUB(a0, a2);
      ch[2*ido  ] = PFFFT_VSUB(a3, a1);
      ch[0      ] = PFFFT_VADD(tr1, tr2);
      ch[4*ido-1] = PFFFT_VSUB(tr2, tr1);
      cc += ido; ch += 4*ido;
    }
    cc = cc_; ch = ch_;
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k = 0; k < l1ido; k += ido) {
      const v4sf * PFFFT_RESTRICT pc = (v4sf*)(cc + 1 + k);
      for (i=2; i<ido; i += 2, pc += 2) {
        int ic = ido - i;
        v4sf wr, wi, cr2, ci2, cr3, ci3, cr4, ci4;
        v4sf tr1, ti1, tr2, ti2, tr3, ti3, tr4, ti4;

        cr2 = pc[1*l1ido+0];
        ci2 = pc[1*l1ido+1];
        wr=PFFFT_LD_PS1(wa1[i - 2]);
        wi=PFFFT_LD_PS1(wa1[i - 1]);
        PFFFT_VCPLXMULCONJ(cr2,ci2,wr,wi);

        cr3 = pc[2*l1ido+0];
        ci3 = pc[2*l1ido+1];
        wr = PFFFT_LD_PS1(wa2[i-2]); 
        wi = PFFFT_LD_PS1(wa2[i-1]);
        PFFFT_VCPLXMULCONJ(cr3, ci3, wr, wi);

        cr4 = pc[3*l1ido];
        ci4 = pc[3*l1ido+1];
        wr = PFFFT_LD_PS1(wa3[i-2]); 
        wi = PFFFT_LD_PS1(wa3[i-1]);
        PFFFT_VCPLXMULCONJ(cr4, ci4, wr, wi);

        /* at this point, on SSE, five of "cr2 cr3 cr4 ci2 ci3 ci4" should be loaded in registers */

        tr1 = PFFFT_VADD(cr2,cr4);
        tr4 = PFFFT_VSUB(cr4,cr2); 
        tr2 = PFFFT_VADD(pc[0],cr3);
        tr3 = PFFFT_VSUB(pc[0],cr3);
        ch[i - 1 + 4*k] = PFFFT_VADD(tr1,tr2);
        ch[ic - 1 + 4*k + 3*ido] = PFFFT_VSUB(tr2,tr1); // at this point tr1 and tr2 can be disposed
        ti1 = PFFFT_VADD(ci2,ci4);
        ti4 = PFFFT_VSUB(ci2,ci4);
        ch[i - 1 + 4*k + 2*ido] = PFFFT_VADD(ti4,tr3);
        ch[ic - 1 + 4*k + 1*ido] = PFFFT_VSUB(tr3,ti4); // dispose tr3, ti4
        ti2 = PFFFT_VADD(pc[1],ci3);
        ti3 = PFFFT_VSUB(pc[1],ci3);
        ch[i + 4*k] = PFFFT_VADD(ti1, ti2);
        ch[ic + 4*k + 3*ido] = PFFFT_VSUB(ti1, ti2);
        ch[i + 4*k + 2*ido] = PFFFT_VADD(tr4, ti3);
        ch[ic + 4*k + 1*ido] = PFFFT_VSUB(tr4, ti3);
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k=0; k<l1ido; k += ido) {
    v4sf a = cc[ido-1 + k + l1ido], b = cc[ido-1 + k + 3*l1ido];
    v4sf c = cc[ido-1 + k], d = cc[ido-1 + k + 2*l1ido];
    v4sf ti1 = PFFFT_SVMUL(minus_hsqt2, PFFFT_VADD(a, b));
    v4sf tr1 = PFFFT_SVMUL(minus_hsqt2, PFFFT_VSUB(b, a));
    ch[ido-1 + 4*k] = PFFFT_VADD(tr1, c);
    ch[ido-1 + 4*k + 2*ido] = PFFFT_VSUB(c, tr1);
    ch[4*k + 1*ido] = PFFFT_VSUB(ti1, d); 
    ch[4*k + 3*ido] = PFFFT_VADD(ti1, d); 
  }
} /* radf4 */


static PFFFT_NEVER_INLINE(void) pffft_radb4_ps(int ido, int l1, const v4sf * PFFFT_RESTRICT cc, v4sf * PFFFT_RESTRICT ch,
                                   const float * PFFFT_RESTRICT wa1, const float * PFFFT_RESTRICT wa2, const float *PFFFT_RESTRICT wa3)
{
  static const float minus_sqrt2 = (float)-1.414213562373095;
  static const float two = 2.f;
  int i, k, l1ido = l1*ido;
  v4sf ci2, ci3, ci4, cr2, cr3, cr4, ti1, ti2, ti3, ti4, tr1, tr2, tr3, tr4;
  {
    const v4sf *PFFFT_RESTRICT cc_ = cc, * PFFFT_RESTRICT ch_end = ch + l1ido; 
    v4sf *ch_ = ch;
    while (ch < ch_end) {
      v4sf a = cc[0], b = cc[4*ido-1];
      v4sf c = cc[2*ido], d = cc[2*ido-1];
      tr3 = PFFFT_SVMUL(two,d);
      tr2 = PFFFT_VADD(a,b);
      tr1 = PFFFT_VSUB(a,b);
      tr4 = PFFFT_SVMUL(two,c);
      ch[0*l1ido] = PFFFT_VADD(tr2, tr3);
      ch[2*l1ido] = PFFFT_VSUB(tr2, tr3);
      ch[1*l1ido] = PFFFT_VSUB(tr1, tr4);
      ch[3*l1ido] = PFFFT_VADD(tr1, tr4);
      
      cc += 4*ido; ch += ido;
    }
    cc = cc_; ch = ch_;
  }
  if (ido < 2) return;
  if (ido != 2) {
    for (k = 0; k < l1ido; k += ido) {
      const v4sf * PFFFT_RESTRICT pc = (v4sf*)(cc - 1 + 4*k);
      v4sf * PFFFT_RESTRICT ph = (v4sf*)(ch + k + 1);
      for (i = 2; i < ido; i += 2) {

        tr1 = PFFFT_VSUB(pc[i], pc[4*ido - i]);
        tr2 = PFFFT_VADD(pc[i], pc[4*ido - i]);
        ti4 = PFFFT_VSUB(pc[2*ido + i], pc[2*ido - i]);
        tr3 = PFFFT_VADD(pc[2*ido + i], pc[2*ido - i]);
        ph[0] = PFFFT_VADD(tr2, tr3);
        cr3 = PFFFT_VSUB(tr2, tr3);

        ti3 = PFFFT_VSUB(pc[2*ido + i + 1], pc[2*ido - i + 1]);
        tr4 = PFFFT_VADD(pc[2*ido + i + 1], pc[2*ido - i + 1]);
        cr2 = PFFFT_VSUB(tr1, tr4);
        cr4 = PFFFT_VADD(tr1, tr4);

        ti1 = PFFFT_VADD(pc[i + 1], pc[4*ido - i + 1]);
        ti2 = PFFFT_VSUB(pc[i + 1], pc[4*ido - i + 1]);

        ph[1] = PFFFT_VADD(ti2, ti3); ph += l1ido;
        ci3 = PFFFT_VSUB(ti2, ti3);
        ci2 = PFFFT_VADD(ti1, ti4);
        ci4 = PFFFT_VSUB(ti1, ti4);
        PFFFT_VCPLXMUL(cr2, ci2, PFFFT_LD_PS1(wa1[i-2]), PFFFT_LD_PS1(wa1[i-1]));
        ph[0] = cr2;
        ph[1] = ci2; ph += l1ido;
        PFFFT_VCPLXMUL(cr3, ci3, PFFFT_LD_PS1(wa2[i-2]), PFFFT_LD_PS1(wa2[i-1]));
        ph[0] = cr3;
        ph[1] = ci3; ph += l1ido;
        PFFFT_VCPLXMUL(cr4, ci4, PFFFT_LD_PS1(wa3[i-2]), PFFFT_LD_PS1(wa3[i-1]));
        ph[0] = cr4;
        ph[1] = ci4; ph = ph - 3*l1ido + 2;
      }
    }
    if (ido % 2 == 1) return;
  }
  for (k=0; k < l1ido; k+=ido) {
    int i0 = 4*k + ido;
    v4sf c = cc[i0-1], d = cc[i0 + 2*ido-1];
    v4sf a = cc[i0+0], b = cc[i0 + 2*ido+0];
    tr1 = PFFFT_VSUB(c,d);
    tr2 = PFFFT_VADD(c,d);
    ti1 = PFFFT_VADD(b,a);
    ti2 = PFFFT_VSUB(b,a);
    ch[ido-1 + k + 0*l1ido] = PFFFT_VADD(tr2,tr2);
    ch[ido-1 + k + 1*l1ido] = PFFFT_SVMUL(minus_sqrt2, PFFFT_VSUB(ti1, tr1));
    ch[ido-1 + k + 2*l1ido] = PFFFT_VADD(ti2, ti2);
    ch[ido-1 + k + 3*l1ido] = PFFFT_SVMUL(minus_sqrt2, PFFFT_VADD(ti1, tr1));
  }
} /* radb4 */

static void pffft_radf5_ps(int ido, int l1, const v4sf * PFFFT_RESTRICT cc, v4sf * PFFFT_RESTRICT ch, 
                     const float *wa1, const float *wa2, const float *wa3, const float *wa4)
{
  static const float tr11 = .309016994374947f;
  static const float ti11 = .951056516295154f;
  static const float tr12 = -.809016994374947f;
  static const float ti12 = .587785252292473f;

  /* System generated locals */
  int cc_offset, ch_offset;

  /* Local variables */
  int i, k, ic;
  v4sf ci2, di2, ci4, ci5, di3, di4, di5, ci3, cr2, cr3, dr2, dr3, dr4, dr5,
    cr5, cr4, ti2, ti3, ti5, ti4, tr2, tr3, tr4, tr5;
  int idp2;


#define pffft_cc_ref(a_1,a_2,a_3) cc[((a_3)*l1 + (a_2))*ido + a_1]
#define pffft_ch_ref(a_1,a_2,a_3) ch[((a_3)*5 + (a_2))*ido + a_1]

  /* Parameter adjustments */
  ch_offset = 1 + ido * 6;
  ch -= ch_offset;
  cc_offset = 1 + ido * (1 + l1);
  cc -= cc_offset;

  /* Function Body */
  for (k = 1; k <= l1; ++k) {
    cr2 = PFFFT_VADD(pffft_cc_ref(1, k, 5), pffft_cc_ref(1, k, 2));
    ci5 = PFFFT_VSUB(pffft_cc_ref(1, k, 5), pffft_cc_ref(1, k, 2));
    cr3 = PFFFT_VADD(pffft_cc_ref(1, k, 4), pffft_cc_ref(1, k, 3));
    ci4 = PFFFT_VSUB(pffft_cc_ref(1, k, 4), pffft_cc_ref(1, k, 3));
    pffft_ch_ref(1, 1, k) = PFFFT_VADD(pffft_cc_ref(1, k, 1), PFFFT_VADD(cr2, cr3));
    pffft_ch_ref(ido, 2, k) = PFFFT_VADD(pffft_cc_ref(1, k, 1), PFFFT_VADD(PFFFT_SVMUL(tr11, cr2), PFFFT_SVMUL(tr12, cr3)));
    pffft_ch_ref(1, 3, k) = PFFFT_VADD(PFFFT_SVMUL(ti11, ci5), PFFFT_SVMUL(ti12, ci4));
    pffft_ch_ref(ido, 4, k) = PFFFT_VADD(pffft_cc_ref(1, k, 1), PFFFT_VADD(PFFFT_SVMUL(tr12, cr2), PFFFT_SVMUL(tr11, cr3)));
    pffft_ch_ref(1, 5, k) = PFFFT_VSUB(PFFFT_SVMUL(ti12, ci5), PFFFT_SVMUL(ti11, ci4));
    //printf("pffft: radf5, k=%d pffft_ch_ref=%f, ci4=%f\n", k, pffft_ch_ref(1, 5, k), ci4);
  }
  if (ido == 1) {
    return;
  }
  idp2 = ido + 2;
  for (k = 1; k <= l1; ++k) {
    for (i = 3; i <= ido; i += 2) {
      ic = idp2 - i;
      dr2 = PFFFT_LD_PS1(wa1[i-3]); di2 = PFFFT_LD_PS1(wa1[i-2]);
      dr3 = PFFFT_LD_PS1(wa2[i-3]); di3 = PFFFT_LD_PS1(wa2[i-2]);
      dr4 = PFFFT_LD_PS1(wa3[i-3]); di4 = PFFFT_LD_PS1(wa3[i-2]);
      dr5 = PFFFT_LD_PS1(wa4[i-3]); di5 = PFFFT_LD_PS1(wa4[i-2]);
      PFFFT_VCPLXMULCONJ(dr2, di2, pffft_cc_ref(i-1, k, 2), pffft_cc_ref(i, k, 2));
      PFFFT_VCPLXMULCONJ(dr3, di3, pffft_cc_ref(i-1, k, 3), pffft_cc_ref(i, k, 3));
      PFFFT_VCPLXMULCONJ(dr4, di4, pffft_cc_ref(i-1, k, 4), pffft_cc_ref(i, k, 4));
      PFFFT_VCPLXMULCONJ(dr5, di5, pffft_cc_ref(i-1, k, 5), pffft_cc_ref(i, k, 5));
      cr2 = PFFFT_VADD(dr2, dr5);
      ci5 = PFFFT_VSUB(dr5, dr2);
      cr5 = PFFFT_VSUB(di2, di5);
      ci2 = PFFFT_VADD(di2, di5);
      cr3 = PFFFT_VADD(dr3, dr4);
      ci4 = PFFFT_VSUB(dr4, dr3);
      cr4 = PFFFT_VSUB(di3, di4);
      ci3 = PFFFT_VADD(di3, di4);
      pffft_ch_ref(i - 1, 1, k) = PFFFT_VADD(pffft_cc_ref(i - 1, k, 1), PFFFT_VADD(cr2, cr3));
      pffft_ch_ref(i, 1, k) = PFFFT_VSUB(pffft_cc_ref(i, k, 1), PFFFT_VADD(ci2, ci3));//
      tr2 = PFFFT_VADD(pffft_cc_ref(i - 1, k, 1), PFFFT_VADD(PFFFT_SVMUL(tr11, cr2), PFFFT_SVMUL(tr12, cr3)));
      ti2 = PFFFT_VSUB(pffft_cc_ref(i, k, 1), PFFFT_VADD(PFFFT_SVMUL(tr11, ci2), PFFFT_SVMUL(tr12, ci3)));//
      tr3 = PFFFT_VADD(pffft_cc_ref(i - 1, k, 1), PFFFT_VADD(PFFFT_SVMUL(tr12, cr2), PFFFT_SVMUL(tr11, cr3)));
      ti3 = PFFFT_VSUB(pffft_cc_ref(i, k, 1), PFFFT_VADD(PFFFT_SVMUL(tr12, ci2), PFFFT_SVMUL(tr11, ci3)));//
      tr5 = PFFFT_VADD(PFFFT_SVMUL(ti11, cr5), PFFFT_SVMUL(ti12, cr4));
      ti5 = PFFFT_VADD(PFFFT_SVMUL(ti11, ci5), PFFFT_SVMUL(ti12, ci4));
      tr4 = PFFFT_VSUB(PFFFT_SVMUL(ti12, cr5), PFFFT_SVMUL(ti11, cr4));
      ti4 = PFFFT_VSUB(PFFFT_SVMUL(ti12, ci5), PFFFT_SVMUL(ti11, ci4));
      pffft_ch_ref(i - 1, 3, k) = PFFFT_VSUB(tr2, tr5);
      pffft_ch_ref(ic - 1, 2, k) = PFFFT_VADD(tr2, tr5);
      pffft_ch_ref(i, 3, k) = PFFFT_VADD(ti2, ti5);
      pffft_ch_ref(ic, 2, k) = PFFFT_VSUB(ti5, ti2);
      pffft_ch_ref(i - 1, 5, k) = PFFFT_VSUB(tr3, tr4);
      pffft_ch_ref(ic - 1, 4, k) = PFFFT_VADD(tr3, tr4);
      pffft_ch_ref(i, 5, k) = PFFFT_VADD(ti3, ti4);
      pffft_ch_ref(ic, 4, k) = PFFFT_VSUB(ti4, ti3);
    }
  }
#undef pffft_cc_ref
#undef pffft_ch_ref
} /* radf5 */

static void pffft_radb5_ps(int ido, int l1, const v4sf *PFFFT_RESTRICT cc, v4sf *PFFFT_RESTRICT ch, 
                  const float *wa1, const float *wa2, const float *wa3, const float *wa4)
{
  static const float tr11 = .309016994374947f;
  static const float ti11 = .951056516295154f;
  static const float tr12 = -.809016994374947f;
  static const float ti12 = .587785252292473f;

  int cc_offset, ch_offset;

  /* Local variables */
  int i, k, ic;
  v4sf ci2, ci3, ci4, ci5, di3, di4, di5, di2, cr2, cr3, cr5, cr4, ti2, ti3,
    ti4, ti5, dr3, dr4, dr5, dr2, tr2, tr3, tr4, tr5;
  int idp2;

#define pffft_cc_ref(a_1,a_2,a_3) cc[((a_3)*5 + (a_2))*ido + a_1]
#define pffft_ch_ref(a_1,a_2,a_3) ch[((a_3)*l1 + (a_2))*ido + a_1]

  /* Parameter adjustments */
  ch_offset = 1 + ido * (1 + l1);
  ch -= ch_offset;
  cc_offset = 1 + ido * 6;
  cc -= cc_offset;

  /* Function Body */
  for (k = 1; k <= l1; ++k) {
    ti5 = PFFFT_VADD(pffft_cc_ref(1, 3, k), pffft_cc_ref(1, 3, k));
    ti4 = PFFFT_VADD(pffft_cc_ref(1, 5, k), pffft_cc_ref(1, 5, k));
    tr2 = PFFFT_VADD(pffft_cc_ref(ido, 2, k), pffft_cc_ref(ido, 2, k));
    tr3 = PFFFT_VADD(pffft_cc_ref(ido, 4, k), pffft_cc_ref(ido, 4, k));
    pffft_ch_ref(1, k, 1) = PFFFT_VADD(pffft_cc_ref(1, 1, k), PFFFT_VADD(tr2, tr3));
    cr2 = PFFFT_VADD(pffft_cc_ref(1, 1, k), PFFFT_VADD(PFFFT_SVMUL(tr11, tr2), PFFFT_SVMUL(tr12, tr3)));
    cr3 = PFFFT_VADD(pffft_cc_ref(1, 1, k), PFFFT_VADD(PFFFT_SVMUL(tr12, tr2), PFFFT_SVMUL(tr11, tr3)));
    ci5 = PFFFT_VADD(PFFFT_SVMUL(ti11, ti5), PFFFT_SVMUL(ti12, ti4));
    ci4 = PFFFT_VSUB(PFFFT_SVMUL(ti12, ti5), PFFFT_SVMUL(ti11, ti4));
    pffft_ch_ref(1, k, 2) = PFFFT_VSUB(cr2, ci5);
    pffft_ch_ref(1, k, 3) = PFFFT_VSUB(cr3, ci4);
    pffft_ch_ref(1, k, 4) = PFFFT_VADD(cr3, ci4);
    pffft_ch_ref(1, k, 5) = PFFFT_VADD(cr2, ci5);
  }
  if (ido == 1) {
    return;
  }
  idp2 = ido + 2;
  for (k = 1; k <= l1; ++k) {
    for (i = 3; i <= ido; i += 2) {
      ic = idp2 - i;
      ti5 = PFFFT_VADD(pffft_cc_ref(i  , 3, k), pffft_cc_ref(ic  , 2, k));
      ti2 = PFFFT_VSUB(pffft_cc_ref(i  , 3, k), pffft_cc_ref(ic  , 2, k));
      ti4 = PFFFT_VADD(pffft_cc_ref(i  , 5, k), pffft_cc_ref(ic  , 4, k));
      ti3 = PFFFT_VSUB(pffft_cc_ref(i  , 5, k), pffft_cc_ref(ic  , 4, k));
      tr5 = PFFFT_VSUB(pffft_cc_ref(i-1, 3, k), pffft_cc_ref(ic-1, 2, k));
      tr2 = PFFFT_VADD(pffft_cc_ref(i-1, 3, k), pffft_cc_ref(ic-1, 2, k));
      tr4 = PFFFT_VSUB(pffft_cc_ref(i-1, 5, k), pffft_cc_ref(ic-1, 4, k));
      tr3 = PFFFT_VADD(pffft_cc_ref(i-1, 5, k), pffft_cc_ref(ic-1, 4, k));
      pffft_ch_ref(i - 1, k, 1) = PFFFT_VADD(pffft_cc_ref(i-1, 1, k), PFFFT_VADD(tr2, tr3));
      pffft_ch_ref(i, k, 1) = PFFFT_VADD(pffft_cc_ref(i, 1, k), PFFFT_VADD(ti2, ti3));
      cr2 = PFFFT_VADD(pffft_cc_ref(i-1, 1, k), PFFFT_VADD(PFFFT_SVMUL(tr11, tr2), PFFFT_SVMUL(tr12, tr3)));
      ci2 = PFFFT_VADD(pffft_cc_ref(i  , 1, k), PFFFT_VADD(PFFFT_SVMUL(tr11, ti2), PFFFT_SVMUL(tr12, ti3)));
      cr3 = PFFFT_VADD(pffft_cc_ref(i-1, 1, k), PFFFT_VADD(PFFFT_SVMUL(tr12, tr2), PFFFT_SVMUL(tr11, tr3)));
      ci3 = PFFFT_VADD(pffft_cc_ref(i  , 1, k), PFFFT_VADD(PFFFT_SVMUL(tr12, ti2), PFFFT_SVMUL(tr11, ti3)));
      cr5 = PFFFT_VADD(PFFFT_SVMUL(ti11, tr5), PFFFT_SVMUL(ti12, tr4));
      ci5 = PFFFT_VADD(PFFFT_SVMUL(ti11, ti5), PFFFT_SVMUL(ti12, ti4));
      cr4 = PFFFT_VSUB(PFFFT_SVMUL(ti12, tr5), PFFFT_SVMUL(ti11, tr4));
      ci4 = PFFFT_VSUB(PFFFT_SVMUL(ti12, ti5), PFFFT_SVMUL(ti11, ti4));
      dr3 = PFFFT_VSUB(cr3, ci4);
      dr4 = PFFFT_VADD(cr3, ci4);
      di3 = PFFFT_VADD(ci3, cr4);
      di4 = PFFFT_VSUB(ci3, cr4);
      dr5 = PFFFT_VADD(cr2, ci5);
      dr2 = PFFFT_VSUB(cr2, ci5);
      di5 = PFFFT_VSUB(ci2, cr5);
      di2 = PFFFT_VADD(ci2, cr5);
      PFFFT_VCPLXMUL(dr2, di2, PFFFT_LD_PS1(wa1[i-3]), PFFFT_LD_PS1(wa1[i-2]));
      PFFFT_VCPLXMUL(dr3, di3, PFFFT_LD_PS1(wa2[i-3]), PFFFT_LD_PS1(wa2[i-2]));
      PFFFT_VCPLXMUL(dr4, di4, PFFFT_LD_PS1(wa3[i-3]), PFFFT_LD_PS1(wa3[i-2]));
      PFFFT_VCPLXMUL(dr5, di5, PFFFT_LD_PS1(wa4[i-3]), PFFFT_LD_PS1(wa4[i-2]));

      pffft_ch_ref(i-1, k, 2) = dr2; pffft_ch_ref(i, k, 2) = di2;
      pffft_ch_ref(i-1, k, 3) = dr3; pffft_ch_ref(i, k, 3) = di3;
      pffft_ch_ref(i-1, k, 4) = dr4; pffft_ch_ref(i, k, 4) = di4;
      pffft_ch_ref(i-1, k, 5) = dr5; pffft_ch_ref(i, k, 5) = di5;
    }
  }
#undef pffft_cc_ref
#undef pffft_ch_ref
} /* radb5 */

static PFFFT_NEVER_INLINE(v4sf *) pffft_rfftf1_ps(int n, const v4sf *input_readonly, v4sf *work1, v4sf *work2, 
                                      const float *wa, const int *ifac) {  
  v4sf *in  = (v4sf*)input_readonly;
  v4sf *out = (in == work2 ? work1 : work2);
  int nf = ifac[1], k1;
  int l2 = n;
  int iw = n-1;
  PFFFT_ASSERT(in != out && work1 != work2);
  for (k1 = 1; k1 <= nf; ++k1) {
    int kh = nf - k1;
    int ip = ifac[kh + 2];
    int l1 = l2 / ip;
    int ido = n / l2;
    iw -= (ip - 1)*ido;
    switch (ip) {
      case 5: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        int ix4 = ix3 + ido;
        pffft_radf5_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4]);
      } break;
      case 4: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        pffft_rradf4_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3]);
      } break;
      case 3: {
        int ix2 = iw + ido;
        pffft_radf3_ps(ido, l1, in, out, &wa[iw], &wa[ix2]);
      } break;
      case 2:
        pffft_radf2_ps(ido, l1, in, out, &wa[iw]);
        break;
      default:
        PFFFT_ASSERT(0);
        break;
    }
    l2 = l1;
    if (out == work2) {
      out = work1; in = work2;
    } else {
      out = work2; in = work1;
    }
  }
  return in; /* this is in fact the output .. */
} /* rfftf1 */

static PFFFT_NEVER_INLINE(v4sf *) pffft_rfftb1_ps(int n, const v4sf *input_readonly, v4sf *work1, v4sf *work2, 
                                      const float *wa, const int *ifac) {  
  v4sf *in  = (v4sf*)input_readonly;
  v4sf *out = (in == work2 ? work1 : work2);
  int nf = ifac[1], k1;
  int l1 = 1;
  int iw = 0;
  PFFFT_ASSERT(in != out);
  for (k1=1; k1<=nf; k1++) {
    int ip = ifac[k1 + 1];
    int l2 = ip*l1;
    int ido = n / l2;
    switch (ip) {
      case 5: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        int ix4 = ix3 + ido;
        pffft_radb5_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4]);
      } break;
      case 4: {
        int ix2 = iw + ido;
        int ix3 = ix2 + ido;
        pffft_radb4_ps(ido, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3]);
      } break;
      case 3: {
        int ix2 = iw + ido;
        pffft_radb3_ps(ido, l1, in, out, &wa[iw], &wa[ix2]);
      } break;
      case 2:
        pffft_radb2_ps(ido, l1, in, out, &wa[iw]);
        break;
      default:
        PFFFT_ASSERT(0);
        break;
    }
    l1 = l2;
    iw += (ip - 1)*ido;

    if (out == work2) {
      out = work1; in = work2;
    } else {
      out = work2; in = work1;
    }
  }
  return in; /* this is in fact the output .. */
}

static int pffft_decompose(int n, int *ifac, const int *ntryh) {
  int nl = n, nf = 0, i, j = 0;
  for (j=0; ntryh[j]; ++j) {
    int ntry = ntryh[j];
    while (nl != 1) {
      int nq = nl / ntry;
      int nr = nl - ntry * nq;
      if (nr == 0) {
        ifac[2+nf++] = ntry;
        nl = nq;
        if (ntry == 2 && nf != 1) {
          for (i = 2; i <= nf; ++i) {
            int ib = nf - i + 2;
            ifac[ib + 1] = ifac[ib];
          }
          ifac[2] = 2;
        }
      } else break;
    }
  }
  ifac[0] = n;
  ifac[1] = nf;  
  return nf;
}



static void pffft_rffti1_ps(int n, float *wa, int *ifac)
{
  static const int ntryh[] = { 4,2,3,5,0 };
  int k1, j, ii;

  int nf = pffft_decompose(n,ifac,ntryh);
  float argh = (2*M_PI) / n;
  int is = 0;
  int nfm1 = nf - 1;
  int l1 = 1;
  for (k1 = 1; k1 <= nfm1; k1++) {
    int ip = ifac[k1 + 1];
    int ld = 0;
    int l2 = l1*ip;
    int ido = n / l2;
    int ipm = ip - 1;
    for (j = 1; j <= ipm; ++j) {
      float argld;
      int i = is, fi=0;
      ld += l1;
      argld = ld*argh;
      for (ii = 3; ii <= ido; ii += 2) {
        i += 2;
        fi += 1;
        wa[i - 2] = cos(fi*argld);
        wa[i - 1] = sin(fi*argld);
      }
      is += ido;
    }
    l1 = l2;
  }
} /* rffti1 */

void pffft_cffti1_ps(int n, float *wa, int *ifac)
{
  static const int ntryh[] = { 5,3,4,2,0 };
  int k1, j, ii;

  int nf = pffft_decompose(n,ifac,ntryh);
  float argh = (2*M_PI)/(float)n;
  int i = 1;
  int l1 = 1;
  for (k1=1; k1<=nf; k1++) {
    int ip = ifac[k1+1];
    int ld = 0;
    int l2 = l1*ip;
    int ido = n / l2;
    int idot = ido + ido + 2;
    int ipm = ip - 1;
    for (j=1; j<=ipm; j++) {
      float argld;
      int i1 = i, fi = 0;
      wa[i-1] = 1;
      wa[i] = 0;
      ld += l1;
      argld = ld*argh;
      for (ii = 4; ii <= idot; ii += 2) {
        i += 2;
        fi += 1;
        wa[i-1] = cos(fi*argld);
        wa[i] = sin(fi*argld);
      }
      if (ip > 5) {
        wa[i1-1] = wa[i-1];
        wa[i1] = wa[i];
      }
    }
    l1 = l2;
  }
} /* cffti1 */


v4sf *pffft_cfftf1_ps(int n, const v4sf *input_readonly, v4sf *work1, v4sf *work2, const float *wa, const int *ifac, int isign) {
  v4sf *in  = (v4sf*)input_readonly;
  v4sf *out = (in == work2 ? work1 : work2); 
  int nf = ifac[1], k1;
  int l1 = 1;
  int iw = 0;
  PFFFT_ASSERT(in != out && work1 != work2);
  for (k1=2; k1<=nf+1; k1++) {
    int ip = ifac[k1];
    int l2 = ip*l1;
    int ido = n / l2;
    int idot = ido + ido;
    switch (ip) {
      case 5: {
        int ix2 = iw + idot;
        int ix3 = ix2 + idot;
        int ix4 = ix3 + idot;
        pffft_passf5_ps(idot, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], &wa[ix4], isign);
      } break;
      case 4: {
        int ix2 = iw + idot;
        int ix3 = ix2 + idot;
        pffft_passf4_ps(idot, l1, in, out, &wa[iw], &wa[ix2], &wa[ix3], isign);
      } break;
      case 2: {
        pffft_passf2_ps(idot, l1, in, out, &wa[iw], isign);
      } break;
      case 3: {
        int ix2 = iw + idot;
        pffft_passf3_ps(idot, l1, in, out, &wa[iw], &wa[ix2], isign);
      } break;
      default:
        PFFFT_ASSERT(0);
    }
    l1 = l2;
    iw += (ip - 1)*idot;
    if (out == work2) {
      out = work1; in = work2;
    } else {
      out = work2; in = work1;
    }
  }

  return in; /* this is in fact the output .. */
}


struct PFFFT_Setup {
  int     N;
  int     Ncvec; // nb of complex simd vectors (N/4 if PFFFT_COMPLEX, N/8 if PFFFT_REAL)
  int ifac[15];
  pffft_transform_t transform;
  v4sf *data; // allocated room for twiddle coefs
  float *e;    // points into 'data' , N/4*3 elements
  float *twiddle; // points into 'data', N/4 elements
};

PFFFT_Setup *pffft_new_setup(int N, pffft_transform_t transform) {
  PFFFT_Setup *s = (PFFFT_Setup*)PFFFT_MALLOC(sizeof(PFFFT_Setup));
  int k, m;
  /* unfortunately, the fft size must be a multiple of 16 for complex FFTs 
     and 32 for real FFTs -- a lot of stuff would need to be rewritten to
     handle other cases (or maybe just switch to a scalar fft, I don't know..) */
  if (transform == PFFFT_REAL) { PFFFT_ASSERT((N%(2*PFFFT_SIMD_SZ*PFFFT_SIMD_SZ))==0 && N>0); }
  if (transform == PFFFT_COMPLEX) { PFFFT_ASSERT((N%(PFFFT_SIMD_SZ*PFFFT_SIMD_SZ))==0 && N>0); }
  //PFFFT_ASSERT((N % 32) == 0);
  s->N = N;
  s->transform = transform;  
  /* nb of complex simd vectors */
  s->Ncvec = (transform == PFFFT_REAL ? N/2 : N)/PFFFT_SIMD_SZ;
  s->data = (v4sf*)pffft_aligned_malloc(2*s->Ncvec * sizeof(v4sf));
  s->e = (float*)s->data;
  s->twiddle = (float*)(s->data + (2*s->Ncvec*(PFFFT_SIMD_SZ-1))/PFFFT_SIMD_SZ);  

  if (transform == PFFFT_REAL) {
    for (k=0; k < s->Ncvec; ++k) {
      int i = k/PFFFT_SIMD_SZ;
      int j = k%PFFFT_SIMD_SZ;
      for (m=0; m < PFFFT_SIMD_SZ-1; ++m) {
        float A = -2*M_PI*(m+1)*k / N;
        s->e[(2*(i*3 + m) + 0) * PFFFT_SIMD_SZ + j] = cos(A);
        s->e[(2*(i*3 + m) + 1) * PFFFT_SIMD_SZ + j] = sin(A);
      }
    }
    pffft_rffti1_ps(N/PFFFT_SIMD_SZ, s->twiddle, s->ifac);
  } else {
    for (k=0; k < s->Ncvec; ++k) {
      int i = k/PFFFT_SIMD_SZ;
      int j = k%PFFFT_SIMD_SZ;
      for (m=0; m < PFFFT_SIMD_SZ-1; ++m) {
        float A = -2*M_PI*(m+1)*k / N;
        s->e[(2*(i*3 + m) + 0)*PFFFT_SIMD_SZ + j] = cos(A);
        s->e[(2*(i*3 + m) + 1)*PFFFT_SIMD_SZ + j] = sin(A);
      }
    }
    pffft_cffti1_ps(N/PFFFT_SIMD_SZ, s->twiddle, s->ifac);
  }

  /* check that N is decomposable with allowed prime factors */
  for (k=0, m=1; k < s->ifac[1]; ++k) { m *= s->ifac[2+k]; }
  if (m != N/PFFFT_SIMD_SZ) {
    pffft_destroy_setup(s); s = 0;
  }

  return s;
}


void pffft_destroy_setup(PFFFT_Setup *s) {
  pffft_aligned_free(s->data);
  PFFFT_FREE(s);
}

#if !defined(PFFFT_SIMD_DISABLE)

/* [0 0 1 2 3 4 5 6 7 8] -> [0 8 7 6 5 4 3 2 1] */
static void pffft_reversed_copy(int N, const v4sf *in, int in_stride, v4sf *out) {
  v4sf g0, g1;
  int k;
  PFFFT_INTERLEAVE2(in[0], in[1], g0, g1); in += in_stride;
  
  *--out = PFFFT_VSWAPHL(g0, g1); // [g0l, g0h], [g1l g1h] -> [g1l, g0h]
  for (k=1; k < N; ++k) {
    v4sf h0, h1;
    PFFFT_INTERLEAVE2(in[0], in[1], h0, h1); in += in_stride;
    *--out = PFFFT_VSWAPHL(g1, h0);
    *--out = PFFFT_VSWAPHL(h0, h1);
    g1 = h1;
  }
  *--out = PFFFT_VSWAPHL(g1, g0);
}

static void pffft_unreversed_copy(int N, const v4sf *in, v4sf *out, int out_stride) {
  v4sf g0, g1, h0, h1;
  int k;
  g0 = g1 = in[0]; ++in;
  for (k=1; k < N; ++k) {
    h0 = *in++; h1 = *in++;
    g1 = PFFFT_VSWAPHL(g1, h0);
    h0 = PFFFT_VSWAPHL(h0, h1);
    PFFFT_UNINTERLEAVE2(h0, g1, out[0], out[1]); out += out_stride;
    g1 = h1;
  }
  h0 = *in++; h1 = g0;
  g1 = PFFFT_VSWAPHL(g1, h0);
  h0 = PFFFT_VSWAPHL(h0, h1);
  PFFFT_UNINTERLEAVE2(h0, g1, out[0], out[1]);
}

void pffft_zreorder(PFFFT_Setup *setup, const float *in, float *out, pffft_direction_t direction) {
  int k, N = setup->N, Ncvec = setup->Ncvec;
  const v4sf *vin = (const v4sf*)in;
  v4sf *vout = (v4sf*)out;
  PFFFT_ASSERT(in != out);
  if (setup->transform == PFFFT_REAL) {
    int dk = N/32;
    if (direction == PFFFT_FORWARD) {
      for (k=0; k < dk; ++k) {
        PFFFT_INTERLEAVE2(vin[k*8 + 0], vin[k*8 + 1], vout[2*(0*dk + k) + 0], vout[2*(0*dk + k) + 1]);
        PFFFT_INTERLEAVE2(vin[k*8 + 4], vin[k*8 + 5], vout[2*(2*dk + k) + 0], vout[2*(2*dk + k) + 1]);
      }
      pffft_reversed_copy(dk, vin+2, 8, (v4sf*)(out + N/2));
      pffft_reversed_copy(dk, vin+6, 8, (v4sf*)(out + N));
    } else {
      for (k=0; k < dk; ++k) {
        PFFFT_UNINTERLEAVE2(vin[2*(0*dk + k) + 0], vin[2*(0*dk + k) + 1], vout[k*8 + 0], vout[k*8 + 1]);
        PFFFT_UNINTERLEAVE2(vin[2*(2*dk + k) + 0], vin[2*(2*dk + k) + 1], vout[k*8 + 4], vout[k*8 + 5]);
      }
      pffft_unreversed_copy(dk, (v4sf*)(in + N/4), (v4sf*)(out + N - 6*PFFFT_SIMD_SZ), -8);
      pffft_unreversed_copy(dk, (v4sf*)(in + 3*N/4), (v4sf*)(out + N - 2*PFFFT_SIMD_SZ), -8);
    }
  } else {
    if (direction == PFFFT_FORWARD) {
      for (k=0; k < Ncvec; ++k) { 
        int kk = (k/4) + (k%4)*(Ncvec/4);
        PFFFT_INTERLEAVE2(vin[k*2], vin[k*2+1], vout[kk*2], vout[kk*2+1]);
      }
    } else {
      for (k=0; k < Ncvec; ++k) { 
        int kk = (k/4) + (k%4)*(Ncvec/4);
        PFFFT_UNINTERLEAVE2(vin[kk*2], vin[kk*2+1], vout[k*2], vout[k*2+1]);
      }
    }
  }
}

void pffft_cplx_finalize(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/PFFFT_SIMD_SZ; // number of 4x4 matrix blocks
  v4sf r0, i0, r1, i1, r2, i2, r3, i3;
  v4sf sr0, dr0, sr1, dr1, si0, di0, si1, di1;
  PFFFT_ASSERT(in != out);
  for (k=0; k < dk; ++k) {    
    r0 = in[8*k+0]; i0 = in[8*k+1];
    r1 = in[8*k+2]; i1 = in[8*k+3];
    r2 = in[8*k+4]; i2 = in[8*k+5];
    r3 = in[8*k+6]; i3 = in[8*k+7];
    PFFFT_VTRANSPOSE4(r0,r1,r2,r3);
    PFFFT_VTRANSPOSE4(i0,i1,i2,i3);
    PFFFT_VCPLXMUL(r1,i1,e[k*6+0],e[k*6+1]);
    PFFFT_VCPLXMUL(r2,i2,e[k*6+2],e[k*6+3]);
    PFFFT_VCPLXMUL(r3,i3,e[k*6+4],e[k*6+5]);

    sr0 = PFFFT_VADD(r0,r2); dr0 = PFFFT_VSUB(r0, r2);
    sr1 = PFFFT_VADD(r1,r3); dr1 = PFFFT_VSUB(r1, r3);
    si0 = PFFFT_VADD(i0,i2); di0 = PFFFT_VSUB(i0, i2);
    si1 = PFFFT_VADD(i1,i3); di1 = PFFFT_VSUB(i1, i3);

    /*
      transformation for each column is:
      
      [1   1   1   1   0   0   0   0]   [r0]
      [1   0  -1   0   0  -1   0   1]   [r1]
      [1  -1   1  -1   0   0   0   0]   [r2]
      [1   0  -1   0   0   1   0  -1]   [r3]
      [0   0   0   0   1   1   1   1] * [i0]
      [0   1   0  -1   1   0  -1   0]   [i1]
      [0   0   0   0   1  -1   1  -1]   [i2]
      [0  -1   0   1   1   0  -1   0]   [i3]    
    */
    
    r0 = PFFFT_VADD(sr0, sr1); i0 = PFFFT_VADD(si0, si1);
    r1 = PFFFT_VADD(dr0, di1); i1 = PFFFT_VSUB(di0, dr1);
    r2 = PFFFT_VSUB(sr0, sr1); i2 = PFFFT_VSUB(si0, si1);
    r3 = PFFFT_VSUB(dr0, di1); i3 = PFFFT_VADD(di0, dr1);
  
    *out++ = r0; *out++ = i0; *out++ = r1; *out++ = i1;
    *out++ = r2; *out++ = i2; *out++ = r3; *out++ = i3;
  }
}

void pffft_cplx_preprocess(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/PFFFT_SIMD_SZ; // number of 4x4 matrix blocks
  v4sf r0, i0, r1, i1, r2, i2, r3, i3;
  v4sf sr0, dr0, sr1, dr1, si0, di0, si1, di1;
  PFFFT_ASSERT(in != out);
  for (k=0; k < dk; ++k) {    
    r0 = in[8*k+0]; i0 = in[8*k+1];
    r1 = in[8*k+2]; i1 = in[8*k+3];
    r2 = in[8*k+4]; i2 = in[8*k+5];
    r3 = in[8*k+6]; i3 = in[8*k+7];

    sr0 = PFFFT_VADD(r0,r2); dr0 = PFFFT_VSUB(r0, r2);
    sr1 = PFFFT_VADD(r1,r3); dr1 = PFFFT_VSUB(r1, r3);
    si0 = PFFFT_VADD(i0,i2); di0 = PFFFT_VSUB(i0, i2);
    si1 = PFFFT_VADD(i1,i3); di1 = PFFFT_VSUB(i1, i3);

    r0 = PFFFT_VADD(sr0, sr1); i0 = PFFFT_VADD(si0, si1);
    r1 = PFFFT_VSUB(dr0, di1); i1 = PFFFT_VADD(di0, dr1);
    r2 = PFFFT_VSUB(sr0, sr1); i2 = PFFFT_VSUB(si0, si1);
    r3 = PFFFT_VADD(dr0, di1); i3 = PFFFT_VSUB(di0, dr1);

    PFFFT_VCPLXMULCONJ(r1,i1,e[k*6+0],e[k*6+1]);
    PFFFT_VCPLXMULCONJ(r2,i2,e[k*6+2],e[k*6+3]);
    PFFFT_VCPLXMULCONJ(r3,i3,e[k*6+4],e[k*6+5]);

    PFFFT_VTRANSPOSE4(r0,r1,r2,r3);
    PFFFT_VTRANSPOSE4(i0,i1,i2,i3);

    *out++ = r0; *out++ = i0; *out++ = r1; *out++ = i1;
    *out++ = r2; *out++ = i2; *out++ = r3; *out++ = i3;
  }
}


static PFFFT_ALWAYS_INLINE(void) pffft_real_finalize_4x4(const v4sf *in0, const v4sf *in1, const v4sf *in,
                            const v4sf *e, v4sf *out) {
  v4sf r0, i0, r1, i1, r2, i2, r3, i3;
  v4sf sr0, dr0, sr1, dr1, si0, di0, si1, di1;
  r0 = *in0; i0 = *in1;
  r1 = *in++; i1 = *in++; r2 = *in++; i2 = *in++; r3 = *in++; i3 = *in++;
  PFFFT_VTRANSPOSE4(r0,r1,r2,r3);
  PFFFT_VTRANSPOSE4(i0,i1,i2,i3);
 
  /*
    transformation for each column is:

    [1   1   1   1   0   0   0   0]   [r0]
    [1   0  -1   0   0  -1   0   1]   [r1]
    [1   0  -1   0   0   1   0  -1]   [r2]
    [1  -1   1  -1   0   0   0   0]   [r3]
    [0   0   0   0   1   1   1   1] * [i0]
    [0  -1   0   1  -1   0   1   0]   [i1]
    [0  -1   0   1   1   0  -1   0]   [i2]
    [0   0   0   0  -1   1  -1   1]   [i3]    
  */
  
  //cerr << "matrix initial, before e , REAL:\n 1: " << r0 << "\n 1: " << r1 << "\n 1: " << r2 << "\n 1: " << r3 << "\n";
  //cerr << "matrix initial, before e, IMAG :\n 1: " << i0 << "\n 1: " << i1 << "\n 1: " << i2 << "\n 1: " << i3 << "\n";

  PFFFT_VCPLXMUL(r1,i1,e[0],e[1]);
  PFFFT_VCPLXMUL(r2,i2,e[2],e[3]);
  PFFFT_VCPLXMUL(r3,i3,e[4],e[5]);

  //cerr << "matrix initial, real part:\n 1: " << r0 << "\n 1: " << r1 << "\n 1: " << r2 << "\n 1: " << r3 << "\n";
  //cerr << "matrix initial, imag part:\n 1: " << i0 << "\n 1: " << i1 << "\n 1: " << i2 << "\n 1: " << i3 << "\n";

  sr0 = PFFFT_VADD(r0,r2); dr0 = PFFFT_VSUB(r0,r2); 
  sr1 = PFFFT_VADD(r1,r3); dr1 = PFFFT_VSUB(r3,r1);
  si0 = PFFFT_VADD(i0,i2); di0 = PFFFT_VSUB(i0,i2); 
  si1 = PFFFT_VADD(i1,i3); di1 = PFFFT_VSUB(i3,i1);

  r0 = PFFFT_VADD(sr0, sr1);
  r3 = PFFFT_VSUB(sr0, sr1);
  i0 = PFFFT_VADD(si0, si1);
  i3 = PFFFT_VSUB(si1, si0);
  r1 = PFFFT_VADD(dr0, di1);
  r2 = PFFFT_VSUB(dr0, di1);
  i1 = PFFFT_VSUB(dr1, di0);
  i2 = PFFFT_VADD(dr1, di0);

  *out++ = r0;
  *out++ = i0;
  *out++ = r1;
  *out++ = i1;
  *out++ = r2;
  *out++ = i2;
  *out++ = r3;
  *out++ = i3;

}

static PFFFT_NEVER_INLINE(void) pffft_real_finalize(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/PFFFT_SIMD_SZ; // number of 4x4 matrix blocks
  /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

  v4sf_union cr, ci, *uout = (v4sf_union*)out;
  v4sf save = in[7], zero=PFFFT_VZERO();
  float xr0, xi0, xr1, xi1, xr2, xi2, xr3, xi3;
  static const float s = M_SQRT2/2;

  cr.v = in[0]; ci.v = in[Ncvec*2-1];
  PFFFT_ASSERT(in != out);
  pffft_real_finalize_4x4(&zero, &zero, in+1, e, out);

  /*
    [cr0 cr1 cr2 cr3 ci0 ci1 ci2 ci3]

    [Xr(1)]  ] [1   1   1   1   0   0   0   0]
    [Xr(N/4) ] [0   0   0   0   1   s   0  -s]
    [Xr(N/2) ] [1   0  -1   0   0   0   0   0]
    [Xr(3N/4)] [0   0   0   0   1  -s   0   s]
    [Xi(1)   ] [1  -1   1  -1   0   0   0   0]
    [Xi(N/4) ] [0   0   0   0   0  -s  -1  -s]
    [Xi(N/2) ] [0  -1   0   1   0   0   0   0]
    [Xi(3N/4)] [0   0   0   0   0  -s   1  -s]
  */

  xr0=(cr.f[0]+cr.f[2]) + (cr.f[1]+cr.f[3]); uout[0].f[0] = xr0;
  xi0=(cr.f[0]+cr.f[2]) - (cr.f[1]+cr.f[3]); uout[1].f[0] = xi0;
  xr2=(cr.f[0]-cr.f[2]);                     uout[4].f[0] = xr2;
  xi2=(cr.f[3]-cr.f[1]);                     uout[5].f[0] = xi2;
  xr1= ci.f[0] + s*(ci.f[1]-ci.f[3]);        uout[2].f[0] = xr1;
  xi1=-ci.f[2] - s*(ci.f[1]+ci.f[3]);        uout[3].f[0] = xi1;
  xr3= ci.f[0] - s*(ci.f[1]-ci.f[3]);        uout[6].f[0] = xr3;
  xi3= ci.f[2] - s*(ci.f[1]+ci.f[3]);        uout[7].f[0] = xi3; 

  for (k=1; k < dk; ++k) {
    v4sf save_next = in[8*k+7];
    pffft_real_finalize_4x4(&save, &in[8*k+0], in + 8*k+1,
                           e + k*6, out + k*8);
    save = save_next;
  }

}

static PFFFT_ALWAYS_INLINE(void) pffft_real_preprocess_4x4(const v4sf *in, 
                                             const v4sf *e, v4sf *out, int first) {
  v4sf r0=in[0], i0=in[1], r1=in[2], i1=in[3], r2=in[4], i2=in[5], r3=in[6], i3=in[7];
  /*
    transformation for each column is:

    [1   1   1   1   0   0   0   0]   [r0]
    [1   0   0  -1   0  -1  -1   0]   [r1]
    [1  -1  -1   1   0   0   0   0]   [r2]
    [1   0   0  -1   0   1   1   0]   [r3]
    [0   0   0   0   1  -1   1  -1] * [i0]
    [0  -1   1   0   1   0   0   1]   [i1]
    [0   0   0   0   1   1  -1  -1]   [i2]
    [0   1  -1   0   1   0   0   1]   [i3]    
  */

  v4sf sr0 = PFFFT_VADD(r0,r3), dr0 = PFFFT_VSUB(r0,r3); 
  v4sf sr1 = PFFFT_VADD(r1,r2), dr1 = PFFFT_VSUB(r1,r2);
  v4sf si0 = PFFFT_VADD(i0,i3), di0 = PFFFT_VSUB(i0,i3); 
  v4sf si1 = PFFFT_VADD(i1,i2), di1 = PFFFT_VSUB(i1,i2);

  r0 = PFFFT_VADD(sr0, sr1);
  r2 = PFFFT_VSUB(sr0, sr1);
  r1 = PFFFT_VSUB(dr0, si1);
  r3 = PFFFT_VADD(dr0, si1);
  i0 = PFFFT_VSUB(di0, di1);
  i2 = PFFFT_VADD(di0, di1);
  i1 = PFFFT_VSUB(si0, dr1);
  i3 = PFFFT_VADD(si0, dr1);

  PFFFT_VCPLXMULCONJ(r1,i1,e[0],e[1]);
  PFFFT_VCPLXMULCONJ(r2,i2,e[2],e[3]);
  PFFFT_VCPLXMULCONJ(r3,i3,e[4],e[5]);

  PFFFT_VTRANSPOSE4(r0,r1,r2,r3);
  PFFFT_VTRANSPOSE4(i0,i1,i2,i3);

  if (!first) {
    *out++ = r0;
    *out++ = i0;
  }
  *out++ = r1;
  *out++ = i1;
  *out++ = r2;
  *out++ = i2;
  *out++ = r3;
  *out++ = i3;
}

static PFFFT_NEVER_INLINE(void) pffft_real_preprocess(int Ncvec, const v4sf *in, v4sf *out, const v4sf *e) {
  int k, dk = Ncvec/PFFFT_SIMD_SZ; // number of 4x4 matrix blocks
  /* fftpack order is f0r f1r f1i f2r f2i ... f(n-1)r f(n-1)i f(n)r */

  v4sf_union Xr, Xi, *uout = (v4sf_union*)out;
  float cr0, ci0, cr1, ci1, cr2, ci2, cr3, ci3;
  static const float s = M_SQRT2;
  PFFFT_ASSERT(in != out);
  for (k=0; k < 4; ++k) {
    Xr.f[k] = ((float*)in)[8*k];
    Xi.f[k] = ((float*)in)[8*k+4];
  }

  pffft_real_preprocess_4x4(in, e, out+1, 1); // will write only 6 values

  /*
    [Xr0 Xr1 Xr2 Xr3 Xi0 Xi1 Xi2 Xi3]

    [cr0] [1   0   2   0   1   0   0   0]
    [cr1] [1   0   0   0  -1   0  -2   0]
    [cr2] [1   0  -2   0   1   0   0   0]
    [cr3] [1   0   0   0  -1   0   2   0]
    [ci0] [0   2   0   2   0   0   0   0]
    [ci1] [0   s   0  -s   0  -s   0  -s]
    [ci2] [0   0   0   0   0  -2   0   2]
    [ci3] [0  -s   0   s   0  -s   0  -s]
  */
  for (k=1; k < dk; ++k) {    
    pffft_real_preprocess_4x4(in+8*k, e + k*6, out-1+k*8, 0);
  }

  cr0=(Xr.f[0]+Xi.f[0]) + 2*Xr.f[2]; uout[0].f[0] = cr0;
  cr1=(Xr.f[0]-Xi.f[0]) - 2*Xi.f[2]; uout[0].f[1] = cr1;
  cr2=(Xr.f[0]+Xi.f[0]) - 2*Xr.f[2]; uout[0].f[2] = cr2;
  cr3=(Xr.f[0]-Xi.f[0]) + 2*Xi.f[2]; uout[0].f[3] = cr3;
  ci0= 2*(Xr.f[1]+Xr.f[3]);                       uout[2*Ncvec-1].f[0] = ci0;
  ci1= s*(Xr.f[1]-Xr.f[3]) - s*(Xi.f[1]+Xi.f[3]); uout[2*Ncvec-1].f[1] = ci1;
  ci2= 2*(Xi.f[3]-Xi.f[1]);                       uout[2*Ncvec-1].f[2] = ci2;
  ci3=-s*(Xr.f[1]-Xr.f[3]) - s*(Xi.f[1]+Xi.f[3]); uout[2*Ncvec-1].f[3] = ci3;
}


void pffft_transform_internal(PFFFT_Setup *setup, const float *finput, float *foutput, v4sf *scratch,
                             pffft_direction_t direction, int ordered) {
  int k, Ncvec   = setup->Ncvec;
  int nf_odd = (setup->ifac[1] & 1);

  // temporary buffer is allocated on the stack if the scratch pointer is NULL
  int stack_allocate = (scratch == 0 ? Ncvec*2 : 1);
  PFFFT_VLA_ARRAY_ON_STACK(v4sf, scratch_on_stack, stack_allocate);

  const v4sf *vinput = (const v4sf*)finput;
  v4sf *voutput      = (v4sf*)foutput;
  v4sf *buff[2]      = { voutput, scratch ? scratch : scratch_on_stack };
  int ib = (nf_odd ^ ordered ? 1 : 0);

  PFFFT_ASSERT(PFFFT_VALIGNED(finput));
  PFFFT_ASSERT(PFFFT_VALIGNED(foutput));

  //PFFFT_ASSERT(finput != foutput);
  if (direction == PFFFT_FORWARD) {
    ib = !ib;
    if (setup->transform == PFFFT_REAL) { 
      ib = (pffft_rfftf1_ps(Ncvec*2, vinput, buff[ib], buff[!ib],
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);      
      pffft_real_finalize(Ncvec, buff[ib], buff[!ib], (v4sf*)setup->e);
    } else {
      v4sf *tmp = buff[ib];
      for (k=0; k < Ncvec; ++k) {
        PFFFT_UNINTERLEAVE2(vinput[k*2], vinput[k*2+1], tmp[k*2], tmp[k*2+1]);
      }
      ib = (pffft_cfftf1_ps(Ncvec, buff[ib], buff[!ib], buff[ib], 
                      setup->twiddle, &setup->ifac[0], -1) == buff[0] ? 0 : 1);
      pffft_cplx_finalize(Ncvec, buff[ib], buff[!ib], (v4sf*)setup->e);
    }
    if (ordered) {
      pffft_zreorder(setup, (float*)buff[!ib], (float*)buff[ib], PFFFT_FORWARD);       
    } else ib = !ib;
  } else {
    if (vinput == buff[ib]) { 
      ib = !ib; // may happen when finput == foutput
    }
    if (ordered) {
      pffft_zreorder(setup, (float*)vinput, (float*)buff[ib], PFFFT_BACKWARD); 
      vinput = buff[ib]; ib = !ib;
    }
    if (setup->transform == PFFFT_REAL) {
      pffft_real_preprocess(Ncvec, vinput, buff[ib], (v4sf*)setup->e);
      ib = (pffft_rfftb1_ps(Ncvec*2, buff[ib], buff[0], buff[1], 
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);
    } else {
      pffft_cplx_preprocess(Ncvec, vinput, buff[ib], (v4sf*)setup->e);
      ib = (pffft_cfftf1_ps(Ncvec, buff[ib], buff[0], buff[1], 
                      setup->twiddle, &setup->ifac[0], +1) == buff[0] ? 0 : 1);
      for (k=0; k < Ncvec; ++k) {
        PFFFT_INTERLEAVE2(buff[ib][k*2], buff[ib][k*2+1], buff[ib][k*2], buff[ib][k*2+1]);
      }
    }
  }
  
  if (buff[ib] != voutput) {
    /* extra copy required -- this situation should only happen when finput == foutput */
    PFFFT_ASSERT(finput==foutput);
    for (k=0; k < Ncvec; ++k) {
      v4sf a = buff[ib][2*k], b = buff[ib][2*k+1];
      voutput[2*k] = a; voutput[2*k+1] = b;
    }
    ib = !ib;
  }
  PFFFT_ASSERT(buff[ib] == voutput);
}

void pffft_zconvolve_accumulate(PFFFT_Setup *s, const float *a, const float *b, float *ab, float scaling) {
  int Ncvec = s->Ncvec;
  const v4sf * PFFFT_RESTRICT va = (const v4sf*)a;
  const v4sf * PFFFT_RESTRICT vb = (const v4sf*)b;
  v4sf * PFFFT_RESTRICT vab = (v4sf*)ab;

#ifdef __arm__
  __builtin_prefetch(va);
  __builtin_prefetch(vb);
  __builtin_prefetch(vab);
  __builtin_prefetch(va+2);
  __builtin_prefetch(vb+2);
  __builtin_prefetch(vab+2);
  __builtin_prefetch(va+4);
  __builtin_prefetch(vb+4);
  __builtin_prefetch(vab+4);
  __builtin_prefetch(va+6);
  __builtin_prefetch(vb+6);
  __builtin_prefetch(vab+6);
# ifndef __clang__
#   define ZCONVOLVE_USING_INLINE_NEON_ASM
# endif
#endif

  float ar, ai, br, bi, abr, abi;
#ifndef ZCONVOLVE_USING_INLINE_ASM
  v4sf vscal = PFFFT_LD_PS1(scaling);
  int i;
#endif

  PFFFT_ASSERT(PFFFT_VALIGNED(a));
  PFFFT_ASSERT(PFFFT_VALIGNED(b));
  PFFFT_ASSERT(PFFFT_VALIGNED(ab));
  ar = ((v4sf_union*)va)[0].f[0];
  ai = ((v4sf_union*)va)[1].f[0];
  br = ((v4sf_union*)vb)[0].f[0];
  bi = ((v4sf_union*)vb)[1].f[0];
  abr = ((v4sf_union*)vab)[0].f[0];
  abi = ((v4sf_union*)vab)[1].f[0];
 
#ifdef ZCONVOLVE_USING_INLINE_ASM // inline asm version, unfortunately miscompiled by clang 3.2, at least on ubuntu.. so this will be restricted to gcc
  const float *a_ = a, *b_ = b; float *ab_ = ab;
  int N = Ncvec;
  asm volatile("mov         r8, %2                  \n"
               "vdup.f32    q15, %4                 \n"
               "1:                                  \n"
               "pld         [%0,#64]                \n"
               "pld         [%1,#64]                \n"
               "pld         [%2,#64]                \n"
               "pld         [%0,#96]                \n"
               "pld         [%1,#96]                \n"
               "pld         [%2,#96]                \n"
               "vld1.f32    {q0,q1},   [%0,:128]!         \n"
               "vld1.f32    {q4,q5},   [%1,:128]!         \n"
               "vld1.f32    {q2,q3},   [%0,:128]!         \n"
               "vld1.f32    {q6,q7},   [%1,:128]!         \n"
               "vld1.f32    {q8,q9},   [r8,:128]!          \n"
               
               "vmul.f32    q10, q0, q4             \n"
               "vmul.f32    q11, q0, q5             \n"
               "vmul.f32    q12, q2, q6             \n" 
               "vmul.f32    q13, q2, q7             \n"                 
               "vmls.f32    q10, q1, q5             \n"
               "vmla.f32    q11, q1, q4             \n"
               "vld1.f32    {q0,q1}, [r8,:128]!     \n"
               "vmls.f32    q12, q3, q7             \n"
               "vmla.f32    q13, q3, q6             \n"
               "vmla.f32    q8, q10, q15            \n"
               "vmla.f32    q9, q11, q15            \n"
               "vmla.f32    q0, q12, q15            \n"
               "vmla.f32    q1, q13, q15            \n"
               "vst1.f32    {q8,q9},[%2,:128]!    \n"
               "vst1.f32    {q0,q1},[%2,:128]!    \n"
               "subs        %3, #2                  \n"
               "bne         1b                      \n"
               : "+r"(a_), "+r"(b_), "+r"(ab_), "+r"(N) : "r"(scaling) : "r8", "q0","q1","q2","q3","q4","q5","q6","q7","q8","q9", "q10","q11","q12","q13","q15","memory");
#else // default routine, works fine for non-arm cpus with current compilers
  for (i=0; i < Ncvec; i += 2) {
    v4sf var, vai, vbr, vbi;
    var = va[2*i+0]; vai = va[2*i+1];
    vbr = vb[2*i+0]; vbi = vb[2*i+1];
    PFFFT_VCPLXMUL(var, vai, vbr, vbi);
    vab[2*i+0] = PFFFT_VMADD(var, vscal, vab[2*i+0]);
    vab[2*i+1] = PFFFT_VMADD(vai, vscal, vab[2*i+1]);
    var = va[2*i+2]; vai = va[2*i+3];
    vbr = vb[2*i+2]; vbi = vb[2*i+3];
    PFFFT_VCPLXMUL(var, vai, vbr, vbi);
    vab[2*i+2] = PFFFT_VMADD(var, vscal, vab[2*i+2]);
    vab[2*i+3] = PFFFT_VMADD(vai, vscal, vab[2*i+3]);
  }
#endif
  if (s->transform == PFFFT_REAL) {
    ((v4sf_union*)vab)[0].f[0] = abr + ar*br*scaling;
    ((v4sf_union*)vab)[1].f[0] = abi + ai*bi*scaling;
  }
}


#else // defined(PFFFT_SIMD_DISABLE)

// standard routine using scalar floats, without SIMD stuff.

#define pffft_zreorder_nosimd pffft_zreorder
void pffft_zreorder_nosimd(PFFFT_Setup *setup, const float *in, float *out, pffft_direction_t direction) {
  int k, N = setup->N;
  if (setup->transform == PFFFT_COMPLEX) {
    for (k=0; k < 2*N; ++k) out[k] = in[k];
    return;
  }
  else if (direction == PFFFT_FORWARD) {
    float x_N = in[N-1];
    for (k=N-1; k > 1; --k) out[k] = in[k-1]; 
    out[0] = in[0];
    out[1] = x_N;
  } else {
    float x_N = in[1];
    for (k=1; k < N-1; ++k) out[k] = in[k+1]; 
    out[0] = in[0];
    out[N-1] = x_N;
  }
}

#define pffft_transform_internal_nosimd pffft_transform_internal
void pffft_transform_internal_nosimd(PFFFT_Setup *setup, const float *input, float *output, float *scratch,
                                    pffft_direction_t direction, int ordered) {
  int Ncvec   = setup->Ncvec;
  int nf_odd = (setup->ifac[1] & 1);

  // temporary buffer is allocated on the stack if the scratch pointer is NULL
  int stack_allocate = (scratch == 0 ? Ncvec*2 : 1);
  PFFFT_VLA_ARRAY_ON_STACK(v4sf, scratch_on_stack, stack_allocate);
  float *buff[2];
  int ib;
  if (scratch == 0) scratch = scratch_on_stack;
  buff[0] = output; buff[1] = scratch;

  if (setup->transform == PFFFT_COMPLEX) ordered = 0; // it is always ordered.
  ib = (nf_odd ^ ordered ? 1 : 0);

  if (direction == PFFFT_FORWARD) {
    if (setup->transform == PFFFT_REAL) { 
      ib = (pffft_rfftf1_ps(Ncvec*2, input, buff[ib], buff[!ib],
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);      
    } else {
      ib = (pffft_cfftf1_ps(Ncvec, input, buff[ib], buff[!ib], 
                      setup->twiddle, &setup->ifac[0], -1) == buff[0] ? 0 : 1);
    }
    if (ordered) {
      pffft_zreorder(setup, buff[ib], buff[!ib], PFFFT_FORWARD); ib = !ib;
    }
  } else {    
    if (input == buff[ib]) { 
      ib = !ib; // may happen when finput == foutput
    }
    if (ordered) {
      pffft_zreorder(setup, input, buff[!ib], PFFFT_BACKWARD); 
      input = buff[!ib];
    }
    if (setup->transform == PFFFT_REAL) {
      ib = (pffft_rfftb1_ps(Ncvec*2, input, buff[ib], buff[!ib], 
                      setup->twiddle, &setup->ifac[0]) == buff[0] ? 0 : 1);
    } else {
      ib = (pffft_cfftf1_ps(Ncvec, input, buff[ib], buff[!ib], 
                      setup->twiddle, &setup->ifac[0], +1) == buff[0] ? 0 : 1);
    }
  }
  if (buff[ib] != output) {
    int k;
    // extra copy required -- this situation should happens only when finput == foutput
    PFFFT_ASSERT(input==output);
    for (k=0; k < Ncvec; ++k) {
      float a = buff[ib][2*k], b = buff[ib][2*k+1];
      output[2*k] = a; output[2*k+1] = b;
    }
    ib = !ib;
  }
  PFFFT_ASSERT(buff[ib] == output);
}

#define pffft_zconvolve_accumulate_nosimd pffft_zconvolve_accumulate
void pffft_zconvolve_accumulate_nosimd(PFFFT_Setup *s, const float *a, const float *b,
                                       float *ab, float scaling) {
  int i, Ncvec = s->Ncvec;

  if (s->transform == PFFFT_REAL) {
    // take care of the fftpack ordering
    ab[0] += a[0]*b[0]*scaling;
    ab[2*Ncvec-1] += a[2*Ncvec-1]*b[2*Ncvec-1]*scaling;
    ++ab; ++a; ++b; --Ncvec;
  }
  for (i=0; i < Ncvec; ++i) {
    float ar, ai, br, bi;
    ar = a[2*i+0]; ai = a[2*i+1];
    br = b[2*i+0]; bi = b[2*i+1];
    PFFFT_VCPLXMUL(ar, ai, br, bi);
    ab[2*i+0] += ar*scaling;
    ab[2*i+1] += ai*scaling;
  }
}

#endif // defined(PFFFT_SIMD_DISABLE)

void pffft_transform(PFFFT_Setup *setup, const float *input, float *output, float *work, pffft_direction_t direction) {
  pffft_transform_internal(setup, input, output, (v4sf*)work, direction, 0);
}

void pffft_transform_ordered(PFFFT_Setup *setup, const float *input, float *output, float *work, pffft_direction_t direction) {
  pffft_transform_internal(setup, input, output, (v4sf*)work, direction, 1);
}

// clang-format on