# 显式状态机转换：逐判断源码映射手册

生成时间: 2026-02-12  
前置依赖: `solve_camera_state_machine_analysis.md`  
目的: **让每一个显式状态的迁移判断都可精确追溯到 CPU 源码的具体行/表达式**，为重写提供逐行级别的实施依据。

---

## 0. 阅读约定

- 🔴 = 需要发射射线（trace_ray / closest_point），是 wavefront 挂起点
- ✅ = 纯计算，不需要几何查询
- `→ STATE` = 状态迁移目标
- 源码行号标记为 `[file:line]`，基线 `stardis-cpu/stardis-solver/0.16.2/src/`

---

## 1. 顶层驱动循环映射

### CPU 原始代码 (`sdis_realisation_Xd.h:105-128`)

```c
/* sample_coupled_path 核心循环 */
while(!T->done) {
    const struct rwalk rwalk_bkp = *rwalk;
    const struct temperature T_bkp = *T;
    size_t nfails = 0;
    do {
        res = T->func(scn, ctx, rwalk, rng, T);       // ← 隐式状态跳转
        if(res == RES_BAD_OP) { *rwalk = rwalk_bkp; *T = T_bkp; }
    } while(res == RES_BAD_OP && ++nfails < MAX_FAILS);
}
```

### 显式状态机等价

```c
/* advance_path() — wavefront 主循环调用此函数 */
while(p->state != PATH_DONE && p->state != PATH_ERROR && p->n_pending_rays == 0) {
    switch(p->state) {
        case PATH_RAD_TRACE:              advance_rad_trace(p);            break;
        case PATH_RAD_BOUNCE_OR_ABSORB:   advance_rad_bounce_or_absorb(p); break;
        // ... 所有状态分支
    }
}
```

**映射关系**: `T->func` 的每一种取值对应一组 `enum path_state` 值：

| `T->func` 取值 | 对应的显式状态集合 |
|---|---|
| `radiative_path_3d` | `PATH_RAD_TRACE`, `PATH_RAD_BOUNCE_OR_ABSORB`, `PATH_RAD_ESCAPE` |
| `boundary_path_3d` | `PATH_BND_*` (约 15 个子状态) |
| `conductive_path_3d` | `PATH_CND_*` (约 7 个子状态) |
| `convective_path_3d` | `PATH_CNV_*` (约 3 个子状态) |
| `T->done = 1` | `PATH_DONE` |

---

## 2. 模块导航

| 模块 | 文件 | 状态数 | 关键射线类型 |
|---|---|---|---|
| 辐射路径 | [esm/01_radiative.md](esm/01_radiative.md) | 2 | trace_ray |
| 边界分派 | [esm/02_boundary_dispatch.md](esm/02_boundary_dispatch.md) | 2 | — |
| solid/solid | [esm/03_solid_solid.md](esm/03_solid_solid.md) | 3 | reinjection ×4 |
| 导热路径 | [esm/04_conductive.md](esm/04_conductive.md) | ~12 | trace/closest |
| picard 系列 | [esm/05_picard.md](esm/05_picard.md) | ~15 | null-coll rad |
| 对流路径 | [esm/06_convective.md](esm/06_convective.md) | 4 | startup ray |
| enclosure | [esm/07_enclosure_query.md](esm/07_enclosure_query.md) | 3 | 6-dir query |

---

## 3. 全局状态迁移图汇总

> 本节使用 Mermaid stateDiagram-v2 描述所有显式状态及其转移。按子系统分为 5 张图，便于分层理解和版本控制。
>
> **图例约定**:
> - 🔴 = 状态内含 trace_ray / closest_point，是 wavefront 挂起点
> - ✅ = 纯计算状态，不挂起
> - `-->` 上的标签 = 转移条件（对应源码判断表达式）

### 3.1 顶层总览：四大路径子系统

