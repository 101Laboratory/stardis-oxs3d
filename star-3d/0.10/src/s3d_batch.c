/* s3d_batch.c — CPU batch shim for wavefront solver compatibility.
 *
 * Implements the batch RT / CP / ENC locate APIs using Embree single-query
 * loops.  The three-phase async pipeline (async → sync_kernel → start_d2h →
 * wait_d2h) is collapsed into:
 *   async       → store request pointers, reset kernel_done flag
 *   sync_kernel → execute all Embree queries (bulk of the work)
 *   start_d2h   → no-op
 *   wait_d2h    → lazy-call sync_kernel if not yet done, copy results
 *
 * The dual-buffer pipeline (O12/O13) skips sync_kernel entirely, calling
 * only async+start_d2h, then later wait_d2h.  The kernel_done flag enables
 * lazy evaluation: wait_d2h triggers the actual Embree work on first access.
 *
 * Copyright (C) 2026.  Part of the CPU wavefront experiment branch.
 */

#include "s3d.h"
#include "s3d_c.h"
#include "s3d_scene_view_c.h"
#include "s3d_geometry.h"

#include <rsys/rsys.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#ifdef _OPENMP
  #include <omp.h>
#endif

#ifdef _WIN32
  #include <windows.h>   /* QueryPerformanceCounter */
#else
  #include <time.h>
#endif

/* ── Diagnostic log (define S3D_BATCH_DIAG=1 to enable) ───────────── */
#include <stdio.h>
#ifdef S3D_BATCH_DIAG
static FILE* g_diag = NULL;
static void diag_open(void) {
  if(!g_diag) {
    g_diag = fopen("s3d_batch_diag.log", "w");
    if(g_diag) { fprintf(g_diag, "=== s3d_batch diagnostic log ===\n"); fflush(g_diag); }
  }
}
#define DIAG(...) do { diag_open(); if(g_diag) { fprintf(g_diag, __VA_ARGS__); fflush(g_diag); } } while(0)
#else
#define DIAG(...) ((void)0)
#endif

/* ── High-resolution timer ────────────────────────────────────────────── */

static double
timer_ms(void)
{
#ifdef _WIN32
  static double freq = 0.0;
  LARGE_INTEGER li;
  if(freq == 0.0) {
    QueryPerformanceFrequency(&li);
    freq = (double)li.QuadPart / 1000.0;
  }
  QueryPerformanceCounter(&li);
  return (double)li.QuadPart / freq;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
#endif
}

/* ── Device extension ─────────────────────────────────────────────────── */

#ifdef _WIN32
  #include <windows.h>
#endif

S3D_API int
s3d_device_get_gpu_sm_count(struct s3d_device* dev)
{
  (void)dev;
#ifdef _WIN32
  {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
  }
#else
  {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
  }
#endif
}

/* ═══════════════════════════════════════════════════════════════════════
 * Batch Ray Tracing — CPU shim
 * ═══════════════════════════════════════════════════════════════════════ */

struct s3d_batch_trace_context {
  size_t max_rays;

  /* Deferred state (set by async, consumed by sync_kernel) */
  struct s3d_scene_view*          sv;
  const struct s3d_ray_request*   requests;
  const struct s3d_ray_pinned*    pinned_rays;    /* Plan E path */
  const struct s3d_filter_per_ray* filter;         /* L4 path    */
  size_t                          nrays;
  int                             use_pinned;
  int                             kernel_done;    /* 1 after sync_kernel executed */

  /* Results (written by sync_kernel, read by wait_d2h) */
  struct s3d_hit* hits;

  /* Pinned-buffer emulation (just malloc'd arrays the solver writes into) */
  struct s3d_ray_pinned*     h_rays_pinned;
  struct s3d_filter_per_ray* h_filter_pinned;

  /* Timing */
  double kernel_ms;
};

