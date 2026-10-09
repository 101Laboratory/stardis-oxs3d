# [辐射路径] 状态迁移映射

> 本文件是 [explicit_state_machine_transition_mapping.md](../explicit_state_machine_transition_mapping.md) 的子模块Radiactive部分。

## 示例 A：辐射路径 trace_radiative_path — 最完整的拆分示例

### 1.1 CPU 原始代码 (`sdis_heat_path_radiative_Xd.h:215-290`)

```c
/* trace_radiative_path_3d — 辐射随机游走核心循环 */
for(;;) {
    /* ---- 位置(A): 准备发射射线 ---- */
    d3_set(pos, rwalk->vtx.P);
    d3_minus(wi, dir);

    res = XD(find_next_fragment)(scn, pos, dir, &rwalk->XD(hit),
      rwalk->vtx.time, rwalk->enc_id, &rwalk->XD(hit), &interf, &frag);
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // 🔴 内部调用 s3d_scene_view_trace_ray — 这是挂起点 #1

    /* ---- 位置(B): 射线返回后，判断 miss/hit ---- */
    if(SXD_HIT_NONE(&rwalk->XD(hit))) {
        set_limit_radiative_temperature(scn, ctx, rwalk, dir, branch_id, T);
        ASSERT(T->done);
        break;
        // → PATH_DONE ✅ 光线逃逸到辐射环境
    }

    /* 更新位置到命中点 */
    d3_set(rwalk->vtx.P, frag.P);                      // ✅ 纯赋值
    rwalk->hit_side = frag.side;                         // ✅ 纯赋值

    /* 获取 BRDF */
    brdf_setup(scn->dev, &brdf_setup_args, &brdf);      // ✅ 纯计算

    /* ---- 位置(C): 关键分支 — 吸收还是反射？---- */
    if(ssp_rng_canonical(rng) < brdf.emissivity) {
        T->func = XD(boundary_path);                     // → PATH_BND_DISPATCH
        rwalk->enc_id = ENCLOSURE_ID_NULL;
        break;
    }

    /* BRDF 反射采样 */
    brdf_sample(&brdf, rng, wi, N, &bounce);            // ✅ 纯计算
    d3_set(dir, bounce.dir);
    // → 回到 for(;;) 顶部 = 再次进入 PATH_RAD_TRACE
}
```

### 1.2 显式状态拆分

```c
/* ========== 状态 PATH_RAD_TRACE ========== */
/* 对应 CPU for(;;) 循环体的前半部分：准备射线参数，发射射线 */
/* 源码位置: sdis_heat_path_radiative_Xd.h:226-231 */
case PATH_RAD_TRACE: {
    /* ✅ 纯计算：准备射线参数 */
    d3_set(p->local.rad.pos, p->rwalk.vtx.P);
    d3_minus(p->local.rad.wi, p->local.rad.dir);

    /* 🔴 发射射线请求 — 对应 find_next_fragment 内部的 trace_ray */
    /* 源码: sdis_heat_path_radiative_Xd.h:228 → find_next_fragment → trace_ray */
    p->ray_slots[0].origin = p->rwalk.vtx.P;
    p->ray_slots[0].direction = p->local.rad.dir;
    p->ray_slots[0].range[0] = 0;
    p->ray_slots[0].range[1] = FLT_MAX;
    p->ray_slots[0].filter = make_hit_filter(p->rwalk.hit_3d, p->rwalk.enc_id);
    p->n_pending_rays = 1;

    p->state = PATH_RAD_BOUNCE_OR_ABSORB;   /* 下一步：处理射线结果 */
    return; /* 挂起，等待射线结果 */
}

/* ========== 状态 PATH_RAD_BOUNCE_OR_ABSORB ========== */
/* 对应 CPU 收到 hit 结果后的判断逻辑 */
/* 源码位置: sdis_heat_path_radiative_Xd.h:233-290 */
case PATH_RAD_BOUNCE_OR_ABSORB: {
    struct s3d_hit hit = p->ray_slots[0].result;

    /* --- 判断 1: 光线逃逸？ --- */
    /* 源码: sdis_heat_path_radiative_Xd.h:235 */
    /* 原始代码: if(SXD_HIT_NONE(&rwalk->XD(hit))) */
    if(S3D_HIT_NONE(&hit)) {
        /* ✅ 纯计算 */
        /* 源码: sdis_heat_path_radiative_Xd.h:236 */
        set_limit_radiative_temperature(..., &p->temperature_value);
        p->state = PATH_DONE;
        return;
    }

    /* ✅ 更新位置到命中点 */
    /* 源码: sdis_heat_path_radiative_Xd.h:249 */
    update_rwalk_to_hit(&p->rwalk, &hit);

    /* ✅ 获取 BRDF */
    /* 源码: sdis_heat_path_radiative_Xd.h:268-271 */
    struct brdf brdf;
    brdf_setup(&brdf, interf, &frag);

    /* --- 判断 2: 吸收 vs 反射？ --- */
    /* 源码: sdis_heat_path_radiative_Xd.h:274 */
    /* 原始代码: if(ssp_rng_canonical(rng) < brdf.emissivity) */
    double r = rng_canonical(&p->rng);
    if(r < brdf.emissivity) {
        /* → 进入边界处理 */
        /* 源码: sdis_heat_path_radiative_Xd.h:275-277 */
        p->rwalk.enc_id = ENCLOSURE_ID_NULL;
        p->state = PATH_BND_DISPATCH;
        return;
    }

    /* ✅ BRDF 反射采样 — 继续辐射路径 */
    /* 源码: sdis_heat_path_radiative_Xd.h:286-288 */
    brdf_sample(&brdf, &p->rng, p->local.rad.wi, N, &bounce);
    d3_set(p->local.rad.dir, bounce.dir);

    /* → 回到 RAD_TRACE 发射新射线（对应 for(;;) 循环回到顶部）*/
    p->state = PATH_RAD_TRACE;
    return;
}
```

