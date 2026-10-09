/*
 * nn_programs.cu - OptiX 9.1.0 device programs for closest-point queries
 * Compiled to PTX via nvcc --ptx (NOT as a regular CUDA object).
 *
 * Algorithm (based on RTNN / PPoPP 2022, adapted for triangles + spheres):
 *   Each primitive (triangle or sphere) is represented as an AABB (its tight
 *   bounding box expanded by search_radius). Query points are dispatched
 *   as zero-length rays (tmin=0, tmax=1e-16). OptiX BVH traversal finds
 *   all AABBs containing the query point. The custom intersection program
 *   computes the exact closest-point distance and tracks the nearest
 *   primitive via payload registers.
 *
 * Extensions:
 *   E2 - Enhanced CP: 9-slot payload carries dist_sq, prim_idx, normal(3),
 *        UV(2), plus 2 padding slots
 *   E4 - __intersection__nn_sphere for closest-point-on-sphere
 *
 * Programs:
 *   __raygen__nn             - Dispatch query as zero-length ray, collect result
 *   __raygen__cp             - Enhanced CP dispatch with CPQuery/CPResult (E2)
 *   __intersection__nn       - Point-to-triangle distance, update nearest
 *   __intersection__nn_sphere - Point-to-sphere distance (E4)
 *   __miss__nn               - No-op
 */

#include <optix.h>
#include "unified_params.h"

/* ---- Launch parameters (device constant) ---- */
extern "C" {
__constant__ UnifiedParams params;
}

