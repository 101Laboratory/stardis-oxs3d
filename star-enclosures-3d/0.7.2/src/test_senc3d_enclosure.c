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
#include "senc3d_sXd_helper.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

#include <star/s3d.h>

static void
test(const int convention)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct senc3d_enclosure* enclosures[2] = { NULL, NULL };
  struct senc3d_enclosure* enclosure;
  struct senc3d_enclosure_header header;
  struct s3d_device* s3d = NULL;
  struct s3d_scene* s3d_scn = NULL;
  struct s3d_shape* s3d_shp = NULL;
  struct s3d_vertex_data s3d_attribs;
  unsigned indices[2][3];
  unsigned medium;
  unsigned gid;
  enum senc3d_side side;
  double vrtx[3];
  double ext_v = 0;
  struct context ctx = CONTEXT_NULL__;
  unsigned i, n, t, count;
  int conv;
  const int conv_front = (convention & SENC3D_CONVENTION_NORMAL_FRONT) != 0;
  const int conv_in = (convention & SENC3D_CONVENTION_NORMAL_INSIDE) != 0;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* A 3D cube.
   * 2 enclosures (inside, outside) sharing the same triangles,
   * but opposite sides */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;

  OK(senc3d_scene_create(dev, convention, ntriangles, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc3d_scene_get_convention(scn, &conv));
  CHK(conv == convention);

  OK(senc3d_scene_get_enclosure_count(scn, &count));
  CHK(count == 2);

  OK(senc3d_scene_get_enclosure(scn, 0, &enclosure));
  BA(senc3d_enclosure_ref_get(NULL));
  OK(senc3d_enclosure_ref_get(enclosure));
  BA(senc3d_enclosure_ref_put(NULL));
  OK(senc3d_enclosure_ref_put(enclosure));

  BA(senc3d_enclosure_get_triangle(NULL, 0, indices[0]));
  BA(senc3d_enclosure_get_triangle(enclosure, ntriangles, indices[0]));
  BA(senc3d_enclosure_get_triangle(enclosure, 0, NULL));
  BA(senc3d_enclosure_get_triangle(NULL, ntriangles, indices[0]));
  BA(senc3d_enclosure_get_triangle(NULL, 0, NULL));
  BA(senc3d_enclosure_get_triangle(enclosure, ntriangles, NULL));
  BA(senc3d_enclosure_get_triangle(NULL, ntriangles, NULL));
  OK(senc3d_enclosure_get_triangle(enclosure, 0, indices[0]));

  BA(senc3d_enclosure_get_vertex(NULL, 0, vrtx));
  BA(senc3d_enclosure_get_vertex(enclosure, nvertices, vrtx));
  BA(senc3d_enclosure_get_vertex(enclosure, 0, NULL));
  BA(senc3d_enclosure_get_vertex(NULL, nvertices, vrtx));
  BA(senc3d_enclosure_get_vertex(NULL, 0, NULL));
  BA(senc3d_enclosure_get_vertex(enclosure, nvertices, NULL));
  BA(senc3d_enclosure_get_vertex(NULL, nvertices, NULL));
  OK(senc3d_enclosure_get_vertex(enclosure, 0, vrtx));

  BA(senc3d_enclosure_get_triangle_id(NULL, 0, &gid, NULL));
  BA(senc3d_enclosure_get_triangle_id(enclosure, ntriangles, &gid, NULL));
  BA(senc3d_enclosure_get_triangle_id(enclosure, 0, NULL, NULL));
  BA(senc3d_enclosure_get_triangle_id(NULL, ntriangles, &gid, NULL));
  BA(senc3d_enclosure_get_triangle_id(NULL, 0, NULL, NULL));
  BA(senc3d_enclosure_get_triangle_id(enclosure, ntriangles, NULL, NULL));
  BA(senc3d_enclosure_get_triangle_id(NULL, ntriangles, NULL, NULL));
  BA(senc3d_enclosure_get_triangle_id(enclosure, 0, &gid, NULL));
  BA(senc3d_enclosure_get_triangle_id(NULL, 0, &gid, &side));
  BA(senc3d_enclosure_get_triangle_id(enclosure, ntriangles, &gid, &side));
  BA(senc3d_enclosure_get_triangle_id(enclosure, 0, NULL, &side));
  BA(senc3d_enclosure_get_triangle_id(NULL, ntriangles, &gid, &side));
  BA(senc3d_enclosure_get_triangle_id(NULL, 0, NULL, &side));
  BA(senc3d_enclosure_get_triangle_id(enclosure, ntriangles, NULL, &side));
  BA(senc3d_enclosure_get_triangle_id(NULL, ntriangles, NULL, &side));
  OK(senc3d_enclosure_get_triangle_id(enclosure, 0, &gid, &side));

  BA(senc3d_enclosure_get_medium(NULL, 0, &medium));
  BA(senc3d_enclosure_get_medium(enclosure, 2, &medium));
  BA(senc3d_enclosure_get_medium(enclosure, 0, NULL));
  BA(senc3d_enclosure_get_medium(NULL, 2, &medium));
  BA(senc3d_enclosure_get_medium(NULL, 0, NULL));
  BA(senc3d_enclosure_get_medium(enclosure, 2, NULL));
  BA(senc3d_enclosure_get_medium(NULL, 2, NULL));
  OK(senc3d_enclosure_get_medium(enclosure, 0, &medium));

  OK(senc3d_enclosure_ref_put(enclosure));

  FOR_EACH(i, 0, count) {
    OK(senc3d_scene_get_enclosure(scn, i, &enclosure));

    BA(senc3d_enclosure_get_header(NULL, &header));
    BA(senc3d_enclosure_get_header(enclosure, NULL));
    BA(senc3d_enclosure_get_header(NULL, NULL));
    OK(senc3d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);
    CHK(header.enclosed_media_count == 1);
    OK(senc3d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the cube in input triangles:
     * if convention is front, front medium (0) is outside,
     * that is medium 0's enclosure is infinite */
    CHK(conv_front == ((medium == 0) == header.is_infinite));
    CHK(header.primitives_count == ntriangles);
    CHK(header.unique_primitives_count == ntriangles);
    CHK(header.vertices_count == nvertices);
    CHK(header.is_infinite == (i == 0));
    if(i == 0) ext_v = header.volume;
    else CHK(header.volume == -ext_v);

    FOR_EACH(t, 0, header.primitives_count) {
      OK(senc3d_enclosure_get_triangle_id(enclosure, t, &gid, &side));
      CHK(gid == t);
      CHK(side == (medium == 0) ? SENC3D_FRONT : SENC3D_BACK);
    }

    OK(senc3d_enclosure_ref_put(enclosure));
  }

  FOR_EACH(i, 0, 2)
    OK(senc3d_scene_get_enclosure(scn, i, enclosures + i));
  FOR_EACH(n, 0, ntriangles) {
    int same, reversed;
    /* Read same triangles in both enclosures */
    FOR_EACH(i, 0, 2)
      OK(senc3d_enclosure_get_triangle(enclosures[i], n, indices[i]));
    /* Same triangles and opposite sides for the 2 enclosures */
    FOR_EACH(i, 0, 3) CHK(indices[0][i] == indices[1][2 - i]);
    /* Enclosure 0 is outside (and contains medium 0 if convention is front).
     * Geometrical normals in output data point in the same direction that those
     * of input triangles for enclosure 0 iff convention is inside.
     * The opposite holds for enclosure 1. */
    cmp_trg(n, enclosures[0], box_indices + 3 * n, box_vertices, &same, &reversed);
    CHK((same && !reversed) == conv_in);
    cmp_trg(n, enclosures[1], box_indices + 3 * n, box_vertices, &same, &reversed);
    CHK(same && reversed == conv_in);
  }
  FOR_EACH(i, 0, 2)
    OK(senc3d_enclosure_ref_put(enclosures[i]));

  OK(senc3d_scene_ref_put(scn));

  /* Same 3D cube, but with a hole (incomplete).
   * 1 single enclosure including both sides of triangles */

  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;

  OK(senc3d_scene_create(dev, convention, ntriangles - 1, get_indices, get_media,
    nvertices, get_position, &ctx, &scn));

  OK(senc3d_scene_get_frontier_segments_count(scn, &count));
  CHK(count == 3);

  OK(senc3d_scene_get_enclosure_count(scn, &count));
  CHK(count == 1);

#ifdef DUMP_ENCLOSURES
  dump_enclosure(scn, 0, "test_enclosure_hole.obj");
#endif

  OK(senc3d_scene_get_enclosure(scn, 0, &enclosure));

  BA(senc3d_enclosure_get_header(NULL, &header));
  BA(senc3d_enclosure_get_header(enclosure, NULL));
  BA(senc3d_enclosure_get_header(NULL, NULL));
  OK(senc3d_enclosure_get_header(enclosure, &header));

  CHK(header.enclosure_id == 0);
  CHK(header.enclosed_media_count == 2);
  CHK(header.primitives_count == 2 * header.unique_primitives_count);
  CHK(header.unique_primitives_count == ntriangles - 1);
  CHK(header.vertices_count == nvertices);
  CHK(header.is_infinite == 1);
  CHK(header.volume == 0);

  FOR_EACH(t, 0, header.primitives_count) {
    OK(senc3d_enclosure_get_triangle_id(enclosure, t, &gid, &side));
    /* The first unique_triangle_count triangles of an enclosure
     * are unique triangles */
    if(t < header.unique_primitives_count) CHK(gid == t);
    CHK(side == (t < header.unique_primitives_count) ? SENC3D_FRONT : SENC3D_BACK);
  }

  /* Put geometry in a 3D view using helper functions */
  s3d_attribs.type = S3D_FLOAT3;
  s3d_attribs.usage = S3D_POSITION;
  s3d_attribs.get = senc3d_sXd_enclosure_get_position;
  OK(s3d_device_create(NULL, &allocator, 0, &s3d));
  OK(s3d_scene_create(s3d, &s3d_scn));
  OK(s3d_shape_create_mesh(s3d, &s3d_shp));
  OK(s3d_mesh_setup_indexed_vertices(s3d_shp, header.primitives_count,
    senc3d_sXd_enclosure_get_indices, header.vertices_count, &s3d_attribs,
    1, enclosure));
  OK(s3d_scene_attach_shape(s3d_scn, s3d_shp));
  S3D(shape_ref_put(s3d_shp));
  S3D(device_ref_put(s3d));
  S3D(scene_ref_put(s3d_scn));

  SENC3D(scene_ref_put(scn));
  SENC3D(device_ref_put(dev));
  SENC3D(enclosure_ref_put(enclosure));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

int
main(int argc, char** argv)
{
  (void) argc, (void) argv;
  test(SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE);
  test(SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_INSIDE);
  test(SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_OUTSIDE);
  test(SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_OUTSIDE);
  return 0;
}
