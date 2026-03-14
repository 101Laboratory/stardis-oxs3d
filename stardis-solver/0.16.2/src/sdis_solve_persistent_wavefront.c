/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

/* Phase B-3 M3: Persistent Wavefront Pool with refill, stream
 * compaction, path-type bucketed dispatch, adaptive pool sizing,
 * and enhanced diagnostics.
 *
 * Builds on M2+M2.5:
 *   M2:   Fixed-size pool with refill (wavefront width constant).
 *   M2.5: Stream compaction + bucket index arrays.
 *   M3:   Bucketed step dispatch (per-type loops for radiative /
 *         conductive), adaptive pool_size based on GPU SM count,
 *         per-phase wall-clock timing, average wavefront width tracking.
 *
 * Reference: guide/upper-parallelization/phase_b3_persistent_wavefront.md
 */

#include "sdis_solve_persistent_wavefront.h"
#include "sdis_wf_rng.h"
#include "sdis_wf_rng_adapter.h"
#include "sdis.h"
#include "sdis_brdf.h"
#include "sdis_c.h"
#include "sdis_camera.h"
#include "sdis_device_c.h"
#include "sdis_estimator_buffer_c.h"
#include "sdis_estimator_c.h"
#include "sdis_heat_path_boundary_c.h"
#include "sdis_heat_path_conductive_c.h"
#include "sdis_heat_path.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_radiative_env_c.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"
#include "sdis_tile.h"

#include <rsys/clock_time.h>
#include <rsys/cstr.h>
#include <rsys/morton.h>

#include <omp.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>  /* pixel trace */

/* O7: Software prefetch — portable macro for MSVC / GCC / Clang */
#ifdef _MSC_VER
#include <intrin.h>
#define PREFETCH_T0(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#else
#define PREFETCH_T0(addr) __builtin_prefetch((const void*)(addr), 0, 3)
#endif

/* Per-path temperature trace (set STARDIS_PIXEL_TRACE=gpu_trace.csv) */
static FILE* s_pixel_trace_fp = NULL;
static int   s_pixel_trace_checked = 0;
static FILE* pixel_trace_file(void) {
  if(!s_pixel_trace_checked) {
    const char* path = getenv("STARDIS_PIXEL_TRACE");
    if(path && path[0]) s_pixel_trace_fp = fopen(path, "w");
    if(s_pixel_trace_fp)
      fprintf(s_pixel_trace_fp,
        "path_id,px,py,spp,T_value,T_done,done_reason,steps,phase\n");
    s_pixel_trace_checked = 1;
  }
  return s_pixel_trace_fp;
}

/*******************************************************************************
 * O11_DIAG: Per-step path state trace (Debug builds only).
 *
 * Enable: set STARDIS_PATH_TRACE=<max_lines> (e.g. "5000").
 * Output: stderr, one line per step.
 * Format: [PT] <tag> round=R s=SLOT pid=PID ph=BEFORE->AFTER act=A nr=N
 *         bidx=B rc=RC steps=S dist=D norm=(x,y,z) rbkt=K done=DR res=LR
 *
 * In Release builds (NDEBUG defined), pt_enabled() returns 0 and pt_log()
 * is a no-op, so the compiler can eliminate all tracing code.
 ******************************************************************************/
#ifndef NDEBUG
static long long s_pt_max  = -1;  /* max lines; -1 = not checked yet */
static long long s_pt_seq  =  0;  /* monotonic sequence counter      */
static long long s_pt_pid  = -1;  /* -1 = log all; >=0 = filter by path_id */

static int pt_enabled(void) {
  if(s_pt_max < 0) {
    const char* e = getenv("STARDIS_PATH_TRACE");
    const char* f = getenv("STARDIS_PATH_TRACE_PID");
    s_pt_max = (e && e[0]) ? atoll(e) : 0;
    s_pt_pid = (f && f[0]) ? atoll(f) : 486729;
    if(s_pt_max > 0)
      fprintf(stderr,
        "[PT] seq,tag,round,slot,pid,ph_before,ph_after,"
        "active,needs_ray,batch_idx,ray_count,steps,"
        "hit_dist,norm_x,norm_y,norm_z,ray_bkt,done_reason,step_res,"
        "enc_resolved,flt_prim,flt_enc,sf_retry\n");
  }
  return s_pt_max > 0 && s_pt_seq < s_pt_max;
}

/* tag: A=distribute, B=cascade, F=first_ray.  lr: step return value. */
static void
pt_log(const char* tag,
       unsigned long long round,
       uint32_t slot,
       const struct path_state* p,
       const struct path_hot*   hot,
       const struct wavefront_pool* pool,
       int ph_before,
       int step_res)
{
  if(!pt_enabled()) return;
  if(s_pt_pid >= 0 && (long long)p->path_id != s_pt_pid) return;
  fprintf(stderr,
    "[PT] %llu,%s,%llu,%u,%u,%d,%d,"
    "%d,%d,%u,%d,%llu,"
    "%.8g,%.8g,%.8g,%.8g,%d,%d,%d,"
    "%u,%u,%u,%d\n",
    (unsigned long long)s_pt_seq++,
    tag,
    round,
    (unsigned)slot,
    (unsigned)p->path_id,
    ph_before,
    (int)hot->phase,
    (int)hot->active,
    (int)hot->needs_ray,
    (unsigned)p->ray_req.batch_idx,
    (int)p->ray_req.ray_count,
    (unsigned long long)p->steps_taken,
    p->rwalk.hit_3d.distance,
    p->rwalk.hit_3d.normal[0],
    p->rwalk.hit_3d.normal[1],
    p->rwalk.hit_3d.normal[2],
    (int)hot->ray_bucket,
    p->done_reason,
    step_res,
    (unsigned)pool->enc_arr[slot].resolved_enc_id,
    (unsigned)p->filter_data_storage.hit_3d.prim.prim_id,
    (unsigned)p->filter_data_storage.enc_id,
    p->locals.bnd_sf.retry_count);
}
#else
static INLINE int pt_enabled(void) { return 0; }
static INLINE void
pt_log(const char* tag, unsigned long long round, uint32_t slot,
       const struct path_state* p, const struct path_hot* hot,
       const struct wavefront_pool* pool,
       int ph_before, int step_res)
{ (void)tag; (void)round; (void)slot; (void)p; (void)hot;
  (void)pool; (void)ph_before; (void)step_res; }
#endif

/*******************************************************************************
 * Step functions are now in sdis_wf_steps.c with LOCAL_SYM linkage.
 * Include the header to access them.
 ******************************************************************************/
#include "sdis_wf_steps.h"

/*******************************************************************************
 * P2: pool_view — Unified pool view init / destroy / merge / split
 *
 * A pool_view represents a view over slots[base..base+view_size).
 * Single-buffer: views[0] covers the entire pool.
 * Dual-buffer:   views[0] front half, views[1] back half.
 *
 * All pool operation functions will be unified to accept (pool, pv) in P3.
 * Until then, the old pool-level fields remain and are used by existing code.
 ******************************************************************************/

/**
 * pool_view_init — Allocate and initialise a single pool view.
 *
 * @param pv         Target view struct (caller-owned, e.g. &pool->views[i])
 * @param base       Slot start offset (0 or view_size)
 * @param view_size  Number of slots this view covers
 * @param capacity   Allocation capacity (>= view_size; views[0] may use
 *                   pool_size to support merge)
 */
static res_T
pool_view_init(struct pool_view* pv, size_t base, size_t view_size,
               size_t capacity)
{
  size_t max_rays = capacity * 6;  /* each slot can request up to 6 rays */

  memset(pv, 0, sizeof(*pv));
  pv->base      = base;
  pv->view_size = view_size;
  pv->capacity  = capacity;
  pv->max_rays  = max_rays;

  /* Index arrays — allocated by capacity */
  pv->active_indices   = (uint32_t*)calloc(capacity, sizeof(uint32_t));
  pv->need_ray_indices = (uint32_t*)calloc(capacity, sizeof(uint32_t));
  pv->done_indices     = (uint32_t*)calloc(capacity, sizeof(uint32_t));
  pv->bucket_radiative = (uint32_t*)calloc(capacity, sizeof(uint32_t));
  pv->bucket_conductive= (uint32_t*)calloc(capacity, sizeof(uint32_t));
  pv->bucket_other     = (uint32_t*)calloc(capacity, sizeof(uint32_t));

  if(!pv->active_indices || !pv->need_ray_indices || !pv->done_indices
  || !pv->bucket_radiative || !pv->bucket_conductive || !pv->bucket_other)
    return RES_MEM_ERR;

  /* Ray buffers — allocated by max_rays */
  pv->ray_requests = (struct s3d_ray_request*)calloc(
    max_rays, sizeof(struct s3d_ray_request));
  pv->ray_to_slot  = (uint32_t*)calloc(max_rays, sizeof(uint32_t));
  pv->ray_slot_sub = (uint32_t*)calloc(max_rays, sizeof(uint32_t));
  pv->ray_hits     = (struct s3d_hit*)calloc(max_rays, sizeof(struct s3d_hit));

  /* L4: GPU inline filter per-ray data */
  pv->filter_per_ray = (struct s3d_filter_per_ray*)calloc(
    max_rays, sizeof(struct s3d_filter_per_ray));

  if(!pv->ray_requests || !pv->ray_to_slot
  || !pv->ray_slot_sub || !pv->ray_hits || !pv->filter_per_ray)
    return RES_MEM_ERR;

  /* GPU batch trace context (will hold independent CUDA stream + params) */
  {
    res_T rc = s3d_batch_trace_context_create(&pv->batch_ctx, max_rays);
    if(rc != RES_OK) return rc;
  }

  /* Plan E: Borrow pinned buffer pointers from batch_ctx for direct-write */
  s3d_batch_trace_context_get_pinned_buffers(
    pv->batch_ctx, &pv->ray_pinned, &pv->filter_pinned, NULL);

  /* enc_locate buffers */
  pv->max_enc_locates     = capacity;
  pv->enc_locate_requests = (struct s3d_enc_locate_request*)calloc(
    capacity, sizeof(struct s3d_enc_locate_request));
  pv->enc_locate_results  = (struct s3d_enc_locate_result*)calloc(
    capacity, sizeof(struct s3d_enc_locate_result));
  pv->enc_locate_to_slot  = (uint32_t*)calloc(capacity, sizeof(uint32_t));

  if(!pv->enc_locate_requests || !pv->enc_locate_results
  || !pv->enc_locate_to_slot)
    return RES_MEM_ERR;

  {
    res_T rc = s3d_batch_enc_context_create(&pv->enc_batch_ctx, capacity);
    if(rc != RES_OK) return rc;
  }

  /* closest_point buffers */
  pv->max_cps     = capacity;
  pv->cp_requests = (struct s3d_cp_request*)calloc(
    capacity, sizeof(struct s3d_cp_request));
  pv->cp_hits     = (struct s3d_hit*)calloc(
    capacity, sizeof(struct s3d_hit));
  pv->cp_to_slot  = (uint32_t*)calloc(capacity, sizeof(uint32_t));

  if(!pv->cp_requests || !pv->cp_hits || !pv->cp_to_slot)
    return RES_MEM_ERR;

  {
    res_T rc = s3d_batch_cp_context_create(&pv->cp_batch_ctx, capacity);
    if(rc != RES_OK) return rc;
  }

  return RES_OK;
}

/**
 * pool_view_destroy — Free all resources held by a pool view.
 */
static void
pool_view_destroy(struct pool_view* pv)
{
  if(!pv) return;

  free(pv->active_indices);
  free(pv->need_ray_indices);
  free(pv->done_indices);
  free(pv->bucket_radiative);
  free(pv->bucket_conductive);
  free(pv->bucket_other);

  free(pv->ray_requests);
  free(pv->ray_to_slot);
  free(pv->ray_slot_sub);
  free(pv->ray_hits);
  free(pv->filter_per_ray);

  if(pv->batch_ctx) s3d_batch_trace_context_destroy(pv->batch_ctx);

  free(pv->enc_locate_requests);
  free(pv->enc_locate_results);
  free(pv->enc_locate_to_slot);
  if(pv->enc_batch_ctx) s3d_batch_enc_context_destroy(pv->enc_batch_ctx);

  free(pv->cp_requests);
  free(pv->cp_hits);
  free(pv->cp_to_slot);
  if(pv->cp_batch_ctx) s3d_batch_cp_context_destroy(pv->cp_batch_ctx);

  memset(pv, 0, sizeof(*pv));
}

/*******************************************************************************
 * P2: Dynamic merge / split — dual-buffer <-> single-buffer transitions
 ******************************************************************************/

/**
 * should_merge — Check if dual-buffer should collapse to single-buffer.
 *
 * Condition: either half's active count drops below 12.5% of its view_size.
 */
static int
should_merge(const struct wavefront_pool* pool)
{
  size_t thresh;
  if(pool->num_active_views != 2) return 0;

  thresh = pool->views[0].view_size / 8;     /* 12.5% */

  return (pool->views[0].active_compact < thresh)
      || (pool->views[1].active_compact < thresh);
}

/**
 * should_split — Check if single-buffer should split to dual-buffer.
 *
 * Condition: active paths > 75% pool capacity, and undistributed tasks remain.
 */
static int
should_split(const struct wavefront_pool* pool)
{
  if(pool->num_active_views != 1) return 0;
  if(pool->task_next >= pool->task_count) return 0;

  return pool->views[0].active_compact > (pool->pool_size * 3 / 4);
}

/**
 * merge_to_single_pool — Collapse dual-buffer views into a single full-range view.
 *
 * Precondition: both views' GPU traces have completed (no async pending).
 * views[0].capacity was allocated as pool_size, so no reallocation needed.
 * views[1] resources are NOT freed (may split back later).
 */
static void
merge_to_single_pool(struct wavefront_pool* pool)
{
  /* ── Fix: invalidate stale batch_idx for former V1 paths ──
   *
   * Each pool_view owns a separate ray_hits[] buffer.  After merge the
   * single-pool loop uses views[0]'s buffer exclusively.  V1 paths whose
   * needs_ray==1 still carry batch_idx values that index into views[1]'s
   * ray_hits — reading from views[0]'s buffer at those offsets yields
   * wrong ray results (root cause of post-merge M5_SF_FAIL failures).
   *
   * Setting batch_idx to the sentinel value makes merged_pass Phase A
   * skip these paths.  Phase B (cascade) is a no-op because it exits
   * immediately on needs_ray==1.  Phase C re-collects the ray requests
   * into views[0]'s buffer with correct indices.  The cost is one extra
   * GPU trace batch for these paths (~V1 active count, typically < 12.5%
   * of pool at merge time). */
  {
    size_t v1_base = pool->views[1].base;
    size_t v1_end  = v1_base + pool->views[1].view_size;
    size_t i;
    size_t invalidated = 0;
    for(i = v1_base; i < v1_end; i++) {
      if(pool->hot_arr[i].active && pool->hot_arr[i].needs_ray) {
        pool->slots[i].ray_req.batch_idx = (uint32_t)-1;
        invalidated++;
      }
    }
    (void)invalidated; /* used only for debugging */
  }

  /* Expand views[0] to cover the full pool */
  pool->views[0].base      = 0;
  pool->views[0].view_size = pool->pool_size;

  /* Clear runtime counts — next compact pass will rebuild them */
  pool->views[0].active_compact     = 0;
  pool->views[0].need_ray_count     = 0;
  pool->views[0].done_count         = 0;
  pool->views[0].bucket_radiative_n = 0;
  pool->views[0].bucket_conductive_n= 0;
  pool->views[0].ray_count          = 0;
  pool->views[0].enc_locate_count   = 0;
  pool->views[0].cp_count           = 0;

  pool->num_active_views = 1;

  /* views[1] resources retained for potential future split */
}

/**
 * split_to_dual_pool — Split single-buffer view into two half-range views.
 *
 * Precondition: views[1] was previously initialised by pool_view_init
 * (only valid if initially started in dual mode; single-mode start does
 * not initialise views[1], and should_split returns 0 in that case).
 *
 * Must be called before compact, so the next compact pass works on
 * the new view ranges.
 */
static void
split_to_dual_pool(struct wavefront_pool* pool)
{
  size_t half = pool->pool_size / 2;

  pool->views[0].base      = 0;
  pool->views[0].view_size = half;

  pool->views[1].base      = half;
  pool->views[1].view_size = half;

  /* Clear both views' runtime counts */
  pool->views[0].active_compact     = 0;
  pool->views[0].need_ray_count     = 0;
  pool->views[0].done_count         = 0;
  pool->views[0].bucket_radiative_n = 0;
  pool->views[0].bucket_conductive_n= 0;
  pool->views[0].ray_count          = 0;
  pool->views[0].enc_locate_count   = 0;
  pool->views[0].cp_count           = 0;

  pool->views[1].active_compact     = 0;
  pool->views[1].need_ray_count     = 0;
  pool->views[1].done_count         = 0;
  pool->views[1].bucket_radiative_n = 0;
  pool->views[1].bucket_conductive_n= 0;
  pool->views[1].ray_count          = 0;
  pool->views[1].enc_locate_count   = 0;
  pool->views[1].cp_count           = 0;

  pool->num_active_views = 2;
}

/*******************************************************************************
 * Pool allocation / deallocation
 *
 * pool_create — Self-adaptive pool allocation.
 *
 * @param per_view_size  Target number of slots per view (from determine_pool_size).
 * @param total_tasks    Total number of tasks (nrealisations, W*H*spp, etc.).
 *
 * Scheduling decision (dual- vs single-buffer) is made internally:
 *   ratio = total_tasks / per_view_size
 *   if ratio >= DUAL_BUFFER_RATIO_THRESHOLD  → dual-buffer pipeline
 *   else                                     → single-buffer
 *
 * Environment override: STARDIS_PIPELINE=0  forces single-buffer.
 *                       STARDIS_PIPELINE=1  forces dual-buffer.
 ******************************************************************************/
#define DUAL_BUFFER_RATIO_THRESHOLD 12
#define DUAL_BUFFER_MIN_VIEW_SIZE   512

static res_T
pool_create(struct wavefront_pool* pool, size_t per_view_size,
            size_t total_tasks)
{
  int dual;
  size_t pool_size;

  ASSERT(pool && per_view_size > 0 && total_tasks > 0);

  /* ---- Scheduling decision ---- */
  {
    const char* env = getenv("STARDIS_PIPELINE");
    if(env && env[0] == '0') {
      dual = 0;  /* forced single-buffer */
    } else if(env && env[0] == '1') {
      dual = 1;  /* forced dual-buffer */
    } else {
      /* Auto-adaptive: dual when workload saturates pool over many rounds */
      size_t ratio = total_tasks / per_view_size;
      dual = (ratio >= DUAL_BUFFER_RATIO_THRESHOLD
           && per_view_size >= DUAL_BUFFER_MIN_VIEW_SIZE) ? 1 : 0;
    }
  }

  if(dual) {
    pool_size = per_view_size * 2;
    /* Ensure even split */
    pool_size = (pool_size / 2) * 2;
    if(pool_size < 2) pool_size = 2;
  } else {
    pool_size = per_view_size;
  }

  memset(pool, 0, sizeof(*pool));
  pool->pool_size = pool_size;

  pool->slots = (struct path_state*)calloc(pool_size, sizeof(struct path_state));
  if(!pool->slots) return RES_MEM_ERR;

  /* Each path can request up to 6 rays per step (B-4 M1: ENC query) */
  pool->max_rays = pool_size * 6;
  pool->ray_requests = (struct s3d_ray_request*)malloc(
    pool->max_rays * sizeof(struct s3d_ray_request));
  pool->ray_to_slot = (uint32_t*)malloc(pool->max_rays * sizeof(uint32_t));
  pool->ray_slot_sub = (uint32_t*)malloc(pool->max_rays * sizeof(uint32_t));
  pool->ray_hits = (struct s3d_hit*)malloc(pool->max_rays * sizeof(struct s3d_hit));

  if(!pool->ray_requests || !pool->ray_to_slot
  || !pool->ray_slot_sub || !pool->ray_hits) {
    return RES_MEM_ERR;
  }

  /* Per-slot RNG array (pointers only; assigned later) */
  pool->slot_rngs = (struct ssp_rng**)calloc(pool_size, sizeof(struct ssp_rng*));
  if(!pool->slot_rngs) return RES_MEM_ERR;

  /* Stream compaction index arrays (M2.5) */
  pool->active_indices   = (uint32_t*)malloc(pool_size * sizeof(uint32_t));
  pool->need_ray_indices = (uint32_t*)malloc(pool_size * sizeof(uint32_t));
  pool->done_indices     = (uint32_t*)malloc(pool_size * sizeof(uint32_t));

  /* Path type buckets (M2.5) */
  pool->bucket_radiative  = (uint32_t*)malloc(pool_size * sizeof(uint32_t));
  pool->bucket_conductive = (uint32_t*)malloc(pool_size * sizeof(uint32_t));

  if(!pool->active_indices || !pool->need_ray_indices || !pool->done_indices
  || !pool->bucket_radiative || !pool->bucket_conductive) {
    return RES_MEM_ERR;
  }

  /* B-4 M10: enc_locate batch buffers */
  pool->max_enc_locates = pool_size;
  pool->enc_locate_requests = (struct s3d_enc_locate_request*)malloc(
    pool_size * sizeof(struct s3d_enc_locate_request));
  pool->enc_locate_results = (struct s3d_enc_locate_result*)malloc(
    pool_size * sizeof(struct s3d_enc_locate_result));
  pool->enc_locate_to_slot = (uint32_t*)malloc(
    pool_size * sizeof(uint32_t));

  if(!pool->enc_locate_requests || !pool->enc_locate_results
  || !pool->enc_locate_to_slot) {
    return RES_MEM_ERR;
  }

  /* B-4 M9: closest_point (WoS) batch buffers */
  pool->max_cps = pool_size;
  pool->cp_requests = (struct s3d_cp_request*)malloc(
    pool_size * sizeof(struct s3d_cp_request));
  pool->cp_hits = (struct s3d_hit*)malloc(
    pool_size * sizeof(struct s3d_hit));
  pool->cp_to_slot = (uint32_t*)malloc(
    pool_size * sizeof(uint32_t));

  if(!pool->cp_requests || !pool->cp_hits || !pool->cp_to_slot) {
    return RES_MEM_ERR;
  }

  /* P0_OPT: path_hot array (replaces P1 dispatch_soa) */
  {
    res_T hot_res = path_hot_arr_alloc(&pool->hot_arr, pool_size);
    if(hot_res != RES_OK) return hot_res;
  }

  /* P1: Cold-block SoA arrays (separated from path_state slots) */
  pool->sfn_arr = (struct path_sfn_data*)calloc(pool_size, sizeof(struct path_sfn_data));
  pool->enc_arr = (struct path_enc_data*)calloc(pool_size, sizeof(struct path_enc_data));
  pool->ext_arr = (struct path_ext_data*)calloc(pool_size, sizeof(struct path_ext_data));
  if(!pool->sfn_arr || !pool->enc_arr || !pool->ext_arr) return RES_MEM_ERR;

  /* Initialise diagnostics */
  pool->diag_min_batch = (size_t)-1; /* updated on first step with rays */
  pool->trace_batch_size_min = (size_t)-1;

  /* P2: Initialise unified pool views (decision already made above) */
  {
    res_T vrc;
    if(dual) {
      size_t half = pool_size / 2;

      /* views[0]: capacity = pool_size (supports merge to full range)
       *           initial view_size = half (front half only) */
      vrc = pool_view_init(&pool->views[0], 0, half, pool_size);
      if(vrc != RES_OK) return vrc;

      /* views[1]: capacity = half (back half only) */
      vrc = pool_view_init(&pool->views[1], half, half, half);
      if(vrc != RES_OK) return vrc;

      pool->num_active_views = 2;
    } else {
      /* Single-buffer mode: views[0] covers entire pool */
      vrc = pool_view_init(&pool->views[0], 0, pool_size, pool_size);
      if(vrc != RES_OK) return vrc;

      pool->num_active_views = 1;
    }
  }

  return RES_OK;
}

static void
pool_destroy(struct wavefront_pool* pool)
{
  if(!pool) return;

  /* P2: Destroy pool views (GPU batch contexts + view buffers) */
  pool_view_destroy(&pool->views[0]);
  pool_view_destroy(&pool->views[1]);  /* safe even if not initialised (zeroed) */

  if(pool->batch_ctx) s3d_batch_trace_context_destroy(pool->batch_ctx);
  if(pool->enc_batch_ctx) s3d_batch_enc_context_destroy(pool->enc_batch_ctx);
  if(pool->cp_batch_ctx) s3d_batch_cp_context_destroy(pool->cp_batch_ctx);

  /* Release per-slot RNGs */
  if(pool->thin_rng_storage) {
    /* Per-path CBRNG mode: thin wrappers own no heap state, free array */
    wf_rng_destroy_thin_ssp_rngs(pool->thin_rng_storage);
    pool->thin_rng_storage = NULL;
  } else if(pool->slot_rngs) {
    /* Legacy round-robin mode: release ref-counted shared RNGs */
    size_t i;
    for(i = 0; i < pool->pool_size; i++) {
      if(pool->slot_rngs[i]) SSP(rng_ref_put(pool->slot_rngs[i]));
    }
  }
  free(pool->slot_rngs);

  free(pool->task_queue);
  free(pool->slots);
  free(pool->ray_requests);
  free(pool->ray_to_slot);
  free(pool->ray_slot_sub);
  free(pool->ray_hits);

  /* Stream compaction arrays */
  free(pool->active_indices);
  free(pool->need_ray_indices);
  free(pool->done_indices);

  /* Bucket arrays */
  free(pool->bucket_radiative);
  free(pool->bucket_conductive);

  /* B-4 M10: enc_locate arrays */
  free(pool->enc_locate_requests);
  free(pool->enc_locate_results);
  free(pool->enc_locate_to_slot);

  /* B-4 M9: closest_point (WoS) arrays */
  free(pool->cp_requests);
  free(pool->cp_hits);
  free(pool->cp_to_slot);

  /* P0_OPT: path_hot array */
  path_hot_arr_free(&pool->hot_arr);

  /* P1: Cold-block SoA arrays */
  free(pool->sfn_arr);
  free(pool->enc_arr);
  free(pool->ext_arr);

  /* O11: per-thread ray buffers */
  if(pool->tl_ray_bufs) {
    int ti;
    for(ti = 0; ti < pool->tl_ray_nbufs; ti++)
      free(pool->tl_ray_bufs[ti]);
    free(pool->tl_ray_bufs);
  }

  memset(pool, 0, sizeof(*pool));
}

/*******************************************************************************
 * Task queue generation
 *
 * Tasks are generated in the same traversal order as the original code:
 *   tile Morton order -> pixel Morton order within tile -> SPP
 ******************************************************************************/
static res_T
generate_task_queue(
  struct wavefront_pool* pool,
  const size_t image_def[2],
  const size_t spp)
{
  size_t ntiles_x, ntiles_y, ntiles_adjusted;
  size_t tile_mcode, pix_mcode;
  size_t task_idx = 0;
  size_t total_tasks = image_def[0] * image_def[1] * spp;

  ASSERT(pool && image_def && spp > 0);

  pool->task_queue = (struct pixel_task*)malloc(
    total_tasks * sizeof(struct pixel_task));
  if(!pool->task_queue) return RES_MEM_ERR;

  pool->task_count = total_tasks;
  pool->task_next = 0;

  /* Tile grid dimensions (same as sdis_solve_camera) */
  ntiles_x = (image_def[0] + (TILE_SIZE - 1)) / TILE_SIZE;
  ntiles_y = (image_def[1] + (TILE_SIZE - 1)) / TILE_SIZE;
  ntiles_adjusted = round_up_pow2(MMAX(ntiles_x, ntiles_y));
  ntiles_adjusted *= ntiles_adjusted;

  /* Iterate tiles in Morton order */
  for(tile_mcode = 0; tile_mcode < ntiles_adjusted; tile_mcode++) {
    size_t tile_x, tile_y;
    size_t tile_org[2], tile_sz[2];
    size_t npixels_morton;

    tile_x = morton2D_decode_u16((uint32_t)(tile_mcode >> 0));
    if(tile_x >= ntiles_x) continue;
    tile_y = morton2D_decode_u16((uint32_t)(tile_mcode >> 1));
    if(tile_y >= ntiles_y) continue;

    tile_org[0] = tile_x * TILE_SIZE;
    tile_org[1] = tile_y * TILE_SIZE;
    tile_sz[0] = MMIN(TILE_SIZE, image_def[0] - tile_org[0]);
    tile_sz[1] = MMIN(TILE_SIZE, image_def[1] - tile_org[1]);

    /* Iterate pixels within tile in Morton order */
    npixels_morton = round_up_pow2(MMAX(tile_sz[0], tile_sz[1]));
    npixels_morton *= npixels_morton;

    for(pix_mcode = 0; pix_mcode < npixels_morton; pix_mcode++) {
      uint16_t ipix_tile[2];
      size_t ipix_image[2];
      size_t irealisation;

      ipix_tile[0] = morton2D_decode_u16((uint32_t)(pix_mcode >> 0));
      if(ipix_tile[0] >= tile_sz[0]) continue;
      ipix_tile[1] = morton2D_decode_u16((uint32_t)(pix_mcode >> 1));
      if(ipix_tile[1] >= tile_sz[1]) continue;

      ipix_image[0] = ipix_tile[0] + tile_org[0];
      ipix_image[1] = ipix_tile[1] + tile_org[1];

      for(irealisation = 0; irealisation < spp; irealisation++) {
        struct pixel_task* t = &pool->task_queue[task_idx];
        t->ipix_image[0] = ipix_image[0];
        t->ipix_image[1] = ipix_image[1];
        t->spp_idx = (uint32_t)irealisation;
        task_idx++;
      }
    }
  }

  ASSERT(task_idx == total_tasks);
  return RES_OK;
}

