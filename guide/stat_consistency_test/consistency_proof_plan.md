# 数值一致性证明 — 图表设计与数据采集计划

**日期**: 2026-03-03  
**状态**: 待实施  
**目标**: 以物理输运过程的复杂度递进为叙事主线，通过 5 张主力曲线图 + 1 张统计汇总图，完备证明 GPU wavefront 与 CPU depth-first 架构的数值一致性

---

## 一、证明思路

### 1.1 核心原则

每张图同时呈现三组数据，共享同一物理坐标系：

```
理论解析曲线（虚线）  →  证明数学模型正确
GPU wavefront 数据点   →  证明 GPU 实现正确
CPU depth-first 数据点 →  证明 CPU 实现正确
三者重合              →  架构一致性成立
```

读者无需热力学知识即可理解：**三组数据落在同一条曲线上 = 正确且一致**。

### 1.2 叙事线：物理复杂度阶梯

```
图1  纯导热 + 体积源         →  最基础的热传导方程
图2  多材料界面 + 接触热阻    →  增加材料异质性和界面物理
图3  对流换热瞬态             →  增加时间维度 + 边界换热
图4  扩散方程精确解           →  最完整的 PDE 解析解（空间+时间）
图5  多域多物理耦合瞬态       →  最接近工业应用的复杂场景
图6  统计汇总                →  全测试体系的定量判定
```

每一层在前一层基础上增加一个物理维度，读者自然理解验证的完备性。

### 1.3 视觉设计原则

- **禁止退化直线**: 选择的测试必须产出非平凡曲线（抛物线、指数、正弦、分段折线）
- **统一图例**: 解析=灰色虚线, GPU=红色方块, CPU=蓝色圆点, 3σ误差棒
- **公式上图**: 每张图在副标题位置渲染解析公式（LaTeX）
- **误差带**: 解析曲线两侧不画误差带（精确解无不确定度），数据点用误差棒

---

## 二、选定测试与理由

### 图 1: A2 — 体积功率稳态导热（倒抛物线）

**物理**: 平板两面固定温度 $T_0$，体积内均匀热源功率 $P$ [W/m³]，稳态温度分布。

**解析解**:
$$T(x) = \frac{P}{2\lambda}\left(\frac{1}{4} - x^2\right) + T_0$$

其中 $\lambda$ 为导热系数，$x \in [-0.5, 0.5]$。这是 Poisson 方程 $\nabla^2 T = -P/\lambda$ 的一维特解——**经典倒抛物线**，中心温度最高，两端等于边界温度。

**曲线形状**: 对称倒抛物线，峰值在 $x=0$。

**选择理由**: 
- 抛物线是最基础且优美的热传导解，物理直觉清晰
- GPU（`test_sdis_wf_a2_volumic`）和 CPU（`test_sdis_volumic_power`）均已通过
- 非退化曲线（非直线），数据点自然分布在抛物线上

**数据采集**:
- GPU: 修改 `test_sdis_wf_a2_volumic.c`，沿 x 轴等间距输出 ~10 个探针
- CPU: 修改 `test_sdis_volumic_power.c`，输出同坐标或随机坐标探针
- test_id = `"A2"`, sub_id = `"default"`, diff_algo = `"DS"`

**X 轴**: 空间坐标 $x$ [m]  
**Y 轴**: 温度 $T$ [K]

---

### 图 2: A3 — 双材料接触热阻（分段线性 + 温度跳跃族）

**物理**: 两种不同导热系数的材料($\lambda_1, \lambda_2$)拼接，界面处有接触热阻 $R$ [m²·K/W]。左端 $T_0 = 0$，右端 $T_L = 100$ K。

**解析解**:
$$q = \frac{T_L - T_0}{\dfrac{x_0}{\lambda_1} + R + \dfrac{L - x_0}{\lambda_2}}$$

- 左侧 ($0 \le x \le x_0$): $T(x) = T_0 + \dfrac{q}{\lambda_1}\,x$
- 右侧 ($x_0 \le x \le L$): $T(x) = T_L - \dfrac{q}{\lambda_2}\,(L-x)$

