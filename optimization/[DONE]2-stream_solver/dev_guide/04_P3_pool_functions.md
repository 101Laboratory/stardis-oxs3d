# P3: Pool 函数统一化

**工作量**: 3 天  
**改动文件**: `sdis_solve_persistent_wavefront.c`  
**前置**: P2 (pool_view 统一池视图结构)  
**验证**: 每个函数在单池视图和半池视图下均产出正确结果

---

## 目标

将 11 个 pool 操作函数统一为接受 `struct pool_view* pv` 参数的版本，
**替代原版函数**（而非新增后缀版本）。
同一套函数同时服务于单池模式和双池模式，调用方只需传入不同的 `pv` 指针。

---

## 统一改造模式

所有函数遵循统一签名：

```c
/* 统一版: 操作视图 [pv->base, pv->base + pv->view_size) */
static void func(struct wavefront_pool* pool, struct pool_view* pv)
{
    size_t base = pv->base;
    size_t end  = base + pv->view_size;
    /* 所有索引数组/缓冲区使用 pv->xxx */
    /* 循环范围 [base, end) */
}
```

**关键原则**:
- `pool->slots[i]` 仍用全局下标访问（物理连续数组不分割）
- 索引数组 `pv->active_indices` 中存的是**全局 slot 下标**（非视图内偏移）
- 射线缓冲区 `pv->ray_requests` 使用**视图内从 0 开始**的索引
- 函数名与原版相同，签名新增 `pv` 参数
- 单池调用: `func(pool, &pool->views[0])`
- 双池调用: `func(pool, &pool->views[0])` 或 `func(pool, &pool->views[1])`

### 对比旧设计

| 方面 | 旧设计 | 新设计 |
|------|--------|--------|
| 函数命名 | `func(pool)` + `func_half(pool, half_id)` | `func(pool, pv)` **仅一套** |
| 参数类型 | `int half_id` (内部查表) | `struct pool_view* pv` (直接使用) |
| 原版保留 | 保留原版作单池回退 | **不保留** — 单池走 `views[0]` |
| 代码量 | 原版 + 半池版 ≈ 2× | **仅 1×** |

---

## 函数 1: `compact_active_paths`

**原函数**: L495-543, 扫描全池分类 slot 状态

```c
static void
compact_active_paths(struct wavefront_pool* pool, struct pool_view* pv)
{
    size_t base = pv->base;
    size_t end  = base + pv->view_size;
    size_t i;

    pv->active_compact      = 0;
    pv->need_ray_count      = 0;
    pv->done_count          = 0;
    pv->bucket_radiative_n  = 0;
    pv->bucket_conductive_n = 0;

    for (i = base; i < end; i++) {
        struct path_state* p = &pool->slots[i];

        if (p->phase == PATH_DONE || p->phase == PATH_ERROR
         || p->phase == PATH_HARVESTED) {
            /* M8: SFN stack resume check */
            if (p->phase == PATH_DONE && p->sfn_stack_depth > 0) {
                p->phase = PATH_BND_SFN_COMPUTE_Ti_RESUME;
                p->active = 1;
            } else {
                pv->done_indices[pv->done_count++] = (uint32_t)i;
                continue;
            }
        }
        if (!p->active) continue;

        pv->active_indices[pv->active_compact++] = (uint32_t)i;

        if (p->needs_ray && p->ray_req.ray_count > 0) {
            pv->need_ray_indices[pv->need_ray_count++] = (uint32_t)i;

            if (p->phase == PATH_RAD_TRACE_PENDING) {
                pv->bucket_radiative[pv->bucket_radiative_n++] = (uint32_t)i;
            } else if (p->phase == PATH_COUPLED_COND_DS_PENDING
                    || p->phase == PATH_CND_DS_STEP_TRACE) {
                pv->bucket_conductive[pv->bucket_conductive_n++] = (uint32_t)i;
            }
        }
    }
}
```

**改动**: 原版 `[0, pool_size)` → `[base, end)`，输出 `pool->xxx` → `pv->xxx`。
签名从 `(pool)` → `(pool, pv)`。

---

## 函数 2: `pool_collect_ray_requests_bucketed`

**原函数**: L672-710+, 收集射线请求到桶排列缓冲区

