# P1: 消除共享状态竞争

**工作量**: 1 天  
**改动文件**: `ox_s3d_internal.h`, `ox_s3d_scene_view.cpp`, `unified_tracer.cpp/h`  
**前置**: P0 (async API 基础设施)  
**验证**: 两个 ctx 可并发执行 _async，结果正确

---

## 目标

消除阻碍两个 `batch_trace_context` 并发使用的两个共享可变状态：

1. **`m_batch_params_ptr`** — UnifiedTracer 实例级共享的 launch params 设备内存
2. **retrace 缓冲区** — `sv->rt_retrace_rays/mhits`，scene_view 级共享
3. **`cudaDeviceSynchronize()`** — 全设备同步，会意外同步其他 stream

---

## Issue 1: `m_batch_params_ptr` 竞争

### 问题定位

`unified_tracer.cpp` L1255-1282:

```cpp
void UnifiedTracer::traceBatchMultiHit(
    Ray* d_rays, MultiHitResult* d_multi_hits,
    unsigned int count, CUstream stream)
{
    UnifiedParams lp = {};
    lp.handle     = activeRTHandle();
    lp.count      = count;
    lp.rays       = d_rays;
    lp.multi_hits = d_multi_hits;

    if (!m_batch_params_allocated) {
        CUDA_CHECK(cudaMalloc(&m_batch_params_ptr, sizeof(UnifiedParams)));
        m_batch_params_allocated = true;
    }
    CUDA_CHECK(cudaMemcpyAsync(m_batch_params_ptr, &lp, sizeof(UnifiedParams),
                               cudaMemcpyHostToDevice, stream));

    OPTIX_CHECK(optixLaunch(m_pipeline, stream, m_batch_params_ptr,
                            sizeof(UnifiedParams), &m_sbt_mh, w, h, 1));
}
```

**问题**: `m_batch_params_ptr` 是单份设备内存。两个 stream 并发调用时：
- Stream-A `cudaMemcpyAsync(params, lp_A, stream_A)` — 写入 A 的参数
- Stream-B `cudaMemcpyAsync(params, lp_B, stream_B)` — 覆盖为 B 的参数
- Stream-A `optixLaunch(stream_A, params)` — 读到 B 的参数 → **数据损坏**

### 解决方案 A: per-ctx params buffer（推荐）

在 `traceBatchMultiHit` 中接受外部 params buffer：

**文件**: `unified_tracer.h`，修改签名

```cpp
/* 原版保留 (使用 tracer 内部 params buffer) */
void traceBatchMultiHit(Ray* d_rays, MultiHitResult* d_multi_hits,
                        unsigned int count, CUstream stream = 0);

/* 新增: 接受外部 params buffer (双缓冲安全) */
void traceBatchMultiHit(Ray* d_rays, MultiHitResult* d_multi_hits,
                        unsigned int count, CUstream stream,
                        CUdeviceptr external_params_ptr);
```

**文件**: `unified_tracer.cpp`，新增重载

```cpp
void UnifiedTracer::traceBatchMultiHit(
    Ray* d_rays, MultiHitResult* d_multi_hits,
    unsigned int count, CUstream stream,
    CUdeviceptr external_params_ptr)
{
    UnifiedParams lp = {};
    lp.handle     = activeRTHandle();
    lp.count      = count;
    lp.rays       = d_rays;
    lp.multi_hits = d_multi_hits;

    /* 使用调用方提供的 params buffer — 每个 ctx 独立 */
    CUDA_CHECK(cudaMemcpyAsync(
        reinterpret_cast<void*>(external_params_ptr),
        &lp, sizeof(UnifiedParams),
        cudaMemcpyHostToDevice, stream));

    unsigned int w, h;
    if (count <= 65536) { w = count; h = 1; }
    else { w = 8192; h = (count + w - 1) / w; }

    OPTIX_CHECK(optixLaunch(m_pipeline, stream, external_params_ptr,
                            sizeof(UnifiedParams), &m_sbt_mh, w, h, 1));
}
```

### 在 ctx 中分配 params buffer

**文件**: `ox_s3d_scene_view.cpp`，在 `batch_trace_async_impl` 中

```cpp
/* 确保 per-ctx params buffer 已分配 */
if (!ctx->params_allocated) {
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&ctx->params_ptr),
                          sizeof(UnifiedParams)));
    ctx->params_allocated = true;
}

/* 使用 per-ctx params buffer */
sv->tracer.traceBatchMultiHit(
    ctx->d_rays.get(), ctx->d_multi_hits.get(), count,
    ctx->stream, ctx->params_ptr);
```

### 解决方案 B: stack-based params（备选）

OptiX 7+ 支持 `optixLaunch` 直接传栈上 params（如果 params ≤ 8 bytes）。
但 `UnifiedParams` 远超 8 bytes，此方案不可行。

---

## Issue 2: retrace 缓冲区共享

### 当前状态 ✅ 部分修复

retrace 缓冲区已从 `static` 移至 `sv` 成员 (ox_s3d_internal.h L219-220):

```cpp
CudaBuffer<Ray>            rt_retrace_rays;    /* sv 成员 */
CudaBuffer<MultiHitResult> rt_retrace_mhits;   /* sv 成员 */
```