S3D_API res_T
s3d_batch_trace_context_create(struct s3d_batch_trace_context** out,
                               size_t max_rays)
{
  struct s3d_batch_trace_context* ctx;
  if(!out) return RES_BAD_ARG;

  DIAG("trace_context_create max_rays=%zu\n", max_rays);

  ctx = (struct s3d_batch_trace_context*)calloc(
    1, sizeof(struct s3d_batch_trace_context));
  if(!ctx) return RES_MEM_ERR;

  ctx->max_rays = max_rays;

  ctx->hits = (struct s3d_hit*)calloc(max_rays, sizeof(struct s3d_hit));
  ctx->h_rays_pinned = (struct s3d_ray_pinned*)calloc(
    max_rays, sizeof(struct s3d_ray_pinned));
  ctx->h_filter_pinned = (struct s3d_filter_per_ray*)calloc(
    max_rays, sizeof(struct s3d_filter_per_ray));

  if(!ctx->hits || !ctx->h_rays_pinned || !ctx->h_filter_pinned) {
    free(ctx->hits);
    free(ctx->h_rays_pinned);
    free(ctx->h_filter_pinned);
    free(ctx);
    return RES_MEM_ERR;
  }

  *out = ctx;
  return RES_OK;
}

S3D_API void
s3d_batch_trace_context_destroy(struct s3d_batch_trace_context* ctx)
{
  if(!ctx) return;
  free(ctx->hits);
  free(ctx->h_rays_pinned);
  free(ctx->h_filter_pinned);
  free(ctx);
}

S3D_API void
s3d_batch_trace_context_get_pinned_buffers(
  struct s3d_batch_trace_context* ctx,
  struct s3d_ray_pinned**       out_rays,
  struct s3d_filter_per_ray**   out_filter,
  size_t*                        out_capacity)
{
  if(out_rays)     *out_rays     = ctx->h_rays_pinned;
  if(out_filter)   *out_filter   = ctx->h_filter_pinned;
  if(out_capacity) *out_capacity = ctx->max_rays;
}

S3D_API float
s3d_batch_trace_context_get_last_kernel_ms(
  struct s3d_batch_trace_context* ctx)
{
  return ctx ? (float)ctx->kernel_ms : 0.0f;
}

/* ── Unfiltered async path ─────────────────────────────────────────── */

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_async(
  struct s3d_scene_view* sv,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays)
{
  if(!sv || !ctx) return RES_BAD_ARG;
  ctx->sv          = sv;
  ctx->requests    = requests;
  ctx->nrays       = nrays;
  ctx->use_pinned  = 0;
  ctx->filter      = NULL;
  ctx->kernel_done = 0;
  return RES_OK;
}

/* ── Filtered async path (L4) ─────────────────────────────────────── */

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_filtered_async(
  struct s3d_scene_view* sv,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests,
  const struct s3d_filter_per_ray* filter_per_ray,
  size_t nrays)
{
  if(!sv || !ctx) return RES_BAD_ARG;
  ctx->sv          = sv;
  ctx->requests    = requests;
  ctx->nrays       = nrays;
  ctx->use_pinned  = 0;
  ctx->filter      = filter_per_ray;
  ctx->kernel_done = 0;
  return RES_OK;
}

/* ── Pinned direct-write async (Plan E) ───────────────────────────── */

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_filtered_pinned_async(
  struct s3d_scene_view* sv,
  struct s3d_batch_trace_context* ctx,
  size_t nrays)
{
  DIAG("filtered_pinned_async nrays=%zu ctx=%p\n", nrays, (void*)ctx);
  if(!sv || !ctx) return RES_BAD_ARG;
  ctx->sv          = sv;
  ctx->requests    = NULL;
  ctx->pinned_rays = ctx->h_rays_pinned;
  ctx->filter      = ctx->h_filter_pinned;
  ctx->nrays       = nrays;
  ctx->use_pinned  = 1;
  ctx->kernel_done = 0;
  return RES_OK;
}

/* ── Inline filter helper ─────────────────────────────────────────── */
/* Replicates the GPU __anyhit__mh_filtered logic on CPU:
 *   ①  Self-intersection: reject if prim_id AND geom_id match origin.
 *   ②  Near-distance:     reject if 0 < t < epsilon.
 * When rejected, the ray is retraced with tmin advanced past the rejected
 * hit (emulates optixIgnoreIntersection → continue BVH traversal).
 */
#define BATCH_MAX_RETRACE 32

