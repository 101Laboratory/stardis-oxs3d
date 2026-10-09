# [enclosure] 状态迁移映射

> 本文件是 [explicit_state_machine_transition_mapping.md](../explicit_state_machine_transition_mapping.md) 的enclosure子模块

## 8. 示例 G：enclosure 查询 — 被多处复用的公共子状态

### 8.1 CPU 原始代码 (`sdis_scene_Xd.h:1318-1385`)

```c
/* scene_get_enclosure_id_in_closed_boundaries_3d */

float dirs[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
float frame[3][3];
f33_rotation(frame, PI/4, PI/4, PI/4);          // ✅

FOR_EACH(idir, 0, 6) {                          // 最多 6 方向
    f33_mulf3(dirs[idir], frame, dirs[idir]);    // ✅ 旋转
    
    scene_view_trace_ray(view, P, dirs[idir], range, NULL, &hit);  // 🔴 射线 #idir

    if(SXD_HIT_NONE(&hit)) continue;            // miss → 试下一个方向
    if(HIT_ON_BOUNDARY(&hit, P, dirs[idir])) continue;  // 边缘命中 → 继续

    if(hit.distance > 1e-6 && |cos_N_dir| > 0.01) {
        enc_id = cos < 0 ? enc_ids[FRONT] : enc_ids[BACK];
        break;    // ✅ 成功确定
    }
}
if(idir >= 6) {                                  // 全部失败
    fallback → scene_get_enclosure_id(遍历所有 primitive)   // 🔴 大量射线
}
```

### 8.2 显式状态设计

由于 enclosure 查询被 **conductive_path**, **find_reinjection_ray**, **sample_next_step_robust** 等多处调用，它应该被设计为**可复用的子状态机**或**内联批量射线请求**。

**策略 A：一次发射全部 6 条射线（推荐）**

```c
/* 将 6 方向射线一次性批量发射，牺牲"提前退出"换取 GPU 友好性 */
static void emit_enclosure_query_batch(struct explicit_path_state* p, const double pos[3]) {
    float dirs[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    float frame[9];
    f33_rotation(frame, PI/4, PI/4, PI/4);

    for(int i = 0; i < 6; i++) {
        f33_mulf3(dirs[i], frame, dirs[i]);
        p->ray_slots[i] = make_ray_f(pos, dirs[i], FLT_MIN, FLT_MAX);
    }
    p->n_pending_rays = 6;
}

/* 解析结果 — 对应 CPU 的 FOR_EACH(idir, 0, 6) 循环 */
static unsigned resolve_enclosure_from_rays(struct explicit_path_state* p) {
    for(int i = 0; i < 6; i++) {
        struct s3d_hit hit = p->ray_slots[i].result;
        if(S3D_HIT_NONE(&hit)) continue;
        /* 源码: sdis_scene_Xd.h:1355-1361 */
        float N[3]; f3_normalize(N, hit.normal);
        float cos_N_dir = f3_dot(N, p->ray_slots[i].direction);
        if(hit.distance > 1e-6f && fabsf(cos_N_dir) > 0.01f) {
            unsigned enc_ids[2];
            scene_get_enclosure_ids(scn, hit.prim.prim_id, enc_ids);
            return cos_N_dir < 0 ? enc_ids[0] : enc_ids[1];
        }
    }
    return ENCLOSURE_ID_NULL; /* 全部失败 → 需要 fallback */
}
```

**封装为"返回地址"pattern**：由于 enclosure 查询被多个状态调用，需要在调用前保存"返回状态"：

```c
/* 调用方式 */
p->local.enc_return_state = PATH_CND_DS_STEP_ADVANCE;  /* 查询完成后返回哪里 */
emit_enclosure_query_batch(p, pos);
p->state = PATH_ENC_QUERY_PENDING;

/* PATH_ENC_QUERY_PENDING 处理结果后 */
case PATH_ENC_QUERY_PENDING: {
    p->local.resolved_enc_id = resolve_enclosure_from_rays(p);
    p->state = p->local.enc_return_state;               /* 返回调用者 */
    return;
}
```
