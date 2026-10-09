/*
 * unified_params.h - Unified OptiX launch parameters
 *
 * Shared by ray tracing (programs.cu) and closest-point (nn_programs.cu)
 * device programs within a single unified pipeline.
 * Each program reads only the fields it needs; unused fields are ignored.
 *
 * Extensions:
 *   E1 - HitGroupData carries geom_id + geom_type for normal computation
 *   E2 - CPResult support in params
 *   E3 - MultiHitResult buffer in params
 *   E4 - Sphere data pointers in HitGroupData and params
 *   E5 - EnclosureResult support in params
 *   E6 - Per-geometry SBT records via HitGroupData
 */
#pragma once

#include "ray_types.h"
#include "nn_types.h"

#ifndef __CUDACC__
#include <cuda_runtime.h>
#endif

#ifdef __CUDACC__
#include <optix.h>
#else
#include <optix_types.h>
#endif

/* ---- Geometry Type Constants (E4/E6) ---- */
#define GEOM_TYPE_TRIANGLE 0u
#define GEOM_TYPE_SPHERE   1u

/* ---- Unified Launch Parameters ---- */
struct UnifiedParams {
    OptixTraversableHandle handle;       /* Current traversable (GAS or IAS) */
    unsigned int           count;        /* num_rays or num_queries          */

    /* Ray tracing fields (__raygen__rg, __closesthit__ch) */
    Ray*       rays;
    HitResult* hits;

    /* Multi-hit fields (E3: __raygen__mh, __anyhit__mh) */
    MultiHitResult* multi_hits;

    /* Closest-point query fields (__raygen__nn, __intersection__nn) */
    float3*    queries;
    float3*    nn_vertices;    /* Triangle vertices for query mesh           */
    uint3*     nn_indices;     /* Triangle indices  for query mesh           */
    NNResult*  results;

    /* Enhanced CP fields (E2: __raygen__cp) */
    CPQuery*   cp_queries;
    CPResult*  cp_results;

    /* Sphere data for CP path (E4) */
    float3*    sphere_centers;  /* Sphere centers for CP sphere queries      */
    float*     sphere_radii;    /* Sphere radii   for CP sphere queries      */

    /* Enclosure fields (E5) */
    EnclosureQuery*  enclosure_queries;
    EnclosureResult* enclosure_results;

    /* CP prim-to-geom mapping table (E7: host-side resolution) */
    unsigned int     nn_num_prims;         /* total prims in query mesh */
};

/* ---- SBT Hit Group Data (E1/E4/E6/E7) ---- */
struct HitGroupData {
    unsigned int geom_id;       /* per-geometry identifier                  */
    unsigned int geom_type;     /* GEOM_TYPE_TRIANGLE or GEOM_TYPE_SPHERE   */
    unsigned int flip_normal;   /* E7: non-zero = negate normal direction   */
    unsigned int pad0;          /* Padding for alignment                    */

    /* Triangle data (valid when geom_type == GEOM_TYPE_TRIANGLE) */
    float3*      vertices;
    uint3*       indices;

    /* Sphere data (valid when geom_type == GEOM_TYPE_SPHERE) */
    float3*      sphere_centers;
    float*       sphere_radii;
};

/* ---- Ray Type Enum (SBT addressing) ---- */
enum RayType {
    RAY_TYPE_RADIANCE = 0,
    RAY_TYPE_COUNT
};
