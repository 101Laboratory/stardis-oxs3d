# [picard] 状态迁移映射

> 本文件是 [explicit_state_machine_transition_mapping.md](../explicit_state_machine_transition_mapping.md) 的picard子模块

## 1. 示例 E：solid_fluid picard1 — null-collision 循环

### 1.1 CPU 原始代码 (`sdis_heat_path_boundary_Xd_solid_fluid_picard1.h:229-310`)

```c
/* null-collision 循环 — 这是 picard1 中最关键的部分 */
for(;;) {
    double h_radi, p_radi;

    r = ssp_rng_canonical(rng);                         // [picard1.h:240]

    /* [picard1.h:243] 进入对流 */
    if(r < p_conv) {
        T->func = convective_path;                      // → PATH_CNV_INIT
        rwalk->enc_id = enc_ids[fluid_side];
        break;
    }

    /* [picard1.h:250] 进入导热 */
    if(r < p_conv + p_cond) {
        solid_reinjection(scn, enc_ids[solid_side], &args);  // ✅ + 内部判断
        break;
        // → PATH_CND_INIT_ENC 或 PATH_BND_DISPATCH (取决于 solid_reinjection 判断)
    }

    /* [picard1.h:264-272] 🔴 采样辐射路径 */
    T_s = *T;
    rwalk_s = *rwalk;
    rwalk_s.enc_id = enc_ids[fluid_side];
    res = radiative_path(scn, ctx, &rwalk_s, rng, &T_s);
    // ^^^ 🔴 内部多次 trace_ray（整条辐射子路径）

    /* [picard1.h:275-276] ✅ 获取参考温度 */
    rwalk_get_Tref(scn, &rwalk_s, &T_s, &Tref_s);

    /* [picard1.h:283-287] ✅ 计算实际辐射系数 */
    h_radi = BOLTZMANN_CONSTANT * epsilon *
      (Tref³ + Tref²*Tref_s + Tref*Tref_s² + Tref_s³);
    p_radi = h_radi / h_hat;

    /* [picard1.h:289-293] 判断: 接受辐射路径？ */
    if(r < p_conv + p_cond + p_radi) {
        *rwalk = rwalk_s;                               // 接受此辐射路径
        *T = T_s;
        break;
        // → 状态由 T_s 决定（可能是 BND_DISPATCH, DONE 等）
    }
    // else: null-collision → 继续循环
}
```

### 1.2 显式状态拆分

