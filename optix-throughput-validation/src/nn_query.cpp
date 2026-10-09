/*
 * nn_query.cpp - Nearest neighbor query implementation
 *
 * Creates a separate OptiX pipeline for custom primitives (AABB-based),
 * builds a GAS from the point cloud, and dispatches NN queries as
 * zero-length ray traces through the AABB BVH.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "nn_query.h"
#include "nn_launch_params.h"
#include "optix_check.h"
#include "nn_kernels.h"

#include <optix_stack_size.h>

#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>

/* ---- SBT Record Template ---- */
template <typename T>
struct NNSbtRecord {
    __align__(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T data;
};

struct NNEmptyData {};

typedef NNSbtRecord<NNEmptyData> NNRaygenRecord;
typedef NNSbtRecord<NNEmptyData> NNMissRecord;
typedef NNSbtRecord<NNEmptyData> NNHitGroupRecord;

/* ===========================================================================
 * NNQuery destructor
 * =========================================================================*/
NNQuery::~NNQuery()
{
    cleanup();
}

/* ===========================================================================
 * NNQuery::init
 * =========================================================================*/
void NNQuery::init(
    OptixDeviceContext  context,
    const std::string&  ptx_source,
    bool                use_instancing)
{
    m_context        = context;
    m_use_instancing = use_instancing;
    createPipeline(ptx_source);
}

/* ===========================================================================
 * Pipeline Creation
 * =========================================================================*/
void NNQuery::createPipeline(const std::string& ptx_source)
{
    createModule(ptx_source);
    createProgramGroups();
    createPipelineObject();
    createSBT();
}

void NNQuery::createModule(const std::string& ptx_source)
{
    /* Module compile options */
    m_module_compile_options = {};
    m_module_compile_options.optLevel   = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    m_module_compile_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

    /* Pipeline compile options — custom primitives, 2 payload regs */
    m_pipeline_compile_options = {};
    m_pipeline_compile_options.usesMotionBlur        = false;
    m_pipeline_compile_options.numPayloadValues      = 2;   /* dist_sq + point_idx */
    m_pipeline_compile_options.numAttributeValues    = 2;   /* min required        */
    m_pipeline_compile_options.exceptionFlags        = OPTIX_EXCEPTION_FLAG_NONE;
    m_pipeline_compile_options.pipelineLaunchParamsVariableName = "nn_params";
    m_pipeline_compile_options.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_CUSTOM;

    if (m_use_instancing) {
        m_pipeline_compile_options.traversableGraphFlags =
            OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    } else {
        m_pipeline_compile_options.traversableGraphFlags =
            OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    }

    /* Create module from PTX */
    OPTIX_CHECK_LOG(optixModuleCreate(
        m_context,
        &m_module_compile_options,
        &m_pipeline_compile_options,
        ptx_source.c_str(),
        ptx_source.size(),
        s_optix_log, &s_optix_log_size,
        &m_module));

    std::cout << "  [NN Pipeline] Module created.\n";
}

void NNQuery::createProgramGroups()
{
    OptixProgramGroupOptions pg_options = {};

    /* Raygen */
    OptixProgramGroupDesc raygen_desc    = {};
    raygen_desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygen_desc.raygen.module            = m_module;
    raygen_desc.raygen.entryFunctionName = "__raygen__nn";

    OPTIX_CHECK_LOG(optixProgramGroupCreate(
        m_context, &raygen_desc, 1, &pg_options,
        s_optix_log, &s_optix_log_size, &m_raygen_pg));

    /* Miss (no-op, but required in SBT) */
    OptixProgramGroupDesc miss_desc    = {};
    miss_desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_MISS;
    miss_desc.miss.module              = m_module;
    miss_desc.miss.entryFunctionName   = "__miss__nn";

    OPTIX_CHECK_LOG(optixProgramGroupCreate(
        m_context, &miss_desc, 1, &pg_options,
        s_optix_log, &s_optix_log_size, &m_miss_pg));

    /* HitGroup: custom intersection only (no closest-hit, no any-hit) */
    OptixProgramGroupDesc hitgroup_desc        = {};
    hitgroup_desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitgroup_desc.hitgroup.moduleIS             = m_module;
    hitgroup_desc.hitgroup.entryFunctionNameIS  = "__intersection__nn";
    hitgroup_desc.hitgroup.moduleCH             = nullptr;
    hitgroup_desc.hitgroup.entryFunctionNameCH  = nullptr;
    hitgroup_desc.hitgroup.moduleAH             = nullptr;
    hitgroup_desc.hitgroup.entryFunctionNameAH  = nullptr;

    OPTIX_CHECK_LOG(optixProgramGroupCreate(
        m_context, &hitgroup_desc, 1, &pg_options,
        s_optix_log, &s_optix_log_size, &m_hitgroup_pg));

    std::cout << "  [NN Pipeline] Program groups created (raygen, miss, intersection).\n";
}

void NNQuery::createPipelineObject()
{
    OptixProgramGroup pgs[] = { m_raygen_pg, m_miss_pg, m_hitgroup_pg };

    OptixPipelineLinkOptions link_options = {};
    link_options.maxTraceDepth            = 1;

    OPTIX_CHECK_LOG(optixPipelineCreate(
        m_context,
        &m_pipeline_compile_options,
        &link_options,
        pgs, 3,
        s_optix_log, &s_optix_log_size,
        &m_pipeline));

    /* Compute and set stack sizes */
    OptixStackSizes stack_sizes = {};
    for (auto& pg : pgs) {
        OPTIX_CHECK(optixUtilAccumulateStackSizes(pg, &stack_sizes, m_pipeline));
    }

    uint32_t dc_from_traversal, dc_from_state, continuation;
    OPTIX_CHECK(optixUtilComputeStackSizes(
        &stack_sizes,
        1,  /* maxTraceDepth */
        0,  /* maxCCDepth    */
        0,  /* maxDCDepth    */
        &dc_from_traversal,
        &dc_from_state,
        &continuation));

    uint32_t max_traversal_depth = m_use_instancing ? 2 : 1;
    OPTIX_CHECK(optixPipelineSetStackSize(
        m_pipeline,
        dc_from_traversal,
        dc_from_state,
        continuation,
        max_traversal_depth));

    std::cout << "  [NN Pipeline] Pipeline created and stack sizes configured.\n";
}

void NNQuery::createSBT()
{
    /* Raygen record */
    {
        NNRaygenRecord rec = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_raygen_pg, &rec));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_raygen_record),
                              sizeof(NNRaygenRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_raygen_record),
                              &rec, sizeof(NNRaygenRecord),
                              cudaMemcpyHostToDevice));
    }

    /* Miss record */
    {
        NNMissRecord rec = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_miss_pg, &rec));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_miss_record),
                              sizeof(NNMissRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_miss_record),
                              &rec, sizeof(NNMissRecord),
                              cudaMemcpyHostToDevice));
    }

    /* HitGroup record */
    {
        NNHitGroupRecord rec = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_hitgroup_pg, &rec));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_hitgroup_record),
                              sizeof(NNHitGroupRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_hitgroup_record),
                              &rec, sizeof(NNHitGroupRecord),
                              cudaMemcpyHostToDevice));
    }

    /* Assemble SBT */
    m_sbt = {};
    m_sbt.raygenRecord                = m_d_raygen_record;
    m_sbt.missRecordBase              = m_d_miss_record;
    m_sbt.missRecordStrideInBytes     = sizeof(NNMissRecord);
    m_sbt.missRecordCount             = 1;
    m_sbt.hitgroupRecordBase          = m_d_hitgroup_record;
    m_sbt.hitgroupRecordStrideInBytes = sizeof(NNHitGroupRecord);
    m_sbt.hitgroupRecordCount         = 1;

    std::cout << "  [NN Pipeline] SBT created.\n";
}

