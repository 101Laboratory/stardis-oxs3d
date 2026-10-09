# P4: 流水线主循环重构

**工作量**: 2 天  
**改动文件**: `sdis_solve_persistent_wavefront.c`  
**前置**: P0 + P1 + P2 + P3 全部完成  
**验证**: porous 场景双缓冲结果与单缓冲对比，容差 1e-6

---

## 目标

将当前串行 7 阶段主循环重构为双调度流水线：
GPU 执行一个视图的光追时，CPU 并行处理另一个视图的非 GPU 阶段，反之亦然。

主循环通过 `num_active_views` 判断当前模式：
- `num_active_views == 1`: 单池串行路径，使用 `&pool.views[0]` 全范围视图
- `num_active_views == 2`: 双池流水线路径，交替使用 `views[0]` 和 `views[1]`
- 运行时可通过 `merge_to_single_pool` / `split_to_dual_pool` 动态切换

---

## Step 1: 辅助整合函数

在主循环之前定义四个阶段整合函数，封装 P3 的统一函数。
所有辅助函数接受 `struct pool_view* pv`。

### 1a. `cpu_pre_gpu` — compact + collect (为 GPU 准备射线)

```c
static res_T
cpu_pre_gpu(struct wavefront_pool* pool, struct pool_view* pv)
{
    res_T res;

    compact_active_paths(pool, pv);

    pv->ray_count = 0;
    res = pool_collect_ray_requests_bucketed(pool, pv);
    if (res != RES_OK) return res;

    return RES_OK;
}
```

### 1b. `gpu_launch_async` — 异步发射 GPU trace

```c
static res_T
gpu_launch_async(struct wavefront_pool* pool, struct pool_view* pv,
                  struct s3d_scene_view* sv)
{
    if (pv->ray_count == 0) return RES_OK;

    return s3d_scene_view_trace_rays_batch_ctx_async(
        sv, pv->batch_ctx, pv->ray_requests, pv->ray_count);
}
```

### 1c. `gpu_wait_and_postprocess` — 等待 GPU + distribute + enc/cp

```c
static res_T
gpu_wait_and_postprocess(struct wavefront_pool* pool,
                          struct pool_view* pv,
                          struct s3d_scene_view* sv,
                          struct sdis_scene* scn)
{
    res_T res;

    /* ---- batch trace 等待 + postprocess ---- */
    if (pv->ray_count > 0) {
        struct s3d_batch_trace_stats stats;
        memset(&stats, 0, sizeof(stats));

        res = s3d_scene_view_trace_rays_batch_ctx_wait(
            sv, pv->batch_ctx,
            pv->ray_requests, pv->ray_count,
            pv->ray_hits, &stats);
        if (res != RES_OK) return res;

        /* 累加全局统计 */
        pool->total_rays_traced += pv->ray_count;
        pool->trace_call_count++;
        pool->trace_batch_size_sum   += pv->ray_count;
        pool->trace_batch_time_ms_sum  += stats.batch_time_ms;
        pool->trace_post_time_ms_sum   += stats.postprocess_time_ms;
        pool->trace_retrace_time_ms_sum += stats.retrace_time_ms;
        pool->trace_retrace_accepted_sum += stats.retrace_accepted;
        pool->trace_retrace_missed_sum   += stats.retrace_missed;
        pool->trace_filter_rejected_sum  += stats.filter_rejected;

        if (pv->ray_count < pool->trace_batch_size_min)
            pool->trace_batch_size_min = pv->ray_count;
        if (pv->ray_count > pool->trace_batch_size_max)
            pool->trace_batch_size_max = pv->ray_count;
    }

    /* ---- distribute ray results ---- */
    res = pool_distribute_ray_results(pool, pv, scn);
    if (res != RES_OK) return res;

    /* ---- enc_locate batch ---- */
    res = pool_collect_enc_locate_requests(pool, pv);
    if (res != RES_OK) return res;

    if (pv->enc_locate_count > 0) {
        struct s3d_batch_enc_stats enc_stats;
        memset(&enc_stats, 0, sizeof(enc_stats));

        /* enc_locate 仍同步执行 (数量极少, 不值得异步化) */
        res = s3d_scene_view_find_enclosure_batch_ctx(
            scn->s3d_view, pv->enc_batch_ctx,
            pv->enc_locate_requests, pv->enc_locate_count,
            pv->enc_locate_results, &enc_stats);
        if (res != RES_OK) return res;

        res = pool_distribute_enc_locate_results(pool, pv);
        if (res != RES_OK) return res;

        pool->enc_locates_total     += pv->enc_locate_count;
        pool->enc_locates_resolved  += enc_stats.resolved;
        pool->enc_locates_degenerate += enc_stats.degenerate;
    }

    /* ---- closest_point batch ---- */
    res = pool_collect_cp_requests(pool, pv);
    if (res != RES_OK) return res;

    if (pv->cp_count > 0) {
        struct s3d_batch_cp_stats cp_stats;
        memset(&cp_stats, 0, sizeof(cp_stats));

        res = s3d_scene_view_closest_point_batch_ctx(
            scn->s3d_view, pv->cp_batch_ctx,
            pv->cp_requests, pv->cp_count,
            pv->cp_hits, &cp_stats);
        if (res != RES_OK) return res;

        res = pool_distribute_cp_results(pool, pv);
        if (res != RES_OK) return res;

        pool->cp_total     += pv->cp_count;
        pool->cp_accepted  += cp_stats.batch_accepted;
        pool->cp_requeried += cp_stats.requery_accepted;
    }

    return RES_OK;
}
```

