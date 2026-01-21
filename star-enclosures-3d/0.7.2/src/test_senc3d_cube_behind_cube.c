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
 * of star-geometry. It uses output from test_sg3_cube_behind_cube. */

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

/* Dump of star-geometry 'cube_behind_cube_2'. */
static const unsigned cube_behind_cube_2_vertices_count = 16;
static const double cube_behind_cube_2_vertices[48] =
{
 0.1, 0, 0,
 1, 0, 0,
 0, 1, 0,
 1, 1, 0,
 0, 0, 1.1,
 1, 0, 1,
 0, 1, 1,
 1, 1.1, 1,
 -1.5, -2, 20,
 3, -2, 20,
 -2, 3, 20,
 3, 3, 20,
 -2, -2, 25.5,
 3, -2, 25,
 -2, 3, 25,
 3, 3.5, 25
};
static const unsigned cube_behind_cube_2_triangles_count = 24;
static const unsigned cube_behind_cube_2_triangles[72] =
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
 8, 10, 9,
 9, 10, 11,
 8, 12, 10,
 10, 12, 14,
 12, 13, 14,
 14, 13, 15,
 11, 15, 9,
 9, 15, 13,
 10, 14, 11,
 11, 14, 15,
 8, 9, 12,
 12, 9, 13
};
static const unsigned cube_behind_cube_2_properties[72] =
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
/* Dump of star-geometry 'cube_behind_cube_3'. */
static const unsigned cube_behind_cube_3_vertices_count = 24;
static const double cube_behind_cube_3_vertices[72] =
{
 0.1, 0, 0,
 1, 0, 0,
 0, 1, 0,
 1, 1, 0,
 0, 0, 1.1,
 1, 0, 1,
 0, 1, 1,
 1, 1.1, 1,
 -1.5, -2, 20,
 3, -2, 20,
 -2, 3, 20,
 3, 3, 20,
 -2, -2, 25.5,
 3, -2, 25,
 -2, 3, 25,
 3, 3.5, 25,
 -2.3, -3, 30,
 4, -3, 30,
 -3, 4, 30,
 4, 4, 30,
 -3, -3, 37.7,
 4, -3, 37,
 -3, 4, 37,
 4, 4.7, 37
};
static const unsigned cube_behind_cube_3_triangles_count = 36;
static const unsigned cube_behind_cube_3_triangles[108] =
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
 8, 10, 9,
 9, 10, 11,
 8, 12, 10,
 10, 12, 14,
 12, 13, 14,
 14, 13, 15,
 11, 15, 9,
 9, 15, 13,
 10, 14, 11,
 11, 14, 15,
 8, 9, 12,
 12, 9, 13,
 16, 18, 17,
 17, 18, 19,
 16, 20, 18,
 18, 20, 22,
 20, 21, 22,
 22, 21, 23,
 19, 23, 17,
 17, 23, 21,
 18, 22, 19,
 19, 22, 23,
 16, 17, 20,
 20, 17, 21
};
static const unsigned cube_behind_cube_3_properties[108] =
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
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0
};

/*
              cube_2                           cube_3

                                       +-----------------------+
                                       |                       3
                                       |                       |
                                    m1 | m0                    |
                                       |                       |
                                       |                       |
                                       |                       |
                                       |                       |--> N
                                       |                       |
                                       |                       |
                                       +-----------------------+
           +------------+                   +------------+
           |            2                   |            2
        m0 | m1         |                m0 | m1         |
           |            |                   |            |
           |            |--> N              |            |--> N
           |            |                   |            |
           +------------+                   +------------+
              +-----+                          +-----+
           m0 | m1  1                       m0 | m1  1
              |     |--> N                     |     |--> N
              +-----+                          +-----+
 */

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned i, ecount, maxm;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* Create a scene with the first and second cubes.
   * Both cubes have medium 1 inside and medium 0 outside,
   * the second cube is +Z from the first cube and is big enough
   * to prevent rays from the first cube to miss it. */
  ctx.positions = cube_behind_cube_2_vertices;
  ctx.indices = cube_behind_cube_2_triangles;
  ctx.properties = cube_behind_cube_2_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    cube_behind_cube_2_triangles_count, get_indices, get_media_from_properties,
    cube_behind_cube_2_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 3);

  FOR_EACH(i, 0, ecount) {
    struct senc3d_enclosure* enclosure;
    struct senc3d_enclosure_header header;
    OK(senc3d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count == 1);
    OK(senc3d_enclosure_ref_put(enclosure));
  }

  OK(senc3d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);
  OK(senc3d_scene_ref_put(scn));

  /* Create a scene with the 3 cubes, same 2 first cubes as above
   * The third cube has medium 0 inside and medium 1 outside and is further
   * in +Z and bigger */
  ctx.positions = cube_behind_cube_3_vertices;
  ctx.indices = cube_behind_cube_3_triangles;
  ctx.properties = cube_behind_cube_3_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    cube_behind_cube_3_triangles_count, get_indices, get_media_from_properties,
    cube_behind_cube_3_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 4);

  FOR_EACH(i, 0, ecount) {
    struct senc3d_enclosure* enclosure;
    struct senc3d_enclosure_header header;
    OK(senc3d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    /* Inside enclosures contain 1 single media */
    CHK(header.enclosed_media_count == (header.is_infinite ? 2u : 1u));
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
