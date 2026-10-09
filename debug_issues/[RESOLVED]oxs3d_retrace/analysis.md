# oxstar-3d Retrace Filter 性能退化分析

**日期**: 2026-02-23  
**症状**: stardis 主程序 profile 显示 `ntdll.dll` self-time 高达 80%  
**触发条件**: oxstar-3d batch trace 接口实现带 filter 的 retrace 后出现  
**影响范围**: 仅 OptiX 后端 (`S3D_BACKEND=optix`)  
**当前构建配置**: `S3D_BACKEND:STRING=optix` (CMakeCache.txt:551)

---

## 1. 症状描述

在 Visual Studio Profiler 中，stardis 主程序首个高耗时调用为 `ntdll.dll`，self-time
占 80%。`ntdll.dll` 是 Windows NT 内核用户态入口，所有 GPU 驱动级操作
（`cudaMalloc`/`cudaFree`、DLL 加载、设备 I/O）均通过其系统调用完成。

此问题在 `batch_trace_impl()` 实现 filter + retrace fallback **之前不存在**。

---

## 2. 根因定位

### 2.1 问题代码路径

```
s3d_scene_view_trace_rays_batch_ctx()        ← 求解器调用入口
  └─ batch_trace_impl()                      ← ox_s3d_scene_view.cpp:1147
       ├─ Phase 1: traceBatchMultiHit(vector) ← 每次 3× cudaMalloc/Free
       ├─ Phase 2: CPU filter 逐射线检查      ← 无 GPU 开销
       └─ Phase 3: retrace_list 逐条调用      ← ★ 性能杀手
            └─ s3d_scene_view_trace_ray()     ← ox_s3d_scene_view.cpp:935
                 └─ while (retry_tmin < tmax)  ← ★ 无深度限制
                      └─ traceSingle()         ← 每次迭代:
                           ├─ cudaMalloc(d_params)
                           ├─ cudaMemcpy H→D
                           ├─ optixLaunch(1,1,1)  ← 为单条射线 launch
                           ├─ cudaDeviceSynchronize()
                           └─ cudaFree(d_params)
```

### 2.2 两个致命缺陷

#### 缺陷 A: retry 循环无深度限制

**oxstar-3d** (`ox_s3d_scene_view.cpp:990-1020`):
```cpp
float retry_tmin = hr.t + 1e-6f;
while (retry_tmin < ray.tmax) {        // ← 无 MAX_FALLBACK_DEPTH!
    ray.tmin = retry_tmin;
    hr = sv->tracer.traceSingle(ray);  // ← 每次迭代完整 GPU roundtrip
    ...
    retry_tmin = hr.t + 1e-6f;
}
```

**custar-3d** (`s3d_scene_view_trace_ray.cpp:57-142`) 的参考实现:
```c
#define MAX_FALLBACK_DEPTH 4           // ← 硬限制

static res_T trace_ray_impl(..., int depth) {
    if (depth >= MAX_FALLBACK_DEPTH) {  // ← 最多递归 4 层
        *hit = S3D_HIT_NULL;
        return RES_OK;
    }
    // 每次 GPU 调用返回 Top-K (K=2) 个候选
    cus3d_trace_ray_single_multi(bvh, ..., TOPK_COUNT, &multi);
    ...
    // tmin 跳过最远候选，而非当前 hit
    fallback_range[0] = multi.hits[TOPK_COUNT - 1].distance + 1e-6f;
    return trace_ray_impl(..., depth + 1);
}
```

| 对比项 | oxstar-3d | custar-3d |
|--------|-----------|-----------|
| 深度限制 | **无** (while循环) | `MAX_FALLBACK_DEPTH = 4` |
| 每次GPU调用返回数 | 1 (单hit) | K=2 (Top-K) |
| tmin 前进幅度 | `当前hit.t + 1e-6f` | `最远候选.t + 1e-6f` |
| 最多GPU调用次数/射线 | **无穷** | 1 + 4 = 5 次 |

#### 缺陷 B: `traceSingle()` 每次调用都 malloc/free/sync

`unified_tracer.cpp:1131-1154`:
```cpp
HitResult UnifiedTracer::traceSingle(const Ray& ray) {
    ensureSingleBuffers();                      // OK: 只分配一次
    m_single_ray_buf.upload(&ray, 1);

    CudaBuffer<UnifiedParams> d_params;         // ← 栈变量
    d_params.alloc(1);                          // ← cudaMalloc(~128B)!
    d_params.upload(&lp, 1);                    // ← cudaMemcpy

    OPTIX_CHECK(optixLaunch(m_pipeline, 0,
        d_params.devicePtr(), sizeof(UnifiedParams),
        &m_sbt_rt, 1, 1, 1));                  // ← launch for 1 ray
    CUDA_SYNC_CHECK();                          // ← cudaDeviceSynchronize()!

    HitResult result;
    m_single_hit_buf.download(&result, 1);
    return result;
}                                               // ← d_params 析构 = cudaFree!
```

同样问题存在于 `traceBatch(ptr)` 和 `traceBatchMultiHit(ptr)` 版本
(`unified_tracer.cpp:1156-1178`, `1248-1269`)，它们对 `d_params` 也是每次
`cudaMalloc` + `cudaFree`。

### 2.3 数值陷阱：无限重试

当场景中存在共面/共边三角形时:
1. 两个三角形的 hit 距离差 `< 1e-6f`
2. 第一个被 filter 拒绝，`retry_tmin = hit.t + 1e-6f`
3. `traceSingle` 返回同一三角形（或紧邻三角形），再被拒绝
4. `retry_tmin += 1e-6f` 不足以跳过 → 循环继续
5. 退化为数千次迭代，每次迭代都做完整 GPU roundtrip

