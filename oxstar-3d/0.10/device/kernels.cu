/*
 * kernels.cu - CUDA utility kernels for ray generation and hit counting
 * Compiled as regular CUDA object code (not OptiX PTX).
 */

#include <cuda_runtime.h>
#include "ray_types.h"
#include "kernels.h"

/* ---- Simple hash-based RNG (reproducible, fast) ---- */
__device__ __forceinline__ unsigned int pcg_hash(unsigned int input)
{
    unsigned int state = input * 747796405u + 2891336453u;
    unsigned int word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ __forceinline__ float rng_float(unsigned int& seed)
{
    seed = pcg_hash(seed);
    return (float)(seed) / (float)(0xFFFFFFFFu);
}

/* ---- Random Ray Generation Kernel ---- */
__global__ void generateRandomRaysKernel(
    Ray*         rays,
    unsigned int count,
    float3       bbox_min,
    float3       bbox_max,
    unsigned int seed)
{
    unsigned int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= count)
        return;

    unsigned int rng_state = seed ^ (idx * 1973u + 1);

    float3 extent = make_float3(
        bbox_max.x - bbox_min.x,
        bbox_max.y - bbox_min.y,
        bbox_max.z - bbox_min.z
    );

    /* Random origin slightly outside the bbox */
    float3 center = make_float3(
        (bbox_min.x + bbox_max.x) * 0.5f,
        (bbox_min.y + bbox_max.y) * 0.5f,
        (bbox_min.z + bbox_max.z) * 0.5f
    );

    float max_extent = fmaxf(fmaxf(extent.x, extent.y), extent.z);
    float radius     = max_extent * 1.5f;

    /* Random direction (uniform sphere) using Marsaglia method */
    float u1, u2, s;
    do {
        u1 = 2.0f * rng_float(rng_state) - 1.0f;
        u2 = 2.0f * rng_float(rng_state) - 1.0f;
        s  = u1 * u1 + u2 * u2;
    } while (s >= 1.0f || s == 0.0f);

    float factor = sqrtf((1.0f - s));
    float3 dir   = make_float3(
        2.0f * u1 * factor,
        2.0f * u2 * factor,
        1.0f - 2.0f * s
    );

    /* Origin: point on sphere, pointing inward toward center */
    float3 origin = make_float3(
        center.x - dir.x * radius,
        center.y - dir.y * radius,
        center.z - dir.z * radius
    );

    rays[idx].origin    = origin;
    rays[idx].tmin      = 0.0f;
    rays[idx].direction = dir;
    rays[idx].tmax      = 1e16f;
}

void generateRandomRaysDevice(
    Ray*         d_rays,
    unsigned int count,
    float3       bbox_min,
    float3       bbox_max,
    unsigned int seed,
    cudaStream_t stream)
{
    const int blockSize = 256;
    const int gridSize  = (count + blockSize - 1) / blockSize;
    generateRandomRaysKernel<<<gridSize, blockSize, 0, stream>>>(
        d_rays, count, bbox_min, bbox_max, seed);
}

/* ---- Orthographic Ray Generation Kernel ---- */
__global__ void generateOrthoRaysKernel(
    Ray*   rays,
    int    width,
    int    height,
    float  x0, float y0, float z,
    float  dx, float dy)
{
    int rayx = threadIdx.x + blockIdx.x * blockDim.x;
    int rayy = threadIdx.y + blockIdx.y * blockDim.y;
    if (rayx >= width || rayy >= height)
        return;

    int idx            = rayx + rayy * width;
    rays[idx].origin   = make_float3(x0 + rayx * dx, y0 + rayy * dy, z);
    rays[idx].tmin     = 0.0f;
    rays[idx].direction = make_float3(0.0f, 0.0f, 1.0f);
    rays[idx].tmax     = 1e16f;
}

