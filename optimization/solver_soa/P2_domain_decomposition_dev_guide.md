# P2: 完整域分解 + path_view — 开发指南

**创建时间**: 2026-02-27  
**阶段**: SoA 二期工程 · Phase 2B  
**前置**: P0（调度层 SoA 镜像） ✅ + P1（超大冷块移出） ⬜  
**目标**: 将 P1 后 ~2040B 的 `path_state` 进一步拆分为 `path_core`（~480B）+ 4 个域 SoA 结构体，引入 `struct path_view` 延迟加载机制  
**预期收益**: DS 导热热路径 cascade re-entry: ~712B（vs P1 后 2040B），CNV: ~512B  
**预计工作量**: ~1500 行改动 / ~2 周

---

## 0. P1 后的 path_state 布局（~2040B）

P1 已移出 `sfn_stack[3]`（3696B）、`enc_query/enc_locate`（592B）、`ext_flux`（360B）。  
剩余 `path_state`：

| 区域 | 大小 | 说明 |
|------|------|------|
| identity: path_id, pixel_x/y, realisation_idx | 12B | |
| lifecycle: phase, active | 8B | |
| `rwalk` | 176B | 指针穿透：`&p->rwalk` 传给 `time_rewind` 等 |
| `ctx` (rwalk_context) | 96B | 指针穿透：`&p->ctx` |
| `T` (temperature) | 24B | 指针穿透：`&p->T` |
| `rad_direction/bounce/retry` | 20B | 辐射 scratch |
| `coupled_nbranchings` | 4B | |
| `ds_*` (DS 导热全部字段) | 232B | 仅 DS 路径使用 |
| `bnd_*` (边界 scratch) | 128B | 仅边界相位使用 |
| `filter_data_storage` | 144B | 仅 ray 设置时使用 |
| `ray_req` | 64B | 仅 collect 时使用 |
| `needs_ray` | 4B | |
| `rng*` + `rng_state` | 112B | 每步 RNG 调用 |
| `ipix_image[2]` | 16B | |
| `union locals` | 928B | 域互斥 |
| `steps_taken` + `done_reason` | 16B | 诊断 |
| `ray_bucket` + `ray_count_ext` | 8B | |
| *(padding)* | ~48B | |
| **合计** | **~2040B** | |

---

## 1. 域分解方案

### 1.1 Group A: `struct path_core`（~480B）

**每次 cascade 迭代必须加载**。包含所有 step 函数的公共访问字段。

```c
struct path_core {
    /* --- Identity --- */
    uint32_t  path_id;
    uint16_t  pixel_x, pixel_y;
    uint32_t  realisation_idx;

    /* --- Lifecycle --- */
    enum path_phase  phase;
    int              active;
    int              needs_ray;

    /* --- Random walk core (pointer penetration: &core->rwalk) --- */
    struct rwalk          rwalk;         /* 176B */
    struct rwalk_context  ctx;           /* 96B  */
    struct temperature    T;             /* 24B  */

    /* --- Coupled path scratch --- */
    int     coupled_nbranchings;

    /* --- RNG --- */
    struct ssp_rng* rng;                 /* 8B, non-owning */
    struct wf_rng   rng_state;           /* 104B */

    /* --- Diagnostics --- */
    size_t  steps_taken;
    int     done_reason;
    size_t  ipix_image[2];

    /* --- P0 dispatch SoA fields (kept inline for cascade hot path) --- */
    enum ray_bucket_type ray_bucket;
    int  ray_count_ext;
};
/* static_assert(sizeof(struct path_core) <= 512) */
```

**指针穿透策略**：`&core->rwalk`、`&core->ctx`、`&core->T` 取代原来的 `&p->rwalk`、`&p->ctx`、`&p->T`。被调函数（`time_rewind`、`solid_reinjection_3d`、`wf_setup_hit_wos` 等）签名**不变**。

### 1.2 Group B: `struct path_ray_io`（~228B）

**仅 cascade 断点 + collect/distribute 时加载**。

```c
struct path_ray_io {
    struct path_ray_request ray_req;     /* 64B  */
    struct hit_filter_data  filter_data_storage;  /* 144B */
    float   rad_direction[3];            /* 12B  */
    int     rad_bounce_count;            /* 4B   */
    int     rad_retry_count;             /* 4B   */
};
```

### 1.3 Group C: `struct path_cnd_ds`（~232B）

**仅 DS 导热相位加载**（`PATH_CND_DS_*` + `PATH_COUPLED_CONDUCTIVE` 当 mode=DS）。

