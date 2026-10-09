# [导热路径] 状态迁移映射

> 本文件是 [explicit_state_machine_transition_mapping.md](../explicit_state_machine_transition_mapping.md) 的conductive子模块

## 1. 示例 D：conductive_path_delta_sphere — 扩散步进循环

### 1.1 CPU 原始代码 (`sdis_heat_path_conductive_delta_sphere_Xd.h:324-474`)

```c
/* conductive_path_delta_sphere_3d */

/* [delta_sphere_Xd.h:351] 🔴 初始 enclosure 查询 */
res = scene_get_enclosure_id_in_closed_boundaries(scn, rwalk->vtx.P, &enc_id);

do { /* 固体内随机游走循环 */

    /* [delta_sphere_Xd.h:387-393] ✅ 检查温度是否已知 */
    if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
        T->value += props.temperature;
        T->done = 1;
        break;                                          // → PATH_DONE
    }

    /* [delta_sphere_Xd.h:401-403] 🔴 采样下一步 */
    res = sample_next_step_robust(scn, enc_id, rng, pos, delta, dir0, dir1, &hit0, &hit1, &delta);
    // 内部:
    //   sample_next_step() [delta_sphere_Xd.h:34-115]:
    //     trace_ray(pos, dirs[0])        ← 🔴 射线 #1
    //     trace_ray(pos, dirs[1])        ← 🔴 射线 #2
    //   if(hit0.distance > delta):
    //     get_enclosure_id_in_closed_boundaries(pos_next)  ← 🔴 最多 6 条验证射线

    /* [delta_sphere_Xd.h:429-433] ✅ 时间退回 */
    mu = (2*DIM*lambda) / (rho*cp*delta_m*delta_m);
    res = time_rewind(scn, mu, t0, rng, rwalk, ctx, T);
    if(T->done) break;                                  // → PATH_DONE

    /* [delta_sphere_Xd.h:436-440] ✅ 判断沿 dir0 是否命中 */
    if(hit0.distance > delta) {
        rwalk->hit = S3D_HIT_NULL;                      // 未命中界面
    } else {
        rwalk->hit = hit0;                               // 命中界面
    }

    /* [delta_sphere_Xd.h:443] ✅ 更新位置 */
    move_pos(rwalk->vtx.P, dir0, delta);

} while(SXD_HIT_NONE(&rwalk->hit));                     // [delta_sphere_Xd.h:452]

/* [delta_sphere_Xd.h:460-461] ✅ 退出循环 → 进入边界 */
T->func = boundary_path;                                // → PATH_BND_DISPATCH
rwalk->enc_id = ENCLOSURE_ID_NULL;
```

### 1.2 显式状态拆分