```c
/* ========== 状态 PATH_BND_SF_PROB_DISPATCH ========== */
/* null-collision 循环入口：根据概率决定 conv/cond/rad */
/* 源码: picard1.h:239-243 */
case PATH_BND_SF_PROB_DISPATCH: {
    double r = rng_canonical(&p->rng);
    p->local.bnd_sf.r = r;   /* 保存随机数用于后续判断 */

    /* --- 判断 1: 进入对流？ --- */
    /* 源码: picard1.h:243 */
    /* 原始代码: if(r < p_conv) → T->func = convective_path */
    if(r < p->local.bnd_sf.p_conv) {
        p->rwalk.enc_id = p->local.bnd_sf.enc_ids[p->local.bnd_sf.fluid_side];
        p->rwalk.hit_side = p->local.bnd_sf.fluid_side;
        p->state = PATH_CNV_INIT;                       // → 对流路径
        return;
    }

    /* --- 判断 2: 进入导热？ --- */
    /* 源码: picard1.h:250 */
    /* 原始代码: if(r < p_conv + p_cond) → solid_reinjection */
    if(r < p->local.bnd_sf.p_conv + p->local.bnd_sf.p_cond) {
        /* ✅ solid_reinjection 纯计算部分 */
        solid_reinjection_compute(p, p->local.bnd_sf.enc_ids[p->local.bnd_sf.solid_side],
                                  &p->local.bnd_sf.reinject_step);
        if(p->done) {
            p->state = PATH_DONE;
        } else {
            /* solid_reinjection 的 CND/BND 判断 */
            if(p->local.bnd_sf.reinject_step.hit.distance !=
               p->local.bnd_sf.reinject_step.distance) {
                p->state = PATH_CND_INIT_ENC;           // → 导热
            } else {
                p->state = PATH_BND_DISPATCH;            // → 边界
            }
        }
        return;
    }

    /* 既不是对流也不是导热 → 需要采样辐射路径来决定 */
    /* 保存当前状态作为 snapshot (用于 null-collision 拒绝时恢复) */
    p->local.bnd_sf.rwalk_snapshot = p->rwalk;
    p->local.bnd_sf.T_snapshot = p->temperature_value;

    /* 准备辐射子路径 */
    p->local.bnd_sf.rwalk_s = p->rwalk;
    p->local.bnd_sf.rwalk_s.enc_id = p->local.bnd_sf.enc_ids[p->local.bnd_sf.fluid_side];
    p->local.bnd_sf.rwalk_s.hit_side = p->local.bnd_sf.fluid_side;

    /* 🔴 发射辐射路径的第一条射线 */
    /* 源码: picard1.h:264 → radiative_path → trace_radiative_path → find_next_fragment → trace_ray */
    /* 这里启动一个"子状态机"：辐射路径自身的 RAD_TRACE 循环 */
    p->state = PATH_BND_SF_NULLCOLL_RAD_TRACE;

    /* 复用辐射路径逻辑，但标记为 null-collision 上下文 */
    prepare_rad_trace_for_nullcoll(p);
    return;
}

/* ========== 状态 PATH_BND_SF_NULLCOLL_RAD_TRACE ========== */
/* 和 PATH_RAD_TRACE 逻辑完全相同，但完成后回到 NULLCOLL_DECIDE */
/* 源码: 复用 trace_radiative_path_3d 的逻辑 */
case PATH_BND_SF_NULLCOLL_RAD_TRACE: {
    /* 🔴 发射射线（同 PATH_RAD_TRACE） */
    /* ... */
    p->state = PATH_BND_SF_NULLCOLL_RAD_RESULT;
    return;
}

/* ========== 状态 PATH_BND_SF_NULLCOLL_DECIDE ========== */
/* 辐射子路径完成后，计算 h_radi，决定接受/拒绝 */
/* 源码: picard1.h:275-300 */
case PATH_BND_SF_NULLCOLL_DECIDE: {
    /* ✅ 获取 Tref */
    /* 源码: picard1.h:275-276 */
    double Tref_s;
    rwalk_get_Tref(scn, &p->local.bnd_sf.rwalk_s, &Tref_s);

    /* ✅ 计算实际 h_radi */
    /* 源码: picard1.h:283-287 */
    double Tref = p->local.bnd_sf.Tref;
    double h_radi = BOLTZMANN_CONSTANT * p->local.bnd_sf.epsilon *
        (Tref*Tref*Tref + Tref*Tref*Tref_s + Tref*Tref_s*Tref_s + Tref_s*Tref_s*Tref_s);
    double p_radi = h_radi / p->local.bnd_sf.h_hat;

    /* --- 判断: 接受辐射路径？ --- */
    /* 源码: picard1.h:289 */
    /* 原始代码: if(r < p_conv + p_cond + p_radi) */
    if(p->local.bnd_sf.r < p->local.bnd_sf.p_conv + p->local.bnd_sf.p_cond + p_radi) {
        /* 接受：采用辐射子路径的状态 */
        p->rwalk = p->local.bnd_sf.rwalk_s;
        p->temperature_value = p->local.bnd_sf.T_s_value;
        /* 辐射子路径的终止状态决定了接下来去哪 */
        /* (可能是 DONE, BND_DISPATCH 等) */
        p->state = determine_state_from_rad_subpath(p);
        return;
    }

    /* 拒绝（null-collision）→ 恢复 snapshot 并重新循环 */
    /* 源码: picard1.h:297 → 回到 for(;;) 循环顶部 */
    p->rwalk = p->local.bnd_sf.rwalk_snapshot;
    p->temperature_value = p->local.bnd_sf.T_snapshot;
    p->state = PATH_BND_SF_PROB_DISPATCH;               // 回到循环顶部
    return;
}
```

**关键映射表 — picard1 null-collision**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 选择对流 | `picard1.h:243` | `r < p_conv` | `SF_PROB_DISPATCH` | `PATH_CNV_INIT` |
| 选择导热 | `picard1.h:250` | `r < p_conv + p_cond` | `SF_PROB_DISPATCH` | `CND_INIT_ENC`/`BND_DISPATCH` |
| 需要采样辐射 | `picard1.h:264` | `r >= p_conv + p_cond` | `SF_PROB_DISPATCH` | `SF_NULLCOLL_RAD_TRACE` |
| 辐射子路径中光线逃逸 | `radiative_Xd.h:235` | `HIT_NONE` | `SF_NULLCOLL_RAD_RESULT` | `SF_NULLCOLL_DECIDE` |
| 辐射子路径中吸收 | `radiative_Xd.h:274` | `r < emissivity` | `SF_NULLCOLL_RAD_RESULT` | `SF_NULLCOLL_DECIDE` |
| 接受辐射路径 | `picard1.h:289` | `r < p_conv+p_cond+p_radi` | `SF_NULLCOLL_DECIDE` | (由子路径决定) |
| 拒绝（null-collision） | `picard1.h:297` | `r >= p_conv+p_cond+p_radi` | `SF_NULLCOLL_DECIDE` | `SF_PROB_DISPATCH` |

