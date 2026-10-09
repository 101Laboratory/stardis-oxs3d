/*
 * nn_query.h - Nearest neighbor query interface using OptiX RT cores
 *
 * Self-contained module that manages its own pipeline (custom primitives),
 * point cloud GAS, and query dispatch. Based on RTNN (PPoPP 2022) approach:
 * map NN search to ray tracing by representing points as AABBs and queries
 * as zero-length rays.
 *
 * Provides single-query and batch-query interfaces with throughput measurement.
 */
#pragma once

#include <optix.h>
#include <cuda_runtime.h>
#include "nn_types.h"
#include "buffer_manager.h"

#include <vector>
#include <string>

/* ---- Benchmark result for NN queries ---- */
struct NNQueryResult {
    unsigned int num_queries;
    float        query_time_ms;      /* GPU query time (ms), median       */
    float        total_time_ms;      /* Including upload/download         */
    double       mqueries_per_sec;   /* MQueries/s                        */
    double       gqueries_per_sec;   /* GQueries/s                        */
    unsigned int num_found;          /* Number of queries with a match    */
    float        found_rate;         /* found / total                     */
};

class NNQuery {
public:
    NNQuery()  = default;
    ~NNQuery();

    /*
     * Initialize the NN query module: create OptiX pipeline for custom primitives.
     *
     * @param context       OptiX device context
     * @param ptx_source    PTX string for nn_programs (loaded from nn_programs.ptx)
     * @param use_instancing  Whether to allow single-level instancing
     */
    void init(
        OptixDeviceContext  context,
        const std::string&  ptx_source,
        bool                use_instancing = false);

    /*
     * Set the point cloud and build the AABB-based GAS.
     * Must be called before any query. Can be called multiple times to
     * replace the point cloud (previous GAS is freed).
     *
     * @param points         Host-side point cloud positions
     * @param search_radius  Search radius (AABB half-width around each point)
     */
    void setPointCloud(
        const std::vector<float3>& points,
        float                      search_radius);

    /*
     * Query the nearest point to a single query position (synchronous).
     * Low throughput - use for debugging/validation only.
     */
    NNResult querySingle(const float3& query_point);

    /*
     * Batch query: find nearest point for each query position.
     * All data lives on the device.
     *
     * @param d_queries  Device pointer to query positions
     * @param d_results  Device pointer to result buffer (pre-allocated)
     * @param count      Number of queries
     * @param stream     CUDA stream (0 = default)
     */
    void queryBatch(
        float3*      d_queries,
        NNResult*    d_results,
        unsigned int count,
        CUstream     stream = 0);

    /*
     * Batch query with throughput measurement using CUDA events.
     * Includes warm-up iterations. Reports median timing.
     */
    NNQueryResult queryBatchTimed(
        float3*      d_queries,
        NNResult*    d_results,
        unsigned int count,
        int          num_iters = 10,
        int          warm_up   = 3,
        CUstream     stream    = 0);

    /*
     * Batch query from host vectors (handles upload/download automatically).
     */
    std::vector<NNResult> queryBatch(const std::vector<float3>& queries);

    /* Accessors */
    float3 getBBoxMin() const { return m_bbox_min; }
    float3 getBBoxMax() const { return m_bbox_max; }
    float  getSearchRadius() const { return m_search_radius; }
    unsigned int getNumPoints() const { return m_num_points; }

    /* Release all resources */
    void cleanup();

private:
    /* Pipeline creation */
    void createPipeline(const std::string& ptx_source);
    void createModule(const std::string& ptx_source);
    void createProgramGroups();
    void createPipelineObject();
    void createSBT();

    /* GAS for point cloud */
    void buildGAS();
    void freeGAS();

    OptixDeviceContext m_context = nullptr;

    /* Pipeline components */
    OptixModule       m_module      = nullptr;
    OptixPipeline     m_pipeline    = nullptr;
    OptixProgramGroup m_raygen_pg   = nullptr;
    OptixProgramGroup m_miss_pg     = nullptr;
    OptixProgramGroup m_hitgroup_pg = nullptr;

    OptixPipelineCompileOptions m_pipeline_compile_options = {};
    OptixModuleCompileOptions   m_module_compile_options   = {};
    OptixShaderBindingTable     m_sbt = {};

    CUdeviceptr m_d_raygen_record   = 0;
    CUdeviceptr m_d_miss_record     = 0;
    CUdeviceptr m_d_hitgroup_record = 0;

    /* Acceleration structure */
    OptixTraversableHandle m_gas_handle = 0;
    CUdeviceptr            m_gas_buffer = 0;

    /* Point cloud data */
    CudaBuffer<float3> m_d_points;
    unsigned int       m_num_points    = 0;
    float              m_search_radius = 0.0f;
    float3             m_bbox_min      = {};
    float3             m_bbox_max      = {};

    bool m_use_instancing = false;

    /* Reusable buffers for single query */
    CudaBuffer<float3>   m_single_query_buf;
    CudaBuffer<NNResult> m_single_result_buf;
    bool                 m_single_bufs_allocated = false;
};