其中 $\lambda_1 = 0.1$, $\lambda_2 = 0.2$, $L = 4$, $x_0 = 3$, $R \in \{0.01, 0.1, 1, 10, 100\}$。

**曲线形状**: 5 条分段线性折线，在 $x = 3$ 处有温度跳跃。$R$ 越大，跳跃越大，右侧斜率越平缓。形成一个扇形展开的折线族——**最具参数可视化冲击力的测试**。

**选择理由**: 
- 多条折线叠加，物理意义清晰（热阻影响温度分布）
- 已有 GPU CSV 数据（20 行，3 个 R 值 × 8 探针）
- CPU CSV 数据（16 行）使用不同 R 值和随机位置，两者在同一坐标系中自然分布

**数据采集**: 
- GPU: **已有** `csv_output/gpu/A3.csv`（R=0.01, 0.1, 1），应扩展到 R=10, 100
- CPU: **已有** `csv_output/cpu/A3.csv`（随机 R 和位置）

**X 轴**: 空间坐标 $x$ [m], $x \in [0, 4]$  
**Y 轴**: 温度 $T$ [K]

---

### 图 3: D1 — 六面对流冷却瞬态（指数衰减趋近平衡）

**物理**: 立方体初始温度 $T_0$，六面暴露于不同温度的流体中，对流换热系数 $H$。固体温度从初温指数衰减趋近流体加权平均温度。

**解析解（集总参数模型）**:
$$T(t) = T_0 \, e^{-\nu t} + T_\infty \left(1 - e^{-\nu t}\right)$$

其中:
$$\nu = \frac{\sum_i H_i S_i}{\rho c_p V}, \quad T_\infty = \frac{\sum_i H_i S_i T_{f,i}}{\sum_i H_i S_i}$$

D1 为均匀对流（各面 $H_i$ 相同），D2 为非均匀。

**曲线形状**: 经典指数衰减曲线——从 $T_0$ 快速下降（或上升），渐近趋平于 $T_\infty$。这是**牛顿冷却定律最直观的时域表现**。

**选择理由**:
- 指数曲线是除抛物线外最具辨识度的物理曲线
- D1 更干净（均匀对流，解析解精确），适合做对比标杆
- GPU（`test_sdis_wf_d1_convection`）和 CPU（`test_sdis_convection`）均已通过
- 但 D2 已有 CPU 侧丰富的瞬态 CSV 数据（4 个时间步），且 GPU 有稳态数据
- **决定**: 优先 D1（公式更清晰），若 D1 数据不足则回退 D2

**数据采集**:
- GPU: 修改 `test_sdis_wf_d1_convection.c`，输出 ~8-10 个时间步（含 $t=0$, 稳态）
- CPU: 修改 `test_sdis_convection.c`，输出 ~8-10 个时间步
- 如使用 D2：GPU 需新增瞬态时间步数据
- test_id = `"D1"` (或 `"D2"`), sub_id = `"3D"`

**X 轴**: 时间 $t$ [s]（或无量纲时间 $\nu t$）  
**Y 轴**: 温度 $T$ [K]

---

### 图 4: E3 — 正弦初始温度场的瞬态衰减（$\sin \times \exp$）

**物理**: 三维立方体，初始温度为正弦分布 $T(x,y,z,0) = A\sin(k_x x)\sin(k_y y)\sin(k_z z)$，六面固定为 0。热扩散使振幅指数衰减，空间正弦形状不变。

**解析解（分离变量法）**:
$$T(x,y,z,t) = \frac{1}{\lambda}\left[B_1(x^3z - 3xy^2z) + B_2 \sin(k_x x)\sin(k_y y)\sin(k_z z) \cdot e^{-\alpha(k_x^2+k_y^2+k_z^2)\,t}\right]$$

其中 $\alpha = \lambda / (\rho c_p)$ 为热扩散系数。

对于固定空间位置的时间序列: $T(t) = T_{ss} + A' \cdot e^{-\alpha k^2 t}$（指数衰减到稳态）。  
对于固定时间的空间扫描: $T(x) \propto \sin(k_x x)$（正弦分布，振幅随时间衰减）。