/* ===========================================================================
 * Point Cloud & GAS
 * =========================================================================*/
void NNQuery::setPointCloud(
    const std::vector<float3>& points,
    float                      search_radius)
{
    /* Free previous GAS if any */
    freeGAS();

    m_num_points    = static_cast<unsigned int>(points.size());
    m_search_radius = search_radius;

    /* Upload points to GPU */
    m_d_points.alloc(m_num_points);
    m_d_points.upload(points);

    /* Compute bounding box */
    m_bbox_min = make_float3(1e30f, 1e30f, 1e30f);
    m_bbox_max = make_float3(-1e30f, -1e30f, -1e30f);
    for (const auto& p : points) {
        m_bbox_min.x = std::min(m_bbox_min.x, p.x);
        m_bbox_min.y = std::min(m_bbox_min.y, p.y);
        m_bbox_min.z = std::min(m_bbox_min.z, p.z);
        m_bbox_max.x = std::max(m_bbox_max.x, p.x);
        m_bbox_max.y = std::max(m_bbox_max.y, p.y);
        m_bbox_max.z = std::max(m_bbox_max.z, p.z);
    }

    /* Build AABB-based GAS */
    buildGAS();

    std::cout << "  [NN] Point cloud set: " << m_num_points
              << " points, search_radius=" << search_radius << "\n";
}

