# P2: pool_view 统一池视图结构

**工作量**: 1 天  
**改动文件**: `sdis_solve_persistent_wavefront.h`, `sdis_solve_persistent_wavefront.c`  
**前置**: 无 (可与 P0/P1 并行开发)  
**验证**: pool_view 结构可正确创建、初始化、销毁；单池/双池模式可动态切换

---

## 目标

定义 `pool_view` 作为**统一池视图**，持有独立的索引数组、射线缓冲区和 GPU batch context。
所有 pool 操作函数统一接受 `struct pool_view* pv` 参数，**同一套函数同时支持单池和双池模式**。
通过运行时动态切换 `num_active_views` 即可在单池（全范围视图）与双池（两半交替视图）之间无缝切换。

### 核心设计理念

```
单池模式: views[0] = { base=0, view_size=pool_size }           ← 全范围视图
双池模式: views[0] = { base=0, view_size=pool_size/2 }         ← 前半视图
          views[1] = { base=pool_size/2, view_size=pool_size/2 } ← 后半视图
```

- `pool_view` 代表"池的一个视图 (view)"
- 同一组函数 `func(pool, pv)` 处理任意大小的视图
- `wavefront_pool` 中 **不再保留重复的索引/缓冲区字段** — 一切通过 `views[]` 访问

---

## Step 1: 定义 `pool_view` 结构

**文件**: `sdis_solve_persistent_wavefront.h`，在 `struct wavefront_pool` 前新增

```c
/*******************************************************************************
 * pool_view — 统一池视图
 *
 * 表示 slots[base .. base+view_size) 范围内的一个工作视图。
 * 单池模式下 views[0] 覆盖全池 (base=0, view_size=pool_size)。
 * 双池模式下 views[0] 和 views[1] 各覆盖一半。
 *
 * 所有 pool 操作函数统一接受 (pool, pv) 参数，无需区分模式。
 ******************************************************************************/
struct pool_view {
  size_t base;              /* 槽位起始偏移: 0 或 view_size */
  size_t view_size;         /* 本视图覆盖的槽位数 */
  size_t capacity;          /* 本视图的分配容量 (≥ view_size) */

  /* ---- Stream compaction indices ---- */
  uint32_t* active_indices;
  size_t    active_compact;

  uint32_t* need_ray_indices;
  size_t    need_ray_count;

  uint32_t* done_indices;
  size_t    done_count;

  /* ---- Path type buckets ---- */
  uint32_t* bucket_radiative;
  size_t    bucket_radiative_n;
  uint32_t* bucket_conductive;
  size_t    bucket_conductive_n;

  /* ---- Ray buffers ---- */
  struct s3d_ray_request* ray_requests;
  uint32_t*               ray_to_slot;
  uint32_t*               ray_slot_sub;
  struct s3d_hit*          ray_hits;
  size_t                   ray_count;
  size_t                   max_rays;      /* capacity × 6 */

  /* ---- Ray bucket offsets ---- */
  size_t bucket_offsets[RAY_BUCKET_COUNT + 1];
  size_t bucket_counts[RAY_BUCKET_COUNT];

  /* ---- GPU batch contexts ---- */
  struct s3d_batch_trace_context* batch_ctx;
  struct s3d_batch_enc_context*   enc_batch_ctx;
  struct s3d_batch_cp_context*    cp_batch_ctx;

  /* ---- enc_locate buffers ---- */
  struct s3d_enc_locate_request* enc_locate_requests;
  struct s3d_enc_locate_result*  enc_locate_results;
  uint32_t*                      enc_locate_to_slot;
  size_t                         enc_locate_count;
  size_t                         max_enc_locates;  /* capacity */

  /* ---- closest_point buffers ---- */
  struct s3d_cp_request* cp_requests;
  struct s3d_hit*        cp_hits;
  uint32_t*              cp_to_slot;
  size_t                 cp_count;
  size_t                 max_cps;         /* capacity */

  /* ---- 视图统计 ---- */
  size_t total_rays_traced;
  size_t paths_completed;
  size_t paths_failed;
};
```

### 关键字段说明

