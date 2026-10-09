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

/*
  2 squares with some edges overlapping 
 */

/* Dump of star-geometry-2d 'invalid'. */
static const unsigned invalid_vertices_count = 6;
static const double invalid_vertices[12] =
{
   0, 0,
   1, 0,
   0, 1,
   1, 1,
   2, 0,
   2, 1
};
static const unsigned invalid_segments_count = 7;
static const unsigned invalid_segments[14] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0,
   2, 5,
   5, 4,
   4, 0
};
static const unsigned invalid_properties[21] =
{
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0
};

static const unsigned degenerated_segments_count = 1;
static const unsigned degenerated_vertices_count = 2;
static const unsigned degenerated[2] = { 0, 0 };
static const double degenerated_vertices[9] = { 0, 0 };
static const unsigned degenerated_properties[3] = { 0, 0, 0 };

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned count, scount, s, e;
  struct senc2d_enclosure* enc;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* Degenerated segment: duplicated vertex */
  ctx.positions = degenerated_vertices;
  ctx.indices = degenerated;
  ctx.properties = degenerated_properties;
  BA(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    degenerated_segments_count, get_indices, get_media_from_properties,
    degenerated_vertices_count, get_position, &ctx, &scn));

  /* Degenerated scene: overlapping segments */
  ctx.positions = invalid_vertices;
  ctx.indices = invalid_segments;
  ctx.properties = invalid_properties;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    invalid_segments_count, get_indices, get_media_from_properties,
    invalid_vertices_count, get_position, &ctx, &scn));

  OK(senc2d_scene_get_segments_count(scn, &scount));
  FOR_EACH(s, 0, scount) {
    unsigned ids[2];
    BO(senc2d_scene_get_segment_enclosures(scn, s, ids));
  }
  BO(senc2d_scene_get_enclosure_count(scn, &count));
  BO(senc2d_scene_get_enclosure(scn, 0, &enc));

  OK(senc2d_scene_get_overlapping_segments_count(scn, &count));
  FOR_EACH(e, 0, count) {
    OK(senc2d_scene_get_overlapping_segment(scn, e, &s));
    ASSERT(s < scount);
  }
  CHK(count == 4);
  OK(senc2d_scene_get_overlapping_segment(scn, 0, &s));
  BA(senc2d_scene_get_overlapping_segment(scn, count, &s));

  OK(senc2d_scene_ref_put(scn));
  OK(senc2d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

