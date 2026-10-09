# WF-C3 Picard 多阶测试系统性正偏分析

**日期**: 2026-02-16  
**测试**: `test_sdis_wf_c3_picard_multi`  
**结果**: FAIL — 2/6 configs pass (33.3%)  
**状态**: wavefront picard 路径已确认存在系统性正偏

---

## 测试输出摘要

| Config | 描述 | wf (K) | ref (K) | Δ (K) | SE (K) | Δ/SE (σ) | Pass? |
|--------|------|--------|---------|-------|--------|----------|-------|
| [0] | Picard1 const-Tref | 316.659 | 315.000 | +1.66 | 0.40 | 4.1 | ❌ |
| [1] | Picard1 T4-ref | 321.797 | 320.371 | +1.43 | 0.39 | 3.7 | ❌ |
| [2] | Picard2 const-Tref | 322.406 | 320.371 | +2.04 | 0.46 | 4.4 | ❌ |
| [3] | Picard3 large-dT | 418.590 | 416.402 | +2.19 | 1.33 | 1.6 | ✅ |
| [4] | Picard1+P const-Tref | 325.881 | 324.253 | +1.63 | 0.42 | 3.9 | ❌ |
| [5] | Picard1+P T4-ref | 329.100 | 327.960 | +1.14 | 0.40 | 2.8 | ✅ |

通过阈值: 3σ

---

## 偏差特征

1. **全部6组均为正偏** — wavefront 结果始终高于参考值 +1.1 ~ +2.2K
2. **SE 足够小** — 10000 realisations 下 SE ≈ 0.4K，置信区间窄，排除随机波动
3. **[3] 和 [5] 仅因 SE 较大而 "通过"** — 偏差量级与其他组一致
4. **[2] Picard2 出现越界错误** — `invalid sub-path temperature 560K (range [280,350])`，96 个 fail realisations

---

## 参数验证：GPU vs CPU 100% 一致

逐项对比 `test_sdis_wf_c3_picard_multi.c` (GPU) vs `test_sdis_picard.c` (CPU)：

| 项目 | 结果 |
|------|------|
| 6 个参考温度 | ✅ 完全一致（精确到末位） |
| 物理参数 (λ, ρ, cp, δ, ε, specular) | ✅ 完全一致 |
| 几何 (12 顶点, 22 三角形) | ✅ 完全一致 |
| 三角形→界面映射 | ✅ 完全一致 |
| 边界条件 (4 种界面类型) | ✅ 完全一致 |
| 辐射环境 (temperature, reference) | ✅ 完全一致 |
| 探针位置 (0.05, 0, 0) | ✅ 完全一致 |
| 6 组子配置 (picard_order, diff_algo, t_range, volumic_power, Tref 各值) | ✅ 完全一致 |

**结论：测试构造无误，偏差来自 wavefront solver 本身。**

---

## 根因分析

### 排除项
- ❌ 测试参数错误 → 已逐项验证一致
- ❌ 参考值错误 → 来自 CPU depth-first solver 高精度计算
- ❌ 统计波动 → SE ≈ 0.4K，偏差 ≈ 4σ，概率 < 0.01%

### 可能根因

1. **Picard 辐射权重累加方式** — wavefront 架构下 picard 子路径的辐射贡献可能存在累加偏差
2. **sub-path 温度截断** — [2] 出现 560K 越界（t_range=[280,350]），截断处理可能引入偏差
3. **T⁴ 线性化展开误差** — wavefront 中 $T_{ref}$ 的选取时机可能与 depth-first 不完全等价
4. **辐射路径终止条件** — wavefront batching 中辐射路径的终止判断可能略有不同

### 偏差量级估计

- 平均正偏: ~1.7K
- 相对误差: ~0.5% (在 ~320K 基础上)
- 一致方向: 全部偏高

---

## 相关日志

### [2] Picard2 越界错误
```
stardis-solver (error): wavefront M8: invalid sub-path temperature 560K (range [280, 350])
```
出现约 20 次，导致 96/10000 realisations 失败。说明 Picard2 阶下 wavefront 的子路径温度估计可以大幅超出预设范围。

---

## 建议

1. **C3 标记为 known-fail** — 作为 wavefront picard 路径修复的回归靶标
2. **优先排查 M8 (picard radiation) step 实现** — 对比 depth-first 的 picard 权重计算逻辑
3. **增加诊断**: 开启 `P0_ENABLE_DIAG=1` 对比同场景下 wavefront vs depth-first 的逐 realisation 统计
4. **考虑扩大 t_range** — [2] 的 560K 越界暗示 t_range=[280,350] 对 picard2 过窄

---

## 关联文件

- GPU 测试: `stardis-cus3d/stardis-solver/0.16.2/src/test_sdis_wf_c3_picard_multi.c`
- CPU 测试: `stardis-cpu/stardis-solver/0.16.2/src/test_sdis_picard.c`
- 设计文档: `guide/upper_parallelization/wf_numerical_tests/cat_C_picard_radiation.md`
- wavefront M8 实现: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c`
