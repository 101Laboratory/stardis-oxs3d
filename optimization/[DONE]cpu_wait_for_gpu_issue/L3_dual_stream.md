# L3 双 Stream 传输-计算重叠方案

**创建日期**: 2026-03-04  
**状态**: 设计完成，待实施  
**前置**: L2 Early-Launch 已完成 (`opt/early-launch`)

---

## 1. 优化动机

L2 瓶颈分析（来自 `gpu_phase_breakdown.md` 实测）：

| 子阶段 | pool=8K 占 GPU-wait | pool=32K 占 GPU-wait | 性质 |
|--------|---------------------|---------------------|------|
| `d2h_download` | **45.7%** | **43.0%** | PCIe 下行传输 |
| `aos2soa_upload` | 10.3% | 11.9% | PCIe 上行传输 |
| `cuda_sync_wait` | 6.8% | 9.6% | 真正的 GPU kernel 等待 |
| 其余 (filter+post+rtrc) | 37.2% | 35.5% | CPU 计算 |

**PCIe 传输 (H2D + D2H) 占 GPU-wait 的 ~56%**，是 L2 下的最大单项瓶颈。

当前所有操作走单一 `ctx->stream`：
```
ctx->stream: [H2D upload] → [kernel] → [sync] → [D2H download]
                             全串行，PCIe 上行/下行不重叠
```

---

## 2. L3 目标：双 CUDA Stream + PCIe 全双工

### 2.1 核心原理

- PCIe 4.0 x16 有独立的上行/下行 DMA engine，**H2D↑ 和 D2H↓ 可同时传输**
- 将 "传输" 和 "计算" 分到两个 CUDA stream，通过 `cudaEvent` 同步
- `transfer_stream` 负责所有 H2D/D2H 操作
- `compute_stream` (现有 `ctx->stream`) 负责 `optixLaunch` kernel

### 2.2 目标时序

```
                        L2 (当前)                                      L3 (目标)
                        
Phase 1:                                              Phase 1:
CPU:  [sync+d2h(A)] [launch(B)] [post(A)] [cpu(A)]   CPU:  [sync_k(A)] [launch(B):AoS2SoA] [post(A)] [cpu(A)]
                                                      
GPU:  ←A done→        ══trace(B)══                    transfer_s: ▒D2H(A)▒  ▓H2D(B)▓
                                                      compute_s:  ←A done→   evt→ ══trace(B)══
                                                                  ↑PCIe全双工: D2H(A)↓ 与 H2D(B)↑ 同时↑
```

**L3 单半周期详细流程**:
```
1. sync_kernel(A)                         — cudaEventSynchronize(A.evt_kernel_done)
2. start_d2h_async(A)                     — cudaStreamWaitEvent(transfer_s, A.evt_kernel_done)
                                            + d_multi_hits.downloadAsync(transfer_s)
3. launch_h2d_and_kernel(B)               — AoS→SoA + uploadAsync(B, transfer_s)
                                            + cudaEventRecord(B.evt_upload_done, transfer_s)
                                            + cudaStreamWaitEvent(compute_s, B.evt_upload_done)
                                            + traceBatchMultiHit(compute_s)
                                            + cudaEventRecord(B.evt_kernel_done, compute_s)
4. wait_d2h(A)                            — cudaStreamSynchronize(transfer_s) or cudaEventSync
5. post(A)                                — CPU distribute + enc + cp + dsoa
6. cpu(A)                                 — cascade + harvest + refill + pre_gpu
```

> 步骤 2 和 3 中 **D2H(A)↓ 和 H2D(B)↑ 通过 PCIe 全双工同时进行**。
> 步骤 3 的 kernel(B) 在 upload(B) 完成后立即开始，与 D2H(A) 可能还在传输。

---

## 3. 改造层次

### 3.1 层 1：GPU 后端 — `batch_trace_context` 双 stream 分离

**文件**: `oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h`

现有结构：
```cpp
struct s3d_batch_trace_context {
    cudaStream_t                stream;          /* 单一 stream */
    std::vector<Ray>            host_rays;       /* host staging */
    std::vector<MultiHitResult> host_mhits;      /* host staging */
    bool                        async_pending;
    size_t                      async_nrays;
    // ...
};
```

改造为：
```cpp
struct s3d_batch_trace_context {
    cudaStream_t                compute_stream;    /* kernel 执行 */
    cudaStream_t                transfer_stream;   /* H2D/D2H 传输 */
    cudaEvent_t                 evt_upload_done;   /* H2D 完成信号 */
    cudaEvent_t                 evt_kernel_done;   /* kernel 完成信号 */

    Ray*                        h_rays_pinned;     /* pinned host staging ★ */
    MultiHitResult*             h_mhits_pinned;    /* pinned host staging ★ */
    size_t                      pinned_capacity;   /* pinned buffer 容量 */

    CudaBuffer<Ray>             d_rays;
    CudaBuffer<MultiHitResult>  d_multi_hits;

    bool                        async_pending;
    bool                        d2h_pending;       /* ★ 新状态: D2H 异步进行中 */
    size_t                      async_nrays;
    // ...
};
```

