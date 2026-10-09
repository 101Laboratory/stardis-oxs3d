/*
 * unified_tracer.cpp - Unified ray tracing + closest-point implementation
 *
 * Creates a single OptiX pipeline from two PTX modules (RT + NN),
 * manages three SBTs (RT/MH/NN) and multi-geometry scene via IAS.
 *
 * Extensions: E1-E6 (see unified_tracer.h)
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "unified_tracer.h"
#include "unified_params.h"
#include "optix_check.h"
#include "nn_kernels.h"
#include "kernels.h"

#include <optix_stack_size.h>

#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>

/* ---- SBT Record Template ---- */
template <typename T>
struct SbtRecord {
    __align__(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T data;
};

struct EmptyData {};

typedef SbtRecord<EmptyData>    RaygenRecord;
typedef SbtRecord<EmptyData>    MissRecord;
typedef SbtRecord<HitGroupData> HitGroupRecord;
typedef SbtRecord<EmptyData>    AABBHitGroupRecord;

/* ===========================================================================
 * Lifecycle
 * =========================================================================*/

UnifiedTracer::~UnifiedTracer()
{
    cleanup();
}

void UnifiedTracer::init(
    OptixDeviceContext  context,
    const std::string&  rt_ptx,
    const std::string&  nn_ptx)
{
    m_context = context;
    createModules(rt_ptx, nn_ptx);
    createProgramGroups();
    createPipeline();
    createBaseSBTs();
    std::cout << "  [UnifiedTracer] Initialized (single pipeline, triple SBT).\n";
}

/* ===========================================================================
 * Module Creation
 * =========================================================================*/

void UnifiedTracer::createModules(
    const std::string& rt_ptx,
    const std::string& nn_ptx)
{
    m_module_compile_options = {};
    m_module_compile_options.optLevel   = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    m_module_compile_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

    m_pipeline_compile_options = {};
    m_pipeline_compile_options.usesMotionBlur        = false;
    m_pipeline_compile_options.numPayloadValues      = 10;  /* E1+E2: 9 RT + 1 extra for CP closest_pos */
    m_pipeline_compile_options.numAttributeValues    = 2;
    m_pipeline_compile_options.exceptionFlags        = OPTIX_EXCEPTION_FLAG_NONE;
    m_pipeline_compile_options.pipelineLaunchParamsVariableName = "params";
    m_pipeline_compile_options.usesPrimitiveTypeFlags =
        OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE | OPTIX_PRIMITIVE_TYPE_FLAGS_CUSTOM;
    m_pipeline_compile_options.traversableGraphFlags =
        OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;

    OPTIX_CHECK_LOG(optixModuleCreate(
        m_context,
        &m_module_compile_options,
        &m_pipeline_compile_options,
        rt_ptx.c_str(), rt_ptx.size(),
        s_optix_log, &s_optix_log_size,
        &m_rt_module));
    std::cout << "  [UnifiedTracer] RT module created.\n";

    OPTIX_CHECK_LOG(optixModuleCreate(
        m_context,
        &m_module_compile_options,
        &m_pipeline_compile_options,
        nn_ptx.c_str(), nn_ptx.size(),
        s_optix_log, &s_optix_log_size,
        &m_nn_module));
    std::cout << "  [UnifiedTracer] NN module created.\n";
}

/* ===========================================================================
 * Program Groups — 12 groups from 2 modules
 * =========================================================================*/

void UnifiedTracer::createProgramGroups()
{
    OptixProgramGroupOptions pg_options = {};

    /* ---- RT Raygen (single-hit) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        desc.raygen.module            = m_rt_module;
        desc.raygen.entryFunctionName = "__raygen__rg";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_raygen_rt_pg));
    }

    /* ---- MH Raygen (multi-hit, E3) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        desc.raygen.module            = m_rt_module;
        desc.raygen.entryFunctionName = "__raygen__mh";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_raygen_mh_pg));
    }

    /* ---- RT Miss ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                   = OPTIX_PROGRAM_GROUP_KIND_MISS;
        desc.miss.module            = m_rt_module;
        desc.miss.entryFunctionName = "__miss__ms";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_miss_rt_pg));
    }

    /* ---- RT HitGroup: triangle (built-in IS + closesthit) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        desc.hitgroup.moduleCH            = m_rt_module;
        desc.hitgroup.entryFunctionNameCH = "__closesthit__ch";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_hitgroup_tri_pg));
    }

    /* ---- RT HitGroup: sphere (IS + closesthit, E4) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        desc.hitgroup.moduleIS            = m_rt_module;
        desc.hitgroup.entryFunctionNameIS = "__intersection__sphere";
        desc.hitgroup.moduleCH            = m_rt_module;
        desc.hitgroup.entryFunctionNameCH = "__closesthit__ch";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_hitgroup_sphere_pg));
    }

    /* ---- MH HitGroup: triangle (built-in IS + anyhit, E3) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        desc.hitgroup.moduleAH            = m_rt_module;
        desc.hitgroup.entryFunctionNameAH = "__anyhit__mh";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_hitgroup_tri_mh_pg));
    }

    /* ---- MH HitGroup: sphere (IS + anyhit, E3/E4) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        desc.hitgroup.moduleIS            = m_rt_module;
        desc.hitgroup.entryFunctionNameIS = "__intersection__sphere";
        desc.hitgroup.moduleAH            = m_rt_module;
        desc.hitgroup.entryFunctionNameAH = "__anyhit__mh";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_hitgroup_sphere_mh_pg));
    }

    /* ---- NN Raygen ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        desc.raygen.module            = m_nn_module;
        desc.raygen.entryFunctionName = "__raygen__nn";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_raygen_nn_pg));
    }

    /* ---- CP Raygen (E2) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        desc.raygen.module            = m_nn_module;
        desc.raygen.entryFunctionName = "__raygen__cp";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_raygen_cp_pg));
    }

    /* ---- NN Miss ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                   = OPTIX_PROGRAM_GROUP_KIND_MISS;
        desc.miss.module            = m_nn_module;
        desc.miss.entryFunctionName = "__miss__nn";
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_miss_nn_pg));
    }

    /* ---- NN HitGroup: triangle AABB (custom IS) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                          = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        desc.hitgroup.moduleIS             = m_nn_module;
        desc.hitgroup.entryFunctionNameIS  = "__intersection__nn";
        desc.hitgroup.moduleCH             = nullptr;
        desc.hitgroup.entryFunctionNameCH  = nullptr;
        desc.hitgroup.moduleAH             = nullptr;
        desc.hitgroup.entryFunctionNameAH  = nullptr;
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_hitgroup_aabb_pg));
    }

    /* ---- NN HitGroup: sphere AABB (E4) ---- */
    {
        OptixProgramGroupDesc desc = {};
        desc.kind                          = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        desc.hitgroup.moduleIS             = m_nn_module;
        desc.hitgroup.entryFunctionNameIS  = "__intersection__nn_sphere";
        desc.hitgroup.moduleCH             = nullptr;
        desc.hitgroup.entryFunctionNameCH  = nullptr;
        desc.hitgroup.moduleAH             = nullptr;
        desc.hitgroup.entryFunctionNameAH  = nullptr;
        OPTIX_CHECK_LOG(optixProgramGroupCreate(
            m_context, &desc, 1, &pg_options,
            s_optix_log, &s_optix_log_size, &m_hitgroup_aabb_sphere_pg));
    }

    std::cout << "  [UnifiedTracer] 12 program groups created.\n";
}

/* ===========================================================================
 * Pipeline — links all program groups
 * =========================================================================*/

void UnifiedTracer::createPipeline()
{
    OptixProgramGroup pgs[] = {
        m_raygen_rt_pg, m_raygen_mh_pg,
        m_miss_rt_pg, m_miss_nn_pg,
        m_hitgroup_tri_pg, m_hitgroup_sphere_pg,
        m_hitgroup_tri_mh_pg, m_hitgroup_sphere_mh_pg,
        m_raygen_nn_pg, m_raygen_cp_pg,
        m_hitgroup_aabb_pg, m_hitgroup_aabb_sphere_pg
    };

    OptixPipelineLinkOptions link_options = {};
    link_options.maxTraceDepth = 1;

    OPTIX_CHECK_LOG(optixPipelineCreate(
        m_context,
        &m_pipeline_compile_options,
        &link_options,
        pgs, 12,
        s_optix_log, &s_optix_log_size,
        &m_pipeline));

    OptixStackSizes stack_sizes = {};
    for (auto& pg : pgs) {
        OPTIX_CHECK(optixUtilAccumulateStackSizes(pg, &stack_sizes, m_pipeline));
    }

    uint32_t dc_from_traversal, dc_from_state, continuation;
    OPTIX_CHECK(optixUtilComputeStackSizes(
        &stack_sizes, 1, 0, 0,
        &dc_from_traversal, &dc_from_state, &continuation));

    OPTIX_CHECK(optixPipelineSetStackSize(
        m_pipeline,
        dc_from_traversal,
        dc_from_state,
        continuation,
        2 /* maxTraversalDepth: IAS -> GAS */));

    std::cout << "  [UnifiedTracer] Pipeline created (12 PGs, stack configured).\n";
}

/* ===========================================================================
 * Base SBT Creation — raygen + miss records (hitgroup built per-scene)
 * =========================================================================*/

void UnifiedTracer::createBaseSBTs()
{
    /* ---- RT SBT: raygen + miss ---- */
    {
        RaygenRecord rg = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_raygen_rt_pg, &rg));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_rt_raygen_record), sizeof(RaygenRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_rt_raygen_record), &rg, sizeof(RaygenRecord), cudaMemcpyHostToDevice));

        MissRecord ms = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_miss_rt_pg, &ms));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_rt_miss_record), sizeof(MissRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_rt_miss_record), &ms, sizeof(MissRecord), cudaMemcpyHostToDevice));

        /* Hitgroup placeholder (1 empty record so SBT is valid before scene) */
        HitGroupRecord hg = {};
        memset(&hg.data, 0, sizeof(HitGroupData));
        OPTIX_CHECK(optixSbtRecordPackHeader(m_hitgroup_tri_pg, &hg));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_rt_hitgroup_records), sizeof(HitGroupRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_rt_hitgroup_records), &hg, sizeof(HitGroupRecord), cudaMemcpyHostToDevice));

        m_sbt_rt = {};
        m_sbt_rt.raygenRecord                = m_d_rt_raygen_record;
        m_sbt_rt.missRecordBase              = m_d_rt_miss_record;
        m_sbt_rt.missRecordStrideInBytes     = sizeof(MissRecord);
        m_sbt_rt.missRecordCount             = 1;
        m_sbt_rt.hitgroupRecordBase          = m_d_rt_hitgroup_records;
        m_sbt_rt.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
        m_sbt_rt.hitgroupRecordCount         = 1;
        m_rt_hitgroup_count = 1;
    }

    /* ---- MH SBT: raygen + miss (E3) ---- */
    {
        RaygenRecord rg = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_raygen_mh_pg, &rg));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_mh_raygen_record), sizeof(RaygenRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_mh_raygen_record), &rg, sizeof(RaygenRecord), cudaMemcpyHostToDevice));

        MissRecord ms = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_miss_rt_pg, &ms));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_mh_miss_record), sizeof(MissRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_mh_miss_record), &ms, sizeof(MissRecord), cudaMemcpyHostToDevice));

        HitGroupRecord hg = {};
        memset(&hg.data, 0, sizeof(HitGroupData));
        OPTIX_CHECK(optixSbtRecordPackHeader(m_hitgroup_tri_mh_pg, &hg));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_mh_hitgroup_records), sizeof(HitGroupRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_mh_hitgroup_records), &hg, sizeof(HitGroupRecord), cudaMemcpyHostToDevice));

        m_sbt_mh = {};
        m_sbt_mh.raygenRecord                = m_d_mh_raygen_record;
        m_sbt_mh.missRecordBase              = m_d_mh_miss_record;
        m_sbt_mh.missRecordStrideInBytes     = sizeof(MissRecord);
        m_sbt_mh.missRecordCount             = 1;
        m_sbt_mh.hitgroupRecordBase          = m_d_mh_hitgroup_records;
        m_sbt_mh.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
        m_sbt_mh.hitgroupRecordCount         = 1;
        m_mh_hitgroup_count = 1;
    }

    /* ---- NN SBT ---- */
    {
        RaygenRecord rg = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_raygen_nn_pg, &rg));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_nn_raygen_record), sizeof(RaygenRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_nn_raygen_record), &rg, sizeof(RaygenRecord), cudaMemcpyHostToDevice));

        MissRecord ms = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_miss_nn_pg, &ms));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_nn_miss_record), sizeof(MissRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_nn_miss_record), &ms, sizeof(MissRecord), cudaMemcpyHostToDevice));

        AABBHitGroupRecord hg = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_hitgroup_aabb_pg, &hg));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_nn_hitgroup_record), sizeof(AABBHitGroupRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_nn_hitgroup_record), &hg, sizeof(AABBHitGroupRecord), cudaMemcpyHostToDevice));

        m_sbt_nn = {};
        m_sbt_nn.raygenRecord                = m_d_nn_raygen_record;
        m_sbt_nn.missRecordBase              = m_d_nn_miss_record;
        m_sbt_nn.missRecordStrideInBytes     = sizeof(MissRecord);
        m_sbt_nn.missRecordCount             = 1;
        m_sbt_nn.hitgroupRecordBase          = m_d_nn_hitgroup_record;
        m_sbt_nn.hitgroupRecordStrideInBytes = sizeof(AABBHitGroupRecord);
        m_sbt_nn.hitgroupRecordCount         = 1;
    }

    /* ---- CP SBT (E2) — shares miss + hitgroup with NN ---- */
    {
        RaygenRecord rg = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_raygen_cp_pg, &rg));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_cp_raygen_record), sizeof(RaygenRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_cp_raygen_record), &rg, sizeof(RaygenRecord), cudaMemcpyHostToDevice));

        m_sbt_cp = {};
        m_sbt_cp.raygenRecord                = m_d_cp_raygen_record;
        m_sbt_cp.missRecordBase              = m_d_nn_miss_record;     /* shared */
        m_sbt_cp.missRecordStrideInBytes     = sizeof(MissRecord);
        m_sbt_cp.missRecordCount             = 1;
        m_sbt_cp.hitgroupRecordBase          = m_d_nn_hitgroup_record; /* shared */
        m_sbt_cp.hitgroupRecordStrideInBytes = sizeof(AABBHitGroupRecord);
        m_sbt_cp.hitgroupRecordCount         = 1;
    }

    std::cout << "  [UnifiedTracer] Base SBTs created (RT + MH + NN + CP).\n";
}

