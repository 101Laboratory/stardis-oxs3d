# M5_SF_FAIL: Serial Fallback 缺失 fill_filter_per_ray — 自相交过滤失败

**发现日期**: 2026-03-05  
**发现于**: stardis-cus3d-o9 worktree (与 O9 改造无关，bug 存在于 main)  
**状态**: ✅ 已修复 (main + p0opt)  
**严重度**: 中 — 仅影响 DRAIN 末期极少量路径  

---

## 症状

DRAIN 末期出现 `M5_SF_FAIL`：reinject 射线 hit 到错误 enclosure → `chosen_dst ≤ 0` → 路径失败。

## 根因

`pool_collect_ray_requests_bucketed()` 的 **serial fallback** 路径（当 `pv->need_ray_count < 128` 时触发）在所有 4 个射线发射点缺失 `fill_filter_per_ray()` 调用。

OMP 路径中每个射线发射点都正确调用了 `fill_filter_per_ray()`，但 serial fallback 路径在代码复制时遗漏。

## 影响链

```
need_ray_count < 128 (DRAIN末期)
  → 切入 serial fallback
    → fill_filter_per_ray 未调用
      → GPU L4 inline filter 读取上一批次的过期 filter_per_ray 数据
        → 自相交过滤失效（以上一条射线的 hit 做过滤）
          → reinject 射线 hit 到错误 enclosure
            → chosen_dst ≤ 0
              → M5_SF_FAIL
```

**触发条件**：仅在 `pv->need_ray_count < 128` 时触发，精确对应 DRAIN 末期 wavefront 收窄阶段。

## 修复

在 serial fallback 的 4 个射线发射点添加 `fill_filter_per_ray()` 调用：

1. **Ray 0** — 主射线
2. **Ray 1** — 2-ray request 的第二条射线  
3. **ENC 6-ray rays 2..5** — 包壳查询额外 4 条射线
4. **SS 4-ray rays 2..3** — solid/solid reinjection 额外 2 条射线

## 修复位置

| 分支 | 文件 | 函数 |
|------|------|------|
| stardis-cus3d (main) | `sdis_solve_persistent_wavefront.c` | `pool_collect_ray_requests_bucketed()` serial fallback |
| stardis-cus3d-p0opt | 同上 | 同上 |

---

*归档: 2026-03-05*
