# 类别 D：对流实验原理

**关联清单**: [../numerical_correctness_test_checklist.md](../numerical_correctness_test_checklist.md)  
**GPU 测试 ID**: WF-D1, WF-D2  
**物理领域**: 集总参数对流换热（牛顿冷却定律）  
**共同容差**: `eq_eps(T.E, ref, 3 * T.SE)`

---

## 物理总论

集总参数模型（Lumped Capacitance Method）适用于 Biot 数 $Bi = \frac{hL_c}{\lambda} \ll 1$ 的场景，此时固体内部温度梯度可忽略，温度仅为时间函数。

能量守恒：

$$\rho c_p V \frac{dT}{dt} = -\sum_i H_i S_i (T - T_{f,i})$$

其中 $H_i$ 为第 $i$ 面的对流换热系数，$S_i$ 为面面积，$T_{f,i}$ 为流体温度。

对均匀 $H$ 简化为：

$$\frac{dT}{dt} = -\nu (T - T_\infty)$$

解为经典指数衰减：

$$\boxed{T(t) = T_0 e^{-\nu t} + T_\infty (1 - e^{-\nu t})}$$

其中 $\nu = \frac{\sum H_i S_i}{\rho c_p V}$ 是系统时间常数的倒数，$T_\infty = \frac{\sum H_i S_i T_{f,i}}{\sum H_i S_i}$ 是稳态平衡温度。

---

## WF-D1：均匀对流六面冷却（对标 `test_sdis_convection`）

### 物理场景

单位立方体（$V = 1$ m³），六面有不同流体温度但相同换热系数 $H$，流体初始温度 $T_{f,0}$。

```
              T₄=340K (+Z)
                 ↓
  T₀=300K ←── ▓▓▓▓ ──→ T₁=310K
  (-X)     ▓▓  ▓▓  ▓▓     (+X)
           T₂=320K  T₃=330K
           (-Y)     (+Y)
                 ↑
              T₅=350K (-Z)
```

### 参数

| 参数 | 值 | 单位 | 说明 |
|---|---|---|---|
| `Tf_0` | 280.0 | K | 流体初始温度 |
| `T0` | 300.0 | K | $-X$ 面流体温度 |
| `T1` | 310.0 | K | $+X$ 面 |
| `T2` | 320.0 | K | $-Y$ 面 |
| `T3` | 330.0 | K | $+Y$ 面 |
| `T4` | 340.0 | K | $+Z$ 面 |
| `T5` | 350.0 | K | $-Z$ 面 |
| `H` | 10.0 | W/(m²·K) | 对流换热系数（所有面相同）|
| `RHO` | 25.0 | kg/m³ | 流体体积质量 |
| `CP` | 2.0 | J/(kg·K) | 流体比热容 |
| `N` | 100000 | — | MC 实现次数 |

### 解析推导（3D）

单位立方体：$V = 1$ m³，每面面积 $S_i = 1$ m²。

$$\nu = \frac{6 \times H}{\rho c_p} = \frac{6 \times 10}{25 \times 2} = \frac{60}{50} = 1.2 \text{ s}^{-1}$$

$$T_\infty = \frac{T_0 + T_1 + T_2 + T_3 + T_4 + T_5}{6} = \frac{300 + 310 + 320 + 330 + 340 + 350}{6} = 325.0 \text{ K}$$

$$T(t) = 280 \times e^{-1.2t} + 325 \times (1 - e^{-1.2t})$$

### 时间采样点

| 索引 $i$ | 时间 $t = i/\nu$ [s] | $e^{-\nu t}$ | 温度 [K] |
|---|---|---|---|
| 0 | $+\infty$ (稳态) | 0 | **325.000** |
| 1 | 0.8333 | $e^{-1}$ = 0.3679 | 308.447 |
| 2 | 1.6667 | $e^{-2}$ = 0.1353 | 318.912 |
| 3 | 2.5000 | $e^{-3}$ = 0.0498 | 322.760 |
| 4 | 3.3333 | $e^{-4}$ = 0.0183 | 324.177 |

### 解析推导（2D，正方形）

单位正方形：$A = 1$ m²（面），周长 4 面，每线段面积 $= 1$ m。

$$\nu_{2D} = \frac{4 \times H}{\rho c_p} = \frac{40}{50} = 0.8 \text{ s}^{-1}$$

$$T_{\infty,2D} = \frac{T_0 + T_1 + T_2 + T_3}{4} = \frac{300 + 310 + 320 + 330}{4} = 315.0 \text{ K}$$

### 观测点

位置：$(0.25, 0.25, 0.25)$

**注意**: 由于是集总参数模型，温度与空间位置无关，任何内部点的温度都相同。观测点位置只影响 MC 路径初始方向，不影响预期温度。

### GPU Wavefront 适配

- **API**: `sdis_solve_wavefront_probe(pos={0.25, 0.25, 0.25}, time=t)` 对每个时间 $t$
- **稳态验证** ($t = \infty$): 最先测试，验证对流 Robin BC 是否正确
- **瞬态验证**: 逐时间步验证指数衰减曲线
- **SPP**: 512（CPU 侧用 100k 实现次数说明方差较大）
- **通过标准**: 3σ MC 标准误差内

### 验证代码模式

```c
double nu = (6.0 * H) / (RHO * CP);
double Tinf = (T0 + T1 + T2 + T3 + T4 + T5) / 6.0;
double ref;
if (time == INFINITY)
    ref = Tinf;
else
    ref = Tf_0 * exp(-nu * time) + Tinf * (1.0 - exp(-nu * time));
CHK(eq_eps(T.E, ref, T.SE * 3.0) == 1);
```

---

## WF-D2：非均匀对流系数（对标 `test_sdis_convection_non_uniform`）

### 物理场景

与 D1 类似，但各面的对流换热系数 $H_i$ 不同。这使得 $\nu$ 和 $T_\infty$ 的公式变为加权形式：

$$\nu = \frac{\sum_i H_i S_i}{\rho c_p V}$$

$$T_\infty = \frac{\sum_i H_i S_i T_{f,i}}{\sum_i H_i S_i}$$

### GPU Wavefront 适配

- **验证重点**: 非均匀边界条件的正确处理
- **SPP**: 512
- **优先级**: P1
- **通过标准**: 3σ

---

## 类别 D 小结

| WF ID | 验证重点 | 失败暗示 |
|---|---|---|
| WF-D1 | 均匀对流指数衰减 | 对流换热边界条件实现错误，或时间积分有误 |
| WF-D2 | 非均匀对流系数加权 | 逐面换热系数差异化处理错误 |

**关键物理量关系**: 

$$T_\infty = 325 \text{ K} \quad \xrightarrow{t \to \infty} \quad T \to T_\infty$$

如果稳态结果正确但瞬态不正确，问题在时间积分；如果两者都不正确，问题在对流 BC 基础实现。

**执行顺序**: WF-D1 稳态（$t=\infty$）→ WF-D1 瞬态 → WF-D2