**关键映射表 — 辐射路径**:

| 判断条件（CPU 源码） | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 光线 miss（逃逸） | `radiative_Xd.h:235` | `SXD_HIT_NONE(&rwalk->hit)` | `RAD_BOUNCE_OR_ABSORB` | `PATH_DONE` |
| 吸收（进入边界） | `radiative_Xd.h:274` | `rng_canonical(rng) < brdf.emissivity` | `RAD_BOUNCE_OR_ABSORB` | `PATH_BND_DISPATCH` |
| 反射（继续辐射） | `radiative_Xd.h:286` | `else`（上面两个都不满足） | `RAD_BOUNCE_OR_ABSORB` | `PATH_RAD_TRACE` |

### 2.3 附注：find_next_fragment 的重试子状态

`find_next_fragment`（`sdis_heat_path_radiative_Xd.h:401-490`）是 `trace_radiative_path` 内部调用的射线-界面求交函数。它包含一个**最多 10 次重试**的容错机制：当射线命中无效的界面片段（corner case）时，会微移起点重试。

```c
/* find_next_fragment — radiative_Xd.h:420-480 */
NATTEMPTS_MAX = SXD_HIT_NONE(in_hit) ? 1 : 10;
dX(set)(rt_pos, in_pos);

do {
    res = RES_OK;

    /* [radiative_Xd.h:444] 🔴 trace_ray(rt_pos, dir, INF) */
    trace_ray(scn, rt_pos, in_dir, INF, enc_id, in_hit, &hit);

    if(SXD_HIT_NONE(&hit)) break;              // miss → 返回无交点

    /* [radiative_Xd.h:456] ✅ 验证界面片段有效性 */
    interf = scene_get_interface(scn, hit.prim.prim_id);
    setup_fragment(&frag, pos, in_dir, time, N, &hit);
    res = check_interface(interf, &frag, nattempts == NATTEMPTS_MAX);

    if(res != RES_OK && nattempts == NATTEMPTS_MAX) goto error;

    /* [radiative_Xd.h:475] 无效 → 微移起点 */
    if(res != RES_OK) {
        move_away_primitive_boundaries(in_hit, delta, rt_pos);
    }
    ++nattempts;
} while(res != RES_OK);
```

**显式状态机中的处理方式**:

在 GPU wavefront 中，这个重试循环可以用两种策略：

1. **内嵌循环（推荐）**: 重试逻辑纯属数值容错，不改变路径物理语义。在 `advance_rad_trace()` 发射射线后，结果处理中如果 `check_interface` 失败，则 `move_away` → 重新设置 `p->ray` → 再次 `n_pending_rays = 1`，回到同一挂起点。这不增加新状态，只在 `RAD_BOUNCE_OR_ABSORB` 内部增加一个重试计数器：

```c
void advance_rad_bounce_or_absorb(struct explicit_path* p) {
    /* ... 获取 trace_ray 结果 ... */

    /* 源码: radiative_Xd.h:456 */
    res = check_interface(interf, &frag, p->local.rad.nattempts == NATTEMPTS_MAX);

    if(res != RES_OK) {
        if(p->local.rad.nattempts >= NATTEMPTS_MAX) {
            p->state = PATH_ERROR;
            return;
        }
        /* 微移起点并重试 — 源码: radiative_Xd.h:475 */
        move_away_primitive_boundaries(&p->local.rad.hit_from,
            delta, p->local.rad.rt_pos);
        p->local.rad.nattempts++;

        /* 重新发射同方向射线 🔴 */
        setup_trace_ray(p, p->local.rad.rt_pos, p->local.rad.dir,
                        INF, enc_id, &p->local.rad.hit_from);
        p->n_pending_rays = 1;
        p->state = PATH_RAD_BOUNCE_OR_ABSORB;   /* 回到自身等待新结果 */
        return;
    }

    /* ... 正常的 miss/absorb/reflect 判断 ... */
}
```

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 片段无效，可重试 | `radiative_Xd.h:468` | `res!=RES_OK && nattempts<MAX` | `RAD_BOUNCE_OR_ABSORB` | `RAD_BOUNCE_OR_ABSORB` (🔴 重发) |
| 片段无效，超限 | `radiative_Xd.h:468` | `res!=RES_OK && nattempts==MAX` | `RAD_BOUNCE_OR_ABSORB` | `PATH_ERROR` |