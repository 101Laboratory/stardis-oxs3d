/*
 * nn_kernels.h - CUDA kernel declarations for closest-point query utilities
 * These are regular CUDA kernels (not OptiX device programs).
 *
 * Extensions:
 *   E4 - generateSphereAABBsDevice for sphere AABB generation
 */
#pragma once

#include <cuda_runtime.h>
#include "nn_types.h"

/*
 * Generate AABB buffer for a triangle mesh (for closest-point queries).
 * Each triangle becomes an axis-aligned bounding box of its 3 vertices,
 * expanded by search_radius in all directions.
 *
 * @param d_aabbs      Output AABB buffer (6 floats per AABB: minX,minY,minZ,maxX,maxY,maxZ)
 * @param d_vertices   Triangle vertex positions
 * @param d_indices    Triangle index triples
 * @param num_tris     Number of triangles
 * @param radius       Search radius (AABB expansion)
 * @param stream       CUDA stream
 */
void generateTriAABBsDevice(
    void*         d_aabbs,
    const float3* d_vertices,
    const uint3*  d_indices,
    unsigned int  num_tris,
    float         radius,
    cudaStream_t  stream = 0
);

/*
 * Generate AABB buffer for spheres (E4, for closest-point queries).
 * Each sphere becomes an AABB of (center - radius - search_radius)
 * to (center + radius + search_radius).
 *
 * @param d_aabbs      Output AABB buffer (6 floats per AABB)
 * @param d_centers    Sphere centers
 * @param d_radii      Sphere radii
 * @param num_spheres  Number of spheres
 * @param search_radius Search radius (AABB expansion beyond sphere extent)
 * @param stream       CUDA stream
 */
void generateSphereAABBsDevice(
    void*          d_aabbs,
    const float3*  d_centers,
    const float*   d_radii,
    unsigned int   num_spheres,
    float          search_radius,
    cudaStream_t   stream = 0
);

/*
 * Generate random query points uniformly distributed within a bounding box.
 *
 * @param d_queries  Output query buffer (device memory, must be pre-allocated)
 * @param count      Number of queries to generate
 * @param bbox_min   Bounding box minimum
 * @param bbox_max   Bounding box maximum
 * @param seed       Random seed for reproducibility
 * @param stream     CUDA stream
 */
void generateRandomQueriesDevice(
    float3*      d_queries,
    unsigned int count,
    float3       bbox_min,
    float3       bbox_max,
    unsigned int seed,
    cudaStream_t stream = 0
);

/*
 * Count the number of valid closest-point results (distance >= 0).
 *
 * @param d_results  Result buffer (device memory)
 * @param count      Number of results
 * @param d_count    Output: number of valid results (device memory, single uint)
 * @param stream     CUDA stream
 */
void countNNHitsDevice(
    const NNResult* d_results,
    unsigned int    count,
    unsigned int*   d_count,
    cudaStream_t    stream = 0
);
