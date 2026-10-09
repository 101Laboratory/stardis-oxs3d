# [对流路径] 状态迁移映射

> 本文件是 [explicit_state_machine_transition_mapping.md](../explicit_state_machine_transition_mapping.md) 的convective子模块

## 1. 示例 F：convective_path — 最简单的路径

### 1.1 CPU 原始代码 (`sdis_heat_path_convective_Xd.h:174-325`)

```c
/* convective_path_3d */

/* [convective_Xd.h:212-213] ✅ 检查已知温度 */
res = handle_known_fluid_temperature(ctx, rwalk, mdm, T);
if(T->done) goto exit;                                 // → PATH_DONE

/* [convective_Xd.h:217] 处理从流体内部启动的情况 */
res = handle_convective_path_startup(scn, rwalk, &path_starts_in_fluid);
// ^^^ 内部可能含 1 条 trace_ray（如果 HIT_NONE）

/* [convective_Xd.h:247-310] 对流随机游走循环 */
for(;;) {
    /* [convective_Xd.h:258-259] ✅ 时间退回 */
    mu = hc_upper_bound / (rho * cp) * S_over_V;
    res = time_rewind(scn, mu, t0, rng, rwalk, ctx, T);
    if(T->done) break;                                  // → PATH_DONE

    /* [convective_Xd.h:262-275] ✅ 在 enclosure 表面均匀采样 */
    scene_view_sample(enc->view, r0, r1, r2, &prim, uv);  // ← 纯采样，无 trace
    /* 获取位置和法向 */

    /* [convective_Xd.h:302] ✅ 获取对流系数 */
    hc = interface_get_convection_coef(interf, &frag);

    /* [convective_Xd.h:312-314] 接受/拒绝 */
    r = ssp_rng_canonical_float(rng);
    if(r < hc / enc->hc_upper_bound) {
        break;                                           // 真正对流
    }
    // else: null-collision → 继续循环
}

/* [convective_Xd.h:317-318] → 边界 */
T->func = boundary_path;                                // → PATH_BND_DISPATCH
rwalk->enc_id = ENCLOSURE_ID_NULL;
```

### 1.2 显式状态拆分

```c
/* ========== 状态 PATH_CNV_INIT ========== */
/* 源码: convective_Xd.h:205-235 */
case PATH_CNV_INIT: {
    /* ✅ 检查温度 */
    /* 源码: convective_Xd.h:212 */
    if(known_fluid_temperature(p)) {
        p->state = PATH_DONE;
        return;
    }

    /* --- 判断: 路径是否从流体内部启动（无 hit）？ --- */
    /* 源码: convective_Xd.h:217 → handle_convective_path_startup */
    if(S3D_HIT_NONE(&p->rwalk.hit_3d)) {
        /* 🔴 需要发射 1 条射线确定初始 hit */
        /* 源码: sdis_heat_path_convective_Xd.h (handle_convective_path_startup 内部) */
        emit_startup_ray(p);
        p->state = PATH_CNV_INIT_RESULT;
        return; /* 挂起 */
    }

    p->state = PATH_CNV_SAMPLE_BOUNDARY;
    return;
}

/* ========== 状态 PATH_CNV_SAMPLE_BOUNDARY ========== */
/* 对流循环体：时间退回 + 表面采样 + 接受/拒绝 */
/* 源码: convective_Xd.h:247-318 */
/* 注意：此状态内部全是纯计算，无 trace_ray！这是 GPU 最友好的路径 */
case PATH_CNV_SAMPLE_BOUNDARY: {
    /* ✅ 时间退回 [convective_Xd.h:258-260] */
    time_rewind(&p->rng, &p->rwalk, ...);
    if(p->done) {
        p->state = PATH_DONE;
        return;
    }

    /* ✅ 表面均匀采样 [convective_Xd.h:262-290] */
    scene_view_sample(enc->view, ...);

    /* ✅ 接受/拒绝 [convective_Xd.h:312-314] */
    /* 原始代码: if(r < hc / enc->hc_upper_bound) → break */
    double r = rng_canonical_float(&p->rng);
    if(r < hc / hc_upper_bound) {
        /* 真正对流 → 进入边界 */
        /* 源码: convective_Xd.h:317-318 */
        p->rwalk.enc_id = ENCLOSURE_ID_NULL;
        p->state = PATH_BND_DISPATCH;
        return;
    }

    /* null-collision → 继续循环（回到自身） */
    p->state = PATH_CNV_SAMPLE_BOUNDARY;
    return;
}
```

