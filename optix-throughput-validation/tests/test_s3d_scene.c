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
#include "test_s3d_utils.h"
#include "test_s3d_cbox.h"

#include <rsys_math.h>

static const float cube_verts[] = {
  5.f, 5.f, 5.f,
  6.f, 5.f, 5.f,
  5.f, 6.f, 5.f,
  6.f, 6.f, 5.f,
  5.f, 5.f, 6.f,
  6.f, 5.f, 6.f,
  5.f, 6.f, 6.f,
  6.f, 6.f, 6.f
};
static const unsigned cube_nverts = sizeof(cube_verts) / (sizeof(float)*3);

/* Front faces are CW. The normals point into the cube */
static const unsigned cube_ids[] = {
  0, 2, 1, 1, 2, 3, /* Front */
  0, 4, 2, 2, 4, 6, /* Left */
  4, 5, 6, 6, 5, 7, /* Back */
  3, 7, 1, 1, 7, 5, /* Right */
  2, 6, 3, 3, 6, 7, /* Top */
  0, 1, 4, 4, 1, 5 /* Bottom */
};
static const unsigned cube_ntris = sizeof(cube_ids) / (sizeof(unsigned)*3);

static void
cube_get_ids(const unsigned itri, unsigned ids[3], void* data)
{
  const unsigned id = itri * 3;
  CHK(data == NULL);
  CHK(ids != NULL);
  CHK(itri < cube_ntris);
  ids[0] = cube_ids[id + 0];
  ids[1] = cube_ids[id + 1];
  ids[2] = cube_ids[id + 2];
}