**曲线形状**: 
- 时间维度: 指数衰减（叠加稳态偏移）
- 空间维度: **正弦曲线**——这是所有测试中**唯一的非单调、周期性曲线**

**选择理由**:
- 正弦 × 指数是扩散方程最完整也是最优美的解析解
- 空间正弦分布提供了所有测试中最有"冲击力"的曲线形状
- GPU（`test_sdis_wf_e3_unsteady_analytic`）和 CPU 均已通过
- 可同时展示空间分布和时间衰减

**数据采集**:
- 建议: 固定 $t$，沿 $x$ 轴扫描（$y, z$ 固定），展示正弦空间分布
- 或: 固定空间点，扫描多个 $t$ 值，展示指数衰减
- **推荐方案**: 2 子图——左图 T(x) 空间正弦（多时刻叠加，振幅递减）；右图 T(t) 指数衰减
- test_id = `"E3"`, sub_id 区分时刻或探针

**X 轴**: 空间坐标 $x$ [m]（左图）或时间 $t$ [s]（右图）  
**Y 轴**: 温度 $T$ [K]

---

### 图 5: E5 — 多域多介质大气瞬态（双探针时间曲线）

**物理**: 流体域 + 固体域 + 大气层多介质耦合，从均匀初温 300 K 开始的瞬态热响应。流体接近辐射边界升温快，固体热惯性大升温慢。

**参考值**: 高精度数值求解（硬编码），非封闭解析式。11 个时间步（0, 1000, 2000, ..., 10000 s）× 2 种探针。

**曲线形状**: 两条指数趋近曲线——流体快速上升至 ~310 K 后趋平，固体缓慢上升至 ~305 K。形成经典的 **"fast/slow approach" 双时间尺度对比**。

**选择理由**:
- 最接近工业真实场景（多域耦合 + 大气辐射）
- 已有丰富的 CPU CSV 数据（44 行）
- 双探针的不同瞬态行为提供了一张图中的对比叙事
- GPU 测试已通过（`test_sdis_wf_e5_unsteady_atm`），需补充 GPU CSV 输出

**数据采集**:
- GPU: 修改 `test_sdis_wf_e5_unsteady_atm.c`，输出 11 时间步 × 2 探针
- CPU: **已有** `csv_output/cpu/E5.csv`（44 行）
- test_id = `"E5"`, sub_id = `"fluid_t=..."` / `"solid_t=..."`

**X 轴**: 时间 $t$ [s], $t \in [0, 10000]$  
**Y 轴**: 温度 $T$ [K]

---

### 图 6: 统计汇总（σ 分布 + 全测试 Parity Plot）

**左子图**: σ 分布直方图
- GPU wavefront + CPU depth-first 的 $\sigma = |E - T_\mathrm{ref}| / \mathrm{SE}$ 分布
- 3σ 阈值竖线
- 分布应集中在 0–2 区间（蒙特卡洛正态预期）

**右子图**: 全测试 Parity Plot ($E$ vs $T_\mathrm{ref}$)
- 45° 对角线 = 完美估计
- GPU 红色方块 + CPU 蓝色圆点散布于对角线两侧
- 温度跨度应覆盖 ~0 K 至 ~2000 K（含 I1 chk2 高温数据）

**选择理由**: 定量统计收束，给出全测试体系的统计判定数字。

**数据来源**: 所有有 CSV 输出的测试的合集。

---

## 三、数据采集实施步骤

### Step 1: 新增 CSV 输出（3 对必选 + 1 对推荐）

| 优先级 | 测试 | GPU 文件 | CPU 文件 | 改动模式 |
|:-----:|------|---------|---------|---------|
| **必选** | A2 | `stardis-cus3d/.../test_sdis_wf_a2_volumic.c` | `stardis-cpu/.../test_sdis_volumic_power.c` | 沿 x 轴 ~10 点 |
| **必选** | D1 | `stardis-cus3d/.../test_sdis_wf_d1_convection.c` | `stardis-cpu/.../test_sdis_convection.c` | ~8 时间步 |
| **必选** | E3 | `stardis-cus3d/.../test_sdis_wf_e3_unsteady_analytic.c` | `stardis-cpu/.../test_sdis_unsteady_analytic_profile.c` | 沿 x 扫描 + 多时刻 |
| 推荐 | E5-GPU | `stardis-cus3d/.../test_sdis_wf_e5_unsteady_atm.c` | (CPU 已有) | 11 时间 × 2 探针 |

