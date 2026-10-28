#ifndef PB_MATH_H
#define PB_MATH_H

/*
 * N91: one libm for the game on every platform. In the game's C (dol, the RELs, aurora's mtx) the transcendental
 * functions go to the bundled copy of musl's math in src/port/math (pb_sin, ...) instead of the platform's libm:
 * glibc, musl (web), bionic, Apple's and Microsoft's differ in the last bit, which two peers in lockstep can't
 * afford. sqrt, fmod, floor, ceil, ldexp, frexp, modf, round and fabs are exact in every libm and stay the
 * platform's.
 *
 * The names are redirected after the system's <math.h> (its declarations, glibc's SIMD ones included, must see the
 * real names): with GCC/clang through the math.h next to this file (#include_next, first on those targets' include
 * path, cmake/pbmath.cmake), so a file's own feature macros (_GNU_SOURCE, ...) still come before any system header;
 * MSVC has no #include_next, there this header is forced in (/FI) and includes <math.h> itself.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#include <math.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

double pb_sin(double x);
double pb_cos(double x);
double pb_tan(double x);
double pb_asin(double x);
double pb_acos(double x);
double pb_atan(double x);
double pb_atan2(double y, double x);
double pb_exp(double x);
double pb_log(double x);
double pb_pow(double x, double y);
float pb_sinf(float x);
float pb_cosf(float x);
float pb_tanf(float x);
float pb_asinf(float x);
float pb_acosf(float x);
float pb_atanf(float x);
float pb_atan2f(float y, float x);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#define sin pb_sin
#define cos pb_cos
#define tan pb_tan
#define asin pb_asin
#define acos pb_acos
#define atan pb_atan
#define atan2 pb_atan2
#define exp pb_exp
#define log pb_log
#define pow pb_pow
#define sinf pb_sinf
#define cosf pb_cosf
#define tanf pb_tanf
#define asinf pb_asinf
#define acosf pb_acosf
#define atanf pb_atanf
#define atan2f pb_atan2f
#endif

#endif