```c
LOCAL_SYM res_T
pool_collect_ray_requests_bucketed(struct wavefront_pool* pool,
                                    struct pool_view* pv)
{
    size_t k, b;
    size_t cursor[RAY_BUCKET_COUNT];

    /* Pass 1: 计数 — 读 pv->need_ray_indices */
    memset(pv->bucket_counts, 0, sizeof(pv->bucket_counts));

    for (k = 0; k < pv->need_ray_count; k++) {
        uint32_t i = pv->need_ray_indices[k];
        struct path_state* p = &pool->slots[i];
        size_t nrays = count_path_rays(p);
        int bkt = (int)p->ray_bucket;
        pv->bucket_counts[bkt] += nrays;
    }

    /* Prefix sum → pv->bucket_offsets */
    pv->bucket_offsets[0] = 0;
    for (b = 0; b < RAY_BUCKET_COUNT; b++) {
        pv->bucket_offsets[b + 1] = pv->bucket_offsets[b] + pv->bucket_counts[b];
    }
    for (b = 0; b < RAY_BUCKET_COUNT; b++) {
        cursor[b] = pv->bucket_offsets[b];
    }

    /* Pass 2: Scatter — 写 pv->ray_requests, pv->ray_to_slot, pv->ray_slot_sub */
    for (k = 0; k < pv->need_ray_count; k++) {
        uint32_t i = pv->need_ray_indices[k];
        struct path_state* p = &pool->slots[i];
        int bkt = (int)p->ray_bucket;

        /* 与原版相同: 遍历 p->ray_req 中的射线,
         * 写入 pv->ray_requests[cursor[bkt]] */
        /* ... 原逻辑不变, 只是:
         *     pool->ray_requests → pv->ray_requests
         *     pool->ray_to_slot  → pv->ray_to_slot
         *     pool->ray_slot_sub → pv->ray_slot_sub
         *     p->ray_req.batch_idx 设为视图内偏移 cursor[bkt]
         */
    }

    pv->ray_count = pv->bucket_offsets[RAY_BUCKET_COUNT];
    return RES_OK;
}
```

**关键**: `p->ray_req.batch_idx` 存的是**视图射线缓冲区的偏移**（从 0 开始），
`distribute` 时通过 `pv->ray_hits[p->ray_req.batch_idx]` 正确索引。

---

## 函数 3: `pool_distribute_ray_results`

**原函数**: L923-960+, 按桶分发射线结果

```c
static res_T
pool_distribute_ray_results(struct wavefront_pool* pool,
                             struct pool_view* pv,
                             struct sdis_scene* scn)
{
    size_t k;
    res_T res = RES_OK;

    /* Phase 1: radiative — 读 pv->bucket_radiative */
    for (k = 0; k < pv->bucket_radiative_n; k++) {
        uint32_t i = pv->bucket_radiative[k];
        struct path_state* p = &pool->slots[i];
        const struct s3d_hit* h0 = &pv->ray_hits[p->ray_req.batch_idx];

        p->needs_ray = 0;
        res = step_radiative_trace(p, scn, h0);
        /* ... 错误处理同原版 ... */
    }

    /* Phase 2: conductive — 读 pv->bucket_conductive */
    for (k = 0; k < pv->bucket_conductive_n; k++) {
        uint32_t i = pv->bucket_conductive[k];
        struct path_state* p = &pool->slots[i];
        const struct s3d_hit* h0 = &pv->ray_hits[p->ray_req.batch_idx];
        const struct s3d_hit* h1 = NULL;
        if (p->ray_req.ray_count >= 2)
            h1 = &pv->ray_hits[p->ray_req.batch_idx2];

        /* ... 原逻辑不变 ... */
    }

    /* Phase 3: other buckets — 同样用 pv-> */
    /* ... */

    return res;
}
```

---

## 函数 4: `pool_cascade_non_ray_steps_compact`

**原函数**: L1114-1155+, OMP 并行推进非射线步

```c
static res_T
pool_cascade_non_ray_steps_compact(struct wavefront_pool* pool,
                                    struct pool_view* pv,
                                    struct sdis_scene* scn)
{
    const size_t n = pv->active_compact;

    /* ... OMP 环境/线程数判断 — 与原版相同 ... */

    #pragma omp parallel num_threads(omp_nthreads)
    {
        /* 与原版相同的 per-thread 累加器 */

        #pragma omp for schedule(dynamic, 64)
        for (size_t idx = 0; idx < n; idx++) {
            uint32_t i = pv->active_indices[idx];
            struct path_state* p = &pool->slots[i];

            /* ... 原版 advance_one_step 逻辑完全不变 ... */
        }

        /* ... OMP critical 合并统计 — 写入 pool-> (全局统计) ... */
    }

    return RES_OK;
}
```

**改动最小**: `pool->active_indices` → `pv->active_indices`，
`pool->active_compact` → `pv->active_compact`。循环体内逻辑完全不变。

---

## 函数 5: `harvest_completed_paths`

**原函数**: L1241-1308, 收割完成路径

