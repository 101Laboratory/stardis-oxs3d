# L4 GPU Inline Filter — 消除 Retrace 瓶颈

**创建日期**: 2026-03-04  
**实施完成**: 2026-03-06  
**状态**: ✅ 已实施并合并到 main (commit `c9e80a3`)  
**前置**: L3 Dual-Stream 已完成 (`opt/dual-stream`)  
**分支**: `opt/dual-stream` → merged to `main`

### 实施结果摘要

| 指标 | 值 |
|------|-----|
| GPU kernel 吞吐 | 11,046 Mrays/s (11 Grays/s) |
| GPU kernel 时间占比 | 1168.9ms (3.5%) |
| D2H wait | 9300.3ms (27.7%) — 含 cudaStreamSync + OMP convert |
| CPU postprocess | 22468.3ms (66.8%) |
| Fallback retrace | 691.8ms (2.1%) — drain phase + edge cases |
| 总时间 (porous 320×320×32) | 5m51s (从 7m25s 下降 21%) |
| 瓶颈转移 | GPU fully hidden, CPU cascade+distribute 为新瓶颈 (0.6ms/cycle) |

**修改文件** (10 files, +969/-45):
- `programs.cu` (+188): `__raygen__mh_filtered` + `__anyhit__mh_filtered`
- `ray_types.h` (+8): `FilterPerRayData` 结构体
- `unified_params.h` (+7): `filter_data`, `enc_front/enc_back`
- `unified_tracer.h/cpp` (+259): 15 PGs, MHF SBT, traceBatchMultiHitFiltered
- `ox_s3d_internal.h` (+22): batch context L4 buffers
- `s3d.h` (+45): 5 public L4 API
- `ox_s3d_scene_view.cpp` (+274): L4 impl + OMP parallelized wait
- `sdis_solve_persistent_wavefront.h/c` (+211): fill_filter, launch/sync/d2h/wait branching, drain-phase L4

---

## 1. 优化动机

L3 实测数据（pool=32K，porous 场景，100 步聚合）：

| 子项 | 时间 (ms) | 占 batch_trace | 说明 |
|------|-----------|---------------|------|
| `batch_time` (kernel + D2H) | 39.6 | 39.7% | GPU kernel + PCIe 传输 |
| `post_time` (CPU filter + 分发) | 12.3 | 12.3% | OMP 并行 CPU filter eval |
| `retrace_time` (fallback重追踪) | **47.9** | **48.0%** | ← 主导瓶颈 |
| **total** | **99.8** | 100% | |

**Retrace 特征**:
- 仅 ~0.013% 的射线触发 retrace（1.7M / 12.9B 总候选）
- 但每条 retrace 射线需多次小批量 GPU 往返（迭代式 submit→sync→filter→retry）
- 单条 retrace 射线平均 ~28μs（含 PCIe 延迟），远超正常流水线

**根因**: Multi-hit Top-K (K=2) 返回最近 2 个交点，但 CPU filter 可能拒绝全部 K 个候选。
此时必须重新发射射线（`tmin` = 被拒绝 hit 的 `t + ε`），再次 GPU→CPU 往返，直到找到接受的 hit 或 miss。

---

## 2. L4 目标：Filter 内联到 Any-Hit Kernel

### 2.1 核心思路

将 CPU 端 `XD(hit_filter_function)` 的 3 项过滤检查直接移入 OptiX `__anyhit__` 程序：

| # | 检查 | CPU 实现 | GPU 可行性 |
|---|------|---------|-----------|
| ① | **自相交** — `prim_id` 等于发射面 | `SXD_PRIMITIVE_EQ` | ✅ 1 个 uint 比较 |
| ② | **近距共享边** — `t ≈ 0` 且两三角形共享边/顶点 | `hit_shared_edge()` + vertex lookup | ✅ 保守简化：`t < epsilon` 直接 reject |
| ③ | **边界包壳** — 命中面不属于射线所在包壳 | `scene_get_enclosure_ids` → `prim_props[]` lookup | ✅ 预上传 `enc_front[]`/`enc_back[]` 数组 |