**关键映射表 — 对流路径**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 流体温度已知 | `convective_Xd.h:212` | `T->done` | `CNV_INIT` | `PATH_DONE` |
| 从流体内部启动 | `convective_Xd.h:217` | `HIT_NONE(rwalk->hit)` | `CNV_INIT` | `CNV_INIT_RESULT` (🔴) |
| time_rewind 到极限 | `convective_Xd.h:260` | `T->done` | `CNV_SAMPLE_BOUNDARY` | `PATH_DONE` |
| 真正对流（接受） | `convective_Xd.h:312` | `r < hc/hc_upper_bound` | `CNV_SAMPLE_BOUNDARY` | `PATH_BND_DISPATCH` |
| null-collision（拒绝） | `convective_Xd.h:314` | `r >= hc/hc_upper_bound` | `CNV_SAMPLE_BOUNDARY` | `CNV_SAMPLE_BOUNDARY` |

### 1.3 附注：handle_convective_path_startup — 初始射线

当对流路径从**流体内部**启动（`rwalk->hit` 为 `HIT_NONE`）时，需要发射 1 条射线来初始化命中信息。此场景出现在 `probe_realisation`（指定体积内某点的温度查询）等入口。

```c
/* handle_convective_path_startup — convective_Xd.h:107-138 */
*path_starts_in_fluid = SXD_HIT_NONE(&rwalk->hit);
if(*path_starts_in_fluid == 0) goto exit;               /* 已有 hit，无需处理 */

/* 🔴 沿 +Z 方向发射 1 条射线 */
dir[DIM-1] = 1;
fX_set_dX(org, rwalk->vtx.P);
scene_view_trace_ray(view, org, dir, range, NULL, &rwalk->hit);

if(SXD_HIT_NONE(&rwalk->hit)) {
    /* 仍然 miss → 错误，该位置在无界流体中 */
    res = RES_BAD_OP; goto error;
}

/* ✅ 设置 hit_side */
rwalk->hit_side = fX(dot)(rwalk->hit.normal, dir) < 0
    ? SDIS_FRONT : SDIS_BACK;
```

**显式状态机映射**:

```c
/* advance_cnv_init() 中的启动射线分支 */
void advance_cnv_init(struct explicit_path* p) {
    /* ✅ 检查已知温度 — 源码: convective_Xd.h:212 */
    if(p->T.done) { p->state = PATH_DONE; return; }

    /* --- 判断: 从流体内部启动（无 hit）？ --- */
    /* 源码: convective_Xd.h:217 → handle_convective_path_startup:113 */
    if(!SXD_HIT_NONE(&p->rwalk.hit)) {
        /* 已有 hit → 直接进入主循环 */
        p->state = PATH_CNV_SAMPLE_BOUNDARY;
        return;
    }

    /* 🔴 沿 +Z 发射 1 条初始化射线 */
    /* 源码: convective_Xd.h:122-128 */
    float dir[3] = {0, 0, 0};
    dir[DIM-1] = 1;
    fX_set_dX(p->ray.org, p->rwalk.vtx.P);
    f3_set(p->ray.dir, dir);
    p->ray.range[0] = FLT_MIN;
    p->ray.range[1] = FLT_MAX;

    p->n_pending_rays = 1;
    p->state = PATH_CNV_INIT_TRACE_RESULT;
    return;
}

/* advance_cnv_init_trace_result() — 启动射线结果 */
void advance_cnv_init_trace_result(struct explicit_path* p) {
    /* 源码: convective_Xd.h:129-133 */
    if(SXD_HIT_NONE(&p->ray_result.hit)) {
        /* 无界流体 → 错误 */
        p->state = PATH_ERROR;
        return;
    }

    p->rwalk.hit = p->ray_result.hit;
    p->rwalk.hit_side = (fX(dot)(p->rwalk.hit.normal, dir) < 0)
        ? SDIS_FRONT : SDIS_BACK;

    /* → 进入主循环 */
    p->state = PATH_CNV_SAMPLE_BOUNDARY;
    return;
}
```

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| 已有 hit（从界面启动） | `convective_Xd.h:113` | `!SXD_HIT_NONE(rwalk->hit)` | `CNV_INIT` | `CNV_SAMPLE_BOUNDARY` |
| 无 hit（流体内部） | `convective_Xd.h:113` | `SXD_HIT_NONE(rwalk->hit)` | `CNV_INIT` | `CNV_INIT_TRACE_RESULT` (🔴) |
| 启动射线 miss | `convective_Xd.h:129` | `SXD_HIT_NONE(hit)` | `CNV_INIT_TRACE_RESULT` | `PATH_ERROR` |
| 启动射线 hit | `convective_Xd.h:129` | `!SXD_HIT_NONE(hit)` | `CNV_INIT_TRACE_RESULT` | `CNV_SAMPLE_BOUNDARY` |
