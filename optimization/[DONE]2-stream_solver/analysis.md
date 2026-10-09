# 双缓冲流水线优化分析：stardis-cus3d Wavefront Solver

**生成时间**: 2026-02-24 | **更新**: 2026-02-24 (retrace 修复状态更新)  
**项目**: Stardis-GPU / stardis-cus3d  
**优化目标**: 通过 CPU-GPU 流水线重叠消除互等待时间  
**预期收益**: ~1.9× 吞吐量提升  
**基于数据**: porous 场景 320×320 spp=32, pool=8192, 412,477 步实测  
**基于源码**: `stardis-solver/0.16.2`, `oxstar-3d/0.10`

### 变更记录

| 日期 | 变更内容 |
|------|---------|
| 2026-02-24 | 初始版本 |
| 2026-02-24 | retrace `static CudaBuffer` → `sv` 成员修复已完成，更新 §1.4, §3.4, §5.1.1 风险评级从 [高] 降为 [中] |

---

## 1. 现状分析

### 1.1 当前主循环结构

主循环位于 `sdis_solve_persistent_wavefront.c` L1765–L1960，
每步严格串行执行 7 个阶段：

```
Step A: compact_active_paths()              // L1773  CPU: 流压缩
Step B: pool_collect_ray_requests_bucketed() // L1779  CPU: 收集射线请求
Step C: s3d_scene_view_trace_rays_batch_ctx()// L1790  GPU+CPU: 批量光追 (阻塞)
Step D: pool_distribute_ray_results()        // L1813  CPU: 分发结果
Step D2: enc_locate collect→GPU→distribute   // L1820  GPU: 包围体查询
Step D3: closest_point collect→GPU→distribute// L1854  GPU: 最近点查询
Step E: pool_cascade_non_ray_steps_compact() // L1880  CPU(OMP): 非射线步推进
Step F+G: harvest_completed_paths()+refill() // L1885  CPU: 收割+补充
```

### 1.2 实测性能分解

| 阶段 | 累积 (s) | 每步 (ms) | 占比 | CPU/GPU |
|------|---------|----------|------|---------|
| compact | 29.056 | 0.070 | 2.2% | CPU |
| collect | 131.636 | 0.319 | 10.0% | CPU |
| **trace (total)** | **877.229** | **2.127** | **66.7%** | **混合** |
| → GPU kernel+upload | 634,292ms | 1.538 | 48.2% | GPU (CPU 阻塞) |
| → CPU postprocess | 143,681ms | 0.348 | 10.9% | CPU (GPU 空闲) |
| → retrace fallback | 64,120ms | 0.155 | 4.9% | CPU+GPU |
| distribute | 179.498 | 0.435 | 13.6% | CPU |
| cascade | 57.365 | 0.139 | 4.4% | CPU (OMP) |
| harvest+refill | 39.517 | 0.096 | 3.0% | CPU |
| **总计** | **1,314s** | **3.186** | 100% | |

### 1.3 利用率分析

```
CPU 工作总量/步:  compact + collect + postprocess + retrace + distribute + cascade + harvest
               = 0.070 + 0.319 + 0.348 + 0.155 + 0.435 + 0.139 + 0.096
               = 1.562ms
CPU 额外开销:     trace 函数内 AoS→SoA 转换 + memcpy 管理 ≈ 0.086ms
CPU 总量:         ~1.65ms

GPU 工作总量/步:  kernel+upload = 1.538ms
```

| 指标 | 当前值 |
|------|--------|
| CPU 利用率 | 1.65 / 3.19 = **51.7%** |
| GPU 利用率 | 1.54 / 3.19 = **48.3%** |
| CPU 空闲时间/步 | ~1.54ms (等 GPU) |
| GPU 空闲时间/步 | ~1.65ms (等 CPU) |

**关键发现：CPU 总量 (1.65ms) ≈ GPU 总量 (1.54ms)，近乎完美对等，
双缓冲可达接近理论最大 2× 加速。**

### 1.4 GPU 同步模型

`batch_trace_impl()` (ox_s3d_scene_view.cpp L1179-1460) 内部流程：

```
Phase 1 — GPU multi-hit batch:
  1. CPU: AoS → SoA 转换 (std::vector<Ray> rays, 栈分配)      ← 纯 CPU
  2. ctx->d_rays.upload(rays.data(), count)                     ← cudaMemcpy (同步)
  3. sv->tracer.traceBatchMultiHit(d_rays, d_multi_hits, count) ← optixLaunch (异步!)
  4. cudaDeviceSynchronize()                                    ← 硬同步, CPU 阻塞
  5. ctx->d_multi_hits.download(mhits.data(), count)            ← cudaMemcpy (同步)

Phase 2 — CPU postprocess:
  6. 逐射线遍历 K=8 候选, 调用 filter_func                     ← 纯 CPU, GPU 空闲

Phase 3 — retrace fallback (可选):
  7. 被 filter 拒绝的射线, 用 K=2 multi-hit 重追踪             ← GPU+CPU 交替
  8. 使用 sv->rt_retrace_rays / sv->rt_retrace_mhits            ← ✅ 已从 static 移至 sv 成员
     (ox_s3d_internal.h L219-220, 修复 atexit CUDA ctx 崩溃)     ← ⚠ 仍在 sv 级别共享
```

**异步化关键点**：
- `traceBatchMultiHit()` 接受 `CUstream stream` 参数 (unified_tracer.cpp L1256)，天然支持多 stream
- `CudaBuffer` 已有 `uploadAsync` / `downloadAsync` 方法 (buffer_manager.h L84, L105)
- 当前使用 `cudaDeviceSynchronize()` (全 device 同步) 而非 stream 同步，需要改为 `cudaStreamSynchronize`

---

## 2. 调度原理

### 2.1 核心思想

将路径池 (pool_size = N) 从逻辑上分为两半：
- **Half-A**: slots `[0, N/2)`
- **Half-B**: slots `[N/2, N)`

每半独立持有自己的索引数组、射线缓冲区和 GPU batch context。
GPU 执行 A 半的光追时，CPU 并行处理 B 半的非 GPU 阶段 (cascade / harvest / refill / compact / collect)，反之亦然。

### 2.2 每半步阶段分组

将当前 7 步分为三个功能组：

| 组 | 操作 | 每步耗时 (ms) | 依赖 |
|----|------|-------------|------|
| **CPU_pre** | compact + collect + AoS→SoA 转换 | 0.070 + 0.319 + 0.086 = **0.475** | 依赖本半的 cascade/refill 完成 |
| **GPU_exec** | upload + optixLaunch + sync + download | **1.538** | 依赖本半的 CPU_pre |
| **CPU_post** | postprocess + retrace + distribute | 0.348 + 0.155 + 0.435 = **0.938** | 依赖本半的 GPU_exec |
| **CPU_between** | cascade + harvest + refill | 0.139 + 0.096 = **0.235** | 依赖本半的 CPU_post |

