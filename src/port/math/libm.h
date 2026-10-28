/*
 * N91: stands in for musl's src/internal/libm.h (and <features.h>'s `hidden`) for the copy of musl's math in this
 * directory (musl 1.2.6 as Emscripten 6.0.13 ships it, the web build's libm; COPYRIGHT.musl). Only what these files
 * use is here. The public functions get their pb_ names from include/pb_math.h (through include/math.h),
 * the internal ones (__sin, __exp_data, ...) are renamed below so they can't clash with the platform libc's own
 * (the web build links musl's libc.a statically next to this).
 */
#ifndef _LIBM_H
#define _LIBM_H

#include <stdint.h>
#include <float.h>
#include <math.h>

/* __rem_pio2's rounding trick and the double_t/float_t temporaries assume no excess precision (x87 would round
   differently): every target we build (x86_64 SSE, arm64, wasm32) has FLT_EVAL_METHOD 0. */
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "src/port/math needs FLT_EVAL_METHOD == 0"
#endif

/* Same results on every platform: no fused multiply-add paths (log.c, pow.c take one where the target has FMA). The
   files are also compiled with -ffp-contract=off (cmake/pbmath.cmake). */
#ifdef __FP_FAST_FMA
#undef __FP_FAST_FMA
#endif

#if defined(__GNUC__) && !defined(_WIN32)
#define hidden __attribute__((__visibility__("hidden")))
#else
#define hidden
#endif

#define __rem_pio2_large pb__rem_pio2_large
#define __rem_pio2 pb__rem_pio2
#define __sin pb__sin
#define __cos pb__cos
#define __tan pb__tan
#define __rem_pio2f pb__rem_pio2f
#define __sindf pb__sindf
#define __cosdf pb__cosdf
#define __tandf pb__tandf
#define __math_xflow pb__math_xflow
#define __math_uflow pb__math_uflow
#define __math_oflow pb__math_oflow
#define __math_divzero pb__math_divzero
#define __math_invalid pb__math_invalid
#define __exp_data pb__exp_data
#define __log_data pb__log_data
#define __pow_log_data pb__pow_log_data

/* Support non-nearest rounding mode.  */
#define WANT_ROUNDING 1
/* Support signaling NaNs.  */
#define WANT_SNAN 0
#define issignalingf_inline(x) 0
#define issignaling_inline(x) 0

#define TOINT_INTRINSICS 0

#ifdef __GNUC__
#define predict_true(x) __builtin_expect(!!(x), 1)
#define predict_false(x) __builtin_expect(x, 0)
#else
#define predict_true(x) (x)
#define predict_false(x) (x)
#endif

static inline float eval_as_float(float x)
{
	float y = x;
	return y;
}

static inline double eval_as_double(double x)
{
	double y = x;
	return y;
}

static inline float fp_barrierf(float x)
{
	volatile float y = x;
	return y;
}

static inline double fp_barrier(double x)
{
	volatile double y = x;
	return y;
}

static inline void fp_force_evalf(float x)
{
	volatile float y;
	y = x;
}

static inline void fp_force_eval(double x)
{
	volatile double y;
	y = x;
}

#define FORCE_EVAL(x) do {                        \
	if (sizeof(x) == sizeof(float)) {         \
		fp_force_evalf(x);                \
	} else {                                  \
		fp_force_eval(x);                 \
	}                                         \
} while(0)

#define asuint(f) ((union{float _f; uint32_t _i;}){f})._i
#define asfloat(i) ((union{uint32_t _i; float _f;}){i})._f
#define asuint64(f) ((union{double _f; uint64_t _i;}){f})._i
#define asdouble(i) ((union{uint64_t _i; double _f;}){i})._f

#define EXTRACT_WORDS(hi,lo,d)                    \
do {                                              \
  uint64_t __u = asuint64(d);                     \
  (hi) = __u >> 32;                               \
  (lo) = (uint32_t)__u;                           \
} while (0)

#define GET_HIGH_WORD(hi,d)                       \
do {                                              \
  (hi) = asuint64(d) >> 32;                       \
} while (0)

#define GET_LOW_WORD(lo,d)                        \
do {                                              \
  (lo) = (uint32_t)asuint64(d);                   \
} while (0)

#define INSERT_WORDS(d,hi,lo)                     \
do {                                              \
  (d) = asdouble(((uint64_t)(hi)<<32) | (uint32_t)(lo)); \
} while (0)

#define SET_HIGH_WORD(d,hi)                       \
  INSERT_WORDS(d, hi, (uint32_t)asuint64(d))

#define SET_LOW_WORD(d,lo)                        \
  INSERT_WORDS(d, asuint64(d)>>32, lo)

#define GET_FLOAT_WORD(w,d)                       \
do {                                              \
  (w) = asuint(d);                                \
} while (0)

#define SET_FLOAT_WORD(d,w)                       \
do {                                              \
  (d) = asfloat(w);                               \
} while (0)

hidden int    __rem_pio2_large(double*,double*,int,int,int);

hidden int    __rem_pio2(double,double*);
hidden double __sin(double,double,int);
hidden double __cos(double,double);
hidden double __tan(double,double,int);

hidden int    __rem_pio2f(float,double*);
hidden float  __sindf(double);
hidden float  __cosdf(double);
hidden float  __tandf(double,int);

hidden double __math_xflow(uint32_t, double);
hidden double __math_uflow(uint32_t);
hidden double __math_oflow(uint32_t);
hidden double __math_divzero(uint32_t);
hidden double __math_invalid(double);

#endif
