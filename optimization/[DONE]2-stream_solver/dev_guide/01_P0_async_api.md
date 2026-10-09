# P0: GPU 后端异步 API

**工作量**: 2 天  
**改动文件**: `ox_s3d_internal.h`, `ox_s3d_scene_view.cpp`, `s3d.h`  
**前置**: 无  
**验证**: 单池模式下 _async+_wait 结果与原 _batch_ctx 一致

---

## 目标

将当前同步的 `batch_trace_impl()` 拆分为两个阶段：
1. **_async**: CPU 端准备 + GPU launch，立即返回
2. **_wait**: 等待 GPU 完成 + 下载结果 + CPU postprocess + retrace

---

## Step 1: 扩展 `s3d_batch_trace_context`

**文件**: `ox_s3d_internal.h` L267-278

### 当前代码

```cpp
struct s3d_batch_trace_context {
    size_t              max_rays;
    CudaBuffer<Ray>             d_rays;
    CudaBuffer<MultiHitResult>  d_multi_hits;

    s3d_batch_trace_context(size_t max)
        : max_rays(max) {
        d_rays.alloc(static_cast<unsigned int>(max));
        d_multi_hits.alloc(static_cast<unsigned int>(max));
    }
};
```

### 目标代码

```cpp
struct s3d_batch_trace_context {
    size_t              max_rays;
    CudaBuffer<Ray>             d_rays;
    CudaBuffer<MultiHitResult>  d_multi_hits;

    /* === P0 新增: 异步支持 === */
    cudaStream_t                stream;          /* 独立 CUDA stream */
    std::vector<Ray>            host_rays;       /* 预分配 host 缓冲 */
    std::vector<MultiHitResult> host_mhits;      /* 预分配 host 缓冲 */
    bool                        async_pending;   /* 异步操作进行中 */
    size_t                      async_nrays;     /* 本次异步的射线数 */

    /* === P1 预留: 独立 launch params + retrace 缓冲区 === */
    CUdeviceptr                 params_ptr;      /* per-ctx params 设备内存 */
    bool                        params_allocated;
    CudaBuffer<Ray>             rt_d_rays;       /* retrace 缓冲 (per-ctx) */
    CudaBuffer<MultiHitResult>  rt_d_mhits;      /* retrace 缓冲 (per-ctx) */

    s3d_batch_trace_context(size_t max)
        : max_rays(max)
        , stream(nullptr)
        , async_pending(false)
        , async_nrays(0)
        , params_ptr(0)
        , params_allocated(false)
    {
        d_rays.alloc(static_cast<unsigned int>(max));
        d_multi_hits.alloc(static_cast<unsigned int>(max));
        CUDA_CHECK(cudaStreamCreate(&stream));
        host_rays.reserve(max);
        host_mhits.reserve(max);
    }

    ~s3d_batch_trace_context() {
        if (params_ptr) {
            cudaFree(reinterpret_cast<void*>(params_ptr));
            params_ptr = 0;
        }
        if (stream) {
            cudaStreamDestroy(stream);
            stream = nullptr;
        }
    }

    /* 禁止拷贝 */
    s3d_batch_trace_context(const s3d_batch_trace_context&) = delete;
    s3d_batch_trace_context& operator=(const s3d_batch_trace_context&) = delete;
};
```

### 注意事项

- `cudaStreamCreate` 在构造时执行，每个 ctx 持有独立 stream
- `host_rays` / `host_mhits` 使用 `reserve` 而非 `resize`，避免无用初始化
- `params_ptr` 和 retrace 缓冲区在 P1 中实际启用，此处仅预留字段
- 析构顺序：先释放 params → 再释放 stream → CudaBuffer 自动释放

---

## Step 2: 声明公共 API

**文件**: `s3d.h`，在 `s3d_scene_view_trace_rays_batch_ctx` (L402) 之后添加

```c
/* P0: 异步光追 — launch 后立即返回，不等待 GPU */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_async(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays);

/* P0: 等待异步光追完成，执行 CPU 后处理，返回结果 */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_wait(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats);
```

---

