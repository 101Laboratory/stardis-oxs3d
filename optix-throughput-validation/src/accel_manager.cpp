/*
 * accel_manager.cpp - OptiX acceleration structure builder implementation
 */

#include "accel_manager.h"
#include "optix_check.h"

#include <cstring>
#include <iostream>

/* ===========================================================================
 * AccelManager destructor
 * =========================================================================*/
AccelManager::~AccelManager()
{
    cleanup();
}

/* ===========================================================================
 * AccelManager::cleanup
 * =========================================================================*/
void AccelManager::cleanup()
{
    for (auto buf : m_allocated_buffers) {
        if (buf)
            cudaFree(reinterpret_cast<void*>(buf));
    }
    m_allocated_buffers.clear();
}

/* ===========================================================================
 * AccelManager::buildGAS
 * Build Geometry Acceleration Structure with optional compaction.
 * =========================================================================*/
AccelStructure AccelManager::buildGAS(
    OptixDeviceContext  context,
    const TriangleMesh& mesh,
    bool                compact)
{
    AccelStructure result;

    /* 1. Upload vertex and index data to GPU */
    CUdeviceptr d_vertices = 0;
    size_t      vertices_size = mesh.vertices.size() * sizeof(float3);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_vertices), vertices_size));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(d_vertices),
                          mesh.vertices.data(), vertices_size,
                          cudaMemcpyHostToDevice));

    CUdeviceptr d_indices = 0;
    size_t      indices_size = mesh.indices.size() * sizeof(uint3);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_indices), indices_size));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(d_indices),
                          mesh.indices.data(), indices_size,
                          cudaMemcpyHostToDevice));

    /* 2. Configure build input */
    const uint32_t triangle_flags[1] = { OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT };

    OptixBuildInput build_input                          = {};
    build_input.type                                     = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    build_input.triangleArray.vertexFormat               = OPTIX_VERTEX_FORMAT_FLOAT3;
    build_input.triangleArray.numVertices                = static_cast<uint32_t>(mesh.vertices.size());
    build_input.triangleArray.vertexBuffers              = &d_vertices;
    build_input.triangleArray.indexFormat                = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    build_input.triangleArray.numIndexTriplets           = static_cast<uint32_t>(mesh.indices.size());
    build_input.triangleArray.indexBuffer                = d_indices;
    build_input.triangleArray.flags                      = triangle_flags;
    build_input.triangleArray.numSbtRecords              = 1;

    /* 3. Configure build options */
    OptixAccelBuildOptions build_options = {};
    build_options.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION
                             | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    build_options.operation  = OPTIX_BUILD_OPERATION_BUILD;

    if (!compact) {
        build_options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    }

    /* 4. Compute memory requirements */
    OptixAccelBufferSizes buffer_sizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(
        context,
        &build_options,
        &build_input,
        1,
        &buffer_sizes));

    /* 5. Allocate temp and output buffers */
    CUdeviceptr d_temp_buffer = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_temp_buffer),
                          buffer_sizes.tempSizeInBytes));

    CUdeviceptr d_output_buffer = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_output_buffer),
                          buffer_sizes.outputSizeInBytes));

    /* 6. Build with optional compaction property */
    CUdeviceptr d_compacted_size = 0;
    OptixAccelEmitDesc emit_desc = {};

    if (compact) {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted_size),
                              sizeof(size_t)));
        emit_desc.type   = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emit_desc.result = d_compacted_size;
    }

    OPTIX_CHECK(optixAccelBuild(
        context,
        0,                  /* CUDA stream */
        &build_options,
        &build_input,
        1,                  /* num build inputs */
        d_temp_buffer,
        buffer_sizes.tempSizeInBytes,
        d_output_buffer,
        buffer_sizes.outputSizeInBytes,
        &result.handle,
        compact ? &emit_desc : nullptr,
        compact ? 1 : 0));

    CUDA_CHECK(cudaDeviceSynchronize());

    /* 7. Compact if requested */
    if (compact) {
        size_t compacted_size = 0;
        CUDA_CHECK(cudaMemcpy(&compacted_size, reinterpret_cast<void*>(d_compacted_size),
                              sizeof(size_t), cudaMemcpyDeviceToHost));

        if (compacted_size < buffer_sizes.outputSizeInBytes) {
            CUdeviceptr d_compacted_buffer = 0;
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_compacted_buffer),
                                  compacted_size));

            OPTIX_CHECK(optixAccelCompact(
                context, 0, result.handle,
                d_compacted_buffer, compacted_size,
                &result.handle));

            CUDA_CHECK(cudaDeviceSynchronize());
            CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_output_buffer)));

            result.buffer      = d_compacted_buffer;
            result.buffer_size = compacted_size;
        } else {
            result.buffer      = d_output_buffer;
            result.buffer_size = buffer_sizes.outputSizeInBytes;
        }

        CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_compacted_size)));
    } else {
        result.buffer      = d_output_buffer;
        result.buffer_size = buffer_sizes.outputSizeInBytes;
    }

    /* 8. Free temporary resources */
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_temp_buffer)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_vertices)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_indices)));

    /* Track the output buffer for cleanup */
    trackBuffer(result.buffer);

    return result;
}

