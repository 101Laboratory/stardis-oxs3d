# Wavefront FSM 状态机完整设计文档

**源文件**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_wf_types.h`  
**步进实现**: `sdis_wf_steps_core.c`, `sdis_wf_steps_bnd_ss.c`, `sdis_wf_steps_bnd_sf.c`, `sdis_wf_steps_bnd_sfn.c`, `sdis_wf_steps_bnd_ext.c`, `sdis_wf_steps_cnd.c`, `sdis_wf_steps_cnv.c`, `sdis_wf_steps_enc.c`  
**版本**: Phase B-4 (全部 Milestones M1–M10)  
**总状态数**: 59（`PATH_PHASE_COUNT = 60`，含哨兵值）  
**更新日期**: 2026-03-19

---

## 目录

1. [总体结构概述](#1-总体结构概述)
2. [状态分类标记说明](#2-状态分类标记说明)
3. [路径生命周期状态](#3-路径生命周期状态)
4. [RAD — 辐射传播](#4-rad--辐射传播)
5. [耦合路径入口状态（Legacy B2/B3）](#5-耦合路径入口状态legacy-b2b3)
6. [BND — 边界事件状态簇（核心路由中心）](#6-bnd--边界事件状态簇核心路由中心)
   - 6.1 [BND 总体调度](#61-bnd-总体调度)
   - 6.2 [BND-SS — 固固界面热接触](#62-bnd-ss--固固界面热接触m3)
   - 6.3 [BND-SF — 固流界面 Picard1](#63-bnd-sf--固流界面-picard1m5)
   - 6.4 [BND-SFN — 固流界面 PicardN](#64-bnd-sfn--固流界面-picardnm8)
   - 6.5 [BND-EXT — 外部净热流子过程](#65-bnd-ext--外部净热流子过程m7)
7. [CND — 传导路径](#7-cnd--传导路径)
   - 7.1 [CND-DS — Delta-Sphere 游走](#71-cnd-ds--delta-sphere-游走m4)
   - 7.2 [CND-WoS — Walk-on-Spheres 游走](#72-cnd-wos--walk-on-spheresm9)
   - 7.3 [CND 其他](#73-cnd-其他)
8. [CNV — 对流路径](#8-cnv--对流路径m6)
9. [ENC — 腔体归属查询](#9-enc--腔体归属查询)
10. [射线桶分类（Ray Bucket）](#10-射线桶分类ray-bucket)
11. [查询类型汇总](#11-查询类型汇总)
12. [状态完整枚举表](#12-状态完整枚举表)
13. [全局状态转移图（文字描述）](#13-全局状态转移图文字描述)

---

## 1. 总体结构概述

Wavefront FSM 的核心思想是将原 DFS 递归求解器（`stardis-cpu`）中所有几何查询调用点（`trace_ray`、`closest_point`、`enc_locate`）显式化为状态机的挂起/恢复节点。每条蒙特卡洛路径在任意时刻由一个 `path_phase` 枚举值唯一标识其执行位置。

### 宏观结构

```
                          ┌─────────┐
                          │PATH_INIT│
                          └────┬────┘
                               │ setup_radiative_trace_ray()
                               ▼
                    ┌──────────────────────┐
              ┌─────│ PATH_RAD_TRACE_PENDING │◄──────────────────────┐
              │     └──────────┬───────────┘                        │
              │                │ 命中界面，被吸收                      │
              │                ▼                                    │
              │     ┌──────────────────────┐                        │
              │     │ PATH_COUPLED_BOUNDARY │                        │
              │     └──────────┬───────────┘                        │
              │                │                                    │
              │                ▼                                    │
              │  ┌──────────────────────────┐                       │
              │  │★ PATH_BND_DISPATCH      │                       │
              │  │  (中心路由: SS/SF/SFN)    │                       │
              │  └──┬─────┬─────┬──────────┘                       │
              │     │     │     │                                   │
              │     ▼     ▼     ▼                                   │
              │  BND-SS BND-SF BND-SFN ──→ BND-EXT (外部热流子过程)  │
              │     │     │     │                                   │
              │     └──┬──┘─────┘                                   │
              │        │ 概率分叉输出:                                │
              │        ├─→ PATH_COUPLED_CONDUCTIVE ─→ CND-DS / CND-WoS
              │        ├─→ PATH_COUPLED_CONVECTIVE ─→ CNV-*          │
              │        └─→ PATH_COUPLED_RADIATIVE ──────────────────┘
              │
              │         反射（未被吸收）
              └─────────────────────────────────────────────────────┘
                                    │
                               PATH_DONE