void NNQuery::buildGAS()
{
    /* 1. Generate AABBs on GPU */
    const size_t aabb_size_bytes = m_num_points * 6 * sizeof(float);
    CUdeviceptr d_aabbs = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_aabbs), aabb_size_bytes));

    generateAABBsDevice(
        reinterpret_cast<void*>(d_aabbs),
        m_d_points.get(),
        m_num_points,
        m_search_radius);
    CUDA_CHECK(cudaDeviceSynchronize());

    /* 2. Configure build input for custom primitives */
    const uint32_t aabb_flags[1] = { OPTIX_GEOMETRY_FLAG_NONE };

    OptixBuildInput build_input = {};
    build_input.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
    build_input.customPrimitiveArray.aabbBuffers    = &d_aabbs;
    build_input.customPrimitiveArray.numPrimitives  = m_num_points;
    build_input.customPrimitiveArray.strideInBytes   = 0;  /* tight: sizeof(OptixAabb) = 24 */
    build_input.customPrimitiveArray.flags           = aabb_flags;
    build_input.customPrimitiveArray.numSbtRecords   = 1;

    /* 3. Build options */
    OptixAccelBuildOptions build_options = {};
    build_options.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION
                             | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    build_options.operation  = OPTIX_BUILD_OPERATION_BUILD;

    /* 4. Compute memory requirements */
    OptixAccelBufferSizes buffer_sizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(
        m_context, &build_options, &build_input, 1, &buffer_sizes));

    /* 5. Allocate temp and output buffers */
    CUdeviceptr d_temp = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_temp),
                          buffer_sizes.tempSizeInBytes));

    CUdeviceptr d_output = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_output),
                          buffer_sizes.outputSizeInBytes));

    /* 6. Build with compaction property */
    CUdeviceptr d_compacted_size = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted_size),
                          sizeof(size_t)));

    OptixAccelEmitDesc emit_desc = {};
    emit_desc.type   = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
    emit_desc.result = d_compacted_size;

    OPTIX_CHECK(optixAccelBuild(
        m_context,
        0,                    /* CUDA stream */
        &build_options,
        &build_input,
        1,                    /* num build inputs */
        d_temp,
        buffer_sizes.tempSizeInBytes,
        d_output,
        buffer_sizes.outputSizeInBytes,
        &m_gas_handle,
        &emit_desc, 1));

    CUDA_CHECK(cudaDeviceSynchronize());

    /* 7. Compact */
    size_t compacted_size = 0;
    CUDA_CHECK(cudaMemcpy(&compacted_size,
                          reinterpret_cast<void*>(d_compacted_size),
                          sizeof(size_t), cudaMemcpyDeviceToHost));

    if (compacted_size < buffer_sizes.outputSizeInBytes) {
        CUdeviceptr d_compacted = 0;
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted),
                              compacted_size));
        OPTIX_CHECK(optixAccelCompact(
            m_context, 0, m_gas_handle,
            d_compacted, compacted_size,
            &m_gas_handle));
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_output)));
        m_gas_buffer = d_compacted;

        std::cout << "  [NN] Point GAS built: "
                  << (compacted_size / 1024) << " KB (compacted from "
                  << (buffer_sizes.outputSizeInBytes / 1024) << " KB)\n";
    } else {
        m_gas_buffer = d_output;
        std::cout << "  [NN] Point GAS built: "
                  << (buffer_sizes.outputSizeInBytes / 1024) << " KB\n";
    }

    /* 8. Free temporary resources */
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_temp)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_compacted_size)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_aabbs)));
}

