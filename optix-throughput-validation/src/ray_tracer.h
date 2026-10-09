/*
 * ray_tracer.h - Ray tracing request handler (single + batch)
 * Provides both synchronous single-ray and high-throughput batch tracing.
 */
#pragma once

#include <optix.h>
#include <cuda_runtime.h>
#include "ray_types.h"
#include "buffer_manager.h"

#include <vector>

/* ---- Benchmark result for a single run ---- */
struct TraceResult {
    unsigned int num_rays;
    float        trace_time_ms;     /* GPU trace time (ms) */
    float        total_time_ms;     /* Including upload/download */
    double       mrays_per_sec;     /* MRays/s   */
    double       grays_per_sec;     /* GRays/s   */
    unsigned int num_hits;          /* Number of intersections */
    float        hit_rate;          /* hit_count / num_rays   */
};

class RayTracer {
public:
    RayTracer()  = default;
    ~RayTracer();

    /*
     * Initialize the ray tracer with a configured pipeline.
     *
     * @param pipeline  OptiX pipeline handle
     * @param sbt       Shader binding table
     * @param handle    Top-level traversable handle (GAS or IAS)
     */
    void init(
        OptixPipeline                  pipeline,
        const OptixShaderBindingTable& sbt,
        OptixTraversableHandle         handle);

    /*
     * Trace a single ray (synchronous, host-side API).
     * Extremely low throughput - use for debugging/validation only.
     */
    HitResult traceSingle(const Ray& ray);

    /*
     * Trace a batch of rays already in device memory.
     * Results are written to the provided device buffer.
     *
     * @param d_rays    Device pointer to ray buffer
     * @param d_hits    Device pointer to hit result buffer (pre-allocated)
     * @param count     Number of rays
     * @param stream    CUDA stream (0 = default)
     */
    void traceBatch(
        Ray*         d_rays,
        HitResult*   d_hits,
        unsigned int count,
        CUstream     stream = 0);

    /*
     * Trace a batch and measure throughput using CUDA events.
     * Includes warm-up iterations.
     *
     * @param d_rays       Device pointer to ray buffer
     * @param d_hits       Device pointer to hit result buffer
     * @param count        Number of rays
     * @param num_iters    Number of timed iterations (median reported)
     * @param warm_up      Number of warm-up iterations
     * @param stream       CUDA stream
     */
    TraceResult traceBatchTimed(
        Ray*         d_rays,
        HitResult*   d_hits,
        unsigned int count,
        int          num_iters = 10,
        int          warm_up   = 3,
        CUstream     stream    = 0);

    /*
     * Trace batch from host arrays (handles upload/download automatically).
     */
    std::vector<HitResult> traceBatch(const std::vector<Ray>& rays);

    /*
     * Update the traversable handle (e.g., when scene changes).
     */
    void setTraversableHandle(OptixTraversableHandle handle) { m_handle = handle; }

private:
    OptixPipeline               m_pipeline = nullptr;
    OptixShaderBindingTable     m_sbt      = {};
    OptixTraversableHandle      m_handle   = 0;

    /* Reusable device buffers for single-ray tracing */
    CudaBuffer<Ray>       m_single_ray_buf;
    CudaBuffer<HitResult> m_single_hit_buf;
    bool                  m_single_bufs_allocated = false;
};
