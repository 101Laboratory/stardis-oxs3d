# scene_prim_id 语义不一致分析

**状态**: TODO — 已归档待后续处理  
**优先级**: 低（与数值一致性主问题独立）  
**发现日期**: 2026-02-28  

## 问题描述

cuBQL 和 oxs3d 后端对 `s3d_hit.prim.scene_prim_id` 的赋值语义不同。

## 后端实现对比

### custar-3d (cuBQL)
**文件**: `stardis-cus3d/custar-3d/0.10/src/cus3d_prim.cpp` L56-58
```c
prim->prim_id = gpu_hit->prim_id - (int)ge->prim_offset;  // shape-local ID
prim->geom_id = ge->shape_name;
prim->scene_prim_id = (unsigned)gpu_hit->prim_id;          // GPU 全局统一 primID
```
- `scene_prim_id` = GPU BVH 中的全局三角形索引
- 跨所有 GAS/shape 全局唯一

### oxstar-3d (OptiX)
**文件**: `stardis-cus3d/oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h` L366
```c
out->prim.prim_id = local_prim_id;    // shape-local ID (由调用者预先计算)
out->prim.scene_prim_id = hr.prim_idx; // optixGetPrimitiveIndex() 返回值
```
- `scene_prim_id` = OptiX GAS-local primitive index
- 对于 multi-GAS 场景，不同 GAS 中的三角形索引从 0 重新开始，**不是全局唯一的**

## 下游使用

`scene_prim_id` 在求解器中仅有一处使用：

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_scene_Xd.h` L1191
```c
*iprim = hit.prim.scene_prim_id;
```
位于 `sdis_scene_find_closest_point_3d()` 函数中，用于 closest-point 查询后返回命中的 primitive 标识符。

## 影响评估

- **数值一致性影响**: 不直接影响射线追踪/filter/cascade 路径（这些使用 `prim_id` + `geom_id` + `inst_id` 而非 `scene_prim_id`）
- **功能正确性影响**: 如果上层代码依赖 `scene_prim_id` 的全局唯一性来标识 primitive（如用于 closest-point 结果的去重或查表），在 instanced 场景中可能出错
- **当前 porous 场景**: 用户确认无 instanced/变换几何体，风险较低，但语义不一致本身仍是 bug

## 建议修复

oxs3d 端应将 `scene_prim_id` 改为全局索引：
```c
out->prim.scene_prim_id = hr.prim_idx + shape_prim_offset;  // 加上 shape 的全局偏移
```
其中 `shape_prim_offset` 可从 `query_prim_range` 结构中获取。

## 备注

- cuBQL 侧的实现也可能是一个意外（直接使用了 GPU hit 的全局 ID），但恰好与 CPU 原版语义一致
- 此问题与 oxs3d 数值偏亮问题独立，可单独修复验证
