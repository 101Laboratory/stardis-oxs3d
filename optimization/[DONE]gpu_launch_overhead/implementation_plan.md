# 方案 E：Pinned 直写实施计划

**日期**: 2026-03-05  
**前置**: analysis.md  
**目标**: 消除 collect→gpu_launch 间的 `ray_requests[]` 中间 buffer 和 AoS→SoA 转换循环  
**预期收益**: gpu_launch 从 ~90s 降至 ~6s（仅剩 driver calls），collect 从 19s→~22s（微增），净省 ~80s  
**实测结果**: gpu_launch 92.9s → 27.5s (-65.4s)，墙钟 262.5s → 210.1s (-52.4s)  
**状态**: ✅ 结题 — branch opt/pinned-write, worktree stardis-cus3d-pinned-write

---

## 1. 当前数据流

```
  collect Pass 2                              gpu_launch_async
  ─────────────                               ─────────────────
  slots[i].ray_req ──散射读──→ pv->ray_requests[cursor]      ──顺序读──→ ctx->h_rays_pinned[i]
                               (pageable calloc, 40B/ray AoS)             (cudaHostAlloc, 32B/ray SoA)
                                                                           → uploadAsync → device

  fill_filter_per_ray(p) ──→  pv->filter_per_ray[cursor]     ──memcpy──→ ctx->h_filter_pinned
                               (pageable calloc, 16B/ray)                  (cudaHostAlloc, 16B/ray)
                                                                           → uploadAsync → device
```

**中间 buffer**:
- `pv->ray_requests[]` — pageable calloc, 40B/ray × max_rays
- `pv->filter_per_ray[]` — pageable calloc, 16B/ray × max_rays

## 2. 目标数据流

```
  collect Pass 2
  ─────────────
  slots[i].ray_req ──散射读──→ ctx->h_rays_pinned[cursor]    → uploadAsync → device
                               (cudaHostAlloc, 32B/ray SoA)

  fill_filter_per_ray(p) ──→  ctx->h_filter_pinned[cursor]   → uploadAsync → device
                               (cudaHostAlloc, 16B/ray)
```

消除中间 buffer，collect 直写 GPU pinned buffer。

## 3. 接口变更清单

### 3.1 pool_view 新增字段

```c
/* sdis_solve_persistent_wavefront.h — pool_view */

/* Pinned direct-write pointers (borrowed from batch_ctx) */
struct s3d_ray_pinned*       ray_pinned;       /* → ctx->h_rays_pinned (Ray layout) */
struct s3d_filter_per_ray*   filter_pinned;     /* → ctx->h_filter_pinned */
```

不分配内存，仅持有指向 batch_ctx 中 pinned buffer 的借用指针。

### 3.2 s3d.h 新增 Ray 写入结构体（C 可见）

```c
/* s3d.h — C-visible Ray layout matching CUDA Ray struct */
struct s3d_ray_pinned {
    float origin_x, origin_y, origin_z;
    float tmin;
    float direction_x, direction_y, direction_z;
    float tmax;
};
/* static_assert sizeof == 32 */
```

需 static_assert 与 `ray_types.h::Ray` layout 一致。

### 3.3 s3d API 新增访问接口

```c
/* s3d.h */

/* 获取 batch_ctx 内部 pinned buffer 指针（borrowing, 不转移所有权） */
void s3d_batch_trace_context_get_pinned_buffers(
    struct s3d_batch_trace_context* ctx,
    struct s3d_ray_pinned**       out_rays,
    struct s3d_filter_per_ray**   out_filter,
    size_t*                        out_capacity);

/* 直接从 pinned buffer 启动 GPU trace（跳过 AoS→SoA 转换） */
res_T s3d_scene_view_trace_rays_batch_ctx_filtered_pinned_async(
    struct s3d_scene_view* sv,
    struct s3d_batch_trace_context* ctx,
    size_t nrays);
```

### 3.4 ox_s3d_scene_view.cpp 新增实现

