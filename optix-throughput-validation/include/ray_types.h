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
