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

#include "senc2d.h"
#include "test_senc2d_utils.h"

#include <rsys/float2.h>
#include <rsys/double2.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct senc2d_enclosure* enc = NULL;
  struct senc2d_enclosure_header header;
  struct context ctx = CONTEXT_NULL__;
  unsigned medfront[2], medback[2], ind[2], ids[2], seg;
  double vrtx[2];
  unsigned count, i, maxm;
  int convention;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* A 2D square.
   * With this geometry front is inside with NORMAL_BACK convention,
   * outside with NORMAL_FRONT convention */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;

  BA(senc2d_scene_create(NULL,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc2d_scene_create(dev,
    0,
    nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    0, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, NULL, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    0, get_position, &ctx, &scn));
  BA(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    nvertices, NULL, &ctx, &scn));
  BA(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, NULL));
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  BA(senc2d_scene_get_convention(NULL, &convention));
  BA(senc2d_scene_get_convention(scn, NULL));
  BA(senc2d_scene_get_convention(NULL, NULL));
  OK(senc2d_scene_get_convention(scn, &convention));
  CHK(convention
    == (SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE));

  BA(senc2d_scene_get_segments_count(NULL, &count));
  BA(senc2d_scene_get_segments_count(scn, NULL));
  BA(senc2d_scene_get_segments_count(NULL, NULL));
  OK(senc2d_scene_get_segments_count(scn, &count));
  CHK(count == nsegments);
  
  BA(senc2d_scene_get_vertices_count(NULL, &count));
  BA(senc2d_scene_get_vertices_count(scn, NULL));
  BA(senc2d_scene_get_vertices_count(NULL, NULL));
  OK(senc2d_scene_get_vertices_count(scn, &count));
  CHK(count == nvertices);

  BA(senc2d_scene_get_segment(NULL, 0, ind));
  BA(senc2d_scene_get_segment(scn, UINT_MAX, ind));
  BA(senc2d_scene_get_segment(scn, 0, NULL));
  BA(senc2d_scene_get_segment(NULL, UINT_MAX, ind));
  BA(senc2d_scene_get_segment(NULL, 0, NULL));
  BA(senc2d_scene_get_segment(scn, UINT_MAX, NULL));
  BA(senc2d_scene_get_segment(NULL, UINT_MAX, NULL));
  OK(senc2d_scene_get_segment(scn, 0, ind));

  BA(senc2d_scene_get_segment_media(NULL, 0, ind));
  BA(senc2d_scene_get_segment_media(scn, UINT_MAX, ind));
  BA(senc2d_scene_get_segment_media(scn, 0, NULL));
  BA(senc2d_scene_get_segment_media(NULL, UINT_MAX, ind));
  BA(senc2d_scene_get_segment_media(NULL, 0, NULL));
  BA(senc2d_scene_get_segment_media(scn, UINT_MAX, NULL));
  BA(senc2d_scene_get_segment_media(NULL, UINT_MAX, NULL));
  OK(senc2d_scene_get_segment_media(scn, 0, ind));

  BA(senc2d_scene_get_vertex(NULL, 0, vrtx));
  BA(senc2d_scene_get_vertex(scn, UINT_MAX, vrtx));
  BA(senc2d_scene_get_vertex(scn, 0, NULL));
  BA(senc2d_scene_get_vertex(NULL, UINT_MAX, vrtx));
  BA(senc2d_scene_get_vertex(NULL, 0, NULL));
  BA(senc2d_scene_get_vertex(scn, UINT_MAX, NULL));
  BA(senc2d_scene_get_vertex(NULL, UINT_MAX, NULL));
  OK(senc2d_scene_get_vertex(scn, 0, vrtx));

  BA(senc2d_scene_get_max_medium(NULL, &maxm));
  BA(senc2d_scene_get_max_medium(scn, NULL));
  BA(senc2d_scene_get_max_medium(NULL, NULL));
  OK(senc2d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);

  BA(senc2d_scene_get_enclosure_count(NULL, &count));
  BA(senc2d_scene_get_enclosure_count(scn, NULL));
  BA(senc2d_scene_get_enclosure_count(NULL, NULL));
  OK(senc2d_scene_get_enclosure_count(scn, &count));
  CHK(count == 2);

  BA(senc2d_scene_get_enclosure_count_by_medium(NULL, 0, &count));
  BA(senc2d_scene_get_enclosure_count_by_medium(scn, 100, &count));
  BA(senc2d_scene_get_enclosure_count_by_medium(scn, 0, NULL));
  BA(senc2d_scene_get_enclosure_count_by_medium(NULL, 100, &count));
  BA(senc2d_scene_get_enclosure_count_by_medium(NULL, 0, NULL));
  BA(senc2d_scene_get_enclosure_count_by_medium(scn, 100, NULL));
  BA(senc2d_scene_get_enclosure_count_by_medium(NULL, 100, NULL));
  OK(senc2d_scene_get_enclosure_count_by_medium(scn, 0, &count));
  CHK(count == 1);
  OK(senc2d_scene_get_enclosure_count_by_medium(scn, SENC2D_UNSPECIFIED_MEDIUM,
    &count));
  CHK(count == 0);

  BA(senc2d_scene_get_enclosure(NULL, 0, &enc));
  BA(senc2d_scene_get_enclosure(scn, UINT_MAX, &enc));
  BA(senc2d_scene_get_enclosure(scn, 0, NULL));
  BA(senc2d_scene_get_enclosure(NULL, UINT_MAX, &enc));
  BA(senc2d_scene_get_enclosure(NULL, 0, NULL));
  BA(senc2d_scene_get_enclosure(scn, UINT_MAX, NULL));
  BA(senc2d_scene_get_enclosure(NULL, UINT_MAX, NULL));
  OK(senc2d_scene_get_enclosure(scn, 0, &enc));
  OK(senc2d_enclosure_ref_put(enc));

  BA(senc2d_scene_get_enclosure_by_medium(NULL, 0, 0, &enc));
  BA(senc2d_scene_get_enclosure_by_medium(scn, 100, 0, &enc));
  BA(senc2d_scene_get_enclosure_by_medium(scn, 0, UINT_MAX, &enc));
  BA(senc2d_scene_get_enclosure_by_medium(scn, 0, 0, NULL));
  BA(senc2d_scene_get_enclosure_by_medium(NULL, 100, 0, &enc));
  BA(senc2d_scene_get_enclosure_by_medium(NULL, 0, UINT_MAX, &enc));
  BA(senc2d_scene_get_enclosure_by_medium(NULL, 0, 0, NULL));
  BA(senc2d_scene_get_enclosure_by_medium(scn, 100, UINT_MAX, &enc));
  BA(senc2d_scene_get_enclosure_by_medium(scn, 100, 0, NULL));
  BA(senc2d_scene_get_enclosure_by_medium(scn, 0, UINT_MAX, NULL));
  BA(senc2d_scene_get_enclosure_by_medium(scn, 100, UINT_MAX, NULL));
  BA(senc2d_scene_get_enclosure_by_medium(NULL, 0, UINT_MAX, NULL));
  BA(senc2d_scene_get_enclosure_by_medium(NULL, 100, 0, NULL));
  BA(senc2d_scene_get_enclosure_by_medium(NULL, 100, UINT_MAX, &enc));
  BA(senc2d_scene_get_enclosure_by_medium(NULL, 100, UINT_MAX, NULL));
  OK(senc2d_scene_get_enclosure_by_medium(scn, 0, 0, &enc));
  OK(senc2d_enclosure_ref_put(enc));
  /* Index 0 is out of range for SENC2D_UNSPECIFIED_MEDIUM. */
  BA(senc2d_scene_get_enclosure_by_medium(scn, SENC2D_UNSPECIFIED_MEDIUM, 0, &enc));

  BA(senc2d_scene_get_segment_enclosures(NULL, 0, ids));
  BA(senc2d_scene_get_segment_enclosures(scn, UINT_MAX, ids));
  BA(senc2d_scene_get_segment_enclosures(scn, 0, NULL));
  BA(senc2d_scene_get_segment_enclosures(NULL, UINT_MAX, ids));
  BA(senc2d_scene_get_segment_enclosures(NULL, 0, NULL));
  BA(senc2d_scene_get_segment_enclosures(scn, UINT_MAX, NULL));
  BA(senc2d_scene_get_segment_enclosures(NULL, UINT_MAX, NULL));
  OK(senc2d_scene_get_segment_enclosures(scn, 0, ids));

  BA(senc2d_scene_get_frontier_vertice_count(NULL, &count));
  BA(senc2d_scene_get_frontier_vertice_count(scn, NULL));
  BA(senc2d_scene_get_frontier_vertice_count(NULL, NULL));
  OK(senc2d_scene_get_frontier_vertice_count(scn, &count));
  CHK(count == 0);

  BA(senc2d_scene_get_frontier_vertex(NULL, 0, &ids[0], &seg));
  BA(senc2d_scene_get_frontier_vertex(scn, UINT_MAX, &ids[0], &seg));
  BA(senc2d_scene_get_frontier_vertex(scn, 0, NULL, &seg));
  BA(senc2d_scene_get_frontier_vertex(scn, 0, &ids[0], NULL));
  BA(senc2d_scene_get_frontier_vertex(NULL, UINT_MAX, &ids[0], &seg));
  BA(senc2d_scene_get_frontier_vertex(NULL, 0, NULL, &seg));
  BA(senc2d_scene_get_frontier_vertex(NULL, 0, &ids[0], NULL));
  BA(senc2d_scene_get_frontier_vertex(scn, UINT_MAX, NULL, &seg));
  BA(senc2d_scene_get_frontier_vertex(scn, UINT_MAX, &ids[0], NULL));
  BA(senc2d_scene_get_frontier_vertex(scn, 0, NULL, NULL));
  BA(senc2d_scene_get_frontier_vertex(NULL, UINT_MAX, NULL, &seg));
  BA(senc2d_scene_get_frontier_vertex(NULL, UINT_MAX, &ids[0], NULL));
  BA(senc2d_scene_get_frontier_vertex(NULL, 0, NULL, NULL));
  BA(senc2d_scene_get_frontier_vertex(scn, UINT_MAX, NULL, NULL));
  BA(senc2d_scene_get_frontier_vertex(NULL, UINT_MAX, NULL, NULL));

  BA(senc2d_scene_get_overlapping_segments_count(NULL, NULL));
  BA(senc2d_scene_get_overlapping_segments_count(scn, NULL));
  BA(senc2d_scene_get_overlapping_segments_count(NULL, &count));
  OK(senc2d_scene_get_overlapping_segments_count(scn, &count));
  CHK(count == 0);

  BA(senc2d_scene_get_overlapping_segment(NULL, 0, &seg));
  BA(senc2d_scene_get_overlapping_segment(scn, UINT_MAX, &seg));
  BA(senc2d_scene_get_overlapping_segment(scn, 0, NULL));
  BA(senc2d_scene_get_overlapping_segment(NULL, UINT_MAX, &seg));
  BA(senc2d_scene_get_overlapping_segment(NULL, 0, NULL));
  BA(senc2d_scene_get_overlapping_segment(scn, UINT_MAX, NULL));
  BA(senc2d_scene_get_overlapping_segment(NULL, UINT_MAX, NULL));

  BA(senc2d_scene_ref_get(NULL));
  OK(senc2d_scene_ref_get(scn));
  BA(senc2d_scene_ref_put(NULL));
  OK(senc2d_scene_ref_put(scn));

  OK(senc2d_scene_ref_put(scn));

  /* Same geometry with SENC2D_UNSPECIFIED_MEDIUM */
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, NULL,
    nvertices, get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_by_medium(scn, SENC2D_UNSPECIFIED_MEDIUM, 0, &enc));
  OK(senc2d_enclosure_ref_put(enc));
  BA(senc2d_scene_get_enclosure_by_medium(scn, SENC2D_UNSPECIFIED_MEDIUM, 100, &enc));

  OK(senc2d_scene_ref_put(scn));

  /* Same geometry with a hole (1 missing segment) */
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments - 1, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc2d_scene_get_frontier_vertice_count(scn, &count));
  CHK(count == 2);
  OK(senc2d_scene_get_frontier_vertex(scn, 0, &ids[0], &seg));
  BA(senc2d_scene_get_frontier_vertex(scn, 3, &ids[0], &seg));

  OK(senc2d_scene_ref_put(scn));

  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_BACK | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc2d_scene_get_convention(scn, &convention));
  CHK(convention
    == (SENC2D_CONVENTION_NORMAL_BACK | SENC2D_CONVENTION_NORMAL_INSIDE));
  /* Check that medium 0 is inside */
  OK(senc2d_scene_get_enclosure_by_medium(scn, 0, 0, &enc));
  OK(senc2d_enclosure_get_header(enc, &header));
  CHK(!header.is_infinite);
  OK(senc2d_enclosure_ref_put(enc));

  OK(senc2d_scene_get_segment_media(scn, 0, medback));
  OK(senc2d_scene_ref_put(scn));

  /* Medium mismatch between neighbour segments, but OK */
  ctx.front_media = medium1_3;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc2d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 3);
  OK(senc2d_scene_get_enclosure_count_by_medium(scn, 0, &count));
  CHK(count == 0); /* Medium 0 unused */
  OK(senc2d_scene_get_enclosure_count_by_medium(scn, 1, &count));
  CHK(count == 2); /* Medium 1 used twice */
  OK(senc2d_scene_get_enclosure_count_by_medium(scn, 2, &count));
  CHK(count == 0); /* Medium 2 unused */
  OK(senc2d_scene_get_enclosure_count_by_medium(scn, 3, &count));
  CHK(count == 1); /* Medium 3 used */

  OK(senc2d_scene_ref_put(scn));

  ctx.front_media = medium0;
  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  /* Check that medium 0 is outside */
  OK(senc2d_scene_get_enclosure_by_medium(scn, 0, 0, &enc));
  OK(senc2d_enclosure_get_header(enc, &header));
  CHK(header.is_infinite);
  OK(senc2d_enclosure_ref_put(enc));

  OK(senc2d_scene_get_segment_media(scn, 0, medfront));
  FOR_EACH(i, 0, 2) CHK(medback[i] == medfront[i]);

  OK(senc2d_scene_ref_put(scn));
  OK(senc2d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}
