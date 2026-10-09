# 耦合热输运 FSM 各物理模块完整分析

> 本文档是 §3.4.1 撰写的核心参考资料。沿耦合路径物理生命周期（RAD → BND → CND/CNV → BND → …）逐模块记录物理方程、MC 算法步骤、FSM 状态表及工程复杂度来源。

---

## 叙事主线与统一范式

以一条蒙特卡洛路径在耦合热输运场景中的完整物理生命周期为主线，沿路径经历的物理过程顺序逐一引入每个 FSM 状态模块。每个模块遵循统一范式：

> **物理方程**（为何会有此过程）→ **蒙特卡洛算法步骤**（如何随机化求解）→ **几何查询需求**（哪些步骤需要 `trace_ray` / `closest_point`）→ **产生的 FSM 状态**（挂起/恢复对）→ **工程复杂度来源**（实际实现超出简化模型之处）

**核心要义**：每个 FSM 状态的存在由其物理方程直接驱动，59 个细粒度状态是物理耦合复杂度的自然投影——3 种热传递模式 × 多种界面拓扑 × 非线性耦合迭代 × 瞬态时间维度 × 实际几何复杂度。

**物理理论来源**：
- Bati et al. 2023（SIGGRAPH/TOG）：§3 物理模型 + §4 三模态路径空间构造 + Algorithms 1-4
- Tregan et al. 2023（PLOS ONE）：传播子形式主义、积分方程形式化
- 源码 `stardis-cpu/`（递归求解器 baseline）、`stardis-oxs3d-merge-phase/`（Wavefront 实现）

---

## 1. 辐射传播模块（RAD）——渲染方程驱动的多次弹射

### 物理背景

- 对应 **Bati Eq. 6**（简化渲染方程，灰体漫射表面）与 **Eq. 19**（辐亮度温度 $\theta_R = E[\theta_S(\mathbf{x}_\Gamma)]$）。
- 物理含义：从红外相机出发的路径在透明流体中沿直线传播，遇到不透明固体表面时以概率 $\varepsilon$ 被吸收（路径终止于此表面，权重为 $\theta_S(\mathbf{x})$），以概率 $1-\varepsilon$ 发生漫反射或镜面反射后继续传播。
- 这是耦合路径的"外层循环"：路径从相机出发，经过若干次辐射弹射到达某个固-流界面——**路径至此进入耦合核心**。

### MC 算法步骤

对应 Bati Algorithm 2: Radiative sub-path：

```
Loop:
  [RT 查询] 从当前位置沿当前方向 trace_ray → 命中 or Miss
  if Miss → 返回环境辐射温度 θ_R,ambient（路径终止）
  if Hit → 到达固体表面 x：
    以概率 ε：被吸收 → **进入界面耦合模块 BND**
    以概率 1-ε：反射 → 按 pr(ω|ωi) 采样新方向，继续 Loop
EndLoop
```

