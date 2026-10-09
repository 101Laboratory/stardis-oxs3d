# Solver → oxstar-3d 三类查询调用链审查

**审查日期**: 2026-03-20  
**代码版本**:
- Solver: `stardis-solver/0.16.2`
- Backend: `oxstar-3d/0.10`

---

## 概述

Wavefront 求解器中的三类几何查询（RT / CP / ENC）经由统一的异步管线进入 oxstar-3d 后端。
调用链的分层结构如下：

```
solver 步骤函数 (sdis_wf_steps_*.c)
    │  填充请求结构体 / 设置路径状态
    ▼
wavefront pool (sdis_solve_persistent_wavefront.c)
    │  收集同类请求 → pool_view.{ray,cp,enc}_requests[]
    │  gpu_launch_all() — 三类请求并发提交，立即返回
    ▼
s3d C API (oxstar-3d/s3d_wrapper/s3d.h)
    │  s3d_scene_view_{trace_rays,closest_point,find_enclosure}_batch_ctx_async()
    ▼
C++ wrapper (ox_s3d_scene_view.cpp)
    │  batch_{trace,cp,enc}_async_impl()
    │  · H2D 上传（pinned memory → CUDA device）
    │  · optixLaunch() 派发到对应 SBT
    ▼
UnifiedTracer (unified_tracer.cpp)
    │  traceBatchMultiHit() / closestPointBatch() / traceBatch()
    ▼
OptiX Pipeline (GPU device programs)
    · programs.cu      — RT raygen/CH/MS (SBT_RT / SBT_MH / SBT_MHF)
    · nn_programs.cu   — CP raygen/IS/CH (SBT_CP)
```

---

## 1. RT 查询（Radiative + Conductive delta-sphere 光线）

### 1.1 触发状态

| 路径状态（`path_phase`） | 触发业务 |
|--------------------------|---------|
| `PATH_RAD_TRACE_PENDING` | 辐射路径的主追踪光线 |
| `PATH_COUPLED_BOUNDARY_REINJECT` | 边界路径的重入射方向光线 |
| `PATH_COUPLED_COND_DS_PENDING` | 导热 delta-sphere 的两条步进光线 |
| `PATH_CND_DS_STEP_TRACE` | 导热 DS 步进方向光线 (dir0 + dir1) |
| `PATH_CND_WOS_FALLBACK_TRACE` | WoS 退化回退光线 |
| `PATH_BND_SS_REINJECT_SAMPLE` | S-S 边界重入射光线（4 射线） |
| `PATH_BND_SF_REINJECT_SAMPLE` | S-F 边界重入射光线 |
| `PATH_BND_SF_NULLCOLL_RAD_TRACE` | 零碰撞辐射光线 |
| 其他 `[R]` 标注状态 | 见 `sdis_wf_types.h` path_phase 枚举 |

所有上述状态：solver 步骤函数在 `path_state.ray_req` 填充 origin/direction/range，并设置 `hot->needs_ray = 1`。

### 1.2 Solver 侧调用链

```c
// sdis_wf_steps_core.c — 以辐射光线为例
step_radiative_submit(p, hot, pos, /*...*/):
    p->ray_req.origin[0..2]    = pos[0..2]
    p->ray_req.direction[0..2] = p->rad_direction[0..2]
    p->ray_req.range[0]        = 0.0f
    p->ray_req.range[1]        = FLT_MAX
    p->ray_req.ray_count       = 1
    hot->needs_ray             = 1
    hot->phase                 = PATH_RAD_TRACE_PENDING

// sdis_solve_persistent_wavefront.c
// 每轮 wavefront compaction 后：
collect_phase: pool_view.ray_requests[ray_idx] ← p->ray_req   // (stream compaction)
//              pool_view.ray_count += 1

gpu_launch_all(pool, pv, sv):
    if use_gpu_filter:
        s3d_scene_view_trace_rays_batch_ctx_filtered_pinned_async(sv, pv->batch_ctx, pv->ray_count)
    else:
        s3d_scene_view_trace_rays_batch_ctx_async(sv, pv->batch_ctx, pv->ray_requests, pv->ray_count)
    → pv->gpu_pending = 1

// 后续：
gpu_sync_kernel_all():   s3d_scene_view_trace_rays_batch_ctx_{filtered_}sync_kernel(ctx)
gpu_start_d2h():         s3d_scene_view_trace_rays_batch_ctx_{filtered_}start_d2h(ctx, nrays)
gpu_wait_d2h():          s3d_scene_view_trace_rays_batch_ctx_{filtered_}wait_d2h(sv, ctx, requests, nrays, hits, stats)
```