```

**核心设计原则**：BND（边界事件）是串联所有物理过程的**中心调度枢纽**。辐射传播（RAD）命中固体界面后进入 BND，BND 根据界面类型分派至三条子路径（SS/SF/SFN），各子路径执行完毕后输出 `PATH_COUPLED_CONDUCTIVE`、`PATH_COUPLED_CONVECTIVE` 或 `PATH_COUPLED_RADIATIVE`，分别接入传导（CND）、对流（CNV）或再次回到辐射传播（RAD）。

---

## 2. 状态分类标记说明

每个状态带有以下标记之一：

| 标记 | 含义 | 调度行为 |
|------|------|----------|
| **[R]** | **射线等待（Ray-Pending）** | 路径挂起，等待批量射线追踪结果；由 `advance_one_step_with_ray()` 处理 |
| **[C]** | **纯计算（Compute-Only）** | 不需要外部查询结果，由 `advance_one_step_no_ray()` 处理；可在同一轮级联推进 |
| **[CP]** | **最近点等待（Closest-Point Pending）** | 路径挂起，等待批量 `closest_point` 查询结果 |
| **[ENC]** | **腔体归属等待（Enc-Locate Pending）** | 路径挂起，等待批量 `enc_locate` 查询结果 |
| **[Future]** | **预留状态** | 已在枚举中定义但尚未激活的步进函数，目前触发 FATAL 断言 |

**调度决策函数**:
- `path_phase_is_ray_pending(ph)` → 判断是否为 [R] 状态
- `path_phase_is_cp_pending(ph)` → 判断是否为 [CP] 状态 (`PATH_CND_WOS_CLOSEST` / `PATH_CND_WOS_DIFFUSION_CHECK`)
- `path_phase_is_enc_locate_pending(ph)` → 判断是否为 [ENC] 状态 (`PATH_ENC_LOCATE_PENDING`)

---

## 3. 路径生命周期状态

| # | 枚举值 | 标记 | 说明 | 出转移 |
|---|--------|------|------|--------|
| 0 | `PATH_INIT` | [C] | 路径未启动，由主循环首次推进 | → `PATH_RAD_TRACE_PENDING`（发射首条辐射射线） |
| 57 | `PATH_DONE` | 终止 | 路径已完成，温度贡献已写入 `T.value` | → `PATH_HARVESTED`（harvest 阶段回收后） |
| 58 | `PATH_HARVESTED` | 终止 | 已被 harvest 阶段回收，可回收槽位 | 不转移 |
| 59 | `PATH_ERROR` | 终止 | 异常终止 | 不转移 |

---

## 4. RAD — 辐射传播

辐射传播是路径的"主干"。路径从初始化或从界面反射回辐射后，持续进行射线追踪直至被界面吸收或逃逸至环境。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 1 | `PATH_RAD_TRACE_PENDING` | **[R]** | `step_radiative_trace()` | 射线已提交，等待命中结果 |
| 2 | `PATH_RAD_PROCESS_HIT` | [C, Future] | — | BRDF/发射处理（预留，目前 inline 在 `step_radiative_trace` 内） |

### 转移逻辑

`step_radiative_trace()` 收到命中结果后：

1. **未命中（miss）**：从辐射环境读取温度 → `T.done = 1` → **PATH_DONE**
2. **命中界面**：
   - 界面有效性检查失败 → 调整位置重试 → **PATH_RAD_TRACE_PENDING**（至多 9 次）
   - 吸收测试 `rand < emissivity`：被吸收 → **PATH_COUPLED_BOUNDARY**
   - 未被吸收：BRDF 反射采样新方向 → **PATH_RAD_TRACE_PENDING**

### 射线参数
- 桶类型: `RAY_BUCKET_RADIATIVE`
- 射线数: 1
- 范围: `[0, FLT_MAX]`
- 需要自交过滤 + 腔体匹配

---

## 5. 耦合路径入口状态（Legacy B2/B3）

这些状态是早期 B2/B3 阶段的粗粒度入口点。在 B4 完成后，它们作为**委托节点**存在——收到控制后立即转发至对应的 B4 细粒度子状态机。

| # | 枚举值 | 标记 | 步进函数 | 转移目标 |
|---|--------|------|----------|----------|
| 3 | `PATH_COUPLED_BOUNDARY` | [C] | `step_boundary()` | → `PATH_BND_DISPATCH`（委托至 B4 边界调度） |
| 4 | `PATH_COUPLED_BOUNDARY_REINJECT` | **[R]** | (legacy) | 遗留固固再射入（B2/B3 代码路径） |
| 5 | `PATH_COUPLED_CONDUCTIVE` | [C] | `step_conductive()` | → `PATH_CND_DS_CHECK_TEMP` (delta-sphere) 或 `PATH_CND_WOS_CHECK_TEMP` (WoS) 或同步 fallback |
| 6 | `PATH_COUPLED_COND_DS_PENDING` | **[R]** | `step_conductive_ds_process()` | 遗留 delta-sphere 2 射线等待（B2/B3 代码路径） |
| 7 | `PATH_COUPLED_CONVECTIVE` | [C] | `step_convective()` | → `PATH_CNV_INIT`（委托至 B4 对流状态机） |
| 8 | `PATH_COUPLED_RADIATIVE` | [C] | `step_coupled_radiative_begin()` | 余弦加权半球采样新方向 → `PATH_RAD_TRACE_PENDING` |

### `step_boundary()` 详细逻辑

`step_boundary()` 管理级联求解（cascade）的分支计数器 `nbranchings`：
- 首次进入时初始化 `nbranchings = 0`
- 检查是否超过 `max_branchings` 限制
- 无条件转至 `PATH_BND_DISPATCH`，委托 B4 细粒度状态机处理

### `step_conductive()` 详细逻辑

根据 `ctx.diff_algo` 选择传导算法：
- `SDIS_DIFFUSION_WOS` → 发射 6 射线腔体查询 `step_enc_query_emit()` → 返回至 `PATH_CND_WOS_CHECK_TEMP`
- `SDIS_DIFFUSION_DELTA_SPHERE` → 发射 6 射线腔体查询 → 返回至 `PATH_CND_DS_CHECK_TEMP`
- 其他 → 同步 fallback，完成后根据 `T.func` 转至对应入口

---

## 6. BND — 边界事件状态簇（核心路由中心）

BND 状态簇是整个 FSM 的**中心调度枢纽**，共 24 个状态。辐射路径命中固体界面被吸收后进入 `PATH_COUPLED_BOUNDARY` → `PATH_BND_DISPATCH`，由此根据界面类型分发至三条子路径。

### 6.1 BND 总体调度

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 9 | `PATH_BND_DISPATCH` | [C] | `step_bnd_dispatch()` | 界面类型三路分派 |
| 10 | `PATH_BND_POST_ROBIN_CHECK` | [C] | `step_bnd_post_robin_check()` | Robin 边界条件后置检查 |

#### `step_bnd_dispatch()` 分派逻辑

```
读取命中基元的界面信息 (interface)
├─ Dirichlet 边界 → 读取温度, T.done=1 → PATH_DONE
├─ Solid/Solid (前后均为固体) → PATH_BND_SS_REINJECT_SAMPLE (M3)
├─ Solid/Fluid picard1 (nbranchings==0 或 Picard迭代次数=1)
│   → PATH_BND_SF_REINJECT_SAMPLE (M5)
├─ Solid/Fluid picardN (nbranchings>0 且 Picard迭代次数>1)
│   → PATH_BND_SFN_PROB_DISPATCH (M8，通过 SF 再射入共享)
└─ Robin 边界 → 计算后置条件 → PATH_BND_POST_ROBIN_CHECK
```

`PATH_BND_POST_ROBIN_CHECK` 完成后根据 Robin 修正结果转至 `PATH_COUPLED_CONDUCTIVE`、`PATH_COUPLED_CONVECTIVE`、`PATH_COUPLED_RADIATIVE` 或 `PATH_DONE`。

---

### 6.2 BND-SS — 固固界面热接触（M3）

**物理含义**：路径到达两个固体介质的界面（solid/solid），需要根据热接触电阻进行概率分叉，决定路径转入前侧固体还是后侧固体继续传导。

**射线需求**：4 条再射入采样射线（前侧 2 条 + 后侧 2 条，采样方向围绕法线）。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 11 | `PATH_BND_SS_REINJECT_SAMPLE` | **[R]** | `step_bnd_ss_reinject_sample()` / `step_bnd_ss_reinject_process()` | 提交 4 条再射入射线 |
| 12 | `PATH_BND_SS_REINJECT_ENC` | [C] | `step_bnd_ss_reinject_enc_result()` | ENC 查询结果处理 |
| 13 | `PATH_BND_SS_REINJECT_DECIDE` | [C] | `step_bnd_ss_reinject_decide()` | 概率判决，转入传导 |

#### 状态转移

```
PATH_BND_DISPATCH
  │ (SS界面)
  ▼