| 字段 | 说明 |
|------|------|
| `base` | 视图起始 slot 下标 |
| `view_size` | 当前视图覆盖的 slot 数量（可动态调整） |
| `capacity` | 分配时的最大容量，`view_size ≤ capacity` 始终成立 |
| `max_rays` | `capacity × 6`，射线缓冲区最大容量 |

**`capacity` 与 `view_size` 的区别**:
- `views[0].capacity = pool_size`（按全池容量分配，支持 merge 后覆盖全池）
- `views[1].capacity = pool_size / 2`（只需半池容量）
- `view_size` 是运行时实际覆盖的 slot 数，merge 后 `views[0].view_size = pool_size`

---

## Step 2: 修改 `wavefront_pool`

**核心变更**: 移除所有重复的索引/缓冲区字段，一切通过 `views[]` 访问。

### 设计原则

- `slots[]` 数组**不分割** — 物理连续，视图通过 `[base, base+view_size)` 范围划分
- 索引数组和射线缓冲区**完全在 pool_view 中** — `wavefront_pool` 不再持有任何重复字段
- GPU batch context **在 pool_view 中** — 每个视图有独立的 stream、params、设备缓冲区
- 全局只读/单写字段**保留在 pool 上** — `task_queue`, `task_next`, `scn`, `cam`, RNG 等
- `num_active_views` 控制当前模式，值为 1（单池）或 2（双池）

### 具体改动

```c
struct wavefront_pool {
  /* ---- 不变字段 ---- */
  struct path_state*  slots;           /* [pool_size] 物理连续 */
  size_t              pool_size;       /* 总槽位数 */
  size_t              active_count;    /* 全池活跃路径数 */

  struct pixel_task*  task_queue;
  size_t              task_count;
  size_t              task_next;       /* 唯一共享可变状态 */

  struct sdis_scene*  scn;
  unsigned            enc_id;
  const struct sdis_camera* cam;
  const double*       time_range;
  const double*       pix_sz;
  size_t              picard_order;
  enum sdis_diffusion_algorithm diff_algo;
  uint32_t            next_path_id;

  struct ssp_rng**    slot_rngs;
  struct ssp_rng*     thin_rng_storage;
  uint64_t            global_seed;

  /* ---- P2: 统一池视图 ---- */
  struct pool_view    views[2];
  int                 num_active_views;   /* 1 = 单池, 2 = 双池 */

  /* ═══════════════════════════════════════════════════════════
   * 注意: 不再保留原版的 active_indices, ray_requests 等字段。
   * 所有操作通过 &pool->views[0] (单池) 或
   * &pool->views[0/1] (双池) 访问。
   *
   * 单池模式:   所有函数传 &pool->views[0]
   * 双池模式:   两半交替传 &pool->views[0] / &pool->views[1]
   * ═══════════════════════════════════════════════════════════ */

  /* ---- drain / statistics / diagnostics — 保持不变 ---- */
  int                 in_drain_phase;
  size_t              drain_step_count;
  size_t              drain_fallback_threshold;
  size_t              total_steps;

  /* ... 统计字段 ... */
  size_t total_rays_traced;
  size_t trace_call_count;
  size_t trace_batch_size_sum;
  size_t trace_batch_size_min;
  size_t trace_batch_size_max;
  double trace_batch_time_ms_sum;
  double trace_post_time_ms_sum;
  double trace_retrace_time_ms_sum;
  size_t trace_retrace_accepted_sum;
  size_t trace_retrace_missed_sum;
  size_t trace_filter_rejected_sum;
  size_t enc_locates_total;
  size_t enc_locates_resolved;
  size_t enc_locates_degenerate;
  size_t cp_total;
  size_t cp_accepted;
  size_t cp_requeried;
  size_t paths_completed;
  size_t paths_failed;
  size_t diag_refill_count;

  /* ---- 计时 ---- */
  double time_compact_s;
  double time_collect_s;
  double time_trace_s;
  double time_distribute_s;
  double time_cascade_s;
  double time_harvest_s;
  double time_refill_s;

  /* ---- 流水线计时 (P4) ---- */
  double time_pipeline_wait_s;
  double time_pipeline_cpu_pre_s;
  double time_pipeline_cpu_between_s;
  double time_pipeline_gpu_idle_s;
};
```