### 1.3 Backend 侧实现（ox_s3d_scene_view.cpp）

```cpp
// batch_trace_async_impl()
1. AoS→SoA: s3d_ray_request[] → ctx->h_rays_pinned[] (Ray 对齐格式)
   (filtered 模式下直接写入 pinned buffer，避免拷贝)
2. H2D 异步上传: ctx->d_rays.uploadAsync(h_rays_pinned, count, transfer_stream)
3. cudaEventRecord(evt_upload_done, transfer_stream)
4. cudaStreamWaitEvent(compute_stream, evt_upload_done)   // 串联两流
5. sv->tracer.traceBatchMultiHit(
       ctx->d_rays.get(), ctx->d_multi_hits.get(), count,
       ctx->compute_stream, ctx->params_ptr)
   → optixLaunch(m_pipeline, compute_stream, params_ptr,
                 sizeof(UnifiedParams), &m_sbt_mh, w, h, 1)
6. cudaEventRecord(evt_kernel_done, compute_stream)
// GPU 异步执行，CPU 立即返回
```

**Filtered 模式 (L4)**：使用 `m_sbt_mhf` (SBT_MHF)，Raygen 在 GPU 内联过滤自交 + epsilon + enclosure，输出单命中 `HitResult[]`，无需 CPU 重追踪。

**非 Filtered 模式**：使用 `m_sbt_mh`，Raygen 输出 Top-K 多命中 `MultiHitResult[]`，D2H 后 CPU post-process 过滤并按需重追踪。

### 1.4 GPU Device Program（programs.cu）

```
RayGen (SBT_MH / SBT_MHF):
    d_rays[launchIdx] → optixTrace(handle, origin, dir, tmin, tmax, ...)
    → RT Core 硬件三角形求交（triangle GAS, activeRTHandle()）
    → AnyHit: 收集多命中到 payload
    → ClosestHit / Miss: 写 MultiHitResult 到 d_multi_hits[launchIdx]
```

---

## 2. CP 查询（Conductive WoS 最近面投影）

### 2.1 触发状态

| 路径状态 | 触发业务 |
|----------|---------|
| `PATH_CND_WOS_CLOSEST` | Walk-on-Spheres: 查询当前点到最近几何面距离（确定球半径） |
| `PATH_CND_WOS_DIFFUSION_CHECK` | WoS: 随机跳跃后验证新位置合法性（偏移是否越面） |

Solver 步骤设置 `hot->needs_ray = 0`（非光线请求），由独立的 CP 批次收集逻辑处理。

### 2.2 Solver 侧调用链

```c
// sdis_wf_steps_cnd.c (WoS conductive steps)
step_wos_closest_submit(p, hot, pos):
    // 填充 cp_locate 子结构（位置 + return_state）
    hot->phase = PATH_CND_WOS_CLOSEST   // or DIFFUSION_CHECK

// sdis_solve_persistent_wavefront.c
// compaction 阶段，独立于 needs_ray 路径：
collect_cp: pool_view.cp_requests[cp_idx].pos = p->wos.query_pos
            pool_view.cp_requests[cp_idx].radius = FLT_MAX   // 无限球
            pool_view.cp_to_slot[cp_idx] = slot_id
            pool_view.cp_count += 1

gpu_launch_all(pool, pv, sv):
    s3d_scene_view_closest_point_batch_ctx_async(
        sv, pv->cp_batch_ctx, pv->cp_requests, pv->cp_count)
    → pv->cp_gpu_pending = 1

gpu_sync_kernel_all(): s3d_scene_view_closest_point_batch_ctx_sync_kernel(ctx)
gpu_start_d2h_all():   s3d_scene_view_closest_point_batch_ctx_start_d2h(ctx, n)
gpu_wait_d2h_all():    s3d_scene_view_closest_point_batch_ctx_wait_d2h(...)
                       → pool_distribute_cp_results()
                          → p->locals.cnd_wos.cached_hit = cp_hits[k]
                          → phase = PATH_CND_WOS_CLOSEST_RESULT (or DIFFUSION_CHECK_RESULT)
```