PATH_BND_SS_REINJECT_SAMPLE [R: 4射线]
  │
  ├─ 命中有效: 直接拿到 enc_id
  │   ▼
  │ PATH_BND_SS_REINJECT_DECIDE [C]
  │   │
  │   ├─ 概率选择前侧 → solid_reinjection → PATH_COUPLED_CONDUCTIVE
  │   └─ 概率选择后侧 → solid_reinjection → PATH_COUPLED_CONDUCTIVE
  │
  ├─ 命中但需 ENC 验证:
  │   ▼
  │ PATH_BND_SS_REINJECT_ENC (→ PATH_ENC_QUERY_EMIT, 返回至 DECIDE)
  │
  └─ 重试: retry_count < limit
      → 重新采样方向 → PATH_BND_SS_REINJECT_SAMPLE
```

#### 射线参数
- 桶类型: `RAY_BUCKET_STEP_PAIR`
- 射线数: 4（`ray_count_ext = 4`，ray_req 只装 2 条，另外 2 条由 collect 阶段从 `bnd_ss.dir_bck[]` 读取）
- 范围: `[0, FLT_MAX]`

#### 特殊路径：Multi-Enclosure 快速路径

当前后两侧均为 `MEDIUM_ID_MULTI` 时，跳过射线追踪，用法线方向作为再射入方向，直接进入 `PATH_BND_SS_REINJECT_DECIDE`。

---

### 6.3 BND-SF — 固流界面 Picard1（M5）

**物理含义**：路径到达固体-流体界面，需要在固体侧进行再射入采样，然后按概率分叉至传导（p_cond）、对流（p_conv）或辐射（p_radi）。辐射分支使用 null-collision 机制。

**射线需求**：2 条再射入采样射线（采样方向 + 镜面反射方向）。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 14 | `PATH_BND_SF_REINJECT_SAMPLE` | **[R]** | `step_bnd_sf_reinject_sample()` / `step_bnd_sf_reinject_process()` | 提交 2 条再射入射线 |
| 15 | `PATH_BND_SF_REINJECT_ENC` | [C] | `step_bnd_sf_reinject_enc_result()` | ENC 查询结果处理 |
| 16 | `PATH_BND_SF_PROB_DISPATCH` | [C] | `step_bnd_sf_prob_dispatch()` | 概率分叉 |
| 17 | `PATH_BND_SF_NULLCOLL_RAD_TRACE` | **[R]** | `step_bnd_sf_nullcoll_rad_trace()` | Null-collision 辐射子路径射线 |
| 18 | `PATH_BND_SF_NULLCOLL_DECIDE` | [C] | `step_bnd_sf_nullcoll_decide()` | 接受/拒绝辐射路径 |

#### 状态转移

```
PATH_BND_DISPATCH
  │ (SF界面, picard1)
  ▼
PATH_BND_SF_REINJECT_SAMPLE [R: 2射线]
  │
  ├─ 命中有效 → 直接获取 enc_id
  │
  ├─ 需要 ENC 验证:
  │   ▼
  │ PATH_BND_SF_REINJECT_ENC (→ ENC_QUERY_EMIT, 返回至 SF_PROB_DISPATCH)
  │
  └─ 重试 → PATH_BND_SF_REINJECT_SAMPLE
      ▼
PATH_BND_SF_PROB_DISPATCH [C]
  │ 计算 p_conv, p_cond, p_radi
  │
  ├─ p_conv 胜出 → PATH_COUPLED_CONVECTIVE
  ├─ p_cond 胜出 → PATH_COUPLED_CONDUCTIVE
  ├─ p_radi 胜出:
  │   │ 需要外部热流? → PATH_BND_EXT_CHECK (M7 子过程)
  │   │ 否 → 发射 null-collision 辐射射线:
  │   ▼
  │ PATH_BND_SF_NULLCOLL_RAD_TRACE [R: 1射线]
  │   │
  │   ▼
  │ PATH_BND_SF_NULLCOLL_DECIDE [C]
  │   ├─ 接受: 返回辐射子路径温度 → PATH_COUPLED_BOUNDARY (null-collision 循环)
  │   └─ 拒绝: 继续 null-collision 循环 → PATH_BND_SF_PROB_DISPATCH
  │
  └─ Dirichlet 温度已知 → PATH_DONE
```

#### 射线参数
- 再射入: `RAY_BUCKET_STEP_PAIR`, 2 射线, `[0, FLT_MAX]`
- Null-collision 辐射: `RAY_BUCKET_RADIATIVE`, 1 射线, `[0, FLT_MAX]`

#### Null-Collision 循环

SF 的辐射分支使用 null-collision 估计器：在界面处发射辐射射线，追踪一步，若命中另一界面被吸收则进入该界面的 `boundary_path`（递归），否则拒绝并重新采样。循环通过 `PATH_BND_SF_PROB_DISPATCH` → `PATH_BND_SF_NULLCOLL_RAD_TRACE` → `PATH_BND_SF_NULLCOLL_DECIDE` → 回到 `PATH_BND_SF_PROB_DISPATCH` 实现。

---

### 6.4 BND-SFN — 固流界面 PicardN（M8）

**物理含义**：高阶 Picard 迭代。与 Picard1 的区别在于：每轮 null-collision 迭代需要评估多个温度样本（`COMPUTE_TEMPERATURE` 递归调用），形成子路径树。需要维护显式的 Picard 递归栈。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 19 | `PATH_BND_SFN_PROB_DISPATCH` | [C] | `step_bnd_sfn_prob_dispatch()` | 概率分叉 + Picard 栈管理 |
| 20 | `PATH_BND_SFN_RAD_TRACE` | **[R]** | `step_bnd_sfn_rad_trace()` | 辐射子路径射线 |
| 21 | `PATH_BND_SFN_RAD_DONE` | [C] | `step_bnd_sfn_rad_done()` | 辐射子路径完成 |
| 22 | `PATH_BND_SFN_COMPUTE_Ti` | [C] | `step_bnd_sfn_compute_Ti()` | 压栈：评估第 i 个温度样本 |
| 23 | `PATH_BND_SFN_COMPUTE_Ti_RESUME` | [C] | `step_bnd_sfn_compute_Ti_resume()` | 弹栈：子路径返回 |
| 24 | `PATH_BND_SFN_CHECK_PMIN_PMAX` | [C] | `step_bnd_sfn_check_pmin_pmax()` | 早终止判断（概率上下界） |

#### 状态转移

```
PATH_BND_DISPATCH
  │ (SF界面, picardN)
  │ 共享 SF 再射入 (PATH_BND_SF_REINJECT_SAMPLE)
  ▼
