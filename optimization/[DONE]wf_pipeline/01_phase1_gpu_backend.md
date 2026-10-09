# Phase 1: GPU 后端改造 — 消除内部串行瓶颈

**前置依赖**: 无  
**预计工时**: 3-4 天  
**验证方式**: 串行模式下 bit-exact 一致 + ctest 全通过

---

## 一、目标

将 GPU batch trace 从"同步阻塞调用"改造为"异步提交 + 等待"两步模式，为 Phase 3 的流水线化提供基础。同时消除每次调用的临时分配/释放开销。

### 当前问题一览

| # | 问题 | 位置 | 影响 |
|---|------|------|------|
| 1 | AoS→SoA 临时数组每次 `malloc/free` | `batch_trace.cpp:199-222` | ~0.1ms/call × 912K calls |
| 2 | `d_results` 每次 `cudaMallocAsync/cudaFreeAsync` | `cus3d_trace.cu:924,1093` | GPU 内存分配器争用 |
| 3 | instanced 场景的 `d_blas_array`+`d_instances` 每次重新分配+上传 | `cus3d_trace.cu:946-995` | 冗余 H2D 传输 |
| 4 | 3 次 `cudaStreamSynchronize` 阻塞 | `batch_trace.cpp:221` + `cus3d_trace.cu:1040,1066,1087` | 阻止 CPU-GPU 重叠 |
| 5 | `transfer_stream` 已声明但未使用 | `cus3d_device` | 无法 overlap H2D/D2H 与 kernel |
| 6 | `g_diag_*` 全局计数器数据竞争风险 | `batch_trace.cpp:140-149` | `BATCH_TRACE_DIAG=1` 时不安全 |

---

## 二、实施步骤

### Step 1.1: 预分配 AoS→SoA 主机缓冲

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

#### 当前代码

```cpp
// trace_rays_batch_impl() L199-222
{
    float3* h_origins    = (float3*)malloc(nrays * sizeof(float3));
    float3* h_directions = (float3*)malloc(nrays * sizeof(float3));
    float2* h_ranges     = (float2*)malloc(nrays * sizeof(float2));
    // ... AoS→SoA转换 + upload ...
    free(h_origins);
    free(h_directions);
    free(h_ranges);
}
```

#### 改造方案

在 `s3d_batch_trace_context` 中增加持久主机缓冲：

```cpp
struct s3d_batch_trace_context {
    struct cus3d_ray_batch          gpu_batch;
    struct cus3d_multi_hit_result*  h_multi_results;
    size_t                          max_rays;
    int                             initialized;
    
    /* === Phase 1.1: 持久 AoS→SoA 主机缓冲 === */
    float3*                         h_origins;      /* [max_rays] */
    float3*                         h_directions;   /* [max_rays] */
    float2*                         h_ranges;       /* [max_rays] */
};
```

#### 修改清单

| # | 函数 | 行号 | 修改内容 |
|---|------|------|---------|
| 1 | `s3d_batch_trace_context_create()` | L85-117 | 增加 `h_origins/h_directions/h_ranges` 的 `malloc` |
| 2 | `s3d_batch_trace_context_destroy()` | L119-126 | 增加 `free(h_origins)` 等 |
| 3 | `trace_rays_batch_impl()` | L199-222 | 删除临时 `malloc/free`，使用 `ctx->h_*` (需传入 ctx) |

#### 接口变更

`trace_rays_batch_impl()` 需要新增 `ctx` 参数（或从 `gpu_batch` 反查）：

```cpp
// 当前签名
static res_T trace_rays_batch_impl(
    struct s3d_scene_view* view,
    struct cus3d_ray_batch* gpu_batch,
    struct cus3d_multi_hit_result* h_multi_results,
    const struct s3d_ray_request* requests, size_t nrays,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats);

// 改造后签名 — 传入完整 context
static res_T trace_rays_batch_impl(
    struct s3d_scene_view* view,
    struct s3d_batch_trace_context* ctx,      /* <-- 替换 gpu_batch + h_multi_results */
    const struct s3d_ray_request* requests, size_t nrays,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats);
```