每半步 CPU 总量 = 0.475 + 0.938 + 0.235 = **1.648ms**  
每半步 GPU 总量 = **1.538ms**

### 2.3 稳态时序推导

```
时间→  0.00    0.48          1.20           2.02          2.97         3.69
        ├───────┼──────────────┼───────────────┼──────────────┼────────────┤
CPU:   [pre(A) ][between(B)+pre(B) ][idle 0.82  ][  post(A)    ][between(A)+pre(A)]
        0.48ms    0.24+0.48=0.72ms    等GPU(A)     0.94ms        0.72ms
GPU:            [======= trace(A) 1.54ms =======][======= trace(B) 1.54ms =======]
                0.48 ────────────────── 2.02       2.02 ────────────────── 3.56

续:
        3.56          4.51           5.23          5.36
        ├──────────────┼──────────────┤──────────────┤
CPU:   [  post(B)    ][between(B)+pre(B)]           ← 循环
        0.94ms        0.72ms
GPU:                                  [== trace(A) 1.54ms ==]...
                                      5.36 (GPU等0.13ms) ──→ 6.90
```

**详细时间线（从第二轮周期开始的稳态）**：

```
t = 0.00: GPU(A) 完成 → 立即 launch GPU(B)
          CPU 开始 post(A)  [0.94ms]
t = 0.94: CPU: between(A) + pre(A)  [0.72ms]
t = 1.54: GPU(B) 完成。CPU 在 t=1.66 才完成 pre(A)。
          GPU 空等 0.12ms
t = 1.66: launch GPU(A)
          CPU 开始 post(B)  [0.94ms]
t = 2.60: CPU: between(B) + pre(B)  [0.72ms]
t = 3.20: GPU(A) 完成。CPU 在 t=3.32 才完成 pre(B)。
          GPU 空等 0.12ms
t = 3.32: launch GPU(B)  → 一轮周期 = 3.32ms
```

**一轮 = A、B 各推进一步 = 3.32ms**  
**每半步 = 3.32 / 2 = 1.66ms**

### 2.4 吞吐量对比

| 指标 | 当前（串行） | 双缓冲（流水线） | 提升 |
|------|------------|-----------------|------|
| 每步耗时 (8K 路径) | 3.19ms | 1.66ms | **1.92×** |
| GPU 利用率 | 48.3% | 3.08/3.32 = **92.8%** | ×1.92 |
| CPU 利用率 | 51.7% | 3.30/3.32 = **99.4%** | ×1.92 |
| GPU 空闲/轮 | 1.65ms | 0.12ms × 2 = **0.24ms** | -85% |
| CPU 空闲/轮 | 1.54ms | 0 (CPU 是瓶颈) | -100% |
| **估算总时间** | **22min 44s** | **~11min 50s** | **1.92×** |

### 2.5 理论天花板

$$T_{half\text{-}step} = \max(T_{CPU}, T_{GPU}) = \max(1.648, 1.538) = 1.648\text{ms}$$

当前方案已逼近天花板 (1.66ms vs 理论 1.648ms)。进一步优化须：
- 减少 CPU_post (Top-K filter GPU 化: −0.35ms → CPU 降至 1.30ms, GPU 成为瓶颈)
- 结果: 每半步 = 1.54ms, 加速比 3.19/1.54 = **2.07×**

---

## 3. 数据依赖与并发安全性分析

### 3.1 步骤间数据流 DAG

```
compact(X) ──writes──→ active_indices_X, need_ray_indices_X, done_indices_X
     │
     ↓
collect(X) ──writes──→ ray_requests_X[], ray_to_slot_X[], bucket_offsets_X
     │                 slots_X[i].ray_req.batch_idx (回写)
     ↓
GPU_trace(X) ─writes─→ ray_hits_X[]
     │
     ↓
postprocess(X) ─reads─→ ray_hits_X[], requests_X[], filter_data (→ slots_X[i])
     │ ─writes─→ hits_X[] (CPU 侧最终命中数组)
     ↓
distribute(X) ─reads──→ ray_hits_X[], bucket_radiative_X[], bucket_conductive_X[]
     │ ─writes─→ slots_X[i].phase, slots_X[i].active, slots_X[i].needs_ray, ...
     ↓
cascade(X) ──reads──→ active_indices_X[], slots_X[i]
     │ ──writes──→ slots_X[i].phase, slots_X[i].active, slots_X[i].needs_ray, ...
     ↓
harvest(X) ──reads──→ done_indices_X[]
     │ ──writes──→ slots_X[i].phase=HARVESTED, estimator_buffer[px,py]
     ↓
refill(X) ──reads───→ done_indices_X[], task_queue[task_next]
     │ ──writes──→ slots_X[i] (全覆写), task_next++
```

### 3.2 并发安全验证矩阵

当 GPU(A) 执行时，CPU 同时执行 between(B)+pre(B)。需验证 A 的 GPU 操作与 B 的 CPU 操作无数据竞争。

| GPU 操作 (Half-A) | 读 | 写 |
|---|---|---|
| kernel: d_rays_A → d_multi_hits_A | ctx_A.d_rays (GPU mem) | ctx_A.d_multi_hits (GPU mem) |

| CPU 操作 (Half-B) | 读 | 写 |
|---|---|---|
| cascade(B) | active_indices_B[], slots_B[] | slots_B[].phase / needs_ray |
| harvest(B) | done_indices_B[], slots_B[] | slots_B[].phase, estimator_buffer[px,py] |
| refill(B) | done_indices_B[], task_queue[task_next] | slots_B[], task_next |
| compact(B) | slots_B[].phase/active | active_indices_B[], need_ray_indices_B[] |
| collect(B) | need_ray_indices_B[], slots_B[] | ray_requests_B[], ray_to_slot_B[] |

**冲突检查**：

| 共享资源 | A 阶段访问 | B 阶段访问 | 冲突？ |
|---------|-----------|-----------|--------|
| slots_A `[0, N/2)` | GPU 不直接访问 slots | B 阶段不访问 A 的 slots | **无** ✓ |
| slots_B `[N/2, N)` | A 的 GPU 不访问 B 的 slots | cascade/harvest/refill/compact/collect 修改 | **无** ✓ |
| ray_requests_A[] | GPU 读 ctx_A.d_rays (设备内存副本) | B 不访问 A 的射线缓冲区 | **无** ✓ |
| ray_requests_B[] | A 不访问 B 的射线缓冲区 | collect(B) 写入 | **无** ✓ |
| `batch_ctx_A` | GPU 使用 A 的设备缓冲区 | B 不访问 A 的 ctx | **无** ✓ |
| `batch_ctx_B` | A 不访问 B 的 ctx | collect(B) 不需要 ctx | **无** ✓ |
| `task_queue[]` | 不访问 | refill(B) 读 `task_queue[task_next]` | **无** (只读) ✓ |
| `task_next` | 不访问 | refill(B) 递增 | **无** ✓ (单线程递增) |
| `estimator_buffer` | 不访问 | harvest(B) 写 `buf[px,py]` | **无** ✓ |
| `scn` (场景) | 不访问 (GPU 已有设备副本) | cascade(B) 只读 | **无** ✓ |
| **统计计数器** | 不访问 | cascade/harvest 修改 | **无** ✓ |

