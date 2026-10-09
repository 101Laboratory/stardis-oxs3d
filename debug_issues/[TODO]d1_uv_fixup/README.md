# [TODO] D1: UV fixup 逻辑对齐

**来源**: `debug_issues/oxs3d_numerical_inconsistency/` 数值一致性诊断中发现  
**状态**: 📋 待处理（低优先级，代码质量改进）  
**关联**: verification_plan.md §2.3 H1

## 问题描述

cuBQL 和 oxs3d 后端的 UV fixup（重心坐标修正）逻辑不等价：

- **cuBQL** (`cus3d_trace_util.h` L27-36): `w < 0` 时从较大的 u/v 分量扣减，保证 `w + u + v == 1` 不变量
- **oxs3d** (`ox_s3d_internal.h` L326-336): 三个分量独立 clamp 到 `[0, 1]`，不做联合约束

## 实验验证结果

在 porous 场景（320×320 spp=32）测试中，将 oxs3d UV fixup 对齐到 cuBQL 等价实现后，**所有计数器完全不变（零差异）**。

原因：OptiX 硬件返回的 barycentric 坐标已在合法范围内（u,v ≥ 0, w+u+v ≈ 1），两种 fixup 逻辑产生相同的输出。差异**从未在实际数据上被触发**。

## 为什么仍需修复

1. **代码正确性**: 两种实现在数学上不等价。在退化三角形或极端几何下可能产生不同结果。
2. **可维护性**: 两个后端的 UV fixup 应保持一致，避免未来场景变更时出现难以追踪的差异。
3. **防御性编程**: 即使当前 OptiX 版本返回合法值，不能假设未来版本也如此。

## 修复方案

将 oxs3d 的 `ox_s3d_internal.h` L326-336 的独立 clamp 替换为 cuBQL 等价的联合约束实现。

## 相关文件

- `stardis-cus3d/custar-3d/0.10/src/cus3d_trace_util.h` — cuBQL 参考实现
- `stardis-cus3d/oxstar-3d/0.10/include/ox_s3d_internal.h` — oxs3d 当前实现
- `stardis-cpu/star-3d/0.10/src/s3d_scene_view_trace.c` — CPU 原版参考
