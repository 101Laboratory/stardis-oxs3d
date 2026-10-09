# [RESOLVED] oxs3d 数值不一致性

**状态**: ✅ 已解决  
**创建日期**: 2026-02-28  
**解决日期**: 2026-03-01  
**严重度**: 高 — 影响 cuBQL→OptiX 迁移的数值一致性判定

---

## 问题描述

从 custar-3d（cuBQL）后端迁移到 oxstar-3d（OptiX）后端后，热传导渲染结果偏亮，总步数减少。初步日志显示 `rays=` 数量减少约 73%，`cond_ds_retry` 从 772 万骤降至 4。

---

## 根因

两个独立 bug 共同导致：

**D0 — Windows MSVC `printf` `size_t` 截断（日志数据误读）**
- `printf("%u", size_t_val)` 在 MSVC 的 64 位 `size_t` 下截断高 32 位
- 导致日志中 `rays=` 等计数误读为较小值，造成 "rays 减少 73%" 的假象
- 实际射线数量正常，仅日志显示错误

**D3 — anyhit tMax Bug**
- OptiX anyhit shader 中 `tMax` 处理存在 bug，导致部分射线错误提前终止
- 修复后渲染结果与 cuBQL 视觉一致性恢复

---

## 修复

- D0：修正 `size_t` 的 `printf` 格式说明符（`%zu` 或强制转换为 `unsigned long long`）
- D3：修复 anyhit shader 中 `tMax` 边界判定逻辑

修复后 porous 场景在多角度/多 spp 配置下与 CPU 参考值 3σ 一致性验证通过。

---

## 附加待处理项

发现过程中识别的两个低优先级代码质量问题已移至独立 TODO 目录：
- `[TODO]d1_uv_fixup/` — UV fixup 逻辑对齐
- `[TODO]d2_normal_transform/` — 法线变换方法对齐

---

## 相关文件

| 文件/目录 | 说明 |
|----------|------|
| `verification_plan.md` | 完整诊断与验证方案 |
| `cubql_last_commit/` | cuBQL 最后可用提交的参考数据 |
| `oxs3d_first_commit/` | oxs3d 首个可运行提交的对比数据 |
| `log_stardis-cus3d.txt` | cus3d 运行日志 |
| `log_stardis-oxs3d.txt` | oxs3d 运行日志 |
