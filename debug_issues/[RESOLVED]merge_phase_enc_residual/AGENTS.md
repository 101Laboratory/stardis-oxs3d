# [TODO] merge_phase_enc_residual — Merge Pool 后 enc 查询访问垃圾值

**状态**: 📋 TODO（已定位，待修复）  
**发现日期**: 2026-03-11  
**分支**: stardis-cus3d-merge-phase  
**严重度**: 中 — 仅在 dual-buffer merge 事件触发，影响 merge 后残留路径  
**与数值不一致问题无关**: 本问题独立于 `merge_phase_numerical_inconsistency`

---

## 问题描述

`merge_to_single_pool()` 将 dual-buffer 合并为 single-buffer 时，pool B（views[1]）中残留的未处理路径的 enc 查询数据未被妥善迁移，导致后续 `merged_pass` 对这些路径的 enc_locate 访问读取到垃圾/陈旧值。

---

## 根因（已定位）

`merge_to_single_pool()` (@353) 的操作：
```c
pool->views[0].base      = 0;
pool->views[0].view_size = pool->pool_size;
pool->views[0].enc_locate_count = 0;  // ← 重置为 0
pool->views[0].cp_count         = 0;  // ← 重置为 0
pool->num_active_views = 1;
```

**问题链**:
1. Pool B 中可能有路径处于 `PATH_ENC_LOCATE_PENDING`（已请求 enc_locate 但未完成）或 `PATH_ENC_LOCATE_RESULT`（GPU 已返回结果但 Phase A 未消费）
2. 合并后 `views[0].enc_locate_count = 0`，这些请求/结果的映射关系（`enc_locate_to_slot[]`）被逻辑丢弃
3. `assert_result_phases_backed()` 会在 debug 构建中对 full pool 范围扫描，发现 RESULT phase 无对应 slot 映射 → assert 失败
4. 在 Release 构建中，这些路径的 `enc_arr[slot].locate` 包含上一轮的陈旧数据，被当作有效结果使用

**代码位置**: `stardis-cus3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`
- `merge_to_single_pool()` @353-378
- `assert_result_phases_backed()` @4210-4235
- `pool_distribute_enc_locate_results()` @2070-2089
- `should_merge()` @320-339（merge 触发条件，无 enc/cp 前置检查）

---

## 推荐修复方案

### F-enc3（推荐）：推迟合并时机

在 `should_merge()` 增加前置条件，确保合并时无挂起的 enc/cp 请求：

```c
static int
should_merge(const struct wavefront_pool* pool)
{
  size_t thresh;
  if(pool->num_active_views != 2) return 0;
  /* 新增：阻止在有挂起 enc/cp 请求时合并 */
  if(pool->views[1].enc_locate_count > 0) return 0;
  if(pool->views[1].cp_count > 0) return 0;
  if(pool->views[0].enc_locate_count > 0) return 0;
  if(pool->views[0].cp_count > 0) return 0;

  thresh = pool->views[0].view_size / 8;
  return (pool->views[0].active_compact < thresh)
      || (pool->views[1].active_compact < thresh);
}
```

### 备选方案

- **F-enc1**: merge 前先完成 pool B 的 enc batch（提交+等待+分发），复杂度较高
- **F-enc2**: merge 后对 PENDING 路径回退状态（让下次 merged_pass 重新收集请求），需处理 RNG 一致性

---

*创建: 2026-03-11*
