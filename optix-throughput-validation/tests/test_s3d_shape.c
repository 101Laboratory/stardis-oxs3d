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

#include <rsys/float3.h>
#include <rsys_math.h>

static int
filter_none
  (const struct s3d_hit* hit,
   const float org[3],
   const float dir[3],
   const float range[2],
   void* ray_data,
   void* filter_data)
{
  (void)hit, (void)org, (void)dir, (void)range, (void)ray_data, (void)filter_data;
  return 0;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  struct s3d_shape* shape;
  struct s3d_shape* shape_copy;
  struct s3d_shape* inst;
  struct s3d_scene* scn;
  struct s3d_vertex_data attribs[4];
  struct s3d_attrib attr;
  unsigned nverts, ntris;
  unsigned ids[3];
  float pos[3];
  float trans[12];
  const unsigned cbox_ntris = cbox_walls_ntris;
  const unsigned cbox_nverts = cbox_walls_nverts;
  unsigned id;
  void* data = (void*)&cbox_walls_desc;
  char c;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  fprintf(stderr, "[MARK] after device_create\n"); fflush(stderr);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  fprintf(stderr, "[MARK] after scene_create\n"); fflush(stderr);

  CHK(s3d_shape_create_mesh(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_create_mesh(dev, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_create_mesh(NULL, &shape) == RES_BAD_ARG);
  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);

  CHK(s3d_shape_get_id(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_get_id(shape, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_get_id(NULL, &id) == RES_BAD_ARG);
  CHK(s3d_shape_get_id(shape, &id) == RES_OK);
  CHK(id != S3D_INVALID_ID);

  CHK(s3d_scene_attach_shape(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_attach_shape(scn, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_attach_shape(NULL, shape) == RES_BAD_ARG);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);

  CHK(s3d_scene_detach_shape(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_detach_shape(scn, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_detach_shape(NULL, shape) == RES_BAD_ARG);
  CHK(s3d_scene_detach_shape(scn, shape) == RES_OK);

  attribs[0].type = S3D_FLOAT3;
  attribs[0].usage = S3D_POSITION;
  attribs[0].get = cbox_get_position;
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, cbox_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, cbox_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, cbox_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, cbox_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, NULL, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, NULL, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, NULL, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, NULL, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, cbox_get_ids, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, cbox_get_ids, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, cbox_get_ids, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, cbox_get_ids, cbox_nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, NULL, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, NULL, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, NULL, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, NULL, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, cbox_get_ids, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, cbox_get_ids, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, cbox_get_ids, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, cbox_get_ids, 0, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, NULL, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, NULL, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, NULL, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, NULL, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, 0, cbox_get_ids, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 0, cbox_get_ids, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (NULL, cbox_ntris, cbox_get_ids, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, cbox_get_ids, cbox_nverts, attribs, 1, data) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, cbox_get_ids, cbox_nverts, attribs, 0, data) == RES_BAD_ARG);

  attribs[0] = S3D_VERTEX_DATA_NULL;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, cbox_get_ids, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);

  attribs[0].type = S3D_FLOAT3;
  attribs[0].usage = S3D_POSITION;
  attribs[0].get = S3D_KEEP;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, cbox_get_ids, cbox_nverts, attribs, 1, data) == RES_OK);

  attribs[0].get = cbox_get_position;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts, attribs, 1, data) == RES_OK);

  attribs[0].type = S3D_FLOAT3;
  attribs[0].usage = S3D_ATTRIB_0;
  attribs[0].get = cbox_get_normal;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts, attribs, 1, data) == RES_BAD_ARG);

  attribs[1].type = S3D_FLOAT3;
  attribs[1].usage = S3D_POSITION;
  attribs[1].get = S3D_KEEP;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts, attribs, 2, data) == RES_OK);

  attribs[2].type = S3D_FLOAT2;
  attribs[2].usage = S3D_ATTRIB_2;
  attribs[2].get = cbox_get_uv;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts, attribs, 3, data) == RES_OK);

  attribs[0].get = S3D_KEEP;
  attribs[1].get = S3D_KEEP;
  attribs[2].get = S3D_KEEP;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, 2, S3D_KEEP, cbox_nverts, attribs, 3, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts+1, attribs, 3, data) == RES_BAD_ARG);
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts, attribs, 3, data) == RES_OK);

  attribs[2].type = S3D_FLOAT3;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts, attribs, 3, data) == RES_BAD_ARG);

  attribs[0].get = cbox_get_position;
  CHK(s3d_mesh_setup_indexed_vertices
    (shape, cbox_ntris, S3D_KEEP, cbox_nverts, attribs, 2, data) == RES_OK);

  CHK(s3d_mesh_get_vertices_count(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertices_count(shape, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertices_count(NULL, &nverts) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertices_count(shape, &nverts) == RES_OK);
  CHK(nverts == cbox_nverts);

  CHK(s3d_mesh_get_vertex_attrib(NULL, nverts, S3D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(shape, nverts, S3D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(NULL, 0, S3D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(shape, 0, S3D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(NULL, nverts, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(shape, nverts, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(NULL, 0, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(shape, 0, S3D_POSITION, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(NULL, nverts, S3D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(shape, nverts, S3D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(NULL, 0, S3D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(shape, 0, S3D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(NULL, nverts, S3D_POSITION, &attr) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(shape, nverts, S3D_POSITION, &attr) == RES_BAD_ARG);
  CHK(s3d_mesh_get_vertex_attrib(NULL, 0, S3D_POSITION, &attr) == RES_BAD_ARG);
  fprintf(stderr, "[MARK] before vertex attrib loop\n"); fflush(stderr);
  FOR_EACH(id, 0, nverts) {
    cbox_get_position(id, pos, data);

    CHK(s3d_mesh_get_vertex_attrib(shape, id, S3D_POSITION, &attr) == RES_OK);
    CHK(attr.type == S3D_FLOAT3);
    CHK(attr.usage == S3D_POSITION);
    CHK(f3_eq_eps(attr.value, pos, 1.e-6f) == 1);

    CHK(s3d_mesh_get_vertex_attrib(shape, id, S3D_ATTRIB_0, &attr) == RES_OK);
    CHK(attr.type == S3D_FLOAT3);
    CHK(attr.usage == S3D_ATTRIB_0);
    CHK(f3_eq_eps(attr.value, pos, 1.e-6f) == 1);
  }
  CHK(s3d_mesh_get_vertex_attrib(shape, id, S3D_ATTRIB_1, &attr) == RES_BAD_ARG);
  fprintf(stderr, "[MARK] after vertex attrib loop\n"); fflush(stderr);

  CHK(s3d_mesh_get_triangles_count(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangles_count(shape, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangles_count(NULL, &ntris) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangles_count(shape, &ntris) == RES_OK);
  CHK(ntris == cbox_ntris);

  CHK(s3d_mesh_get_triangle_indices(NULL, ntris, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangle_indices(shape, ntris, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangle_indices(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangle_indices(shape, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangle_indices(NULL, ntris, ids) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangle_indices(shape, ntris, ids) == RES_BAD_ARG);
  CHK(s3d_mesh_get_triangle_indices(NULL, 0, ids) == RES_BAD_ARG);

  FOR_EACH(id, 0, ntris) {
    unsigned indices[3];
    CHK(s3d_mesh_get_triangle_indices(shape, id, ids) == RES_OK);
    cbox_get_ids(id, indices, data);
    CHK(ids[0] == indices[0]);
    CHK(ids[1] == indices[1]);
    CHK(ids[2] == indices[2]);
  }

  CHK(s3d_shape_is_enabled(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_is_enabled(shape, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_is_enabled(NULL, &c) == RES_BAD_ARG);
  CHK(s3d_shape_is_enabled(shape, &c) == RES_OK);
  CHK(c != 0);

  CHK(s3d_shape_enable(NULL, 0) == RES_BAD_ARG);
  CHK(s3d_shape_enable(shape, 0) == RES_OK);
  CHK(s3d_shape_is_enabled(shape, &c) == RES_OK);
  CHK(c == 0);

  CHK(s3d_shape_flip_surface(NULL) == RES_BAD_ARG);
  CHK(s3d_shape_flip_surface(shape) == RES_OK);
  CHK(s3d_shape_flip_surface(shape) == RES_OK);

  CHK(s3d_mesh_set_hit_filter_function(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_set_hit_filter_function(shape, NULL, NULL) == RES_OK);
  CHK(s3d_mesh_set_hit_filter_function(NULL, filter_none, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_set_hit_filter_function(shape, filter_none, NULL) == RES_OK);

  CHK(s3d_mesh_get_hit_filter_data(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_hit_filter_data(shape, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_get_hit_filter_data(NULL, &data) == RES_BAD_ARG);
  CHK(s3d_mesh_get_hit_filter_data(shape, &data) == RES_OK);
  CHK(data == NULL);

  CHK(s3d_mesh_set_hit_filter_function(shape, NULL, NULL) == RES_OK);
  CHK(s3d_mesh_get_hit_filter_data(shape, &data) == RES_OK);
  CHK(data == NULL);
  CHK(s3d_mesh_set_hit_filter_function
    (shape, filter_none, (void*)((uintptr_t)0xDEADBEEF)) == RES_OK);
  CHK(s3d_mesh_get_hit_filter_data(shape, &data) == RES_OK);
  CHK((uintptr_t)data == 0xDEADBEEF);

  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  fprintf(stderr, "[MARK] before instantiate\n"); fflush(stderr);

  CHK(s3d_scene_instantiate(scn, &inst) == RES_OK);
  fprintf(stderr, "[MARK] after instantiate\n"); fflush(stderr);

  CHK(s3d_instance_set_position(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_set_position(inst, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_set_position(NULL, pos) == RES_BAD_ARG);
  CHK(s3d_instance_set_position(inst, pos) == RES_OK);
  CHK(s3d_instance_set_position(shape, pos) == RES_BAD_ARG);

  CHK(s3d_instance_translate(NULL, -1, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_translate(inst, -1, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_translate(NULL, S3D_LOCAL_TRANSFORM, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_translate(inst, S3D_LOCAL_TRANSFORM, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_translate(NULL, -1, pos) == RES_BAD_ARG);
  CHK(s3d_instance_translate(inst, -1, pos) == RES_BAD_ARG);
  CHK(s3d_instance_translate(NULL, S3D_LOCAL_TRANSFORM, pos) == RES_BAD_ARG);
  CHK(s3d_instance_translate(inst, S3D_LOCAL_TRANSFORM, pos) == RES_OK);
  CHK(s3d_instance_translate(inst, S3D_WORLD_TRANSFORM, pos) == RES_OK);
  CHK(s3d_instance_translate(shape, S3D_WORLD_TRANSFORM, pos) == RES_BAD_ARG);

  CHK(s3d_instance_transform(NULL, -1, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_transform(inst, -1, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_transform(NULL, S3D_LOCAL_TRANSFORM, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_transform(inst, S3D_LOCAL_TRANSFORM, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_transform(NULL, -1, trans) == RES_BAD_ARG);
  CHK(s3d_instance_transform(inst, -1, trans) == RES_BAD_ARG);
  CHK(s3d_instance_transform(NULL, S3D_LOCAL_TRANSFORM, trans) == RES_BAD_ARG);
  CHK(s3d_instance_transform(inst, S3D_LOCAL_TRANSFORM, trans) == RES_OK);
  CHK(s3d_instance_transform(inst, S3D_WORLD_TRANSFORM, trans) == RES_OK);
  CHK(s3d_instance_transform(shape, S3D_LOCAL_TRANSFORM, trans) == RES_BAD_ARG);

  CHK(s3d_instance_set_transform(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_set_transform(inst, NULL) == RES_BAD_ARG);
  CHK(s3d_instance_set_transform(NULL, trans) == RES_BAD_ARG);
  CHK(s3d_instance_set_transform(shape, trans) == RES_BAD_ARG);
  CHK(s3d_instance_set_transform(inst, trans) == RES_OK);

  CHK(s3d_shape_flip_surface(inst) == RES_OK);

  CHK(s3d_shape_create_mesh(dev, &shape_copy) == RES_OK);
  CHK(s3d_mesh_copy(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_copy(shape, NULL) == RES_BAD_ARG);
  CHK(s3d_mesh_copy(NULL, shape_copy) == RES_BAD_ARG);
  CHK(s3d_mesh_copy(shape, shape_copy) == RES_OK);

  CHK(s3d_shape_ref_get(NULL) == RES_BAD_ARG);
  CHK(s3d_shape_ref_get(shape) == RES_OK);
  CHK(s3d_shape_ref_put(NULL) == RES_BAD_ARG);
  fprintf(stderr, "[MARK] before ref_put chain\n"); fflush(stderr);
  CHK(s3d_shape_ref_put(shape) == RES_OK);
  fprintf(stderr, "[MARK] after ref_put(shape) #1\n"); fflush(stderr);
  CHK(s3d_shape_ref_put(shape) == RES_OK);
  fprintf(stderr, "[MARK] after ref_put(shape) #2\n"); fflush(stderr);
  CHK(s3d_shape_ref_put(inst) == RES_OK);
  fprintf(stderr, "[MARK] after ref_put(inst)\n"); fflush(stderr);
  CHK(s3d_shape_ref_put(shape_copy) == RES_OK);
  fprintf(stderr, "[MARK] after ref_put(shape_copy)\n"); fflush(stderr);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  fprintf(stderr, "[MARK] after scene_ref_put\n"); fflush(stderr);

  CHK(s3d_device_ref_put(dev) == RES_OK);;
  fprintf(stderr, "[MARK] after device_ref_put\n"); fflush(stderr);

  check_memory_allocator(&allocator);
  fprintf(stderr, "[MARK] after check_memory_allocator\n"); fflush(stderr);
  mem_shutdown_proxy_allocator(&allocator);
  fprintf(stderr, "[MARK] after mem_shutdown_proxy_allocator\n"); fflush(stderr);
  CHK(mem_allocated_size() == 0);
  fprintf(stderr, "[MARK] ALL DONE\n"); fflush(stderr);
  return 0;
}

