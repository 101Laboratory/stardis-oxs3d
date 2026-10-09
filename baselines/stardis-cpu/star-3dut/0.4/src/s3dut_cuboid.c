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

#include "s3dut.h"
#include "s3dut_mesh.h"

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3dut_create_cuboid
  (struct mem_allocator* allocator,
   const double width,
   const double height,
   const double depth,
   struct s3dut_mesh** mesh)
{
  struct s3dut_mesh* cuboid = NULL;
  double x, y, z;
  double* coords;
  size_t* ids;
  size_t i;
  res_T res = RES_OK;

  if(width <= 0 || height <= 0 || depth <= 0 || !mesh) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = mesh_create(allocator, S3DUT_MESH_CUBOID, 8, 12, &cuboid);
  if(res != RES_OK) goto error;

  coords = darray_double_data_get(&cuboid->coords);
  ids = darray_size_t_data_get(&cuboid->ids);
  x = width * 0.5;
  y = height * 0.5;
  z = depth * 0.5;

  i = 0;
  coords[i++]=-x; coords[i++]=-y; coords[i++]=-z;
  coords[i++]= x; coords[i++]=-y; coords[i++]=-z;
  coords[i++]= x; coords[i++]= y; coords[i++]=-z;
  coords[i++]=-x; coords[i++]= y; coords[i++]=-z;
  coords[i++]=-x; coords[i++]=-y; coords[i++]= z;
  coords[i++]= x; coords[i++]=-y; coords[i++]= z;
  coords[i++]= x; coords[i++]= y; coords[i++]= z;
  coords[i++]=-x; coords[i++]= y; coords[i++]= z;

  i = 0;
  ids[i++]=0; ids[i++]=2; ids[i++]=3;
  ids[i++]=0; ids[i++]=1; ids[i++]=2;
  ids[i++]=0; ids[i++]=3; ids[i++]=7;
  ids[i++]=0; ids[i++]=7; ids[i++]=4;
  ids[i++]=0; ids[i++]=4; ids[i++]=1;
  ids[i++]=4; ids[i++]=5; ids[i++]=1;
  ids[i++]=5; ids[i++]=6; ids[i++]=1;
  ids[i++]=1; ids[i++]=6; ids[i++]=2;
  ids[i++]=2; ids[i++]=6; ids[i++]=7;
  ids[i++]=2; ids[i++]=7; ids[i++]=3;
  ids[i++]=7; ids[i++]=6; ids[i++]=4;
  ids[i++]=6; ids[i++]=5; ids[i++]=4;

exit:
  if(mesh) *mesh = cuboid;
  return res;
error:
  if(cuboid) {
    S3DUT(mesh_ref_put(cuboid));
    cuboid = NULL;
  }
  goto exit;
}

