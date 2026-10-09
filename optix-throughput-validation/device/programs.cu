/*
 * programs.cu - OptiX 9.1.0 device programs (RT + multi-hit + sphere)
 * Compiled to PTX via nvcc --ptx (NOT as a regular CUDA object).
 *
 * Extensions:
 *   E1 - 9-slot payload: t, bary_u, bary_v, prim_idx, geom_id, inst_id,
 *        normal_x, normal_y, normal_z
 *   E3 - __raygen__mh + __anyhit__mh for top-K multi-hit tracing
 *   E4 - __intersection__sphere for ray-sphere intersection
 *   E6 - __closesthit__ch reads per-geometry SBT data for geom_id
 *
 * Programs:
 *   __raygen__rg            - Single-hit buffer-based ray tracing
 *   __raygen__mh            - Multi-hit buffer-based ray tracing (E3)
 *   __miss__ms              - Records miss (t = -1)
 *   __closesthit__ch        - Records hit with geom_id, inst_id, normal (E1)
 *   __anyhit__mh            - Records every hit for multi-hit (E3)
 *   __intersection__sphere  - Ray-sphere analytic intersection (E4)
 */

#include <optix.h>
#include "unified_params.h"

/* ---- Launch parameters (device constant) ---- */
extern "C" {
__constant__ UnifiedParams params;
}

/* ---- Payload helpers (10 slots) ---- */
static __forceinline__ __device__ void setPayloadMiss()
{
    optixSetPayload_0(__float_as_uint(-1.0f));  /* t           */
    optixSetPayload_1(__float_as_uint(0.0f));   /* bary_u      */
    optixSetPayload_2(__float_as_uint(0.0f));   /* bary_v      */
    optixSetPayload_3(0xFFFFFFFFu);             /* prim_idx    */
    optixSetPayload_4(0xFFFFFFFFu);             /* geom_id     */
    optixSetPayload_5(0xFFFFFFFFu);             /* inst_id     */
    optixSetPayload_6(__float_as_uint(0.0f));   /* normal.x    */
    optixSetPayload_7(__float_as_uint(0.0f));   /* normal.y    */
    optixSetPayload_8(__float_as_uint(0.0f));   /* normal.z    */
    optixSetPayload_9(0);                       /* unused (CP slot) */
}

static __forceinline__ __device__ void setPayloadHit(
    float t, float bu, float bv, unsigned int pid,
    unsigned int gid, unsigned int iid,
    float nx, float ny, float nz)
{
    optixSetPayload_0(__float_as_uint(t));
    optixSetPayload_1(__float_as_uint(bu));
    optixSetPayload_2(__float_as_uint(bv));
    optixSetPayload_3(pid);
    optixSetPayload_4(gid);
    optixSetPayload_5(iid);
    optixSetPayload_6(__float_as_uint(nx));
    optixSetPayload_7(__float_as_uint(ny));
    optixSetPayload_8(__float_as_uint(nz));
    optixSetPayload_9(0);                       /* unused (CP slot) */
}

/* ===========================================================================
 * __raygen__rg — single-hit ray tracing (9 payloads)
 * =========================================================================*/
extern "C" __global__ void __raygen__rg()
{
    const uint3        idx        = optixGetLaunchIndex();
    const uint3        dim        = optixGetLaunchDimensions();
    const unsigned int linear_idx = idx.y * dim.x + idx.x;

    if (linear_idx >= params.count)
        return;

    const Ray ray = params.rays[linear_idx];

    unsigned int p0, p1, p2, p3, p4, p5, p6, p7, p8, p9;
    optixTrace(
        params.handle,
        ray.origin,
        ray.direction,
        ray.tmin,
        ray.tmax,
        0.0f,
        OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_DISABLE_ANYHIT,
        RAY_TYPE_RADIANCE,
        RAY_TYPE_COUNT,
        RAY_TYPE_RADIANCE,
        p0, p1, p2, p3, p4, p5, p6, p7, p8, p9
    );

    HitResult hit;
    hit.t        = __uint_as_float(p0);
    hit.bary_u   = __uint_as_float(p1);
    hit.bary_v   = __uint_as_float(p2);
    hit.prim_idx = p3;
    hit.geom_id  = p4;
    hit.inst_id  = p5;
    hit.normal[0] = __uint_as_float(p6);
    hit.normal[1] = __uint_as_float(p7);
    hit.normal[2] = __uint_as_float(p8);

    params.hits[linear_idx] = hit;
}

/* ===========================================================================
 * __raygen__mh — multi-hit ray tracing (E3)
 * Initializes MultiHitResult, traces without DISABLE_ANYHIT so __anyhit__mh
 * is invoked for every intersection. After trace, sorts hits by distance.
 * =========================================================================*/