void generateOrthoRaysDevice(
    Ray*         d_rays,
    int          width,
    int          height,
    float3       bbox_min,
    float3       bbox_max,
    cudaStream_t stream)
{
    float3 span = make_float3(
        bbox_max.x - bbox_min.x,
        bbox_max.y - bbox_min.y,
        bbox_max.z - bbox_min.z
    );

    float padding = 0.05f;
    float dx      = span.x * (1.0f + 2.0f * padding) / width;
    float dy      = span.y * (1.0f + 2.0f * padding) / height;
    float x0      = bbox_min.x - span.x * padding + dx * 0.5f;
    float y0      = bbox_min.y - span.y * padding + dy * 0.5f;
    float z       = bbox_min.z - fmaxf(span.z, 1.0f) * 0.001f;

    dim3 blockSize(32, 16);
    dim3 gridSize(
        (width  + blockSize.x - 1) / blockSize.x,
        (height + blockSize.y - 1) / blockSize.y
    );
    generateOrthoRaysKernel<<<gridSize, blockSize, 0, stream>>>(
        d_rays, width, height, x0, y0, z, dx, dy);
}

/* ---- Hit Counting Kernel ---- */
__global__ void countHitsKernel(
    const HitResult* hits,
    unsigned int     count,
    unsigned int*    hit_count)
{
    __shared__ unsigned int block_count;
    if (threadIdx.x == 0) block_count = 0;
    __syncthreads();

    unsigned int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx < count && hits[idx].t >= 0.0f) {
        atomicAdd(&block_count, 1);
    }
    __syncthreads();

    if (threadIdx.x == 0) {
        atomicAdd(hit_count, block_count);
    }
}

void countHitsDevice(
    const HitResult* d_hits,
    unsigned int     count,
    unsigned int*    d_count,
    cudaStream_t     stream)
{
    /* Reset counter */
    cudaMemsetAsync(d_count, 0, sizeof(unsigned int), stream);

    const int blockSize = 256;
    const int gridSize  = (count + blockSize - 1) / blockSize;
    countHitsKernel<<<gridSize, blockSize, 0, stream>>>(d_hits, count, d_count);
}

/* ---- GPU Postprocess: in-place UV/normal fixup on HitResult ---- */

#define GPU_PP_PI 3.14159265358979323846f

__global__ void postprocessHitsInPlaceKernel(
    HitResult*        __restrict__ hits,
    const GpuPpEntry* __restrict__ pp_table,
    unsigned int pp_table_sz,
    unsigned int count)
{
    unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    HitResult& hr = hits[idx];
    if (hr.t < 0.0f) return;  /* miss — leave as-is */

    /* Lookup pp_table entry (L2-cached, small table) */
    unsigned char shape_type = 0;
    unsigned char flip_surface = 0;
    if (hr.geom_id < pp_table_sz) {
        GpuPpEntry e = pp_table[hr.geom_id];
        shape_type   = e.shape_type;
        flip_surface = e.flip_surface;
    }

    if (shape_type == 1) {
        /* SPHERE: compute spherical UV from normal, store in bary_u/bary_v.
         * Normal is preserved (already correct from device program). */
        float nx = hr.normal[0], ny = hr.normal[1], nz = hr.normal[2];
        if (flip_surface) { nx = -nx; ny = -ny; nz = -nz; }
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }
        nz = fminf(fmaxf(nz, -1.0f), 1.0f);
        float theta = acosf(nz);
        float phi   = atan2f(ny, nx);
        if (phi < 0.0f) phi += 2.0f * GPU_PP_PI;
        hr.bary_u = theta / GPU_PP_PI;
        hr.bary_v = phi   / (2.0f * GPU_PP_PI);
    } else {
        /* MESH: UV swap (bary → w,u) + clamp; normal negation (CCW→CW) */
        float u = hr.bary_u;
        float w = 1.0f - hr.bary_u - hr.bary_v;
        hr.bary_u = fminf(fmaxf(w, 0.0f), 1.0f);
        hr.bary_v = fminf(fmaxf(u, 0.0f), 1.0f);
        hr.normal[0] = -hr.normal[0];
        hr.normal[1] = -hr.normal[1];
        hr.normal[2] = -hr.normal[2];
    }
}

void postprocessHitsInPlaceDevice(
    HitResult*        d_hits,
    const GpuPpEntry* d_pp_table,
    unsigned int      pp_table_sz,
    unsigned int      count,
    cudaStream_t      stream)
{
    if (count == 0) return;
    const int blockSize = 256;
    const int gridSize  = (count + blockSize - 1) / blockSize;
    postprocessHitsInPlaceKernel<<<gridSize, blockSize, 0, stream>>>(
        d_hits, d_pp_table, pp_table_sz, count);
}

