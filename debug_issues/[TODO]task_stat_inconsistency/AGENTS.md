# task_stat_inconsistency — 射线统计 CPU/GPU 不一致

**状态**: 🔍 活跃 — 根因已定位，待修复  
**创建日期**: 2026-03-01  
**严重度**: 低 — 影响统计展示，不影响求解正确性  
**关联分支**: `feat/cpu-ray-stats` (stardis-cpu-raystats worktree)

---

## 问题描述

对比同场景下 CPU 与 GPU 的 ray statistics，发现 **enclosure 射线计数**相差 9.5 亿（100% 差异）：

| 类别 | CPU | GPU | 严重性 |
|------|-----|-----|--------|
| enclosure rays | **0** | 9,555,681,468 | CRITICAL |
| radiative rays | 863,304,355 | 866,422,168 | 正常（+0.36%） |
| cond_ds rays | 2,489,104,904 | 2,491,154,618 | 正常（+0.08%） |

radiative / cond_ds 的差值在统计误差范围内，属预期差异。

---

## 根因

CPU 的 enclosure id 查询函数 `scene_get_enclosure_id_in_closed_boundaries()` 直接调用 `scene_view_trace_ray()`，**绕过了 ray_stats 计数基础设施**。

GPU 路径通过 `pool_collect_ray_requests_bucketed()` 统一走 wavefront pool，每次发射 6 条 enclosure 射线均会计入 `tl_rays_enclosure`。CPU 路径没有等价的计数钩子，导致 CPU enclosure 计数始终为 0。

---

## 修复方向

在 CPU 的 `scene_get_enclosure_id_in_closed_boundaries()` 中，对 `scene_view_trace_ray()` 的每次调用补充 `ray_stats_record_enclosure()` 计数（或等价机制），使 CPU 与 GPU 的统计基础对齐。

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `README.md` | 完整分析（观测数据、调用链对比、根因推导） |