extern "C" __global__ void __raygen__mh()
{
    const uint3        idx        = optixGetLaunchIndex();
    const uint3        dim        = optixGetLaunchDimensions();
    const unsigned int linear_idx = idx.y * dim.x + idx.x;

    if (linear_idx >= params.count)
        return;

    /* Initialize multi-hit result */
    params.multi_hits[linear_idx].count = 0;

    const Ray ray = params.rays[linear_idx];

    unsigned int p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;
    optixTrace(
        params.handle,
        ray.origin,
        ray.direction,
        ray.tmin,
        ray.tmax,
        0.0f,
        OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_NONE,   /* DO NOT disable any-hit */
        RAY_TYPE_RADIANCE,
        RAY_TYPE_COUNT,
        RAY_TYPE_RADIANCE,
        p0, p1, p2, p3, p4, p5, p6, p7, p8, p9
    );

    /* Sort hits by distance (ascending) — bubble sort for K=2 */
    MultiHitResult& result = params.multi_hits[linear_idx];
    if (result.count == 2 && result.hits[0].t > result.hits[1].t) {
        HitResult tmp    = result.hits[0];
        result.hits[0]   = result.hits[1];
        result.hits[1]   = tmp;
    }
}

/* ===========================================================================
 * __miss__ms — records miss
 * =========================================================================*/
extern "C" __global__ void __miss__ms()
{
    setPayloadMiss();
}

/* ===========================================================================
 * __closesthit__ch — single-hit closest hit (E1/E4/E6)
 * Reads geom_id from SBT data, inst_id from optixGetInstanceId(),
 * computes geometric normal from triangle vertices or sphere geometry.
 * =========================================================================*/
extern "C" __global__ void __closesthit__ch()
{
    const float          t    = optixGetRayTmax();
    const unsigned int   prim = optixGetPrimitiveIndex();
    const HitGroupData*  data = reinterpret_cast<const HitGroupData*>(optixGetSbtDataPointer());
    const unsigned int   gid  = data->geom_id;
    const unsigned int   iid  = optixGetInstanceId();

    float bu = 0.0f, bv = 0.0f;
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;

    if (optixIsTriangleHit()) {
        /* Triangle hit — barycentrics from built-in IS */
        const float2 bary = optixGetTriangleBarycentrics();
        bu = bary.x;
        bv = bary.y;

        /* Compute geometric normal: cross(e1, e2) in object space */
        if (data->vertices && data->indices) {
            const uint3  tri = data->indices[prim];
            const float3 v0  = data->vertices[tri.x];
            const float3 v1  = data->vertices[tri.y];
            const float3 v2  = data->vertices[tri.z];
            const float3 e1  = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
            const float3 e2  = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
            nx = e1.y * e2.z - e1.z * e2.y;
            ny = e1.z * e2.x - e1.x * e2.z;
            nz = e1.x * e2.y - e1.y * e2.x;
        }
        /* Transform normal from object space to world space (IAS instance) */
        const float3 wn = optixTransformNormalFromObjectToWorldSpace(
                              make_float3(nx, ny, nz));
        nx = wn.x;  ny = wn.y;  nz = wn.z;
    } else {
        /* Sphere hit (E4) — compute normal from hit point */
        if (data->sphere_centers) {
            const float3 origin = optixGetWorldRayOrigin();
            const float3 dir    = optixGetWorldRayDirection();
            const float3 hp     = { origin.x + t * dir.x,
                                    origin.y + t * dir.y,
                                    origin.z + t * dir.z };
            const float3 center = data->sphere_centers[prim];
            nx = hp.x - center.x;
            ny = hp.y - center.y;
            nz = hp.z - center.z;
        }
    }

    /* E7: flip normal if geometry has flip_surface set */
    if (data->flip_normal) {
        nx = -nx; ny = -ny; nz = -nz;
    }

    setPayloadHit(t, bu, bv, prim, gid, iid, nx, ny, nz);
}

/* ===========================================================================
 * __anyhit__mh — multi-hit any-hit program (E3)
 * Records every intersection into params.multi_hits[ray_idx].
 * Keeps only the K closest hits; ignores intersections farther than K-th.
 * =========================================================================*/