static void
trace_ray_with_inline_filter(
  struct s3d_scene_view*          sv,
  const float                     org[3],
  const float                     dir[3],
  float                           range[2],
  const struct s3d_filter_per_ray* f,
  struct s3d_hit*                 out)
{
  const int has_filter =
    (f && f->hit_from_prim_id != (uint32_t)0xFFFFFFFFu);
  int budget = BATCH_MAX_RETRACE;

  *out = S3D_HIT_NULL;

  while(budget-- > 0) {
    struct s3d_hit h = S3D_HIT_NULL;

    s3d_scene_view_trace_ray(sv, org, dir, range, NULL, &h);

    if(S3D_HIT_NONE(&h)) {
      *out = h;
      return;
    }

    if(!has_filter) {
      *out = h;
      return;
    }

    /* ---- Filter ①: Self-intersection rejection ---- */
    if(h.prim.prim_id == f->hit_from_prim_id
    && h.prim.geom_id == f->hit_from_geom_id) {
      /* Advance tmin just past the rejected hit and retrace */
      range[0] = h.distance * (1.0f + 1e-5f) + 1e-7f;
      if(range[0] >= range[1]) { *out = S3D_HIT_NULL; return; }
      continue;
    }

    /* ---- Filter ②: Near-distance epsilon rejection ---- */
    if(h.distance > 0.0f && h.distance < f->epsilon) {
      range[0] = h.distance * (1.0f + 1e-5f) + 1e-7f;
      if(range[0] >= range[1]) { *out = S3D_HIT_NULL; return; }
      continue;
    }

    /* All filters passed */
    *out = h;
    return;
  }

  /* Exhausted retrace budget — return miss */
  *out = S3D_HIT_NULL;
}

/* ── sync_kernel — the actual Embree work ─────────────────────────── */

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_sync_kernel(
  struct s3d_batch_trace_context* ctx)
{
  int i;
  int nrays;
  double t0, t1;

  if(!ctx) return RES_BAD_ARG;
  nrays = (int)ctx->nrays;
  t0 = timer_ms();

  #pragma omp parallel for schedule(static)
  for(i = 0; i < nrays; ++i) {
    float org[3], dir[3], range[2];

    if(ctx->use_pinned) {
      const struct s3d_ray_pinned* rp = &ctx->pinned_rays[i];
      const struct s3d_filter_per_ray* f =
        ctx->filter ? &ctx->filter[i] : NULL;
      org[0] = rp->origin_x;  org[1] = rp->origin_y;  org[2] = rp->origin_z;
      dir[0] = rp->direction_x; dir[1] = rp->direction_y; dir[2] = rp->direction_z;
      range[0] = rp->tmin;  range[1] = rp->tmax;

      trace_ray_with_inline_filter(ctx->sv, org, dir, range, f,
                                   &ctx->hits[i]);
    } else {
      const struct s3d_ray_request* r = &ctx->requests[i];
      org[0] = r->origin[0];  org[1] = r->origin[1];  org[2] = r->origin[2];
      dir[0] = r->direction[0]; dir[1] = r->direction[1]; dir[2] = r->direction[2];
      range[0] = r->range[0]; range[1] = r->range[1];

      ctx->hits[i] = S3D_HIT_NULL;
      s3d_scene_view_trace_ray(ctx->sv, org, dir, range,
                               r->filter_data, &ctx->hits[i]);
    }
  }

  t1 = timer_ms();
  ctx->kernel_ms = t1 - t0;
  ctx->kernel_done = 1;

  DIAG("RT sync_kernel nrays=%d pinned=%d time=%.1fms\n",
    nrays, ctx->use_pinned, ctx->kernel_ms);

  return RES_OK;
}

/* filtered sync_kernel delegates to same implementation (inline filter
 * is applied via s3d_filter_per_ray in the pinned path) */
S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_filtered_sync_kernel(
  struct s3d_batch_trace_context* ctx)
{
  DIAG("filtered_sync_kernel ctx=%p nrays=%zu\n", (void*)ctx, ctx ? ctx->nrays : (size_t)0);
  return s3d_scene_view_trace_rays_batch_ctx_sync_kernel(ctx);
}

