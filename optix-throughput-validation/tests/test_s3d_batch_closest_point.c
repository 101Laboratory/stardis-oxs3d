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

/*
 * Batch closest point test — GPU batch vs CPU single equivalence.
 *
 * Tests:
 *  1. Batch vs single equivalence on Cornell box (no filter)
 *  2. Context reuse (pre-allocated GPU buffers, 3 iterations)
 *  3. Empty batch (should return RES_OK immediately)
 *  4. Bad args
 *  5. Miss consistency (all queries far from geometry)
 *  6. Instanced scene equivalence (batch vs single on instanced cbox)
 *  7. Stats accuracy
 */

#define _POSIX_C_SOURCE 200112L

#include "s3d.h"
#include "test_s3d_cbox.h"
#include "test_s3d_utils.h"

#include <rsys/float3.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define NQUERIES 256

/* Distance tolerance: GPU uses float BVH + double triangle math;
 * CPU brute-force uses double throughout. Allow small deviation. */
#define DIST_EPS  1e-3f
#define UV_EPS    1e-3f
#define NORM_EPS  1e-4f

/**
 * Compare two s3d_hit results for closest point equivalence.
 *
 * Tolerances are somewhat relaxed because:
 *  - GPU uses shrinkingRadiusQuery with float BVH bounds
 *  - GPU triangle math is double but traversal order may differ
 *  - Ties (equidistant primitives) may resolve differently
 *
 * For edge/vertex equidistant cases we only check distance equality.
 */
static int
cp_hits_equal(const struct s3d_hit* a, const struct s3d_hit* b)
{
  if(S3D_HIT_NONE(a) && S3D_HIT_NONE(b)) return 1;
  if(S3D_HIT_NONE(a) || S3D_HIT_NONE(b)) return 0;

  /* Distance must match within tolerance */
  if(fabsf(a->distance - b->distance) > DIST_EPS) return 0;

  /* If same primitive, check UV and normal too */
  if(a->prim.prim_id == b->prim.prim_id
  && a->prim.geom_id == b->prim.geom_id) {
    if(fabsf(a->uv[0] - b->uv[0]) > UV_EPS) return 0;
    if(fabsf(a->uv[1] - b->uv[1]) > UV_EPS) return 0;
    if(fabsf(a->normal[0] - b->normal[0]) > NORM_EPS) return 0;
    if(fabsf(a->normal[1] - b->normal[1]) > NORM_EPS) return 0;
    if(fabsf(a->normal[2] - b->normal[2]) > NORM_EPS) return 0;
  }
  /* Different primitive but same distance — equidistant tie, OK */

  return 1;
}

/*******************************************************************************
 * Build a Cornell box scene (walls + blocks) — reuses test_s3d_cbox.h
 ******************************************************************************/
static void
setup_cbox_scene
  (struct s3d_device* dev,
   struct s3d_scene** out_scn,
   struct s3d_scene_view** out_view,
   struct s3d_shape** out_walls,
   struct s3d_shape** out_tall,
   struct s3d_shape** out_short)
{
  struct s3d_vertex_data attribs;
  struct cbox_desc desc;
  unsigned ntris, nverts;

  CHK(s3d_scene_create(dev, out_scn) == RES_OK);

  attribs.usage = S3D_POSITION;
  attribs.type = S3D_FLOAT3;
  attribs.get = cbox_get_position;