/* ---- Helper: dot product ---- */
static __forceinline__ __device__ float dot3(float3 a, float3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* ---- Helper: subtract ---- */
static __forceinline__ __device__ float3 sub3(float3 a, float3 b)
{
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

/* ---- Helper: add ---- */
static __forceinline__ __device__ float3 add3(float3 a, float3 b)
{
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

/* ---- Helper: scale ---- */
static __forceinline__ __device__ float3 scale3(float3 a, float s)
{
    return make_float3(a.x * s, a.y * s, a.z * s);
}

/* ---- Helper: cross product ---- */
static __forceinline__ __device__ float3 cross3(float3 a, float3 b)
{
    return make_float3(a.y * b.z - a.z * b.y,
                       a.z * b.x - a.x * b.z,
                       a.x * b.y - a.y * b.x);
}

/* ---- Double-precision helpers for closestPointOnTriangle ---- */
struct double3_t { double x, y, z; };

static __forceinline__ __device__ double3_t make_d3(double x, double y, double z)
{ double3_t r; r.x = x; r.y = y; r.z = z; return r; }

static __forceinline__ __device__ double3_t f3_to_d3(float3 v)
{ return make_d3((double)v.x, (double)v.y, (double)v.z); }

static __forceinline__ __device__ float3 d3_to_f3(double3_t v)
{ return make_float3((float)v.x, (float)v.y, (float)v.z); }

static __forceinline__ __device__ double3_t dsub3(double3_t a, double3_t b)
{ return make_d3(a.x - b.x, a.y - b.y, a.z - b.z); }

static __forceinline__ __device__ double3_t dadd3(double3_t a, double3_t b)
{ return make_d3(a.x + b.x, a.y + b.y, a.z + b.z); }

static __forceinline__ __device__ double3_t dscale3(double3_t a, double s)
{ return make_d3(a.x * s, a.y * s, a.z * s); }

static __forceinline__ __device__ double ddot3(double3_t a, double3_t b)
{ return a.x * b.x + a.y * b.y + a.z * b.z; }

/* ===========================================================================
 * Closest-point-on-triangle with barycentric coordinates
 *
 * Returns: dist_sq, out_u, out_v (barycentric coords of closest point)
 *   closest = (1 - u - v)*v0 + u*v1 + v*v2
 *
 * Based on Voronoi region method (Ericson, "Real-Time Collision Detection")
 * =========================================================================*/
/*
 * closestPointOnTriangle — Voronoi region closest-point (DOUBLE PRECISION)
 *
 * Returns dist² and outputs barycentrics in **s3d convention**:
 *   P = out_w * v0  +  out_u * v1  +  (1 - out_w - out_u) * v2
 *   out_w = weight of v0,  out_u = weight of v1
 *
 * All internal computation in double to avoid catastrophic cancellation
 * in Voronoi area tests (d1*d4 - d3*d2, etc.). Output proj cast to float.
 */
static __forceinline__ __device__ float closestPointOnTriangle(
    float3 p, float3 v0, float3 v1, float3 v2,
    float& out_w, float& out_u, float3& out_proj)
{
    /* Promote all inputs to double */
    double3_t dp  = f3_to_d3(p);
    double3_t dv0 = f3_to_d3(v0);
    double3_t dv1 = f3_to_d3(v1);
    double3_t dv2 = f3_to_d3(v2);

    double3_t ab = dsub3(dv1, dv0);
    double3_t ac = dsub3(dv2, dv0);
    double3_t ap = dsub3(dp,  dv0);

    double d1 = ddot3(ab, ap);
    double d2 = ddot3(ac, ap);
    /* Vertex region A (v0) */
    if (d1 <= 0.0 && d2 <= 0.0) {
        out_w = 1.0f; out_u = 0.0f;
        out_proj = v0;
        double3_t diff = dsub3(dp, dv0);
        return (float)ddot3(diff, diff);
    }

    double3_t bp = dsub3(dp, dv1);
    double d3 = ddot3(ab, bp);
    double d4 = ddot3(ac, bp);
    /* Vertex region B (v1) */
    if (d3 >= 0.0 && d4 <= d3) {
        out_w = 0.0f; out_u = 1.0f;
        out_proj = v1;
        double3_t diff = dsub3(dp, dv1);
        return (float)ddot3(diff, diff);
    }

    /* Edge region AB (v0–v1) */
    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        double inv = 1.0 / (d1 - d3);
        double w_u = d1 * inv;
        double w_w = (-d3) * inv;
        out_u = (float)w_u;
        out_w = (float)w_w;
        double3_t proj = dadd3(dv0, dscale3(ab, w_u));
        out_proj = d3_to_f3(proj);
        double3_t diff = dsub3(dp, proj);
        return (float)ddot3(diff, diff);
    }

    double3_t cp = dsub3(dp, dv2);
    double d5 = ddot3(ab, cp);
    double d6 = ddot3(ac, cp);
    /* Vertex region C (v2) */
    if (d6 >= 0.0 && d5 <= d6) {
        out_w = 0.0f; out_u = 0.0f;
        out_proj = v2;
        double3_t diff = dsub3(dp, dv2);
        return (float)ddot3(diff, diff);
    }

    /* Edge region AC (v0–v2) */
    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        double inv = 1.0 / (d2 - d6);
        double t_ac = d2 * inv;
        double w_w  = (-d6) * inv;
        out_w = (float)w_w;
        out_u = 0.0f;
        double3_t proj = dadd3(dv0, dscale3(ac, t_ac));
        out_proj = d3_to_f3(proj);
        double3_t diff = dsub3(dp, proj);
        return (float)ddot3(diff, diff);
    }

    /* Edge region BC (v1–v2) */
    double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        double e1 = d4 - d3;
        double e2 = d5 - d6;
        double inv = 1.0 / (e1 + e2);
        out_w = 0.0f;
        out_u = (float)(e2 * inv);
        double w_bc = e1 * inv;
        double3_t bc = dsub3(dv2, dv1);
        double3_t proj = dadd3(dv1, dscale3(bc, w_bc));
        out_proj = d3_to_f3(proj);
        double3_t diff = dsub3(dp, proj);
        return (float)ddot3(diff, diff);
    }

    /* Interior — project onto triangle face */
    double denom = 1.0 / (va + vb + vc);
    double w_w  = va * denom;
    double w_u  = vb * denom;
    double wt_v2 = vc * denom;
    out_w = (float)w_w;
    out_u = (float)w_u;
    double3_t proj = dadd3(dv0, dadd3(dscale3(ab, w_u), dscale3(ac, wt_v2)));
    out_proj = d3_to_f3(proj);
    double3_t diff = dsub3(dp, proj);
    return (float)ddot3(diff, diff);
}

/* ===========================================================================
 * __raygen__nn — simple CP dispatch (NNResult, 9 payloads padded)
 * =========================================================================*/
