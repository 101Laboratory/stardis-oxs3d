/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#ifndef RSYS_MATH_H
#define RSYS_MATH_H

#define _USE_MATH_DEFINES
#include <math.h>
#include <float.h>

#include "rsys.h"




//double fabs(double);
//double sin(double);
//double cos(double);
//double sqrt(double);
//double log(double);
//double exp(double);
//double pow(double);
//
//float fabsf(float);
//float sinf(float);
//float cosf(float);
//float sqrtf(float);
//float expf(float);
//float logf(float);
//float powf(float);

//#ifdef _MSC_VER
///* ---------------- MSVC ---------------- */
///* MSVC float �汾ͨ�������ڣ��� double cast */
//static inline float fsqrtf(float x) { return (float)sqrt(x); }
//static inline float flogf(float x) { return (float)log(x); }
//static inline float fexpf(float x) { return (float)exp(x); }
//static inline float fpowf(float x, float y) { return (float)pow(x, y); }
//static inline float fsinf(float x) { return (float)sin(x); }
//static inline float fcosf(float x) { return (float)cos(x); }
//static inline float ftanf(float x) { return (float)tan(x); }
//static inline float fsinhf(float x) { return (float)sinh(x); }
//static inline float ftanhf(float x) { return (float)tanh(x); }
//static inline float fremainderf(float x, float y) { return (float)remainder(x, y); }
//static inline float fremquof(float x, float y, int* quo) { return (float)remquo(x, y, quo); }
//static inline float frintf(float x) { return (float)rint(x); }
//static inline float fscalbnf(float x, int n) { return (float)scalbn(x, n); }
//static inline float fscalblnf(float x, long n) { return (float)scalbln(x, n); }
//static inline float ftgammaf(float x) { return (float)tgamma(x); }
//
///* long double -> double cast */
//#define facosl(x)  acos((double)(x))
//#define facoshl(x) acosh((double)(x))
//#define fasinl(x)  asin((double)(x))
//#define fasinhl(x) asinh((double)(x))
//#define fatanl(x)  atan((double)(x))
//
//#else
///* ---------------- GCC/Clang (C99/C11) ---------------- */
//#define fsqrtf(x) sqrtf(x)
//#define flogf(x)  logf(x)
//#define fexpf(x)  expf(x)
//#define fpowf(x,y) powf(x,y)
//#define fsinf(x)  sinf(x)
//#define fcosf(x)  cosf(x)
//#define ftanf(x)  tanf(x)
//#define fsinhf(x) sinhf(x)
//#define ftanhf(x) tanhf(x)
//#define fremainderf(x,y) remainderf(x,y)
//#define fremquof(x,y,quo) remquof(x,y,quo)
//#define frintf(x) rintf(x)
//#define fscalbnf(x,n) scalbnf(x,n)
//#define fscalblnf(x,n) scalblnf(x,n)
//#define ftgammaf(x) tgammaf(x)
//
///* long double �汾 */
//#define facosl(x)  acosl(x)
//#define facoshl(x) acoshl(x)
//#define fasinl(x)  asinl(x)
//#define fasinhl(x) asinhl(x)
//#define fatanl(x)  atanl(x)
//
//#endif /* _MSC_VER */


#define MMAX(A, B) ((A) > (B) ? (A) : (B))
#define MMIN(A, B) ((A) < (B) ? (A) : (B))
#define CLAMP(A, Min, Max) MMIN(MMAX(Min, A), Max)
#define IS_POW2(A) (((A) & ((A)-1)) == 0 && (A) > 0)
#define INF (DBL_MAX + DBL_MAX)
#define NaN (-(INF*0))
#define IS_INF(X) ((X==INF) || (X==-INF))
#define IS_NaN(X) (!((X)==(X)))
#define PI 3.14159265358979323846
#define RCP_PI 0.31830988618379067154	/* 1/pi */
#define SQRT2 1.41421356237309504880 /* sqrt(2) */
#define MDEG2RAD(Deg) ((Deg)*PI/180.0)
#define MRAD2DEG(Rad) ((Rad)*180.0/PI)

static FINLINE size_t
round_up_pow2(const size_t i)
{
  if(IS_POW2(i)) {
    return i;
  } else if(!i) {
    return 1;
  } else {
    size_t j = i - 1;
    unsigned k;
    for(k = 1; k < sizeof(int)*8; k <<= 1)
      j |= j >> k;
    return j + 1;
  }
}

static FINLINE int
log2i(const int i)
{
  union { float f; int32_t i; } ucast;
  ASSERT(i != 0);
  ucast.f = (float)i;
  return ((ucast.i>>23/*#bits mantissa*/) & ((1<<8/*#bits exponent*/)-1)) - 127;
}

static NOINLINE float
absf(const float flt)
{
  union { float f; int32_t i; } ucast;
  ucast.f = flt;
  ucast.i &= 0x7FFFFFFF;
  return ucast.f;
}

static FINLINE float
signf(const float flt)
{
  return flt < 0.f ? -1.f : 1.f;
}

static FINLINE double
sign(const double dbl)
{
  return dbl < 0.0 ? -1.0 : 1.0;
}

static FINLINE char
eq_eps(const double a, const double b, const double eps)
{
  return fabs(a - b) <= eps;
}

static FINLINE char
eq_epsf(const float a, const float b, const float eps)
{
  return absf(a - b) <= eps;
}

static FINLINE double
sin2cos(const double d)
{
  return sqrt(MMAX(0.0, 1.0 - d*d));
}

static FINLINE double
cos2sin(const double d)
{
  return sin2cos(d);
}

#endif
