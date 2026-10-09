/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "sg3d.h"
#include "test_sg3d_utils.h"
#include "test_sg3d_utils2.h"

#include <rsys/double3.h>

#include <star/s3dut.h>

#include <stdio.h>
#include <limits.h>

#define NB_CYL_X 4
#define NB_CYL_Y 4
#define NB_CYL_Z 4
#define NB_CYL (NB_CYL_X * NB_CYL_Y * NB_CYL_Z)

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sg3d_device* dev;
  struct sg3d_geometry* geom;
  struct sg3d_geometry_add_callbacks callbacks = SG3D_ADD_CALLBACKS_NULL__;
  unsigned cyl_trg_count, cyl_vrtx_count, count;
  int i, j, k;
  unsigned m_in, m_out, itf = 0;
  struct s3dut_context ctx = { {NULL,NULL,0,0}, CONTEXT_NULL__ };
  struct s3dut_mesh* cyl = NULL;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sg3d_device_create(NULL, &allocator, 1, &dev));
  OK(sg3d_geometry_create(dev, &geom));
  SG3D(device_ref_put(dev));

  callbacks.get_indices = get_s3dut_indices;
  callbacks.get_properties = get_s3dut_properties;
  callbacks.get_position = get_s3dut_position;

  ctx.ctx.positions = box_vertices;
  ctx.ctx.indices = cube_indices;
  ctx.ctx.front_media = &m_in;
  ctx.ctx.back_media = &m_out;
  ctx.ctx.intface = &itf;

  /* A 20 triangles 12 vertices cylinder template */
  S3DUT(create_cylinder(&allocator, 1, 1, 5, 1, &cyl));
  S3DUT(mesh_get_data(cyl, &ctx.data));
  ASSERT(ctx.data.nprimitives <= UINT_MAX);
  ASSERT(ctx.data.nvertices <= UINT_MAX);
  cyl_trg_count = (unsigned)ctx.data.nprimitives;
  cyl_vrtx_count = (unsigned)ctx.data.nvertices;
  OK(sg3d_geometry_reserve(geom, NB_CYL * cyl_vrtx_count, NB_CYL * cyl_trg_count, 0));
  FOR_EACH(i, 0, NB_CYL_X) {
    double center_x = 2 * (1 + NB_CYL_Z) * (i - NB_CYL_X / 2);
    FOR_EACH(j, 0, NB_CYL_Y) {
      double misalignment = 0;
      FOR_EACH(k, 0, NB_CYL_Z) {
        double center_y = 2 * (1 + NB_CYL_Z) * (j - NB_CYL_Y / 2);
        m_in = (unsigned)k;
        m_out = (unsigned)(k + 1);
        d3_splat(ctx.ctx.scale, k + 1);
#ifdef MITIGATE_EMBREE_181
        /* Mitigate Embree issue #181
         * We cannot keep perfect alignment of cylinders
         * or some hits are missed in some raytracing tasks */
        misalignment = (k % 2) ? -0.01 : +0.01;
#endif
        d3(ctx.ctx.offset, center_x + misalignment, center_y + misalignment, 0);
        OK(sg3d_geometry_add(geom, cyl_vrtx_count, cyl_trg_count, &callbacks, &ctx));
      }
    }
  }
  S3DUT(mesh_ref_put(cyl));

  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "some_enclosures",
    SG3D_C_DUMP_CONST | SG3D_C_DUMP_STATIC));

  SG3D(geometry_ref_put(geom));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