```mermaid
stateDiagram-v2
    direction TB

    [*] --> RAD_TRACE : ray_realisation 入口

    state "辐射路径 (Radiative)" as RAD {
        RAD_TRACE
        RAD_BOUNCE_OR_ABSORB
    }

    state "边界分派 (Boundary)" as BND {
        BND_DISPATCH
        BND_POST_ROBIN_CHECK
        state "solid/solid" as SS
        state "picard1 (solid/fluid)" as SF
        state "picardN (solid/fluid)" as SFN
        state "外部净通量" as EXT
    }

    state "导热路径 (Conductive)" as CND {
        CND_INIT_ENC
        state "delta-sphere" as CND_DS
        state "WoS" as CND_WOS
        CND_CUSTOM
    }

    state "对流路径 (Convective)" as CNV {
        CNV_INIT
        CNV_SAMPLE_BOUNDARY
    }

    RAD --> BND : absorb (进入边界)
    BND --> CND : solid_reinjection → 导热
    BND --> CNV : r < p_conv → 对流
    CND --> BND : hit 界面 → 边界分派
    CNV --> BND : accept → 边界分派
    BND --> [*] : Dirichlet / Robin / T_known → DONE
    RAD --> [*] : miss (逃逸) → DONE
    CND --> [*] : T_known / time_done → DONE
    CNV --> [*] : T_known / time_done → DONE
```

### 3.2 辐射路径详细状态图

```mermaid
stateDiagram-v2
    direction TB

    [*] --> RAD_TRACE

    state "RAD_TRACE 🔴" as RAD_TRACE
    note right of RAD_TRACE
        发射辐射光线 (trace_ray)
        含 find_next_fragment 重试机制
        源码: radiative_Xd.h:215-290
    end note

    state "RAD_BOUNCE_OR_ABSORB ✅" as RAD_BOA
    note right of RAD_BOA
        判断 miss/absorb/reflect
        + check_interface 重试 (≤10次)
        源码: radiative_Xd.h:235-290
    end note

    RAD_TRACE --> RAD_BOA : 射线结果返回

    RAD_BOA --> PATH_DONE : HIT_NONE (光线逃逸)
    RAD_BOA --> BND_DISPATCH : random < emissivity (吸收)
    RAD_BOA --> RAD_TRACE : else (反射, 采样新方向)
    RAD_BOA --> RAD_TRACE : check_interface 失败\n且 nattempts < 10 (重试)
    RAD_BOA --> PATH_ERROR : check_interface 失败\n且 nattempts == 10

    state "PATH_DONE ✅" as PATH_DONE
    state "PATH_ERROR" as PATH_ERROR
    state "BND_DISPATCH ✅" as BND_DISPATCH
```

### 9.3 边界分派与子路径状态图

