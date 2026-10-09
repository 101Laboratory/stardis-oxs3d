# [TODO] scene_prim_id 语义不一致

**状态**: 📋 待处理（低优先级，multi-GAS instanced 场景触发）  
**优先级**: 低 — 当前 pseudo-instance/非 instanced 场景无影响  
**来源**: `[RESOLVED]oxs3d_numerical_inconsistency/` 诊断过程中发现  
**发现日期**: 2026-02-28

---

## 问题描述

cuBQL 和 oxs3d 后端对 `s3d_hit.prim.scene_prim_id` 的赋值语义不同：

- **cuBQL** (`cus3d_prim.cpp` L56-58): `scene_prim_id` = GPU BVH 中的**全局三角形索引**，跨所有 GAS/shape 全局唯一
- **oxs3d** (`ox_s3d_internal.h` L366): `scene_prim_id` = `optixGetPrimitiveIndex()` 返回值 = **GAS-local 索引**，multi-GAS 场景中不同 GAS 的三角形从 0 重新计数，不全局唯一

**触发条件**：multi-GAS 场景（真实 instancing，多个 GAS）。  
**当前影响**：pseudo-instance/非 instanced 场景中无影响（单 GAS 时 CPU/GPU 索引对应）。  
**潜在后果**：downstream 的 `sdis_scene_find_closest_point_3d()` 依赖 `scene_prim_id` 全局唯一性，在 multi-GAS 场景下可能查到错误的三角形属性。

---

## 修复方向

在 oxs3d 的 anyhit/closest-hit shader 中将 GAS-local prim_idx 映射为全局索引。
可在场景构建时为每个 GAS 记录 prim_offset，运行时相加即可。

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `analysis.md` | 完整问题描述、后端实现对比、影响链分析 |
