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

/* Phase B-3: Persistent Wavefront Pool for solve_camera.
 *
 * Instead of per-tile wavefront isolation (Phase B-2), this module pools ALL
 * pixel×SPP tasks from the entire image into a single persistent wavefront.
 * This eliminates the GPU utilisation bottleneck caused by TILE_SIZE=4
 * (only 512 paths/tile → <3% GPU occupancy on RTX 4090).
 *
 * M2: Fixed-size pool with refill — completed paths are immediately replaced
 * with new tasks from the global queue, keeping the wavefront width constant
 * at pool_size until the task queue is exhausted (drain phase).
 *
 * M2.5: Stream compaction + type bucketing — every step rebuilds compact
 * index arrays (active_indices, need_ray_indices, done_indices) to avoid
 * scanning the full pool.  Paths are also bucketed by type (radiative vs
 * conductive) for cache-friendly step dispatch.
 *
 * M3: Bucketed step dispatch + adaptive pool_size + enhanced diagnostics.
 * Ray results are distributed via per-type loops (radiative then conductive)
 * instead of a generic switch dispatch, improving branch prediction and
 * cache locality.  pool_size is computed from GPU SM count.  Per-phase
 * timing and average wavefront width are tracked for performance analysis.
 *
 * Key design points:
 *   - Single-threaded CPU scheduling (no OMP) — one CUDA stream, no races.
 *   - Per-slot RNG shared from solve_camera's per_thread_rng via ref_get().
 *   - All step functions from sdis_solve_wavefront.c are fully reused.
 *   - Results written directly to estimator_buffer (bypass tile layer).
 *
 * Reference: guide/upper-parallelization/phase_b3_persistent_wavefront.md
 */

#ifndef SDIS_SOLVE_PERSISTENT_WAVEFRONT_H
#define SDIS_SOLVE_PERSISTENT_WAVEFRONT_H

#include "sdis.h"
#include "sdis_wf_types.h"  /* path_phase, ray_request */
#include "sdis_wf_state.h"  /* path_state */
#include "sdis_wf_hot.h"    /* P0_OPT: path_hot */

#include <star/s3d.h>
#include <star/ssp.h>
#include <stdint.h>

/* Forward declarations */
struct sdis_estimator_buffer;
struct sdis_scene;
struct sdis_camera;
struct ssp_rng;

/*******************************************************************************
 * pixel_task — one pixel×realisation work item
 ******************************************************************************/
struct pixel_task {
  size_t   ipix_image[2];  /* image-space pixel coordinates */
  uint32_t spp_idx;        /* realisation index (0..spp-1)  */
};

/*******************************************************************************
 * Default pool size — tuned for RTX 4090 (128 SM × 256 threads/SM).
 * Overridable via STARDIS_POOL_SIZE environment variable.
 ******************************************************************************/
#define PERSISTENT_WF_DEFAULT_POOL_SIZE  32768

/*******************************************************************************
 * wavefront_ops — mode-specific callbacks (camera vs probe)
 *
 * The persistent wavefront main loop is mode-agnostic.  These three callbacks
 * isolate the only mode-specific operations:
 *   1. generate_tasks — populate the task queue
 *   2. init_path     — initialise a single path from a task
 *   3. harvest       — collect completed path results
 *
 * Two static instances (wf_ops_camera, wf_ops_probe) are defined in the .c
 * file.  The main loop dispatches through pool->ops->xxx().
 ******************************************************************************/
struct wavefront_pool;  /* forward */
struct pool_view;       /* forward */

struct wavefront_ops {
  /* Populate pool->task_queue from mode_ctx parameters.
   * Camera: Morton-order W×H×spp tasks.
   * Probe:  nrealisations linear tasks at (0,0). */
  res_T (*generate_tasks)(struct wavefront_pool* pool,
                          const void* mode_ctx);

  /* Initialise a single path in slot from a pixel_task.
   * Camera: camera_ray + PATH_INIT.
   * Probe:  position + PATH_COUPLED_CONDUCTIVE/CONVECTIVE.
   * P0_OPT: hot receives the 5 dispatch-hot fields (phase, active, ...). */
  res_T (*init_path)(struct path_state* p,
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
                     uint64_t global_seed);

  /* Accumulate one completed path's result into result_ctx.
   * Camera: estimator_buffer_grab(buf, ipix[0], ipix[1]) + sum/sum2.
   * Probe:  accum->sum += T.value, accum->count++. */
  void (*accumulate_result)(const struct path_state* p,
                            void* result_ctx);
};

