# [边界分派] 状态迁移映射

> 本文件是 [explicit_state_machine_transition_mapping.md](../explicit_state_machine_transition_mapping.md) 的boundary_dispatch子模块

## 1. 示例 B：boundary_path — 边界分派的三路判断

### 1.1 CPU 原始代码 (`sdis_heat_path_boundary_Xd.h:59-92`)

```c
/* boundary_path_3d — 边界状态入口 */

/* ---- 判断 1: 边界温度已知？ ---- */
tmp = interface_side_get_temperature(interf, &frag);   // ✅ 纯查表
if(SDIS_TEMPERATURE_IS_KNOWN(tmp)) {                   // [boundary_Xd.h:64]
    T->value += tmp;
    T->done = 1;
    goto exit;
    // → PATH_DONE
}

/* 获取界面两侧介质 */
mdm_front = interface_get_medium(interf, SDIS_FRONT);  // ✅
mdm_back = interface_get_medium(interf, SDIS_BACK);    // ✅

/* ---- 判断 2: 三路分派 ---- */
if(mdm_front->type == mdm_back->type) {                // [boundary_Xd.h:80]
    solid_solid_boundary_path(...)                      // → PATH_BND_SS_REINJECT_SAMPLE
} else if(ctx->nbranchings == ctx->max_branchings) {   // [boundary_Xd.h:82]
    solid_fluid_boundary_picard1_path(...)              // → PATH_BND_SF_REINJECT_SAMPLE
} else {                                                // [boundary_Xd.h:85]
    solid_fluid_boundary_picardN_path(...)              // → PATH_BND_SFN_REINJECT_SAMPLE
}

/* ---- 判断 3: Robin 边界条件？ ---- */
/* [boundary_Xd.h:101] */
if(T->func == convective_path || T->func == conductive_path) {
    query_medium_temperature_from_boundary(scn, ctx, rwalk, T);
    if(T->done) goto exit;                              // → PATH_DONE
}
```

### 1.2 显式状态拆分

```c
/* ========== 状态 PATH_BND_DISPATCH ========== */
/* 对应 CPU boundary_path_3d 函数入口的分派逻辑 */
/* 源码位置: sdis_heat_path_boundary_Xd.h:32-105 */
case PATH_BND_DISPATCH: {
    /* ✅ 设置界面 fragment */
    /* 源码: boundary_Xd.h:53-55 */
    setup_interface_fragment(&p->local.bnd.frag, &p->rwalk);
    p->local.bnd.interf = scene_get_interface(scn, p->rwalk.hit_3d.prim.prim_id);

    /* --- 判断 1: 边界温度已知 (Dirichlet)？ --- */
    /* 源码: boundary_Xd.h:62-64 */
    /* 原始代码: tmp = interface_side_get_temperature(interf, &frag); */
    /*           if(SDIS_TEMPERATURE_IS_KNOWN(tmp)) { ... } */
    double tmp = interface_side_get_temperature(p->local.bnd.interf, &p->local.bnd.frag);
    if(SDIS_TEMPERATURE_IS_KNOWN(tmp)) {
        p->temperature_value += tmp;
        p->state = PATH_DONE;
        return;
    }

    /* ✅ 获取界面两侧介质类型 */
    /* 源码: boundary_Xd.h:77-78 */
    struct sdis_medium* mdm_front = interface_get_medium(p->local.bnd.interf, SDIS_FRONT);
    struct sdis_medium* mdm_back = interface_get_medium(p->local.bnd.interf, SDIS_BACK);

    /* --- 判断 2: 三路分派 --- */
    if(mdm_front->type == mdm_back->type) {
        /* 源码: boundary_Xd.h:80 — solid/solid */
        /* 保存必要的局部变量，进入 solid/solid reinjection 采样 */
        init_solid_solid_locals(p);
        p->state = PATH_BND_SS_REINJECT_SAMPLE;

    } else if(p->ctx.nbranchings == p->ctx.max_branchings) {
        /* 源码: boundary_Xd.h:82 — solid/fluid picard1 */
        init_solid_fluid_picard1_locals(p);
        p->state = PATH_BND_SF_REINJECT_SAMPLE;

    } else {
        /* 源码: boundary_Xd.h:85 — solid/fluid picardN */
        init_solid_fluid_picardN_locals(p);
        p->state = PATH_BND_SFN_REINJECT_SAMPLE;
    }
    return;
}
```

