# [enc_locate] Point-in-Enclosure via BVH Closest Primitive — M10

> Phase B-4 M10: 替换 M1 6-ray enclosure query + brute-force fallback
> 的 BVH 最近原语搜索方案。

## 1. 动机

M1 的 enclosure query 子状态机存在根本性问题：

1. **6 条射线占用 ray bucket** — 每次 enclosure 查询消耗 6 个 ray slot，
   占据 `RAY_BUCKET_ENCLOSURE` 容量，增加 batch trace 延迟。
2. **Brute-force fallback** — `step_enc_query_resolve()` 在 cascade 循环中
   调用 `scene_get_enclosure_id()`，该函数发射同步 width=1 射线追踪，
   完全破坏 wavefront 设计。
3. **6-ray 完整度过低** — 只有第一个有效 hit 被使用，剩余 5 条射线浪费。

## 2. 核心洞察

**Enclosure 查询不是射线追踪问题，而是点定位（point-location）问题。**

给定一个查询点 P，找到距离 P 最近的场景表面原语（primitive），然后通过
法线方向判断 P 位于该原语的哪一侧：

```
dot(P - Q, N) < 0  →  front side → enc_ids[0]
dot(P - Q, N) >= 0 →  back side  → enc_ids[1]
```

其中 Q 是最近点，N 是该原语的几何法线。

这可以通过 BVH 最近邻搜索（shrinkingRadiusQuery）在 **O(log N)** 时间
内完成，无需射线追踪，无 fallback，无 ray bucket 占用。

## 3. 状态迁移设计

### 3.1 新增状态

```
PATH_ENC_LOCATE_PENDING  [E]  — enc_locate batch 中等待 GPU 结果
PATH_ENC_LOCATE_RESULT   [C]  — 结果已到达，解析 prim_id + side → enc_id
```

注意：`[E]` 表示 enc_locate-pending，既非 ray-pending `[R]` 也非
compute-only `[C]`。它参与独立的 enc_locate collect/distribute 循环。

### 3.2 状态迁移图

```
┌────────────────────────┐
│ caller state           │  step_enc_locate_submit(p, pos, return_state)
│ (e.g. PATH_CND_INIT_ENC) │
└───────────┬────────────┘
            │
            ▼
┌─────────────────────────┐
│ PATH_ENC_LOCATE_PENDING │  等待 enc_locate batch dispatch
│ [E] collect → GPU batch │
└───────────┬─────────────┘
            │  pool_distribute_enc_locate_results()
            ▼
┌─────────────────────────┐
│ PATH_ENC_LOCATE_RESULT  │  step_enc_locate_result() 解析 enc_id
│ [C] cascade 中执行       │
└───────────┬─────────────┘
            │  phase = enc_locate.return_state
            ▼
┌────────────────────────┐
│ return_state            │  回到调用者指定的后续状态
└────────────────────────┘
```

### 3.3 与 M1 的关系（已完成替换）

M1 的 6-ray enclosure query 已被 **完全移除**（2026-02-06）。
所有调用点已迁移到 M10 的 `step_enc_locate_submit()`。

已删除的 M1 组件：
- `PATH_ENC_QUERY_EMIT` / `PATH_ENC_QUERY_RESOLVE` 状态枚举
- `RAY_BUCKET_ENCLOSURE` 射线桶类型（桶数从 5 降至 4）
- `enc_query` 结构体（directions[6][3], dir_hits[6], batch_indices[6] 等）
- `step_enc_query_emit()` / `step_enc_query_resolve()` 实现及声明
- wavefront collect/distribute 中的 6-ray 特殊处理代码
- `rays_enclosure` 统计计数器

M1 测试 `test_sdis_b4_m1_enclosure_batch` 已从 CMakeLists.txt 中禁用。

## 4. 实现层次

### 4.1 GPU 内核层 (custar-3d)

| 文件 | 说明 |
|------|------|
| `cus3d_find_enclosure.h` | 结构定义 + API 声明 |
| `cus3d_find_enclosure.cu` | GPU 内核实现（单层 + 两层 BVH） |

内核使用 `cuBQL::shrinkingRadiusQuery::forEachPrim` 遍历 BVH，
对每个候选原语计算最近点距离，输出 `cus3d_enc_result`（prim_id, side, distance, normal）。

### 4.2 s3d API 层

| 文件 | 说明 |
|------|------|
| `s3d.h` | 公共 API 声明（`s3d_enc_locate_request/result`, batch 函数） |
| `s3d_scene_view_find_enclosure.cpp` | AoS→SoA + kernel + post-process |

### 4.3 求解器层 (stardis-solver)

| 文件 | 修改 |
|------|------|
| `sdis_wf_types.h` | 新增 `PATH_ENC_LOCATE_PENDING/RESULT`, `path_phase_is_enc_locate_pending()` |
| `sdis_wf_state.h` | 新增 `enc_locate` struct in `path_state` |
| `sdis_wf_steps.h/c` | 新增 `step_enc_locate_submit()`, `step_enc_locate_result()`, 更新 dispatch |
| `sdis_solve_persistent_wavefront.h/c` | enc_locate 缓冲区 + collect/distribute + 主循环 |
| `sdis_solve_wavefront.h/c` | simple wavefront enc_locate 支持 |

## 5. Wavefront 主循环中的位置

