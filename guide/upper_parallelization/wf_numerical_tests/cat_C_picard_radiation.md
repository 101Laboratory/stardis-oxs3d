# 类别 C：导热-辐射耦合 Picard 实验原理

**关联清单**: [../numerical_correctness_test_checklist.md](../numerical_correctness_test_checklist.md)  
**GPU 测试 ID**: WF-C1, WF-C3, WF-C4  
**物理领域**: 稳态导热-辐射耦合，Picard 迭代法  
**共同容差**: `eq_eps(T.E, ref, 3 * T.SE)`

---

## 物理总论

当固体间存在辐射热交换时，热传导方程变为非线性——辐射热通量与 $T^4$ 成正比。求解器使用 **Picard 迭代**（固定点迭代）线性化：

$$-\nabla \cdot (\lambda \nabla T^{(n+1)}) + h_r (T^{(n+1)} - T_{env}) = P$$

其中线性化辐射换热系数：

$$h_r = 4 \sigma T_{ref}^3 \varepsilon$$

$\sigma = 5.6696 \times 10^{-8}$ W/(m²·K⁴) 是 Stefan-Boltzmann 常数，$T_{ref}$ 是参考温度（由 Picard 阶数决定），$\varepsilon$ 是表面发射率。

每次 Picard 迭代，$T_{ref}$ 用上一次迭代结果更新，界面发射率 $\varepsilon$ 和参考温度共同决定辐射耦合强度。

### Picard 阶数含义

| picard_order | $T_{ref}$ 来源 | 含义 |
|---|---|---|
| 0 | 用户指定常数 | 仅一次线性化，最粗糙 |
| 1 | picard_order=0 的结果 | 一次修正 |
| 2 | picard_order=1 的结果 | 两次修正 |
| $n$ | picard_order=$n-1$ 的结果 | 收敛到自洽解 |

---

## WF-C1：导热-辐射耦合 picard_order=1（对标 `test_sdis_conducto_radiative`）

### 物理场景

表面辐射耦合的薄板导热：两面固定温度，4 个固体表面有辐射发射率 $\varepsilon = 1$。

```
  T₀=300K  ├── λ=0.1 ── (辐射面 ε=1 ↑↓) ──┤  T₁=310K
  x=-1         厚度 = 2.0 m              x=+1
```

### 场景详细描述

- **主固体**: 从 $x = -1$ 到 $x = +1$，厚度 $= 2$ m
- **环绕固体**: 热导率 $= 0$（绝热）
- **左边界** ($x = -1$): 固定温度 $T_0 = 300$ K
- **右边界** ($x = +1$): 固定温度 $T_1 = 310$ K
- **辐射面**: ±X 面发射率 $\varepsilon = 1$, specular_fraction $= 1$

### 界面配置

| 界面 | 类型 | 参数 |
|---|---|---|
| 0 (绝热) | adiabatic | convection=-1, emissivity=-1 |
| 1 (辐射面) | solid radiating | emissivity=1, specular=1, $T_{ref}$=300K |
| 2 (弹射) | bounce | emissivity=0, specular=1 |
| 3 (T₀ BC) | Dirichlet + 辐射 | T=300K, emissivity=1, $T_{ref}$=300K |
| 4 (T₁ BC) | Dirichlet + 辐射 | T=310K, emissivity=1, $T_{ref}$=310K |

### 解析推导

线性化辐射换热系数：

$$h_r = 4 \sigma T_{ref}^3 \varepsilon = 4 \times 5.6696 \times 10^{-8} \times 300^3 \times 1 = 6.12317 \times 10^{-3} \text{ W/(m²·K)}$$

由于辐射的存在，表面温度不完全等于边界温度。设左壁面温度 $T_{s0}$、右壁面温度 $T_{s1}$：

$$\text{tmp} = \frac{\lambda}{2\lambda + \text{thickness} \times h_r} (T_1 - T_0)$$

$$T_{s0} = T_0 + \text{tmp}, \quad T_{s1} = T_1 - \text{tmp}$$

内部线性插值：

$$T(x) = T_{s0} + \frac{x - (-1)}{2} (T_{s1} - T_{s0}) = T_{s0}(1 - u) + T_{s1} \cdot u$$

其中 $u = (x + 1) / 2$。

### 参数

| 参数 | 值 | 单位 |
|---|---|---|
| $\lambda$ | 0.1 | W/(m·K) |
| $T_0$ | 300 | K |
| $T_1$ | 310 | K |
| $\varepsilon$ | 1 | — |
| $T_{ref}$ | 300 | K |
| thickness | 2.0 | m |
| Boltzmann | $5.6696 \times 10^{-8}$ | W/(m²·K⁴) |

### GPU Wavefront 适配