/* ===========================================================================
 * AccelManager::buildIAS
 * Build Instance Acceleration Structure.
 * =========================================================================*/
AccelStructure AccelManager::buildIAS(
    OptixDeviceContext               context,
    const std::vector<InstanceDesc>& instances)
{
    AccelStructure result;

    /* 1. Convert to OptixInstance array */
    std::vector<OptixInstance> optix_instances(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        OptixInstance& oi = optix_instances[i];
        memset(&oi, 0, sizeof(OptixInstance));

        memcpy(oi.transform, instances[i].transform, sizeof(float) * 12);
        oi.instanceId        = instances[i].instance_id;
        oi.sbtOffset         = instances[i].sbt_offset;
        oi.visibilityMask    = instances[i].visibility_mask;
        oi.traversableHandle = instances[i].gas_handle;
        oi.flags             = OPTIX_INSTANCE_FLAG_NONE;
    }

    /* 2. Upload instance data to GPU */
    CUdeviceptr d_instances = 0;
    size_t      instances_size = optix_instances.size() * sizeof(OptixInstance);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_instances), instances_size));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(d_instances),
                          optix_instances.data(), instances_size,
                          cudaMemcpyHostToDevice));

    /* 3. Configure build input */
    OptixBuildInput build_input          = {};
    build_input.type                     = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    build_input.instanceArray.instances  = d_instances;
    build_input.instanceArray.numInstances = static_cast<uint32_t>(instances.size());

    /* 4. Build options */
    OptixAccelBuildOptions build_options = {};
    build_options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    build_options.operation  = OPTIX_BUILD_OPERATION_BUILD;

    /* 5. Compute memory requirements */
    OptixAccelBufferSizes buffer_sizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(
        context,
        &build_options,
        &build_input,
        1,
        &buffer_sizes));

    /* 6. Allocate and build */
    CUdeviceptr d_temp_buffer = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_temp_buffer),
                          buffer_sizes.tempSizeInBytes));

    CUdeviceptr d_output_buffer = 0;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_output_buffer),
                          buffer_sizes.outputSizeInBytes));

    OPTIX_CHECK(optixAccelBuild(
        context,
        0,
        &build_options,
        &build_input,
        1,
        d_temp_buffer,
        buffer_sizes.tempSizeInBytes,
        d_output_buffer,
        buffer_sizes.outputSizeInBytes,
        &result.handle,
        nullptr, 0));

    CUDA_CHECK(cudaDeviceSynchronize());

    /* 7. Cleanup temp */
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_temp_buffer)));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_instances)));

    result.buffer      = d_output_buffer;
    result.buffer_size = buffer_sizes.outputSizeInBytes;
    trackBuffer(result.buffer);

    return result;
}
