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
  2 overlapping cubes with faces splitted on opposite diagonals
  (cannot merge triangles)
 */

/* Dump of star-geometry-3d 'invalid_1'. */
static const unsigned invalid_vertices_count = 8;
static const double invalid_vertices[24] =
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
static const unsigned invalid_triangles_count = 24;
static const unsigned invalid_triangles[72] =
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
   0, 3, 1,
   0, 2, 3,
   0, 6, 2,
   0, 4, 6,
   4, 5, 7,
   6, 4, 7,
   3, 7, 5,
   1, 3, 5,
   2, 6, 7,
   3, 2, 7,
   0, 1, 5,
   4, 0, 5
};
static const unsigned invalid_properties[72] =
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

static const unsigned degenerated1_triangles_count = 1;
static const unsigned degenerated1_vertices_count = 3;
static const unsigned degenerated1[3] = { 0, 0, 0 };

static const unsigned degenerated2_triangles_count = 1;
static const unsigned degenerated2_vertices_count = 3;
static const unsigned degenerated2[3] = { 0, 1, 2 };

static const double degenerated_vertices[9]
= { 0, 0, 0,  1, 0, 0,  2, 0, 0 };
static const unsigned degenerated_properties[9]
= { 0, 0, 0,  1, 0, 0,  2, 0, 0 };

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned count, tcount, t, e;
  struct senc3d_enclosure* enc;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* Degenerated triangle: duplicated vertex */
  ctx.positions = degenerated_vertices;
  ctx.indices = degenerated1;
  ctx.properties = degenerated_properties;
  BA(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    degenerated1_triangles_count, get_indices, get_media_from_properties,
    degenerated1_vertices_count, get_position, &ctx, &scn));

  /* Degenerated triangles: flat triangle */
  ctx.positions = degenerated_vertices;
  ctx.indices = degenerated2;
  ctx.properties = degenerated_properties;
  BA(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    degenerated2_triangles_count, get_indices, get_media_from_properties,
    degenerated2_vertices_count, get_position, &ctx, &scn));

  /* Degenerated scene: overlapping triangles */
  ctx.positions = invalid_vertices;
  ctx.indices = invalid_triangles;
  ctx.properties = invalid_properties;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    invalid_triangles_count, get_indices, get_media_from_properties,
    invalid_vertices_count, get_position, &ctx, &scn));

  OK(senc3d_scene_get_triangles_count(scn, &tcount));
  FOR_EACH(t, 0, tcount) {
    unsigned ids[2];
    BO(senc3d_scene_get_triangle_enclosures(scn, t, ids));
  }
  BO(senc3d_scene_get_enclosure_count(scn, &count));
  BO(senc3d_scene_get_enclosure(scn, 0, &enc));

  OK(senc3d_scene_get_overlapping_triangles_count(scn, &count));
  FOR_EACH(e, 0, count) {
    OK(senc3d_scene_get_overlapping_triangle(scn, e, &t));
    ASSERT(t < tcount);
  }
  CHK(count == invalid_triangles_count);
  OK(senc3d_scene_get_overlapping_triangle(scn, 0, &t));
  BA(senc3d_scene_get_overlapping_triangle(scn, invalid_triangles_count, &t));

  OK(senc3d_scene_ref_put(scn));
  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
