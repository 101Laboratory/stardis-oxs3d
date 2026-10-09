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

 /* Dump of star-geometry-3d 'multi_media'. */
static const unsigned multi_media_vertices_count = 8;
static const double multi_media_vertices[24] =
{
   0, 0, 0,
   1, 0, 0,
   0, 1, 0,
   1, 1, 0,
   0, 0, 1,
   1, 0, 1,
   0, 1, 1,
   1, 1, 1
};
static const unsigned multi_media_triangles_count = 12;
static const unsigned multi_media_triangles[36] =
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
   4, 1, 5
};
static const unsigned multi_media_properties[36] =
{
   0, 4, 4,
   0, 4, 4,
   0, 3, 3,
   0, 3, 3,
   0, 3, 3,
   0, 3, 3,
   0, 2, 2,
   0, 2, 2,
   0, 2, 2,
   0, 1, 1,
   0, 1, 1,
   0, 1, 1
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned ecount, tcount, t, e;
  struct senc3d_enclosure* enc;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* Degenerated triangle: duplicated vertex */
  ctx.positions = multi_media_vertices;
  ctx.indices = multi_media_triangles;
  ctx.properties = multi_media_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    multi_media_triangles_count, get_indices, get_media_from_properties,
    multi_media_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_triangles_count(scn, &tcount));
  CHK(tcount == multi_media_triangles_count);
  FOR_EACH(t, 0, tcount) {
    unsigned ids[2];
    OK(senc3d_scene_get_triangle_enclosures(scn, t, ids));
    CHK(ids[0] == 0 && ids[1] == 1);
  }
  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);
  FOR_EACH(e, 0, ecount) {
    struct senc3d_enclosure_header header;
    OK(senc3d_scene_get_enclosure(scn, e, &enc));
    OK(senc3d_enclosure_get_header(enc, &header));
    CHK(header.primitives_count == multi_media_triangles_count);
    CHK(header.enclosed_media_count == (header.is_infinite ? 1u : 4u));
    OK(senc3d_enclosure_ref_put(enc));
  }

  OK(senc3d_scene_ref_put(scn));
  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