---

## 1b. 示例 I：handle_external_net_flux — 外部源净通量子系统

> **调用上下文**: 由 `solid_fluid_boundary_picard1_path`（`picard1.h:175`）和 `solid_fluid_boundary_picardN_path` 在 reinjection 采样后调用。本身不产生状态迁移（不改变 `T->func`），但**内部包含多条 trace_ray**，必须拆分为挂起点。

### 1b.1 CPU 原始代码 (`sdis_heat_path_boundary_Xd_handle_external_net_flux.h:248-390`)

```c
/* handle_external_net_flux_3d */

/* [handle_flux.h:300-311] ✅ 检查是否需要处理外部通量 */
handle_flux = interface_side_is_external_flux_handled(interf, &frag);
handle_flux = handle_flux && (scn->source != NULL);
if(!handle_flux) goto exit;                             // → 什么都不做，返回

/* [handle_flux.h:319-321] ✅ 获取发射率 */
emissivity = interface_side_get_emissivity(interf, src_id, &frag);
if(emissivity == 0) goto exit;                          // → 什么都不做，返回

/* [handle_flux.h:327] ✅ 采样光源方向 */
res = source_sample(scn->source, &src_props, rng, frag.P, &src_sample);

/* [handle_flux.h:333-338] 🔴 direct_contribution (shadow ray #1) */
cos_theta = d3_dot(N, src_sample.dir);
if(cos_theta > 0) {
    Ld = direct_contribution(scn, &src_sample, pos, enc_id, hit); // 🔴 trace_ray
    incident_flux_direct = cos_theta * Ld / src_sample.pdf;
}

/* [handle_flux.h:341-343] 🔴 compute_incident_diffuse_flux */
res = compute_incident_diffuse_flux(scn, rng, &src_props, frag.P, N,
    frag.time, enc_ids[frag.side], hit, &incident_flux_diffuse);
/*
 * compute_incident_diffuse_flux 内部 [handle_flux.h:128-230]:
 *   初始: 采样余弦加权方向 dir
 *   for(;;) { // 漫反射弹跳循环
 *     🔴 find_next_fragment(pos, dir) → hit, interf, frag     [handle_flux.h:165]
 *     if(HIT_NONE): { scattered = PI; break; }                [handle_flux.h:173]
 *     ✅ brdf_setup + 吸收检查
 *     ✅ brdf_sample → bounce direction
 *     if(specular): 🔴 direct_contribution (shadow ray)       [handle_flux.h:201]
 *     if(diffuse):  source_sample + 🔴 direct_contribution    [handle_flux.h:214]
 *     → 继续循环
 *   }
 */

/* [handle_flux.h:347-371] ✅ 纯计算: incident_flux → net_flux → T->value += */
```

### 6b.2 显式状态机映射