/* ===========================================================================
 * BBox Utility
 * =========================================================================*/

void UnifiedTracer::computeBBox(
    const std::vector<float3>& pts,
    float3& out_min, float3& out_max)
{
    out_min = make_float3( 1e30f,  1e30f,  1e30f);
    out_max = make_float3(-1e30f, -1e30f, -1e30f);
    for (const auto& p : pts) {
        out_min.x = std::min(out_min.x, p.x);
        out_min.y = std::min(out_min.y, p.y);
        out_min.z = std::min(out_min.z, p.z);
        out_max.x = std::max(out_max.x, p.x);
        out_max.y = std::max(out_max.y, p.y);
        out_max.z = std::max(out_max.z, p.z);
    }
}

void UnifiedTracer::computeSceneBBox()
{
    m_scene_bbox_min = make_float3( 1e30f,  1e30f,  1e30f);
    m_scene_bbox_max = make_float3(-1e30f, -1e30f, -1e30f);

    for (auto& kv : m_geometries) {
        auto& desc = kv.second;
        if (!desc.enabled) continue;

        /* Get local bbox */
        float3 local_min = make_float3( 1e30f,  1e30f,  1e30f);
        float3 local_max = make_float3(-1e30f, -1e30f, -1e30f);

        if (desc.type == GeometryDesc::MESH) {
            computeBBox(desc.host_vertices, local_min, local_max);
        } else {
            for (size_t i = 0; i < desc.host_centers.size(); ++i) {
                float3 c = desc.host_centers[i];
                float  r = desc.host_radii[i];
                local_min.x = std::min(local_min.x, c.x - r);
                local_min.y = std::min(local_min.y, c.y - r);
                local_min.z = std::min(local_min.z, c.z - r);
                local_max.x = std::max(local_max.x, c.x + r);
                local_max.y = std::max(local_max.y, c.y + r);
                local_max.z = std::max(local_max.z, c.z + r);
            }
        }

        /* Transform bbox corners (conservative) */
        const float* t = desc.transform;
        float3 corners[8];
        corners[0] = make_float3(local_min.x, local_min.y, local_min.z);
        corners[1] = make_float3(local_max.x, local_min.y, local_min.z);
        corners[2] = make_float3(local_min.x, local_max.y, local_min.z);
        corners[3] = make_float3(local_max.x, local_max.y, local_min.z);
        corners[4] = make_float3(local_min.x, local_min.y, local_max.z);
        corners[5] = make_float3(local_max.x, local_min.y, local_max.z);
        corners[6] = make_float3(local_min.x, local_max.y, local_max.z);
        corners[7] = make_float3(local_max.x, local_max.y, local_max.z);

        for (int c = 0; c < 8; ++c) {
            float3 p = corners[c];
            float3 tp;
            tp.x = t[0]*p.x + t[1]*p.y + t[ 2]*p.z + t[ 3];
            tp.y = t[4]*p.x + t[5]*p.y + t[ 6]*p.z + t[ 7];
            tp.z = t[8]*p.x + t[9]*p.y + t[10]*p.z + t[11];
            m_scene_bbox_min.x = std::min(m_scene_bbox_min.x, tp.x);
            m_scene_bbox_min.y = std::min(m_scene_bbox_min.y, tp.y);
            m_scene_bbox_min.z = std::min(m_scene_bbox_min.z, tp.z);
            m_scene_bbox_max.x = std::max(m_scene_bbox_max.x, tp.x);
            m_scene_bbox_max.y = std::max(m_scene_bbox_max.y, tp.y);
            m_scene_bbox_max.z = std::max(m_scene_bbox_max.z, tp.z);
        }
    }
}