```c
static res_T
harvest_completed_paths(struct wavefront_pool* pool,
                         struct pool_view* pv,
                         struct sdis_estimator_buffer* buf)
{
    size_t k;

    for (k = 0; k < pv->done_count; k++) {
        uint32_t i = pv->done_indices[k];
        struct path_state* p = &pool->slots[i];

        /* ... 原版全部逻辑不变 ... */
        /* 写 estimator_buffer — 安全:
         *   两个视图的 harvest 在 CPU 上不会同时执行 (串行调度) */

        pv->paths_completed++;
    }

    return RES_OK;
}
```

---

## 函数 6: `refill_pool`

**原函数**: L1317-1360, 用新任务填充空槽

```c
static res_T
refill_pool(struct wavefront_pool* pool,
             struct pool_view* pv,
             size_t* out_refill_count)
{
    size_t k, refill_count = 0;

    if (pool->task_next >= pool->task_count) {
        *out_refill_count = 0;
        return RES_OK;
    }

    for (k = 0; k < pv->done_count; k++) {
        uint32_t i = pv->done_indices[k];
        struct path_state* p = &pool->slots[i];

        if (p->active) continue;
        if (p->phase != PATH_DONE && p->phase != PATH_ERROR
         && p->phase != PATH_HARVESTED) continue;
        if (pool->task_next >= pool->task_count) break;
        /*  ^^^ pool->task_next: 全局共享, 单线程递增, 安全 */

        {
            const struct pixel_task* task = &pool->task_queue[pool->task_next];
            /* ... init_single_path + advance 与原版相同 ... */
            pool->task_next++;
            refill_count++;
        }
    }

    pool->diag_refill_count += refill_count;
    *out_refill_count = refill_count;
    return RES_OK;
}
```

**注意**: `pool->task_next` 是两个视图共享的唯一可变写入点。
由于两个视图的 `refill` 不会同时执行 (CPU 串行调度)，无数据竞争。

---

## 函数 7-8: enc_locate collect + distribute

```c
/* 函数 7 */
static res_T
pool_collect_enc_locate_requests(struct wavefront_pool* pool,
                                  struct pool_view* pv)
{
    pv->enc_locate_count = 0;

    for (size_t idx = 0; idx < pv->active_compact; idx++) {
        uint32_t i = pv->active_indices[idx];
        struct path_state* p = &pool->slots[i];

        if (p->phase != PATH_NEED_ENC_LOCATE) continue;

        /* 填充 pv->enc_locate_requests[pv->enc_locate_count] */
        /* ... 与原版逻辑相同, pool->enc_locate_* → pv->enc_locate_* ... */
        pv->enc_locate_to_slot[pv->enc_locate_count] = i;
        pv->enc_locate_count++;
    }
    return RES_OK;
}

/* 函数 8 */
static res_T
pool_distribute_enc_locate_results(struct wavefront_pool* pool,
                                    struct pool_view* pv)
{
    for (size_t k = 0; k < pv->enc_locate_count; k++) {
        uint32_t i = pv->enc_locate_to_slot[k];
        struct path_state* p = &pool->slots[i];

        /* ... 与原版逻辑相同, pool->enc_locate_results → pv->enc_locate_results ... */
    }
    return RES_OK;
}
```

---

## 函数 9-10: closest_point collect + distribute

```c
/* 函数 9 */
static res_T
pool_collect_cp_requests(struct wavefront_pool* pool,
                          struct pool_view* pv)
{
    pv->cp_count = 0;

    for (size_t idx = 0; idx < pv->active_compact; idx++) {
        uint32_t i = pv->active_indices[idx];
        struct path_state* p = &pool->slots[i];

        if (!path_needs_closest_point(p)) continue;

        /* 填充 pv->cp_requests + pv->cp_to_slot */
        pv->cp_to_slot[pv->cp_count] = i;
        pv->cp_count++;
    }
    return RES_OK;
}

/* 函数 10 */
static res_T
pool_distribute_cp_results(struct wavefront_pool* pool,
                            struct pool_view* pv)
{
    for (size_t k = 0; k < pv->cp_count; k++) {
        uint32_t i = pv->cp_to_slot[k];
        struct path_state* p = &pool->slots[i];

        /* ... 与原版逻辑相同 ... */
    }
    return RES_OK;
}
```

---

## 函数 11: `pool_update_active_count`

```c
/**
 * 更新全池活跃路径数。
 * 单池模式: 直接取 views[0].active_compact。
 * 双池模式: 两个视图相加。
 */
static void
pool_update_active_count(struct wavefront_pool* pool)
{
    if (pool->num_active_views == 1) {
        pool->active_count = pool->views[0].active_compact;
    } else {
        pool->active_count = pool->views[0].active_compact
                           + pool->views[1].active_compact;
    }
}
```

---

## 改动汇总

