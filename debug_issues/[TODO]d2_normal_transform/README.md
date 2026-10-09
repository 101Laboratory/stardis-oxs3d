# [TODO] D2: 法线变换方法差异

**来源**: `debug_issues/oxs3d_numerical_inconsistency/` 数值一致性诊断中发现  
**状态**: 📋 待处理（低优先级，仅特定场景触发）  
**关联**: verification_plan.md §4 D2

## 问题描述

cuBQL 和 oxs3d 后端使用不同的法线变换方法：

- **cuBQL**: 使用 `forward_transform` 3×3 旋转矩阵直接变换法线
- **oxs3d**: 使用 `optixTransformNormalFromObjectToWorldSpace()`（逆转置矩阵方法）

## 数学分析

对于**纯旋转+平移**变换矩阵 $M$，正变换和逆转置变换完全等价：
$$M^{-T} = M \quad (\text{when } M \text{ is orthogonal})$$

当矩阵包含**非均匀缩放**时，两者不等价。逆转置矩阵是正确的法线变换方法。

## 当前场景验证

porous 测试场景的所有 instance 变换均为纯旋转+平移（无缩放），因此两种方法产生相同结果。**当前场景无数值影响**。

## 触发条件

包含非均匀缩放的场景会触发此差异。例如：
- `scale(2, 1, 1)` — 沿 X 轴拉伸 2 倍
- 此时 cuBQL 的 forward_transform 法线变换**数学上不正确**

## 修复方案

将 cuBQL 后端的法线变换改为逆转置矩阵方法，与 oxs3d 对齐。或者在 star-3d 层面统一提供正确的法线变换 API。

> 注：oxs3d 使用 `optixTransformNormalFromObjectToWorldSpace()` 是**正确**的做法。cuBQL 使用 forward_transform 在非均匀缩放下是 bug。

## 相关文件

- `stardis-cus3d/custar-3d/0.10/src/` — cuBQL 法线变换实现
- `stardis-cus3d/oxstar-3d/0.10/` — oxs3d 法线变换（OptiX API）
- `stardis-cpu/star-3d/0.10/src/` — CPU 原版参考