```c
/* advance_bnd_ext_check() — BND_EXT_CHECK 状态 */
void advance_bnd_ext_check(struct explicit_path* p) {
    /* 源码: handle_flux.h:300-311 */
    int handle_flux = interface_side_is_external_flux_handled(interf, &frag)
                   && (scn->source != NULL);
    if(!handle_flux || emissivity == 0) {
        /* 不需要外部通量处理 → 返回调用者状态 */
        p->state = p->local.bnd_ext.return_state;
        return;
    }

    /* 采样光源方向 + 准备 shadow ray */
    source_sample(scn->source, &src_props, &p->rng, frag.P, &src_sample);
    double cos_theta = d3_dot(N, src_sample.dir);

    if(cos_theta <= 0) {
        /* 光源在界面背面 → 跳过直接贡献，进入漫反射 */
        p->local.bnd_ext.incident_flux_direct = 0;
        p->state = PATH_BND_EXT_DIFFUSE_INIT;
        return;
    }

    /* 🔴 发射 shadow ray #1: direct contribution */
    /* 源码: handle_flux.h:93-101 → direct_contribution → trace_ray */
    setup_shadow_ray(p, frag.P, src_sample.dir, src_sample.dst,
                     enc_ids[frag.side], hit);
    p->n_pending_rays = 1;
    p->state = PATH_BND_EXT_DIRECT_RESULT;
    return;
}

/* advance_bnd_ext_direct_result() — shadow ray #1 结果 */
void advance_bnd_ext_direct_result(struct explicit_path* p) {
    /* 源码: handle_flux.h:97-98 */
    if(!SXD_HIT_NONE(&p->ray_result.hit)) {
        p->local.bnd_ext.incident_flux_direct = 0;     /* 被遮挡 */
    } else {
        double Ld = src_sample.radiance_term;
        p->local.bnd_ext.incident_flux_direct =
            cos_theta * Ld / src_sample.pdf;
    }

    /* 进入漫反射通量计算 */
    p->state = PATH_BND_EXT_DIFFUSE_INIT;
    return;
}

/* advance_bnd_ext_diffuse_init() — 初始化漫反射弹跳循环 */
void advance_bnd_ext_diffuse_init(struct explicit_path* p) {
    /* 源码: handle_flux.h:136-142 */
    /* 采样余弦加权半球方向 — 初始化循环状态 */
    ssp_ran_hemisphere_cos(&p->rng, N, p->local.bnd_ext.dir, NULL);
    p->local.bnd_ext.diffuse_reflected = 0;
    p->local.bnd_ext.nbounces = 0;
    d3_set(p->local.bnd_ext.pos, frag.P);
    p->local.bnd_ext.hit = *hit;

    /* 🔴 发射第一条漫反射弹跳射线 (find_next_fragment) */
    /* 源码: handle_flux.h:165 */
    setup_trace_ray(p, p->local.bnd_ext.pos, p->local.bnd_ext.dir,
                    INF, enc_id, &p->local.bnd_ext.hit);
    p->n_pending_rays = 1;
    p->state = PATH_BND_EXT_DIFFUSE_TRACE_RESULT;
    return;
}

/* advance_bnd_ext_diffuse_trace_result() — 漫反射弹跳射线结果 */
void advance_bnd_ext_diffuse_trace_result(struct explicit_path* p) {
    struct sXd(hit)* hit = &p->ray_result.hit;

    /* --- 判断 1: miss — 光线逃逸到环境 --- */
    /* 源码: handle_flux.h:173-178 */
    if(SXD_HIT_NONE(hit)) {
        p->local.bnd_ext.scattered = PI;
        d3_set(p->local.bnd_ext.scatter_dir, p->local.bnd_ext.dir);
        /* 整个漫反射计算完成 → 归总 */
        p->state = PATH_BND_EXT_FINALIZE;
        return;
    }

    /* hit → 获取界面属性，检查吸收 */
    /* 源码: handle_flux.h:180-189 */
    d3_set(p->local.bnd_ext.pos, frag.P);
    brdf_setup(scn->dev, &brdf_setup_args, &brdf);

    /* --- 判断 2: 被吸收 --- */
    if(ssp_rng_canonical(&p->rng) < brdf.emissivity) {
        p->local.bnd_ext.scattered = 0;
        p->state = PATH_BND_EXT_FINALIZE;
        return;
    }

    /* 反射 → 采样弹跳方向 */
    brdf_sample(&brdf, &p->rng, wi, N, &bounce);
    d3_set(p->local.bnd_ext.dir, bounce.dir);

    /* --- 判断 3: specular 或 diffuse 弹跳 --- */
    /* 源码: handle_flux.h:197-220 */
    /* 两者都需要 shadow ray 来计算 direct_contribution */
    if(bounce.cpnt == BRDF_SPECULAR) {
        source_trace_to(scn->source, &src_props,
            p->local.bnd_ext.pos, bounce.dir, &samp);
    } else {
        source_sample(scn->source, &src_props, &p->rng,
            p->local.bnd_ext.pos, &samp);
    }

    /* 🔴 发射 shadow ray (弹跳处直接贡献) */
    if(!SOURCE_SAMPLE_NONE(&samp)) {
        setup_shadow_ray(p, p->local.bnd_ext.pos, samp.dir, samp.dst,
                         enc_id, hit);
        p->n_pending_rays = 1;
        p->state = PATH_BND_EXT_DIFFUSE_SHADOW_RESULT;
    } else {
        /* 无光源样本 → 继续下一次弹跳 */
        p->state = PATH_BND_EXT_DIFFUSE_NEXT_BOUNCE;
    }
    return;
}

/* advance_bnd_ext_diffuse_shadow_result() — 弹跳处 shadow ray 结果 */
void advance_bnd_ext_diffuse_shadow_result(struct explicit_path* p) {
    /* 源码: handle_flux.h:201-214 */
    if(!SXD_HIT_NONE(&p->ray_result.hit)) {
        /* 被遮挡 → 无直接贡献 */
    } else {
        double L = src_sample.radiance_term;
        if(bounce_cpnt == BRDF_DIFFUSE)
            L = L * cos_theta / (PI * samp.pdf);
        p->local.bnd_ext.diffuse_reflected += L;
    }

    p->state = PATH_BND_EXT_DIFFUSE_NEXT_BOUNCE;
    return;
}

/* advance_bnd_ext_diffuse_next_bounce() — 继续弹跳循环或发射新射线 */
void advance_bnd_ext_diffuse_next_bounce(struct explicit_path* p) {
    /* 🔴 发射下一条弹跳射线 (find_next_fragment) */
    /* 源码: handle_flux.h:165 → 回到 for(;;) 顶部 */
    setup_trace_ray(p, p->local.bnd_ext.pos, p->local.bnd_ext.dir,
                    INF, enc_id, &p->local.bnd_ext.hit);
    p->n_pending_rays = 1;
    p->state = PATH_BND_EXT_DIFFUSE_TRACE_RESULT;  /* 回到弹跳循环 */
    return;
}

/* advance_bnd_ext_finalize() — 汇总计算 */
void advance_bnd_ext_finalize(struct explicit_path* p) {
    /* 源码: handle_flux.h:347-371 — 全是 ✅ 纯计算 */
    double incident_flux = p->local.bnd_ext.incident_flux_direct
                         + p->local.bnd_ext.diffuse_reflected * PI;
    double net_flux = incident_flux * emissivity;
    p->T.value += net_flux / sum_h * src_props.power;
    /* ... scattered 部分同理 ... */

    /* 返回调用者状态 */
    p->state = p->local.bnd_ext.return_state;
    return;
}
```