PATH_BND_SFN_PROB_DISPATCH [C]
  │ 首次: 计算 h_hat, h_conv, h_cond, h_radi
  │ 后续: null-collision 分叉
  │
  ├─ p_conv → PATH_COUPLED_CONVECTIVE
  ├─ p_cond → PATH_COUPLED_CONDUCTIVE
  ├─ p_radi:
  │   ▼
  │ PATH_BND_SFN_RAD_TRACE [R: 1射线]
  │   ▼
  │ PATH_BND_SFN_RAD_DONE [C]
  │   │ 保存 rwalk_s, T_s
  │   │ 初始检查 h_radi_min
  │   ▼
  │ PATH_BND_SFN_COMPUTE_Ti [C]
  │   │
  │   ├─ T.done (温度已知) → 直接使用值 → CHECK_PMIN_PMAX
  │   └─ T 未知 → 压 Picard 栈, 启动子路径:
  │       恢复 rwalk, T 至子路径初始状态
  │       → PATH_COUPLED_BOUNDARY (递归入口)
  │       子路径完成后:
  │         ▼
  │       PATH_BND_SFN_COMPUTE_Ti_RESUME [C]
  │         │ 弹栈, 累加温度贡献
  │         │ i++ → 还有更多样本?
  │         ├─ 是 → PATH_BND_SFN_COMPUTE_Ti
  │         └─ 否 → PATH_BND_SFN_CHECK_PMIN_PMAX
  │
  │ PATH_BND_SFN_CHECK_PMIN_PMAX [C]
  │   ├─ 接受: T.value 累加 → PATH_COUPLED_BOUNDARY (外层循环)
  │   ├─ 拒绝: 继续循环 → PATH_BND_SFN_PROB_DISPATCH
  │   └─ 递归深度超限: 同步 fallback
  │
  └─ Dirichlet → PATH_DONE
```

#### Picard 递归栈

`path_sfn_data` 维护固定深度 `MAX_PICARD_DEPTH = 3` 的显式栈：
- 每层保存 `rwalk`, `T`, `hvtx`, `bnd_sf` 快照
- 子路径通过修改 `coupled_nbranchings` 进入 `PATH_COUPLED_BOUNDARY`
- 子路径完成后（`T.done`）触发 `PATH_BND_SFN_COMPUTE_Ti_RESUME` 弹栈

---

### 6.5 BND-EXT — 外部净热流子过程（M7）

**物理含义**：当界面存在外部辐射源（如太阳照射）时，需额外计算外部净热流贡献。此子过程在 SF/SFN 的 `PROB_DISPATCH` 发现需要外部热流时调用。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 25 | `PATH_BND_EXT_CHECK` | [C] | `step_bnd_ext_check()` | 判断是否需要外部热流 |
| 26 | `PATH_BND_EXT_DIRECT_TRACE` | **[R]** | `step_bnd_ext_direct_result()` | 直接照射阴影射线 |
| 27 | `PATH_BND_EXT_DIRECT_RESULT` | [C] | (inline) | 直接照射结果处理 |
| 28 | `PATH_BND_EXT_DIFFUSE_TRACE` | **[R]** | `step_bnd_ext_diffuse_result()` | 漫反射弹射射线 |
| 29 | `PATH_BND_EXT_DIFFUSE_RESULT` | [C] | (inline) | 漫反射结果处理 |
| 30 | `PATH_BND_EXT_DIFFUSE_SHADOW_TRACE` | **[R]** | `step_bnd_ext_diffuse_shadow_result()` | 弹射阴影射线 |
| 31 | `PATH_BND_EXT_DIFFUSE_SHADOW_RESULT` | [C] | (inline) | 弹射阴影结果处理 |
| 32 | `PATH_BND_EXT_FINALIZE` | [C] | `step_bnd_ext_finalize()` | 汇总热流并返回 |

#### 状态转移

```
PATH_BND_EXT_CHECK [C]
  │
  ├─ 无外部热流 → return_state (bypass，回到 SF/SFN 调用者)
  │
  ├─ 有直接照射源:
  │   ▼
  │ PATH_BND_EXT_DIRECT_TRACE [R: 1 shadow ray]
  │   ▼
  │ (结果 inline 处理)
  │   ▼
  │ PATH_BND_EXT_DIFFUSE_TRACE [R: 1 cosine-weighted bounce ray]
  │   ▼
  │ (命中 & 反射?)
  │   ├─ 是: PATH_BND_EXT_DIFFUSE_SHADOW_TRACE [R: 1 shadow ray]
  │   │       ▼
  │   │     (结果处理 → 循环: 回到 DIFFUSE_TRACE? 或 FINALIZE)
  │   └─ 否/吸收: PATH_BND_EXT_FINALIZE
  │
  ▼
PATH_BND_EXT_FINALIZE [C]
  │ 将 flux_direct + flux_diffuse_reflected + flux_scattered 累加到 T
  ▼
return_state (回到 SF_PROB_DISPATCH 或 SFN_PROB_DISPATCH)
```

#### 射线参数
- 直接照射阴影射线: `RAY_BUCKET_SHADOW`, 1 射线, `[0, source_distance]`
- 漫反射弹射射线: `RAY_BUCKET_RADIATIVE`, 1 射线, `[0, FLT_MAX]`
- 弹射阴影射线: `RAY_BUCKET_SHADOW`, 1 射线, `[0, source_distance]`

---

## 7. CND — 传导路径

传导路径模拟固体介质内的热扩散。有两种算法：delta-sphere（M4）和 Walk-on-Spheres（M9）。

入口统一经过 `PATH_COUPLED_CONDUCTIVE` → `step_conductive()`，后者根据 `ctx.diff_algo` 分派。分派前先发射 6 射线腔体查询（ENC_QUERY_EMIT）确定当前位置的腔体归属。

### 7.1 CND-DS — Delta-Sphere 游走（M4）

**物理含义**：每步沿随机方向发射两条对向射线（`dir0` 和 `-dir0`），取两者命中距离的最小值作为步长 δ，在 δ 半径球面上均匀采样下一位置，循环至到达已知温度的边界。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 35 | `PATH_CND_DS_CHECK_TEMP` | [C] | `step_cnd_ds_check_temp()` | 初始化（首次）+ 检查是否达到已知温度边界 |
| 36 | `PATH_CND_DS_STEP_TRACE` | **[R]** | `step_conductive_ds_process()` | 2 条对向 step 射线 |
| 37 | `PATH_CND_DS_STEP_PROCESS` | [C, Future] | — | 命中结果处理（inline 在 `step_conductive_ds_process` 内） |
| 38 | `PATH_CND_DS_STEP_ENC_VERIFY` | [C] | `step_cnd_ds_step_enc_verify()` | 腔体验证子查询设置 |
| 39 | `PATH_CND_DS_STEP_ADVANCE` | [C] | `step_cnd_ds_step_advance()` | 位移更新 + 循环判断 |

#### 状态转移

```
PATH_COUPLED_CONDUCTIVE
  │ diff_algo == DELTA_SPHERE
  │ 发射 6-ray enc_query → 返回至:
  ▼