/*******************************************************************************
 * camera_mode_ctx — parameters specific to camera rendering
 ******************************************************************************/
struct camera_mode_ctx {
  const struct sdis_camera* cam;
  const double*             pix_sz;       /* pixel physical size [2] */
  const size_t*             image_def;    /* image width, height     */
  size_t                    spp;
};

/*******************************************************************************
 * probe_mode_ctx — parameters specific to probe temperature estimation
 ******************************************************************************/
struct probe_mode_ctx {
  double                    position[3];  /* probe spatial position  */
  size_t                    nrealisations;
  enum sdis_medium_type     mdm_type;     /* SOLID or FLUID          */
};

/*******************************************************************************
 * probe_batch_mode_ctx — parameters for multi-probe batch estimation
 *
 * Allows K probes × N realisations to share a single persistent pool,
 * amortising pool startup / BVH-cache cost.  Each probe is identified by
 * its index (0..nprobes-1), stored in pixel_task::ipix_image[0] and
 * propagated to path_state::ipix_image[0] so accumulate_result can route
 * each completed path to the correct accum bucket.
 ******************************************************************************/
struct probe_batch_mode_ctx {
  const double*             positions;    /* [nprobes * 3]           */
  const unsigned*           enc_ids;      /* [nprobes]               */
  const enum sdis_medium_type* mdm_types; /* [nprobes]               */
  struct accum*             accums;       /* [nprobes] — output      */
  size_t                    nprobes;
  size_t                    nrealisations; /* per-probe realisation count */
};

/*******************************************************************************
 * pool_view — unified pool view (P2: 2-stream pipeline)
 *
 * Represents a view over slots[base .. base+view_size) within the pool.
 * Single-buffer mode: views[0] covers the entire pool (base=0, view_size=pool_size).
 * Dual-buffer mode:   views[0] covers the first half, views[1] the second half.
 *
 * All pool operation functions will be unified to accept (pool, pv) in P3.
 ******************************************************************************/
struct pool_view {
  size_t base;              /* slot start offset: 0 or view_size          */
  size_t view_size;         /* number of slots this view covers           */
  size_t capacity;          /* allocated capacity (>= view_size)          */

  /* ---- Stream compaction indices ---- */
  uint32_t* active_indices;
  size_t    active_compact;

  uint32_t* need_ray_indices;
  size_t    need_ray_count;

  uint32_t* done_indices;
  size_t    done_count;

  /* ---- Path type buckets ---- */
  uint32_t* bucket_radiative;
  size_t    bucket_radiative_n;
  uint32_t* bucket_conductive;
  size_t    bucket_conductive_n;
  uint32_t* bucket_other;         /* O2: non-radiative, non-conductive ray paths */
  size_t    bucket_other_n;

  /* ---- Ray buffers ---- */
  struct s3d_ray_request* ray_requests;
  uint32_t*               ray_to_slot;
  uint32_t*               ray_slot_sub;
  struct s3d_hit*          ray_hits;
  size_t                   ray_count;
  size_t                   max_rays;      /* capacity * 6 */

  /* ---- L4: GPU inline filter per-ray data ---- */
  struct s3d_filter_per_ray* filter_per_ray;

  /* ---- Plan E: Pinned direct-write pointers (borrowed from batch_ctx) ---- */
  struct s3d_ray_pinned*     ray_pinned;      /* → ctx->h_rays_pinned   */
  struct s3d_filter_per_ray* filter_pinned;   /* → ctx->h_filter_pinned */

  /* ---- Ray bucket offsets ---- */
  size_t bucket_offsets[RAY_BUCKET_COUNT + 1];
  size_t bucket_counts[RAY_BUCKET_COUNT];

  /* ---- GPU batch contexts ---- */
  struct s3d_batch_trace_context* batch_ctx;
  struct s3d_batch_enc_context*   enc_batch_ctx;
  struct s3d_batch_cp_context*    cp_batch_ctx;

  /* ---- enc_locate buffers ---- */
  struct s3d_enc_locate_request* enc_locate_requests;
  struct s3d_enc_locate_result*  enc_locate_results;
  uint32_t*                      enc_locate_to_slot;
  size_t                         enc_locate_count;
  size_t                         max_enc_locates;  /* capacity */

