/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S2D_BACKEND_H
#define S2D_BACKEND_H

#include <rsys/rsys.h>

#ifdef COMPILER_CL
  /* Structure was padded due to alignment specifier */
  #pragma warning(push)
  #pragma warning(disable: 4324)
#elif defined COMPILER_GCC
  /* Disable the "ISO C restricts enumerator values to range of 'int'" compiler
   * warning in rtcore_common.h, line 293 (RTC_FEATURE_FLAG_ALL = 0xffffffff) */
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <embree4/rtcore.h>

#ifdef COMPILER_CL
  #pragma warning(pop)
#elif defined COMPILER_GCC
  #pragma GCC diagnostic pop
#endif

#endif /* S2D_BACKEND_H */

