# Per-Pixel Mean Difference Map + 统计显著性检验方案

**日期**: 2026-03-03  
**状态**: 实施中  
**工具**: `scripts/pixel_consistency_analysis.py`  
**依赖**: 一对同场景/同相机参数/同分辨率的 GPU + CPU `.ht` 文件  

---

## 一、动机

现有 [consistency_proof_plan.md](consistency_proof_plan.md) 聚焦 per-probe 曲线对比（~200 行 CSV），可直观验证物理正确性，但空间覆盖有限。  
本方案补充 **per-pixel 全像素** 层面的统计一致性检验，利用相机模式 `.ht` 输出（256×256 = 65,536 像素），提供空间差异热图和严格的多重比较校正。

两种方案互补：
- 曲线对比 → **物理可解释性**（每张图对应一个解析解）
- 像素热图 → **统计完备性**（覆盖全空间，量化系统偏差）

---

## 二、数据源

### 2.1 .ht 文件格式

每像素行: `T.E  T.SE  0 0 0 0  time.E  time.SE`

| 列 | 字段 | 含义 |
|----|-------|------|
| 1 | `T.E` | 温度期望值 (K) |
| 2 | `T.SE` | 温度标准误差 (K) = √(V/N) |
| 3–6 | `0 0 0 0` | 预留位 |
| 7–8 | `time.E`, `time.SE` | 实现时间的期望值和标准误差 |

第一行为 `<width> <height>`，后续 W×H 行按行主序排列。

### 2.2 现有可用配对

| 场景 | CPU | GPU | 分辨率 | spp | 推荐 |
|------|-----|-----|--------|-----|------|
| cube_IR | `IR_rendering_stardis-cpu_256x256x32.ht` | `IR_rendering_stardis-cus3d_256x256x32.ht` | 256×256 | 32 | 调试用 |
| porous | `IR_rendering_stardis-cpu_256x256x4.ht` | `IR_rendering_stardis-cus3d_256x256x4.ht` | 256×256 | 4 | spp太低 |

**正式结论建议**: 补采 spp≥128 配对。

---

## 三、统计方法

### 3.1 逐像素双样本 Z 检验

对每个有效像素 $(x,y)$：

$$Z_{x,y} = \frac{|E_{\text{gpu}}(x,y) - E_{\text{cpu}}(x,y)|}{\sqrt{SE_{\text{gpu}}^2(x,y) + SE_{\text{cpu}}^2(x,y)}}$$

- 跳过 $SE_{\text{combined}} < 10^{-12}$ 的像素（零方差）
- 跳过两端均 $E=0, SE=0$ 的像素（背景/天空）

### 3.2 多重比较校正

对 $N$ 个有效像素的双侧 p 值 $p_i = 2(1 - \Phi(Z_i))$ 执行两种校正：

1. **Bonferroni**: 显著阈值 $\alpha_B = 0.05/N$，即 $Z_{\text{Bonf}} \approx 5.05$ (N=65536)  
   — 最保守。几乎无假阳性，但可能遗漏真偏差。

2. **Benjamini-Hochberg FDR** (α=0.05):  
   排序 p 值 $p_{(1)} \leq p_{(2)} \leq \cdots \leq p_{(N)}$，  
   找最大 $k$ 使 $p_{(k)} \leq \frac{k}{N}\alpha$。  
   — 控制错误发现率 FDR ≤ 5%，统计功效优于 Bonferroni。

### 3.3 判定标准

| 判定等级 | 条件 |
|---------|------|
| **PASS (强)** | Bonferroni 显著像素 = 0，Z 分布 KS p > 0.05 |
| **PASS** | FDR 显著像素 ≤ 1%，Z 均值 < 0.1 |
| **MARGINAL** | FDR 显著像素 ≤ 5% |
| **FAIL** | FDR 显著像素 > 5% 或 Z 均值 > 0.5 |

### 3.4 辅助诊断

- **Z 分布 vs N(0,1)**: 直方图 + Q-Q plot + KS 检验  
  若 GPU/CPU 统计等价，Z 应服从标准正态。偏离揭示系统性偏差。
- **空间自相关**: 显著像素若集中于某区域 → 可能是特定几何/边界的实现差异。

---

## 四、可视化设计

### 4.1 主图: 2×2 面板

| 位置 | 内容 | 色图 |
|------|------|------|
| 左上 | $E_{\text{cpu}}(x,y)$ 温度图 | `inferno` |
| 右上 | $E_{\text{gpu}}(x,y)$ 温度图 | `inferno` |
| 左下 | $\Delta T = E_{\text{gpu}} - E_{\text{cpu}}$ 均值差异 | `RdBu_r` (中心=0) |
| 右下 | $Z(x,y)$ + 显著性标记 | `viridis` + 红色轮廓(Bonf) + 橙色轮廓(FDR) |

### 4.2 诊断图: 1×2 面板

| 左 | Z 直方图 + N(0,1) 叠加 + KS p 值 |
| 右 | Q-Q plot |

### 4.3 视觉规范

- 复用 `plot_comparison.py` 的全局 rcParams 和色彩常量
- 色标标注单位 (K) 和关键统计量
- DPI=200 保存

---

## 五、与现有框架的关系

| 组件 | 已有 | 本方案新增 |
|------|------|-----------|
| .ht 输出 | ✅ `dump_ht_image()` (C) | — |
| per-pixel Z test | ✅ `e2e_compare_images()` (C, 同代码库内) | Python 跨项目版 |
| 多重比较校正 | ❌ | Bonferroni + BH-FDR |
| 空间热图 | ❌ | Mean Difference Map |
| Z 分布诊断 | ❌ | 直方图 + Q-Q + KS |
| Probe CSV 对比 | ✅ `plot_comparison.py` | 不变 |

---

## 六、使用方法

```bash
python scripts/pixel_consistency_analysis.py \
    --cpu  path/to/cpu.ht \
    --gpu  path/to/gpu.ht \
    --scene cube_IR \
    --outdir img/ \
    --report pixel_consistency_report.md
```

输出:
- `img/{scene}_difference_map.png` — 2×2 主图
- `img/{scene}_z_diagnostic.png` — Z 分布诊断
- `pixel_consistency_report.md` — 统计报告

---

*创建: 2026-03-03*
