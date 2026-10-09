/*
 * nn_types.h - Closest-point and enclosure query data structures
 * Shared between host code, CUDA kernels, and OptiX device programs.
 * No OptiX dependency - works everywhere.
 *
 * Extensions:
 *   E2 - CPResult / CPQuery (enhanced closest-point with normal, UV, IDs)
 *   E5 - EnclosureResult / EnclosureQuery (inside/outside classification)
 */
#pragma once

#ifndef __CUDACC__
#include <cuda_runtime.h>
#endif

/* ---- Simple Closest-Point Result (legacy, used by basic CP queries) ---- */
struct NNResult {
    float        distance;     /* Distance to closest surface point; < 0 means no match */
    unsigned int prim_idx;     /* Index of the closest triangle in the query mesh */
};

/* ---- Enhanced Closest-Point Query (E2) ---- */
struct CPQuery {
    float3 position;           /* Query point                              */
    float  radius;             /* Maximum search radius                    */
};

struct CPResult {
    float        distance;     /* Distance to nearest surface; < 0 = miss  */
    float        normal[3];    /* Un-normalized geometric normal of face   */
    float        uv[2];        /* Barycentric coords of closest point      */
    float        closest_pos[3]; /* GPU-computed closest point position    */
    unsigned int prim_idx;     /* GAS-local primitive index                 */
    unsigned int geom_id;      /* Geometry ID                              */
    unsigned int inst_id;      /* Instance ID (0xFFFFFFFF if none)          */
};

/* ---- Enclosure Query (E5) ---- */
struct EnclosureQuery {
    float3 position;           /* Query point (world space)                */
};

struct EnclosureResult {
    int          prim_idx;     /* Nearest primitive; -1 = miss             */
    float        distance;     /* Distance to nearest surface              */
    int          side;         /* 0=front, 1=back, -1=degenerate           */
    unsigned int geom_id;      /* Geometry ID                              */
    unsigned int inst_id;      /* Instance ID (0xFFFFFFFF if none)          */
};
