/*
 * device_manager.cpp - CUDA device and OptiX context management implementation
 */

#include "device_manager.h"
#include "optix_check.h"

#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <iostream>
#include <iomanip>

/* ---- OptiX context log callback ---- */
static void contextLogCallback(
    unsigned int level,
    const char*  tag,
    const char*  message,
    void*        /* cbdata */)
{
    std::cerr << "[OptiX][" << std::setw(2) << level << "]["
              << std::setw(12) << tag << "]: " << message << "\n";
}

/* ---- DeviceManager::init ---- */
void DeviceManager::init(int device_id, bool enable_validation)
{
    m_device_id = device_id;

    /* 1. Initialize CUDA */
    CUDA_CHECK(cudaSetDevice(device_id));
    CUDA_CHECK(cudaFree(0));   /* force context creation */

    /* Query device properties */
    cudaDeviceProp props;
    CUDA_CHECK(cudaGetDeviceProperties(&props, device_id));
    m_device_name         = props.name;
    m_compute_capability  = props.major * 10 + props.minor;
    m_total_memory        = props.totalGlobalMem;

    /* 2. Initialize the OptiX API (loads function table) */
    OPTIX_CHECK(optixInit());

    /* 3. Create OptiX device context */
    OptixDeviceContextOptions options = {};
    options.logCallbackFunction       = &contextLogCallback;
    options.logCallbackLevel          = 4;  /* 1-4, higher = more verbose */

    if (enable_validation) {
        options.validationMode = OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL;
    }

    CUcontext cuCtx = 0;  /* 0 = use current CUDA context */
    OPTIX_CHECK(optixDeviceContextCreate(cuCtx, &options, &m_context));
}

/* ---- DeviceManager::shutdown ---- */
void DeviceManager::shutdown()
{
    if (m_context) {
        OPTIX_CHECK(optixDeviceContextDestroy(m_context));
        m_context = nullptr;
    }
}

/* ---- DeviceManager::printDeviceInfo ---- */
void DeviceManager::printDeviceInfo() const
{
    double mem_gb = (double)m_total_memory / (1024.0 * 1024.0 * 1024.0);
    std::cout << "===== GPU Device Info =====\n"
              << "  Device:          " << m_device_name << "\n"
              << "  Compute Cap:     " << (m_compute_capability / 10)
              << "." << (m_compute_capability % 10) << "\n"
              << "  Global Memory:   " << std::fixed << std::setprecision(1)
              << mem_gb << " GB\n"
              << "  OptiX Version:   "
              << (OPTIX_VERSION / 10000) << "."
              << ((OPTIX_VERSION % 10000) / 100) << "."
              << (OPTIX_VERSION % 100) << "\n"
              << "===========================\n";
}
