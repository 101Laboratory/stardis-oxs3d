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
#define REALX_DIMENSION__ 2
#include "realX_begin.h"
#include "realX.h"

static FINLINE REAL_TYPE__
REALX_FUNC__(cross)(const REAL_TYPE__ a[2], const REAL_TYPE__ b[2])
{
  ASSERT(a && b);
  return a[0]*b[1] - a[1]*b[0];
}

#include "realX_end.h"