PATH_CND_DS_CHECK_TEMP [C]
  │ 首次: 从 enc_query 结果初始化 enc_id, medium, delta_solid
  │ 后续: 查询当前位置温度
  │
  ├─ 温度已知 → T.done = 1 → PATH_DONE
  ├─ 温度未知:
  │   ▼
  │ 发射 2 条对向射线 setup_delta_sphere_rays()
  │ PATH_CND_DS_STEP_TRACE [R: 2射线]
  │   │
  │   ▼ step_conductive_ds_process():
  │   ├─ 前向命中在 enc 内 → enc 匹配检查:
  │   │   ├─ 匹配 → PATH_CND_DS_STEP_ADVANCE
  │   │   └─ 不匹配 → 重试 → PATH_CND_DS_CHECK_TEMP
  │   │
  │   └─ 无前向命中 / 距离 > delta:
  │       ▼
  │     PATH_CND_DS_STEP_ENC_VERIFY [C]
  │       │ 发射 6-ray enc_query 验证 pos_next
  │       │ → 返回至 PATH_CND_DS_STEP_ADVANCE
  │       ▼
  │     PATH_CND_DS_STEP_ADVANCE [C]
  │       │ enc 匹配检查
  │       │ 计算体积功率贡献
  │       │ 时间回退 (unsteady)
  │       │ 位移更新
  │       │
  │       ├─ 到达界面 → PATH_COUPLED_BOUNDARY
  │       ├─ 继续游走 → PATH_CND_DS_CHECK_TEMP (循环)
  │       └─ 异常 → PATH_DONE (错误)
```

#### 射线参数
- 桶类型: `RAY_BUCKET_STEP_PAIR`
- 射线数: 2（对向）
- 范围: `[FLT_MIN, delta_solid * RAY_RANGE_MAX_SCALE]`

---

### 7.2 CND-WoS — Walk-on-Spheres（M9）

**物理含义**：查询当前位置到最近界面的距离 d，以 d 为半径在球面上均匀采样下一位置。当 d < ε（进入 ε-壳层）时，吸附到最近界面读取温度。使用 **closest_point (CP)** 批量查询替代射线追踪。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 40 | `PATH_CND_WOS_CHECK_TEMP` | [C] | `step_cnd_wos_check_temp()` | 循环顶部：检查温度 + 发射 CP 查询 |
| 41 | `PATH_CND_WOS_CLOSEST` | **[CP]** | `step_cnd_wos_closest()` | 最近点查询已提交，等待结果 |
| 42 | `PATH_CND_WOS_CLOSEST_RESULT` | [C] | `step_cnd_wos_closest_result()` | 处理查询结果：ε-壳/采样 |
| 43 | `PATH_CND_WOS_DIFFUSION_CHECK` | **[CP]** | `step_cnd_wos_diffusion_check()` | 批量 CP 验证新位置 |
| 44 | `PATH_CND_WOS_DIFFUSION_CHECK_RESULT` | [C] | `step_cnd_wos_diffusion_check_result()` | 验证结果处理 |
| 45 | `PATH_CND_WOS_FALLBACK_TRACE` | **[R]** | `step_cnd_wos_fallback_trace()` / `step_cnd_wos_fallback_result()` | fallback 射线追踪 |
| 46 | `PATH_CND_WOS_FALLBACK_RESULT` | [C, Future] | — | fallback 结果（inline 实现） |
| 47 | `PATH_CND_WOS_TIME_TRAVEL` | [C] | `step_cnd_wos_time_travel()` | 时间回退 + 循环判决 |

#### 状态转移

```
PATH_COUPLED_CONDUCTIVE
  │ diff_algo == WOS
  │ 发射 6-ray enc_query → 返回至:
  ▼
PATH_CND_WOS_CHECK_TEMP [C]
  │ 首次: 初始化 WoS (enc_id, medium, alpha, delta)
  │ 后续: 查询当前位置温度
  │
  ├─ 温度已知 → T.done = 1 → PATH_DONE
  ├─ 温度未知:
  │   ▼
  │ 发射 closest_point 查询
  │ PATH_CND_WOS_CLOSEST [CP]
  │   │
  │   ▼
  │ PATH_CND_WOS_CLOSEST_RESULT [C]
  │   │
  │   ├─ d < ε (ε-壳层):
  │   │   吸附到界面 → PATH_CND_WOS_TIME_TRAVEL
  │   │
  │   └─ d >= ε (正常步):
  │       球面均匀采样新位置候选
  │       发射 CP 验证查询
  │       ▼
  │     PATH_CND_WOS_DIFFUSION_CHECK [CP]
  │       ▼
  │     PATH_CND_WOS_DIFFUSION_CHECK_RESULT [C]
  │       ├─ 位置有效 → 移动, PATH_CND_WOS_TIME_TRAVEL
  │       └─ 位置无效 (穿越边界):
  │           ▼
  │         PATH_CND_WOS_FALLBACK_TRACE [R: 1射线]
  │           ▼
  │         (inline 处理)
  │           ▼
  │         PATH_CND_WOS_TIME_TRAVEL
  │
  ▼
PATH_CND_WOS_TIME_TRAVEL [C]
  │ 时间回退 (unsteady)
  │ 计算体积功率
  │
  ├─ T.done → PATH_DONE
  ├─ 到达界面 → PATH_COUPLED_BOUNDARY
  └─ 继续 → PATH_CND_WOS_CHECK_TEMP (循环)