```
  Step A: compact_active_paths()
  Step B: pool_collect_ray_requests_bucketed()     ← ray collect
  Step C: s3d_scene_view_trace_rays_batch_ctx()    ← GPU ray trace
  Step D: pool_distribute_ray_results()            ← ray distribute
  Step D2: pool_collect_enc_locate_requests()      ← M10 enc_locate collect
           s3d_scene_view_find_enclosure_batch_ctx() ← GPU enc_locate
           resolve prim_id+side → enc_id           ← CPU post-process
           pool_distribute_enc_locate_results()    ← M10 enc_locate distribute
  Step E: pool_cascade_non_ray_steps_compact()     ← cascade（处理 RESULT 状态）
  Step F+G: harvest + refill
```

## 6. enc_locate 数据流

```
path_state.enc_locate.query_pos[3]
    ↓ pool_collect_enc_locate_requests()
s3d_enc_locate_request { pos[3], user_id }
    ↓ AoS→SoA + H2D upload
cus3d_enc_batch { d_positions, count }
    ↓ GPU kernel (find_enclosure_kernel / find_enclosure_instanced_kernel)
cus3d_enc_result { prim_id, side, distance, normal, closest_pos }
    ↓ D2H download
s3d_enc_locate_result { prim_id, side, distance, enc_id }
    ↓ CPU: scene_get_enclosure_ids(prim_id) → enc_ids[side]
    ↓ pool_distribute_enc_locate_results()
path_state.enc_locate.{prim_id, side, distance}
    ↓ step_enc_locate_result()
path_state.enc_locate.resolved_enc_id
    ↓ phase = return_state
```

## 7. Degenerate 处理

当 `distance < 1e-6f`（点在表面上）时，内核输出 `side = -1`。
`step_enc_locate_result()` 检测到 `side < 0` 后 fallback 到
`scene_get_enclosure_id()` — 这是唯一保留的同步 fallback，
但只在极端退化情况下触发（频率极低）。

## 8. 性能分析

| 指标 | M1 (6-ray) | M10 (BVH NN) |
|------|-----------|-------------|
| GPU 操作次数/查询 | 6 条射线 + 可能的 brute-force | 1 次 BVH 遍历 |
| Ray bucket 占用 | 6 slots/query | 0 |
| 延迟 | batch_trace 延迟 | 独立 enc_locate batch |
| Fallback 频率 | ~5-10% (6-ray 全 miss) | ~0.01% (degenerate only) |
| 内存/query | 6×ray_request + 6×hit (>500B) | 1×request + 1×result (~40B) |

## 9. 迁移状态（已完成）

以下步骤已全部完成：

1. ~~**M10 部署后** — 新的 enclosure 查询调用点直接使用 `step_enc_locate_submit()`~~ ✅
2. ~~**逐步替换 M1** — 将现有 `step_enc_query_emit()` 调用改为 `step_enc_locate_submit()`~~ ✅
3. ~~**完全废弃 M1** — 移除 `PATH_ENC_QUERY_EMIT/RESOLVE`, `enc_query` struct, `RAY_BUCKET_ENCLOSURE`~~ ✅
4. **M9 WoS 集成** — WoS conductive path 的 closest_point 也可复用 BVH NN 基础设施（待实现）

### 替换的调用点（7 处 emit + 6 处字段读取）

| 位置 | 原调用 | 替换为 | return_state |
|------|--------|--------|-------------|
| `step_conductive` | `step_enc_query_emit(p)` | `step_enc_locate_submit(p, P, PATH_CND_DS_CHECK_TEMP)` | DS check temp |
| `step_cnd_ds_process` | `enc_query.query_pos` | `enc_locate.query_pos` | — (store) |
| `step_cnd_ds_step_enc_verify` | `step_enc_query_emit(p)` | `step_enc_locate_submit(p, query_pos, PATH_CND_DS_STEP_ADVANCE)` | DS step advance |
| `step_bnd_ss_reinject` (front) | `step_enc_query_emit(p)` | `step_enc_locate_submit(p, pos, PATH_BND_SS_REINJECT_ENC)` | SS reinject enc |
| `step_bnd_ss_reinject` (back) | `step_enc_query_emit(p)` | `step_enc_locate_submit(p, pos, PATH_BND_SS_REINJECT_ENC)` | SS reinject enc |
| `step_bnd_ss_reinject_enc_result` | `step_enc_query_emit(p)` | `step_enc_locate_submit(p, pos, PATH_BND_SS_REINJECT_ENC)` | SS reinject enc |
| `step_bnd_sf_reinject` | `step_enc_query_emit(p)` | `step_enc_locate_submit(p, pos, PATH_BND_SF_REINJECT_ENC)` | SF reinject enc |

所有 `enc_query.resolved_enc_id` 读取已替换为 `enc_locate.resolved_enc_id`。

### 验证结果

| 测试 | 结果 |
|------|------|
| M2 ray bucketing (11 tests) | ✅ ALL PASS |
| M3 solid-solid (11 tests) | ✅ ALL PASS |
| M4 delta sphere (all tests) | ✅ ALL PASS |
| M5 picard1 (9 tests) | ✅ ALL PASS |
| M6 convective (18 tests) | ✅ ALL PASS |

---
*创建: 2026-02-06 | 更新: 2026-02-06 M1 完全替换完成 | Phase B-4 M10*