**关键映射表 — 外部净通量**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 不需要外部通量 | `handle_flux.h:300` | `!handle_flux \|\| emissivity==0` | `BND_EXT_CHECK` | (return_state) |
| 光源在背面 | `handle_flux.h:333` | `cos_theta <= 0` | `BND_EXT_CHECK` | `BND_EXT_DIFFUSE_INIT` |
| shadow ray #1 被遮挡 | `handle_flux.h:97` | `!HIT_NONE(shadow_hit)` | `BND_EXT_DIRECT_RESULT` | `BND_EXT_DIFFUSE_INIT` |
| shadow ray #1 可见 | `handle_flux.h:97` | `HIT_NONE(shadow_hit)` | `BND_EXT_DIRECT_RESULT` | `BND_EXT_DIFFUSE_INIT` |
| 漫反射 miss（逃逸） | `handle_flux.h:173` | `SXD_HIT_NONE(hit)` | `DIFFUSE_TRACE_RESULT` | `BND_EXT_FINALIZE` |
| 漫反射吸收 | `handle_flux.h:190` | `random < emissivity` | `DIFFUSE_TRACE_RESULT` | `BND_EXT_FINALIZE` |
| 漫反射反射（需 shadow ray） | `handle_flux.h:197` | `else` | `DIFFUSE_TRACE_RESULT` | `DIFFUSE_SHADOW_RESULT` |
| 弹跳 shadow ray 完成 | `handle_flux.h:201-214` | (always) | `DIFFUSE_SHADOW_RESULT` | `DIFFUSE_NEXT_BOUNCE` |
| 继续弹跳循环 | `handle_flux.h:165` | (always) | `DIFFUSE_NEXT_BOUNCE` | `DIFFUSE_TRACE_RESULT` |

> **GPU wavefront 注意**: 外部净通量的漫反射弹跳循环可能产生多次 trace_ray（每次弹跳一条 find_next_fragment + 一条 shadow ray），弹跳次数不定。这与辐射路径类似，适合 refilling 策略。整个外部通量子系统是 picard1/picardN 的**内嵌子过程**，完成后返回主 null-collision 循环的调用点。

---

## 1c. 示例 J：solid_fluid_boundary_picardN — 递归 COMPUTE_TEMPERATURE 栈

> **与 picard1 的核心差异**: picardN 中辐射系数 `h_radi` 不再用固定上/下界，而是通过 Stefan-Boltzmann 定律精确计算 $h_{radi} = \sigma(T_0 T_1 T_2 + T_0 T_1 T_3 + T_0 T_3 T_4 + T_3 T_4 T_5)$。其中 T0-T5 各需独立采样一条完整路径（递归调用 `sample_coupled_path`）。

### 1c.1 CPU 原始代码 (`sdis_heat_path_boundary_Xd_solid_fluid_picardN.h:260-420`)