```

#### WoS 特殊之处：两种查询类型

WoS 是唯一同时使用 **CP** 和 **RT** 两种查询类型的子状态机：
- 主循环用 CP（`PATH_CND_WOS_CLOSEST` / `PATH_CND_WOS_DIFFUSION_CHECK`）
- Fallback 用 RT（`PATH_CND_WOS_FALLBACK_TRACE`）

`path_phase_is_cp_pending()` 检测 CP 等待状态，使 Wavefront 调度器可独立于 RT 批次处理 CP 批次。

---

### 7.3 CND 其他

| # | 枚举值 | 标记 | 说明 |
|---|--------|------|------|
| 34 | `PATH_CND_INIT_ENC` | [R, Future] | 初始腔体归属查询（预留，目前由 `step_conductive()` 内的 `step_enc_query_emit()` 替代） |
| 48 | `PATH_CND_CUSTOM` | [C, Future] | 自定义传导回调插件接口 |

---

## 8. CNV — 对流路径（M6）

**物理含义**：路径进入流体介质后的对流换热。使用 null-collision 机制模拟流体内部的散射/吸收过程。

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 49 | `PATH_CNV_INIT` | [C] | `step_cnv_init()` | 获取流体温度，初始化 null-collision |
| 50 | `PATH_CNV_STARTUP_TRACE` | **[R]** | `step_cnv_startup_result()` | 启动探针射线 |
| 51 | `PATH_CNV_STARTUP_RESULT` | [C, Future] | — | 启动结果处理（inline） |
| 52 | `PATH_CNV_SAMPLE_LOOP` | [C] | `step_cnv_sample_loop()` | Null-collision 采样循环 |

#### 状态转移

```
PATH_COUPLED_CONVECTIVE
  ▼
PATH_CNV_INIT [C]
  │ 获取流体温度
  │
  ├─ 温度已知 → T.done = 1 → PATH_DONE
  ├─ 需要启动射线:
  │   ▼
  │ PATH_CNV_STARTUP_TRACE [R: 1射线, +Z方向]
  │   ▼
  │ (inline 处理: 设置 hit_side)
  │   ▼
  │ PATH_CNV_SAMPLE_LOOP [C]
  │
  └─ 不需要启动射线 → PATH_CNV_SAMPLE_LOOP
      ▼
PATH_CNV_SAMPLE_LOOP [C]
  │ null-collision 循环体:
  │ 采样散射/吸收
  │
  ├─ T.done → PATH_DONE
  ├─ 到达界面 → PATH_COUPLED_BOUNDARY
  └─ 应继续但需重建 (稀有) → 同步 fallback
```

#### 射线参数
- 启动射线: `RAY_BUCKET_STARTUP`, 1 射线, `[FLT_MIN, FLT_MAX]`, 方向 `(0,0,1)`

---

## 9. ENC — 腔体归属查询

腔体归属查询确定给定空间点位于哪个封闭腔体内。有两种实现。

### M1-v2：6 射线 BVH 查询（主用）

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 53 | `PATH_ENC_QUERY_EMIT` | **[R]** | `step_enc_query_emit()` / `step_enc_query_resolve()` | 6 条 PI/4 旋转轴对齐射线 |
| 54 | `PATH_ENC_QUERY_FB_EMIT` | **[R]** | `step_enc_query_fb_resolve()` | 6 射线全失败时的 fallback 射线 |

#### 状态转移

```
(任意调用者, 如 CND-DS / CND-WoS / BND-SS / BND-SF)
  │ step_enc_query_emit(p, hot, enc, pos, return_state)
  ▼
PATH_ENC_QUERY_EMIT [R: 6射线]
  │ 6 条 PI/4 旋转轴对齐方向
  │
  ▼ step_enc_query_resolve():
  ├─ 任一射线有效命中 → resolved_enc_id, → return_state
  └─ 全部失败:
      ▼
    PATH_ENC_QUERY_FB_EMIT [R: 1 fallback射线, 方向(1,1,1)/√3]
      │
      ▼ step_enc_query_fb_resolve():
      ├─ 命中有效 → resolved_enc_id, → return_state
      └─ 仍然失败 → 同步 brute-force fallback → return_state