void NNQuery::freeGAS()
{
    if (m_gas_buffer) {
        cudaFree(reinterpret_cast<void*>(m_gas_buffer));
        m_gas_buffer = 0;
        m_gas_handle = 0;
    }
}

/* ===========================================================================
 * NNQuery::querySingle
 * Synchronous single-point query. Low throughput — for debugging only.
 * =========================================================================*/
NNResult NNQuery::querySingle(const float3& query_point)
{
    /* Lazy-allocate single query buffers */
    if (!m_single_bufs_allocated) {
        m_single_query_buf.alloc(1);
        m_single_result_buf.alloc(1);
        m_single_bufs_allocated = true;
    }

    m_single_query_buf.upload(&query_point, 1);

    /* Set up launch params */
    NNLaunchParams lp = {};
    lp.handle      = m_gas_handle;
    lp.queries     = m_single_query_buf.get();
    lp.points      = m_d_points.get();
    lp.results     = m_single_result_buf.get();
    lp.num_queries = 1;

    CudaBuffer<NNLaunchParams> d_params;
    d_params.alloc(1);
    d_params.upload(&lp, 1);

    /* Launch 1x1x1 */
    OPTIX_CHECK(optixLaunch(
        m_pipeline,
        0,  /* default stream */
        d_params.devicePtr(),
        sizeof(NNLaunchParams),
        &m_sbt,
        1, 1, 1));

    CUDA_SYNC_CHECK();

    /* Download result */
    NNResult result;
    m_single_result_buf.download(&result, 1);
    return result;
}

/* ===========================================================================
 * NNQuery::queryBatch (device pointers)
 * High-throughput batch query.
 * =========================================================================*/
void NNQuery::queryBatch(
    float3*      d_queries,
    NNResult*    d_results,
    unsigned int count,
    CUstream     stream)
{
    NNLaunchParams lp = {};
    lp.handle      = m_gas_handle;
    lp.queries     = d_queries;
    lp.points      = m_d_points.get();
    lp.results     = d_results;
    lp.num_queries = count;

    CUdeviceptr d_params;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_params),
                          sizeof(NNLaunchParams)));
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(d_params),
                               &lp, sizeof(NNLaunchParams),
                               cudaMemcpyHostToDevice, stream));

    /* Compute 2D launch dimensions */
    unsigned int launch_width, launch_height;
    if (count <= 65536) {
        launch_width  = count;
        launch_height = 1;
    } else {
        launch_width  = 8192;
        launch_height = (count + launch_width - 1) / launch_width;
    }

    OPTIX_CHECK(optixLaunch(
        m_pipeline,
        stream,
        d_params,
        sizeof(NNLaunchParams),
        &m_sbt,
        launch_width,
        launch_height,
        1));

    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_params)));
}

/* ===========================================================================
 * NNQuery::queryBatchTimed
 * Batch query with GPU timing via CUDA events. Returns throughput metrics.
 * =========================================================================*/