在 batch_trace_impl Phase 3 中通过引用使用 (L1323-1324):

```cpp
CudaBuffer<Ray>&            s_rt_d_rays  = sv->rt_retrace_rays;
CudaBuffer<MultiHitResult>& s_rt_d_mhits = sv->rt_retrace_mhits;
```

### 残余问题

在双缓冲方案中，两半的 `_wait` (含 retrace) 不会在 CPU 上并发执行（单线程串行）。
但 retrace 内部的 GPU launch 可能与另一半的 async trace 在不同 stream 上并发。
更关键的是，retrace 使用 `cudaDeviceSynchronize()` (L1365)，
会同步**所有** stream，包括另一半正在执行的 async trace — 导致流水线退化为串行。

### 修复步骤

#### 2a. 将 retrace 缓冲区移入 ctx

P0 已在 `s3d_batch_trace_context` 中预留了 `rt_d_rays` 和 `rt_d_mhits` 字段。

**文件**: `ox_s3d_scene_view.cpp`，修改 `batch_trace_wait_impl` Phase 3

```cpp
/* 原:
CudaBuffer<Ray>&            s_rt_d_rays  = sv->rt_retrace_rays;
CudaBuffer<MultiHitResult>& s_rt_d_mhits = sv->rt_retrace_mhits;
*/

/* 改为: 使用 per-ctx retrace 缓冲区 */
CudaBuffer<Ray>&            s_rt_d_rays  = ctx->rt_d_rays;
CudaBuffer<MultiHitResult>& s_rt_d_mhits = ctx->rt_d_mhits;
```

#### 2b. retrace GPU launch 使用 ctx->stream

```cpp
/* 原:
sv->tracer.traceBatchMultiHit(
    s_rt_d_rays.get(), s_rt_d_mhits.get(), act_count);
*/

/* 改为: 传 ctx->stream + per-ctx params */
sv->tracer.traceBatchMultiHit(
    s_rt_d_rays.get(), s_rt_d_mhits.get(), act_count,
    ctx->stream, ctx->params_ptr);
```

注意 retrace 也需要 params buffer。由于 retrace 在 `_wait` 中执行，
此时 `_async` 尚未被再次调用，ctx 的 params buffer 不会冲突。

---

## Issue 3: `cudaDeviceSynchronize` → `cudaStreamSynchronize`

### 需修改的位置

| 位置 | 行号 | 上下文 | 改为 |
|------|------|--------|------|
| Phase 1 完成同步 | L1227 | `batch_trace_impl` | `cudaStreamSynchronize(ctx->stream)` |
| Phase 3 retrace 同步 | L1365 | retrace 迭代中 | `cudaStreamSynchronize(ctx->stream)` |

### Phase 1 (在 `batch_trace_wait_impl` 中)

```cpp
/* 原: cudaDeviceSynchronize(); */
cudaStreamSynchronize(ctx->stream);
```

### Phase 3 retrace 迭代 (在 `batch_trace_wait_impl` 中)

```cpp
/* 原:
sv->tracer.traceBatchMultiHit(
    s_rt_d_rays.get(), s_rt_d_mhits.get(), act_count);
cudaDeviceSynchronize();
*/

/* 改为: */
sv->tracer.traceBatchMultiHit(
    s_rt_d_rays.get(), s_rt_d_mhits.get(), act_count,
    ctx->stream, ctx->params_ptr);
cudaStreamSynchronize(ctx->stream);
```

### 同时保留原 `batch_trace_impl` 的兼容性

原 `batch_trace_impl` 中的 `cudaDeviceSynchronize()` 在 P0 Step 6 中已被替换为
通过 `_async` + `_wait` 调用，因此原始代码路径中的 `cudaDeviceSynchronize` 不再被调用。

如果需保留原 `batch_trace_impl` 作为后备，建议添加编译开关：

```cpp
#ifdef STARDIS_DUAL_BUFFER
    cudaStreamSynchronize(ctx->stream);
#else
    cudaDeviceSynchronize();
#endif
```

---

## 修改汇总

| 文件 | 修改 | 新增行 | 修改行 |
|------|------|--------|--------|
| `unified_tracer.h` | 新增 `traceBatchMultiHit` 重载 (带 external_params) | +3 | 0 |
| `unified_tracer.cpp` | 实现新重载 | +20 | 0 |
| `ox_s3d_internal.h` | ctx 字段已在 P0 添加 | 0 | 0 |
| `ox_s3d_scene_view.cpp` | async_impl 使用 per-ctx params; wait_impl retrace 改 ctx | 0 | ~15 |
| **总计** | | **+23** | **~15** |

---

## 验证清单

- [ ] 两个独立的 `batch_trace_context` 可交替调用 `_async`，结果正确
- [ ] retrace 不再触发 `cudaDeviceSynchronize`
- [ ] Nsight Systems trace 确认两个 stream 存在并行区间
- [ ] porous 场景结果与 P0 阶段一致 (bit-exact)
- [ ] 无 CUDA 内存泄漏 (`cuda-memcheck` 或 `compute-sanitizer`)