/*******************************************************************************
 * Path initialisation -- initialise a single path from a pixel_task
 *
 * Mirrors init_all_paths from sdis_solve_wavefront.c but operates on a
 * single slot.
 ******************************************************************************/
static res_T
init_single_path(
  struct path_state* p,
  struct path_hot* hot,
  struct ssp_rng* rng,
  const struct pixel_task* task,
  struct sdis_scene* scn,
  const unsigned enc_id,
  const struct sdis_camera* cam,
  const double time_range[2],
  const double pix_sz[2],
  const size_t picard_order,
  const enum sdis_diffusion_algorithm diff_algo,
  uint32_t path_id,
  uint64_t global_seed)
{
  double samp[2];
  double ray_pos[3], ray_dir[3];
  double time;

  ASSERT(p && rng && task && scn && cam && pix_sz && time_range);

  /* O8: Full memset is load-bearing — filter_data_storage, ray_bucket,
   * ray_count_ext, and the locals union (~1420B) all require zero-init.
   * Targeted elimination requires auditing ~45 step functions.  Deferred
   * to O9 (path_state hot/cold SoA split) which naturally separates the
   * rarely-written cold portion.  Cost: ~2KB × refill_count/step. */
  memset(p, 0, sizeof(*p));

  /* Identity */
  p->path_id = path_id;
  p->pixel_x = (uint16_t)task->ipix_image[0]; /* absolute image coords */
  p->pixel_y = (uint16_t)task->ipix_image[1];
  p->realisation_idx = task->spp_idx;
  p->ipix_image[0] = task->ipix_image[0];
  p->ipix_image[1] = task->ipix_image[1];

  /* Use per-slot RNG (thin wrapper pointing to p->rng_state) */
  p->rng = rng;

  /* Seed per-path CBRNG with (pixel_x, pixel_y, spp_idx, global_seed).
   * The thin ssp_rng wrapper's state already points to &p->rng_state,
   * so all ssp_rng_canonical(p->rng) calls will use this seeded state. */
  wf_rng_seed(&p->rng_state,
              (uint32_t)task->ipix_image[0],
              (uint32_t)task->ipix_image[1],
              task->spp_idx,
              global_seed);

  /* Sample time (same RNG call order as solve_pixel) */
  time = sample_time(p->rng, time_range);

  /* Pixel sub-sample */
  samp[0] = ((double)task->ipix_image[0]
            + ssp_rng_canonical(p->rng)) * pix_sz[0];
  samp[1] = ((double)task->ipix_image[1]
            + ssp_rng_canonical(p->rng)) * pix_sz[1];

  /* Camera ray */
  camera_ray(cam, samp, ray_pos, ray_dir);

  /* Initialise random walk */
  p->rwalk = RWALK_NULL;
  d3_set(p->rwalk.vtx.P, ray_pos);
  p->rwalk.vtx.time = time;
  p->rwalk.hit_3d = S3D_HIT_NULL;
  p->rwalk.hit_side = SDIS_SIDE_NULL__;
  p->rwalk.enc_id = enc_id;

  /* O11_SAFETY: sentinel — marks "never submitted to GPU" so the first
   * merged_pass Phase A skips distribute for this path. */
  p->ray_req.batch_idx = (uint32_t)-1;

  /* Initialise rwalk_context */
  p->ctx = RWALK_CONTEXT_NULL;
  p->ctx.heat_path   = NULL;
  p->ctx.green_path  = NULL;
  p->ctx.Tmin  = scn->tmin;
  p->ctx.Tmin2 = scn->tmin * scn->tmin;
  p->ctx.Tmin3 = scn->tmin * scn->tmin * scn->tmin;
  p->ctx.That  = scn->tmax;
  p->ctx.That2 = scn->tmax * scn->tmax;
  p->ctx.That3 = scn->tmax * scn->tmax * scn->tmax;
  p->ctx.max_branchings = picard_order - 1;
  p->ctx.nbranchings    = SIZE_MAX;
  p->ctx.irealisation   = task->spp_idx;
  p->ctx.diff_algo      = diff_algo;

  /* Temperature accumulator */
  p->T = TEMPERATURE_NULL;

  /* Set initial radiative direction from camera ray */
  f3_set_d3(p->rad_direction, ray_dir);
  p->rad_bounce_count = 0;
  p->rad_retry_count  = 0;

  /* Set lifecycle — P0_OPT: write hot fields to path_hot */
  hot->phase   = (uint8_t)PATH_INIT;
  hot->active  = 1;
  hot->needs_ray = 0;
  hot->ray_bucket = 0;
  hot->ray_count_ext = 0;

  /* Zero scratch areas */
  p->locals.cnd_ds.initialized = 0;
  p->locals.cnd_ds.enc_id = ENCLOSURE_ID_NULL;
  p->locals.cnd_ds.medium = NULL;
  p->locals.cnd_ds.props_ref = SOLID_PROPS_NULL;
  p->locals.cnd_ds.green_power_term = 0;
  memset(p->locals.cnd_ds.position_start, 0, sizeof(p->locals.cnd_ds.position_start));
  p->locals.cnd_ds.robust_attempt = 0;
  p->locals.cnd_ds.delta = 0;
  p->locals.cnd_ds.delta_solid_param = 0;
  p->coupled_nbranchings = -1;
  p->steps_taken = 0;
  p->done_reason = 0;

  return RES_OK;
}

/*******************************************************************************
 * wavefront_ops — CAMERA mode callbacks
 ******************************************************************************/

/* camera_generate_tasks: populate task queue in Morton order W×H×spp */
static res_T
camera_generate_tasks(struct wavefront_pool* pool, const void* mode_ctx)
{
  const struct camera_mode_ctx* ctx =
    (const struct camera_mode_ctx*)mode_ctx;
  return generate_task_queue(pool, ctx->image_def, ctx->spp);
}

/* camera_init_path: init a single camera path from a pixel_task */
static res_T
camera_init_path(struct path_state* p,
                 struct path_hot* hot,
                 struct ssp_rng* rng,
                 const struct pixel_task* task,
                 struct sdis_scene* scn,
                 unsigned enc_id,
                 const void* mode_ctx,
                 const double* time_range,
                 size_t picard_order,
                 enum sdis_diffusion_algorithm diff_algo,
                 uint32_t path_id,
                 uint64_t global_seed)
{
  const struct camera_mode_ctx* ctx =
    (const struct camera_mode_ctx*)mode_ctx;
  return init_single_path(p, hot, rng, task, scn, enc_id,
    ctx->cam, time_range, ctx->pix_sz,
    picard_order, diff_algo, path_id, global_seed);
}

/* camera_accumulate_result: write one completed path to estimator_buffer */
static void
camera_accumulate_result(const struct path_state* p, void* result_ctx)
{
  struct sdis_estimator_buffer* buf =
    (struct sdis_estimator_buffer*)result_ctx;
  struct sdis_estimator* estimator =
    estimator_buffer_grab(buf, p->ipix_image[0], p->ipix_image[1]);

  if(p->T.done) {
    double val = p->T.value;
    #pragma omp atomic
    estimator->temperature.sum  += val;
    #pragma omp atomic
    estimator->temperature.sum2 += val * val;
    #pragma omp atomic
    estimator->temperature.count += 1;
    #pragma omp atomic
    estimator->realisation_time.count += 1;
    #pragma omp atomic
    estimator->nrealisations += 1;
  }
}

/* Static camera vtable instance */
static const struct wavefront_ops wf_ops_camera = {
  camera_generate_tasks,
  camera_init_path,
  camera_accumulate_result
};

/*******************************************************************************
 * wavefront_ops — PROBE mode callbacks
 ******************************************************************************/

/* probe_generate_tasks: nrealisations linear tasks at (0,0) */
static res_T
probe_generate_tasks(struct wavefront_pool* pool, const void* mode_ctx)
{
  const struct probe_mode_ctx* ctx =
    (const struct probe_mode_ctx*)mode_ctx;
  size_t i;
  size_t total = ctx->nrealisations;

  pool->task_queue = (struct pixel_task*)malloc(
    total * sizeof(struct pixel_task));
  if(!pool->task_queue) return RES_MEM_ERR;

  pool->task_count = total;
  pool->task_next = 0;

  for(i = 0; i < total; i++) {
    struct pixel_task* t = &pool->task_queue[i];
    t->ipix_image[0] = 0;
    t->ipix_image[1] = 0;
    t->spp_idx = (uint32_t)i;
  }
  return RES_OK;
}

/* probe_init_path: init a single conductive/convective path from probe position.
 * Adapted from init_paths_from_probe() in sdis_solve_wavefront.c (single-path
 * version with CBRNG seeding instead of shared RNG). */
static res_T
probe_init_path(struct path_state* p,
                struct path_hot* hot,
                struct ssp_rng* rng,
                const struct pixel_task* task,
                struct sdis_scene* scn,
                unsigned enc_id,
                const void* mode_ctx,
                const double* time_range,
                size_t picard_order,
                enum sdis_diffusion_algorithm diff_algo,
                uint32_t path_id,
                uint64_t global_seed)
{
  const struct probe_mode_ctx* ctx =
    (const struct probe_mode_ctx*)mode_ctx;
  double time;

  ASSERT(p && rng && task && scn && time_range);

  /* O8: See comment in init_single_path \u2014 full memset is load-bearing. */
  memset(p, 0, sizeof(*p));

  /* Identity \u2014 probe uses virtual pixel (0,0) */
  p->path_id = path_id;
  p->pixel_x = 0;
  p->pixel_y = 0;
  p->realisation_idx = task->spp_idx;
  p->ipix_image[0] = 0;
  p->ipix_image[1] = 0;

  /* Per-slot RNG (thin wrapper pointing to p->rng_state) */
  p->rng = rng;

  /* Seed per-path CBRNG (same as camera but pixel coords are zero) */
  wf_rng_seed(&p->rng_state, 0, 0, task->spp_idx, global_seed);

  /* Sample time */
  time = sample_time(p->rng, time_range);

  /* Directly set probe position — no camera_ray */
  p->rwalk = RWALK_NULL;
  d3_set(p->rwalk.vtx.P, ctx->position);
  p->rwalk.vtx.time = time;
  p->rwalk.hit_3d = S3D_HIT_NULL;
  p->rwalk.hit_side = SDIS_SIDE_NULL__;
  p->rwalk.enc_id = enc_id;

  /* O11_SAFETY: sentinel — marks "never submitted to GPU" */
  p->ray_req.batch_idx = (uint32_t)-1;

  /* Initialise rwalk_context (matches init_paths_from_probe) */
  p->ctx = RWALK_CONTEXT_NULL;
  p->ctx.heat_path   = NULL;
  p->ctx.green_path  = NULL;
  p->ctx.Tmin   = scn->tmin;
  p->ctx.Tmin2  = scn->tmin * scn->tmin;
  p->ctx.Tmin3  = scn->tmin * scn->tmin * scn->tmin;
  p->ctx.That   = scn->tmax;
  p->ctx.That2  = scn->tmax * scn->tmax;
  p->ctx.That3  = scn->tmax * scn->tmax * scn->tmax;
  p->ctx.max_branchings = picard_order - 1;
  p->ctx.nbranchings    = 0;  /* probe: nbranchings=0 at entry */
  p->ctx.irealisation   = task->spp_idx;
  p->ctx.diff_algo      = diff_algo;

  /* Temperature accumulator — T.func based on medium type */
  p->T = TEMPERATURE_NULL;
  if(ctx->mdm_type == SDIS_SOLID) {
    p->T.func = conductive_path_3d;
  } else {
    p->T.func = convective_path_3d;
  }

  /* Probe has no camera ray direction */
  memset(p->rad_direction, 0, sizeof(p->rad_direction));
  p->rad_bounce_count = 0;
  p->rad_retry_count  = 0;

  /* Lifecycle — P0_OPT: write hot fields to path_hot */
  if(ctx->mdm_type == SDIS_SOLID) {
    hot->phase = (uint8_t)PATH_COUPLED_CONDUCTIVE;
  } else {
    hot->phase = (uint8_t)PATH_COUPLED_CONVECTIVE;
  }
  hot->active  = 1;
  hot->needs_ray = 0;
  hot->ray_bucket = 0;
  hot->ray_count_ext = 0;

  /* Zero scratch areas */
  p->locals.cnd_ds.initialized = 0;
  p->locals.cnd_ds.enc_id = ENCLOSURE_ID_NULL;
  p->locals.cnd_ds.medium = NULL;
  p->locals.cnd_ds.props_ref = SOLID_PROPS_NULL;
  p->locals.cnd_ds.green_power_term = 0;
  memset(p->locals.cnd_ds.position_start, 0, sizeof(p->locals.cnd_ds.position_start));
  p->locals.cnd_ds.robust_attempt = 0;
  p->locals.cnd_ds.delta = 0;
  p->locals.cnd_ds.delta_solid_param = 0;
  p->coupled_nbranchings = 0;
  p->steps_taken = 0;
  p->done_reason = 0;

  return RES_OK;
}

/* probe_accumulate_result: accumulate one completed path to struct accum */
static void
probe_accumulate_result(const struct path_state* p, void* result_ctx)
{
  struct accum* acc = (struct accum*)result_ctx;
  if(p->T.done) {
    double val = p->T.value;
    #pragma omp atomic
    acc->sum  += val;
    #pragma omp atomic
    acc->sum2 += val * val;
    #pragma omp atomic
    acc->count += 1;
  }
}

/* Static probe vtable instance */
static const struct wavefront_ops wf_ops_probe = {
  probe_generate_tasks,
  probe_init_path,
  probe_accumulate_result
};

/*******************************************************************************
 * wavefront_ops — PROBE BATCH mode callbacks
 *
 * Multi-probe variant: K probes share the pool.  Each task carries its probe
 * index in ipix_image[0], which propagates through path_state to allow
 * accumulate_result to route to the correct accum bucket.
 ******************************************************************************/

/* probe_batch_generate_tasks: nprobes × nrealisations tasks.
 * ipix_image[0] = probe_index, spp_idx = realisation index. */
static res_T
probe_batch_generate_tasks(struct wavefront_pool* pool, const void* mode_ctx)
{
  const struct probe_batch_mode_ctx* ctx =
    (const struct probe_batch_mode_ctx*)mode_ctx;
  size_t total = ctx->nprobes * ctx->nrealisations;
  size_t pi, ri, k;

  pool->task_queue = (struct pixel_task*)malloc(
    total * sizeof(struct pixel_task));
  if(!pool->task_queue) return RES_MEM_ERR;

  pool->task_count = total;
  pool->task_next = 0;

  /* Interleave: probe 0 real 0, probe 1 real 0, ..., probe K-1 real 0,
   * probe 0 real 1, ...  This maximises diversity within each batch. */
  k = 0;
  for(ri = 0; ri < ctx->nrealisations; ri++) {
    for(pi = 0; pi < ctx->nprobes; pi++) {
      struct pixel_task* t = &pool->task_queue[k++];
      t->ipix_image[0] = pi;        /* probe index */
      t->ipix_image[1] = 0;
      t->spp_idx = (uint32_t)ri;
    }
  }
  return RES_OK;
}

/* probe_batch_init_path: same as probe_init_path but reads position/enc_id/
 * mdm_type from batch arrays indexed by task->ipix_image[0]. */
static res_T
probe_batch_init_path(struct path_state* p,
                      struct path_hot* hot,
                      struct ssp_rng* rng,
                      const struct pixel_task* task,
                      struct sdis_scene* scn,
                      unsigned enc_id,
                      const void* mode_ctx,
                      const double* time_range,
                      size_t picard_order,
                      enum sdis_diffusion_algorithm diff_algo,
                      uint32_t path_id,
                      uint64_t global_seed)
{
  const struct probe_batch_mode_ctx* ctx =
    (const struct probe_batch_mode_ctx*)mode_ctx;
  size_t probe_idx = task->ipix_image[0];
  const double* pos = &ctx->positions[probe_idx * 3];
  unsigned probe_enc_id = ctx->enc_ids[probe_idx];
  enum sdis_medium_type mdm_type = ctx->mdm_types[probe_idx];
  double time;

  (void)enc_id; /* batch overrides with per-probe enc_id */

  ASSERT(p && rng && task && scn && time_range);
  ASSERT(probe_idx < ctx->nprobes);

  memset(p, 0, sizeof(*p));

  /* Identity — ipix_image[0] = probe_index for accumulate routing */
  p->path_id = path_id;
  p->pixel_x = 0;
  p->pixel_y = 0;
  p->realisation_idx = task->spp_idx;
  p->ipix_image[0] = probe_idx;
  p->ipix_image[1] = 0;

  p->rng = rng;
  wf_rng_seed(&p->rng_state,
    (uint32_t)probe_idx, 0, task->spp_idx, global_seed);

  time = sample_time(p->rng, time_range);

  p->rwalk = RWALK_NULL;
  d3_set(p->rwalk.vtx.P, pos);
  p->rwalk.vtx.time = time;
  p->rwalk.hit_3d = S3D_HIT_NULL;
  p->rwalk.hit_side = SDIS_SIDE_NULL__;
  p->rwalk.enc_id = probe_enc_id;

  /* O11_SAFETY: sentinel — marks "never submitted to GPU" */
  p->ray_req.batch_idx = (uint32_t)-1;

  p->ctx = RWALK_CONTEXT_NULL;
  p->ctx.heat_path   = NULL;
  p->ctx.green_path  = NULL;
  p->ctx.Tmin   = scn->tmin;
  p->ctx.Tmin2  = scn->tmin * scn->tmin;
  p->ctx.Tmin3  = scn->tmin * scn->tmin * scn->tmin;
  p->ctx.That   = scn->tmax;
  p->ctx.That2  = scn->tmax * scn->tmax;
  p->ctx.That3  = scn->tmax * scn->tmax * scn->tmax;
  p->ctx.max_branchings = picard_order - 1;
  p->ctx.nbranchings    = 0;
  p->ctx.irealisation   = task->spp_idx;
  p->ctx.diff_algo      = diff_algo;

  p->T = TEMPERATURE_NULL;
  if(mdm_type == SDIS_SOLID) {
    p->T.func = conductive_path_3d;
  } else {
    p->T.func = convective_path_3d;
  }

  memset(p->rad_direction, 0, sizeof(p->rad_direction));
  p->rad_bounce_count = 0;
  p->rad_retry_count  = 0;

  /* P0_OPT: write hot fields to path_hot */
  if(mdm_type == SDIS_SOLID) {
    hot->phase = (uint8_t)PATH_COUPLED_CONDUCTIVE;
  } else {
    hot->phase = (uint8_t)PATH_COUPLED_CONVECTIVE;
  }
  hot->active  = 1;
  hot->needs_ray = 0;
  hot->ray_bucket = 0;
  hot->ray_count_ext = 0;

  p->locals.cnd_ds.initialized = 0;
  p->locals.cnd_ds.enc_id = ENCLOSURE_ID_NULL;
  p->locals.cnd_ds.medium = NULL;
  p->locals.cnd_ds.props_ref = SOLID_PROPS_NULL;
  p->locals.cnd_ds.green_power_term = 0;
  memset(p->locals.cnd_ds.position_start, 0, sizeof(p->locals.cnd_ds.position_start));
  p->locals.cnd_ds.robust_attempt = 0;
  p->locals.cnd_ds.delta = 0;
  p->locals.cnd_ds.delta_solid_param = 0;
  p->coupled_nbranchings = 0;
  p->steps_taken = 0;
  p->done_reason = 0;

  return RES_OK;
}

/* probe_batch_accumulate_result: route result to accums[probe_index] */
static void
probe_batch_accumulate_result(const struct path_state* p, void* result_ctx)
{
  struct probe_batch_mode_ctx* ctx = (struct probe_batch_mode_ctx*)result_ctx;
  size_t probe_idx = p->ipix_image[0];
  struct accum* acc;
  ASSERT(probe_idx < ctx->nprobes);
  acc = &ctx->accums[probe_idx];
  if(p->T.done) {
    double val = p->T.value;
    #pragma omp atomic
    acc->sum  += val;
    #pragma omp atomic
    acc->sum2 += val * val;
    #pragma omp atomic
    acc->count += 1;
  }
}

/* Static probe batch vtable instance */
static const struct wavefront_ops wf_ops_probe_batch = {
  probe_batch_generate_tasks,
  probe_batch_init_path,
  probe_batch_accumulate_result
};

/*******************************************************************************
 * Advance a single path past PATH_INIT to its first ray-needed phase (or
 * PATH_DONE).  Shared by fill_pool() and refill_pool().
 ******************************************************************************/
static res_T
advance_path_to_first_ray(struct path_state* p,
                          struct sdis_scene* scn,
                          struct wavefront_pool* pool,
                          size_t slot_idx)
{
  /* P0_OPT: hot was already initialised by init_path().  Step functions
   * write to hot directly.  No SYNC POINT C needed afterward. */
  struct path_hot* hot = &pool->hot_arr[slot_idx];
  res_T res = RES_OK;

  while(hot->active && !hot->needs_ray
      && (enum path_phase)hot->phase != PATH_DONE
      && (enum path_phase)hot->phase != PATH_ERROR) {
    int advanced = 0;
    enum path_phase phase_before = (enum path_phase)hot->phase;
    if(path_phase_is_ray_pending(phase_before)) break;

    res = advance_one_step_no_ray(p, hot, scn, &advanced, pool, slot_idx);

    /* O11_DIAG: log after first-ray cascade step */
    pt_log("F", 0, (uint32_t)slot_idx, p, hot, pool,
           (int)phase_before, (int)res);

    if(res != RES_OK && res != RES_BAD_OP
    && res != RES_BAD_OP_IRRECOVERABLE) return res;
    if(res == RES_BAD_OP || res == RES_BAD_OP_IRRECOVERABLE) {
      hot->phase = (uint8_t)PATH_DONE;
      hot->active = 0;
      p->done_reason = -1;
      #pragma omp atomic
      pool->paths_failed++;
      res = RES_OK;
      break;
    }
    if(!advanced) break;
    /* M8: intercept PATH_DONE when sfn_stack_depth > 0 */
    if((enum path_phase)hot->phase == PATH_DONE
    && pool->sfn_arr[slot_idx].depth > 0) {
      hot->phase = (uint8_t)PATH_BND_SFN_COMPUTE_Ti_RESUME;
      hot->active = 1;
    }
    p->steps_taken++;
  }
  return RES_OK;
}

/*******************************************************************************
 * Fill pool -- load the initial batch of tasks
 ******************************************************************************/
static res_T
fill_pool(struct wavefront_pool* pool)
{
  size_t i;
  res_T res = RES_OK;

  ASSERT(pool && pool->task_next == 0);

  for(i = 0; i < pool->pool_size && pool->task_next < pool->task_count; i++) {
    const struct pixel_task* task = &pool->task_queue[pool->task_next];

    res = pool->ops->init_path(
      &pool->slots[i], &pool->hot_arr[i], pool->slot_rngs[i], task,
      pool->scn, pool->enc_id, pool->mode_ctx,
      pool->time_range,
      pool->picard_order, pool->diff_algo,
      pool->next_path_id++, pool->global_seed);
    if(res != RES_OK) return res;

    /* Advance past PATH_INIT to first ray request */
    res = advance_path_to_first_ray(&pool->slots[i], pool->scn, pool, i);
    if(res != RES_OK) return res;
    /* P0_OPT: hot_arr[i] already written by advance_path_to_first_ray */

    pool->task_next++;
  }

  /* Count active paths (P0_OPT: read from hot_arr) */
  pool->active_count = 0;
  for(i = 0; i < pool->pool_size; i++) {
    if(pool->hot_arr[i].active) pool->active_count++;
  }

  return RES_OK;
}

/*******************************************************************************
 * Stream Compaction (M2.5 + P1 SoA) -- rebuild compact index arrays each step.
 *
 * P0_OPT: reads phase/active/needs_ray from hot_arr (8B AoS) instead of
 * path_state (~2KB AoS), reducing cache footprint.  8 slots / cache line.
 *
 * After this function:
 *   active_indices[0..active_compact-1]   = indices of active, non-done slots
 *   need_ray_indices[0..need_ray_count-1] = active slots needing ray trace
 *   done_indices[0..done_count-1]         = slots with PATH_DONE
 *   bucket_radiative[0..n-1]              = need_ray + radiative phase
 *   bucket_conductive[0..n-1]             = need_ray + conductive phase
 ******************************************************************************/
static void
compact_active_paths(struct wavefront_pool* pool, struct pool_view* pv)
{
  size_t base = pv->base;
  size_t end  = base + pv->view_size;
  size_t i;

  pv->active_compact      = 0;
  pv->done_count          = 0;

  /* O12: In merged architecture, need_ray_indices and bucket arrays are
   * not used — merged_pass handles collect inline.  Compact now only scans
   * hot_arr (80KB, fits in L2) without touching path_state (20MB). */
  for(i = base; i < end; i++) {
    enum path_phase ph = (enum path_phase)pool->hot_arr[i].phase;
    int act = pool->hot_arr[i].active;

    if(ph == PATH_DONE || ph == PATH_ERROR
    || ph == PATH_HARVESTED) {
      /* M8: if sfn_stack_depth > 0, the sub-path finished but the
       * parent picardN frame still needs to resume.  Re-activate.
       * Need to access SoA for sfn_stack_depth (rare path). */
      if(ph == PATH_DONE && pool->sfn_arr[i].depth > 0) {
        pool->hot_arr[i].phase = (uint8_t)PATH_BND_SFN_COMPUTE_Ti_RESUME;
        pool->hot_arr[i].active = 1;
        act = 1;
        /* Fall through to treat as active path */
      } else {
        pv->done_indices[pv->done_count++] = (uint32_t)i;
        continue;
      }
    }
    if(!act) continue;

    pv->active_indices[pv->active_compact++] = (uint32_t)i;
  }
}

/*******************************************************************************
 * Collect ray requests -- compact version (M2.5)
 *
 * Only iterates over need_ray_indices instead of the full pool.
 ******************************************************************************/
static res_T
pool_collect_ray_requests_compact(struct wavefront_pool* pool)
{
  size_t ray_idx = 0;
  size_t k;

  for(k = 0; k < pool->need_ray_count; k++) {
    uint32_t i = pool->need_ray_indices[k];
    struct path_state* p = &pool->slots[i];
    enum path_phase cph = (enum path_phase)pool->hot_arr[i].phase;

    ASSERT(pool->hot_arr[i].active && pool->hot_arr[i].needs_ray
        && p->ray_req.ray_count >= 1);

    /* Detailed stats by phase — semantic layer + per-phase layer.
     * NOTE: compact collect lacks SoA; inline count_path_rays logic.
     * P0_OPT: read phase from hot_arr (source of truth). */
    {
      size_t nrays = (cph == PATH_ENC_QUERY_EMIT
                   && p->ray_req.ray_count >= 2) ? 6
                   : (size_t)p->ray_req.ray_count;
      switch(cph) {
      case PATH_RAD_TRACE_PENDING:
      case PATH_BND_SF_NULLCOLL_RAD_TRACE:
      case PATH_BND_SFN_RAD_TRACE:
      case PATH_BND_EXT_DIFFUSE_TRACE:
      case PATH_CND_WOS_FALLBACK_TRACE:
      case PATH_COUPLED_BOUNDARY_REINJECT:
      case PATH_BND_SS_REINJECT_SAMPLE:
      case PATH_BND_SF_REINJECT_SAMPLE:
        pool->rays_radiative += nrays; break;
      case PATH_COUPLED_COND_DS_PENDING:
      case PATH_CND_DS_STEP_TRACE:
        if(p->locals.cnd_ds.robust_attempt > 0) pool->rays_conductive_ds_retry += nrays;
        else                         pool->rays_conductive_ds += nrays;
        break;
      case PATH_BND_EXT_DIRECT_TRACE:
      case PATH_BND_EXT_DIFFUSE_SHADOW_TRACE:
        pool->rays_shadow += nrays; break;
      case PATH_ENC_QUERY_EMIT:
      case PATH_ENC_QUERY_FB_EMIT:
      case PATH_CND_INIT_ENC:
        pool->rays_enclosure += nrays; break;
      case PATH_CNV_STARTUP_TRACE:
        pool->rays_startup += nrays; break;
      default:
        pool->rays_other += nrays; break;
      }
    }

    /* Ray 0 */
    {
      struct s3d_ray_request* rr = &pool->ray_requests[ray_idx];
      rr->origin[0]    = p->ray_req.origin[0];
      rr->origin[1]    = p->ray_req.origin[1];
      rr->origin[2]    = p->ray_req.origin[2];
      rr->direction[0] = p->ray_req.direction[0];
      rr->direction[1] = p->ray_req.direction[1];
      rr->direction[2] = p->ray_req.direction[2];
      rr->range[0]     = p->ray_req.range[0];
      rr->range[1]     = p->ray_req.range[1];
      rr->user_id      = i;

      if(!S3D_HIT_NONE(&p->filter_data_storage.hit_3d)) {
        rr->filter_data = &p->filter_data_storage;
      } else {
        rr->filter_data = NULL;
      }

      pool->ray_to_slot[ray_idx] = i;
      pool->ray_slot_sub[ray_idx] = 0;
      p->ray_req.batch_idx = (uint32_t)ray_idx;
      ray_idx++;
    }

    /* Ray 1 (if 2-ray request: delta_sphere or reinjection) */
    if(p->ray_req.ray_count >= 2) {
      struct s3d_ray_request* rr = &pool->ray_requests[ray_idx];
      rr->origin[0]    = p->ray_req.origin[0];
      rr->origin[1]    = p->ray_req.origin[1];
      rr->origin[2]    = p->ray_req.origin[2];
      rr->direction[0] = p->ray_req.direction2[0];
      rr->direction[1] = p->ray_req.direction2[1];
      rr->direction[2] = p->ray_req.direction2[2];
      rr->range[0]     = p->ray_req.range2[0];
      rr->range[1]     = p->ray_req.range2[1];
      if(!S3D_HIT_NONE(&p->filter_data_storage.hit_3d)) {
        rr->filter_data = &p->filter_data_storage;
      } else {
        rr->filter_data = NULL;
      }
      rr->user_id      = i;

      pool->ray_to_slot[ray_idx] = i;
      pool->ray_slot_sub[ray_idx] = 1;
      p->ray_req.batch_idx2 = (uint32_t)ray_idx;
      ray_idx++;
    }
  }

  pool->ray_count = ray_idx;
  return RES_OK;
}