/* ---- Plan-D: HitResult + GpuPpEntry → GpuS3dHit (56B) kernel ---- */
__global__ void postprocessToS3dHitKernel(
    const HitResult*  __restrict__ hits,
    const GpuPpEntry* __restrict__ pp_table,
    GpuS3dHit*        __restrict__ out,
    unsigned int pp_table_sz,
    unsigned int count)
{
    unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    const HitResult& hr = hits[idx];
    GpuS3dHit& o = out[idx];

    if (hr.t < 0.0f) {
        /* Miss → S3D_HIT_NULL sentinel (distance = FLT_MAX, IDs = 0xFFFFFFFF) */
        o.prim_id       = 0xFFFFFFFFu;
        o.geom_id       = 0xFFFFFFFFu;
        o.inst_id       = 0xFFFFFFFFu;
        o.scene_prim_id = 0xFFFFFFFFu;
        o.shape_ptr     = 0;
        o.inst_ptr      = 0;
        o.normal[0] = 0.0f; o.normal[1] = 0.0f; o.normal[2] = 0.0f;
        o.uv[0]     = 0.0f; o.uv[1]     = 0.0f;
        o.distance  = 3.402823466e+38f;  /* FLT_MAX */
        return;
    }

    /* pp_table lookup (L2-cached, small table) */
    unsigned int shape_id   = 0;
    unsigned int pp_inst_id = 0xFFFFFFFFu;
    unsigned char shape_type   = 0;
    unsigned char flip_surface = 0;
    if (hr.geom_id < pp_table_sz) {
        GpuPpEntry e = pp_table[hr.geom_id];
        shape_id     = e.shape_id;
        pp_inst_id   = e.inst_id;
        shape_type   = e.shape_type;
        flip_surface = e.flip_surface;
    }

    /* Primitive fields */
    o.prim_id       = hr.prim_idx;
    o.geom_id       = shape_id;
    o.inst_id       = pp_inst_id;
    o.scene_prim_id = hr.prim_idx;
    o.shape_ptr     = 0;
    o.inst_ptr      = 0;
    o.distance      = hr.t;

    /* UV / normal transform (same logic as in-place kernel) */
    if (shape_type == 1) {
        float nx = hr.normal[0], ny = hr.normal[1], nz = hr.normal[2];
        if (flip_surface) { nx = -nx; ny = -ny; nz = -nz; }
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }
        nz = fminf(fmaxf(nz, -1.0f), 1.0f);
        float theta = acosf(nz);
        float phi   = atan2f(ny, nx);
        if (phi < 0.0f) phi += 2.0f * GPU_PP_PI;
        o.uv[0]     = theta / GPU_PP_PI;
        o.uv[1]     = phi   / (2.0f * GPU_PP_PI);
        o.normal[0] = hr.normal[0];
        o.normal[1] = hr.normal[1];
        o.normal[2] = hr.normal[2];
    } else {
        float u = hr.bary_u;
        float w = 1.0f - hr.bary_u - hr.bary_v;
        o.uv[0]     = fminf(fmaxf(w, 0.0f), 1.0f);
        o.uv[1]     = fminf(fmaxf(u, 0.0f), 1.0f);
        o.normal[0] = -hr.normal[0];
        o.normal[1] = -hr.normal[1];
        o.normal[2] = -hr.normal[2];
    }
}

void postprocessToS3dHitDevice(
    const HitResult*  d_hits,
    const GpuPpEntry* d_pp_table,
    GpuS3dHit*        d_out,
    unsigned int      pp_table_sz,
    unsigned int      count,
    cudaStream_t      stream)
{
    if (count == 0) return;
    const int blockSize = 256;
    const int gridSize  = (count + blockSize - 1) / blockSize;
    postprocessToS3dHitKernel<<<gridSize, blockSize, 0, stream>>>(
        d_hits, d_pp_table, d_out, pp_table_sz, count);
}