```c
struct path_cnd_ds {
    float   ds_dir0[3], ds_dir1[3];
    struct s3d_hit ds_hit0, ds_hit1;     /* 112B */
    double  ds_delta_solid;

    int     ds_initialized;
    unsigned ds_enc_id;
    struct sdis_medium* ds_medium;
    struct solid_props  ds_props_ref;    /* 56B */
    double  ds_green_power_term;
    double  ds_position_start[3];
    int     ds_robust_attempt;
    float   ds_delta;
    float   ds_delta_solid_param;
};
```

### 1.4 Group D: `struct path_bnd`（~128B）

**仅边界 dispatch/重注入相位加载**（`PATH_BND_*`、`PATH_COUPLED_BOUNDARY`）。

```c
struct path_bnd {
    struct s3d_hit bnd_hit0, bnd_hit1;   /* 112B */
    double  bnd_reinject_distance;
    unsigned bnd_solid_enc_id;
    int     bnd_retry_count;
};
```

### 1.5 Group E: `struct path_locals`（~928B，保留 union）

**仅域特定操作加载**。Union 各分支互斥，CPU cache line 粒度加载仅触碰实际使用的分支前缀。

```c
struct path_locals {
    union {
        struct { ... } bnd_ss;               /* ~560B */
        struct path_bnd_sf_locals bnd_sf;    /* ~928B */
        struct { ... } cnd_wos;              /* ~336B */
        struct { ... } cnv;                  /* ~32B  */
    } u;
};
```

**不拆分 union 的理由**：4 个分支互斥，拆成独立数组会导致 4× 内存（32768 × 928B × 4 = 116MB vs 当前 32768 × 928B = 29MB），浪费 3× 内存。

---

## 2. `struct path_view`：延迟加载机制

### 2.1 定义

```c
/* Lazy-loading view into a single path's SoA domain arrays.
 * NULL pointers are resolved on first access via pv_xxx() inlines. */
struct path_view {
    struct wavefront_pool *pool;
    size_t slot_idx;

    /* Eagerly loaded (every cascade step) */
    struct path_core    *core;

    /* Lazily loaded (NULL until first access) */
    struct path_ray_io  *ray_io;
    struct path_cnd_ds  *cnd_ds;
    struct path_bnd     *bnd;
    struct path_locals  *locals;

    /* P1 cold blocks (already separate) */
    struct path_sfn_data *sfn;
    struct path_enc_data *enc;
    struct path_ext_data *ext;
};
```

### 2.2 延迟加载内联函数

```c
static inline struct path_cnd_ds *pv_cnd_ds(struct path_view *pv) {
    if (!pv->cnd_ds)
        pv->cnd_ds = &pv->pool->cnd_ds_arr[pv->slot_idx];
    return pv->cnd_ds;
}

static inline struct path_bnd *pv_bnd(struct path_view *pv) {
    if (!pv->bnd)
        pv->bnd = &pv->pool->bnd_arr[pv->slot_idx];
    return pv->bnd;
}

/* 同理: pv_ray_io(), pv_locals(), pv_sfn(), pv_enc(), pv_ext() */
```

### 2.3 cascade 使用模式

```c
static void cascade_advance_single_path(
    const struct sdis_scene *scn,
    struct wavefront_pool *pool,
    size_t slot_idx)
{
    struct path_view pv;
    memset(&pv, 0, sizeof(pv));
    pv.pool = pool;
    pv.slot_idx = slot_idx;
    pv.core = &pool->core_arr[slot_idx];

    for (;;) {
        int advanced = advance_one_step_no_ray(scn, &pv);
        if (pv.core->needs_ray) break;
        if (pv.core->phase == PATH_DONE || pv.core->phase == PATH_ERROR) {
            /* PicardN intercept */
            struct path_sfn_data *sfn = pv_sfn(&pv);
            if (sfn->depth > 0) {
                pv.core->phase = PATH_BND_SFN_COMPUTE_Ti_RESUME;
                continue;
            }
            break;
        }
        if (path_phase_is_ray_pending(pv.core->phase)) break;
        if (path_phase_is_enc_locate_pending(pv.core->phase)) break;
        if (path_phase_is_cp_pending(pv.core->phase)) break;
        if (!advanced) break;
    }
}
```

### 2.4 step 函数内使用模式

