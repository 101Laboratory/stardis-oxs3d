# oxstar-3d Retrace 性能退化 — 修复实施方案

**前置文档**: [analysis.md](analysis.md)  
**优先级**: P0 (主程序 80% 时间浪费在系统调用上)  
**估计工作量**: Step 1-2 约 1 小时, Step 3 约 30 分钟, Step 4 约 1 小时

---

## 修复总览

按优先级排序，共 4 步。Step 1-2 为最小必要修复（消除根因），Step 3-4 为对齐
custar-3d 完整策略。

```
Step 1: 给 retry 循环加 MAX_FALLBACK_DEPTH           ← 消除无限循环风险
Step 2: traceSingle d_params 预分配                   ← 消除每次 malloc/free/sync
Step 3: traceBatch/traceBatchMultiHit d_params 预分配  ← 消除 Phase 1 额外 malloc/free
Step 4: batch_trace_impl 使用 ctx 预分配 buffer        ← 消除 Phase 1 大 buffer malloc/free
```

---

## Step 1: 给 `s3d_scene_view_trace_ray` retry 循环加 MAX_FALLBACK_DEPTH

**目标**: 对齐 custar-3d 的 `MAX_FALLBACK_DEPTH = 4` 限制，防止无限循环

**文件**: `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp`

### 1.1 在文件顶部添加常量

在 `#include` 之后、第一个函数定义之前添加:

```cpp
/* Maximum retry depth for filter-rejected hits.
 * Matches custar-3d MAX_FALLBACK_DEPTH (s3d_scene_view_trace_ray.cpp:52). */
#define OX_MAX_FILTER_RETRY 8
```

> 注: oxstar-3d 每次 retry 只跳过 1 个 hit (而非 custar-3d 的 K=2 个)，
> 因此上限设为 8 (= custar-3d 的 4 × Top-K 2)，等效覆盖 8 个被拒候选。

### 1.2 修改 `s3d_scene_view_trace_ray` 中的 retry 循环

**当前代码** (`L988-1020`):
```cpp
        if (rej != 0) {
            float retry_tmin = hr.t + 1e-6f;
            bool accepted = false;
            while (retry_tmin < ray.tmax) {
                ray.tmin = retry_tmin;
                hr = sv->tracer.traceSingle(ray);
                ...
```

**修改为**:
```cpp
        if (rej != 0) {
            float retry_tmin = hr.t + 1e-6f;
            bool accepted = false;
            int retry_depth = 0;
            while (retry_tmin < ray.tmax && retry_depth < OX_MAX_FILTER_RETRY) {
                ++retry_depth;
                ray.tmin = retry_tmin;
                hr = sv->tracer.traceSingle(ray);
                ...
```

**验证**: retry_depth 达到上限时 accepted 保持 false → `*hit = S3D_HIT_NULL`，
与 custar-3d 行为一致。

### 1.3 同样检查 `s3d_scene_view_trace_rays` 中的 retry 循环

在 `ox_s3d_scene_view.cpp` L1095-1130 区域也有一个类似的 retry 循环
（`trace_rays` 非 batch 版本的 filter 处理），需做同样修改。

**搜索**: `retry_tmin` 在文件中的所有出现位置，确保都加了深度限制。

---

## Step 2: `traceSingle()` 中 `d_params` 预分配

**目标**: 消除每次 `traceSingle()` 调用的 `cudaMalloc`/`cudaFree`

**文件**: `oxstar-3d/0.10/src/unified_tracer.h` + `unified_tracer.cpp`

### 2.1 在 `unified_tracer.h` 添加成员变量

在已有的 `m_single_*` 系列之后 (L357 附近) 添加:

```cpp
    /* ---- Reusable single-query buffers ---- */
    CudaBuffer<Ray>       m_single_ray_buf;
    CudaBuffer<HitResult> m_single_hit_buf;
    CudaBuffer<float3>    m_single_query_buf;
    CudaBuffer<NNResult>  m_single_result_buf;
    bool                  m_single_bufs_allocated = false;

    /* ---- Pre-allocated launch params buffer (Step 2) ---- */
    CudaBuffer<UnifiedParams> m_single_params_buf;   // ← 新增
```

### 2.2 在 `ensureSingleBuffers()` 中一并分配

**当前代码** (`unified_tracer.cpp:494-502`):
```cpp
void UnifiedTracer::ensureSingleBuffers() {
    if (!m_single_bufs_allocated) {
        m_single_ray_buf.alloc(1);
        m_single_hit_buf.alloc(1);
        m_single_query_buf.alloc(1);
        m_single_result_buf.alloc(1);
        m_single_bufs_allocated = true;
    }
}
```

**修改为**:
```cpp
void UnifiedTracer::ensureSingleBuffers() {
    if (!m_single_bufs_allocated) {
        m_single_ray_buf.alloc(1);
        m_single_hit_buf.alloc(1);
        m_single_query_buf.alloc(1);
        m_single_result_buf.alloc(1);
        m_single_params_buf.alloc(1);    // ← 新增
        m_single_bufs_allocated = true;
    }
}
```