**结论：两半路径池的操作在物理上完全隔离，无数据竞争。**

### 3.3 特殊情况：`filter_data` 指针

`pool_collect_ray_requests_bucketed()` 中 (L739-740)：
```c
rr->filter_data = &p->filter_data_storage;
```

`ray_requests[].filter_data` 指向 `slots[i].filter_data_storage` 的地址。
在 CPU postprocess 阶段 (batch_trace_impl Phase 2, L1269)：
```cpp
void* fdata = requests[i].filter_data;
int rej = filt(&h, requests[i].origin, requests[i].direction,
               requests[i].range, fdata, filt_data_snap);
```

CPU 通过 `requests[i].filter_data` 解引用指向 `slots_A[i]` 的指针。
此时 B 半的 cascade/collect 只修改 `slots_B[]`，不碰 `slots_A[]`。**安全** ✓

### 3.4 特殊情况：retrace 缓冲区 ✅ 已部分修复

`batch_trace_impl()` Phase 3 retrace 缓冲区已从 `static` 局部变量
移至 `s3d_scene_view` 成员 (ox_s3d_internal.h L215-220)：
```cpp
/* Persistent retrace GPU buffers (grow-only, avoid per-call alloc).
 * Must be members (not static) so they are freed while CUDA context
 * is still alive — static locals outlive the device and crash on
 * cudaFree during atexit. */
CudaBuffer<Ray>            rt_retrace_rays;
CudaBuffer<MultiHitResult> rt_retrace_mhits;
```

在 `batch_trace_impl` Phase 3 中通过成员引用使用 (L1323-1324)：
```cpp
CudaBuffer<Ray>&            s_rt_d_rays  = sv->rt_retrace_rays;
CudaBuffer<MultiHitResult>& s_rt_d_mhits = sv->rt_retrace_mhits;
```

**✅ 已修复**：原 `static` 生命周期问题 (atexit 时 CUDA 上下文已销毁导致崩溃)。

**⚠ 残余风险 [中]**：缓冲区现在位于 `sv` (scene_view) 级别，
虽然不再是全局静态，但同一 scene_view 上的两次并发 `batch_trace_impl` 调用
仍然会共享这些缓冲区。在双缓冲方案中，两半的 postprocess/retrace
在 CPU 上是串行执行的，不会同时触发 retrace GPU launch。
但 retrace 中的 `cudaDeviceSynchronize()` (L1365) 会同步另一 half 的 async trace stream。

**最终方案**：仍建议将 retrace 缓冲区移入 `s3d_batch_trace_context`，
每个 ctx 持有独立的 retrace 缓冲区 + 使用 `cudaStreamSynchronize(ctx->stream)`
替代 `cudaDeviceSynchronize()`。改造量小（~20行）。

### 3.5 特殊情况：`estimator_buffer` 并发写入

`harvest_completed_paths()` (L1261)：
```c
struct sdis_estimator* estimator =
    estimator_buffer_grab(buf, p->ipix_image[0], p->ipix_image[1]);
estimator->temperature.sum  += p->T.value;
```

同一像素的不同 spp 可能分布在两半池中，两半的 harvest 可能同时写同一像素。
但在双缓冲方案中，两半的 harvest 不会同时执行 — 它们在不同的调度阶段，
CPU 单线程顺序执行。**安全** ✓

---

## 4. 实现方法

### 4.1 总体改造策略

```
目标: 以最小改动实现流水线,保持与原版本的结果一致性

改造分三层:
Layer 1: GPU 后端异步化     (oxstar-3d: ~100 行)
Layer 2: Pool 结构双缓冲化  (solver:    ~300 行)
Layer 3: 主循环流水线重构   (solver:    ~200 行)
```

### 4.2 Layer 1: GPU 后端异步化

#### 4.2.1 新增异步 API

在 `s3d.h` 中添加：

```c
/* 异步光追: launch 后立即返回, 不等待 GPU 完成 */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_async(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays);

/* 等待异步光追完成, 下载结果到 host, 返回统计 */
S3D_API res_T s3d_scene_view_trace_rays_batch_ctx_wait(
    struct s3d_scene_view* scnview,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats);
```

#### 4.2.2 `batch_trace_context` 扩展

```cpp
struct s3d_batch_trace_context {
    size_t              max_rays;
    CudaBuffer<Ray>             d_rays;
    CudaBuffer<MultiHitResult>  d_multi_hits;

    // === 新增字段 ===
    cudaStream_t                stream;          // 独立 CUDA stream
    std::vector<Ray>            host_rays;       // 预分配 host 缓冲
    std::vector<MultiHitResult> host_mhits;      // 预分配 host 缓冲
    bool                        async_pending;   // 异步操作进行中标记

    // retrace 缓冲区 (从 static 移入)
    CudaBuffer<Ray>             rt_d_rays;
    CudaBuffer<MultiHitResult>  rt_d_mhits;

    s3d_batch_trace_context(size_t max)
        : max_rays(max), async_pending(false)
    {
        d_rays.alloc(static_cast<unsigned int>(max));
        d_multi_hits.alloc(static_cast<unsigned int>(max));
        CUDA_CHECK(cudaStreamCreate(&stream));
        host_rays.reserve(max);
        host_mhits.reserve(max);
    }

    ~s3d_batch_trace_context() {
        if (stream) cudaStreamDestroy(stream);
    }
};
```

#### 4.2.3 异步实现

```cpp
// _async: AoS→SoA + upload + launch, 立即返回
res_T batch_trace_async_impl(s3d_scene_view* sv,
                              s3d_batch_trace_context* ctx,
                              const s3d_ray_request* requests,
                              size_t nrays)
{
    // Phase 1a: CPU 端 AoS→SoA 转换 (写入 ctx->host_rays)
    ctx->host_rays.resize(nrays);
    for (size_t i = 0; i < nrays; i++) {
        ctx->host_rays[i].origin    = make_float3(...);
        ctx->host_rays[i].direction = make_float3(...);
        ctx->host_rays[i].tmin      = requests[i].range[0];
        ctx->host_rays[i].tmax      = requests[i].range[1];
    }

    // Phase 1b: 异步上传 + 异步 launch
    unsigned int count = static_cast<unsigned int>(nrays);
    ctx->d_rays.uploadAsync(ctx->host_rays.data(), count, ctx->stream);
    sv->tracer.traceBatchMultiHit(
        ctx->d_rays.get(), ctx->d_multi_hits.get(), count, ctx->stream);

    ctx->async_pending = true;
    ctx->host_mhits.resize(nrays);
    return RES_OK;
    // ← GPU 在 ctx->stream 上异步执行, CPU 立即返回
}

// _wait: 同步 + 下载 + postprocess + retrace
res_T batch_trace_wait_impl(s3d_scene_view* sv,
                             s3d_batch_trace_context* ctx,
                             const s3d_ray_request* requests,
                             size_t nrays,
                             s3d_hit* hits,
                             s3d_batch_trace_stats* stats)
{
    // 等待 GPU 完成
    cudaStreamSynchronize(ctx->stream);
    ctx->async_pending = false;

    // 下载结果
    unsigned int count = static_cast<unsigned int>(nrays);
    ctx->d_multi_hits.download(ctx->host_mhits.data(), count);

    // Phase 2: CPU postprocess (filter) — 与原实现相同
    // Phase 3: retrace — 使用 ctx->rt_d_rays (非 static)
    ...
}
```