void UnifiedTracer::ensureSingleBuffers()
{
    if (!m_single_bufs_allocated) {
        m_single_ray_buf.alloc(1);
        m_single_hit_buf.alloc(1);
        m_single_query_buf.alloc(1);
        m_single_result_buf.alloc(1);
        m_single_bufs_allocated = true;
    }
}

/* ===========================================================================
 * Mesh GAS Building (E6)
 * =========================================================================*/

void UnifiedTracer::buildMeshGAS(GeometryDesc& desc, bool compact)
{
    /* Upload vertex + index data (persistent for closesthit normal) */
    size_t v_bytes = desc.host_vertices.size() * sizeof(float3);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&desc.d_vertices), v_bytes));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(desc.d_vertices),
                          desc.host_vertices.data(), v_bytes, cudaMemcpyHostToDevice));

    size_t i_bytes = desc.host_indices.size() * sizeof(uint3);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&desc.d_indices), i_bytes));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(desc.d_indices),
                          desc.host_indices.data(), i_bytes, cudaMemcpyHostToDevice));

    /* Build input — NO DISABLE_ANYHIT so multi-hit works via ray flag */
    const uint32_t flags[1] = { OPTIX_GEOMETRY_FLAG_NONE };

    OptixBuildInput build_input = {};
    build_input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    build_input.triangleArray.vertexFormat    = OPTIX_VERTEX_FORMAT_FLOAT3;
    build_input.triangleArray.numVertices     = static_cast<uint32_t>(desc.host_vertices.size());
    build_input.triangleArray.vertexBuffers   = &desc.d_vertices;
    build_input.triangleArray.indexFormat     = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    build_input.triangleArray.numIndexTriplets = static_cast<uint32_t>(desc.host_indices.size());
    build_input.triangleArray.indexBuffer     = desc.d_indices;
    build_input.triangleArray.flags           = flags;
    build_input.triangleArray.numSbtRecords   = 1;

    OptixAccelBuildOptions build_options = {};
    build_options.buildFlags = compact
        ? (OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE)
        : OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    build_options.operation = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes buf_sizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(
        m_context, &build_options, &build_input, 1, &buf_sizes));

    CUdeviceptr d_temp = 0, d_output = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_temp), buf_sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_output), buf_sizes.outputSizeInBytes));

    CUdeviceptr d_compacted_size = 0;
    OptixAccelEmitDesc emit = {};
    if (compact) {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted_size), sizeof(size_t)));
        emit.type   = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emit.result = d_compacted_size;
    }

    OPTIX_CHECK(optixAccelBuild(
        m_context, 0, &build_options, &build_input, 1,
        d_temp, buf_sizes.tempSizeInBytes,
        d_output, buf_sizes.outputSizeInBytes,
        &desc.gas_handle,
        compact ? &emit : nullptr, compact ? 1 : 0));
    CUDA_CHECK(cudaDeviceSynchronize());

    if (compact) {
        size_t compacted_size = 0;
        CUDA_CHECK(cudaMemcpy(&compacted_size, reinterpret_cast<void*>(d_compacted_size),
                              sizeof(size_t), cudaMemcpyDeviceToHost));
        if (compacted_size < buf_sizes.outputSizeInBytes) {
            CUdeviceptr d_compacted = 0;
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted), compacted_size));
            OPTIX_CHECK(optixAccelCompact(m_context, 0, desc.gas_handle,
                                          d_compacted, compacted_size, &desc.gas_handle));
            CUDA_CHECK(cudaDeviceSynchronize());
            CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_output)));
            desc.gas_buffer = d_compacted;
        } else {
            desc.gas_buffer = d_output;
        }
        CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_compacted_size)));
    } else {
        desc.gas_buffer = d_output;
    }

    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_temp)));
}

/* ===========================================================================
 * Sphere GAS Building (E4) — custom AABB primitives
 * =========================================================================*/

