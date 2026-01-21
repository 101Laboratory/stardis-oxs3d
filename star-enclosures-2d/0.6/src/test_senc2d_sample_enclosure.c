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

#include <rsys/float2.h>
#include <rsys/double2.h>

#include <star/s2d.h>
#include <star/ssp.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct senc2d_enclosure* enclosure = NULL;
  struct senc2d_enclosure_header header;
  struct s2d_device* s2d = NULL;
  struct s2d_scene* s2d_scn = NULL;
  struct s2d_scene_view* s2d_view = NULL;
  struct s2d_shape* s2d_shp = NULL;
  struct s2d_primitive prim;
  struct s2d_vertex_data vrtx_get;
  struct ssp_rng* rng;
  struct context ctx = CONTEXT_NULL__;
  int i;
  float st;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));

  /* A 2D square, but with a hole (incomplete).
   * 1 single enclosure including both sides of segments */
  ctx.positions = square_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium0;

  OK(senc2d_scene_create(dev,
    SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE,
    nsegments - 1, get_indices, get_media, nvertices, get_position, &ctx,
    &scn));

  OK(senc2d_scene_get_enclosure(scn, 0, &enclosure));
  OK(senc2d_enclosure_get_header(enclosure, &header));

  /* Put enclosure in a 2D view... */
  vrtx_get.type = S2D_FLOAT2;
  vrtx_get.usage = S2D_POSITION;
  vrtx_get.get = senc2d_sXd_enclosure_get_position;
  S2D(device_create(NULL, &allocator, 0, &s2d));
  S2D(scene_create(s2d, &s2d_scn));
  S2D(shape_create_line_segments(s2d, &s2d_shp));
  S2D(line_segments_setup_indexed_vertices(s2d_shp, header.primitives_count,
    senc2d_sXd_enclosure_get_indices, header.vertices_count, &vrtx_get, 1,
    enclosure));
  S2D(scene_attach_shape(s2d_scn, s2d_shp));
  S2D(scene_view_create(s2d_scn, S2D_SAMPLE, &s2d_view));

  /* ... and sample it. */
  OK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng));
  FOR_EACH(i, 0, 10000) {
    struct s2d_attrib attrib;
    int n, c;
    S2D(scene_view_sample(s2d_view, ssp_rng_canonical_float(rng),
      ssp_rng_canonical_float(rng), &prim, &st));
    S2D(primitive_get_attrib(&prim, S2D_POSITION, st, &attrib));
    c = 0;
    FOR_EACH(n, 0, 2)
      if(eq_eps(attrib.value[n], 0, FLT_EPSILON)
        || eq_eps(attrib.value[n], 1, FLT_EPSILON))
        c++;
    CHK(c == 1);
    S2D(primitive_get_attrib(&prim, S2D_GEOMETRY_NORMAL, st, &attrib));
    c = 0;
    FOR_EACH(n, 0, 2)
      if(eq_eps(attrib.value[n], -1, FLT_EPSILON)
        || eq_eps(attrib.value[n], 1, FLT_EPSILON))
        c++;
    CHK(c == 1);
    c = 0;
    FOR_EACH(n, 0, 2)
      if(eq_eps(attrib.value[n], 0, FLT_EPSILON))
        c++;
    CHK(c == 1);
  }

  SENC2D(enclosure_ref_put(enclosure));
  SENC2D(scene_ref_put(scn));
  SENC2D(device_ref_put(dev));

  SSP(rng_ref_put(rng));

  S2D(shape_ref_put(s2d_shp));
  S2D(scene_view_ref_put(s2d_view));
  S2D(device_ref_put(s2d));
  S2D(scene_ref_put(s2d_scn));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}