```c
/* null-collision 主循环 (picardN) */
for(;;) {
    struct temperature T_s;
    struct rwalk rwalk_s;
    double T0, T1, T2, T3, T4, T5;

    r = ssp_rng_canonical(rng);                     // [picardN.h:290]

    /* [picardN.h:293] 进入对流 */
    if(r < p_conv) { T->func = convective_path; break; }

    /* [picardN.h:300] 进入导热 */
    if(r < p_conv + p_cond) { solid_reinjection; break; }

    /* [picardN.h:324-328] 🔴 采样辐射路径 (radiative_path) */
    T_s = *T;  rwalk_s = *rwalk;
    rwalk_s.enc_id = enc_ids[fluid_side];
    res = radiative_path(scn, ctx, &rwalk_s, rng, &T_s);
    /* ^^^ 辐射子路径完成: T_s.done 或 rwalk_s 停在某界面 */

    h_radi_min = BOLTZMANN * Tmin^3; p_radi_min = h_radi_min / h_hat;

    /* [picardN.h:339] 提前接受检查 */
    if(r < p_conv + p_cond + p_radi_min) { accept_radiative; break; }

    /* ─── 以下每一步 COMPUTE_TEMPERATURE 都可能递归 ─── */

    /* [picardN.h:355] T0: sample_path at rwalk_s (辐射终点) */
    COMPUTE_TEMPERATURE(T0, &rwalk_s, &T_s);  // 🔴🔴 递归 sample_coupled_path
    h_radi_min = σ(Tmin³ + 3Tmin²·T0);
    h_radi_max = σ(That³ + 3That²·T0);
    CHECK_PMIN_PMAX;                           // 可能提前接受/拒绝

    /* [picardN.h:361] T1 */
    COMPUTE_TEMPERATURE(T1, &rwalk_s, &T_s);  // 🔴🔴
    CHECK_PMIN_PMAX;

    /* [picardN.h:367] T2 */
    COMPUTE_TEMPERATURE(T2, &rwalk_s, &T_s);  // 🔴🔴
    CHECK_PMIN_PMAX;

    /* [picardN.h:373] T3: sample_path at rwalk (界面位置) */
    COMPUTE_TEMPERATURE(T3, rwalk, T);         // 🔴🔴 注意: 用 rwalk, 不是 rwalk_s
    CHECK_PMIN_PMAX;

    /* [picardN.h:379] T4 */
    COMPUTE_TEMPERATURE(T4, rwalk, T);         // 🔴🔴
    CHECK_PMIN_PMAX;

    /* [picardN.h:385] T5 */
    COMPUTE_TEMPERATURE(T5, rwalk, T);         // 🔴🔴
    /* 最终精确 h_radi 判断 */
    h_radi = σ(T3·T4·T5 + T0·T3·T4 + T0·T1·T3 + T0·T1·T2);
    p_radi = h_radi * ε / h_hat;
    if(r < p_cond + p_conv + p_radi) { accept; break; }
    else { null_collision; continue; }
}
```

### 1c.2 COMPUTE_TEMPERATURE 宏展开 (`picardN.h:346-355`)

```c
/* 宏定义 */
#define COMPUTE_TEMPERATURE(Result, RWalk, Temp) {
    struct temperature T_p;
    if((Temp)->done) {                        // 辐射终点已有环境温度
        T_p = *(Temp);                        // ✅ 直接复用
    } else {
        res = sample_path(scn, RWalk, ctx, rng, &T_p);  // 🔴🔴 递归!
        // sample_path → sample_coupled_path → 完整路径求解
    }
    Result = T_p.value;
}
```

> **GPU 并行化难点**: 每个 `COMPUTE_TEMPERATURE` 调用可能触发一条**完整的嵌套路径**（radiative → boundary → conductive → ...），在 CPU 上靠函数调用栈自动保存上下文。GPU wavefront 中需要**显式栈**。

### 6c.3 显式状态机映射 — 栈机制