### FSM 状态表（2 个状态，1 个 RT 挂起点）

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_RAD_TRACE_PENDING` [RT] | 辐射弹射射线已发出，等待最近命中结果 |
| `PATH_RAD_PROCESS_HIT` | 命中结果到达，判断 Miss / 吸收 / 反射 |

### 特征

- 全部 FSM 模块中最简单的：每次弹射仅需 1 条 RT 查询。
- 恢复后根据命中信息和 Russian Roulette 采样结果决定继续弹射（循环回 `RAD_TRACE_PENDING`）或进入 BND 模块。
- 递归求解器对应：`trace_radiative_path()` 中的 `find_next_fragment()` → `trace_ray()` 调用点。

### 模块出口

辐射路径被吸收 → 检查该表面温度是否已知。若已知（Dirichlet BC），路径以该温度终止；若未知，进入 **BND 模块**——耦合物理路径空间区别于纯渲染路径的关键分叉点。

---

## 2. 界面耦合模块（BND）——能量通量连续性驱动的多模态分叉

> **FSM 中最复杂的部分**。物理上，界面是三种热传递模式交汇的核心节点；算法上，界面处的分叉产生不同子路径类型；工程上，实际实现远超 Bati "didactic model" 的简化假设。

### 物理总纲

**Bati Eq. 13 / Eq. 5**——在固-流界面处，能量守恒要求：

$$\varphi_{\text{cond}}[\theta] = \varphi_{\text{conv}}[\theta] + \varphi_{\text{rad}}[L_\lambda]$$

耦合的物理根源：固体温度 $\theta_S$ 不能仅由传导求出（受辐射和对流影响），辐射场 $L_\lambda$ 也不能仅由辐射传输求出（发射率 $\varepsilon$ 依赖于 $\theta_S$）。

Bati 通过线性化和有限差分改写为概率期望形式（**Eq. 22-23**）：

$$\theta_S(\mathbf{x}) = P_{\text{cond}}\,\theta_S(\mathbf{x}-\delta\hat{n}) + P_{\text{conv}}\,\theta_F + P_{\text{rad}}\int_{2\pi}\theta_R\frac{\boldsymbol{\omega}\cdot\hat{n}}{\pi}d\boldsymbol{\omega}$$

三个概率由物理参数决定：
- $P_{\text{conv}} = h / (k/\delta + h + h_R)$
- $P_{\text{rad}} = h_R / (k/\delta + h + h_R)$
- $P_{\text{cond}} = 1 - P_{\text{conv}} - P_{\text{rad}}$

### 界面分叉分类

| 界面类型 | 物理方程 | 分叉模式 | FSM 子模块 |
|---------|---------|---------|-----------|
| 固-流（温度已知）| Dirichlet BC | 直接终止 | `BND_DISPATCH` → `DONE` |
| 固-流（温度未知，Picard-1） | Bati Eq. 23 + null-collision | 三模态 + null-collision 循环 | §2a |
| 固-流（温度未知，PicardN） | 高阶 Picard 迭代 | 递归子路径树 | §2b |
| 固-固 | Bati Eq. 26 | 二元传导注入 | §2c |
| 外部环境 | 净辐射通量 | 直射+漫射+遮挡 | §2d |

FSM 首先经 **`PATH_BND_DISPATCH`** 根据界面类型和温度可知性分流。递归求解器对应：`boundary_path()` in `sdis_heat_path_boundary_Xd.h`。

### 2-pre. 腔体归属查询（ENC）——界面参数查找的前置步骤

**为什么需要**：固-流或固-固界面的物理参数（$\varepsilon$, $k$, $h$）绑定于封闭腔体（enclosure）。多腔体场景中同一三角面片可能属于不同腔体边界，必须先确定归属才能查找正确物理参数。

**超出 Bati 简化模型**：Bati 假设简单腔体结构。实际工程场景（多层建筑、多孔介质）存在大量嵌套腔体，归属判定是非平凡几何问题。

**算法**：6-ray 轴对齐投票。沿 ±x, ±y, ±z 发射射线，根据命中面片腔体标注投票，歧义时用随机方向回退射线补充。

**后端化**：ENC 查询已实现后端化，挂起/恢复协议与 RT 完全同构。Wavefront 调度层面不可区分。

**FSM 状态**：

| 状态 | 含义 |
|------|------|
| `PATH_ENC_LOCATE_PENDING` [ENC] | BVH 最邻近基元定位（快速粗查） |
| `PATH_ENC_QUERY_EMIT` [ENC] | 6-ray 投票射线发射 |
| `PATH_ENC_QUERY_FB_EMIT` [ENC] | 投票歧义时随机回退射线 |

结果写入 `path_state.enc_id`，后续 BND 全部子模块依赖此值。

### 2a. 固-流界面三模态——Picard-1 null-collision（M5）

**物理方程**：**Bati Eq. 23**。线性化后界面温度表为三模态期望值。

**为何需要 null-collision**：

Eq. 23 中 $P_{\text{rad}}$ 含 $h_R = 4\sigma\varepsilon\theta_{\text{ref}}^3$，当 $\theta_{\text{ref}}$ 不准时三概率之和偏离 1。**Bati §6.3** 引入 null-collision：设上界 $\hat{h}_R$，以 $P_{\text{rad}}^{\hat{}} = \hat{h}_R/(k/\delta + h + \hat{h}_R)$ 采样辐射子路径，子路径返回 $\theta_R$ 后计算实际 $h_R$，以 $h_R/\hat{h}_R$ 概率接受（"true collision"），否则 null-collision 重新分叉。

**MC 算法步骤**（对应 Bati Algorithm 4，solid-fluid 分支）：

```
1. 确定腔体 → 读取 ε, k, h [依赖 ENC 查询]
2. [RT] δ-重注入采样：法线内侧射线确定 x-δn
3. 计算 P_cond, P_conv, P_rad(上界)
4. 随机选择模式：
   if 传导 → 进入 CND 模块
   if 对流 → 进入 CNV 模块
   if 辐射 → [RT] 采样辐射方向，发起辐射子路径
     → 子路径返回 θ_R → null-collision 判定
     → if true → 终止界面事件
     → if null → 回到步骤 4
