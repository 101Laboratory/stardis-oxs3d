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
 * of star-geometry. It uses a star-cad scene that crashed. */

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

#include <stdio.h>

/*
 *    +-------------+
 *    |             |
 *    |      +      |
 *    |     /|\     |
 *    |    / | \    |
 *    |   +  |  +   |
 *    |   |  |  |   |
 *    |   |  |  |   |
 *    |   +--+--+   |
 *    |             |
 *    +-------------+
*/

static const unsigned bad3_vertices_count = 24;
static const double bad3_vertices[72] =
{
    0, 10, 10,
    0, 0, 0,
    0, 0, 10,
    0, 10, 0,
    10, 10, 10,
    10, 0, 10,
    10, 0, 0,
    10, 10, 0,

    1, 5, 5,
    1, 1, 1,
    1, 1, 5,
    1, 5, 1,
    5, 5, 6,
    5, 1, 6,
    5, 1, 1,
    5, 5, 1,

    5, 5, 6.12, /* Duplicate, use #12 instead */
    5, 1, 1.14, /* Duplicate, use #14 instead */
    5, 1, 6.13, /* Duplicate, use #13 instead */
    5, 5, 1.15, /* Duplicate, use #15 instead */
    9, 5, 5,
    9, 1, 5,
    9, 1, 1,
    9, 5, 1
};
unsigned bad3_triangles_count = 34;
unsigned bad3_triangles[582] =
{
  0, 1, 2,
  0, 3, 1,
  4, 5, 6,
  4, 6, 7,
  6, 2, 1,
  5, 2, 6,
  7, 3, 0,
  4, 7, 0,
  3, 6, 1,
  7, 6, 3,
  0, 2, 5,
  4, 0, 5,

  8, 9, 10,
  8, 11, 9,
  12, 13, 14,
  12, 14, 15,
  14, 10, 9,
  13, 10, 14,
  15, 11, 8,
  12, 15, 8,
  11, 14, 9,
  15, 14, 11,
  8, 10, 13,
  12, 8, 13,

  /* 12, 14, 13, */ /* Duplicate */
  /* 12, 15, 14, */ /* Duplicate */
  20, 21, 22,
  20, 22, 23,
  22, 13, 14,
  21, 13, 22,
  23, 15, 12,
  20, 23, 12,
  15, 22, 14,
  23, 22, 15,
  12, 13, 21,
  20, 12, 21
};
unsigned bad3_properties[102] =
{
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   /* SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, */
   /* SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, */
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM,
   SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM, SENC3D_UNSPECIFIED_MEDIUM
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned count, e;
  const double volumes[] = { -1000, 856, 72, 72};
  int volume_used[] = {0, 0, 0, 0};
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* Create a scene with the cubes.
   * The enclosures in the small cubes contain medium 0, the external enclosure
   * contains medium 2, the enclosure between the small and big cubes
   * contains medium 1. */
  ctx.positions = bad3_vertices;
  ctx.indices = bad3_triangles;
  ctx.properties = bad3_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_OUTSIDE,
    bad3_triangles_count, get_indices, get_media_from_properties,
    bad3_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_vertices_count(scn, &count));
  CHK(count == bad3_vertices_count);

  OK(senc3d_scene_get_triangles_count(scn, &count));
  CHK(count == bad3_triangles_count);

  OK(senc3d_scene_get_enclosure_count(scn, &count));
  CHK(count == 4);
  FOR_EACH(e, 0, count) {
    struct senc3d_enclosure* enclosure;
    struct senc3d_enclosure_header header;
    size_t i;
    int found = 0;
    OK(senc3d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    OK(senc3d_enclosure_ref_put(enclosure));
    for(i = 0; i < sizeof(volumes)/sizeof(*volumes); i++) {
      if(!volume_used[i] && fabs(volumes[i] - header.volume) < DBL_EPSILON) {
        volume_used[i] = 1;
        found = 1;
        break;
      }
    }
    if(!found) return 1;
  }

  OK(senc3d_scene_ref_put(scn));
  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