  /* ---- closest_point buffers ---- */
  struct s3d_cp_request* cp_requests;
  struct s3d_hit*        cp_hits;
  uint32_t*              cp_to_slot;
  size_t                 cp_count;
  size_t                 max_cps;         /* capacity */

  /* ---- Async tracking (P4) ---- */
  int gpu_pending;           /* 1 = async GPU trace in flight        */
  int enc_gpu_pending;       /* 1 = async enc_locate in flight       */
  int cp_gpu_pending;        /* 1 = async closest_point in flight    */

  /* ---- Per-step pending ray stats (accumulated in collect, promoted
   *      to pool counters only when trace completes in wait_and_postprocess) */
  size_t pending_rays_radiative;
  size_t pending_rays_conductive_ds;
  size_t pending_rays_conductive_ds_retry;
  size_t pending_rays_shadow;
  size_t pending_rays_enclosure;
  size_t pending_rays_startup;
  size_t pending_rays_other;

  /* ---- View statistics ---- */
  size_t total_rays_traced;
  size_t paths_completed;
  size_t paths_failed;
};

/*******************************************************************************
 * wavefront_pool — persistent pool holding all active paths
 ******************************************************************************/
struct wavefront_pool {
  /* --- Path pool (fixed size) --- */
  struct path_state*  slots;
  size_t              pool_size;       /* number of path slots */
  size_t              active_count;    /* currently active paths */

  /* --- Global task queue --- */
  struct pixel_task*  task_queue;
  size_t              task_count;      /* image_w × image_h × spp */
  size_t              task_next;       /* next unassigned task index */

  /* --- Scene params (cached for refill) --- */
  struct sdis_scene*  scn;
  unsigned            enc_id;
  const double*       time_range;
  size_t              picard_order;
  enum sdis_diffusion_algorithm diff_algo;
  uint32_t            next_path_id;    /* monotonically increasing path id */

  /* --- Mode-specific dispatch (camera / probe) --- */
  const struct wavefront_ops* ops;      /* mode vtable               */
  const void*                 mode_ctx; /* camera_mode_ctx* or probe_mode_ctx* */
  void*                       result_ctx; /* estimator_buffer* or accum*       */

  /* --- Per-slot RNG (independent streams) --- */
  struct ssp_rng**    slot_rngs;

  /* --- Per-path CBRNG thin wrapper storage (allocated by adapter) --- */
  struct ssp_rng*     thin_rng_storage;  /* contiguous array [pool_size]   */
  uint64_t            global_seed;       /* user-controlled seed for CBRNG */

  /* --- Stream Compaction indices (M2.5) --- */
  uint32_t*           active_indices;  /* compact array of active slot ids  */
  size_t              active_compact;  /* length of active_indices          */

  uint32_t*           need_ray_indices;/* slots that need ray trace         */
  size_t              need_ray_count;  /* length of need_ray_indices        */

  uint32_t*           done_indices;    /* slots with PATH_DONE, !active     */
  size_t              done_count;      /* length of done_indices            */

  /* --- Path type buckets (M2.5) --- */
  uint32_t*           bucket_radiative;   /* radiative slot indices         */
  size_t              bucket_radiative_n;
  uint32_t*           bucket_conductive;  /* conductive slot indices        */
  size_t              bucket_conductive_n;

  /* --- Ray request buffers --- */
  struct s3d_ray_request* ray_requests;
  uint32_t*           ray_to_slot;     /* ray → slot index */
  uint32_t*           ray_slot_sub;    /* ray → sub-ray (0 or 1) */
  struct s3d_hit*     ray_hits;
  size_t              ray_count;
  size_t              max_rays;        /* pool_size × 6 */

  /* --- Ray bucket offsets (B-4 M2) --- */
  size_t              bucket_offsets[RAY_BUCKET_COUNT + 1];
  size_t              bucket_counts[RAY_BUCKET_COUNT];

  /* --- Batch trace context (Phase B-1) --- */
  struct s3d_batch_trace_context* batch_ctx;

  /* --- Batch enc_locate context (Phase B-4 M10) --- */
  struct s3d_batch_enc_context* enc_batch_ctx;
  struct s3d_enc_locate_request* enc_locate_requests;
  struct s3d_enc_locate_result*  enc_locate_results;
  uint32_t* enc_locate_to_slot;  /* enc_locate index → slot mapping */
  size_t    enc_locate_count;    /* queries this step */
  size_t    max_enc_locates;     /* pool_size */

