# 热传导路径 `path_state` 字段冷热分析

**生成时间**: 2026-02-27  
**分析范围**: Delta-Sphere (M4) + WoS (M9) 导热主循环路径  
**目的**: 为 AoS→SoA 内存布局转换提供字段访问频率依据

---

## 1. 热传导路径状态流

热传导是蒙特卡洛辐射传输的**绝对主导路径**——每条 MC 路径在辐射追踪命中吸收面后进入 `boundary → conductive` 循环，导热步在循环体内**每次迭代都执行**，直到温度已知或命中边界。

### 1.1 Delta-Sphere (M4) 状态流 — 主循环

```
step_conductive (entry, once)
  → step_enc_query_emit         [R] 6 rays         PATH_ENC_QUERY_EMIT
  → step_enc_query_resolve      [C]                 (→ fallback if needed)
  ┌─────────────────────────────────── loop ──────────────────────────────────┐
  │ step_cnd_ds_check_temp      [C] init + props    PATH_CND_DS_CHECK_TEMP   │
  │   → setup_delta_sphere_rays                                               │
  │ step_conductive_ds_process  [C] 2-hit process   PATH_CND_DS_STEP_TRACE   │
  │   → (enc resolved from hit)  OR                                           │
  │   → step_cnd_ds_step_enc_verify → step_enc_query_emit [R] 6 rays        │
  │ step_cnd_ds_step_advance    [C] move+time       PATH_CND_DS_STEP_ADVANCE │
  │   → loop back to CHECK_TEMP                                               │
  └──────────────────────────────────────────────────────────────────────────┘
  exit: PATH_DONE (temp known / time rewind) | PATH_COUPLED_BOUNDARY (hit)
```

### 1.2 WoS (M9) 状态流 — 主循环

```
step_conductive (entry, once)
  → step_enc_query_emit         [R] 6 rays         PATH_ENC_QUERY_EMIT
  → step_enc_query_resolve      [C]
  ┌─────────────────────────────────── loop ──────────────────────────────────┐
  │ step_cnd_wos_check_temp     [C] init + props    PATH_CND_WOS_CHECK_TEMP  │
  │ step_cnd_wos_closest        [CP] batch CP       PATH_CND_WOS_CLOSEST     │
  │ step_cnd_wos_closest_result [C] ε-shell/diffuse PATH_CND_WOS_CLOSEST_RES │
  │   → step_cnd_wos_diffusion_check [CP] validate  PATH_CND_WOS_DIFF_CHECK  │
  │   → step_cnd_wos_diffusion_check_result [C]                               │
  │     → (valid: move) OR step_cnd_wos_fallback_trace [R]                    │
  │     → step_cnd_wos_fallback_result [C]                                    │
  │ step_cnd_wos_time_travel    [C] time+power      PATH_CND_WOS_TIME_TRAVEL │
  │   → loop back to CHECK_TEMP                                               │
  └──────────────────────────────────────────────────────────────────────────┘
  exit: PATH_DONE (temp known / initial cond) | PATH_COUPLED_BOUNDARY (hit)
```

---

## 2. 字段冷热分级定义

| 级别 | 含义 | SoA 策略 |
|------|------|----------|
| **🔥 HOT** | 每次循环迭代都 R/W | 放入连续 SoA 数组，coalesced 访问 |
| **🟡 WARM** | 每次循环读，偶尔写 | 同 HOT 或独立数组 |
| **🟢 INIT** | 仅入口/出口时访问 | 独立数组，不与 HOT 混排 |
| **❄️ COLD** | 导热路径完全不访问 | 可从 SoA 排除或放最末尾 |

---

## 3. Delta-Sphere (M4) 主循环字段访问表

每行 = `path_state` 的一个顶层字段/子结构，列 = 各 step 函数中的读(R)/写(W)。

### 3.1 核心标量 & 生命周期

| 字段 | 大小 | check_temp | setup_ds_rays | ds_process | ds_step_advance | 冷热 |
|------|------|-----------|---------------|------------|-----------------|------|
| `phase` | 4B | W | – | W | W | 🔥 HOT |
| `active` | 4B | W(exit) | – | – | W(exit) | 🟢 INIT |
| `needs_ray` | 4B | – | W | W | W | 🔥 HOT |
| `done_reason` | 4B | W(exit) | – | – | W(exit) | 🟢 INIT |
| `steps_taken` | 8B | – | – | – | – | ❄️ COLD |
| `path_id` | 4B | – | – | – | – | ❄️ COLD |
| `pixel_x/y` | 4B | – | – | – | – | ❄️ COLD |
| `realisation_idx` | 4B | – | – | – | – | ❄️ COLD |

