# distribute 审计: cus3d HEAD

**审计对象**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`  
**函数**: `pool_distribute_ray_results()` (L2092-2402)  
**日期**: 2026-03-07  

---

## 概要

distribute 按 bucket 分三个 Phase 处理路径, 每个 path 的连续操作链如下:

## Phase 1: `bucket_radiative` (RAY_BUCKET_RADIATIVE)

**进入状态**: `PATH_RAD_TRACE_PENDING` + 其他辐射类 pending (由 collect 分桶时确定为单 ray)

| 序号 | 操作 | 写入字段 |
|------|------|---------|
| 1 | 从 `pv->ray_hits[batch_idx]` 读取 hit | — |
| 2 | `hot->needs_ray = 0` | `hot_arr[slot].needs_ray` |
| 3 | `step_radiative_trace(p, hot, scn, h0)` | `hot->phase` (推进到下一状态) + `p->rwalk.*` 等 |
| 4 | 若 step 返回 BAD_OP: `hot->phase = PATH_DONE`, `hot->active = 0`, `p->done_reason = -1` | 3 字段 |
| 5 | `p->steps_taken++` | `path_state.steps_taken` |

**覆盖的 phase**: `PATH_RAD_TRACE_PENDING`, `PATH_BND_SF_NULLCOLL_RAD_TRACE`, `PATH_BND_SFN_RAD_TRACE`, `PATH_BND_EXT_DIFFUSE_TRACE`, `PATH_CND_WOS_FALLBACK_TRACE`, `PATH_COUPLED_BOUNDARY_REINJECT` 等 — 但 Phase 1 实际只走 `step_radiative_trace()`, 其他辐射类在代码层面被分到 Phase 1 bucket 而非 Phase 3。

**实际**: collect bucket 阶段就已按 `RAY_BUCKET_RADIATIVE` 分桶, 对应的 phase 全部走 `step_radiative_trace()`。

---

## Phase 2: `bucket_conductive` (RAY_BUCKET_STEP_PAIR)

**进入状态**: `PATH_COUPLED_COND_DS_PENDING` / `PATH_CND_DS_STEP_TRACE`

| 序号 | 操作 | 写入字段 |
|------|------|---------|
| 1 | 从 `pv->ray_hits[batch_idx]` 读取 h0; 若 `ray_count >= 2` 读取 h1 | — |
| 2 | `hot->needs_ray = 0` | `hot_arr[slot].needs_ray` |
| 3 | `step_conductive_ds_process(p, hot, scn, h0, h1, &enc_arr[i])` | `hot->phase` + `p->ds_*` + 可能写 `enc_arr` |
| 4 | 若 BAD_OP: 同上标记失败 | 3 字段 |
| 5 | `p->steps_taken++` | `path_state.steps_taken` |

---

## Phase 3: `bucket_other` (所有非 radiative/conductive 的 ray path)

Phase 3 包含 **多种 phase**, 每种有不同的 **前置数据拷贝** + 统一的 `advance_one_step_with_ray()` 调用。按 phase 分组:

### 3a. `PATH_ENC_QUERY_EMIT` (6-ray enclosure query)

| 序号 | 操作 | 写入字段 |
|------|------|---------|
| 1 | 读 h0 (= `ray_hits[batch_idx]`) | — |
| 2 | **预拷贝 6 条 hit**: `enc_arr[i].dir_hits[0..5] = ray_hits[enc_arr[i].batch_indices[0..5]]` | `enc_arr[slot].dir_hits[6]` |
| 3 | `hot->needs_ray = 0` | `hot_arr[slot].needs_ray` |
| 4 | `advance_one_step_with_ray()` → **`step_enc_query_resolve()`** | `hot->phase`, `enc_arr` |
| 5 | 若 BAD_OP: 标记失败 | 3 字段 |
| 6 | `p->steps_taken++` | `path_state.steps_taken` |

### 3b. `PATH_ENC_QUERY_FB_EMIT` (fallback 1-ray enclosure)

| 序号 | 操作 | 写入字段 |
|------|------|---------|
| 1 | 读 h0 | — |
| 2 | **预拷贝 fb_hit**: `enc_arr[i].fb_hit = ray_hits[batch_idx]` | `enc_arr[slot].fb_hit` |
| 3 | `hot->needs_ray = 0` | `hot_arr[slot].needs_ray` |
| 4 | `advance_one_step_with_ray()` → **`step_enc_query_fb_resolve()`** | `hot->phase`, `enc_arr` |
| 5 | **若 step 后 phase == PATH_ENC_LOCATE_PENDING**: 计数 `enc_query_escalated_to_m10++` | pool 统计 |
| 6 | 若 BAD_OP: 标记失败 | 3 字段 |
| 7 | `p->steps_taken++` | `path_state.steps_taken` |

### 3c. `PATH_BND_SS_REINJECT_SAMPLE` (solid-solid 4-ray)

| 序号 | 操作 | 写入字段 |
|------|------|---------|
| 1 | 读 h0, 可能 h1 | — |
| 2 | **预拷贝 4 条 hit** (当 `ray_count_ext == 4`): | |
|    | `bnd_ss.ray_frt[0] = ray_hits[batch_idx_frt0]` | `locals.bnd_ss.ray_frt[0]` |
|    | `bnd_ss.ray_frt[1] = ray_hits[batch_idx_frt1]` | `locals.bnd_ss.ray_frt[1]` |
|    | `bnd_ss.ray_bck[0] = ray_hits[batch_idx_bck0]` | `locals.bnd_ss.ray_bck[0]` |
|    | `bnd_ss.ray_bck[1] = ray_hits[batch_idx_bck1]` | `locals.bnd_ss.ray_bck[1]` |
| 3 | `hot->needs_ray = 0` | `hot_arr[slot].needs_ray` |
| 4 | `advance_one_step_with_ray()` → **`step_bnd_ss_reinject_process()`** | `hot->phase`, `enc_arr`, `p->locals.bnd_ss.*` |
| 5 | 若 BAD_OP: 标记失败 | 3 字段 |
| 6 | `p->steps_taken++` | `path_state.steps_taken` |

### 3d. 其余 phase (无前置拷贝, 直接走 step)

以下 phase 进入 `advance_one_step_with_ray()` 的 switch, 无额外预处理:

| Phase | Step 函数 | 消费的 hit |
|-------|----------|-----------|
| `PATH_BND_SF_REINJECT_SAMPLE` | `step_bnd_sf_reinject_process()` | h0 + h1 |
| `PATH_BND_SF_NULLCOLL_RAD_TRACE` | `step_bnd_sf_nullcoll_rad_trace()` | h0 |
| `PATH_BND_EXT_DIRECT_TRACE` | `step_bnd_ext_direct_result()` | h0 |
| `PATH_BND_EXT_DIFFUSE_TRACE` | `step_bnd_ext_diffuse_result()` | h0 |
| `PATH_BND_EXT_DIFFUSE_SHADOW_TRACE` | `step_bnd_ext_diffuse_shadow_result()` | h0 |
| `PATH_BND_SFN_RAD_TRACE` | `step_bnd_sfn_rad_trace()` | h0 |
| `PATH_CND_WOS_FALLBACK_TRACE` | `step_cnd_wos_fallback_result()` | h0 |
| `PATH_CNV_STARTUP_TRACE` | `step_cnv_startup_result()` | h0 |

每条路径的操作链均为: **clear needs_ray → step → fail check → steps_taken++**。

---

## gpu_postprocess 中 distribute 之后的同步操作

distribute 返回后, `gpu_postprocess()` (L3308-3378) 紧接着执行:

### enc_locate 部分 (L3325-3349)

| 序号 | 操作 |
|------|------|
| 1 | `pool_collect_enc_locate_requests()` — 扫描 hot_arr 找 `PATH_ENC_LOCATE_PENDING`, 收集 query_pos |
| 2 | `s3d_scene_view_find_enclosure_batch_ctx()` — **同步 GPU 调用** |
| 3 | `pool_distribute_enc_locate_results()` — 写 `enc_arr[slot].locate.{prim_id, side, distance}` + `phase = PATH_ENC_LOCATE_RESULT` |

### cp 部分 (L3352-3376)

| 序号 | 操作 |
|------|------|
| 1 | `pool_collect_cp_requests()` — 扫描 hot_arr 找 `PATH_CND_WOS_CLOSEST` / `DIFFUSION_CHECK` |
| 2 | `s3d_scene_view_closest_point_batch_ctx()` — **同步 GPU 调用** |
| 3 | `pool_distribute_cp_results()` — 写 `cnd_wos.cached_hit` + `phase = *_RESULT` |

---

## 汇总: distribute 对同一路径的完整操作序列

```
每条路径:
  ┌─ 可选: 预拷贝 multi-ray hits (仅 ENC_QUERY_EMIT 6条, ENC_QUERY_FB_EMIT 1条, BND_SS 4条)
  │         写入目标: enc_arr.dir_hits / enc_arr.fb_hit / locals.bnd_ss.ray_frt/bck
  │
  ├─ hot->needs_ray = 0                  ← 清除 ray-pending 标志
  │
  ├─ step_*() 函数调用                   ← 消费 hit, 推进 hot->phase + 写 path_state 内部状态
  │   (具体 step 函数由 phase 决定)
  │
  ├─ 失败处理 (若 step 返回 BAD_OP)     ← phase=DONE, active=0, done_reason=-1
  │
  └─ p->steps_taken++                    ← 计步
