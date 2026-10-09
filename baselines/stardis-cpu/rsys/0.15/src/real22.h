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
#define REALX_DIMENSION__ 2
#define REALY_DIMENSION__ 2
#include "realXY_begin.h"
#include "realXY.h"

/* Specific real22 funcs */
static FINLINE REAL_TYPE__*
REALXY_CTOR__
  (REAL_TYPE__* dst,
   const REAL_TYPE__ a,
   const REAL_TYPE__ b,
   const REAL_TYPE__ c,
   const REAL_TYPE__ d)
{
  ASSERT(dst);
  dst[0] = a; dst[1] = b; dst[2] = c; dst[3] = d;
  return dst;
}

static FINLINE REAL_TYPE__
REALXY_FUNC__(det)(const REAL_TYPE__* mat)
{
  return mat[0] * mat[3] - mat[2] * mat[1];
}

static FINLINE REAL_TYPE__
REALXY_FUNC__(inverse)(REAL_TYPE__* dst, const REAL_TYPE__* mat)
{
  REAL_TYPE__ det, rcp_det, mat0;
  ASSERT(dst && mat);
  det = REALXY_FUNC__(det)(mat);
  rcp_det = 1.f / det;
  mat0 = mat[0];
  dst[0] =  mat[3] * rcp_det;
  dst[1] = -mat[1] * rcp_det;
  dst[2] = -mat[2] * rcp_det;
  dst[3] =  mat0   * rcp_det;
  return det;
}

static FINLINE REAL_TYPE__
REALXY_FUNC__(invtrans)(REAL_TYPE__* dst, const REAL_TYPE__* mat)
{
  REAL_TYPE__ det, dst1, dst2;
  ASSERT(dst && mat);
  det = REALXY_FUNC__(inverse)(dst, mat);
  dst1 = dst[1];
  dst2 = dst[2];
  dst[1] = dst2;
  dst[2] = dst1;
  return det;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(rotation)
  (REAL_TYPE__ dst[4], const REAL_TYPE__ angle/*in radian*/)
{
  const REAL_TYPE__ c = (REAL_TYPE__)cos((double)angle);
  const REAL_TYPE__ s = (REAL_TYPE__)sin((double)angle);
  ASSERT(dst);
  dst[0] = c; dst[1] = s;
  dst[2] =-s; dst[3] = c;
  return dst;
}

#include "realXY_end.h"