extern "C" __global__ void __raygen__nn()
{
    const uint3        idx        = optixGetLaunchIndex();
    const uint3        dim        = optixGetLaunchDimensions();
    const unsigned int linear_idx = idx.y * dim.x + idx.x;

    if (linear_idx >= params.count)
        return;

    const float3 query = params.queries[linear_idx];

    unsigned int p0 = __float_as_uint(1e30f);   /* best dist_sq             */
    unsigned int p1 = 0xFFFFFFFFu;              /* prim_idx                 */
    unsigned int p2 = 0;                        /* normal.x (E2)            */
    unsigned int p3 = 0;                        /* normal.y                 */
    unsigned int p4 = 0;                        /* normal.z                 */
    unsigned int p5 = 0;                        /* uv_w (E2)               */
    unsigned int p6 = 0;                        /* uv_u                    */
    unsigned int p7 = 0;                        /* closest_pos.x            */
    unsigned int p8 = 0;                        /* closest_pos.y            */
    unsigned int p9 = 0;                        /* closest_pos.z            */

    optixTrace(
        params.handle,
        query,
        make_float3(1.0f, 0.0f, 0.0f),
        0.0f,
        1e-16f,
        0.0f,
        OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_NONE,
        0, 1, 0,
        p0, p1, p2, p3, p4, p5, p6, p7, p8, p9
    );

    NNResult result;
    if (p1 != 0xFFFFFFFFu) {
        result.distance = sqrtf(__uint_as_float(p0));
        result.prim_idx = p1;
    } else {
        result.distance  = -1.0f;
        result.prim_idx  = 0xFFFFFFFFu;
    }

    params.results[linear_idx] = result;
}

/* ===========================================================================
 * __raygen__cp — enhanced CP dispatch (E2: CPQuery → CPResult, 9 payloads)
 * Each query has its own search radius.
 * =========================================================================*/
extern "C" __global__ void __raygen__cp()
{
    const uint3        idx        = optixGetLaunchIndex();
    const uint3        dim        = optixGetLaunchDimensions();
    const unsigned int linear_idx = idx.y * dim.x + idx.x;

    if (linear_idx >= params.count)
        return;

    const CPQuery q = params.cp_queries[linear_idx];

    unsigned int p0 = __float_as_uint(1e30f);
    unsigned int p1 = 0xFFFFFFFFu;
    unsigned int p2 = 0, p3 = 0, p4 = 0;
    unsigned int p5 = 0, p6 = 0;
    unsigned int p7 = 0, p8 = 0, p9 = 0;

    optixTrace(
        params.handle,
        q.position,
        make_float3(1.0f, 0.0f, 0.0f),
        0.0f,
        1e-16f,
        0.0f,
        OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_NONE,
        0, 1, 0,
        p0, p1, p2, p3, p4, p5, p6, p7, p8, p9
    );

    CPResult cr;
    if (p1 != 0xFFFFFFFFu) {
        float dist = sqrtf(__uint_as_float(p0));
        /* Check against per-query radius (exclusive upper bound) */
        if (dist < q.radius) {
            cr.distance  = dist;
            cr.prim_idx  = p1;
            cr.normal[0] = __uint_as_float(p2);
            cr.normal[1] = __uint_as_float(p3);
            cr.normal[2] = __uint_as_float(p4);
            cr.uv[0]     = __uint_as_float(p5);
            cr.uv[1]     = __uint_as_float(p6);
            cr.closest_pos[0] = __uint_as_float(p7);
            cr.closest_pos[1] = __uint_as_float(p8);
            cr.closest_pos[2] = __uint_as_float(p9);
            cr.geom_id   = 0xFFFFFFFFu;  /* resolved on host via prim ranges */
            cr.inst_id   = 0xFFFFFFFFu;
        } else {
            cr.distance  = -1.0f;
            cr.prim_idx  = 0xFFFFFFFFu;
            cr.normal[0] = cr.normal[1] = cr.normal[2] = 0.0f;
            cr.uv[0] = cr.uv[1] = 0.0f;
            cr.closest_pos[0] = cr.closest_pos[1] = cr.closest_pos[2] = 0.0f;
            cr.geom_id  = 0xFFFFFFFFu;
            cr.inst_id  = 0xFFFFFFFFu;
        }
    } else {
        cr.distance  = -1.0f;
        cr.prim_idx  = 0xFFFFFFFFu;
        cr.normal[0] = cr.normal[1] = cr.normal[2] = 0.0f;
        cr.uv[0] = cr.uv[1] = 0.0f;
        cr.closest_pos[0] = cr.closest_pos[1] = cr.closest_pos[2] = 0.0f;
        cr.geom_id  = 0xFFFFFFFFu;
        cr.inst_id  = 0xFFFFFFFFu;
    }

    params.cp_results[linear_idx] = cr;
}

