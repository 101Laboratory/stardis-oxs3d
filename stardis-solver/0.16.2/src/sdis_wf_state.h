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

/* Wavefront per-path state structures.
 *
 * Extracted from sdis_solve_wavefront.h.  Contains the fully externalised
 * state of one Monte-Carlo path (struct path_state) and its ray request
 * descriptor (struct path_ray_request).
 */

#ifndef SDIS_WF_STATE_H
#define SDIS_WF_STATE_H

#include "sdis_wf_types.h"  /* enum path_phase, enum ray_bucket_type        */
#include "sdis_wf_rng.h"   /* struct wf_rng, per-path CBRNG                */
#include "sdis_heat_path.h" /* struct rwalk, rwalk_context, temperature      */
#include "sdis_medium_c.h"  /* struct solid_props                            */
#include "sdis_scene_c.h"   /* struct hit_filter_data                        */
#include "sdis_source_c.h"  /* struct source_props, source_sample (M7)       */

#include <star/s3d.h>        /* struct s3d_hit                                */
#include <star/ssp.h>        /* struct ssp_rng                                */
#include <stdint.h>          /* uint32_t, uint16_t                            */

/* Forward declarations (pointer-only usage in path_state) */
struct sdis_medium;
struct green_path_handle;
struct wavefront_pool;  /* P1: forward-declared for cold-block pool pointer */

/*******************************************************************************
 * Per-ray request descriptor (wavefront internal)
 ******************************************************************************/
struct path_ray_request {
  float    origin[3];
  float    direction[3];
  float    range[2];

  /* Second ray (delta_sphere / find_reinjection_ray can emit 2 rays) */
  float    direction2[3];
  float    range2[2];
  int      ray_count;        /* 1 or 2                                       */

  /* Batch dispatch indices (set by collect_ray_requests) */
  uint32_t batch_idx;        /* index of ray 0 in the batch                  */
  uint32_t batch_idx2;       /* index of ray 1 in the batch (if ray_count==2)*/
};

/*******************************************************************************
 * struct path_state — fully externalised state of one MC path
 *
 * B-4 expansion (original estimate):
 *   ~600 B (original) + ~600 B (locals union) + ~200 B (ext_flux)
 *   + ~200 B (enc_query) + ~600 B (sfn_stack x 3) ≈ 2.2 KB/path.
 * Actual size after M5 bnd_sf snapshot fields (rwalk_snapshot,
 * hvtx_saved etc.): ~4 KB/path (see test_path_state_size, limit 4096).
 * 32K paths × 4 KB = 128 MB, within CPU memory budget.
 ******************************************************************************/

/* Maximum recursion depth for picardN COMPUTE_TEMPERATURE stack.
 * 3 = up to 3 nested boundary sub-paths.  If the depth is exceeded during
 * a recursive Ti sampling, the path falls back to synchronous execution. */
#define MAX_PICARD_DEPTH 3

/*******************************************************************************
 * P1: Cold-block structures — SoA-separated from path_state.
 *
 * These three data blocks account for ~70% of the per-path slot size
 * (~4648B out of ~6688B) but are only accessed by specific step functions.
 * Moving them to parallel SoA arrays indexed by slot_idx reduces the
 * cache working set of the cascade hot loop from 6.7KB/slot to ~2KB/slot.
 ******************************************************************************/

/* Forward-declared here so path_sfn_data can embed it; the full definition
 * of path_bnd_sf_locals also appears as locals.bnd_sf inside path_state. */
struct path_bnd_sf_locals {
    double  p_conv, p_cond, p_radi;
    double  h_hat, h_conv, h_cond;
    double  h_radi_hat;
    double  epsilon, Tref;
    double  lambda;
    double  delta_boundary;
    double  delta_m;
    float   reinject_dir[2][3];
    struct s3d_hit reinject_hit[2];
    float   chosen_dir[3];
    float   chosen_dst;
    struct s3d_hit chosen_hit;
    unsigned solid_enc_id;
    unsigned enc_ids[2];
    unsigned enc0_id, enc1_id;
    enum sdis_side solid_side;
    enum sdis_side fluid_side;
    int     need_enc;
    int     retry_count;
    double  rwalk_pos_backup[3];
    double  r;
    float   rad_sub_direction[3];
    int     rad_sub_bounce_count;
    int     rad_sub_retry_count;
    struct rwalk      rwalk_snapshot;
    struct temperature T_snapshot;
    struct sdis_heat_vertex hvtx_saved;
    size_t  ihvtx_radi_begin;
    int     is_picardn;
    struct rwalk rwalk_s;
    struct temperature T_s;
    struct sdis_heat_vertex hvtx_s;
    size_t  ihvtx_radi_end;
    int     coupled_nbranchings_saved;
};

/* PicardN recursive stack — only accessed by BND_SFN step functions and
 * cascade intercept (sfn_stack_depth > 0 test). ~3700B */
