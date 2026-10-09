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

/* Wavefront path state machine types.
 *
 * Extracted from sdis_solve_wavefront.h to allow fine-grained inclusion
 * by step function modules without pulling in scheduler-level types.
 *
 * Contains:
 *   - enum path_phase       (59 states)
 *   - enum ray_bucket_type  (5 ray categories)
 *   - path_phase_is_ray_pending()  inline helper
 */

#ifndef SDIS_WF_TYPES_H
#define SDIS_WF_TYPES_H

#include "sdis.h"  /* LOCAL_SYM, INLINE */

/*******************************************************************************
 * Path phase — models the state machine of a single Monte-Carlo path.
 *
 * Phase B-4: Fine-grained explicit state machine (59 states).
 * Each trace_ray call site becomes a wavefront suspend/resume point.
 *
 * Legend:
 *   [R] = ray-pending (needs batch trace result before resuming)
 *   [C] = pure-compute (advances without ray)
 *
 * States marked [B4-FUTURE] are defined here for completeness but currently
 * fall through to the original synchronous code path.  They will be activated
 * in subsequent B-4 milestones.
 ******************************************************************************/
enum path_phase {
  /* === Initialisation === */
  PATH_INIT,                              /* [C] Not yet started              */

  /* === Radiative path phases === */
  PATH_RAD_TRACE_PENDING,                 /* [R] trace_ray submitted          */
  PATH_RAD_PROCESS_HIT,                   /* [C] hit: BRDF/emit [B4-FUTURE]   */

  /* === Coupled path state machine (B-2/B-3 legacy) === */
  PATH_COUPLED_BOUNDARY,                  /* [C] boundary_path decision        */
  PATH_COUPLED_BOUNDARY_REINJECT,         /* [R] find_reinjection_ray wait    */
  PATH_COUPLED_CONDUCTIVE,                /* [C] conductive_path entry         */
  PATH_COUPLED_COND_DS_PENDING,           /* [R] delta_sphere 2-ray wait      */
  PATH_COUPLED_CONVECTIVE,                /* [C] convective_path               */
  PATH_COUPLED_RADIATIVE,                 /* [C] bounce into radiative         */

  /* === B-4 Fine-grained: Boundary dispatch === */
  PATH_BND_DISPATCH,                      /* [C] type -> 3-way dispatch [B4-FUTURE] */
  PATH_BND_POST_ROBIN_CHECK,              /* [C] Robin post-check       [B4-FUTURE] */

  /* === B-4: Solid/Solid boundary === */
  PATH_BND_SS_REINJECT_SAMPLE,            /* [R] 4 reinjection rays     [B4-FUTURE] */
  PATH_BND_SS_REINJECT_ENC,               /* [C] post-ENC-resolve       [B4-FUTURE] */
  PATH_BND_SS_REINJECT_DECIDE,            /* [C] probability decision   [B4-FUTURE] */

  /* === B-4: Solid/Fluid picard1 boundary === */
  PATH_BND_SF_REINJECT_SAMPLE,            /* [R] 2 reinjection rays     [B4-FUTURE] */
  PATH_BND_SF_REINJECT_ENC,               /* [C] post-ENC-resolve       [B4-FUTURE] */
  PATH_BND_SF_PROB_DISPATCH,              /* [C] prob dispatch           [B4-FUTURE] */
  PATH_BND_SF_NULLCOLL_RAD_TRACE,         /* [R] null-collision rad ray [B4-FUTURE] */
  PATH_BND_SF_NULLCOLL_DECIDE,            /* [C] accept/reject          [B4-FUTURE] */

  /* === B-4: Solid/Fluid picardN boundary === */
  PATH_BND_SFN_PROB_DISPATCH,             /* [C] prob + stack mgmt      [B4-FUTURE] */
  PATH_BND_SFN_RAD_TRACE,                 /* [R] rad sub-path ray       [B4-FUTURE] */
  PATH_BND_SFN_RAD_DONE,                  /* [C] rad sub-path done      [B4-FUTURE] */
  PATH_BND_SFN_COMPUTE_Ti,                /* [C] push sub-path          [B4-FUTURE] */
  PATH_BND_SFN_COMPUTE_Ti_RESUME,         /* [C] pop sub-path           [B4-FUTURE] */
  PATH_BND_SFN_CHECK_PMIN_PMAX,           /* [C] early accept/reject    [B4-FUTURE] */