**每次 traceSingle roundtrip 的系统调用**: 
`cudaMalloc` → `ntdll!NtAllocateVirtualMemory` +
`cudaFree` → `ntdll!NtFreeVirtualMemory` +
`cudaDeviceSynchronize` → `ntdll!NtWaitForSingleObject`

若 N 条射线需 retrace，每条平均 K 次 retry → N×K 次完整 roundtrip，全部计入
`ntdll.dll` self-time。

### 2.4 ctx 预分配缓冲区被完全忽略

`ox_s3d_scene_view.cpp:1293-1310`:
```cpp
res_T s3d_scene_view_trace_rays_batch_ctx(
    s3d_scene_view* sv,
    s3d_batch_trace_context* /*ctx*/,    // ← 被注释掉！
    const s3d_ray_request* requests,
    size_t nrays,
    s3d_hit* hits,
    s3d_batch_trace_stats* stats)
{
    // ctx 未传入 batch_trace_impl
    return batch_trace_impl(sv, requests, nrays, hits, stats);
}
```

而 `s3d_batch_trace_context` 已经预分配了 device buffer:
```cpp
struct s3d_batch_trace_context {   // ox_s3d_internal.h:260
    size_t              max_rays;
    CudaBuffer<Ray>             d_rays;         // 预分配!
    CudaBuffer<MultiHitResult>  d_multi_hits;   // 预分配!
    // 构造时 alloc(max_rays)
};
```

这导致 `batch_trace_impl` 中 Phase 1 调用的是 `traceBatchMultiHit(vector)` 便利
版本，每次调用都重新分配 3 个 device buffer (d_rays + d_mh + d_params)。

---

## 3. 开销量化

### 单次 batch_trace_impl 调用 (N 条射线, R 条需 retrace, 每条平均 K 次 retry)

| 阶段 | cudaMalloc 次数 | cudaFree 次数 | cudaSync 次数 | optixLaunch 次数 |
|------|----------------|---------------|---------------|-----------------|
| Phase 1 (batch)   | 3 | 3 | 1 | 1 |
| Phase 2 (filter)  | 0 | 0 | 0 | 0 |
| Phase 3 (retrace) | R×K | R×K | R×K | R×K |
| **合计** | **3 + R×K** | **3 + R×K** | **1 + R×K** | **1 + R×K** |

**典型场景**: N=1024, R=50 (5% 需 retrace), K=10 (密集场景平均重试):
- cudaMalloc: 503 次
- cudaFree: 503 次
- optixLaunch(1,1,1): 500 次 (为单条射线!)
- cudaDeviceSynchronize: 501 次

**退化场景**: R=200, K=100 (数值陷阱):
- cudaMalloc: **20003** 次 → ntdll.dll 主导 profile

---

## 4. 确认方法

### 4.1 Profile 调用树验证
在 VS Profiler 调用树中展开 ntdll.dll，确认调用者为:
- `cudaMalloc` / `cuMemAlloc` (CUDA 内存分配)
- `cudaFree` / `cuMemFree` (CUDA 内存释放)
- `cudaDeviceSynchronize` / `cuCtxSynchronize` (GPU 同步)

### 4.2 Stats 验证
检查 `s3d_batch_trace_stats` 的输出：
- `filter_rejected` → 有多少射线进入了 retrace 路径
- `retrace_time_ms` vs `batch_time_ms` → retrace 是否主导耗时
- `retrace_accepted + retrace_missed == filter_rejected` (不变量)

### 4.3 快速验证: 切换后端
```bash
cd stardis-cus3d/build
cmake -DS3D_BACKEND=cubql ..
cmake --build . --config Release
```
如果 ntdll.dll 开销消失 → 确认是 oxstar-3d retrace 实现导致。

---

## 5. 涉及的源文件

| 文件 | 关键位置 | 角色 |
|------|---------|------|
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | L935-1028 | `s3d_scene_view_trace_ray` retry循环 (无深度限制) |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | L1147-1280 | `batch_trace_impl` 三阶段实现 |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | L1293-1310 | `_ctx` 版本忽略 ctx 参数 |
| `oxstar-3d/0.10/src/unified_tracer.cpp` | L1131-1154 | `traceSingle` 每次 malloc/free |
| `oxstar-3d/0.10/src/unified_tracer.cpp` | L1156-1178 | `traceBatch(ptr)` 每次 malloc/free d_params |
| `oxstar-3d/0.10/src/unified_tracer.cpp` | L1248-1269 | `traceBatchMultiHit(ptr)` 每次 malloc/free d_params |
| `oxstar-3d/0.10/src/unified_tracer.cpp` | L1273-1286 | `traceBatchMultiHit(vector)` 每次 3× malloc/free |
| `oxstar-3d/0.10/src/unified_tracer.h`   | L353-357 | 已有 `m_single_*` 预分配模式 |
| `oxstar-3d/0.10/src/buffer_manager.h`    | L50-65 | `CudaBuffer::alloc/free` = cudaMalloc/Free |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h` | L260-270 | `s3d_batch_trace_context` 预分配结构 |
| `custar-3d/0.10/src/s3d_scene_view_trace_ray.cpp` | L40-142 | 参考实现: MAX_FALLBACK_DEPTH + Top-K |
| `custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp` | L157-386 | 参考实现: batch + retrace (有诊断计数) |
| `custar-3d/0.10/src/cus3d_types.h` | L50 | `CUS3D_MAX_MULTI_HITS = 2` |