> **注意**: `s3d_scene_view_trace_rays_batch()` (非 ctx 版本，L455-489) 内部创建临时缓冲，此路径保持原有 `malloc/free` 行为——仅 ctx 版本受益。

#### 验证要点

- [x] `s3d_scene_view_trace_rays_batch_ctx()` 调用路径正常
- [x] 非 ctx 版本 (`s3d_scene_view_trace_rays_batch()`) 不受影响
- [x] `nrays <= ctx->max_rays` 断言

---

### Step 1.2: 预分配 GPU 结果缓冲

**文件**: `stardis-cus3d/custar-3d/0.10/src/cus3d_trace.cu`

#### 当前代码

```cuda
// cus3d_trace_ray_batch_multi() L924-925
struct cus3d_multi_hit_result* d_results = NULL;
TRACE_CUDA_CHECK(
    cudaMallocAsync(&d_results,
                    num_rays * sizeof(struct cus3d_multi_hit_result), s),
    "alloc d_results (batch_multi)");

// ... kernel + download ...

// L1093
cudaFreeAsync(d_results, s);
```

#### 改造方案

在 `cus3d_ray_batch` 中增加预分配的 `d_results`：

```cuda
struct cus3d_ray_batch {
    struct gpu_buffer_float3   d_origins;
    struct gpu_buffer_float3   d_directions;
    struct gpu_buffer_float2   d_ranges;
    struct gpu_buffer_uint32   d_ray_data_offsets;
    size_t                     count;
    
    /* === Phase 1.2: 预分配 GPU 结果缓冲 === */
    struct cus3d_multi_hit_result*  d_multi_results;   /* GPU端, [max_rays] */
    size_t                          max_rays;          /* 分配容量 */
};
```

#### 修改清单

| # | 函数 | 行号 | 修改内容 |
|---|------|------|---------|
| 1 | `cus3d_ray_batch_create()` | L673-717 | 增加 `cudaMalloc(&batch->d_multi_results, ...)` |
| 2 | `cus3d_ray_batch_destroy()` | L719-730 | 增加 `cudaFree(batch->d_multi_results)` |
| 3 | `cus3d_trace_ray_batch_multi()` | L898-1100 | 删除 per-call `cudaMallocAsync/cudaFreeAsync`，使用 `rays->d_multi_results` |

#### 实例数据预分配 (instanced 场景)

porous 场景使用 instanced 路径。每次 `cus3d_trace_ray_batch_multi()` 调用都重新：
1. `malloc` host `h_blas_array` + `h_instances` (L941-952)
2. 填充实例数据 (L958-982)
3. `cudaMallocAsync` device `d_blas_array` + `d_instances` (L984-998)
4. `cudaMemcpyAsync` H2D (L990-1000)
5. kernel 后 `cudaFreeAsync` (L1068-1069)

**优化**: 实例数据在 BVH 构建后不变，可在 `cus3d_bvh` 或 `cus3d_ray_batch` 上预分配一次：

```cuda
/* 在 cus3d_bvh 中新增（场景生命周期） */
struct cus3d_bvh {
    // ... existing fields ...
    
    /* === Phase 1.2b: 预分配实例GPU数据 === */
    cuBQL::BinaryBVH<float, 3>*  d_blas_array;      /* GPU端, [tlas_count] */
    struct instance_gpu_data*    d_instances;        /* GPU端, [tlas_count] */
    int                          instances_uploaded; /* 是否已上传 */
};
```

然后在 `cus3d_bvh_build()` 完成后一次性上传。`cus3d_trace_ray_batch_multi()` 直接引用 `bvh->d_blas_array`。

> **风险评估**: 修改 `cus3d_bvh` 结构需谨慎——确认场景不会在 solve 过程中重建 BVH。本项目场景静态，可安全预分配。

#### 验证要点

- [x] `max_rays` 一致性: `gpu_batch.max_rays` ≥ 每次调用的 `nrays`
- [x] `d_multi_results` 指针在多次 kernel launch 间有效
- [x] instanced 场景路径测试通过

---

### Step 1.3: 拆分 batch trace 为 submit + wait

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