### 对比旧设计

| 方面 | 旧设计 | 新设计 |
|------|--------|--------|
| 模式控制 | `pipeline_enabled` (bool) | `num_active_views` (1 or 2) |
| 原版字段 | 保留在 pool 中作回退 | **已移除**，单池走 `views[0]` |
| 函数版本 | 原版 + `_half` 两套 | **统一一套**，接受 `pv` |
| 动态切换 | 不支持 | `merge_to_single_pool` / `split_to_dual_pool` |

---

## Step 3: 池视图初始化

**文件**: `sdis_solve_persistent_wavefront.c`，在 `pool_create` 函数中新增

```c
/**
 * pool_view_init — 初始化一个池视图
 *
 * @param pv         目标视图
 * @param base       slot 起始下标
 * @param view_size  视图覆盖的 slot 数
 * @param capacity   分配容量 (≥ view_size, views[0] 可能按全池容量分配)
 */
static res_T
pool_view_init(struct pool_view* pv, size_t base, size_t view_size,
               size_t capacity)
{
    size_t max_rays = capacity * 6;  /* 每 slot 最多 6 条射线 */

    memset(pv, 0, sizeof(*pv));
    pv->base      = base;
    pv->view_size = view_size;
    pv->capacity  = capacity;
    pv->max_rays  = max_rays;

    /* 索引数组 — 按 capacity 分配 */
    pv->active_indices   = (uint32_t*)calloc(capacity, sizeof(uint32_t));
    pv->need_ray_indices = (uint32_t*)calloc(capacity, sizeof(uint32_t));
    pv->done_indices     = (uint32_t*)calloc(capacity, sizeof(uint32_t));
    pv->bucket_radiative = (uint32_t*)calloc(capacity, sizeof(uint32_t));
    pv->bucket_conductive= (uint32_t*)calloc(capacity, sizeof(uint32_t));

    if (!pv->active_indices || !pv->need_ray_indices || !pv->done_indices
     || !pv->bucket_radiative || !pv->bucket_conductive)
        return RES_NO_MEM;

    /* 射线缓冲区 — 按 max_rays 分配 */
    pv->ray_requests = (struct s3d_ray_request*)calloc(max_rays,
                                                        sizeof(struct s3d_ray_request));
    pv->ray_to_slot  = (uint32_t*)calloc(max_rays, sizeof(uint32_t));
    pv->ray_slot_sub = (uint32_t*)calloc(max_rays, sizeof(uint32_t));
    pv->ray_hits     = (struct s3d_hit*)calloc(max_rays, sizeof(struct s3d_hit));

    if (!pv->ray_requests || !pv->ray_to_slot || !pv->ray_slot_sub || !pv->ray_hits)
        return RES_NO_MEM;

    /* GPU batch trace context (持有独立 CUDA stream + params — from P0) */
    {
        res_T rc = s3d_batch_trace_context_create(&pv->batch_ctx, max_rays);
        if (rc != RES_OK) return rc;
    }

    /* enc_locate buffers */
    pv->max_enc_locates     = capacity;
    pv->enc_locate_requests = (struct s3d_enc_locate_request*)calloc(
        capacity, sizeof(struct s3d_enc_locate_request));
    pv->enc_locate_results  = (struct s3d_enc_locate_result*)calloc(
        capacity, sizeof(struct s3d_enc_locate_result));
    pv->enc_locate_to_slot  = (uint32_t*)calloc(capacity, sizeof(uint32_t));

    if (!pv->enc_locate_requests || !pv->enc_locate_results || !pv->enc_locate_to_slot)
        return RES_NO_MEM;

    {
        res_T rc = s3d_batch_enc_context_create(&pv->enc_batch_ctx, capacity);
        if (rc != RES_OK) return rc;
    }

    /* closest_point buffers */
    pv->max_cps     = capacity;
    pv->cp_requests = (struct s3d_cp_request*)calloc(capacity, sizeof(struct s3d_cp_request));
    pv->cp_hits     = (struct s3d_hit*)calloc(capacity, sizeof(struct s3d_hit));
    pv->cp_to_slot  = (uint32_t*)calloc(capacity, sizeof(uint32_t));

    if (!pv->cp_requests || !pv->cp_hits || !pv->cp_to_slot)
        return RES_NO_MEM;

    {
        res_T rc = s3d_batch_cp_context_create(&pv->cp_batch_ctx, capacity);
        if (rc != RES_OK) return rc;
    }

    return RES_OK;
}
```

