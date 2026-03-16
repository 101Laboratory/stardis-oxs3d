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

/*
 * GPU postprocess: in-place UV/normal fixup on HitResult.
 * Overwrites bary_u/bary_v/normal fields with final s3d values:
 *   - Sphere: spherical UV from normal (atan2/acos)
 *   - Mesh: UV swap (bary → w,u) + clamp, normal negation (CCW→CW)
 * D2H transfers the same 40B HitResult — no extra buffer needed.
 *
 * @param d_hits       HitResult buffer from OptiX (device, modified in-place)
 * @param d_pp_table   Per-geometry postprocess entries (device)
 * @param pp_table_sz  Number of entries in pp_table
 * @param count        Number of rays
 * @param stream       CUDA stream
 */
void postprocessHitsInPlaceDevice(
    HitResult*        d_hits,
    const GpuPpEntry* d_pp_table,
    unsigned int      pp_table_sz,
    unsigned int      count,
    cudaStream_t      stream = 0
);

/*
 * Plan-D GPU postprocess: HitResult + GpuPpEntry → GpuS3dHit (56B).
 * Reads OptiX HitResult, performs UV/normal transform, fills all
 * s3d_hit-compatible fields.  Output is D2H'd directly to pinned
 * memory and consumed by merged_pass with zero CPU postprocess.
 *
 * @param d_hits       HitResult buffer from OptiX (device, read-only)
 * @param d_pp_table   Per-geometry postprocess entries (device)
 * @param d_out        GpuS3dHit output buffer (device, 56B/ray)
 * @param pp_table_sz  Number of entries in pp_table
 * @param count        Number of rays
 * @param stream       CUDA stream
 */
void postprocessToS3dHitDevice(
    const HitResult*  d_hits,
    const GpuPpEntry* d_pp_table,
    GpuS3dHit*        d_out,
    unsigned int      pp_table_sz,
    unsigned int      count,
    cudaStream_t      stream = 0
);