```c
static int step_cnd_ds_check_temp(
    const struct sdis_scene *scn,
    struct path_view *pv)
{
    struct path_core   *c  = pv->core;
    struct path_cnd_ds *ds = pv_cnd_ds(pv);
    /* 之前: p->rwalk  → c->rwalk */
    /* 之前: p->ds_enc_id → ds->ds_enc_id */
    /* 之前: &p->rwalk → &c->rwalk (指针穿透不变) */
    ...
}
```

---

## 3. Pool 结构变更

在 `struct wavefront_pool` 中，`slots` 数组被替换为多个 SoA 数组：

```c
struct wavefront_pool {
    /* P2: domain SoA arrays */
    struct path_core    *core_arr;      /* [pool_size] */
    struct path_ray_io  *ray_io_arr;    /* [pool_size] */
    struct path_cnd_ds  *cnd_ds_arr;    /* [pool_size] */
    struct path_bnd     *bnd_arr;       /* [pool_size] */
    struct path_locals  *locals_arr;    /* [pool_size] */

    /* P1: cold block arrays (already present) */
    struct path_sfn_data *sfn_arr;      /* [pool_size] */
    struct path_enc_data *enc_arr;      /* [pool_size] */
    struct path_ext_data *ext_arr;      /* [pool_size] */

    /* P0: dispatch SoA (可合并到 core_arr 或保留) */
    /* ... */

    size_t pool_size;
    /* ... existing fields ... */
};
```

**内存预算**（32768 paths）：

| 数组 | 单元大小 | 总量 |
|------|---------|------|
| `core_arr` | 480B | 15.0 MB |
| `ray_io_arr` | 228B | 7.1 MB |
| `cnd_ds_arr` | 232B | 7.2 MB |
| `bnd_arr` | 128B | 4.0 MB |
| `locals_arr` | 928B | 29.0 MB |
| `sfn_arr` | 3700B | 115.7 MB |
| `enc_arr` | 596B | 18.6 MB |
| `ext_arr` | 360B | 11.3 MB |
| **合计** | | **207.9 MB** |

vs 原始 `slots[]`（6688B × 32768 = 209 MB），总内存基本持平，但 **cascade 工作集大幅缩小**。

---

## 4. 每种路径的 cascade re-entry 加载量

| 路径类型 | 加载组 | 字节数 | vs 原始 6688B | 改善 |
|----------|--------|--------|--------------|------|
| **CND_DS** | core(480) + cnd_ds(232) | **712B** | ↓94% | **9.4×** |
| **CND_WOS** | core(480) + locals(336B touched) | **816B** | ↓88% | **8.2×** |
| **CNV** | core(480) + locals(32B touched) | **512B** | ↓92% | **13.1×** |
| **BND_SS** | core(480) + bnd(128) + locals(560) | **1168B** | ↓83% | **5.7×** |
| **BND_SF** | core(480) + bnd(128) + locals(928) + ext(360) | **1896B** | ↓72% | **3.5×** |
| **BND_SFN** | core(480) + bnd(128) + locals(928) + sfn(3700) + ext(360) | **5596B** | ↓16% | **1.2×** |

**注意**：`locals_arr` 虽总大小 928B，但 per-step 实际触碰量取决于 union 分支：WoS 336B、CNV 32B、SS 560B、SF 928B。CPU 只加载触碰的 cache lines。

---

## 5. 分步实施清单

### Step 1: 定义新结构体

**文件**: sdis_wf_state.h（或新建 sdis_wf_domain_state.h）

1. 定义 `struct path_core`、`struct path_ray_io`、`struct path_cnd_ds`、`struct path_bnd`、`struct path_locals`
2. 定义 `struct path_view` 及 `pv_xxx()` 内联函数
3. 删除 `struct path_state`（整个结构体不再存在）

### Step 2: Pool 重构

**文件**: sdis_solve_persistent_wavefront.h + sdis_solve_persistent_wavefront.c

1. 替换 `struct path_state *slots` 为 5 个域 SoA 数组
2. 修改 `pool_create()`/`pool_destroy()` 的分配/释放逻辑
3. 修改 `compact_active_paths()`：compact 时同步移动所有域数组（或使用 indirection 索引避免拷贝）

### Step 3: 修改分发表签名

**文件**: sdis_wf_steps_core.c

1. `advance_one_step_no_ray(scn, pv)` — 接收 `struct path_view *`
2. `advance_one_step_with_ray(scn, pv, hit0, hit1)` — 同上
3. 所有 case 分支按域加载需求选择性调用 `pv_xxx()`

### Step 4: 逐文件修改 step 函数

按文件计划（共 ~1200 处 `p->` 替换）：