```c
/* ========== 状态 PATH_CND_INIT_ENC ========== */
/* 初始 enclosure 查询 */
/* 源码: conductive_Xd.h:44 + delta_sphere_Xd.h:351 */
case PATH_CND_INIT_ENC: {
    /* 🔴 发射 6 方向 enclosure 查询射线 */
    /* 源码: sdis_scene_Xd.h:1340-1361 */
    emit_enclosure_query_batch(p, p->rwalk.vtx.P);
    p->state = PATH_CND_INIT_ENC_RESULT;
    return; /* 挂起 */
}

/* ========== 状态 PATH_CND_INIT_ENC_RESULT ========== */
/* 处理 enclosure 查询结果，初始化导热循环 */
case PATH_CND_INIT_ENC_RESULT: {
    /* ✅ 从射线结果确定 enc_id */
    /* 源码: sdis_scene_Xd.h:1355-1361 */
    p->local.cnd.enc_id = resolve_enclosure_from_rays(p);
    if(p->local.cnd.enc_id == ENCLOSURE_ID_NULL) {
        p->state = PATH_ERROR;
        return;
    }
    /* 验证一致性 [delta_sphere_Xd.h:354-360] */
    if(p->local.cnd.enc_id != p->rwalk.enc_id) {
        p->state = PATH_ERROR;
        return;
    }
    p->state = PATH_CND_DS_CHECK_TEMPERATURE;
    return;
}

/* ========== 状态 PATH_CND_DS_CHECK_TEMPERATURE ========== */
/* 每步开始：检查温度是否已知 */
/* 源码: delta_sphere_Xd.h:387-393 */
case PATH_CND_DS_CHECK_TEMPERATURE: {
    /* ✅ 获取物理属性 */
    solid_get_properties(mdm, &p->rwalk.vtx, &props);

    /* --- 判断: 温度已知？ --- */
    /* 源码: delta_sphere_Xd.h:387 */
    /* 原始代码: if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) */
    if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
        p->temperature_value += props.temperature;
        p->state = PATH_DONE;
        return;
    }

    /* 🔴 发射扩散步射线（dir0 + dir1 两条） */
    /* 源码: delta_sphere_Xd.h:401 → sample_next_step → trace_ray ×2 */
    /* 源码: delta_sphere_Xd.h:58 (dirs[0]) + :66 (dirs[1]) */
    float dir0[3], dir1[3];
    ssp_ran_sphere_uniform_float(&p->rng, dir0, NULL);
    f3_minus(dir1, dir0);

    p->ray_slots[0] = make_ray_f(p->rwalk.vtx.P, dir0, 0, delta*RAY_RANGE_MAX_SCALE);
    p->ray_slots[1] = make_ray_f(p->rwalk.vtx.P, dir1, 0, delta*RAY_RANGE_MAX_SCALE);
    p->n_pending_rays = 2;
    p->local.cnd_ds.dir0 = dir0;
    p->local.cnd_ds.dir1 = dir1;
    p->local.cnd_ds.delta_solid = props.delta;

    p->state = PATH_CND_DS_STEP_PROCESS;
    return; /* 挂起 */
}

/* ========== 状态 PATH_CND_DS_STEP_PROCESS ========== */
/* 处理扩散步射线结果，确定 delta，可能需要 enclosure 验证 */
/* 源码: delta_sphere_Xd.h:68-115 (sample_next_step 结果处理) */
/*       delta_sphere_Xd.h:148-173 (sample_next_step_robust enclosure 检查) */
case PATH_CND_DS_STEP_PROCESS: {
    struct s3d_hit hit0 = p->ray_slots[0].result;
    struct s3d_hit hit1 = p->ray_slots[1].result;

    /* ✅ 确定 delta（与 CPU 完全相同的逻辑） */
    /* 源码: delta_sphere_Xd.h:68-72 */
    float delta;
    if(S3D_HIT_NONE(&hit0) && S3D_HIT_NONE(&hit1)) {
        delta = p->local.cnd_ds.delta_solid;
    } else {
        delta = MMIN(hit0.distance, hit1.distance);
    }
    p->local.cnd_ds.delta = delta;
    p->local.cnd_ds.hit0 = hit0;
    p->local.cnd_ds.hit1 = hit1;

    /* --- 判断: 是否需要 enclosure 验证？ --- */
    /* 源码: delta_sphere_Xd.h:155-159 */
    /* 原始代码: if(hit0->distance > delta) → get_enclosure_id_in_closed_boundaries */
    if(hit0.distance > delta) {
        /* 需要验证下一位置的 enclosure */
        /* 🔴 发射 enclosure 查询射线 */
        double pos_next[3];
        move_pos(pos_next, p->rwalk.vtx.P, p->local.cnd_ds.dir0, delta);
        emit_enclosure_query_batch(p, pos_next);
        p->state = PATH_CND_DS_STEP_ENC_VERIFY;
        return; /* 挂起 */
    }

    /* 不需要验证，直接步进 */
    p->state = PATH_CND_DS_STEP_ADVANCE;
    return;
}

/* ========== 状态 PATH_CND_DS_STEP_ADVANCE ========== */
/* 执行步进：时间退回 + 位置更新 + 判断是否命中界面 */
/* 源码: delta_sphere_Xd.h:429-452 */
case PATH_CND_DS_STEP_ADVANCE: {
    /* ✅ 时间退回 [delta_sphere_Xd.h:429-431] */
    double delta_m = p->local.cnd_ds.delta * fp_to_meter;
    double mu = (2*DIM*lambda) / (rho*cp*delta_m*delta_m);
    time_rewind(mu, t0, &p->rng, &p->rwalk, &p->ctx, &p->temperature_value, &p->done);

    /* --- 判断: 时间退回到达极限？ --- */
    /* 源码: delta_sphere_Xd.h:432 */
    if(p->done) {
        p->state = PATH_DONE;
        return;
    }

    /* ✅ 更新 hit 状态 [delta_sphere_Xd.h:436-440] */
    /* 原始代码: if(hit0.distance > delta) → HIT_NULL; else → hit0 */
    if(p->local.cnd_ds.hit0.distance > p->local.cnd_ds.delta) {
        p->rwalk.hit_3d = S3D_HIT_NULL;
    } else {
        p->rwalk.hit_3d = p->local.cnd_ds.hit0;
        p->rwalk.hit_side = compute_hit_side(p->local.cnd_ds.hit0, p->local.cnd_ds.dir0);
    }

    /* ✅ 更新位置 [delta_sphere_Xd.h:443] */
    move_pos(p->rwalk.vtx.P, p->local.cnd_ds.dir0, p->local.cnd_ds.delta);

    /* --- 判断: 是否命中界面？ --- */
    /* 源码: delta_sphere_Xd.h:452 */
    /* 原始代码: while(SXD_HIT_NONE(&rwalk->hit)) ← 循环条件 */
    if(S3D_HIT_NONE(&p->rwalk.hit_3d)) {
        /* 未命中界面 → 继续扩散步 */
        p->state = PATH_CND_DS_CHECK_TEMPERATURE;       // 回到循环顶部
    } else {
        /* 命中界面 → 退出导热，进入边界 */
        /* 源码: delta_sphere_Xd.h:460-461 */
        p->rwalk.enc_id = ENCLOSURE_ID_NULL;
        p->state = PATH_BND_DISPATCH;
    }
    return;
}
```