/*******************************************************************************
 * Distribute batch trace results — bucketed dispatch (M3)
 *
 * Instead of a generic switch over p->phase via advance_one_step_with_ray,
 * iterate per-type buckets: all radiative paths first, then all conductive
 * paths.  This eliminates branch misprediction (~50% → ~99% hit rate) and
 * improves cache locality since each step function accesses a distinct
 * subset of scene data.
 *
 * Any ray-pending paths NOT in either bucket (e.g. future boundary reinject)
 * fall through to a generic fallback loop.
 ******************************************************************************/

/* Helper: mark a failed path as done (P0_OPT: write to hot) */
static INLINE void
mark_path_failed(struct path_state* p, struct path_hot* hot,
                 struct wavefront_pool* pool)
{
  hot->phase = (uint8_t)PATH_DONE;
  hot->active = 0;
  p->done_reason = -1;
  pool->paths_failed++;
}

/* Helper: elapsed time in seconds between two struct time values (M3) */
static INLINE double
time_elapsed_sec(const struct time* t0, const struct time* t1)
{
  struct time dt;
  time_sub(&dt, t1, t0);
  return (double)dt.sec + (double)dt.nsec * 1e-9;
}

/*******************************************************************************
 * Bucketed ray request collection (B-4 M2)
 *
 * 2-pass radix scatter: sorts rays by ray_bucket_type so that same-type
 * rays occupy contiguous segments in the ray_requests[] array.  This
 * improves GPU warp coherence during BVH traversal since rays in the
 * same warp share similar traversal patterns.
 *
 * Pass 1: Count rays per bucket (iterate need_ray_indices once).
 * Pass 2: Compute bucket offsets, then scatter rays into final positions.
 *
 * The output is still a single contiguous ray_requests[] array.  Bucket
 * boundaries are recorded in pool->bucket_offsets[RAY_BUCKET_COUNT+1].
 ******************************************************************************/

/* count_path_rays is now static INLINE in persistent_wavefront.h */

/**
 * fill_filter_per_ray — extract GPU filter data from path_state.
 * Called for each emitted ray alongside the s3d_ray_request fill.
 */
static INLINE void
fill_filter_per_ray(struct s3d_filter_per_ray* fp,
                    const struct path_state* p)
{
  if(!S3D_HIT_NONE(&p->filter_data_storage.hit_3d)) {
    fp->hit_from_prim_id = p->filter_data_storage.hit_3d.prim.prim_id;
    fp->hit_from_geom_id = p->filter_data_storage.hit_3d.prim.geom_id;
    fp->epsilon          = (float)p->filter_data_storage.epsilon;
    fp->enc_id           = (p->filter_data_storage.scn != NULL)
                             ? p->filter_data_storage.enc_id
                             : (uint32_t)0xFFFFFFFFu;
  } else {
    fp->hit_from_prim_id = (uint32_t)0xFFFFFFFFu;
    fp->hit_from_geom_id = (uint32_t)0xFFFFFFFFu;
    fp->enc_id           = (uint32_t)0xFFFFFFFFu;
    fp->epsilon          = 0.0f;
  }
}

LOCAL_SYM res_T
pool_collect_ray_requests_bucketed(struct wavefront_pool* pool,
                                    struct pool_view* pv)
{
  size_t b;
  int omp_nthreads;
  int use_omp = 1;

  /* Max OMP threads for stack-allocated per-thread arrays */
  #define COLLECT_MAX_THREADS 32

  /* Zero per-view pending stats (promoted to pool after trace completes) */
  pv->pending_rays_radiative           = 0;
  pv->pending_rays_conductive_ds       = 0;
  pv->pending_rays_conductive_ds_retry = 0;
  pv->pending_rays_shadow              = 0;
  pv->pending_rays_enclosure           = 0;
  pv->pending_rays_startup             = 0;
  pv->pending_rays_other               = 0;

  /* OMP toggle: STARDIS_COLLECT_OMP=0 disables */
  {
    const char* env = getenv("STARDIS_COLLECT_OMP");
    if(env && env[0] == '0') use_omp = 0;
  }
  omp_nthreads = (pool->scn && pool->scn->dev)
               ? (int)pool->scn->dev->nthreads : 1;
  if(omp_nthreads < 1) omp_nthreads = 1;
  if(omp_nthreads > COLLECT_MAX_THREADS) omp_nthreads = COLLECT_MAX_THREADS;
  if(pv->need_ray_count < 128) use_omp = 0;

  if(use_omp) {
    /* ════════════ OMP 3-pass bucketed collect (zero atomic) ════════════
     *
     * Pass 1: Per-thread bucket counting.  Each thread counts rays per
     *         bucket for its chunk.  Same schedule(static) as Pass 2 so
     *         thread t processes identical index range in both passes.
     *
     * Prefix sum: Compute global bucket_offsets, then per-thread per-bucket
     *             write offsets.  Thread t's rays for bucket b start at
     *             tl_write_base[t][b].  No overlap -> zero contention.
     *
     * Pass 2: Scatter.  Each thread writes rays using a local cursor
     *         initialised from tl_write_base.  No atomic, no sharing.
     * ================================================================== */

    /* Per-thread per-bucket ray counts (padded to avoid false sharing) */
    size_t tl_ray_counts[COLLECT_MAX_THREADS][RAY_BUCKET_COUNT];
    size_t tl_write_base[COLLECT_MAX_THREADS][RAY_BUCKET_COUNT];

    memset(tl_ray_counts, 0, sizeof(tl_ray_counts));

    /* ---- Pass 1: Per-thread bucket counting (P1: from SoA) ---- */
    #pragma omp parallel num_threads(omp_nthreads)
    {
      int tid = omp_get_thread_num();
      int kk;

      #pragma omp for schedule(static)
      for(kk = 0; kk < (int)pv->need_ray_count; kk++) {
        uint32_t i = pv->need_ray_indices[kk];
        struct path_state* p = &pool->slots[i];
        size_t nrays = count_path_rays_soa(
          (enum path_phase)pool->hot_arr[i].phase, pool->hot_arr[i].ray_count_ext, p);
        int bkt = (int)pool->hot_arr[i].ray_bucket;
        tl_ray_counts[tid][bkt] += nrays;
      }
    }

    /* ---- Aggregate bucket totals ---- */
    memset(pv->bucket_counts, 0, sizeof(pv->bucket_counts));
    {
      int t;
      for(t = 0; t < omp_nthreads; t++) {
        for(b = 0; b < RAY_BUCKET_COUNT; b++) {
          pv->bucket_counts[b] += tl_ray_counts[t][b];
        }
      }
    }

    /* ---- Bucket offsets (global prefix sum) ---- */
    pv->bucket_offsets[0] = 0;
    for(b = 0; b < RAY_BUCKET_COUNT; b++) {
      pv->bucket_offsets[b + 1] = pv->bucket_offsets[b]
                                 + pv->bucket_counts[b];
    }

    /* ---- Per-thread per-bucket write bases ---- */
    for(b = 0; b < RAY_BUCKET_COUNT; b++) {
      size_t running = pv->bucket_offsets[b];
      int t;
      for(t = 0; t < omp_nthreads; t++) {
        tl_write_base[t][b] = running;
        running += tl_ray_counts[t][b];
      }
    }

    /* ---- Pass 2: Scatter rays (no atomic, each thread owns its region) ---- */
    #pragma omp parallel num_threads(omp_nthreads)
    {
      int tid = omp_get_thread_num();
      int kk, bb;
      size_t my_cursor[RAY_BUCKET_COUNT];
      size_t tl_rays_radiative = 0;
      size_t tl_rays_cond_ds = 0;
      size_t tl_rays_cond_ds_retry = 0;
      size_t tl_rays_shadow = 0;
      size_t tl_rays_enclosure = 0;
      size_t tl_rays_startup = 0;
      size_t tl_rays_other = 0;

      for(bb = 0; bb < RAY_BUCKET_COUNT; bb++)
        my_cursor[bb] = tl_write_base[tid][bb];

      /* MUST use identical schedule(static) as Pass 1 */
      #pragma omp for schedule(static)
      for(kk = 0; kk < (int)pv->need_ray_count; kk++) {
        uint32_t i = pv->need_ray_indices[kk];
        struct path_state* p = &pool->slots[i];
        int bkt = (int)pool->hot_arr[i].ray_bucket;  /* P0_OPT: from hot_arr */
        enum path_phase ph_i = (enum path_phase)pool->hot_arr[i].phase; /* P0_OPT */

        /* Thread-local stats accumulation — semantic + per-phase */
        {
          size_t nrays = count_path_rays_soa(
            ph_i, pool->hot_arr[i].ray_count_ext, p);
          switch(ph_i) {
          case PATH_RAD_TRACE_PENDING:
          case PATH_BND_SF_NULLCOLL_RAD_TRACE:
          case PATH_BND_SFN_RAD_TRACE:
          case PATH_BND_EXT_DIFFUSE_TRACE:
          case PATH_CND_WOS_FALLBACK_TRACE:
          case PATH_COUPLED_BOUNDARY_REINJECT:
          case PATH_BND_SS_REINJECT_SAMPLE:
          case PATH_BND_SF_REINJECT_SAMPLE:
            tl_rays_radiative += nrays; break;
          case PATH_COUPLED_COND_DS_PENDING:
          case PATH_CND_DS_STEP_TRACE:
            if(p->locals.cnd_ds.robust_attempt > 0) tl_rays_cond_ds_retry += nrays;
            else                         tl_rays_cond_ds += nrays;
            break;
          case PATH_BND_EXT_DIRECT_TRACE:
          case PATH_BND_EXT_DIFFUSE_SHADOW_TRACE:
            tl_rays_shadow += nrays; break;
          case PATH_ENC_QUERY_EMIT:
          case PATH_ENC_QUERY_FB_EMIT:
          case PATH_CND_INIT_ENC:
            tl_rays_enclosure += nrays; break;
          case PATH_CNV_STARTUP_TRACE:
            tl_rays_startup += nrays; break;
          default:
            tl_rays_other += nrays; break;
          }
        }

        /* --- Emit ray 0 (Plan E: pinned direct-write) --- */
        {
          size_t ray_idx = my_cursor[bkt]++;
          struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
          rp->origin_x    = p->ray_req.origin[0];
          rp->origin_y    = p->ray_req.origin[1];
          rp->origin_z    = p->ray_req.origin[2];
          rp->direction_x = p->ray_req.direction[0];
          rp->direction_y = p->ray_req.direction[1];
          rp->direction_z = p->ray_req.direction[2];
          rp->tmin         = p->ray_req.range[0];
          rp->tmax         = p->ray_req.range[1];

          fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

          pv->ray_to_slot[ray_idx] = i;
          pv->ray_slot_sub[ray_idx] = 0;
          p->ray_req.batch_idx = (uint32_t)ray_idx;
        }

        /* --- Ray 1 (2-ray request, Plan E: pinned) --- */
        if(p->ray_req.ray_count >= 2) {
          size_t ray_idx = my_cursor[bkt]++;
          struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
          rp->origin_x    = p->ray_req.origin[0];
          rp->origin_y    = p->ray_req.origin[1];
          rp->origin_z    = p->ray_req.origin[2];
          rp->direction_x = p->ray_req.direction2[0];
          rp->direction_y = p->ray_req.direction2[1];
          rp->direction_z = p->ray_req.direction2[2];
          rp->tmin         = p->ray_req.range2[0];
          rp->tmax         = p->ray_req.range2[1];

          fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

          pv->ray_to_slot[ray_idx] = i;
          pv->ray_slot_sub[ray_idx] = 1;
          p->ray_req.batch_idx2 = (uint32_t)ray_idx;
        }

        /* --- Rays 2..5 for 6-ray enclosure query (P0_OPT: from hot_arr) --- */
        if(pool->hot_arr[i].ray_count_ext == 6
        && ph_i == PATH_ENC_QUERY_EMIT) {
          int j;
          pool->enc_arr[i].batch_indices[0] = p->ray_req.batch_idx;
          pool->enc_arr[i].batch_indices[1] = p->ray_req.batch_idx2;

          for(j = 2; j < 6; j++) {
            size_t ray_idx = my_cursor[bkt]++;
            struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
            rp->origin_x    = p->ray_req.origin[0];
            rp->origin_y    = p->ray_req.origin[1];
            rp->origin_z    = p->ray_req.origin[2];
            rp->direction_x = pool->enc_arr[i].directions[j][0];
            rp->direction_y = pool->enc_arr[i].directions[j][1];
            rp->direction_z = pool->enc_arr[i].directions[j][2];
            rp->tmin         = p->ray_req.range[0];
            rp->tmax         = p->ray_req.range[1];

            fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

            pv->ray_to_slot[ray_idx] = i;
            pv->ray_slot_sub[ray_idx] = (uint32_t)j;
            pool->enc_arr[i].batch_indices[j] = (uint32_t)ray_idx;
          }
        }

        /* --- B-4 M3: Rays 2..3 for SS 4-ray reinjection --- */
        /* BUG-FIX: previously missing — back direction rays were never
         * emitted in the persistent wavefront collect, causing
         * step_bnd_ss_reinject_process to read stale/zero hits for
         * ray_bck[0] and ray_bck[1]. */
        if(pool->hot_arr[i].ray_count_ext == 4
        && ph_i == PATH_BND_SS_REINJECT_SAMPLE) {
          int j;
          p->locals.bnd_ss.batch_idx_frt0 = p->ray_req.batch_idx;
          p->locals.bnd_ss.batch_idx_frt1 = p->ray_req.batch_idx2;

          for(j = 0; j < 2; j++) {
            size_t ray_idx = my_cursor[bkt]++;
            struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
            rp->origin_x    = p->ray_req.origin[0];
            rp->origin_y    = p->ray_req.origin[1];
            rp->origin_z    = p->ray_req.origin[2];
            rp->direction_x = p->locals.bnd_ss.dir_bck[j][0];
            rp->direction_y = p->locals.bnd_ss.dir_bck[j][1];
            rp->direction_z = p->locals.bnd_ss.dir_bck[j][2];
            rp->tmin         = p->ray_req.range[0];
            rp->tmax         = p->ray_req.range[1];

            fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

            pv->ray_to_slot[ray_idx] = i;
            pv->ray_slot_sub[ray_idx] = (uint32_t)(j + 2);
            if(j == 0) p->locals.bnd_ss.batch_idx_bck0 = (uint32_t)ray_idx;
            else       p->locals.bnd_ss.batch_idx_bck1 = (uint32_t)ray_idx;
          }
        }
      } /* end omp for */

      #pragma omp atomic
      pv->pending_rays_radiative           += tl_rays_radiative;
      #pragma omp atomic
      pv->pending_rays_conductive_ds       += tl_rays_cond_ds;
      #pragma omp atomic
      pv->pending_rays_conductive_ds_retry += tl_rays_cond_ds_retry;
      #pragma omp atomic
      pv->pending_rays_shadow              += tl_rays_shadow;
      #pragma omp atomic
      pv->pending_rays_enclosure           += tl_rays_enclosure;
      #pragma omp atomic
      pv->pending_rays_startup             += tl_rays_startup;
      #pragma omp atomic
      pv->pending_rays_other               += tl_rays_other;
    } /* end omp parallel */

    pv->ray_count = pv->bucket_offsets[RAY_BUCKET_COUNT];
    return RES_OK;

    #undef COLLECT_MAX_THREADS
  }

  /* ════════════ Serial fallback ════════════ */
  {
    size_t k;
    size_t ser_cursor[RAY_BUCKET_COUNT];

    memset(pv->bucket_counts, 0, sizeof(pv->bucket_counts));

    /* P0_OPT: Serial fallback Pass 1 — read bucket + ray_count from hot_arr */
    for(k = 0; k < pv->need_ray_count; k++) {
      uint32_t i = pv->need_ray_indices[k];
      struct path_state* p = &pool->slots[i];
      size_t nrays = count_path_rays_soa(
        (enum path_phase)pool->hot_arr[i].phase, pool->hot_arr[i].ray_count_ext, p);
      int bkt = (int)pool->hot_arr[i].ray_bucket;
      ASSERT(bkt >= 0 && bkt < RAY_BUCKET_COUNT);
      pv->bucket_counts[bkt] += nrays;
    }

    pv->bucket_offsets[0] = 0;
    for(b = 0; b < RAY_BUCKET_COUNT; b++) {
      pv->bucket_offsets[b + 1] = pv->bucket_offsets[b]
                                 + pv->bucket_counts[b];
    }
    for(b = 0; b < RAY_BUCKET_COUNT; b++) {
      ser_cursor[b] = pv->bucket_offsets[b];
    }

    for(k = 0; k < pv->need_ray_count; k++) {
      uint32_t i = pv->need_ray_indices[k];
      struct path_state* p = &pool->slots[i];
      int bkt = (int)pool->hot_arr[i].ray_bucket;   /* P0_OPT: from hot_arr */
      enum path_phase ph_i = (enum path_phase)pool->hot_arr[i].phase; /* P0_OPT */

      /* Semantic + per-phase stats (written to pv->pending_*,
       * promoted to pool after trace completes) */
      {
        size_t nrays = count_path_rays_soa(
          ph_i, pool->hot_arr[i].ray_count_ext, p);
        switch(ph_i) {
        case PATH_RAD_TRACE_PENDING:
        case PATH_BND_SF_NULLCOLL_RAD_TRACE:
        case PATH_BND_SFN_RAD_TRACE:
        case PATH_BND_EXT_DIFFUSE_TRACE:
        case PATH_CND_WOS_FALLBACK_TRACE:
        case PATH_COUPLED_BOUNDARY_REINJECT:
        case PATH_BND_SS_REINJECT_SAMPLE:
        case PATH_BND_SF_REINJECT_SAMPLE:
          pv->pending_rays_radiative += nrays; break;
        case PATH_COUPLED_COND_DS_PENDING:
        case PATH_CND_DS_STEP_TRACE:
          if(p->locals.cnd_ds.robust_attempt > 0) pv->pending_rays_conductive_ds_retry += nrays;
          else                         pv->pending_rays_conductive_ds += nrays;
          break;
        case PATH_BND_EXT_DIRECT_TRACE:
        case PATH_BND_EXT_DIFFUSE_SHADOW_TRACE:
          pv->pending_rays_shadow += nrays; break;
        case PATH_ENC_QUERY_EMIT:
        case PATH_ENC_QUERY_FB_EMIT:
        case PATH_CND_INIT_ENC:
          pv->pending_rays_enclosure += nrays; break;
        case PATH_CNV_STARTUP_TRACE:
          pv->pending_rays_startup += nrays; break;
        default:
          pv->pending_rays_other += nrays; break;
        }
      }

      /* Serial Ray 0 (Plan E: pinned direct-write) */
      {
        size_t ray_idx = ser_cursor[bkt]++;
        struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
        rp->origin_x    = p->ray_req.origin[0];
        rp->origin_y    = p->ray_req.origin[1];
        rp->origin_z    = p->ray_req.origin[2];
        rp->direction_x = p->ray_req.direction[0];
        rp->direction_y = p->ray_req.direction[1];
        rp->direction_z = p->ray_req.direction[2];
        rp->tmin         = p->ray_req.range[0];
        rp->tmax         = p->ray_req.range[1];

        fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

        pv->ray_to_slot[ray_idx] = i;
        pv->ray_slot_sub[ray_idx] = 0;
        p->ray_req.batch_idx = (uint32_t)ray_idx;
      }

      /* Serial Ray 1 (Plan E: pinned) */
      if(p->ray_req.ray_count >= 2) {
        size_t ray_idx = ser_cursor[bkt]++;
        struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
        rp->origin_x    = p->ray_req.origin[0];
        rp->origin_y    = p->ray_req.origin[1];
        rp->origin_z    = p->ray_req.origin[2];
        rp->direction_x = p->ray_req.direction2[0];
        rp->direction_y = p->ray_req.direction2[1];
        rp->direction_z = p->ray_req.direction2[2];
        rp->tmin         = p->ray_req.range2[0];
        rp->tmax         = p->ray_req.range2[1];

        fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

        pv->ray_to_slot[ray_idx] = i;
        pv->ray_slot_sub[ray_idx] = 1;
        p->ray_req.batch_idx2 = (uint32_t)ray_idx;
      }

      /* P0_OPT: read phase + ray_count_ext from hot_arr */
      if(pool->hot_arr[i].ray_count_ext == 6
      && ph_i == PATH_ENC_QUERY_EMIT) {
        int j;
        pool->enc_arr[i].batch_indices[0] = p->ray_req.batch_idx;
        pool->enc_arr[i].batch_indices[1] = p->ray_req.batch_idx2;

        for(j = 2; j < 6; j++) {
          size_t ray_idx = ser_cursor[bkt]++;
          struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
          rp->origin_x    = p->ray_req.origin[0];
          rp->origin_y    = p->ray_req.origin[1];
          rp->origin_z    = p->ray_req.origin[2];
          rp->direction_x = pool->enc_arr[i].directions[j][0];
          rp->direction_y = pool->enc_arr[i].directions[j][1];
          rp->direction_z = pool->enc_arr[i].directions[j][2];
          rp->tmin         = p->ray_req.range[0];
          rp->tmax         = p->ray_req.range[1];

          fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

          pv->ray_to_slot[ray_idx] = i;
          pv->ray_slot_sub[ray_idx] = (uint32_t)j;
          pool->enc_arr[i].batch_indices[j] = (uint32_t)ray_idx;
        }
      }

      /* B-4 M3: Rays 2..3 for SS 4-ray reinjection (serial path) */
      if(pool->hot_arr[i].ray_count_ext == 4
      && ph_i == PATH_BND_SS_REINJECT_SAMPLE) {
        int j;
        p->locals.bnd_ss.batch_idx_frt0 = p->ray_req.batch_idx;
        p->locals.bnd_ss.batch_idx_frt1 = p->ray_req.batch_idx2;

        for(j = 0; j < 2; j++) {
          size_t ray_idx = ser_cursor[bkt]++;
          struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
          rp->origin_x    = p->ray_req.origin[0];
          rp->origin_y    = p->ray_req.origin[1];
          rp->origin_z    = p->ray_req.origin[2];
          rp->direction_x = p->locals.bnd_ss.dir_bck[j][0];
          rp->direction_y = p->locals.bnd_ss.dir_bck[j][1];
          rp->direction_z = p->locals.bnd_ss.dir_bck[j][2];
          rp->tmin         = p->ray_req.range[0];
          rp->tmax         = p->ray_req.range[1];

          fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);

          pv->ray_to_slot[ray_idx] = i;
          pv->ray_slot_sub[ray_idx] = (uint32_t)(j + 2);
          if(j == 0) p->locals.bnd_ss.batch_idx_bck0 = (uint32_t)ray_idx;
          else       p->locals.bnd_ss.batch_idx_bck1 = (uint32_t)ray_idx;
        }
      }
    }

    pv->ray_count = pv->bucket_offsets[RAY_BUCKET_COUNT];
    return RES_OK;
  }
}


/*******************************************************************************
 * B-4 M10: Distribute enc_locate results back to paths
 *
 * For each enc_locate result, write prim_id/side/distance into the path's
 * enc_locate struct, then transition to PATH_ENC_LOCATE_RESULT so the
 * cascade loop can resolve enc_id via step_enc_locate_result.
 ******************************************************************************/
static res_T
pool_distribute_enc_locate_results(struct wavefront_pool* pool,
                                    struct pool_view* pv)
{
  size_t k;

  for(k = 0; k < pv->enc_locate_count; k++) {
    uint32_t slot_id = pv->enc_locate_to_slot[k];
    const struct s3d_enc_locate_result* r = &pv->enc_locate_results[k];

    /* O11_SAFETY: write-before-read — data must be written before phase → RESULT */
    pool->enc_arr[slot_id].locate.prim_id  = r->prim_id;
    pool->enc_arr[slot_id].locate.side     = r->side;
    pool->enc_arr[slot_id].locate.distance = r->distance;
    pool->hot_arr[slot_id].phase = (uint8_t)PATH_ENC_LOCATE_RESULT;
  }

  return RES_OK;
}


/*******************************************************************************
 * B-4 M9: Distribute closest_point results back to WoS paths
 *
 * For each CP result, write the hit into the path's cached_hit field,
 * then transition to the matching *_RESULT phase so the cascade loop
 * can process the result.
 *
 * BUG-FIX: previously always set PATH_CND_WOS_CLOSEST_RESULT regardless
 * of the origin phase.  Now correctly maps:
 *   PATH_CND_WOS_CLOSEST          → PATH_CND_WOS_CLOSEST_RESULT
 *   PATH_CND_WOS_DIFFUSION_CHECK  → PATH_CND_WOS_DIFFUSION_CHECK_RESULT
 ******************************************************************************/
LOCAL_SYM res_T
pool_distribute_cp_results(struct wavefront_pool* pool,
                            struct pool_view* pv)
{
  size_t k;

  for(k = 0; k < pv->cp_count; k++) {
    uint32_t slot_id = pv->cp_to_slot[k];
    struct path_state* p = &pool->slots[slot_id];
    enum path_phase cp_ph = (enum path_phase)pool->hot_arr[slot_id].phase;

    /* O11_SAFETY: write-before-read — data must be written before phase → RESULT */
    p->locals.cnd_wos.cached_hit = pv->cp_hits[k];

    if(cp_ph == PATH_CND_WOS_DIFFUSION_CHECK)
      pool->hot_arr[slot_id].phase = (uint8_t)PATH_CND_WOS_DIFFUSION_CHECK_RESULT;
    else
      pool->hot_arr[slot_id].phase = (uint8_t)PATH_CND_WOS_CLOSEST_RESULT;
  }

  return RES_OK;
}


/*******************************************************************************
 * Cascade non-ray steps -- serial inner kernel
 *
 * Advances a single path through non-ray phases until a ray is needed,
 * the path completes, or no progress can be made.  Accumulates statistics
 * into caller-provided local counters (thread-safe for OMP).
 *
 * Returns: 0 = ok, 1 = fatal error (propagate to caller).
 ******************************************************************************/
static int
cascade_advance_single_path(
  struct path_state* p,
  struct sdis_scene* scn,
  struct wavefront_pool* pool, size_t slot_idx,
  /* --- thread-local accumulators --- */
  size_t* local_iterations,
  size_t* local_advances,
  size_t* local_paths_failed,
  size_t* local_enc_degenerate_null
#ifdef SDIS_CASCADE_PROFILE
  ,size_t  local_phase_count[]
  ,double  local_phase_time[]
#endif
)
{
  struct path_hot* hot = &pool->hot_arr[slot_idx];
  for(;;) {
    int advanced = 0;
    enum path_phase phase_before;
#ifdef SDIS_CASCADE_PROFILE
    struct time t_step0, t_step1;
#endif
    res_T res;

    if(hot->needs_ray) break;
    if((enum path_phase)hot->phase == PATH_DONE
    || (enum path_phase)hot->phase == PATH_ERROR) {
      /* M8: intercept PATH_DONE when sfn_stack_depth > 0. */
      if((enum path_phase)hot->phase == PATH_DONE
      && pool->sfn_arr[slot_idx].depth > 0) {
        hot->phase = (uint8_t)PATH_BND_SFN_COMPUTE_Ti_RESUME;
        hot->active = 1;
        continue;
      }
      break;
    }
    if(path_phase_is_ray_pending((enum path_phase)hot->phase)) break;
    if(path_phase_is_enc_locate_pending((enum path_phase)hot->phase)) break;
    if(path_phase_is_cp_pending((enum path_phase)hot->phase)) break;

    (*local_iterations)++;
    phase_before = (enum path_phase)hot->phase;
#ifdef SDIS_CASCADE_PROFILE
    time_current(&t_step0);
#endif

    res = advance_one_step_no_ray(p, hot, scn, &advanced, pool, slot_idx);

    /* O11_DIAG: log after cascade step */
    pt_log("B", pool->total_steps, (uint32_t)slot_idx, p, hot, pool,
           (int)phase_before, (int)res);

#ifdef SDIS_CASCADE_PROFILE
    time_current(&t_step1);
    if((int)phase_before >= 0 && (int)phase_before < PATH_PHASE_COUNT) {
      local_phase_count[(int)phase_before]++;
      local_phase_time[(int)phase_before]
        += time_elapsed_sec(&t_step0, &t_step1);
    }
#endif
    if(phase_before == PATH_ENC_LOCATE_RESULT
    && pool->enc_arr[slot_idx].locate.prim_id >= 0 && pool->enc_arr[slot_idx].locate.side < 0) {
      (*local_enc_degenerate_null)++;
    }

    if(res != RES_OK && res != RES_BAD_OP
    && res != RES_BAD_OP_IRRECOVERABLE) return 1; /* fatal */
    if(res == RES_BAD_OP || res == RES_BAD_OP_IRRECOVERABLE) {
      hot->phase = (uint8_t)PATH_DONE;
      hot->active = 0;
      p->done_reason = -1;
      (*local_paths_failed)++;
      break;
    }
    if(!advanced) break;
    (*local_advances)++;
    p->steps_taken++;
  }

  return 0; /* ok */
}

/*******************************************************************************
 * Cascade non-ray steps -- compact version (M2.5)
 *
 * For each active path, advance through non-ray phases until a ray is
 * needed, the path completes, or no progress can be made.
 *
 * OMP parallelization:
 *   - Controlled by STARDIS_CASCADE_OMP env var (default=1=on).
 *   - Thread count from scn->dev->nthreads (matches CPU version's -t flag).
 *   - schedule(dynamic, 64) for load balancing (variable cascade depth).
 *   - Per-thread local accumulators, reduced after the parallel region.
 ******************************************************************************/