```mermaid
stateDiagram-v2
    direction TB

    state "BND_DISPATCH ✅" as BND_DISP
    note right of BND_DISP
        查 Dirichlet → 三路分派 → Robin 后检查
        源码: boundary_Xd.h:32-117
    end note

    state "BND_POST_ROBIN_CHECK ✅" as BND_ROBIN
    note right of BND_ROBIN
        query_medium_temperature_from_boundary
        纯查表, 源码: boundary_Xd_c.h:1026-1065
    end note

    %% ─── solid/solid 子系统 ───
    state "SS_REINJECT_SAMPLE 🔴" as SS_SAMP
    note right of SS_SAMP
        4 条 reinjection 射线 (front×2 + back×2)
        源码: solid_solid.h:96-102
    end note
    state "SS_REINJECT_ENC 🔴" as SS_ENC
    note right of SS_ENC
        enclosure 查询 (miss 时)
        源码: boundary_Xd_c.h:320
    end note
    state "SS_REINJECT_DECIDE ✅" as SS_DECIDE

    %% ─── picard1 子系统 ───
    state "SF_REINJECT_SAMPLE 🔴" as SF_SAMP
    state "SF_PROB_DISPATCH ✅" as SF_PROB
    note right of SF_PROB
        null-collision 概率分派
        源码: picard1.h:229-310
    end note
    state "SF_NULLCOLL_RAD_TRACE 🔴" as SF_RAD
    state "SF_NULLCOLL_DECIDE ✅" as SF_DECIDE

    %% ─── picardN 子系统 ───
    state "SFN_PROB_DISPATCH ✅" as SFN_PROB
    state "SFN_RAD_TRACE 🔴" as SFN_RAD
    state "SFN_RAD_DONE ✅" as SFN_RAD_DONE
    state "SFN_COMPUTE_Ti ✅" as SFN_Ti
    state "SFN_COMPUTE_Ti_RESUME ✅" as SFN_RESUME
    state "SFN_CHECK_PMIN_PMAX ✅" as SFN_CHECK

    %% ─── 外部净通量子系统 ───
    state "BND_EXT_CHECK ✅" as EXT_CHK
    state "BND_EXT_DIRECT_RESULT 🔴" as EXT_DIR
    state "BND_EXT_DIFFUSE_INIT 🔴" as EXT_DIFF_INIT
    state "BND_EXT_DIFFUSE_TRACE_RESULT 🔴" as EXT_DIFF_TR
    state "BND_EXT_DIFFUSE_SHADOW_RESULT 🔴" as EXT_DIFF_SH
    state "BND_EXT_DIFFUSE_NEXT_BOUNCE 🔴" as EXT_DIFF_NB
    state "BND_EXT_FINALIZE ✅" as EXT_FIN

    state "PATH_DONE" as DONE
    state "CNV_INIT" as CNV_INIT
    state "CND_INIT_ENC" as CND_INIT

    %% ═══ BND_DISPATCH 转移 ═══
    BND_DISP --> DONE : TEMPERATURE_IS_KNOWN (Dirichlet)
    BND_DISP --> SS_SAMP : front.type == back.type (solid/solid)
    BND_DISP --> SF_SAMP : nbranchings == max (picard1)
    BND_DISP --> SFN_PROB : nbranchings < max (picardN)

    %% ═══ solid/solid 流程 ═══
    SS_SAMP --> SS_ENC : 某条射线 miss → 需 enclosure 查询
    SS_SAMP --> SS_DECIDE : 所有射线均 hit
    SS_ENC --> SS_DECIDE : enclosure 确定
    SS_DECIDE --> DONE : time_rewind 到达极限
    SS_DECIDE --> CND_INIT : 注入到固体内部
    SS_DECIDE --> BND_DISP : 注入到界面

    %% ═══ picard1 流程 ═══
    SF_SAMP --> EXT_CHK : reinjection 完成 → 检查外部通量
    EXT_CHK --> SF_PROB : 外部通量处理完毕 / 不需要
    SF_PROB --> CNV_INIT : r < p_conv (对流)
    SF_PROB --> CND_INIT : r < p_conv+p_cond (导热, reinjection)
    SF_PROB --> BND_DISP : r < p_conv+p_cond (导热, 注入到界面)
    SF_PROB --> SF_RAD : else (采样辐射子路径)
    SF_RAD --> SF_DECIDE : 辐射子路径完成
    SF_DECIDE --> BND_ROBIN : accept (r < threshold)
    SF_DECIDE --> SF_PROB : reject (null-collision → 循环)

    %% ═══ picardN 流程 ═══
    SFN_PROB --> CNV_INIT : r < p_conv
    SFN_PROB --> CND_INIT : r < p_conv+p_cond
    SFN_PROB --> SFN_RAD : else (辐射)
    SFN_RAD --> SFN_RAD_DONE : 辐射子路径完成
    SFN_RAD_DONE --> BND_ROBIN : 提前接受 (r < min_threshold)
    SFN_RAD_DONE --> SFN_Ti : 需要 Ti 采样
    SFN_Ti --> SFN_CHECK : T_s.done (直接复用)
    SFN_Ti --> BND_DISP : !T_s.done (压栈, 递归子路径)
    SFN_RESUME --> SFN_CHECK : 子路径完成 (弹栈)
    SFN_CHECK --> BND_ROBIN : 提前接受
    SFN_CHECK --> SFN_PROB : 提前拒绝 (null-collision)
    SFN_CHECK --> SFN_Ti : 继续 (i < 6)
    SFN_CHECK --> BND_ROBIN : 最终精确判断 - 接受
    SFN_CHECK --> SFN_PROB : 最终精确判断 - 拒绝

    %% ═══ Robin 后检查 ═══
    BND_ROBIN --> DONE : Robin 温度已知
    BND_ROBIN --> CNV_INIT : Robin 未知 + 下一跳 = convective
    BND_ROBIN --> CND_INIT : Robin 未知 + 下一跳 = conductive

    %% ═══ 外部净通量流程 ═══
    EXT_CHK --> EXT_DIR : cos_theta > 0 (发射 shadow ray)
    EXT_CHK --> EXT_DIFF_INIT : cos_theta ≤ 0 (跳过直接)
    EXT_DIR --> EXT_DIFF_INIT : shadow ray 结果处理完毕
    EXT_DIFF_INIT --> EXT_DIFF_TR : 🔴 漫反射弹跳射线
    EXT_DIFF_TR --> EXT_FIN : miss (逃逸) / absorb
    EXT_DIFF_TR --> EXT_DIFF_SH : reflect → 🔴 弹跳处 shadow ray
    EXT_DIFF_SH --> EXT_DIFF_NB : shadow 结果 → 继续弹跳
    EXT_DIFF_NB --> EXT_DIFF_TR : 🔴 下一次弹跳射线 (循环)
    EXT_FIN --> SF_PROB : 返回 picard1 主循环
```