这是 Phase 1 的**核心步骤**——将同步调用拆为异步提交和等待两部分。

#### 当前调用链

```
solve_camera_persistent_wavefront() [主循环 Step C]
  └─ s3d_scene_view_trace_rays_batch_ctx(view, ctx, requests, nrays, hits, &stats)
       └─ trace_rays_batch_impl(view, gpu_batch, h_multi_results, requests, nrays, hits, &stats)
            ├─ Step 1: AoS→SoA + upload + cudaStreamSynchronize  [阻塞 #1]
            ├─ Step 2: cus3d_trace_ray_batch_multi()               [阻塞 #2, #3]
            ├─ Step 3: CPU Top-K filter + hit fixup               [CPU串行]
            └─ Step 4: Fallback retrace                           [rare GPU调用]
```

#### 拆分设计

```
submit:
  ├─ AoS→SoA 转换 (CPU)
  ├─ gpu_buffer_*_upload H2D (异步)
  ├─ cudaEventRecord(upload_done, transfer_stream)     [Step 1.4]
  ├─ cudaStreamWaitEvent(stream, upload_done)           [Step 1.4]
  ├─ kernel launch (异步，不同步)
  ├─ cudaEventRecord(kernel_done, stream)
  ├─ cudaStreamWaitEvent(transfer_stream, kernel_done)  [Step 1.4]
  ├─ cudaMemcpyAsync D2H (transfer_stream)              [Step 1.4]
  └─ cudaEventRecord(download_done, transfer_stream)

wait:
  ├─ cudaEventSynchronize(download_done)    [唯一阻塞点]
  ├─ CPU Top-K filter + hit fixup           [CPU串行]
  └─ Fallback retrace                       [rare, 同步]
```

#### 新增 API

在 `s3d_scene_view_batch_trace.cpp` 和对应头文件中新增：

```c
/* ============ s3d.h 新增声明 ============ */

/* 异步提交光线追踪 — CPU不阻塞 */
res_T s3d_scene_view_trace_rays_batch_submit(
    struct s3d_scene_view*          view,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request*   requests,
    size_t                          nrays);

/* 等待GPU完成 + CPU后处理 */
res_T s3d_scene_view_trace_rays_batch_wait(
    struct s3d_scene_view*          view,
    struct s3d_batch_trace_context* ctx,
    struct s3d_hit*                 hits,
    size_t                          nrays,
    struct s3d_batch_trace_stats*   stats);
```

#### submit 实现

```cpp
res_T
s3d_scene_view_trace_rays_batch_submit(
    struct s3d_scene_view* view,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests,
    size_t nrays)
{
    if(!view || !ctx || !requests) return RES_BAD_ARG;
    if(nrays == 0) return RES_OK;
    if(nrays > ctx->max_rays) return RES_BAD_ARG;
    if(!(view->mask & S3D_TRACE)) return RES_BAD_ARG;
    
    struct cus3d_device* dev = view->scn->dev->gpu;
    struct cus3d_ray_batch* gpu_batch = &ctx->gpu_batch;
    size_t i;
    
    /* Step 1: AoS→SoA 转换（使用预分配缓冲） */
    for(i = 0; i < nrays; i++) {
        ctx->h_origins[i]    = make_float3(requests[i].origin[0],
                                           requests[i].origin[1],
                                           requests[i].origin[2]);
        ctx->h_directions[i] = make_float3(requests[i].direction[0],
                                           requests[i].direction[1],
                                           requests[i].direction[2]);
        ctx->h_ranges[i]     = make_float2(requests[i].range[0],
                                           requests[i].range[1]);
    }
    
    /* Step 2: H2D 上传 (异步) */
    cudaStream_t upload_stream = dev->transfer_stream;  /* Phase 1.4 */
    gpu_buffer_float3_upload(&gpu_batch->d_origins, ctx->h_origins, nrays, upload_stream);
    gpu_buffer_float3_upload(&gpu_batch->d_directions, ctx->h_directions, nrays, upload_stream);
    gpu_buffer_float2_upload(&gpu_batch->d_ranges, ctx->h_ranges, nrays, upload_stream);
    gpu_batch->count = nrays;
    
    /* Step 3: 用 event 确保上传完成后再 kernel */
    cudaEventRecord(ctx->evt_upload_done, upload_stream);
    cudaStreamWaitEvent(dev->stream, ctx->evt_upload_done, 0);
    
    /* Step 4: Kernel launch (异步，不同步) */
    res_T res = cus3d_trace_ray_batch_multi_async(
        view->bvh, view->geom_store, dev, gpu_batch,
        CUS3D_MAX_MULTI_HITS,
        gpu_batch->d_multi_results);   /* 直接写入预分配 GPU 缓冲 */
    if(res != RES_OK) return res;
    
    /* Step 5: Kernel 完成后 D2H 下载 */
    cudaEventRecord(ctx->evt_kernel_done, dev->stream);
    cudaStreamWaitEvent(upload_stream, ctx->evt_kernel_done, 0);
    
    cudaMemcpyAsync(ctx->h_multi_results,
                    gpu_batch->d_multi_results,
                    nrays * sizeof(struct cus3d_multi_hit_result),
                    cudaMemcpyDeviceToHost,
                    upload_stream);
    
    cudaEventRecord(ctx->evt_download_done, upload_stream);
    
    /* 保存 nrays 供 wait 使用 */
    ctx->pending_nrays = nrays;
    ctx->has_pending   = 1;
    
    return RES_OK;
}
```

