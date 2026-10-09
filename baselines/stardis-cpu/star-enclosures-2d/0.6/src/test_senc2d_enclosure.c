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
#include "senc2d_sXd_helper.h"
#include "test_senc2d_utils.h"

#include <rsys/double2.h>

#include <star/s2d.h>

static void
test(const int convention)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct senc2d_enclosure* enclosures[2] = { NULL, NULL };
  struct senc2d_enclosure* enclosure;
  struct senc2d_enclosure_header header;
  struct s2d_device* s2d = NULL;
  struct s2d_scene* s2d_scn = NULL;
  struct s2d_shape* s2d_shp = NULL;
  struct s2d_vertex_data s2d_attribs;
  unsigned indices[2][2];
  unsigned medium;
  unsigned gid;
  enum senc2d_side side;
  double vrtx[2];
  double ext_v = 0;
  struct context ctx = CONTEXT_NULL__;
  unsigned i, n, s, count;
  int conv;
  const int conv_front = (convention & SENC2D_CONVENTION_NORMAL_FRONT) != 0;
  const int conv_in = (convention & SENC2D_CONVENTION_NORMAL_INSIDE) != 0;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* A 2D square.
   * 2 enclosures (inside, outside) sharing the same segments,
   * but opposite sides */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;

  OK(senc2d_scene_create(dev, convention, nsegments, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc2d_scene_get_convention(scn, &conv));
  CHK(conv == convention);

  OK(senc2d_scene_get_enclosure_count(scn, &count));
  CHK(count == 2);
  
  OK(senc2d_scene_get_enclosure(scn, 0, &enclosure));
  BA(senc2d_enclosure_ref_get(NULL));
  OK(senc2d_enclosure_ref_get(enclosure));
  BA(senc2d_enclosure_ref_put(NULL));
  OK(senc2d_enclosure_ref_put(enclosure));

  BA(senc2d_enclosure_get_segment(NULL, 0, indices[0]));
  BA(senc2d_enclosure_get_segment(enclosure, nsegments, indices[0]));
  BA(senc2d_enclosure_get_segment(enclosure, 0, NULL));
  BA(senc2d_enclosure_get_segment(NULL, nsegments, indices[0]));
  BA(senc2d_enclosure_get_segment(NULL, 0, NULL));
  BA(senc2d_enclosure_get_segment(enclosure, nsegments, NULL));
  BA(senc2d_enclosure_get_segment(NULL, nsegments, NULL));
  OK(senc2d_enclosure_get_segment(enclosure, 0, indices[0]));

  BA(senc2d_enclosure_get_vertex(NULL, 0, vrtx));
  BA(senc2d_enclosure_get_vertex(enclosure, nvertices, vrtx));
  BA(senc2d_enclosure_get_vertex(enclosure, 0, NULL));
  BA(senc2d_enclosure_get_vertex(NULL, nvertices, vrtx));
  BA(senc2d_enclosure_get_vertex(NULL, 0, NULL));
  BA(senc2d_enclosure_get_vertex(enclosure, nvertices, NULL));
  BA(senc2d_enclosure_get_vertex(NULL, nvertices, NULL));
  OK(senc2d_enclosure_get_vertex(enclosure, 0, vrtx));

  BA(senc2d_enclosure_get_segment_id(NULL, 0, &gid, NULL));
  BA(senc2d_enclosure_get_segment_id(enclosure, nsegments, &gid, NULL));
  BA(senc2d_enclosure_get_segment_id(enclosure, 0, NULL, NULL));
  BA(senc2d_enclosure_get_segment_id(NULL, nsegments, &gid, NULL));
  BA(senc2d_enclosure_get_segment_id(NULL, 0, NULL, NULL));
  BA(senc2d_enclosure_get_segment_id(enclosure, nsegments, NULL, NULL));
  BA(senc2d_enclosure_get_segment_id(NULL, nsegments, NULL, NULL));
  BA(senc2d_enclosure_get_segment_id(enclosure, 0, &gid, NULL));
  BA(senc2d_enclosure_get_segment_id(NULL, 0, &gid, &side));
  BA(senc2d_enclosure_get_segment_id(enclosure, nsegments, &gid, &side));
  BA(senc2d_enclosure_get_segment_id(enclosure, 0, NULL, &side));
  BA(senc2d_enclosure_get_segment_id(NULL, nsegments, &gid, &side));
  BA(senc2d_enclosure_get_segment_id(NULL, 0, NULL, &side));
  BA(senc2d_enclosure_get_segment_id(enclosure, nsegments, NULL, &side));
  BA(senc2d_enclosure_get_segment_id(NULL, nsegments, NULL, &side));
  OK(senc2d_enclosure_get_segment_id(enclosure, 0, &gid, &side));

  BA(senc2d_enclosure_get_medium(NULL, 0, &medium));
  BA(senc2d_enclosure_get_medium(enclosure, 2, &medium));
  BA(senc2d_enclosure_get_medium(enclosure, 0, NULL));
  BA(senc2d_enclosure_get_medium(NULL, 2, &medium));
  BA(senc2d_enclosure_get_medium(NULL, 0, NULL));
  BA(senc2d_enclosure_get_medium(enclosure, 2, NULL));
  BA(senc2d_enclosure_get_medium(NULL, 2, NULL));
  OK(senc2d_enclosure_get_medium(enclosure, 0, &medium));

  OK(senc2d_enclosure_ref_put(enclosure));

  FOR_EACH(i, 0, count) {
    OK(senc2d_scene_get_enclosure(scn, i, &enclosure));

    BA(senc2d_enclosure_get_header(NULL, &header));
    BA(senc2d_enclosure_get_header(enclosure, NULL));
    BA(senc2d_enclosure_get_header(NULL, NULL));
    OK(senc2d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);
    CHK(header.enclosed_media_count == 1);
    OK(senc2d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the square in input segments:
     * if convention is front, front medium (0) is outside,
     * that is medium 0's enclosure is infinite */
    CHK(conv_front == ((medium == 0) == header.is_infinite));
    CHK(header.primitives_count == nsegments);
    CHK(header.unique_primitives_count == nsegments);
    CHK(header.vertices_count == nvertices);
    CHK(header.is_infinite == (i == 0));
    if(i == 0) ext_v = header.volume;
    else CHK(header.volume == -ext_v);

    FOR_EACH(s, 0, header.primitives_count) {
      OK(senc2d_enclosure_get_segment_id(enclosure, s, &gid, &side));
      CHK(gid == s);
      CHK(side == (medium == 0) ? SENC2D_FRONT : SENC2D_BACK);
    }

    OK(senc2d_enclosure_ref_put(enclosure));
  }

  FOR_EACH(i, 0, 2)
    OK(senc2d_scene_get_enclosure(scn, i, enclosures + i));
  FOR_EACH(n, 0, nsegments) {
    int same, reversed;
    /* Read same segments in both enclosures */
    FOR_EACH(i, 0, 2)
      OK(senc2d_enclosure_get_segment(enclosures[i], n, indices[i]));
    /* Same segments and opposite sides for the 2 enclosures */
    FOR_EACH(i, 0, 2) CHK(indices[0][i] == indices[1][1 - i]);
    /* Enclosure 0 is outside (and contains medium 0 if convention is front).
     * Geometrical normals in output data point in the same direction that those
     * of input segments for enclosure 0 iff convention is inside.
     * The opposite holds for enclosure 1. */
    cmp_seg(n, enclosures[0], box_indices + 2 * n, box_vertices, &same, &reversed);
    CHK((same && !reversed) == conv_in);
    cmp_seg(n, enclosures[1], box_indices + 2 * n, box_vertices, &same, &reversed);
    CHK(same && reversed == conv_in);
  }
  FOR_EACH(i, 0, 2)
    OK(senc2d_enclosure_ref_put(enclosures[i]));

  OK(senc2d_scene_ref_put(scn));

  /* Same 2D square, but with a hole (incomplete).
   * 1 single enclosure including both sides of segments */

  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;

  OK(senc2d_scene_create(dev, convention, nsegments - 1, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc2d_scene_get_frontier_vertice_count(scn, &count));
  CHK(count == 2);

  OK(senc2d_scene_get_enclosure_count(scn, &count));
  CHK(count == 1);

#ifdef DUMP_ENCLOSURES
  dump_enclosure(scn, 0, "test_enclosure_hole.obj");
#endif

  OK(senc2d_scene_get_enclosure(scn, 0, &enclosure));

  BA(senc2d_enclosure_get_header(NULL, &header));
  BA(senc2d_enclosure_get_header(enclosure, NULL));
  BA(senc2d_enclosure_get_header(NULL, NULL));
  OK(senc2d_enclosure_get_header(enclosure, &header));

  CHK(header.enclosure_id == 0);
  CHK(header.enclosed_media_count == 2);
  CHK(header.primitives_count == 2 * header.unique_primitives_count);
  CHK(header.unique_primitives_count == nsegments - 1);
  CHK(header.vertices_count == nvertices);
  CHK(header.is_infinite == 1);
  CHK(header.volume == 0);

  FOR_EACH(s, 0, header.primitives_count) {
    OK(senc2d_enclosure_get_segment_id(enclosure, s, &gid, &side));
    /* The first unique_primitives_count segments of an enclosure
     * are unique segments */
    if(s < header.unique_primitives_count) CHK(gid == s);
    CHK(side == (s < header.unique_primitives_count) ? SENC2D_FRONT : SENC2D_BACK);
  }

  /* Put geometry in a 2D view using helper functions */
  s2d_attribs.type = S2D_FLOAT2;
  s2d_attribs.usage = S2D_POSITION;
  s2d_attribs.get = senc2d_sXd_enclosure_get_position;
  OK(s2d_device_create(NULL, &allocator, 0, &s2d));
  OK(s2d_scene_create(s2d, &s2d_scn));
  S2D(shape_create_line_segments(s2d, &s2d_shp));
  S2D(line_segments_setup_indexed_vertices(s2d_shp, header.primitives_count,
    senc2d_sXd_enclosure_get_indices, header.vertices_count, &s2d_attribs,
    1, enclosure));
  OK(s2d_scene_attach_shape(s2d_scn, s2d_shp));
  S2D(shape_ref_put(s2d_shp));
  S2D(device_ref_put(s2d));
  S2D(scene_ref_put(s2d_scn));

  SENC2D(scene_ref_put(scn));
  SENC2D(device_ref_put(dev));
  SENC2D(enclosure_ref_put(enclosure));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test(SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE);
  test(SENC2D_CONVENTION_NORMAL_BACK | SENC2D_CONVENTION_NORMAL_INSIDE);
  test(SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_OUTSIDE);
  test(SENC2D_CONVENTION_NORMAL_BACK | SENC2D_CONVENTION_NORMAL_OUTSIDE);
  return 0;
}
