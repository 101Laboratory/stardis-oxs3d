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

#include <rsys/float3.h>
#include <rsys/double3.h>

#include <star/s3d.h>
#include <star/ssp.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct senc3d_enclosure* enclosure = NULL;
  struct senc3d_enclosure_header header;
  struct s3d_device* s3d = NULL;
  struct s3d_scene* s3d_scn = NULL;
  struct s3d_scene_view* s3d_view = NULL;
  struct s3d_shape* s3d_shp = NULL;
  struct s3d_primitive prim;
  struct s3d_vertex_data vrtx_get;
  struct ssp_rng* rng;
  struct context ctx = CONTEXT_NULL__;
  int i;
  float st[2];
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* A 3D cube, but with a hole (incomplete).
   * 1 single enclosure including both sides of triangles */
  ctx.positions = cube_vertices;
  ctx.indices = box_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium0;

  OK(senc3d_scene_create(dev,
    SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE,
    ntriangles - 1, get_indices, get_media, nvertices, get_position, &ctx,
    &scn));

  OK(senc3d_scene_get_enclosure(scn, 0, &enclosure));
  OK(senc3d_enclosure_get_header(enclosure, &header));

  /* Put enclosure in a 3D view... */
  vrtx_get.type = S3D_FLOAT3;
  vrtx_get.usage = S3D_POSITION;
  vrtx_get.get = senc3d_sXd_enclosure_get_position;
  S3D(device_create(NULL, &allocator, 0, &s3d));
  S3D(scene_create(s3d, &s3d_scn));
  S3D(shape_create_mesh(s3d, &s3d_shp));
  S3D(mesh_setup_indexed_vertices(s3d_shp, header.primitives_count,
    senc3d_sXd_enclosure_get_indices, header.vertices_count,
    &vrtx_get, 1, enclosure));
  S3D(scene_attach_shape(s3d_scn, s3d_shp));
  S3D(scene_view_create(s3d_scn, S3D_SAMPLE, &s3d_view));

  /* ... and sample it. */
  OK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng));
  FOR_EACH(i, 0, 10000) {
    struct s3d_attrib attrib;
    int n, c;
    S3D(scene_view_sample(s3d_view,
      ssp_rng_canonical_float(rng),
      ssp_rng_canonical_float(rng),
      ssp_rng_canonical_float(rng),
      &prim, st));
    S3D(primitive_get_attrib(&prim, S3D_POSITION, st, &attrib));
    c = 0;
    FOR_EACH(n, 0, 3)
      if(eq_eps(attrib.value[n], 0, FLT_EPSILON)
        || eq_eps(attrib.value[n], 1, FLT_EPSILON))
        c++;
    CHK(c == 1);
    S3D(primitive_get_attrib(&prim, S3D_GEOMETRY_NORMAL, st, &attrib));
    c = 0;
    FOR_EACH(n, 0, 3)
      if(eq_eps(attrib.value[n], -1, FLT_EPSILON)
        || eq_eps(attrib.value[n], 1, FLT_EPSILON))
        c++;
    CHK(c == 1);
    c = 0;
    FOR_EACH(n, 0, 3)
      if(eq_eps(attrib.value[n], 0, FLT_EPSILON))
        c++;
    CHK(c == 2);
  }

  SENC3D(enclosure_ref_put(enclosure));
  SENC3D(scene_ref_put(scn));
  SENC3D(device_ref_put(dev));

  SSP(rng_ref_put(rng));

  S3D(shape_ref_put(s3d_shp));
  S3D(scene_view_ref_put(s3d_view));
  S3D(device_ref_put(s3d));
  S3D(scene_ref_put(s3d_scn));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}