```

## 所有 phase → step 映射

| Bucket | 入口 Phase | Step 函数 | 预拷贝 |
|--------|-----------|----------|--------|
| radiative | `PATH_RAD_TRACE_PENDING` | `step_radiative_trace` | 无 |
| conductive | `PATH_COUPLED_COND_DS_PENDING` | `step_conductive_ds_process` | 无 |
| conductive | `PATH_CND_DS_STEP_TRACE` | `step_conductive_ds_process` | 无 |
| other | `PATH_ENC_QUERY_EMIT` | `step_enc_query_resolve` | 6-ray → `enc_arr.dir_hits` |
| other | `PATH_ENC_QUERY_FB_EMIT` | `step_enc_query_fb_resolve` | 1-ray → `enc_arr.fb_hit` |
| other | `PATH_BND_SS_REINJECT_SAMPLE` | `step_bnd_ss_reinject_process` | 4-ray → `bnd_ss.ray_frt/bck` |
| other | `PATH_BND_SF_REINJECT_SAMPLE` | `step_bnd_sf_reinject_process` | 无 |
| other | `PATH_BND_SF_NULLCOLL_RAD_TRACE` | `step_bnd_sf_nullcoll_rad_trace` | 无 |
| other | `PATH_BND_EXT_DIRECT_TRACE` | `step_bnd_ext_direct_result` | 无 |
| other | `PATH_BND_EXT_DIFFUSE_TRACE` | `step_bnd_ext_diffuse_result` | 无 |
| other | `PATH_BND_EXT_DIFFUSE_SHADOW_TRACE` | `step_bnd_ext_diffuse_shadow_result` | 无 |
| other | `PATH_BND_SFN_RAD_TRACE` | `step_bnd_sfn_rad_trace` | 无 |
| other | `PATH_CND_WOS_FALLBACK_TRACE` | `step_cnd_wos_fallback_result` | 无 |
| other | `PATH_CNV_STARTUP_TRACE` | `step_cnv_startup_result` | 无 |
| other | `PATH_COUPLED_BOUNDARY_REINJECT` | *(走 default → FATAL)* | 无 |
| other | `PATH_CND_INIT_ENC` | *(FATAL: 未激活的 future state)* | 无 |

共 **15 种可进入 distribute 的 phase**, 其中 13 种有对应 step 函数, 2 种是 fatal guard。
需要前置 multi-ray 预拷贝的有 **3 种** (ENC_QUERY_EMIT, ENC_QUERY_FB_EMIT, BND_SS_REINJECT_SAMPLE)。
