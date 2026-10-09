/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S3D_MESH_H
#define S3D_MESH_H

#include "s3d_c.h"

#include "s3d_buffer.h"
#include "s3d_geometry.h"

#include <rsys/dynamic_array_u32.h>
#include <rsys/dynamic_array_float.h>
#include <rsys/ref_count.h>

/* Generate the index buffer data type */
#define BUFFER_NAME index_buffer
#define BUFFER_DARRAY darray_u32
#include "s3d_buffer.h"

/* Generate the vertex buffer data type */
#define BUFFER_NAME vertex_buffer
#define BUFFER_DARRAY darray_float
#include "s3d_buffer.h"

struct mesh { /* Triangular mesh */
  struct index_buffer* indices;
  struct vertex_buffer* attribs[S3D_ATTRIBS_COUNT__];
  enum s3d_type attribs_type[S3D_ATTRIBS_COUNT__];
  struct darray_float cdf;
  struct hit_filter filter;

  struct s3d_device* dev;
  ref_T ref;
};

extern LOCAL_SYM res_T
mesh_create
  (struct s3d_device* dev,
   struct mesh** mesh);

extern LOCAL_SYM void
mesh_ref_get
  (struct mesh* mesh);

extern LOCAL_SYM void
mesh_ref_put
  (struct mesh* mesh);

extern LOCAL_SYM void
mesh_clear
  (struct mesh* mesh);

extern LOCAL_SYM size_t
mesh_get_ntris
  (const struct mesh* mesh);

extern LOCAL_SYM size_t
mesh_get_nverts
  (const struct mesh* mesh);

extern LOCAL_SYM uint32_t*
mesh_get_ids
  (struct mesh* mesh);

extern LOCAL_SYM float*
mesh_get_pos
  (struct mesh* mesh);

extern LOCAL_SYM float*
mesh_get_attr
  (struct mesh* mesh,
   const enum s3d_attrib_usage usage);

extern LOCAL_SYM float
mesh_compute_area
  (struct mesh* mesh);

/* Compute mesh CDF */
extern LOCAL_SYM res_T
mesh_compute_cdf
  (struct mesh* mesh);

extern LOCAL_SYM float
mesh_compute_volume
  (struct mesh* mesh,
   const char flip_surface); /* Flip the shape surface or not */

extern LOCAL_SYM res_T
mesh_setup_indexed_vertices
  (struct mesh* mesh,
   const unsigned ntris,
   void (*get_indices)(const unsigned itri, unsigned ids[3], void* ctx),
   const unsigned nverts,
   struct s3d_vertex_data attribs[],
   const unsigned nattribs,
   void* data);

extern LOCAL_SYM void
mesh_compute_aabb
  (struct mesh* mesh,
   float lower[3],
   float upper[3]);

extern LOCAL_SYM void
mesh_copy_indexed_vertices
  (const struct mesh* src,
   struct mesh* dst);

#endif /* S3D_MESH_H */

