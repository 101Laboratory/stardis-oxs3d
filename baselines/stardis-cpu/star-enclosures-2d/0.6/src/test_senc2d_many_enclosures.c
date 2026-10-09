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

/* This test is similar to test_senc2d_some_enclosures, but involves 64*64*64
 * circles instead of 4*4*4, thus making it impossible to define the geometry
 * through static arrays. */

#define NB_CIRC_X 32
#define NB_CIRC_Y 32
#define NB_CIRC_Z 64
/* 64^3 = 262144 circles */
#define NB_CIRC (NB_CIRC_X * NB_CIRC_Y * NB_CIRC_Z)

#include "senc2d.h"
#include "test_senc2d_utils.h"
#include "test_senc2d_utils2.h"

#include <rsys/clock_time.h>
#include <rsys/double2.h>

#include <stdio.h>
#include <limits.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned count;
  unsigned circ_seg_count, circ_vrtx_count, e;
  char dump[64];
  struct time t0, t1;
  (void)argc, (void)argv;

  OK(mem_init_regular_allocator(&allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* A 16 segments circle template */
  create_circle(1, 16, &ctx);
  ASSERT(sa_size(ctx.positions) % 2 == 0
    && sa_size(ctx.positions) / 2 < UINT_MAX);
  ASSERT(sa_size(ctx.indices) % 2 == 0
    && sa_size(ctx.indices) / 2 < UINT_MAX);
  circ_seg_count = (unsigned)sa_size(ctx.indices) / 2;
  circ_vrtx_count = (unsigned)sa_size(ctx.positions) / 2;

  /* Create the scene with NB_CIRC circles.
   * There are NB_CIRC_X * NB_CIRC_Y imbrications of NB_CIRC_Z circles each.
   * Each imbrication is located on a grid.
   * The get_ctx_xxx getters have to retrieve the circle from the
   * primitive and vertice indexes. */
  time_current(&t0);
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    NB_CIRC * circ_seg_count, get_ctx_indices, get_ctx_media,
    NB_CIRC * circ_vrtx_count, get_ctx_position, &ctx, &scn));
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_MSEC | TIME_SEC | TIME_MIN, NULL, dump, sizeof(dump));
  printf("Scene created in: %s\n", dump);
  circle_release(&ctx);

  OK(senc2d_scene_get_vertices_count(scn, &count));
  CHK(count == NB_CIRC * circ_vrtx_count);
  OK(senc2d_scene_get_segments_count(scn, &count));
  CHK(count == NB_CIRC * circ_seg_count);

  OK(senc2d_scene_get_enclosure_count(scn, &count));
  CHK(count == 1 + NB_CIRC);
  FOR_EACH(e, 0, count) {
    struct senc2d_enclosure* enclosure;
    struct senc2d_enclosure_header header;
    unsigned m;
    OK(senc2d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count == 1);
    OK(senc2d_enclosure_get_medium(enclosure, 0, &m));
    CHK(header.primitives_count ==
      (header.is_infinite /* Outermost enclosure: NB_CIRC_X*NB_CIRC_Y circles */
        ? NB_CIRC_X * NB_CIRC_Y * circ_seg_count
        : (m == 0
          ? circ_seg_count /* Innermost enclosures: 1 circles */
          : 2 * circ_seg_count))); /* Other enclosures: 2 circles */
    OK(senc2d_enclosure_ref_put(enclosure));
  }

  OK(senc2d_scene_ref_put(scn));
  OK(senc2d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_regular_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