static void
cube_get_pos(const unsigned ivert, float pos[3], void* data)
{
  const unsigned i = ivert*3;
  CHK(data == NULL);
  CHK(pos != NULL);
  CHK(ivert < cube_nverts);
  pos[0] = cube_verts[i + 0];
  pos[1] = cube_verts[i + 1];
  pos[2] = cube_verts[i + 2];
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_primitive prims[10];
  struct s3d_device* dev, *dev2;
  struct s3d_scene* scn;
  struct s3d_scene* scn2;
  struct s3d_scene* scn3;
  struct s3d_scene_view* scnview;
  struct s3d_scene_view* scnview2;
  struct s3d_scene_view* scnview3;
  struct s3d_vertex_data attribs;
  struct s3d_shape* shapes[4];
  const size_t nshapes = sizeof(shapes)/sizeof(struct s3d_shape*);
  void* data = (void*)&cbox_walls_desc;
  size_t i, n;
  size_t nprims;
  float area, volume, lower[3], upper[3];
  unsigned id;
  int mask;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  FOR_EACH(i, 0, nshapes)
    CHK(s3d_shape_create_mesh(dev, shapes + i) == RES_OK);

  CHK(s3d_scene_create(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_create(dev, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_create(NULL, &scn) == RES_BAD_ARG);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);
  CHK(s3d_scene_create(dev, &scn3) == RES_OK);

  CHK(s3d_scene_get_shapes_count(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_get_shapes_count(scn, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_get_shapes_count(NULL, &n) == RES_BAD_ARG);
  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == 0);

  CHK(s3d_scene_attach_shape(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_attach_shape(scn, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_attach_shape(NULL, shapes[0]) == RES_BAD_ARG);
  CHK(s3d_scene_attach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == 1);
  CHK(s3d_scene_attach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == 1);

  CHK(s3d_scene_detach_shape(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_detach_shape(scn, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_detach_shape(NULL, shapes[0]) == RES_BAD_ARG);
  CHK(s3d_scene_detach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_detach_shape(scn, shapes[0]) == RES_BAD_ARG);
  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == 0);

  FOR_EACH(i, 1, nshapes) {
    CHK(s3d_scene_attach_shape(scn, shapes[i]) == RES_OK);
    CHK(s3d_shape_ref_put(shapes[i]) == RES_OK);
  }
  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == nshapes - 1);

  CHK(s3d_scene_instantiate(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_instantiate(scn, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_instantiate(NULL, shapes + 1) == RES_BAD_ARG);
  CHK(s3d_scene_instantiate(scn, shapes + 1) == RES_OK);

  CHK(s3d_shape_get_id(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_get_id(shapes[1], NULL) == RES_BAD_ARG);
  CHK(s3d_shape_get_id(NULL, &id) == RES_BAD_ARG);
  CHK(s3d_shape_get_id(shapes[1], &id) == RES_OK);
  CHK(id != S3D_INVALID_ID);

  CHK(s3d_scene_clear(NULL) == RES_BAD_ARG);
  CHK(s3d_scene_clear(scn) == RES_OK);
  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == 0);
  CHK(s3d_scene_clear(scn) == RES_OK);
  CHK(s3d_scene_instantiate(scn, shapes + 2) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shapes[2]) == RES_BAD_ARG);

  CHK(s3d_scene_attach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == 1);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK(mask == S3D_TRACE);

  CHK(s3d_scene_detach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_clear(scn) == RES_OK);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_detach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shapes[0]) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_attach_shape(scn2, shapes[1]) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, shapes[2]) == RES_OK);
  CHK(s3d_scene_attach_shape(scn3, shapes[1]) == RES_OK);

  CHK(s3d_scene_get_shapes_count(scn, &n) == RES_OK);
  CHK(n == 1);
  CHK(s3d_scene_get_shapes_count(scn2, &n) == RES_OK);
  CHK(n == 2);
  CHK(s3d_scene_get_shapes_count(scn3, &n) == RES_OK);
  CHK(n == 1);

  CHK(s3d_scene_view_create(scn2, S3D_SAMPLE|S3D_TRACE, &scnview2) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn3, S3D_SAMPLE, &scnview3) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);

  CHK(s3d_scene_view_compute_area(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_compute_area(scnview2, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_compute_area(NULL, &area) == RES_BAD_ARG);
  CHK(s3d_scene_view_compute_area(scnview2, &area) == RES_OK);
  CHK(area == 0.f);

  CHK(s3d_scene_view_compute_volume(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_compute_volume(scnview2, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_compute_volume(NULL, &volume) == RES_BAD_ARG);
  CHK(s3d_scene_view_compute_volume(scnview2, &volume) == RES_OK);
  CHK(volume == 0.f);

  CHK(s3d_scene_view_primitives_count(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_primitives_count(scnview2, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_primitives_count(NULL, &nprims) == RES_BAD_ARG);
  CHK(s3d_scene_view_primitives_count(scnview2, &nprims) == RES_OK);
  CHK(nprims == 0);

  CHK(s3d_scene_view_get_aabb(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(scnview2, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(NULL, lower, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(scnview2, lower, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(NULL, NULL, upper) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(scnview2, NULL, upper) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(NULL, lower, upper) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(scnview2, lower, upper) == RES_OK);
  CHK(lower[0] > upper[0]);
  CHK(lower[1] > upper[1]);
  CHK(lower[2] > upper[2]);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview2) == RES_OK);

  CHK(s3d_scene_instantiate(scn2, shapes + 3) == RES_OK);
  CHK(s3d_scene_attach_shape(scn3, shapes[3]) == RES_OK);
  CHK(s3d_scene_get_shapes_count(scn3, &n) == RES_OK);
  CHK(n == 2);
  CHK(s3d_scene_view_create(scn3, S3D_SAMPLE|S3D_TRACE, &scnview3) == RES_BAD_ARG);

  CHK(s3d_scene_detach_shape(scn, shapes[0]) == RES_OK);

  CHK(s3d_shape_ref_put(shapes[0]) == RES_OK);
  CHK(s3d_shape_ref_put(shapes[1]) == RES_OK);
  CHK(s3d_shape_ref_put(shapes[2]) == RES_OK);
  CHK(s3d_shape_ref_put(shapes[3]) == RES_OK);

  CHK(s3d_scene_ref_get(NULL) == RES_BAD_ARG);
  CHK(s3d_scene_ref_get(scn) == RES_OK);
  CHK(s3d_scene_ref_put(NULL) == RES_BAD_ARG);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_scene_ref_put(scn3) == RES_OK);

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);

  attribs.type = S3D_FLOAT3;
  attribs.usage = S3D_POSITION;
  attribs.get = cbox_get_position;
  CHK(s3d_shape_create_mesh(dev, shapes + 0) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shapes[0], cbox_walls_ntris,
    cbox_get_ids, cbox_walls_nverts, &attribs, 1, data) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shapes[0]) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, 1532296.f, 1.e-6f) == 1);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 10);
  CHK(s3d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(eq_epsf(lower[0], 0.f, 1.e-6f) == 1);
  CHK(eq_epsf(lower[1], 0.f, 1.e-6f) == 1);
  CHK(eq_epsf(lower[2], 0.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[0], 552.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[1], 559.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[2], 548.f, 1.e-6f) == 1);

  CHK(s3d_scene_instantiate(scn, shapes + 1) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, shapes[1]) == RES_OK);

  CHK(s3d_scene_view_create(scn2, S3D_GET_PRIMITIVE, &scnview2) == RES_OK);
  CHK(s3d_scene_view_compute_area(scnview2, &area) == RES_OK);
  CHK(eq_epsf(area, 1532296.f, 1.e-6f) == 1);
  CHK(s3d_scene_view_primitives_count(scnview2, &nprims) == RES_OK);
  CHK(nprims == 10);
  CHK(s3d_scene_view_get_aabb(scnview2, lower, upper) == RES_OK);
  CHK(eq_epsf(lower[0], 0.f, 1.e-6f) == 1);
  CHK(eq_epsf(lower[1], 0.f, 1.e-6f) == 1);
  CHK(eq_epsf(lower[2], 0.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[0], 552.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[1], 559.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[2], 548.f, 1.e-6f) == 1);

  CHK(s3d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, 1532296.f, 1.e-6f) == 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_view_get_primitive(NULL, 11, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_primitive(scnview2, 11, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_primitive(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_primitive(scnview2, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_primitive(NULL, 11, prims + 0) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_primitive(scnview2, 11, prims + 0) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_primitive(NULL, 0, prims + 0) == RES_BAD_ARG);

  FOR_EACH(i, 0, nprims) {
    size_t j;
    CHK(s3d_scene_view_get_primitive(scnview2, (unsigned)i, prims + i) == RES_OK);
    CHK(S3D_PRIMITIVE_EQ(prims + i, &S3D_PRIMITIVE_NULL) == 0);
    CHK(prims[i].scene_prim_id == i);
    FOR_EACH(j, 0, i)
      CHK(S3D_PRIMITIVE_EQ(prims + i, prims + j) == 0);
  }
  CHK(s3d_scene_view_ref_put(scnview2) == RES_OK);

  attribs.type = S3D_FLOAT3;
  attribs.usage = S3D_POSITION;
  attribs.get = cube_get_pos;
  CHK(s3d_mesh_setup_indexed_vertices
    (shapes[0], cube_ntris, cube_get_ids, cube_nverts, &attribs, 1, NULL) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, 6.f, 1.e-6f) == 1);
  CHK(s3d_scene_view_compute_volume(scnview, &volume) == RES_OK);
  CHK(eq_epsf(volume, 1.f, 1.e-6f) == 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_shape_flip_surface(shapes[0]) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_compute_volume(scnview, &volume) == RES_OK);
  CHK(eq_epsf(volume, -1.f, 1.e-6f) == 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_get_device(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_get_device(scn, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_get_device(NULL, &dev2) == RES_BAD_ARG);
  CHK(s3d_scene_get_device(scn, &dev2) == RES_OK);
  CHK(dev2 == dev);
  CHK(s3d_scene_get_device(scn2, &dev2) == RES_OK);
  CHK(dev2 == dev);

  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_shape_ref_put(shapes[0]) == RES_OK);
  CHK(s3d_shape_ref_put(shapes[1]) == RES_OK);

  CHK(s3d_device_ref_put(dev) == RES_OK);;

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