  /* === B-4: External net flux (picard sub-process) === */
  PATH_BND_EXT_CHECK,                     /* [C] need ext flux?         [B4-FUTURE] */
  PATH_BND_EXT_DIRECT_TRACE,              /* [R] shadow ray             [B4-FUTURE] */
  PATH_BND_EXT_DIRECT_RESULT,             /* [C] shadow result          [B4-FUTURE] */
  PATH_BND_EXT_DIFFUSE_TRACE,             /* [R] diffuse bounce ray     [B4-FUTURE] */
  PATH_BND_EXT_DIFFUSE_RESULT,            /* [C] bounce result          [B4-FUTURE] */
  PATH_BND_EXT_DIFFUSE_SHADOW_TRACE,      /* [R] bounce shadow ray      [B4-FUTURE] */
  PATH_BND_EXT_DIFFUSE_SHADOW_RESULT,     /* [C] bounce shadow result   [B4-FUTURE] */
  PATH_BND_EXT_FINALIZE,                  /* [C] sum flux -> return     [B4-FUTURE] */

  /* === B-4: Conductive path === */
  PATH_CND_INIT_ENC,                      /* [R] init ENC query         [B4-FUTURE] */

  /* --- Delta-sphere conductive --- */
  PATH_CND_DS_CHECK_TEMP,                 /* [C] check known temp       [B4-FUTURE] */
  PATH_CND_DS_STEP_TRACE,                 /* [R] 2 step rays (dir0+dir1)[B4-FUTURE] */
  PATH_CND_DS_STEP_PROCESS,               /* [C] process hit0/hit1      [B4-FUTURE] */
  PATH_CND_DS_STEP_ENC_VERIFY,            /* [C] set up ENC sub-query   [B4-FUTURE] */
  PATH_CND_DS_STEP_ADVANCE,               /* [C] pos update + loop      [B4-FUTURE] */

  /* --- Walk on Spheres (WoS) conductive --- */
  PATH_CND_WOS_CHECK_TEMP,                /* [C] check known temp       [B4-FUTURE] */
  PATH_CND_WOS_CLOSEST,                   /* [R] closest_point query    [B4-FUTURE] */
  PATH_CND_WOS_CLOSEST_RESULT,            /* [C] e-shell / diffuse      [B4-FUTURE] */
  PATH_CND_WOS_DIFFUSION_CHECK,           /* [R] CP batch: validate new pos [B4]     */
  PATH_CND_WOS_DIFFUSION_CHECK_RESULT,     /* [C] process validation result  [B4]     */
  PATH_CND_WOS_FALLBACK_TRACE,            /* [R] fallback trace_ray     [B4-FUTURE] */
  PATH_CND_WOS_FALLBACK_RESULT,           /* [C] fallback result        [B4-FUTURE] */
  PATH_CND_WOS_TIME_TRAVEL,               /* [C] time rewind + loop     [B4-FUTURE] */

  /* --- Custom conductive (Phase 7 plugin) --- */
  PATH_CND_CUSTOM,                        /* [C] custom callback        [B4-FUTURE] */

  /* === B-4: Convective path === */
  PATH_CNV_INIT,                           /* [C] fluid temp check       [B4-FUTURE] */
  PATH_CNV_STARTUP_TRACE,                 /* [R] 1 startup ray          [B4-FUTURE] */
  PATH_CNV_STARTUP_RESULT,                /* [C] startup result         [B4-FUTURE] */
  PATH_CNV_SAMPLE_LOOP,                   /* [C] null-collision loop    [B4-FUTURE] */

  /* === B-4 M10: Point-in-enclosure via BVH closest primitive === */
  PATH_ENC_LOCATE_PENDING,                /* [E] enc_locate batch pending */
  PATH_ENC_LOCATE_RESULT,                 /* [C] enc_locate result ready  */