static res_T
pool_cascade_non_ray_steps_compact(struct wavefront_pool* pool,
                                   struct pool_view* pv,
                                   struct sdis_scene* scn)
{
  const size_t n = pv->active_compact;
  int use_omp = 1;
  int omp_nthreads;
  int had_fatal = 0;

  /* Check env var for cascade OMP toggle (default = on) */
  {
    const char* env = getenv("STARDIS_CASCADE_OMP");
    if(env && env[0] == '0') use_omp = 0;
  }

  /* Determine thread count: use scn->dev->nthreads, same as CPU -t flag */
  omp_nthreads = (scn && scn->dev) ? (int)scn->dev->nthreads : 1;
  if(omp_nthreads < 1) omp_nthreads = 1;

  /* For very small wavefronts, serial is faster than OMP overhead */
  if(n < 64) use_omp = 0;

  if(use_omp) {
    /* === OMP parallel cascade === */
    #pragma omp parallel num_threads(omp_nthreads)
    {
      /* Per-thread local accumulators */
      size_t tl_iterations = 0;
      size_t tl_advances = 0;
      size_t tl_paths_failed = 0;
      size_t tl_enc_degenerate_null = 0;
#ifdef SDIS_CASCADE_PROFILE
      size_t tl_phase_count[PATH_PHASE_COUNT];
      double tl_phase_time[PATH_PHASE_COUNT];
#endif
      int ph;
#ifdef SDIS_CASCADE_PROFILE
      for(ph = 0; ph < PATH_PHASE_COUNT; ph++) {
        tl_phase_count[ph] = 0;
        tl_phase_time[ph] = 0.0;
      }
#endif

      #pragma omp for schedule(dynamic, 64)
      for(ph = 0; ph < (int)n; ph++) {
        uint32_t idx = pv->active_indices[ph];
        struct path_state* p = &pool->slots[idx];
        if(!pool->hot_arr[idx].active) continue;

        if(cascade_advance_single_path(
              p, scn, pool, (size_t)idx,
              &tl_iterations, &tl_advances,
              &tl_paths_failed, &tl_enc_degenerate_null
#ifdef SDIS_CASCADE_PROFILE
              ,tl_phase_count, tl_phase_time
#endif
              )) {
          /* Fatal error — signal, but can't break from OMP for.
           * Other threads will finish their current work items.
           * Plain write is safe: x86 aligned-int stores are atomic,
           * and we only ever set to 1 (monotonic flag). */
          had_fatal = 1;
        }
      }

      /* Reduction: merge thread-local counters into pool */
#ifdef SDIS_CASCADE_PROFILE
      #pragma omp critical
      {
        int j;
        pool->cascade_total_iterations += tl_iterations;
        pool->cascade_total_advances   += tl_advances;
        pool->paths_failed             += tl_paths_failed;
        pool->enc_locate_degenerate_null += tl_enc_degenerate_null;
        for(j = 0; j < PATH_PHASE_COUNT; j++) {
          pool->cascade_phase_count[j] += tl_phase_count[j];
          pool->cascade_phase_time[j]  += tl_phase_time[j];
        }
      }
#else
      #pragma omp atomic
      pool->cascade_total_iterations += tl_iterations;
      #pragma omp atomic
      pool->cascade_total_advances   += tl_advances;
      #pragma omp atomic
      pool->paths_failed             += tl_paths_failed;
      #pragma omp atomic
      pool->enc_locate_degenerate_null += tl_enc_degenerate_null;
#endif
    } /* end omp parallel */

    return had_fatal ? RES_UNKNOWN_ERR : RES_OK;
  }

  /* === Serial fallback (STARDIS_CASCADE_OMP=0 or small wavefront) === */
  {
    size_t k;
    size_t tl_iterations = 0;
    size_t tl_advances = 0;
    size_t tl_paths_failed = 0;
    size_t tl_enc_degenerate_null = 0;
#ifdef SDIS_CASCADE_PROFILE
    size_t tl_phase_count[PATH_PHASE_COUNT];
    double tl_phase_time[PATH_PHASE_COUNT];
    int ph;
    for(ph = 0; ph < PATH_PHASE_COUNT; ph++) {
      tl_phase_count[ph] = 0;
      tl_phase_time[ph] = 0.0;
    }
#endif

    for(k = 0; k < n; k++) {
      uint32_t idx = pv->active_indices[k];
      struct path_state* p = &pool->slots[idx];
      if(!pool->hot_arr[idx].active) continue;

      if(cascade_advance_single_path(
            p, scn, pool, (size_t)idx,
            &tl_iterations, &tl_advances,
            &tl_paths_failed, &tl_enc_degenerate_null
#ifdef SDIS_CASCADE_PROFILE
            ,tl_phase_count, tl_phase_time
#endif
            )) {
        return RES_UNKNOWN_ERR;
      }
    }

    pool->cascade_total_iterations += tl_iterations;
    pool->cascade_total_advances   += tl_advances;
    pool->paths_failed             += tl_paths_failed;
    pool->enc_locate_degenerate_null += tl_enc_degenerate_null;
#ifdef SDIS_CASCADE_PROFILE
    {
      int j;
      for(j = 0; j < PATH_PHASE_COUNT; j++) {
        pool->cascade_phase_count[j] += tl_phase_count[j];
        pool->cascade_phase_time[j]  += tl_phase_time[j];
      }
    }
#endif
  }

  return RES_OK;
}


/*******************************************************************************
 * Refill pool -- replace completed slots with new tasks from the queue (M2)
 *
 * O5: OMP-parallel refill.  Pre-scans done_indices to build a compact list
 * of refillable slots, pre-allocates path_id + task ranges, then dispatches
 * init_path + advance_path_to_first_ray in parallel.
 *
 * Scans done_indices for PATH_DONE, inactive slots and fills them with new
 * tasks.  Returns refill count.
 *
 * NOTE: Phase 1 compacts refillable slot indices into the *beginning* of
 * pv->done_indices[] in-place.  This is safe because refill_n <= k always
 * holds (we skip some entries), and harvest has already consumed done_indices.
 ******************************************************************************/
static res_T
refill_pool(struct wavefront_pool* pool, struct pool_view* pv,
            size_t* out_refill_count)
{
  size_t k;
  size_t refill_n = 0;
  size_t refill_avail;
  uint32_t base_path_id;
  size_t base_task;
  int omp_nthreads;
  int use_omp = 1;
  int had_error = 0;

  if(pool->task_next >= pool->task_count) {
    *out_refill_count = 0;
    return RES_OK;
  }

  /* Phase 1 (serial): scan done_indices → compact refill list */
  refill_avail = pool->task_count - pool->task_next;
  for(k = 0; k < pv->done_count && refill_n < refill_avail; k++) {
    uint32_t i = pv->done_indices[k];
    struct path_state* p = &pool->slots[i];

    /* P0_OPT: read dispatch fields from hot_arr (source of truth) */
    if(pool->hot_arr[i].active) continue;
    { enum path_phase rph = (enum path_phase)pool->hot_arr[i].phase;
      if(rph != PATH_DONE && rph != PATH_ERROR
      && rph != PATH_HARVESTED) continue;
    }

    pv->done_indices[refill_n++] = i;  /* compact in-place; refill_n <= k */
  }

  if(refill_n == 0) {
    *out_refill_count = 0;
    return RES_OK;
  }

  /* Phase 2 (serial): pre-allocate path_id + task ranges */
  base_path_id = pool->next_path_id;
  base_task    = pool->task_next;
  pool->next_path_id += (uint32_t)refill_n;
  pool->task_next    += refill_n;

  /* Phase 3: parallel init_path + advance */
  omp_nthreads = (pool->scn && pool->scn->dev)
               ? (int)pool->scn->dev->nthreads : 1;
  if(omp_nthreads < 1) omp_nthreads = 1;
  if(refill_n < 128) use_omp = 0;

  if(use_omp) {
    #pragma omp parallel num_threads(omp_nthreads)
    {
      int kk;
      size_t tl_paths_failed = 0;

      #pragma omp for schedule(static)
      for(kk = 0; kk < (int)refill_n; kk++) {
        uint32_t i = pv->done_indices[kk];
        struct path_state* p = &pool->slots[i];
        const struct pixel_task* task = &pool->task_queue[base_task + kk];
        res_T lr;

        lr = pool->ops->init_path(
          p, &pool->hot_arr[i], pool->slot_rngs[i], task,
          pool->scn, pool->enc_id, pool->mode_ctx,
          pool->time_range,
          pool->picard_order, pool->diff_algo,
          base_path_id + (uint32_t)kk, pool->global_seed);
        if(lr != RES_OK) { had_error = 1; continue; }

        lr = advance_path_to_first_ray(p, pool->scn, pool, i);
        if(lr != RES_OK) { had_error = 1; continue; }
        /* P0_OPT: hot_arr[i] already written by advance_path_to_first_ray */
      }

      if(tl_paths_failed > 0) {
        #pragma omp atomic
        pool->paths_failed += tl_paths_failed;
      }
    } /* end omp parallel */
  } else {
    /* Serial fallback */
    for(k = 0; k < refill_n; k++) {
      uint32_t i = pv->done_indices[k];
      struct path_state* p = &pool->slots[i];
      const struct pixel_task* task = &pool->task_queue[base_task + k];
      res_T lr;

      lr = pool->ops->init_path(
        p, &pool->hot_arr[i], pool->slot_rngs[i], task,
        pool->scn, pool->enc_id, pool->mode_ctx,
        pool->time_range,
        pool->picard_order, pool->diff_algo,
        base_path_id + (uint32_t)k, pool->global_seed);
      if(lr != RES_OK) return lr;

      lr = advance_path_to_first_ray(p, pool->scn, pool, i);
      if(lr != RES_OK) return lr;
      /* P0_OPT: hot_arr[i] already written by advance_path_to_first_ray */
    }
  }

  pool->diag_refill_count += refill_n;
  *out_refill_count = refill_n;
  return had_error ? RES_UNKNOWN_ERR : RES_OK;
}

/*******************************************************************************
 * Update active count
 ******************************************************************************/
static void
pool_update_active_count(struct wavefront_pool* pool)
{
  if(pool->num_active_views == 1) {
    pool->active_count = pool->views[0].active_compact;
  } else {
    pool->active_count = pool->views[0].active_compact
                       + pool->views[1].active_compact;
  }
}

/*******************************************************************************
 * Update diagnostics -- track batch size statistics + wavefront width (M3)
 ******************************************************************************/
static void
pool_update_diagnostics(struct wavefront_pool* pool, struct pool_view* pv)
{
  if(pv->ray_count > 0) {
    if(pv->ray_count < pool->diag_min_batch)
      pool->diag_min_batch = pv->ray_count;
    if(pv->ray_count > pool->diag_max_batch)
      pool->diag_max_batch = pv->ray_count;
  }

  if(pool->in_drain_phase) {
    pool->diag_drain_rays += pv->ray_count;
    pool->drain_step_count++;
  } else {
    pool->diag_refill_rays += pv->ray_count;
  }

  /* M3: accumulate active_count for average wavefront width */
  pool->diag_total_active += pool->active_count;
}

/*******************************************************************************
 * Log drain phase report — enhanced with M3 per-phase timing + wavefront width
 ******************************************************************************/
static void
log_drain_phase_report(struct sdis_device* dev, struct wavefront_pool* pool)
{
  double drain_ray_pct = 0;
  double avg_width = 0;
  double trace_gpu_s, trace_cpu_s;
  double total_timed, wall_s, coverage;
  if(pool->total_rays_traced > 0) {
    drain_ray_pct = (double)pool->diag_drain_rays * 100.0
                  / (double)pool->total_rays_traced;
  }
  if(pool->total_steps > 0) {
    avg_width = (double)pool->diag_total_active / (double)pool->total_steps;
  }

  /* Derive trace GPU vs CPU split from batch trace stats (ms → s) */
  trace_gpu_s = (pool->trace_kernel_time_ms_sum
               + pool->trace_batch_time_ms_sum) * 1e-3;
  trace_cpu_s = (pool->trace_post_time_ms_sum
               + pool->trace_retrace_time_ms_sum) * 1e-3;

  /* Total timed = sum of all instrumented phase timers */
  total_timed = pool->time_compact_s
              + pool->time_collect_s
              + pool->time_trace_s
              + pool->time_distribute_s
              + pool->time_enc_locate_s
              + pool->time_cp_s
              + pool->time_cascade_s
              + pool->time_harvest_s
              + pool->time_housekeeping_s
              + pool->time_gpu_sync_s
              + pool->time_gpu_launch_s;

  /* Wall = refill + drain phase durations */
  wall_s = pool->time_refill_phase_s + pool->time_drain_phase_s;
  if(wall_s <= 0) wall_s = total_timed;  /* fallback if phases not recorded */
  coverage = (wall_s > 0) ? total_timed / wall_s * 100.0 : 0.0;

  log_info(dev,
    "persistent wavefront summary:\n"
    "  total_steps=%llu  total_rays=%llu  avg_wavefront_width=%.1f\n"
    "  refill_phase: rays=%llu (%.1f%%)  wall=%.3fs\n"
    "  drain_phase:  rays=%llu (%.1f%%)  steps=%llu  wall=%.3fs\n"
    "  batch_size: min=%llu, max=%llu\n"
    "  rays: radiative=%llu  cond_ds=%llu(retry=%llu)  "
    "shadow=%llu  enclosure=%llu  startup=%llu  other=%llu\n"
    "  paths: completed=%llu, failed=%llu, truncated=%llu, max_depth=%llu\n"
    "  refills=%llu\n"
    "  timing: compact=%.3fs  collect=%.3fs  trace=%.3fs(gpu=%.3fs cpu=%.3fs)\n"
    "          distribute=%.3fs  enc_locate=%.3fs  cp=%.3fs\n"
    "          cascade=%.3fs\n"
    "          harvest+refill=%.3fs  housekeeping=%.3fs\n"
    "          gpu_sync=%.3fs  gpu_launch=%.3fs\n"
    "          total_timed=%.3fs  wall=%.3fs  coverage=%.1f%%\n",
    (unsigned long long)pool->total_steps,
    (unsigned long long)pool->total_rays_traced,
    avg_width,
    (unsigned long long)pool->diag_refill_rays,
    100.0 - drain_ray_pct,
    pool->time_refill_phase_s,
    (unsigned long long)pool->diag_drain_rays,
    drain_ray_pct,
    (unsigned long long)pool->drain_step_count,
    pool->time_drain_phase_s,
    (unsigned long long)(pool->diag_min_batch == (size_t)-1
                    ? 0 : pool->diag_min_batch),
    (unsigned long long)pool->diag_max_batch,
    (unsigned long long)pool->rays_radiative,
    (unsigned long long)pool->rays_conductive_ds,
    (unsigned long long)pool->rays_conductive_ds_retry,
    (unsigned long long)pool->rays_shadow,
    (unsigned long long)pool->rays_enclosure,
    (unsigned long long)pool->rays_startup,
    (unsigned long long)pool->rays_other,
    (unsigned long long)pool->paths_completed,
    (unsigned long long)pool->paths_failed,
    (unsigned long long)pool->paths_truncated,
    (unsigned long long)pool->max_path_depth,
    (unsigned long long)pool->diag_refill_count,
    pool->time_compact_s,
    pool->time_collect_s,
    pool->time_trace_s, trace_gpu_s, trace_cpu_s,
    pool->time_distribute_s,
    pool->time_enc_locate_s,
    pool->time_cp_s,
    pool->time_cascade_s,
    pool->time_harvest_s,
    pool->time_housekeeping_s,
    pool->time_gpu_sync_s,
    pool->time_gpu_launch_s,
    total_timed, wall_s, coverage);

  /* Verify ray stats sum == total_rays_traced */
  {
    size_t sum = pool->rays_radiative
               + pool->rays_conductive_ds
               + pool->rays_conductive_ds_retry
               + pool->rays_shadow
               + pool->rays_enclosure
               + pool->rays_startup
               + pool->rays_other;
    if(sum != pool->total_rays_traced) {
      log_info(dev,
        "  WARNING: ray stats sum=%llu != total_rays_traced=%llu "
        "(delta=%lld)\n",
        (unsigned long long)sum,
        (unsigned long long)pool->total_rays_traced,
        (long long)((long long)sum - (long long)pool->total_rays_traced));
    }
    ASSERT(sum == pool->total_rays_traced);
  }

  /* Enclosure query escalation stats */
  if(pool->enc_query_escalated_to_m10 > 0
  || pool->enc_locate_degenerate_null > 0) {
    log_info(dev,
      "  enc_escalation: query_fb->m10=%llu  m10_degenerate_null=%llu\n",
      (unsigned long long)pool->enc_query_escalated_to_m10,
      (unsigned long long)pool->enc_locate_degenerate_null);
  }

  /* --- Experiment 3: Cascade summary + per-phase hotspots --- */
  if(pool->cascade_total_advances > 0) {
    log_info(dev,
      "cascade profiling: total_iterations=%llu  total_advances=%llu\n",
      (unsigned long long)pool->cascade_total_iterations,
      (unsigned long long)pool->cascade_total_advances);

#ifdef SDIS_CASCADE_PROFILE
    {
      /* Per-phase top-N hotspots (requires SDIS_CASCADE_PROFILE build) */
      int top[10];
      int ti, tj;
      int used[PATH_PHASE_COUNT];
      memset(used, 0, sizeof(used));

      for(ti = 0; ti < 10; ti++) {
        int best = -1;
        double best_t = 0;
        for(tj = 0; tj < PATH_PHASE_COUNT; tj++) {
          if(used[tj]) continue;
          if(pool->cascade_phase_time[tj] > best_t) {
            best_t = pool->cascade_phase_time[tj];
            best = tj;
          }
        }
        top[ti] = best;
        if(best >= 0) used[best] = 1;
      }

      for(ti = 0; ti < 10; ti++) {
        int ph = top[ti];
        if(ph < 0 || pool->cascade_phase_count[ph] == 0) break;
        log_info(dev,
          "  cascade phase[%2d]: count=%10llu  time=%8.3fs  avg=%.3fus  "
          "(%.1f%% of cascade)\n",
          ph,
          (unsigned long long)pool->cascade_phase_count[ph],
          pool->cascade_phase_time[ph],
          pool->cascade_phase_time[ph] * 1e6
            / (double)pool->cascade_phase_count[ph],
          pool->cascade_phase_time[ph] * 100.0 / pool->time_cascade_s);
      }
    }
#endif
  }

  /* --- Experiment 7+8: Batch trace per-call summary --- */
  if(pool->trace_call_count > 0) {
    double avg_batch = (double)pool->trace_batch_size_sum
                     / (double)pool->trace_call_count;
    double avg_kernel_ms = pool->trace_kernel_time_ms_sum
                         / (double)pool->trace_call_count;
    double avg_d2h_ms = pool->trace_batch_time_ms_sum
                      / (double)pool->trace_call_count;
    double avg_post_ms  = pool->trace_post_time_ms_sum
                        / (double)pool->trace_call_count;
    double total_trace_ms = pool->trace_kernel_time_ms_sum
                          + pool->trace_batch_time_ms_sum
                          + pool->trace_post_time_ms_sum
                          + pool->trace_retrace_time_ms_sum;
    double kern_pct = pool->trace_kernel_time_ms_sum * 100.0 / total_trace_ms;
    double d2h_pct  = pool->trace_batch_time_ms_sum * 100.0 / total_trace_ms;
    double post_pct = pool->trace_post_time_ms_sum * 100.0 / total_trace_ms;
    double rtrc_pct = pool->trace_retrace_time_ms_sum * 100.0 / total_trace_ms;
    double tp_kernel = (pool->trace_kernel_time_ms_sum > 0.0)
                     ? (double)pool->total_rays_traced
                       / (pool->trace_kernel_time_ms_sum * 1e-3) / 1e6
                     : 0.0;

    log_info(dev,
      "batch trace profiling: calls=%llu  avg_batch=%.0f  "
      "min=%llu  max=%llu\n"
      "  gpu_kernel:        total=%.1fms (%.1f%%)  avg=%.2fms/call\n"
      "  d2h_wait:          total=%.1fms (%.1f%%)  avg=%.2fms/call\n"
      "  cpu_postprocess:   total=%.1fms (%.1f%%)  avg=%.2fms/call\n"
      "  fallback_retrace:  total=%.1fms (%.1f%%)  "
      "accepted=%llu  missed=%llu  rejected=%llu\n"
      "  gpu_throughput: %.1f Mrays/s  (kernel only)\n",
      (unsigned long long)pool->trace_call_count,
      avg_batch,
      (unsigned long long)(pool->trace_batch_size_min == (size_t)-1
                      ? 0 : pool->trace_batch_size_min),
      (unsigned long long)pool->trace_batch_size_max,
      pool->trace_kernel_time_ms_sum, kern_pct, avg_kernel_ms,
      pool->trace_batch_time_ms_sum, d2h_pct, avg_d2h_ms,
      pool->trace_post_time_ms_sum, post_pct, avg_post_ms,
      pool->trace_retrace_time_ms_sum, rtrc_pct,
      (unsigned long long)pool->trace_retrace_accepted_sum,
      (unsigned long long)pool->trace_retrace_missed_sum,
      (unsigned long long)pool->trace_filter_rejected_sum,
      tp_kernel);
  }
}


/*******************************************************************************
 * L3: Fine-grained pipeline helper functions (dual-stream)
 *
 * Split the old gpu_wait_download into three steps:
 *   gpu_sync_kernel  — wait for GPU kernel to complete (no D2H)
 *   gpu_start_d2h    — launch async D2H download (returns immediately)
 *   gpu_wait_d2h     — wait for D2H, then CPU filter/retrace + stats
 *
 * This allows overlapping D2H(A) with H2D(B)+kernel(B) on the bus.
 ******************************************************************************/

/**
 * gpu_sync_kernel — wait for GPU compute kernel to finish (no data transfer).
 * Measures wall-clock wait time and accumulates into pool->trace_kernel_time_ms_sum.
 */
static res_T
gpu_sync_kernel(struct wavefront_pool* pool, struct pool_view* pv)
{
  struct time k0, k1;
  res_T rc;
  if(pv->ray_count == 0 || !pv->gpu_pending) return RES_OK;
  time_current(&k0);
  if(pool->use_gpu_filter)
    rc = s3d_scene_view_trace_rays_batch_ctx_filtered_sync_kernel(
      pv->batch_ctx);
  else
    rc = s3d_scene_view_trace_rays_batch_ctx_sync_kernel(pv->batch_ctx);
  time_current(&k1);
  /* Use CUDA event elapsed time for accurate GPU kernel measurement */
  pool->trace_kernel_time_ms_sum +=
    (double)s3d_batch_trace_context_get_last_kernel_ms(pv->batch_ctx);
  return rc;
}

/**
 * gpu_start_d2h — launch async D2H download on transfer_stream.
 * Returns immediately.  Caller must call gpu_wait_d2h() before reading.
 */
static res_T
gpu_start_d2h(struct wavefront_pool* pool, struct pool_view* pv)
{
  if(pv->ray_count == 0 || !pv->gpu_pending) return RES_OK;
  if(pool->use_gpu_filter)
    return s3d_scene_view_trace_rays_batch_ctx_filtered_start_d2h(
      pv->batch_ctx, pv->ray_count);
  return s3d_scene_view_trace_rays_batch_ctx_start_d2h(
    pv->batch_ctx, pv->ray_count);
}

/**
 * gpu_wait_d2h — wait for D2H transfer, CPU filter/retrace, accumulate stats.
 * Replaces gpu_wait_download() in the L3 pipeline.
 * L4: when use_gpu_filter, calls the filtered wait (no retrace).
 */
static res_T
gpu_wait_d2h(struct wavefront_pool* pool,
             struct pool_view* pv,
             struct s3d_scene_view* sv)
{
  struct time t0, t1;
  res_T res;

  time_current(&t0);
  if(pv->ray_count > 0 && pv->gpu_pending) {
    struct s3d_batch_trace_stats stats;
    memset(&stats, 0, sizeof(stats));

    if(pool->use_gpu_filter) {
      res = s3d_scene_view_trace_rays_batch_ctx_filtered_wait_d2h(
        sv, pv->batch_ctx,
        pv->ray_requests, pv->ray_count,
        pv->ray_hits, &stats);
    } else {
      res = s3d_scene_view_trace_rays_batch_ctx_wait_d2h(
        sv, pv->batch_ctx,
        pv->ray_requests, pv->ray_count,
        pv->ray_hits, &stats);
    }
    if(res != RES_OK) return res;
    pv->gpu_pending = 0;

    pool->total_rays_traced += pv->ray_count;

    /* Promote per-view pending ray stats to pool-level counters */
    {
      pool->rays_radiative           += pv->pending_rays_radiative;
      pool->rays_conductive_ds       += pv->pending_rays_conductive_ds;
      pool->rays_conductive_ds_retry += pv->pending_rays_conductive_ds_retry;
      pool->rays_shadow              += pv->pending_rays_shadow;
      pool->rays_enclosure           += pv->pending_rays_enclosure;
      pool->rays_startup             += pv->pending_rays_startup;
      pool->rays_other               += pv->pending_rays_other;
    }

    pool->trace_call_count++;
    pool->trace_batch_size_sum   += pv->ray_count;
    pool->trace_batch_time_ms_sum  += stats.batch_time_ms;
    pool->trace_post_time_ms_sum   += stats.postprocess_time_ms;
    pool->trace_retrace_time_ms_sum += stats.retrace_time_ms;
    pool->trace_retrace_accepted_sum += stats.retrace_accepted;
    pool->trace_retrace_missed_sum   += stats.retrace_missed;
    pool->trace_filter_rejected_sum  += stats.filter_rejected;

    if(pv->ray_count < pool->trace_batch_size_min)
      pool->trace_batch_size_min = pv->ray_count;
    if(pv->ray_count > pool->trace_batch_size_max)
      pool->trace_batch_size_max = pv->ray_count;
  }
  time_current(&t1);
  pool->time_trace_s += time_elapsed_sec(&t0, &t1);

  return RES_OK;
}

/*******************************************************************************
 * O11: Unified async dispatch — RT + enc_locate + closest_point
 *
 * gpu_launch_all   — launch RT + enc + cp async (replaces gpu_launch_async
 *                    for views that have pending enc/cp requests)
 * gpu_sync_kernel_all — sync RT + enc + cp compute kernels
 * gpu_start_d2h_all   — start D2H for RT + enc + cp
 * gpu_wait_d2h_all    — wait D2H + post-process + distribute enc/cp results
 *
 * This eliminates the synchronous post_merged_enc_cp() from the CPU phase,
 * hiding enc/cp GPU latency behind the dual-buffer pipeline.
 ******************************************************************************/

/**
 * gpu_launch_all — launch RT + enc_locate + closest_point async.
 * Replaces gpu_launch_async() and absorbs the GPU launch part of
 * post_merged_enc_cp().
 */
static res_T
gpu_launch_all(struct wavefront_pool* pool, struct pool_view* pv,
               struct s3d_scene_view* sv)
{
  res_T res;

  /* RT launch (existing) */
  if(pv->ray_count > 0) {
    if(pool->use_gpu_filter) {
      res = s3d_scene_view_trace_rays_batch_ctx_filtered_pinned_async(
        sv, pv->batch_ctx, pv->ray_count);
    } else {
      res = s3d_scene_view_trace_rays_batch_ctx_async(
        sv, pv->batch_ctx, pv->ray_requests, pv->ray_count);
    }
    if(res != RES_OK) return res;
    pv->gpu_pending = 1;
  } else {
    pv->gpu_pending = 0;
  }

  /* enc_locate launch (new async) */
  if(pv->enc_locate_count > 0) {
    res = s3d_scene_view_find_enclosure_batch_ctx_async(
      sv, pv->enc_batch_ctx,
      pv->enc_locate_requests, pv->enc_locate_count);
    if(res != RES_OK) return res;
    pv->enc_gpu_pending = 1;
  } else {
    pv->enc_gpu_pending = 0;
  }

  /* closest_point launch (new async) */
  if(pv->cp_count > 0) {
    res = s3d_scene_view_closest_point_batch_ctx_async(
      sv, pv->cp_batch_ctx,
      pv->cp_requests, pv->cp_count);
    if(res != RES_OK) return res;
    pv->cp_gpu_pending = 1;
  } else {
    pv->cp_gpu_pending = 0;
  }

  return RES_OK;
}

/**
 * gpu_sync_kernel_all — sync RT + enc + cp compute kernels.
 */
static res_T
gpu_sync_kernel_all(struct wavefront_pool* pool, struct pool_view* pv)
{
  res_T rc;
  struct time k0, k1;

  /* RT sync (existing logic) */
  if(pv->ray_count > 0 && pv->gpu_pending) {
    time_current(&k0);
    if(pool->use_gpu_filter)
      rc = s3d_scene_view_trace_rays_batch_ctx_filtered_sync_kernel(
        pv->batch_ctx);
    else
      rc = s3d_scene_view_trace_rays_batch_ctx_sync_kernel(pv->batch_ctx);
    if(rc != RES_OK) return rc;
    time_current(&k1);
    pool->trace_kernel_time_ms_sum +=
      (double)s3d_batch_trace_context_get_last_kernel_ms(pv->batch_ctx);
  }

  /* enc sync */
  if(pv->enc_gpu_pending) {
    rc = s3d_scene_view_find_enclosure_batch_ctx_sync_kernel(pv->enc_batch_ctx);
    if(rc != RES_OK) return rc;
  }

  /* cp sync */
  if(pv->cp_gpu_pending) {
    rc = s3d_scene_view_closest_point_batch_ctx_sync_kernel(pv->cp_batch_ctx);
    if(rc != RES_OK) return rc;
  }

  return RES_OK;
}

/**
 * gpu_start_d2h_all — start async D2H for RT + enc + cp.
 */