## Step 3: 实现 `batch_trace_async_impl`

**文件**: `ox_s3d_scene_view.cpp`，在 `batch_trace_impl` (L1185) 之后新增

### 伪代码

```cpp
static res_T batch_trace_async_impl(
    s3d_scene_view* sv,
    s3d_batch_trace_context* ctx,
    const s3d_ray_request* requests,
    size_t nrays)
{
    /* 1. ensure_built */
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    /* 2. 空场景快速返回 (标记无异步操作) */
    if (!sv->has_geometry || nrays == 0) {
        ctx->async_pending = false;
        ctx->async_nrays = nrays;
        return RES_OK;
    }

    /* 3. AoS → SoA 转换 (写入 ctx->host_rays) */
    ctx->host_rays.resize(nrays);
    for (size_t i = 0; i < nrays; i++) {
        ctx->host_rays[i].origin    = make_float3(
            requests[i].origin[0], requests[i].origin[1], requests[i].origin[2]);
        ctx->host_rays[i].direction = make_float3(
            requests[i].direction[0], requests[i].direction[1], requests[i].direction[2]);
        ctx->host_rays[i].tmin      = requests[i].range[0];
        ctx->host_rays[i].tmax      = requests[i].range[1];
    }

    /* 4. 异步上传 + 异步 GPU launch */
    unsigned int count = static_cast<unsigned int>(nrays);
    ctx->d_rays.uploadAsync(ctx->host_rays.data(), count, ctx->stream);
    sv->tracer.traceBatchMultiHit(
        ctx->d_rays.get(), ctx->d_multi_hits.get(), count, ctx->stream);

    /* 5. 标记异步状态 */
    ctx->async_pending = true;
    ctx->async_nrays   = nrays;

    return RES_OK;
    /* ← GPU 在 ctx->stream 上异步执行，CPU 立即返回 */
}
```

### 关键实现要点

1. **不调用 `cudaDeviceSynchronize`** — 这是与原 `batch_trace_impl` 最大的区别
2. **使用 `uploadAsync`** — `CudaBuffer::uploadAsync` 已存在 (buffer_manager.h L84)
3. **`traceBatchMultiHit` 传 stream** — 签名 `(Ray*, MultiHitResult*, uint, CUstream stream=0)` 已支持
4. **AoS→SoA 在 CPU 端完成** — 仍在同一线程，耗时约 0.086ms
5. **注意**: 此时 `m_batch_params_ptr` 仍是共享的 → P1 中修复

---

## Step 4: 实现 `batch_trace_wait_impl`

**文件**: `ox_s3d_scene_view.cpp`，紧接 `batch_trace_async_impl` 之后

### 伪代码

```cpp
static res_T batch_trace_wait_impl(
    s3d_scene_view* sv,
    s3d_batch_trace_context* ctx,
    const s3d_ray_request* requests,
    size_t nrays,
    s3d_hit* hits,
    s3d_batch_trace_stats* stats)
{
    if (stats) memset(stats, 0, sizeof(*stats));
    if (stats) stats->total_rays = nrays;

    /* 空场景 → 全 miss */
    if (!sv->has_geometry || nrays == 0) {
        for (size_t i = 0; i < nrays; i++) hits[i] = S3D_HIT_NULL;
        if (stats) stats->batch_accepted = nrays;
        ctx->async_pending = false;
        return RES_OK;
    }

    double t0 = now_ms();

    /* ---- Phase 1 完成: 等待 GPU + 下载 ---- */
    if (ctx->async_pending) {
        cudaStreamSynchronize(ctx->stream);           /* ← stream 同步，非 device */
        ctx->async_pending = false;
    }

    unsigned int count = static_cast<unsigned int>(nrays);
    ctx->host_mhits.resize(nrays);
    ctx->d_multi_hits.download(ctx->host_mhits.data(), count);

    double t1 = now_ms();
    if (stats) stats->batch_time_ms = t1 - t0;

    /* ---- Phase 2: CPU postprocess (与原版 L1242-1305 完全相同) ---- */
    std::vector<size_t> retrace_list;
    std::vector<float>  retrace_tmin;

    for (size_t i = 0; i < nrays; i++) {
        const MultiHitResult& mh = ctx->host_mhits[i];   /* 注意: 从 ctx 读取 */
        /* ... 与原 batch_trace_impl Phase 2 完全相同的 filter 逻辑 ... */
        /* ... 产出 retrace_list + retrace_tmin ... */
    }

    double t2 = now_ms();
    if (stats) stats->postprocess_time_ms = t2 - t1;

    /* ---- Phase 3: retrace (与原版 L1310-1430 相同，但使用 ctx 缓冲区) ---- */
    if (!retrace_list.empty()) {
        /* P0 阶段: 仍使用 sv->rt_retrace_rays (与原版相同)
         * P1 阶段: 改为使用 ctx->rt_d_rays (per-ctx 独立) */
        CudaBuffer<Ray>&            rt_buf  = sv->rt_retrace_rays;
        CudaBuffer<MultiHitResult>& rt_mhit = sv->rt_retrace_mhits;

        /* ... 与原 Phase 3 完全相同的迭代 retrace 逻辑 ... */
        /* 注意: 此处仍使用 cudaDeviceSynchronize(), P1 中改为 stream sync */
    }

    double t3 = now_ms();
    if (stats) stats->retrace_time_ms = t3 - t2;

    return RES_OK;
}
```

