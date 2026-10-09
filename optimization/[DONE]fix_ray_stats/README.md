# [DONE] 射线统计修复 — 遗漏桶 + 维度混淆 + 日志重设计

**来源**: `debug_issues/oxs3d_numerical_inconsistency/` 诊断过程中发现  
**状态**: ✅ 已完成（2026-03-01）  
**优先级**: 中 — 不影响运行时正确性，影响诊断数据可信度  
**前置依赖**: 无（可独立实施）；与 `[TODO]remove_ray_bucket` 解耦但建议先实施本项

## 问题概述

`log_drain_phase_report()` 输出的射线分类统计存在 4 个问题，导致 `Σ rays_* ≠ total_rays_traced`。

## Bug 清单

### Bug 1 — `RAY_BUCKET_ENCLOSURE` 无独立计数器

`wavefront_pool` 定义了 5 个计数器（`rays_radiative`, `rays_conductive_ds`, `rays_conductive_ds_retry`, `rays_shadow`, `rays_startup`），但 `enum ray_bucket_type` 有 5 种桶（含 `ENCLOSURE`）。Enclosure 射线（~72% DS 路径触发，每次 6 条）无独立计数，只能减法推导。

**位置**: `sdis_solve_persistent_wavefront.h` L242-L246

### Bug 2 — 遗漏的 ray-pending phases（"幽灵射线"）

phase 维度统计只覆盖 3 个条件：
- `PATH_RAD_TRACE_PENDING` → `rays_radiative`
- `PATH_COUPLED_COND_DS_PENDING` / `PATH_CND_DS_STEP_TRACE` → `rays_conductive_ds`

但 `path_phase_is_ray_pending()` 包含 **15 种** ray-pending phase。以下 phase 的射线未被任何计数器捕获：

| Phase | 说明 |
|-------|------|
| `PATH_COUPLED_BOUNDARY_REINJECT` | 边界重注入 |
| `PATH_BND_SS_REINJECT_SAMPLE` | SS 边界重注入 |
| `PATH_BND_SF_REINJECT_SAMPLE` | SF 边界重注入 |
| `PATH_BND_SF_NULLCOLL_RAD_TRACE` | 零碰撞辐射 |
| `PATH_BND_SFN_RAD_TRACE` | 辐射子路径 |
| `PATH_BND_EXT_DIRECT_TRACE` | 直接阴影 |
| `PATH_BND_EXT_DIFFUSE_TRACE` | 漫反射弹射 |
| `PATH_BND_EXT_DIFFUSE_SHADOW_TRACE` | 漫反射阴影 |
| `PATH_CND_INIT_ENC` | 初始 enclosure |
| `PATH_CND_WOS_FALLBACK_TRACE` | WoS 回退 |
| `PATH_CNV_STARTUP_TRACE` | 对流启动 |
| `PATH_ENC_QUERY_EMIT` | enclosure 6-ray |
| `PATH_ENC_QUERY_FB_EMIT` | enclosure 回退 |

**位置**: `sdis_solve_persistent_wavefront.c` L1097-L1109（OMP collect）, L1236-L1247（串行 collect）

### Bug 3 — `ray_buckets:` 日志标签语义混淆

```
ray_buckets: radiative=%llu, step_pair=%llu, shadow=%llu, startup=%llu
```

标签 `step_pair` 实际打印的是 `pool->rays_conductive_ds`（按 **phase** 分类），而非 `RAY_BUCKET_STEP_PAIR`（按 **桶** 分类）。两套维度不正交。

**位置**: `sdis_solve_persistent_wavefront.c` L2119-L2120

### Bug 4 — `rays_conductive_ds_retry` 未在详细报告中打印

`log_drain_phase_report` 的 `ray_buckets:` 行只有 4 项。`ds_retry` 仅在最终 DONE 行（L3285）出现。

## 修复方案

### 新统计体系：两级维度

**语义层**（summary 日志）— 7 个计数器，按物理用途分类：

| 计数器 | 覆盖的 phases |
|--------|--------------|
| `rays_radiative` | RAD_TRACE_PENDING, BND_SF_NULLCOLL_RAD_TRACE, BND_SFN_RAD_TRACE, BND_EXT_DIFFUSE_TRACE, CND_WOS_FALLBACK_TRACE, reinject samples |
| `rays_conductive_ds` | COUPLED_COND_DS_PENDING, CND_DS_STEP_TRACE (attempt=0) |
| `rays_conductive_ds_retry` | 同上 (attempt>0) |
| `rays_shadow` | BND_EXT_DIRECT_TRACE, BND_EXT_DIFFUSE_SHADOW_TRACE |
| `rays_enclosure` | ENC_QUERY_EMIT, ENC_QUERY_FB_EMIT, CND_INIT_ENC |
| `rays_startup` | CNV_STARTUP_TRACE |
| `rays_other` | 兜底（出现即为 bug） |