### 3.4 导热路径详细状态图

```mermaid
stateDiagram-v2
    direction TB

    state "CND_INIT_ENC 🔴" as CND_ENC
    note right of CND_ENC
        6 方向 enclosure 查询
        源码 scene_Xd.h 1318-1385
    end note

    state "PATH_DONE" as DONE
    state "BND_DISPATCH" as BND_DISP

    %% ═══ Delta-Sphere 子系统 ═══
    state "CND_DS_CHECK_TEMP ✅" as DS_CHK
    state "CND_DS_STEP_SAMPLE 🔴" as DS_STEP
    note right of DS_STEP
        sample_next_step_robust
        发射 2 条方向射线
        源码 delta_sphere_Xd.h 125-320
    end note
    state "CND_DS_STEP_PROCESS ✅" as DS_PROC
    state "CND_DS_STEP_ENC_VERIFY 🔴" as DS_ENC
    state "CND_DS_STEP_ADVANCE ✅" as DS_ADV

    %% ═══ WoS 子系统 ═══
    state "CND_WOS_CHECK_TEMP ✅" as WOS_CHK
    state "CND_WOS_CLOSEST 🔴" as WOS_CP
    note right of WOS_CP
        closest_point 查询 (非 trace_ray!)
        源码 wos_Xd.h 514-517
    end note
    state "CND_WOS_CLOSEST_RESULT ✅" as WOS_CR
    state "CND_WOS_FALLBACK_TRACE 🔴" as WOS_FB
    note right of WOS_FB
        扩散位置无效时的 fallback trace_ray
        源码 wos_Xd.h 566-600
    end note
    state "CND_WOS_FALLBACK_RESULT ✅" as WOS_FR
    state "CND_WOS_TIME_TRAVEL ✅" as WOS_TT

    %% ═══ Custom 导热 (占位) ═══
    state "CND_CUSTOM ✅" as CUSTOM

    %% ═══ 入口分派 ═══
    CND_ENC --> DS_CHK : diff_algo == DELTA_SPHERE
    CND_ENC --> WOS_CHK : diff_algo == WOS
    CND_ENC --> CUSTOM : diff_algo == CUSTOM

    %% ═══ Delta-Sphere 转移 ═══
    DS_CHK --> DONE : TEMPERATURE_IS_KNOWN
    DS_CHK --> DS_STEP : else (发射 2 条步进射线)

    DS_STEP --> DS_PROC : 射线结果返回
    DS_PROC --> DS_ENC : hit0.distance > delta (需 enc 验证)
    DS_PROC --> DS_ADV : hit0.distance ≤ delta (无需验证)
    DS_ENC --> DS_ADV : 验证完成

    DS_ADV --> DONE : T->done (time_rewind 到极限)
    DS_ADV --> DS_CHK : HIT_NONE (继续循环)
    DS_ADV --> BND_DISP : !HIT_NONE (命中界面)

    %% ═══ WoS 转移 ═══
    WOS_CHK --> DONE : TEMPERATURE_IS_KNOWN
    WOS_CHK --> WOS_CP : 发射 closest_point

    WOS_CP --> WOS_CR : closest_point 结果返回

    WOS_CR --> WOS_TT : ε-shell 内 (snap to boundary)
    WOS_CR --> WOS_TT : diffusion_position 有效 (直接移动)
    WOS_CR --> WOS_FB : diffusion_position 无效 (fallback)

    WOS_FB --> WOS_FR : trace_ray 结果返回
    WOS_FR --> WOS_TT : miss → snap / hit → setup_hit

    WOS_TT --> DONE : T->done (time_travel/initial_cond)
    WOS_TT --> BND_DISP : !HIT_NONE (命中界面)
    WOS_TT --> WOS_CHK : else (继续 WoS 循环)

    %% ═══ Custom 转移 ═══
    CUSTOM --> DONE : path.at_limit
    CUSTOM --> BND_DISP : !HIT_NONE
```