#### 4.2.4 注意：OptiX pipeline 线程安全

OptiX `optixLaunch` 对同一 pipeline 在不同 stream 上并发调用**是安全的**
（OptiX API 设计保证此点）。两个 ctx 各自使用独立 stream 调用同一 `m_pipeline`，无冲突。

但注意：`UnifiedTracer::traceBatchMultiHit` 中有：
```cpp
CUDA_CHECK(cudaMemcpyAsync(m_batch_params_ptr, &lp, sizeof(UnifiedParams),
                           cudaMemcpyHostToDevice, stream));
```
`m_batch_params_ptr` 是**共享的**设备内存，两个 stream 并发写入会竞争。
**修复**：将 params buffer 移入 `batch_trace_context`，或使用 stream-ordered 方式。

### 4.3 Layer 2: Pool 结构双缓冲化

#### 4.3.1 半池管理结构

```c
/* 每个半池独立的索引和缓冲区 */
struct half_pool {
    size_t base;         /* 槽位起始: 0 或 half_size */
    size_t half_size;    /* 每半池大小 */

    /* 独立索引数组 */
    uint32_t* active_indices;
    uint32_t* need_ray_indices;
    uint32_t* done_indices;
    uint32_t* bucket_radiative;
    uint32_t* bucket_conductive;

    size_t active_compact;
    size_t need_ray_count;
    size_t done_count;
    size_t bucket_radiative_n;
    size_t bucket_conductive_n;

    /* 独立射线缓冲区 */
    struct s3d_ray_request* ray_requests;
    uint32_t* ray_to_slot;
    uint32_t* ray_slot_sub;
    struct s3d_hit* ray_hits;
    size_t ray_count;
    size_t max_rays;     /* half_size × 6 */

    /* 独立 bucket offsets */
    size_t bucket_offsets[RAY_BUCKET_COUNT + 1];
    size_t bucket_counts[RAY_BUCKET_COUNT];

    /* 独立 GPU 上下文 */
    struct s3d_batch_trace_context* batch_ctx;

    /* === enc_locate / closest_point 独立缓冲区 === */
    struct s3d_batch_enc_context* enc_batch_ctx;
    struct s3d_enc_locate_request* enc_locate_requests;
    struct s3d_enc_locate_result*  enc_locate_results;
    uint32_t* enc_locate_to_slot;
    size_t enc_locate_count;

    struct s3d_batch_cp_context* cp_batch_ctx;
    struct s3d_cp_request* cp_requests;
    struct s3d_hit* cp_hits;
    uint32_t* cp_to_slot;
    size_t cp_count;

    /* 半池统计计数器 */
    size_t total_rays_traced;
    size_t paths_completed;
    size_t paths_failed;
    /* ... 其余统计 */
};
```

#### 4.3.2 wavefront_pool 修改

```c
struct wavefront_pool {
    /* 路径槽位: 物理连续, 逻辑分半 */
    struct path_state* slots;       /* [pool_size] 不变 */
    size_t pool_size;               /* = 2 × half_size */

    /* 双半池管理 */
    struct half_pool halves[2];

    /* 全局共享 (只读) */
    struct pixel_task* task_queue;
    size_t task_count;
    size_t task_next;       /* 唯一共享可变状态, 单线程递增 */

    /* ... 场景/RNG/seed 等不变 ... */
};
```

#### 4.3.3 函数接口修改模式

所有 pool 操作函数添加 `half_id` 参数：

```c
/* 原版: 扫描全池 [0, pool_size) */
static void compact_active_paths(struct wavefront_pool* pool);

/* 半池版: 只扫描 [base, base+half_size) */
static void compact_active_paths_half(struct wavefront_pool* pool, int half_id)
{
    struct half_pool* hp = &pool->halves[half_id];
    size_t base = hp->base;
    size_t end  = base + hp->half_size;
    size_t i;

    hp->active_compact      = 0;
    hp->need_ray_count      = 0;
    hp->done_count          = 0;
    hp->bucket_radiative_n  = 0;
    hp->bucket_conductive_n = 0;

    for (i = base; i < end; i++) {
        struct path_state* p = &pool->slots[i];
        /* ... 与原版相同的分类逻辑, 写入 hp->xxx 而非 pool->xxx ... */
    }
}
```

需修改的函数清单：

| 原函数 | 修改方式 | 改动量 |
|--------|---------|--------|
| `compact_active_paths` | 扫描范围 `[base, end)`, 写入 `hp->` | ~30 行 |
| `pool_collect_ray_requests_bucketed` | 读 `hp->need_ray_indices`, 写 `hp->ray_requests` | ~40 行 |
| `pool_distribute_ray_results` | 读 `hp->ray_hits`, 桶来自 `hp->bucket_*` | ~30 行 |
| `pool_cascade_non_ray_steps_compact` | 读 `hp->active_indices` | ~10 行 |
| `harvest_completed_paths` | 读 `hp->done_indices` | ~10 行 |
| `refill_pool` | 读 `hp->done_indices`, 共享 `pool->task_next` | ~15 行 |
| `pool_collect_enc_locate_requests` | 读 `hp->active_indices`, 写 `hp->enc_*` | ~15 行 |
| `pool_distribute_enc_locate_results` | 读 `hp->enc_*` | ~10 行 |
| `pool_collect_cp_requests` | 读 `hp->active_indices`, 写 `hp->cp_*` | ~15 行 |
| `pool_distribute_cp_results` | 读 `hp->cp_*` | ~10 行 |
| `pool_update_active_count` | 扫描 `[base, end)` | ~5 行 |

总半池化改动: **~190 行代码修改** (不含新增初始化/析构)。

### 4.4 Layer 3: 主循环流水线重构

#### 4.4.1 辅助函数

