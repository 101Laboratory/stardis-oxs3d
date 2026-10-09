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
 * of star-geometry-2D. It uses output from test_sg2_square_behind_square. */

#include "senc2d.h"
#include "test_senc2d_utils.h"

#include <rsys/double2.h>

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

/* Dump of star-geometry-2D 'square_behind_square_2'. */
static const unsigned square_behind_square_2_vertices_count = 8;
static const double square_behind_square_2_vertices[16] =
{
   0.1, 0,
   1, 0,
   0, 1.1,
   1, 1,
   -0.2, 2,
   2.5, 2,
   -0.5, 5.3,
   2.5, 5
};
static const unsigned square_behind_square_2_segments_count = 8;
static const unsigned square_behind_square_2_segments[16] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0,
   4, 6,
   6, 7,
   7, 5,
   5, 4
};
static const unsigned square_behind_square_2_properties[24] =
{
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0
};
/* Dump of star-geometry-2D 'square_behind_square_3'. */
static const unsigned square_behind_square_3_vertices_count = 12;
static const double square_behind_square_3_vertices[24] =
{
   0.1, 0,
   1, 0,
   0, 1.1,
   1, 1,
   -0.2, 2,
   2.5, 2,
   -0.5, 5.3,
   2.5, 5,
   -0.5, 6,
   4, 6,
   -1, 11.5,
   4, 11
};
static const unsigned square_behind_square_3_segments_count = 12;
static const unsigned square_behind_square_3_segments[24] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0,
   4, 6,
   6, 7,
   7, 5,
   5, 4,
   8, 10,
   10, 11,
   11, 9,
   9, 8
};
static const unsigned square_behind_square_3_properties[36] =
{
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
   1, 0, 0
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned i, ecount, maxm;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* Create a scene with the first and second squares.
   * Both squares have medium 1 inside and medium 0 outside,
   * the second square is +Y from the first square and is big enough
   * to prevent rays from the first square to miss it. */
  ctx.positions = square_behind_square_2_vertices;
  ctx.indices = square_behind_square_2_segments;
  ctx.properties = square_behind_square_2_properties;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    square_behind_square_2_segments_count, get_indices,
    get_media_from_properties, square_behind_square_2_vertices_count,
    get_position, &ctx, &scn));
  
  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 3);

  FOR_EACH(i, 0, ecount) {
    struct senc2d_enclosure* enclosure;
    struct senc2d_enclosure_header header;
    OK(senc2d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));
    ASSERT(header.enclosed_media_count == 1);
    OK(senc2d_enclosure_ref_put(enclosure));
  }

  OK(senc2d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);
  OK(senc2d_scene_ref_put(scn));

  /* Create a scene with the 3 squares, same 2 first squares as above
   * The third square has medium 0 inside and medium 1 outside and is further
   * in +Y and bigger */
  ctx.positions = square_behind_square_3_vertices;
  ctx.indices = square_behind_square_3_segments;
  ctx.properties = square_behind_square_3_properties;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    square_behind_square_3_segments_count, get_indices,
    get_media_from_properties, square_behind_square_3_vertices_count,
    get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 4);

  FOR_EACH(i, 0, ecount) {
    struct senc2d_enclosure* enclosure;
    struct senc2d_enclosure_header header;
    OK(senc2d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));
    /* Inside enclosures contain 1 single media */
    ASSERT(header.enclosed_media_count == (header.is_infinite ? 2u : 1u));
    OK(senc2d_enclosure_ref_put(enclosure));
  }

  OK(senc2d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);

  OK(senc2d_scene_ref_put(scn));
  OK(senc2d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
