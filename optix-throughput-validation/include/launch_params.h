/*
 * launch_params.h - OptiX launch parameters and SBT data structures
 * Shared between host code and OptiX device programs (programs.cu).
 * Requires OptiX headers to be included before this file.
 */
#pragma once

#include "ray_types.h"

#ifdef __CUDACC__
#include <optix.h>
#else
#include <optix_types.h>
#endif

/* ---- SBT Hit Group Data ---- */
struct HitGroupData {
    /* Intentionally minimal for throughput testing.
     * Extend with vertex/index buffers if normal computation is needed. */
    float3* vertices;
    uint3*  indices;
};

/* ---- Launch Parameters ---- */
struct LaunchParams {
    OptixTraversableHandle handle;
    Ray*                   rays;
    HitResult*             hits;
    unsigned int           num_rays;
};

/* ---- Ray Type Enum ---- */
enum RayType {
    RAY_TYPE_RADIANCE = 0,
    RAY_TYPE_COUNT
};
