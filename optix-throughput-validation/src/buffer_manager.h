/*
 * buffer_manager.h - RAII wrapper for CUDA device buffers
 * Header-only template for type-safe GPU memory management.
 */
#pragma once

#include <cuda_runtime.h>
#include "optix_check.h"

#include <vector>
#include <cstring>

template <typename T>
class CudaBuffer {
public:
    CudaBuffer() = default;

    ~CudaBuffer()
    {
        free();
    }

    /* Move semantics */
    CudaBuffer(CudaBuffer&& other) noexcept
        : m_ptr(other.m_ptr), m_count(other.m_count), m_size_bytes(other.m_size_bytes)
    {
        other.m_ptr        = nullptr;
        other.m_count      = 0;
        other.m_size_bytes = 0;
    }

    CudaBuffer& operator=(CudaBuffer&& other) noexcept
    {
        if (this != &other) {
            free();
            m_ptr              = other.m_ptr;
            m_count            = other.m_count;
            m_size_bytes       = other.m_size_bytes;
            other.m_ptr        = nullptr;
            other.m_count      = 0;
            other.m_size_bytes = 0;
        }
        return *this;
    }

    /* No copy */
    CudaBuffer(const CudaBuffer&)            = delete;
    CudaBuffer& operator=(const CudaBuffer&) = delete;

    /* ---- Allocation ---- */
    void alloc(size_t count)
    {
        free();
        m_count      = count;
        m_size_bytes = count * sizeof(T);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_ptr), m_size_bytes));
    }

    void free()
    {
        if (m_ptr) {
            CUDA_CHECK(cudaFree(m_ptr));
            m_ptr        = nullptr;
            m_count      = 0;
            m_size_bytes = 0;
        }
    }

    /* ---- Transfers ---- */
    void upload(const T* host_data, size_t count)
    {
        if (count > m_count) {
            alloc(count);
        }
        CUDA_CHECK(cudaMemcpy(m_ptr, host_data, count * sizeof(T),
                              cudaMemcpyHostToDevice));
    }

    void upload(const std::vector<T>& host_data)
    {
        upload(host_data.data(), host_data.size());
    }

    void uploadAsync(const T* host_data, size_t count, cudaStream_t stream)
    {
        if (count > m_count) {
            alloc(count);
        }
        CUDA_CHECK(cudaMemcpyAsync(m_ptr, host_data, count * sizeof(T),
                                   cudaMemcpyHostToDevice, stream));
    }

    void download(T* host_data, size_t count) const
    {
        CUDA_CHECK(cudaMemcpy(host_data, m_ptr,
                              count * sizeof(T), cudaMemcpyDeviceToHost));
    }

    void download(std::vector<T>& host_data) const
    {
        host_data.resize(m_count);
        download(host_data.data(), m_count);
    }

    void downloadAsync(T* host_data, size_t count, cudaStream_t stream) const
    {
        CUDA_CHECK(cudaMemcpyAsync(host_data, m_ptr,
                                   count * sizeof(T),
                                   cudaMemcpyDeviceToHost, stream));
    }

    /* ---- Clear to zero ---- */
    void zero()
    {
        if (m_ptr)
            CUDA_CHECK(cudaMemset(m_ptr, 0, m_size_bytes));
    }

    void zeroAsync(cudaStream_t stream)
    {
        if (m_ptr)
            CUDA_CHECK(cudaMemsetAsync(m_ptr, 0, m_size_bytes, stream));
    }

    /* ---- Accessors ---- */
    T*          get()       const { return m_ptr; }
    CUdeviceptr devicePtr() const { return reinterpret_cast<CUdeviceptr>(m_ptr); }
    size_t      count()     const { return m_count; }
    size_t      sizeBytes() const { return m_size_bytes; }
    bool        valid()     const { return m_ptr != nullptr; }

private:
    T*     m_ptr        = nullptr;
    size_t m_count      = 0;
    size_t m_size_bytes = 0;
};