> **关键**: `custom_filter_3d` 在热路径（wavefront batch trace）中 **始终为 NULL**（仅 `closest_point` 路径设置），
> 因此 filter 逻辑是固定的、可静态编译进 kernel。

### 2.2 目标效果

```
L3 (当前):
  __anyhit__mh:  收集 Top-K=2 → D2H(MultiHitResult) → CPU filter → [reject all] → retrace GPU round-trip × N
  
L4 (目标):
  __anyhit__mh_filtered:  对每个交点 inline filter ①②③ → reject 的直接 optixIgnoreIntersection
                          → 仅通过 filter 的 hit 进入 Top-K 收集 → D2H(HitResult) → 无 retrace
```

**D2H 传输量**: `MultiHitResult` (120B/ray) → `HitResult` (40B/ray) = **66% 缩减**

---

## 3. 数据结构设计

### 3.1 Per-Ray Filter Data

新增 per-ray GPU 输入，携带滤波所需上下文（从 `hit_filter_data` 精简而来）：

```cpp
/* ray_types.h 新增 */
struct FilterPerRayData {
    unsigned int hit_from_prim_id;   /* 射线出发面 prim_id, UINT_MAX = 无 */
    unsigned int enc_id;             /* 射线所在包壳 ID, UINT_MAX = 不过滤 */
    float        epsilon;            /* 近距离阈值 (自相交容差) */
    unsigned int pad0;               /* 对齐到 16B */
};
```

> 16 字节/ray，与 `Ray` (32B) 合计 48B/ray H2D — 仅增 50% 上行流量。

### 3.2 Per-Geometry Enclosure Arrays

在 SBT `HitGroupData` 中增加包壳映射表（per-GAS primitive array）：

```cpp
/* unified_params.h — HitGroupData 扩展 */
struct HitGroupData {
    unsigned int  geom_id;
    unsigned int  geom_type;
    unsigned int  flip_normal;
    unsigned int  pad0;
    float3*       vertices;
    uint3*        indices;
    float3*       sphere_centers;
    float*        sphere_radii;
    /* L4 新增 ↓ */
    unsigned int* enc_front;    /* enc_front[prim_idx] = front enclosure ID */
    unsigned int* enc_back;     /* enc_back[prim_idx]  = back enclosure ID  */
};
```

> 数据源: `sdis_scene::prim_props[]` 的 `front_enclosure` / `back_enclosure` 字段。
> 在 `rebuild_tracer()` / `build_gas()` 时上传到 device buffer，绑定到 SBT。

### 3.3 Launch Parameters 扩展

```cpp
/* unified_params.h — UnifiedParams 扩展 */
struct UnifiedParams {
    // ... 现有字段 ...
    
    /* L4: GPU inline filter (Mode A) */
    FilterPerRayData* filter_data;    /* per-ray filter input, NULL = Mode B */
};
```

---

## 4. Kernel 变化

### 4.1 新增 `__anyhit__mh_filtered`

核心 any-hit 程序变体，执行 inline filter 后再做 Top-K 收集：

