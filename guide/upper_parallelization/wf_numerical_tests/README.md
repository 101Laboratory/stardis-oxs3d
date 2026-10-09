# GPU Wavefront 数值正确性 — 分类实验原理索引

**上层清单**: [../numerical_correctness_test_checklist.md](../numerical_correctness_test_checklist.md)

---

## 文档目录

| 分类 | 文件 | WF 测试 ID | 优先级 |
|---|---|---|---|
| **A. 纯导热稳态** | [cat_A_steady_conduction.md](cat_A_steady_conduction.md) | WF-A1, A2, A3, A6, A7 | P0/P1/P2 |
| **B. 探针/边界/介质** | [cat_B_probe_boundary_medium.md](cat_B_probe_boundary_medium.md) | WF-B2, B3, B5 | P0/P3 |
| **C. 导热-辐射 Picard** | [cat_C_picard_radiation.md](cat_C_picard_radiation.md) | WF-C1, C3, C4 | P0/P1/P3 |
| **D. 对流** | [cat_D_convection.md](cat_D_convection.md) | WF-D1, D2 | P0/P1 |
| **E. 瞬态/非稳态** | [cat_E_transient.md](cat_E_transient.md) | WF-E1, E2, E3, E5 | P2/P3 |
| **F. 外部通量** | [cat_F_external_flux.md](cat_F_external_flux.md) | WF-F1, F2 | P1 |
| **G. 鲁棒性 + 特殊路径** | [cat_G_robustness.md](cat_G_robustness.md) | WF-G1, A7, I3 | P2/P3 |
| **CSV 输出改造** | [csv_output_modification_guide.md](csv_output_modification_guide.md) | 最小覆盖集 ×10 | — |

## 建议阅读顺序

1. 先读 [上层清单](../numerical_correctness_test_checklist.md) 了解整体框架
2. 按 P0 → P1 → P2 → P3 实施顺序阅读各分类

## 共同元素

- **容差**: 绝大多数 `eq_eps(T.E, ref, 3 * T.SE)`（3σ MC 标准误差）
- **几何**: 单位立方体 $(0,0,0)\to(1,1,1)$ / 单位正方形
- **API**: `sdis_solve_wavefront_probe` (新增公共接口)
- **双重验证**: 验证 A (vs 解析解) + 验证 B (vs depth-first)