static res_T
gpu_start_d2h_all(struct wavefront_pool* pool, struct pool_view* pv)
{
  res_T rc;

  /* RT D2H (existing logic) */
  if(pv->ray_count > 0 && pv->gpu_pending) {
    if(pool->use_gpu_filter)
      rc = s3d_scene_view_trace_rays_batch_ctx_filtered_start_d2h(
        pv->batch_ctx, pv->ray_count);
    else
      rc = s3d_scene_view_trace_rays_batch_ctx_start_d2h(
        pv->batch_ctx, pv->ray_count);
    if(rc != RES_OK) return rc;
  }

  /* enc D2H */
  if(pv->enc_gpu_pending) {
    rc = s3d_scene_view_find_enclosure_batch_ctx_start_d2h(
      pv->enc_batch_ctx, pv->enc_locate_count);
    if(rc != RES_OK) return rc;
  }

  /* cp D2H */
  if(pv->cp_gpu_pending) {
    rc = s3d_scene_view_closest_point_batch_ctx_start_d2h(
      pv->cp_batch_ctx, pv->cp_count);
    if(rc != RES_OK) return rc;
  }

  return RES_OK;
}

/**
 * gpu_wait_d2h_all — wait for D2H + post-process + distribute enc/cp results.
 * Replaces gpu_wait_d2h() and absorbs the distribute+post-process parts
 * of post_merged_enc_cp().
 */
static res_T
gpu_wait_d2h_all(struct wavefront_pool* pool,
                 struct pool_view* pv,
                 struct s3d_scene_view* sv)
{
  struct time t0, t1;
  res_T res;

  /* ── RT wait + post-process (existing gpu_wait_d2h logic) ── */
  time_current(&t0);
  if(pv->ray_count > 0 && pv->gpu_pending) {
    struct s3d_batch_trace_stats stats;
    memset(&stats, 0, sizeof(stats));

    if(pool->use_gpu_filter) {
      res = s3d_scene_view_trace_rays_batch_ctx_filtered_wait_d2h(
        sv, pv->batch_ctx,
        pv->ray_requests, pv->ray_count,
        pv->ray_hits, &stats);
    } else {
      res = s3d_scene_view_trace_rays_batch_ctx_wait_d2h(
        sv, pv->batch_ctx,
        pv->ray_requests, pv->ray_count,
        pv->ray_hits, &stats);
    }
    if(res != RES_OK) return res;
    pv->gpu_pending = 0;

    pool->total_rays_traced += pv->ray_count;

    {
      pool->rays_radiative           += pv->pending_rays_radiative;
      pool->rays_conductive_ds       += pv->pending_rays_conductive_ds;
      pool->rays_conductive_ds_retry += pv->pending_rays_conductive_ds_retry;
      pool->rays_shadow              += pv->pending_rays_shadow;
      pool->rays_enclosure           += pv->pending_rays_enclosure;
      pool->rays_startup             += pv->pending_rays_startup;
      pool->rays_other               += pv->pending_rays_other;
    }

    pool->trace_call_count++;
    pool->trace_batch_size_sum   += pv->ray_count;
    pool->trace_batch_time_ms_sum  += stats.batch_time_ms;
    pool->trace_post_time_ms_sum   += stats.postprocess_time_ms;
    pool->trace_retrace_time_ms_sum += stats.retrace_time_ms;
    pool->trace_retrace_accepted_sum += stats.retrace_accepted;
    pool->trace_retrace_missed_sum   += stats.retrace_missed;
    pool->trace_filter_rejected_sum  += stats.filter_rejected;

    if(pv->ray_count < pool->trace_batch_size_min)
      pool->trace_batch_size_min = pv->ray_count;
    if(pv->ray_count > pool->trace_batch_size_max)
      pool->trace_batch_size_max = pv->ray_count;
  }
  time_current(&t1);
  pool->time_trace_s += time_elapsed_sec(&t0, &t1);

  /* ── enc_locate wait + post-process (distribute deferred to merged_pass Phase A) ── */
  time_current(&t0);
  if(pv->enc_gpu_pending) {
    struct s3d_batch_enc_stats enc_stats;
    memset(&enc_stats, 0, sizeof(enc_stats));

    res = s3d_scene_view_find_enclosure_batch_ctx_wait_d2h(
      sv, pv->enc_batch_ctx,
      pv->enc_locate_requests, pv->enc_locate_count,
      pv->enc_locate_results, &enc_stats);
    if(res != RES_OK) return res;
    pv->enc_gpu_pending = 0;

    pool->enc_locates_total     += pv->enc_locate_count;
    pool->enc_locates_resolved  += enc_stats.resolved;
    pool->enc_locates_degenerate += enc_stats.degenerate;
  }
  time_current(&t1);
  pool->time_enc_locate_s += time_elapsed_sec(&t0, &t1);

  /* ── closest_point wait + post-process (distribute deferred to merged_pass Phase A) ── */
  time_current(&t0);
  if(pv->cp_gpu_pending) {
    struct s3d_batch_cp_stats cp_stats;
    memset(&cp_stats, 0, sizeof(cp_stats));

    res = s3d_scene_view_closest_point_batch_ctx_wait_d2h(
      sv, pv->cp_batch_ctx,
      pv->cp_requests, pv->cp_count,
      pv->cp_hits, &cp_stats);
    if(res != RES_OK) return res;
    pv->cp_gpu_pending = 0;

    pool->cp_total     += pv->cp_count;
    pool->cp_accepted  += cp_stats.batch_accepted;
    pool->cp_requeried += cp_stats.requery_accepted;
  }
  time_current(&t1);
  pool->time_cp_s += time_elapsed_sec(&t0, &t1);

  return RES_OK;
}

/**
 * gpu_wait_download_all — monolithic sync+D2H+wait for RT + enc + cp.
 * Used by pool_run_dual and single-pool loops that don't split the pipeline.
 */
static res_T
gpu_wait_download_all(struct wavefront_pool* pool,
                      struct pool_view* pv,
                      struct s3d_scene_view* sv)
{
  res_T res;
  res = gpu_sync_kernel_all(pool, pv);
  if(res != RES_OK) return res;
  res = gpu_start_d2h_all(pool, pv);
  if(res != RES_OK) return res;
  return gpu_wait_d2h_all(pool, pv, sv);
}

/*******************************************************************************
 * O11: merged_pass — single OMP scan combining distribute + cascade + collect
 *      + harvest.
 *
 * Replaces the 6-scan pipeline (compact+collect, distribute, enc/cp batch,
 * cascade, compact, harvest) with a single scan over path_state[].
 * Each path is loaded into L1 once; all per-path operations execute while
 * the cache line is hot.
 *
 * Phase A: Distribute (pure writer) — consume results from PREVIOUS round:
 *          ray_hits + enc_results + cp_results → write into slot-local storage.
 *          Also call step functions (absorbed from old distribute).
 *
 * Phase B: Cascade — no-ray inner loop until needs_ray / pending / done.
 *
 * Phase C: Collect — if needs_ray: write ray request to thread-local buffer.
 *          Also collect enc_locate and cp requests (atomic append).
 *
 * Phase D: Harvest — if PATH_DONE: accumulate result + mark PATH_HARVESTED.
 *
 * After the OMP parallel region, thread-local ray buffers are merged into
 * the pinned buffer (unsorted — no bucket ordering, Option A).
 ******************************************************************************/

/* --- Per-ray entry in thread-local ray buffer --- */
struct tl_ray_entry {
  uint32_t  slot;
  uint32_t  sub;          /* 0, 1, 2..5 for multi-ray */
  double    origin[3];
  double    direction[3];
  double    tmin, tmax;
  struct s3d_filter_per_ray filter;
};

/* Maximum rays per thread-local buffer before forced flush.
 * 8192 entries × ~104 B ≈ 800 KB stack — safe for OMP thread. */
#define TL_RAY_BUF_MAX 8192

/**
 * merged_pass_distribute_step — Phase A for one path.
 *
 * If this path had a ray traced last round (needs_ray == 1 on entry),
 * write back ray results and call the appropriate step function.
 * This absorbs the old pool_distribute_ray_results() three-bucket logic.
 *
 * Returns: 0 = ok, 1 = fatal error.
 */
static INLINE int
merged_pass_distribute_step(
  struct wavefront_pool* pool,
  struct pool_view*      pv,
  uint32_t               slot,
  struct path_state*     p,
  struct path_hot*       hot,
  struct sdis_scene*     scn,
  size_t*                tl_paths_failed,
  size_t*                tl_enc_escalated)
{
  /* Only process if this path had a ray last round (needs_ray still set) */
  if(!hot->needs_ray) return 0;

  /* O11_SAFETY: first-round guard — if batch_idx is still the sentinel
   * value from init, no GPU trace has completed for this path yet.
   * Skip distribute and let Phase C collect the ray for the first time. */
  if(p->ray_req.batch_idx == (uint32_t)-1) return 0;

  {
    /* Fetch ray hit(s) from previous GPU trace via batch_idx */
    const struct s3d_hit* h0 = &pv->ray_hits[p->ray_req.batch_idx];
    const struct s3d_hit* h1 = NULL;
    enum path_phase ph_before = (enum path_phase)hot->phase;
    res_T lr;

    if(p->ray_req.ray_count >= 2)
      h1 = &pv->ray_hits[p->ray_req.batch_idx2];

    /* Pre-deliver multi-ray results for special phases */
    if(ph_before == PATH_ENC_QUERY_EMIT && hot->ray_count_ext == 6) {
      int j;
      for(j = 0; j < 6; j++) {
        pool->enc_arr[slot].dir_hits[j] =
          pv->ray_hits[pool->enc_arr[slot].batch_indices[j]];
      }
    }
    if(ph_before == PATH_ENC_QUERY_FB_EMIT) {
      pool->enc_arr[slot].fb_hit = pv->ray_hits[p->ray_req.batch_idx];
    }
    /* B-4 M3: SS 4-ray pre-delivery */
    if(ph_before == PATH_BND_SS_REINJECT_SAMPLE
    && hot->ray_count_ext == 4) {
      p->locals.bnd_ss.ray_frt[0] =
        pv->ray_hits[p->locals.bnd_ss.batch_idx_frt0];
      p->locals.bnd_ss.ray_frt[1] =
        pv->ray_hits[p->locals.bnd_ss.batch_idx_frt1];
      p->locals.bnd_ss.ray_bck[0] =
        pv->ray_hits[p->locals.bnd_ss.batch_idx_bck0];
      p->locals.bnd_ss.ray_bck[1] =
        pv->ray_hits[p->locals.bnd_ss.batch_idx_bck1];
    }

    /* Clear needs_ray before calling step (matches old distribute) */
    hot->needs_ray = 0;

    /* O11_DIAG: log ray hit BEFORE step consumes it */
#ifndef NDEBUG
    if(pt_enabled() && (s_pt_pid < 0 || (long long)p->path_id == s_pt_pid)) {
      fprintf(stderr,
        "[PT-HIT] s=%u bidx=%u dist=%.8g norm=(%.8g,%.8g,%.8g) "
        "geom=%u prim=%u\n",
        (unsigned)slot,
        (unsigned)p->ray_req.batch_idx,
        h0->distance,
        h0->normal[0], h0->normal[1], h0->normal[2],
        (unsigned)h0->prim.geom_id, (unsigned)h0->prim.prim_id);
    }
#endif

    /* Call step function based on bucket type (absorbed from old distribute).
     *
     * RADIATIVE guard: only PATH_RAD_TRACE_PENDING is a true radiative
     * trace.  Other phases (BND_SF_NULLCOLL_RAD_TRACE, BND_SFN_RAD_TRACE,
     * BND_EXT_DIFFUSE_TRACE, CND_WOS_FALLBACK_TRACE, etc.) also set
     * ray_bucket=RADIATIVE for GPU batching but must go through
     * advance_one_step_with_ray for correct per-phase dispatch.
     * (Main branch uses phase-based bucketing in compact_active_paths
     * which routes only RAD_TRACE_PENDING to step_radiative_trace.)
     *
     * STEP_PAIR guard: only CND_DS_STEP_TRACE / COUPLED_COND_DS_PENDING are
     * true conductive DS step-pairs.  Other 2-ray phases (BND_SF_REINJECT,
     * BND_SS_REINJECT) also set ray_bucket=STEP_PAIR for GPU batching but
     * must go through advance_one_step_with_ray for correct dispatch. */
    switch((enum ray_bucket_type)hot->ray_bucket) {
    case RAY_BUCKET_RADIATIVE:
      if(ph_before == PATH_RAD_TRACE_PENDING) {
        lr = step_radiative_trace(p, hot, scn, h0);
      } else {
        /* Non-RAD_TRACE phases sharing RADIATIVE bucket — generic dispatch */
        lr = advance_one_step_with_ray(p, hot, scn, h0, h1,
                                        pool, (size_t)slot);
        if(ph_before == PATH_ENC_QUERY_FB_EMIT
        && (enum path_phase)hot->phase == PATH_ENC_LOCATE_PENDING) {
          (*tl_enc_escalated)++;
        }
      }
      break;
    case RAY_BUCKET_STEP_PAIR:
      if(ph_before == PATH_CND_DS_STEP_TRACE
      || ph_before == PATH_COUPLED_COND_DS_PENDING) {
        lr = step_conductive_ds_process(p, hot, scn, h0, h1,
                                         &pool->enc_arr[slot]);
      } else {
        lr = advance_one_step_with_ray(p, hot, scn, h0, h1,
                                        pool, (size_t)slot);
        if(ph_before == PATH_ENC_QUERY_FB_EMIT
        && (enum path_phase)hot->phase == PATH_ENC_LOCATE_PENDING) {
          (*tl_enc_escalated)++;
        }
      }
      break;
    default:
      lr = advance_one_step_with_ray(p, hot, scn, h0, h1,
                                      pool, (size_t)slot);
      if(ph_before == PATH_ENC_QUERY_FB_EMIT
      && (enum path_phase)hot->phase == PATH_ENC_LOCATE_PENDING) {
        (*tl_enc_escalated)++;
      }
      break;
    }

    /* O11_DIAG: log after step */
    pt_log("A", pool->total_steps, slot, p, hot, pool, (int)ph_before, (int)lr);

    /* Error handling (matches old distribute) */
    if(lr != RES_OK && lr != RES_BAD_OP
    && lr != RES_BAD_OP_IRRECOVERABLE) return 1; /* fatal */
    if(lr == RES_BAD_OP || lr == RES_BAD_OP_IRRECOVERABLE) {
      hot->phase = (uint8_t)PATH_DONE;
      hot->active = 0;
      p->done_reason = -1;
      (*tl_paths_failed)++;
    }
    p->steps_taken++;
  }
  return 0;
}

/**
 * merged_pass_collect_ray — Phase C: collect ray request for one path into
 * thread-local buffer.  Returns number of rays added.
 */
static INLINE size_t
merged_pass_collect_ray(
  struct wavefront_pool* pool,
  struct pool_view*      pv,
  struct path_state*     p,
  struct path_hot*       hot,
  uint32_t               slot,
  struct tl_ray_entry*   tl_entries,
  size_t                 tl_count)
{
  size_t added = 0;
  enum path_phase ph_i = (enum path_phase)hot->phase;
  (void)pv;

  /* Ray 0 */
  {
    struct tl_ray_entry* e = &tl_entries[tl_count + added];
    e->slot = slot;
    e->sub  = 0;
    e->origin[0]    = p->ray_req.origin[0];
    e->origin[1]    = p->ray_req.origin[1];
    e->origin[2]    = p->ray_req.origin[2];
    e->direction[0] = p->ray_req.direction[0];
    e->direction[1] = p->ray_req.direction[1];
    e->direction[2] = p->ray_req.direction[2];
    e->tmin          = p->ray_req.range[0];
    e->tmax          = p->ray_req.range[1];
    fill_filter_per_ray(&e->filter, p);
    added++;
  }

  /* Ray 1 (2-ray request) */
  if(p->ray_req.ray_count >= 2) {
    struct tl_ray_entry* e = &tl_entries[tl_count + added];
    e->slot = slot;
    e->sub  = 1;
    e->origin[0]    = p->ray_req.origin[0];
    e->origin[1]    = p->ray_req.origin[1];
    e->origin[2]    = p->ray_req.origin[2];
    e->direction[0] = p->ray_req.direction2[0];
    e->direction[1] = p->ray_req.direction2[1];
    e->direction[2] = p->ray_req.direction2[2];
    e->tmin          = p->ray_req.range2[0];
    e->tmax          = p->ray_req.range2[1];
    fill_filter_per_ray(&e->filter, p);
    added++;
  }

  /* Rays 2..5 for 6-ray enclosure query */
  if(hot->ray_count_ext == 6 && ph_i == PATH_ENC_QUERY_EMIT) {
    int j;
    /* batch_indices[0..1] are set from Ray 0/1 above */
    for(j = 2; j < 6; j++) {
      struct tl_ray_entry* e = &tl_entries[tl_count + added];
      e->slot = slot;
      e->sub  = (uint32_t)j;
      e->origin[0]    = p->ray_req.origin[0];
      e->origin[1]    = p->ray_req.origin[1];
      e->origin[2]    = p->ray_req.origin[2];
      e->direction[0] = pool->enc_arr[slot].directions[j][0];
      e->direction[1] = pool->enc_arr[slot].directions[j][1];
      e->direction[2] = pool->enc_arr[slot].directions[j][2];
      e->tmin          = p->ray_req.range[0];
      e->tmax          = p->ray_req.range[1];
      fill_filter_per_ray(&e->filter, p);
      added++;
    }
  }

  /* Rays 2..3 for SS 4-ray reinjection */
  if(hot->ray_count_ext == 4 && ph_i == PATH_BND_SS_REINJECT_SAMPLE) {
    int j;
    for(j = 0; j < 2; j++) {
      struct tl_ray_entry* e = &tl_entries[tl_count + added];
      e->slot = slot;
      e->sub  = (uint32_t)(j + 2);
      e->origin[0]    = p->ray_req.origin[0];
      e->origin[1]    = p->ray_req.origin[1];
      e->origin[2]    = p->ray_req.origin[2];
      e->direction[0] = p->locals.bnd_ss.dir_bck[j][0];
      e->direction[1] = p->locals.bnd_ss.dir_bck[j][1];
      e->direction[2] = p->locals.bnd_ss.dir_bck[j][2];
      e->tmin          = p->ray_req.range[0];
      e->tmax          = p->ray_req.range[1];
      fill_filter_per_ray(&e->filter, p);
      added++;
    }
  }

  return added;
}

/**
 * merged_pass_flush_tl_rays — merge thread-local ray buffer into pinned
 * buffer at the given base offset.
 *
 * Called per-thread AFTER the OMP for loop, under atomic-based offset
 * reservation.  Writes rays contiguously starting at ray_base.
 */
static void
merged_pass_flush_tl_rays(
  struct wavefront_pool* pool,
  struct pool_view*      pv,
  const struct tl_ray_entry* entries,
  size_t                 count,
  size_t                 ray_base)
{
  size_t r;
  for(r = 0; r < count; r++) {
    size_t ray_idx = ray_base + r;
    const struct tl_ray_entry* e = &entries[r];
    struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];

    rp->origin_x    = e->origin[0];
    rp->origin_y    = e->origin[1];
    rp->origin_z    = e->origin[2];
    rp->direction_x = e->direction[0];
    rp->direction_y = e->direction[1];
    rp->direction_z = e->direction[2];
    rp->tmin         = e->tmin;
    rp->tmax         = e->tmax;

    pv->filter_pinned[ray_idx] = e->filter;
    pv->ray_to_slot[ray_idx]   = e->slot;
    pv->ray_slot_sub[ray_idx]  = e->sub;
  }
}

/**
 * merged_pass_fixup_batch_idx — after all rays are written to pinned,
 * set batch_idx/batch_idx2/batch_indices on path_state so distribute
 * can find the results next round.
 *
 * Must be called AFTER flush, because ray indices are known only then.
 */
static void
merged_pass_fixup_batch_idx(
  struct wavefront_pool* pool,
  struct pool_view*      pv,
  size_t                 total_rays)
{
  int ri;
  int n = (int)total_rays;

  #pragma omp parallel for schedule(static)
  for(ri = 0; ri < n; ri++) {
    size_t r = (size_t)ri;
    uint32_t slot = pv->ray_to_slot[r];
    uint32_t sub  = pv->ray_slot_sub[r];
    struct path_state* p = &pool->slots[slot];

    switch(sub) {
    case 0:
      p->ray_req.batch_idx = (uint32_t)r;
      /* For 6-ray enc query, also store in enc_arr.batch_indices[0] */
      if(pool->hot_arr[slot].ray_count_ext == 6
      && (enum path_phase)pool->hot_arr[slot].phase == PATH_ENC_QUERY_EMIT)
        pool->enc_arr[slot].batch_indices[0] = (uint32_t)r;
      /* For 4-ray SS, store batch_idx_frt0 */
      if(pool->hot_arr[slot].ray_count_ext == 4
      && (enum path_phase)pool->hot_arr[slot].phase == PATH_BND_SS_REINJECT_SAMPLE)
        p->locals.bnd_ss.batch_idx_frt0 = (uint32_t)r;
      break;
    case 1:
      p->ray_req.batch_idx2 = (uint32_t)r;
      if(pool->hot_arr[slot].ray_count_ext == 6
      && (enum path_phase)pool->hot_arr[slot].phase == PATH_ENC_QUERY_EMIT)
        pool->enc_arr[slot].batch_indices[1] = (uint32_t)r;
      if(pool->hot_arr[slot].ray_count_ext == 4
      && (enum path_phase)pool->hot_arr[slot].phase == PATH_BND_SS_REINJECT_SAMPLE)
        p->locals.bnd_ss.batch_idx_frt1 = (uint32_t)r;
      break;
    case 2: case 3: case 4: case 5:
      if(pool->hot_arr[slot].ray_count_ext == 6)
        pool->enc_arr[slot].batch_indices[sub] = (uint32_t)r;
      if(sub == 2 && pool->hot_arr[slot].ray_count_ext == 4)
        p->locals.bnd_ss.batch_idx_bck0 = (uint32_t)r;
      if(sub == 3 && pool->hot_arr[slot].ray_count_ext == 4)
        p->locals.bnd_ss.batch_idx_bck1 = (uint32_t)r;
      break;
    }
  }
}

/*******************************************************************************
 * O11_SAFETY: Debug assertions for enc/cp delayed-one-round model.
 * Verify write-before-read causality and one-shot RESULT consumption.
 * Enable with: -DSDIS_DEBUG_CHECKS at compile time.
 ******************************************************************************/
#ifdef SDIS_DEBUG_CHECKS

/* Assertion 1: merged_pass entry — every RESULT phase has a matching
 * slot in the previous round's enc_locate_to_slot / cp_to_slot arrays,
 * proving that post_merged_enc_cp actually wrote the backing data. */
static void
assert_result_phases_backed(struct wavefront_pool* pool,
                            struct pool_view*      pv)
{
  size_t i, k;
  for(i = pv->base; i < pv->base + pv->view_size; i++) {
    if(pool->hot_arr[i].phase == (uint8_t)PATH_ENC_LOCATE_RESULT) {
      int found = 0;
      for(k = 0; k < pv->enc_locate_count; k++) {
        if(pv->enc_locate_to_slot[k] == (uint32_t)i) { found = 1; break; }
      }
      ASSERT(found && "O11_SAFETY: RESULT phase without GPU write: enc_locate");
    }
    if(pool->hot_arr[i].phase == (uint8_t)PATH_CND_WOS_CLOSEST_RESULT
    || pool->hot_arr[i].phase == (uint8_t)PATH_CND_WOS_DIFFUSION_CHECK_RESULT) {
      int found = 0;
      for(k = 0; k < pv->cp_count; k++) {
        if(pv->cp_to_slot[k] == (uint32_t)i) { found = 1; break; }
      }
      ASSERT(found && "O11_SAFETY: RESULT phase without GPU write: cp");
    }
  }
}

/* Assertion 2: merged_pass exit — no active path may still hold a RESULT
 * phase. All RESULTs must have been consumed by cascade in Phase B. */
static void
assert_no_residual_results(struct wavefront_pool* pool,
                           struct pool_view*      pv)
{
  size_t i;
  for(i = pv->base; i < pv->base + pv->view_size; i++) {
    if(!pool->hot_arr[i].active) continue;
    ASSERT(pool->hot_arr[i].phase != (uint8_t)PATH_ENC_LOCATE_RESULT
        && "O11_SAFETY: Residual ENC_LOCATE_RESULT after merged_pass");
    ASSERT(pool->hot_arr[i].phase != (uint8_t)PATH_CND_WOS_CLOSEST_RESULT
        && "O11_SAFETY: Residual CND_WOS_CLOSEST_RESULT after merged_pass");
    ASSERT(pool->hot_arr[i].phase != (uint8_t)PATH_CND_WOS_DIFFUSION_CHECK_RESULT
        && "O11_SAFETY: Residual CND_WOS_DIFFUSION_CHECK_RESULT after merged_pass");
  }
}

/* Assertion 3: before gpu_launch — confirm no RESULT phases remain.
 * Identical to assertion 2 but placed at a second verification point. */
static void
assert_no_pending_result_before_dispatch(struct wavefront_pool* pool,
                                          struct pool_view*      pv)
{
  size_t i;
  for(i = pv->base; i < pv->base + pv->view_size; i++) {
    enum path_phase ph;
    if(!pool->hot_arr[i].active) continue;
    ph = (enum path_phase)pool->hot_arr[i].phase;
    ASSERT(ph != PATH_ENC_LOCATE_RESULT
        && ph != PATH_CND_WOS_CLOSEST_RESULT
        && ph != PATH_CND_WOS_DIFFUSION_CHECK_RESULT
        && "O11_SAFETY: RESULT phase still present at gpu_dispatch");
  }
}

#endif /* SDIS_DEBUG_CHECKS */

/**
 * merged_pass — O11 single OMP scan.
 */
static res_T
merged_pass(struct wavefront_pool* pool,
            struct pool_view*      pv,
            struct sdis_scene*     scn)
{
  int omp_nthreads = (scn && scn->dev) ? (int)scn->dev->nthreads : 1;
  int had_fatal = 0;
  size_t n = pv->active_compact;
  size_t total_rays = 0;
  res_T res = RES_OK;

  if(omp_nthreads < 1) omp_nthreads = 1;

  /* O11_SAFETY: verify RESULT phases are backed by previous round's writes */
#ifdef SDIS_DEBUG_CHECKS
  assert_result_phases_backed(pool, pv);
#endif

  /* ── Phase A: Distribute all GPU results ── */

  /* A-enc: distribute enc_locate results (batch-level, before counter reset) */
  if(pv->enc_locate_count > 0 && !pv->enc_gpu_pending) {
    res = pool_distribute_enc_locate_results(pool, pv);
    if(res != RES_OK) return res;
  }

  /* A-cp: distribute closest_point results (batch-level, before counter reset) */
  if(pv->cp_count > 0 && !pv->cp_gpu_pending) {
    res = pool_distribute_cp_results(pool, pv);
    if(res != RES_OK) return res;
  }

  /* Reset per-view counters (enc/cp counts consumed above; A-rt below in per-path loop) */
  pv->ray_count = 0;
  pv->enc_locate_count = 0;
  pv->cp_count = 0;
  pv->done_count = 0;

  /* Zero pending ray stats */
  pv->pending_rays_radiative           = 0;
  pv->pending_rays_conductive_ds       = 0;
  pv->pending_rays_conductive_ds_retry = 0;
  pv->pending_rays_shadow              = 0;
  pv->pending_rays_enclosure           = 0;
  pv->pending_rays_startup             = 0;
  pv->pending_rays_other               = 0;

  if(n == 0) return RES_OK;