void UnifiedTracer::buildSphereGAS(GeometryDesc& desc)
{
    unsigned int num_spheres = static_cast<unsigned int>(desc.host_centers.size());

    /* Upload sphere data (persistent for closesthit) */
    size_t c_bytes = num_spheres * sizeof(float3);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&desc.d_centers), c_bytes));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(desc.d_centers),
                          desc.host_centers.data(), c_bytes, cudaMemcpyHostToDevice));

    size_t r_bytes = num_spheres * sizeof(float);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&desc.d_radii), r_bytes));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(desc.d_radii),
                          desc.host_radii.data(), r_bytes, cudaMemcpyHostToDevice));

    /* Generate AABBs for spheres */
    size_t aabb_bytes = num_spheres * 6 * sizeof(float);
    CUdeviceptr d_aabbs = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_aabbs), aabb_bytes));

    generateSphereAABBsDevice(
        reinterpret_cast<void*>(d_aabbs),
        reinterpret_cast<const float3*>(desc.d_centers),
        reinterpret_cast<const float*>(desc.d_radii),
        num_spheres, 0.0f /* no extra search radius for RT spheres */);
    CUDA_CHECK(cudaDeviceSynchronize());

    /* Build custom AABB GAS */
    const uint32_t aabb_flags[1] = { OPTIX_GEOMETRY_FLAG_NONE };

    OptixBuildInput build_input = {};
    build_input.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
    build_input.customPrimitiveArray.aabbBuffers    = &d_aabbs;
    build_input.customPrimitiveArray.numPrimitives   = num_spheres;
    build_input.customPrimitiveArray.strideInBytes    = 0;
    build_input.customPrimitiveArray.flags            = aabb_flags;
    build_input.customPrimitiveArray.numSbtRecords    = 1;

    OptixAccelBuildOptions build_options = {};
    build_options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    build_options.operation  = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes buf_sizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(
        m_context, &build_options, &build_input, 1, &buf_sizes));

    CUdeviceptr d_temp = 0, d_output = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_temp), buf_sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_output), buf_sizes.outputSizeInBytes));

    OPTIX_CHECK(optixAccelBuild(
        m_context, 0, &build_options, &build_input, 1,
        d_temp, buf_sizes.tempSizeInBytes,
        d_output, buf_sizes.outputSizeInBytes,
        &desc.gas_handle,
        nullptr, 0));
    CUDA_CHECK(cudaDeviceSynchronize());

    desc.gas_buffer = d_output;

    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_temp)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_aabbs)));
}

void UnifiedTracer::freeGeometryGAS(GeometryDesc& desc)
{
    if (desc.gas_buffer) { cudaFree(reinterpret_cast<void*>(desc.gas_buffer)); desc.gas_buffer = 0; desc.gas_handle = 0; }
    if (desc.d_vertices) { cudaFree(reinterpret_cast<void*>(desc.d_vertices)); desc.d_vertices = 0; }
    if (desc.d_indices)  { cudaFree(reinterpret_cast<void*>(desc.d_indices));  desc.d_indices = 0; }
    if (desc.d_centers)  { cudaFree(reinterpret_cast<void*>(desc.d_centers));  desc.d_centers = 0; }
    if (desc.d_radii)    { cudaFree(reinterpret_cast<void*>(desc.d_radii));    desc.d_radii = 0; }
}

/* ===========================================================================
 * Multi-Geometry Scene Management (E6)
 * =========================================================================*/

static void setIdentityTransform(float* t)
{
    memset(t, 0, sizeof(float) * 12);
    t[0] = 1.0f; t[5] = 1.0f; t[10] = 1.0f;
}

unsigned int UnifiedTracer::addGeometryMesh(
    const TriangleMesh& mesh,
    const float* transform_3x4,
    bool compact,
    bool flip_normal)
{
    unsigned int gid = m_next_geom_id++;

    GeometryDesc desc = {};
    desc.type         = GeometryDesc::MESH;
    desc.gas_handle   = 0;
    desc.gas_buffer   = 0;
    desc.d_vertices   = 0;
    desc.d_indices    = 0;
    desc.d_centers    = 0;
    desc.d_radii      = 0;
    desc.enabled      = true;
    desc.flip_normal  = flip_normal;
    desc.prim_count   = static_cast<unsigned int>(mesh.indices.size());
    desc.vert_count   = static_cast<unsigned int>(mesh.vertices.size());
    desc.host_vertices = mesh.vertices;
    desc.host_indices  = mesh.indices;

    if (transform_3x4) {
        memcpy(desc.transform, transform_3x4, sizeof(float) * 12);
    } else {
        setIdentityTransform(desc.transform);
    }

    buildMeshGAS(desc, compact);

    m_geometries[gid] = std::move(desc);
    return gid;
}

unsigned int UnifiedTracer::addGeometrySphere(
    const SphereMesh& spheres,
    const float* transform_3x4,
    bool flip_normal)
{
    unsigned int gid = m_next_geom_id++;

    GeometryDesc desc = {};
    desc.type         = GeometryDesc::SPHERE;
    desc.gas_handle   = 0;
    desc.gas_buffer   = 0;
    desc.d_vertices   = 0;
    desc.d_indices    = 0;
    desc.d_centers    = 0;
    desc.d_radii      = 0;
    desc.enabled      = true;
    desc.flip_normal  = flip_normal;
    desc.prim_count   = static_cast<unsigned int>(spheres.centers.size());
    desc.vert_count   = 0;
    desc.host_centers = spheres.centers;
    desc.host_radii   = spheres.radii;

    if (transform_3x4) {
        memcpy(desc.transform, transform_3x4, sizeof(float) * 12);
    } else {
        setIdentityTransform(desc.transform);
    }

    buildSphereGAS(desc);

    m_geometries[gid] = std::move(desc);
    return gid;
}

void UnifiedTracer::removeGeometry(unsigned int geom_id)
{
    auto it = m_geometries.find(geom_id);
    if (it != m_geometries.end()) {
        freeGeometryGAS(it->second);
        m_geometries.erase(it);
    }
}

void UnifiedTracer::enableGeometry(unsigned int geom_id, bool enable)
{
    auto it = m_geometries.find(geom_id);
    if (it != m_geometries.end()) {
        it->second.enabled = enable;
    }
}

void UnifiedTracer::clearAllGeometry()
{
    for (auto& kv : m_geometries) {
        freeGeometryGAS(kv.second);
    }
    m_geometries.clear();
    m_next_geom_id = 0;
    freeIAS();
}

/* ===========================================================================
 * IAS Building — from all enabled geometries
 * =========================================================================*/

void UnifiedTracer::buildIAS()
{
    freeIAS();

    /* Collect enabled geometries with their SBT indices */
    std::vector<OptixInstance> instances;
    unsigned int sbt_idx = 0;

    for (auto& kv : m_geometries) {
        auto& desc = kv.second;
        if (!desc.enabled || desc.gas_handle == 0) continue;

        OptixInstance inst;
        memset(&inst, 0, sizeof(OptixInstance));
        memcpy(inst.transform, desc.transform, sizeof(float) * 12);
        inst.instanceId        = kv.first;     /* geom_id as instance ID */
        inst.sbtOffset         = sbt_idx;      /* one hitgroup record per geometry */
        inst.visibilityMask    = 255;
        inst.flags             = OPTIX_INSTANCE_FLAG_NONE;
        inst.traversableHandle = desc.gas_handle;
        instances.push_back(inst);
        sbt_idx++;
    }

    if (instances.empty()) return;

    CUdeviceptr d_instances = 0;
    size_t inst_bytes = instances.size() * sizeof(OptixInstance);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_instances), inst_bytes));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(d_instances),
                          instances.data(), inst_bytes, cudaMemcpyHostToDevice));

    OptixBuildInput build_input = {};
    build_input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    build_input.instanceArray.instances    = d_instances;
    build_input.instanceArray.numInstances = static_cast<unsigned int>(instances.size());

    OptixAccelBuildOptions build_options = {};
    build_options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    build_options.operation  = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes buf_sizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(
        m_context, &build_options, &build_input, 1, &buf_sizes));

    CUdeviceptr d_temp = 0, d_output = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_temp), buf_sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_output), buf_sizes.outputSizeInBytes));

    OPTIX_CHECK(optixAccelBuild(
        m_context, 0, &build_options, &build_input, 1,
        d_temp, buf_sizes.tempSizeInBytes,
        d_output, buf_sizes.outputSizeInBytes,
        &m_ias_handle,
        nullptr, 0));
    CUDA_CHECK(cudaDeviceSynchronize());

    m_ias_buffer = d_output;

    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_temp)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_instances)));
}

void UnifiedTracer::freeIAS()
{
    if (m_ias_buffer) {
        cudaFree(reinterpret_cast<void*>(m_ias_buffer));
        m_ias_buffer = 0;
        m_ias_handle = 0;
    }
}

