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

#ifndef REAL_TYPE__
  #error Missing arguments
#endif

/* Generate common realXY funcs */
#define REALX_DIMENSION__ 3
#define REALY_DIMENSION__ 3
#include "realXY_begin.h"
#include "realXY.h"

/* Specific real33 funcs */
static FINLINE REAL_TYPE__*
REALXY_CTOR__
  (REAL_TYPE__* dst,
   const REAL_TYPE__ a, const REAL_TYPE__ b, const REAL_TYPE__ c,
   const REAL_TYPE__ d, const REAL_TYPE__ e, const REAL_TYPE__ f,
   const REAL_TYPE__ g, const REAL_TYPE__ h, const REAL_TYPE__ i)
{
  ASSERT(dst);
  dst[0] = a; dst[1] = b; dst[2] = c;
  dst[3] = d; dst[4] = e; dst[5] = f;
  dst[6] = g; dst[7] = h; dst[8] = i;
  return dst;
}

static FINLINE REAL_TYPE__
REALXY_FUNC__(det)(const REAL_TYPE__* mat)
{
  REAL_TYPE__ tmp[3];
  REALX_FUNC__(cross)
    (tmp, REALXY_FUNC__(col_cptr)(mat, 0), REALXY_FUNC__(col_cptr)(mat, 1));
  return REALX_FUNC__(dot)(REALXY_FUNC__(col_cptr)(mat, 2), tmp);
}

static FINLINE REAL_TYPE__
REALXY_FUNC__(invtrans)(REAL_TYPE__* dst, const REAL_TYPE__* src)
{
  REAL_TYPE__ m33[9];
  REAL_TYPE__ det;
  REALX_FUNC__(cross)
    (REALXY_FUNC__(col_ptr)(m33, 0),
     REALXY_FUNC__(col_cptr)(src, 1),
     REALXY_FUNC__(col_cptr)(src, 2));
  REALX_FUNC__(cross)
    (REALXY_FUNC__(col_ptr)(m33, 1),
     REALXY_FUNC__(col_cptr)(src, 2),
     REALXY_FUNC__(col_cptr)(src, 0));
  REALX_FUNC__(cross)
    (REALXY_FUNC__(col_ptr)(m33, 2),
     REALXY_FUNC__(col_cptr)(src, 0),
     REALXY_FUNC__(col_cptr)(src, 1));

  det = REALX_FUNC__(dot)
    (REALXY_FUNC__(col_cptr)(m33, 2),
     REALXY_FUNC__(col_cptr)(src, 2));
  REALXY_FUNC__(mul)(dst, m33, 1.f / det);
  return det;
}

static FINLINE REAL_TYPE__
REALXY_FUNC__(inverse)(REAL_TYPE__* dst, const REAL_TYPE__* src)
{
  REAL_TYPE__ m33[9];
  const REAL_TYPE__ det = REALXY_FUNC__(invtrans)(m33, src);
  REALXY_FUNC__(transpose)(dst, m33);
  return det;
}

static INLINE REAL_TYPE__*
REALXY_FUNC__(rotation) /* XYZ norm */
  (REAL_TYPE__* dst,
   /* In radian */
   const REAL_TYPE__ pitch,
   const REAL_TYPE__ yaw,
   const REAL_TYPE__ roll)
{
  const REAL_TYPE__ c1 = (REAL_TYPE__)cos((double)pitch);
  const REAL_TYPE__ c2 = (REAL_TYPE__)cos((double)yaw);
  const REAL_TYPE__ c3 = (REAL_TYPE__)cos((double)roll);
  const REAL_TYPE__ s1 = (REAL_TYPE__)sin((double)pitch);
  const REAL_TYPE__ s2 = (REAL_TYPE__)sin((double)yaw);
  const REAL_TYPE__ s3 = (REAL_TYPE__)sin((double)roll);
  ASSERT(dst);
  dst[0] = c2*c3; dst[1] = c1*s3 + c3*s1*s2; dst[2] = s1*s3 - c1*c3*s2;
  dst[3] =-c2*s3; dst[4] = c1*c3 - s1*s2*s3; dst[5] = c1*s2*s3 + c3*s1;
  dst[6] = s2;    dst[7] =-c2*s1;            dst[8] = c1*c2;
  return dst;
}