  if(n < 64 || omp_nthreads <= 1) {
    /* ════════ Serial fallback ════════ */
    size_t k;
    size_t tl_iterations = 0, tl_advances = 0, tl_paths_failed = 0;
    size_t tl_enc_escalated = 0;
    size_t tl_enc_degenerate_null = 0;
    size_t tl_completed = 0, tl_done_rad = 0, tl_done_temp = 0, tl_done_bnd = 0;
    size_t ser_ray_count = 0;
    /* Heap-alloc ray buffer for serial path */
    struct tl_ray_entry* ser_rays = NULL;
    size_t ser_rays_cap = 0;

    for(k = 0; k < n; k++) {
      uint32_t slot = pv->active_indices[k];
      struct path_state* p = &pool->slots[slot];
      struct path_hot* hot = &pool->hot_arr[slot];

      if(!hot->active) continue;

      /* ── Phase A: Distribute+Step (if had ray last round) ── */
      if(merged_pass_distribute_step(pool, pv, slot, p, hot, scn,
                                      &tl_paths_failed, &tl_enc_escalated))
        return RES_UNKNOWN_ERR;

      /* ── Phase B: Cascade ── */
      if(hot->active
      && (enum path_phase)hot->phase != PATH_DONE
      && (enum path_phase)hot->phase != PATH_ERROR) {
        if(cascade_advance_single_path(
              p, scn, pool, (size_t)slot,
              &tl_iterations, &tl_advances,
              &tl_paths_failed, &tl_enc_degenerate_null
#ifdef SDIS_CASCADE_PROFILE
              ,pool->cascade_phase_count, pool->cascade_phase_time
#endif
              ))
          return RES_UNKNOWN_ERR;
      }

      /* ── Phase C: Collect ray request ── */
      if(hot->needs_ray && hot->active && p->ray_req.ray_count > 0) {
        size_t nrays = count_path_rays_soa(
          (enum path_phase)hot->phase, hot->ray_count_ext, p);

        /* Ensure buffer capacity */
        if(ser_ray_count + nrays > ser_rays_cap) {
          size_t new_cap = (ser_rays_cap == 0) ? 4096 : ser_rays_cap * 2;
          while(new_cap < ser_ray_count + nrays) new_cap *= 2;
          ser_rays = (struct tl_ray_entry*)realloc(ser_rays,
            new_cap * sizeof(struct tl_ray_entry));
          ser_rays_cap = new_cap;
        }

        {
          size_t added = merged_pass_collect_ray(pool, pv, p, hot, slot,
                                                  ser_rays, ser_ray_count);
          /* Accumulate ray stats */
          {
            enum path_phase ph_i = (enum path_phase)hot->phase;
            switch(ph_i) {
            case PATH_RAD_TRACE_PENDING:
            case PATH_BND_SF_NULLCOLL_RAD_TRACE:
            case PATH_BND_SFN_RAD_TRACE:
            case PATH_BND_EXT_DIFFUSE_TRACE:
            case PATH_CND_WOS_FALLBACK_TRACE:
            case PATH_COUPLED_BOUNDARY_REINJECT:
            case PATH_BND_SS_REINJECT_SAMPLE:
            case PATH_BND_SF_REINJECT_SAMPLE:
              pv->pending_rays_radiative += nrays; break;
            case PATH_COUPLED_COND_DS_PENDING:
            case PATH_CND_DS_STEP_TRACE:
              if(p->locals.cnd_ds.robust_attempt > 0) pv->pending_rays_conductive_ds_retry += nrays;
              else                          pv->pending_rays_conductive_ds += nrays;
              break;
            case PATH_BND_EXT_DIRECT_TRACE:
            case PATH_BND_EXT_DIFFUSE_SHADOW_TRACE:
              pv->pending_rays_shadow += nrays; break;
            case PATH_ENC_QUERY_EMIT:
            case PATH_ENC_QUERY_FB_EMIT:
            case PATH_CND_INIT_ENC:
              pv->pending_rays_enclosure += nrays; break;
            case PATH_CNV_STARTUP_TRACE:
              pv->pending_rays_startup += nrays; break;
            default:
              pv->pending_rays_other += nrays; break;
            }
          }
          ser_ray_count += added;
        }
      }

      /* ── Phase C2: Collect enc_locate request ── */
      if((enum path_phase)hot->phase == PATH_ENC_LOCATE_PENDING) {
        size_t idx = pv->enc_locate_count;
        struct s3d_enc_locate_request* req = &pv->enc_locate_requests[idx];
        req->pos[0] = (float)pool->enc_arr[slot].locate.query_pos[0];
        req->pos[1] = (float)pool->enc_arr[slot].locate.query_pos[1];
        req->pos[2] = (float)pool->enc_arr[slot].locate.query_pos[2];
        req->user_id = slot;
        pv->enc_locate_to_slot[idx] = slot;
        pool->enc_arr[slot].locate.batch_idx = (uint32_t)idx;
        pv->enc_locate_count++;
      }

      /* ── Phase C3: Collect cp request ── */
      if(path_phase_is_cp_pending((enum path_phase)hot->phase)) {
        size_t idx = pv->cp_count;
        struct s3d_cp_request* req = &pv->cp_requests[idx];
        req->pos[0] = (float)p->locals.cnd_wos.query_pos[0];
        req->pos[1] = (float)p->locals.cnd_wos.query_pos[1];
        req->pos[2] = (float)p->locals.cnd_wos.query_pos[2];
        req->radius = p->locals.cnd_wos.query_radius;
        req->query_data = NULL;
        req->user_id = slot;
        pv->cp_to_slot[idx] = slot;
        p->locals.cnd_wos.batch_cp_idx = (uint32_t)idx;
        pv->cp_count++;
      }

      /* ── Phase D: Harvest ── */
      if(((enum path_phase)hot->phase == PATH_DONE
       || (enum path_phase)hot->phase == PATH_ERROR)
      && !hot->active) {
        /* M8: check sfn_stack_depth for Picard N resumption */
        if((enum path_phase)hot->phase == PATH_DONE
        && pool->sfn_arr[slot].depth > 0) {
          hot->phase = (uint8_t)PATH_BND_SFN_COMPUTE_Ti_RESUME;
          hot->active = 1;
        } else {
          pool->ops->accumulate_result(p, pool->result_ctx);

          { FILE* tf = pixel_trace_file();
            if(tf) {
              fprintf(tf, "%u,%u,%u,%u,%.17g,%d,%d,%llu,%d\n",
                (unsigned)p->path_id,
                (unsigned)p->ipix_image[0], (unsigned)p->ipix_image[1],
                (unsigned)p->realisation_idx,
                p->T.value, (int)p->T.done, p->done_reason,
                (unsigned long long)p->steps_taken,
                (int)(enum path_phase)hot->phase);
            }
          }

          if(p->steps_taken > pool->max_path_depth)
            pool->max_path_depth = p->steps_taken;

          tl_completed++;
          switch(p->done_reason) {
          case 1:  tl_done_rad++;  break;
          case 2:  tl_done_temp++; break;
          case 3:  tl_done_bnd++;  break;
          case 4:  tl_done_temp++; break;
          case -1: break;
          default: break;
          }

          hot->active = 0;
          hot->phase  = (uint8_t)PATH_HARVESTED;
          /* Record for refill */
          pv->done_indices[pv->done_count++] = slot;
        }
      }
    } /* end serial loop */

    /* Flush serial ray buffer to pinned */
    if(ser_ray_count > 0) {
      merged_pass_flush_tl_rays(pool, pv, ser_rays, ser_ray_count, 0);
      merged_pass_fixup_batch_idx(pool, pv, ser_ray_count);
    }
    pv->ray_count = ser_ray_count;
    free(ser_rays);

    /* Update pool counters */
    pool->cascade_total_iterations += tl_iterations;
    pool->cascade_total_advances   += tl_advances;
    pool->paths_failed             += tl_paths_failed;
    pool->enc_locate_degenerate_null += tl_enc_degenerate_null;
    pool->enc_query_escalated_to_m10 += tl_enc_escalated;
    pool->paths_completed          += tl_completed;
    pool->paths_done_radiative     += tl_done_rad;
    pool->paths_done_temperature   += tl_done_temp;
    pool->paths_done_boundary      += tl_done_bnd;

    return RES_OK;
  }

  /* Lazy-init per-thread ray buffers (once per pool lifetime) */
  if(!pool->tl_ray_bufs || pool->tl_ray_nbufs < omp_nthreads) {
    int ti;
    /* Free old (if nthreads grew) */
    if(pool->tl_ray_bufs) {
      for(ti = 0; ti < pool->tl_ray_nbufs; ti++)
        free(pool->tl_ray_bufs[ti]);
      free(pool->tl_ray_bufs);
    }
    pool->tl_ray_nbufs = omp_nthreads;
    pool->tl_ray_bufs = (void**)calloc(
      (size_t)omp_nthreads, sizeof(void*));
    if(!pool->tl_ray_bufs) return RES_MEM_ERR;
    for(ti = 0; ti < omp_nthreads; ti++) {
      pool->tl_ray_bufs[ti] = malloc(
        TL_RAY_BUF_MAX * sizeof(struct tl_ray_entry));
      if(!pool->tl_ray_bufs[ti]) return RES_MEM_ERR;
    }
  }

  /* ════════ OMP parallel merged_pass ════════ */
  #pragma omp parallel num_threads(omp_nthreads)
  {
    /* Thread-local accumulators */
    size_t tl_iterations = 0, tl_advances = 0, tl_paths_failed = 0;
    size_t tl_enc_escalated = 0, tl_enc_degenerate_null = 0;
    size_t tl_completed = 0, tl_done_rad = 0, tl_done_temp = 0, tl_done_bnd = 0;
    size_t tl_max_depth = 0;
    int tl_fatal = 0;

    /* Per-thread ray buffer from pre-allocated pool */
    struct tl_ray_entry* tl_rays =
      (struct tl_ray_entry*)pool->tl_ray_bufs[omp_get_thread_num()];
    size_t tl_ray_count = 0;

    /* Thread-local pending ray stats */
    size_t tl_rays_radiative = 0, tl_rays_cond_ds = 0;
    size_t tl_rays_cond_ds_retry = 0, tl_rays_shadow = 0;
    size_t tl_rays_enclosure = 0, tl_rays_startup = 0;
    size_t tl_rays_other = 0;

    int ph;

    #pragma omp for schedule(dynamic, 64)
    for(ph = 0; ph < (int)n; ph++) {
      uint32_t slot = pv->active_indices[ph];
      struct path_state* p = &pool->slots[slot];
      struct path_hot* hot = &pool->hot_arr[slot];

      if(!hot->active) continue;

      /* O7: prefetch 4 iterations ahead */
      if(ph + 4 < (int)n) {
        PREFETCH_T0(&pool->slots[pv->active_indices[ph + 4]]);
        PREFETCH_T0(&pool->hot_arr[pv->active_indices[ph + 4]]);
      }

      /* ── Phase A: Distribute+Step ── */
      if(merged_pass_distribute_step(pool, pv, slot, p, hot, scn,
                                      &tl_paths_failed, &tl_enc_escalated)) {
        tl_fatal = 1;
      }

      /* ── Phase B: Cascade ── */
      if(!tl_fatal && hot->active
      && (enum path_phase)hot->phase != PATH_DONE
      && (enum path_phase)hot->phase != PATH_ERROR) {
        if(cascade_advance_single_path(
              p, scn, pool, (size_t)slot,
              &tl_iterations, &tl_advances,
              &tl_paths_failed, &tl_enc_degenerate_null
#ifdef SDIS_CASCADE_PROFILE
              ,pool->cascade_phase_count, pool->cascade_phase_time
#endif
              )) {
          tl_fatal = 1;
        }
      }

      /* ── Phase C: Collect ray request ── */
      if(!tl_fatal && hot->needs_ray && hot->active
      && p->ray_req.ray_count > 0) {
        size_t nrays = count_path_rays_soa(
          (enum path_phase)hot->phase, hot->ray_count_ext, p);
        size_t added;

        /* Mid-loop flush: if buffer would overflow, flush to pinned
         * and reset.  Atomic reservation ensures no overlap. */
        if(tl_ray_count + nrays > TL_RAY_BUF_MAX) {
          size_t ray_base = (size_t)_InterlockedExchangeAdd64(
            (volatile long long*)&total_rays, (long long)tl_ray_count);
          merged_pass_flush_tl_rays(pool, pv, tl_rays, tl_ray_count, ray_base);
          tl_ray_count = 0;
        }

        {
          added = merged_pass_collect_ray(pool, pv, p, hot, slot,
                                           tl_rays, tl_ray_count);
          /* Ray stats */
          {
            enum path_phase ph_i = (enum path_phase)hot->phase;
            switch(ph_i) {
            case PATH_RAD_TRACE_PENDING:
            case PATH_BND_SF_NULLCOLL_RAD_TRACE:
            case PATH_BND_SFN_RAD_TRACE:
            case PATH_BND_EXT_DIFFUSE_TRACE:
            case PATH_CND_WOS_FALLBACK_TRACE:
            case PATH_COUPLED_BOUNDARY_REINJECT:
            case PATH_BND_SS_REINJECT_SAMPLE:
            case PATH_BND_SF_REINJECT_SAMPLE:
              tl_rays_radiative += nrays; break;
            case PATH_COUPLED_COND_DS_PENDING:
            case PATH_CND_DS_STEP_TRACE:
              if(p->locals.cnd_ds.robust_attempt > 0) tl_rays_cond_ds_retry += nrays;
              else                          tl_rays_cond_ds += nrays;
              break;
            case PATH_BND_EXT_DIRECT_TRACE:
            case PATH_BND_EXT_DIFFUSE_SHADOW_TRACE:
              tl_rays_shadow += nrays; break;
            case PATH_ENC_QUERY_EMIT:
            case PATH_ENC_QUERY_FB_EMIT:
            case PATH_CND_INIT_ENC:
              tl_rays_enclosure += nrays; break;
            case PATH_CNV_STARTUP_TRACE:
              tl_rays_startup += nrays; break;
            default:
              tl_rays_other += nrays; break;
            }
          }
          tl_ray_count += added;
        }
      }

      /* ── Phase C2: Collect enc_locate request (atomic) ── */
      if(!tl_fatal
      && (enum path_phase)hot->phase == PATH_ENC_LOCATE_PENDING) {
        size_t idx;
        idx = (size_t)_InterlockedExchangeAdd64(
          (volatile long long*)&pv->enc_locate_count, 1);

        {
          struct s3d_enc_locate_request* req = &pv->enc_locate_requests[idx];
          req->pos[0] = (float)pool->enc_arr[slot].locate.query_pos[0];
          req->pos[1] = (float)pool->enc_arr[slot].locate.query_pos[1];
          req->pos[2] = (float)pool->enc_arr[slot].locate.query_pos[2];
          req->user_id = slot;
          pv->enc_locate_to_slot[idx] = slot;
          pool->enc_arr[slot].locate.batch_idx = (uint32_t)idx;
        }
      }

      /* ── Phase C3: Collect cp request (atomic) ── */
      if(!tl_fatal
      && path_phase_is_cp_pending((enum path_phase)hot->phase)) {
        size_t idx;
        idx = (size_t)_InterlockedExchangeAdd64(
          (volatile long long*)&pv->cp_count, 1);

        {
          struct s3d_cp_request* req = &pv->cp_requests[idx];
          req->pos[0] = (float)p->locals.cnd_wos.query_pos[0];
          req->pos[1] = (float)p->locals.cnd_wos.query_pos[1];
          req->pos[2] = (float)p->locals.cnd_wos.query_pos[2];
          req->radius = p->locals.cnd_wos.query_radius;
          req->query_data = NULL;
          req->user_id = slot;
          pv->cp_to_slot[idx] = slot;
          p->locals.cnd_wos.batch_cp_idx = (uint32_t)idx;
        }
      }

      /* ── Phase D: Harvest ── */
      if(!tl_fatal
      && ((enum path_phase)hot->phase == PATH_DONE
       || (enum path_phase)hot->phase == PATH_ERROR)
      && !hot->active) {
        /* M8: Picard N resumption check */
        if((enum path_phase)hot->phase == PATH_DONE
        && pool->sfn_arr[slot].depth > 0) {
          hot->phase = (uint8_t)PATH_BND_SFN_COMPUTE_Ti_RESUME;
          hot->active = 1;
        } else {
          /* accumulate_result is atomic-safe (writes to distinct pixels) */
          pool->ops->accumulate_result(p, pool->result_ctx);

          { FILE* tf = pixel_trace_file();
            if(tf) {
              #pragma omp critical(harvest_trace)
              fprintf(tf, "%u,%u,%u,%u,%.17g,%d,%d,%llu,%d\n",
                (unsigned)p->path_id,
                (unsigned)p->ipix_image[0], (unsigned)p->ipix_image[1],
                (unsigned)p->realisation_idx,
                p->T.value, (int)p->T.done, p->done_reason,
                (unsigned long long)p->steps_taken,
                (int)(enum path_phase)hot->phase);
            }
          }

          if(p->steps_taken > tl_max_depth)
            tl_max_depth = p->steps_taken;

          tl_completed++;
          switch(p->done_reason) {
          case 1:  tl_done_rad++;  break;
          case 2:  tl_done_temp++; break;
          case 3:  tl_done_bnd++;  break;
          case 4:  tl_done_temp++; break;
          case -1: break;
          default: break;
          }

          hot->active = 0;
          hot->phase  = (uint8_t)PATH_HARVESTED;

          /* Record done slot for refill (atomic append) */
          {
            size_t di;
            di = (size_t)_InterlockedExchangeAdd64(
              (volatile long long*)&pv->done_count, 1);
            pv->done_indices[di] = slot;
          }
        }
      }
    } /* end omp for */

    /* ── Thread-local ray buffer → pinned merge ── */
    if(tl_ray_count > 0) {
      size_t ray_base;
      ray_base = (size_t)_InterlockedExchangeAdd64(
        (volatile long long*)&total_rays, (long long)tl_ray_count);
      merged_pass_flush_tl_rays(pool, pv, tl_rays, tl_ray_count, ray_base);
    }

    /* ── Reduction: thread-local → pool ── */
    #pragma omp atomic
    pool->cascade_total_iterations += tl_iterations;
    #pragma omp atomic
    pool->cascade_total_advances   += tl_advances;
    #pragma omp atomic
    pool->paths_failed             += tl_paths_failed;
    #pragma omp atomic
    pool->enc_locate_degenerate_null += tl_enc_degenerate_null;
    #pragma omp atomic
    pool->enc_query_escalated_to_m10 += tl_enc_escalated;
    #pragma omp atomic
    pool->paths_completed          += tl_completed;
    #pragma omp atomic
    pool->paths_done_radiative     += tl_done_rad;
    #pragma omp atomic
    pool->paths_done_temperature   += tl_done_temp;
    #pragma omp atomic
    pool->paths_done_boundary      += tl_done_bnd;

    #pragma omp atomic
    pv->pending_rays_radiative           += tl_rays_radiative;
    #pragma omp atomic
    pv->pending_rays_conductive_ds       += tl_rays_cond_ds;
    #pragma omp atomic
    pv->pending_rays_conductive_ds_retry += tl_rays_cond_ds_retry;
    #pragma omp atomic
    pv->pending_rays_shadow              += tl_rays_shadow;
    #pragma omp atomic
    pv->pending_rays_enclosure           += tl_rays_enclosure;
    #pragma omp atomic
    pv->pending_rays_startup             += tl_rays_startup;
    #pragma omp atomic
    pv->pending_rays_other               += tl_rays_other;

    #pragma omp critical(merged_max_depth)
    {
      if(tl_max_depth > pool->max_path_depth)
        pool->max_path_depth = tl_max_depth;
    }

    if(tl_fatal) had_fatal = 1;
  } /* end omp parallel */

  if(had_fatal) return RES_UNKNOWN_ERR;

  /* Fixup batch_idx after all rays are in pinned buffer */
  pv->ray_count = total_rays;
  merged_pass_fixup_batch_idx(pool, pv, total_rays);

  /* O11_SAFETY: verify all RESULT phases consumed — none should survive cascade */
#ifdef SDIS_DEBUG_CHECKS
  assert_no_residual_results(pool, pv);
#endif

  return RES_OK;
}



/*******************************************************************************
 * pool_run_single — Single-buffer main loop (bare, no progress reporting).
 *
 * O11 merged architecture:
 *   1. merged_pass:     distribute+cascade+collect+harvest (single OMP scan)
 *   2. compact:         rebuild active_indices from hot_arr
 *   3. refill:          replace completed paths with new tasks
 *   4. enc/cp batch:    synchronous enc_locate + closest_point
 *   5. gpu_launch:      async RT trace
 *   6. gpu_wait:        sync + D2H (ray results ready for next merged_pass)
 ******************************************************************************/
static res_T
pool_run_single(struct wavefront_pool* pool,
                struct s3d_scene_view* sv,
                struct sdis_scene* scn)
{
  struct pool_view* pv = &pool->views[0];
  struct time t0, t1;
  res_T res;
  size_t refill_count = 0;

  compact_active_paths(pool, pv);
  pool_update_active_count(pool);

  while(pool->active_count > 0 || pool->task_next < pool->task_count) {
    pool->total_steps++;

    /* Step 1: O11 merged_pass (distribute+cascade+collect+harvest) */
    time_current(&t0);
    res = merged_pass(pool, pv, scn);
    if(res != RES_OK) return res;
    time_current(&t1);
    pool->time_cascade_s += time_elapsed_sec(&t0, &t1);

    /* Step 2: compact active indices */
    time_current(&t0);
    compact_active_paths(pool, pv);
    time_current(&t1);
    pool->time_compact_s += time_elapsed_sec(&t0, &t1);

    /* Step 3: refill completed slots */
    time_current(&t0);
    res = refill_pool(pool, pv, &refill_count);
    if(res != RES_OK) return res;
    time_current(&t1);
    pool->time_harvest_s += time_elapsed_sec(&t0, &t1);

    /* Step 4: GPU launch RT + enc + cp (async) */
#ifdef SDIS_DEBUG_CHECKS
    assert_no_pending_result_before_dispatch(pool, pv);
#endif
    time_current(&t0);
    res = gpu_launch_all(pool, pv, sv);
    if(res != RES_OK) return res;
    time_current(&t1);
    pool->time_gpu_launch_s += time_elapsed_sec(&t0, &t1);

    /* Step 5: GPU wait + D2H for RT + enc + cp */
    res = gpu_wait_download_all(pool, pv, sv);
    if(res != RES_OK) return res;

    /* DIAG_V3: optional extra cascade after enc/cp distribution.
     * Main branch resolves enc_locate/cp synchronously within the same
     * iteration *before* cascade.  Merge-phase defers them to GPU,
     * meaning freshly-distributed enc/cp RESULT phases wait until the
     * next iteration's Phase B cascade.  Enable with env var
     * STARDIS_DIAG_V3=1 to add a compensating cascade sweep here.
     * If results then match main, the enc/cp timing gap causes the
     * residual divergence (H2). */
    {
      static int diag_v3 = -1;
      if(diag_v3 < 0) {
        const char* e = getenv("STARDIS_DIAG_V3");
        diag_v3 = (e && e[0] == '1') ? 1 : 0;
      }
      if(diag_v3) {
        res = pool_cascade_non_ray_steps_compact(pool, pv, scn);
        if(res != RES_OK) return res;
      }
    }

    pool_update_active_count(pool);

    /* Safety: prevent infinite loops */
    if(pool->total_steps > pool->task_count * 1000) {
      return RES_BAD_OP;
    }
  }

  return RES_OK;
}

/*******************************************************************************
 * pool_run_dual — Dual-buffer pipeline main loop (bare, no progress).
 *
 * O11: Overlaps GPU trace on one view with merged_pass on the other.
 * Each half-cycle: wait GPU(X) → merged_pass(X) + compact + refill +
 *   enc/cp batch on X → gpu_launch(X).
 ******************************************************************************/
static res_T
pool_run_dual(struct wavefront_pool* pool,
              struct s3d_scene_view* sv,
              struct sdis_scene* scn)
{
  struct pool_view* pv_a = &pool->views[0];
  struct pool_view* pv_b = &pool->views[1];
  res_T res;
  size_t refill_count = 0;

  /* Startup: initial compact + merged_pass(A) → launch GPU(A) */
  compact_active_paths(pool, pv_a);
  res = merged_pass(pool, pv_a, scn);
  if(res != RES_OK) return res;
  compact_active_paths(pool, pv_a);
  res = refill_pool(pool, pv_a, &refill_count);
  if(res != RES_OK) return res;
#ifdef SDIS_DEBUG_CHECKS
  assert_no_pending_result_before_dispatch(pool, pv_a);
#endif
  res = gpu_launch_all(pool, pv_a, sv);
  if(res != RES_OK) return res;

  /* Startup: initial compact + merged_pass(B) */
  compact_active_paths(pool, pv_b);
  res = merged_pass(pool, pv_b, scn);
  if(res != RES_OK) return res;
  compact_active_paths(pool, pv_b);
  res = refill_pool(pool, pv_b, &refill_count);
  if(res != RES_OK) return res;
  /* B's enc/cp requests pending — will be launched in first gpu_launch_all(B) */

  while(pool->active_count > 0 || pool->task_next < pool->task_count) {
    struct time t_pl0, t_pl1;

    /* ════════ Phase 1: wait GPU(A) → launch GPU(B) → CPU on A ════════ */
    time_current(&t_pl0);
    res = gpu_wait_download_all(pool, pv_a, sv);
    if(res != RES_OK) return res;
    time_current(&t_pl1);
    pool->time_pipeline_wait_s += time_elapsed_sec(&t_pl0, &t_pl1);

#ifdef SDIS_DEBUG_CHECKS
    assert_no_pending_result_before_dispatch(pool, pv_b);
#endif
    res = gpu_launch_all(pool, pv_b, sv);
    if(res != RES_OK) return res;

    pool->total_steps++;

    time_current(&t_pl0);
    /* O11: merged_pass on A (distribute+cascade+collect+harvest) */
    { struct time t_mp0, t_mp1;
      time_current(&t_mp0);
      res = merged_pass(pool, pv_a, scn);
      if(res != RES_OK) return res;
      time_current(&t_mp1);
      pool->time_cascade_s += time_elapsed_sec(&t_mp0, &t_mp1);
    }
    { struct time t_cr0, t_cr1;
      time_current(&t_cr0);
      compact_active_paths(pool, pv_a);
      res = refill_pool(pool, pv_a, &refill_count);
      if(res != RES_OK) return res;
      if(refill_count > 0) compact_active_paths(pool, pv_a);
      time_current(&t_cr1);
      pool->time_harvest_s += time_elapsed_sec(&t_cr0, &t_cr1);
    }
    time_current(&t_pl1);
    pool->time_pipeline_cpu_between_s += time_elapsed_sec(&t_pl0, &t_pl1);

    pool_update_active_count(pool);

    if(!pool->in_drain_phase && pool->task_next >= pool->task_count) {
      pool->in_drain_phase = 1;
    }

    if(pool->total_steps > pool->task_count * 1000) {
      return RES_BAD_OP;
    }

    /* ════════ Phase 2: wait GPU(B) → launch GPU(A) → CPU on B ════════ */
    time_current(&t_pl0);
    res = gpu_wait_download_all(pool, pv_b, sv);
    if(res != RES_OK) return res;
    time_current(&t_pl1);
    pool->time_pipeline_wait_s += time_elapsed_sec(&t_pl0, &t_pl1);

#ifdef SDIS_DEBUG_CHECKS
    assert_no_pending_result_before_dispatch(pool, pv_a);
#endif
    res = gpu_launch_all(pool, pv_a, sv);
    if(res != RES_OK) return res;

    pool->total_steps++;

    time_current(&t_pl0);
    /* O11: merged_pass on B */
    { struct time t_mp0, t_mp1;
      time_current(&t_mp0);
      res = merged_pass(pool, pv_b, scn);
      if(res != RES_OK) return res;
      time_current(&t_mp1);
      pool->time_cascade_s += time_elapsed_sec(&t_mp0, &t_mp1);
    }
    { struct time t_cr0, t_cr1;
      time_current(&t_cr0);
      compact_active_paths(pool, pv_b);
      res = refill_pool(pool, pv_b, &refill_count);
      if(res != RES_OK) return res;
      if(refill_count > 0) compact_active_paths(pool, pv_b);
      time_current(&t_cr1);
      pool->time_harvest_s += time_elapsed_sec(&t_cr0, &t_cr1);
    }
    time_current(&t_pl1);
    pool->time_pipeline_cpu_between_s += time_elapsed_sec(&t_pl0, &t_pl1);

    pool_update_active_count(pool);

    if(!pool->in_drain_phase && pool->task_next >= pool->task_count) {
      pool->in_drain_phase = 1;
    }

    if(pool->total_steps > pool->task_count * 1000) {
      return RES_BAD_OP;
    }

    /* ════════ Dynamic merge check ════════ */
    if(should_merge(pool)) {
      log_warn(pool->scn->dev,
        "O11_MERGE step=%llu v0_active=%zu v0_size=%zu v1_active=%zu v1_size=%zu thresh=%zu tasks=%zu/%zu\n",
        (unsigned long long)pool->total_steps,
        pool->views[0].active_compact, pool->views[0].view_size,
        pool->views[1].active_compact, pool->views[1].view_size,
        pool->views[0].view_size / 8,
        pool->task_next, pool->task_count);
      if(pv_a->gpu_pending || pv_a->enc_gpu_pending || pv_a->cp_gpu_pending) {
        res = gpu_wait_download_all(pool, pv_a, sv);
        if(res != RES_OK) return res;
      }
      if(pv_b->gpu_pending || pv_b->enc_gpu_pending || pv_b->cp_gpu_pending) {
        res = gpu_wait_download_all(pool, pv_b, sv);
        if(res != RES_OK) return res;
      }
      merge_to_single_pool(pool);
      break;
    }
  } /* end dual-buffer while */

  /* Drain: wait for last pending GPU calls */
  if(pool->num_active_views == 2) {
    if(pv_a->gpu_pending || pv_a->enc_gpu_pending || pv_a->cp_gpu_pending) {
      res = gpu_wait_download_all(pool, pv_a, sv);
      if(res != RES_OK) return res;
      pool->total_steps++;
      res = merged_pass(pool, pv_a, scn);
      if(res != RES_OK) return res;
      compact_active_paths(pool, pv_a);
    }
    if(pv_b->gpu_pending || pv_b->enc_gpu_pending || pv_b->cp_gpu_pending) {
      res = gpu_wait_download_all(pool, pv_b, sv);
      if(res != RES_OK) return res;
      pool->total_steps++;
      res = merged_pass(pool, pv_b, scn);
      if(res != RES_OK) return res;
      compact_active_paths(pool, pv_b);
    }
  }

  return RES_OK;
}

/*******************************************************************************
 * pool_run — Unified main loop dispatcher.
 *
 * Runs the dual-buffer pipeline if num_active_views == 2 (which may
 * dynamically merge to single).  Then runs single-buffer to completion.
 *
 * This is the entry point for probe / probe_batch modes.  Camera mode
 * currently uses its own verbose loop with progress reporting.
 ******************************************************************************/
static res_T
pool_run(struct wavefront_pool* pool,
         struct s3d_scene_view* sv,
         struct sdis_scene* scn)
{
  res_T res = RES_OK;

  /* Phase 1: Dual-buffer pipeline (if scheduled) */
  if(pool->num_active_views == 2) {
    log_info(scn->dev,
      "pool_run: dual-buffer pipeline, per-view=%llu, pool=%llu\n",
      (unsigned long long)pool->views[0].view_size,
      (unsigned long long)pool->pool_size);

    res = pool_run_dual(pool, sv, scn);
    if(res != RES_OK) return res;

    /* May have merged — log if so */
    if(pool->num_active_views == 1) {
      log_info(scn->dev,
        "pool_run: merged to single-buffer at step %llu\n",
        (unsigned long long)pool->total_steps);
    }
  }

  /* Phase 2: Single-buffer drain (or full run if started single) */
  if(pool->num_active_views == 1) {
    res = pool_run_single(pool, sv, scn);
    if(res != RES_OK) return res;
  }

  return res;
}

/*******************************************************************************
 * Determine pool size — adaptive based on GPU SM count (M3)
 *
 * Formula: pool_size = sm_count × WARPS_PER_SM × 32
 *   - Ensures enough threads to fill all SMs with reasonable occupancy.
 *   - Falls back to PERSISTENT_WF_DEFAULT_POOL_SIZE if SM count unavailable.
 *   - Capped at total_tasks (no point allocating more slots than tasks).
 *   - Overridable via STARDIS_POOL_SIZE environment variable.
 ******************************************************************************/
