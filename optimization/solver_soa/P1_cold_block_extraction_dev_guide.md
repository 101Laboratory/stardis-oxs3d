# P1: 超大冷块移出 — 开发指南

**创建时间**: 2026-02-27  
**阶段**: SoA 二期工程 · Phase 2A  
**前置**: P0（调度层 SoA 镜像）已完成  
**目标**: 将 `path_state` 中三个超大冷数据块（`sfn_stack[3]`、`enc_query`、`ext_flux`）移出为独立 SoA 数组，slot 尺寸从 ~6.7KB 降至 ~2.0KB  
**预期收益**: cascade re-entry 加载量 ↓3.3×；OMP chunk 工作集 64×2KB=128KB（≤L2）vs 64×6.7KB=430KB（超 L2）  
**预计工作量**: ~500 行改动 / ~5 天

---

## 0. 背景与修正模型

### 0.1 path_state 实际尺寸：~6688B

P0 注释中估计 ~4KB/path，但 PicardN M8 扩展后实际结构体约 **6688 bytes**。

| 区域 | 大小 | 占比 |
|------|------|------|
| identity + lifecycle + phase | 20B | 0.3% |
| `rwalk` | 176B | 2.6% |
| `ctx` (rwalk_context) | 96B | 1.4% |
| `T` (temperature) | 24B | 0.4% |
| `rad_*` 辐射 scratch | 20B | 0.3% |
| `ds_*` DS 导热字段 | 232B | 3.5% |
| `bnd_*` 边界 scratch | 128B | 1.9% |
| `filter_data_storage` | 144B | 2.2% |
| `ray_req` | 64B | 1.0% |
| `rng*` + `rng_state` | 112B | 1.7% |
| `union locals` | 928B | 13.9% |
| **`ext_flux`** | **360B** | **5.4%** |
| **`enc_query`** + **`enc_locate`** | **592B** | **8.8%** |
| **`sfn_stack[3]`** | **3696B** | **55.3%** |
| 其余（diag/ipix/etc） | ~96B | 1.4% |

主要贡献者：
- **`sfn_stack[3]`**（55.3%）：每元素含 `rwalk_saved`(176B) + `T_saved`(24B) + `bnd_sf_backup`(928B) ≈ 1232B × 3
- **`enc_query`**（544B）：6-ray 方向/命中/批索引 + fallback
- **`ext_flux`**（360B）：M7 外部通量全状态

### 0.2 修正的 cascade 行为模型

整个路径生命周期累计上万次 cascade 迭代，但中间频繁被 `needs_ray`、`cp-pending`、`enc-locate-pending` 打断。**单次 cascade 爆发长度按路径类型差异很大**：

| 链 | 典型爆发长度 | 中断类型 |
|-----|------------|----------|
| CND_DS（导热 DS 循环） | 2 步 | needs_ray (2 delta-sphere rays) |
| CND_WOS（导热 WoS 循环） | 3 步 | cp-pending |
| CNV（对流 null-collision） | 1步×N（自循环，无上限） | 无中断直到 accept |
| BND_SF（SF 边界） | 3-8 步 | needs_ray |
| BND_SFN（PicardN Ti check） | 2-12 步 | 递归子路径或 accept |
| BND_SS（SS 边界） | 3 步 | needs_ray (4 rays) |

**关键洞察**：DS 路径（最热循环）每次爆发仅 2 步后中断，经历 collect→trace→distribute 后 re-entry，每次必须重载整个 6.7KB slot 但只使用 ~700B。**9.5× 浪费**。

### 0.3 本阶段策略

移出三个冷块（4648B，占 70%），将 `path_state` 压缩至 ~2040B。不改动 step 函数签名（仍传 `struct path_state *p`），通过额外参数或 pool 指针间接访问移出的数据。

---

## 1. 新增数据结构

文件：[sdis_wf_state.h](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_state.h)

### 1.1 `struct path_sfn_data`（~3700B）

