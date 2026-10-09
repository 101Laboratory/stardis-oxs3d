# [RESOLVED] Block Firefly Noise

**状态**: ✅ 已解决  
**创建日期**: 2026-02-17  
**解决日期**: 2026-02-17  
**严重度**: 高 — 影响所有 wavefront 渲染图像的温度分布正确性

---

## 问题描述

GPU persistent wavefront solver 渲染结果出现 **block 状噪声**：
- 噪声以矩形块为单位（约 32×32 像素），块内温度一致但与周围像素偏差明显
- 噪声固定于图像空间坐标，与相机角度无关
- pool_size 改变时，块的大小和覆盖范围随之变化
- 仅出现在直接命中实体的区域；反射像命中多孔体时可见（经 LAT 边界镜面反射平滑后浮现）

---

## 根因

`harvest_completed_paths()` 在 drain phase 中**重复累加**已完成路径的温度贡献。

完成状态的路径在每个 wavefront 步骤内均被 harvest，导致同一条路径的温度被多次叠加到图像像素，造成系统性正偏。噪声的 block 形状来源于 GPU 任务分块（warp/block 粒度的路径分配）。

---

## 修复方案

引入 `PATH_HARVESTED` 终态：路径首次被 harvest 后立即标记为该状态，后续步骤中跳过已标记路径，阻止重复累加。

辅助工具：实现 per-path CBRNG（Threefry4x64），key=(px, py, spp, seed)，用于排除 RNG 共享假说并定位根因。

---

## 影响范围

所有 wavefront persistent solver 渲染路径，包括直接辐射和经界面反射/折射的多次弹射路径。修复后图像质量恢复正常。

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `RESOLVED_block_firefly_noise.md` | 完整分析与解决报告 |
| `investigation.md` | 源头诊断记录（分阶段假说验证） |
| `firefly_analysis.md` | 萤火虫噪声成因综合分析 |
| `fix_per_path_cbrng.md` | per-path CBRNG 实现记录（辅助定位工具） |
| `verification_plan.md` | 验证方案 |