### 与旧设计的关键区别

| 方面 | 旧设计 | 新设计 |
|------|--------|--------|
| 分配参数 | `max_rays_per_half` | `capacity` (显式分配容量) |
| `views[0]` 容量 | `pool_size/2 × 6` | **`pool_size × 6`** (支持 merge) |
| 新增字段 | 无 | `capacity` (分配时上限) |

---

## Step 4: 池视图销毁

```c
static void
pool_view_destroy(struct pool_view* pv)
{
    if (!pv) return;

    free(pv->active_indices);
    free(pv->need_ray_indices);
    free(pv->done_indices);
    free(pv->bucket_radiative);
    free(pv->bucket_conductive);

    free(pv->ray_requests);
    free(pv->ray_to_slot);
    free(pv->ray_slot_sub);
    free(pv->ray_hits);

    if (pv->batch_ctx) s3d_batch_trace_context_destroy(pv->batch_ctx);

    free(pv->enc_locate_requests);
    free(pv->enc_locate_results);
    free(pv->enc_locate_to_slot);
    if (pv->enc_batch_ctx) s3d_batch_enc_context_destroy(pv->enc_batch_ctx);

    free(pv->cp_requests);
    free(pv->cp_hits);
    free(pv->cp_to_slot);
    if (pv->cp_batch_ctx) s3d_batch_cp_context_destroy(pv->cp_batch_ctx);

    memset(pv, 0, sizeof(*pv));
}
```

---

## Step 5: 在 `pool_create` 中集成

```c
/* P2: 统一池视图初始化 — 始终执行 */
{
    const char* env = getenv("STARDIS_PIPELINE");
    int dual = (env && env[0] == '0') ? 0 : 1;

    if (dual) {
        /* ---- 双池模式 ---- */
        size_t half = pool->pool_size / 2;

        /*
         * views[0]: capacity = pool_size (full)
         *   - 支持 merge 后覆盖全池
         *   - 初始 view_size = half (只看前半)
         *
         * views[1]: capacity = half
         *   - 初始 view_size = half (只看后半)
         */
        res = pool_view_init(&pool->views[0], 0,    half, pool->pool_size);
        if (res != RES_OK) goto cleanup;

        res = pool_view_init(&pool->views[1], half, half, half);
        if (res != RES_OK) goto cleanup;

        pool->num_active_views = 2;

        log_info(scn->dev,
            "persistent_wavefront: dual-buffer pipeline, "
            "view_size=%lu, views[0].capacity=%lu, views[1].capacity=%lu\n",
            (unsigned long)half,
            (unsigned long)pool->pool_size,
            (unsigned long)half);
    } else {
        /* ---- 单池模式 ---- */
        /*
         * views[0]: base=0, view_size=pool_size, capacity=pool_size
         *   - 全范围视图，所有函数直接使用
         *
         * views[1]: 不初始化
         */
        res = pool_view_init(&pool->views[0], 0, pool->pool_size, pool->pool_size);
        if (res != RES_OK) goto cleanup;

        pool->num_active_views = 1;

        log_info(scn->dev,
            "persistent_wavefront: single-buffer mode, "
            "pool_size=%lu\n",
            (unsigned long)pool->pool_size);
    }
}
```

### 单池模式下的调用方式

```c
/* 单池模式: 所有原版函数调用变为 */
struct pool_view* pv = &pool->views[0];

compact_active_paths(pool, pv);
pool_collect_ray_requests_bucketed(pool, pv);
/* ... GPU trace ... */
pool_distribute_ray_results(pool, pv, scn);
pool_cascade_non_ray_steps_compact(pool, pv, scn);
harvest_completed_paths(pool, pv, buf);
refill_pool(pool, pv, &refill_count);
```

