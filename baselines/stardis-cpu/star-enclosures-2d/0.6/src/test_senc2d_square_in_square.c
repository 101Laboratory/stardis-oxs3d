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
 * of star-geometry-2D. It uses output from test_sg2_square_in_square. */

#include "senc2d.h"
#include "test_senc2d_utils.h"

#include <rsys/double2.h>

/*
            square_2                            square_3

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

/* Dump of star-geometry-2D 'square_in_square_2'. */
static const unsigned square_in_square_2_vertices_count = 8;
static const double square_in_square_2_vertices[16] =
{
   0.1, 0,
   1, 0,
   0, 1.1,
   1, 1,
   -0.7, -1,
   2, -1,
   -1, 2.3,
   2, 2
};
static const unsigned square_in_square_2_segments_count = 8;
static const unsigned square_in_square_2_segments[16] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0,
   6, 4,
   7, 6,
   5, 7,
   4, 5
};
static const unsigned square_in_square_2_properties[24] =
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
/* Dump of star-geometry-2D 'square_in_square_3'. */
static const unsigned square_in_square_3_vertices_count = 12;
static const double square_in_square_3_vertices[24] =
{
   0.1, 0,
   1, 0,
   0, 1.1,
   1, 1,
   -0.7, -1,
   2, -1,
   -1, 2.3,
   2, 2,
   -3, -4,
   6, -4,
   -4, 7,
   6, 6
};
static const unsigned square_in_square_3_segments_count = 12;
static const unsigned square_in_square_3_segments[24] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0,
   6, 4,
   7, 6,
   5, 7,
   4, 5,
   10, 8,
   11, 10,
   9, 11,
   8, 9
};
static const unsigned square_in_square_3_properties[36] =
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
   0, 1, 0
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned e, ecount, maxm, e2;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* Create a scene with the first and second squares.
   * The enclosure in the small contains medium 1, the external enclosure
   * contains medium 1, the enclosure between the small and medium squares
   * contains medium 0. */
  ctx.positions = square_in_square_2_vertices;
  ctx.indices = square_in_square_2_segments;
  ctx.properties = square_in_square_2_properties;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    square_in_square_2_segments_count, get_indices, get_media_from_properties,
    square_in_square_2_vertices_count, get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 3);

  FOR_EACH(e, 0, ecount) {
    struct senc2d_enclosure* enclosure;
    struct senc2d_enclosure_header header;
    unsigned m;
    OK(senc2d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));
    ASSERT(header.enclosed_media_count == 1);
    OK(senc2d_enclosure_get_medium(enclosure, 0, &m));
    ASSERT(m <= 1);
    ASSERT((m == 0) == (header.primitives_count == square_in_square_2_segments_count));
    OK(senc2d_enclosure_ref_put(enclosure));
  }

  OK(senc2d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);
  OK(senc2d_scene_ref_put(scn));

  /* Create a scene with the 3 squares, same 2 first squares as above.
   * The enclosure in the small square contains medium 1, the external enclosure
   * contains medium 1, the enclosure between the small and medium squares
   * contains medium 0 and the enclosure between the medium and big squares
   * contains both media 0 and 1. */
  ctx.positions = square_in_square_3_vertices;
  ctx.indices = square_in_square_3_segments;
  ctx.properties = square_in_square_3_properties;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    square_in_square_3_segments_count, get_indices, get_media_from_properties,
    square_in_square_3_vertices_count, get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 4);

  e2 = ecount;
  FOR_EACH(e, 0, ecount) {
    struct senc2d_enclosure* enclosure;
    struct senc2d_enclosure_header header;
    OK(senc2d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));
    ASSERT(header.enclosed_media_count <= 2);
    if(header.enclosed_media_count == 2) {
      /* A single internal enclosure has 2 media */
      ASSERT(!header.is_infinite);
      ASSERT(e2 == ecount); (void)e2;
      e2 = e;
    }
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