#### wait 实现

```cpp
res_T
s3d_scene_view_trace_rays_batch_wait(
    struct s3d_scene_view* view,
    struct s3d_batch_trace_context* ctx,
    struct s3d_hit* hits,
    size_t nrays,
    struct s3d_batch_trace_stats* stats)
{
    if(!view || !ctx || !hits) return RES_BAD_ARG;
    if(!ctx->has_pending) return RES_OK;
    assert(nrays == ctx->pending_nrays);
    
    double t0 = stats ? get_time_ms() : 0;
    
    /* 唯一阻塞点: 等待 D2H 完成 */
    cudaEventSynchronize(ctx->evt_download_done);
    
    double t1 = stats ? get_time_ms() : 0;
    if(stats) stats->batch_time_ms = t1 - t0;
    
    /* CPU 后处理: Top-K filter + hit fixup (与当前 Step 3 完全相同) */
    t0 = stats ? get_time_ms() : 0;
    
    // ... 完全复用当前 trace_rays_batch_impl() 的 Step 3 代码 ...
    // ... 完全复用当前 trace_rays_batch_impl() 的 Step 4 retrace 代码 ...
    
    t1 = stats ? get_time_ms() : 0;
    if(stats) stats->postprocess_time_ms = t1 - t0;
    
    ctx->has_pending = 0;
    return RES_OK;
}
```

#### context 结构新增字段

```cpp
struct s3d_batch_trace_context {
    /* --- 现有字段 --- */
    struct cus3d_ray_batch          gpu_batch;
    struct cus3d_multi_hit_result*  h_multi_results;
    size_t                          max_rays;
    int                             initialized;
    
    /* === Phase 1.1: 预分配主机缓冲 === */
    float3*                         h_origins;
    float3*                         h_directions;
    float2*                         h_ranges;
    
    /* === Phase 1.3: 异步提交/等待状态 === */
    cudaEvent_t                     evt_upload_done;
    cudaEvent_t                     evt_kernel_done;
    cudaEvent_t                     evt_download_done;
    size_t                          pending_nrays;
    int                             has_pending;     /* 1 = GPU有inflight工作 */
};
```

#### cus3d_trace_ray_batch_multi 异步版本

需要在 `cus3d_trace.cu` 中新增一个不做 sync 的变体：