```c
/* picardN 需要在 explicit_path 中维护一个轻量级栈 */
struct explicit_path {
    /* ... 其他字段 ... */

    /* picardN 递归栈 */
    struct sfn_stack_frame {
        struct rwalk rwalk_saved;         /* 每层递归保存的 rwalk */
        double T_value_saved;             /* 保存的温度累加值 */
        double T_values[6];               /* T0-T5 */
        int next_Ti;                      /* 下一个要计算的 Ti 索引 (0-5) */
        double r;                         /* 保存的随机数 */
        double p_conv, p_cond, h_hat;     /* 概率参数 */
        int return_state;                 /* 该层完成后的返回状态 */
    } sfn_stack[MAX_PICARD_DEPTH];        /* 栈帧数组 */
    int sfn_stack_depth;                  /* 当前栈深度 */
};

/* advance_sfn_prob_dispatch() — picardN 概率分派 */
void advance_sfn_prob_dispatch(struct explicit_path* p) {
    struct sfn_stack_frame* frame = &p->sfn_stack[p->sfn_stack_depth];
    double r = frame->r;

    /* --- 判断 1: 对流？ --- */
    /* 源码: picardN.h:293 */
    if(r < frame->p_conv) {
        p->state = PATH_CNV_INIT;
        return;
    }

    /* --- 判断 2: 导热？ --- */
    /* 源码: picardN.h:300 */
    if(r < frame->p_conv + frame->p_cond) {
        /* solid_reinjection + conductive */
        p->state = PATH_SFN_SOLID_REINJECT;
        return;
    }

    /* --- 开始辐射路径采样 → 然后进入 COMPUTE_TEMPERATURE 链 --- */
    /* 保存当前状态快照 */
    frame->rwalk_saved = p->rwalk;
    frame->T_value_saved = p->T.value;
    frame->next_Ti = 0;

    /* 设置 rwalk_s = rwalk 到 fluid 侧 */
    p->rwalk.enc_id = enc_ids[fluid_side];
    p->rwalk.hit_side = fluid_side;

    /* 🔴 开始辐射子路径 (与 picard1 相同) */
    p->state = PATH_SFN_RAD_TRACE;
    return;
}

/* advance_sfn_rad_done() — 辐射子路径完成后 */
void advance_sfn_rad_done(struct explicit_path* p) {
    struct sfn_stack_frame* frame = &p->sfn_stack[p->sfn_stack_depth];

    /* 保存辐射终点的 rwalk_s 和 T_s */
    frame->rwalk_s = p->rwalk;
    frame->T_s = p->T;

    /* 恢复主 rwalk/T */
    p->rwalk = frame->rwalk_saved;
    p->T.value = frame->T_value_saved;

    /* CHECK_PMIN_PMAX 初始检查 */
    double h_radi_min = BOLTZMANN * Tmin3;
    double p_radi_min = h_radi_min / frame->h_hat;
    if(frame->r < frame->p_conv + frame->p_cond + p_radi_min) {
        /* 提前接受辐射路径 [picardN.h:339] */
        p->rwalk = frame->rwalk_s;
        p->T = frame->T_s;
        p->state = frame->return_state;
        return;
    }

    /* 开始 COMPUTE_TEMPERATURE 链 */
    frame->next_Ti = 0;
    p->state = PATH_SFN_COMPUTE_Ti;
    return;
}

/* advance_sfn_compute_Ti() — 计算第 next_Ti 个温度 */
void advance_sfn_compute_Ti(struct explicit_path* p) {
    struct sfn_stack_frame* frame = &p->sfn_stack[p->sfn_stack_depth];
    int i = frame->next_Ti;

    /* 确定从哪个位置采样 (T0-T2: rwalk_s, T3-T5: rwalk) */
    struct rwalk* sample_rwalk = (i < 3) ? &frame->rwalk_s : &frame->rwalk_saved;
    struct temperature* sample_T = (i < 3) ? &frame->T_s : &frame->T_saved_main;

    /* --- 判断: T_s.done? 如果辐射终点已有温度则直接复用 --- */
    /* 源码: picardN.h:346 COMPUTE_TEMPERATURE 宏 */
    if(sample_T->done) {
        frame->T_values[i] = sample_T->value;
        frame->next_Ti = i + 1;
        p->state = PATH_SFN_CHECK_PMIN_PMAX;
        return;
    }

    /* 否则需要递归采样 → 压栈! */
    /* 保存当前帧的进度 */
    frame->next_Ti = i;

    /* 压入新栈帧 */
    p->sfn_stack_depth++;
    struct sfn_stack_frame* new_frame = &p->sfn_stack[p->sfn_stack_depth];
    /* 初始化子路径: 从 sample_rwalk 开始的完整 sample_coupled_path */
    new_frame->return_state = PATH_SFN_COMPUTE_Ti_RESUME;

    /* 设置 rwalk 和 T 为子路径起点 */
    p->rwalk = *sample_rwalk;
    p->T.value = 0;
    p->T.done = 0;
    p->state = PATH_BND_DISPATCH;  /* 子路径从 boundary_path 开始 */
    return;
}

/* advance_sfn_compute_Ti_resume() — 子路径返回后恢复 */
void advance_sfn_compute_Ti_resume(struct explicit_path* p) {
    /* 弹出栈帧 */
    p->sfn_stack_depth--;
    struct sfn_stack_frame* frame = &p->sfn_stack[p->sfn_stack_depth];

    /* 子路径结果 = p->T.value */
    frame->T_values[frame->next_Ti] = p->T.value;
    frame->next_Ti++;

    /* 恢复本层的 rwalk/T */
    p->rwalk = frame->rwalk_saved;
    p->T.value = frame->T_value_saved;

    p->state = PATH_SFN_CHECK_PMIN_PMAX;
    return;
}

/* advance_sfn_check_pmin_pmax() — 执行 CHECK_PMIN_PMAX */
void advance_sfn_check_pmin_pmax(struct explicit_path* p) {
    struct sfn_stack_frame* frame = &p->sfn_stack[p->sfn_stack_depth];
    int i = frame->next_Ti;

    /* 基于已有 T0..T(i-1) 计算 h_radi_min/max */
    /* 源码: picardN.h:355-385 — 每步的公式不同 */
    double h_radi_min, h_radi_max, p_radi_min, p_radi_max;
    compute_h_radi_bounds(frame, i, &h_radi_min, &h_radi_max);

    p_radi_min = h_radi_min * epsilon / frame->h_hat;
    p_radi_max = h_radi_max * epsilon / frame->h_hat;

    /* --- 判断: 提前接受？ --- */
    /* 源码: picardN.h CHECK_PMIN_PMAX 宏 */
    if(frame->r < frame->p_conv + frame->p_cond + p_radi_min) {
        /* SWITCH_IN_RADIATIVE — 接受辐射路径 */
        p->rwalk = frame->rwalk_s;
        p->T = frame->T_s;
        p->state = frame->return_state;
        return;
    }

    /* --- 判断: 提前拒绝？ --- */
    if(frame->r > frame->p_conv + frame->p_cond + p_radi_max) {
        /* NULL_COLLISION — 回到循环顶部 */
        p->rwalk = frame->rwalk_saved;
        p->T.value = frame->T_value_saved;
        p->state = PATH_SFN_PROB_DISPATCH;      /* 回到 for(;;) 顶部 */
        return;
    }

    /* 既不接受也不拒绝 → 继续计算下一个 Ti */
    if(i < 6) {
        p->state = PATH_SFN_COMPUTE_Ti;
        return;
    }

    /* i == 6: 所有 T0-T5 已计算 → 精确判断 */
    /* 源码: picardN.h:385-395 */
    double h_radi = BOLTZMANN * (T3*T4*T5 + T0*T3*T4 + T0*T1*T3 + T0*T1*T2);
    double p_radi = h_radi * epsilon / frame->h_hat;

    if(frame->r < frame->p_conv + frame->p_cond + p_radi) {
        p->rwalk = frame->rwalk_s;
        p->T = frame->T_s;
        p->state = frame->return_state;
    } else {
        p->rwalk = frame->rwalk_saved;
        p->T.value = frame->T_value_saved;
        p->state = PATH_SFN_PROB_DISPATCH;
    }
    return;
}
```

