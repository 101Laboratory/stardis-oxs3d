# Cascade 相位转换链与字段访问分析

**创建时间**: 2026-02-27  
**用途**: P1/P2 SoA 域分解的技术参考。记录每种路径类型在 cascade 内的相位转换链、no-ray 爆发长度、字段访问域。

---

## 0. Cascade 循环机制

[cascade_advance_single_path](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1706) 执行 `for(;;)` 循环，每次调用 `advance_one_step_no_ray(p)`, 直到以下任一条件满足退出:

| 退出条件 | 含义 |
|----------|------|
| `p->needs_ray` | 路径需要光追 |
| `p->phase == PATH_DONE \|\| PATH_ERROR` | 路径终止（若 sfn_stack_depth > 0 则拦截为 PicardN resume） |
| `path_phase_is_ray_pending(p->phase)` | 相位需要 batched ray result |
| `path_phase_is_enc_locate_pending(p->phase)` | 相位需要 enc_locate result |
| `path_phase_is_cp_pending(p->phase)` | 相位需要 closest_point result (WoS) |
| `!advanced` | step 函数未推进 |

### 修正的 cascade 行为模型

整个路径生命周期累计上万次 cascade 迭代，但中间频繁被 ray_query 插入。单次 cascade 爆发（`for(;;)` 从进入到跳出）长度按路径类型差异很大：

| 路径类型 | 典型爆发长度 | 中断类型 |
|----------|------------|----------|
| CND_DS（导热 DS） | **2 步** | needs_ray (2 delta-sphere rays) |
| CND_WOS（导热 WoS） | **3 步** | cp-pending |
| CNV（对流 null-collision） | **1×N 步**（无上限） | 无中断直到 accept |
| BND_SF（SF 边界掷硬币） | **3-8 步** | needs_ray |
| BND_SFN（PicardN Ti check） | **2-12 步** | 递归子路径 |
| BND_SS（SS 边界） | **3 步** | needs_ray (4 rays) |

---

## 1. 完整分发表

### 1.1 `advance_one_step_no_ray` 分发表

源码：[sdis_wf_steps_core.c:L703](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L703)

