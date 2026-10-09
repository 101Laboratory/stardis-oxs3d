/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#include "s3d.h"
#include "test_s3d_cbox.h"
#include "test_s3d_utils.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys_math.h>

static const float plane_verts[] = {
  0.f, 0.f, 0.f,
  1.f, 0.f, 0.f,
  1.f, 1.f, 0.f,
  0.f, 1.f, 0.f
};
static const unsigned plane_nverts = sizeof(plane_verts) / (sizeof(float)*3);

static const unsigned plane_ids[] = { 0, 1, 2, 2, 3, 0 };
static const unsigned plane_ntris = sizeof(plane_ids) / (sizeof(unsigned)*3);

static void
plane_get_ids(const unsigned itri, unsigned ids[3], void* data)
{
  const unsigned id = itri * 3;
  (void)data;
  CHK(ids != NULL);
  CHK(itri < plane_ntris);
  ids[0] = plane_ids[id + 0];
  ids[1] = plane_ids[id + 1];
  ids[2] = plane_ids[id + 2];
}

static void
plane_get_pos(const unsigned ivert, float pos[3], void* data)
{
  const unsigned i = ivert*3;
  (void)data;
  CHK(pos != NULL);
  CHK(ivert < plane_nverts);
  pos[0] = plane_verts[i + 0];
  pos[1] = plane_verts[i + 1];
  pos[2] = plane_verts[i + 2];
}

