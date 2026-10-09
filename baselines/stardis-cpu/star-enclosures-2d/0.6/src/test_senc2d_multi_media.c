/* Copyright (C) |Meso|Star> 2016-2020 (contact@meso-star.com)
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

#include "senc2d.h"
#include "test_senc2d_utils.h"

#include <rsys/double2.h>

#include <stdio.h>

static const unsigned multi_media_vertices_count = 4;
static const double multi_media_vertices[8] =
{
   0, 0,
   1, 0,
   0, 1,
   1, 1
};
static const unsigned multi_media_segments_count = 4;
static const unsigned multi_media_segments[8] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0
};
static const unsigned multi_media_properties[12] =
{
   0, 4, 4,
   0, 3, 3,
   0, 2, 2,
   0, 1, 1
};

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned ecount, scount, s, e;
  struct senc2d_enclosure* enc;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* Degenerated triangle: duplicated vertex */
  ctx.positions = multi_media_vertices;
  ctx.indices = multi_media_segments;
  ctx.properties = multi_media_properties;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    multi_media_segments_count, get_indices, get_media_from_properties,
    multi_media_vertices_count, get_position, &ctx, &scn));

  OK(senc2d_scene_get_segments_count(scn, &scount));
  CHK(scount == multi_media_segments_count);
  FOR_EACH(s, 0, scount) {
    unsigned ids[2];
    OK(senc2d_scene_get_segment_enclosures(scn, s, ids));
    CHK(ids[0] == 0 && ids[1] == 1);
  }
  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);
  FOR_EACH(e, 0, ecount) {
    struct senc2d_enclosure_header header;
    OK(senc2d_scene_get_enclosure(scn, e, &enc));
    OK(senc2d_enclosure_get_header(enc, &header));
    CHK(header.primitives_count == multi_media_segments_count);
    CHK(header.enclosed_media_count == (header.is_infinite ? 1u : 4u));
    OK(senc2d_enclosure_ref_put(enc));
  }

  OK(senc2d_scene_ref_put(scn));
  OK(senc2d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
