/*
 * nn_kernels.cu - CUDA utility kernels for closest-point queries
 * Compiled as regular CUDA object code (not OptiX PTX).
 */

#include <cuda_runtime.h>
#include "nn_types.h"
#include "nn_kernels.h"

/* ---- Simple hash-based RNG (same as kernels.cu) ---- */
__device__ __forceinline__ unsigned int nn_pcg_hash(unsigned int input)
{
    unsigned int state = input * 747796405u + 2891336453u;
    unsigned int word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ __forceinline__ float nn_rng_float(unsigned int& seed)
{
    seed = nn_pcg_hash(seed);
    return (float)(seed) / (float)(0xFFFFFFFFu);
}

/* ===========================================================================
 * Triangle AABB Generation Kernel
 * Each triangle → AABB of its 3 vertices, expanded by radius.
 * Output layout matches OptixAabb: 6 floats (minX,minY,minZ,maxX,maxY,maxZ)
 * =========================================================================*/
__global__ void generateTriAABBsKernel(
    float*        d_aabbs,
    const float3* d_vertices,
    const uint3*  d_indices,
    unsigned int  num_tris,
    float         radius)
{
    unsigned int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= num_tris) return;

    uint3  tri = d_indices[idx];
    float3 v0  = d_vertices[tri.x];
    float3 v1  = d_vertices[tri.y];
    float3 v2  = d_vertices[tri.z];

    float* aabb = d_aabbs + idx * 6;

    /* Tight bounding box of triangle vertices, expanded by radius */
    aabb[0] = fminf(fminf(v0.x, v1.x), v2.x) - radius;   /* minX */
    aabb[1] = fminf(fminf(v0.y, v1.y), v2.y) - radius;   /* minY */
    aabb[2] = fminf(fminf(v0.z, v1.z), v2.z) - radius;   /* minZ */
    aabb[3] = fmaxf(fmaxf(v0.x, v1.x), v2.x) + radius;   /* maxX */
    aabb[4] = fmaxf(fmaxf(v0.y, v1.y), v2.y) + radius;   /* maxY */
    aabb[5] = fmaxf(fmaxf(v0.z, v1.z), v2.z) + radius;   /* maxZ */
}

void generateTriAABBsDevice(
    void*         d_aabbs,
    const float3* d_vertices,
    const uint3*  d_indices,
    unsigned int  num_tris,
    float         radius,
    cudaStream_t  stream)
{
    const int blockSize = 256;
    const int gridSize  = (num_tris + blockSize - 1) / blockSize;
    generateTriAABBsKernel<<<gridSize, blockSize, 0, stream>>>(
        reinterpret_cast<float*>(d_aabbs), d_vertices, d_indices,
        num_tris, radius);
}

/* ===========================================================================
 * Sphere AABB Generation Kernel (E4)
 * Each sphere → AABB = (center - radius - search_radius, center + radius + search_radius)
 * =========================================================================*/
__global__ void generateSphereAABBsKernel(
    float*        d_aabbs,
    const float3* d_centers,
    const float*  d_radii,
    unsigned int  num_spheres,
    float         search_radius)
{
    unsigned int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= num_spheres) return;

    float3 c = d_centers[idx];
    float  r = d_radii[idx] + search_radius;

    float* aabb = d_aabbs + idx * 6;
    aabb[0] = c.x - r;   /* minX */
    aabb[1] = c.y - r;   /* minY */
    aabb[2] = c.z - r;   /* minZ */
    aabb[3] = c.x + r;   /* maxX */
    aabb[4] = c.y + r;   /* maxY */
    aabb[5] = c.z + r;   /* maxZ */
}

void generateSphereAABBsDevice(
    void*          d_aabbs,
    const float3*  d_centers,
    const float*   d_radii,
    unsigned int   num_spheres,
    float          search_radius,
    cudaStream_t   stream)
{
    const int blockSize = 256;
    const int gridSize  = (num_spheres + blockSize - 1) / blockSize;
    generateSphereAABBsKernel<<<gridSize, blockSize, 0, stream>>>(
        reinterpret_cast<float*>(d_aabbs), d_centers, d_radii,
        num_spheres, search_radius);
}

/* ===========================================================================
 * Random Query Generation Kernel
 * Uniformly distributed within bounding box.
 * =========================================================================*/
__global__ void generateRandomQueriesKernel(
    float3*      d_queries,
    unsigned int count,
    float3       bbox_min,
    float3       bbox_max,
    unsigned int seed)
{
    unsigned int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= count) return;

    unsigned int rng = seed ^ (idx * 1973u + 1);

    float3 extent = make_float3(
        bbox_max.x - bbox_min.x,
        bbox_max.y - bbox_min.y,
        bbox_max.z - bbox_min.z
    );

    d_queries[idx] = make_float3(
        bbox_min.x + nn_rng_float(rng) * extent.x,
        bbox_min.y + nn_rng_float(rng) * extent.y,
        bbox_min.z + nn_rng_float(rng) * extent.z
    );
}

void generateRandomQueriesDevice(
    float3*      d_queries,
    unsigned int count,
    float3       bbox_min,
    float3       bbox_max,
    unsigned int seed,
    cudaStream_t stream)
{
    const int blockSize = 256;
    const int gridSize  = (count + blockSize - 1) / blockSize;
    generateRandomQueriesKernel<<<gridSize, blockSize, 0, stream>>>(
        d_queries, count, bbox_min, bbox_max, seed);
}

/* ===========================================================================
 * NN Hit Counting Kernel
 * Count results with distance >= 0 (valid nearest neighbor found).
 * =========================================================================*/
__global__ void countNNHitsKernel(
    const NNResult* d_results,
    unsigned int    count,
    unsigned int*   d_count)
{
    __shared__ unsigned int block_count;
    if (threadIdx.x == 0) block_count = 0;
    __syncthreads();

    unsigned int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx < count && d_results[idx].distance >= 0.0f) {
        atomicAdd(&block_count, 1);
    }
    __syncthreads();

    if (threadIdx.x == 0) {
        atomicAdd(d_count, block_count);
    }
}

void countNNHitsDevice(
    const NNResult* d_results,
    unsigned int    count,
    unsigned int*   d_count,
    cudaStream_t    stream)
{
    cudaMemsetAsync(d_count, 0, sizeof(unsigned int), stream);
    const int blockSize = 256;
    const int gridSize  = (count + blockSize - 1) / blockSize;
    countNNHitsKernel<<<gridSize, blockSize, 0, stream>>>(d_results, count, d_count);
}