**关键映射表 — delta-sphere 导热路径**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 温度已知 | `delta_sphere_Xd.h:387` | `TEMPERATURE_IS_KNOWN(props.temperature)` | `CND_DS_CHECK_TEMP` | `PATH_DONE` |
| 步进后需 enclosure 验证 | `delta_sphere_Xd.h:155` | `hit0.distance > delta` | `CND_DS_STEP_PROCESS` | `CND_DS_STEP_ENC_VERIFY` |
| 步进后无需验证 | `delta_sphere_Xd.h:155` | `hit0.distance <= delta` | `CND_DS_STEP_PROCESS` | `CND_DS_STEP_ADVANCE` |
| time_rewind 到达极限 | `delta_sphere_Xd.h:432` | `T->done` | `CND_DS_STEP_ADVANCE` | `PATH_DONE` |
| do-while 循环继续 | `delta_sphere_Xd.h:452` | `SXD_HIT_NONE(&rwalk->hit)` | `CND_DS_STEP_ADVANCE` | `CND_DS_CHECK_TEMP` |
| do-while 循环结束 | `delta_sphere_Xd.h:452` | `!SXD_HIT_NONE(&rwalk->hit)` | `CND_DS_STEP_ADVANCE` | `PATH_BND_DISPATCH` |

---

## 1b. 示例 H：conductive_path_wos — Walk-on-Spheres 导热路径

> **与 delta-sphere 的核心差异**: delta-sphere 用 `trace_ray` 采样步进方向，WoS 使用 `closest_point`（最近点查询）确定球半径，在球面上均匀采样新位置，再用 `check_diffusion_position` 验证。WoS 有 3 层 fallback: check_diffusion → trace_ray → setup_hit_wos（snap to boundary）。

### 1b.1 CPU 原始代码 (`sdis_heat_path_conductive_wos_Xd.h:607-735`)

