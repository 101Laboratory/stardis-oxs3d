/*
 * pipeline_manager.cpp - OptiX pipeline management implementation
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pipeline_manager.h"
#include "launch_params.h"
#include "optix_check.h"

#include <algorithm>
#include <optix_stack_size.h>

#include <cstring>
#include <iostream>

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

/* ===========================================================================
 * PipelineManager destructor
 * =========================================================================*/
PipelineManager::~PipelineManager()
{
    cleanup();
}

/* ===========================================================================
 * PipelineManager::create
 * =========================================================================*/
void PipelineManager::create(
    OptixDeviceContext  context,
    const std::string&  ptx_source,
    int  num_payload_regs,
    int  num_attrib_regs,
    int  max_trace_depth,
    bool use_instancing)
{
    m_context          = context;
    m_num_payload_regs = num_payload_regs;
    m_num_attrib_regs  = num_attrib_regs;
    m_use_instancing   = use_instancing;

    createModule(context, ptx_source);
    createProgramGroups(context);
    createPipelineObject(context, max_trace_depth);
    createSBT();
}

/* ===========================================================================
 * PipelineManager::createModule
 * =========================================================================*/
void PipelineManager::createModule(
    OptixDeviceContext context,
    const std::string& ptx_source)
{
    /* Module compile options */
    m_module_compile_options = {};
    m_module_compile_options.optLevel   = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    m_module_compile_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

    /* Pipeline compile options */
    m_pipeline_compile_options = {};
    m_pipeline_compile_options.usesMotionBlur  = false;
    m_pipeline_compile_options.numPayloadValues    = m_num_payload_regs;
    m_pipeline_compile_options.numAttributeValues  = m_num_attrib_regs;
    m_pipeline_compile_options.exceptionFlags      = OPTIX_EXCEPTION_FLAG_NONE;
    m_pipeline_compile_options.pipelineLaunchParamsVariableName = "params";
    m_pipeline_compile_options.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

    if (m_use_instancing) {
        m_pipeline_compile_options.traversableGraphFlags =
            OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    } else {
        m_pipeline_compile_options.traversableGraphFlags =
            OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    }

    /* Create module from PTX */
    OPTIX_CHECK_LOG(optixModuleCreate(
        context,
        &m_module_compile_options,
        &m_pipeline_compile_options,
        ptx_source.c_str(),
        ptx_source.size(),
        s_optix_log, &s_optix_log_size,
        &m_module));

    std::cout << "  [Pipeline] Module created successfully.\n";
}

/* ===========================================================================
 * PipelineManager::createProgramGroups
 * =========================================================================*/
void PipelineManager::createProgramGroups(OptixDeviceContext context)
{
    OptixProgramGroupOptions pg_options = {};

    /* Raygen */
    OptixProgramGroupDesc raygen_desc    = {};
    raygen_desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygen_desc.raygen.module            = m_module;
    raygen_desc.raygen.entryFunctionName = "__raygen__rg";

    OPTIX_CHECK_LOG(optixProgramGroupCreate(
        context, &raygen_desc, 1, &pg_options,
        s_optix_log, &s_optix_log_size, &m_raygen_pg));

    /* Miss */
    OptixProgramGroupDesc miss_desc    = {};
    miss_desc.kind                     = OPTIX_PROGRAM_GROUP_KIND_MISS;
    miss_desc.miss.module              = m_module;
    miss_desc.miss.entryFunctionName   = "__miss__ms";

    OPTIX_CHECK_LOG(optixProgramGroupCreate(
        context, &miss_desc, 1, &pg_options,
        s_optix_log, &s_optix_log_size, &m_miss_pg));

    /* Hit Group (closest hit only, no any-hit for throughput) */
    OptixProgramGroupDesc hitgroup_desc        = {};
    hitgroup_desc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitgroup_desc.hitgroup.moduleCH            = m_module;
    hitgroup_desc.hitgroup.entryFunctionNameCH = "__closesthit__ch";

    OPTIX_CHECK_LOG(optixProgramGroupCreate(
        context, &hitgroup_desc, 1, &pg_options,
        s_optix_log, &s_optix_log_size, &m_hitgroup_pg));

    std::cout << "  [Pipeline] Program groups created (raygen, miss, closesthit).\n";
}