### 2.3 Backend 侧实现（ox_s3d_scene_view.cpp）

```cpp
// batch_cp_async_impl()
1. 半径检查：若请求中最大 radius > sv->search_radius，触发 rebuildQueryGAS()
2. AoS→SoA: s3d_cp_request[] → ctx->h_queries_pinned[] (CPQuery 格式)
3. H2D 异步上传: ctx->d_queries.uploadAsync(h_queries_pinned, count, transfer_stream)
4. cudaStreamWaitEvent(compute_stream, evt_upload_done)
5. sv->tracer.closestPointBatch(
       ctx->d_queries.get(), ctx->d_results.get(), count,
       ctx->compute_stream, ctx->params_ptr)
   → optixLaunch(m_pipeline, compute_stream, params_ptr,
                 sizeof(UnifiedParams), &m_sbt_cp, w, h, 1)
   注意: handle = m_aabb_gas_handle (AABB proxy GAS，非三角形 GAS)
6. cudaEventRecord(evt_kernel_done, compute_stream)
```

### 2.4 GPU Device Program（nn_programs.cu）

```
RayGen (SBT_CP):
    d_cp_queries[launchIdx] → {position, radius}
    → 构造 AABB proxy 零长度射线 (origin=position, dir=near-zero, tmax=radius)
    → optixTrace(m_aabb_gas_handle, ...)
    → 对每个 AABB proxy 触发 Intersection Program
Intersection Program:
    · 读取 RayData.nn_vertices[prim_idx*3+0..2]（三角形三顶点）
    · 计算精确点-三角形距离（CPU-style exact）
    · 若 distance <= query.radius → 报告为命中（optixReportIntersection）
ClosestHit:
    · 从 payload 更新最近命中 prim_idx + distance
    · 写 CPResult 到 d_cp_results[launchIdx]
```

**核心设计**：AABB proxy 的 AABB 是三角形轴对齐包围盒扩展 search_radius；RT Core 用于 BVH 加速遍历（无硬件三角形求交），SM 执行 Intersection Program 完成精确距离计算。

---

## 3. ENC 查询（Enclosure 归属判定）

### 3.1 触发状态

| 路径状态 | 触发业务 |
|----------|---------|
| `PATH_ENC_LOCATE_PENDING` | 路径初始化或边界转换后判断点在哪个 enclosure |
| `PATH_CND_INIT_ENC` | 导热路径开始时的 enclosure 初始化 |
| `PATH_CND_DS_STEP_ENC_VERIFY` | DS 步进后的 enclosure 一致性验证 |
| `PATH_BND_SS_REINJECT_ENC` | S-S 重入射后的 enclosure 确认 |
| `PATH_BND_SF_REINJECT_ENC` | S-F 重入射后的 enclosure 确认 |

### 3.2 Solver 侧调用链

