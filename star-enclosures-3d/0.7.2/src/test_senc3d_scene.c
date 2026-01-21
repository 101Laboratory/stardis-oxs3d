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

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/float3.h>
#include <rsys/double3.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct senc3d_enclosure* enc = NULL;
  struct senc3d_enclosure_header header;
  struct context ctx = CONTEXT_NULL__;
  unsigned medfront[2], medback[2], ind[3], ids[2], trg;
  double vrtx[3];
  unsigned count, i, maxm;
  int convention;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* A 3D cube.
   * With this geometry front is inside with NORMAL_BACK convention,
   * outside with NORMAL_FRONT convention */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;

  BA(senc3d_scene_create(NULL,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc3d_scene_create(dev,
    0,
    ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    0, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, NULL, get_media,
    nvertices, get_position, &ctx, &scn));
  BA(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    0, get_position, &ctx, &scn));
  BA(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    nvertices, NULL, &ctx, &scn));
  BA(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, NULL));
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  BA(senc3d_scene_get_convention(NULL, &convention));
  BA(senc3d_scene_get_convention(scn, NULL));
  BA(senc3d_scene_get_convention(NULL, NULL));
  OK(senc3d_scene_get_convention(scn, &convention));
  CHK(convention
    == (SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE));

  BA(senc3d_scene_get_triangles_count(NULL, &count));
  BA(senc3d_scene_get_triangles_count(scn, NULL));
  BA(senc3d_scene_get_triangles_count(NULL, NULL));
  OK(senc3d_scene_get_triangles_count(scn, &count));
  CHK(count == ntriangles);

  BA(senc3d_scene_get_vertices_count(NULL, &count));
  BA(senc3d_scene_get_vertices_count(scn, NULL));
  BA(senc3d_scene_get_vertices_count(NULL, NULL));
  OK(senc3d_scene_get_vertices_count(scn, &count));
  CHK(count == nvertices);

  BA(senc3d_scene_get_triangle(NULL, 0, ind));
  BA(senc3d_scene_get_triangle(scn, UINT_MAX, ind));
  BA(senc3d_scene_get_triangle(scn, 0, NULL));
  BA(senc3d_scene_get_triangle(NULL, UINT_MAX, ind));
  BA(senc3d_scene_get_triangle(NULL, 0, NULL));
  BA(senc3d_scene_get_triangle(scn, UINT_MAX, NULL));
  BA(senc3d_scene_get_triangle(NULL, UINT_MAX, NULL));
  OK(senc3d_scene_get_triangle(scn, 0, ind));

  BA(senc3d_scene_get_triangle_media(NULL, 0, ind));
  BA(senc3d_scene_get_triangle_media(scn, UINT_MAX, ind));
  BA(senc3d_scene_get_triangle_media(scn, 0, NULL));
  BA(senc3d_scene_get_triangle_media(NULL, UINT_MAX, ind));
  BA(senc3d_scene_get_triangle_media(NULL, 0, NULL));
  BA(senc3d_scene_get_triangle_media(scn, UINT_MAX, NULL));
  BA(senc3d_scene_get_triangle_media(NULL, UINT_MAX, NULL));
  OK(senc3d_scene_get_triangle_media(scn, 0, ind));

  BA(senc3d_scene_get_vertex(NULL, 0, vrtx));
  BA(senc3d_scene_get_vertex(scn, UINT_MAX, vrtx));
  BA(senc3d_scene_get_vertex(scn, 0, NULL));
  BA(senc3d_scene_get_vertex(NULL, UINT_MAX, vrtx));
  BA(senc3d_scene_get_vertex(NULL, 0, NULL));
  BA(senc3d_scene_get_vertex(scn, UINT_MAX, NULL));
  BA(senc3d_scene_get_vertex(NULL, UINT_MAX, NULL));
  OK(senc3d_scene_get_vertex(scn, 0, vrtx));

  BA(senc3d_scene_get_max_medium(NULL, &maxm));
  BA(senc3d_scene_get_max_medium(scn, NULL));
  BA(senc3d_scene_get_max_medium(NULL, NULL));
  OK(senc3d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 1);

  BA(senc3d_scene_get_enclosure_count(NULL, &count));
  BA(senc3d_scene_get_enclosure_count(scn, NULL));
  BA(senc3d_scene_get_enclosure_count(NULL, NULL));
  OK(senc3d_scene_get_enclosure_count(scn, &count));
  CHK(count == 2);

  BA(senc3d_scene_get_enclosure_count_by_medium(NULL, 0, &count));
  BA(senc3d_scene_get_enclosure_count_by_medium(scn, 100, &count));
  BA(senc3d_scene_get_enclosure_count_by_medium(scn, 0, NULL));
  BA(senc3d_scene_get_enclosure_count_by_medium(NULL, 100, &count));
  BA(senc3d_scene_get_enclosure_count_by_medium(NULL, 0, NULL));
  BA(senc3d_scene_get_enclosure_count_by_medium(scn, 100, NULL));
  BA(senc3d_scene_get_enclosure_count_by_medium(NULL, 100, NULL));
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, 0, &count));
  CHK(count == 1);
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, SENC3D_UNSPECIFIED_MEDIUM,
    &count));
  CHK(count == 0);

  BA(senc3d_scene_get_enclosure(NULL, 0, &enc));
  BA(senc3d_scene_get_enclosure(scn, UINT_MAX, &enc));
  BA(senc3d_scene_get_enclosure(scn, 0, NULL));
  BA(senc3d_scene_get_enclosure(NULL, UINT_MAX, &enc));
  BA(senc3d_scene_get_enclosure(NULL, 0, NULL));
  BA(senc3d_scene_get_enclosure(scn, UINT_MAX, NULL));
  BA(senc3d_scene_get_enclosure(NULL, UINT_MAX, NULL));
  OK(senc3d_scene_get_enclosure(scn, 0, &enc));
  OK(senc3d_enclosure_ref_put(enc));

  BA(senc3d_scene_get_enclosure_by_medium(NULL, 0, 0, &enc));
  BA(senc3d_scene_get_enclosure_by_medium(scn, 100, 0, &enc));
  BA(senc3d_scene_get_enclosure_by_medium(scn, 0, UINT_MAX, &enc));
  BA(senc3d_scene_get_enclosure_by_medium(scn, 0, 0, NULL));
  BA(senc3d_scene_get_enclosure_by_medium(NULL, 100, 0, &enc));
  BA(senc3d_scene_get_enclosure_by_medium(NULL, 0, UINT_MAX, &enc));
  BA(senc3d_scene_get_enclosure_by_medium(NULL, 0, 0, NULL));
  BA(senc3d_scene_get_enclosure_by_medium(scn, 100, UINT_MAX, &enc));
  BA(senc3d_scene_get_enclosure_by_medium(scn, 100, 0, NULL));
  BA(senc3d_scene_get_enclosure_by_medium(scn, 0, UINT_MAX, NULL));
  BA(senc3d_scene_get_enclosure_by_medium(scn, 100, UINT_MAX, NULL));
  BA(senc3d_scene_get_enclosure_by_medium(NULL, 0, UINT_MAX, NULL));
  BA(senc3d_scene_get_enclosure_by_medium(NULL, 100, 0, NULL));
  BA(senc3d_scene_get_enclosure_by_medium(NULL, 100, UINT_MAX, &enc));
  BA(senc3d_scene_get_enclosure_by_medium(NULL, 100, UINT_MAX, NULL));
  OK(senc3d_scene_get_enclosure_by_medium(scn, 0, 0, &enc));
  OK(senc3d_enclosure_ref_put(enc));
  /* Index 0 is out of range for SENC3D_UNSPECIFIED_MEDIUM. */
  BA(senc3d_scene_get_enclosure_by_medium(scn, SENC3D_UNSPECIFIED_MEDIUM, 0, &enc));

  BA(senc3d_scene_get_triangle_enclosures(NULL, 0, ids));
  BA(senc3d_scene_get_triangle_enclosures(scn, UINT_MAX, ids));
  BA(senc3d_scene_get_triangle_enclosures(scn, 0, NULL));
  BA(senc3d_scene_get_triangle_enclosures(NULL, UINT_MAX, ids));
  BA(senc3d_scene_get_triangle_enclosures(NULL, 0, NULL));
  BA(senc3d_scene_get_triangle_enclosures(scn, UINT_MAX, NULL));
  BA(senc3d_scene_get_triangle_enclosures(NULL, UINT_MAX, NULL));
  OK(senc3d_scene_get_triangle_enclosures(scn, 0, ids));

  BA(senc3d_scene_get_frontier_segments_count(NULL, &count));
  BA(senc3d_scene_get_frontier_segments_count(scn, NULL));
  BA(senc3d_scene_get_frontier_segments_count(NULL, NULL));
  OK(senc3d_scene_get_frontier_segments_count(scn, &count));
  CHK(count == 0);

  BA(senc3d_scene_get_frontier_segment(NULL, 0, ids, &trg));
  BA(senc3d_scene_get_frontier_segment(scn, UINT_MAX, ids, &trg));
  BA(senc3d_scene_get_frontier_segment(scn, 0, NULL, &trg));
  BA(senc3d_scene_get_frontier_segment(scn, 0, ids, NULL));
  BA(senc3d_scene_get_frontier_segment(NULL, UINT_MAX, ids, &trg));
  BA(senc3d_scene_get_frontier_segment(NULL, 0, NULL, &trg));
  BA(senc3d_scene_get_frontier_segment(NULL, 0, ids, NULL));
  BA(senc3d_scene_get_frontier_segment(scn, UINT_MAX, NULL, &trg));
  BA(senc3d_scene_get_frontier_segment(scn, UINT_MAX, ids, NULL));
  BA(senc3d_scene_get_frontier_segment(scn, 0, NULL, NULL));
  BA(senc3d_scene_get_frontier_segment(NULL, UINT_MAX, NULL, &trg));
  BA(senc3d_scene_get_frontier_segment(NULL, UINT_MAX, ids, NULL));
  BA(senc3d_scene_get_frontier_segment(NULL, 0, NULL, NULL));
  BA(senc3d_scene_get_frontier_segment(scn, UINT_MAX, NULL, NULL));
  BA(senc3d_scene_get_frontier_segment(NULL, UINT_MAX, NULL, NULL));

  BA(senc3d_scene_get_overlapping_triangles_count(NULL, NULL));
  BA(senc3d_scene_get_overlapping_triangles_count(scn, NULL));
  BA(senc3d_scene_get_overlapping_triangles_count(NULL, &count));
  OK(senc3d_scene_get_overlapping_triangles_count(scn, &count));
  CHK(count == 0);

  BA(senc3d_scene_get_overlapping_triangle(NULL, 0, &trg));
  BA(senc3d_scene_get_overlapping_triangle(scn, UINT_MAX, &trg));
  BA(senc3d_scene_get_overlapping_triangle(scn, 0, NULL));
  BA(senc3d_scene_get_overlapping_triangle(NULL, UINT_MAX, &trg));
  BA(senc3d_scene_get_overlapping_triangle(NULL, 0, NULL));
  BA(senc3d_scene_get_overlapping_triangle(scn, UINT_MAX, NULL));
  BA(senc3d_scene_get_overlapping_triangle(NULL, UINT_MAX, NULL));

  BA(senc3d_scene_ref_get(NULL));
  OK(senc3d_scene_ref_get(scn));
  BA(senc3d_scene_ref_put(NULL));
  OK(senc3d_scene_ref_put(scn));

  OK(senc3d_scene_ref_put(scn));

  /* Same geometry with SENC3D_UNSPECIFIED_MEDIUM */
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, NULL,
    nvertices, get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_by_medium(scn, SENC3D_UNSPECIFIED_MEDIUM, 0, &enc));
  OK(senc3d_enclosure_ref_put(enc));
  BA(senc3d_scene_get_enclosure_by_medium(scn, SENC3D_UNSPECIFIED_MEDIUM, 100, &enc));

  OK(senc3d_scene_ref_put(scn));

  /* Same geometry with a hole (1 missing triangle) */
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles - 1, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc3d_scene_get_frontier_segments_count(scn, &count));
  CHK(count == 3);
  OK(senc3d_scene_get_frontier_segment(scn, 0, ids, &trg));
  BA(senc3d_scene_get_frontier_segment(scn, 3, ids, &trg));

  OK(senc3d_scene_ref_put(scn));

  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc3d_scene_get_convention(scn, &convention));
  CHK(convention
    == (SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_INSIDE));
  /* Check that medium 0 is inside */
  OK(senc3d_scene_get_enclosure_by_medium(scn, 0, 0, &enc));
  OK(senc3d_enclosure_get_header(enc, &header));
  CHK(!header.is_infinite);
  OK(senc3d_enclosure_ref_put(enc));

  OK(senc3d_scene_get_triangle_media(scn, 0, medback));
  OK(senc3d_scene_ref_put(scn));

  /* Medium mismatch between neighbour segments, but OK */
  ctx.front_media = medium1_3;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc3d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == 3);
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, 0, &count));
  CHK(count == 0); /* Medium 0 unused */
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, 1, &count));
  CHK(count == 2); /* Medium 1 used twice */
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, 2, &count));
  CHK(count == 0); /* Medium 2 unused */
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, 3, &count));
  CHK(count == 1); /* Medium 3 used */
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, SENC3D_UNSPECIFIED_MEDIUM,
    &count));
  CHK(count == 0);

  OK(senc3d_scene_ref_put(scn));

  ctx.front_media = medium0;
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));
  /* Check that medium 0 is outside */
  OK(senc3d_scene_get_enclosure_by_medium(scn, 0, 0, &enc));
  OK(senc3d_enclosure_get_header(enc, &header));
  CHK(header.is_infinite);
  OK(senc3d_enclosure_ref_put(enc));

  OK(senc3d_scene_get_triangle_media(scn, 0, medfront));
  FOR_EACH(i, 0, 2) CHK(medback[i] == medfront[i]);

  OK(senc3d_scene_ref_put(scn));

  /* Geometry with no media information */
  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles, get_indices, NULL,
    nvertices, get_position, &ctx, &scn));
  OK(senc3d_scene_get_max_medium(scn, &maxm));
  CHK(maxm == SENC3D_UNSPECIFIED_MEDIUM);
  OK(senc3d_scene_get_enclosure_count_by_medium(scn, SENC3D_UNSPECIFIED_MEDIUM,
    &count));
  CHK(count == 2);

  OK(senc3d_scene_ref_put(scn));
  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}
