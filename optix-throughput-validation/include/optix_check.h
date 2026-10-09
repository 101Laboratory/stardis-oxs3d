/*
 * optix_check.h - CUDA and OptiX error checking macros
 * Standalone implementation (no sutil dependency)
 */
#pragma once

#include <cuda_runtime.h>
#include <optix.h>
#include <optix_stubs.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

//=============================================================================
// CUDA Error Checking
//=============================================================================

#define CUDA_CHECK(call)                                                      \
    do {                                                                       \
        cudaError_t rc = (call);                                               \
        if (rc != cudaSuccess) {                                               \
            std::ostringstream ss;                                             \
            ss << "CUDA call '" << #call << "' failed: "                       \
               << cudaGetErrorString(rc)                                       \
               << " (" << __FILE__ << ":" << __LINE__ << ")";                  \
            throw std::runtime_error(ss.str());                                \
        }                                                                      \
    } while (0)

#define CUDA_SYNC_CHECK()                                                     \
    do {                                                                       \
        cudaDeviceSynchronize();                                               \
        cudaError_t rc = cudaGetLastError();                                   \
        if (rc != cudaSuccess) {                                               \
            std::ostringstream ss;                                             \
            ss << "CUDA sync error: "                                          \
               << cudaGetErrorString(rc)                                       \
               << " (" << __FILE__ << ":" << __LINE__ << ")";                  \
            throw std::runtime_error(ss.str());                                \
        }                                                                      \
    } while (0)

//=============================================================================
// OptiX Error Checking
//=============================================================================

#define OPTIX_CHECK(call)                                                     \
    do {                                                                       \
        OptixResult rc = (call);                                               \
        if (rc != OPTIX_SUCCESS) {                                             \
            std::ostringstream ss;                                             \
            ss << "OptiX call '" << #call << "' failed: "                      \
               << optixGetErrorName(rc) << " - "                               \
               << optixGetErrorString(rc)                                      \
               << " (" << __FILE__ << ":" << __LINE__ << ")";                  \
            throw std::runtime_error(ss.str());                                \
        }                                                                      \
    } while (0)

// OptiX check with log output
static char s_optix_log[4096];
static size_t s_optix_log_size = sizeof(s_optix_log);

#define OPTIX_CHECK_LOG(call)                                                 \
    do {                                                                       \
        s_optix_log_size = sizeof(s_optix_log);                                \
        OptixResult rc = (call);                                               \
        if (rc != OPTIX_SUCCESS) {                                             \
            std::ostringstream ss;                                             \
            ss << "OptiX call '" << #call << "' failed: "                      \
               << optixGetErrorName(rc) << " - "                               \
               << optixGetErrorString(rc);                                     \
            if (s_optix_log_size > 1)                                          \
                ss << "\nLog: " << s_optix_log;                                \
            ss << " (" << __FILE__ << ":" << __LINE__ << ")";                  \
            throw std::runtime_error(ss.str());                                \
        }                                                                      \
        if (s_optix_log_size > 1) {                                            \
            std::cerr << "[OptiX Log]: " << s_optix_log << std::endl;          \
        }                                                                      \
    } while (0)
