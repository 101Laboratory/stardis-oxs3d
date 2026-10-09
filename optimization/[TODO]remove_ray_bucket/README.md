# [TODO] 移除射线级分桶机制（保留路径级分派）

**来源**: `debug_issues/oxs3d_numerical_inconsistency/` 诊断中发现统计 Bug 后，分析发现分桶设计目标已不存在  
**状态**: 📋 待实施  
**优先级**: 中 — 可节省 collect 阶段 CPU 开销，简化代码  
**前置依赖**: 建议先完成 `[TODO]fix_ray_stats`（统计修复），再移除桶字段

## 背景：两套独立桶系统

当前代码存在两套**功能完全独立**的桶系统：

| | 射线级桶 (A) — 🔴 移除 | 路径级桶 (B) — 🟢 保留 |
|---|---|---|
| **数据结构** | `enum ray_bucket_type`, `dsoa.ray_bucket[]`, `pv->bucket_offsets[6]`, `pv->bucket_counts[5]` | `pv->bucket_radiative[]`, `pv->bucket_conductive[]` |
| **填充位置** | `pool_collect_ray_requests_bucketed()` OMP 3-pass radix scatter | `compact_active_paths()` 按 phase 分类 |
| **消费者** | **无** — batch trace 接收整个 `ray_requests[0..ray_count]` | `pool_distribute_ray_results()` 三阶段分派 |
| **设计目标** | GPU warp coherence（同类射线连续排列） | CPU 分支预测优化（50%→99% 命中率） |
| **实际效果** | **无实际收益** — GPU kernel 不知道桶边界，batch trace 一次性提交全部射线 | **有真实收益** — Phase 1/2 单一 step 函数循环，icache/dcache 局部性好 |

## 移除范围

### 完全移除的项目

| 项目 | 位置 |
|------|------|
| `enum ray_bucket_type` 及 5 个常量 | `sdis_wf_types.h` L145-152 |
| `path_state.ray_bucket` 字段 | `sdis_wf_state.h` L360 |
| `dispatch_soa.ray_bucket[]` 数组 | `sdis_wf_soa.h` L46 |
| `dispatch_soa_sync_from_path` 中 `ray_bucket` 同步 | `sdis_wf_soa.h` sync 函数 |
| `dispatch_soa_sync_to_path` 中 `ray_bucket` 同步 | `sdis_wf_soa.h` sync 函数 |
| `dispatch_soa_assert_consistent` 中 `ray_bucket` 断言 | `sdis_wf_soa.h` assert 函数 |
| `pool_view.bucket_offsets[]`, `bucket_counts[]` | `sdis_solve_persistent_wavefront.h` |
| 所有 step 函数中 `p->ray_bucket = RAY_BUCKET_*` 赋值 | grep `ray_bucket =` 全部删除 |
| `test_sdis_b4_m2_ray_bucketing.c` | 整个文件（专测 ray bucketing） |
| `test_sdis_dispatch_soa.c` 中 `ray_bucket` 相关断言 | 更新测试 |
| `compact_active_paths()` 中 `dsoa.ray_bucket[i]` 读取 | 相关行删除 |

### 简化 collect 函数

`pool_collect_ray_requests_bucketed()` 的 OMP 3-pass radix scatter 退化为**单 pass 顺序写入**：

```
当前（3-pass radix scatter）：
  Pass 1: 按 ray_bucket 计数 → tl_ray_counts[tid][bkt]
  Prefix sum: 计算 bucket_offsets + tl_write_base
  Pass 2: 按 bucket offset scatter → ray_requests[]

简化后（单 pass 顺序写入）：
  单 pass: 遍历 need_ray_indices[]，顺序写入 ray_requests[]
  ray_count = 最终 ray_idx
```

实质上退化为 `pool_collect_ray_requests_compact()`（L860-936）的逻辑。两个函数可合并。

**保留函数入口** `pool_collect_ray_requests_bucketed()` 的签名不变，内部逻辑简化。