### 3.2 Random Walk 核心 (`rwalk.*`)

| 字段 | 大小 | check_temp | setup_ds_rays | ds_process | ds_step_advance | 冷热 |
|------|------|-----------|---------------|------------|-----------------|------|
| `rwalk.vtx.P[3]` | 24B | R(props) | R(origin) | R(pos_next) | R+W(move) | 🔥 HOT |
| `rwalk.vtx.time` | 8B | – | – | – | R(time_rewind) | 🟡 WARM |
| `rwalk.enc_id` | 4B | R+W(init) | – | – | W(exit→bnd) | 🟢 INIT |
| `rwalk.hit_3d` | ~48B | – | – | – | W(update hit) | 🟡 WARM |
| `rwalk.hit_side` | 4B | – | – | – | W | 🟡 WARM |
| `rwalk.elapsed_time` | 8B | – | – | – | R+W(time_rewind) | 🟡 WARM |
| `rwalk.dir[3]` | 24B | – | – | – | – | ❄️ COLD |
| `rwalk.hit_2d` | ~40B | – | – | – | – | ❄️ COLD |

### 3.3 Temperature (`T.*`)

| 字段 | 大小 | check_temp | setup_ds_rays | ds_process | ds_step_advance | 冷热 |
|------|------|-----------|---------------|------------|-----------------|------|
| `T.value` | 8B | W(exit) | – | – | R+W(power,time) | 🔥 HOT |
| `T.done` | 4B | W(exit) | – | – | R+W(time) | 🟡 WARM |
| `T.func` | 8B | – | – | – | W(exit→bnd) | 🟢 INIT |

### 3.4 Delta-Sphere 专用字段 (`ds_*`)

| 字段 | 大小 | check_temp | setup_ds_rays | ds_process | ds_step_advance | 冷热 |
|------|------|-----------|---------------|------------|-----------------|------|
| `ds_dir0[3]` | 12B | – | W | R(dot,swap) | R(move) | 🔥 HOT |
| `ds_dir1[3]` | 12B | – | W | R+W(swap) | – | 🔥 HOT |
| `ds_hit0` | ~48B | – | – | W(store) | R(check) | 🔥 HOT |
| `ds_hit1` | ~48B | – | – | W(store) | – | 🟡 WARM |
| `ds_delta_solid_param` | 4B | W | – | R(calc) | R(power) | 🔥 HOT |
| `ds_delta` | 4B | – | – | W | R(move,power,time) | 🔥 HOT |
| `ds_enc_id` | 4B | W(init) | – | R(verify) | R(verify) | 🔥 HOT |
| `ds_medium` | 8B | R(init) | – | – | R(props,green) | 🔥 HOT |
| `ds_initialized` | 4B | R+W(init) | – | – | W(exit→bnd) | 🟡 WARM |
| `ds_props_ref` | ~80B | W(init) | – | – | R(green) | 🟢 INIT |
| `ds_green_power_term` | 8B | W(init=0) | – | – | R+W(power) | 🟡 WARM |
| `ds_position_start[3]` | 24B | W(init) | – | – | – | 🟢 INIT |
| `ds_robust_attempt` | 4B | W(init) | – | R+W(retry) | R+W(reset) | 🟡 WARM |
| `ds_delta_solid` | 8B | – | – | – | – | ❄️ COLD |

### 3.5 共享基础设施

| 字段 | 大小 | check_temp | setup_ds_rays | ds_process | ds_step_advance | 冷热 |
|------|------|-----------|---------------|------------|-----------------|------|
| `rng` | 8B | – | R(sphere_uniform) | – | – | 🔥 HOT |
| `rng_state` | ~32B | – | (via rng ptr) | – | (via rng ptr) | 🟡 WARM |
| `ray_req` | ~64B | – | W(full) | – | – | 🔥 HOT |
| `ray_bucket` | 4B | – | W | – | – | 🔥 HOT |
| `ray_count_ext` | 4B | – | W | – | – | 🔥 HOT |
| `filter_data_storage` | ~64B | – | W | – | – | 🔥 HOT |
| `ctx.*` (整体) | ~104B | R(green,heat) | – | – | R(green,heat,time) | 🟡 WARM |

### 3.6 导热路径不访问的字段 (❄️ COLD)

| 字段组 | 总大小 (估算) |
|--------|-------------|
| `rad_direction[3]`, `rad_bounce_count`, `rad_retry_count` | ~20B |
| `coupled_nbranchings` | 4B |
| `bnd_hit0/1`, `bnd_reinject_distance`, `bnd_solid_enc_id`, `bnd_retry_count` | ~112B |
| `ipix_image[2]` | 16B |
| `locals.bnd_ss` (union, 导热路径不用) | – |
| `locals.bnd_sf` (union, 导热路径不用) | – |
| `locals.cnv` (union, 导热路径不用) | – |
| `ext_flux` (整体) | ~256B |
| `sfn_stack[3]` + `sfn_stack_depth` | ~600B+ |
| `enc_locate` (仅 M10 fallback 时短暂使用) | ~40B |