- **关键验证**: Picard 迭代 + 辐射换热的正确耦合
- **picard_order**: 先测 order=0（常数 $T_{ref}$），再测 order=1
- **SPP**: 512（辐射路径增加方差）
- **通过标准**: ≥95% 探针在 3σ 内

---

## WF-C3：Picard 多阶迭代（对标 `test_sdis_picard`）

### 物理场景

薄固体板（厚度 0.1 m）+ 右侧流体域（1.0 m）+ 辐射环境。Picard 迭代直到自洽。

```
  辐射环境        固体           流体
  (T_env)   ├── 0.1m ──┤──── 1.0m ────┤
           x=0         x=0.1        x=1.1
           探针 @ x=0.05
```

### 参数

| 参数 | 值 | 单位 |
|---|---|---|
| $\lambda$ | 1.15 | W/(m·K) |
| $\rho$ | 1000 | kg/m³ |
| $c_p$ | 800 | J/(kg·K) |
| $\varepsilon$ | 1 | — |
| $\delta$ | 0.0025 | m |
| 体积功率(初始) | 0 | W/m³ |
| probe | (0.05, 0, 0) | m |
| $N$ | 10000 | — |

### Picard 多阶参考值

#### Picard1 (常数 $T_{ref} = 300$ K)

| 物理量 | 参考值 [K] |
|---|---|
| $T$ | 314.99999999999989 |
| $T_1$ (左壁面) | 307.64122364709766 |
| $T_2$ (右壁面) | 322.35877635290217 |

#### Picard1 (T⁴ 参考)

$$T_{ref}^{-X} = T_1, \quad T_{ref}^{+X} = T_2, \quad T_{ref}^{boundary} = 350K$$
辐射环境: $T_{env} = 280$ K, $T_{ref,env} = 280$ K

| 物理量 | 参考值 [K] |
|---|---|
| $T$ | 320.37126474482994 |
| $T_1$ | 312.12650299072266 |
| $T_2$ | 328.61602649893723 |

#### Picard2 (常数 $T_{ref} = 300$ K)

| 物理量 | 参考值 [K] |
|---|---|
| $T$ | 320.37126474482994 |
| $T_1$ | 312.12650299072266 |
| $T_2$ | 328.61602649893723 |

#### Picard3 ($\Delta T = 300$ K, $t_{range} = [200, 500]$)

$$T_{boundary,+X} = 500K, \quad T_{ref}^{-X} = 350K, \quad T_{ref}^{+X} = 450K$$
辐射环境: $T_{env} = 200$ K, $T_{ref,env} = 200$ K

| 物理量 | 参考值 [K] |
|---|---|
| $T$ | 416.4023 |
| $T_1$ | 372.7557 |
| $T_2$ | 460.0489 |

#### Picard1 + 体积功率 1000 W/m³ (常数 $T_{ref} = 300$ K)

| 物理量 | 参考值 [K] |
|---|---|
| $T$ | 324.25266420769509 |
| $T_1$ | 315.80693133305368 |
| $T_2$ | 330.52448403885825 |

#### Picard1 + 体积功率 1000 W/m³ (T⁴ 参考)

| 物理量 | 参考值 [K] |
|---|---|
| $T$ | 327.95981050850446 |
| $T_1$ | 318.75148773193359 |
| $T_2$ | 334.99422024159708 |

### GPU Wavefront 适配

- **关键验证**: 多 Picard 阶数下温度收敛行为
- **挑战**: Wavefront 架构中 Picard 迭代需在 host 侧循环驱动
- **SPP**: 512
- **test 设计**: 对每个 Picard 配置分别发 wavefront 请求，验证 $T$, $T_1$, $T_2$ 各在 3σ 内
- **通过标准**: 每配置 ≥95% 通过

---

## WF-C4：复合瞬态 + Picard + 通量（对标 `test_sdis_flux2`）

### 物理场景

最复杂的 Picard 测试：通量 BC + 对流 BC + 辐射耦合，在多个时间步验证。参考值为硬编码的 15×5 矩阵（来自高精度 CPU 求解）。

### 参数

- 15 个时间步 × 5 个物理量（$T$, $T_1$, $T_2$, flux, power）
- 硬编码参考矩阵（非解析解）

### GPU Wavefront 适配

- **优先级**: P3（最复杂，依赖所有前置功能）
- **挑战**: 瞬态 + Picard + 通量三者耦合
- **建议**: 完成 WF-C1/C3 后再开展

---

## 类别 C 小结

| WF ID | 验证重点 | 失败暗示 |
|---|---|---|
| WF-C1 | 基础辐射线性化 | $h_r$ 计算或辐射面处理错误 |
| WF-C3 | 多阶 Picard 收敛 | 迭代循环或 $T_{ref}$ 更新传递错误 |
| WF-C4 | 最复杂耦合场景 | 多物理耦合中的数值传递链断裂 |

**执行顺序**: WF-C1（P0）→ WF-C3（P1）→ WF-C4（P3）