```

#### 射线参数
- 桶类型: `RAY_BUCKET_ENCLOSURE`
- 射线数: 6（主查询）/ 1（fallback）
- 范围: `[FLT_MIN, FLT_MAX]`
- 无自交过滤

### M10：BVH Closest-Primitive 查询（备用）

| # | 枚举值 | 标记 | 步进函数 | 说明 |
|---|--------|------|----------|------|
| 55 | `PATH_ENC_LOCATE_PENDING` | **[ENC]** | `step_enc_locate_submit()` | BVH 最近基元查询已提交 |
| 56 | `PATH_ENC_LOCATE_RESULT` | [C] | `step_enc_locate_result()` | 查询结果处理 |

#### 转移
- `step_enc_locate_submit(p, hot, enc, pos, return_state)` → `PATH_ENC_LOCATE_PENDING`
- 结果到达后 → `PATH_ENC_LOCATE_RESULT` [C] → `return_state`

---

## 10. 射线桶分类（Ray Bucket）

`enum ray_bucket_type` 将射线按 BVH 遍历模式分为 5 类，用于 GPU 端 warp 一致性分组：

| 桶 | 枚举值 | 说明 | 使用者 |
|----|--------|------|--------|
| 0 | `RAY_BUCKET_RADIATIVE` | 长程随机方向，`[ε, ∞)` | RAD 弹射、SF/SFN null-collision、WoS fallback |
| 1 | `RAY_BUCKET_STEP_PAIR` | 短程对向射线（delta-sphere）| CND-DS step、BND-SS/SF 再射入 |
| 2 | `RAY_BUCKET_SHADOW` | 固定距离阴影射线，`[0, dist]` | BND-EXT 直接照射/弹射阴影 |
| 3 | `RAY_BUCKET_STARTUP` | 单方向探针射线 | CNV 启动射线 |
| 4 | `RAY_BUCKET_ENCLOSURE` | 6 射线轴对齐腔体查询 | ENC M1-v2 |

---

## 11. 查询类型汇总

整个 FSM 涉及 **4 种异构查询类型**：

| 查询类型 | 缩写 | 批处理接口 | 状态判别函数 | 使用的状态 |
|----------|------|-----------|-------------|-----------|
| 射线追踪 | RT | `s3d_batch_trace` | `path_phase_is_ray_pending()` | 全部 [R] 状态（约 17 个） |
| 最近点投影 | CP | `s3d_batch_cp_context` | `path_phase_is_cp_pending()` | `PATH_CND_WOS_CLOSEST`, `PATH_CND_WOS_DIFFUSION_CHECK` |
| 腔体归属(closest-prim) | ENC | `pool_enc_locate` | `path_phase_is_enc_locate_pending()` | `PATH_ENC_LOCATE_PENDING` |
| 腔体归属(6-ray) | ENC-RT | 复用 RT 批次 | （作为 RT 的一部分） | `PATH_ENC_QUERY_EMIT`, `PATH_ENC_QUERY_FB_EMIT` |

**调度优先级**：RT > CP > ENC。Wavefront 主循环每轮先处理 RT 批次，再处理 CP 批次，最后处理 ENC 批次。

---

## 12. 状态完整枚举表

按 `sdis_wf_types.h` 中的定义顺序，完整列出全部 59 个状态：

| # | 枚举名 | 标记 | 物理过程 | 步进函数 |
|---|--------|------|----------|----------|
| 0 | `PATH_INIT` | C | 生命周期 | `step_init` |
| 1 | `PATH_RAD_TRACE_PENDING` | R | RAD | `step_radiative_trace` |
| 2 | `PATH_RAD_PROCESS_HIT` | C/Future | RAD | — |
| 3 | `PATH_COUPLED_BOUNDARY` | C | Legacy入口 | `step_boundary` |
| 4 | `PATH_COUPLED_BOUNDARY_REINJECT` | R | Legacy | (B2/B3) |
| 5 | `PATH_COUPLED_CONDUCTIVE` | C | Legacy入口 | `step_conductive` |
| 6 | `PATH_COUPLED_COND_DS_PENDING` | R | Legacy | `step_conductive_ds_process` |
| 7 | `PATH_COUPLED_CONVECTIVE` | C | Legacy入口 | `step_convective` |
| 8 | `PATH_COUPLED_RADIATIVE` | C | Legacy入口 | `step_coupled_radiative_begin` |
| 9 | `PATH_BND_DISPATCH` | C | BND调度 | `step_bnd_dispatch` |
| 10 | `PATH_BND_POST_ROBIN_CHECK` | C | BND调度 | `step_bnd_post_robin_check` |
| 11 | `PATH_BND_SS_REINJECT_SAMPLE` | R | BND-SS | `step_bnd_ss_reinject_sample` |
| 12 | `PATH_BND_SS_REINJECT_ENC` | C | BND-SS | `step_bnd_ss_reinject_enc_result` |
| 13 | `PATH_BND_SS_REINJECT_DECIDE` | C | BND-SS | `step_bnd_ss_reinject_decide` |
| 14 | `PATH_BND_SF_REINJECT_SAMPLE` | R | BND-SF | `step_bnd_sf_reinject_sample` |
| 15 | `PATH_BND_SF_REINJECT_ENC` | C | BND-SF | `step_bnd_sf_reinject_enc_result` |
| 16 | `PATH_BND_SF_PROB_DISPATCH` | C | BND-SF | `step_bnd_sf_prob_dispatch` |
| 17 | `PATH_BND_SF_NULLCOLL_RAD_TRACE` | R | BND-SF | `step_bnd_sf_nullcoll_rad_trace` |
| 18 | `PATH_BND_SF_NULLCOLL_DECIDE` | C | BND-SF | `step_bnd_sf_nullcoll_decide` |
| 19 | `PATH_BND_SFN_PROB_DISPATCH` | C | BND-SFN | `step_bnd_sfn_prob_dispatch` |
| 20 | `PATH_BND_SFN_RAD_TRACE` | R | BND-SFN | `step_bnd_sfn_rad_trace` |
| 21 | `PATH_BND_SFN_RAD_DONE` | C | BND-SFN | `step_bnd_sfn_rad_done` |
| 22 | `PATH_BND_SFN_COMPUTE_Ti` | C | BND-SFN | `step_bnd_sfn_compute_Ti` |
| 23 | `PATH_BND_SFN_COMPUTE_Ti_RESUME` | C | BND-SFN | `step_bnd_sfn_compute_Ti_resume` |
| 24 | `PATH_BND_SFN_CHECK_PMIN_PMAX` | C | BND-SFN | `step_bnd_sfn_check_pmin_pmax` |
| 25 | `PATH_BND_EXT_CHECK` | C | BND-EXT | `step_bnd_ext_check` |
| 26 | `PATH_BND_EXT_DIRECT_TRACE` | R | BND-EXT | `step_bnd_ext_direct_result` |
| 27 | `PATH_BND_EXT_DIRECT_RESULT` | C | BND-EXT | (inline) |
| 28 | `PATH_BND_EXT_DIFFUSE_TRACE` | R | BND-EXT | `step_bnd_ext_diffuse_result` |
| 29 | `PATH_BND_EXT_DIFFUSE_RESULT` | C | BND-EXT | (inline) |
| 30 | `PATH_BND_EXT_DIFFUSE_SHADOW_TRACE` | R | BND-EXT | `step_bnd_ext_diffuse_shadow_result` |
| 31 | `PATH_BND_EXT_DIFFUSE_SHADOW_RESULT` | C | BND-EXT | (inline) |
| 32 | `PATH_BND_EXT_FINALIZE` | C | BND-EXT | `step_bnd_ext_finalize` |
| 33 | `PATH_CND_INIT_ENC` | R/Future | CND | — |
| 34 | `PATH_CND_DS_CHECK_TEMP` | C | CND-DS | `step_cnd_ds_check_temp` |
| 35 | `PATH_CND_DS_STEP_TRACE` | R | CND-DS | `step_conductive_ds_process` |
| 36 | `PATH_CND_DS_STEP_PROCESS` | C/Future | CND-DS | — |
| 37 | `PATH_CND_DS_STEP_ENC_VERIFY` | C | CND-DS | `step_cnd_ds_step_enc_verify` |
| 38 | `PATH_CND_DS_STEP_ADVANCE` | C | CND-DS | `step_cnd_ds_step_advance` |
| 39 | `PATH_CND_WOS_CHECK_TEMP` | C | CND-WoS | `step_cnd_wos_check_temp` |
| 40 | `PATH_CND_WOS_CLOSEST` | CP | CND-WoS | `step_cnd_wos_closest` |
| 41 | `PATH_CND_WOS_CLOSEST_RESULT` | C | CND-WoS | `step_cnd_wos_closest_result` |
| 42 | `PATH_CND_WOS_DIFFUSION_CHECK` | CP | CND-WoS | `step_cnd_wos_diffusion_check` |
| 43 | `PATH_CND_WOS_DIFFUSION_CHECK_RESULT` | C | CND-WoS | `step_cnd_wos_diffusion_check_result` |
| 44 | `PATH_CND_WOS_FALLBACK_TRACE` | R | CND-WoS | `step_cnd_wos_fallback_trace` |
| 45 | `PATH_CND_WOS_FALLBACK_RESULT` | C/Future | CND-WoS | `step_cnd_wos_fallback_result` |
| 46 | `PATH_CND_WOS_TIME_TRAVEL` | C | CND-WoS | `step_cnd_wos_time_travel` |
| 47 | `PATH_CND_CUSTOM` | C/Future | CND | — |
| 48 | `PATH_CNV_INIT` | C | CNV | `step_cnv_init` |
| 49 | `PATH_CNV_STARTUP_TRACE` | R | CNV | `step_cnv_startup_result` |
| 50 | `PATH_CNV_STARTUP_RESULT` | C/Future | CNV | (inline) |
| 51 | `PATH_CNV_SAMPLE_LOOP` | C | CNV | `step_cnv_sample_loop` |
| 52 | `PATH_ENC_LOCATE_PENDING` | ENC | ENC-M10 | `step_enc_locate_submit` |
| 53 | `PATH_ENC_LOCATE_RESULT` | C | ENC-M10 | `step_enc_locate_result` |
| 54 | `PATH_ENC_QUERY_EMIT` | R | ENC-M1v2 | `step_enc_query_emit` / `step_enc_query_resolve` |
| 55 | `PATH_ENC_QUERY_FB_EMIT` | R | ENC-M1v2 | `step_enc_query_fb_resolve` |
| 56 | `PATH_DONE` | 终止 | 生命周期 | — |
| 57 | `PATH_HARVESTED` | 终止 | 生命周期 | — |
| 58 | `PATH_ERROR` | 终止 | 生命周期 | — |

**`PATH_PHASE_COUNT` = 59**（哨兵值，不代表真实状态）

---

## 13. 全局状态转移图（文字描述）

### 一级转移：物理过程间跳转

```
PATH_INIT ──[setup ray]──→ PATH_RAD_TRACE_PENDING
PATH_RAD_TRACE_PENDING ──[miss]──→ PATH_DONE
PATH_RAD_TRACE_PENDING ──[absorbed]──→ PATH_COUPLED_BOUNDARY
PATH_RAD_TRACE_PENDING ──[reflect]──→ PATH_RAD_TRACE_PENDING (loop)