```

**FSM 状态（5 个，含 2 个 RT + ENC 依赖）**：

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_BND_SF_REINJECT_SAMPLE` [RT] | δ-重注入候选位置射线 |
| `PATH_BND_SF_REINJECT_ENC` | 重注入位置腔体归属确认 |
| `PATH_BND_SF_PROB_DISPATCH` | 三概率计算、模式选择、分发 |
| `PATH_BND_SF_NULLCOLL_RAD_TRACE` [RT] | null-collision 辐射子路径射线 |
| `PATH_BND_SF_NULLCOLL_DECIDE` | accept/reject 判定 |

**工程复杂度**：
- 重注入射线有失败/退化可能 → 状态内 robust retry
- null-collision 循环通过 FSM 状态回边实现（非递归）
- 递归求解器对应：`solid_fluid_boundary_picard1_path()` + 内部 `sample_coupled_path()` 递归

### 2b. PicardN 高阶迭代——递归子路径（M8）

**物理背景**：高温差场景 $h_R$ 对 $\theta$ 强依赖 → Picard-1 不充分 → 多轮迭代。对应 **Bati §6.3.3** "Situation 3: nonlinear kinetics"。

**算法**：PicardN 在当前界面递归展开子路径树，每层 $\leq N_\text{branch}$（实现中 $\leq 6$）条完整 `sample_coupled_path` 子路径。递归深度可达 3 层。

**FSM 核心挑战——递归树压平**：
- 递归求解器：嵌套 `sample_coupled_path()` 递归
- FSM：`path_state` 中预分配固定深度递归栈（`path_sfn_data`，3 层，~3.7 KB/path），栈指针模拟展开/回退
- 子路径走 RAD/BND/CND/CNV 全流程，复用主 FSM 状态集

**FSM 状态（6 个 + 递归栈）**：

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_BND_SFN_PROB_DISPATCH` | PicardN 当前层模态概率计算与分发 |
| `PATH_BND_SFN_RAD_TRACE` [RT] | 递归子辐射路径弹射步 |
| `PATH_BND_SFN_RAD_DONE` | 子辐射路径完成，收集 $\theta_R$ |
| `PATH_BND_SFN_COMPUTE_Ti` | 子路径结果 → 本层温度估计 $T_i$ |
| `PATH_BND_SFN_COMPUTE_Ti_RESUME` | $T_i$ 计算恢复 |
| `PATH_BND_SFN_CHECK_PMIN_PMAX` | 收敛检查（$P_\min$, $P_\max$ 阈值） |

**设计决策**：递归栈（~3.7 KB）是 `path_state` 中最大组成部分。仅少数路径进入 PicardN → 分离为 SoA 冷块 `path_sfn_data`，常规推进不加载。

递归求解器对应：`solid_fluid_boundary_picardN_path()` + `COMPUTE_TEMPERATURE` 宏。

### 2c. 固-固界面导热耦合（M3）

**物理方程**：**Bati Eq. 10 + Eq. 25-26**。两相邻固体子部件传导通量连续：

$$k_1\frac{\theta_S(\mathbf{x}-\delta_1\hat{n})-\theta_S(\mathbf{x})}{\delta_1} = k_2\frac{\theta_S(\mathbf{x})-\theta_S(\mathbf{x}+\delta_2\hat{n})}{\delta_2}$$

概率期望：$\theta_S(\mathbf{x}) = P_{\text{cond},1}\,\theta_S(\mathbf{x}-\delta_1\hat{n}) + P_{\text{cond},2}\,\theta_S(\mathbf{x}+\delta_2\hat{n})$

其中 $P_{\text{cond},1} = (k_1/\delta_1)/(k_1/\delta_1 + k_2/\delta_2)$。

**MC 步骤**（Bati Algorithm 4，solid-solid 分支）：

```
1. 确定两侧固体 k1, k2, δ1, δ2
2. [RT×4] 双侧各 2 条射线（法线+偏移）→ 两侧重注入候选位置
3. [ENC] 腔体归属验证
4. 按 P_cond,1 vs P_cond,2 随机选侧 → 进入 CND 模块
```

**FSM 状态（3 个 + 4 条 RT + ENC）**：

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_BND_SS_REINJECT_SAMPLE` [RT] | 双侧 4 条重注入候选射线 |
| `PATH_BND_SS_REINJECT_ENC` | 候选位置腔体归属验证 |
| `PATH_BND_SS_REINJECT_DECIDE` | 概率选择注入侧 + dispatch CND |

