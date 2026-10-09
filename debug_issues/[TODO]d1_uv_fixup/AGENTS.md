# [TODO] D1: UV fixup 逻辑对齐

**状态**: 📋 待处理（低优先级，代码质量改进）  
**优先级**: 低 — 当前 OptiX 版本未触发差异，为防御性修复  
**来源**: `[RESOLVED]oxs3d_numerical_inconsistency/` 诊断过程中发现  
**发现日期**: 2026-02-28

---

## 问题描述

cuBQL 和 oxs3d 后端的 UV fixup（重心坐标修正）逻辑不等价：

- **cuBQL** (`cus3d_trace_util.h` L27-36): 联合约束，`w < 0` 时从较大的 u/v 分量扣减，保证 `w + u + v == 1` 不变量
- **oxs3d** (`ox_s3d_internal.h` L326-336): 三个分量独立 clamp 到 `[0, 1]`，不做联合约束

实验验证：porous 场景下将 oxs3d UV fixup 对齐为 cuBQL 等价实现后，所有计数器零差异。原因是 OptiX 硬件返回的 barycentric 坐标已在合法范围内，差异从未实际触发。

**当前影响**：无（正常几何下 OptiX 返回合法 barycentric 坐标）  
**潜在风险**：退化三角形或极端几何下可能产生不同结果

---

## 修复方案

将 `ox_s3d_internal.h` L326-336 的独立 clamp 替换为联合约束实现（对齐 cuBQL 逻辑）。

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `README.md` | 完整问题描述、实验验证结果、修复方案 |