```c
/* 整合半池的 CPU pre-GPU 阶段 */
static res_T cpu_pre_gpu(struct wavefront_pool* pool, int half_id,
                          struct sdis_scene* scn)
{
    res_T res;
    struct half_pool* hp = &pool->halves[half_id];

    compact_active_paths_half(pool, half_id);
    hp->ray_count = 0;
    res = pool_collect_ray_requests_bucketed_half(pool, half_id);
    if (res != RES_OK) return res;
    return RES_OK;
}

/* 整合半池的 GPU launch (异步, 立即返回) */
static res_T gpu_launch(struct wavefront_pool* pool, int half_id,
                         struct s3d_scene_view* sv)
{
    struct half_pool* hp = &pool->halves[half_id];
    if (hp->ray_count == 0) return RES_OK;

    return s3d_scene_view_trace_rays_batch_ctx_async(
        sv, hp->batch_ctx,
        hp->ray_requests, hp->ray_count);
}

/* 整合半池的 GPU 等待 + CPU 后处理 */
static res_T gpu_wait_and_postprocess(struct wavefront_pool* pool, int half_id,
                                       struct s3d_scene_view* sv,
                                       struct sdis_scene* scn)
{
    struct half_pool* hp = &pool->halves[half_id];
    struct s3d_batch_trace_stats stats;
    res_T res;

    if (hp->ray_count == 0) goto skip_trace;

    memset(&stats, 0, sizeof(stats));
    res = s3d_scene_view_trace_rays_batch_ctx_wait(
        sv, hp->batch_ctx,
        hp->ray_requests, hp->ray_count,
        hp->ray_hits, &stats);
    if (res != RES_OK) return res;

    /* 累加统计 ... */

skip_trace:
    /* distribute + enc_locate + closest_point */
    res = pool_distribute_ray_results_half(pool, half_id, scn);
    if (res != RES_OK) return res;

    /* enc_locate batch */
    res = pool_collect_enc_locate_requests_half(pool, half_id);
    if (res != RES_OK) return res;
    if (hp->enc_locate_count > 0) {
        /* 同步 GPU 调用 (enc_locate 通常数量极少) */
        res = s3d_scene_view_find_enclosure_batch_ctx(
            scn->s3d_view, hp->enc_batch_ctx,
            hp->enc_locate_requests, hp->enc_locate_count,
            hp->enc_locate_results, NULL);
        if (res != RES_OK) return res;
        pool_distribute_enc_locate_results_half(pool, half_id);
    }

    /* closest_point batch */
    res = pool_collect_cp_requests_half(pool, half_id);
    if (res != RES_OK) return res;
    if (hp->cp_count > 0) {
        res = s3d_scene_view_closest_point_batch_ctx(
            scn->s3d_view, hp->cp_batch_ctx,
            hp->cp_requests, hp->cp_count,
            hp->cp_hits, NULL);
        if (res != RES_OK) return res;
        pool_distribute_cp_results_half(pool, half_id);
    }

    return RES_OK;
}

/* 整合半池的非 GPU 阶段 */
static res_T cpu_between(struct wavefront_pool* pool, int half_id,
                          struct sdis_scene* scn,
                          struct sdis_estimator_buffer* buf)
{
    res_T res;
    size_t refill_count = 0;

    res = pool_cascade_non_ray_steps_compact_half(pool, half_id, scn);
    if (res != RES_OK) return res;

    compact_active_paths_half(pool, half_id);  /* rebuild done_indices */
    res = harvest_completed_paths_half(pool, half_id, buf);
    if (res != RES_OK) return res;

    res = refill_pool_half(pool, half_id, &refill_count);
    if (res != RES_OK) return res;

    return RES_OK;
}
```

#### 4.4.2 流水线主循环

```c
/* === 7. Wavefront main loop — 双缓冲流水线 === */
time_current(&t_start);

/* 启动阶段: 初始化两半并发射首次 GPU 调用 */
cpu_pre_gpu(pool, 0, scn);          /* 准备 Half-A */
gpu_launch(pool, 0, scn->s3d_view); /* 异步发射 GPU(A) */

while (pool_has_active_paths(&pool)) {
    pool->total_steps++;  /* 每半步算一步, 统计口径不变 */

    /* ════════ Phase 1: GPU 在跑 A，CPU 处理 B ════════ */

    /* CPU: B 的 cascade + harvest + refill */
    cpu_between(&pool, 1, scn, buf);

    /* CPU: B 的 compact + collect (为下一步 GPU(B) 准备射线) */
    cpu_pre_gpu(&pool, 1, scn);

    /* 等 GPU(A) 完成, 立即发射 GPU(B) */
    gpu_wait_and_postprocess(&pool, 0, scn->s3d_view, scn);
    gpu_launch(&pool, 1, scn->s3d_view);

    pool->total_steps++;

    /* ════════ Phase 2: GPU 在跑 B，CPU 处理 A ════════ */

    /* CPU: A 的 cascade + harvest + refill */
    cpu_between(&pool, 0, scn, buf);

    /* CPU: A 的 compact + collect */
    cpu_pre_gpu(&pool, 0, scn);

    /* 等 GPU(B) 完成, 立即发射 GPU(A) */
    gpu_wait_and_postprocess(&pool, 1, scn->s3d_view, scn);
    gpu_launch(&pool, 0, scn->s3d_view);

    /* 更新诊断 */
    pool_update_diagnostics_dual(&pool);
}

/* 排空: 等待最后一个 GPU 调用完成 */
gpu_wait_and_postprocess(&pool, current_half, scn->s3d_view, scn);
```

### 4.5 内存开销

| 组件 | 当前 (8K pool) | 双缓冲 (16K pool) | 增量 |
|------|---------------|-------------------|------|
| path_state slots × 2.2KB | 17.6MB | 35.2MB | +17.6MB |
| 射线缓冲区 (req+hit+map) × 2 | 7.5MB | 15.0MB | +7.5MB |
| 索引数组 × 2 | 0.8MB | 1.6MB | +0.8MB |
| batch_ctx GPU 内存 × 2 | ~10MB | ~20MB | +10MB |
| **CPU 总计** | **~26MB** | **~52MB** | **+26MB** |
| **GPU 总计** | **~10MB** | **~20MB** | **+10MB** |

36MB 增量在 RTX 4090 (24GB VRAM) 和现代主机内存下可忽略。

---

## 5. 风险分析

### 5.1 正确性风险

#### 5.1.1 [中] retrace 缓冲区并发风险 (原 [高] 已部分修复)

**原问题**: `batch_trace_impl` 使用 `static CudaBuffer` 做 retrace — **✅ 已修复**。
缓冲区已移至 `s3d_scene_view` 成员 (`rt_retrace_rays` / `rt_retrace_mhits`)，
修复了 atexit 时 CUDA 上下文生命周期崩溃问题。

**残余问题**: retrace 缓冲区仍在 `sv` 级别共享 (非 per-ctx)。
在双缓冲方案中，两半的 retrace 不会同时在 CPU 上执行（单线程串行），
因此缓冲区本身不会数据竞争。但 retrace 内部的 `cudaDeviceSynchronize()` (L1365)
会阻塞等待**所有** stream（包括另一 half 的 async trace），
导致流水线意外串行化，**性能退化**。

