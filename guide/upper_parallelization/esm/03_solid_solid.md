# [solid-solid] 状态迁移映射

> 本文件是 [explicit_state_machine_transition_mapping.md](../explicit_state_machine_transition_mapping.md) 的solid-solid reinjection子模块

## 1. 示例 C：solid_solid_boundary — reinjection 射线发射与决策

### 1.1 CPU 原始代码 (`sdis_heat_path_boundary_Xd_solid_solid.h:32-175`)

这是最复杂的射线嵌套场景之一。关键流程：

```c
/* solid_solid_boundary_path_3d */

/* [solid_solid.h:87-90] 准备 front/back 侧参数 */
samp_reinject_step_frt_args.distance = delta_boundary_frt;
samp_reinject_step_bck_args.distance = delta_boundary_bck;

/* [solid_solid.h:96-102] 🔴 同时采样 front 和 back 两侧的 reinjection step */
res = sample_reinjection_step_solid_solid(
    scn, &frt_args, &bck_args, &step_frt, &step_bck);
// 内部流程 (boundary_Xd_c.h:649-826):
//   1. sample_reinjection_dir(rwalk, rng, dir_frt_samp)             ✅
//   2. reflect(dir_frt_refl, dir_frt_samp, normal)                  ✅
//   3. minus(dir_bck_samp, dir_frt_samp)                            ✅
//   4. minus(dir_bck_refl, dir_frt_refl)                            ✅
//   5. find_reinjection_ray_and_check_validity(frt_args, &ray_frt)  🔴 front 侧
//        └─ find_reinjection_ray(scn, args, ray)
//             ├─ trace_ray(org, dir0)                               🔴 射线 #1
//             ├─ trace_ray(org, dir1)                               🔴 射线 #2
//             ├─ if miss dir0: get_enclosure_id_in_closed_boundaries 🔴 最多 6 条
//             └─ if miss dir1: get_enclosure_id_in_closed_boundaries 🔴 最多 6 条
//        └─ if miss: get_enclosure_id_in_closed_boundaries          🔴 最多 6 条
//   6. find_reinjection_ray_and_check_validity(bck_args, &ray_bck)  🔴 back 侧
//        └─ (同上)

/* [solid_solid.h:130-140] ✅ 概率选择注入侧 */
r = ssp_rng_canonical(rng);
if(r < proba) {
    reinject_step = &step_frt;     // 注入 front
    solid_enc_id = enc_ids[FRONT];
} else {
    reinject_step = &step_bck;     // 注入 back
    solid_enc_id = enc_ids[BACK];
}

/* [solid_solid.h:148-153] solid_reinjection — 执行注入并决定下一步 */
res = solid_reinjection(scn, solid_enc_id, &args);
// 内部判断 (boundary_Xd_c.h:876-886):
//   if(reinjection->hit.distance != reinjection->distance) {
//       T->func = conductive_path;        // → 注入到固体内部
//   } else {
//       T->func = boundary_path;          // → 注入到界面
//   }
```

### 1.2 显式状态拆分

