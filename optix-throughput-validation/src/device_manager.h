/*
 * device_manager.h - CUDA device and OptiX context management
 */
#pragma once

#include <optix.h>
#include <cuda_runtime.h>
#include <string>

class DeviceManager {
public:
    DeviceManager()  = default;
    ~DeviceManager() = default;

    /* Initialize CUDA device and OptiX context.
     * @param device_id  CUDA device index (0 = default GPU)
     * @param enable_validation  Enable OptiX validation mode (slower but catches errors)
     */
    void init(int device_id = 0, bool enable_validation = false);

    /* Shut down and release all resources */
    void shutdown();

    /* Accessors */
    OptixDeviceContext getContext() const { return m_context; }
    int                getDeviceId() const { return m_device_id; }
    std::string        getDeviceName() const { return m_device_name; }
    int                getComputeCapability() const { return m_compute_capability; }
    size_t             getTotalMemory() const { return m_total_memory; }

    /* Print device info to stdout */
    void printDeviceInfo() const;

private:
    OptixDeviceContext m_context             = nullptr;
    int                m_device_id           = 0;
    std::string        m_device_name;
    int                m_compute_capability  = 0;
    size_t             m_total_memory        = 0;
};