### 3.5 对流路径详细状态图

```mermaid
stateDiagram-v2
    direction TB

    state "CNV_INIT ✅" as CNV_INIT
    state "CNV_INIT_TRACE 🔴" as CNV_TRACE
    note right of CNV_TRACE
        从流体内部启动时沿 +Z 发射 1 条射线
        源码 convective_Xd.h 107-138
    end note
    state "CNV_INIT_TRACE_RESULT ✅" as CNV_TR_RES
    state "CNV_SAMPLE_BOUNDARY ✅" as CNV_SAMP

    state "PATH_DONE" as DONE
    state "PATH_ERROR" as ERR
    state "BND_DISPATCH" as BND_DISP

    CNV_INIT --> DONE : T->done (流体温度已知)
    CNV_INIT --> CNV_SAMP : 已有 hit (从界面启动)
    CNV_INIT --> CNV_TRACE : HIT_NONE (从流体内部启动)

    CNV_TRACE --> CNV_TR_RES : 射线结果返回
    CNV_TR_RES --> ERR : HIT_NONE (无界流体, 错误)
    CNV_TR_RES --> CNV_SAMP : hit (设置 hit_side)

    CNV_SAMP --> DONE : T->done (time_rewind 到极限)
    CNV_SAMP --> BND_DISP : r < hc/hc_upper (真正对流事件)
    CNV_SAMP --> CNV_SAMP : else (null-collision, 继续循环)
```

---

## 4. 本地变量生命周期与 union 策略

每个状态分支有独立的局部变量集。在 CPU 上它们存在于函数调用栈中，显式状态机中需要持久化到 `struct explicit_path_state` 内。关键约束：

| 状态分支 | 需持久化的变量 | 源码定义位置 | 大小估算 |
|---|---|---|---|
| `bnd_ss` | `dir_frt/bck_samp/refl[3]×4`, `ray_frt/bck`, `step_frt/bck`, `enc_ids[2]`, `proba`, `lambda_frt/bck` | `solid_solid.h:40-80` | ~300 bytes |
| `bnd_sf` | `rwalk_snapshot`, `T_snapshot`, `p_conv/cond`, `h_hat`, `reinject_step`, `epsilon`, `Tref`, `r` | `picard1.h:90-150` | ~500 bytes |
| `bnd_sfn` | `sfn_stack[depth]`: rwalk_saved, T_saved, T_values[6], r, p_conv/cond/h_hat, return_state | `picardN.h:250-290` | ~600 bytes × depth |
| `bnd_ext` | `pos`, `dir`, `hit`, `incident_flux_direct`, `diffuse_reflected`, `scattered`, `nbounces`, `return_state` | `handle_flux.h:130-240` | ~200 bytes |
| `cnd_ds` | `enc_id`, `dir0/dir1[3]`, `hit0/hit1`, `delta`, `delta_solid`, `position_start[3]` | `delta_sphere_Xd.h:330-345` | ~200 bytes |
| `cnd_wos` | `query_pos`, `query_radius`, `dir`, `new_pos`, `cached_closest_hit`, `delta`, `props`, `last_distance` | `wos_Xd.h:630-680` | ~250 bytes |
| `cnv` | `enc`, `props_ref`, `hc_upper_bound` | `convective_Xd.h:185-200` | ~100 bytes |