| Phase | Step 函数 | 文件:行 |
|-------|----------|---------|
| PATH_INIT | step_init | [sdis_wf_steps_core.c:L226](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L226) |
| PATH_COUPLED_BOUNDARY | step_boundary | [sdis_wf_steps_core.c:L409](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L409) |
| PATH_COUPLED_CONDUCTIVE | step_conductive | [sdis_wf_steps_core.c:L463](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L463) |
| PATH_COUPLED_CONVECTIVE | step_convective | [sdis_wf_steps_core.c:L653](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L653) |
| PATH_COUPLED_RADIATIVE | step_coupled_radiative_begin | [sdis_wf_steps_core.c:L661](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L661) |
| PATH_BND_DISPATCH | step_bnd_dispatch | [sdis_wf_steps_cnv.c:L377](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnv.c#L377) |
| PATH_BND_POST_ROBIN_CHECK | step_bnd_post_robin_check | [sdis_wf_steps_cnv.c:L445](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnv.c#L445) |
| PATH_BND_SS_REINJECT_ENC | step_bnd_ss_reinject_enc_result | [sdis_wf_steps_bnd_ss.c:L534](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L534) |
| PATH_BND_SS_REINJECT_DECIDE | step_bnd_ss_reinject_decide | [sdis_wf_steps_bnd_ss.c:L629](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L629) |
| PATH_BND_SF_REINJECT_ENC | step_bnd_sf_reinject_enc_result | [sdis_wf_steps_bnd_sf.c:L370](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c#L370) |
| PATH_BND_SF_PROB_DISPATCH | step_bnd_sf_prob_dispatch | [sdis_wf_steps_bnd_sf.c:L429](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c#L429) |
| PATH_BND_SF_NULLCOLL_DECIDE | step_bnd_sf_nullcoll_decide | [sdis_wf_steps_bnd_sf.c:L877](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c#L877) |
| PATH_BND_EXT_CHECK | step_bnd_ext_check | [sdis_wf_steps_bnd_ext.c:L80](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L80) |
| PATH_BND_EXT_FINALIZE | step_bnd_ext_finalize | [sdis_wf_steps_bnd_ext.c:L594](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L594) |
| PATH_BND_SFN_PROB_DISPATCH | step_bnd_sfn_prob_dispatch | [sdis_wf_steps_bnd_sfn.c:L183](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L183) |
| PATH_BND_SFN_RAD_DONE | step_bnd_sfn_rad_done | [sdis_wf_steps_bnd_sfn.c:L536](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L536) |
| PATH_BND_SFN_COMPUTE_Ti | step_bnd_sfn_compute_Ti | [sdis_wf_steps_bnd_sfn.c:L618](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L618) |
| PATH_BND_SFN_COMPUTE_Ti_RESUME | step_bnd_sfn_compute_Ti_resume | [sdis_wf_steps_bnd_sfn.c:L724](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L724) |
| PATH_BND_SFN_CHECK_PMIN_PMAX | step_bnd_sfn_check_pmin_pmax | [sdis_wf_steps_bnd_sfn.c:L777](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L777) |
| PATH_CND_DS_CHECK_TEMP | step_cnd_ds_check_temp | [sdis_wf_steps_cnd.c:L57](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L57) |
| PATH_CND_DS_STEP_ENC_VERIFY | step_cnd_ds_step_enc_verify | [sdis_wf_steps_cnd.c:L152](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L152) |
| PATH_CND_DS_STEP_ADVANCE | step_cnd_ds_step_advance | [sdis_wf_steps_cnd.c:L159](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L159) |
| PATH_CND_WOS_CHECK_TEMP | step_cnd_wos_check_temp | [sdis_wf_steps_cnd.c:L502](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L502) |
| PATH_CND_WOS_CLOSEST_RESULT | step_cnd_wos_closest_result | [sdis_wf_steps_cnd.c:L640](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L640) |
| PATH_CND_WOS_DIFFUSION_CHECK_RESULT | step_cnd_wos_diffusion_check_result | [sdis_wf_steps_cnd.c:L727](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L727) |
| PATH_CND_WOS_TIME_TRAVEL | step_cnd_wos_time_travel | [sdis_wf_steps_cnd.c:L806](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L806) |
| PATH_CNV_INIT | step_cnv_init | [sdis_wf_steps_cnv.c:L61](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnv.c#L61) |
| PATH_CNV_SAMPLE_LOOP | step_cnv_sample_loop | [sdis_wf_steps_cnv.c:L230](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnv.c#L230) |
| PATH_ENC_LOCATE_RESULT | step_enc_locate_result | [sdis_wf_steps_enc.c:L86](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L86) |

### 1.2 `advance_one_step_with_ray` 分发表

源码：[sdis_wf_steps_core.c:L909](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L909)

由 distribute 后调用，处理 ray-pending 相位。相位列表参见 P2 指南附录 A.2。

---

## 2. 按路径类型的相位转换链

### 2.1 CND_DS（导热 Delta-Sphere，M4）

```
COUPLED_CONDUCTIVE (DS, initialized)
  → CND_DS_CHECK_TEMP (temp NOT known)
  → ⛔ CND_DS_STEP_TRACE [needs_ray: 2 delta-sphere rays]
    ⟪ ray result ⟫
  → DS_STEP_ADVANCE (enc match, no boundary)
  → CND_DS_CHECK_TEMP (loop)
  → ⛔ [needs_ray]
  ... (上千次)
```

**ENC verify 子链（无前向命中时）**:
```
DS_STEP_ENC_VERIFY → ⛔ ENC_QUERY_EMIT [needs_ray: 6 enc rays]
  ⟪ ray result ⟫
  → enc_query.return_state = CND_DS_STEP_ADVANCE
  → CND_DS_CHECK_TEMP
```

**爆发长度**: 2 步 | **域**: core + cnd_ds (偶尔 +enc)

### 2.2 CND_WOS（导热 Walk-on-Spheres，M9）

```
COUPLED_CONDUCTIVE (WoS, initialized)
  → CND_WOS_CHECK_TEMP
  → ⛔ CND_WOS_CLOSEST [cp-pending]
    ⟪ closest_point result ⟫
  → CND_WOS_CLOSEST_RESULT
    → (ε-shell) → CND_WOS_TIME_TRAVEL → CND_WOS_CHECK_TEMP → ⛔
    → (diffuse) → ⛔ CND_WOS_DIFFUSION_CHECK [cp-pending]
      ⟪ CP2 result ⟫
      → (valid) → CND_WOS_TIME_TRAVEL → CHECK_TEMP → ⛔
      → (invalid) → ⛔ CND_WOS_FALLBACK_TRACE [needs_ray]
```

**爆发长度**: 3 步（ε-shell 路径）| **域**: core + locals(cnd_wos)

### 2.3 CNV（对流 null-collision，M6）

```
CNV_SAMPLE_LOOP → (null-coll reject) → CNV_SAMPLE_LOOP → ... × N
  → (accept) → BND_DISPATCH
```

**爆发长度**: 1×N（N = null-collision 拒绝次数，**无上限**）| **域**: core + locals(cnv)

### 2.4 BND_SF（SF 边界 Picard1，M5）

```
COUPLED_BOUNDARY → BND_DISPATCH → (SF picard1)
  → ⛔ BND_SF_REINJECT_SAMPLE [needs_ray: 2 rays]
    ⟪ ray result ⟫
  → SF_PROB_DISPATCH (first entry: h_hat=0)
    → BND_EXT_CHECK → (no flux) → SF_PROB_DISPATCH
    → (convective) → BND_POST_ROBIN_CHECK → COUPLED_CONVECTIVE
      → CNV_INIT → (temp known) → PATH_DONE
    → (conductive) → COUPLED_CONDUCTIVE → ⛔
    → (radiative) → ⛔ SF_NULLCOLL_RAD_TRACE [needs_ray]
```

**爆发长度**: 3-8 步 | **域**: core + bnd + locals(bnd_sf) + ext

### 2.5 BND_SFN（PicardN，M8）

```
SFN_RAD_DONE → SFN_COMPUTE_Ti
  → (T known) → SFN_CHECK_PMIN_PMAX
    → (accept) → BND_POST_ROBIN_CHECK
    → (reject) → SFN_PROB_DISPATCH (null-coll loop)
    → (need more) → SFN_COMPUTE_Ti
      → (push stack) → COUPLED_BOUNDARY (递归子路径)
        ... 整个子路径在同一 cascade for(;;) 内执行 ...
      → PATH_DONE (拦截) → SFN_COMPUTE_Ti_RESUME
        → SFN_CHECK_PMIN_PMAX → ...
```

**爆发长度**: 2-12 步（Ti check），递归时可更长 | **域**: core + locals(bnd_sf) + sfn

### 2.6 BND_SS（SS 边界，M3）

```
COUPLED_BOUNDARY → BND_DISPATCH → (SS)
  → ⛔ BND_SS_REINJECT_SAMPLE [needs_ray: 4 rays]
    ⟪ ray result ⟫
  → SS_REINJECT_DECIDE → COUPLED_CONDUCTIVE → ...
```

**爆发长度**: 3 步 | **域**: core + bnd + locals(bnd_ss)

### 2.7 EXT（外部通量子链，M7）

```
BND_EXT_CHECK → (有外部通量)
  → ⛔ BND_EXT_DIRECT_TRACE [needs_ray: shadow ray]
  → ⛔ BND_EXT_DIFFUSE_TRACE [needs_ray]
    → (miss) → BND_EXT_FINALIZE → return_state
    → ⛔ BND_EXT_DIFFUSE_SHADOW_TRACE [needs_ray]
```

**爆发长度**: 大部分为 ray-bound | **域**: core + ext

---

## 3. path_state 子结构体尺寸参考

| 结构体 | 大小 |
|--------|------|
| `sdis_rwalk_vertex` | 32B |
| `s3d_hit` | 56B |
| `rwalk` | 176B |
| `rwalk_context` | 96B |
| `temperature` | 24B |
| `sdis_heat_vertex` | 48B |
| `solid_props` | 56B |
| `hit_filter_data` | 144B |
| `wf_rng` | 104B |
| `source_props` | 56B |
| `source_sample` | 56B |
| `path_ray_request` | 64B |
| `path_bnd_sf_locals` | ~928B |

### path_state 总计 ~6688B 构成

| 区域 | 大小 | 占比 |
|------|------|------|
| identity + lifecycle | 20B | 0.3% |
| rwalk + ctx + T | 296B | 4.4% |
| rad_* scratch | 20B | 0.3% |
| ds_* scratch | 232B | 3.5% |
| bnd_* scratch | 128B | 1.9% |
| filter_data_storage | 144B | 2.2% |
| ray_req + needs_ray | 68B | 1.0% |
| rng* + rng_state | 112B | 1.7% |
| union locals | 928B | 13.9% |
| ext_flux | 360B | 5.4% |
| enc_query + enc_locate | 592B | 8.8% |
| **sfn_stack[3]** | **3696B** | **55.3%** |
| 其余 | ~92B | 1.4% |