static INLINE REAL_TYPE__*
REALXY_FUNC__(rotation_axis_angle)
  (REAL_TYPE__* dst,
   const REAL_TYPE__ axis[3], /* Should be normalized */
   const REAL_TYPE__ angle) /* In radian */
{
  const REAL_TYPE__ c = (REAL_TYPE__)cos((double)angle);
  const REAL_TYPE__ s = (REAL_TYPE__)sin((double)angle);
  const REAL_TYPE__ C = 1 - c;
  ASSERT(dst && axis && REALX_FUNC__(is_normalized)(axis));

  dst[0] = axis[0] * axis[0] * C + c;
  dst[1] = axis[0] * axis[1] * C + s * axis[2];
  dst[2] = axis[0] * axis[2] * C - s * axis[1];

  dst[3] = axis[1] * axis[0] * C - s * axis[2];
  dst[4] = axis[1] * axis[1] * C + c;
  dst[5] = axis[1] * axis[2] * C + s * axis[0];

  dst[6] = axis[2] * axis[0] * C + s * axis[1];
  dst[7] = axis[2] * axis[1] * C - s * axis[0];
  dst[8] = axis[2] * axis[2] * C + c;
  return dst;
}

static INLINE REAL_TYPE__*
REALXY_FUNC__(rotation_pitch)
  (REAL_TYPE__ dst[9], const REAL_TYPE__ pitch/* in radian */)
{
  const REAL_TYPE__ c = (REAL_TYPE__)cos((double)pitch);
  const REAL_TYPE__ s = (REAL_TYPE__)sin((double)pitch);
  ASSERT(dst);
  dst[0] = 1.f; dst[1] = 0.f; dst[2] = 0.f;
  dst[3] = 0.f; dst[4] = c;   dst[5] = s;
  dst[6] = 0.f; dst[7] =-s;   dst[8] = c;
  return dst;
}

static INLINE REAL_TYPE__*
REALXY_FUNC__(rotation_yaw)
  (REAL_TYPE__ dst[9], const REAL_TYPE__ yaw/* in radian */)
{
  const REAL_TYPE__ c = (REAL_TYPE__)cos((double)yaw);
  const REAL_TYPE__ s = (REAL_TYPE__)sin((double)yaw);
  ASSERT(dst);
  dst[0] = c;   dst[1] = 0.f; dst[2] =-s;
  dst[3] = 0.f; dst[4] = 1.f; dst[5] = 0.f;
  dst[6] = s;   dst[7] = 0.f; dst[8] = c;
  return dst;
}

static INLINE REAL_TYPE__*
REALXY_FUNC__(rotation_roll)
  (REAL_TYPE__ dst[9], const REAL_TYPE__ roll/* in radian */)
{
  const REAL_TYPE__ c = (REAL_TYPE__)cos((double)roll);
  const REAL_TYPE__ s = (REAL_TYPE__)sin((double)roll);
  ASSERT(dst);
  dst[0] = c;   dst[1] = s;   dst[2] = 0.f;
  dst[3] =-s;   dst[4] = c;   dst[5] = 0.f;
  dst[6] = 0.f; dst[7] = 0.f; dst[8] = 1.f;
  return dst;
}

static INLINE REAL_TYPE__*
REALXY_FUNC__(basis)(REAL_TYPE__ dst[9], const REAL_TYPE__ N[3])
{
  REAL_TYPE__ a[3], b[3], x[3], y[3], normal[3], len;
  ASSERT(N && REALX_FUNC__(is_normalized(N)));
  REALX_FUNC__(set)(normal, N);
  a[0] = 1.f, a[1] = 0.f, a[2] = 0.f;
  b[0] = 0.f, b[1] = 1.f, b[2] = 0.f;
  REALX_FUNC__(cross)(a, a, normal);
  REALX_FUNC__(cross)(b, b, normal);
  len = REALX_FUNC__(normalize)
    (x, REALX_FUNC__(dot)(a, a) > REALX_FUNC__(dot)(b, b) ? a : b);
  if(len <= 0.f) {
    ASSERT(0 && "Degenerated normal");
    return REALXY_FUNC__(splat)(dst, 0.f);
  }
  len = REALX_FUNC__(normalize)(y, REALX_FUNC__(cross)(y, normal, x));
  ASSERT(len > 0);
  REALX_FUNC__(set)(dst + 0, x);
  REALX_FUNC__(set)(dst + 3, y);
  REALX_FUNC__(set)(dst + 6, normal);
  return dst;
}

#include "realXY_end.h"