```c
/* conductive_path_wos_3d */

/* [wos_Xd.h:652-653] ✅ 获取材料属性，计算扩散率 alpha */
alpha = props_ref.lambda / (props_ref.rho * props_ref.cp);

for(;;) { /* WoS 扩散循环 */

    /* [wos_Xd.h:671-676] ✅ 检查温度是否已知 */
    if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
        T->value += props.temperature;
        T->done = 1;
        break;                                          // → PATH_DONE
    }

    /* [wos_Xd.h:680-681] 🔴 sample_next_position — 核心几何查询 */
    res = sample_next_position(scn, rwalk, rng, props.delta, &dst);
    /*
     * sample_next_position 内部流程 [wos_Xd.h:504-600]:
     *   1. 🔴 closest_point(wos_pos, wos_radius) → hit, wos_distance
     *   2. if(wos_distance <= epsilon_shell):
     *        setup_hit_wos(hit) → snap 到界面  [wos_Xd.h:526]
     *   3. else:
     *        采样球面上均匀方向 dir
     *        pos = rwalk.P + dir * hit.distance           [wos_Xd.h:541]
     *        ✅ check_diffusion_position(scn, enc_id, delta, pos)
     *   4. if(check 失败):
     *        🔴 trace_ray(scn, rwalk.P, dir, INF) → hit_rt [wos_Xd.h:570]
     *        if(HIT_NONE): setup_hit_wos(hit) → snap     [wos_Xd.h:581]
     *        else: setup_hit_rt(hit_rt) → 或 fallback snap [wos_Xd.h:585-597]
     */

    /* [wos_Xd.h:684-685] ✅ time_travel — 时间退回 */
    res = time_travel(scn, rwalk, rng, mdm, alpha, props.t0, pos, &dst, T);

    /* [wos_Xd.h:688-689] ✅ handle_volumic_power_wos */
    res = handle_volumic_power_wos(scn, &props, dst, &power_term, T);

    /* [wos_Xd.h:696-698] ✅ 判断: 是否到达初始条件？ */
    if(T->done) {
        T->func = NULL;
        break;                                          // → PATH_DONE
    }

    /* [wos_Xd.h:701-704] ✅ 判断: 是否命中界面？ */
    if(!SXD_HIT_NONE(&rwalk->hit)) {
        T->func = boundary_path;
        rwalk->enc_id = ENCLOSURE_ID_NULL;
        break;                                          // → PATH_BND_DISPATCH
    }

    /* else: 获取新位置的材料属性，继续循环 [wos_Xd.h:710-713] */
}
```

### 5b.2 显式状态机映射