**函数签名完全相同于双池模式** — 唯一区别是 `pv` 指向的视图范围不同。

---

## Step 6: 动态 merge/split

### 时机与阈值

```c
/* 合并条件: 某一半的活跃路径数 < 12.5% 其容量 */
static int
should_merge(const struct wavefront_pool* pool)
{
    size_t thresh;
    if (pool->num_active_views != 2) return 0;

    thresh = pool->views[0].view_size / 8;     /* 12.5% */

    return (pool->views[0].active_compact < thresh)
        || (pool->views[1].active_compact < thresh);
}

/* 分裂条件: 活跃路径数 > 75% 池容量，且仍有未分发任务 */
static int
should_split(const struct wavefront_pool* pool)
{
    if (pool->num_active_views != 1) return 0;
    if (pool->task_next >= pool->task_count) return 0;

    return pool->views[0].active_compact > (pool->pool_size * 3 / 4);
}
```

### merge: 双池 → 单池

```c
/**
 * merge_to_single_pool — 将双池合并为单池视图
 *
 * 前提: 两个视图的 GPU trace 均已完成 (无 async_pending)
 *
 * 做法: 扩展 views[0] 的 view_size 为 pool_size,
 *       views[0].capacity 已经是 pool_size (初始化时预分配),
 *       所以无需重新分配内存。
 */
static void
merge_to_single_pool(struct wavefront_pool* pool)
{
    /* views[0] 已按 pool_size 容量分配，直接扩展视图范围 */
    pool->views[0].base      = 0;
    pool->views[0].view_size = pool->pool_size;

    /* 清除 views[0] 的运行时计数 (下一轮 compact 会重建) */
    pool->views[0].active_compact     = 0;
    pool->views[0].need_ray_count     = 0;
    pool->views[0].done_count         = 0;
    pool->views[0].bucket_radiative_n = 0;
    pool->views[0].bucket_conductive_n= 0;
    pool->views[0].ray_count          = 0;
    pool->views[0].enc_locate_count   = 0;
    pool->views[0].cp_count           = 0;

    pool->num_active_views = 1;

    /* views[1] 的资源不释放 (后续可能 split 回来) */
}
```

### split: 单池 → 双池

```c
/**
 * split_to_dual_pool — 将单池拆分为双池视图
 *
 * 前提: views[1] 已被 pool_view_init 初始化过 (只是 view_size 可能为 0)
 *
 * 做法: 缩小 views[0] 视图范围为前半，恢复 views[1] 视图范围为后半。
 *       需在 compact 之前调用，让下一轮 compact 按新范围工作。
 */
static void
split_to_dual_pool(struct wavefront_pool* pool)
{
    size_t half = pool->pool_size / 2;

    pool->views[0].base      = 0;
    pool->views[0].view_size = half;

    pool->views[1].base      = half;
    pool->views[1].view_size = half;

    /* 清除两个视图的运行时计数 */
    pool->views[0].active_compact     = 0;
    pool->views[0].need_ray_count     = 0;
    pool->views[0].done_count         = 0;
    pool->views[0].bucket_radiative_n = 0;
    pool->views[0].bucket_conductive_n= 0;
    pool->views[0].ray_count          = 0;
    pool->views[0].enc_locate_count   = 0;
    pool->views[0].cp_count           = 0;

    pool->views[1].active_compact     = 0;
    pool->views[1].need_ray_count     = 0;
    pool->views[1].done_count         = 0;
    pool->views[1].bucket_radiative_n = 0;
    pool->views[1].bucket_conductive_n= 0;
    pool->views[1].ray_count          = 0;
    pool->views[1].enc_locate_count   = 0;
    pool->views[1].cp_count           = 0;

    pool->num_active_views = 2;
}
```

### merge/split 的内存安全保证