NNQueryResult NNQuery::queryBatchTimed(
    float3*      d_queries,
    NNResult*    d_results,
    unsigned int count,
    int          num_iters,
    int          warm_up,
    CUstream     stream)
{
    /* Warm-up iterations */
    for (int i = 0; i < warm_up; ++i) {
        queryBatch(d_queries, d_results, count, stream);
    }
    CUDA_CHECK(cudaStreamSynchronize(stream));

    /* Timed iterations */
    std::vector<float> times(num_iters);
    cudaEvent_t start, stop;
    CUDA_CHECK(cudaEventCreate(&start));
    CUDA_CHECK(cudaEventCreate(&stop));

    for (int i = 0; i < num_iters; ++i) {
        CUDA_CHECK(cudaEventRecord(start, stream));
        queryBatch(d_queries, d_results, count, stream);
        CUDA_CHECK(cudaEventRecord(stop, stream));
        CUDA_CHECK(cudaEventSynchronize(stop));

        float ms = 0;
        CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));
        times[i] = ms;
    }

    CUDA_CHECK(cudaEventDestroy(start));
    CUDA_CHECK(cudaEventDestroy(stop));

    /* Median for robustness */
    std::sort(times.begin(), times.end());
    float median_ms = times[num_iters / 2];

    /* Count successful queries */
    CudaBuffer<unsigned int> d_hit_count;
    d_hit_count.alloc(1);
    countNNHitsDevice(d_results, count, d_hit_count.get(), stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    unsigned int hit_count = 0;
    d_hit_count.download(&hit_count, 1);

    /* Build result */
    NNQueryResult result = {};
    result.num_queries       = count;
    result.query_time_ms     = median_ms;
    result.total_time_ms     = median_ms;
    result.mqueries_per_sec  = (double)count / (median_ms * 1000.0);
    result.gqueries_per_sec  = result.mqueries_per_sec / 1000.0;
    result.num_found         = hit_count;
    result.found_rate        = (float)hit_count / (float)count;

    return result;
}

/* ===========================================================================
 * NNQuery::queryBatch (host vectors)
 * Convenience wrapper with upload/download.
 * =========================================================================*/
std::vector<NNResult> NNQuery::queryBatch(const std::vector<float3>& queries)
{
    unsigned int count = static_cast<unsigned int>(queries.size());

    CudaBuffer<float3>   d_queries;
    CudaBuffer<NNResult> d_results;
    d_queries.alloc(count);
    d_results.alloc(count);

    d_queries.upload(queries);

    queryBatch(d_queries.get(), d_results.get(), count);
    CUDA_SYNC_CHECK();

    std::vector<NNResult> results;
    d_results.download(results);
    return results;
}

/* ===========================================================================
 * NNQuery::cleanup
 * Release all resources in reverse creation order.
 * =========================================================================*/
void NNQuery::cleanup()
{
    /* SBT device memory */
    if (m_d_raygen_record)   { cudaFree(reinterpret_cast<void*>(m_d_raygen_record));   m_d_raygen_record   = 0; }
    if (m_d_miss_record)     { cudaFree(reinterpret_cast<void*>(m_d_miss_record));     m_d_miss_record     = 0; }
    if (m_d_hitgroup_record) { cudaFree(reinterpret_cast<void*>(m_d_hitgroup_record)); m_d_hitgroup_record = 0; }

    /* Pipeline objects */
    if (m_pipeline)    { optixPipelineDestroy(m_pipeline);        m_pipeline    = nullptr; }
    if (m_hitgroup_pg) { optixProgramGroupDestroy(m_hitgroup_pg); m_hitgroup_pg = nullptr; }
    if (m_miss_pg)     { optixProgramGroupDestroy(m_miss_pg);     m_miss_pg     = nullptr; }
    if (m_raygen_pg)   { optixProgramGroupDestroy(m_raygen_pg);   m_raygen_pg   = nullptr; }
    if (m_module)      { optixModuleDestroy(m_module);            m_module      = nullptr; }

    /* GAS and point data */
    freeGAS();
    m_d_points.free();

    m_num_points    = 0;
    m_search_radius = 0.0f;
}