  /* walls */
  ntris = cbox_walls_ntris;
  nverts = cbox_walls_nverts;
  desc.vertices = cbox_walls;
  desc.indices = cbox_walls_ids;
  CHK(s3d_shape_create_mesh(dev, out_walls) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(
    *out_walls, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(*out_scn, *out_walls) == RES_OK);

  /* tall block */
  ntris = cbox_block_ntris;
  nverts = cbox_block_nverts;
  desc.vertices = cbox_tall_block;
  desc.indices = cbox_block_ids;
  CHK(s3d_shape_create_mesh(dev, out_tall) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(
    *out_tall, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(*out_scn, *out_tall) == RES_OK);

  /* short block */
  desc.vertices = cbox_short_block;
  CHK(s3d_shape_create_mesh(dev, out_short) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(
    *out_short, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(*out_scn, *out_short) == RES_OK);

  CHK(s3d_scene_view_create(*out_scn, S3D_TRACE, out_view) == RES_OK);
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
  float low[3], upp[3], mid[3], sz[3];
  size_t i;

  struct s3d_cp_request requests[NQUERIES];
  struct s3d_hit batch_hits[NQUERIES];
  struct s3d_hit single_hits[NQUERIES];
  struct s3d_batch_cp_stats stats;
  struct s3d_batch_cp_context* ctx;
  int mismatches;
  res_T res;

  (void)argc; (void)argv;
  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);

  setup_cbox_scene(dev, &scn, &scnview, &walls, &tall_block, &short_block);

  /* Compute scene AABB for random position generation */
  CHK(s3d_scene_view_get_aabb(scnview, low, upp) == RES_OK);
  f3_mulf(mid, f3_add(mid, low, upp), 0.5f);
  f3_sub(sz, upp, low);

  /* =====================================================================
   * Test 1: batch vs single — no filter — NQUERIES random positions
   * ===================================================================== */
  printf("Test 1: batch vs single (no filter), %d queries\n", NQUERIES);
  for(i = 0; i < NQUERIES; i++) {
    /* Random point in 2× scene AABB */
    requests[i].pos[0] = mid[0] + (rand_canonic() * 2.f - 1.f) * sz[0];
    requests[i].pos[1] = mid[1] + (rand_canonic() * 2.f - 1.f) * sz[1];
    requests[i].pos[2] = mid[2] + (rand_canonic() * 2.f - 1.f) * sz[2];
    requests[i].radius = (float)FLT_MAX;
    requests[i].query_data = NULL;
    requests[i].user_id = (uint32_t)i;
  }

  memset(&stats, 0, sizeof(stats));
  CHK(s3d_scene_view_closest_point_batch(
    scnview, requests, NQUERIES, batch_hits, &stats) == RES_OK);

  /* Single-query reference */
  for(i = 0; i < NQUERIES; i++) {
    CHK(s3d_scene_view_closest_point(
      scnview,
      requests[i].pos,
      requests[i].radius,
      NULL,
      &single_hits[i]) == RES_OK);
  }

  mismatches = 0;
  for(i = 0; i < NQUERIES; i++) {
    if(!cp_hits_equal(&batch_hits[i], &single_hits[i])) {
      mismatches++;
      fprintf(stderr,
        "  MISMATCH query %zu: "
        "batch(prim=%u geom=%u dist=%f uv=[%f,%f]) vs "
        "single(prim=%u geom=%u dist=%f uv=[%f,%f])\n",
        i,
        batch_hits[i].prim.prim_id, batch_hits[i].prim.geom_id,
        (double)batch_hits[i].distance,
        (double)batch_hits[i].uv[0], (double)batch_hits[i].uv[1],
        single_hits[i].prim.prim_id, single_hits[i].prim.geom_id,
        (double)single_hits[i].distance,
        (double)single_hits[i].uv[0], (double)single_hits[i].uv[1]);
    }
  }
  printf("  Results: %zu queries, %zu batch_accepted, %zu filter_rejected, "
         "%d mismatches\n",
    stats.total_queries, stats.batch_accepted, stats.filter_rejected,
    mismatches);
  printf("  Timing: batch=%.3fms, postprocess=%.3fms, requery=%.3fms\n",
    stats.batch_time_ms, stats.postprocess_time_ms, stats.requery_time_ms);
  CHK(mismatches == 0);
  CHK(stats.total_queries == NQUERIES);
  CHK(stats.filter_rejected == 0);

  /* =====================================================================
   * Test 2: context reuse — same queries, same results
   * ===================================================================== */
  printf("Test 2: context reuse (%d queries x 3 iterations)\n", NQUERIES);
  CHK(s3d_batch_cp_context_create(&ctx, NQUERIES) == RES_OK);
  {
    int iter;
    for(iter = 0; iter < 3; iter++) {
      struct s3d_hit ctx_hits[NQUERIES];
      memset(&stats, 0, sizeof(stats));
      CHK(s3d_scene_view_closest_point_batch_ctx(
        scnview, ctx, requests, NQUERIES, ctx_hits, &stats) == RES_OK);

      mismatches = 0;
      for(i = 0; i < NQUERIES; i++) {
        if(!cp_hits_equal(&ctx_hits[i], &single_hits[i]))
          mismatches++;
      }
      printf("  iter %d: %d mismatches\n", iter, mismatches);
      CHK(mismatches == 0);
    }
  }
  s3d_batch_cp_context_destroy(ctx);
  ctx = NULL;

  /* =====================================================================
   * Test 3: empty batch — should return RES_OK immediately
   * ===================================================================== */
  printf("Test 3: empty batch\n");
  res = s3d_scene_view_closest_point_batch(
    scnview, requests, 0, batch_hits, NULL);
  CHK(res == RES_OK);

  /* =====================================================================
   * Test 4: bad args
   * ===================================================================== */
  printf("Test 4: bad args\n");
  CHK(s3d_scene_view_closest_point_batch(
    NULL, requests, NQUERIES, batch_hits, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_closest_point_batch(
    scnview, NULL, NQUERIES, batch_hits, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_closest_point_batch(
    scnview, requests, NQUERIES, NULL, NULL) == RES_BAD_ARG);

  /* =====================================================================
   * Test 5: miss consistency — all queries far from geometry
   * ===================================================================== */
  printf("Test 5: miss consistency (%d queries)\n", NQUERIES);
  for(i = 0; i < NQUERIES; i++) {
    /* Point very far from scene */
    requests[i].pos[0] = 1e6f + rand_canonic() * 100.f;
    requests[i].pos[1] = 1e6f + rand_canonic() * 100.f;
    requests[i].pos[2] = 1e6f + rand_canonic() * 100.f;
    requests[i].radius = 1.f; /* Very small radius, no hits expected */
    requests[i].query_data = NULL;
    requests[i].user_id = (uint32_t)i;
  }
  memset(&stats, 0, sizeof(stats));
  CHK(s3d_scene_view_closest_point_batch(
    scnview, requests, NQUERIES, batch_hits, &stats) == RES_OK);
  for(i = 0; i < NQUERIES; i++) {
    CHK(S3D_HIT_NONE(&batch_hits[i]));
  }
  CHK(stats.batch_accepted == NQUERIES);
  CHK(stats.filter_rejected == 0);
  printf("  All %d queries correctly returned miss\n", NQUERIES);

  /* Verify single-query also returns miss */
  for(i = 0; i < NQUERIES; i++) {
    CHK(s3d_scene_view_closest_point(
      scnview, requests[i].pos, requests[i].radius,
      NULL, &single_hits[i]) == RES_OK);
    CHK(S3D_HIT_NONE(&single_hits[i]));
  }
  printf("  Single-query miss consistency confirmed\n");

  /* =====================================================================
   * Test 6: instanced scene — batch vs single equivalence
   * ===================================================================== */
  printf("Test 6: instanced scene (%d queries)\n", NQUERIES);
  {
    struct s3d_scene* inst_scn;
    struct s3d_scene_view* inst_view;
    struct s3d_shape* inst0;
    struct s3d_shape* inst1;
    float inst_low[3], inst_upp[3], inst_mid[3], inst_sz[3];
    float trans0[3], trans1[3];

    /* Create instances of the original cbox scene */
    CHK(s3d_scene_instantiate(scn, &inst0) == RES_OK);
    CHK(s3d_scene_instantiate(scn, &inst1) == RES_OK);

    f3_mulf(trans0, sz, 0.5f);
    CHK(s3d_instance_translate(
      inst0, S3D_WORLD_TRANSFORM, trans0) == RES_OK);
    f3_mulf(trans1, sz, -0.5f);
    CHK(s3d_instance_translate(
      inst1, S3D_WORLD_TRANSFORM, trans1) == RES_OK);

    CHK(s3d_scene_create(dev, &inst_scn) == RES_OK);
    CHK(s3d_scene_attach_shape(inst_scn, inst0) == RES_OK);
    CHK(s3d_scene_attach_shape(inst_scn, inst1) == RES_OK);
    CHK(s3d_scene_view_create(inst_scn, S3D_TRACE, &inst_view) == RES_OK);

    /* Compute instanced scene AABB */
    CHK(s3d_scene_view_get_aabb(inst_view, inst_low, inst_upp) == RES_OK);
    f3_mulf(inst_mid, f3_add(inst_mid, inst_low, inst_upp), 0.5f);
    f3_sub(inst_sz, inst_upp, inst_low);

    /* Generate random queries in the instanced scene */
    for(i = 0; i < NQUERIES; i++) {
      requests[i].pos[0] = inst_mid[0]
        + (rand_canonic() * 2.f - 1.f) * inst_sz[0];
      requests[i].pos[1] = inst_mid[1]
        + (rand_canonic() * 2.f - 1.f) * inst_sz[1];
      requests[i].pos[2] = inst_mid[2]
        + (rand_canonic() * 2.f - 1.f) * inst_sz[2];
      requests[i].radius = (float)FLT_MAX;
      requests[i].query_data = NULL;
      requests[i].user_id = (uint32_t)i;
    }

    /* Batch closest point */
    memset(&stats, 0, sizeof(stats));
    CHK(s3d_scene_view_closest_point_batch(
      inst_view, requests, NQUERIES, batch_hits, &stats) == RES_OK);

    /* Single reference */
    for(i = 0; i < NQUERIES; i++) {
      CHK(s3d_scene_view_closest_point(
        inst_view,
        requests[i].pos,
        requests[i].radius,
        NULL,
        &single_hits[i]) == RES_OK);
    }

    mismatches = 0;
    for(i = 0; i < NQUERIES; i++) {
      if(!cp_hits_equal(&batch_hits[i], &single_hits[i])) {
        mismatches++;
        fprintf(stderr,
          "  INST MISMATCH query %zu: "
          "batch(prim=%u geom=%u inst=%u dist=%f) vs "
          "single(prim=%u geom=%u inst=%u dist=%f)\n",
          i,
          batch_hits[i].prim.prim_id, batch_hits[i].prim.geom_id,
          batch_hits[i].prim.inst_id,
          (double)batch_hits[i].distance,
          single_hits[i].prim.prim_id, single_hits[i].prim.geom_id,
          single_hits[i].prim.inst_id,
          (double)single_hits[i].distance);
      }
    }
    printf("  Instanced: %zu queries, %d mismatches\n",
      stats.total_queries, mismatches);
    CHK(mismatches == 0);

    /* Cleanup instanced scene */
    CHK(s3d_scene_view_ref_put(inst_view) == RES_OK);
    CHK(s3d_shape_ref_put(inst0) == RES_OK);
    CHK(s3d_shape_ref_put(inst1) == RES_OK);
    CHK(s3d_scene_ref_put(inst_scn) == RES_OK);
  }

  /* =====================================================================
   * Test 7: stats accuracy
   * ===================================================================== */
  printf("Test 7: stats accuracy\n");
  /* Re-run a simple batch for stats verification */
  for(i = 0; i < NQUERIES; i++) {
    requests[i].pos[0] = mid[0] + (rand_canonic() * 2.f - 1.f) * sz[0];
    requests[i].pos[1] = mid[1] + (rand_canonic() * 2.f - 1.f) * sz[1];
    requests[i].pos[2] = mid[2] + (rand_canonic() * 2.f - 1.f) * sz[2];
    requests[i].radius = (float)FLT_MAX;
    requests[i].query_data = NULL;
    requests[i].user_id = (uint32_t)i;
  }
  memset(&stats, 0, sizeof(stats));
  CHK(s3d_scene_view_closest_point_batch(
    scnview, requests, NQUERIES, batch_hits, &stats) == RES_OK);

  CHK(stats.total_queries == NQUERIES);
  CHK(stats.batch_accepted + stats.filter_rejected == NQUERIES);
  CHK(stats.requery_accepted + stats.requery_missed == stats.filter_rejected);
  printf("  Stats consistent: total=%zu, accepted=%zu, rejected=%zu\n",
    stats.total_queries, stats.batch_accepted, stats.filter_rejected);

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

  printf("All batch closest point tests passed.\n");
  return 0;
}