**关键映射表 — picardN 递归栈**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 选择对流 | `picardN.h:293` | `r < p_conv` | `SFN_PROB_DISPATCH` | `PATH_CNV_INIT` |
| 选择导热 | `picardN.h:300` | `r < p_conv + p_cond` | `SFN_PROB_DISPATCH` | `SFN_SOLID_REINJECT` |
| 开始辐射 | `picardN.h:324` | `else` | `SFN_PROB_DISPATCH` | `SFN_RAD_TRACE` |
| 辐射子路径完成 + 立即接受 | `picardN.h:339` | `r < min_threshold` | `SFN_RAD_DONE` | (return_state) |
| 需要 Ti 采样 | `picardN.h:346` | `else` | `SFN_RAD_DONE` | `SFN_COMPUTE_Ti` |
| Ti 已知（T_s.done） | `picardN.h:348` | `Temp->done` | `SFN_COMPUTE_Ti` | `SFN_CHECK_PMIN_PMAX` |
| Ti 需递归（压栈） | `picardN.h:351` | `!Temp->done` | `SFN_COMPUTE_Ti` | `PATH_BND_DISPATCH` (子路径) |
| 子路径完成（弹栈） | `picardN.h:351` | `T_p.done` | `SFN_COMPUTE_Ti_RESUME` | `SFN_CHECK_PMIN_PMAX` |
| CHECK: 提前接受 | `picardN.h:CHECK_PMIN_PMAX` | `r < min_threshold` | `SFN_CHECK_PMIN_PMAX` | (return_state) |
| CHECK: 提前拒绝 | `picardN.h:CHECK_PMIN_PMAX` | `r > max_threshold` | `SFN_CHECK_PMIN_PMAX` | `SFN_PROB_DISPATCH` |
| CHECK: 继续 | `picardN.h:CHECK_PMIN_PMAX` | `else && i<6` | `SFN_CHECK_PMIN_PMAX` | `SFN_COMPUTE_Ti` |
| 最终精确判断: 接受 | `picardN.h:390` | `r < exact_p_radi` | `SFN_CHECK_PMIN_PMAX` | (return_state) |
| 最终精确判断: 拒绝 | `picardN.h:395` | `r >= exact_p_radi` | `SFN_CHECK_PMIN_PMAX` | `SFN_PROB_DISPATCH` |

> **GPU 设计要点**:  
> 1. **栈深度上界**: `MAX_PICARD_DEPTH` 通常 = `max_branchings`（典型值 2-4）。每多一层约增加 ~200 bytes/path。  
> 2. **栈帧最小化**: 只需保存 rwalk/T/Ti 数组/概率参数/r/return_state，无需完整函数栈。  
> 3. **替代方案**: 对于 depth=1，可以不用栈，直接用线性状态机；depth≥2 才真正需要栈机制。  
> 4. **子路径全复用**: 压栈后的子路径使用完全相同的状态机（RAD → BND → CND → ...），只需设好 return_state 即可在完成时弹栈。