/* ── start_d2h — no-op on CPU ─────────────────────────────────────── */

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_start_d2h(
  struct s3d_batch_trace_context* ctx, size_t nrays)
{
  (void)ctx; (void)nrays;
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_filtered_start_d2h(
  struct s3d_batch_trace_context* ctx, size_t nrays)
{
  DIAG("filtered_start_d2h nrays=%zu\n", nrays);
  (void)ctx; (void)nrays;
  return RES_OK;
}

/* ── wait_d2h — copy results + fill stats ─────────────────────────── */

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_wait_d2h(
  struct s3d_scene_view* sv,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays,
  struct s3d_hit* hits, struct s3d_batch_trace_stats* stats)
{
  size_t i, accepted = 0;
  (void)sv; (void)requests;

  if(!ctx || !hits) return RES_BAD_ARG;

  /* Lazy evaluation: if sync_kernel was never called (dual-buffer pipeline
   * skips it), do the actual Embree work now before copying results. */
  if(!ctx->kernel_done) {
    DIAG("wait_d2h: lazy sync_kernel nrays=%zu\n", ctx->nrays);
    s3d_scene_view_trace_rays_batch_ctx_sync_kernel(ctx);
  }

  for(i = 0; i < nrays; ++i) {
    hits[i] = ctx->hits[i];
    if(!S3D_HIT_NONE(&hits[i]))
      ++accepted;
  }

