/*
 * accel_manager.h - OptiX acceleration structure builder
 * Supports GAS (geometry), IAS (instance), and optional compaction.
 */
#pragma once

#include <optix.h>
#include <cuda_runtime.h>
#include "geometry_manager.h"

#include <vector>

/* ---- Built acceleration structure result ---- */
struct AccelStructure {
    OptixTraversableHandle handle      = 0;
    CUdeviceptr            buffer      = 0;
    size_t                 buffer_size = 0;
};

/* ---- Instance description for IAS ---- */
struct InstanceDesc {
    OptixTraversableHandle gas_handle;
    float                  transform[12];  /* 3x4 row-major */
    unsigned int           instance_id;
    unsigned int           sbt_offset;
    unsigned int           visibility_mask;
};

class AccelManager {
public:
    AccelManager()  = default;
    ~AccelManager();

    /*
     * Build a Geometry Acceleration Structure (GAS) from a triangle mesh.
     * Supports optional compaction for reduced memory usage.
     *
     * @param context    OptiX device context
     * @param mesh       Triangle mesh data
     * @param compact    Enable compaction (recommended for static geometry)
     * @return           AccelStructure with traversable handle
     */
    AccelStructure buildGAS(
        OptixDeviceContext  context,
        const TriangleMesh& mesh,
        bool                compact = true);

    /*
     * Build an Instance Acceleration Structure (IAS) from instance descriptions.
     *
     * @param context    OptiX device context
     * @param instances  Instance descriptions
     * @return           AccelStructure with traversable handle
     */
    AccelStructure buildIAS(
        OptixDeviceContext            context,
        const std::vector<InstanceDesc>& instances);

    /* Release all managed GPU buffers */
    void cleanup();

private:
    /* Track allocated buffers for cleanup */
    std::vector<CUdeviceptr> m_allocated_buffers;

    void trackBuffer(CUdeviceptr buf) { m_allocated_buffers.push_back(buf); }
};