---

## 4. WoS (M9) 主循环字段访问表

WoS 使用 `locals.cnd_wos` union 分支代替 `ds_*` 字段。

### 4.1 核心标量 (同 DS，差异标注)

| 字段 | wos_check_temp | wos_closest | wos_closest_result | wos_diff_check_result | wos_fallback_* | wos_time_travel | 冷热 |
|------|---------------|-------------|-------------------|----------------------|---------------|----------------|------|
| `phase` | W | W | W | W | W | W | 🔥 HOT |
| `needs_ray` | – | W | – | – | W | – | 🔥 HOT |
| `T.value` | W(exit) | – | – | – | – | R+W | 🔥 HOT |
| `T.done` | W(exit) | – | – | – | – | R+W | 🟡 WARM |
| `T.func` | – | – | – | – | – | W(exit) | 🟢 INIT |
| `rwalk.vtx.P[3]` | R(props) | R(query) | R(shell) | – | R(origin) | R+W | 🔥 HOT |
| `rwalk.vtx.time` | – | – | – | – | – | R+W | 🟡 WARM |
| `rwalk.enc_id` | R(init) | – | – | R(verify) | – | W(exit) | 🟡 WARM |
| `rwalk.hit_3d` | – | – | W(e-shell) | – | W | R+W | 🟡 WARM |
| `rwalk.hit_side` | – | – | W(e-shell) | – | W | – | 🟡 WARM |
| `rwalk.elapsed_time` | – | – | – | – | – | R+W | 🟡 WARM |
| `rng` | – | – | R(sphere) | – | – | R(canonical) | 🔥 HOT |
| `ray_req` | – | – | – | – | W(fallback) | – | 🟡 WARM |
| `ctx.*` | R(green,heat) | – | – | – | – | R(green,heat) | 🟡 WARM |

### 4.2 WoS 专用字段 (`locals.cnd_wos.*`)

| 字段 | 大小 | check_temp | closest | closest_result | diff_check_result | fallback_* | time_travel | 冷热 |
|------|------|-----------|---------|---------------|-------------------|-----------|-------------|------|
| `query_pos[3]` | 24B | – | W | – | W(diff) | – | – | 🔥 HOT |
| `query_radius` | 8B | – | W | – | W(diff) | – | – | 🔥 HOT |
| `new_pos[3]` | 12B | – | W | – | – | – | – | 🟡 WARM |
| `dir[3]` | 12B | – | – | W(sphere) | – | R(fallback ray) | – | 🟡 WARM |
| `cached_hit` | ~48B | – | – | R(dist,snap) | R(enc_ids) | R(snap) | – | 🔥 HOT |
| `delta` | 8B | W | – | R(e-shell) | R(cp radius) | – | – | 🔥 HOT |
| `last_distance` | 8B | – | – | W | – | – | R(time,power) | 🔥 HOT |
| `alpha` | 8B | W(init) | – | – | – | – | R(time_travel) | 🔥 HOT |
| `enc_id` | 4B | W(init) | – | – | – | – | – | 🟢 INIT |
| `medium` | 8B | R(init) | – | – | – | – | R(green) | 🟡 WARM |
| `props_ref` | ~80B | W(init) | – | – | – | – | R(green) | 🟢 INIT |
| `props` | ~80B | R+W | – | – | – | – | R(power) | 🔥 HOT |
| `green_power_term` | 8B | W(init=0) | – | – | – | – | R+W(power) | 🟡 WARM |
| `position_start[3]` | 24B | W | – | – | – | – | R(time) | 🟡 WARM |
| `wos_initialized` | 4B | R+W | – | – | – | – | W(exit) | 🟡 WARM |
| `batch_cp_idx` | 4B | – | W | – | – | – | – | 🟡 WARM |
| `diffusion_pos[3]` | 24B | – | – | W | R | – | – | 🟡 WARM |
| `batch_cp2_idx` | 4B | – | – | – | – | – | – | 🟢 INIT |

### 4.3 WoS 路径的 COLD 字段 (与 DS 相同 + DS 专用字段)

| 字段组 | 说明 |
|--------|------|
| `ds_*` (全部 ds_ 前缀) | WoS 不使用 delta-sphere 字段 |
| 其余同 §3.6 | 辐射/边界/ext_flux/sfn 等 |

---

