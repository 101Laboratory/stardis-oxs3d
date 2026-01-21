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

/*
 * Internal header used to generate funcs on REAL_TYPE__ vector of X dimensions
 */
#include "rsys_math.h"
#include <rsys_math.h>

#ifdef COMPILER_GCC
  #pragma GCC push_options
  #pragma GCC optimize("unroll-loops")
#endif

#if REALX_DIMENSION__ <= 4
static FINLINE REAL_TYPE__*
REALX_CTOR__
  (REAL_TYPE__* dst
  ,const REAL_TYPE__ x
  ,const REAL_TYPE__ y
#if REALX_DIMENSION__ > 2
  ,const REAL_TYPE__ z
#endif
#if REALX_DIMENSION__ > 3
  ,const REAL_TYPE__ w
#endif
  )
{
  ASSERT(dst);
  dst[0] = x;
  dst[1] = y;
#if REALX_DIMENSION__ > 2
  dst[2] = z;
#endif
#if REALX_DIMENSION__ > 3
  dst[3] = w;
#endif
  return dst;
}
#endif

static FINLINE REAL_TYPE__*
REALX_CAST__(REAL_TYPE__* dst, const REAL_TYPE_COMPATIBLE__* src)
{
  int i;
  ASSERT(dst && src);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    dst[i] = (REAL_TYPE__)src[i];
  return dst;
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(splat)(REAL_TYPE__* dst, const REAL_TYPE__ val)
{
  int i;
  ASSERT(dst);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    dst[i] = val;
  return dst;
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(set__)(REAL_TYPE__* dst, const REAL_TYPE__* src)
{
  int i;
  ASSERT(dst && src);
  ASSERT(!MEM_AREA_OVERLAP(dst, SIZEOF_REALX__, src, SIZEOF_REALX__));
  FOR_EACH(i, 0, REALX_DIMENSION__)
    dst[i] = src[i];
  return dst;
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(set)(REAL_TYPE__* dst, const REAL_TYPE__* src)
{
  ASSERT(dst && src);
  if(!MEM_AREA_OVERLAP(dst, SIZEOF_REALX__, src, SIZEOF_REALX__)) {
    return REALX_FUNC__(set__)(dst, src);
  } else {
    REAL_TYPE__ tmp[REALX_DIMENSION__];
    return REALX_FUNC__(set__)(dst, REALX_FUNC__(set__)(tmp, src));
  }
}

static FINLINE REAL_TYPE__
REALX_FUNC__(dot)(const REAL_TYPE__* a, const REAL_TYPE__* b)
{
  REAL_TYPE__ dot = 0;
  int i;
  ASSERT(a && b);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    dot += a[i] * b[i];
  return dot;
}

static FINLINE REAL_TYPE__
REALX_FUNC__(len)(const REAL_TYPE__* a)
{
  ASSERT(a);
  return (REAL_TYPE__)sqrt((double)REALX_FUNC__(dot)(a, a));
}

static FINLINE REAL_TYPE__
REALX_FUNC__(normalize)(REAL_TYPE__* dst, const REAL_TYPE__* a)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  REAL_TYPE__ len, rcp_len;
  int i;
  ASSERT(dst && a);

  len = REALX_FUNC__(len)(a);
  if(len == 0)
    return len;

  rcp_len = 1.0f / len;
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] * rcp_len;
  REALX_FUNC__(set__)(dst, tmp);
  return len;
}

static FINLINE int
REALX_FUNC__(is_normalized)(const REAL_TYPE__* a)
{
  return REAL_EQ_EPS__(REALX_FUNC__(len)(a), (REAL_TYPE__)1.0, REAL_EPSILON__);
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(add)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a && b);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] + b[i];
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_REAL_FUNC__(add)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__ f)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] + f;
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(sub)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a && b);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] - b[i];
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_REAL_FUNC__(sub)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__ f)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] - f;
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(mul)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a && b);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] * b[i];
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_REAL_FUNC__(mul)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__ f)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] * f;
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(div)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a && b);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] / b[i];
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_REAL_FUNC__(div)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__ f)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] / f;
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(minus)(REAL_TYPE__* dst, const REAL_TYPE__* a)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = -a[i];
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__
REALX_FUNC__(sum)(const REAL_TYPE__* a)
{
  REAL_TYPE__ f = 0;
  int i = 0;
  ASSERT(a);
  do {
    f += a[i];
    ++i;
  } while(i < REALX_DIMENSION__);
  return f;
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(lerp)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* from,
   const REAL_TYPE__* to,
   const REAL_TYPE__ t)
{
  ASSERT(dst && from && to);
  if(t <= 0.f) {
    REALX_FUNC__(set)(dst, from);
  } else if(t >= 1) {
    REALX_FUNC__(set)(dst, to);
  } else {
    REAL_TYPE__ tmp[REALX_DIMENSION__];
    int i;
    FOR_EACH(i, 0, REALX_DIMENSION__)
      tmp[i] = from[i] + t * (to[i] - from[i]);
    REALX_FUNC__(set__)(dst, tmp);
  }
  return dst;
}

static FINLINE int
REALX_FUNC__(eq)(const REAL_TYPE__* a, const REAL_TYPE__* b)
{
  int i = 0;
  int is_eq = 1;
  ASSERT(a && b);
  do {
    is_eq = a[i] == b[i];
    ++i;
  } while(i < REALX_DIMENSION__  && is_eq);
  return is_eq;
}

static FINLINE int
REALX_FUNC__(eq_eps)
  (const REAL_TYPE__* a,
   const REAL_TYPE__* b,
   const REAL_TYPE__ eps)
{
  int i = 0;
  int is_eq = 1;
  ASSERT(a && b);
  do {
    is_eq = REAL_EQ_EPS__(a[i], b[i], eps);
    ++i;
  } while(i < REALX_DIMENSION__ && is_eq);
  return is_eq;
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(max)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a && b);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] > b[i] ? a[i] : b[i];
  return REALX_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALX_FUNC__(min)
  (REAL_TYPE__* dst,
   const REAL_TYPE__* a,
   const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int i;
  ASSERT(dst && a && b);
  FOR_EACH(i, 0, REALX_DIMENSION__)
    tmp[i] = a[i] < b[i] ? a[i] : b[i];
  return REALX_FUNC__(set__)(dst, tmp);
}

#ifdef COMPILER_GCC
  #pragma GCC pop_options
#endif
