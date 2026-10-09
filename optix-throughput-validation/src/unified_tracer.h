/*
 * unified_tracer.h - Unified ray tracing + closest-point + multi-hit query module
 *
 * Single OptiX pipeline with two PTX modules, three SBTs (RT/MH/NN),
 * and multi-geometry scene management via IAS.
 *
 * Extensions:
 *   E1 - Extended HitResult (geom_id, inst_id, normal)
 *   E2 - Enhanced closest-point query (CPQuery -> CPResult)
 *   E3 - Multi-hit tracing (top-K hits via any-hit program)
 *   E4 - Sphere geometry support
 *   E5 - Enclosure query (inside/outside classification)
 *   E6 - Multi-GAS + IAS scene management
 *
 * Architecture:
 *   Pipeline = RT module (programs.cu) + NN module (nn_programs.cu)
 *   SBT_RT = {raygen_rg, miss_ms, N hitgroup records}
 *   SBT_MH = {raygen_mh, miss_ms, N hitgroup records (with any-hit)}
 *   SBT_NN = {raygen_nn/cp, miss_nn, hitgroup records}
 *   Each optixLaunch uses the appropriate SBT and IAS/GAS handle.
 */
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <optix.h>
#include <cuda_runtime.h>
#include "buffer_manager.h"
#include "ray_types.h"
#include "nn_types.h"
#include "geometry_manager.h"

#include <vector>
#include <string>
#include <map>

/* ---- Geometry Descriptor (E6/E7) ---- */
struct GeometryDesc {
    enum Type { MESH = 0, SPHERE = 1 };
    Type                    type;
    OptixTraversableHandle  gas_handle;
    CUdeviceptr             gas_buffer;

    /* Persistent device data for SBT (closesthit reads vertices for normal) */
    CUdeviceptr             d_vertices;    /* float3*  (MESH only)  */
    CUdeviceptr             d_indices;     /* uint3*   (MESH only)  */
    CUdeviceptr             d_centers;     /* float3*  (SPHERE only) */
    CUdeviceptr             d_radii;       /* float*   (SPHERE only) */

    float                   transform[12]; /* 3x4 row-major (identity if unset) */
    bool                    enabled;
    bool                    flip_normal;   /* E7: flip geometric normal */
    unsigned int            prim_count;
    unsigned int            vert_count;

    /* Host-side data (for CP mesh merge and future ops) */
    std::vector<float3>     host_vertices;
    std::vector<uint3>      host_indices;
    std::vector<float3>     host_centers;
    std::vector<float>      host_radii;
};

/* ---- TraceResult (RT benchmark output) ---- */
struct TraceResult {
    unsigned int num_rays;
    float        trace_time_ms;
    float        total_time_ms;
    double       mrays_per_sec;
    double       grays_per_sec;
    unsigned int num_hits;
    float        hit_rate;
};

/* ---- NNQueryResult (NN benchmark output) ---- */
struct NNQueryResult {
    unsigned int num_queries;
    float        query_time_ms;
    float        total_time_ms;
    double       mqueries_per_sec;
    double       gqueries_per_sec;
    unsigned int num_found;
    float        found_rate;
};

class UnifiedTracer {
public:
    UnifiedTracer()  = default;
    ~UnifiedTracer();

    /*
     * Initialize: create unified pipeline from two PTX modules.
     * Call once; then set scene data and issue queries.
     */
    void init(OptixDeviceContext  context,
              const std::string&  rt_ptx,
              const std::string&  nn_ptx);

    /* ================================================================
     * Multi-Geometry Scene Management (E6)
     * ================================================================*/

    unsigned int addGeometryMesh(const TriangleMesh& mesh,
                                 const float* transform_3x4 = nullptr,
                                 bool compact = true,
                                 bool flip_normal = false);

    unsigned int addGeometrySphere(const SphereMesh& spheres,
                                   const float* transform_3x4 = nullptr,
                                   bool flip_normal = false);

    void removeGeometry(unsigned int geom_id);
    void enableGeometry(unsigned int geom_id, bool enable);
    void rebuildScene();
    void clearAllGeometry();

    /* ================================================================
     * Legacy Triangle Scene Management (convenience wrappers)
     * ================================================================*/

    void setTriangleMesh(const TriangleMesh& mesh, bool compact = true);

    void setTriangleInstances(const TriangleMesh& base_mesh,
                              const float*        transforms_3x4,
                              unsigned int        num_instances);

    /* ================================================================
     * Query Mesh Management (for closest-point queries)
     * ================================================================*/

    void setQueryMesh(const TriangleMesh& mesh, float search_radius);
    void clearQueryMesh();
    void setSearchRadius(float radius);
    void rebuildQueryGAS(bool compact = true);