/* ===========================================================================
 * __intersection__nn — closest-point-on-triangle (E2 enhanced with normal+UV)
 * =========================================================================*/
extern "C" __global__ void __intersection__nn()
{
    const unsigned int prim_idx = optixGetPrimitiveIndex();
    const float3       query    = optixGetWorldRayOrigin();

    const uint3  tri = params.nn_indices[prim_idx];
    const float3 v0  = params.nn_vertices[tri.x];
    const float3 v1  = params.nn_vertices[tri.y];
    const float3 v2  = params.nn_vertices[tri.z];

    float uv_u, uv_v;
    float3 proj;
    const float dist_sq = closestPointOnTriangle(query, v0, v1, v2, uv_u, uv_v, proj);

    const float current_min_sq = __uint_as_float(optixGetPayload_0());

    if (dist_sq < current_min_sq) {
        optixSetPayload_0(__float_as_uint(dist_sq));
        optixSetPayload_1(prim_idx);

        /* Geometric normal: cross(v2-v0, v1-v0) — left-hand convention (s3d) */
        float3 n = cross3(sub3(v2, v0), sub3(v1, v0));
        optixSetPayload_2(__float_as_uint(n.x));
        optixSetPayload_3(__float_as_uint(n.y));
        optixSetPayload_4(__float_as_uint(n.z));

        /* Barycentric coords of closest point */
        optixSetPayload_5(__float_as_uint(uv_u));
        optixSetPayload_6(__float_as_uint(uv_v));

        /* Exact closest point position for host-side precision recovery */
        optixSetPayload_7(__float_as_uint(proj.x));
        optixSetPayload_8(__float_as_uint(proj.y));
        optixSetPayload_9(__float_as_uint(proj.z));
    }

    /* Do NOT call optixReportIntersection — continue BVH traversal */
}

/* ===========================================================================
 * __intersection__nn_sphere — closest-point-on-sphere (E4)
 * =========================================================================*/
extern "C" __global__ void __intersection__nn_sphere()
{
    const unsigned int prim_idx = optixGetPrimitiveIndex();
    const float3       query    = optixGetWorldRayOrigin();

    const float3 center = params.sphere_centers[prim_idx];
    const float  radius = params.sphere_radii[prim_idx];

    float3 d = sub3(query, center);
    float  dist_to_center = sqrtf(dot3(d, d));

    float dist_to_surface;
    if (dist_to_center <= 1e-10f) {
        dist_to_surface = radius;
    } else {
        dist_to_surface = fabsf(dist_to_center - radius);
    }

    float dist_sq = dist_to_surface * dist_to_surface;

    const float current_min_sq = __uint_as_float(optixGetPayload_0());

    if (dist_sq < current_min_sq) {
        optixSetPayload_0(__float_as_uint(dist_sq));
        optixSetPayload_1(prim_idx);

        /* Normal = direction from center to query point */
        float inv_d = (dist_to_center > 1e-10f) ? (1.0f / dist_to_center) : 0.0f;
        optixSetPayload_2(__float_as_uint(d.x * inv_d));
        optixSetPayload_3(__float_as_uint(d.y * inv_d));
        optixSetPayload_4(__float_as_uint(d.z * inv_d));

        /* UV for sphere: (0, 0) — not applicable */
        optixSetPayload_5(__float_as_uint(0.0f));
        optixSetPayload_6(__float_as_uint(0.0f));

        /* Closest surface point */
        float3 cp_pos;
        if (dist_to_center > 1e-10f) {
            cp_pos = add3(center, scale3(d, radius * inv_d));
        } else {
            cp_pos = make_float3(center.x + radius, center.y, center.z);
        }
        optixSetPayload_7(__float_as_uint(cp_pos.x));
        optixSetPayload_8(__float_as_uint(cp_pos.y));
        optixSetPayload_9(__float_as_uint(cp_pos.z));
    }
}

/* ===========================================================================
 * __miss__nn — no-op
 * =========================================================================*/
extern "C" __global__ void __miss__nn()
{
    /* Nothing to do — results are in payload */
}