| 文件 | 函数数 | 估计改动处 | 加载域 |
|------|--------|-----------|--------|
| [sdis_wf_steps_cnd.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnd.c) | 7 | ~300 | core + cnd_ds / locals(wos) |
| [sdis_wf_steps_bnd_sf.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c) | 5 | ~200 | core + bnd + locals + ext |
| [sdis_wf_steps_bnd_sfn.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c) | 5 | ~250 | core + locals + sfn |
| [sdis_wf_steps_bnd_ss.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ss.c) | 4 | ~100 | core + bnd + locals + enc |
| [sdis_wf_steps_bnd_ext.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_ext.c) | 5 | ~130 | core + ext |
| [sdis_wf_steps_enc.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c) | 5 | ~80 | core + enc |
| [sdis_wf_steps_cnv.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_cnv.c) | 3 | ~60 | core + locals(cnv) |
| [sdis_wf_steps_core.c](stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c) | 6+分发表 | ~150 | core + 按 phase 选择 |

**替换模式**：
- `p->phase` → `pv->core->phase`
- `p->rwalk` → `pv->core->rwalk`
- `&p->rwalk` → `&pv->core->rwalk`
- `p->ds_enc_id` → `pv_cnd_ds(pv)->ds_enc_id`
- `p->locals.bnd_sf` → `pv_locals(pv)->u.bnd_sf`
- `p->ray_req` → `pv_ray_io(pv)->ray_req`

### Step 5: 修改 wavefront 管线函数

**文件**: sdis_solve_persistent_wavefront.c

| 函数 | 行 | 改动要点 |
|------|-----|---------|
| `compact_active_paths` | [L781](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L781) | 读 `core_arr[i].phase/active`；compact 时同步移动所有域 |
| `pool_collect_ray_requests_bucketed` | [L974](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L974) | 读 `core_arr[i].ray_req` → `ray_io_arr[i].ray_req` |
| distribute | ~L1500+ | 写 ray_io → hit results；构建 path_view 传递给 `advance_one_step_with_ray` |
| `pool_cascade_non_ray_steps_compact` | [L1784](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1784) | 构建 path_view，传递给 `cascade_advance_single_path` |
| harvest / refill | | 从 core_arr 读结果 / 写初始状态 |

**Compact 策略**：

方案 A（简单）：compact 时同步 swap/copy 所有 8 个 SoA 数组。开销：每次 compact 移动 ~6.5KB/path × N_dead（但 compact 频率远低于 cascade）。

方案 B（优化，P3候选）：使用 `indirection_arr[]`（索引映射），compact 只交换 4B 索引而非数据。cascade 通过 `indirection[compact_idx]` 映射到实际 SoA 位置。

**P2 采用方案 A**，方案 B 作为 P3 性能优化候选。

### Step 6: 测试与验证

同 P1 测试计划：
1. `sizeof(struct path_core)` 静态断言 ≤ 512B
2. `ctest -C Release --output-on-failure` 全通过
3. IR 渲染 porous 场景逐像素对比（容差 1e-6）
4. 性能基准：cascade 阶段 OMP 吞吐量应显著提升

---

## 6. 关键设计决策记录

### 6.1 为什么保留 `union locals` 而不拆分？

- 4 个分支互斥（bnd_ss / bnd_sf / cnd_wos / cnv），拆分为独立数组会导致 4× 内存膨胀：32768 × 928B × 4 = 116MB vs union 29MB
- CPU cache line 粒度加载：WoS 仅触碰 336B（6 cache lines），不会加载 bnd_sf 的 928B
- 如果未来需要 GPU 端按分支独立加载，可仅拆分最热分支（cnd_wos 和 cnv）

### 6.2 为什么 rwalk/T/ctx 留在 core 而不单独 SoA？

- `&core->rwalk`、`&core->ctx`、`&core->T` 在 30+ 处被指针穿透传递给底层函数
- 这些底层函数（`time_rewind`、`solid_reinjection_3d`、`wf_setup_hit_wos` 等）不属于 wavefront 层，修改其签名会级联到 20+ 个不相关文件
- 在 `path_core` 内，rwalk + ctx + T 总共 296B，与 identity/lifecycle/rng 合在一起仍在 480B 内（<8 cache lines），L1 友好

### 6.3 `path_view` 延迟加载 vs 预加载？

- 预加载：cascade 入口一次性填充所有 8 个指针。每次 re-entry 加载 8 个指针（64B），但所有域数组都会被 prefetch 到 cache
- 延迟加载：仅加载 core（1 指针），其余按需。DS 路径（最热）仅 2 个域（core + cnd_ds）
- **选择延迟**：DS 路径每次 re-entry 2 步就中断，加载 6 个 NULL 指针 + 分支预测代价 <1ns，远低于预加载 6 个冷数组行的代价（~60ns）