  /* --- Batch closest_point context (Phase B-4 M9: WoS) --- */
  struct s3d_batch_cp_context* cp_batch_ctx;
  struct s3d_cp_request*       cp_requests;
  struct s3d_hit*              cp_hits;
  uint32_t*                    cp_to_slot;   /* cp index → slot mapping */
  size_t                       cp_count;     /* queries this step */
  size_t                       max_cps;      /* pool_size */

  /* --- P0_OPT: Dispatch-hot compact array --- */
  struct path_hot*     hot_arr;         /* [pool_size], 8B per slot */

  /* --- P1: Cold-block SoA arrays (separated from path_state slots) --- */
  struct path_sfn_data *sfn_arr;     /* [pool_size], PicardN stack        */
  struct path_enc_data *enc_arr;     /* [pool_size], enclosure query       */
  struct path_ext_data *ext_arr;     /* [pool_size], external net flux     */

  /* --- P2: Unified pool views (2-stream pipeline) --- */
  struct pool_view    views[2];
  int                 num_active_views;   /* 1 = single-buffer, 2 = dual-buffer */

  /* --- Drain phase control (M2) --- */
  int                 in_drain_phase;  /* 1 = task_queue exhausted          */
  size_t              drain_step_count;/* steps taken in drain phase        */
  size_t              drain_fallback_threshold; /* batch < this → CPU (M3.5)*/

  /* --- Statistics --- */
  size_t total_steps;
  size_t total_rays_traced;
  size_t paths_completed;
  size_t paths_failed;
  size_t max_path_depth;

  /* Detailed ray statistics — semantic layer (7 counters) */
  size_t rays_radiative;
  size_t rays_conductive_ds;
  size_t rays_conductive_ds_retry;
  size_t rays_shadow;
  size_t rays_enclosure;
  size_t rays_startup;
  size_t rays_other;          /* catch-all: nonzero ⇒ classification bug */

  size_t paths_done_radiative;
  size_t paths_done_temperature;
  size_t paths_done_boundary;

  /* Diagnostics (M2.5 + M3) */
  size_t diag_min_batch;    /* smallest batch size seen              */
  size_t diag_max_batch;    /* largest batch size seen               */
  size_t diag_refill_count; /* total refill operations               */
  size_t diag_drain_rays;   /* rays traced during drain phase        */
  size_t diag_refill_rays;  /* rays traced during refill phase       */

  /* M10: enc_locate batch stats (cumulative) */
  size_t enc_locates_total;  /* total enc_locate queries dispatched   */
  size_t enc_locates_resolved; /* successfully resolved                */
  size_t enc_locates_degenerate; /* degenerate fallbacks               */
  double time_enc_locate_s;  /* cumulative enc_locate batch time      */

  /* M9: closest_point (WoS) batch stats (cumulative) */
  size_t cp_total;           /* total closest_point queries dispatched */
  size_t cp_accepted;        /* accepted from GPU batch               */
  size_t cp_requeried;       /* re-queried via CPU fallback           */
  double time_cp_s;          /* cumulative closest_point batch time   */

  /* Enclosure query escalation counters (M1-v2 → M10 cascade) */
  size_t enc_query_escalated_to_m10; /* fb_resolve → enc_locate cascade */
  size_t enc_locate_degenerate_null; /* M10 degenerate → accept NULL    */

  /* M3: Per-phase wall-clock timing (seconds) */
  double time_collect_s;    /* cumulative collect_ray_requests time  */
  double time_trace_s;      /* cumulative GPU batch trace time       */
  double time_distribute_s; /* cumulative distribute+advance time    */
  double time_cascade_s;    /* cumulative cascade non-ray time       */
  double time_compact_s;    /* cumulative stream compaction time     */
  double time_harvest_s;    /* cumulative harvest+refill time        */
  /* P0_OPT: sync timers removed (sync_a, sync_b eliminated) */
  double time_housekeeping_s; /* cumulative Steps H-L (update/diag/progress)   */
  double time_trace_cpu_s;  /* trace CPU component (postprocess+retrace) ms→s  */
  double time_gpu_sync_s;   /* cumulative GPU kernel sync wait (dual-buffer)   */
  double time_gpu_launch_s; /* cumulative GPU launch+startD2h (dual-buffer)    */
  double time_submit_s;     /* cumulative gpu_submit_all API overhead (O12)    */
  double time_refill_phase_s; /* wall-clock of refill phase total    */
  double time_drain_phase_s;  /* wall-clock of drain phase total     */