```c
// sdis_wf_steps_enc.c
step_enc_locate_submit(p, hot, enc, pos, return_state):
    enc->locate.query_pos[0..2] = pos[0..2]
    enc->locate.return_state    = return_state
    enc->locate.batch_idx       = (uint32_t)-1   // 待 pool 填写
    hot->needs_ray              = 0              // 独立批次，不走 ray 队列
    hot->phase                  = PATH_ENC_LOCATE_PENDING

// sdis_solve_persistent_wavefront.c
// compaction 阶段：
collect_enc: pool_view.enc_locate_requests[enc_idx].pos   = enc->locate.query_pos
             pool_view.enc_locate_requests[enc_idx].user_id = slot_id
             pool_view.enc_locate_count += 1

gpu_launch_all(pool, pv, sv):
    s3d_scene_view_find_enclosure_batch_ctx_async(
        sv, pv->enc_batch_ctx, pv->enc_locate_requests, pv->enc_locate_count)
    → pv->enc_gpu_pending = 1

gpu_sync_kernel_all(): s3d_scene_view_find_enclosure_batch_ctx_sync_kernel(ctx)
gpu_start_d2h_all():   s3d_scene_view_find_enclosure_batch_ctx_start_d2h(ctx, n)
gpu_wait_d2h_all():    s3d_scene_view_find_enclosure_batch_ctx_wait_d2h(...)
                       → pool_distribute_enc_locate_results()
                          → enc->locate.prim_id  = result.prim_id
                          → enc->locate.side     = result.side
                          → enc->locate.distance = result.distance
                          → phase = PATH_ENC_LOCATE_RESULT
```

### 3.3 Backend 侧实现（ox_s3d_scene_view.cpp）

ENC 查询在 backend 内部被分解为**两个 GPU kernel**，在同一 compute_stream 上背靠背发射：

```cpp
// batch_enc_async_impl()
1. 为每个 enc 请求同时准备两类 GPU 查询（写入 pinned buffers）：
   · CP query: h_cp_queries_pinned[i] = {position, radius=1e30f}
   · RT ray:   h_rays_pinned[i] = {origin=position, dir=(1,0,0), tmin=1e-6, tmax=1e30}

2. H2D 异步上传（双缓冲，同一 transfer_stream）：
   ctx->d_cp_queries.uploadAsync(h_cp_queries_pinned, count, transfer_stream)
   ctx->d_rays.uploadAsync(h_rays_pinned, count, transfer_stream)
   cudaEventRecord(evt_upload_done, transfer_stream)
   cudaStreamWaitEvent(compute_stream, evt_upload_done)

3. Step 1 — CP kernel（compute_stream）：
   sv->tracer.closestPointBatch(d_cp_queries, d_cp_results, count,
                                 compute_stream, params_ptr_cp)
   → m_sbt_cp，AABB GAS，同 §2
   → 输出 CPResult[i].{prim_idx, distance}

4. Step 2 — RT kernel（同 compute_stream，Step 1 之后入队，无需 sync）：
   sv->tracer.traceBatch(d_rays, d_hits, count,
                         compute_stream, params_ptr_rt)
   → m_sbt_rt，三角形 GAS，RT Core 硬件求交
   → 输出 HitResult[i].{t, normal[3]}

5. cudaEventRecord(evt_kernels_done, compute_stream)
// GPU 上 CP 与 RT 串行（在同一 stream 上），但与 CPU 异步
```

### 3.4 CPU 后处理（结果合并）

```cpp
// batch_enc_wait_d2h_impl()
// D2H: transfer_stream 等待 evt_kernels_done，下载 d_cp_results + d_hits

for i in 0..nqueries:
    cp = h_cp_results_pinned[i]    // nearest prim + distance
    rt = h_rt_results_pinned[i]    // +X ray hit result

    if cp.distance < 0:            // 未找到面 → 查询失败
        result.side = -1; result.enc_id = INVALID
    elif cp.distance < 1e-8:       // 退化（点在面上）
        result.side = -1
    elif rt.t < 0:                 // +X 射线未命中 → 点在所有 enclosure 外
        result.side = 0            // outside
    else:
        dot_dn = rt.normal[0]      // dot((1,0,0), hit_normal) = normal.x
        result.side = (dot_dn < 0) ? 0 : 1
        // 法线朝内（与射线反向）→ 从外打进 → side=0 (outside from enclosure perspective)
        // 法线朝外（与射线同向）→ 从内打出 → side=1 (inside)

    result.prim_id  = cp.prim_idx
    result.distance = cp.distance
    result.enc_id   = INVALID       // 由 Solver 通过 prim_props 查表解析
```

