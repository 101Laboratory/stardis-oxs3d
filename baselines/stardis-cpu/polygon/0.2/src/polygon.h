/* Copyright (C) 2014-2017, 2021-2023 Vincent Forest (vaplv@free.fr)
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

#ifndef POLYGON_H
#define POLYGON_H

#include <rsys/rsys.h>

#ifdef POLYGON_SHARED_BUILD
  #define POLYGON_API extern EXPORT_SYM
#elif defined(POLYGON_STATIC_BUILD)
  #define POLYGON_API extern LOCAL_SYM
#else
  #define POLYGON_API extern IMPORT_SYM
#endif

#ifndef NDEBUG
  #define POLYGON(Func) ASSERT(polygon_##Func == RES_OK)
#else
  #define POLYGON(Func) polygon_##Func
#endif

struct mem_allocator;
struct polygon;

BEGIN_DECLS

POLYGON_API res_T
polygon_create
  (struct mem_allocator* allocator, /* May be NULL <=> default allocator */
   struct polygon** polygon);

POLYGON_API res_T
polygon_ref_get
  (struct polygon* polygon);

POLYGON_API res_T
polygon_ref_put
  (struct polygon* polygon);

POLYGON_API res_T
polygon_clear
  (struct  polygon* polygon);

/* Append a vertex to the polygon contour. Note that this vertex may replace
 * the previous one if it is aligned with the two last ones */
POLYGON_API res_T
polygon_vertex_add
  (struct polygon* polygon,
   const float pos[3]);

POLYGON_API res_T
polygon_vertices_count_get
  (const struct polygon* polygon,
   uint32_t* nvertices);

POLYGON_API res_T
polygon_vertex_get
  (const struct polygon* polygon,
   const uint32_t ivertex,
   float position[3]);

/* This function assumes that the polygon vertices lie on the same plane and
 * that they define a unique contour that is not intersected itself */
POLYGON_API res_T
polygon_triangulate
  (struct polygon* polygon,
   const uint32_t** indices,
   uint32_t* indices_count);

END_DECLS

#endif /* POLYGON_H */