  /* M3: Wavefront width tracking */
  size_t diag_total_active; /* sum of active_count across all steps  */
  size_t paths_truncated;   /* paths force-terminated in drain (M3.5)*/

  /* === Experiment 3: Cascade per-phase profiling === */
  size_t cascade_phase_count[PATH_PHASE_COUNT]; /* dispatch count per phase */
  double cascade_phase_time[PATH_PHASE_COUNT];  /* cumulative seconds/phase */
  size_t cascade_total_advances;                /* total advance_one_step calls */
  size_t cascade_total_iterations;              /* total inner-loop iterations  */

  /* === Experiment 7: Batch trace per-call timing === */
  double trace_kernel_time_ms_sum;   /* sum of gpu_sync_kernel wait time */
  double trace_batch_time_ms_sum;    /* sum of batch_time_ms (D2H wait)  */
  double trace_post_time_ms_sum;     /* sum of postprocess_time_ms */
  double trace_retrace_time_ms_sum;  /* sum of retrace_time_ms */
  size_t trace_call_count;           /* number of batch trace calls */
  size_t trace_batch_size_sum;       /* sum of nrays across calls  */
  size_t trace_batch_size_min;       /* smallest single-call nrays */
  size_t trace_batch_size_max;       /* largest single-call nrays  */

  /* === Experiment 8: cudaStreamSynchronize breakdown (from stats) === */
  /* Populated from s3d_batch_trace_stats per call */
  size_t trace_retrace_accepted_sum;
  size_t trace_retrace_missed_sum;
  size_t trace_filter_rejected_sum;

  /* === P2: Pipeline timing (2-stream) === */
  double time_pipeline_wait_s;
  double time_pipeline_cpu_pre_s;
  double time_pipeline_cpu_between_s;
  double time_pipeline_gpu_idle_s;

  /* === L4: GPU inline filter mode === */
  int use_gpu_filter;  /* 1 = filtered trace (Mode A), 0 = legacy retrace */

  /* Pre-allocated per-thread ray buffers for OMP merged_pass.
   * Lazily allocated on first merged_pass call, freed in pool_destroy.
   * Each element is a malloc'd struct tl_ray_entry[TL_RAY_BUF_MAX]. */
  void** tl_ray_bufs;   /* [tl_ray_nbufs], cast to struct tl_ray_entry* */
  int tl_ray_nbufs;     /* number of allocated buffers   */

  /* === O14: per-thread partition (done list + active list) === */
  uint32_t** tl_done_indices;   /* [nthreads][0..tl_done_count[tid])   */
  size_t*    tl_done_count;     /* per-thread done slot count           */
  uint32_t** tl_active_indices; /* [nthreads][0..tl_active_count[tid])  */
  size_t*    tl_active_count;   /* per-thread active slot count         */
  size_t     tl_done_capacity;  /* per-thread capacity = partition_size */
  size_t     o14_partition_size;/* pool_size / nthreads (floor)         */
  int        o14_nthreads;      /* thread count used for O14 partition  */

  /* === O13: Async submit thread (per-view channels) === */
  void*  submit_thread_handle;   /* HANDLE from _beginthreadex       */
  void*  submit_evt_go[2];       /* per-view auto-reset triggers     */
  void*  submit_evt_done[2];     /* per-view manual-reset completion */
  volatile int submit_shutdown;  /* 1 = thread should exit           */
  /* Per-view job params (independent slots — no sharing) */
  struct pool_view*      submit_pv[2];
  struct s3d_scene_view* submit_sv[2];
  res_T                  submit_result[2];
  double                 submit_elapsed_s[2];
};

/*******************************************************************************
 * Public API — called from sdis_solve_camera
 ******************************************************************************/

/* Persistent wavefront replacement of the OMP tile loop.
/* ============================================================================
 * Internal helpers — LOCAL_SYM, accessible by unit tests via OBJECT lib
 * ============================================================================ */

static INLINE size_t
count_path_rays(const struct path_state* p)
{
  /* B-4 M1-v2: 6-ray enc_query uses ray_count_ext for extra rays.
   * P0_OPT: phase/ray_count_ext moved to path_hot; this legacy helper
   * retained for old code paths that don't have SoA context.
   * The persistent wavefront uses count_path_rays_soa() instead. */
  (void)p;
  return (size_t)p->ray_req.ray_count;  /* hot fields unavailable */
}