```cpp
/* batch_trace_filtered_pinned_async_impl
 * 跳过 AoS→SoA 循环 + memcpy，直接 uploadAsync 从 pinned buffer */
static res_T batch_trace_filtered_pinned_async_impl(
    s3d_scene_view* sv,
    s3d_batch_trace_context* ctx,
    size_t nrays)
{
    // NO conversion loop — data already in h_rays_pinned / h_filter_pinned
    unsigned int count = (unsigned int)nrays;
    ctx->d_rays.uploadAsync(ctx->h_rays_pinned, count, ctx->transfer_stream);
    ctx->d_filter_data.uploadAsync(
        reinterpret_cast<FilterPerRayData*>(ctx->h_filter_pinned),
        count, ctx->transfer_stream);
    CUDA_CHECK(cudaEventRecord(ctx->evt_upload_done, ctx->transfer_stream));
    CUDA_CHECK(cudaStreamWaitEvent(ctx->compute_stream, ctx->evt_upload_done, 0));

    // kernel launch (identical to existing)
    sv->tracer.traceBatchMultiHitFiltered(...);
    CUDA_CHECK(cudaEventRecord(ctx->evt_kernel_done, ctx->compute_stream));

    ctx->async_pending = true;
    ctx->async_nrays   = nrays;
    return RES_OK;
}
```

### 3.5 collect Pass 2 scatter 修改

当前写 `pv->ray_requests[ray_idx]` + `pv->filter_per_ray[ray_idx]`，改为写 `pv->ray_pinned[ray_idx]` + `pv->filter_pinned[ray_idx]`。

核心变更（每处 ray emit 点）:

```c
/* 当前 */
struct s3d_ray_request* rr = &pv->ray_requests[ray_idx];
rr->origin[0]    = p->ray_req.origin[0];
rr->origin[1]    = p->ray_req.origin[1];
rr->origin[2]    = p->ray_req.origin[2];
rr->direction[0] = p->ray_req.direction[0];
rr->direction[1] = p->ray_req.direction[1];
rr->direction[2] = p->ray_req.direction[2];
rr->range[0]     = p->ray_req.range[0];
rr->range[1]     = p->ray_req.range[1];
rr->user_id      = i;
rr->filter_data  = ...;
fill_filter_per_ray(&pv->filter_per_ray[ray_idx], p);

/* 方案 E 直写 */
struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
rp->origin_x    = p->ray_req.origin[0];
rp->origin_y    = p->ray_req.origin[1];
rp->origin_z    = p->ray_req.origin[2];
rp->direction_x = p->ray_req.direction[0];
rp->direction_y = p->ray_req.direction[1];
rp->direction_z = p->ray_req.direction[2];
rp->tmin         = p->ray_req.range[0];
rp->tmax         = p->ray_req.range[1];
fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);
```

注意：`user_id` 和 `filter_data` 不上传 GPU，但 `ray_to_slot[ray_idx]` 和 `ray_slot_sub[ray_idx]` 仍需保留（distribute 使用）。

### 3.6 gpu_launch_async 修改

```c
/* gpu_launch_async — 方案 E */
if(pool->use_gpu_filter) {
    res = s3d_scene_view_trace_rays_batch_ctx_filtered_pinned_async(
      sv, pv->batch_ctx, pv->ray_count);   /* 不再传 ray_requests / filter_per_ray */
} else {
    /* 非 L4 路径保持不变（backward compat） */
    res = s3d_scene_view_trace_rays_batch_ctx_async(
      sv, pv->batch_ctx, pv->ray_requests, pv->ray_count);
}
```

### 3.7 废弃 `ray_requests[]` 和 `filter_per_ray[]` 分析

#### 3.7.1 两个 buffer 的完整 usage 审计

**pv->ray_requests** (pageable calloc, 40B × max_rays):

| 阶段 | R/W | 路径 | 详情 |
|------|-----|------|------|
| `pool_collect_ray_requests_bucketed` Pass 2 | **WRITE** | shared | 5 处 ray emit 点（Ray 0/1/ENC 2-5/SS 2-3），OMP+串行各一份 |
| `gpu_launch_async` | **READ ptr** | L4 + 非L4 | 传指针给 oxstar-3d |
| `batch_trace_filtered_async_impl` (L4) | **READ** | L4 | AoS→SoA 转换循环读 origin/direction/range → 写入 h_rays_pinned |
| `batch_trace_async_impl` (非L4) | **READ** | 非L4 | 同上 + CPU retrace 读 filter_data 指针 |
| `batch_trace_filtered_wait_d2h_impl` (L4) | ❌ **未使用** | L4 | 签名接收但函数体不读（仅用 h_hits_pinned） |
| `batch_trace_wait_d2h_impl` (非L4) | **READ** | 非L4 | CPU filter retrace 读 origin/direction/range/filter_data |
| single-pool loop | **READ ptr** | L4+非L4 | 同 gpu_launch/wait 路径 |
| distribute / cascade | ❌ | — | 不访问 |
| drain phase | ❌ | — | 走同一 collect→launch→wait 流程 |

**pv->filter_per_ray** (pageable calloc, 16B × max_rays):

