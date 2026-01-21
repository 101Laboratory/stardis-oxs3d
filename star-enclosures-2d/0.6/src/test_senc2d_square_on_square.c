/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

/* This test has been created using the sg2_geometry_dump_as_C_code feature
 * of star-geometry-2D. It uses output from test_sg2_square_on_square. */

#define _POSIX_C_SOURCE 200112L /* snprintf */

#include "senc2d.h"
#include "test_senc2d_utils.h"

#include <rsys/double2.h>

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

/* Dump of star-geometry-2D 'square_on_square'. */
static const unsigned square_on_square_vertices_count = 10;
static const double square_on_square_vertices[20] =
{
   1, 1,
   2, 1,
   1, 2,
   2, 2,
   1, 3,
   2, 3,
   0.4, 0,
   4, 0,
   0, 4.4,
   4, 4
};
static const unsigned square_on_square_segments_count = 11;
static const unsigned square_on_square_segments[22] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0,
   2, 4,
   4, 5,
   5, 3,
   8, 6,
   9, 8,
   7, 9,
   6, 7
};
static const unsigned square_on_square_properties[33] =
{
   1, 0, 0,
   0, 0, 0,
   1, 0, 0,
   1, 0, 0,
   1, 0, 0,
   1, 0, 0,
   1, 0, 0,
   1, 2, 0,
   1, 2, 0,
   1, 2, 0,
   1, 2, 0
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned count, e;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* Create a scene with the squares.
   * The enclosures in the small squares contain medium 0, the external enclosure
   * contains medium 2, the enclosure between the small and big squares
   * contains medium 1. */
  ctx.positions = square_on_square_vertices;
  ctx.indices = square_on_square_segments;
  ctx.properties = square_on_square_properties;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    square_on_square_segments_count, get_indices, get_media_from_properties,
    square_on_square_vertices_count, get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_count(scn, &count));
  CHK(count == 4);

  OK(senc2d_scene_get_vertices_count(scn, &count));
  CHK(count == square_on_square_vertices_count);

  OK(senc2d_scene_get_segments_count(scn, &count));
  CHK(count == square_on_square_segments_count);

  OK(senc2d_scene_get_enclosure_count(scn, &count));
  CHK(count == 4);
  FOR_EACH(e, 0, count) {
    struct senc2d_enclosure* enclosure;
    struct senc2d_enclosure_header header;
    unsigned m;
    char name[128]; (void)name;
    OK(senc2d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count == 1);
    OK(senc2d_enclosure_get_medium(enclosure, 0, &m));
    if(header.is_infinite) ASSERT(m == 2); /* External */
    else if(header.primitives_count == 4) ASSERT(m == 0); /* Internal */
    else  ASSERT(m == 1); /* In between */
    OK(senc2d_enclosure_ref_put(enclosure));
#ifdef DUMP_ENCLOSURES
    snprintf(name, sizeof(name), "test_square_on_square_%u.obj", e);
    dump_enclosure(scn, e, name);
#endif
  }

  OK(senc2d_scene_ref_put(scn));
  OK(senc2d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