#define WARPS_PER_SM  8  /* Conservative occupancy target per SM */

static size_t
determine_pool_size(size_t total_tasks, struct sdis_scene* scn)
{
  size_t pool_size = PERSISTENT_WF_DEFAULT_POOL_SIZE;
  const char* env = getenv("STARDIS_POOL_SIZE");

  /* Priority 1: Environment variable override */
  if(env) {
    long val = atol(env);
    if(val > 0) {
      pool_size = (size_t)val;
      goto cap;
    }
  }

  /* Priority 2: Adaptive sizing from GPU SM count */
  if(scn && scn->dev && scn->dev->s3d_dev) {
    int sm_count = s3d_device_get_gpu_sm_count(scn->dev->s3d_dev);
    if(sm_count > 0) {
      /* sm_count × warps_per_SM × 32 threads/warp */
      pool_size = (size_t)sm_count * WARPS_PER_SM * 32;
    }
  }

cap:
  if(pool_size > total_tasks) pool_size = total_tasks;
  if(pool_size == 0) pool_size = 1;

  return pool_size;
}

/*******************************************************************************
 * Public entry point
 ******************************************************************************/
#ifndef SDIS_SOLVE_PERSISTENT_WAVEFRONT_SKIP_PUBLIC_API
res_T
solve_camera_persistent_wavefront(
  struct sdis_scene*          scn,
  struct ssp_rng**            per_thread_rng,
  const size_t                nthreads,
  const unsigned              enc_id,
  const struct sdis_camera*   cam,
  const double                time_range[2],
  const size_t                image_def[2],
  const size_t                spp,
  const int                   register_paths,
  const double                pix_sz[2],
  const size_t                picard_order,
  const enum sdis_diffusion_algorithm diff_algo,
  struct sdis_estimator_buffer* buf,
  int32_t                     progress[],
  const int                   pcent_progress,
  const char*                 progress_label)
{
  struct wavefront_pool pool;
  struct camera_mode_ctx cam_ctx;
  size_t total_tasks;
  size_t pool_size;
  res_T res = RES_OK;
  struct time t_start, t_end, t_elapsed;
  int last_pcent = 0;

  ASSERT(scn && per_thread_rng && nthreads > 0 && cam && spp);
  ASSERT(image_def && image_def[0] > 0 && image_def[1] > 0);
  ASSERT(pix_sz && pix_sz[0] > 0 && pix_sz[1] > 0 && time_range && buf);

  (void)register_paths; /* heat path tracking not yet supported */

  total_tasks = image_def[0] * image_def[1] * spp;
  pool_size = determine_pool_size(total_tasks, scn);

  /* pool_create decides dual/single based on total_tasks/pool_size ratio.
   * pool_size here is the per-view target; pool_create doubles if dual. */

  {
    int sm = (scn->dev && scn->dev->s3d_dev)
           ? s3d_device_get_gpu_sm_count(scn->dev->s3d_dev) : 0;
    log_info(scn->dev,
      "Persistent wavefront (Phase B-3 M3): %llux%llu spp=%llu, "
      "per_view_size=%llu (SM=%d), total_tasks=%llu\n",
      (unsigned long long)image_def[0], (unsigned long long)image_def[1],
      (unsigned long long)spp, (unsigned long long)pool_size,
      sm, (unsigned long long)total_tasks);
  }

  /* Log cascade OMP configuration */
  {
    const char* env = getenv("STARDIS_CASCADE_OMP");
    int cascade_omp = (env && env[0] == '0') ? 0 : 1;
    int omp_nt = (scn && scn->dev) ? (int)scn->dev->nthreads : 1;
    if(omp_nt < 1) omp_nt = 1;
    log_info(scn->dev,
      "Cascade OMP: %s, threads=%d (STARDIS_CASCADE_OMP=%s)\n",
      cascade_omp ? "ENABLED" : "DISABLED",
      cascade_omp ? omp_nt : 1,
      env ? env : "<unset,default=1>");
  }

  /* ====== 1. Allocate pool (self-adaptive dual/single) ====== */
  res = pool_create(&pool, pool_size, total_tasks);
  if(res != RES_OK) {
    log_err(scn->dev,
      "persistent_wavefront: pool_create failed for %llu paths -- %s\n",
      (unsigned long long)pool_size, res_to_cstr(res));
    goto cleanup;
  }

  /* P2: Log pool view configuration */
  if(pool.num_active_views == 2) {
    log_info(scn->dev,
      "persistent_wavefront: dual-buffer pipeline, "
      "view_size=%llu, views[0].capacity=%llu, views[1].capacity=%llu\n",
      (unsigned long long)pool.views[0].view_size,
      (unsigned long long)pool.views[0].capacity,
      (unsigned long long)pool.views[1].capacity);
  } else {
    log_info(scn->dev,
      "persistent_wavefront: single-buffer mode, pool_size=%llu\n",
      (unsigned long long)pool.pool_size);
  }

  /* ====== 1b. Wire camera vtable + context (needed before step 3) ====== */
  cam_ctx.cam       = cam;
  cam_ctx.pix_sz    = pix_sz;
  cam_ctx.image_def = image_def;
  cam_ctx.spp       = spp;
  pool.ops          = &wf_ops_camera;
  pool.mode_ctx     = &cam_ctx;
  pool.result_ctx   = buf;

  /* ====== 2. Create per-path CBRNG thin wrappers ====== */
  /* Each slot gets its own ssp_rng whose desc.get delegates to wf_rng_get()
   * on the embedded path_state.rng_state.  This eliminates the round-robin
   * RNG sharing that caused 32x32-pixel block noise patterns. */
  {
    struct ssp_rng* thin_storage = NULL;
    /* Build array of wf_rng pointers (one per slot) for the C++ adapter */
    struct wf_rng** rng_ptrs = (struct wf_rng**)malloc(
        pool.pool_size * sizeof(struct wf_rng*));
    if(!rng_ptrs) {
      res = RES_MEM_ERR;
      log_err(scn->dev,
        "persistent_wavefront: rng_ptrs alloc failed\n");
      goto cleanup;
    }
    { size_t ri;
      for(ri = 0; ri < pool.pool_size; ri++)
        rng_ptrs[ri] = &pool.slots[ri].rng_state;
    }
    res = wf_rng_create_thin_ssp_rngs(pool.pool_size, rng_ptrs,
                                       pool.slot_rngs, &thin_storage);
    free(rng_ptrs);
    if(res != RES_OK) {
      log_err(scn->dev,
        "persistent_wavefront: wf_rng thin wrapper creation failed -- %s\n",
        res_to_cstr(res));
      goto cleanup;
    }
    pool.thin_rng_storage = thin_storage;
  }

  /* Store global seed for per-path CBRNG seeding.
   * Derived from the first per_thread_rng to ensure deterministic
   * behaviour for a given input seed (user's -s parameter). */
  {
    double r0 = ssp_rng_canonical(per_thread_rng[0]);
    double r1 = ssp_rng_canonical(per_thread_rng[0]);
    uint32_t hi = (uint32_t)(r0 * 4294967296.0);  /* [0, 2^32) */
    uint32_t lo = (uint32_t)(r1 * 4294967296.0);  /* [0, 2^32) */
    pool.global_seed = ((uint64_t)hi << 32) | (uint64_t)lo;
  }

  /* ====== 3. Generate task queue (via ops dispatch) ====== */
  res = pool.ops->generate_tasks(&pool, pool.mode_ctx);
  if(res != RES_OK) {
    log_err(scn->dev,
      "persistent_wavefront: task queue generation failed -- %s\n",
      res_to_cstr(res));
    goto cleanup;
  }

  /* ====== 4. Create batch trace context ====== */
  res = s3d_batch_trace_context_create(&pool.batch_ctx, pool.max_rays);
  if(res != RES_OK) {
    log_err(scn->dev,
      "persistent_wavefront: batch_ctx creation failed for %llu rays -- %s\n",
      (unsigned long long)pool.max_rays, res_to_cstr(res));
    goto cleanup;
  }

  /* ====== 4b. Create batch enc_locate context (M10) ====== */
  res = s3d_batch_enc_context_create(&pool.enc_batch_ctx, pool.max_enc_locates);
  if(res != RES_OK) {
    log_err(scn->dev,
      "persistent_wavefront: enc_batch_ctx creation failed for %llu queries -- %s\n",
      (unsigned long long)pool.max_enc_locates, res_to_cstr(res));
    goto cleanup;
  }

  /* ====== 4c. Create batch closest_point context (M9: WoS) ====== */
  res = s3d_batch_cp_context_create(&pool.cp_batch_ctx, pool.max_cps);
  if(res != RES_OK) {
    log_err(scn->dev,
      "persistent_wavefront: cp_batch_ctx creation failed for %llu queries -- %s\n",
      (unsigned long long)pool.max_cps, res_to_cstr(res));
    goto cleanup;
  }

  /* ====== 5. Cache scene params for refill ====== */
  pool.scn          = scn;
  pool.enc_id       = enc_id;
  pool.time_range   = time_range;
  pool.picard_order = picard_order;
  pool.diff_algo    = diff_algo;

  /* ====== 5b. L4: Enable GPU inline filter (Mode A) ====== */
  /* When set, gpu_launch_async/gpu_sync_kernel/gpu_start_d2h/gpu_wait_d2h
   * use the filtered trace path that does hit filtering on the GPU,
   * eliminating the CPU retrace bottleneck (~48% of postprocess time).
   * Filter ③ (enclosure boundary) requires per-shape enclosure data
   * upload via s3d_scene_view_set_enclosure_data(); without it, only
   * filters ①(self-intersection) and ②(near-epsilon) are active. */
  pool.use_gpu_filter = 1;

  /* TODO(L4): Upload per-shape enclosure arrays to GPU for filter ③.
   * Requires iterating scn->prim_props per shape, extracting
   * front_enclosure/back_enclosure arrays, and calling
   * s3d_scene_view_set_enclosure_data(scn->s3d_view, shape_id,
   *   enc_front, enc_back, num_prims) for each shape.
   * Without this, filter ③ is safely skipped (enc_front==NULL). */

  /* ====== 6. Fill pool with initial tasks ====== */
  res = fill_pool(&pool);
  if(res != RES_OK) goto cleanup;

  log_info(scn->dev,
    "persistent_wavefront: %llu paths initialised (%llu/%llu tasks consumed), "
    "starting wavefront loop\n",
    (unsigned long long)pool.active_count,
    (unsigned long long)pool.task_next,
    (unsigned long long)pool.task_count);

  /* ====== 7. Wavefront main loop ====== */
  time_current(&t_start);

  /* ═══════════════════════════════════════════════════════ */
  /*  P4: Dual-buffer pipeline (num_active_views == 2)       */
  /* ═══════════════════════════════════════════════════════ */
  if(pool.num_active_views == 2) {
    struct pool_view* pv_a = &pool.views[0];
    struct pool_view* pv_b = &pool.views[1];

    log_info(scn->dev,
      "pipeline: starting dual-buffer mode, half=%llu\n",
      (unsigned long long)pv_a->view_size);

    /* ---- O11 Startup: merged_pass(A) → launch GPU(A) ---- */
    compact_active_paths(&pool, pv_a);
    log_info(scn->dev,
      "O11_STARTUP A pre-merge: active=%llu need_ray=%llu done=%llu\n",
      (unsigned long long)pv_a->active_compact,
      (unsigned long long)pv_a->need_ray_count,
      (unsigned long long)pv_a->done_count);
    res = merged_pass(&pool, pv_a, scn);
    if(res != RES_OK) goto cleanup;
    compact_active_paths(&pool, pv_a);
    log_info(scn->dev,
      "O11_STARTUP A post-merge: active=%llu need_ray=%llu done=%llu rays=%llu\n",
      (unsigned long long)pv_a->active_compact,
      (unsigned long long)pv_a->need_ray_count,
      (unsigned long long)pv_a->done_count,
      (unsigned long long)pv_a->ray_count);
    { size_t rc = 0;
      res = refill_pool(&pool, pv_a, &rc);
      if(res != RES_OK) goto cleanup; }
#ifdef SDIS_DEBUG_CHECKS
    assert_no_pending_result_before_dispatch(&pool, pv_a);
#endif
    res = gpu_launch_all(&pool, pv_a, scn->s3d_view);
    if(res != RES_OK) goto cleanup;

    /* ---- O11 Startup: merged_pass(B) ---- */
    compact_active_paths(&pool, pv_b);
    log_info(scn->dev,
      "O11_STARTUP B pre-merge: active=%llu need_ray=%llu done=%llu\n",
      (unsigned long long)pv_b->active_compact,
      (unsigned long long)pv_b->need_ray_count,
      (unsigned long long)pv_b->done_count);
    res = merged_pass(&pool, pv_b, scn);
    if(res != RES_OK) goto cleanup;
    compact_active_paths(&pool, pv_b);
    log_info(scn->dev,
      "O11_STARTUP B post-merge: active=%llu need_ray=%llu done=%llu rays=%llu\n",
      (unsigned long long)pv_b->active_compact,
      (unsigned long long)pv_b->need_ray_count,
      (unsigned long long)pv_b->done_count,
      (unsigned long long)pv_b->ray_count);
    { size_t rc = 0;
      res = refill_pool(&pool, pv_b, &rc);
      if(res != RES_OK) goto cleanup; }
    /* B's enc/cp requests pending — will be launched in first gpu_launch_all(B) */

    while(pool.active_count > 0 || pool.task_next < pool.task_count) {
      struct time t_pl0, t_pl1, t_hk;
      /* O11 timeline: 11 timestamps spanning one full A+B cycle
       * [0]=start
       * [1]=syncKernA [2]=startD2hA [3]=launchB [4]=waitD2hA [5]=cpuA(merged)
       * [6]=syncKernB [7]=startD2hB [8]=launchA [9]=waitD2hB [10]=cpuB(merged)
       */
      struct time t_cy[11];

      /* ════════ Phase 1: syncK(A) → d2h↓(A) → launch(B) → waitD2h(A) → O11 merged(A) ════════ */
      time_current(&t_cy[0]);

      /* 1a. Sync compute kernels: RT + enc + cp (no D2H yet) */
      res = gpu_sync_kernel_all(&pool, pv_a);
      if(res != RES_OK) goto cleanup;
      time_current(&t_cy[1]);  /* after syncKernel(A) */

      /* 1b. Start async D2H for RT + enc + cp — returns immediately */
      res = gpu_start_d2h_all(&pool, pv_a);
      if(res != RES_OK) goto cleanup;
      time_current(&t_cy[2]);  /* after startD2h(A) */

      /* 1c. Launch GPU(B) RT+enc+cp — overlap with D2H(A) */
#ifdef SDIS_DEBUG_CHECKS
      assert_no_pending_result_before_dispatch(&pool, pv_b);
#endif
      res = gpu_launch_all(&pool, pv_b, scn->s3d_view);
      if(res != RES_OK) goto cleanup;
      time_current(&t_cy[3]);  /* after launch(B) */

      /* Accumulate Phase 1 untimed GPU ops */
      pool.time_gpu_sync_s   += time_elapsed_sec(&t_cy[0], &t_cy[1]);
      pool.time_gpu_launch_s += time_elapsed_sec(&t_cy[1], &t_cy[3]);

      /* 1d. Wait D2H(A) for RT + enc + cp + distribute enc/cp results */
      time_current(&t_pl0);
      res = gpu_wait_d2h_all(&pool, pv_a, scn->s3d_view);
      if(res != RES_OK) goto cleanup;
      time_current(&t_pl1);
      pool.time_pipeline_wait_s += time_elapsed_sec(&t_pl0, &t_pl1);
      t_cy[4] = t_pl1;  /* after waitD2h(A) */

      pool.total_steps++;  /* view A completed one step */

      /* 1e. O11 merged_pass on A (distribute+cascade+collect+harvest) */
      time_current(&t_pl0);
      { struct time t_mp0, t_mp1;
        time_current(&t_mp0);
        res = merged_pass(&pool, pv_a, scn);
        if(res != RES_OK) goto cleanup;
        time_current(&t_mp1);
        pool.time_cascade_s += time_elapsed_sec(&t_mp0, &t_mp1);
      }
      { struct time t_cr0, t_cr1;
        time_current(&t_cr0);
        compact_active_paths(&pool, pv_a);
        { size_t rc = 0;
          res = refill_pool(&pool, pv_a, &rc);
          if(res != RES_OK) goto cleanup;
          if(rc > 0) compact_active_paths(&pool, pv_a); }
        time_current(&t_cr1);
        pool.time_harvest_s += time_elapsed_sec(&t_cr0, &t_cr1);
      }
      time_current(&t_pl1);
      pool.time_pipeline_cpu_between_s += time_elapsed_sec(&t_pl0, &t_pl1);
      t_cy[5] = t_pl1;  /* after merged(A) */

      /* -- Housekeeping after A's step (timed) -- */
      time_current(&t_hk);
      pool_update_active_count(&pool);

      if(!pool.in_drain_phase && pool.task_next >= pool.task_count) {
        struct time t_now;
        pool.in_drain_phase = 1;
        time_current(&t_now);
        pool.time_refill_phase_s = time_elapsed_sec(&t_start, &t_now);
        log_info(scn->dev,
          "persistent_wavefront: entering drain phase at step %llu, "
          "%llu active paths remain, refill_wall=%.3fs\n",
          (unsigned long long)pool.total_steps,
          (unsigned long long)pool.active_count,
          pool.time_refill_phase_s);
      }
      pool_update_diagnostics(&pool, pv_a);
      {
        size_t tasks_done = pool.paths_completed + pool.paths_failed;
        int pcent = (int)((double)tasks_done * 100.0
                        / (double)total_tasks + 0.5);
        if(pcent > 100) pcent = 100;
        if(pcent / pcent_progress > last_pcent / pcent_progress) {
          last_pcent = pcent;
          if(progress) {
            progress[0] = pcent;
            print_progress_update(scn->dev, progress, progress_label);
          }
        }
      }
      if(pool.total_steps % 1000 == 1
      || (pool.in_drain_phase && pool.drain_step_count % 500 == 0
       && pool.active_count > 0)) {
        log_info(scn->dev,
          "persistent_wavefront pipeline step %llu [A]: %llu/%llu active, "
          "%llu rays (rad=%llu ds=%llu shd=%llu enc=%llu st=%llu), "
          "%llu/%llu tasks done, %s\n",
          (unsigned long long)pool.total_steps,
          (unsigned long long)pool.active_count,
          (unsigned long long)pool.pool_size,
          (unsigned long long)pv_a->ray_count,
          (unsigned long long)pv_a->bucket_counts[RAY_BUCKET_RADIATIVE],
          (unsigned long long)pv_a->bucket_counts[RAY_BUCKET_STEP_PAIR],
          (unsigned long long)pv_a->bucket_counts[RAY_BUCKET_SHADOW],
          (unsigned long long)pv_a->bucket_counts[RAY_BUCKET_ENCLOSURE],
          (unsigned long long)pv_a->bucket_counts[RAY_BUCKET_STARTUP],
          (unsigned long long)(pool.paths_completed + pool.paths_failed),
          (unsigned long long)total_tasks,
          pool.in_drain_phase ? "DRAIN" : "refill");
      }
      { /* STARDIS_PIPELINE_LOG */
        const char* env_log = getenv("STARDIS_PIPELINE_LOG");
        if(env_log && env_log[0] == '1') {
          log_info(scn->dev,
            "[A] step %llu: rays=%llu, active=%llu\n",
            (unsigned long long)pool.total_steps,
            (unsigned long long)pv_a->ray_count,
            (unsigned long long)pv_a->active_compact);
        }
      }
      { struct time t_hk_end; time_current(&t_hk_end);
        pool.time_housekeeping_s += time_elapsed_sec(&t_hk, &t_hk_end);
        t_hk = t_hk_end; } /* t_hk now = end of housekeeping A */

      /* ════════ Phase 2: syncK(B) → d2h↓(B) → launch(A) → waitD2h(B) → O11 merged(B) ════════ */

      /* 2a. Sync compute kernels: RT + enc + cp (no D2H yet) */
      res = gpu_sync_kernel_all(&pool, pv_b);
      if(res != RES_OK) goto cleanup;
      time_current(&t_cy[6]);  /* after syncKernel(B) */

      /* 2b. Start async D2H for RT + enc + cp — returns immediately */
      res = gpu_start_d2h_all(&pool, pv_b);
      if(res != RES_OK) goto cleanup;
      time_current(&t_cy[7]);  /* after startD2h(B) */

      /* 2c. Launch GPU(A) RT+enc+cp — overlap with D2H(B) */
#ifdef SDIS_DEBUG_CHECKS
      assert_no_pending_result_before_dispatch(&pool, pv_a);
#endif
      res = gpu_launch_all(&pool, pv_a, scn->s3d_view);
      if(res != RES_OK) goto cleanup;
      time_current(&t_cy[8]);  /* after launch(A) */

      /* Accumulate Phase 2 untimed GPU ops (t_hk = end of housekeeping A) */
      pool.time_gpu_sync_s   += time_elapsed_sec(&t_hk, &t_cy[6]);
      pool.time_gpu_launch_s += time_elapsed_sec(&t_cy[6], &t_cy[8]);

      /* 2d. Wait D2H(B) for RT + enc + cp + distribute enc/cp results */
      time_current(&t_pl0);
      res = gpu_wait_d2h_all(&pool, pv_b, scn->s3d_view);
      if(res != RES_OK) goto cleanup;
      time_current(&t_pl1);
      pool.time_pipeline_wait_s += time_elapsed_sec(&t_pl0, &t_pl1);
      t_cy[9] = t_pl1;  /* after waitD2h(B) */

      pool.total_steps++;  /* view B completed one step */

      /* 2e. O11 merged_pass on B */
      time_current(&t_pl0);
      { struct time t_mp0, t_mp1;
        time_current(&t_mp0);
        res = merged_pass(&pool, pv_b, scn);
        if(res != RES_OK) goto cleanup;
        time_current(&t_mp1);
        pool.time_cascade_s += time_elapsed_sec(&t_mp0, &t_mp1);
      }
      { struct time t_cr0, t_cr1;
        time_current(&t_cr0);
        compact_active_paths(&pool, pv_b);
        { size_t rc = 0;
          res = refill_pool(&pool, pv_b, &rc);
          if(res != RES_OK) goto cleanup;
          if(rc > 0) compact_active_paths(&pool, pv_b); }
        time_current(&t_cr1);
        pool.time_harvest_s += time_elapsed_sec(&t_cr0, &t_cr1);
      }
      time_current(&t_pl1);
      pool.time_pipeline_cpu_between_s += time_elapsed_sec(&t_pl0, &t_pl1);
      t_cy[10] = t_pl1;  /* after merged(B) */

      /* -- Housekeeping after B's step (timed) -- */
      time_current(&t_hk);
      pool_update_active_count(&pool);

      if(!pool.in_drain_phase && pool.task_next >= pool.task_count) {
        struct time t_now;
        pool.in_drain_phase = 1;
        time_current(&t_now);
        pool.time_refill_phase_s = time_elapsed_sec(&t_start, &t_now);
        log_info(scn->dev,
          "persistent_wavefront: entering drain phase at step %llu, "
          "%llu active paths remain, refill_wall=%.3fs\n",
          (unsigned long long)pool.total_steps,
          (unsigned long long)pool.active_count,
          pool.time_refill_phase_s);
      }
      pool_update_diagnostics(&pool, pv_b);
      {
        size_t tasks_done = pool.paths_completed + pool.paths_failed;
        int pcent = (int)((double)tasks_done * 100.0
                        / (double)total_tasks + 0.5);
        if(pcent > 100) pcent = 100;
        if(pcent / pcent_progress > last_pcent / pcent_progress) {
          last_pcent = pcent;
          if(progress) {
            progress[0] = pcent;
            print_progress_update(scn->dev, progress, progress_label);
          }
        }
      }
      if(pool.total_steps % 1000 == 0
      || (pool.in_drain_phase && pool.drain_step_count % 500 == 0
       && pool.active_count > 0)) {
        log_info(scn->dev,
          "persistent_wavefront pipeline step %llu [B]: %llu/%llu active, "
          "%llu rays (rad=%llu ds=%llu shd=%llu enc=%llu st=%llu), "
          "%llu/%llu tasks done, %s\n",
          (unsigned long long)pool.total_steps,
          (unsigned long long)pool.active_count,
          (unsigned long long)pool.pool_size,
          (unsigned long long)pv_b->ray_count,
          (unsigned long long)pv_b->bucket_counts[RAY_BUCKET_RADIATIVE],
          (unsigned long long)pv_b->bucket_counts[RAY_BUCKET_STEP_PAIR],
          (unsigned long long)pv_b->bucket_counts[RAY_BUCKET_SHADOW],
          (unsigned long long)pv_b->bucket_counts[RAY_BUCKET_ENCLOSURE],
          (unsigned long long)pv_b->bucket_counts[RAY_BUCKET_STARTUP],
          (unsigned long long)(pool.paths_completed + pool.paths_failed),
          (unsigned long long)total_tasks,
          pool.in_drain_phase ? "DRAIN" : "refill");
      }
      { /* STARDIS_PIPELINE_LOG */
        const char* env_log = getenv("STARDIS_PIPELINE_LOG");
        if(env_log && env_log[0] == '1') {
          log_info(scn->dev,
            "[B] step %llu: rays=%llu, active=%llu\n",
            (unsigned long long)pool.total_steps,
            (unsigned long long)pv_b->ray_count,
            (unsigned long long)pv_b->active_compact);
        }
      }
      /* ════════ O11 Timeline output (STARDIS_PIPELINE_LOG=2) ════════ */
      {
        const char* env_tl = getenv("STARDIS_PIPELINE_LOG");
        env_tl="2";
        if(env_tl && env_tl[0] == '2' && pool.total_steps % 1000 == 0) {
          /* Relative durations (ms) */
          double syncKA   = time_elapsed_sec(&t_cy[0],  &t_cy[1])  * 1000.0;
          double startDA  = time_elapsed_sec(&t_cy[1],  &t_cy[2])  * 1000.0;
          double launchB  = time_elapsed_sec(&t_cy[2],  &t_cy[3])  * 1000.0;
          double waitDA   = time_elapsed_sec(&t_cy[3],  &t_cy[4])  * 1000.0;
          double cpuA     = time_elapsed_sec(&t_cy[4],  &t_cy[5])  * 1000.0;
          double syncKB   = time_elapsed_sec(&t_cy[5],  &t_cy[6])  * 1000.0;
          double startDB  = time_elapsed_sec(&t_cy[6],  &t_cy[7])  * 1000.0;
          double launchA  = time_elapsed_sec(&t_cy[7],  &t_cy[8])  * 1000.0;
          double waitDB   = time_elapsed_sec(&t_cy[8],  &t_cy[9])  * 1000.0;
          double cpuB     = time_elapsed_sec(&t_cy[9],  &t_cy[10]) * 1000.0;
          double cycle    = time_elapsed_sec(&t_cy[0],  &t_cy[10]) * 1000.0;
          
          /* Absolute timestamps from program start (ms) */
          double t0  = time_elapsed_sec(&t_start, &t_cy[0])  * 1000.0;
          double t1  = time_elapsed_sec(&t_start, &t_cy[1])  * 1000.0;
          double t2  = time_elapsed_sec(&t_start, &t_cy[2])  * 1000.0;
          double t3  = time_elapsed_sec(&t_start, &t_cy[3])  * 1000.0;
          double t4  = time_elapsed_sec(&t_start, &t_cy[4])  * 1000.0;
          double t5  = time_elapsed_sec(&t_start, &t_cy[5])  * 1000.0;
          double t6  = time_elapsed_sec(&t_start, &t_cy[6])  * 1000.0;
          double t7  = time_elapsed_sec(&t_start, &t_cy[7])  * 1000.0;
          double t8  = time_elapsed_sec(&t_start, &t_cy[8])  * 1000.0;
          double t9  = time_elapsed_sec(&t_start, &t_cy[9])  * 1000.0;
          double t10 = time_elapsed_sec(&t_start, &t_cy[10]) * 1000.0;
          
          log_info(scn->dev,
            "[TIMELINE] step=%llu "
            "|syncKA=%.2f|startDA=%.2f|launchB=%.2f|waitDA=%.2f|cpuA=%.2f"
            "|syncKB=%.2f|startDB=%.2f|launchA=%.2f|waitDB=%.2f|cpuB=%.2f"
            "|cycle=%.2fms raysA=%llu raysB=%llu\n",
            (unsigned long long)pool.total_steps,
            syncKA, startDA, launchB, waitDA, cpuA,
            syncKB, startDB, launchA, waitDB, cpuB,
            cycle,
            (unsigned long long)pv_a->ray_count,
            (unsigned long long)pv_b->ray_count);
          
          log_info(scn->dev,
            "[TIMELINE_TS] step=%llu "
            "|t0=%.2f|t1=%.2f|t2=%.2f|t3=%.2f|t4=%.2f|t5=%.2f"
            "|t6=%.2f|t7=%.2f|t8=%.2f|t9=%.2f|t10=%.2f\n",
            (unsigned long long)pool.total_steps,
            t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10);

          /* PCIe overlap proof: GPU-side transfer timing via CUDA events.
           * Phase 1: startD2H(A) then launch(B) → D2H(A) vs H2D(B)
           * Phase 2: startD2H(B) then launch(A) → D2H(B) vs H2D(A) */
          {
            float p1_d2h, p1_h2d, p1_offset, p1_overlap;
            float p2_d2h, p2_h2d, p2_offset, p2_overlap;
            s3d_batch_trace_context_pcie_overlap_query(
              pv_a->batch_ctx, pv_b->batch_ctx,
              &p1_d2h, &p1_h2d, &p1_offset, &p1_overlap);
            s3d_batch_trace_context_pcie_overlap_query(
              pv_b->batch_ctx, pv_a->batch_ctx,
              &p2_d2h, &p2_h2d, &p2_offset, &p2_overlap);
            log_info(scn->dev,
              "[PCIE_OVERLAP] step=%llu "
              "phase1(D2H_A→H2D_B): d2h=%.4f h2d=%.4f offset=%.4f overlap=%.4fms "
              "phase2(D2H_B→H2D_A): d2h=%.4f h2d=%.4f offset=%.4f overlap=%.4fms\n",
              (unsigned long long)pool.total_steps,
              p1_d2h, p1_h2d, p1_offset, p1_overlap,
              p2_d2h, p2_h2d, p2_offset, p2_overlap);
          }
        }
      }

      /* ════════ Dynamic merge check ════════ */
      if(should_merge(&pool)) {
        /* Wait for all pending GPU calls (RT + enc + cp) */
        if(pv_a->gpu_pending || pv_a->enc_gpu_pending || pv_a->cp_gpu_pending) {
          res = gpu_wait_download_all(&pool, pv_a, scn->s3d_view);
          if(res != RES_OK) goto cleanup;
        }
        if(pv_b->gpu_pending || pv_b->enc_gpu_pending || pv_b->cp_gpu_pending) {
          res = gpu_wait_download_all(&pool, pv_b, scn->s3d_view);
          if(res != RES_OK) goto cleanup;
        }
        log_info(scn->dev,
          "pipeline: merging to single-pool at step %llu "
          "(A.active=%llu, B.active=%llu)\n",
          (unsigned long long)pool.total_steps,
          (unsigned long long)pv_a->active_compact,
          (unsigned long long)pv_b->active_compact);
        merge_to_single_pool(&pool);
        { struct time t_hk_end; time_current(&t_hk_end);
          pool.time_housekeeping_s += time_elapsed_sec(&t_hk, &t_hk_end); }
        break;  /* fall through to single-pool loop */
      }
      { struct time t_hk_end; time_current(&t_hk_end);
        pool.time_housekeeping_s += time_elapsed_sec(&t_hk, &t_hk_end); }
    } /* end dual-pool while */

    /* ---- Drain: wait for last pending GPU calls (RT + enc + cp) ---- */
    if(pool.num_active_views == 2) {
      if(pv_a->gpu_pending || pv_a->enc_gpu_pending || pv_a->cp_gpu_pending) {
        res = gpu_wait_download_all(&pool, pv_a, scn->s3d_view);
        if(res != RES_OK) goto cleanup;
        pool.total_steps++;
        res = merged_pass(&pool, pv_a, scn);
        if(res != RES_OK) goto cleanup;
        compact_active_paths(&pool, pv_a);
      }
      if(pv_b->gpu_pending || pv_b->enc_gpu_pending || pv_b->cp_gpu_pending) {
        res = gpu_wait_download_all(&pool, pv_b, scn->s3d_view);
        if(res != RES_OK) goto cleanup;
        pool.total_steps++;
        res = merged_pass(&pool, pv_b, scn);
        if(res != RES_OK) goto cleanup;
        compact_active_paths(&pool, pv_b);
      }
    }

    /* Log pipeline stats */
    {
      struct time t_now;
      double pipeline_wall;
      time_current(&t_now);
      pipeline_wall = time_elapsed_sec(&t_start, &t_now);
      if(pipeline_wall <= 0) pipeline_wall = 1e-9;
      log_info(scn->dev,
        "pipeline stats: wall=%.3fs, gpu_wait=%.3fs, cpu_between=%.3fs, "
        "gpu_util=%.1f%%, steps=%llu\n",
        pipeline_wall,
        pool.time_pipeline_wait_s,
        pool.time_pipeline_cpu_between_s,
        100.0 * (1.0 - pool.time_pipeline_gpu_idle_s / pipeline_wall),
        (unsigned long long)pool.total_steps);
    }
  } /* end if(num_active_views == 2) */

  /* ═══════════════════════════════════════════════════════════ */
  /*  Single-pool main loop (num_active_views == 1)              */
  /*  - STARDIS_PIPELINE=0 enters directly                      */
  /*  - Or merge_to_single_pool above falls through              */
  /* ═══════════════════════════════════════════════════════════ */
  if(pool.num_active_views == 1) {

  while(pool.active_count > 0 || pool.task_next < pool.task_count) {
    size_t refill_count = 0;
    struct time t_phase0, t_phase1; /* M3 per-phase timing */
    struct pool_view* pv = &pool.views[0]; /* single-pool view */
    pool.total_steps++;

    /* O11: merged_pass (distribute+cascade+collect+harvest in one scan) */
    time_current(&t_phase0);
    res = merged_pass(&pool, pv, scn);
    if(res != RES_OK) goto cleanup;
    time_current(&t_phase1);
    pool.time_cascade_s += time_elapsed_sec(&t_phase0, &t_phase1);
    pool.time_pipeline_cpu_between_s += time_elapsed_sec(&t_phase0, &t_phase1);

    /* Compact + refill */
    time_current(&t_phase0);
    compact_active_paths(&pool, pv);
    res = refill_pool(&pool, pv, &refill_count);
    if(res != RES_OK) goto cleanup;
    time_current(&t_phase1);
    pool.time_harvest_s += time_elapsed_sec(&t_phase0, &t_phase1);

    /* GPU trace RT+enc+cp (sync in single-pool camera mode) */
    time_current(&t_phase0);
#ifdef SDIS_DEBUG_CHECKS
    assert_no_pending_result_before_dispatch(&pool, pv);
#endif
    res = gpu_launch_all(&pool, pv, scn->s3d_view);
    if(res != RES_OK) goto cleanup;
    res = gpu_wait_download_all(&pool, pv, scn->s3d_view);
    if(res != RES_OK) goto cleanup;
    time_current(&t_phase1);
    pool.time_trace_s += time_elapsed_sec(&t_phase0, &t_phase1);

    /* Steps H-L: Housekeeping (update_active, drain detect, diagnostics,
     * progress reporting, periodic logging, safety check) */
    time_current(&t_phase0);

    /* Step H: Update active count */
    pool_update_active_count(&pool);

    /* Step I: Detect drain phase transition */
    if(!pool.in_drain_phase && pool.task_next >= pool.task_count) {
      pool.in_drain_phase = 1;
      /* Record refill phase total wall-clock */
      {
        struct time t_now;
        time_current(&t_now);
        pool.time_refill_phase_s = time_elapsed_sec(&t_start, &t_now);
      }
      log_info(scn->dev,
        "persistent_wavefront: entering drain phase at step %llu, "
        "%llu active paths remain, refill_wall=%.3fs\n",
        (unsigned long long)pool.total_steps,
        (unsigned long long)pool.active_count,
        pool.time_refill_phase_s);
    }

    /* Step J: Update diagnostics (M2.5 + M3 wavefront width) */
    pool_update_diagnostics(&pool, pv);

    /* Step K: Progress reporting */
    {
      size_t tasks_done = pool.paths_completed + pool.paths_failed;
      int pcent = (int)((double)tasks_done * 100.0
                      / (double)total_tasks + 0.5);
      if(pcent > 100) pcent = 100;

      if(pcent / pcent_progress > last_pcent / pcent_progress) {
        last_pcent = pcent;
        if(progress) {
          progress[0] = pcent;
          print_progress_update(scn->dev, progress, progress_label);
        }
      }
    }

    /* Step L: Periodic status logging */
    if(pool.total_steps % 1000 == 0
    || (pool.in_drain_phase && pool.drain_step_count % 500 == 0
     && pool.active_count > 0)) {
      log_info(scn->dev,
        "persistent_wavefront step %llu: %llu/%llu active, "
        "%llu rays this step (rad=%llu ds=%llu shd=%llu enc=%llu st=%llu), "
        "%llu/%llu tasks done, %s\n",
        (unsigned long long)pool.total_steps,
        (unsigned long long)pool.active_count,
        (unsigned long long)pool.pool_size,
        (unsigned long long)pv->ray_count,
        (unsigned long long)pv->bucket_counts[RAY_BUCKET_RADIATIVE],
        (unsigned long long)pv->bucket_counts[RAY_BUCKET_STEP_PAIR],
        (unsigned long long)pv->bucket_counts[RAY_BUCKET_SHADOW],
        (unsigned long long)pv->bucket_counts[RAY_BUCKET_ENCLOSURE],
        (unsigned long long)pv->bucket_counts[RAY_BUCKET_STARTUP],
        (unsigned long long)(pool.paths_completed + pool.paths_failed),
        (unsigned long long)total_tasks,
        pool.in_drain_phase ? "DRAIN" : "refill");
    }



    time_current(&t_phase1);
    pool.time_housekeeping_s += time_elapsed_sec(&t_phase0, &t_phase1);
  } /* end single-pool while */
  } /* end if(pool.num_active_views == 1) */

  /* ====== 8. Summary logging ====== */
  time_current(&t_end);
  time_sub(&t_elapsed, &t_end, &t_start);

  /* Record drain phase wall-clock (total - refill phase) */
  if(pool.in_drain_phase) {
    double total_wall = time_elapsed_sec(&t_start, &t_end);
    pool.time_drain_phase_s = total_wall - pool.time_refill_phase_s;
  }

  {
    double avg_width = 0;
    char time_str[128];
    time_dump(&t_elapsed, TIME_ALL, NULL, time_str, sizeof(time_str));
    if(pool.total_steps > 0)
      avg_width = (double)pool.diag_total_active / (double)pool.total_steps;
    log_info(scn->dev,
      "persistent_wavefront DONE: %llux%llu spp=%llu pool=%llu "
      "elapsed=%s  steps=%llu  rays=%llu  avg_width=%.1f  "
      "(rad=%llu cond_ds=%llu(retry=%llu) shadow=%llu enc=%llu "
      "startup=%llu other=%llu)  "
      "done: rad=%llu temp=%llu bnd=%llu fail=%llu  "
      "max_depth=%llu\n",
      (unsigned long long)image_def[0], (unsigned long long)image_def[1],
      (unsigned long long)spp, (unsigned long long)pool.pool_size,
      time_str,
      (unsigned long long)pool.total_steps,
      (unsigned long long)pool.total_rays_traced,
      avg_width,
      (unsigned long long)pool.rays_radiative,
      (unsigned long long)pool.rays_conductive_ds,
      (unsigned long long)pool.rays_conductive_ds_retry,
      (unsigned long long)pool.rays_shadow,
      (unsigned long long)pool.rays_enclosure,
      (unsigned long long)pool.rays_startup,
      (unsigned long long)pool.rays_other,
      (unsigned long long)pool.paths_done_radiative,
      (unsigned long long)pool.paths_done_temperature,
      (unsigned long long)pool.paths_done_boundary,
      (unsigned long long)pool.paths_failed,
      (unsigned long long)pool.max_path_depth);
  }

  /* M9: WoS closest_point batch summary */
  if(pool.cp_total > 0) {
    log_info(scn->dev,
      "persistent_wavefront CP (WoS): %llu queries, "
      "%llu gpu-accepted, %llu requeried, %.3fs total\n",
      (unsigned long long)pool.cp_total,
      (unsigned long long)pool.cp_accepted,
      (unsigned long long)pool.cp_requeried,
      pool.time_cp_s);
  }

  log_drain_phase_report(scn->dev, &pool);

cleanup:
  /* Flush and close pixel trace file */
  if(s_pixel_trace_fp) { fclose(s_pixel_trace_fp); s_pixel_trace_fp = NULL; s_pixel_trace_checked = 0; }

  pool_destroy(&pool);
  return res;
}