    /* ================================================================
     * Ray Tracing Queries (single-hit)
     * ================================================================*/

    HitResult traceSingle(const Ray& ray);

    void traceBatch(Ray* d_rays, HitResult* d_hits,
                    unsigned int count, CUstream stream = 0);

    TraceResult traceBatchTimed(Ray* d_rays, HitResult* d_hits,
                                unsigned int count,
                                int num_iters = 10, int warm_up = 3,
                                CUstream stream = 0);

    std::vector<HitResult> traceBatch(const std::vector<Ray>& rays);

    /* ================================================================
     * Multi-Hit Queries (E3)
     * ================================================================*/

    void traceBatchMultiHit(Ray* d_rays, MultiHitResult* d_multi_hits,
                            unsigned int count, CUstream stream = 0);

    std::vector<MultiHitResult> traceBatchMultiHit(const std::vector<Ray>& rays);

    /* ================================================================
     * Closest-Point Queries (simple NNResult interface)
     * ================================================================*/

    NNResult querySingle(const float3& query_point);

    void queryBatch(float3* d_queries, NNResult* d_results,
                    unsigned int count, CUstream stream = 0);

    NNQueryResult queryBatchTimed(float3* d_queries, NNResult* d_results,
                                  unsigned int count,
                                  int num_iters = 10, int warm_up = 3,
                                  CUstream stream = 0);

    std::vector<NNResult> queryBatch(const std::vector<float3>& queries);

    /* ================================================================
     * Enhanced Closest-Point Queries (E2: CPQuery -> CPResult)
     * ================================================================*/

    void closestPointBatch(CPQuery* d_queries, CPResult* d_results,
                           unsigned int count, CUstream stream = 0);

    std::vector<CPResult> closestPointBatch(const std::vector<CPQuery>& queries);

    /* ================================================================
     * Enclosure Queries (E5: EnclosureQuery -> EnclosureResult)
     * ================================================================*/

    void findEnclosureBatch(EnclosureQuery* d_queries, EnclosureResult* d_results,
                            unsigned int count, CUstream stream = 0);

    std::vector<EnclosureResult> findEnclosureBatch(
        const std::vector<EnclosureQuery>& queries);

    /* ================================================================
     * Prim Range Table (for CP geom_id resolution on host)
     * ================================================================*/

    struct PrimRange {
        unsigned int geom_id;
        unsigned int prim_offset;
        unsigned int prim_count;
    };

    std::vector<PrimRange> getQueryPrimRangeTable() const;

    /* ================================================================
     * Geometry flip_normal (E7)
     * ================================================================*/

    void setGeometryFlipNormal(unsigned int geom_id, bool flip);

    /* ================================================================
     * Accessors
     * ================================================================*/

    unsigned int getNumGeometries()    const { return static_cast<unsigned int>(m_geometries.size()); }
    bool         hasScene()            const { return m_ias_handle != 0 || !m_geometries.empty(); }
    bool         hasQueryMesh()        const { return m_aabb_gas_handle != 0; }
    unsigned int getNumQueryTriangles() const { return m_num_query_tris; }
    float        getSearchRadius()     const { return m_search_radius; }

    float3       getSceneBBoxMin()     const { return m_scene_bbox_min; }
    float3       getSceneBBoxMax()     const { return m_scene_bbox_max; }
    float3       getQueryBBoxMin()     const { return m_query_bbox_min; }
    float3       getQueryBBoxMax()     const { return m_query_bbox_max; }

    /* Backward compat aliases */
    float3       getTriBBoxMin()       const { return m_scene_bbox_min; }
    float3       getTriBBoxMax()       const { return m_scene_bbox_max; }
    bool         hasTriangles()        const { return hasScene(); }

    void cleanup();

private:
    /* Pipeline creation */
    void createModules(const std::string& rt_ptx, const std::string& nn_ptx);
    void createProgramGroups();
    void createPipeline();
    void createBaseSBTs();

    /* GAS building */
    void buildMeshGAS(GeometryDesc& desc, bool compact);
    void buildSphereGAS(GeometryDesc& desc);
    void freeGeometryGAS(GeometryDesc& desc);

    /* IAS building */
    void buildIAS();
    void freeIAS();

    /* Dynamic SBT rebuild for multi-geometry */
    void rebuildRTSBT();
    void rebuildMHSBT();

    /* Query GAS */
    void buildQueryGASInternal(bool compact);
    void freeQueryGAS();

    /* Active traversable */
    OptixTraversableHandle activeRTHandle() const { return m_ias_handle; }

    /* BBox utility */
    static void computeBBox(const std::vector<float3>& pts,
                            float3& out_min, float3& out_max);
    void computeSceneBBox();

