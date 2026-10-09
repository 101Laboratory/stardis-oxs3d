/* ================================================================
 *  GPU Wavefront Feasibility Benchmark — Kernel A-D
 * ================================================================
 *  Standalone synthetic kernels that mimic the memory-access and
 *  compute patterns of a wavefront Monte-Carlo solver.  No stardis
 *  dependency; only CUDA + CUB.
 * ================================================================ */

#include "gpu_wf_bench.h"
#include <cuda_runtime.h>
#include <cub/cub.cuh>
#include <cstdlib>
#include <cstring>

/* Round up to next multiple of 16 to ensure float4 / uint4 alignment */
static inline int align16(int x) { return (x + 15) & ~15; }

/* ────────────────────────────────────────────────────────────────
 *  Kernel A : bandwidth_scan
 *  Pure read-16B / write-16B per path.  Measures raw bandwidth
 *  to path_state-sized strides.
 * ──────────────────────────────────────────────────────────────── */

__global__ void kernel_bandwidth_scan(
    char* __restrict__ pool,
    int   stride,
    int   n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    char* p = pool + (size_t)i * stride;

    /* read first 16 B, write last 16 B */
    uint4 val = *reinterpret_cast<uint4*>(p);
    val.x += 1u;
    *reinterpret_cast<uint4*>(p + stride - 16) = val;
}

void run_bandwidth_scan(const BenchParams& p, BenchResult& r)
{
    const size_t total_bytes = (size_t)p.pool_size * p.path_state_size;
    char* d_pool = nullptr;
    CUDA_CHECK(cudaMalloc(&d_pool, total_bytes));
    CUDA_CHECK(cudaMemset(d_pool, 0, total_bytes));

    const int threads = 256;
    const int blocks  = (p.pool_size + threads - 1) / threads;

    /* warm up */
    for (int w = 0; w < p.warmup_rounds; w++)
        kernel_bandwidth_scan<<<blocks, threads>>>(d_pool, p.path_state_size, p.pool_size);
    CUDA_CHECK(cudaDeviceSynchronize());

    /* measure */
    cudaEvent_t t0, t1;
    CUDA_CHECK(cudaEventCreate(&t0));
    CUDA_CHECK(cudaEventCreate(&t1));

    CUDA_CHECK(cudaEventRecord(t0));
    for (int m = 0; m < p.measure_rounds; m++)
        kernel_bandwidth_scan<<<blocks, threads>>>(d_pool, p.path_state_size, p.pool_size);
    CUDA_CHECK(cudaEventRecord(t1));
    CUDA_CHECK(cudaEventSynchronize(t1));

    float ms = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));

    r.kernel_us = (double)ms * 1000.0 / p.measure_rounds;

    /* effective bandwidth: each thread touches 2 × 128-byte sectors (read + write) */
    const double bytes_per_launch = (double)p.pool_size * 2.0 * 128.0;
    r.bandwidth_GBs = (bytes_per_launch * p.measure_rounds) / ((double)ms * 1e-3) / 1e9;

    CUDA_CHECK(cudaEventDestroy(t0));
    CUDA_CHECK(cudaEventDestroy(t1));
    CUDA_CHECK(cudaFree(d_pool));
}

/* ────────────────────────────────────────────────────────────────
 *  Kernel B : cascade_stub
 *  Simulates cascade loop: read hot → indirect cold → inner ALU
 *  loop → write hot + cold field.
 * ──────────────────────────────────────────────────────────────── */

__global__ void kernel_cascade_stub(
    path_hot_gpu*   __restrict__ hot_arr,
    char*           __restrict__ cold_pool,
    const int*      __restrict__ active_idx,
    int    cold_stride,
    int    n,
    int    inner_iters)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n) return;

    int slot = active_idx[tid];             /* indirect access */
    path_hot_gpu h = hot_arr[slot];         /* 8 B read        */

    if (!h.active) return;

    char* cold = cold_pool + (size_t)slot * cold_stride;

    float acc = 0.0f;
    for (int iter = 0; iter < inner_iters; iter++) {
        /* simulate switch(phase) reading different cold offsets */
        int offset = ((int)h.phase * 64 + iter * 128) % cold_stride;
        offset = offset & ~15;              /* 16-B aligned    */

        float4 val = *reinterpret_cast<float4*>(cold + offset);
        acc += val.x * val.y - val.z + val.w;

        h.phase = (uint8_t)((h.phase + 1) % 50);
    }

    h.needs_ray = (acc > 0.0f) ? 1 : 0;
    hot_arr[slot] = h;                      /* 8 B write       */
    *reinterpret_cast<float*>(cold + 8) = acc;  /* 4 B write   */
}