从 `path_state` 移出 `sfn_stack[MAX_PICARD_DEPTH]` 和 `sfn_stack_depth`：

```c
/* PicardN recursive stack — SoA-separated from path_state.
 * Only accessed by BND_SFN step functions and cascade intercept. */
struct path_sfn_data {
    struct {
        enum path_phase return_state;
        double  partial_temperature;
        struct rwalk rwalk_saved;
        struct temperature T_saved;
        double  T_values[6];
        int     T_count;
        double  r, p_conv, p_cond, h_hat;
        struct path_bnd_sf_locals bnd_sf_backup;
    } stack[MAX_PICARD_DEPTH];
    int depth;  /* was sfn_stack_depth */
};
```

### 1.2 `struct path_enc_data`（~596B）

从 `path_state` 移出 `enc_query` 和 `enc_locate`：

```c
/* 6-ray enclosure query + enc_locate state.
 * Only accessed by ENC step functions, DS enc_verify, and collect/distribute. */
struct path_enc_data {
    /* M1-v2: 6-ray enclosure query */
    double   query_pos[3];
    enum path_phase return_state;
    unsigned resolved_enc_id;
    float    directions[6][3];
    struct s3d_hit dir_hits[6];
    uint32_t batch_indices[6];
    float    fb_direction[3];
    struct s3d_hit fb_hit;
    uint32_t fb_batch_idx;

    /* M10: point-in-enclosure via BVH */
    struct {
        double   query_pos[3];
        enum path_phase return_state;
        unsigned resolved_enc_id;
        int32_t  prim_id;
        int32_t  side;
        float    distance;
        uint32_t batch_idx;
    } locate;
};
```

### 1.3 `struct path_ext_data`（~360B）

从 `path_state` 移出 `ext_flux`：

```c
/* M7 external net flux sub-chain state.
 * Only accessed by BND_EXT step functions and step_bnd_sf_prob_dispatch. */
struct path_ext_data {
    float   pos[3];
    float   dir[3];
    float   range;
    struct s3d_hit hit;

    double  flux_direct;
    double  flux_diffuse_reflected;
    double  flux_scattered;
    double  scattered_dir[3];
    int     nbounces;

    double  emissivity;
    double  sum_h;
    double  cos_theta;
    double  N[3];
    unsigned enc_id_fluid;
    struct source_props  src_props;
    struct source_sample src_sample;

    double  frag_time;
    double  frag_P[3];

    struct green_path_handle* green_path;
    enum path_phase return_state;
};
```

### 1.4 修改后的 `struct path_state`（~2040B）

从 `path_state` 中**删除**以下字段：
- `sfn_stack[MAX_PICARD_DEPTH]` 及 `sfn_stack_depth`
- 匿名 struct `ext_flux`
- 匿名 struct `enc_query`
- 匿名 struct `enc_locate`

`path_state` 保留：identity、lifecycle、rwalk、ctx、T、rad_*、ds_*、bnd_*、filter_data_storage、ray_req、needs_ray、rng*、rng_state、ipix_image、union locals、steps_taken、done_reason、ray_bucket、ray_count_ext。

---

## 2. Pool 结构变更

文件：[sdis_solve_persistent_wavefront.h](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h)

