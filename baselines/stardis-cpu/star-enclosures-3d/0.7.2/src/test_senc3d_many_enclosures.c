/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

/* This test is similar to test_senc3d_some_enclosures, but involves 64*64*64
 * cylinders instead of 4*4*4, thus making it impossible to define the geometry
 * through static arrays. */

#define NB_CYL_X 32
#define NB_CYL_Y 32
#define NB_CYL_Z 16
#define NB_CYL (NB_CYL_X * NB_CYL_Y * NB_CYL_Z)

#include "senc3d.h"
#include "test_senc3d_utils.h"
#include "test_senc3d_utils2.h"

#include <star/s3dut.h>
#include <rsys/clock_time.h>
#include <rsys/double3.h>

#include <stdio.h>
#include <limits.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct s3dut_mesh* cyl = NULL;
  struct s3dut_context ctx = { {NULL,NULL,0,0}, CONTEXT_NULL__ };
  unsigned count;
  unsigned cyl_trg_count, cyl_vrtx_count, e;
  char dump[64];
  struct time t0, t1;
  (void)argc, (void)argv;

  OK(mem_init_regular_allocator(&allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* A 20 triangles 12 vertices cylinder template */
  S3DUT(create_cylinder(&allocator, 1, 1, 3, 1, &cyl));
  S3DUT(mesh_get_data(cyl, &ctx.data));
  ASSERT(ctx.data.nprimitives < UINT_MAX);
  ASSERT(ctx.data.nvertices < UINT_MAX);
  cyl_trg_count = (unsigned)ctx.data.nprimitives;
  cyl_vrtx_count = (unsigned)ctx.data.nvertices;

  /* Create the scene with N_CYL cylinders.
   * There are NB_CYL_X * NB_CYL_Y imbrications of NB_CYL_Z cylinders each.
   * Each imbrication is located on a grid.
   * The get_s3du_xxx getters have to retrieve the cylinder from the
   * primitive and vertice indexes. */
  time_current(&t0);
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    NB_CYL * cyl_trg_count, get_s3dut_indices, get_s3dut_media,
    NB_CYL * cyl_vrtx_count, get_s3dut_position, &ctx, &scn));
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_MSEC | TIME_SEC | TIME_MIN, NULL, dump, sizeof(dump));
  printf("Scene created in: %s\n", dump);
  S3DUT(mesh_ref_put(cyl));

  OK(senc3d_scene_get_vertices_count(scn, &count));
  CHK(count == NB_CYL * cyl_vrtx_count);
  OK(senc3d_scene_get_triangles_count(scn, &count));
  CHK(count == NB_CYL * cyl_trg_count);

  OK(senc3d_scene_get_enclosure_count(scn, &count));
  CHK(count == 1 + NB_CYL);
  FOR_EACH(e, 0, count) {
    struct senc3d_enclosure* enclosure;
    struct senc3d_enclosure_header header;
    unsigned m;
    OK(senc3d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count == 1);
    OK(senc3d_enclosure_get_medium(enclosure, 0, &m));
    CHK(header.primitives_count ==
      (header.is_infinite /* Outermost enclosure: NB_CYL_X*NB_CYL_Y cylinders */
        ? NB_CYL_X * NB_CYL_Y * cyl_trg_count
        : (m == 0
          ? cyl_trg_count /* Innermost enclosures: 1 cylinder */
          : 2 * cyl_trg_count))); /* Other enclosures: 2 cylinders */
    OK(senc3d_enclosure_ref_put(enclosure));
  }

  OK(senc3d_scene_ref_put(scn));
  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_regular_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