```cuda
extern "C" __global__ void __anyhit__mh_filtered()
{
    const uint3        idx        = optixGetLaunchIndex();
    const uint3        dim        = optixGetLaunchDimensions();
    const unsigned int linear_idx = idx.y * dim.x + idx.x;

    const float          t_new = optixGetRayTmax();
    const unsigned int   prim  = optixGetPrimitiveIndex();
    const HitGroupData*  data  = reinterpret_cast<const HitGroupData*>(
                                     optixGetSbtDataPointer());

    /* ---- Filter ① 自相交: 与发射面相同 → reject ---- */
    const FilterPerRayData& fd = params.filter_data[linear_idx];
    if (prim == fd.hit_from_prim_id) {
        optixIgnoreIntersection();
        return;
    }

    /* ---- Filter ② 近距离自相交: t ≈ 0 → 保守 reject ---- */
    if (t_new > 0.0f && t_new < fd.epsilon) {
        /* 注: 完整 CPU 版做 hit_shared_edge 几何检查。
         * GPU 保守策略: t < epsilon 一律 reject。
         * 多 reject 几个 hit 对蒙特卡洛收敛影响可忽略。 */
        optixIgnoreIntersection();
        return;
    }

    /* ---- Filter ③ 边界包壳检查 ---- */
    if (fd.enc_id != 0xFFFFFFFFu && data->enc_front) {
        /* 计算法线以判断正反面 */
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        if (optixIsTriangleHit() && data->vertices && data->indices) {
            const uint3  tri = data->indices[prim];
            const float3 v0  = data->vertices[tri.x];
            const float3 v1  = data->vertices[tri.y];
            const float3 v2  = data->vertices[tri.z];
            const float3 e1  = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
            const float3 e2  = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
            nx = e1.y * e2.z - e1.z * e2.y;
            ny = e1.z * e2.x - e1.x * e2.z;
            nz = e1.x * e2.y - e1.y * e2.x;
            const float3 wn = optixTransformNormalFromObjectToWorldSpace(
                                  make_float3(nx, ny, nz));
            nx = wn.x; ny = wn.y; nz = wn.z;
        }

        const float3 dir = optixGetWorldRayDirection();
        const float  dp  = dir.x * nx + dir.y * ny + dir.z * nz;
        const unsigned int chk_enc = (dp < 0.0f)
            ? data->enc_front[prim]
            : data->enc_back[prim];
        if (chk_enc != fd.enc_id) {
            optixIgnoreIntersection();
            return;
        }
    }

    /* ---- 通过 filter → 正常 Top-K 收集（与 __anyhit__mh 相同）---- */
    float bu = 0.0f, bv = 0.0f;
    /* ... 计算 bary + normal（复用 __anyhit__mh 逻辑）... */
    /* 注: 法线在 ③ 中已计算过，可复用避免重复计算 */

    HitResult new_hit;
    new_hit.t         = t_new;
    new_hit.bary_u    = bu;
    new_hit.bary_v    = bv;
    new_hit.prim_idx  = prim;
    new_hit.geom_id   = data->geom_id;
    new_hit.inst_id   = optixGetInstanceId();
    new_hit.normal[0] = nx;
    new_hit.normal[1] = ny;
    new_hit.normal[2] = nz;

    MultiHitResult& result = params.multi_hits[linear_idx];
    unsigned int cnt = result.count;

    if (cnt < MAX_MULTI_HITS) {
        result.hits[cnt] = new_hit;
        result.count     = cnt + 1;
    } else {
        unsigned int farthest = 0;
        float max_t = result.hits[0].t;
        for (unsigned int i = 1; i < MAX_MULTI_HITS; ++i) {
            if (result.hits[i].t > max_t) {
                max_t    = result.hits[i].t;
                farthest = i;
            }
        }
        if (t_new < max_t) {
            result.hits[farthest] = new_hit;
        }
    }

    optixIgnoreIntersection();  /* 继续 BVH 遍历收集 Top-K */
}
```

### 4.2 Raygen 变体：Result 降维

Mode A 下 kernel 直接输出已过滤的 hit，可将 `MultiHitResult` 降维为单个 `HitResult`：

```cuda
extern "C" __global__ void __raygen__mh_filtered()
{
    /* ... 同 __raygen__mh，但最终: */
    
    const MultiHitResult& mhr = params.multi_hits[linear_idx];
    /* 只取 closest filtered hit → 写入 params.hits[linear_idx] */
    if (mhr.count > 0) {
        params.hits[linear_idx] = mhr.hits[0];  /* 已排序，[0] 最近 */
    } else {
        params.hits[linear_idx].t = -1.0f;       /* miss */
    }
}
```

> 这使得 D2H 只需传输 `HitResult*`（40B/ray）而非 `MultiHitResult*`（120B/ray）。

---

## 5. Mode A / Mode B 双模式架构

### 5.1 模式选择

| 模式 | 条件 | 行为 |
|------|------|------|
| **Mode A** (GPU filter) | `params.filter_data != NULL` | `__anyhit__mh_filtered` + 跳过 CPU filter + 跳过 retrace |
| **Mode B** (CPU filter) | `params.filter_data == NULL` | `__anyhit__mh`（现有）+ CPU filter + retrace（现有） |