/* ===========================================================================
 * Dynamic SBT Rebuild (E6) — per-geometry hitgroup records
 * =========================================================================*/

void UnifiedTracer::rebuildRTSBT()
{
    /* Free old hitgroup records */
    if (m_d_rt_hitgroup_records) {
        cudaFree(reinterpret_cast<void*>(m_d_rt_hitgroup_records));
        m_d_rt_hitgroup_records = 0;
    }

    /* Build one HitGroupRecord per enabled geometry (order matches IAS sbtOffset) */
    std::vector<HitGroupRecord> records;
    for (auto& kv : m_geometries) {
        auto& desc = kv.second;
        if (!desc.enabled || desc.gas_handle == 0) continue;

        HitGroupRecord rec = {};
        OptixProgramGroup pg = (desc.type == GeometryDesc::MESH)
                                ? m_hitgroup_tri_pg
                                : m_hitgroup_sphere_pg;
        OPTIX_CHECK(optixSbtRecordPackHeader(pg, &rec));

        rec.data.geom_id  = kv.first;
        rec.data.geom_type = (desc.type == GeometryDesc::MESH) ? GEOM_TYPE_TRIANGLE : GEOM_TYPE_SPHERE;
        rec.data.flip_normal = desc.flip_normal ? 1u : 0u;
        rec.data.pad0 = 0;
        rec.data.vertices       = reinterpret_cast<float3*>(desc.d_vertices);
        rec.data.indices        = reinterpret_cast<uint3*>(desc.d_indices);
        rec.data.sphere_centers = reinterpret_cast<float3*>(desc.d_centers);
        rec.data.sphere_radii   = reinterpret_cast<float*>(desc.d_radii);

        records.push_back(rec);
    }

    if (records.empty()) {
        /* Insert a dummy record so SBT is valid */
        HitGroupRecord dummy = {};
        memset(&dummy.data, 0, sizeof(HitGroupData));
        OPTIX_CHECK(optixSbtRecordPackHeader(m_hitgroup_tri_pg, &dummy));
        records.push_back(dummy);
    }

    size_t bytes = records.size() * sizeof(HitGroupRecord);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_rt_hitgroup_records), bytes));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_rt_hitgroup_records),
                          records.data(), bytes, cudaMemcpyHostToDevice));

    m_sbt_rt.hitgroupRecordBase          = m_d_rt_hitgroup_records;
    m_sbt_rt.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
    m_sbt_rt.hitgroupRecordCount         = static_cast<unsigned int>(records.size());
    m_rt_hitgroup_count = static_cast<unsigned int>(records.size());
}

void UnifiedTracer::rebuildMHSBT()
{
    if (m_d_mh_hitgroup_records) {
        cudaFree(reinterpret_cast<void*>(m_d_mh_hitgroup_records));
        m_d_mh_hitgroup_records = 0;
    }

    std::vector<HitGroupRecord> records;
    for (auto& kv : m_geometries) {
        auto& desc = kv.second;
        if (!desc.enabled || desc.gas_handle == 0) continue;

        HitGroupRecord rec = {};
        OptixProgramGroup pg = (desc.type == GeometryDesc::MESH)
                                ? m_hitgroup_tri_mh_pg
                                : m_hitgroup_sphere_mh_pg;
        OPTIX_CHECK(optixSbtRecordPackHeader(pg, &rec));

        rec.data.geom_id  = kv.first;
        rec.data.geom_type = (desc.type == GeometryDesc::MESH) ? GEOM_TYPE_TRIANGLE : GEOM_TYPE_SPHERE;
        rec.data.flip_normal = desc.flip_normal ? 1u : 0u;
        rec.data.pad0 = 0;
        rec.data.vertices       = reinterpret_cast<float3*>(desc.d_vertices);
        rec.data.indices        = reinterpret_cast<uint3*>(desc.d_indices);
        rec.data.sphere_centers = reinterpret_cast<float3*>(desc.d_centers);
        rec.data.sphere_radii   = reinterpret_cast<float*>(desc.d_radii);

        records.push_back(rec);
    }

    if (records.empty()) {
        HitGroupRecord dummy = {};
        memset(&dummy.data, 0, sizeof(HitGroupData));
        OPTIX_CHECK(optixSbtRecordPackHeader(m_hitgroup_tri_mh_pg, &dummy));
        records.push_back(dummy);
    }

    size_t bytes = records.size() * sizeof(HitGroupRecord);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_mh_hitgroup_records), bytes));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_mh_hitgroup_records),
                          records.data(), bytes, cudaMemcpyHostToDevice));

    m_sbt_mh.hitgroupRecordBase          = m_d_mh_hitgroup_records;
    m_sbt_mh.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
    m_sbt_mh.hitgroupRecordCount         = static_cast<unsigned int>(records.size());
    m_mh_hitgroup_count = static_cast<unsigned int>(records.size());
}

/* ===========================================================================
 * rebuildScene — rebuilds IAS + SBTs + bbox
 * =========================================================================*/

void UnifiedTracer::rebuildScene()
{
    buildIAS();
    rebuildRTSBT();
    rebuildMHSBT();
    computeSceneBBox();
}

/* ===========================================================================
 * Legacy Scene Management (convenience wrappers)
 * =========================================================================*/

void UnifiedTracer::setTriangleMesh(const TriangleMesh& mesh, bool compact)
{
    clearAllGeometry();
    addGeometryMesh(mesh, nullptr, compact);
    rebuildScene();
}

void UnifiedTracer::setTriangleInstances(
    const TriangleMesh& base_mesh,
    const float*        transforms_3x4,
    unsigned int        num_instances)
{
    clearAllGeometry();
    for (unsigned int i = 0; i < num_instances; ++i) {
        addGeometryMesh(base_mesh, transforms_3x4 + i * 12, true);
    }
    rebuildScene();
}

/* ===========================================================================
 * Query Mesh Management (for closest-point queries)
 * =========================================================================*/

void UnifiedTracer::setQueryMesh(
    const TriangleMesh& mesh,
    float               search_radius)
{
    freeQueryGAS();
    m_host_nn_vertices = mesh.vertices;
    m_host_nn_indices  = mesh.indices;
    m_num_query_tris   = static_cast<unsigned int>(mesh.indices.size());
    m_search_radius    = search_radius;
    computeBBox(mesh.vertices, m_query_bbox_min, m_query_bbox_max);

    m_d_nn_vertices.alloc(static_cast<unsigned int>(mesh.vertices.size()));
    m_d_nn_vertices.upload(mesh.vertices);
    m_d_nn_indices.alloc(m_num_query_tris);
    m_d_nn_indices.upload(mesh.indices);

    buildQueryGASInternal(true);
}

void UnifiedTracer::clearQueryMesh()
{
    freeQueryGAS();
    m_d_nn_vertices.free();
    m_d_nn_indices.free();
    m_host_nn_vertices.clear();
    m_host_nn_indices.clear();
    m_num_query_tris = 0;
    m_search_radius  = 0.0f;
    m_query_bbox_min = {};
    m_query_bbox_max = {};
}

void UnifiedTracer::setSearchRadius(float radius)
{
    m_search_radius = radius;
}

void UnifiedTracer::rebuildQueryGAS(bool compact)
{
    freeQueryGAS();
    if (m_host_nn_indices.empty()) return;

    computeBBox(m_host_nn_vertices, m_query_bbox_min, m_query_bbox_max);
    m_num_query_tris = static_cast<unsigned int>(m_host_nn_indices.size());

    m_d_nn_vertices.alloc(static_cast<unsigned int>(m_host_nn_vertices.size()));
    m_d_nn_vertices.upload(m_host_nn_vertices);
    m_d_nn_indices.alloc(m_num_query_tris);
    m_d_nn_indices.upload(m_host_nn_indices);

    buildQueryGASInternal(compact);
}