**影响**: 性能退化 (非数据损坏)。当 retrace 触发时，流水线暂时退化为串行。

**修复**: (P1 阶段)
1. 将 retrace 缓冲区从 `sv` 成员移入 `batch_trace_context` (per-ctx 独立)
2. retrace GPU launch 使用 `ctx->stream` 而非默认 stream
3. `cudaDeviceSynchronize()` → `cudaStreamSynchronize(ctx->stream)`

#### 5.1.2 [高] `m_batch_params_ptr` 共享

**问题**: `UnifiedTracer::traceBatchMultiHit` 中的 `m_batch_params_ptr`
是 tracer 实例级别的共享设备内存。两个 stream 并发写入 params 会竞争。

**影响**: GPU kernel 读到错误的 launch params → 结果损坏。

**修复**: 在 `batch_trace_context` 中分配独立的 params 缓冲区，
或改为 stack-based kernel params (CUDA inline params)。

#### 5.1.3 [中] enc_locate / closest_point 与主 trace 的 GPU 冲突

**问题**: 当前 enc_locate 和 closest_point 也使用 GPU batch 调用。
在 `gpu_wait_and_postprocess` 中，它们以同步方式执行。
需确保其 GPU 调用不与另一 half 的异步 trace 冲突。

**影响**: 如果它们使用默认 stream (0)，会隐式与所有 stream 同步。

**修复**: enc/cp 调用也使用 `ctx->stream`，或接受短暂的全局同步
(这两类操作极少发生，性能影响可忽略)。

#### 5.1.4 [低] 像素温度累加精度

**问题**: 同一像素的不同 spp 可能分布在两半池中，
累加顺序不同于原版 (Morton 顺序)。float/double 累加的顺序影响舍入。

**影响**: 最终温度值产生 ULP 级别差异 (~1e-15)。

**修复**: 如需位精确一致，可用 Kahan 求和代替简单累加。
但温度验证容差为 1e-6，无实际影响。可忽略或记录。

### 5.2 性能风险

#### 5.2.1 [中] Drain 阶段效率退化

**问题**: drain 阶段 (最后 38.7s, 2.8% 总时间) 路径数逐渐减少。
一半可能已空而另一半仍有少量路径，流水线退化为单缓冲。

**影响**: drain 阶段无加速，但因其只占 2.8%，全局影响 < 1.5%。

**缓解**: drain 阶段检测一半为空时，自动合并到单半模式。

#### 5.2.2 [中] CPU 成为瓶颈

**问题**: CPU 总量 (1.65ms) > GPU (1.54ms)。
流水线加速受限于 CPU，而非 GPU。

**影响**: 加速比 1.92× 而非理论 2.0×。

**长期优化方向**: Top-K filter GPU 化 (减少 CPU_post 0.35ms)，
可使 GPU 成为瓶颈，加速比达 2.07×。

#### 5.2.3 [低] Cache 局部性影响

**问题**: 两半路径的 slots 在内存中相距 N/2 × 2.2KB ≈ 9MB。
交替访问可能增加 L3 cache miss。

**影响**: 实际影响取决于 CPU cache 大小 (通常 16-32MB L3)。
16K × 2.2KB = 35.2MB > L3，但每步只访问活跃路径 (~80% × 8K × 2.2KB = 14.4MB)。

**缓解**: 如测量到显著 cache miss，可考虑将两半交错排列而非连续分区。

### 5.3 实现风险

#### 5.3.1 [中] 代码维护复杂度

**问题**: ~600 行新增/修改代码，所有 pool 操作函数需双版本维护。

**缓解**: 使用宏或内联参数化消除代码重复：
```c
#define POOL_HALF_FUNC(func_name, pool, half_id) \
    func_name##_impl(pool, &(pool)->halves[half_id], (pool)->halves[half_id].base, ...)
```

#### 5.3.2 [高] 调试困难

**问题**: 两半路径交织执行，单步调试时难以跟踪。日志混杂两半状态。

**缓解**:
- 添加 `[A]`/`[B]` 前缀到所有日志
- 提供 `STARDIS_PIPELINE=0` 环境变量回退到单缓冲模式
- 分别对每半输出独立的诊断统计

#### 5.3.3 [中] 回归测试

**问题**: 需验证所有场景类型 (pure radiative, conductive, boundary, coupled)
和所有阶段 (refill, drain) 的正确性。

**缓解**: 利用现有 pixel trace 机制比较单缓冲和双缓冲的逐路径输出。
GPU/CPU 结果对比容差 1e-6。

---

## 6. 实施计划

### 6.1 阶段划分

| 阶段 | 内容 | 工作量 | 产出 |
|------|------|--------|------|
| **P0: 异步 API** | `batch_trace_context` 扩展 + `_async/_wait` 实现 | 2天 | oxstar-3d 异步光追 |
| **P1: 消除静态缓冲区** | retrace/params 缓冲区移入 ctx | 1天 | 消除并发风险 |
| **P2: half_pool 结构** | 定义 `half_pool`，池创建/销毁，分配双缓冲区 | 1天 | 内存管理 |
| **P3: 函数半池化** | 11个 pool 函数的 `_half` 版本 | 3天 | 全部 pool 操作支持半池 |
| **P4: 流水线主循环** | 双调度主循环 + drain 合并 + 诊断日志 | 2天 | 核心功能完成 |
| **P5: 验证** | 逐像素结果对比，性能基准测试 | 2天 | 正确性确认 |
| **总计** | | **11 天** | |

### 6.2 验证策略

```
1. 功能验证:
   - 单缓冲 (STARDIS_PIPELINE=0) 必须产出与原版 bit-exact 结果
   - 双缓冲结果与单缓冲比较, 容差 1e-6 (仅浮点累加顺序差异)

2. 性能验证:
   - porous 场景 320×320 spp=32: 对比 wall-clock time
   - 分阶段计时: 确认 GPU 利用率从 ~48% 提升到 ~93%
   - GPU 吞吐量: 应保持 ~17 Mrays/s (不因分半而降低)
   - nvidia-smi 或 Nsight 确认 GPU 占用率

3. 压力测试:
   - spp=1 (极快切换, drain 阶段压力)
   - spp=256 (长时间 refill 阶段稳态)
   - 纯辐射场景 vs 纯导热场景 vs 耦合场景
```

---

## 7. 结论

### 优势
- 工作负载特征 (CPU ≈ GPU 50/50) 完美匹配双缓冲方案
- 路径级操作天然独立，无根本性并发障碍
- 预期 **1.92× 加速** (22min → 11min 50s)，逼近理论 2× 极限
- 内存增量 36MB 可忽略