### 与原 `batch_trace_impl` 的差异一览

| 方面 | 原版 `batch_trace_impl` | `_wait_impl` |
|------|----------------------|--------------|
| Phase 1 GPU 调用 | 在函数内执行 | 已由 `_async` 完成 |
| Phase 1 同步 | `cudaDeviceSynchronize()` | `cudaStreamSynchronize(ctx->stream)` |
| mhits 来源 | 局部 `std::vector<MultiHitResult>` | `ctx->host_mhits` (预分配) |
| retrace 缓冲 | `sv->rt_retrace_rays` | P0: 同左; P1: `ctx->rt_d_rays` |
| retrace 同步 | `cudaDeviceSynchronize()` | P0: 同左; P1: `cudaStreamSynchronize` |

---

## Step 5: 暴露公共 API 包装

**文件**: `ox_s3d_scene_view.cpp`，在 `s3d_scene_view_trace_rays_batch_ctx` 实现附近

```cpp
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_async(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays)
{
    if (!scnview || !ctx) return RES_BAD;
    return batch_trace_async_impl(scnview, ctx, requests, nrays);
}

S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_wait(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats)
{
    if (!scnview || !ctx || !hits) return RES_BAD;
    return batch_trace_wait_impl(scnview, ctx, requests, nrays, hits, stats);
}
```

---

## Step 6: 保持原同步 API 兼容

原 `s3d_scene_view_trace_rays_batch_ctx` 不删除，改为调用 async + wait：

```cpp
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats)
{
    res_T rc = s3d_scene_view_trace_rays_batch_ctx_async(
        scnview, ctx, requests, nrays);
    if (rc != RES_OK) return rc;

    return s3d_scene_view_trace_rays_batch_ctx_wait(
        scnview, ctx, requests, nrays, hits, stats);
}
```

这确保所有现有调用方无需修改，且可验证 async+wait 路径的正确性。

---

## 验证清单

- [ ] 编译通过 (无新 warning)
- [ ] 原 `_batch_ctx` API 调用者行为不变
- [ ] porous 场景 320×320 spp=32 结果与修改前 bit-exact 一致
- [ ] 单步调试确认 `_async` 后 CPU 立即返回 (不阻塞在 GPU 上)
- [ ] `cudaStreamSynchronize` 替代 `cudaDeviceSynchronize` 后无 GPU 错误

## 已知限制 (P0 阶段)

- `m_batch_params_ptr` 仍共享 → 两个 ctx 不能并发调 `_async` → P1 修复
- retrace 仍用 `sv->` 成员 + `cudaDeviceSynchronize` → P1 修复
- 单池模式下 `_async` + `_wait` 等价于原同步调用，无性能提升 → P4 整合后生效