改动模式同现有测试（`#include "test_sdis_csv_utils.h"`，`csv_open/csv_row/csv_close`），环境变量 `SDIS_CSV_DIR` 触发。

### Step 2: A3 GPU 扩展 R 值

当前 GPU A3 CSV 仅有 R=0.01, 0.1, 1 三个值。修改 `test_sdis_wf_a3_contact_resistance.c` 扩展到 R=10, 100（如果测试原本包含但 CSV 未输出，则仅需调整 CSV 写入逻辑）。

### Step 3: 构建 + 运行

```powershell
# GPU 侧
cd stardis-cus3d/build
cmake --build . --config Release > build.log 2>&1
$env:SDIS_CSV_DIR = "D:\Works\Projects\Stardis-GPU\csv_output\gpu"
ctest -C Release -R "test_sdis_wf_(a2|a3|d1|e3|e5)" -V --timeout 600

# CPU 侧
cd stardis-cpu-test-data/build  # 或 stardis-cpu/build
cmake --build . --config Release > build.log 2>&1
$env:SDIS_CSV_DIR = "D:\Works\Projects\Stardis-GPU\csv_output\cpu"
ctest -C Release -R "test_sdis_(volumic_power|contact_resistance|convection|unsteady_analytic_profile|unsteady_atm)" -V --timeout 600
```

### Step 4: 重写绘图脚本 + 分析文档

见 §四 和 §五。

---

## 四、绘图脚本设计

### 统一视觉规范

```python
# 颜色
C_THEORY = '#555555'   # 灰色虚线 (理论解析)
C_GPU    = '#D62828'    # 红色 (GPU wavefront)
C_CPU    = '#1D3557'    # 深蓝 (CPU depth-first)

# 标记
MK_GPU = 's'  # 方块
MK_CPU = 'o'  # 圆点

# 线型
LS_THEORY = '--'  # 虚线

# 每张图结构:
#   suptitle = 物理过程名
#   subtitle (text) = LaTeX 解析公式
#   legend 固定三项: "Analytical" / "GPU wavefront" / "CPU depth-first"
#   3σ 误差棒
#   网格 alpha=0.2
```

### 图 1: A2 抛物线

```
布局: 单图
X: probe_x ∈ [-0.5, 0.5]
Y: T [K]
虚线: T(x) = P/(2λ)(1/4 - x²) + T₀, 300 点采样
GPU: 红色方块 + 3σ 误差棒
CPU: 蓝色圆点 + 3σ 误差棒
标注: 在抛物线顶部标注 T_max = P/(8λ) + T₀
```

### 图 2: A3 分段折线族

```
布局: 单图
X: probe_x ∈ [0, 4]
Y: T [K] ∈ [0, 100]
5 条虚线（每条对应一个 R 值）: 颜色渐变 (浅→深) 对应 R 从小到大
GPU: 方块，颜色与对应 R 的虚线一致
CPU: 圆点，颜色与对应 R 的虚线一致
图例标注每个 R 值
在 x=3 处画垂直虚线标注 "Interface"
```

### 图 3: D1 指数衰减

```
布局: 单图
X: 时间 t [s] (或 νt 无量纲)
Y: T [K]
虚线: T(t) = T₀·exp(-νt) + T∞·(1-exp(-νt))
GPU: 红色方块 + 3σ 误差棒
CPU: 蓝色圆点 + 3σ 误差棒
标注: T∞ 水平虚线, T₀ 初始点
```

### 图 4: E3 正弦 × 指数（双子图）

```
布局: 1×2 子图

左图 "空间分布":
  X: x ∈ [0, L]
  Y: T [K]
  多条虚线: 同一 x 扫描在不同时刻 t₁, t₂, t₃... 的 sin 分布 (振幅递减)
  数据点: GPU/CPU 分别标注
  效果: 一族正弦曲线振幅从大到小排列

右图 "时间衰减":
  X: t [s]
  Y: T [K] (固定空间点)
  虚线: T_ss + A'·exp(-αk²t)
  GPU + CPU 数据点
```