### `pv->ray_count` 计算方式

当前：`pv->ray_count = pv->bucket_offsets[RAY_BUCKET_COUNT]`（取 prefix sum 末尾 sentinel）  
改为：`pv->ray_count = ray_idx`（直接累加）

### 不动的项目

| 项目 | 原因 |
|------|------|
| `pv->bucket_radiative[]` / `bucket_conductive[]` | **路径级桶 (B)**，用于 CPU distribute 三阶段分派，提供 50%→99% 分支预测命中率 |
| `compact_active_paths()` 中按 phase 分类到 `bucket_radiative/conductive` | 与射线级桶无关 |
| `pool_distribute_ray_results()` 三阶段结构 | 消费路径级桶，不依赖射线级桶 |
| `advance_one_step_with_ray()` switch dispatch | Phase 3 fallback，独立于桶 |

## 性能收益

- **省去 OMP 3-pass radix scatter**：2 次 `omp parallel` + prefix sum + per-thread per-bucket 数组分配
- **省去 `ray_bucket` SoA 内存**：`pool_size × sizeof(enum ray_bucket_type)` per frame
- **省去 step 函数中 ~15 处 `p->ray_bucket = RAY_BUCKET_*` 赋值**
- **简化 dispatch_soa 同步**：少一个字段 sync

## 线程安全性

当前 3-pass radix scatter 线程安全依赖 Pass 1/2 **相同 `schedule(static)`** 保证。简化为单 pass 后，如需 OMP 并行，需用不同的策略（如 OMP parallel for + atomic `ray_idx++`，或预分配写位置）。但 collect 本身耗时占比 <5%，可先用串行版本，后续按需优化。

## 修改文件列表

| 文件 | 修改类型 |
|------|---------|
| `sdis_wf_types.h` | 删除 `enum ray_bucket_type` |
| `sdis_wf_state.h` | 删除 `ray_bucket` 字段 |
| `sdis_wf_soa.h` | 删除 `ray_bucket[]` + 更新 sync/assert |
| `sdis_wf_soa.c` | 删除 `ray_bucket` alloc/free |
| `sdis_solve_persistent_wavefront.h` | 删除 `bucket_offsets[]`, `bucket_counts[]` |
| `sdis_solve_persistent_wavefront.c` | 简化 collect、删除 bucket 相关代码 |
| `sdis_wf_steps_*.c/h` | 删除所有 `p->ray_bucket = RAY_BUCKET_*` 赋值 |
| `test_sdis_b4_m2_ray_bucketing.c` | 整个文件删除 |
| `test_sdis_dispatch_soa.c` | 更新测试 |
| `CMakeLists.txt` | 移除 ray_bucketing 测试注册 |
| `stardis-cus3d-ref-cus3d/` 同名文件 | 同步修改 |

## Verification

```bash
# 低分辨率低 spp 验证功能正确性
stardis -M porous.txt -t 4 -V 3 -R spp=1:img=4x4:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > test.ht 2> test.log
# ctest 通过（bucketing 测试已删除，dispatch_soa 测试已更新）
ctest -C Release --output-on-failure
```

## 架构确认

batch trace 调用方式确认（单次提交全部射线，不分段）：

```c
// stardis-cus3d L3047-3052 (sync path)
res = s3d_scene_view_trace_rays_batch_ctx(
    scn->s3d_view, pv->batch_ctx,
    pv->ray_requests, pv->ray_count,  // ← 全部射线，无桶分段
    pv->ray_hits, &stats);

// stardis-cus3d L2302 (async path)
res = s3d_scene_view_trace_rays_batch_ctx_async(
    sv, pv->batch_ctx, pv->ray_requests, pv->ray_count);  // ← 同上
```

cuBQL 后端 (`cus3d_trace_ray_batch_multi`) 和 OptiX 后端 (`traceBatchMultiHit` + `SBT_MH`) 均为统一 multi-hit pipeline，不区分射线类型。`bucket_offsets` 在 trace 调用中**从未被读取**。