```cuda
/* 仅 launch kernel + 不做 sync + 不做 D2H + 不做 free
 * d_results 由调用者预分配 */
res_T
cus3d_trace_ray_batch_multi_async(
    const struct cus3d_bvh* bvh,
    const struct cus3d_geom_store* store,
    struct cus3d_device* dev,
    const struct cus3d_ray_batch* rays,
    int max_hits,
    struct cus3d_multi_hit_result* d_results)  /* 调用者提供的GPU缓冲 */
{
    // ... 参数检查 ...
    cudaStream_t s = dev->stream;
    uint32_t num_rays = (uint32_t)rays->count;
    const uint32_t block_size = 256;
    uint32_t grid_size = (num_rays + block_size - 1) / block_size;
    
    if (bvh->tlas_valid && bvh->tlas_count > 0) {
        /* instanced: 使用预分配的 bvh->d_blas_array, bvh->d_instances */
        trace_rays_instanced_topk_kernel<<<grid_size, block_size, 0, s>>>(
            bvh->tlas,
            bvh->d_blas_array,     /* Phase 1.2b 预分配 */
            bvh->d_instances,      /* Phase 1.2b 预分配 */
            (uint32_t)bvh->tlas_count,
            store->d_vertices.data,
            store->d_indices.data,
            store->d_spheres,
            store->d_prim_to_geom.data,
            store->d_geom_entries,
            store->total_tris,
            rays->d_origins.data,
            rays->d_directions.data,
            rays->d_ranges.data,
            num_rays,
            max_hits,
            d_results);
    } else {
        /* single-level */
        trace_rays_topk_kernel<<<grid_size, block_size, 0, s>>>(
            bvh->bvh,
            store->d_vertices.data,
            store->d_indices.data,
            store->d_spheres,
            store->d_prim_to_geom.data,
            store->d_geom_entries,
            store->total_tris,
            rays->d_origins.data,
            rays->d_directions.data,
            rays->d_ranges.data,
            num_rays,
            max_hits,
            d_results);
    }
    
    TRACE_CUDA_CHECK(cudaGetLastError(), "launch topk kernel (async)");
    /* 不做 cudaStreamSynchronize — 异步返回 */
    return RES_OK;
}
```

#### 文件修改清单

| # | 文件 | 修改 |
|---|------|------|
| 1 | `cus3d_trace.h` | 新增 `cus3d_trace_ray_batch_multi_async()` 声明 + `d_multi_results`/`max_rays` 到 `cus3d_ray_batch` |
| 2 | `cus3d_trace.cu` | 实现 `cus3d_trace_ray_batch_multi_async()` + `cus3d_ray_batch_create()` 分配 `d_multi_results` |
| 3 | `s3d_scene_view_batch_trace.cpp` | 实现 `submit/wait` + context 扩展 + `trace_rays_batch_impl` 签名改为用 ctx |
| 4 | `s3d.h` 或相应头文件 | 新增 `submit/wait` 公共声明 |

#### 兼容性

**保留原有同步API**。`s3d_scene_view_trace_rays_batch_ctx()` 改为内部调用 `submit + wait`：

```cpp
res_T
s3d_scene_view_trace_rays_batch_ctx(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests,
    size_t nrays,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats)
{
    res_T res = s3d_scene_view_trace_rays_batch_submit(scnview, ctx, requests, nrays);
    if(res != RES_OK) return res;
    return s3d_scene_view_trace_rays_batch_wait(scnview, ctx, hits, nrays, stats);
}
```

这保证 Phase 1 完成后，**Phase 2/3 不实施也能正常工作** — 串行模式完全不受影响。

---

### Step 1.4: 利用 transfer_stream 实现传输-计算重叠

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

#### 当前状况

`cus3d_device` 中已声明 `transfer_stream`，但所有操作都使用 `dev->stream`：

```
当前:  stream -> [H2D][sync][kernel][sync][D2H][sync]  (纯串行)

目标:  transfer_stream -> [H2D] ─event─> stream -> [kernel] ─event─> transfer_stream -> [D2H]
                                         ↕ overlap ↕              ↕ overlap ↕
```

#### CUDA Event 使用

```
GPU timeline:
  transfer_stream: ──[H2D upload]──evt_upload_done──────────────[D2H download]──evt_download_done──
                                       │                            ↑
  stream:          ──────────waitEvent──[kernel]──evt_kernel_done───│──
```

