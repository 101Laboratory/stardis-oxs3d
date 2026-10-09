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

#ifndef SSF_FRESNEL_C_H
#define SSF_FRESNEL_C_H

#include "ssf.h"
#include <rsys/ref_count.h>

struct mem_allocator;

struct ssf_fresnel {
  struct ssf_fresnel_type type;
  void* data; /* Specific internal data of the Fresnel term */

  /* Private data */
  ref_T ref;
  struct mem_allocator* allocator;
};

#define FRESNEL_TYPE_EQ(A, B)                                                  \
  (  (A)->init == (B)->init                                                    \
  && (A)->release == (B)->release                                              \
  && (A)->eval == (B)->eval                                                    \
  && (A)->sizeof_fresnel == (B)->sizeof_fresnel                                \
  && (A)->alignof_fresnel == (B)->alignof_fresnel)

#endif /* SSF_FRESNEL_C_H */