/* ===========================================================================
 * PipelineManager::createPipelineObject
 * =========================================================================*/
void PipelineManager::createPipelineObject(
    OptixDeviceContext context,
    int max_trace_depth)
{
    OptixProgramGroup program_groups[] = {
        m_raygen_pg, m_miss_pg, m_hitgroup_pg
    };

    OptixPipelineLinkOptions link_options = {};
    link_options.maxTraceDepth            = max_trace_depth;

    OPTIX_CHECK_LOG(optixPipelineCreate(
        context,
        &m_pipeline_compile_options,
        &link_options,
        program_groups,
        sizeof(program_groups) / sizeof(program_groups[0]),
        s_optix_log, &s_optix_log_size,
        &m_pipeline));

    /* Compute and set stack sizes */
    OptixStackSizes stack_sizes = {};
    for (auto& pg : program_groups) {
        OPTIX_CHECK(optixUtilAccumulateStackSizes(pg, &stack_sizes, m_pipeline));
    }

    uint32_t dc_from_traversal, dc_from_state, continuation;
    OPTIX_CHECK(optixUtilComputeStackSizes(
        &stack_sizes,
        max_trace_depth,
        0,  /* maxCCDepth */
        0,  /* maxDCDepth */
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

    std::cout << "  [Pipeline] Pipeline created and stack sizes configured.\n";
}

/* ===========================================================================
 * PipelineManager::createSBT
 * =========================================================================*/
void PipelineManager::createSBT()
{
    /* Raygen record */
    {
        RaygenRecord rg = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_raygen_pg, &rg));

        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_raygen_record),
                              sizeof(RaygenRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_raygen_record),
                              &rg, sizeof(RaygenRecord),
                              cudaMemcpyHostToDevice));
    }

    /* Miss record */
    {
        MissRecord ms = {};
        OPTIX_CHECK(optixSbtRecordPackHeader(m_miss_pg, &ms));

        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_miss_record),
                              sizeof(MissRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_miss_record),
                              &ms, sizeof(MissRecord),
                              cudaMemcpyHostToDevice));
    }

    /* Hit group record */
    {
        HitGroupRecord hg = {};
        memset(&hg.data, 0, sizeof(HitGroupData));
        OPTIX_CHECK(optixSbtRecordPackHeader(m_hitgroup_pg, &hg));

        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_d_hitgroup_record),
                              sizeof(HitGroupRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(m_d_hitgroup_record),
                              &hg, sizeof(HitGroupRecord),
                              cudaMemcpyHostToDevice));
    }

    /* Assemble SBT */
    m_sbt = {};
    m_sbt.raygenRecord                = m_d_raygen_record;
    m_sbt.missRecordBase              = m_d_miss_record;
    m_sbt.missRecordStrideInBytes     = sizeof(MissRecord);
    m_sbt.missRecordCount             = RAY_TYPE_COUNT;
    m_sbt.hitgroupRecordBase          = m_d_hitgroup_record;
    m_sbt.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
    m_sbt.hitgroupRecordCount         = 1;

    std::cout << "  [Pipeline] SBT created.\n";
}

/* ===========================================================================
 * PipelineManager::cleanup
 * =========================================================================*/
void PipelineManager::cleanup()
{
    if (m_d_raygen_record)   { cudaFree(reinterpret_cast<void*>(m_d_raygen_record));   m_d_raygen_record   = 0; }
    if (m_d_miss_record)     { cudaFree(reinterpret_cast<void*>(m_d_miss_record));     m_d_miss_record     = 0; }
    if (m_d_hitgroup_record) { cudaFree(reinterpret_cast<void*>(m_d_hitgroup_record)); m_d_hitgroup_record = 0; }

    if (m_pipeline)    { optixPipelineDestroy(m_pipeline);       m_pipeline    = nullptr; }
    if (m_hitgroup_pg) { optixProgramGroupDestroy(m_hitgroup_pg); m_hitgroup_pg = nullptr; }
    if (m_miss_pg)     { optixProgramGroupDestroy(m_miss_pg);     m_miss_pg     = nullptr; }
    if (m_raygen_pg)   { optixProgramGroupDestroy(m_raygen_pg);   m_raygen_pg   = nullptr; }
    if (m_module)      { optixModuleDestroy(m_module);            m_module      = nullptr; }
}