### 1d. `cpu_between` — cascade + harvest + refill

```c
static res_T
cpu_between(struct wavefront_pool* pool, struct pool_view* pv,
            struct sdis_scene* scn,
            struct sdis_estimator_buffer* buf)
{
    size_t refill_count = 0;
    res_T res;

    /* cascade non-ray steps */
    res = pool_cascade_non_ray_steps_compact(pool, pv, scn);
    if (res != RES_OK) return res;

    /* rebuild done_indices after cascade */
    compact_active_paths(pool, pv);

    /* harvest completed paths */
    res = harvest_completed_paths(pool, pv, buf);
    if (res != RES_OK) return res;

    /* refill with new tasks */
    res = refill_pool(pool, pv, &refill_count);
    if (res != RES_OK) return res;

    return RES_OK;
}
```

---

## Step 2: 主循环 — 统一分支

替换原主循环 (L1760-L1957)。单池和双池共用同一套辅助函数，仅调度逻辑不同。

```c
/* ====== 7. Wavefront main loop ====== */
time_current(&t_start);

if (pool.num_active_views == 2) {
    /* ═══════════════════════════════════════════════════════ */
    /*  双池流水线主循环                                       */
    /* ═══════════════════════════════════════════════════════ */

    struct pool_view* pv_a = &pool.views[0];
    struct pool_view* pv_b = &pool.views[1];

    /* ---- 启动阶段: 准备 A 并发射首次 GPU ---- */
    res = cpu_pre_gpu(&pool, pv_a);
    if (res != RES_OK) goto cleanup;
    res = gpu_launch_async(&pool, pv_a, scn->s3d_view);
    if (res != RES_OK) goto cleanup;

    /* ---- 准备 B (首次 between 无 cascade, 直接 pre) ---- */
    res = cpu_pre_gpu(&pool, pv_b);
    if (res != RES_OK) goto cleanup;

    while (pool.active_count > 0 || pool.task_next < pool.task_count) {

        /* ════════ Phase 1: GPU 在跑 A，CPU 处理 B ════════ */

        /* 等 GPU(A) 完成 → 立即发射 GPU(B) */
        res = gpu_wait_and_postprocess(&pool, pv_a, scn->s3d_view, scn);
        if (res != RES_OK) goto cleanup;
        res = gpu_launch_async(&pool, pv_b, scn->s3d_view);
        if (res != RES_OK) goto cleanup;

        pool.total_steps++;  /* view A 完成一步 */

        /* CPU: A 的 cascade + harvest + refill */
        res = cpu_between(&pool, pv_a, scn, buf);
        if (res != RES_OK) goto cleanup;

        /* CPU: A 的 compact + collect (为下一步 GPU(A) 准备) */
        res = cpu_pre_gpu(&pool, pv_a);
        if (res != RES_OK) goto cleanup;

        /* ════════ Phase 2: GPU 在跑 B，CPU 处理 A ════════ */

        /* 等 GPU(B) 完成 → 立即发射 GPU(A) */
        res = gpu_wait_and_postprocess(&pool, pv_b, scn->s3d_view, scn);
        if (res != RES_OK) goto cleanup;
        res = gpu_launch_async(&pool, pv_a, scn->s3d_view);
        if (res != RES_OK) goto cleanup;

        pool.total_steps++;  /* view B 完成一步 */

        /* CPU: B 的 cascade + harvest + refill */
        res = cpu_between(&pool, pv_b, scn, buf);
        if (res != RES_OK) goto cleanup;

        /* CPU: B 的 compact + collect */
        res = cpu_pre_gpu(&pool, pv_b);
        if (res != RES_OK) goto cleanup;

        /* ════════ 更新全池状态 + 动态切换检测 ════════ */
        pool_update_active_count(&pool);

        /* 动态 merge: 某一半过于稀疏 → 合并为单池 */
        if (should_merge(&pool)) {
            /* 等待所有挂起的 GPU 调用 */
            if (pv_a->batch_ctx->async_pending) {
                res = gpu_wait_and_postprocess(&pool, pv_a, scn->s3d_view, scn);
                if (res != RES_OK) goto cleanup;
            }
            if (pv_b->batch_ctx->async_pending) {
                res = gpu_wait_and_postprocess(&pool, pv_b, scn->s3d_view, scn);
                if (res != RES_OK) goto cleanup;
            }
            merge_to_single_pool(&pool);
            break;  /* 跳出双池循环，进入单池循环 */
        }

        /* 诊断日志 + 安全检查 */
        if (pool.total_steps > pool.task_count * 1000) {
            log_err(scn->dev, "pipeline: infinite loop safety break\n");
            res = RES_BAD_OP;
            goto cleanup;
        }
    }

    /* ---- drain: 等待最后挂起的 GPU 调用 ---- */
    if (pool.num_active_views == 2) {
        if (pv_a->batch_ctx->async_pending) {
            res = gpu_wait_and_postprocess(&pool, pv_a, scn->s3d_view, scn);
            if (res != RES_OK) goto cleanup;
            pool.total_steps++;
            res = cpu_between(&pool, pv_a, scn, buf);
            if (res != RES_OK) goto cleanup;
        }
        if (pv_b->batch_ctx->async_pending) {
            res = gpu_wait_and_postprocess(&pool, pv_b, scn->s3d_view, scn);
            if (res != RES_OK) goto cleanup;
            pool.total_steps++;
            res = cpu_between(&pool, pv_b, scn, buf);
            if (res != RES_OK) goto cleanup;
        }
    }
}

/* ═══════════════════════════════════════════════════════════ */
/*  单池主循环 (num_active_views == 1)                         */
/*  - 初始 STARDIS_PIPELINE=0 进入                            */
/*  - 或由上方 merge_to_single_pool 切换后落入                   */
/* ═══════════════════════════════════════════════════════════ */
if (pool.num_active_views == 1) {
    struct pool_view* pv = &pool.views[0];

    while (pool.active_count > 0 || pool.task_next < pool.task_count) {

        /* compact + collect */
        res = cpu_pre_gpu(&pool, pv);
        if (res != RES_OK) goto cleanup;

        /* GPU trace (同步 — 单池无需异步) */
        if (pv->ray_count > 0) {
            struct s3d_batch_trace_stats stats;
            memset(&stats, 0, sizeof(stats));

            res = s3d_scene_view_trace_rays_batch_ctx(
                scn->s3d_view, pv->batch_ctx,
                pv->ray_requests, pv->ray_count,
                pv->ray_hits, &stats);
            if (res != RES_OK) goto cleanup;

            pool.total_rays_traced += pv->ray_count;
            pool.trace_call_count++;
            /* ... 统计更新同上 ... */
        }

        /* distribute + enc/cp */
        res = pool_distribute_ray_results(&pool, pv, scn);
        if (res != RES_OK) goto cleanup;

        /* enc_locate + cp (同 gpu_wait_and_postprocess 的后半段) */
        res = pool_collect_enc_locate_requests(&pool, pv);
        if (res != RES_OK) goto cleanup;
        if (pv->enc_locate_count > 0) {
            /* ... enc_locate 同步执行 ... */
        }
        res = pool_collect_cp_requests(&pool, pv);
        if (res != RES_OK) goto cleanup;
        if (pv->cp_count > 0) {
            /* ... cp 同步执行 ... */
        }

        /* cascade + harvest + refill */
        res = cpu_between(&pool, pv, scn, buf);
        if (res != RES_OK) goto cleanup;

        pool.total_steps++;
        pool_update_active_count(&pool);

        /* 动态 split: 负载充足 → 切换双池 */
        if (should_split(&pool)) {
            split_to_dual_pool(&pool);
            /* 注意: split 后需重新进入双池循环，
             * 但此时我们已在单池循环内。
             * 简单做法: 记录标志，break 后重入外层。
             * 或: 仅在启动时决定模式，运行中不 split。
             * 推荐: 首版实现不做 split (仅 merge)，待验证后再启用。
             */
        }

        /* 安全检查 */
        if (pool.total_steps > pool.task_count * 1000) {
            log_err(scn->dev, "single-pool: infinite loop safety break\n");
            res = RES_BAD_OP;
            goto cleanup;
        }
    }
}
```