---

## 4. 数据流与 SBT 对照表

| 查询 | Solver 收集字段 | s3d API | UnifiedTracer 方法 | SBT | GAS | Device Program |
|------|----------------|---------|-------------------|-----|-----|----------------|
| RT（无 filter） | `pv->ray_requests[]` | `trace_rays_batch_ctx_async` | `traceBatchMultiHit` | `m_sbt_mh` | triangle GAS | `programs.cu` RayGen/AH/CH |
| RT（GPU filter） | `pv->ray_pinned[]`（pinned 直写） | `trace_rays_batch_ctx_filtered_pinned_async` | `traceBatchMultiHitFiltered` | `m_sbt_mhf` | triangle GAS | `programs.cu` filtered RayGen |
| CP | `pv->cp_requests[]` | `closest_point_batch_ctx_async` | `closestPointBatch` | `m_sbt_cp` | AABB proxy GAS | `nn_programs.cu` RayGen/IS/CH |
| ENC (Step 1 CP) | `pv->enc_locate_requests[]` | `find_enclosure_batch_ctx_async` | `closestPointBatch` (内部) | `m_sbt_cp` | AABB proxy GAS | `nn_programs.cu` |
| ENC (Step 2 RT) | （同上，同一请求） | （同上） | `traceBatch` (内部) | `m_sbt_rt` | triangle GAS | `programs.cu` single-hit RayGen |

---

## 5. 异步管线时序

```
CPU:  [fill requests] → gpu_launch_all() → [CPU cascade steps] → gpu_sync_kernel_all() → gpu_start_d2h() → [CPU work] → gpu_wait_d2h() → distribute results

GPU:  (idle)           [H2D transfer]     [RT kernel ‖ CP kernel ‖ ENC_CP kernel → ENC_RT kernel]   [D2H]

Stream 分配:
  transfer_stream:  H2D upload → wait evt_kernels_done → D2H download
  compute_stream:   wait evt_upload_done → kernel(s)
```

RT / CP / ENC 三类查询在 `gpu_launch_all()` 中**并发提交**到 GPU（三个 compute_stream 各自独立），由于没有 kernel 间依赖，三条流可并行执行（受 SM 资源决定实际并行度）。  
例外：ENC 内部的 CP + RT 两步 kernel 串行在同一 stream 上（有数据依赖关系）。

---

## 6. 关键源文件索引

| 文件 | 层次 | 职责 |
|------|------|------|
| `stardis-solver/0.16.2/src/sdis_wf_types.h` | Solver | `path_phase` 枚举（所有状态定义） |
| `stardis-solver/0.16.2/src/sdis_wf_steps_core.c` | Solver | RT 光线请求构造（radiative/DS/WoS fallback） |
| `stardis-solver/0.16.2/src/sdis_wf_steps_enc.c` | Solver | ENC 查询请求构造 + 结果分发 |
| `stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c` | Solver | CP 查询请求构造（WoS conductive） |
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | Solver | Wavefront pool 调度、`gpu_launch_all`、收集与分发 |
| `oxstar-3d/0.10/s3d_wrapper/s3d.h` | C API | 所有 `s3d_scene_view_*` 函数声明 + 数据结构 |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | C++ Wrapper | 三类查询的 async_impl + sync_kernel + d2h + 后处理 |
| `oxstar-3d/0.10/src/unified_tracer.h/.cpp` | Core | `traceBatchMultiHit` / `closestPointBatch` / `traceBatch` → `optixLaunch` |
| `oxstar-3d/0.10/device/programs.cu` | GPU | RT raygen/AH/CH（SBT_RT / SBT_MH / SBT_MHF） |
| `oxstar-3d/0.10/device/nn_programs.cu` | GPU | CP raygen/IS/CH（SBT_CP，AABB proxy 求交） |