void run_cascade_stub(const BenchParams& p, BenchResult& r)
{
    const int n_active = (int)(p.pool_size * p.active_ratio);
    const int cold_stride = align16(p.path_state_size - 8);

    /* allocate device arrays */
    path_hot_gpu* d_hot  = nullptr;
    char*         d_cold = nullptr;
    int*          d_idx  = nullptr;

    CUDA_CHECK(cudaMalloc(&d_hot,  (size_t)p.pool_size * sizeof(path_hot_gpu)));
    CUDA_CHECK(cudaMalloc(&d_cold, (size_t)p.pool_size * cold_stride));
    CUDA_CHECK(cudaMalloc(&d_idx,  (size_t)n_active * sizeof(int)));

    /* init host-side hot + index arrays */
    {
        path_hot_gpu* h_hot = new path_hot_gpu[p.pool_size];
        int* h_idx = new int[n_active];
        for (int i = 0; i < p.pool_size; i++) {
            memset(&h_hot[i], 0, sizeof(path_hot_gpu));
            h_hot[i].phase  = (uint8_t)(i % 50);
            h_hot[i].active = (i < n_active) ? 1 : 0;
        }
        for (int i = 0; i < n_active; i++)
            h_idx[i] = i;   /* contiguous for baseline; can shuffle for scattered test */

        CUDA_CHECK(cudaMemcpy(d_hot, h_hot, (size_t)p.pool_size * sizeof(path_hot_gpu), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_idx, h_idx, (size_t)n_active * sizeof(int), cudaMemcpyHostToDevice));
        delete[] h_hot;
        delete[] h_idx;
    }

    /* init cold with non-zero data to avoid compiler elision */
    {
        size_t cold_bytes = (size_t)p.pool_size * cold_stride;
        char* h_cold = new char[cold_bytes];
        for (size_t i = 0; i < cold_bytes; i++)
            h_cold[i] = (char)(i & 0xFF);
        CUDA_CHECK(cudaMemcpy(d_cold, h_cold, cold_bytes, cudaMemcpyHostToDevice));
        delete[] h_cold;
    }

    const int threads = 256;
    const int blocks  = (n_active + threads - 1) / threads;

    /* warm up */
    for (int w = 0; w < p.warmup_rounds; w++)
        kernel_cascade_stub<<<blocks, threads>>>(
            d_hot, d_cold, d_idx, cold_stride, n_active, p.inner_iters);
    CUDA_CHECK(cudaDeviceSynchronize());

    /* measure */
    cudaEvent_t t0, t1;
    CUDA_CHECK(cudaEventCreate(&t0));
    CUDA_CHECK(cudaEventCreate(&t1));

    CUDA_CHECK(cudaEventRecord(t0));
    for (int m = 0; m < p.measure_rounds; m++)
        kernel_cascade_stub<<<blocks, threads>>>(
            d_hot, d_cold, d_idx, cold_stride, n_active, p.inner_iters);
    CUDA_CHECK(cudaEventRecord(t1));
    CUDA_CHECK(cudaEventSynchronize(t1));

    float ms = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));

    r.kernel_us     = (double)ms * 1000.0 / p.measure_rounds;
    r.iters_per_sec = (double)n_active * p.inner_iters * p.measure_rounds / ((double)ms * 1e-3);

    CUDA_CHECK(cudaEventDestroy(t0));
    CUDA_CHECK(cudaEventDestroy(t1));
    CUDA_CHECK(cudaFree(d_hot));
    CUDA_CHECK(cudaFree(d_cold));
    CUDA_CHECK(cudaFree(d_idx));
}

/* ────────────────────────────────────────────────────────────────
 *  Kernel C : compact_bench
 *  CUB DeviceSelect::If on pool-sized flag array.
 * ──────────────────────────────────────────────────────────────── */

struct IsActive {
    __device__ __forceinline__
    bool operator()(const uint8_t& flag) const { return flag != 0; }
};