| 函数 | 签名变化 | 改动类型 | 估算行数 |
|------|---------|---------|---------|
| `compact_active_paths` | `(pool)` → `(pool, pv)` | 循环范围 + 输出目标 | ~35 |
| `pool_collect_ray_requests_bucketed` | `(pool)` → `(pool, pv)` | 读/写目标 | ~45 |
| `pool_distribute_ray_results` | `(pool, scn)` → `(pool, pv, scn)` | 读目标 | ~30 |
| `pool_cascade_non_ray_steps_compact` | `(pool, scn)` → `(pool, pv, scn)` | 读目标 | ~10 |
| `harvest_completed_paths` | `(pool, buf)` → `(pool, pv, buf)` | 读目标 | ~10 |
| `refill_pool` | `(pool, &n)` → `(pool, pv, &n)` | 读目标 + 共享 task_next | ~15 |
| `pool_collect_enc_locate_requests` | `(pool)` → `(pool, pv)` | 读/写目标 | ~15 |
| `pool_distribute_enc_locate_results` | `(pool)` → `(pool, pv)` | 读目标 | ~10 |
| `pool_collect_cp_requests` | `(pool)` → `(pool, pv)` | 读/写目标 | ~15 |
| `pool_distribute_cp_results` | `(pool)` → `(pool, pv)` | 读目标 | ~10 |
| `pool_update_active_count` | `(pool)` 不变 | 合并 views | ~10 |
| **总计** | | | **~205 行** |

---

## 调用方式对比

### 单池模式 (原串行路径)

```c
struct pool_view* pv = &pool.views[0];   /* 全范围视图 */

compact_active_paths(&pool, pv);
pool_collect_ray_requests_bucketed(&pool, pv);

/* GPU trace */
s3d_scene_view_trace_rays_batch_ctx(sv, pv->batch_ctx,
    pv->ray_requests, pv->ray_count, pv->ray_hits, &stats);

pool_distribute_ray_results(&pool, pv, scn);
pool_cascade_non_ray_steps_compact(&pool, pv, scn);
harvest_completed_paths(&pool, pv, buf);
refill_pool(&pool, pv, &refill_count);

pool_update_active_count(&pool);
```

### 双池模式 (流水线)

```c
struct pool_view* pv_a = &pool.views[0];
struct pool_view* pv_b = &pool.views[1];

/* Phase 1: GPU trace A, CPU process B */
compact_active_paths(&pool, pv_a);
pool_collect_ray_requests_bucketed(&pool, pv_a);
gpu_launch_async(&pool, pv_a, sv);           /* 异步发射 */

pool_cascade_non_ray_steps_compact(&pool, pv_b, scn);
harvest_completed_paths(&pool, pv_b, buf);
refill_pool(&pool, pv_b, &refill_count);

/* Phase 2: 等 A, 发射 B, CPU process A */
gpu_wait_and_postprocess(&pool, pv_a, sv, scn);
compact_active_paths(&pool, pv_b);
pool_collect_ray_requests_bucketed(&pool, pv_b);
gpu_launch_async(&pool, pv_b, sv);

pool_cascade_non_ray_steps_compact(&pool, pv_a, scn);
harvest_completed_paths(&pool, pv_a, buf);
refill_pool(&pool, pv_a, &refill_count);

pool_update_active_count(&pool);
```

**注意**: 上面两段代码调用的是**完全相同的函数** — 唯一区别是传入的 `pv` 指针不同。

---

## 单元测试策略

由于函数只有一个版本，验证策略：

1. **单池回归测试**: 创建全池 `{base=0, view_size=pool_size}` 视图，
   用新签名函数运行一步 → 结果必须与原版 bit-exact。
   （这确保改造没有引入 bug）

2. **双半等价性测试**: 创建全池 pool + 两个半视图，
   分别对两半运行 → 合并结果 → 与单池模式对比。

3. **边界测试**:
   - view_size = 1 (最小视图)
   - 一半全空 (所有 slot 已 HARVESTED)
   - 全部需要射线 vs 全部不需要

4. **batch_idx 正确性**: 确认 `collect` 写入的 `batch_idx` 正确，
   `distribute` 通过 `pv->ray_hits[batch_idx]` 能索引到正确结果。

5. **merge 后全池测试**: merge 后 `views[0].view_size = pool_size`，
   函数对全池范围工作，结果正确。

---

## 验证清单

- [ ] 每个函数编译通过（新签名）
- [ ] 单池回归: `&pool->views[0]` (full view) 与原代码结果 bit-exact
- [ ] 双半等价: 两个视图分别执行 → 合并 → 与单池一致
- [ ] batch_idx 回写正确（collect → trace → distribute 链路）
- [ ] OMP cascade 在任意视图大小下正常工作
- [ ] 主循环中所有调用点已更新为新签名
- [ ] 统计计数器正确累加