static void
plane_get_uv(const unsigned ivert, float uv[2], void* data)
{
  const unsigned i = ivert*3;
  (void)data;
  CHK(uv != NULL);
  CHK(ivert < plane_nverts);
  uv[0] = -plane_verts[i + 0];
  uv[1] = -plane_verts[i + 1];
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  struct s3d_scene* scn;
  struct s3d_scene_view* scnview;
  struct s3d_shape* walls;
  struct s3d_shape* plane;
  struct s3d_attrib attr;
  struct s3d_primitive prim = S3D_PRIMITIVE_NULL;
  struct s3d_vertex_data attribs[2];
  struct cbox_desc desc;
  size_t nprims;
  size_t i;
  float transform[12];
  float vec[3];
  float uv[2];
  float area;
  unsigned ntris, nverts;
  unsigned walls_id;
  char b;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &plane) == RES_OK);
  CHK(s3d_shape_get_id(walls, &walls_id) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);

  attribs[1].usage = S3D_ATTRIB_0;
  attribs[1].type = S3D_FLOAT2;
  attribs[1].get = plane_get_uv;

  attribs[0].usage = S3D_POSITION;
  attribs[0].type = S3D_FLOAT3;
  attribs[0].get = cbox_get_position;

  ntris = cbox_walls_ntris;
  nverts = cbox_walls_nverts;
  desc.vertices = cbox_walls;
  desc.indices = cbox_walls_ids;
  CHK(s3d_mesh_setup_indexed_vertices
    (walls, ntris, cbox_get_ids, nverts, attribs, 1, &desc) == RES_OK);

  attribs[0].get = plane_get_pos;
  CHK(s3d_mesh_setup_indexed_vertices
    (plane, plane_ntris, plane_get_ids, plane_nverts, attribs, 2, NULL) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0, 0, 0, &prim, uv) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_primitive_get_attrib(NULL, S3D_ATTRIBS_COUNT__, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_ATTRIBS_COUNT__, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(NULL, S3D_ATTRIBS_COUNT__, uv, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_ATTRIBS_COUNT__, uv, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(NULL, S3D_ATTRIBS_COUNT__, NULL, &attr) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_ATTRIBS_COUNT__, NULL, &attr) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(NULL, S3D_ATTRIBS_COUNT__, uv, &attr) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_ATTRIBS_COUNT__, uv, &attr) == RES_BAD_ARG);

  CHK(s3d_primitive_get_attrib(NULL, S3D_POSITION, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(NULL, S3D_POSITION, uv, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(NULL, S3D_POSITION, NULL, &attr) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, NULL, &attr) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(NULL, S3D_POSITION, uv, &attr) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT3);
  CHK(attr.usage == S3D_POSITION);
  CHK(s3d_primitive_get_attrib(&prim, S3D_GEOMETRY_NORMAL, uv, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT3);
  CHK(attr.usage == S3D_GEOMETRY_NORMAL);
  CHK(s3d_primitive_get_attrib(&prim, S3D_ATTRIB_0, uv, &attr) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&S3D_PRIMITIVE_NULL, S3D_POSITION, uv, &attr) == RES_BAD_ARG);

  CHK(s3d_primitive_has_attrib(NULL, S3D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_has_attrib(&prim, S3D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_has_attrib(NULL, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_has_attrib(&prim, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_has_attrib(NULL, S3D_ATTRIBS_COUNT__, &b) == RES_BAD_ARG);
  CHK(s3d_primitive_has_attrib(&prim, S3D_ATTRIBS_COUNT__, &b) == RES_BAD_ARG);
  CHK(s3d_primitive_has_attrib(NULL, S3D_POSITION, &b) == RES_BAD_ARG);
  CHK(s3d_primitive_has_attrib(&prim, S3D_POSITION, &b) == RES_OK);
  CHK(b == 1);
  CHK(s3d_primitive_has_attrib(&prim, S3D_GEOMETRY_NORMAL, &b) == RES_OK);
  CHK(b == 1);
  CHK(s3d_primitive_has_attrib(&prim, S3D_ATTRIB_0, &b) == RES_OK);
  CHK(b == 0);

  CHK(s3d_primitive_get_transform(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_transform(&prim, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_get_transform(NULL, transform) == RES_BAD_ARG);
  CHK(s3d_primitive_get_transform(&prim, transform) == RES_OK);
  CHK(f3_eq(transform + 0, f3(vec, 1.f, 0.f, 0.f)) == 1);
  CHK(f3_eq(transform + 3, f3(vec, 0.f, 1.f, 0.f)) == 1);
  CHK(f3_eq(transform + 6, f3(vec, 0.f, 0.f, 1.f)) == 1);
  CHK(f3_eq(transform + 9, f3(vec, 0.f, 0.f, 0.f)) == 1);

  CHK(s3d_scene_clear(scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, plane) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 2);
  CHK(s3d_scene_view_get_primitive(scnview, 0, &prim) == RES_OK);
  CHK(S3D_PRIMITIVE_EQ(&prim, &S3D_PRIMITIVE_NULL) == 0);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_primitive_has_attrib(&prim, S3D_ATTRIB_0, &b) == RES_OK);
  CHK(b == 1);
  CHK(s3d_primitive_has_attrib(&prim, S3D_ATTRIB_1, &b) == RES_OK);
  CHK(b == 0);

  CHK(s3d_primitive_compute_area(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_compute_area(&prim, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_compute_area(NULL, &area) == RES_BAD_ARG);
  CHK(s3d_primitive_compute_area(&prim, &area) == RES_OK);
  CHK(eq_epsf(area, 0.5f, 1.e-6f) == 1);

  CHK(s3d_primitive_sample(NULL,  1.f, 1.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 1.f, 1.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(NULL, 0.f, 1.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 0.f, 1.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(NULL,  1.f, 0.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 1.f, 0.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(NULL, 0.f, 0.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 0.f, 0.f, NULL) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(NULL,  1.f, 1.f, uv) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 1.f, 1.f, uv) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(NULL, 0.f, 1.f, uv) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 0.f, 1.f, uv) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(NULL,  1.f, 0.f, uv) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 1.f, 0.f, uv) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(NULL, 0.f, 0.f, uv) == RES_BAD_ARG);
  CHK(s3d_primitive_sample(&prim, 0.f, 0.f, uv) == RES_OK);

  FOR_EACH(i, 0, 4096) {
    CHK(s3d_primitive_sample
      (&prim, rand_canonic(), rand_canonic(), uv) == RES_OK);
    CHK(uv[0] >= 0.f);
    CHK(uv[0] <= 1.f);
    CHK(uv[1] >= 0.f);
    CHK(uv[1] <= 1.f);
  }

  #define GET_VERTEX_ATTR s3d_triangle_get_vertex_attrib
  CHK(GET_VERTEX_ATTR(NULL, 3, S3D_GEOMETRY_NORMAL, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 3, S3D_GEOMETRY_NORMAL, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(NULL, 0, S3D_GEOMETRY_NORMAL, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 0, S3D_GEOMETRY_NORMAL, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(NULL, 3, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 3, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(NULL, 0, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 0, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(NULL, 3, S3D_GEOMETRY_NORMAL, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 3, S3D_GEOMETRY_NORMAL, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(NULL, 0, S3D_GEOMETRY_NORMAL, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 0, S3D_GEOMETRY_NORMAL, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(NULL, 3, S3D_POSITION, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 3, S3D_POSITION, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(NULL, 0, S3D_POSITION, &attr) == RES_BAD_ARG);

  CHK(GET_VERTEX_ATTR(&prim, 0, S3D_POSITION, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT3);
  CHK(f3_eq_eps(attr.value, plane_verts + plane_ids[0]*3, 1.e-6f) == 1);
  CHK(GET_VERTEX_ATTR(&prim, 1, S3D_POSITION, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT3);
  CHK(f3_eq_eps(attr.value, plane_verts + plane_ids[1]*3, 1.e-6f) == 1);
  CHK(GET_VERTEX_ATTR(&prim, 2, S3D_POSITION, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT3);
  CHK(f3_eq_eps(attr.value, plane_verts + plane_ids[2]*3, 1.e-6f) == 1);

  CHK(GET_VERTEX_ATTR(&prim, 0, S3D_ATTRIB_0, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT2);
  f2_minus(uv, plane_verts + plane_ids[0]*3);
  CHK(f2_eq_eps(attr.value, uv, 1.e-6f) == 1);
  CHK(GET_VERTEX_ATTR(&prim, 1, S3D_ATTRIB_0, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT2);
  f2_minus(uv, plane_verts + plane_ids[1]*3);
  CHK(f2_eq_eps(attr.value, uv, 1.e-6f) == 1);
  CHK(GET_VERTEX_ATTR(&prim, 2, S3D_ATTRIB_0, &attr) == RES_OK);
  CHK(attr.type == S3D_FLOAT2);
  f2_minus(uv, plane_verts + plane_ids[2]*3);
  CHK(f2_eq_eps(attr.value, uv, 1.e-6f) == 1);
  #undef GET_VERTEX_ATTR

  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_shape_ref_put(walls) == RES_OK);
  CHK(s3d_shape_ref_put(plane) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