> **关键**: `host_mhits` 必须改为 `cudaHostAlloc` 分配的 pinned memory，否则
> `downloadAsync` 会退化为同步传输（CUDA 对 pageable memory 自动回退）。

### 3.2 层 2：拆分 GPU 后端 API

**文件**: `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp`

从当前 2 个 API 拆为 4 个：

| API | 职责 | Stream |
|-----|------|--------|
| `batch_trace_async_impl()` | AoS→SoA + H2D(transfer_s) + kernel(compute_s) | 两个 |
| `batch_trace_sync_kernel_impl()` ★ | `cudaEventSynchronize(evt_kernel_done)` | — |
| `batch_trace_start_d2h_impl()` ★ | `downloadAsync(transfer_s)` 异步 D2H | transfer |
| `batch_trace_wait_d2h_impl()` ★ | `cudaStreamSync(transfer_s)` + CPU filter + retrace | transfer |

> ★ = 新增 API

#### `batch_trace_async_impl` 改造

```cpp
// 现有: 全走 ctx->stream
ctx->d_rays.uploadAsync(host_rays, count, ctx->stream);
sv->tracer.traceBatchMultiHit(..., ctx->stream, ...);

// L3: upload 走 transfer, kernel 走 compute, event 同步
ctx->d_rays.uploadAsync(h_rays_pinned, count, ctx->transfer_stream);
cudaEventRecord(ctx->evt_upload_done, ctx->transfer_stream);
cudaStreamWaitEvent(ctx->compute_stream, ctx->evt_upload_done, 0);
sv->tracer.traceBatchMultiHit(..., ctx->compute_stream, ...);
cudaEventRecord(ctx->evt_kernel_done, ctx->compute_stream);
```

#### 新增 `batch_trace_sync_kernel_impl`

```cpp
static res_T batch_trace_sync_kernel_impl(s3d_batch_trace_context* ctx) {
    if (!ctx->async_pending) return RES_OK;
    cudaEventSynchronize(ctx->evt_kernel_done);  // 仅等 kernel
    return RES_OK;
}
```

#### 新增 `batch_trace_start_d2h_impl`

```cpp
static res_T batch_trace_start_d2h_impl(s3d_batch_trace_context* ctx) {
    unsigned count = (unsigned)ctx->async_nrays;
    // kernel 完成后才开始 D2H
    cudaStreamWaitEvent(ctx->transfer_stream, ctx->evt_kernel_done, 0);
    ctx->d_multi_hits.downloadAsync(ctx->h_mhits_pinned, count, ctx->transfer_stream);
    ctx->d2h_pending = true;
    return RES_OK;
}
```

#### 新增 `batch_trace_wait_d2h_impl`

```cpp
static res_T batch_trace_wait_d2h_impl(
    s3d_batch_trace_context* ctx,
    const s3d_ray_request* requests, size_t nrays,
    s3d_hit* hits, s3d_batch_trace_stats* stats)
{
    if (ctx->d2h_pending) {
        cudaStreamSynchronize(ctx->transfer_stream);
        ctx->d2h_pending = false;
    }
    ctx->async_pending = false;
    // ... CPU filter eval + retrace (与现有 wait_impl Phase 2/3 相同) ...
}
```

### 3.3 层 3：求解器主循环 API 与时序

**文件**: `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

新增求解器封装函数：

```c
static res_T gpu_sync_kernel(pool, pv, sv);       /* 仅等 kernel */
static res_T gpu_start_d2h_async(pool, pv, sv);   /* 异步启动 D2H */
static res_T gpu_wait_d2h(pool, pv, sv, scn);     /* 等 D2H + CPU filter + stats */
```

主循环 Phase 1 变为：
```c
/* Phase 1: sync_kernel(A) → start_d2h(A) → launch(B) → wait_d2h(A) → post(A) → cpu(A) */
gpu_sync_kernel(&pool, pv_a, sv);           // 等 kernel(A) 完成
gpu_start_d2h_async(&pool, pv_a, sv);       // 异步 D2H(A) 启动
gpu_launch_async(&pool, pv_b, sv);          // H2D(B) + kernel(B) — PCIe 全双工!
gpu_wait_d2h(&pool, pv_a, sv, scn);         // 等 D2H(A) 完成 + CPU filter
gpu_postprocess(&pool, pv_a, scn);          // distribute + enc + cp + dsoa
cpu_between(&pool, pv_a);                   // cascade + harvest + refill
cpu_pre_gpu(&pool, pv_a);                   // compact + collect
```

TIMELINE 扩展 (11 时间戳):
```
[TIMELINE] |syncA|d2hStartA|launchB|d2hWaitA|postA|cpuA|syncB|d2hStartB|launchA|d2hWaitB|postB|cpuB|cycle|
```

---

## 4. 前置改动

### 4.1 Pinned Memory（必须）

当前 `host_mhits` 是 `std::vector<MultiHitResult>`（pageable memory）。
`cudaMemcpyAsync` 对 pageable memory 会自动退化为同步 — **完全抵消双 stream 收益**。

改为 `cudaHostAlloc`:
```cpp
// 初始化时
cudaHostAlloc(&ctx->h_mhits_pinned, max_rays * sizeof(MultiHitResult), cudaHostAllocDefault);
cudaHostAlloc(&ctx->h_rays_pinned,  max_rays * sizeof(Ray), cudaHostAllocDefault);