### 设计说明

| 方面 | 说明 |
|------|------|
| merge 行为 | 双池循环中检测到 `should_merge` → `merge_to_single_pool` → `break` 跳出 → 落入单池循环 |
| split 行为 | 首版实现建议**不启用** (仅 merge)，待稳定后再加 split→双池重入逻辑 |
| drain | 双池循环结束后检查两个 `async_pending`，逐个等待 |
| 单池路径 | 使用同一套 P3 函数 + `&pool.views[0]`，逻辑与原版等价 |

---

## Step 3: `pool_update_active_count`

已在 P3 文档定义，此处直接引用：

```c
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

## Step 4: drain 阶段优化

当一半已完全排空时，退化为单视图工作模式（通过 merge）：

```c
/* 在双池主循环内部, merge 检测已包含此逻辑:
 * should_merge 检查 active_compact < 12.5% → merge_to_single_pool
 * 之后进入单池循环, 自动只处理有活跃路径的 slot */
```

相比旧设计（手动跳过空半操作），merge 方案更简洁。

---

## Step 5: 诊断日志

添加 `[A]`/`[B]` 前缀区分两个视图的日志：

```c
#define VIEW_TAG(pv, pool) ((pv) == &(pool)->views[0] ? "[A]" : "[B]")

/* 在 gpu_wait_and_postprocess 中 */
if (getenv("STARDIS_PIPELINE_LOG") && ...) {
    log_info(scn->dev,
        "%s step %lu: rays=%lu, batch=%.2fms, post=%.2fms\n",
        VIEW_TAG(pv, pool),
        (unsigned long)pool->total_steps,
        (unsigned long)pv->ray_count,
        stats.batch_time_ms,
        stats.postprocess_time_ms);
}
```

---

## Step 6: 计时分解

为流水线模式新增计时统计：

```c
/* wavefront_pool 中新增 (已在 P2 的 wavefront_pool 定义中包含) */
double time_pipeline_wait_s;       /* 累计 GPU 等待时间 (应接近 0) */
double time_pipeline_cpu_pre_s;
double time_pipeline_cpu_between_s;
double time_pipeline_gpu_idle_s;   /* GPU 等 CPU 的空闲时间 */
```

在主循环中每阶段前后 `time_current()` 计时，最终输出流水线效率指标：

```c
/* 结束打印 */
log_info(scn->dev,
    "pipeline stats: gpu_util=%.1f%%, cpu_util=%.1f%%, "
    "gpu_idle=%.3fs, cpu_wait=%.3fs\n",
    100.0 * (1.0 - pool.time_pipeline_gpu_idle_s / total_time),
    100.0 * (1.0 - pool.time_pipeline_wait_s / total_time),
    pool.time_pipeline_gpu_idle_s,
    pool.time_pipeline_wait_s);