**为何 4 条射线**：两侧各一组方向对确定法线内侧 δ-注入位置，每侧 2 条（法线+偏移修正）。δ-sphere 近似（Bati §4.2）的直接需求。

### 2d. 外部环境净辐射通量（M7）

**物理背景**：路径到达场景外部边界（与外部辐射环境相交），计算该表面从外部接收的净辐射通量（太阳直射 + 天空漫射 - 自发射）。对应 **Bati Eq. 7** 中 $\varphi_\text{rad}$ 的外部环境部分。

**MC 子流程**：

```
1. [RT] 直射阴影射线 → 太阳方向遮挡测试
2. [RT] 漫射弹射方向 → 天空半球采样
3. [RT] 漫射遮挡射线 → 确认未被遮挡
4. 汇总直射 + 漫射 → 净辐射通量
```

**FSM 状态（8 个，含 3 个 RT）**：

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_BND_EXT_CHECK` | 外部边界条件检查 |
| `PATH_BND_EXT_DIRECT_TRACE` [RT] | 太阳直射阴影射线 |
| `PATH_BND_EXT_DIRECT_RESULT` | 直射遮挡结果 |
| `PATH_BND_EXT_DIFFUSE_TRACE` [RT] | 漫射半球采样射线 |
| `PATH_BND_EXT_DIFFUSE_RESULT` | 漫射命中结果 |
| `PATH_BND_EXT_DIFFUSE_SHADOW_TRACE` [RT] | 漫射遮挡测试射线 |
| `PATH_BND_EXT_DIFFUSE_SHADOW_RESULT` | 遮挡测试结果 |
| `PATH_BND_EXT_FINALIZE` | 汇总净通量 |

---

## 3. 固体传导模块（CND）——扩散方程驱动的球面随机游走

### 物理方程

**Bati Eq. 8**（固体内纯扩散 $\partial\theta_S/\partial t = \alpha\Delta\theta_S$）与 **Eq. 24**（WoS MC 解 $\theta_S(\mathbf{x}-\delta\hat{n}) = E[\theta_S(\mathbf{x}_\text{WoS})]$）。

路径进入固体后不再沿直线传播，而是球面随机游走模拟热扩散。每步跳至球面随机位置，到达固体边界时回到 BND 模块。

实现中存在两种方案：

### 3a. Walk-on-δ-Sphere（M4）——固定半径近似

**来源**：**Bati §4.2**。使用固定半径 δ 替代最近面距离。Bati 原文动机是统一所有查询为 RT（无需 CP），便于 RT Core 加速。在本文实现中 CP 已后端化，δ-Sphere 优势转为固定步长的数值稳定性和实现简洁性。

**MC 步骤**：

```
Loop:
  if 温度已知 → 终止
  1. 采样各向同性方向 Ω
  2. [RT×2] ±Ω 双向射线，得 s+, s-
  3. δ̃ = min(δ, s+, s-) → 跳至 x + δ̃·Ω
  4. [ENC] 新位置腔体归属验证
  5. if 到达边界 → 进入 BND
  6. 时间回溯 → if 到达初始时刻 → 终止
  else → 继续