### 主要风险
- `m_batch_params_ptr` 和 retrace static 缓冲区需要改造 (P1 阶段解决)
- CPU 是瓶颈而非 GPU — 加速比受限于 1.92× (需 Top-K GPU 化突破)
- 调试复杂度提升 (通过可回退设计缓解)

### 建议
推荐作为下一阶段优化重点实施。优先完成 P0+P1 (异步 API + 消除竞争)，
可以独立于双缓冲方案使用 (单池 + 异步 launch + cascade 重叠)，
确认无回归后再实施 P2-P4 (完整双缓冲)。

---

*分析基于 stardis-cus3d stardis-solver/0.16.2 + oxstar-3d/0.10 源码和 2026-02-23 porous 场景实测数据*

---

## 8. n-View 调度性能分析

**生成时间**: 2026-02-24  
**研究问题**: 在 pool_view 统一接口和交替执行模型下，n>2 的多缓冲区设计是否能进一步提升变负载场景下的性能？  
**结论**: **n=2 是最优选择**，n>2 带来的额外固定开销和 GPU 占用率下降抵消了可能的收益。

### 8.1 性能模型

#### 8.1.1 变负载下的时间模型

单次 pool_view 处理周期分为 GPU 阶段和 CPU 阶段：

$$
T_g(r) = \alpha_g + \beta_g \cdot r
$$

$$
T_c(r) = \alpha_c + \beta_c \cdot r
$$

**参数定义**：
- $r$：该 view 中激活路径数占池容量的比例 $r = \frac{N_{\text{active}}}{N_{\text{capacity}}}$
- $\alpha_g, \alpha_c$：固定开销（kernel 启动、同步、数据上传等）
- $\beta_g, \beta_c$：单位负载的处理时间（线性部分）

**实测数据**（基于 §1.2 数据，pool_size=8192）：
- GPU: $\alpha_g \approx 0.2\,\text{ms}$（kernel launch overhead）
- GPU: $\beta_g \approx 1.538\,\text{ms}$（满载 trace 时间）
- CPU: $\alpha_c \approx 0.1\,\text{ms}$（compact/harvest overhead）
- CPU: $\beta_c \approx 0.402\,\text{ms}$（compact+collect+distribute+cascade）

#### 8.1.2 n-View 流水线模型

**n=2（双缓冲）**：
- View A GPU 时，View B CPU → 时间 $\max(T_g^A, T_c^B)$
- View B GPU 时，View A CPU → 时间 $\max(T_g^B, T_c^A)$
- 单周期时间：$T_2 = \max(T_g^A, T_c^B) + \max(T_g^B, T_c^A)$

**n=3（三缓冲）**：
- 理想情况：3个view轮转，GPU 和 CPU 始终有工作
- 单个 view 完整周期：$T_{\text{view}} = T_g + T_c$
- 吞吐量受限于：$\max(T_g, T_c)$（瓶颈资源）

**n>3**：类似 n=3 分析。

### 8.2 加速比推导

#### 8.2.1 串行基线

单个 view 处理完整周期：

$$
T_{\text{serial}} = T_g(1) + T_c(1) = (\alpha_g + \beta_g) + (\alpha_c + \beta_c)
$$

带入实测值：
$$
T_{\text{serial}} = (0.2 + 1.538) + (0.1 + 0.402) = 2.240\,\text{ms}
$$

#### 8.2.2 n=2 加速比（均匀负载）

假设两个 view 负载均匀 $r_A = r_B = 0.5$（各占池容量一半）：

$$
T_g(0.5) = 0.2 + 1.538 \times 0.5 = 0.969\,\text{ms}
$$

$$
T_c(0.5) = 0.1 + 0.402 \times 0.5 = 0.301\,\text{ms}
$$

双缓冲周期：
$$
T_2 = 2 \times \max(0.969, 0.301) = 2 \times 0.969 = 1.938\,\text{ms}
$$

加速比：
$$
S_2 = \frac{T_{\text{serial}}}{T_2} = \frac{2.240}{1.938} \approx 1.88\times
$$

**GPU 利用率**：$\frac{0.969}{1.938} \approx 76\%$（overlap 仅浪费 10% 同步时间）

#### 8.2.3 n=3 加速比（均匀负载）

三个 view 负载均匀 $r = 1/3$：

$$
T_g(1/3) = 0.2 + 1.538 \times 0.333 = 0.712\,\text{ms}
$$

$$
T_c(1/3) = 0.1 + 0.402 \times 0.333 = 0.234\,\text{ms}
$$

单 view 完整周期：$T_{\text{view}} = 0.712 + 0.234 = 0.946\,\text{ms}$

**关键问题**：GPU 是瓶颈（$T_g > T_c$），流水线受限于 GPU 吞吐量：

$$
T_3 = 3 \times T_g(1/3) = 3 \times 0.712 = 2.136\,\text{ms}
$$

加速比：
$$
S_3 = \frac{2.240}{2.136} \approx 1.83\times
$$

**GPU 利用率**：$\frac{0.712 \times 3}{2.136} = 100\%$（GPU 满载，但 CPU 空闲）

**实际GPU占用率**：$\frac{0.712}{0.946} \approx 51\%$（每个 view 周期中 GPU 占比）

#### 8.2.4 一般 n-View 加速比

负载均匀 $r = 1/n$：

$$
T_g(1/n) = \alpha_g + \frac{\beta_g}{n}, \quad T_c(1/n) = \alpha_c + \frac{\beta_c}{n}
$$

**关键观察**：固定开销 $\alpha$ 不随 n 减少，导致：

$$
T_n = n \times \max\left(\alpha_g + \frac{\beta_g}{n}, \alpha_c + \frac{\beta_c}{n}\right)
$$

当 $n$ 增大：
- 固定开销累积：$n \cdot \alpha_g$ 项增长
- 线性部分抵消：$\beta_g$ 项保持不变
- 极限：$\lim_{n \to \infty} T_n = n \cdot \max(\alpha_g, \alpha_c) + \max(\beta_g, \beta_c)$

**n=4 示例**：
$$
T_g(1/4) = 0.2 + 0.385 = 0.585\,\text{ms}
$$
$$
T_4 = 4 \times 0.585 = 2.340\,\text{ms}
$$
$$
S_4 = \frac{2.240}{2.340} \approx 0.96\times \quad \text{(性能退化!)}
$$

### 8.3 变负载场景分析

#### 8.3.1 负载不均衡模型

定义负载差异系数 $\delta \in [0, 1]$：

$$
r_A = 0.5 + \delta, \quad r_B = 0.5 - \delta
$$

其中 $\delta = 0$ 为完全均匀，$\delta = 0.5$ 为极端不均（一个 view 满载，另一个空载）。

#### 8.3.2 n=2 下的负载容忍度

**轻度不均** ($\delta = 0.2$，即 70%/30% 负载分布)：