/* P1: SoA-friendly variant — reads phase + ray_count_ext from SoA,
 * falls back to AoS ray_req.ray_count for the common (non-ENC) case.
 *
 * BUG-FIX: also handle ray_count_ext==4 for SS 4-ray reinjection
 * (PATH_BND_SS_REINJECT_SAMPLE).  Previously only the ENC 6-ray case
 * was handled, causing SS reinject to under-count rays as 2 instead of 4. */
static INLINE size_t
count_path_rays_soa(enum path_phase phase, int ray_count_ext,
                    const struct path_state* p)
{
  if(phase == PATH_ENC_QUERY_EMIT && ray_count_ext == 6)
    return 6;
  if(phase == PATH_BND_SS_REINJECT_SAMPLE && ray_count_ext == 4)
    return 4;
  return (size_t)p->ray_req.ray_count;
}

extern LOCAL_SYM res_T
pool_collect_ray_requests_bucketed(struct wavefront_pool* pool,
                                    struct pool_view* pv);

/* B-4 M10: Collect enc_locate requests from paths in PATH_ENC_LOCATE_PENDING */
extern LOCAL_SYM res_T
pool_collect_enc_locate_requests(struct wavefront_pool* pool,
                                  struct pool_view* pv);

/* B-4 M9: Collect closest_point requests from WoS paths in PATH_CND_WOS_CLOSEST */
extern LOCAL_SYM res_T
pool_collect_cp_requests(struct wavefront_pool* pool,
                          struct pool_view* pv);

/* B-4 M9: Distribute closest_point results to WoS paths */
extern LOCAL_SYM res_T
pool_distribute_cp_results(struct wavefront_pool* pool,
                            struct pool_view* pv);

/* ============================================================================
 * Public API
 * ============================================================================ */

/* Processes the entire image in a single wavefront pool.
 * Receives the already-created per-thread RNG array (nthreads entries)
 * from solve_camera.  The buckets are already locked by create_per_thread_rng
 * so we must not call ssp_rng_proxy_create_rng again. */
extern LOCAL_SYM res_T
solve_camera_persistent_wavefront
  (struct sdis_scene*          scn,
   struct ssp_rng**            per_thread_rng,
   const size_t                nthreads,
   const unsigned              enc_id,
   const struct sdis_camera*   cam,
   const double                time_range[2],
   const size_t                image_def[2],  /* image width, height */
   const size_t                spp,
   const int                   register_paths,
   const double                pix_sz[2],
   const size_t                picard_order,
   const enum sdis_diffusion_algorithm diff_algo,
   struct sdis_estimator_buffer* buf,
   int32_t                     progress[],
   const int                   pcent_progress,
   const char*                 progress_label);

/* Persistent wavefront probe solver — same semantics as
 * sdis_solve_wavefront_probe but uses the persistent pool main loop.
 * This shares the full main loop with solve_camera_persistent_wavefront
 * via the wavefront_ops vtable, avoiding code duplication. */
extern LOCAL_SYM res_T
solve_persistent_wavefront_probe
  (struct sdis_scene*          scn,
   struct ssp_rng*             base_rng,
   const unsigned              enc_id,
   const double                position[3],
   const double                time_range[2],
   const size_t                nrealisations,
   const size_t                picard_order,
   const enum sdis_diffusion_algorithm diff_algo,
   struct accum*               out_acc_temp);

/* Persistent wavefront batch probe solver — runs K probes × N realisations
 * in a single persistent pool, improving GPU utilisation.
 * out_acc_temps must point to an array of nprobes accum structs. */
extern LOCAL_SYM res_T
solve_persistent_wavefront_probe_batch
  (struct sdis_scene*          scn,
   struct ssp_rng*             base_rng,
   const size_t                nprobes,
   const unsigned              enc_ids[],
   const double                positions[],   /* [nprobes * 3] */
   const double                time_range[2],
   const size_t                nrealisations, /* per-probe */
   const size_t                picard_order,
   const enum sdis_diffusion_algorithm diff_algo,
   const enum sdis_medium_type mdm_types[],
   struct accum*               out_acc_temps); /* [nprobes] */

#endif /* SDIS_SOLVE_PERSISTENT_WAVEFRONT_H */