**关键映射表 — 边界分派**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| Dirichlet 温度已知 | `boundary_Xd.h:64` | `SDIS_TEMPERATURE_IS_KNOWN(tmp)` | `BND_DISPATCH` | `PATH_DONE` |
| solid/solid 界面 | `boundary_Xd.h:80` | `mdm_front->type == mdm_back->type` | `BND_DISPATCH` | `BND_SS_REINJECT_SAMPLE` |
| solid/fluid picard1 | `boundary_Xd.h:82` | `nbranchings == max_branchings` | `BND_DISPATCH` | `BND_SF_REINJECT_SAMPLE` |
| solid/fluid picardN | `boundary_Xd.h:85` | `else` | `BND_DISPATCH` | `BND_SFN_REINJECT_SAMPLE` |
| Robin 条件命中 | `boundary_Xd.h:101` | `T->done` (after `query_medium_temperature_from_boundary`) | `BND_POST_CHECK` | `PATH_DONE` |

### 1.3 附注：Robin 后检查 — query_medium_temperature_from_boundary

`boundary_path` 在三路分派（solid/solid、picard1、picardN）**返回后**，如果下一状态是 `convective_path` 或 `conductive_path`，会额外检查：当前界面侧的介质温度是否已知。这对应 Robin 边界条件中温度直接给定的情况。

```c
/* boundary_path — boundary_Xd.h:93-105 */
/* 三路分派返回后... */
if(T->done) goto exit;                              /* 子路径已解决 → 结束 */

/* Robin 后检查 — 仅对 convective 和 conductive 下一跳 */
if(T->func == convective_path || T->func == conductive_path) {
    res = query_medium_temperature_from_boundary(scn, ctx, rwalk, T);
    if(res != RES_OK) goto error;
    if(T->done) goto exit;                          /* 温度已知 → PATH_DONE */
}
```

`query_medium_temperature_from_boundary`（`boundary_Xd_c.h:1026-1065`）是**纯查表操作**，不含任何 trace_ray：

```c
/* query_medium_temperature_from_boundary — boundary_Xd_c.h:1026-1065 */
/* ✅ 纯查表 — 无射线 */
if(SXD_HIT_NONE(&rwalk->hit)) return RES_OK;          /* 无界面 → 不做事 */

interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
mdm = (hit_side == FRONT) ? interf->medium_front : interf->medium_back;
temperature = medium_get_temperature(mdm, &rwalk->vtx);

if(SDIS_TEMPERATURE_IS_UNKNOWN(temperature)) goto exit; /* 未知 → 不做事 */

T->value += temperature;
T->done = 1;                                            /* → PATH_DONE */
```

**显式状态机映射**:

```c
/* advance_bnd_post_robin_check() — BND_POST_ROBIN_CHECK 状态 */
void advance_bnd_post_robin_check(struct explicit_path* p) {
    /* 源码: boundary_Xd.h:101-105 */
    /* 前置条件: 三路分派已完成，T->func 已被设为 convective 或 conductive */

    /* --- 判断: 下一跳是 convective/conductive 且有界面信息？ --- */
    if((p->next_path_type == PATH_TYPE_CONVECTIVE ||
        p->next_path_type == PATH_TYPE_CONDUCTIVE) &&
       !SXD_HIT_NONE(&p->rwalk.hit))
    {
        /* ✅ 纯查表 — 源码: boundary_Xd_c.h:1043-1049 */
        struct sdis_interface* interf =
            scene_get_interface(scn, p->rwalk.hit.prim.prim_id);
        struct sdis_medium* mdm = (p->rwalk.hit_side == SDIS_FRONT)
            ? interf->medium_front : interf->medium_back;
        double temperature = medium_get_temperature(mdm, &p->rwalk.vtx);

        if(SDIS_TEMPERATURE_IS_KNOWN(temperature)) {
            p->T.value += temperature;
            p->T.done = 1;
            p->state = PATH_DONE;
            return;
        }
    }

    /* 温度未知或不适用 → 继续到下一跳 */
    /* 根据 next_path_type 转移到对应状态 */
    switch(p->next_path_type) {
        case PATH_TYPE_CONVECTIVE: p->state = PATH_CNV_INIT;     break;
        case PATH_TYPE_CONDUCTIVE: p->state = PATH_CND_INIT_ENC; break;
        default: /* boundary dispatch 已设好 */ break;
    }
    return;
}
```

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| Robin 温度已知 | `boundary_Xd_c.h:1049` | `TEMPERATURE_IS_KNOWN(tmp)` | `BND_POST_ROBIN_CHECK` | `PATH_DONE` |
| Robin 温度未知 + convective | `boundary_Xd_c.h:1051` | `TEMPERATURE_IS_UNKNOWN(\*)` | `BND_POST_ROBIN_CHECK` | `PATH_CNV_INIT` |
| Robin 温度未知 + conductive | `boundary_Xd_c.h:1051` | `TEMPERATURE_IS_UNKNOWN(\*)` | `BND_POST_ROBIN_CHECK` | `PATH_CND_INIT_ENC` |
| 非 Robin 情况（直接继续） | `boundary_Xd.h:101` | `T->func != conv/cond` | `BND_DISPATCH` | (由子路径决定) |