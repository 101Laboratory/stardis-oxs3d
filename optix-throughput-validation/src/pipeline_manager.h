/*
 * pipeline_manager.h - OptiX pipeline (module, program groups, SBT) management
 */
#pragma once

#include <optix.h>
#include <cuda_runtime.h>
#include <string>

class PipelineManager {
public:
    PipelineManager()  = default;
    ~PipelineManager();

    /*
     * Create the complete OptiX pipeline from PTX source.
     *
     * @param context          OptiX device context
     * @param ptx_source       PTX string (loaded from file)
     * @param num_payload_regs Number of payload registers (4 for our programs)
     * @param num_attrib_regs  Number of attribute registers (2 for built-in triangles)
     * @param max_trace_depth  Maximum recursion depth (1 for non-recursive)
     * @param use_instancing   Whether the scene uses instancing (IAS)
     */
    void create(
        OptixDeviceContext context,
        const std::string& ptx_source,
        int  num_payload_regs = 4,
        int  num_attrib_regs  = 2,
        int  max_trace_depth  = 1,
        bool use_instancing   = false);

    /* Build the Shader Binding Table (SBT) */
    void createSBT();

    /* Destroy all resources */
    void cleanup();

    /* Accessors */
    OptixPipeline                    getPipeline() const { return m_pipeline; }
    const OptixShaderBindingTable&   getSBT()      const { return m_sbt; }
    const OptixPipelineCompileOptions& getPipelineCompileOptions() const {
        return m_pipeline_compile_options;
    }

private:
    void createModule(OptixDeviceContext context, const std::string& ptx_source);
    void createProgramGroups(OptixDeviceContext context);
    void createPipelineObject(OptixDeviceContext context, int max_trace_depth);

    OptixDeviceContext          m_context                  = nullptr;
    OptixModule                 m_module                   = nullptr;
    OptixPipeline               m_pipeline                 = nullptr;
    OptixProgramGroup           m_raygen_pg                = nullptr;
    OptixProgramGroup           m_miss_pg                  = nullptr;
    OptixProgramGroup           m_hitgroup_pg              = nullptr;

    OptixPipelineCompileOptions m_pipeline_compile_options = {};
    OptixModuleCompileOptions   m_module_compile_options   = {};

    OptixShaderBindingTable     m_sbt                      = {};

    /* SBT device memory */
    CUdeviceptr m_d_raygen_record   = 0;
    CUdeviceptr m_d_miss_record     = 0;
    CUdeviceptr m_d_hitgroup_record = 0;

    int  m_num_payload_regs = 4;
    int  m_num_attrib_regs  = 2;
    bool m_use_instancing   = false;
};