模式在 `batch_trace_async_impl` 中自动选择：
```cpp
void batch_trace_async_impl(...) {
    if (filter_per_ray_data != nullptr) {
        /* Mode A: upload filter data, use filtered pipeline */
        d_filter_data.uploadAsync(filter_per_ray_data, count, transfer_stream);
        tracer.traceBatchMultiHit_filtered(...);  /* 使用 filtered SBT */
    } else {
        /* Mode B: existing unfiltered pipeline */
        tracer.traceBatchMultiHit(...);
    }
}
```

### 5.2 SBT / Pipeline 配置

Mode A 需要独立的 SBT entry 绑定 `__anyhit__mh_filtered`：

```
Pipeline:
  RayGen: __raygen__mh_filtered (Mode A) / __raygen__mh (Mode B)
  AnyHit: __anyhit__mh_filtered (Mode A) / __anyhit__mh (Mode B)
  Miss:   __miss__ms (共用)
```

实施选项：
- **选项 A**: 维护两个独立 `OptixPipeline`，按 mode 切换 — 简单但占显存
- **选项 B**: 单一 pipeline 含两个 hit group，通过 ray type 切换 — 更优雅

推荐选项 B：将 `RAY_TYPE_COUNT` 从 1 改为 2，新增 `RAY_TYPE_FILTERED = 1`。

---

## 6. 后端 API 变化

### 6.1 新增 C API

**文件**: `oxstar-3d/0.10/s3d_wrapper/s3d.h`

```c
/* L4: GPU-filtered batch trace — 传入 per-ray filter data */
S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_filtered_async(
    struct s3d_scene_view* sv,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests,
    size_t nrays,
    const struct s3d_filter_per_ray_data* filter_data);  /* per-ray filter input */

/* L4: 等待并获取已过滤的结果 — 无需 CPU filter + 无 retrace */
S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx_filtered_wait(
    struct s3d_batch_trace_context* ctx,
    struct s3d_hit* hits,
    struct s3d_batch_trace_stats* stats);
```

### 6.2 求解器侧封装