### 2.3 修改 `traceSingle()` 使用预分配 buffer

**当前代码** (`unified_tracer.cpp:1131-1154`):
```cpp
HitResult UnifiedTracer::traceSingle(const Ray& ray) {
    ensureSingleBuffers();
    m_single_ray_buf.upload(&ray, 1);

    UnifiedParams lp = {};
    lp.handle = activeRTHandle();
    lp.count  = 1;
    lp.rays   = m_single_ray_buf.get();
    lp.hits   = m_single_hit_buf.get();

    CudaBuffer<UnifiedParams> d_params;  // ← 栈变量，每次 alloc/free
    d_params.alloc(1);
    d_params.upload(&lp, 1);

    OPTIX_CHECK(optixLaunch(
        m_pipeline, 0, d_params.devicePtr(), sizeof(UnifiedParams),
        &m_sbt_rt, 1, 1, 1));
    CUDA_SYNC_CHECK();

    HitResult result;
    m_single_hit_buf.download(&result, 1);
    return result;
}
```

**修改为**:
```cpp
HitResult UnifiedTracer::traceSingle(const Ray& ray) {
    ensureSingleBuffers();
    m_single_ray_buf.upload(&ray, 1);

    UnifiedParams lp = {};
    lp.handle = activeRTHandle();
    lp.count  = 1;
    lp.rays   = m_single_ray_buf.get();
    lp.hits   = m_single_hit_buf.get();

    m_single_params_buf.upload(&lp, 1);  // ← 复用预分配 buffer

    OPTIX_CHECK(optixLaunch(
        m_pipeline, 0, m_single_params_buf.devicePtr(), sizeof(UnifiedParams),
        &m_sbt_rt, 1, 1, 1));
    CUDA_SYNC_CHECK();

    HitResult result;
    m_single_hit_buf.download(&result, 1);
    return result;
}
```

**效果**: 消除 retry 循环中每次迭代的 `cudaMalloc` + `cudaFree`。

### 2.4 关于 `UnifiedParams` forward declaration

`unified_tracer.h` 需要知道 `UnifiedParams` 的大小才能声明 `CudaBuffer<UnifiedParams>`。
检查 `UnifiedParams` 定义位置 (`unified_params.h`)，确认已包含或可前向声明。

如果 `unified_params.h` 仅在 `.cpp` 中包含，则改为:
- 在 `unified_tracer.h` 中 forward declare `struct UnifiedParams;`
- 但 `CudaBuffer<T>` 需要 `sizeof(T)`，所以必须在 `.h` 中 #include `unified_params.h`

或者将 `m_single_params_buf` 改为 `CUdeviceptr` 类型手动管理（避免头文件依赖）:

```cpp
    CUdeviceptr m_single_params_ptr = 0;    // 在 ensureSingleBuffers 中 cudaMalloc
    size_t      m_single_params_size = 0;
```

---

## Step 3: `traceBatch`/`traceBatchMultiHit` 指针版本 d_params 预分配

**目标**: 消除 Phase 1 中 `traceBatchMultiHit(ptr)` 的 `cudaMalloc(d_params)`/`cudaFree`

**文件**: `oxstar-3d/0.10/src/unified_tracer.h` + `unified_tracer.cpp`

### 3.1 添加 batch params buffer 成员

```cpp
    /* ---- Pre-allocated batch launch params ---- */
    CUdeviceptr m_batch_params_ptr  = 0;
    bool        m_batch_params_allocated = false;
```

### 3.2 一次性分配 (lazy)

在 `traceBatch(ptr)` 和 `traceBatchMultiHit(ptr)` 入口:
```cpp
if (!m_batch_params_allocated) {
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&m_batch_params_ptr),
                          sizeof(UnifiedParams)));
    m_batch_params_allocated = true;
}
```

### 3.3 修改 `traceBatchMultiHit(ptr)` (L1248-1269)

**当前**:
```cpp
    CUdeviceptr d_params;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_params), sizeof(UnifiedParams)));
    CUDA_CHECK(cudaMemcpyAsync(...));
    OPTIX_CHECK(optixLaunch(..., d_params, ...));
    CUDA_CHECK(cudaFree(reinterpret_cast<void*>(d_params)));
```

**修改为**:
```cpp
    ensureBatchParamsBuffer();  // 一次性分配
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(m_batch_params_ptr),
                               &lp, sizeof(UnifiedParams),
                               cudaMemcpyHostToDevice, stream));
    OPTIX_CHECK(optixLaunch(..., m_batch_params_ptr, ...));
    // 不 free — 复用
```

### 3.4 同样修改 `traceBatch(ptr)` (L1156-1178)

完全相同的模式。

### 3.5 cleanup 中释放

在 `UnifiedTracer::cleanup()` 添加:
```cpp
if (m_batch_params_ptr) {
    cudaFree(reinterpret_cast<void*>(m_batch_params_ptr));
    m_batch_params_ptr = 0;
    m_batch_params_allocated = false;
}
```