在 `struct wavefront_pool`（[L151](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h#L151)）中添加：

```c
/* SoA cold-block arrays (P1: separated from path_state slots) */
struct path_sfn_data *sfn_arr;     /* [pool_size], PicardN stack        */
struct path_enc_data *enc_arr;     /* [pool_size], enclosure query       */
struct path_ext_data *ext_arr;     /* [pool_size], external net flux     */
```

这些数组与 `slots[i]` 通过相同索引 `i` 关联。

---

## 3. 分步实施清单

### Step 1: 定义新结构体 + 修改 path_state

**文件**: [sdis_wf_state.h](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_state.h)

1. 在 `struct path_state` **之前**定义 `struct path_sfn_data`、`struct path_enc_data`、`struct path_ext_data`
2. 从 `struct path_state` 删除 `sfn_stack[MAX_PICARD_DEPTH]`、`sfn_stack_depth`、`ext_flux { ... }`、`enc_query { ... }`、`enc_locate { ... }`
3. 在 `struct path_state` 末尾添加注释指引到 SoA 数组

**验证**: 编译应失败，引用这些字段的代码都会报错——这正是我们的修改清单指引

---

### Step 2: Pool 分配/释放

**文件**: [sdis_solve_persistent_wavefront.h](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h) + [sdis_solve_persistent_wavefront.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c)

1. 在 `struct wavefront_pool` 中添加 3 个 SoA 数组指针（见 §2）
2. 在 `pool_create()`（[L323](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L323)）中分配：
   ```c
   pool->sfn_arr = (struct path_sfn_data*)calloc(pool_size, sizeof(struct path_sfn_data));
   pool->enc_arr = (struct path_enc_data*)calloc(pool_size, sizeof(struct path_enc_data));
   pool->ext_arr = (struct path_ext_data*)calloc(pool_size, sizeof(struct path_ext_data));
   ```
3. 在 `pool_destroy()`（[L433](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L433)）中释放
4. 可选：打印分配量诊断（`pool_size * sizeof(...)` 汇总）

**估计改动**: ~30 行

---

### Step 3: 引入索引传递机制

为 step 函数提供访问冷块的途径。两种方案，选 **3A**：

**方案 3A（推荐）—— pool 指针 + slot 索引传参**

修改所有调用冷块的 step 函数签名，增加 `pool` + `slot_idx` 参数：

```c
/* Before */
static int step_bnd_sfn_compute_Ti(
    const struct sdis_scene *scn,
    struct path_state *p);

/* After */
static int step_bnd_sfn_compute_Ti(
    const struct sdis_scene *scn,
    struct path_state *p,
    struct wavefront_pool *pool,   /* NEW */
    size_t slot_idx);              /* NEW */
```

函数内通过 `pool->sfn_arr[slot_idx]` 访问冷块。

**原因**：
- step 函数都是 `static`，签名变更无 ABI 影响
- 调用方（`advance_one_step_no_ray`）已有 `pool` 和 `slot_idx`
- 未触碰冷块的 step 函数签名 **不变**

**需签名变更的函数清单（~22 个）**：

| 组 | 函数 | 文件:行 | 访问冷块 |
|----|------|---------|---------|
| SFN | `step_bnd_sfn_prob_dispatch` | [sdis_wf_steps_bnd_sfn.c:L159](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L159) | sfn |
| SFN | `step_bnd_sfn_rad_done` | [sdis_wf_steps_bnd_sfn.c:L549](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L549) | sfn |
| SFN | `step_bnd_sfn_compute_Ti` | [sdis_wf_steps_bnd_sfn.c:L613](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L613) | sfn |
| SFN | `step_bnd_sfn_compute_Ti_resume` | [sdis_wf_steps_bnd_sfn.c:L736](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L736) | sfn |
| SFN | `step_bnd_sfn_check_pmin_pmax` | [sdis_wf_steps_bnd_sfn.c:L790](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L790) | sfn |
| SFN辅助 | `sfn_switch_in_radiative` 等辅助函数 | [sdis_wf_steps_bnd_sfn.c:L116](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L116) | sfn |
| ENC | `step_enc_query_emit` | [sdis_wf_steps_enc.c:L118](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L118) | enc |
| ENC | `step_enc_query_resolve` | [sdis_wf_steps_enc.c:L182](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L182) | enc |
| ENC | `step_enc_query_fb_resolve` | [sdis_wf_steps_enc.c:L258](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L258) | enc |
| ENC | `step_enc_locate_submit` | [sdis_wf_steps_enc.c:L56](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L56) | enc |
| ENC | `step_enc_locate_result` | [sdis_wf_steps_enc.c:L80](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L80) | enc |
| EXT | `step_bnd_ext_check` | [sdis_wf_steps_bnd_ext.c:L68](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L68) | ext |
| EXT | `step_bnd_ext_direct_result` | [sdis_wf_steps_bnd_ext.c:L250](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L250) | ext |
| EXT | `step_bnd_ext_diffuse_result` | [sdis_wf_steps_bnd_ext.c:L313](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L313) | ext |
| EXT | `step_bnd_ext_diffuse_shadow_result` | [sdis_wf_steps_bnd_ext.c:L544](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L544) | ext |
| EXT | `step_bnd_ext_finalize` | [sdis_wf_steps_bnd_ext.c:L602](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L602) | ext |
| SF | `step_bnd_sf_prob_dispatch` | [sdis_wf_steps_bnd_sf.c:L429](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c#L429) | ext |
| CND | `step_cnd_ds_check_temp` | [sdis_wf_steps_cnd.c:L54](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L54) | enc |
| CND | `step_cnd_ds_step_enc_verify` | [sdis_wf_steps_cnd.c:L152](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L152) | enc |
| SS | `step_bnd_ss_reinject_enc_result` | [sdis_wf_steps_bnd_ss.c:L533](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L533) | enc |
| SS | `step_bnd_ss_reinject_process`（`enc_query_emit` 调用点） | [sdis_wf_steps_bnd_ss.c:~L503](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L503) | enc |
| SF | `step_bnd_sf_reinject_enc_result` | [sdis_wf_steps_bnd_sf.c:L370](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c#L370) | enc |

**约束**：未触碰冷块的 step 函数（如 `step_cnd_ds_step_advance`、`step_cnd_wos_*`、`step_cnv_*`、`step_init` 等约 27+ 函数）签名 **保持不变**。

---

### Step 4: 修改分发表

**文件**: [sdis_wf_steps_core.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c)

修改 `advance_one_step_no_ray`（[L703](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L703)）和 `advance_one_step_with_ray`（[L909](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c#L909)）的签名，增加 `pool` + `slot_idx` 参数：

```c
/* Before */
static int advance_one_step_no_ray(
    const struct sdis_scene *scn,
    struct path_state *p);

/* After */
static int advance_one_step_no_ray(
    const struct sdis_scene *scn,
    struct path_state *p,
    struct wavefront_pool *pool,
    size_t slot_idx);
```

在 dispatch switch-case 中，仅对需要冷块的 phase 传递额外参数：

```c
case PATH_BND_SFN_COMPUTE_Ti:
    return step_bnd_sfn_compute_Ti(scn, p, pool, slot_idx);  /* +pool,idx */
case PATH_CND_DS_CHECK_TEMP:
    return step_cnd_ds_check_temp(scn, p, pool, slot_idx);   /* +pool,idx (enc) */
case PATH_CND_DS_STEP_ADVANCE:
    return step_cnd_ds_step_advance(scn, p);                 /* 不变 */
```

**估计改动**: ~60 行（dispatch 分支修改）

---

### Step 5: 修改 cascade 和 compact

**文件**: [sdis_solve_persistent_wavefront.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c)

#### 5.1 `cascade_advance_single_path`（[L1706](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1706)）

1. 签名增加 `pool`（如果尚无）
2. PicardN intercept（[L1725](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1725)）：
   ```c
   /* Before: p->sfn_stack_depth */
   /* After:  pool->sfn_arr[slot_idx].depth */
   ```
3. 传递 `pool, slot_idx` 给 `advance_one_step_no_ray`

#### 5.2 `compact_active_paths`（[L781](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L781)）

[L801-804](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L801) sfn_stack_depth 读取改为 `pool->sfn_arr[i].depth`

#### 5.3 `pool_cascade_non_ray_steps_compact`（[L1784](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1784)）

传递 `pool` 给 `cascade_advance_single_path`

**估计改动**: ~40 行

---

### Step 6: 修改 collect/distribute（enc_query）

**文件**: [sdis_solve_persistent_wavefront.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c)

`collect_ray_requests_bucketed`（[L974](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L974)）和 distribute 中访问 `p->enc_query` 的代码改为 `pool->enc_arr[slot_idx]`：

| 位置 | 当前代码 | 改为 |
|------|---------|------|
| [L1148-1169](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1148) | `p->enc_query.batch_indices[j]`, `p->enc_query.directions[j]` | `enc->batch_indices[j]`, `enc->directions[j]` |
| [L1284-1305](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1284) | 同上（第二循环） | 同上 |
| [L1556-1560](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1556) | `p->enc_query.dir_hits[j]` 写入 | `enc->dir_hits[j]` |
| [L1563-1564](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1563) | `p->enc_query.fb_hit` 写入 | `enc->fb_hit` |
| [L1661-1669](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1661) | 第二路径同上 | 同上 |

每个 loop 头部加 `struct path_enc_data *enc = &pool->enc_arr[slot_idx];`。

**估计改动**: ~50 行

---

### Step 7: 逐文件修改 step 函数内的字段访问

以下按文件列出所有 `p->sfn_stack`/`p->enc_query`/`p->ext_flux`/`p->enc_locate` 替换点。

#### 7.1 sdis_wf_steps_bnd_sfn.c（SFN 冷块）

每个函数头部加局部指针：
```c
struct path_sfn_data *sfn = &pool->sfn_arr[slot_idx];
```

| 行范围 | 替换 | 说明 |
|--------|------|------|
| [L116](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L116) | `p->sfn_stack[depth].T_values` → `sfn->stack[depth].T_values` | 辅助函数 |
| [L585-600](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L585) | `p->sfn_stack_depth` → `sfn->depth`；`p->sfn_stack[d].*` → `sfn->stack[d].*` | rad_done 初始化 frame |
| [L621-656](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L621) | 同上 | compute_Ti 全部 |
| [L713-714](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L713) | `p->sfn_stack[d].bnd_sf_backup` → `sfn->stack[d].bnd_sf_backup`；`p->sfn_stack_depth++` → `sfn->depth++` | push stack |
| [L742-770](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L742) | `p->sfn_stack_depth` → `sfn->depth`；`p->sfn_stack[d].*` → `sfn->stack[d].*` | resume + pop |
| [L800-807](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c#L800) | 同上 | check_pmin_pmax 读取 |

**估计**: ~60 处 `p->sfn_stack` / `p->sfn_stack_depth` 替换

#### 7.2 sdis_wf_steps_enc.c（ENC 冷块）

每个函数头部加局部指针：
```c
struct path_enc_data *enc = &pool->enc_arr[slot_idx];
```

| 行范围 | 替换 | 说明 |
|--------|------|------|
| [L132-165](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L132) | `p->enc_query.*` → `enc->*` | emit: 初始化所有字段 |
| [L190-234](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L190) | 同上 | resolve: 读 dir_hits, directions |
| [L265-294](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L265) | 同上 | fb_resolve: 读 fb_hit, fb_direction |
| [L56-90](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c#L56) | `p->enc_locate.*` → `enc->locate.*` | locate submit/result |

**注意**：`step_enc_query_emit` 是被其他 step 函数调用的辅助函数（如 `step_cnd_ds_step_enc_verify`），其签名变更会级联到调用方。

**估计**: ~40 处替换

#### 7.3 sdis_wf_steps_bnd_ext.c（EXT 冷块）

每个函数头部加局部指针：
```c
struct path_ext_data *ext = &pool->ext_arr[slot_idx];
```

| 行范围 | 操作 | 说明 |
|--------|------|------|
| [L68-246](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L68) | `p->ext_flux.*` → `ext->*` | check: ~50 处 |
| [L250-308](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L250) | 同上 | direct_result |
| [L313-540](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L313) | 同上 | diffuse_result: ~60 处 |
| [L544-598](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L544) | 同上 | diffuse_shadow_result |
| [L602-660](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c#L602) | 同上 | finalize: 全面读取 |

**估计**: ~130 处 `p->ext_flux.*` 替换

#### 7.4 sdis_wf_steps_bnd_sf.c（EXT 触发点）

| 行 | 替换 | 说明 |
|----|------|------|
| [L527](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c#L527) | `p->ext_flux.return_state` → `ext->return_state` | prob_dispatch 中设置返回状态 |

`step_bnd_sf_prob_dispatch` 内部调用 `step_bnd_ext_check` 时需传递 `pool, slot_idx`。

**估计**: ~5 处

#### 7.5 sdis_wf_steps_cnd.c（ENC 触发点）

| 行 | 替换 | 说明 |
|----|------|------|
| [L63](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L63) | `p->enc_query.resolved_enc_id` → `enc->resolved_enc_id` | ds_check_temp |
| [L156](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L156) | `step_enc_query_emit(p, ...)` → `step_enc_query_emit(p, pool, slot_idx, ...)` | ds_step_enc_verify |
| [L175](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c#L175) | `p->enc_query.resolved_enc_id` → `enc->resolved_enc_id` | ds_step_advance 中的 enc 检查 |

**估计**: ~8 处

#### 7.6 sdis_wf_steps_bnd_ss.c（ENC 触发点）

| 行 | 替换 | 说明 |
|----|------|------|
| [L503](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L503) | `step_enc_query_emit(p, ...)` 调用签名变更 | reinject_process |
| [L515](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L515) | 同上 | |
| [L541](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L541) | `p->enc_query.resolved_enc_id` → `enc->resolved_enc_id` | reinject_enc_result |
| [L560](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c#L560) | `step_enc_query_emit(p, ...)` 签名变更 | reinject_enc_result 中的重入 |

**估计**: ~8 处

---

## 4. 编译验证清单

改完上述所有文件后，执行：

```bash
cd stardis-cus3d/build
cmake --build . --config Release > build.log 2>&1
```

**预期**：零 error、零 warning（关于已删除字段）。若有遗漏的 `p->sfn_stack`/`p->enc_query`/`p->ext_flux` 引用，编译器会精确指出。

---

## 5. 测试计划

### 5.1 结构体尺寸静态断言

在测试文件中添加：
```c
SDIS_STATIC_ASSERT(sizeof(struct path_state) <= 2200,
                   "path_state should be <= 2.2KB after cold block extraction");
SDIS_STATIC_ASSERT(sizeof(struct path_sfn_data) <= 4096,
                   "sfn_data within budget");
```

### 5.2 功能测试

```bash
cd stardis-cus3d/build
ctest -C Release --output-on-failure
```

所有现有测试（包括 P0 的 `test_sdis_dispatch_soa`）必须通过。

### 5.3 IR 渲染回归

```bash
cd Stardis-Starter-Pack/porous
<stardis-exe> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > IR_P1.ht
```

逐像素与 P0 基线对比，容差 1e-6。

---

## 6. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| `step_enc_query_emit` 被多处调用，签名变更级联 | 中 | 该函数本身是 static，编译器会精确报出所有调用点 |
| `sfn_stack[d].bnd_sf_backup = p->locals.bnd_sf` 现在跨数组 | 低 | 仍是值拷贝语义，`p->locals.bnd_sf` 仍在 path_state 内 |
| collect/distribute 中 enc_query 访问需要 slot_idx | 中 | 这些循环已有 `p = &pool->slots[compact_idx]`，`slot_idx = compact_idx` |
| OMP 并行下 sfn_arr/enc_arr/ext_arr 线程安全 | 低 | 各 slot 独立，无共享写入 |

---

## 7. 后续路径（P2）

P1 完成后，`path_state` ~2040B，主要组成：
- core（identity/lifecycle/rwalk/ctx/T）~480B
- cnd_ds scratch ~232B
- bnd scratch ~128B
- locals union ~928B
- 其余（rad/filter/ray/rng/diag）~272B

P2 将进一步域分解为 `path_core`（~480B）+ 多个域 SoA，配合 `struct path_view` 延迟加载。详见 [P2 开发指南](P2_domain_decomposition_dev_guide.md)。