struct path_sfn_data {
    struct {
        enum path_phase return_state;
        double  partial_temperature;
        struct rwalk rwalk_saved;
        struct temperature T_saved;
        double  T_values[6];
        int     T_count;
        double  r, p_conv, p_cond, h_hat;
        struct path_bnd_sf_locals bnd_sf_backup;
    } stack[MAX_PICARD_DEPTH];
    int depth;  /* was sfn_stack_depth */
};

/* 6-ray enclosure query + enc_locate state — only accessed by ENC step
 * functions, DS enc_verify, and collect/distribute. ~596B */
struct path_enc_data {
    /* M1-v2: 6-ray enclosure query */
    double   query_pos[3];
    enum path_phase return_state;
    unsigned resolved_enc_id;
    float    directions[6][3];
    struct s3d_hit dir_hits[6];
    uint32_t batch_indices[6];
    float    fb_direction[3];
    struct s3d_hit fb_hit;
    uint32_t fb_batch_idx;

    /* M10: point-in-enclosure via BVH */
    struct {
        double   query_pos[3];
        enum path_phase return_state;
        unsigned resolved_enc_id;
        int32_t  prim_id;
        int32_t  side;
        float    distance;
        uint32_t batch_idx;
    } locate;
};

/* M7 external net flux sub-chain state — only accessed by BND_EXT step
 * functions and step_bnd_sf_prob_dispatch. ~360B */
struct path_ext_data {
    /* Ray data for current shadow / diffuse ray */
    float   pos[3];
    float   dir[3];
    float   range;
    struct s3d_hit hit;

    /* Accumulated flux components */
    double  flux_direct;
    double  flux_diffuse_reflected;
    double  flux_scattered;
    double  scattered_dir[3];
    int     nbounces;

    /* Persistent state across suspend points */
    double  emissivity;
    double  sum_h;
    double  cos_theta;
    double  N[3];
    unsigned enc_id_fluid;
    struct source_props  src_props;
    struct source_sample src_sample;

    /* Fragment info for BRDF at bounce */
    double  frag_time;
    double  frag_P[3];

    /* Green function handle (non-owning pointer) */
    struct green_path_handle* green_path;

    /* Return state after flux computation is done */
    enum path_phase return_state;
};

/*******************************************************************************
 * struct path_state — fully externalised state of one MC path
 *
 * P1: Cold blocks (sfn_stack, enc_query, enc_locate, ext_flux) moved to
 * separate SoA arrays (struct path_sfn_data/path_enc_data/path_ext_data).
 * Access via pool->sfn_arr[slot_idx] etc.
 * Remaining size: ~2040B/path.
 ******************************************************************************/
struct path_state {
  /* --- Identity --- */
  uint32_t  path_id;                   /* global unique id within the tile   */
  uint16_t  pixel_x, pixel_y;         /* pixel coords (tile-local)          */
  uint32_t  realisation_idx;           /* SPP index                          */

  /* --- Lifecycle --- P0_OPT: phase/active moved to path_hot --- */

  /* --- Random walk core --- */
  struct rwalk          rwalk;         /* position, time, hit, enc_id ...    */
  struct rwalk_context  ctx;           /* Tmin/That/branchings ...           */

  /* --- Temperature / weight --- */
  struct temperature T;                /* func ptr + value + done            */

  /* --- Radiative path scratch --- */
  float   rad_direction[3];            /* current radiative direction        */
  int     rad_bounce_count;
  int     rad_retry_count;             /* find_next_fragment retries         */

  /* --- Coupled path scratch --- */
  int     coupled_nbranchings;

  /* (BND scratch fields removed — dead code from pre-B4 refactoring)
   * (DS fields moved into locals.cnd_ds — see union below)            */

  /* --- Filter data for current ray request --- */
  struct hit_filter_data  filter_data_storage;

  /* --- Diagnostics --- */
  size_t  steps_taken;                 /* total steps this path has taken     */
  int     done_reason;                 /* 0=none, 1=rad_miss, 2=temp_known,
                                          3=boundary_done, 4=time_rewind,
                                          -1=failed                          */

  /* --- Ray request output (set by step functions) --- */
  struct path_ray_request ray_req;
  /* P0_OPT: needs_ray moved to path_hot */

  /* --- RNG (shared per-thread, non-owning pointer) --- */
  struct ssp_rng* rng;

  /* --- Per-path CBRNG state (inline, keyed by pixel+spp+seed) --- */
  struct wf_rng   rng_state;

  /* --- Image-space pixel coords (in image plane) --- */
  size_t  ipix_image[2];

  /* ===================================================================== */
  /* B-4: Fine-grained state machine extensions                            */
  /* ===================================================================== */