void run_compact_bench(const BenchParams& p, BenchResult& r)
{
    const int n = p.pool_size;
    const int n_active = (int)(n * p.active_ratio);

    /* device arrays */
    uint8_t* d_flags      = nullptr;
    int*     d_indices_in  = nullptr;
    int*     d_indices_out = nullptr;
    int*     d_num_sel     = nullptr;

    CUDA_CHECK(cudaMalloc(&d_flags,      (size_t)n));
    CUDA_CHECK(cudaMalloc(&d_indices_in,  (size_t)n * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_indices_out, (size_t)n * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_num_sel,     sizeof(int)));

    /* init flags: first n_active are 1, rest 0 */
    {
        uint8_t* h_flags = new uint8_t[n];
        int*     h_idx   = new int[n];
        for (int i = 0; i < n; i++) {
            h_flags[i] = (i < n_active) ? 1 : 0;
            h_idx[i]   = i;
        }
        CUDA_CHECK(cudaMemcpy(d_flags,     h_flags, (size_t)n,              cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_indices_in, h_idx,   (size_t)n * sizeof(int), cudaMemcpyHostToDevice));
        delete[] h_flags;
        delete[] h_idx;
    }

    /* determine temp storage */
    size_t temp_bytes = 0;
    cub::DeviceSelect::Flagged(nullptr, temp_bytes,
        d_indices_in, d_flags, d_indices_out, d_num_sel, n);

    void* d_temp = nullptr;
    CUDA_CHECK(cudaMalloc(&d_temp, temp_bytes));

    /* warm up */
    for (int w = 0; w < p.warmup_rounds; w++)
        cub::DeviceSelect::Flagged(d_temp, temp_bytes,
            d_indices_in, d_flags, d_indices_out, d_num_sel, n);
    CUDA_CHECK(cudaDeviceSynchronize());

    /* measure */
    cudaEvent_t t0, t1;
    CUDA_CHECK(cudaEventCreate(&t0));
    CUDA_CHECK(cudaEventCreate(&t1));

    CUDA_CHECK(cudaEventRecord(t0));
    for (int m = 0; m < p.measure_rounds; m++)
        cub::DeviceSelect::Flagged(d_temp, temp_bytes,
            d_indices_in, d_flags, d_indices_out, d_num_sel, n);
    CUDA_CHECK(cudaEventRecord(t1));
    CUDA_CHECK(cudaEventSynchronize(t1));

    float ms = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));

    r.compact_us = (double)ms * 1000.0 / p.measure_rounds;

    CUDA_CHECK(cudaEventDestroy(t0));
    CUDA_CHECK(cudaEventDestroy(t1));
    CUDA_CHECK(cudaFree(d_flags));
    CUDA_CHECK(cudaFree(d_indices_in));
    CUDA_CHECK(cudaFree(d_indices_out));
    CUDA_CHECK(cudaFree(d_num_sel));
    CUDA_CHECK(cudaFree(d_temp));
}

/* ────────────────────────────────────────────────────────────────
 *  Kernel D : multi_phase_sim
 *  Launch cascade+collect+compact+distribute+harvest per round
 *  and measure aggregate per-round cost including launch overhead.
 * ──────────────────────────────────────────────────────────────── */

/* A lightweight "touch" kernel used as collect / distribute / harvest stub.
   stride may be as small as 8 (hot_arr), so use a 4-byte read/write
   to avoid misaligned-address faults on strict GPUs. */
__global__ void kernel_touch(char* __restrict__ buf, int stride, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    char* p = buf + (size_t)i * stride;
    unsigned int v = *reinterpret_cast<unsigned int*>(p);   /* 4 B read  */
    v += 1u;
    *reinterpret_cast<unsigned int*>(p) = v;                /* 4 B write */
}