void UnifiedTracer::buildQueryGASInternal(bool compact)
{
    const size_t aabb_bytes = m_num_query_tris * 6 * sizeof(float);
    CUdeviceptr d_aabbs = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_aabbs), aabb_bytes));

    generateTriAABBsDevice(
        reinterpret_cast<void*>(d_aabbs),
        m_d_nn_vertices.get(), m_d_nn_indices.get(),
        m_num_query_tris, m_search_radius);
    CUDA_CHECK(cudaDeviceSynchronize());

    const uint32_t aabb_flags[1] = { OPTIX_GEOMETRY_FLAG_NONE };
    OptixBuildInput build_input = {};
    build_input.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
    build_input.customPrimitiveArray.aabbBuffers   = &d_aabbs;
    build_input.customPrimitiveArray.numPrimitives  = m_num_query_tris;
    build_input.customPrimitiveArray.strideInBytes   = 0;
    build_input.customPrimitiveArray.flags           = aabb_flags;
    build_input.customPrimitiveArray.numSbtRecords   = 1;

    OptixAccelBuildOptions build_options = {};
    build_options.buildFlags = compact
        ? (OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE)
        : OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    build_options.operation = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes buf_sizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(
        m_context, &build_options, &build_input, 1, &buf_sizes));

    CUdeviceptr d_temp = 0, d_output = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_temp), buf_sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_output), buf_sizes.outputSizeInBytes));

    CUdeviceptr d_compacted_size = 0;
    OptixAccelEmitDesc emit = {};
    if (compact) {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted_size), sizeof(size_t)));
        emit.type   = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emit.result = d_compacted_size;
    }

    OPTIX_CHECK(optixAccelBuild(
        m_context, 0, &build_options, &build_input, 1,
        d_temp, buf_sizes.tempSizeInBytes,
        d_output, buf_sizes.outputSizeInBytes,
        &m_aabb_gas_handle,
        compact ? &emit : nullptr, compact ? 1 : 0));
    CUDA_CHECK(cudaDeviceSynchronize());

    if (compact) {
        size_t compacted_size = 0;
        CUDA_CHECK(cudaMemcpy(&compacted_size, reinterpret_cast<void*>(d_compacted_size),
                              sizeof(size_t), cudaMemcpyDeviceToHost));
        if (compacted_size < buf_sizes.outputSizeInBytes) {
            CUdeviceptr d_compacted = 0;
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted), compacted_size));
            OPTIX_CHECK(optixAccelCompact(m_context, 0, m_aabb_gas_handle,
                                          d_compacted, compacted_size, &m_aabb_gas_handle));
            CUDA_CHECK(cudaDeviceSynchronize());
            CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_output)));
            m_aabb_gas_buffer = d_compacted;
        } else {
            m_aabb_gas_buffer = d_output;
        }
        CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_compacted_size)));
    } else {
        m_aabb_gas_buffer = d_output;
    }

    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_temp)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_aabbs)));
}

void UnifiedTracer::freeQueryGAS()
{
    if (m_aabb_gas_buffer) {
        cudaFree(reinterpret_cast<void*>(m_aabb_gas_buffer));
        m_aabb_gas_buffer = 0;
        m_aabb_gas_handle = 0;
    }
}

/* ===========================================================================
 * Ray Tracing — dispatch via RT SBT (single-hit)
 * =========================================================================*/

HitResult UnifiedTracer::traceSingle(const Ray& ray)
{
    ensureSingleBuffers();
    m_single_ray_buf.upload(&ray, 1);

    UnifiedParams lp = {};
    lp.handle = activeRTHandle();
    lp.count  = 1;
    lp.rays   = m_single_ray_buf.get();
    lp.hits   = m_single_hit_buf.get();

    CudaBuffer<UnifiedParams> d_params;
    d_params.alloc(1);
    d_params.upload(&lp, 1);

    OPTIX_CHECK(optixLaunch(
        m_pipeline, 0, d_params.devicePtr(), sizeof(UnifiedParams),
        &m_sbt_rt, 1, 1, 1));
    CUDA_SYNC_CHECK();

    HitResult result;
    m_single_hit_buf.download(&result, 1);
    return result;
}

void UnifiedTracer::traceBatch(
    Ray* d_rays, HitResult* d_hits,
    unsigned int count, CUstream stream)
{
    UnifiedParams lp = {};
    lp.handle = activeRTHandle();
    lp.count  = count;
    lp.rays   = d_rays;
    lp.hits   = d_hits;

    CUdeviceptr d_params;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_params), sizeof(UnifiedParams)));
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(d_params),
                               &lp, sizeof(UnifiedParams),
                               cudaMemcpyHostToDevice, stream));

    unsigned int w, h;
    if (count <= 65536) { w = count; h = 1; }
    else { w = 8192; h = (count + w - 1) / w; }

    OPTIX_CHECK(optixLaunch(m_pipeline, stream, d_params, sizeof(UnifiedParams),
                            &m_sbt_rt, w, h, 1));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_params)));
}

TraceResult UnifiedTracer::traceBatchTimed(
    Ray* d_rays, HitResult* d_hits,
    unsigned int count, int num_iters, int warm_up, CUstream stream)
{
    for (int i = 0; i < warm_up; ++i)
        traceBatch(d_rays, d_hits, count, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

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

    std::sort(times.begin(), times.end());
    float median_ms = times[num_iters / 2];

    CudaBuffer<unsigned int> d_hit_count;
    d_hit_count.alloc(1);
    countHitsDevice(d_hits, count, d_hit_count.get(), stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));
    unsigned int hit_count = 0;
    d_hit_count.download(&hit_count, 1);

    TraceResult r = {};
    r.num_rays      = count;
    r.trace_time_ms = median_ms;
    r.total_time_ms = median_ms;
    r.mrays_per_sec = (double)count / (median_ms * 1000.0);
    r.grays_per_sec = r.mrays_per_sec / 1000.0;
    r.num_hits      = hit_count;
    r.hit_rate      = (float)hit_count / (float)count;
    return r;
}

std::vector<HitResult> UnifiedTracer::traceBatch(const std::vector<Ray>& rays)
{
    unsigned int count = static_cast<unsigned int>(rays.size());
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

/* ===========================================================================
 * Multi-Hit Queries (E3) — dispatch via MH SBT
 * =========================================================================*/

void UnifiedTracer::traceBatchMultiHit(
    Ray* d_rays, MultiHitResult* d_multi_hits,
    unsigned int count, CUstream stream)
{
    UnifiedParams lp = {};
    lp.handle     = activeRTHandle();
    lp.count      = count;
    lp.rays       = d_rays;
    lp.multi_hits = d_multi_hits;

    CUdeviceptr d_params;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_params), sizeof(UnifiedParams)));
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(d_params),
                               &lp, sizeof(UnifiedParams),
                               cudaMemcpyHostToDevice, stream));

    unsigned int w, h;
    if (count <= 65536) { w = count; h = 1; }
    else { w = 8192; h = (count + w - 1) / w; }

    OPTIX_CHECK(optixLaunch(m_pipeline, stream, d_params, sizeof(UnifiedParams),
                            &m_sbt_mh, w, h, 1));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_params)));
}

std::vector<MultiHitResult> UnifiedTracer::traceBatchMultiHit(const std::vector<Ray>& rays)
{
    unsigned int count = static_cast<unsigned int>(rays.size());
    CudaBuffer<Ray>            d_rays;
    CudaBuffer<MultiHitResult> d_mh;
    d_rays.alloc(count);
    d_mh.alloc(count);
    d_rays.upload(rays);

    traceBatchMultiHit(d_rays.get(), d_mh.get(), count);
    CUDA_SYNC_CHECK();

    std::vector<MultiHitResult> results;
    d_mh.download(results);
    return results;
}

