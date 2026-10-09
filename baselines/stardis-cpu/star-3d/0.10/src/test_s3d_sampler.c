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

#define NSAMPS 4096

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  struct s3d_scene* scn;
  struct s3d_scene* scn2;
  struct s3d_scene_view* scnview;
  struct s3d_shape* cbox;
  struct s3d_shape* walls;
  struct s3d_shape* short_block;
  struct s3d_shape* tall_block;
  struct s3d_vertex_data attribs;
  struct s3d_primitive prim;
  struct s3d_primitive prim1;
  struct s3d_attrib attr0, attr1;
  struct cbox_desc desc;
  float uv[2];
  size_t i;
  unsigned ntris, nverts;
  unsigned cbox_id;
  unsigned walls_id;
  unsigned short_block_id;
  unsigned tall_block_id;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);
  CHK(s3d_scene_instantiate(scn, &cbox) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, cbox) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &short_block) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &tall_block) == RES_OK);

  CHK(s3d_shape_get_id(cbox, &cbox_id) == RES_OK);
  CHK(s3d_shape_get_id(walls, &walls_id) == RES_OK);
  CHK(s3d_shape_get_id(short_block, &short_block_id) == RES_OK);
  CHK(s3d_shape_get_id(tall_block, &tall_block_id) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(NULL, 0, 0, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, 0, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(NULL, 0, 0, 0, &prim, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, 0, 0, &prim, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(NULL, 0, 0, 0, NULL, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, 0, 0, NULL, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(NULL, 0, 0, 0, &prim, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, 0, 0, &prim, uv) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, -1, 0, 0, &prim, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, -1, 0, &prim, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, 0, -1, &prim, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 1, 0, 0, &prim, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, 1, 0, &prim, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0, 0, 1, &prim, uv) == RES_BAD_ARG);
  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(S3D_PRIMITIVE_EQ(&prim, &S3D_PRIMITIVE_NULL) == 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  attribs.usage = S3D_POSITION;
  attribs.type = S3D_FLOAT3;
  attribs.get = cbox_get_position;

  ntris = cbox_walls_ntris;
  nverts = cbox_walls_nverts;
  desc.vertices = cbox_walls;
  desc.indices = cbox_walls_ids;
  CHK(s3d_mesh_setup_indexed_vertices
    (walls, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);

  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);

  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr0) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr1) == RES_OK);

  prim1 = prim;
  CHK(S3D_PRIMITIVE_EQ(&prim, &prim1) == 1);
  prim1.inst_id = prim.inst_id + 1;
  CHK(S3D_PRIMITIVE_EQ(&prim, &prim1) == 0);
  prim1.inst_id = prim.inst_id;
  CHK(S3D_PRIMITIVE_EQ(&prim, &prim1) == 1);
  prim1.prim_id = S3D_INVALID_ID;
  CHK(S3D_PRIMITIVE_EQ(&prim, &prim1) == 0);
  prim1.prim_id = prim.prim_id;
  prim1.geom_id = S3D_INVALID_ID;
  CHK(S3D_PRIMITIVE_EQ(&prim, &prim1) == 0);

  CHK(attr0.type == S3D_FLOAT3);
  CHK(attr1.type == S3D_FLOAT3);
  CHK(f3_eq_eps(attr0.value, attr1.value, 1.e-6f) == 1);

  CHK(s3d_scene_view_sample(scnview, 0.3f, 0.1f, 0.2f, &prim, uv) == RES_OK);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr1) == RES_OK);
  CHK(f3_eq_eps(attr0.value, attr1.value, 1.e-6f) != 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_shape_enable(walls, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(S3D_PRIMITIVE_EQ(&prim, &S3D_PRIMITIVE_NULL) == 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_shape_enable(walls, 1) == RES_OK);

  ntris = cbox_block_ntris;
  nverts = cbox_block_nverts;
  desc.vertices = cbox_short_block;
  desc.indices = cbox_block_ids;
  CHK(s3d_mesh_setup_indexed_vertices
    (short_block, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);

  CHK(s3d_scene_attach_shape(scn, short_block) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, tall_block) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr0) == RES_OK);
  desc.vertices = cbox_tall_block;
  CHK(s3d_mesh_setup_indexed_vertices
    (tall_block, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr1) == RES_OK);
  CHK(f3_eq_eps(attr0.value, attr1.value, 1.e-6f) == 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr1) == RES_OK);
  CHK(f3_eq_eps(attr0.value, attr1.value, 1.e-6f) != 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_shape_enable(cbox, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn2, S3D_SAMPLE|S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.5f, 0.5f, 0.5f, &prim, uv) == RES_OK);
  CHK(S3D_PRIMITIVE_EQ(&prim, &S3D_PRIMITIVE_NULL) == 1);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_shape_enable(cbox, 1) == RES_OK);
  CHK(s3d_scene_view_create(scn2, S3D_SAMPLE|S3D_TRACE, &scnview) == RES_OK);
  FOR_EACH(i, 0, NSAMPS) {
    const float u = rand_canonic();
    const float v = rand_canonic();
    const float w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview, u, v, w, &prim, uv) == RES_OK);
    CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, uv, &attr0) == RES_OK);

    CHK(prim.inst_id == cbox_id);
    CHK(prim.geom_id == walls_id
     || prim.geom_id == tall_block_id
     || prim.geom_id == short_block_id);
    CHK(prim.prim_id < 10);
    CHK(prim.scene_prim_id >= prim.prim_id);
    CHK(prim.scene_prim_id < 30);
    printf("%f %f %f\n", SPLIT3(attr0.value));
  }
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_shape_ref_put(cbox) == RES_OK);
  CHK(s3d_shape_ref_put(walls) == RES_OK);
  CHK(s3d_shape_ref_put(short_block) == RES_OK);
  CHK(s3d_shape_ref_put(tall_block) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