void run_multi_phase_sim(const BenchParams& p, BenchResult& r)
{
    const int n_active   = (int)(p.pool_size * p.active_ratio);
    const int cold_stride = align16(p.path_state_size - 8);
    const int threads    = 256;
    const int blk_full   = (p.pool_size + threads - 1) / threads;
    const int blk_active = (n_active   + threads - 1) / threads;

    /* allocations */
    path_hot_gpu* d_hot  = nullptr;
    char*         d_cold = nullptr;
    int*          d_idx  = nullptr;
    uint8_t*      d_flags = nullptr;
    int*          d_idx_in  = nullptr;
    int*          d_idx_out = nullptr;
    int*          d_num_sel = nullptr;

    CUDA_CHECK(cudaMalloc(&d_hot,     (size_t)p.pool_size * sizeof(path_hot_gpu)));
    CUDA_CHECK(cudaMalloc(&d_cold,    (size_t)p.pool_size * cold_stride));
    CUDA_CHECK(cudaMalloc(&d_idx,     (size_t)n_active * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_flags,   (size_t)p.pool_size));
    CUDA_CHECK(cudaMalloc(&d_idx_in,  (size_t)p.pool_size * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_idx_out, (size_t)p.pool_size * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_num_sel, sizeof(int)));

    /* init */
    {
        path_hot_gpu* h_hot = new path_hot_gpu[p.pool_size];
        int*          h_idx = new int[p.pool_size]; /* also used for idx_in */
        uint8_t*      h_flg = new uint8_t[p.pool_size];
        size_t cold_bytes = (size_t)p.pool_size * cold_stride;
        char* h_cold = new char[cold_bytes];

        for (int i = 0; i < p.pool_size; i++) {
            memset(&h_hot[i], 0, sizeof(path_hot_gpu));
            h_hot[i].phase  = (uint8_t)(i % 50);
            h_hot[i].active = (i < n_active) ? 1 : 0;
            h_flg[i]        = h_hot[i].active;
            h_idx[i]        = i;
        }
        for (size_t i = 0; i < cold_bytes; i++)
            h_cold[i] = (char)(i & 0xFF);

        CUDA_CHECK(cudaMemcpy(d_hot,    h_hot,   (size_t)p.pool_size * sizeof(path_hot_gpu), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_cold,   h_cold,  cold_bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_idx,    h_idx,   (size_t)n_active * sizeof(int), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_flags,  h_flg,   (size_t)p.pool_size, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_idx_in, h_idx,   (size_t)p.pool_size * sizeof(int), cudaMemcpyHostToDevice));

        delete[] h_hot;
        delete[] h_idx;
        delete[] h_flg;
        delete[] h_cold;
    }

    /* CUB temp buffer */
    size_t temp_bytes = 0;
    cub::DeviceSelect::Flagged(nullptr, temp_bytes,
        d_idx_in, d_flags, d_idx_out, d_num_sel, p.pool_size);
    void* d_temp = nullptr;
    CUDA_CHECK(cudaMalloc(&d_temp, temp_bytes));

    cudaStream_t stream;
    CUDA_CHECK(cudaStreamCreate(&stream));

    /* warm up */
    for (int w = 0; w < p.warmup_rounds; w++) {
        kernel_cascade_stub<<<blk_active, threads, 0, stream>>>(
            d_hot, d_cold, d_idx, cold_stride, n_active, p.inner_iters);
        kernel_touch<<<blk_full, threads, 0, stream>>>(
            reinterpret_cast<char*>(d_hot), 8, p.pool_size);
        cub::DeviceSelect::Flagged(d_temp, temp_bytes,
            d_idx_in, d_flags, d_idx_out, d_num_sel, p.pool_size, stream);
        kernel_touch<<<blk_active, threads, 0, stream>>>(
            d_cold, cold_stride, n_active);
        kernel_touch<<<blk_full, threads, 0, stream>>>(
            reinterpret_cast<char*>(d_hot), 8, p.pool_size);
    }
    CUDA_CHECK(cudaStreamSynchronize(stream));

    /* measure */
    cudaEvent_t t0, t1;
    CUDA_CHECK(cudaEventCreate(&t0));
    CUDA_CHECK(cudaEventCreate(&t1));

    CUDA_CHECK(cudaEventRecord(t0, stream));
    for (int m = 0; m < p.measure_rounds; m++) {
        /* Phase 1: cascade */
        kernel_cascade_stub<<<blk_active, threads, 0, stream>>>(
            d_hot, d_cold, d_idx, cold_stride, n_active, p.inner_iters);
        /* Phase 2: collect — touch hot */
        kernel_touch<<<blk_full, threads, 0, stream>>>(
            reinterpret_cast<char*>(d_hot), 8, p.pool_size);
        /* Phase 3: compact */
        cub::DeviceSelect::Flagged(d_temp, temp_bytes,
            d_idx_in, d_flags, d_idx_out, d_num_sel, p.pool_size, stream);
        /* Phase 4: distribute — touch cold */
        kernel_touch<<<blk_active, threads, 0, stream>>>(
            d_cold, cold_stride, n_active);
        /* Phase 5: harvest/refill — touch hot */
        kernel_touch<<<blk_full, threads, 0, stream>>>(
            reinterpret_cast<char*>(d_hot), 8, p.pool_size);
    }
    CUDA_CHECK(cudaEventRecord(t1, stream));
    CUDA_CHECK(cudaEventSynchronize(t1));

    float ms = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));

    r.round_us = (double)ms * 1000.0 / p.measure_rounds;

    CUDA_CHECK(cudaEventDestroy(t0));
    CUDA_CHECK(cudaEventDestroy(t1));
    CUDA_CHECK(cudaStreamDestroy(stream));
    CUDA_CHECK(cudaFree(d_hot));
    CUDA_CHECK(cudaFree(d_cold));
    CUDA_CHECK(cudaFree(d_idx));
    CUDA_CHECK(cudaFree(d_flags));
    CUDA_CHECK(cudaFree(d_idx_in));
    CUDA_CHECK(cudaFree(d_idx_out));
    CUDA_CHECK(cudaFree(d_num_sel));
    CUDA_CHECK(cudaFree(d_temp));
}