  if(stats) {
    memset(stats, 0, sizeof(*stats));
    stats->total_rays       = nrays;
    stats->batch_accepted   = accepted;
    stats->batch_time_ms    = ctx->kernel_ms;
  }
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_filtered_wait_d2h(
  struct s3d_scene_view* sv,
  struct s3d_batch_trace_context* ctx,
  const struct s3d_ray_request* requests, size_t nrays,
  struct s3d_hit* hits, struct s3d_batch_trace_stats* stats)
{
  /* On CPU the filtered path is identical to unfiltered */
  return s3d_scene_view_trace_rays_batch_ctx_wait_d2h(
    sv, ctx, requests, nrays, hits, stats);
}

/* ── set_enclosure_data — no-op on CPU (filter ③ not applicable) ── */

S3D_API res_T
s3d_scene_view_set_enclosure_data(
  struct s3d_scene_view* sv,
  unsigned int shape_id,
  const unsigned int* enc_front, const unsigned int* enc_back,
  size_t num_prims)
{
  (void)sv; (void)shape_id;
  (void)enc_front; (void)enc_back; (void)num_prims;
  return RES_OK;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Batch Closest Point — CPU shim
 * ═══════════════════════════════════════════════════════════════════════ */

struct s3d_batch_cp_context {
  size_t max_queries;

  /* Deferred */
  struct s3d_scene_view*        sv;
  const struct s3d_cp_request*  requests;
  size_t                        nqueries;

  /* Results */
  struct s3d_hit* hits;

  /* Timing */
  double kernel_ms;

  /* Lazy eval flag (dual-buffer pipeline may skip sync_kernel) */
  int kernel_done;
};

S3D_API res_T
s3d_batch_cp_context_create(struct s3d_batch_cp_context** out,
                            size_t max_queries)
{
  struct s3d_batch_cp_context* ctx;
  if(!out) return RES_BAD_ARG;

  ctx = (struct s3d_batch_cp_context*)calloc(
    1, sizeof(struct s3d_batch_cp_context));
  if(!ctx) return RES_MEM_ERR;

  ctx->max_queries = max_queries;
  ctx->hits = (struct s3d_hit*)calloc(max_queries, sizeof(struct s3d_hit));
  if(!ctx->hits) { free(ctx); return RES_MEM_ERR; }

  *out = ctx;
  return RES_OK;
}

S3D_API void
s3d_batch_cp_context_destroy(struct s3d_batch_cp_context* ctx)
{
  if(!ctx) return;
  free(ctx->hits);
  free(ctx);
}

S3D_API res_T
s3d_scene_view_closest_point_batch_ctx_async(
  struct s3d_scene_view* sv,
  struct s3d_batch_cp_context* ctx,
  const struct s3d_cp_request* requests, size_t nqueries)
{
  if(!sv || !ctx) return RES_BAD_ARG;
  ctx->sv          = sv;
  ctx->requests    = requests;
  ctx->nqueries    = nqueries;
  ctx->kernel_done = 0;
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_closest_point_batch_ctx_sync_kernel(
  struct s3d_batch_cp_context* ctx)
{
  int i;
  int nq;
  double t0, t1;

  if(!ctx) return RES_BAD_ARG;
  nq = (int)ctx->nqueries;
  t0 = timer_ms();

  #pragma omp parallel for schedule(static)
  for(i = 0; i < nq; ++i) {
    const struct s3d_cp_request* r = &ctx->requests[i];
    ctx->hits[i] = S3D_HIT_NULL;
    s3d_scene_view_closest_point(ctx->sv,
      r->pos, r->radius, r->query_data, &ctx->hits[i]);
  }

  t1 = timer_ms();
  ctx->kernel_ms = t1 - t0;
  ctx->kernel_done = 1;

  DIAG("CP sync_kernel nq=%zu time=%.1fms\n", ctx->nqueries, ctx->kernel_ms);
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_closest_point_batch_ctx_start_d2h(
  struct s3d_batch_cp_context* ctx, size_t nqueries)
{
  (void)ctx; (void)nqueries;
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_closest_point_batch_ctx_wait_d2h(
  struct s3d_scene_view* sv,
  struct s3d_batch_cp_context* ctx,
  const struct s3d_cp_request* requests, size_t nqueries,
  struct s3d_hit* hits, struct s3d_batch_cp_stats* stats)
{
  size_t i, accepted = 0;
  (void)sv; (void)requests;

  if(!ctx || !hits) return RES_BAD_ARG;

  if(!ctx->kernel_done) {
    DIAG("CP wait_d2h: lazy sync_kernel nq=%zu\n", ctx->nqueries);
    s3d_scene_view_closest_point_batch_ctx_sync_kernel(ctx);
  }

  for(i = 0; i < nqueries; ++i) {
    hits[i] = ctx->hits[i];
    if(!S3D_HIT_NONE(&hits[i]))
      ++accepted;
  }

  if(stats) {
    memset(stats, 0, sizeof(*stats));
    stats->total_queries  = nqueries;
    stats->batch_accepted = accepted;
    stats->batch_time_ms  = ctx->kernel_ms;
  }
  return RES_OK;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Batch Enclosure Locate — CPU shim
 *
 * Algorithm: for each query point, use closest_point to find the nearest
 * surface primitive, then determine which side the point lies on using
 * the dot product of (query − closest) with the hit normal.
 * ═══════════════════════════════════════════════════════════════════════ */

struct s3d_batch_enc_context {
  size_t max_queries;

  /* Deferred */
  struct s3d_scene_view*                sv;
  const struct s3d_enc_locate_request*  requests;
  size_t                                nqueries;

  /* Results */
  struct s3d_enc_locate_result* results;

  /* Timing */
  double kernel_ms;

  /* Lazy eval flag */
  int kernel_done;
};

S3D_API res_T
s3d_batch_enc_context_create(struct s3d_batch_enc_context** out,
                             size_t max_queries)
{
  struct s3d_batch_enc_context* ctx;
  if(!out) return RES_BAD_ARG;

  ctx = (struct s3d_batch_enc_context*)calloc(
    1, sizeof(struct s3d_batch_enc_context));
  if(!ctx) return RES_MEM_ERR;

  ctx->max_queries = max_queries;
  ctx->results = (struct s3d_enc_locate_result*)calloc(
    max_queries, sizeof(struct s3d_enc_locate_result));
  if(!ctx->results) { free(ctx); return RES_MEM_ERR; }

  *out = ctx;
  return RES_OK;
}

S3D_API void
s3d_batch_enc_context_destroy(struct s3d_batch_enc_context* ctx)
{
  if(!ctx) return;
  free(ctx->results);
  free(ctx);
}

S3D_API res_T
s3d_scene_view_find_enclosure_batch_ctx_async(
  struct s3d_scene_view* sv,
  struct s3d_batch_enc_context* ctx,
  const struct s3d_enc_locate_request* requests, size_t nqueries)
{
  if(!sv || !ctx) return RES_BAD_ARG;
  ctx->sv          = sv;
  ctx->requests    = requests;
  ctx->nqueries    = nqueries;
  ctx->kernel_done = 0;
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_find_enclosure_batch_ctx_sync_kernel(
  struct s3d_batch_enc_context* ctx)
{
  int i;
  int nq;
  double t0, t1;

  if(!ctx) return RES_BAD_ARG;
  nq = (int)ctx->nqueries;
  t0 = timer_ms();

  #pragma omp parallel for schedule(static)
  for(i = 0; i < nq; ++i) {
    const struct s3d_enc_locate_request* req = &ctx->requests[i];
    struct s3d_enc_locate_result* res = &ctx->results[i];
    struct s3d_hit hit = S3D_HIT_NULL;

    /* Stage 1: find nearest surface primitive via closest_point */
    s3d_scene_view_closest_point(ctx->sv,
      req->pos, FLT_MAX, NULL, &hit);

    if(S3D_HIT_NONE(&hit)) {
      /* No geometry found — degenerate */
      res->prim_id  = -1;
      res->distance = FLT_MAX;
      res->side     = -1;
      res->enc_id   = S3D_INVALID_ID;
      continue;
    }

    res->prim_id  = (int32_t)hit.prim.scene_prim_id;
    res->distance = hit.distance;

    /* Stage 2: determine side via dot(normal, query − closest_point).
     *
     * The hit normal is the unnormalized geometry face normal.
     * The closest point on the surface = query_pos - normal_hat * distance,
     * but we only need the sign of dot(normal, query − surface_pt).
     * Since distance = |query - surface_pt|, and the direction from
     * surface to query is (query - surface_pt), the sign of
     * dot(normal, query - surface_pt) tells us the side.
     *
     * For a closest_point query, hit.normal is the face normal at the
     * hit point.  We compute: query_pos interpolated via barycentric —
     * but actually, we can use a simpler approach:
     *
     * Get the interpolated position at the hit's barycentric coords,
     * then compute the ray from there to our query point.
     */
    {
      struct s3d_attrib pos_attrib;
      float dx, dy, dz, dot_val;

      s3d_primitive_get_attrib(&hit.prim, S3D_POSITION,
                               hit.uv, &pos_attrib);

      dx = req->pos[0] - pos_attrib.value[0];
      dy = req->pos[1] - pos_attrib.value[1];
      dz = req->pos[2] - pos_attrib.value[2];

      dot_val = hit.normal[0] * dx
              + hit.normal[1] * dy
              + hit.normal[2] * dz;

      if(dot_val > 0.0f)
        res->side = 0;   /* front */
      else if(dot_val < 0.0f)
        res->side = 1;   /* back */
      else
        res->side = -1;  /* degenerate — exactly on surface */
    }

    res->enc_id = S3D_INVALID_ID; /* resolved by solver, not backend */
  }

  t1 = timer_ms();
  ctx->kernel_ms = t1 - t0;
  ctx->kernel_done = 1;
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_find_enclosure_batch_ctx_start_d2h(
  struct s3d_batch_enc_context* ctx, size_t nqueries)
{
  (void)ctx; (void)nqueries;
  return RES_OK;
}

S3D_API res_T
s3d_scene_view_find_enclosure_batch_ctx_wait_d2h(
  struct s3d_scene_view* sv,
  struct s3d_batch_enc_context* ctx,
  const struct s3d_enc_locate_request* requests, size_t nqueries,
  struct s3d_enc_locate_result* results,
  struct s3d_batch_enc_stats* stats)
{
  size_t i, resolved = 0, degenerate = 0, missed = 0;
  (void)sv; (void)requests;

  if(!ctx || !results) return RES_BAD_ARG;

  if(!ctx->kernel_done) {
    DIAG("ENC wait_d2h: lazy sync_kernel nq=%zu\n", ctx->nqueries);
    s3d_scene_view_find_enclosure_batch_ctx_sync_kernel(ctx);
  }

  for(i = 0; i < nqueries; ++i) {
    results[i] = ctx->results[i];
    if(results[i].prim_id < 0)
      ++missed;
    else if(results[i].side < 0)
      ++degenerate;
    else
      ++resolved;
  }

  if(stats) {
    memset(stats, 0, sizeof(*stats));
    stats->total_queries = nqueries;
    stats->resolved      = resolved;
    stats->degenerate    = degenerate;
    stats->missed        = missed;
    stats->batch_time_ms = ctx->kernel_ms;
  }
  return RES_OK;
}