    void ensureSingleBuffers();

    OptixDeviceContext m_context = nullptr;

    /* ---- Modules ---- */
    OptixModule m_rt_module = nullptr;
    OptixModule m_nn_module = nullptr;

    /* ---- Pipeline ---- */
    OptixPipeline               m_pipeline = nullptr;
    OptixPipelineCompileOptions m_pipeline_compile_options = {};
    OptixModuleCompileOptions   m_module_compile_options   = {};

    /* ---- Program Groups ---- */
    OptixProgramGroup m_raygen_rt_pg         = nullptr;
    OptixProgramGroup m_raygen_mh_pg         = nullptr;
    OptixProgramGroup m_raygen_nn_pg         = nullptr;
    OptixProgramGroup m_raygen_cp_pg         = nullptr;
    OptixProgramGroup m_miss_rt_pg           = nullptr;
    OptixProgramGroup m_miss_nn_pg           = nullptr;
    OptixProgramGroup m_hitgroup_tri_pg      = nullptr;
    OptixProgramGroup m_hitgroup_sphere_pg   = nullptr;
    OptixProgramGroup m_hitgroup_tri_mh_pg   = nullptr;
    OptixProgramGroup m_hitgroup_sphere_mh_pg = nullptr;
    OptixProgramGroup m_hitgroup_aabb_pg     = nullptr;
    OptixProgramGroup m_hitgroup_aabb_sphere_pg = nullptr;

    /* ---- RT SBT (single-hit) ---- */
    OptixShaderBindingTable m_sbt_rt = {};
    CUdeviceptr m_d_rt_raygen_record    = 0;
    CUdeviceptr m_d_rt_miss_record      = 0;
    CUdeviceptr m_d_rt_hitgroup_records = 0;
    unsigned int m_rt_hitgroup_count    = 0;

    /* ---- MH SBT (multi-hit, E3) ---- */
    OptixShaderBindingTable m_sbt_mh = {};
    CUdeviceptr m_d_mh_raygen_record    = 0;
    CUdeviceptr m_d_mh_miss_record      = 0;
    CUdeviceptr m_d_mh_hitgroup_records = 0;
    unsigned int m_mh_hitgroup_count    = 0;

    /* ---- NN SBT (closest-point) ---- */
    OptixShaderBindingTable m_sbt_nn = {};
    CUdeviceptr m_d_nn_raygen_record   = 0;
    CUdeviceptr m_d_nn_miss_record     = 0;
    CUdeviceptr m_d_nn_hitgroup_record = 0;

    /* ---- CP SBT (enhanced closest-point, E2) ---- */
    OptixShaderBindingTable m_sbt_cp = {};
    CUdeviceptr m_d_cp_raygen_record = 0;

    /* ---- ENC SBT (enclosure query, E5) ---- */
    OptixShaderBindingTable m_sbt_enc = {};
    CUdeviceptr m_d_enc_raygen_record = 0;

    /* ---- Enclosure Program Group (E5) ---- */
    OptixProgramGroup m_raygen_enc_pg = nullptr;

    /* ---- Multi-Geometry State (E6) ---- */
    std::map<unsigned int, GeometryDesc> m_geometries;
    unsigned int m_next_geom_id = 0;

    /* ---- IAS ---- */
    OptixTraversableHandle m_ias_handle = 0;
    CUdeviceptr            m_ias_buffer = 0;
    float3                 m_scene_bbox_min = {};
    float3                 m_scene_bbox_max = {};

    /* ---- AABB GAS (query mesh for closest-point queries) ---- */
    OptixTraversableHandle m_aabb_gas_handle   = 0;
    CUdeviceptr            m_aabb_gas_buffer   = 0;
    CudaBuffer<float3>     m_d_nn_vertices;
    CudaBuffer<uint3>      m_d_nn_indices;
    std::vector<float3>    m_host_nn_vertices;
    std::vector<uint3>     m_host_nn_indices;
    unsigned int           m_num_query_tris    = 0;
    float                  m_search_radius     = 0.0f;
    float3                 m_query_bbox_min    = {};
    float3                 m_query_bbox_max    = {};

    /* ---- Query mesh prim range table (for CP geom_id resolution) ---- */
    std::vector<PrimRange> m_query_prim_ranges;

    /* ---- Reusable single-query buffers ---- */
    CudaBuffer<Ray>       m_single_ray_buf;
    CudaBuffer<HitResult> m_single_hit_buf;
    CudaBuffer<float3>    m_single_query_buf;
    CudaBuffer<NNResult>  m_single_result_buf;
    bool                  m_single_bufs_allocated = false;
};
