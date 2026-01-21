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

/* Generate common realX funcs */
#define REALX_DIMENSION__ 3
#include "realX_begin.h"
#include "realX.h"

static FINLINE REAL_TYPE__*
REALX_FUNC__(cross)
  (REAL_TYPE__ dst[3],
   const REAL_TYPE__ a[3],
   const REAL_TYPE__ b[3])
{
  REAL_TYPE__ tmp[3];
  ASSERT(dst && a && b);
  tmp[0] = a[1]*b[2] - a[2]*b[1];
  tmp[1] = a[2]*b[0] - a[0]*b[2];
  tmp[2] = a[0]*b[1] - a[1]*b[0];
  return REALX_FUNC__(set)(dst, tmp);
}

#include "realX_end.h"