extern "C" __global__ void __anyhit__mh()
{
    const uint3        idx        = optixGetLaunchIndex();
    const uint3        dim        = optixGetLaunchDimensions();
    const unsigned int linear_idx = idx.y * dim.x + idx.x;

    const float          t_new = optixGetRayTmax();
    const unsigned int   prim  = optixGetPrimitiveIndex();
    const HitGroupData*  data  = reinterpret_cast<const HitGroupData*>(optixGetSbtDataPointer());
    const unsigned int   gid   = data->geom_id;
    const unsigned int   iid   = optixGetInstanceId();

    float bu = 0.0f, bv = 0.0f;
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;

    if (optixIsTriangleHit()) {
        const float2 bary = optixGetTriangleBarycentrics();
        bu = bary.x;
        bv = bary.y;
        if (data->vertices && data->indices) {
            const uint3  tri = data->indices[prim];
            const float3 v0  = data->vertices[tri.x];
            const float3 v1  = data->vertices[tri.y];
            const float3 v2  = data->vertices[tri.z];
            const float3 e1  = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
            const float3 e2  = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
            nx = e1.y * e2.z - e1.z * e2.y;
            ny = e1.z * e2.x - e1.x * e2.z;
            nz = e1.x * e2.y - e1.y * e2.x;
        }
        /* Transform normal from object space to world space (IAS instance) */
        const float3 wn = optixTransformNormalFromObjectToWorldSpace(
                              make_float3(nx, ny, nz));
        nx = wn.x;  ny = wn.y;  nz = wn.z;
    } else {
        if (data->sphere_centers) {
            const float3 origin = optixGetWorldRayOrigin();
            const float3 dir    = optixGetWorldRayDirection();
            const float3 hp     = { origin.x + t_new * dir.x,
                                    origin.y + t_new * dir.y,
                                    origin.z + t_new * dir.z };
            const float3 center = data->sphere_centers[prim];
            nx = hp.x - center.x;
            ny = hp.y - center.y;
            nz = hp.z - center.z;
        }
    }

    /* E7: flip normal if geometry has flip_surface set */
    if (data->flip_normal) {
        nx = -nx; ny = -ny; nz = -nz;
    }

    /* Build hit record */
    HitResult new_hit;
    new_hit.t         = t_new;
    new_hit.bary_u    = bu;
    new_hit.bary_v    = bv;
    new_hit.prim_idx  = prim;
    new_hit.geom_id   = gid;
    new_hit.inst_id   = iid;
    new_hit.normal[0] = nx;
    new_hit.normal[1] = ny;
    new_hit.normal[2] = nz;

    MultiHitResult& result = params.multi_hits[linear_idx];
    unsigned int cnt = result.count;

    if (cnt < MAX_MULTI_HITS) {
        result.hits[cnt] = new_hit;
        result.count     = cnt + 1;
    } else {
        /* Find farthest hit and replace if new hit is closer */
        unsigned int farthest = 0;
        float max_t = result.hits[0].t;
        for (unsigned int i = 1; i < MAX_MULTI_HITS; ++i) {
            if (result.hits[i].t > max_t) {
                max_t    = result.hits[i].t;
                farthest = i;
            }
        }
        if (t_new < max_t) {
            result.hits[farthest] = new_hit;
        } else {
            optixIgnoreIntersection();
        }
    }
}

/* ===========================================================================
 * __intersection__sphere — ray-sphere analytic intersection (E4)
 * Solves |O + t*D - C|^2 = r^2 for the nearest positive t in [tmin, tmax].
 * Reports intersection via optixReportIntersection (hit kind = 0).
 * =========================================================================*/
extern "C" __global__ void __intersection__sphere()
{
    const unsigned int   prim = optixGetPrimitiveIndex();
    const HitGroupData*  data = reinterpret_cast<const HitGroupData*>(optixGetSbtDataPointer());

    const float3 center = data->sphere_centers[prim];
    const float  radius = data->sphere_radii[prim];

    const float3 origin = optixGetObjectRayOrigin();
    const float3 dir    = optixGetObjectRayDirection();
    const float  tmin   = optixGetRayTmin();
    const float  tmax   = optixGetRayTmax();

    const float3 oc = { origin.x - center.x,
                        origin.y - center.y,
                        origin.z - center.z };

    const float a = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    const float b = 2.0f * (oc.x * dir.x + oc.y * dir.y + oc.z * dir.z);
    const float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - radius * radius;

    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f)
        return;

    const float sqrtD   = sqrtf(discriminant);
    const float inv_2a  = 0.5f / a;
    const float t_near  = (-b - sqrtD) * inv_2a;
    const float t_far   = (-b + sqrtD) * inv_2a;

    if (t_near >= tmin && t_near <= tmax) {
        optixReportIntersection(t_near, 0);
    } else if (t_far >= tmin && t_far <= tmax) {
        optixReportIntersection(t_far, 0);
    }
}