```c
/* advance_cnd_wos_closest() — CND_WOS_CLOSEST 状态 */
void advance_cnd_wos_closest(struct explicit_path* p) {
    /* 发射 closest_point 查询 */
    /* 源码: wos_Xd.h:514-517 */
    fX_set_dX(p->local.cnd_wos.query_pos, p->rwalk.vtx.P);
    p->local.cnd_wos.query_radius = (float)INF;

    /* 🔴 挂起等待 closest_point 结果 */
    p->pending_query = QUERY_CLOSEST_POINT;
    p->state = PATH_CND_WOS_CLOSEST_RESULT;
    return;
}

/* advance_cnd_wos_closest_result() — closest_point 返回后处理 */
void advance_cnd_wos_closest_result(struct explicit_path* p) {
    double wos_distance = p->query_result.hit.distance;
    double wos_epsilon = EPSILON_SHELL(p->local.cnd_wos.delta);

    /* --- 判断 1: 在 epsilon-shell 内？ --- */
    /* 源码: wos_Xd.h:524-526 */
    if(wos_distance <= wos_epsilon) {
        /* snap 到最近界面 (setup_hit_wos) — ✅ 纯计算 */
        setup_hit_wos(&p->query_result.hit, p->local.cnd_wos.delta, &p->rwalk);
        /* → 直接进入时间退回 */
        p->state = PATH_CND_WOS_TIME_TRAVEL;
        return;
    }

    /* --- 在 epsilon-shell 外: 球面均匀采样 --- */
    /* 源码: wos_Xd.h:533-541 */
    ssp_ran_sphere_uniform(&p->rng, p->local.cnd_wos.dir, NULL);
    dX(muld)(p->local.cnd_wos.new_pos, p->local.cnd_wos.dir,
             (double)p->query_result.hit.distance);
    dX(add)(p->local.cnd_wos.new_pos, p->local.cnd_wos.new_pos, p->rwalk.vtx.P);

    /* ✅ check_diffusion_position — 验证新位置在正确包壳内 */
    /* 源码: wos_Xd.h:555 — 这是 closest_point 级别的查询 */
    res = check_diffusion_position(scn, p->rwalk.enc_id,
              p->local.cnd_wos.delta, p->local.cnd_wos.new_pos);

    /* --- 判断 2: 扩散位置有效？ --- */
    if(res == RES_OK) {
        /* 直接移动到新位置 */
        dX(set)(p->rwalk.vtx.P, p->local.cnd_wos.new_pos);
        p->state = PATH_CND_WOS_TIME_TRAVEL;
        return;
    }

    /* --- 扩散位置无效: 需要 fallback trace_ray --- */
    /* 源码: wos_Xd.h:566-570 */
    p->state = PATH_CND_WOS_FALLBACK_TRACE;
    return;
}

/* advance_cnd_wos_fallback_trace() — 发射 fallback 射线 */
void advance_cnd_wos_fallback_trace(struct explicit_path* p) {
    /* 🔴 发射 trace_ray(rwalk.P, dir, INF) */
    /* 源码: wos_Xd.h:570-573 */
    fX_set_dX(p->ray.org, p->rwalk.vtx.P);
    fX_set_dX(p->ray.dir, p->local.cnd_wos.dir);
    p->ray.range[0] = 0;
    p->ray.range[1] = (float)INF;

    p->n_pending_rays = 1;
    p->state = PATH_CND_WOS_FALLBACK_RESULT;
    return;
}

/* advance_cnd_wos_fallback_result() — fallback 射线结果处理 */
void advance_cnd_wos_fallback_result(struct explicit_path* p) {
    struct sXd(hit)* hit_rt = &p->ray_result.hit;

    /* --- 判断 3: fallback 射线是否命中？ --- */
    /* 源码: wos_Xd.h:577-597 */
    if(SXD_HIT_NONE(hit_rt)) {
        /* miss → snap 到最近点 (已在 closest_result 中缓存) */
        /* 源码: wos_Xd.h:581 */
        setup_hit_wos(&p->local.cnd_wos.cached_closest_hit,
                      p->local.cnd_wos.delta, &p->rwalk);
    } else {
        res = setup_hit_rt(p->rwalk.vtx.P, p->local.cnd_wos.dir, hit_rt, &p->rwalk);
        if(res != RES_OK) {
            /* setup_hit_rt 失败也 snap → 源码: wos_Xd.h:593-597 */
            setup_hit_wos(&p->local.cnd_wos.cached_closest_hit,
                          p->local.cnd_wos.delta, &p->rwalk);
        }
    }

    p->state = PATH_CND_WOS_TIME_TRAVEL;
    return;
}

/* advance_cnd_wos_time_travel() — 时间退回 + 终止判断 */
void advance_cnd_wos_time_travel(struct explicit_path* p) {
    double dst = p->local.cnd_wos.last_distance;

    /* ✅ time_travel — 源码: wos_Xd.h:684 */
    time_travel(scn, &p->rwalk, &p->rng, mdm, alpha, t0, pos, &dst, &p->T);

    /* ✅ handle_volumic_power — 源码: wos_Xd.h:688 */
    handle_volumic_power_wos(scn, &props, dst, &power, &p->T);

    /* --- 判断 4: 到达初始条件 (T->done by time_travel)？ --- */
    /* 源码: wos_Xd.h:696 */
    if(p->T.done) {
        p->state = PATH_DONE;
        return;
    }

    /* --- 判断 5: 命中界面？ --- */
    /* 源码: wos_Xd.h:701 */
    if(!SXD_HIT_NONE(&p->rwalk.hit)) {
        p->rwalk.enc_id = ENCLOSURE_ID_NULL;
        p->state = PATH_BND_DISPATCH;
        return;
    }

    /* 继续 WoS 循环 — 获取新位置属性后发射下一个 closest_point */
    /* 源码: wos_Xd.h:710 → 回到 for(;;) 顶部 */
    solid_get_properties(mdm, &p->rwalk.vtx, &p->local.cnd_wos.props);
    p->state = PATH_CND_WOS_CHECK_TEMP;
    return;
}

/* advance_cnd_wos_check_temp() — 循环顶部温度检查 */
void advance_cnd_wos_check_temp(struct explicit_path* p) {
    /* --- 判断 0: 温度已知？ --- */
    /* 源码: wos_Xd.h:671 */
    if(SDIS_TEMPERATURE_IS_KNOWN(p->local.cnd_wos.props.temperature)) {
        p->T.value += p->local.cnd_wos.props.temperature;
        p->T.done = 1;
        p->state = PATH_DONE;
        return;
    }

    /* 发射下一个 closest_point */
    p->state = PATH_CND_WOS_CLOSEST;
    return;
}
```