校验：`ASSERT(Σ rays_* == total_rays_traced)`

**Phase 层**（verbose，debug build 或 `-V 4`）— `size_t rays_by_phase[PATH_PHASE_COUNT]` 数组，每 ray-pending phase 单独计数。无交叉覆盖。

### 统计累加逻辑

在 collect 函数中用 switch 归类（替换当前的 if-else chain）：

```c
switch(ph) {
case PATH_RAD_TRACE_PENDING:
case PATH_BND_SF_NULLCOLL_RAD_TRACE:
case PATH_BND_SFN_RAD_TRACE:
case PATH_BND_EXT_DIFFUSE_TRACE:
case PATH_CND_WOS_FALLBACK_TRACE:
case PATH_COUPLED_BOUNDARY_REINJECT:
case PATH_BND_SS_REINJECT_SAMPLE:
case PATH_BND_SF_REINJECT_SAMPLE:
    pool->rays_radiative += nrays; break;
case PATH_COUPLED_COND_DS_PENDING:
case PATH_CND_DS_STEP_TRACE:
    if(p->ds_robust_attempt > 0) pool->rays_conductive_ds_retry += nrays;
    else                         pool->rays_conductive_ds += nrays;
    break;
case PATH_BND_EXT_DIRECT_TRACE:
case PATH_BND_EXT_DIFFUSE_SHADOW_TRACE:
    pool->rays_shadow += nrays; break;
case PATH_ENC_QUERY_EMIT:
case PATH_ENC_QUERY_FB_EMIT:
case PATH_CND_INIT_ENC:
    pool->rays_enclosure += nrays; break;
case PATH_CNV_STARTUP_TRACE:
    pool->rays_startup += nrays; break;
default:
    pool->rays_other += nrays; break;
}
```

### 日志格式

```
rays: radiative=N  cond_ds=N(retry=N)  shadow=N  enclosure=N  startup=N  other=N
```

### 修改文件列表

| 文件 | 修改 |
|------|------|
| `sdis_solve_persistent_wavefront.h` | 添加 `rays_enclosure`, `rays_other`, `rays_by_phase[]` |
| `sdis_solve_persistent_wavefront.c` | collect 统计逻辑、log 格式、debug assert |
| `stardis-cus3d-ref-cus3d/` 同文件 | 同步修改 |

### Verification

```bash
# 低分辨率低 spp 验证
stardis -M porous.txt -t 4 -V 3 -R spp=1:img=4x4:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > test.ht 2> test.log
# 确认日志中 Σ rays_* == total_rays，debug build assert 不触发
```

## 线程安全性

当前 collect 的 OMP 统计使用 thread-local 累加 + `#pragma omp critical` 归约，**线程安全且无显著竞争**。新设计保持同一模式。

---

## 实施记录

**完成日期**: 2026-03-01  
**修改范围**: `stardis-cus3d/stardis-solver/0.16.2/src/`

### 已实施内容

1. **Bug 1-4 全部修复** — 新增 `rays_enclosure`、`rays_other` 计数器；15 个 ray-pending phase 全覆盖（switch 归类）；日志格式改为 `rays: radiative=N cond_ds=N(retry=N) shadow=N enclosure=N startup=N other=N`
2. **Enclosure 射线计数修正** — collect 中使用 `count_path_rays_soa()` 取代 `p->ray_req.ray_count`，正确计算 6-ray enclosure 查询
3. **管线终止幻影统计修复** — 引入 per-view pending stats 两阶段提交：collect 写入 `pv->pending_rays_*`，仅在 `gpu_wait_and_postprocess` 中 trace 完成后才提升到 `pool->rays_*`，消除双缓冲管线最后一次未执行 trace 的幻影统计
4. **周期日志补全** — 每 1000 步日志增加 `enc=` 字段显示 enclosure 桶射线数
5. **Debug 代码清理** — 移除 `rays_by_phase[PATH_PHASE_COUNT]`（pool + pv）、`diag_step_stats_total/prev`、per-step `log_err` 一致性检查、`tl_rays_by_phase[]` 线程局部数组,避免热路径性能开销

### 验证结果

```
256x256 spp=4 pool=12288:
  total_rays=1,043,986,219
  Σ(rad + cond_ds + retry + shadow + enc + startup + other)
  = 70,086,075 + 201,358,448 + 2 + 0 + 772,541,694 + 0 + 0
  = 1,043,986,219  ✅ delta=0
```
