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

#include <rsys/float3.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
compute_mesh_aabb
  (const float* pos,
   const size_t nverts,
   const unsigned* ids,
   const size_t ntris,
   float low[3],
   float upp[3])
{
  size_t i;

  CHK(low && upp && pos && nverts && ids && ntris);
  f3_splat(low, FLT_MAX);
  f3_splat(upp,-FLT_MAX);

  FOR_EACH(i, 0, ntris*3) {
    const float* vertex = pos + ids[i]*3;
    f3_min(low, vertex, low);
    f3_max(upp, vertex, upp);
  }
}

static int
aabb_is_degenerated(const float low[3], const float upp[3])
{
  CHK(low && upp);
  return low[0] > upp[0]
      && low[1] > upp[1]
      && low[2] > upp[2];
}

/*******************************************************************************
 * Instances
 ******************************************************************************/
static void
test_instances(struct s3d_device* dev)
{
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_scene* scn2 = NULL;
  struct s3d_scene_view* view = NULL;
  struct s3d_shape* sphere = NULL;
  struct s3d_shape* tall_block = NULL;
  struct s3d_shape* inst0 = NULL;
  struct s3d_shape* inst1 = NULL;
  struct cbox_desc desc;
  float low[3], upp[3];
  float scn_low[3], scn_upp[3];
  float scn2_low[3], scn2_upp[3];
  float inst0_low[3], inst0_upp[3];
  float inst1_low[3], inst1_upp[3];
  float tall_block_low[3], tall_block_upp[3];
  float sphere_low[3], sphere_upp[3];
  float inst0_trans[3];
  float inst1_trans[3];
  float pos[3];
  float radius;

  /* Create the tall block shape*/
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = cbox_get_position;
  desc.vertices = cbox_tall_block;
  desc.indices = cbox_block_ids;
  CHK(s3d_shape_create_mesh(dev, &tall_block) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(tall_block, cbox_block_ntris, cbox_get_ids,
    cbox_block_nverts, &vdata, 1, &desc) == RES_OK);
  compute_mesh_aabb(cbox_tall_block, cbox_block_nverts, cbox_block_ids,
    cbox_block_ntris, tall_block_low, tall_block_upp);

  /* Create the sphere and ensure that it is not contained into the block */
  pos[0] =  tall_block_low[0] - 10.f;
  pos[1] = (tall_block_upp[1] + tall_block_low[1]) * 0.5f;
  pos[2] = (tall_block_upp[2] + tall_block_low[2]) * 0.5f;
  radius = 1.f;
  CHK(s3d_shape_create_sphere(dev, &sphere) == RES_OK);
  CHK(s3d_sphere_setup(sphere, pos, radius) == RES_OK);
  f3_subf(sphere_low, pos, radius);
  f3_addf(sphere_upp, pos, radius);

  /* Create the scene to instantiate */
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, tall_block) == RES_OK);

  /* Compute the AABB of the original scene */
  f3_min(scn_low, tall_block_low, sphere_low);
  f3_max(scn_upp, tall_block_upp, sphere_upp);

  /* Create two instances */
  inst0_trans[0] = -100.f;
  inst0_trans[1] = 0.f;
  inst0_trans[2] = -50.f;
  inst1_trans[0] = 123.f;
  inst1_trans[1] = 4.56f;
  inst1_trans[2] = 0.789f;
  CHK(s3d_scene_instantiate(scn, &inst0) == RES_OK);
  CHK(s3d_scene_instantiate(scn, &inst1) == RES_OK);
  CHK(s3d_instance_translate(inst0, S3D_WORLD_TRANSFORM, inst0_trans) == RES_OK);
  CHK(s3d_instance_translate(inst1, S3D_WORLD_TRANSFORM, inst1_trans) == RES_OK);
  f3_add(inst0_low, scn_low, inst0_trans);
  f3_add(inst0_upp, scn_upp, inst0_trans);
  f3_add(inst1_low, scn_low, inst1_trans);
  f3_add(inst1_upp, scn_upp, inst1_trans);

  /* Create the scene with the 2 instances */
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, inst1) == RES_OK);

  /* Compute the AABB of the scene with instances */
  f3_min(scn2_low, inst0_low, inst1_low);
  f3_max(scn2_upp, inst0_upp, inst1_upp);

  /* Original scene */
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(scn_low, low, 1.e-6f));
  CHK(f3_eq_eps(scn_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Retry the original scene to test the cache mechanism */
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(scn_low, low, 1.e-6f));
  CHK(f3_eq_eps(scn_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Scene with the 2 instances */
  CHK(s3d_scene_view_create(scn2, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(scn2_low, low, 1.e-6f));
  CHK(f3_eq_eps(scn2_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Retry the scene with the 2 instances to check the cache mechanism */
  CHK(s3d_scene_view_create(scn2, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(scn2_low, low, 1.e-6f));
  CHK(f3_eq_eps(scn2_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Scene with only one instance */
  CHK(s3d_shape_enable(inst0, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn2, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(inst1_low, low, 1.e-6f));
  CHK(f3_eq_eps(inst1_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Scene whose instances have only the tall_block */
  CHK(s3d_shape_enable(inst0, 1) == RES_OK);
  CHK(s3d_shape_enable(sphere, 0) == RES_OK);
  f3_add(inst0_low, tall_block_low, inst0_trans);
  f3_add(inst0_upp, tall_block_upp, inst0_trans);
  f3_add(inst1_low, tall_block_low, inst1_trans);
  f3_add(inst1_upp, tall_block_upp, inst1_trans);
  f3_min(scn2_low, inst0_low, inst1_low);
  f3_max(scn2_upp, inst0_upp, inst1_upp);
  CHK(s3d_scene_view_create(scn2, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(scn2_low, low, 1.e-6f));
  CHK(f3_eq_eps(scn2_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Scene with one instance, one mesh and one sphere */
  CHK(s3d_scene_detach_shape(scn2, inst1) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, sphere) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, tall_block) == RES_OK);
  f3_min(scn2_low, f3_min(scn2_low, tall_block_low, sphere_low), inst0_low);
  f3_max(scn2_upp, f3_max(scn2_upp, tall_block_upp, sphere_upp), inst0_upp);
  CHK(s3d_scene_view_create(scn2, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(scn2_low, low, 1.e-6f));
  CHK(f3_eq_eps(scn2_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Clean up */
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_shape_ref_put(sphere) == RES_OK);
  CHK(s3d_shape_ref_put(tall_block) == RES_OK);
  CHK(s3d_shape_ref_put(inst0) == RES_OK);
  CHK(s3d_shape_ref_put(inst1) == RES_OK);
}

/*******************************************************************************
 * Cornell box
 ******************************************************************************/
static void
test_cbox(struct s3d_device* dev)
{
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_scene_view* view = NULL;
  struct s3d_shape* walls = NULL;
  struct s3d_shape* tall_block = NULL;
  struct s3d_shape* short_block = NULL;
  struct cbox_desc desc;
  float low[3], upp[3];
  float walls_low[3], walls_upp[3];
  float tall_block_low[3], tall_block_upp[3];
  float short_block_low[3], short_block_upp[3];
  float aabb_low[3], aabb_upp[3];

  /* Create the Star-3D scene and the Cornell box meshes */
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &tall_block) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &short_block) == RES_OK);

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = cbox_get_position;

  /* Setup the Cornell box walls */
  desc.vertices = cbox_walls;
  desc.indices = cbox_walls_ids;
  CHK(s3d_mesh_setup_indexed_vertices(walls, cbox_walls_ntris, cbox_get_ids,
    cbox_walls_nverts, &vdata, 1, &desc) == RES_OK);
  compute_mesh_aabb(cbox_walls, cbox_walls_nverts, cbox_walls_ids,
    cbox_walls_ntris, walls_low, walls_upp);

  /* Setup the Cornell box tall block */
  desc.vertices = cbox_tall_block;
  desc.indices = cbox_block_ids;
  CHK(s3d_mesh_setup_indexed_vertices(tall_block, cbox_block_ntris, cbox_get_ids,
    cbox_block_nverts, &vdata, 1, &desc) == RES_OK);
  compute_mesh_aabb(cbox_tall_block, cbox_block_nverts, cbox_block_ids,
    cbox_block_ntris, tall_block_low, tall_block_upp);

  /* Setup the Cornell box short block */
  desc.vertices = cbox_short_block;
  desc.indices = cbox_block_ids;
  CHK(s3d_mesh_setup_indexed_vertices(short_block, cbox_block_ntris, cbox_get_ids,
    cbox_block_nverts, &vdata, 1, &desc) == RES_OK);
  compute_mesh_aabb(cbox_short_block, cbox_block_nverts, cbox_block_ids,
    cbox_block_ntris, short_block_low, short_block_upp);

  /* Tall block only */
  CHK(s3d_scene_attach_shape(scn, tall_block) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(tall_block_low, low, 1.e-6f));
  CHK(f3_eq_eps(tall_block_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Disabled tall block only */
  CHK(s3d_shape_enable(tall_block, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(aabb_is_degenerated(low, upp));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Tall block only with wo S3D_TRACE flag */
  CHK(s3d_shape_enable(tall_block, 1) == RES_OK);
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(tall_block_low, low, 1.e-6f));
  CHK(f3_eq_eps(tall_block_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* All blocks */
  CHK(s3d_scene_attach_shape(scn, short_block) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  f3_min(aabb_low, tall_block_low, short_block_low);
  f3_max(aabb_upp, tall_block_upp, short_block_upp);
  CHK(f3_eq_eps(aabb_low, low, 1.e-6f));
  CHK(f3_eq_eps(aabb_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Another try with all blocks to check the cache mechanism */
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(aabb_low, low, 1.e-6f));
  CHK(f3_eq_eps(aabb_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* All blocks but the tall block is disabled */
  CHK(s3d_shape_enable(tall_block, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(short_block_low, low, 1.e-6f));
  CHK(f3_eq_eps(short_block_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* All blocks but the short block is disabled */
  CHK(s3d_shape_enable(tall_block, 1) == RES_OK);
  CHK(s3d_shape_enable(short_block, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(tall_block_low, low, 1.e-6f));
  CHK(f3_eq_eps(tall_block_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* The whole Cornell box */
  CHK(s3d_shape_enable(short_block, 1) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  f3_min(aabb_low, f3_min(aabb_low, tall_block_low, short_block_low), walls_low);
  f3_max(aabb_upp, f3_max(aabb_upp, tall_block_upp, short_block_upp), walls_upp);
  CHK(f3_eq_eps(aabb_low, low, 1.e-6f));
  CHK(f3_eq_eps(aabb_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Retry the whole Cornell box */
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(aabb_low, low, 1.e-6f));
  CHK(f3_eq_eps(aabb_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Disable all */
  CHK(s3d_shape_enable(walls, 0) == RES_OK);
  CHK(s3d_shape_enable(tall_block, 0) == RES_OK);
  CHK(s3d_shape_enable(short_block, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(aabb_is_degenerated(low, upp));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Only the short block */
  CHK(s3d_shape_enable(walls, 1) == RES_OK);
  CHK(s3d_shape_enable(tall_block, 1) == RES_OK);
  CHK(s3d_shape_enable(short_block, 1) == RES_OK);
  CHK(s3d_scene_detach_shape(scn, walls) == RES_OK);
  CHK(s3d_scene_detach_shape(scn, tall_block) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(f3_eq_eps(short_block_low, low, 1.e-6f));
  CHK(f3_eq_eps(short_block_upp, upp, 1.e-6f));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Only the short block */
  CHK(s3d_scene_clear(scn) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(aabb_is_degenerated(low, upp));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Clean up the data */
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_shape_ref_put(walls) == RES_OK);
  CHK(s3d_shape_ref_put(tall_block) == RES_OK);
  CHK(s3d_shape_ref_put(short_block) == RES_OK);
}

/*******************************************************************************
 * Test the API
 ******************************************************************************/
static void
test_api(struct s3d_device* dev)
{
  struct s3d_scene* scn = NULL;
  struct s3d_scene_view* view = NULL;
  float low[3] = {0,0,0};
  float upp[3] = {0,0,0};

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_view_create(scn, 0, &view) == RES_OK);

  CHK(s3d_scene_view_get_aabb(NULL, low, upp) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(view, NULL, upp) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(view, low, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_aabb(view, low, upp) == RES_OK);
  CHK(aabb_is_degenerated(low, upp));

  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev = NULL;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  test_api(dev);
  test_cbox(dev);
  test_instances(dev);

  CHK(s3d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