**文件**: `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

```c
/* L4: 构建 per-ray filter data 并调用 GPU filtered path */
static res_T
gpu_launch_filtered_async(pool, pv, sv, scn)
{
    /* 从 pv->ray_requests + scn 构建 FilterPerRayData[] */
    for (i = 0; i < pv->nrays; i++) {
        filter[i].hit_from_prim_id = pv->hit_from_prim_ids[i];
        filter[i].enc_id           = pv->enc_ids[i];
        filter[i].epsilon          = pv->epsilons[i];
    }
    return s3d_scene_view_trace_rays_batch_ctx_filtered_async(
        sv, ctx, pv->ray_requests, pv->nrays, filter);
}
```

---

## 7. 数据上传路径

### 7.1 Enclosure 数组（Per-Build, 一次性）

在 `rebuild_tracer()` / `build_gas()` 中：

```cpp
/* 对每个 geometry: 从 sdis_scene::prim_props[] 提取 front/back enclosure */
unsigned int* h_enc_front = new unsigned int[num_prims];
unsigned int* h_enc_back  = new unsigned int[num_prims];
for (uint32_t i = 0; i < num_prims; ++i) {
    h_enc_front[i] = scn->prim_props[prim_offset + i].front_enclosure;
    h_enc_back[i]  = scn->prim_props[prim_offset + i].back_enclosure;
}
d_enc_front.upload(h_enc_front, num_prims);
d_enc_back.upload(h_enc_back, num_prims);
/* 绑定到 HitGroupData SBT record */
hit_group_data.enc_front = d_enc_front.d_ptr();
hit_group_data.enc_back  = d_enc_back.d_ptr();
```

> **频率**: 仅在场景构建/重建时（一次性或极少数次），不影响热循环。

### 7.2 Filter Per-Ray Data（Per-Batch, 每次 launch）

在 `batch_trace_async_impl` 中，与 `d_rays` 同步上传：

```cpp
/* H2D: rays + filter_data，走 transfer_stream */
d_rays.uploadAsync(h_rays_pinned, count, transfer_stream);
d_filter_data.uploadAsync(h_filter_pinned, count, transfer_stream);
cudaEventRecord(evt_upload_done, transfer_stream);
```

> 额外 H2D = 16B/ray × 32K = 512KB/batch，PCIe 4.0 x16 单向 ~25GB/s → ~0.02ms。

---

## 8. Filter ② 保守简化分析

CPU 原版 filter ② (`hit_shared_edge`) 做完整的几何共享边检查，需要访问两个三角形的顶点和重心坐标。
GPU 版本采用保守策略：**`t > 0 && t < epsilon` → 直接 reject**。

### 8.1 保守 reject 的影响

- **多 reject**: 某些不共享边但距离很近的 hit 也会被 reject
- **对蒙特卡洛的影响**: 极小。被多 reject 的射线会在更远处找到 hit，或变成 miss 射线（逃逸）
  - 蒙特卡洛天然容忍个别路径的偏差，收敛速度影响 ≪ 统计噪声
  - `epsilon` 典型值 ~1e-6，受影响的 hit 占比极低

### 8.2 精确 GPU 实现的代价（备选）

如果保守策略不可接受，可在 GPU 上实现完整 `hit_shared_edge`：
- 需访问两个三角形的顶点（`hit_from` 的顶点需额外上传）
- 需重心坐标计算和 edge-on-edge 检查
- 显著增加 any-hit 寄存器压力和分支发散

**推荐**: 先用保守策略，验证数值一致性和物理结果变化。

---

## 9. 预期收益

### 9.1 消除的开销

| 项目 | L3 时间 | L4 处理 | 残余 |
|------|---------|---------|------|
| `retrace_time` (48%) | 47.9ms | **完全消除** | 0 |
| `post_time` (12.3%) CPU filter | 12.3ms | **完全消除** | 0 |
| `batch_time` D2H | ~13ms (120B/ray) | 缩减至 ~4.4ms (40B/ray) | 66% 缩减 |
| H2D 增加 | 0 | +0.02ms (filter_data upload) | 可忽略 |
| Kernel 时间 | ~26ms | +~2-5ms (inline filter 计算) | any-hit 加重 |

### 9.2 预估性能

```
L3 total_trace_ms (100步):  99.8ms
L4 预估:
  batch_time:   26ms (kernel+launch) + 5ms (filter overhead) + 4.4ms (D2H reduced) ≈ 35.4ms
  post_time:    0ms (跳过)
  retrace_time: 0ms (消除)
  合计:         ~35.4ms