```c
/* ========== 状态 PATH_BND_SS_REINJECT_SAMPLE ========== */
/* 准备 reinjection 方向并发射 4 条射线（front dir0/dir1 + back dir0/dir1） */
/* 源码: boundary_Xd_c.h:732-739 (sample_reinjection_dir) */
/*       boundary_Xd_c.h:752-766 (find_reinjection_ray 内的 trace_ray) */
case PATH_BND_SS_REINJECT_SAMPLE: {
    /* ✅ 采样方向 [boundary_Xd_c.h:732-739] */
    sample_reinjection_dir(&p->rwalk, &p->rng, p->local.bnd_ss.dir_frt_samp);
    reflect(p->local.bnd_ss.dir_frt_refl, p->local.bnd_ss.dir_frt_samp, p->rwalk.hit_3d.normal);
    f3_minus(p->local.bnd_ss.dir_bck_samp, p->local.bnd_ss.dir_frt_samp);
    f3_minus(p->local.bnd_ss.dir_bck_refl, p->local.bnd_ss.dir_frt_refl);

    /* 🔴 发射 4 条射线请求 — 对应 find_reinjection_ray 中的 2×2 trace_ray */
    /* 源码: boundary_Xd_c.h:314-317 (front dir0/dir1) */
    /*       boundary_Xd_c.h:314-317 (back dir0/dir1)  */
    p->ray_slots[0] = make_ray(p->rwalk.vtx.P, p->local.bnd_ss.dir_frt_samp); // front dir0
    p->ray_slots[1] = make_ray(p->rwalk.vtx.P, p->local.bnd_ss.dir_frt_refl); // front dir1
    p->ray_slots[2] = make_ray(p->rwalk.vtx.P, p->local.bnd_ss.dir_bck_samp); // back dir0
    p->ray_slots[3] = make_ray(p->rwalk.vtx.P, p->local.bnd_ss.dir_bck_refl); // back dir1
    p->n_pending_rays = 4;

    p->state = PATH_BND_SS_REINJECT_PROCESS;
    return; /* 挂起 */
}

/* ========== 状态 PATH_BND_SS_REINJECT_PROCESS ========== */
/* 处理 4 条射线结果，判断是否需要额外的 enclosure 查询 */
/* 源码: boundary_Xd_c.h:318-375 (find_reinjection_ray 中的 enclosure 逻辑) */
case PATH_BND_SS_REINJECT_PROCESS: {
    struct s3d_hit hit_frt0 = p->ray_slots[0].result;
    struct s3d_hit hit_frt1 = p->ray_slots[1].result;
    struct s3d_hit hit_bck0 = p->ray_slots[2].result;
    struct s3d_hit hit_bck1 = p->ray_slots[3].result;

    /* ✅ 处理 front 侧 */
    /* 源码: boundary_Xd_c.h:320-330 */
    /* 原始逻辑: if(!SXD_HIT_NONE(&hit0)) → 直接从 hit 获取 enc_id */
    /*           else → 需要 get_enclosure_id_in_closed_boundaries 🔴 */
    int need_enc_frt0 = S3D_HIT_NONE(&hit_frt0);
    int need_enc_frt1 = S3D_HIT_NONE(&hit_frt1);
    int need_enc_bck0 = S3D_HIT_NONE(&hit_bck0);
    int need_enc_bck1 = S3D_HIT_NONE(&hit_bck1);

    /* 从有效 hit 直接提取 enc_id（纯计算） */
    if(!need_enc_frt0) p->local.bnd_ss.enc0_frt = extract_enc_from_hit(hit_frt0, dir_frt0);
    if(!need_enc_frt1) p->local.bnd_ss.enc1_frt = extract_enc_from_hit(hit_frt1, dir_frt1);
    if(!need_enc_bck0) p->local.bnd_ss.enc0_bck = extract_enc_from_hit(hit_bck0, dir_bck0);
    if(!need_enc_bck1) p->local.bnd_ss.enc1_bck = extract_enc_from_hit(hit_bck1, dir_bck1);

    /* --- 判断: 是否需要 enclosure 查询？ --- */
    if(need_enc_frt0 || need_enc_frt1 || need_enc_bck0 || need_enc_bck1) {
        /* 🔴 对 miss 的方向发射 enclosure 查询射线 */
        /* 源码: boundary_Xd_c.h:326-329 → scene_get_enclosure_id_in_closed_boundaries */
        p->n_pending_rays = 0;
        if(need_enc_frt0) emit_enclosure_query_rays(p, compute_reinject_pos(frt, dir0));
        if(need_enc_frt1) emit_enclosure_query_rays(p, compute_reinject_pos(frt, dir1));
        // ...（每个 enclosure 查询最多 6 条射线）
        p->state = PATH_BND_SS_REINJECT_ENC;
        return; /* 挂起 */
    }

    /* 所有 enc_id 已确定，直接进入决策 */
    p->state = PATH_BND_SS_REINJECT_DECIDE;
    /* fall through 或 return */
}

/* ========== 状态 PATH_BND_SS_REINJECT_DECIDE ========== */
/* 根据概率选择注入侧，然后执行 solid_reinjection */
/* 源码: solid_solid.h:108-140 (概率计算) + boundary_Xd_c.h:827-886 (solid_reinjection) */
case PATH_BND_SS_REINJECT_DECIDE: {
    /* ✅ 完成 find_reinjection_ray 后处理逻辑 */
    /* 源码: boundary_Xd_c.h:380-440 (选择 dir0/dir1/threshold) */
    finalize_reinjection_ray(&p->local.bnd_ss.ray_frt, ...);
    finalize_reinjection_ray(&p->local.bnd_ss.ray_bck, ...);

    /* ✅ 概率计算 [solid_solid.h:112-130] */
    /* 原始代码: */
    /* const double tmp_frt = lambda_frt / reinject_step_frt.distance; */
    /* const double tmp_bck = lambda_bck / reinject_step_bck.distance; */
    /* proba = tmp_frt / (tmp_frt + tmp_bck);  (无热接触电阻时) */
    double proba = compute_ss_proba(p);

    /* --- 判断: 注入 front 还是 back？ --- */
    /* 源码: solid_solid.h:132-140 */
    /* 原始代码: r = ssp_rng_canonical(rng); if(r < proba) → front else → back */
    double r = rng_canonical(&p->rng);
    struct reinjection_step* step;
    unsigned solid_enc_id;
    if(r < proba) {
        step = &p->local.bnd_ss.step_frt;
        solid_enc_id = p->local.bnd_ss.enc_ids[SDIS_FRONT];
    } else {
        step = &p->local.bnd_ss.step_bck;
        solid_enc_id = p->local.bnd_ss.enc_ids[SDIS_BACK];
    }

    /* ✅ 执行 solid_reinjection 的纯计算部分 */
    /* 源码: boundary_Xd_c.h:855-868 (time_rewind) */
    solid_reinjection_compute(p, solid_enc_id, step);

    /* --- 判断: time_rewind 后是否已到达极限条件？ --- */
    /* 源码: boundary_Xd_c.h:868 */
    if(p->done) {
        p->state = PATH_DONE;
        return;
    }

    /* --- 判断: 注入到固体内部还是界面？ --- */
    /* 源码: boundary_Xd_c.h:876-886 */
    /* 原始代码: */
    /* if(reinjection->hit.distance != reinjection->distance) → conductive */
    /* else → boundary */
    if(step->hit_3d.distance != step->distance) {
        /* 注入到固体内部 */
        p->rwalk.enc_id = solid_enc_id;
        p->rwalk.hit_3d = S3D_HIT_NULL;
        p->state = PATH_CND_INIT_ENC;          // → 导热路径
    } else {
        /* 注入到界面 */
        p->rwalk.enc_id = ENCLOSURE_ID_NULL;
        p->rwalk.hit_3d = step->hit_3d;
        p->state = PATH_BND_DISPATCH;           // → 再次进入边界分派
    }
    return;
}
```

