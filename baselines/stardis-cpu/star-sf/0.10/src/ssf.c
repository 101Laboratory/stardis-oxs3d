/* Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)
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

#include "ssf.h"

/*******************************************************************************
 * API function
 ******************************************************************************/
res_T
ssf_get_info(struct ssf_info* info)
{
  if(!info) return RES_BAD_ARG;
#ifdef SSF_USE_SIMD_128
  info->simd_128 = 1;
#else
  info->simd_128 = 0;
#endif
#ifdef SSF_USE_SIMD_256
  info->simd_256 = 1;
#else
  info->simd_256 = 0;
#endif
  return RES_OK;
}
