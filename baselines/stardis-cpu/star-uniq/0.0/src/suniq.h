/* Copyright (C) 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef SUNIQ_H
#define SUNIQ_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SUNIQ_SHARED_BUILD)
  #define SUNIQ_API extern EXPORT_SYM
#else
  #define SUNIQ_API extern IMPORT_SYM
#endif

/* Help macro which, in debug mode, stops execution when API functions fail
 * To be used when calling functions for which there is no error handling */
#ifndef NDEBUG
  #define SUNIQ(Func) ASSERT(suniq_ ## Func == RES_OK)
#else
  #define SUNIQ(Func) suniq_ ## Func
#endif

/* Forward declaration of opaque data types */
struct logger;
struct mem_allocator;

struct suniq_triangle {
  double vertices[3][3];
};
#define SUNIQ_TRIANGLE_NULL__ {{{0,0,0},{0,0,0},{0,0,0}}}
static const struct suniq_triangle SUNIQ_TRIANGLE_NULL = SUNIQ_TRIANGLE_NULL__;

struct suniq_desc {
  const size_t* indices;
  const double* positions;
  size_t ntriangles;
  size_t nvertices;
};
#define SUNIQ_DESC_NULL__ {NULL,NULL,0,0}
static const struct suniq_desc SUNIQ_DESC_NULL = SUNIQ_DESC_NULL__;

/* Forward declaration of opaque data types */
struct suniq;

BEGIN_DECLS

SUNIQ_API res_T
suniq_create
  (struct mem_allocator* allocator, /* NULL <=> use default allocator */
   struct suniq** suniq);

SUNIQ_API res_T
suniq_ref_get
  (struct suniq* suniq);

SUNIQ_API res_T
suniq_ref_put
  (struct suniq* suniq);

SUNIQ_API res_T
suniq_register_triangle
  (struct suniq* suniq,
   const struct suniq_triangle* triangle,
   size_t* id); /* NULL <=> do not return the triangle identifier */

SUNIQ_API res_T
suniq_get_triangle
  (const struct suniq* suniq,
   const size_t id,
   struct suniq_triangle* triangle);

SUNIQ_API res_T
suniq_get_desc
  (const struct suniq* suniq,
   struct suniq_desc* desc);

SUNIQ_API res_T
suniq_desc_get_vertex
  (const struct suniq_desc* desc,
   const size_t ivertex,
   double vertex[3]);

SUNIQ_API res_T
suniq_desc_get_triangle_indices
  (const struct suniq_desc* desc,
   const size_t itriangle,
   size_t indices[3]);

SUNIQ_API res_T
suniq_desc_get_triangle
  (const struct suniq_desc* desc,
   const size_t itriangle,
   struct suniq_triangle* triangle);

SUNIQ_API res_T
suniq_clear
  (struct suniq* suniq);

END_DECLS

#endif /* SUNIQ_H */