**关键映射表 — solid/solid reinjection**:

| 判断条件 | 源码位置 | 判断表达式 | 源状态 | 目标状态 |
|---|---|---|---|---|
| reinjection 射线 miss（需 enclosure 查询） | `boundary_Xd_c.h:320` | `SXD_HIT_NONE(&hit0)` | `SS_REINJECT_PROCESS` | `SS_REINJECT_ENC` |
| reinjection 射线均有效 | `boundary_Xd_c.h:342` | `!need_enc_*` (所有都 hit) | `SS_REINJECT_PROCESS` | `SS_REINJECT_DECIDE` |
| 注入 front | `solid_solid.h:132` | `r < proba` | `SS_REINJECT_DECIDE` | (继续处理) |
| 注入 back | `solid_solid.h:138` | `r >= proba` | `SS_REINJECT_DECIDE` | (继续处理) |
| time_rewind 到达极限 | `boundary_Xd_c.h:868` | `T->done` | `SS_REINJECT_DECIDE` | `PATH_DONE` |
| 注入到固体内部 | `boundary_Xd_c.h:876` | `hit.distance != reinject.distance` | `SS_REINJECT_DECIDE` | `PATH_CND_INIT_ENC` |
| 注入到界面 | `boundary_Xd_c.h:882` | `hit.distance == reinject.distance` | `SS_REINJECT_DECIDE` | `PATH_BND_DISPATCH` |