3 个 `cudaEvent_t` 需要在 `s3d_batch_trace_context` 中创建：

```cpp
// s3d_batch_trace_context_create() 中:
cudaEventCreateWithFlags(&ctx->evt_upload_done, cudaEventDisableTiming);
cudaEventCreateWithFlags(&ctx->evt_kernel_done, cudaEventDisableTiming);
cudaEventCreateWithFlags(&ctx->evt_download_done, cudaEventDisableTiming);
```

> **`cudaEventDisableTiming`** — 禁用计时功能，减少 event 开销。如需在诊断模式计时，可条件创建。

#### 前置条件

确认 `dev->transfer_stream` 已在 `cus3d_device_create()` 中创建。如果未创建：

```cuda
// cus3d_device.cu 中:
cudaStreamCreate(&dev->transfer_stream);
```

#### 预期收益

- [x] H2D 传输 (~0.1ms) 与前一个 kernel 尾部重叠
- [x] D2H 传输 (~0.05ms) 与 kernel 计算重叠
- [x] 总体减少 ~0.15ms/call × 912K calls ≈ **~137s 节省**（理论上限）
- 实际取决于传输与 kernel 的时间比，预计 **5-10% GPU 阶段加速**

---

### Step 1.5: Enclosure batch submit/wait 预留

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_find_enclosure.cpp`

#### 设计

与 ray trace 相同模式，新增：

```c
res_T s3d_scene_view_find_enclosure_batch_submit(
    struct s3d_scene_view* view,
    struct s3d_batch_enc_context* ctx,
    const struct s3d_enc_locate_request* requests,
    size_t nqueries);

res_T s3d_scene_view_find_enclosure_batch_wait(
    struct s3d_scene_view* view,
    struct s3d_batch_enc_context* ctx,
    struct s3d_enc_locate_result* results,
    size_t nqueries,
    struct s3d_batch_enc_stats* stats);
```

#### 实施优先级

**Phase 1 中仅声明接口，初始实现为同步调用**：

```cpp
res_T s3d_scene_view_find_enclosure_batch_submit(...) {
    /* Phase 1: 同步实现 — 后续可改为异步 */
    ctx->pending_nqueries = nqueries;
    ctx->has_pending = 1;
    // 实际 GPU 工作延迟到 wait 中
    return RES_OK;
}

res_T s3d_scene_view_find_enclosure_batch_wait(...) {
    /* Phase 1: 实际执行全部工作 */
    return find_enclosure_batch_impl(view, &ctx->gpu_batch, ctx->h_results,
                                     ctx->pending_requests, ctx->pending_nqueries,
                                     results, stats);
}
```

**理由**: ENC 查询频率远低于 ray trace（仅特定 path 状态触发），异步化收益有限，可后续按需改造。

---

### Step 1.6: 消除全局诊断计数器竞争

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

#### 当前代码

```cpp
// L140-149
static size_t g_diag_batch_calls       = 0;
static size_t g_diag_total_rays        = 0;
static size_t g_diag_total_accepted0   = 0;
// ... 共 10 个 static 计数器
static size_t g_diag_accept_hist[CUS3D_MAX_MULTI_HITS] = {0};
```

#### 方案选择

| 方案 | 优点 | 缺点 |
|------|------|------|
| **A: 移入 ctx** | 无竞争、per-context 隔离 | 需要在公共 API 中暴露 |
| **B: `_Atomic`** | 最小改动 | C11 原子操作，可能有 MSVC 兼容问题 |
| **C: 仅在 `BATCH_TRACE_DIAG=1` 时 #warning** | 零改动 | 不解决问题 |

**推荐方案 A**: 将计数器移入 `s3d_batch_trace_context`：

```cpp
struct s3d_batch_trace_context {
    // ... 其他字段 ...
    