## 5. ENC Query 子路径字段访问 (导热入口一次性)

ENC query 在导热`入口`和 DS `enc_verify` 时触发，**不在主内循环每次迭代执行**（除非 enc mismatch retry）。

| 字段 (`enc_query.*`) | 大小 | enc_query_emit | enc_query_resolve | enc_query_fb_resolve | 冷热 (导热视角) |
|----------------------|------|---------------|------------------|---------------------|----------------|
| `query_pos[3]` | 24B | W | – | R(fb) | 🟢 INIT |
| `return_state` | 4B | W | R | R | 🟢 INIT |
| `resolved_enc_id` | 4B | W(null) | W | W | 🟢 INIT |
| `directions[6][3]` | 72B | W | R | – | 🟢 INIT |
| `dir_hits[6]` | ~288B | W(null) | R | – | 🟢 INIT |
| `batch_indices[6]` | 24B | W | – | – | 🟢 INIT |
| `fb_direction[3]` | 12B | – | W | R | ❄️ COLD (rare) |
| `fb_hit` | ~48B | – | W | R | ❄️ COLD (rare) |
| `fb_batch_idx` | 4B | – | W | – | ❄️ COLD (rare) |

---

## 6. 汇总统计

### 6.1 字段冷热分布 (Delta-Sphere M4 主循环视角)

| 级别 | 字段数 | 估算访问字节/迭代 | 占 path_state 比例 |
|------|--------|-------------------|-------------------|
| **🔥 HOT** | ~18 | ~300 B | ~7% |
| 🟡 WARM | ~14 | ~250 B | ~6% |
| 🟢 INIT | ~10 | ~240 B | ~6% |
| ❄️ COLD | ~25+ | ~3300 B | ~81% |

### 6.2 字段冷热分布 (WoS M9 主循环视角)

| 级别 | 字段数 | 估算访问字节/迭代 | 占 path_state 比例 |
|------|--------|-------------------|-------------------|
| **🔥 HOT** | ~14 | ~280 B | ~7% |
| 🟡 WARM | ~16 | ~300 B | ~7% |
| 🟢 INIT | ~8 | ~200 B | ~5% |
| ❄️ COLD | ~27+ | ~3300 B | ~81% |

### 6.3 SoA Hot-Tier 候选 (两种算法共用)

以下字段在**两种**导热算法的内循环中均为 HOT，应优先放入 SoA 连续数组：

| 字段 | 类型 | 大小 | 用途 |
|------|------|------|------|
| `phase` | `enum` (int) | 4B | 状态机分派 |
| `needs_ray` | `int` | 4B | 射线请求标记 |
| `rwalk.vtx.P[3]` | `double[3]` | 24B | 空间位置 (最核心) |
| `T.value` | `double` | 8B | 累积温度 |
| `rng` | `ptr` | 8B | RNG 指针 |
| `ray_req` (部分) | struct | ~32B | origin + direction + range |

**DS 独有 HOT**:  
`ds_dir0/1`, `ds_hit0`, `ds_delta`, `ds_delta_solid_param`, `ds_enc_id`, `ds_medium`

**WoS 独有 HOT**:  
`locals.cnd_wos.{query_pos, query_radius, cached_hit, delta, last_distance, alpha, props}`

---

## 7. SoA 转换建议

1. **Tier-0 (必须 SoA)**: `phase`, `needs_ray`, `rwalk.vtx.P[3]`, `T.value` — 每个 warp 的 32 条路径必须 coalesced 访问这些字段
2. **Tier-1 (强烈建议)**: `rng`, `ray_req.{origin,direction,range}`, `T.done` — 每次迭代访问
3. **Tier-2 (算法特定)**: DS 的 `ds_*` 或 WoS 的 `locals.cnd_wos.*` — 取决于场景选择哪种算法；可按算法分 kernel
4. **Tier-3 (保持 AoS 或稀疏)**: `ctx.*`, `enc_query.*`, `ext_flux.*`, `sfn_stack[]` — 低频访问，AoS 对 cache line 影响小
5. **排除**: `rwalk.hit_2d`, `rad_*`, `bnd_*`, `ipix_image` — 导热路径完全不触碰

> **关键观察**: 导热主循环每次迭代实际只访问 ~300B / 路径，但 `path_state` 实际大小 ~4KB
> （原始设计估算 2.2KB，M5 bnd_sf snapshot 扩展后涨至 ~4KB，见
> `sdis_wf_state.h` L62 注释 及 `test_sdis_b4_m1_enclosure_batch.c:407` 的 4096 断言）。
> **~81% 的数据在导热路径中完全不被访问**。SoA 分离后，L1/L2 cache 利用率可提升 **~5x**。
