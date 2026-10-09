# [RESOLVED] M5_SF_FAIL — Serial Fallback 缺失 fill_filter_per_ray

**状态**: ✅ 已解决  
**发现日期**: 2026-03-05  
**修复分支**: main + p0opt  
**严重度**: 中 — 仅影响 DRAIN 末期极少量路径

---

## 问题描述

DRAIN 末期出现 `M5_SF_FAIL`：reinject 射线命中错误 enclosure → `chosen_dst ≤ 0` → 路径失败。

---

## 根因

`pool_collect_ray_requests_bucketed()` 的 **serial fallback** 路径（当 `pv->need_ray_count < 128` 时触发）在所有 4 个射线发射点缺失 `fill_filter_per_ray()` 调用。

OMP 路径中每个射线发射点都正确调用了 `fill_filter_per_ray()`，但 serial fallback 路径在代码复制时遗漏。

**影响链**：
```
need_ray_count < 128 (DRAIN末期)
  → 切入 serial fallback
    → fill_filter_per_ray 未调用
      → GPU L4 inline filter 读取上一批次的过期 filter_per_ray 数据
        → 自相交过滤失效
          → reinject 射线命中错误 enclosure
            → chosen_dst ≤ 0 → M5_SF_FAIL
```

---

## 修复

在 `pool_collect_ray_requests_bucketed()` serial fallback 路径的 4 个射线发射点补充 `fill_filter_per_ray()` 调用，与 OMP 路径对齐。

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `fix_serial_fallback_filter.md` | 根因分析与修复说明 |
| `log.txt` | 问题复现日志 |