  /* --- B-4: Fine-grained locale variables (union, one branch active) --- */
  union {
    struct {                            /* solid/solid reinjection         */
      float   dir_frt[2][3];           /* front side 2 directions         */
      float   dir_bck[2][3];           /* back side 2 directions          */
      struct s3d_hit ray_frt[2];       /* hit results for front dir0/dir1 */
      struct s3d_hit ray_bck[2];       /* hit results for back dir0/dir1  */
      unsigned enc_ids[2];             /* [FRONT] and [BACK] enclosure ids*/
      double  lambda_frt, lambda_bck;
      double  delta_boundary_frt;      /* reinject distance front side    */
      double  delta_boundary_bck;      /* reinject distance back side     */
      double  tcr;                     /* thermal contact resistance      */
      double  proba;
      int     multi_frt, multi_bck;    /* 1 if enclosure is MEDIUM_ID_MULTI */

      /* Reinjection ray results (mirrors find_reinjection_ray output) */
      float   reinject_dir_frt[3];     /* chosen front reinjection dir    */
      float   reinject_dst_frt;        /* chosen front reinjection dist   */
      struct s3d_hit reinject_hit_frt; /* chosen front reinjection hit    */
      float   reinject_dir_bck[3];     /* chosen back reinjection dir     */
      float   reinject_dst_bck;        /* chosen back reinjection dist    */
      struct s3d_hit reinject_hit_bck; /* chosen back reinjection hit     */

      /* Enclosure ids at reinjection endpoints (from trace or ENC query) */
      unsigned enc0_frt, enc1_frt;     /* enc_id along frt dir0, dir1     */
      unsigned enc0_bck, enc1_bck;     /* enc_id along bck dir0, dir1     */
      int      need_enc_frt, need_enc_bck; /* which side needs ENC query  */
      int      enc_side;               /* 0=frt,1=bck for current ENC    */

      /* Position backup for retry logic */
      double  rwalk_pos_backup[3];
      int     position_was_moved;      /* did find_reinjection_ray move?  */
      int     retry_count;             /* attempt counter (MAX=10)        */

      /* Batch indices for 4-ray collect/distribute */
      uint32_t batch_idx_frt0, batch_idx_frt1;
      uint32_t batch_idx_bck0, batch_idx_bck1;
    } bnd_ss;

    struct path_bnd_sf_locals bnd_sf;   /* solid/fluid picard1/N           */

    struct {                            /* WoS conductive                  */
      double  query_pos[3];
      double  query_radius;
      float   new_pos[3];
      float   dir[3];
      struct s3d_hit cached_hit;        /* closest_point hit (for snap)    */
      double  delta;                    /* material delta parameter        */
      double  last_distance;            /* wos_distance from closest query */
      double  alpha;                    /* diffusivity = lambda/(rho*cp)   */
      unsigned enc_id;                  /* enclosure id for WoS walk       */
      struct sdis_medium* medium;       /* solid medium pointer            */
      struct solid_props props_ref;     /* reference properties at start   */
      struct solid_props props;         /* current position properties     */
      double  green_power_term;         /* accumulated green function pwr  */
      double  position_start[3];        /* pos before diffusion step       */
      int     wos_initialized;          /* 1 = init phase done             */
      float   scene_diag;               /* scene AABB diagonal (CP radius) */
      uint32_t batch_cp_idx;            /* index in closest_point batch    */
      /* --- diffusion-check batch (CP2) --- */
      double  diffusion_pos[3];         /* candidate new pos for validation*/
      uint32_t batch_cp2_idx;           /* index in CP batch (2nd round)   */
    } cnd_wos;

    struct {                            /* convective path                 */
      unsigned enc_id;
      double  hc_upper_bound;
      double  rho_cp;
      double  S_over_V;
    } cnv;

    struct {                            /* conductive delta-sphere         */
      float   dir0[3], dir1[3];
      struct s3d_hit hit0, hit1;
      int     initialized;              /* 1 = init phase done             */
      unsigned enc_id;                  /* enclosure id for conductive walk */
      struct sdis_medium* medium;       /* solid medium pointer            */
      struct solid_props  props_ref;    /* reference properties at start   */
      double  green_power_term;         /* accumulated green function power*/
      double  position_start[3];        /* starting position backup        */
      int     robust_attempt;           /* robust retry counter            */
      float   delta;                    /* computed step distance           */
      float   delta_solid_param;        /* props.delta (medium step param) */
    } cnd_ds;
  } locals;

  /* --- B-4 M7/M10/M1-v2/M8: Cold blocks moved to SoA arrays (P1) ---
   * Access via pool->ext_arr[slot_idx], pool->enc_arr[slot_idx],
   * pool->sfn_arr[slot_idx].  See struct path_ext_data, path_enc_data,
   * path_sfn_data defined above. */

  /* --- B-4: Ray type tag --- P0_OPT: ray_bucket/ray_count_ext moved to path_hot */
};

#endif /* SDIS_WF_STATE_H */