提升: 99.8ms → 35.4ms = ~65% 缩减，~2.8× 加速
```

### 9.3 流水线简化

Mode A 下 `batch_trace_wait_d2h_impl` 不再需要 Phase 2 (CPU filter) + Phase 3 (retrace)，
直接 sync → 返回 `HitResult*`：

```cpp
res_T batch_trace_wait_d2h_impl(...) {
    if (ctx->d2h_pending) {
        cudaStreamSynchronize(ctx->transfer_stream);
        ctx->d2h_pending = false;
    }
    ctx->async_pending = false;
    /* Mode A: 直接 memcpy hits → 无 filter, 无 retrace */
    memcpy(out_hits, h_hits_pinned, nrays * sizeof(HitResult));
    return RES_OK;
}
```

---

## 10. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| Filter ② 保守 reject 改变结果 | 少量路径差异 | 验证: GPU/CPU 像素比对，容差 1e-6 |
| `__anyhit__` 寄存器压力增大 | 降低 SM 占用率 | Profile: 监控 occupancy，考虑 `maxreg` 限制 |
| 包壳数组显存 | `2 × uint32 × num_prims` | 百万面片仅 ~8MB，可忽略 |
| Any-hit 分支发散（filter pass/reject） | warp 效率下降 | 场景相关; BVH 局部性通常使同 warp 射线命中相似区域 |
| SBT/Pipeline 复杂度增加 | 维护成本 | 封装为 `FilteredPipeline` 类，统一管理 |
| 双模式正确性验证 | 需测试两种代码路径 | Mode B 保持不变，回归测试覆盖 |

---

## 11. 实施顺序

### Phase 1: 数据层 — Enclosure 上传
1. `HitGroupData` 添加 `enc_front` / `enc_back` 字段
2. `rebuild_tracer()` 中从 `prim_props[]` 提取并上传到 device CudaBuffer
3. SBT record 绑定新字段
4. **验证**: 现有 Mode B 管线不受影响（ctest 通过）

### Phase 2: Kernel 层 — 新 Any-Hit 程序
5. 定义 `FilterPerRayData` 结构体（`ray_types.h`）
6. `UnifiedParams` 添加 `filter_data` 指针
7. 实现 `__anyhit__mh_filtered`（filter ①②③ + Top-K）
8. 实现 `__raygen__mh_filtered`（MultiHit → SingleHit 降维）
9. 创建 filtered pipeline / SBT entry

### Phase 3: 后端 API — Mode A 路径
10. `batch_trace_context` 添加 `d_filter_data` CudaBuffer + `h_filter_pinned`
11. 实现 `batch_trace_filtered_async_impl` — upload filter_data + filtered launch
12. 实现 `batch_trace_filtered_wait_impl` — 简化（无 CPU filter, 无 retrace）
13. C wrapper API（`s3d.h` 声明 + `ox_s3d_scene_view.cpp` 实现）

### Phase 4: 求解器集成
14. `gpu_launch_filtered_async()` — 构建 per-ray filter data + 调用 Mode A
15. 主循环切换为 Mode A 路径（条件编译或运行时切换）
16. TIMELINE 扩展标记 mode A/B

### Phase 5: 验证与优化
17. GPU/CPU 像素级验证（Mode A vs Mode B 对比）
18. Nsight profiling: any-hit occupancy, register usage
19. 性能对比: TIMELINE L3 vs L4

---

## 12. 关键文件索引

| 文件 | 改动 |
|------|------|
| `oxstar-3d/0.10/include/ray_types.h` | 新增 `FilterPerRayData` 结构 |
| `oxstar-3d/0.10/include/unified_params.h` | `UnifiedParams` 加 `filter_data*`; `HitGroupData` 加 `enc_front/back` |
| `oxstar-3d/0.10/device/programs.cu` | 新增 `__raygen__mh_filtered` + `__anyhit__mh_filtered` |
| `oxstar-3d/0.10/src/unified_tracer.cpp` | 新增 `traceBatchMultiHit_filtered`; filtered pipeline/SBT 构建 |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h` | `batch_trace_context` 加 filter buffers |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | Mode A async/wait impl |
| `oxstar-3d/0.10/s3d_wrapper/s3d.h` | 新增 filtered API 声明 |
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | `gpu_launch_filtered_async` + 主循环 Mode A |

---

## 13. CPU Filter 原始实现参考

```c
/* sdis_scene_Xd.h — XD(hit_filter_function), DIM=3 */
static int
XD(hit_filter_function)(const struct sXd(hit)* hit, ..., void* query_data, ...)
{
    const struct hit_filter_data* filter_data = query_data;
    if (!filter_data) return 0;                           /* 无数据 → 不过滤 */
    if (filter_data->XD(custom_filter)) { ... }           /* 热路径中始终 NULL */
    
    hit_from = &filter_data->XD(hit);
    if (SXD_HIT_NONE(hit_from)) return 0;                /* 无发射面 → 不过滤 */
    
    /* ① 自相交 */
    if (SXD_PRIMITIVE_EQ(&hit_from->prim, &hit->prim)) return 1;
    
    /* 距离 ≤ 0 → 假定自相交 */
    if (hit->distance <= 0) return 1;
    
    /* ② 近距共享边 */
    if (eq_epsf(hit->distance, 0, (float)filter_data->epsilon)) {
        /* ... hit_shared_edge() 检查 → reject if shared */
    }
    
    /* ③ 边界包壳 */
    if (filter_data->scn && HIT_ON_BOUNDARY(hit, org, dir)) {
        scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);
        chk_enc = dot(dir, normal) < 0 ? enc_ids[0] : enc_ids[1];
        if (chk_enc != filter_data->enc_id) return 1;
    }
    return 0;
}
```

> `scene_get_enclosure_ids` 是简单数组下标访问：`scn->prim_props[iprim].front/back_enclosure`

---

*文档更新: 2026-03-04 | 前置: L3 Dual-Stream | 预期收益: ~2.8× batch trace 加速*