    /* === Phase 1.6: per-context 诊断计数器 === */
#if BATCH_TRACE_DIAG
    size_t diag_batch_calls;
    size_t diag_total_rays;
    size_t diag_total_accepted0;
    size_t diag_total_topk_saved;
    size_t diag_total_all_reject;
    size_t diag_total_miss;
    size_t diag_total_no_filter;
    size_t diag_total_retrace_ok;
    size_t diag_total_retrace_miss;
    size_t diag_accept_hist[CUS3D_MAX_MULTI_HITS];
#endif
};
```

`calloc` 初始化自动清零。输出报告在 `wait` 中用 `ctx->diag_*` 替换 `g_diag_*`。

---

## 三、文件修改总表

| # | 文件 | 修改范围 | 新增行数 | 步骤 |
|---|------|---------|---------|------|
| 1 | `cus3d_trace.h` | 结构体 + 新函数声明 | ~15 | 1.2, 1.3 |
| 2 | `cus3d_trace.cu` | `ray_batch_create/destroy` + 新增 `_async` | ~80 | 1.2, 1.3 |
| 3 | `s3d_scene_view_batch_trace.cpp` | context 扩展 + submit/wait + 重构 impl | ~200 | 1.1, 1.3, 1.4, 1.6 |
| 4 | `s3d.h` (或对应头) | 新增 submit/wait 声明 | ~20 | 1.3, 1.5 |
| 5 | `s3d_scene_view_find_enclosure.cpp` | submit/wait 桩实现 | ~40 | 1.5 |
| 6 | `cus3d_bvh.h` + `cus3d_bvh.cu` (如存在) | 实例数据预分配 | ~50 | 1.2b |
| 7 | `cus3d_device.cu` | 确保 `transfer_stream` 已创建 | ~5 | 1.4 |

**预计总新增/修改: ~410 行**

---

## 四、实施顺序与检查点

```
Step 1.1 (预分配主机缓冲)
  ├─ 修改 context 结构体 + create/destroy
  ├─ 修改 trace_rays_batch_impl 签名
  └─ ✅ 检查点: ctest 全通过, 结果 bit-exact

Step 1.2 (预分配GPU结果缓冲)
  ├─ 修改 cus3d_ray_batch + create/destroy
  ├─ 修改 cus3d_trace_ray_batch_multi() 消除 per-call alloc
  ├─ (1.2b) 实例数据预分配（如时间允许）
  └─ ✅ 检查点: ctest 全通过, 结果 bit-exact

Step 1.3 (submit + wait 拆分)  ← 核心步骤
  ├─ 新增 cus3d_trace_ray_batch_multi_async()
  ├─ 实现 s3d submit/wait 函数
  ├─ 重写 s3d_scene_view_trace_rays_batch_ctx() 为 submit+wait
  └─ ✅ 检查点: ctest 全通过, 结果 bit-exact

Step 1.4 (transfer_stream)
  ├─ 确认 dev->transfer_stream 已创建
  ├─ 创建 cudaEvent_t × 3
  ├─ submit 中使用 transfer_stream + event 依赖
  └─ ✅ 检查点: ctest 全通过 + Nsight Systems 确认传输-计算重叠

Step 1.5 (ENC batch submit/wait 桩)
  ├─ 声明接口
  ├─ 同步桩实现
  └─ ✅ 检查点: 接口可用, ctest 通过

Step 1.6 (诊断计数器迁移)
  ├─ 移入 context
  ├─ 更新输出代码
  └─ ✅ 检查点: BATCH_TRACE_DIAG=1 正常输出
```

---

## 五、风险与注意事项

| 风险 | 缓解 |
|------|------|
| `transfer_stream` 未初始化 | 检查 `cus3d_device_create()`, 必要时添加 |
| `d_multi_results` 生命周期与 kernel 不匹配 | `max_rays` ≥ 所有调用的 `nrays`, 在 context 生命周期内有效 |
| Fallback retrace (Step 4) 在 wait 中仍调用同步 GPU | 保持原行为，retrace 极少 (<0.01% rays) |
| instanced 场景 BLAS 指针在 kernel 执行期间失效 | BVH 在 solve 期间不重建 — 安全 |
| `s3d_scene_view_trace_rays_batch()` (非 ctx) 路径不受影响 | 保持原有 malloc/free 行为 |

---

*Phase 2 详见 → [02_phase2_double_buffer.md](02_phase2_double_buffer.md)*
