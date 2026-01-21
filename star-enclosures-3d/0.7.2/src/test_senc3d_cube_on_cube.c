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
 * of star-geometry. It uses output from test_sg3_cube_on_cube. */

#define _POSIX_C_SOURCE 200112L /* snprintf */

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

#include <stdio.h>

/*
         +-----------------------+
         |                       3
         |     +------+          |
      m2 | m1  | m0   2          |
         |     |      |--> N     |
         |     +------+          |
         |     | m0   1          |
         |     |      |--> N     |
         |     +------+          |
         |--> N                  |
         +-----------------------+
 */

/* Dump of star-geometry 'cube_on_cube'. */
static const unsigned cube_on_cube_vertices_count = 20;
static const double cube_on_cube_vertices[60] =
{
 1, 1, 2,
 2, 1, 2,
 1, 2, 2,
 2, 2, 2,
 1, 1, 3,
 2, 1, 3,
 1, 2, 3,
 2, 2, 3,
 1, 1, 1,
 2, 1, 1,
 1, 2, 1,
 2, 2, 1,
 0.4, 0, 0,
 4, 0, 0,
 0, 4, 0,
 4, 4, 0,
 0, 0, 4.4,
 4, 0, 4,
 0, 4, 4,
 4, 4.4, 4
};
static const unsigned cube_on_cube_triangles_count = 34;
static const unsigned cube_on_cube_triangles[102] =
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
 8, 0, 10,
 10, 0, 2,
 11, 3, 9,
 9, 3, 1,
 10, 2, 11,
 11, 2, 3,
 8, 9, 0,
 0, 9, 1,
 12, 13, 14,
 13, 15, 14,
 12, 14, 16,
 14, 18, 16,
 16, 18, 17,
 18, 19, 17,
 15, 13, 19,
 13, 17, 19,
 14, 15, 18,
 15, 19, 18,
 12, 16, 13,
 16, 17, 13
};
static const unsigned cube_on_cube_properties[102] =
{
 0, 0, 0,
 0, 0, 0,
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
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 0, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0,
 1, 2, 0
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned count, e;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* Create a scene with the cubes.
   * The enclosures in the small cubes contain medium 0, the external enclosure
   * contains medium 2, the enclosure between the small and big cubes
   * contains medium 1. */
  ctx.positions = cube_on_cube_vertices;
  ctx.indices = cube_on_cube_triangles;
  ctx.properties = cube_on_cube_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    cube_on_cube_triangles_count, get_indices, get_media_from_properties,
    cube_on_cube_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &count));
  CHK(count == 4);

  OK(senc3d_scene_get_vertices_count(scn, &count));
  CHK(count == cube_on_cube_vertices_count);

  OK(senc3d_scene_get_triangles_count(scn, &count));
  CHK(count == cube_on_cube_triangles_count);

  OK(senc3d_scene_get_enclosure_count(scn, &count));
  CHK(count == 4);
  FOR_EACH(e, 0, count) {
    struct senc3d_enclosure* enclosure;
    struct senc3d_enclosure_header header;
    unsigned m;
    char name[128]; (void)name;
    OK(senc3d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count == 1);
    OK(senc3d_enclosure_get_medium(enclosure, 0, &m));
    if(header.is_infinite) {
      CHK(m == 2); /* External */
    }
    else if(header.primitives_count == 12) {
      CHK(m == 0); /* Internal */
    } else {
      CHK(m == 1); /* In between */
    }
    OK(senc3d_enclosure_ref_put(enclosure));
#ifdef DUMP_ENCLOSURES
    snprintf(name, sizeof(name), "test_cube_on_cube_%u.obj", e);
    dump_enclosure(scn, e, name);
#endif
  }

  OK(senc3d_scene_ref_put(scn));
  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
