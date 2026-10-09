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

#ifndef S2D_LINE_SEGMENTS_H
#define S2D_LINE_SEGMENTS_H

#include "s2d.h"

#include "s2d_buffer.h"

#include <rsys/dynamic_array_u32.h>
#include <rsys/dynamic_array_float.h>
#include <rsys/ref_count.h>

/* Generate the index buffer data type */
#define BUFFER_NAME index_buffer
#define BUFFER_DARRAY darray_u32
#include "s2d_buffer.h"

/* Generate the vertex buffer data type */
#define BUFFER_NAME vertex_buffer
#define BUFFER_DARRAY darray_float
#include "s2d_buffer.h"

enum buffer_type {
  INDEX_BUFFER = BIT(0),
  VERTEX_BUFFER = BIT(1)
};

/* Filter function and its associated user defined data */
struct hit_filter {
  s2d_hit_filter_function_T func;
  void* data;
};

struct line_segments { /* Segmented contour */
  struct index_buffer* indices;
  struct vertex_buffer* attribs[S2D_ATTRIBS_COUNT__];
  enum s2d_type attribs_type[S2D_ATTRIBS_COUNT__];
  struct darray_float cdf;
  struct hit_filter filter;

  int resize_mask; /* Combination of buffer_type */
  int update_mask; /* Combination of buffer_type */
  struct s2d_device* dev;
  ref_T ref;
};

extern LOCAL_SYM res_T
line_segments_create
  (struct s2d_device* dev,
   struct line_segments** lines);

extern LOCAL_SYM void
line_segments_ref_get
  (struct line_segments* lines);

extern LOCAL_SYM void
line_segments_ref_put
  (struct line_segments* lines);

extern LOCAL_SYM void
line_segments_clear
  (struct line_segments* lines);

extern LOCAL_SYM size_t
line_segments_get_nsegments
  (const struct line_segments* lines);

extern LOCAL_SYM size_t
line_segments_get_nverts
  (const struct line_segments* lines);

extern LOCAL_SYM uint32_t*
line_segments_get_ids
  (struct line_segments* lines);

extern LOCAL_SYM float*
line_segments_get_pos
  (struct line_segments* lines);

extern LOCAL_SYM float*
line_segments_get_attr
  (struct line_segments* lines,
   const enum s2d_attrib_usage usage);

/* Compute line segments Cumulative Distribution Function */
extern LOCAL_SYM res_T
line_segments_compute_cdf
  (struct line_segments* line);

extern LOCAL_SYM float
line_segments_compute_length
  (struct line_segments* lines);

extern LOCAL_SYM float
line_segments_compute_area
  (struct line_segments* lines,
   const char flip_contour);

extern LOCAL_SYM res_T
line_segments_setup_indexed_vertices
  (struct line_segments* lines,
   const unsigned ntris,
   void (*get_indices)(const unsigned isegment, unsigned ids[2], void* ctx),
   const unsigned nverts,
   struct s2d_vertex_data attribs[],
   const unsigned nattribs,
   void* data);

extern LOCAL_SYM void
line_segments_compute_aabb
  (struct line_segments* lines,
   float lower[2],
   float upper[2]);

extern LOCAL_SYM void
line_segments_copy_indexed_vertices
  (const struct line_segments* src,
   struct line_segments* dst);

#endif /* S2D_LINE_SEGMENTS_H */