由于路径同一时刻只处于一个分支，**这些局部变量可以放在 union 中**。但注意 `bnd_sfn`（picardN 栈帧）和 `bnd_ext`（外部通量）可能**同时活跃**（外部通量是 picard 的内嵌子过程），所以这两者**不能放在同一 union**。推荐结构：

```c
struct explicit_path_locals {
    union {                         /* 主路径 — 互斥 */
        struct bnd_ss_locals bnd_ss;
        struct bnd_sf_locals bnd_sf;
        struct cnd_ds_locals cnd_ds;
        struct cnd_wos_locals cnd_wos;
        struct cnv_locals cnv;
    };
    struct bnd_ext_locals bnd_ext;  /* 内嵌子过程 — 独立存储 */
    struct sfn_stack_frame sfn_stack[MAX_PICARD_DEPTH]; /* 递归栈 — 独立 */
};
```

总大小约 600 + 200 + 600×depth bytes/path。对于 depth=2, ~1.8 KB/path, 1M 路径 ~1.7 GB, 仍在 RTX 4090 (24 GB) 可接受范围内。

---

## 5. 实施顺序指导

基于上面 10 个示例（A-J）及其附注，建议按以下顺序实施：

| 阶段 | 目标 | 涉及的示例/附注 | 新增状态 | 可验证标准 |
|---|---|---|---|---|
| **Phase 0** | enclosure 查询子状态 | 示例 G | `ENC_QUERY_*` | 给定位置能正确返回 enc_id |
| **Phase 1** | 辐射路径 | 示例 A + find_next_fragment 重试附注 | `RAD_TRACE`, `RAD_BOUNCE_OR_ABSORB` | 纯辐射场景 GPU = CPU |
| **Phase 2** | 边界分派 + 对流路径 | 示例 B + F + Robin 后检查附注 + 启动射线附注 | `BND_DISPATCH`, `BND_POST_ROBIN_CHECK`, `CNV_*` | convective-only 场景验证 |
| **Phase 3** | solid/solid boundary | 示例 C | `SS_REINJECT_*` | solid/solid 场景验证 |
| **Phase 3.5** | 外部净通量 | 示例 I | `BND_EXT_*` (~8 状态) | picard1 + 外部源场景验证 |
| **Phase 4** | delta-sphere 导热 | 示例 D | `CND_DS_*` | conductive (delta-sphere) 场景验证 |
| **Phase 4b** | WoS 导热 | 示例 H | `CND_WOS_*` (~6 状态) | conductive (WoS) 场景验证 |
| **Phase 5** | picard1 null-collision | 示例 E | `SF_PROB_DISPATCH`, `SF_NULLCOLL_*` | 完整 coupled 场景验证 |
| **Phase 6** | picardN 递归栈 | 示例 J | `SFN_*` + 栈机制 (~7 状态) | picardN 场景验证 |
| **Phase 7** | 自定义导热（插件） | 示例 H-附 (custom 占位) | `CND_CUSTOM` | 按需扩展 |

**总状态数估算**: ~45-50 个 `enum path_state` 值，覆盖所有路径类型和子系统。

---

*文档结束 — 每个状态迁移的判断条件均已精确映射到 stardis-cpu 源码行号。覆盖模块: radiative, boundary (solid/solid, picard1, picardN), conductive (delta-sphere, WoS, custom), convective, external net flux, enclosure query, Robin post-check, find_next_fragment retry。*