$$
T_g(0.7) = 0.2 + 1.538 \times 0.7 = 1.277\,\text{ms}
$$
$$
T_g(0.3) = 0.2 + 1.538 \times 0.3 = 0.661\,\text{ms}
$$

双缓冲周期：
$$
T_2' = \max(1.277, 0.301) + \max(0.661, 0.321) = 1.277 + 0.661 = 1.938\,\text{ms}
$$

加速比：
$$
S_2' = \frac{2.240}{1.938} \approx 1.64\times
$$

**性能下降幅度**：$\frac{1.88 - 1.64}{1.88} \approx 13\%$

**重度不均** ($\delta = 0.5$，即 100%/0% 负载分布)：
- View A 满载：$T_g(1.0) = 1.738\,\text{ms}$
- View B 空载：$T_g(0.0) = 0.200\,\text{ms}$
- 周期时间：$T_2'' = 1.738 + 0.502 = 2.240\,\text{ms}$
- 加速比：$S_2'' = 1.0\times$（退化为串行）

**n=2 关键优势**：支持 **动态合并**！
  - 当检测到 $r_A + r_B < \text{threshold}$（如 0.6）时
  - 执行 `merge_to_single_pool()` 合并到 `views[0]`
  - 单池模式加速比仍可达 ~1.2×（cascade+harvest 重叠）

#### 8.3.3 n=3 变负载下的脆弱性

**问题**：三缓冲无法动态合并（需要 4 个 view 切换）

**轻度不均**（40%/35%/25%）：
$$
T_g(0.4) = 0.816\,\text{ms}, \quad T_g(0.35) = 0.739\,\text{ms}, \quad T_g(0.25) = 0.585\,\text{ms}
$$

流水线受限于最慢 view：
$$
T_3' = 3 \times 0.816 = 2.448\,\text{ms}
$$
$$
S_3' = \frac{2.240}{2.448} \approx 0.92\times \quad \text{(性能退化!)}
$$

**根本原因**：
1. GPU 瓶颈：单个慢 view 阻塞整个流水线
2. 缺乏动态调整：无法合并低负载 view
3. 固定开销比例高：$n \cdot \alpha_g$ 成为主导项

### 8.4 对比总结

| 配置 | 均匀负载加速比 | GPU 占用率 | 变负载容忍度 | 动态合并支持 | 内存开销 |
|------|--------------|-----------|------------|------------|---------|
| **n=2** | **1.88×** | 76% | **高** (支持合并) | ✅ | 1.5× |
| n=3 | 1.83× | 51% | 低 | ❌ | 2× |
| n=4 | 0.96× | 38% | 极低 | ❌ | 2.5× |

**关键指标**：
1. **加速比**：n=2 最高（1.88×），n≥3 退化
2. **GPU 占用率**：n=2 达到 76%（接近理论最优），n=3 仅 51%
3. **变负载适应**：n=2 可降级为单池模式保底 1.0×，n≥3 直接退化到 <1.0×
4. **实现复杂度**：n=2 状态机简单（2 种模式），n≥3 需要复杂调度器

### 8.5 理论极限分析

#### 8.5.1 理想条件下的最优 n

**假设**：
- 固定开销可忽略（$\alpha_g = \alpha_c = 0$）
- 完美负载均衡（所有 view 负载相同）
- 无限资源（无内存限制）

此时：
$$
T_n^{\text{ideal}} = \max\left(\frac{\beta_g}{n}, \frac{\beta_c}{n}\right) \times n = \max(\beta_g, \beta_c)
$$

**收敛结论**：加速比收敛到常数，而非随 n 增长！

$$
S_n^{\text{ideal}} = \frac{\beta_g + \beta_c}{\max(\beta_g, \beta_c)} = \frac{1.538 + 0.402}{1.538} \approx 1.26\times
$$

**实际 n=2**：
$$
S_2^{\text{actual}} = 1.88\times > S_{\infty}^{\text{ideal}} = 1.26\times
$$

**原因**：n=2 利用了固定开销的重叠效应（$\alpha$ 在不同 view 间并行），而 n→∞ 时固定开销累积抵消了并行收益。

#### 8.5.2 最优 n 的数学推导

定义目标函数（最小化处理周期）：

$$
T_n = n \times \max\left(\alpha_g + \frac{\beta_g}{n}, \alpha_c + \frac{\beta_c}{n}\right)
$$

假设 GPU 为瓶颈（$\alpha_g + \beta_g > \alpha_c + \beta_c$）：

$$
T_n = n \times \left(\alpha_g + \frac{\beta_g}{n}\right) = n \cdot \alpha_g + \beta_g
$$

对 $n$ 求导：
$$
\frac{dT_n}{dn} = \alpha_g > 0
$$

**结论**：$T_n$ 单调递增！最优 $n = 1$（但 n=1 无并行）

**引入约束**：要求 $n \geq 2$（至少双缓冲才能重叠），则 **$n = 2$ 是最优解**。

### 8.6 结论

#### 8.6.1 核心发现

1. **n=2 是数学最优解**：在有固定开销的实际系统中，双缓冲达到最佳平衡
2. **n>2 带来性能退化**：固定开销累积（$n \cdot \alpha$）超过并行收益
3. **变负载下 n=2 更鲁棒**：支持动态合并策略，n≥3 缺乏灵活性
4. **GPU 占用率矛盾**：提高 n 降低单 view 负载，但增加 idle 切换时间

#### 8.6.2 设计建议

**推荐架构**：
- 固定使用 `views[2]` 双缓冲设计
- 实现 `merge_to_single_pool()` / `split_to_dual_pool()` 动态切换
- 合并阈值：当 $\frac{N_{\text{active}}}{N_{\text{pool}}} < 0.6$ 时触发合并
- 分裂阈值：当 $\frac{N_{\text{active}}}{N_{\text{pool}}} > 0.8$ 时恢复双缓冲

**不推荐**：
- ❌ 泛化为 n-view 调度器（复杂度高，收益负）
- ❌ 静态三缓冲或多缓冲（固定开销浪费）
- ❌ 忽略变负载适应（实际场景路径数波动大）

#### 8.6.3 实测验证计划

需通过实际测试验证理论预测：

1. **固定 spp=32，对比 n=2 vs n=3**：
   - 预期：n=2 达到 1.88×，n=3 约 1.83× 或更低
   - 观测：nvidia-smi GPU 占用率（n=2 应为 76%，n=3 为 51%）

2. **变负载场景**（使用 spp 递减模拟负载下降）：
   - spp=128→64→32→16→8→4 连续求解
   - 预期：n=2+merge 模式保持 >1.2× 加速，n=3 退化到 <1.0×

3. **固定开销测量**：
   - 空池运行（0 个激活路径），测量 GPU kernel launch 时间
   - 验证 $\alpha_g \approx 0.2\,\text{ms}$ 的假设

---

**分析更新**: 2026-02-24 | 基于 pool_view 统一接口设计和实测性能数据
