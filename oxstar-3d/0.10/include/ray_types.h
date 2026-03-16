/*
 * ray_types.h - Ray, hit result, and geometry data structures
 * Shared between host code, CUDA kernels, and OptiX device programs.
 * No OptiX dependency - works everywhere.
 *
 * Extensions:
 *   E1 - HitResult includes geom_id, inst_id, geometric normal
 *   E3 - MultiHitResult (top-K hits, K = MAX_MULTI_HITS)
 *   E4 - SphereData for sphere geometry support
 */
#pragma once

#ifndef __CUDACC__
#include <cuda_runtime.h>
#endif

/* ---- Ray ---- */
struct Ray {
    float3 origin;
    float  tmin;
    float3 direction;
    float  tmax;
};

/* Plan E: Verify Ray layout matches C-visible s3d_ray_pinned (32 bytes).
 * s3d_ray_pinned is defined in s3d.h as 8 consecutive floats. */
static_assert(sizeof(Ray) == 32, "Ray must be 32 bytes to match s3d_ray_pinned");

/* ---- Hit Result (E1 extended) ---- */
struct HitResult {
    float        t;            /* hit distance; < 0 means miss             */
    float        bary_u;       /* barycentric u coordinate                 */
    float        bary_v;       /* barycentric v coordinate                 */
    unsigned int prim_idx;     /* primitive index (GAS-local)              */
    unsigned int geom_id;      /* geometry ID from SBT record              */
    unsigned int inst_id;      /* instance ID (0xFFFFFFFF if no instancing)*/
    float        normal[3];    /* un-normalized geometric normal           */
};

/* ---- Multi-Hit Result (E3) ---- */
#define MAX_MULTI_HITS 2u

struct MultiHitResult {
    unsigned int count;                    /* actual hit count [0, MAX_MULTI_HITS] */
    HitResult    hits[MAX_MULTI_HITS];     /* sorted by distance (ascending)       */
};

/* ---- Sphere Data (E4) ---- */
struct SphereData {
    float3 center;
    float  radius;
};

/* ---- Per-Ray Filter Data (L4: GPU inline filter) ---- */
struct FilterPerRayData {
    unsigned int hit_from_prim_id;  /* GAS-local prim_idx of origin surface; 0xFFFFFFFF = none    */
    unsigned int hit_from_geom_id;  /* geom_id of origin surface; 0xFFFFFFFF = none               */
    unsigned int enc_id;            /* enclosure ID the ray is in; 0xFFFFFFFF = no enc filter     */
    float        epsilon;           /* near-distance self-intersection threshold                  */
};

/* ---- GPU Postprocess: Per-geometry lookup entry (uploaded once at scene build) ---- */
struct GpuPpEntry {
    unsigned int shape_id;      /* API-level shape_id  → s3d_hit.prim.geom_id  */
    unsigned int inst_id;       /* inst->id or 0xFFFFFFFF → s3d_hit.prim.inst_id */
    unsigned char shape_type;   /* 0 = mesh, 1 = sphere (matches OX_SHAPE_*)    */
    unsigned char flip_surface; /* build-time snapshot of shape->flip_surface    */
    unsigned char pad[2];
};

/* ---- GPU-side mirror of s3d_hit (56 bytes, identical binary layout) ----
 * s3d_hit contains void* pointers (shape__, inst__) which are 8 bytes on
 * the host.  CUDA device code uses unsigned long long as a stand-in with
 * the same size and alignment.  The kernel writes 0 for both because
 * merged_pass never dereferences them.
 *
 * Layout must match s3d_primitive + s3d_hit exactly:
 *   [0..3]   prim_id        (unsigned)
 *   [4..7]   geom_id        (unsigned)
 *   [8..11]  inst_id        (unsigned)
 *   [12..15] scene_prim_id  (unsigned)
 *   [16..23] shape__        (void* / ull)
 *   [24..31] inst__         (void* / ull)
 *   [32..43] normal[3]      (float)
 *   [44..51] uv[2]          (float)
 *   [52..55] distance       (float)
 */
struct GpuS3dHit {
    unsigned int       prim_id;
    unsigned int       geom_id;
    unsigned int       inst_id;
    unsigned int       scene_prim_id;
    unsigned long long shape_ptr;      /* always 0 */
    unsigned long long inst_ptr;       /* always 0 */
    float              normal[3];
    float              uv[2];
    float              distance;
};