/*******************************************************************************
 * solve_persistent_wavefront_probe — probe temperature estimation using the
 * persistent wavefront pool.
 *
 * Same main loop as camera mode but:
 *   - Uses wf_ops_probe vtable (linear tasks, no camera ray, accum result).
 *   - Single-buffer mode only (nrealisations typically small).
 *   - No progress reporting.
 ******************************************************************************/
res_T
solve_persistent_wavefront_probe(
  struct sdis_scene*                   scn,
  struct ssp_rng*                      base_rng,
  const unsigned                       enc_id,
  const double                         position[3],
  const double                         time_range[2],
  const size_t                         nrealisations,
  const size_t                         picard_order,
  const enum sdis_diffusion_algorithm  diff_algo,
  struct accum*                        out_acc_temp)
{
  struct wavefront_pool pool;
  struct probe_mode_ctx probe_ctx;
  size_t total_tasks = nrealisations;
  size_t pool_size;
  res_T res = RES_OK;
  struct time t_start, t_end;

  ASSERT(scn && base_rng && position && time_range && nrealisations > 0);
  ASSERT(out_acc_temp);

  *out_acc_temp = ACCUM_NULL;

  /* Resolve medium type from enclosure */
  {
    const struct enclosure* enc = scene_get_enclosure(scn, enc_id);
    struct sdis_medium* mdm = NULL;
    res = scene_get_enclosure_medium(scn, enc, &mdm);
    if(res != RES_OK) return res;
    probe_ctx.mdm_type = sdis_medium_get_type(mdm);
  }
  d3_set(probe_ctx.position, position);
  probe_ctx.nrealisations = nrealisations;

  pool_size = determine_pool_size(total_tasks, scn);
  /* ====== 1. Allocate pool (self-adaptive dual/single) ====== */
  res = pool_create(&pool, pool_size, total_tasks);
  if(res != RES_OK) {
    log_err(scn->dev,
      "persistent_wavefront_probe: pool_create failed for %llu paths -- %s\n",
      (unsigned long long)pool_size, res_to_cstr(res));
    return res;
  }

  /* Wire probe vtable + context */
  pool.ops       = &wf_ops_probe;
  pool.mode_ctx  = &probe_ctx;
  pool.result_ctx = out_acc_temp;

  /* ====== 2. Create per-path CBRNG thin wrappers ====== */
  {
    struct ssp_rng* thin_storage = NULL;
    struct wf_rng** rng_ptrs = (struct wf_rng**)malloc(
        pool.pool_size * sizeof(struct wf_rng*));
    if(!rng_ptrs) {
      res = RES_MEM_ERR;
      goto cleanup;
    }
    { size_t ri;
      for(ri = 0; ri < pool.pool_size; ri++)
        rng_ptrs[ri] = &pool.slots[ri].rng_state;
    }
    res = wf_rng_create_thin_ssp_rngs(pool.pool_size, rng_ptrs,
                                       pool.slot_rngs, &thin_storage);
    free(rng_ptrs);
    if(res != RES_OK) goto cleanup;
    pool.thin_rng_storage = thin_storage;
  }

  /* Global seed from base_rng */
  {
    double r0 = ssp_rng_canonical(base_rng);
    double r1 = ssp_rng_canonical(base_rng);
    uint32_t hi = (uint32_t)(r0 * 4294967296.0);
    uint32_t lo = (uint32_t)(r1 * 4294967296.0);
    pool.global_seed = ((uint64_t)hi << 32) | (uint64_t)lo;
  }

  /* ====== 3. Generate task queue ====== */
  res = pool.ops->generate_tasks(&pool, pool.mode_ctx);
  if(res != RES_OK) goto cleanup;

  /* ====== 4. Create batch contexts ====== */
  res = s3d_batch_trace_context_create(&pool.batch_ctx, pool.max_rays);
  if(res != RES_OK) goto cleanup;
  res = s3d_batch_enc_context_create(&pool.enc_batch_ctx, pool.max_enc_locates);
  if(res != RES_OK) goto cleanup;
  res = s3d_batch_cp_context_create(&pool.cp_batch_ctx, pool.max_cps);
  if(res != RES_OK) goto cleanup;

  /* ====== 5. Cache scene params ====== */
  pool.scn          = scn;
  pool.enc_id       = enc_id;
  pool.time_range   = time_range;
  pool.picard_order = picard_order;
  pool.diff_algo    = diff_algo;

  /* L4: GPU inline filter (Mode A) — see camera solver for docs */
  pool.use_gpu_filter = 1;

  /* ====== 6. Fill pool ====== */
  res = fill_pool(&pool);
  if(res != RES_OK) goto cleanup;

  log_info(scn->dev,
    "persistent_wavefront_probe: %llu paths, pool=%llu, enc_id=%u\n",
    (unsigned long long)nrealisations,
    (unsigned long long)pool.pool_size, enc_id);

  /* ====== 7. Main loop (dispatched by pool_run) ====== */
  time_current(&t_start);
  res = pool_run(&pool, scn->s3d_view, scn);
  if(res != RES_OK) goto cleanup;
  time_current(&t_end);

  log_info(scn->dev,
    "persistent_wavefront_probe: done, steps=%llu, rays=%llu, "
    "accum count=%llu, wall=%.3fs\n",
    (unsigned long long)pool.total_steps,
    (unsigned long long)pool.total_rays_traced,
    (unsigned long long)out_acc_temp->count,
    time_elapsed_sec(&t_start, &t_end));

cleanup:
  pool_destroy(&pool);
  return res;
}

/*******************************************************************************
 * solve_persistent_wavefront_probe_batch — K probes in a single pool.
 *
 * Runs nprobes × nrealisations paths through the persistent wavefront pool,
 * interleaving probe indices for maximum batch diversity.  Each probe's
 * results are accumulated independently in out_acc_temps[probe_idx].
 ******************************************************************************/
res_T
solve_persistent_wavefront_probe_batch(
  struct sdis_scene*                   scn,
  struct ssp_rng*                      base_rng,
  const size_t                         nprobes,
  const unsigned                       enc_ids[],
  const double                         positions[],
  const double                         time_range[2],
  const size_t                         nrealisations,
  const size_t                         picard_order,
  const enum sdis_diffusion_algorithm  diff_algo,
  const enum sdis_medium_type          mdm_types[],
  struct accum*                        out_acc_temps)
{
  struct wavefront_pool pool;
  struct probe_batch_mode_ctx batch_ctx;
  size_t total_tasks;
  size_t pool_size;
  res_T res = RES_OK;
  struct time t_start, t_end;
  size_t pi;

  ASSERT(scn && base_rng && nprobes > 0 && nrealisations > 0);
  ASSERT(positions && enc_ids && mdm_types && out_acc_temps);

  total_tasks = nprobes * nrealisations;

  /* Zero all output accumulators */
  for(pi = 0; pi < nprobes; pi++)
    out_acc_temps[pi] = ACCUM_NULL;

  /* Setup batch context */
  batch_ctx.positions     = positions;
  batch_ctx.enc_ids       = enc_ids;
  batch_ctx.mdm_types     = mdm_types;
  batch_ctx.accums        = out_acc_temps;
  batch_ctx.nprobes       = nprobes;
  batch_ctx.nrealisations = nrealisations;

  pool_size = determine_pool_size(total_tasks, scn);

  /* ====== 1. Allocate pool (self-adaptive dual/single) ====== */
  res = pool_create(&pool, pool_size, total_tasks);
  if(res != RES_OK) {
    log_err(scn->dev,
      "persistent_wavefront_probe_batch: pool_create failed for %llu -- %s\n",
      (unsigned long long)pool_size, res_to_cstr(res));
    return res;
  }

  /* Wire batch vtable + context.
   * result_ctx points to the batch_ctx (not accums directly) so
   * accumulate_result can index by probe_idx. */
  pool.ops       = &wf_ops_probe_batch;
  pool.mode_ctx  = &batch_ctx;
  pool.result_ctx = &batch_ctx;

  /* ====== 2. Create per-path CBRNG thin wrappers ====== */
  {
    struct ssp_rng* thin_storage = NULL;
    struct wf_rng** rng_ptrs = (struct wf_rng**)malloc(
        pool.pool_size * sizeof(struct wf_rng*));
    if(!rng_ptrs) {
      res = RES_MEM_ERR;
      goto cleanup_batch;
    }
    { size_t ri;
      for(ri = 0; ri < pool.pool_size; ri++)
        rng_ptrs[ri] = &pool.slots[ri].rng_state;
    }
    res = wf_rng_create_thin_ssp_rngs(pool.pool_size, rng_ptrs,
                                       pool.slot_rngs, &thin_storage);
    free(rng_ptrs);
    if(res != RES_OK) goto cleanup_batch;
    pool.thin_rng_storage = thin_storage;
  }

  /* Global seed from base_rng */
  {
    double r0 = ssp_rng_canonical(base_rng);
    double r1 = ssp_rng_canonical(base_rng);
    uint32_t hi = (uint32_t)(r0 * 4294967296.0);
    uint32_t lo = (uint32_t)(r1 * 4294967296.0);
    pool.global_seed = ((uint64_t)hi << 32) | (uint64_t)lo;
  }

  /* ====== 3. Generate task queue ====== */
  res = pool.ops->generate_tasks(&pool, pool.mode_ctx);
  if(res != RES_OK) goto cleanup_batch;

  /* ====== 4. Create batch contexts ====== */
  res = s3d_batch_trace_context_create(&pool.batch_ctx, pool.max_rays);
  if(res != RES_OK) goto cleanup_batch;
  res = s3d_batch_enc_context_create(&pool.enc_batch_ctx, pool.max_enc_locates);
  if(res != RES_OK) goto cleanup_batch;
  res = s3d_batch_cp_context_create(&pool.cp_batch_ctx, pool.max_cps);
  if(res != RES_OK) goto cleanup_batch;

  /* ====== 5. Cache scene params ====== */
  pool.scn          = scn;
  pool.enc_id       = 0; /* unused — batch init_path reads per-probe enc_id */
  pool.time_range   = time_range;
  pool.picard_order = picard_order;
  pool.diff_algo    = diff_algo;

  /* L4: GPU inline filter (Mode A) — see camera solver for docs */
  pool.use_gpu_filter = 1;

  /* ====== 6. Fill pool ====== */
  res = fill_pool(&pool);
  if(res != RES_OK) goto cleanup_batch;

  log_info(scn->dev,
    "persistent_wavefront_probe_batch: %llu probes x %llu = %llu paths, "
    "pool=%llu\n",
    (unsigned long long)nprobes,
    (unsigned long long)nrealisations,
    (unsigned long long)total_tasks,
    (unsigned long long)pool.pool_size);

  /* ====== 7. Main loop (dispatched by pool_run) ====== */
  time_current(&t_start);
  res = pool_run(&pool, scn->s3d_view, scn);
  if(res != RES_OK) goto cleanup_batch;
  time_current(&t_end);

  { size_t total_accum = 0;
    for(pi = 0; pi < nprobes; pi++)
      total_accum += out_acc_temps[pi].count;
    log_info(scn->dev,
      "persistent_wavefront_probe_batch: done, steps=%llu, rays=%llu, "
      "total_accum=%llu, wall=%.3fs\n",
      (unsigned long long)pool.total_steps,
      (unsigned long long)pool.total_rays_traced,
      (unsigned long long)total_accum,
      time_elapsed_sec(&t_start, &t_end));
  }

cleanup_batch:
  pool_destroy(&pool);
  return res;
}

/*******************************************************************************
 * Public API: sdis_solve_persistent_wavefront_probe
 *
 * Same as sdis_solve_wavefront_probe but uses the persistent pool main loop
 * (solve_persistent_wavefront_probe) shared with the camera solver.
 *
 * Extracted from sdis_solve_wavefront.c dead code block (F2 fix).
 ******************************************************************************/
res_T
sdis_solve_persistent_wavefront_probe(
  struct sdis_scene*                   scn,
  const struct sdis_solve_probe_args*  args,
  struct sdis_estimator**              out_estimator)
{
  struct ssp_rng* rng = NULL;
  struct sdis_estimator* estimator = NULL;
  unsigned enc_id = ENCLOSURE_ID_NULL;
  struct accum acc_temp = ACCUM_NULL;
  res_T res = RES_OK;

  if(!scn || !args || !out_estimator) return RES_BAD_ARG;
  if(args->nrealisations == 0) return RES_BAD_ARG;

  *out_estimator = NULL;

  /* Retrieve enclosure at probe position */
  res = scene_get_enclosure_id(scn, args->position, &enc_id);
  if(res != RES_OK) goto error_p;

  /* Create RNG */
  if(args->rng_state) {
    rng = args->rng_state;
  } else {
    res = ssp_rng_create(NULL, args->rng_type, &rng);
    if(res != RES_OK) goto error_p;
  }

  /* Create estimator */
  res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
  if(res != RES_OK) goto error_p;

  /* Run persistent wavefront probe solver */
  res = solve_persistent_wavefront_probe(
    scn, rng, enc_id,
    args->position, args->time_range,
    args->nrealisations,
    args->picard_order,
    args->diff_algo,
    &acc_temp);
  if(res != RES_OK) goto error_p;

  /* Setup estimator from accumulator */
  estimator_setup_realisations_count(estimator,
    args->nrealisations, acc_temp.count);
  estimator_setup_temperature(estimator, acc_temp.sum, acc_temp.sum2);

  *out_estimator = estimator;
  goto exit_p;

error_p:
  if(estimator) { sdis_estimator_ref_put(estimator); estimator = NULL; }
exit_p:
  if(rng && !args->rng_state) ssp_rng_ref_put(rng);
  return res;
}

/*******************************************************************************
 * Public API: sdis_solve_persistent_wavefront_probe_batch
 *
 * Multi-probe variant — runs K probes in a single persistent pool pass.
 * Each probe gets its own estimator with independent sum/sum2/count.
 *
 * Extracted from sdis_solve_wavefront.c dead code block (F2 fix).
 ******************************************************************************/
res_T
sdis_solve_persistent_wavefront_probe_batch(
  struct sdis_scene*                   scn,
  const size_t                         nprobes,
  const struct sdis_solve_probe_args*  args_array,
  struct sdis_estimator**              out_estimators)
{
  struct ssp_rng* rng = NULL;
  unsigned* enc_ids = NULL;
  double* positions = NULL;
  enum sdis_medium_type* mdm_types = NULL;
  struct accum* accums = NULL;
  size_t* valid_map = NULL; /* compact index -> original index */
  size_t nvalid = 0;
  res_T res = RES_OK;
  size_t i;

  if(!scn || !args_array || !out_estimators || nprobes == 0)
    return RES_BAD_ARG;

  for(i = 0; i < nprobes; i++)
    out_estimators[i] = NULL;

  /* ---- Allocate per-probe arrays ---- */
  enc_ids   = (unsigned*)malloc(nprobes * sizeof(unsigned));
  positions = (double*)malloc(nprobes * 3 * sizeof(double));
  mdm_types = (enum sdis_medium_type*)malloc(
    nprobes * sizeof(enum sdis_medium_type));
  accums    = (struct accum*)malloc(nprobes * sizeof(struct accum));
  valid_map = (size_t*)malloc(nprobes * sizeof(size_t));

  if(!enc_ids || !positions || !mdm_types || !accums || !valid_map) {
    res = RES_MEM_ERR;
    goto error_batch;
  }

  /* ---- Resolve per-probe enclosures + medium types ---- */
  /* Probes that fail enclosure/medium resolution are silently skipped
   * (estimator stays NULL), matching the serial API where each probe may
   * independently return RES_BAD_OP for fluid/multi-material enclosures.
   * Only truly fatal errors (bad args, OOM) abort the whole batch. */
  for(i = 0; i < nprobes; i++) {
    const struct sdis_solve_probe_args* a = &args_array[i];
    res_T pres; /* per-probe result */
    if(a->nrealisations == 0) { res = RES_BAD_ARG; goto error_batch; }

    pres = scene_get_enclosure_id(scn, a->position, &enc_ids[nvalid]);
    if(pres != RES_OK) continue; /* skip — out_estimators[i] stays NULL */

    { const struct enclosure* enc = scene_get_enclosure(scn, enc_ids[nvalid]);
      struct sdis_medium* mdm = NULL;
      pres = scene_get_enclosure_medium(scn, enc, &mdm);
      if(pres != RES_OK) continue; /* skip — fluid/multi-material enclosure */
      mdm_types[nvalid] = sdis_medium_get_type(mdm);
    }

    positions[nvalid*3+0] = a->position[0];
    positions[nvalid*3+1] = a->position[1];
    positions[nvalid*3+2] = a->position[2];
    valid_map[nvalid] = i;
    nvalid++;
  }

  /* All probes skipped — nothing to solve, return OK with all NULL ests */
  if(nvalid == 0) goto exit_batch;

  /* ---- Create RNG (from first args entry) ---- */
  if(args_array[0].rng_state) {
    rng = args_array[0].rng_state;
  } else {
    res = ssp_rng_create(NULL, args_array[0].rng_type, &rng);
    if(res != RES_OK) goto error_batch;
  }

  /* ---- Create estimators for valid probes only ---- */
  for(i = 0; i < nvalid; i++) {
    res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE,
                           &out_estimators[valid_map[i]]);
    if(res != RES_OK) goto error_batch;
  }

  /* ---- Run batch solver (compact arrays, nvalid probes) ---- */
  res = solve_persistent_wavefront_probe_batch(
    scn, rng, nvalid, enc_ids, positions,
    args_array[0].time_range,
    args_array[0].nrealisations,
    args_array[0].picard_order,
    args_array[0].diff_algo,
    mdm_types, accums);
  if(res != RES_OK) goto error_batch;

  /* ---- Setup estimators from accumulators ---- */
  for(i = 0; i < nvalid; i++) {
    size_t orig = valid_map[i];
    estimator_setup_realisations_count(out_estimators[orig],
      args_array[orig].nrealisations, accums[i].count);
    estimator_setup_temperature(out_estimators[orig],
      accums[i].sum, accums[i].sum2);
  }

  goto exit_batch;

error_batch:
  for(i = 0; i < nprobes; i++) {
    if(out_estimators[i]) {
      sdis_estimator_ref_put(out_estimators[i]);
      out_estimators[i] = NULL;
    }
  }
exit_batch:
  free(enc_ids);
  free(positions);
  free(mdm_types);
  free(accums);
  free(valid_map);
  if(rng && !args_array[0].rng_state) ssp_rng_ref_put(rng);
  return res;
}

#endif /* !SDIS_SOLVE_PERSISTENT_WAVEFRONT_SKIP_PUBLIC_API */