### 图 5: E5 双探针瞬态

```
布局: 单图（双曲线共用坐标轴）
X: t [s] ∈ [0, 10000]
Y: T [K]

流体探针:
  虚线: 参考值连线（灰色虚线）
  GPU: 红色方块
  CPU: 蓝色圆点

固体探针:
  虚线: 参考值连线（灰色虚线，较深色调）
  GPU: 红色菱形
  CPU: 蓝色三角

标注: "Fluid probe (fast response)" / "Solid probe (slow response)"
置信带: 数据点周围半透明填充
```

### 图 6: 汇总双子图

```
布局: 1×2 子图

左图 "σ 分布":
  直方图: GPU (红) + CPU (蓝) 叠加
  竖线: σ=3 阈值
  
右图 "Parity Plot":
  散点: GPU (红方块) + CPU (蓝圆点)
  对角线: E = ref
  温度跨度: ~0 到 ~2000 K
```

---

## 五、分析文档结构

```markdown
# 数值一致性分析

## 1. 验证方法
   3σ MC 准则定义, 对比标的说明

## 2. 稳态热传导 (图1: A2)
   Poisson 方程 + 体积源, 抛物线 T(x)
   → GPU/CPU 数据点与解析抛物线重合, σ 统计

## 3. 多材料界面传导 (图2: A3)
   分段线性 + 温度跳跃, R 参数扫描
   → 5 组折线族验证材料异质性处理

## 4. 对流换热瞬态 (图3: D1)
   牛顿冷却定律指数衰减
   → GPU/CPU 时间序列与解析解重合

## 5. 扩散方程精确解 (图4: E3)
   分离变量法 sin×exp
   → 空间正弦分布 + 时间衰减的双维度验证

## 6. 多域耦合瞬态 (图5: E5)
   流体/固体/大气三域
   → 双时间尺度响应验证

## 7. 统计汇总 (图6)
   σ 分布, parity plot, 全局通过率

## 8. 已知局限
   I1 GPU 超时/偏差(嵌套包壳)
   C3 GPU 段错误(Picard多阶)

## 9. 结论
```

---

## 六、与现有数据的关系

| 现有 CSV | 新计划中的用途 |
|---------|-------------|
| `gpu/A3.csv` | **保留**, 图2 主力数据（需扩展 R=10, 100） |
| `cpu/A3.csv` | **保留**, 图2 对比数据 |
| `gpu/A7.csv` | 仅纳入图6汇总 |
| `cpu/A7.csv` | 仅纳入图6汇总 |
| `gpu/B3.csv` | 仅纳入图6汇总 |
| `cpu/B3.csv` | 仅纳入图6汇总 |
| `gpu/B5.csv` | 仅纳入图6汇总 |
| `cpu/B5.csv` | 仅纳入图6汇总 |
| `cpu/C3.csv` | 仅纳入图6汇总 |
| `gpu/D2.csv` | 仅纳入图6汇总（若新增 D1 则 D2 降级为补充） |
| `cpu/D2.csv` | 同上 |
| `cpu/E5.csv` | **保留**, 图5 CPU 主力数据 |
| `cpu/F1.csv` | 仅纳入图6汇总 |
| `gpu/F2.csv` | 仅纳入图6汇总 |
| `cpu/F2.csv` | 仅纳入图6汇总 |
| `gpu/I1.csv` | 已知缺陷，§8 讨论 |
| `cpu/I1.csv` | 仅纳入图6汇总 |

**新增采集**:
| 新增 CSV | 用途 |
|---------|------|
| `gpu/A2.csv` + `cpu/A2.csv` | 图1 (抛物线) |
| `gpu/D1.csv` + `cpu/D1.csv` | 图3 (指数衰减) |
| `gpu/E3.csv` + `cpu/E3.csv` | 图4 (正弦×指数) |
| `gpu/E5.csv` | 图5 GPU 侧数据 |

---

*计划创建: 2026-03-03 | 预计实施: 1-2 个工作日*