  /* === B-4 M1-v2: 6-ray enclosure query (replaces M10 at call sites) === */
  PATH_ENC_QUERY_EMIT,                    /* [R] 6 axis-aligned rays pending */
  PATH_ENC_QUERY_FB_EMIT,                 /* [R] 1 fallback ray pending      */

  /* === Terminal === */
  PATH_DONE,                               /* path finished              */
  PATH_HARVESTED,                          /* done + result accumulated   */
  PATH_ERROR,                              /* error termination          */

  PATH_PHASE_COUNT                         /* total state count          */
};

/*******************************************************************************
 * Ray bucket type — classifies rays by BVH traversal pattern for
 * warp-coherent GPU batching (Phase B-4).
 ******************************************************************************/
enum ray_bucket_type {
  RAY_BUCKET_RADIATIVE,       /* long-range random direction, range=[e,inf)  */
  RAY_BUCKET_STEP_PAIR,       /* short-range opposing rays (delta-sphere)    */
  RAY_BUCKET_SHADOW,          /* fixed-distance shadow ray, range=[0,dist]   */
  RAY_BUCKET_STARTUP,         /* single-direction probe ray                  */
  RAY_BUCKET_ENCLOSURE,       /* 6-ray axis-aligned enclosure query          */
  RAY_BUCKET_COUNT
};

/*******************************************************************************
 * path_phase_is_ray_pending — returns 1 if the phase requires a ray trace
 * result before the path can advance.  Used by cascade loops and stream
 * compaction to avoid attempting no-ray advance on these phases.
 ******************************************************************************/
static INLINE int
path_phase_is_ray_pending(enum path_phase ph)
{
  switch(ph) {
  /* B-2/B-3 legacy ray-pending */
  case PATH_RAD_TRACE_PENDING:
  case PATH_COUPLED_BOUNDARY_REINJECT:
  case PATH_COUPLED_COND_DS_PENDING:
  /* B-4 fine-grained ray-pending */
  case PATH_BND_SS_REINJECT_SAMPLE:
  /* PATH_BND_SS_REINJECT_ENC is compute-only (post-ENC-resolve) */
  case PATH_BND_SF_REINJECT_SAMPLE:
  /* PATH_BND_SF_REINJECT_ENC is compute-only (post-ENC-resolve) */
  case PATH_BND_SF_NULLCOLL_RAD_TRACE:
  case PATH_BND_SFN_RAD_TRACE:
  case PATH_BND_EXT_DIRECT_TRACE:
  case PATH_BND_EXT_DIFFUSE_TRACE:
  case PATH_BND_EXT_DIFFUSE_SHADOW_TRACE:
  case PATH_CND_INIT_ENC:
  case PATH_CND_DS_STEP_TRACE:
  /* PATH_CND_DS_STEP_ENC_VERIFY is compute-only (sets up ENC query) */
  /* PATH_CND_WOS_CLOSEST is closest_point batch, not ray trace */
  case PATH_CND_WOS_FALLBACK_TRACE:
  case PATH_CNV_STARTUP_TRACE:
  /* B-4 M1-v2 enclosure query */
  case PATH_ENC_QUERY_EMIT:
  case PATH_ENC_QUERY_FB_EMIT:
    return 1;
  default:
    return 0;
  }
}

/*******************************************************************************
 * path_phase_is_enc_locate_pending — returns 1 if the path is waiting for a
 * batch enc_locate result (M10).  These paths participate in the enc_locate
 * collect/distribute cycle, NOT the ray trace cycle.
 ******************************************************************************/
static INLINE int
path_phase_is_enc_locate_pending(enum path_phase ph)
{
  return ph == PATH_ENC_LOCATE_PENDING;
}

/*******************************************************************************
 * path_phase_is_cp_pending — returns 1 if the path is waiting for a
 * batch closest_point result (M9 WoS).  These paths participate in the
 * closest_point collect/distribute cycle, NOT the ray trace cycle.
 ******************************************************************************/
static INLINE int
path_phase_is_cp_pending(enum path_phase ph)
{
  return ph == PATH_CND_WOS_CLOSEST
      || ph == PATH_CND_WOS_DIFFUSION_CHECK;
}

#endif /* SDIS_WF_TYPES_H */