**关键映射表 — WoS 导热路径**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 温度已知 | `wos_Xd.h:671` | `TEMPERATURE_IS_KNOWN(props.temperature)` | `CND_WOS_CHECK_TEMP` | `PATH_DONE` |
| 在 epsilon-shell 内 | `wos_Xd.h:524` | `wos_distance <= wos_epsilon` | `CND_WOS_CLOSEST_RESULT` | `CND_WOS_TIME_TRAVEL` |
| 扩散位置有效 | `wos_Xd.h:555` | `check_diffusion_position == RES_OK` | `CND_WOS_CLOSEST_RESULT` | `CND_WOS_TIME_TRAVEL` |
| 扩散位置无效（需 fallback） | `wos_Xd.h:566` | `check_diffusion_position != RES_OK` | `CND_WOS_CLOSEST_RESULT` | `CND_WOS_FALLBACK_TRACE` |
| fallback 射线 miss | `wos_Xd.h:577` | `SXD_HIT_NONE(hit_rt)` | `CND_WOS_FALLBACK_RESULT` | `CND_WOS_TIME_TRAVEL` |
| fallback 射线 hit | `wos_Xd.h:585` | `!SXD_HIT_NONE(hit_rt)` | `CND_WOS_FALLBACK_RESULT` | `CND_WOS_TIME_TRAVEL` |
| time_travel 到达初始条件 | `wos_Xd.h:696` | `T->done` | `CND_WOS_TIME_TRAVEL` | `PATH_DONE` |
| 命中界面 | `wos_Xd.h:701` | `!SXD_HIT_NONE(&rwalk->hit)` | `CND_WOS_TIME_TRAVEL` | `PATH_BND_DISPATCH` |
| 继续 WoS 循环 | `wos_Xd.h:710` | `else`（未 done，未 hit） | `CND_WOS_TIME_TRAVEL` | `CND_WOS_CHECK_TEMP` |

> **GPU wavefront 注意**: WoS 引入了新的几何查询类型 `QUERY_CLOSEST_POINT`，与 `trace_ray` 不同。wavefront 调度器需区分这两种查询：closest_point 由 cuBQL 的 `findClosestPoint` API 执行，trace_ray 由 `traceRay` API 执行。`check_diffusion_position` 内部也包含一次 closest_point 查询（`sdis_heat_path_conductive_wos_Xd.h:375-420`），在显式状态机中可将其与主 closest_point 合并为一次 batch 查询，或拆成额外子状态。

---

## 1c. 示例 H-附：conductive_path_custom — 用户自定义导热路径（占位）

> **来源**: `sdis_heat_path_conductive_custom_Xd.h:142-212`  
> **性质**: 通过函数指针 `mdm->shader.solid.sample_path()` 调用用户提供的导热路径采样器。  
> **GPU 策略**: 此路径在 GPU 并行化中属于 **Phase 7（扩展阶段）**。目前仅记录其接口签名，不做显式状态拆分。

```c
/* conductive_path_custom 接口签名 — custom_Xd.h:142-170 */
path.vtx = rwalk->vtx;
res = mdm->shader.solid.sample_path(scn, rng, &path, mdm->data);  // 用户回调
/* 返回后: path.at_limit → DONE, !HIT_NONE → BND_DISPATCH, else → CND_INIT_ENC */
```

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 到达极限 | `custom_Xd.h:180-185` | `path.at_limit` | `CND_CUSTOM` | `PATH_DONE` |
| 命中界面 | `custom_Xd.h:190-195` | `!SXD_HIT_NONE(&rwalk->hit)` | `CND_CUSTOM` | `PATH_BND_DISPATCH` |
| 仍在固体内 | `custom_Xd.h:195` | `else` | `CND_CUSTOM` | (由 custom 回调决定) |
