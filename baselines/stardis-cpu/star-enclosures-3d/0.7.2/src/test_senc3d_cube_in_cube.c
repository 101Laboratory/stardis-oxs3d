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

/* This test has been created using the sg3_geometry_dump_as_C_code feature
 * of star-geometry. It uses output from test_sg3_cube_in_cube. */

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

/*
             cube_2                             cube_3

                                       +-------------------------+
                                       |                         B
         +-------------+               |     +-------------+     |
      m1 |     m0      M            m1 |  m1 |      m0     M     |
         | +------+    |               | m0  | +------+    |     |
         | |  m1  S    |               |     | |  m1  S    |     |
         | | N <--|    |               |     | | N <--|    |     |
         | +------+    |               |     | +------+    |     |
         |        N <--|               |     |        N <--|     |
         +-------------+               |     +-------------+     |
                                       |                    N <--|
                                       +-------------------------+
 */


/* Dump of star-geometry 'cube_in_cube_2'. */
static const unsigned cube_in_cube_2_vertices_count = 16;
static const double cube_in_cube_2_vertices[48] =
{
 0.1, 0, 0,
 1, 0, 0,
 0, 1, 0,
 1, 1, 0,
 0, 0, 1.1,
 1, 0, 1,
 0, 1, 1,
 1, 1.1, 1,
 -0.7, -1, -1,
 2, -1, -1,
 -1, 2, -1,
 2, 2, -1,
 -1, -1, 2.3,
 2, -1, 2,
 -1, 2, 2,
 2, 2.3, 2
};
static const unsigned cube_in_cube_2_triangles_count = 24;
static const unsigned cube_in_cube_2_triangles[72] =
{
 0, 2, 1,
 1, 2, 3,
 0, 4, 2,
 2, 4, 6,
 4, 5, 6,
 6, 5, 7,
 3, 7, 1,
 1, 7, 5,
 2, 6, 3,
 3, 6, 7,
 0, 1, 4,
 4, 1, 5,
 8, 9, 10,
 9, 11, 10,
 8, 10, 12,
 10, 14, 12,
 12, 14, 13,
 14, 15, 13,
 11, 9, 15,
 9, 13, 15,
 10, 11, 14,
 11, 15, 14,
 8, 12, 9,
 12, 13, 9
};
static const unsigned cube_in_cube_2_properties[72] =
{
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0
};
/* Dump of star-geometry 'cube_in_cube_3'. */
static const unsigned cube_in_cube_3_vertices_count = 24;
static const double cube_in_cube_3_vertices[72] =
{
 0.1, 0, 0,
 1, 0, 0,
 0, 1, 0,
 1, 1, 0,
 0, 0, 1.1,
 1, 0, 1,
 0, 1, 1,
 1, 1.1, 1,
 -0.7, -1, -1,
 2, -1, -1,
 -1, 2, -1,
 2, 2, -1,
 -1, -1, 2.3,
 2, -1, 2,
 -1, 2, 2,
 2, 2.3, 2,
 -3, -4, -4,
 6, -4, -4,
 -4, 6, -4,
 6, 6, -4,
 -4, -4, 7,
 6, -4, 6,
 -4, 6, 6,
 6, 7, 6
};
static const unsigned cube_in_cube_3_triangles_count = 36;
static const unsigned cube_in_cube_3_triangles[108] =
{
 0, 2, 1,
 1, 2, 3,
 0, 4, 2,
 2, 4, 6,
 4, 5, 6,
 6, 5, 7,
 3, 7, 1,
 1, 7, 5,
 2, 6, 3,
 3, 6, 7,
 0, 1, 4,
 4, 1, 5,
 8, 9, 10,
 9, 11, 10,
 8, 10, 12,
 10, 14, 12,
 12, 14, 13,
 14, 15, 13,
 11, 9, 15,
 9, 13, 15,
 10, 11, 14,
 11, 15, 14,
 8, 12, 9,
 12, 13, 9,
 16, 17, 18,
 17, 19, 18,
 16, 18, 20,
 18, 22, 20,
 20, 22, 21,
 22, 23, 21,
 19, 17, 23,
 17, 21, 23,
 18, 19, 22,
 19, 23, 22,
 16, 20, 17,
 20, 21, 17
};
static const unsigned cube_in_cube_3_properties[108] =
{
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0,
 0, 1, 0
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned e, ecount, maxm, e2;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* Create a scene with the first and second cubes.
   * The enclosure in the small contains medium 1, the external enclosure
   * contains medium 1, the enclosure between the small and medium cubes
   * contains medium 0. */
  ctx.positions = cube_in_cube_2_vertices;
  ctx.indices = cube_in_cube_2_triangles;
  ctx.properties = cube_in_cube_2_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    cube_in_cube_2_triangles_count, get_indices, get_media_from_properties,
    cube_in_cube_2_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 3);

  FOR_EACH(e, 0, ecount) {
    struct senc3d_enclosure* enclosure;
    struct senc3d_enclosure_header header;
    unsigned m;
    OK(senc3d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count == 1);
    OK(senc3d_enclosure_get_medium(enclosure, 0, &m));
    CHK(m <= 1);
    CHK((m == 0) == (header.primitives_count == cube_in_cube_2_triangles_count));
    OK(senc3d_enclosure_ref_put(enclosure));
  }

  OK(senc3d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);
  OK(senc3d_scene_ref_put(scn));

  /* Create a scene with the 3 cubes, same 2 first cubes as above.
   * The enclosure in the small cube contains medium 1, the external enclosure
   * contains medium 1, the enclosure between the small and medium cubes
   * contains medium 0 and the enclosure between the medium and big cubes
   * contains both media 0 and 1. */
  ctx.positions = cube_in_cube_3_vertices;
  ctx.indices = cube_in_cube_3_triangles;
  ctx.properties = cube_in_cube_3_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    cube_in_cube_3_triangles_count, get_indices, get_media_from_properties,
    cube_in_cube_3_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 4);

  e2 = ecount;
  FOR_EACH(e, 0, ecount) {
    struct senc3d_enclosure* enclosure;
    struct senc3d_enclosure_header header;
    OK(senc3d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count <= 2);
    if(header.enclosed_media_count == 2) {
      /* A single internal enclosure has 2 media */
      CHK(!header.is_infinite);
      CHK(e2 == ecount);
      e2 = e;
    }
    OK(senc3d_enclosure_ref_put(enclosure));
  }

  OK(senc3d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);

  OK(senc3d_scene_ref_put(scn));
  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