EndLoop
```

**FSM 状态（4 个 + 2 RT + ENC）**：

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_CND_DS_CHECK_TEMP` | 温度检查 |
| `PATH_CND_DS_STEP_TRACE` [RT] | δ-sphere 双向射线 |
| `PATH_CND_DS_STEP_PROCESS` | 射线结果 → δ̃ 计算 |
| `PATH_CND_DS_STEP_ENC_VERIFY` | 腔体归属验证 |
| `PATH_CND_DS_STEP_ADVANCE` | 时间回溯 + 循环判定 |

### 3b. Walk-on-Sphere（M9）——最近面投影精确方案

**来源**：**Muller 1956** 经典 WoS。CP 查询确定最近面距离作为球半径，远离边界时收敛更优（对数收敛）。

**MC 步骤**：

```
Loop:
  if 温度已知 → 终止
  1. [CP] closest_point → d_wos
  2. if d_wos ≤ ε_shell(δ) → snap 到边界 → 进入 BND
  3. else → 球面采样，跳至 x + d_wos·dir
  4. [CP] 新位置最近面验证
  5. if 验证失败 → [RT] 回退射线
  6. 时间回溯
  else → 继续
EndLoop
```

**FSM 状态（8 个，含 2 CP + 1 RT）**：

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_CND_WOS_CHECK_TEMP` | 温度检查 |
| `PATH_CND_WOS_CLOSEST` [CP] | 最近面投影（球半径） |
| `PATH_CND_WOS_SPHERE_STEP` | 球面采样 + 跳跃 |
| `PATH_CND_WOS_DIFFUSION_CHECK` [CP] | 新位置最近面验证 |
| `PATH_CND_WOS_DIFFUSION_RESULT` | 验证结果处理 |
| `PATH_CND_WOS_FALLBACK_TRACE` [RT] | 验证失败回退射线 |
| `PATH_CND_WOS_FALLBACK_RESULT` | 回退结果处理 |
| `PATH_CND_WOS_TIME_TRAVEL` | 瞬态时间回溯（Bati Eq. 34-35） |

### 两方案关系

WoS 收敛更快（对数），依赖 CP 确定精确球半径。δ-Sphere 步长固定、实现简洁。**本文实现中 CP 已后端化**，WoS 不再有"CP 无法 GPU 加速"的架构限制，选择回归纯数值效率考量。两者并存，由场景参数决定。

### 3c. 瞬态时间回溯

**Bati Eq. 34-35**：$\theta_S(\mathbf{x},t) = E[H_{I,\text{cond}}\,\theta_S(\mathbf{x},t_I) + (1-H_{I,\text{cond}})\,\theta_S(\mathbf{x}+\tilde\delta\vec\Omega, t-T_{b,\text{cond}})]$

- $T_{b,\text{cond}}$：指数分布向后时间跳跃，$\tau_\text{cond} = \rho c\tilde\delta^2 / 6k$
- 到达初始时刻 $t_I$ → 已知初始温度终止
- 嵌入 3a/3b 每步循环尾部，不产生额外挂起点

---

## 4. 流体对流模块（CNV）——Newton 冷却边界采样

### 物理方程

**Bati Eq. 11**（$\varphi_\text{conv} = h(\theta_S - \theta_F)$）+ **Eq. 27-28**（$\theta_F = E[\theta_S(\mathbf{X}_S)]$，$\mathbf{X}_S \sim h(\mathbf{x})/\int_S h\,dx$）。

完美混合流体假设 → $\theta_F$ 等于腔体边界固体温度的 $h$-加权平均 → MC：按 $h(\mathbf{x})$ 采样位置 $\mathbf{X}_S$，读取 $\theta_S$。若未知，回到 BND。

**瞬态对流**（**Bati Eq. 29-31**）：指数采样 $T_{b,\text{conv}}$（$\tau_\text{conv} = \rho c V / hS$），到达初始时刻终止。

### MC 步骤

对应 Bati Algorithm 1：

```
1. 确定腔体 → 读取 ρc, V, S, h
2. [RT] 腔体几何边界探针射线
3. 采样时间跳跃 → if 到达初始时刻 → 返回 θ_F(t_I)
4. 按 h(x) 采样 X_S → 返回 θ_S(X_S)
   → if 未知 → 进入 BND