| 操作 | 条件 | 内存安全性 |
|------|------|-----------|
| merge | `views[0].capacity == pool_size` | ✅ 初始化时已按全池容量分配 |
| split | `views[1].capacity == pool_size/2` | ✅ 初始化时已分配（双池启动时） |
| split (首次，从单池启动) | `views[1]` 未初始化 | ❌ 需先调用 `pool_view_init` |

> **注意**: 如果初始以 `STARDIS_PIPELINE=0` 单池模式启动，`views[1]` 未初始化，
> 此时 `should_split` 始终返回 0，不会触发 split。动态切换仅在初始双池模式下可用。

---

## 内存开销估算

以 pool_size=8192 (half=4096) 为例：

### views[0] — 按 pool_size 容量分配 (支持 merge)

| 组件 | 大小 | 说明 |
|------|------|------|
| active_indices | 32KB | uint32_t × 8192 |
| need_ray_indices | 32KB | |
| done_indices | 32KB | |
| bucket_radiative/cond | 64KB | |
| ray_requests | 1.9MB | s3d_ray_request(40B) × 49152 |
| ray_to_slot/sub | 384KB | uint32_t × 49152 × 2 |
| ray_hits | 4.6MB | s3d_hit(96B) × 49152 |
| batch_ctx GPU | ~10MB | CudaBuffer d_rays + d_multi_hits |
| enc_locate bufs | 224KB | |
| cp bufs | 1.1MB | |
| **views[0] CPU** | **~8.4MB** | |
| **views[0] GPU** | **~10MB** | |

### views[1] — 按 half 容量分配

| 组件 | 大小 | 说明 |
|------|------|------|
| active_indices | 16KB | uint32_t × 4096 |
| need_ray_indices | 16KB | |
| done_indices | 16KB | |
| bucket_radiative/cond | 32KB | |
| ray_requests | 960KB | s3d_ray_request(40B) × 24576 |
| ray_to_slot/sub | 192KB | uint32_t × 24576 × 2 |
| ray_hits | 2.3MB | s3d_hit(96B) × 24576 |
| batch_ctx GPU | ~5MB | CudaBuffer d_rays + d_multi_hits |
| enc_locate bufs | 112KB | |
| cp bufs | 544KB | |
| **views[1] CPU** | **~4.2MB** | |
| **views[1] GPU** | **~5MB** | |

### 总额外开销

| | CPU | GPU | 说明 |
|-|-----|-----|------|
| 旧设计 (原版字段 + 双半各半容量) | ~8.4MB | ~10MB | 原字段保留 + 2×半容量 |
| **新设计 (无重复字段)** | **~12.6MB** | **~15MB** | views[0] 全容量 + views[1] 半容量 |
| 差异 | +4.2MB | +5MB | views[0] 从半容量变为全容量 |

> 代价：CPU +4.2MB, GPU +5MB（views[0] 需支持 merge 后全池范围）。
> 在 RTX 4090 (24GB VRAM) 和现代工作站上完全可接受。

---

## 验证清单

- [ ] 单池模式 (`STARDIS_PIPELINE=0`):
  - [ ] `views[0]` 初始化为 `{base=0, view_size=pool_size, capacity=pool_size}`
  - [ ] `num_active_views == 1`
  - [ ] 所有函数通过 `&pool->views[0]` 正常工作
  - [ ] 结果与原串行代码 bit-exact
- [ ] 双池模式 (`STARDIS_PIPELINE=1`):
  - [ ] `views[0]` 初始化为 `{base=0, view_size=half, capacity=pool_size}`
  - [ ] `views[1]` 初始化为 `{base=half, view_size=half, capacity=half}`
  - [ ] 两个 batch_ctx 各持有独立 CUDA stream
- [ ] merge/split:
  - [ ] `merge_to_single_pool` 后 `views[0].view_size == pool_size`
  - [ ] `split_to_dual_pool` 后两个视图范围正确
  - [ ] merge 后用全池视图运行一步，结果正确
- [ ] 内存: 分配全成功，无泄漏 (`_CrtDumpMemoryLeaks`)
- [ ] `pool_destroy` 正确释放所有 pool_view 内存和 GPU 资源
