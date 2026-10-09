# 类别 B：探针/边界/介质求解实验原理

**关联清单**: [../numerical_correctness_test_checklist.md](../numerical_correctness_test_checklist.md)  
**GPU 测试 ID**: WF-B2, WF-B3, WF-B5  
**物理领域**: 稳态温度/通量求解——探针(probe)、边界(boundary)、介质平均(medium)  
**共同容差**: `eq_eps(T.E, ref, 3 * T.SE)`

---

## 物理总论

本类别测试覆盖求解器的三种主要输出模式：

1. **探针求解 (probe)**: 在指定空间点估计温度 $T(\mathbf{x})$
2. **边界求解 (boundary)**: 在材料边界面上估计温度（含通量分量 CF/RF/TF）
3. **介质平均 (medium)**: 估计体积域内平均温度 $\bar{T} = \frac{\int_\Omega T \, dV}{\int_\Omega dV}$

对于 wavefront 架构，探针求解可通过 `sdis_solve_wavefront_probe` 直接映射；边界求解需要新接口 `sdis_solve_wavefront_boundary`；介质平均可能需要专门的体积采样策略。

---

## WF-B2：Dirichlet + 对流边界稳态（对标 `test_sdis_solve_boundary`）

### 物理场景

单材料立方体（$A = 1$ m），一面 Dirichlet（$x = 0$, 温度 $T_b$），对面 Robin（$x = 1$, 对流换热系数 $H$, 流体温度 $T_f$），其余四面绝热。

```
  T_b (固定温度)      λ (导体)         H, T_f (对流冷却)
  ├────────────── ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ ──────────────┤
  x=0                                            x=1=A
               ← 绝热 (4面) →
```

### 解析推导

一维稳态 $-\lambda \frac{d^2T}{dx^2} = 0$ → $T(x) = c_1 x + c_2$

边界条件：
- $T(0) = T_b$ → $c_2 = T_b$
- $-\lambda c_1 = H(T(A) - T_f)$ → $c_1 = \frac{H(T_f - T_b)}{H \cdot A + \lambda}$

在 $x = A$ 处（对流面温度）：

$$\boxed{T_{boundary} = \frac{H \cdot T_f + \lambda \cdot T_b / A}{H + \lambda / A}}$$

### 参数

| 参数 | 值 | 单位 | CPU 源标识 |
|---|---|---|---|
| `T_f` | 310.0 | K | `Tf` |
| `T_b` | 300.0 | K | `Tb` |
| `H` | 0.5 | W/(m²·K) | `H` |
| `λ` | 0.1 | W/(m·K) | `LAMBDA` |
| `A` | 1.0 | m | 单位立方体边长 |
| `N` | 10000 | — | MC 实现次数 |

### 材料属性

| 属性 | 值 | 单位 |
|---|---|---|
| 比热容 $c_p$ | 2.0 | J/(kg·K) |
| 热导率 $\lambda$ | 0.1 | W/(m·K) |
| 体积质量 $\rho$ | 25.0 | kg/m³ |
| delta | 0.05 | m |
| 初始温度($t \le 0$) | 310.0 | K |

### 解析参考值

$$T_{boundary} = \frac{0.5 \times 310.0 + 0.1 \times 300.0}{0.5 + 0.1} = \frac{155.0 + 30.0}{0.6} = 308.333\ldots \text{ K}$$

### GPU Wavefront 适配

- **API 需求**: `sdis_solve_wavefront_boundary`（需新增），或将 boundary 温度作为 probe 在边界附近求解的极限验证
- **简化策略**: 对于 P0 优先级，可先用 `sdis_solve_wavefront_probe(pos={0.99, 0.5, 0.5})` 近似验证（贴近对流面）
- **SPP**: 256
- **通过标准**: 3σ MC 标准误差内

### 验证代码模式

```c
double ref = (H * Tf + LAMBDA * Tb) / (H + LAMBDA);
/* ref = 308.333... */
CHK(eq_eps(T.E, ref, 3.0 * T.SE) == 1);
```

---

## WF-B3：面通量分量（对标 `test_sdis_solve_boundary_flux`）

### 物理场景

与 B2 同几何，但验证边界面上的三个通量分量：

| 分量 | 符号 | 物理含义 |
|---|---|---|
| **CF** (Conductive Flux) | $q_c$ | 导热通量 $= -\lambda \nabla T \cdot \hat{n}$ |
| **RF** (Radiative Flux) | $q_r$ | 辐射通量（本场景为 0，无辐射） |
| **TF** (Total Flux) | $q_t$ | 总通量 $= q_c + q_r$ |

### 解析参考

对于纯导热稳态：$q_c = H(T_{boundary} - T_f)$，$q_r = 0$，$q_t = q_c$。

### GPU Wavefront 适配

- **挑战**: Wavefront 相机输出通常只有温度通道；通量分量需要扩展输出缓冲区
- **优先级**: P3（需要通道扩展）
- **SPP**: 1024（通量方差通常大于温度方差）
- **前置条件**: 通道扩展 API 就绪

---

## WF-B5：三线性温度场 + 批量探针（对标 `test_sdis_solve_probe_list`）

### 物理场景

精心构造的三线性温度场 $T(x,y,z) = 333x + 432y + 579z + T_0$，立方体各面温度根据此线性函数设定。蒙特卡洛求解器应精确恢复该线性场（对 MC 来说是零方差问题的低方差类比）。

### 参数

CPU 测试使用 `solve_probe_list` API 一次求解多个探针位置，验证批量接口的一致性。

| 参数 | 值 | 说明 |
|---|---|---|
| $a_x$ | 333 | X方向梯度 [K/m] |
| $a_y$ | 432 | Y方向梯度 [K/m] |
| $a_z$ | 579 | Z方向梯度 [K/m] |
| `N` | 10000 | MC 实现次数 |
| 几何 | 超形状 (superquadric) | 非简单立方体 |

### 解析参考

$$T(\mathbf{x}) = 333 x + 432 y + 579 z + T_0$$

对于线性温度场，MC 估计值的方差极低（理论上为零），因此 SE 应当接近零。

### GPU Wavefront 适配

- **API**: `sdis_solve_wavefront_probe_list`（批量探针），或用正交相机像素映射探针位置
- **挑战**: 超形状几何需 GPU BVH 正确支持
- **SPP**: 256
- **优先级**: P3（需超形状几何支持）
- **特殊验证**: SE 应非常小（≈0），说明线性场的 MC 表示是零方差的

---

## 不映射到 GPU 的 B 类测试

| CPU 测试 | 原因 | 处理 |
|---|---|---|
| B1 `solve_probe`（均匀场） | 零方差退化验证，`CHK(T.E == Tf_exact)` | 可作为冒烟测试但不需 MC |
| B4 `solve_medium` | 体积平均积分，与光追后端无关 | 保留 CPU 侧测试 |
| B6 `solve_probe_boundary_list` | 同 B5 但包含边界探针 | 需 boundary wavefront API |

---

## 类别 B 小结

| WF ID | 验证重点 | 失败暗示 |
|---|---|---|
| WF-B2 | 对流 Robin BC + Dirichlet BC | 边界条件类型混合处理错误 |
| WF-B3 | 通量分量输出正确性 | 输出通道扩展实现有误 |
| WF-B5 | 线性场精确恢复 + 超形状几何 | 几何/拓扑处理或批量探针映射错误 |

**执行顺序**: WF-B2（P0）→ WF-B5（P3）→ WF-B3（P3，需通道扩展）
