/* Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)
 * Copyright (C) 2018 Université Paul Sabatier
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef SVX_C_H
#define SVX_C_H

#include "svx.h"
#include <rsys/rsys.h>

/* Count the number of bits set to 1 */
static FINLINE int
popcount(const uint8_t x)
{
  int n = x - ((x >> 1) & 0x55);
  n = (n & 0x33) + ((n >> 2) & 0x33);
  n = (n + (n >> 4)) & 0x0f;
  return (n * 0x0101) >> 8;
}

static INLINE int
check_svx_voxel_desc(const struct svx_voxel_desc* desc)
{
  return desc
      && desc->get
      && desc->merge
      && desc->challenge_merge
      && desc->size > 0
      && desc->size <= SVX_MAX_SIZEOF_VOXEL;
}

#endif /* SVX_C_H */