```

---

## 时序验证

### 预期稳态时序 (porous 320×320 spp=32)

```
每轮 (A+B 各一步) = 3.32ms
 ├─ GPU(A) trace = 1.54ms
 ├─ GPU(B) trace = 1.54ms
 ├─ GPU 空闲     = 0.12ms × 2 = 0.24ms (等 CPU pre)
 ├─ CPU post(A)  = 0.94ms
 ├─ CPU between(A)+pre(A) = 0.72ms
 ├─ CPU post(B)  = 0.94ms
 ├─ CPU between(B)+pre(B) = 0.72ms
 └─ CPU 空闲     = 0 (CPU 是瓶颈)

每半步 = 3.32 / 2 = 1.66ms
加速比 = 3.19 / 1.66 = 1.92×
```

### 关键性能指标

| 指标 | 目标值 |
|------|--------|
| GPU 利用率 | ≥ 90% (理论 92.8%) |
| CPU 利用率 | ≥ 95% (理论 99.4%) |
| 每步耗时 | ≤ 1.75ms (目标 1.66ms) |
| porous 总时间 | ≤ 12min (原 22min 44s) |

---

## 验证清单

- [ ] `STARDIS_PIPELINE=0`: 单池路径使用 `views[0]` 全范围视图，结果与原版 bit-exact
- [ ] `STARDIS_PIPELINE=1`: 双缓冲流水线正常运行，无崩溃
- [ ] drain 阶段正确排空（所有路径完成，无遗漏）
- [ ] merge 正确: 稀疏时自动合并为单池，继续正常运行
- [ ] 统计计数器正确（total_steps, paths_completed 等与单池一致）
- [ ] 日志 [A]/[B] 前缀正确（使用 `pv` 指针比较）
- [ ] 无 CUDA 错误 (`compute-sanitizer --tool memcheck`)
- [ ] 性能提升接近 1.9× (通过 wall-clock 和 Nsight 确认)