### 6.4 compact 时是否需要同步所有域？

- 是的。compact 通过 swap 将 dead paths 移到池尾，所有域数组必须同步 swap
- 但 compact 频率远低于 cascade（每个 wavefront step 仅 1 次），swap 6.5KB/dead_path 的开销可忽略
- P3 可引入 indirection 索引消除 swap

---

## 7. 与 GPU 移植的关系

P2 完成后的域分解布局是 GPU kernel 所需的：

| CPU SoA 数组 | GPU kernel 加载 | 说明 |
|-------------|----------------|------|
| `core_arr` | 每个 warp 一次 | 480B/thread → 32 threads = 15KB（shared memory 友好） |
| `cnd_ds_arr` | DS kernel | 232B/thread |
| `locals_arr` | 按分支 | GPU 可按分支分组 warp 避免 divergence |
| `sfn_arr` | 仅 SFN kernel | 3.7KB 可能需要寄存器溢出到 local memory |

域分解后，GPU kernel 可选择性 `cudaMemcpyAsync` 仅需要的域数组到 device，避免传输 cold data。

---

## 附录 A: 完整相位转换图与域加载需求

### A.1 no-ray cascade 爆发链

| 链 | 相位序列 | 爆发长度 | 域需求 |
|----|---------|----------|--------|
| **CND_DS 循环** | COUPLED_CONDUCTIVE → CND_DS_CHECK_TEMP → ⛔ STEP_TRACE | 2 步 | core + cnd_ds |
| | (ray result) → DS_STEP_ADVANCE → CND_DS_CHECK_TEMP → ⛔ | 2 步 | core + cnd_ds |
| **CND_DS enc 验证** | DS_STEP_ENC_VERIFY → ⛔ ENC_QUERY_EMIT | 1 步 | core + cnd_ds + enc |
| **CND_WOS 循环** | COUPLED_CONDUCTIVE → CND_WOS_CHECK_TEMP → ⛔ cp-pending | 2 步 | core + locals(wos) |
| | (CP result) → CLOSEST_RESULT → TIME_TRAVEL → CHECK_TEMP → ⛔ | 3 步 | core + locals(wos) |
| **CNV null-coll** | CNV_SAMPLE_LOOP → CNV_SAMPLE_LOOP → ... → BND_DISPATCH | 1×N + 1 | core + locals(cnv→bnd) |
| **BND_SF prob** | SF_PROB_DISPATCH → EXT_CHECK → (no flux) → SF_PROB_DISPATCH → ... → POST_ROBIN → COUPLED_* | 3-8 步 | core + bnd + locals + ext |
| **BND_SFN Ti** | SFN_RAD_DONE → SFN_COMPUTE_Ti → SFN_CHECK_PMIN_PMAX → ... | 2-12 步 | core + locals + sfn |
| **BND_SS** | COUPLED_BOUNDARY → BND_DISPATCH → ⛔ SS_REINJECT_SAMPLE(4 rays) | 2 步 | core + bnd + locals |

### A.2 with-ray 分发域需求

`advance_one_step_with_ray` 在 distribute 后调用，相位已确定：

| 相位 | 域需求 |
|------|--------|
| PATH_RAD_TRACE_PENDING | core + ray_io |
| PATH_CND_DS_STEP_TRACE | core + cnd_ds + ray_io |
| PATH_BND_SS_REINJECT_SAMPLE | core + bnd + locals + ray_io |
| PATH_BND_SF_REINJECT_SAMPLE | core + bnd + locals + ray_io |
| PATH_BND_SF_NULLCOLL_RAD_TRACE | core + locals + ray_io |
| PATH_BND_SFN_RAD_TRACE | core + locals + ray_io |
| PATH_BND_EXT_DIRECT_TRACE | core + ext + ray_io |
| PATH_BND_EXT_DIFFUSE_TRACE | core + ext + ray_io |
| PATH_BND_EXT_DIFFUSE_SHADOW_TRACE | core + ext + ray_io |
| PATH_ENC_QUERY_EMIT | core + enc + ray_io |
| PATH_ENC_QUERY_FB_EMIT | core + enc + ray_io |
| PATH_CND_WOS_FALLBACK_TRACE | core + locals + ray_io |
| PATH_CNV_STARTUP_TRACE | core + locals + ray_io |