```

### FSM 状态（4 个，1 RT + ENC 依赖）

| FSM 状态 | 物理含义 |
|----------|---------|
| `PATH_CNV_INIT` | 腔体参数初始化 |
| `PATH_CNV_STARTUP_TRACE` [RT] | 腔体几何探针射线 |
| `PATH_CNV_STARTUP_RESULT` | 探针结果、腔体确认 |
| `PATH_CNV_SAMPLE_LOOP` | 时间回溯 + 边界采样 + $\theta_S$ 获取 |

---

## 5. 完整 FSM 综合

### 路径生命周期

```
INIT → RAD ←→ BND
                ├→ CND (δ-sphere 或 WoS) → 游走至边界 → BND
                ├→ CNV → 获取 θ_F → BND（若 θ_S 未知）
                ├→ RAD（辐射再发射）
                └→ DONE（已知温度 / 初始时刻 / 外部环境）
```

### 复杂度来源

- 59 细粒度状态 = 3 模式 × 多界面拓扑 × 非线性迭代 × 瞬态 × 几何复杂度
- 路径长度：最短 2 步（RAD → 已知温度 → DONE），最长数千步（深 PicardN + 长传导 + 多次界面往返）
- 路径长度是场景物理参数的函数（导热率、发射率、腔体尺度），非程序复杂度产物

### 各模块状态数与查询需求汇总

| 模块 | 物理方程来源 | 状态数 | RT | ENC | CP |
|------|-------------|--------|-----|-----|-----|
| RAD | Bati Eq. 6/19 | 2 | 1 | 0 | 0 |
| BND-ENC | 工程需求 | 3 | 0 | 3 | 0 |
| BND-SF (Picard-1) | Bati Eq. 23, §6.3 | 5 | 2 | dep | 0 |
| BND-SFN (PicardN) | Bati §6.3.3 | 6 | 1+ | dep | 0 |
| BND-SS | Bati Eq. 26 | 3 | 4 | dep | 0 |
| BND-EXT | Bati Eq. 7 | 8 | 3 | 0 | 0 |
| CND-DS (δ-Sphere) | Bati §4.2 | 5 | 2/步 | dep | 0 |
| CND-WoS | Muller 1956 | 8 | 1 | 0 | 2 |
| CNV | Bati Eq. 28 | 4 | 1 | dep | 0 |
| **合计** | — | **59** | **14 点** | **3 点** | **2 点** |

> 全部 19 挂起点在本文实现中均为**同构挂起点**（RT/ENC/CP 均已后端化），Wavefront 主循环统一调度。

---

## 6. 挂起/恢复协议

- **挂起**三步：写查询参数 → 置 PENDING 枚举 → 推进函数返回
- **恢复**两步：结果写回 `path_state` → 置 RESULT 枚举 → 下轮 `advance_paths()` 分发
- 设备无关性：不假定后端类型。三类查询均已后端化为同构挂起点，无需按类型分流

## 7. 等价性保证

- **状态转移等价**：每个 `sdis_wf_step_*` 与递归求解器对应代码段逐行对应
- **状态覆盖等价**：`path_state` 字段集覆盖递归求解器各挂起点调用栈局部变量（无遗漏/无冗余）
- **随机数**：递归求解器与 Wavefront RNG 消耗顺序不同，但 per-path CBRNG 独立 → 估计量无偏

---
*归档日期：2026-03-20*
*来源：Bati et al. 2023 (TOG/SIGGRAPH), Tregan et al. 2023 (PLOS ONE), stardis-cpu / stardis-oxs3d-merge-phase 源码分析*