| 阶段 | R/W | 路径 | 详情 |
|------|-----|------|------|
| `pool_collect_ray_requests_bucketed` Pass 2 | **WRITE** | L4 only | 通过 `fill_filter_per_ray()` 写全部 4 字段 |
| `gpu_launch_async` | **READ ptr** | L4 only | 传指针给 oxstar-3d |
| `batch_trace_filtered_async_impl` | **READ** | L4 | `memcpy` 到 h_filter_pinned |
| 非L4 路径 | ❌ | 非L4 | 不使用 |

#### 3.7.2 保留带来的 overhead

若方案 E 直写 pinned 但保留这两个 buffer：

| 开销类型 | 影响 |
|----------|------|
| **内存浪费** | `40B × max_rays + 16B × max_rays = 56B × max_rays`。max_rays = capacity×6 = 16384×6 = 98304 → **~5.3MB/view × 2 views = ~10.6MB**。不算大但是纯浪费 |
| **calloc 初始化** | 启动时 `calloc` 10.6MB 归零，~1ms 一次性，忽略 |
| **TLB 占用** | 10.6MB pageable memory 占 ~3 个 2MB huge page 的 TLB entry，轻微增加 TLB 压力 |
| **代码复杂度** | collect 需同时写 pinned + 废弃 buffer，或保留不写但签名仍传递→死代码 |
| **运行时 overhead** | 如果不写废弃 buffer 则为 0；如果仍写则浪费 ~5-8s |

**结论：保留不写 = ~10MB 内存浪费 + 死代码。保留且仍写 = 额外 ~5-8s 运行时浪费。**

#### 3.7.3 删除涉及的修改

**Phase 1：L4 路径断开引用（方案 E 同步进行）**

| # | 文件 | 修改 |
|---|------|------|
| 1 | `sdis_solve_persistent_wavefront.c` — `gpu_launch_async` | L4 分支调用 `_pinned_async`，不再传 `pv->ray_requests` / `pv->filter_per_ray` |
| 2 | `sdis_solve_persistent_wavefront.c` — `gpu_wait_d2h`/`gpu_wait_download` | L4 分支的 `_filtered_wait_d2h` 调用改为不传 `pv->ray_requests`（反正函数体不读） |
| 3 | `ox_s3d_scene_view.cpp` — `batch_trace_filtered_wait_d2h_impl` | 从签名中去掉 `const s3d_ray_request* requests` 参数（或保留但标记为 unused） |
| 4 | `s3d.h` — `_filtered_wait_d2h` 声明 | 同步修改签名 |
| 5 | single-pool loop（drain phase） | L4 分支同步更新调用 |

**Phase 2：移除 buffer 分配（安全清理）**

| # | 文件 | 修改 |
|---|------|------|
| 1 | `sdis_solve_persistent_wavefront.h` — `pool_view` | 删除 `ray_requests`、`filter_per_ray` 字段声明 |
| 2 | `sdis_solve_persistent_wavefront.c` — `pool_view_init` | 删除 `calloc(ray_requests)` 和 `calloc(filter_per_ray)` |
| 3 | `sdis_solve_persistent_wavefront.c` — `pool_view_destroy` | 删除 `free(ray_requests)` 和 `free(filter_per_ray)` |
| 4 | `sdis_solve_persistent_wavefront.c` — collect Pass 2 | 移除所有写 `pv->ray_requests[ray_idx]` 的代码（已被 pinned 直写替代） |
| 5 | `sdis_solve_persistent_wavefront.c` — collect Pass 2 | 移除所有 `fill_filter_per_ray(&pv->filter_per_ray[ray_idx], p)` 调用（已被 pinned 直写替代） |

**Phase 2 的前提条件**：非 L4 路径也需要被处理。有两个选择：

| 选项 | 描述 | 影响 |
|------|------|------|
| **A. 仅 L4 路径删除** | 保留 `ray_requests` 用于非 L4 路径，用 `#if` 或运行时分支 | 代码分叉，复杂但安全 |
| **B. 非 L4 路径也直写** | 非 L4 的 `batch_trace_async_impl` 也改为从 pinned 读（需额外转换） | 需改非 L4 后端，工作量大 |
| **C. 强制 L4 only** | 删除非 L4 路径支持 | **最干净**——非 L4 在当前代码中已弃用(use_gpu_filter 恒为 1) |

**推荐选项 C**：当前 `use_gpu_filter` 在 main 分支恒为 `true`，非 L4 路径已是死代码。删除可简化整个 launch/wait 链。

#### 3.7.4 影响范围

