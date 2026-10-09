/* Copyright (C) 2016, 2017, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S3DUT_MESH_H
#define S3DUT_MESH_H

#include <rsys/dynamic_array_double.h>
#include <rsys/dynamic_array_size_t.h>
#include <rsys/ref_count.h>

enum s3dut_mesh_type {
  S3DUT_MESH_CUBOID,
  S3DUT_MESH_CYLINDER,
  S3DUT_MESH_THICK_CYLINDER,
  S3DUT_MESH_HEMISPHERE,
  S3DUT_MESH_SPHERE,
  S3DUT_MESH_THICK_SPHERE,
  S3DUT_MESH_SUPER_SHAPE
};

struct s3dut_mesh {
  enum s3dut_mesh_type type;
  struct darray_double coords;
  struct darray_size_t ids;

  ref_T ref;
  struct mem_allocator* allocator;
};

extern LOCAL_SYM res_T
mesh_create
  (struct mem_allocator* allocator,
   const enum s3dut_mesh_type type,
   const size_t nvertices,
   const size_t ntriangles,
   struct s3dut_mesh** out_mesh);

#endif /* S3DUT_MESH_H */