---

## Step 4: `batch_trace_impl` 使用 ctx 预分配 buffer

**目标**: 消除 Phase 1 中 `traceBatchMultiHit(vector)` 每次创建的
`d_rays` + `d_multi_hits` 大 buffer

**文件**: `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp`

### 4.1 修改 `batch_trace_impl` 签名，接收 ctx

**当前**:
```cpp
static res_T batch_trace_impl(s3d_scene_view* sv,
                               const s3d_ray_request* requests,
                               size_t nrays,
                               s3d_hit* hits,
                               s3d_batch_trace_stats* stats)
```

**修改为**:
```cpp
static res_T batch_trace_impl(s3d_scene_view* sv,
                               s3d_batch_trace_context* ctx,  // ← 新增
                               const s3d_ray_request* requests,
                               size_t nrays,
                               s3d_hit* hits,
                               s3d_batch_trace_stats* stats)
```

### 4.2 Phase 1 使用 ctx 的预分配 buffer

**当前**:
```cpp
    std::vector<Ray> rays(nrays);
    for (...) { /* fill rays */ }
    std::vector<MultiHitResult> mhits = sv->tracer.traceBatchMultiHit(rays);
```

**修改为**:
```cpp
    std::vector<Ray> rays(nrays);
    for (...) { /* fill rays */ }

    std::vector<MultiHitResult> mhits;
    if (ctx && nrays <= ctx->max_rays) {
        /* 使用预分配 device buffer */
        ctx->d_rays.upload(rays.data(), nrays);
        sv->tracer.traceBatchMultiHit(
            ctx->d_rays.get(), ctx->d_multi_hits.get(),
            static_cast<unsigned int>(nrays));
        CUDA_SYNC_CHECK();
        mhits.resize(nrays);
        ctx->d_multi_hits.download(mhits.data(), nrays);
    } else {
        /* 回退到每次分配版本 */
        mhits = sv->tracer.traceBatchMultiHit(rays);
    }
```

### 4.3 修改 `_ctx` 版本传递 ctx

**当前**:
```cpp
res_T s3d_scene_view_trace_rays_batch_ctx(
    s3d_scene_view* sv,
    s3d_batch_trace_context* /*ctx*/,
    ...) {
    return batch_trace_impl(sv, requests, nrays, hits, stats);
}
```

**修改为**:
```cpp
res_T s3d_scene_view_trace_rays_batch_ctx(
    s3d_scene_view* sv,
    s3d_batch_trace_context* ctx,
    ...) {
    return batch_trace_impl(sv, ctx, requests, nrays, hits, stats);
}
```

### 4.4 修改无 ctx 版本传 nullptr

```cpp
res_T s3d_scene_view_trace_rays_batch(...) {
    return batch_trace_impl(sv, nullptr, requests, nrays, hits, stats);
}
```

---

## 验证计划

### 单元测试
```bash
cd stardis-cus3d/build
cmake --build . --config Release --target test_s3d_batch_trace > build.log 2>&1
.\bin\Release\test_s3d_batch_trace.exe
```
验证:
- 所有现有断言通过
- `retrace_accepted + retrace_missed == filter_rejected` 不变量

### Profile 验证
用 VS Profiler 重新跑 porous IR 渲染场景，确认:
- `ntdll.dll` self-time 从 80% 大幅下降
- `retrace_time_ms` 下降

### 正确性回归
运行 porous 场景完整 IR 渲染，对比修复前后输出:
```bash
cd Stardis-Starter-Pack/porous
<stardis-exe> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > "IR_after_fix.ht"
```
与之前的输出做逐像素 diff (容差 1e-6)。

---

## 风险评估

| Step | 风险 | 缓解措施 |
|------|------|---------|
| 1 | `OX_MAX_FILTER_RETRY` 不足导致正确性退化 (合法 hit 被截断) | 参考 custar-3d 的 4×K=8 上限; stats 最终输出中 retrace_missed 计数可监控 |
| 2 | `m_single_params_buf` 并发访问 (多线程调用 traceSingle) | 当前 oxstar-3d 是单线程调用模式，无风险; 未来并行化需加锁或 per-thread buffer |
| 3 | 同 Step 2 | 同上 |
| 4 | ctx 生命周期管理 (ctx 在场景 rebuild 后可能失效) | ctx 不引用 scene 数据，仅是 device buffer 容器，rebuild 不影响 |

---

## 预期效果

| 指标 | 修复前 | Step 1 后 | Step 1+2 后 | 全部完成后 |
|------|--------|----------|------------|-----------|
| ntdll.dll self% | 80% | ~30% (限制循环次数) | ~5% (消除 malloc/free) | <1% |
| retrace 路径 cudaMalloc/次 | R×K | R×min(K,8) | 0 | 0 |
| Phase 1 cudaMalloc/次 | 3 | 3 | 3 | 0 (使用 ctx) |
| 最坏单射线 GPU roundtrip | 无穷 | 8 | 8 | 8 |