| 影响 | 详情 |
|------|------|
| **测试** | `test_sdis_b4_m2_ray_bucketing.c` 会 break — 需同步更新测试中的 `pv->ray_requests` 访问 |
| **ray_sort** | `sdis_ray_sort.c` 操作 `pool->ray_requests`（不同 buffer），不受 `pv->ray_requests` 删除影响 |
| **wavefront_context** | `sdis_solve_wavefront.c` 的 `wf.ray_requests` 是独立的旧路径，不受影响 |
| **向后兼容** | 如选择选项 C 删除非 L4，需确保没有运行时 fallback 到非 L4 的路径 |

#### 3.7.5 建议实施策略

**方案 E 第一版**：Phase 1 only —— collect 直写 pinned，`gpu_launch` 改调 `_pinned_async`。**保留** `ray_requests[]` 和 `filter_per_ray[]` 但不再写入。L4 wait_d2h 签名保持不变（传 NULL 或废弃指针）。

**方案 E 第二版**：Phase 1 + Phase 2 —— 确认性能收益后，清除废弃 buffer 和非 L4 路径死代码。预估额外 ~30 行删除 + 测试文件更新。

## 4. 修改文件清单

| # | 文件 | 变更 |
|---|------|------|
| 1 | `s3d.h` | +`s3d_ray_pinned` 结构体, +`get_pinned_buffers` API, +`pinned_async` API |
| 2 | `ray_types.h` | +static_assert `Ray` 与 `s3d_ray_pinned` layout 一致 |
| 3 | `ox_s3d_internal.h` | 无变更（pinned buffer 已存在） |
| 4 | `ox_s3d_scene_view.cpp` | +`batch_trace_filtered_pinned_async_impl`, +`get_pinned_buffers_impl` |
| 5 | `sdis_solve_persistent_wavefront.h` | pool_view +`ray_pinned`, `filter_pinned` 指针 |
| 6 | `sdis_solve_persistent_wavefront.c` | pool_view 初始化借用指针; collect Pass 2 scatter 改写; `gpu_launch_async` 分支 |

**总计: 6 文件, 预估 +120/-60 行**

## 5. 实施步骤

### Step 1: 添加 C 可见 Ray 结构体

- `s3d.h` 添加 `s3d_ray_pinned` (32B, 与 `Ray` binary compatible)
- `ray_types.h` 添加 `static_assert(sizeof(Ray) == sizeof(s3d_ray_pinned))`

### Step 2: 添加 pinned buffer 访问 API

- `s3d.h` 声明 `s3d_batch_trace_context_get_pinned_buffers`
- `ox_s3d_scene_view.cpp` 实现（返回 `ctx->h_rays_pinned` cast 为 `s3d_ray_pinned*`）

### Step 3: 添加 pinned async launch

- `s3d.h` 声明 `s3d_scene_view_trace_rays_batch_ctx_filtered_pinned_async`
- `ox_s3d_scene_view.cpp` 实现 `batch_trace_filtered_pinned_async_impl`（核心：删掉转换循环和 memcpy，直接 uploadAsync）

### Step 4: 求解器侧集成

- `sdis_solve_persistent_wavefront.h` — pool_view 添加借用指针
- `sdis_solve_persistent_wavefront.c`:
  - pool_view 初始化时调用 `get_pinned_buffers` 获取指针
  - collect Pass 2 scatter 中替换写目标（5 处 ray emit 点）
  - `gpu_launch_async` 调用 pinned_async 版本

### Step 5: 验证

- 编译通过
- porous 320×320×32 运行，逐像素对比 GPU/CPU 结果一致（容差 1e-6）
- timing 对比：预期 collect+gpu_launch 从 ~109s 降至 ~25-28s

## 6. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| `s3d_ray_pinned` 与 `Ray` layout 不一致 | 静默数据错误 | static_assert 编译期检查 |
| Dual-buffer A/B 共享 pinned buffer 竞争 | 数据覆盖 | 每个 pool_view 有独立 batch_ctx → 独立 pinned buffer |
| Single-pool 路径（含 drain phase）需同步更新 | 遗漏导致 drain 仍用旧路径 | drain 走 single-pool 循环，同样有 L4 分支，需一并修改 `_filtered_async` 调用 |
| OMP collect 写 pinned memory 的 NUMA 亲和性 | 性能不如预期 | pinned memory 是 `cudaHostAllocDefault` (WB cached)，与常规内存一致 |

## 7. 后续增量优化

- collect Pass 2 精简 per-ray 工作（stats 统计移至 Pass 1，减少 Pass 2 分支开销）

---

*实施计划创建: 2026-03-05 | 预估改动量: 6 文件, +120/-60 行*