// 释放时
cudaFreeHost(ctx->h_mhits_pinned);
cudaFreeHost(ctx->h_rays_pinned);
```

### 4.2 `CudaBuffer::downloadAsync` 方法（可能需新增）

检查 `CudaBuffer` 模板是否已有 `downloadAsync(T* host, size_t count, cudaStream_t stream)` 方法。
若无，需在 `CudaBuffer.h` 中新增。

### 4.3 `transfer_stream` 传递到 `batch_trace_context`

`cus3d_device` 已声明 `transfer_stream` 并在 `setup_cus3d_device()` 中创建。
需要在 `s3d_scene_view_alloc_batch_trace_ctx()` 中将 `dev->transfer_stream` 传递给 `ctx->transfer_stream`。

---

## 5. 预期收益

### 5.1 消除的传输开销

| 项目 | L2 占 wait | L3 处理 | 残余 |
|------|-----------|---------|------|
| `d2h_download` (43-46%) | 串行 PCIe 传输 | 与 H2D(B)↑ 全双工重叠 | ~20% (仅取 max(H2D,D2H)) |
| `aos2soa_upload` (10-12%) | 串行 PCIe 传输 | 与 D2H(A)↓ 全双工重叠 | ~0% (被 D2H 遮盖) |
| `cuda_sync_wait` (7-10%) | GPU kernel 执行 | 不变 | 7-10% |

### 5.2 周期缩短估算 (pool=8192)

```
L2 半周期: d2h(sync+transfer) ≈ 0.62ms
  其中 cuda_sync ≈ 0.04ms, d2h_transfer ≈ 0.28ms, upload(在launch中) ≈ 0.06ms
  
L3 半周期: sync_kernel ≈ 0.04ms + max(d2h_async, h2d_async+kernel_launch) ≈ max(0.28, 0.06+0.01)
         = 0.28ms (D2H dominant)
  
但 D2H(A) 与 H2D(B)+kernel(B) 重叠 → 实际 wait ≈ max(0.04, 0.28 - overlap)

保守: wait 缩短 40-50% → 半周期 0.62ms → ~0.35ms
整周期: 2.32ms → ~1.4ms (约 40% 提升)
```

---

## 6. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| Pinned memory 占用 | 32K × sizeof(MultiHitResult) × 2 ctx ≈ 额外 ~64MB | RTX 4090 有 24GB，可忽略 |
| cudaEvent 开销 | 2-5μs/event × 4 events/半周期 ≈ 10-20μs | 对 0.3ms+ 的传输可忽略 |
| downloadAsync 对 pageable 退化 | 完全抵消收益 | **硬性前置**: 必须用 pinned memory |
| 双 stream 争用 PCIe 带宽 | 全双工仅在不同方向有效 | H2D(B)↑ 和 D2H(A)↓ 方向不同，OK |
| OptiX `optixLaunch` stream 限制 | 某些 OptiX 版本要求特定 stream | 需验证 OptiX 9 对多 stream 支持 |

---

## 7. 实施顺序

1. **Pinned memory 改造** — `s3d_batch_trace_context` 中 host staging 改 `cudaHostAlloc`
2. **`CudaBuffer::downloadAsync`** — 若缺失则新增
3. **`transfer_stream` 传递** — 从 `cus3d_device` 传递到 `batch_trace_context`
4. **cudaEvent 基础设施** — 创建 `evt_upload_done` + `evt_kernel_done`
5. **拆分 `batch_trace_async_impl`** — upload→transfer_s, kernel→compute_s, event 同步
6. **新增 3 个 API** — sync_kernel / start_d2h / wait_d2h
7. **C wrapper 层** — `s3d_scene_view_trace_rays_batch_ctx_sync_kernel()` 等
8. **求解器主循环改造** — 插入 6 步序列
9. **TIMELINE 扩展** — 11 时间戳
10. **验证** — TIMELINE L2 vs L3 对比 + ctest + bit-exact