PATH_COUPLED_BOUNDARY ──→ PATH_BND_DISPATCH
PATH_BND_DISPATCH ──[Dirichlet]──→ PATH_DONE
PATH_BND_DISPATCH ──[SS]──→ PATH_BND_SS_REINJECT_SAMPLE
PATH_BND_DISPATCH ──[SF]──→ PATH_BND_SF_REINJECT_SAMPLE
PATH_BND_DISPATCH ──[SFN]──→ PATH_BND_SFN_PROB_DISPATCH (via SF reinject)

BND-SS/SF/SFN 最终输出:
  ──[p_cond]──→ PATH_COUPLED_CONDUCTIVE
  ──[p_conv]──→ PATH_COUPLED_CONVECTIVE
  ──[p_radi]──→ PATH_COUPLED_RADIATIVE

PATH_COUPLED_CONDUCTIVE ──→ CND-DS / CND-WoS (via enc_query)
PATH_COUPLED_CONVECTIVE ──→ PATH_CNV_INIT
PATH_COUPLED_RADIATIVE ──[cosine sample]──→ PATH_RAD_TRACE_PENDING

CND-DS/WoS 最终输出:
  ──[temp known]──→ PATH_DONE
  ──[hit boundary]──→ PATH_COUPLED_BOUNDARY

CNV 最终输出:
  ──[temp known]──→ PATH_DONE
  ──[hit boundary]──→ PATH_COUPLED_BOUNDARY
```

### 二级转移：ENC 子查询

ENC 查询作为子状态机可被任何需要腔体归属信息的状态调用：

```
调用者 (CND-DS, CND-WoS, BND-SS, BND-SF) 
  │ step_enc_query_emit(p, hot, enc, pos, return_state)
  ▼
PATH_ENC_QUERY_EMIT [R]
  ├─ 成功 → return_state (调用者指定的恢复状态)
  └─ 失败 → PATH_ENC_QUERY_FB_EMIT [R]
      ├─ 成功 → return_state
      └─ 失败 → 同步 fallback → return_state
```

ENC 子查询的 `return_state` 由调用者在发射时指定，典型值：
- `PATH_CND_DS_CHECK_TEMP`（delta-sphere 初始化后）
- `PATH_CND_DS_STEP_ADVANCE`（delta-sphere 步进验证后）
- `PATH_CND_WOS_CHECK_TEMP`（WoS 初始化后）
- `PATH_BND_SS_REINJECT_DECIDE`（SS 再射入验证后）
- `PATH_BND_SF_PROB_DISPATCH`（SF 再射入验证后）

---

## 附录 A：源文件与状态的对应关系

| 源文件 | 负责的状态 | Milestone |
|--------|-----------|-----------|
| `sdis_wf_steps_core.c` | `PATH_INIT`, `PATH_RAD_*`, `PATH_COUPLED_*`, dispatch | Core |
| `sdis_wf_steps_bnd_ss.c` | `PATH_BND_SS_*` | M3 |
| `sdis_wf_steps_bnd_sf.c` | `PATH_BND_SF_*` | M5 |
| `sdis_wf_steps_bnd_sfn.c` | `PATH_BND_SFN_*` | M8 |
| `sdis_wf_steps_bnd_ext.c` | `PATH_BND_EXT_*` | M7 |
| `sdis_wf_steps_cnd.c` | `PATH_CND_DS_*`, `PATH_CND_WOS_*` | M4, M9 |
| `sdis_wf_steps_cnv.c` | `PATH_CNV_*`, `PATH_BND_DISPATCH`, `PATH_BND_POST_ROBIN_CHECK` | M6 |
| `sdis_wf_steps_enc.c` | `PATH_ENC_*` | M1-v2, M10 |

## 附录 B：path_state 内存布局概要

```
struct path_state (~2 KB hot + ~2.6 KB cold)
├── Hot path (频繁访问):
│   ├── rwalk (当前游走状态: 位置、时间、命中信息、enc_id)
│   ├── T (温度累加器: value, done, func)
│   ├── ctx (上下文: max_branchings, diff_algo, ...)
│   ├── rng (per-path CBRNG)
│   ├── ray_req (射线请求: origin, direction, range, ray_count)
│   ├── filter_data_storage (自交过滤参数)
│   ├── rad_direction[3], rad_bounce_count, rad_retry_count
│   ├── pixel_x, pixel_y, spp_idx
│   └── coupled_nbranchings, done_reason
│
├── locals (union, 按当前子过程使用):
│   ├── bnd_ss (SS: dir_frt, dir_bck, enc_ids, lambda, tcr, ...)
│   ├── bnd_sf (SF/SFN: p_conv/cond/radi, h_hat, epsilon, Tref, ...)
│   ├── cnd_ds (DS: dir0, dir1, hit0, hit1, delta, enc_id, ...)
│   └── cnd_wos (WoS: wos_distance, alpha, medium, ...)
│
└── Cold blocks (P1 SoA分离, 存储在 pool->*_arr[slot_idx]):
    ├── path_enc_data (ENC: query_pos, directions[6], dir_hits[6], ...)
    ├── path_ext_data (EXT: flux_direct, flux_diffuse, source_*, ...)
    └── path_sfn_data (SFN: picard栈, rwalk/T/hvtx 快照 x MAX_PICARD_DEPTH)
```
