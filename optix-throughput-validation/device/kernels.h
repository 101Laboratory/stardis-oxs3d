/*
 * kernels.h - CUDA kernel declarations for ray generation and utilities
 * These are regular CUDA kernels (not OptiX device programs).
 */
#pragma once

#include <cuda_runtime.h>
#include "ray_types.h"

/*
 * Generate random rays within a bounding box.
 * Origins are randomly distributed inside the bbox,
 * directions are random unit vectors on the hemisphere.
 *
 * @param d_rays    Output ray buffer (device memory, must be pre-allocated)
 * @param count     Number of rays to generate
 * @param bbox_min  Scene bounding box minimum
 * @param bbox_max  Scene bounding box maximum
 * @param seed      Random seed for reproducibility
 * @param stream    CUDA stream
 */
void generateRandomRaysDevice(
    Ray*         d_rays,
    unsigned int count,
    float3       bbox_min,
    float3       bbox_max,
    unsigned int seed,
    cudaStream_t stream = 0
);

/*
 * Generate parallel rays (orthographic) for throughput testing.
 * All rays point in +Z direction, origins form a grid in XY plane.
 *
 * @param d_rays    Output ray buffer (device memory)
 * @param width     Grid width
 * @param height    Grid height
 * @param bbox_min  Scene bounding box minimum
 * @param bbox_max  Scene bounding box maximum
 * @param stream    CUDA stream
 */
void generateOrthoRaysDevice(
    Ray*         d_rays,
    int          width,
    int          height,
    float3       bbox_min,
    float3       bbox_max,
    cudaStream_t stream = 0
);

/*
 * Count the number of hits (t >= 0) in a hit result buffer.
 *
 * @param d_hits    Hit result buffer (device memory)
 * @param count     Number of results
 * @param d_count   Output: number of hits (device memory, single uint)
 * @param stream    CUDA stream
 */
void countHitsDevice(
    const HitResult* d_hits,
    unsigned int     count,
    unsigned int*    d_count,
    cudaStream_t     stream = 0
);