/* ===========================================================================
 * Closest-Point Queries — dispatch via NN SBT (simple NNResult)
 * =========================================================================*/

NNResult UnifiedTracer::querySingle(const float3& query_point)
{
    ensureSingleBuffers();
    m_single_query_buf.upload(&query_point, 1);

    UnifiedParams lp = {};
    lp.handle      = m_aabb_gas_handle;
    lp.count       = 1;
    lp.queries     = m_single_query_buf.get();
    lp.nn_vertices = m_d_nn_vertices.get();
    lp.nn_indices  = m_d_nn_indices.get();
    lp.results     = m_single_result_buf.get();

    CudaBuffer<UnifiedParams> d_params;
    d_params.alloc(1);
    d_params.upload(&lp, 1);

    OPTIX_CHECK(optixLaunch(
        m_pipeline, 0, d_params.devicePtr(), sizeof(UnifiedParams),
        &m_sbt_nn, 1, 1, 1));
    CUDA_SYNC_CHECK();

    NNResult result;
    m_single_result_buf.download(&result, 1);
    return result;
}

void UnifiedTracer::queryBatch(
    float3* d_queries, NNResult* d_results,
    unsigned int count, CUstream stream)
{
    UnifiedParams lp = {};
    lp.handle      = m_aabb_gas_handle;
    lp.count       = count;
    lp.queries     = d_queries;
    lp.nn_vertices = m_d_nn_vertices.get();
    lp.nn_indices  = m_d_nn_indices.get();
    lp.results     = d_results;

    CUdeviceptr d_params;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_params), sizeof(UnifiedParams)));
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(d_params),
                               &lp, sizeof(UnifiedParams),
                               cudaMemcpyHostToDevice, stream));

    unsigned int w, h;
    if (count <= 65536) { w = count; h = 1; }
    else { w = 8192; h = (count + w - 1) / w; }

    OPTIX_CHECK(optixLaunch(m_pipeline, stream, d_params, sizeof(UnifiedParams),
                            &m_sbt_nn, w, h, 1));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_params)));
}

NNQueryResult UnifiedTracer::queryBatchTimed(
    float3* d_queries, NNResult* d_results,
    unsigned int count, int num_iters, int warm_up, CUstream stream)
{
    for (int i = 0; i < warm_up; ++i)
        queryBatch(d_queries, d_results, count, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

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

    std::sort(times.begin(), times.end());
    float median_ms = times[num_iters / 2];

    CudaBuffer<unsigned int> d_hit_count;
    d_hit_count.alloc(1);
    countNNHitsDevice(d_results, count, d_hit_count.get(), stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));
    unsigned int hit_count = 0;
    d_hit_count.download(&hit_count, 1);

    NNQueryResult r = {};
    r.num_queries      = count;
    r.query_time_ms    = median_ms;
    r.total_time_ms    = median_ms;
    r.mqueries_per_sec = (double)count / (median_ms * 1000.0);
    r.gqueries_per_sec = r.mqueries_per_sec / 1000.0;
    r.num_found        = hit_count;
    r.found_rate       = (float)hit_count / (float)count;
    return r;
}

std::vector<NNResult> UnifiedTracer::queryBatch(const std::vector<float3>& queries)
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
 * Enhanced Closest-Point Queries (E2) — dispatch via CP SBT
 * =========================================================================*/

void UnifiedTracer::closestPointBatch(
    CPQuery* d_queries, CPResult* d_results,
    unsigned int count, CUstream stream)
{
    UnifiedParams lp = {};
    lp.handle      = m_aabb_gas_handle;
    lp.count       = count;
    lp.cp_queries  = d_queries;
    lp.cp_results  = d_results;
    lp.nn_vertices = m_d_nn_vertices.get();
    lp.nn_indices  = m_d_nn_indices.get();

    CUdeviceptr d_params;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_params), sizeof(UnifiedParams)));
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(d_params),
                               &lp, sizeof(UnifiedParams),
                               cudaMemcpyHostToDevice, stream));

    unsigned int w, h;
    if (count <= 65536) { w = count; h = 1; }
    else { w = 8192; h = (count + w - 1) / w; }

    OPTIX_CHECK(optixLaunch(m_pipeline, stream, d_params, sizeof(UnifiedParams),
                            &m_sbt_cp, w, h, 1));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_params)));
}

std::vector<CPResult> UnifiedTracer::closestPointBatch(const std::vector<CPQuery>& queries)
{
    unsigned int count = static_cast<unsigned int>(queries.size());
    CudaBuffer<CPQuery>  d_queries;
    CudaBuffer<CPResult> d_results;
    d_queries.alloc(count);
    d_results.alloc(count);
    d_queries.upload(queries);

    closestPointBatch(d_queries.get(), d_results.get(), count);
    CUDA_SYNC_CHECK();

    std::vector<CPResult> results;
    d_results.download(results);
    return results;
}

/* ===========================================================================
 * setGeometryFlipNormal (E7) — update flip flag and request SBT rebuild
 * =========================================================================*/

void UnifiedTracer::setGeometryFlipNormal(unsigned int geom_id, bool flip)
{
    auto it = m_geometries.find(geom_id);
    if (it != m_geometries.end()) {
        it->second.flip_normal = flip;
    }
}

/* ===========================================================================
 * getQueryPrimRangeTable — prim_idx → geom_id mapping for CP results
 * =========================================================================*/

std::vector<UnifiedTracer::PrimRange> UnifiedTracer::getQueryPrimRangeTable() const
{
    return m_query_prim_ranges;
}

/* ===========================================================================
 * Enclosure Queries (E5) — combined CP + ray side detection
 *
 * Algorithm (matching cus3d):
 *  1. Find nearest surface primitive via closestPointBatch
 *  2. For each query, test which side by shooting a ray from the query
 *     point along an axis direction. side = dot(ray_dir, hit_normal) < 0
 *     → front (0), > 0 → back (1), degenerate → -1
 * =========================================================================*/

