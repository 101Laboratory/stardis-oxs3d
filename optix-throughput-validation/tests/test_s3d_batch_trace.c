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

#define _POSIX_C_SOURCE 200112L

#include "s3d.h"
#include "test_s3d_cbox.h"
#include "test_s3d_camera.h"
#include "test_s3d_utils.h"

#include <rsys/float3.h>
#include <rsys/float2.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define NRAYS 256

static int
hits_equal(const struct s3d_hit* a, const struct s3d_hit* b)
{
  if(S3D_HIT_NONE(a) && S3D_HIT_NONE(b)) return 1;
  if(S3D_HIT_NONE(a) || S3D_HIT_NONE(b)) return 0;

  return a->prim.prim_id       == b->prim.prim_id
      && a->prim.geom_id       == b->prim.geom_id
      && a->prim.scene_prim_id == b->prim.scene_prim_id
      && fabsf(a->distance - b->distance) < 1e-6f
      && fabsf(a->uv[0] - b->uv[0]) < 1e-6f
      && fabsf(a->uv[1] - b->uv[1]) < 1e-6f
      && fabsf(a->normal[0] - b->normal[0]) < 1e-5f
      && fabsf(a->normal[1] - b->normal[1]) < 1e-5f
      && fabsf(a->normal[2] - b->normal[2]) < 1e-5f;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  struct s3d_scene* scn;
  struct s3d_scene_view* scnview;
  struct s3d_shape* walls;
  struct s3d_shape* tall_block;
  struct s3d_shape* short_block;
  struct s3d_vertex_data attribs;
  struct cbox_desc desc;
  struct camera cam;
  unsigned ntris, nverts;
  size_t i;
  float pos[3], tgt[3], up[3];

  struct s3d_ray_request requests[NRAYS];
  struct s3d_hit batch_hits[NRAYS];
  struct s3d_hit single_hits[NRAYS];
  struct s3d_batch_trace_stats stats;
  struct s3d_batch_trace_context* ctx = NULL;
  int mismatches;
  res_T res;

  (void)argc; (void)argv;
  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  attribs.usage = S3D_POSITION;
  attribs.type = S3D_FLOAT3;
  attribs.get = cbox_get_position;

  /* walls */
  ntris = cbox_walls_ntris;
  nverts = cbox_walls_nverts;
  desc.vertices = cbox_walls;
  desc.indices = cbox_walls_ids;
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (walls, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);

  /* tall block */
  ntris = cbox_block_ntris;
  nverts = cbox_block_nverts;
  desc.vertices = cbox_tall_block;
  desc.indices = cbox_block_ids;
  CHK(s3d_shape_create_mesh(dev, &tall_block) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (tall_block, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, tall_block) == RES_OK);

  /* short block */
  desc.vertices = cbox_short_block;
  CHK(s3d_shape_create_mesh(dev, &short_block) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (short_block, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, short_block) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);

  f3(pos, 278.f, -800.f, 273.f);
  f3(tgt, 278.f, 0.f, 273.f);
  f3(up, 0.f, 0.f, 1.f);
  camera_init(&cam, pos, tgt, up, (float)PI*0.25f, 1.f);

  /* =====================================================================
   * Test 1: batch vs single — no filter — NRAYS camera rays
   * ===================================================================== */
  printf("Test 1: batch vs single (no filter), %d rays\n", NRAYS);
  for(i = 0; i < NRAYS; i++) {
    float pixel[2], org[3], dir[3];
    pixel[0] = rand_canonic();
    pixel[1] = rand_canonic();
    camera_ray(&cam, pixel, org, dir);

    f3_set(requests[i].origin, org);
    f3_set(requests[i].direction, dir);
    requests[i].range[0] = 0.f;
    requests[i].range[1] = FLT_MAX;
    requests[i].filter_data = NULL;
    requests[i].user_id = (uint32_t)i;
  }

  memset(&stats, 0, sizeof(stats));
  CHK(s3d_scene_view_trace_rays_batch(
    scnview, requests, NRAYS, batch_hits, &stats) == RES_OK);

  for(i = 0; i < NRAYS; i++) {
    CHK(s3d_scene_view_trace_ray(
      scnview,
      requests[i].origin,
      requests[i].direction,
      requests[i].range,
      NULL,
      &single_hits[i]) == RES_OK);
  }

  mismatches = 0;
  for(i = 0; i < NRAYS; i++) {
    if(!hits_equal(&batch_hits[i], &single_hits[i])) {
      mismatches++;
      fprintf(stderr,
        "  MISMATCH ray %zu: batch(prim=%u, dist=%f) vs single(prim=%u, dist=%f)\n",
        i,
        batch_hits[i].prim.prim_id, (double)batch_hits[i].distance,
        single_hits[i].prim.prim_id, (double)single_hits[i].distance);
    }
  }
  printf("  Results: %zu rays, %zu batch_accepted, %zu filter_rejected, "
         "%d mismatches\n",
    stats.total_rays, stats.batch_accepted, stats.filter_rejected,
    mismatches);
  printf("  Timing: batch=%.3fms, postprocess=%.3fms, retrace=%.3fms\n",
    stats.batch_time_ms, stats.postprocess_time_ms, stats.retrace_time_ms);
  CHK(mismatches == 0);
  CHK(stats.total_rays == NRAYS);
  CHK(stats.filter_rejected == 0);

  /* =====================================================================
   * Test 2: context reuse — same rays, same results
   * ===================================================================== */
  printf("Test 2: context reuse (%d rays x 3 iterations)\n", NRAYS);
  CHK(s3d_batch_trace_context_create(&ctx, NRAYS) == RES_OK);
  {
    int iter;
    for(iter = 0; iter < 3; iter++) {
      struct s3d_hit ctx_hits[NRAYS];
      memset(&stats, 0, sizeof(stats));
      CHK(s3d_scene_view_trace_rays_batch_ctx(
        scnview, ctx, requests, NRAYS, ctx_hits, &stats) == RES_OK);

      mismatches = 0;
      for(i = 0; i < NRAYS; i++) {
        if(!hits_equal(&ctx_hits[i], &single_hits[i]))
          mismatches++;
      }
      printf("  iter %d: %d mismatches\n", iter, mismatches);
      CHK(mismatches == 0);
    }
  }
  s3d_batch_trace_context_destroy(ctx);
  ctx = NULL;

  /* =====================================================================
   * Test 3: empty batch — should return RES_OK immediately
   * ===================================================================== */
  printf("Test 3: empty batch\n");
  res = s3d_scene_view_trace_rays_batch(scnview, requests, 0, batch_hits, NULL);
  CHK(res == RES_OK);

  /* =====================================================================
   * Test 4: bad args
   * ===================================================================== */
  printf("Test 4: bad args\n");
  CHK(s3d_scene_view_trace_rays_batch(NULL, requests, NRAYS, batch_hits, NULL)
    == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_rays_batch(scnview, NULL, NRAYS, batch_hits, NULL)
    == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_rays_batch(scnview, requests, NRAYS, NULL, NULL)
    == RES_BAD_ARG);

  /* =====================================================================
   * Test 5: miss consistency — all rays aimed away from scene
   * ===================================================================== */
  printf("Test 5: miss consistency (%d rays)\n", NRAYS);
  for(i = 0; i < NRAYS; i++) {
    f3(requests[i].origin, 0.f, -2000.f, 0.f);
    f3(requests[i].direction, 0.f, -1.f, 0.f);
    requests[i].range[0] = 0.f;
    requests[i].range[1] = FLT_MAX;
    requests[i].filter_data = NULL;
    requests[i].user_id = (uint32_t)i;
  }
  CHK(s3d_scene_view_trace_rays_batch(
    scnview, requests, NRAYS, batch_hits, &stats) == RES_OK);
  for(i = 0; i < NRAYS; i++) {
    CHK(S3D_HIT_NONE(&batch_hits[i]));
  }
  CHK(stats.batch_accepted == NRAYS);
  CHK(stats.filter_rejected == 0);
  printf("  All %d rays correctly returned miss\n", NRAYS);

  /* =====================================================================
   * Test 6: stats accuracy
   * ===================================================================== */
  printf("Test 6: stats accuracy\n");
  CHK(stats.total_rays == NRAYS);
  CHK(stats.batch_accepted + stats.filter_rejected == NRAYS);
  CHK(stats.retrace_accepted + stats.retrace_missed == stats.filter_rejected);
  printf("  Stats consistent: total=%zu, accepted=%zu, rejected=%zu\n",
    stats.total_rays, stats.batch_accepted, stats.filter_rejected);

  /* Cleanup */
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_shape_ref_put(walls) == RES_OK);
  CHK(s3d_shape_ref_put(tall_block) == RES_OK);
  CHK(s3d_shape_ref_put(short_block) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  printf("All batch trace tests passed.\n");
  return 0;
}
