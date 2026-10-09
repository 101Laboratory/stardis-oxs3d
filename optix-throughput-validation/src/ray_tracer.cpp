/*
 * ray_tracer.cpp - Ray tracing request handler implementation
 */

#include "ray_tracer.h"
#include "launch_params.h"
#include "optix_check.h"
#include "kernels.h"

#include <algorithm>
#include <numeric>
#include <iostream>
#include <cmath>

/* ===========================================================================
 * RayTracer destructor
 * =========================================================================*/
RayTracer::~RayTracer()
{
    /* CudaBuffer RAII handles cleanup */
}

/* ===========================================================================
 * RayTracer::init
 * =========================================================================*/
void RayTracer::init(
    OptixPipeline                  pipeline,
    const OptixShaderBindingTable& sbt,
    OptixTraversableHandle         handle)
{
    m_pipeline = pipeline;
    m_sbt      = sbt;
    m_handle   = handle;
}

/* ===========================================================================
 * RayTracer::traceSingle
 * Trace one ray synchronously. Low throughput - for debugging only.
 * =========================================================================*/
HitResult RayTracer::traceSingle(const Ray& ray)
{
    /* Lazy-allocate single-ray buffers */
    if (!m_single_bufs_allocated) {
        m_single_ray_buf.alloc(1);
        m_single_hit_buf.alloc(1);
        m_single_bufs_allocated = true;
    }

    /* Upload ray */
    m_single_ray_buf.upload(&ray, 1);

    /* Set up launch params */
    LaunchParams lp;
    lp.handle   = m_handle;
    lp.rays     = m_single_ray_buf.get();
    lp.hits     = m_single_hit_buf.get();
    lp.num_rays = 1;

    CudaBuffer<LaunchParams> d_params;
    d_params.alloc(1);
    d_params.upload(&lp, 1);

    /* Launch with 1x1x1 dimensions */
    OPTIX_CHECK(optixLaunch(
        m_pipeline,
        0,  /* default stream */
        d_params.devicePtr(),
        sizeof(LaunchParams),
        &m_sbt,
        1, 1, 1));

    CUDA_SYNC_CHECK();

    /* Download result */
    HitResult result;
    m_single_hit_buf.download(&result, 1);
    return result;
}

/* ===========================================================================
 * RayTracer::traceBatch (device pointers)
 * High-throughput batch trace.
 * =========================================================================*/
void RayTracer::traceBatch(
    Ray*         d_rays,
    HitResult*   d_hits,
    unsigned int count,
    CUstream     stream)
{
    LaunchParams lp;
    lp.handle   = m_handle;
    lp.rays     = d_rays;
    lp.hits     = d_hits;
    lp.num_rays = count;

    CUdeviceptr d_params;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_params), sizeof(LaunchParams)));
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(d_params),
                               &lp, sizeof(LaunchParams),
                               cudaMemcpyHostToDevice, stream));

    /* Compute 2D launch dimensions for better GPU occupancy */
    unsigned int launch_width, launch_height;
    if (count <= 65536) {
        launch_width  = count;
        launch_height = 1;
    } else {
        /* Use a 2D grid: width up to 8192, height = ceil(count / width) */
        launch_width  = 8192;
        launch_height = (count + launch_width - 1) / launch_width;
    }

    OPTIX_CHECK(optixLaunch(
        m_pipeline,
        stream,
        d_params,
        sizeof(LaunchParams),
        &m_sbt,
        launch_width,
        launch_height,
        1));

    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_params)));
}

/* ===========================================================================
 * RayTracer::traceBatchTimed
 * Trace with GPU timing via CUDA events. Returns throughput metrics.
 * =========================================================================*/
TraceResult RayTracer::traceBatchTimed(
    Ray*         d_rays,
    HitResult*   d_hits,
    unsigned int count,
    int          num_iters,
    int          warm_up,
    CUstream     stream)
{
    /* Warm-up iterations */
    for (int i = 0; i < warm_up; ++i) {
        traceBatch(d_rays, d_hits, count, stream);
    }
    CUDA_CHECK(cudaStreamSynchronize(stream));

    /* Timed iterations */
    std::vector<float> times(num_iters);

    cudaEvent_t start, stop;
    CUDA_CHECK(cudaEventCreate(&start));
    CUDA_CHECK(cudaEventCreate(&stop));

    for (int i = 0; i < num_iters; ++i) {
        CUDA_CHECK(cudaEventRecord(start, stream));

        traceBatch(d_rays, d_hits, count, stream);

        CUDA_CHECK(cudaEventRecord(stop, stream));
        CUDA_CHECK(cudaEventSynchronize(stop));

        float ms = 0;
        CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));
        times[i] = ms;
    }

    CUDA_CHECK(cudaEventDestroy(start));
    CUDA_CHECK(cudaEventDestroy(stop));

    /* Compute statistics (use median for robustness) */
    std::sort(times.begin(), times.end());
    float median_ms = times[num_iters / 2];

    /* Count hits */
    CudaBuffer<unsigned int> d_hit_count;
    d_hit_count.alloc(1);
    countHitsDevice(d_hits, count, d_hit_count.get(), stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    unsigned int hit_count = 0;
    d_hit_count.download(&hit_count, 1);

    /* Build result */
    TraceResult result;
    result.num_rays       = count;
    result.trace_time_ms  = median_ms;
    result.total_time_ms  = median_ms;  /* No upload/download overhead here */
    result.mrays_per_sec  = (double)count / (median_ms * 1000.0);  /* M = 1e6 */
    result.grays_per_sec  = result.mrays_per_sec / 1000.0;
    result.num_hits       = hit_count;
    result.hit_rate       = (float)hit_count / (float)count;

    return result;
}

/* ===========================================================================
 * RayTracer::traceBatch (host vectors)
 * Convenience wrapper that handles upload/download.
 * =========================================================================*/
std::vector<HitResult> RayTracer::traceBatch(const std::vector<Ray>& rays)
{
    unsigned int count = (unsigned int)rays.size();

    CudaBuffer<Ray>       d_rays;
    CudaBuffer<HitResult> d_hits;
    d_rays.alloc(count);
    d_hits.alloc(count);

    d_rays.upload(rays);

    traceBatch(d_rays.get(), d_hits.get(), count);
    CUDA_SYNC_CHECK();

    std::vector<HitResult> results;
    d_hits.download(results);
    return results;
}