void UnifiedTracer::findEnclosureBatch(
    EnclosureQuery* d_queries, EnclosureResult* d_results,
    unsigned int count, CUstream stream)
{
    /* Step 1: Run closest-point queries to find nearest primitive */
    CudaBuffer<CPQuery>  d_cp_queries;
    CudaBuffer<CPResult> d_cp_results;
    d_cp_queries.alloc(count);
    d_cp_results.alloc(count);

    /* Convert EnclosureQuery to CPQuery on host, then upload */
    std::vector<EnclosureQuery> h_enc_queries(count);
    CUDA_CHECK(cudaMemcpy(h_enc_queries.data(), d_queries,
                          count * sizeof(EnclosureQuery), cudaMemcpyDeviceToHost));

    std::vector<CPQuery> h_cp_queries(count);
    for (unsigned int i = 0; i < count; ++i) {
        h_cp_queries[i].position = h_enc_queries[i].position;
        h_cp_queries[i].radius   = 1e30f;  /* unlimited search radius */
    }
    d_cp_queries.upload(h_cp_queries);

    /* Run CP query */
    closestPointBatch(d_cp_queries.get(), d_cp_results.get(), count, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    /* Download CP results */
    std::vector<CPResult> h_cp_results(count);
    d_cp_results.download(h_cp_results.data(), count);

    /* Step 2: For each query, cast a ray along +X to determine side */
    CudaBuffer<Ray>       d_rays;
    CudaBuffer<HitResult> d_hits;
    d_rays.alloc(count);
    d_hits.alloc(count);

    std::vector<Ray> h_rays(count);
    for (unsigned int i = 0; i < count; ++i) {
        h_rays[i].origin    = h_enc_queries[i].position;
        h_rays[i].direction = make_float3(1.0f, 0.0f, 0.0f);
        h_rays[i].tmin      = 1e-6f;
        h_rays[i].tmax      = 1e30f;
    }
    d_rays.upload(h_rays);

    /* Trace rays through RT scene */
    traceBatch(d_rays.get(), d_hits.get(), count, stream);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    std::vector<HitResult> h_hits(count);
    d_hits.download(h_hits.data(), count);

    /* Step 3: Build enclosure results */
    std::vector<EnclosureResult> h_enc_results(count);
    const float DEGENERATE_THRESHOLD = 1e-8f;

    for (unsigned int i = 0; i < count; ++i) {
        EnclosureResult& er = h_enc_results[i];

        if (h_cp_results[i].distance < 0.0f) {
            /* No surface found */
            er.prim_idx = -1;
            er.distance = -1.0f;
            er.side     = -1;
            er.geom_id  = 0xFFFFFFFFu;
            er.inst_id  = 0xFFFFFFFFu;
            continue;
        }

        er.prim_idx = static_cast<int>(h_cp_results[i].prim_idx);
        er.distance = h_cp_results[i].distance;

        /* Degenerate: too close to surface */
        if (er.distance < DEGENERATE_THRESHOLD) {
            er.side    = -1;
            er.geom_id = h_cp_results[i].geom_id;
            er.inst_id = h_cp_results[i].inst_id;
            continue;
        }

        /* Determine side from ray hit */
        if (h_hits[i].t < 0.0f) {
            /* Ray missed — outside */
            er.side = 0;
        } else {
            /* dot(ray_dir, hit_normal) — if negative, we hit front face (outside) */
            float dot_dn = h_hits[i].normal[0]; /* ray_dir = (1,0,0) */
            er.side = (dot_dn < 0.0f) ? 0 : 1;
        }
        er.geom_id = h_cp_results[i].geom_id;
        er.inst_id = h_cp_results[i].inst_id;
    }

    /* Upload results */
    CUDA_CHECK(cudaMemcpy(d_results, h_enc_results.data(),
                          count * sizeof(EnclosureResult), cudaMemcpyHostToDevice));
}

std::vector<EnclosureResult> UnifiedTracer::findEnclosureBatch(
    const std::vector<EnclosureQuery>& queries)
{
    unsigned int count = static_cast<unsigned int>(queries.size());
    CudaBuffer<EnclosureQuery>  d_queries;
    CudaBuffer<EnclosureResult> d_results;
    d_queries.alloc(count);
    d_results.alloc(count);
    d_queries.upload(queries);

    findEnclosureBatch(d_queries.get(), d_results.get(), count);
    CUDA_SYNC_CHECK();

    std::vector<EnclosureResult> results;
    d_results.download(results);
    return results;
}

/* ===========================================================================
 * Cleanup — release all GPU resources in reverse creation order
 * =========================================================================*/

void UnifiedTracer::cleanup()
{
    /* RT SBT device memory */
    if (m_d_rt_raygen_record)    { cudaFree(reinterpret_cast<void*>(m_d_rt_raygen_record));    m_d_rt_raygen_record    = 0; }
    if (m_d_rt_miss_record)      { cudaFree(reinterpret_cast<void*>(m_d_rt_miss_record));      m_d_rt_miss_record      = 0; }
    if (m_d_rt_hitgroup_records) { cudaFree(reinterpret_cast<void*>(m_d_rt_hitgroup_records)); m_d_rt_hitgroup_records = 0; }

    /* MH SBT device memory */
    if (m_d_mh_raygen_record)    { cudaFree(reinterpret_cast<void*>(m_d_mh_raygen_record));    m_d_mh_raygen_record    = 0; }
    if (m_d_mh_miss_record)      { cudaFree(reinterpret_cast<void*>(m_d_mh_miss_record));      m_d_mh_miss_record      = 0; }
    if (m_d_mh_hitgroup_records) { cudaFree(reinterpret_cast<void*>(m_d_mh_hitgroup_records)); m_d_mh_hitgroup_records = 0; }

    /* NN SBT device memory */
    if (m_d_nn_raygen_record)   { cudaFree(reinterpret_cast<void*>(m_d_nn_raygen_record));   m_d_nn_raygen_record   = 0; }
    if (m_d_nn_miss_record)     { cudaFree(reinterpret_cast<void*>(m_d_nn_miss_record));     m_d_nn_miss_record     = 0; }
    if (m_d_nn_hitgroup_record) { cudaFree(reinterpret_cast<void*>(m_d_nn_hitgroup_record)); m_d_nn_hitgroup_record = 0; }

    /* CP SBT device memory */
    if (m_d_cp_raygen_record)   { cudaFree(reinterpret_cast<void*>(m_d_cp_raygen_record));   m_d_cp_raygen_record = 0; }

    /* ENC SBT device memory (E5) */
    if (m_d_enc_raygen_record)  { cudaFree(reinterpret_cast<void*>(m_d_enc_raygen_record));  m_d_enc_raygen_record = 0; }

    /* Pipeline */
    if (m_pipeline)                 { optixPipelineDestroy(m_pipeline);                             m_pipeline                 = nullptr; }
    if (m_hitgroup_aabb_sphere_pg)  { optixProgramGroupDestroy(m_hitgroup_aabb_sphere_pg);          m_hitgroup_aabb_sphere_pg  = nullptr; }
    if (m_hitgroup_aabb_pg)         { optixProgramGroupDestroy(m_hitgroup_aabb_pg);                 m_hitgroup_aabb_pg         = nullptr; }
    if (m_hitgroup_sphere_mh_pg)    { optixProgramGroupDestroy(m_hitgroup_sphere_mh_pg);            m_hitgroup_sphere_mh_pg    = nullptr; }
    if (m_hitgroup_tri_mh_pg)       { optixProgramGroupDestroy(m_hitgroup_tri_mh_pg);               m_hitgroup_tri_mh_pg       = nullptr; }
    if (m_hitgroup_sphere_pg)       { optixProgramGroupDestroy(m_hitgroup_sphere_pg);               m_hitgroup_sphere_pg       = nullptr; }
    if (m_hitgroup_tri_pg)          { optixProgramGroupDestroy(m_hitgroup_tri_pg);                  m_hitgroup_tri_pg          = nullptr; }
    if (m_miss_nn_pg)               { optixProgramGroupDestroy(m_miss_nn_pg);                       m_miss_nn_pg               = nullptr; }
    if (m_miss_rt_pg)               { optixProgramGroupDestroy(m_miss_rt_pg);                       m_miss_rt_pg               = nullptr; }
    if (m_raygen_cp_pg)             { optixProgramGroupDestroy(m_raygen_cp_pg);                     m_raygen_cp_pg             = nullptr; }
    if (m_raygen_nn_pg)             { optixProgramGroupDestroy(m_raygen_nn_pg);                     m_raygen_nn_pg             = nullptr; }
    if (m_raygen_mh_pg)             { optixProgramGroupDestroy(m_raygen_mh_pg);                     m_raygen_mh_pg             = nullptr; }
    if (m_raygen_rt_pg)             { optixProgramGroupDestroy(m_raygen_rt_pg);                     m_raygen_rt_pg             = nullptr; }
    if (m_nn_module)                { optixModuleDestroy(m_nn_module);                              m_nn_module                = nullptr; }
    if (m_rt_module)                { optixModuleDestroy(m_rt_module);                              m_rt_module                = nullptr; }

    /* Geometry and acceleration structures */
    for (auto& kv : m_geometries)
        freeGeometryGAS(kv.second);
    m_geometries.clear();
    freeIAS();
    freeQueryGAS();
    m_d_nn_vertices.free();
    m_d_nn_indices.free();

    m_host_nn_vertices.clear();
    m_host_nn_indices.clear();
    m_num_query_tris = 0;
}
