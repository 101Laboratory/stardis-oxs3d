# Bati 方程 → FSM 状态映射表

> 速查表：Bati et al. 2023 关键方程/章节 → 源码 FSM 状态组 → 查询类型

## 方程级映射

| Bati 方程/章节 | 物理含义 | MC 算法 | FSM 状态组 | 源码里程碑 | 查询类型 |
|---------------|---------|---------|-----------|-----------|---------|
| Eq. 6 / 19 | $\theta_R = E[\theta_S(\mathbf{x}_\Gamma)]$，辐射渲染方程 | Russian Roulette 反射链 | `PATH_RAD_*` | — | RT×1/弹射 |
| Eq. 13 / 23 | $\varphi_\text{cond} = \varphi_\text{conv} + \varphi_\text{rad}$，固-流三模态耦合 | $P_\text{cond}/P_\text{conv}/P_\text{rad}$ 概率选择 + null-collision | `PATH_BND_SF_*` | M5 | RT×2, ENC |
| §6.3 | Null-collision 隐式 $h_R$ 处理 | Accept/reject 辐射子路径 | `PATH_BND_SF_NULLCOLL_*` | M5 | RT×1 |
| §6.3.3 | PicardN 非线性迭代 | 递归子路径树（$\leq 6$ 分支/层） | `PATH_BND_SFN_*` | M8 | RT + 递归栈 |
| Eq. 10 / 25-26 | $k_1 \nabla\theta_1 = k_2 \nabla\theta_2$，固-固传导通量连续 | 二元概率选择注入侧 | `PATH_BND_SS_*` | M3 | RT×4, ENC |
| Eq. 8 / 24 | $\partial\theta_S/\partial t = \alpha\Delta\theta_S$，固体扩散 | Walk-on-δ-Sphere | `PATH_CND_DS_*` | M4 | RT×2/步 |
| Eq. 24 (Muller 1956) | $\theta_S(\mathbf{x}) = E[\theta_S(\mathbf{x}_\text{WoS})]$ | Walk-on-Sphere（最近面投影） | `PATH_CND_WOS_*` | M9 | CP×2, RT×1 |
| Eq. 34-35 | 瞬态指数时间回溯 $T_{b,\text{cond}}$ | 指数采样 + 初始时刻终止 | 嵌入 CND 步进函数内 | M4/M9 | 无额外查询 |
| Eq. 11 / 27-28 | $\varphi_\text{conv} = h(\theta_S - \theta_F)$，Newton 冷却 | 边界 $h$-加权采样 $\theta_F$ | `PATH_CNV_*` | M6 | RT×1, ENC |
| Eq. 29-31 | 瞬态对流时间回溯 | 指数采样 $T_{b,\text{conv}}$ | 嵌入 CNV 步进函数内 | M6 | 无额外查询 |
| — (工程需求) | 外部环境净辐射通量 | 直射+漫射+遮挡 3 条射线 | `PATH_BND_EXT_*` | M7 | RT×3 |
| — (工程需求) | 封闭腔体归属判定 | 6-ray 轴对齐投票 + 回退 | `PATH_ENC_*` | M10/M1-v2 | ENC (已后端化) |

## 源码里程碑索引

| 里程碑 | 物理模块 | 关键源文件（Wavefront） | DFS 对应文件 |
|--------|---------|----------------------|-------------|
| M3 | 固-固界面 | `sdis_wf_step_bnd_ss.c` | `sdis_heat_path_boundary_Xd.h` |
| M4 | δ-Sphere 传导 | `sdis_wf_step_cnd.c` | `sdis_heat_path_conductive_wos_Xd.h` |
| M5 | 固-流界面 Picard-1 | `sdis_wf_step_bnd_sf.c` | `sdis_heat_path_boundary_Xd.h` |
| M6 | 对流 | `sdis_wf_step_cnv.c` | `sdis_heat_path_boundary_Xd.h` |
| M7 | 外部净通量 | `sdis_wf_step_bnd_ext.c`（推测） | `sdis_heat_path_boundary_Xd.h` |
| M8 | PicardN | `sdis_wf_step_bnd_sf.c`（sfn 段） | `sdis_heat_path_boundary_Xd.h` |
| M9 | WoS 传导 | `sdis_wf_step_cnd.c`（wos 段） | `sdis_heat_path_conductive_wos_Xd.h` |
| M10 | 腔体归属 | `sdis_wf_step_enc.c` | 内联于各 boundary 函数 |

## 三类查询汇总

| 查询类型 | 挂起点数 | 物理必要性 | 后端化状态 |
|---------|---------|-----------|-----------|
| **RT** (Ray Trace) | 14 | 辐射弹射、各类重注入、传导步长、对流初始化、外部净通量 | 已后端化（GPU RT Core） |
| **ENC** (Enclosure) | 3 | 多腔体参数查找前置步骤 | 已后端化（同构挂起点） |
| **CP** (Closest Point) | 2 | WoS 传导球半径确定 | 已后端化（同构挂起点） |
| **合计** | **19** | — | 全部 19 点同构 |

> **关键工程结论**：在 `stardis-oxs3d-merge-phase` 实现中，ENC 与 CP 查询均已后端化，三类查询的挂起点在 Wavefront 调度层面完全同构——主循环无需按查询类型分流，统一收集/提交/恢复。

---
*来源：Bati et al. 2023 (TOG/SIGGRAPH)，Tregan et al. 2023 (PLOS ONE)，stardis-cpu / stardis-oxs3d-merge-phase 源码分析*
