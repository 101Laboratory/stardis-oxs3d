# O11 Merge-Phase 开发指南

**代号**: O11-MergePhase  
**目标**: 将 distribute + cascade + collect + harvest 合并为单次 OMP 扫描; 统一 RT/enc/cp 为外部查询模型  
**预期收益**: 208s → ~47s (pool=20K, i9-13900 + RTX 4090)  
**验证标准**: 功能正确性 (非 bit-exact, 因 enc/cp 延迟一轮导致 RNG 序列偏移)  
**源文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`  

---

## 目录

1. [前置条件](#1-前置条件)
2. [架构变更总览](#2-架构变更总览)
3. [实施步骤](#3-实施步骤)
4. [代码变更详解](#4-代码变更详解)
5. [验证流程](#5-验证流程)
6. [附录 C: enc/cp 延迟安全性验证](#附录-c-enccp-延迟安全性验证)

---

## 1. 前置条件

### 1.1 Worktree 创建

在独立 worktree 上开发, 不向后兼容 main, 直接替换原有函数。

```bash
cd stardis-cus3d
git checkout -b opt/merge-phase main
git worktree add ../stardis-cus3d-merge-phase opt/merge-phase
cd ../stardis-cus3d-merge-phase
```

验证通过后合并到 main, worktree 随即删除。回退方式: `git revert` merge commit。

### 1.2 确认基线数据

阅读 [profile log]("../../../../opt_profile_log.txt") 或运行当前 main 版本获取基线 (pool=20000):
```bash
cd Stardis-Starter-Pack/porous
<stardis-exe> -M porous.txt -t 32 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > baseline.ht
```

记录: wall time, cascade, distribute, collect, compact, harvest+refill, total_rays, cascade_iterations, failed paths.

### 1.3 前置知识

必须阅读:
- `optimization/solver_merge_phase/analysis.md` — 方案分析与性能预估
- `guide/upper_parallelization/phase_b4_fine_grained_state_machine.md` — 状态机设计
- 当前 `sdis_solve_persistent_wavefront.c` 中的 pipeline 注释 (L3160-3170)

---

## 2. 架构变更总览

### 2.1 当前循环结构 (6 次扫描)

```
cpu_pre_gpu():           compact + collect           Scan #1,2
gpu_launch_async()
gpu_wait_download()
gpu_postprocess():       distribute(step推进+写入) + enc/cp batch(同步)  Scan #3 + 2×hot_arr
cpu_between():           cascade + compact + harvest  Scan #4,5,6
```

### 2.2 目标循环结构 (1 次 path_state 扫描)

```
merged_pass():           distribute(纯写) + cascade(含step) + collect(3类) + harvest   Scan #1 (40MB)
compact_indices():       hot_arr 扫描                               (160KB, L2内)
refill():                串行 pre-alloc + OMP init                  (通常 <100 slots)
gpu_dispatch_all():      RT + CP + enc_locate 全部异步发射           (CPU 单次调用, 立刻返回)
gpu_wait_d2h_all():      等待 + D2H + enc后处理                     (同步)
```

**关键架构变更 (统一外部查询 + 统一 GPU 分发):**
- RT, enc_locate, cp 三种外部查询完全对称, 全部延迟一轮
- distribute 退化为纯写者: 写回 ray_hits + enc_results + cp_results, 不调用 step
- cascade 吸收 distribute 的 step 逻辑: 消费 pending 结果 → step推进 → no-ray 循环
- collect 收集三种请求: ray + enc + cp, 直接写 pinned buffer
- **gpu_dispatch_all()** 替代 gpu_launch_async() + post_batch_enc_cp():
  一次性排队 RT trace + CP batch + enc_locate (CP+RT) 到 compute_stream,
  CPU 立刻返回进入下轮 merged_pass. 三种查询的延迟全部被隐藏。
- 详见 analysis.md §5.2-5.7

### 2.3 不变的组件

| 组件 | 变化 | 说明 |
|------|------|------|
| `gpu_launch_async()` | 无 | 读 pinned buffer, 发射 kernel |
| `gpu_sync_kernel()` | 无 | CUDA event 等待 |
| `gpu_start_d2h()` | 无 | 异步 D2H |
| `gpu_wait_d2h()` | 无 | D2H 同步 + stats |
| Plan E pinned write | 移入 merged_pass | 写入方式不变 |
| hot_arr P0_OPT | 无 | step 函数直写 hot_arr |

---

## 3. 实施步骤

### Step 1: 新建 `merged_pass()` 函数框架

在 `sdis_solve_persistent_wavefront.c` 中, 在 `cpu_between()` 附近新增:

```c
/**
 * merged_pass — single OMP scan combining distribute + cascade + collect + harvest.
 *
 * Runs after gpu_wait_d2h() has delivered ray_hits for the previous round.
 * Each path_state is loaded into L1 exactly once, and all per-path operations
 * are performed before moving to the next slot.
 *
 * enc_locate / closest_point batch requests are collected into pv->enc_pending[]
 * and pv->cp_pending[] for post-pass processing.
 */
static res_T
merged_pass(struct wavefront_pool* pool,
            struct pool_view*      pv,
            struct sdis_scene*     scn)
```

### Step 2: 实现 merged_pass 内部逻辑

merged_pass 的 OMP 循环按以下顺序处理每个 active slot。
**核心变更**: distribute 为纯写者, cascade 吸收 step 逻辑, collect 收集三种请求。

```c
static res_T
merged_pass(struct wavefront_pool* pool,
            struct pool_view*      pv,
            struct sdis_scene*     scn)
{
  int omp_nthreads = (scn && scn->dev) ? (int)scn->dev->nthreads : 1;
  int had_fatal = 0;
  size_t n = pv->active_compact;

  /* 串行 fallback */
  if(n < 64 || omp_nthreads <= 1) {
    return merged_pass_serial(pool, pv, scn);
  }

  /* 重置 per-view 计数器 */
  pv->ray_count = 0;
  pv->enc_locate_count = 0;
  pv->cp_count = 0;
  pv->bucket_radiative_n = 0;
  pv->bucket_conductive_n = 0;
  pv->bucket_other_n = 0;

  #pragma omp parallel num_threads(omp_nthreads)
  {
    /* ---- thread-local accumulators ---- */
    size_t tl_iterations = 0, tl_advances = 0, tl_paths_failed = 0;
    int tl_fatal = 0;

    /* thread-local ray buffer (避免 atomic 竞争) */
    struct tl_ray_buf tl_buf;
    tl_ray_buf_init(&tl_buf);

    #pragma omp for schedule(dynamic, 64)
    for(int ph = 0; ph < (int)n; ph++) {
      uint32_t slot = pv->active_indices[ph];
      struct path_state* p = &pool->slots[slot];
      struct path_hot* hot = &pool->hot_arr[slot];
      /* ════ path_state[slot] 现在在 L1 ════ */

      /* ──── Phase A: Distribute (纯写者) ──── */
      /* 写回上轮三种外部查询的结果, 不调用 step 函数 */
      distribute_write_results(pool, pv, slot, p, hot);

      /* ──── Phase B: Cascade (含 step 逻辑) ──── */
      /* 先消费 pending 结果 (ray/enc/cp) 调用 step_*() 推进,
       * 然后进入 no-ray 内循环直到产生新请求或 path 完成 */
      cascade_advance_single_path_unified(
          p, hot, scn, pool, slot,
          &tl_iterations, &tl_advances, &tl_paths_failed);

      /* ──── Phase C: Collect (三种请求) ──── */
      if(hot->needs_ray) {
        collect_ray_to_tl_buf(pool, pv, p, hot, slot, &tl_buf);
      }
      if(hot->phase == (uint8_t)PATH_ENC_LOCATE_PENDING) {
        collect_enc_request(pv, p, slot);
      }
      if(path_phase_is_cp_pending(hot->phase)) {
        collect_cp_request(pv, p, slot);
      }

      /* ──── Phase D: Harvest (如果 path 完成) ──── */
      if(hot->phase == (uint8_t)PATH_DONE && hot->active == 0) {
        harvest_single_path(pool, pv, p, slot);
      }
    } /* end omp for */

    /* ---- thread-local → global reduction ---- */
    merge_tl_ray_buf_to_pinned(pv, &tl_buf);

    #pragma omp atomic
    pool->cascade_total_iterations += tl_iterations;
    #pragma omp atomic
    pool->cascade_total_advances += tl_advances;
    #pragma omp atomic
    pool->paths_failed += tl_paths_failed;

  } /* end omp parallel */

  if(had_fatal) return RES_UNKNOWN_ERR;

  return RES_OK;
}
```

### Step 3: 实现 distribute 纯写者

distribute 不再调用 step 函数, 仅将三种外部查询结果写入 slot-local 存储:

```c
/**
 * distribute_write_results — pure writer, no step logic.
 *
 * Writes back results from THREE external query types:
 * 1. ray_hits  → from GPU trace (previous round)
 * 2. enc_results → from enc_locate batch (previous round)
 * 3. cp_results  → from cp batch (previous round)
 *
 * The step functions that consume these results are now in cascade.
 */
static INLINE void
distribute_write_results(struct wavefront_pool* pool,
                         struct pool_view*      pv,
                         uint32_t               slot,
                         struct path_state*     p,
                         struct path_hot*       hot)
{
  /* ---- 1. Ray hits (来自 GPU trace) ---- */
  if(hot->has_pending_hit) {
    /* 写入 hit 数据到 slot-local 存储 */
    switch(hot->ray_bucket) {
      case RAY_BUCKET_RADIATIVE: {
        p->hit_result = pv->ray_hits[p->ray_req.batch_idx];
        break;
      }
      case RAY_BUCKET_STEP_PAIR: {
        p->hit_result = pv->ray_hits[p->ray_req.batch_idx];
        if(p->ray_req.ray_count >= 2)
          p->hit_result2 = pv->ray_hits[p->ray_req.batch_idx2];
        break;
      }
      default: {
        /* other bucket: enc_query 6-ray, bnd_ss 4-ray 等 */
        /* 预拷贝多条 hit 到 enc_arr 或 locals */
        distribute_write_other_hits(pool, pv, slot, p, hot);
        break;
      }
    }
    /* has_pending_hit 由 cascade 消费后清零 */
  }

  /* ---- 2. enc_locate results (来自 batch) ---- */
  if(hot->phase == (uint8_t)PATH_ENC_LOCATE_RESULT) {
    /* enc_results 已在 post_batch 中写入 enc_arr[slot] */
    /* 无额外操作, cascade 直接消费 */
  }

  /* ---- 3. cp results (来自 batch) ---- */
  if(path_phase_is_cp_result(hot->phase)) {
    /* cp_results 已在 post_batch 中写入 p->locals.cnd_wos */
    /* 无额外操作, cascade 直接消费 */
  }
}
```

**对比旧设计**: 旧 distribute 调用 `step_radiative_trace()`, `step_conductive_ds_process()` 等
推进状态机并产生 enc/cp PENDING。新设计将这些 step 全部移入 cascade (Step 3b)。

**关键**: `distribute_write_other_hits()` 仍需处理 enc_query 的 6-ray 和 bnd_ss 的
4-ray 预拷贝 (当前在 distribute Phase 3 中, L2204-2247), 但只做数据拷贝, 不调用 step。

### Step 3b: cascade 吸收 step 逻辑

将原 distribute 的 step 函数移入 cascade 的入口段:

```c
/**
 * cascade_advance_single_path_unified — cascades with step absorption.
 *
 * Phase 1 (NEW): consume pending external query results by calling step_*.
 * Phase 2 (EXISTING): no-ray cascade loop, unchanged.
 */
static void
cascade_advance_single_path_unified(
    struct path_state* p, struct path_hot* hot,
    struct sdis_scene* scn, struct wavefront_pool* pool,
    uint32_t slot,
    size_t* tl_iterations, size_t* tl_advances, size_t* tl_paths_failed)
{
  /* ═══ Phase 1: 消费 pending 外部查询结果 (原 distribute step 逻辑) ═══ */
  if(hot->has_pending_hit) {
    hot->needs_ray = 0;
    switch(hot->ray_bucket) {
      case RAY_BUCKET_RADIATIVE:
        step_radiative_trace(p, hot, scn, &p->hit_result);
        break;
      case RAY_BUCKET_STEP_PAIR:
        step_conductive_ds_process(p, hot, scn,
            &p->hit_result,
            (p->ray_req.ray_count >= 2) ? &p->hit_result2 : NULL,
            &pool->enc_arr[slot]);
        break;
      default:
        advance_one_step_with_ray(p, hot, scn, ...);
        break;
    }
    hot->has_pending_hit = 0;
    p->steps_taken++;
  }
  /* enc_locate RESULT / cp RESULT: advance_one_step_no_ray 自然消费 */

  /* ═══ Phase 2: no-ray cascade loop (不变) ═══ */
  for(;;) {
    if(hot->needs_ray) break;          /* 需要 ray trace */
    if(hot->phase == PATH_DONE) break; /* 完成 */
    if(path_phase_is_enc_locate_pending(hot->phase)) break;  /* 需要 enc_locate */
    if(path_phase_is_cp_pending(hot->phase)) break;          /* 需要 cp */

    advance_one_step_no_ray(p, hot, scn, ...);
    (*tl_iterations)++;
  }
  (*tl_advances)++;
}
```

**Phase 1 的本质**: 就是把原 `pool_distribute_ray_results()` 中 per-path 的
`step_*()` 调用搬到 cascade 入口。从状态机角度看, cascade 现在是:
"消费上轮外部结果 → 推进 no-ray 步骤 → 产生新请求"。

### Step 4: 提取 collect 的 per-path 逻辑

从 `pool_collect_ray_requests_bucketed()` (L1514) 中提取单 path 写入:

```c
/**
 * collect_single_path_to_pinned — write one path's ray request to pinned buffer.
 *
 * Thread-local buffer approach: each thread accumulates rays into a local buffer,
 * then merges to pinned at the end of the OMP region.
 */
static INLINE void
collect_single_path_to_pinned(struct wavefront_pool* pool,
                              struct pool_view*      pv,
                              struct path_state*     p,
                              struct path_hot*       hot,
                              uint32_t               slot,
                              struct tl_ray_buf*     tl)
{
  /* 写入 thread-local buffer, 记录 bucket 分类 */
  size_t local_idx = tl->count++;
  tl->entries[local_idx].slot = slot;
  tl->entries[local_idx].bucket = hot->ray_bucket;
  tl->entries[local_idx].ray_count = hot->ray_count_ext;

  /* 记录各字段到 tl buffer (延迟写 pinned) */
  tl->entries[local_idx].origin[0] = p->ray_req.origin[0];
  tl->entries[local_idx].origin[1] = p->ray_req.origin[1];
  tl->entries[local_idx].origin[2] = p->ray_req.origin[2];
  tl->entries[local_idx].dir[0]    = p->ray_req.direction[0];
  tl->entries[local_idx].dir[1]    = p->ray_req.direction[1];
  tl->entries[local_idx].dir[2]    = p->ray_req.direction[2];
  tl->entries[local_idx].tmin      = p->ray_req.tmin;
  tl->entries[local_idx].tmax      = p->ray_req.tmax;
}
```

**注意**: 当前 bucketed collect 是 2-pass (先计数, 再写入) 以实现 bucket 排序。
在 merged_pass 中无法做 2-pass (只扫描一遍)。两种选择:

**选项 A**: 放弃 bucket 排序 — 光线按 slot 顺序写入 pinned buf, 不按 bucket 分段。
GPU warp coherence 略降, 但 GPU throughput 已远超需求 (1667 Mrays/s)。

**选项 B**: thread-local bucket buffer — 每线程按 bucket 分类, 最后 merge 到 pinned。
更复杂但保留 warp coherence。

**推荐选项 A** (先验证原理, 性能问题后续再优化)。

### Step 5: 修改 harvest 为 per-path 调用

从 `harvest_completed_paths()` (L2655) 提取:

```c
static INLINE void
harvest_single_path(struct wavefront_pool* pool,
                    struct pool_view*      pv,
                    struct path_state*     p,
                    uint32_t               slot)
{
  /* 调用 accumulate_result (estimator 写入, 需要原子操作保护像素) */
  pool->ops->accumulate_result(pool->ops_ctx, p, &pool->slots[slot]);

  /* 标记为已收获 */
  pool->hot_arr[slot].phase = (uint8_t)PATH_HARVESTED;
  pool->hot_arr[slot].active = 0;

  /* 条件: CSV 追踪 (需要 critical section) */
#ifdef SDIS_TRACE_PATHS
  if(pool->trace_fp) {
    #pragma omp critical(trace_write)
    {
      fprintf(pool->trace_fp, "...", ...);
    }
  }
#endif
}
```

**关键注意**: `accumulate_result` 可能写入共享的 image 像素。
如果多个 path 写同一像素, 需要原子保护。当前实现已经用 `omp atomic` 或 `omp critical` —— 确认这一点。

### Step 6: 修改 refill

refill 不能完全合并, 因为它有串行 pre-allocation 阶段:

```c
/* refill 分为 3 步:
 * 1. 串行: 收集 harvestable slots, 分配 task_id 范围
 * 2. OMP: 并行初始化新 paths
 * 3. 串行: 更新 pool 计数器
 *
 * Step 1 和 3 必须在 merged_pass 之后做.
 * Step 2 可以合并, 但由于 step 1 先于 step 2, 无法和 merged_pass 合并.
 */
```

**决定**: refill 保持独立, 在 merged_pass + compact 之后执行。
refill 本身只对 done 的 slots 操作 (通常 <100 个/round), 开销极小。

### Step 7: 重组主循环

#### 7.1 修改 `pool_run_single()` (L3531)

```c
static res_T
pool_run_single(struct wavefront_pool* pool,
                struct s3d_scene_view* sv,
                struct sdis_scene* scn)
{
  struct pool_view* pv = &pool->views[0];
  res_T res;

  /* 初始 compact */
  compact_indices(pool, pv);
  pool_update_active_count(pool);

  while(pool->active_count > 0 || pool->task_next < pool->task_count) {
    pool->total_steps++;

    /* O11: 单次合并扫描 (distribute纯写 + cascade含step + collect三类 + harvest) */
    res = merged_pass(pool, pv, scn);
    if(res != RES_OK) return res;

    /* compact (仅 hot_arr, 160KB) */
    compact_indices(pool, pv);

    /* refill (串行 pre-alloc + OMP init) */
    res = refill_pool(pool, pv, &refill_count);
    if(res != RES_OK) return res;

    /* 统一 GPU dispatch: RT + CP + enc_locate 全部异步发射 */
    res = gpu_dispatch_all(pool, pv, sv);
    if(res != RES_OK) return res;

    /* GPU 等待: 全部三种 kernel 完成 + D2H 下载 */
    res = gpu_wait_d2h_all(pool, pv, sv);
    if(res != RES_OK) return res;

    pool_update_active_count(pool);

    if(pool->total_steps > pool->task_count * 1000)
      return RES_BAD_OP;
  }
  return RES_OK;
}
```

#### 7.2 修改 `pool_run_dual()` (L3588)

双缓冲 pipeline 中, merged_pass 替代 `cpu_between()` + `gpu_postprocess()`:

```c
/* ════════ Phase 1: wait GPU(A) → launch GPU(B) → merged CPU on A ════════ */
res = gpu_sync_kernel(pool, pv_a);
res = gpu_start_d2h(pool, pv_a);

res = gpu_launch_async(pool, pv_b, sv);   /* B 的 H2D + kernel (异步) */

res = gpu_wait_d2h(pool, pv_a, sv);        /* 等 A 的 D2H 完成 */

/* O11: merged_pass (distribute纯写 + cascade含step + collect三类 + harvest) */
res = merged_pass(pool, pv_a, scn);
compact_indices(pool, pv_a);
res = refill_pool(pool, pv_a, &cnt);

/* 统一 GPU dispatch: RT + CP + enc 全部异步 (A) */
res = gpu_dispatch_all(pool, pv_a, sv);

/* ════════ Phase 2: 同理 B ════════ */
```

### Step 8: 处理 distribute 纯写者的前置条件

merged_pass 中 distribute 需要知道 **上一轮**哪些 path 有 pending 外部查询结果。
使用 `hot->has_pending_hit` 标志 (新增):

```c
/* 在 merged_pass 的 OMP for 中: */

/* Phase A: distribute (纯写) */
distribute_write_results(pool, pv, slot, p, hot);
/* 写入 hit_result/enc_result/cp_result 到 slot-local 字段 */
/* has_pending_hit 仍为 1, 等 cascade 消费后清零 */

/* Phase B: cascade (含 step) */
cascade_advance_single_path_unified(...);
/* cascade Phase 1: 消费 pending 结果, 调用 step_*(), 清零 has_pending_hit */
/* cascade Phase 2: no-ray 循环, 可能产生新 needs_ray / enc_pending / cp_pending */

/* Phase C: collect (三种请求) */
/* hot->needs_ray / phase==PENDING 在 cascade 中设置, 此处收集 */
```

**上轮 gpu_dispatch_all 返回的三种结果**: ray_hits 在 pinned D2H buffer 中,
enc_locate 结果由 `gpu_wait_d2h_all()` 后处理写入 `enc_arr[slot]`,
cp 结果写入 `p->locals.cnd_wos`, 并更新 `hot->phase` 为 RESULT。
distribute 纯写者负责将 ray_hits 拷入 slot-local 字段 (见 Step 3)。
enc/cp 的 RESULT 状态由 cascade 的 `advance_one_step_no_ray()` 自然消费。

**has_pending_hit vs needs_ray 的区别:**
- `needs_ray`: cascade 输出标志, 表示 path 需要新的 ray trace
- `has_pending_hit`: distribute 输入标志, 表示上轮 GPU trace 有结果待写入

在 collect 阶段设置: `hot->has_pending_hit = hot->needs_ray;`
(当前 collect 写入 pinned buffer 的同时, 标记这些 path 在下轮有 hit 结果)。

### Step 8b: 统一 GPU Dispatch (gpu_dispatch_all)

替代原有的 `gpu_launch_async()` + `post_batch_enc_cp()`,
一次性排队所有 GPU 工作 (RT + CP + enc_locate), CPU 立刻返回。

```c
/**
 * gpu_dispatch_all — unified async launch of all external queries.
 *
 * Queues RT trace + CP batch + enc_locate batch on compute_stream.
 * CPU returns immediately. Results available after gpu_wait_d2h_all().
 *
 * enc_locate is decomposed into CP + RT steps, both on compute_stream.
 * Step 1 (CP) and Step 2 (RT) are independent (ray dir = fixed +X)
 * and can be launched back-to-back without inter-step sync.
 */
static res_T
gpu_dispatch_all(struct wavefront_pool* pool,
                 struct pool_view*      pv,
                 struct s3d_scene_view* sv)
{
  s3d_batch_trace_context* ctx = pv->trace_ctx;

  /* ── H2D on transfer_stream ── */
  if (pv->ray_count > 0)
    upload_rays_pinned_async(ctx, pv, ctx->transfer_stream);

  if (pv->cp_count > 0)
    upload_cp_pinned_async(ctx, pv, ctx->transfer_stream);

  if (pv->enc_locate_count > 0) {
    upload_enc_pinned_async(ctx, pv, ctx->transfer_stream);
    /* enc Step 2 rays: +X from each query position, pre-built in collect */
    upload_enc_rays_pinned_async(ctx, pv, ctx->transfer_stream);
  }
  cudaEventRecord(ctx->evt_upload_done, ctx->transfer_stream);

  /* ── Kernels on compute_stream ── */
  cudaStreamWaitEvent(ctx->compute_stream, ctx->evt_upload_done, 0);

  if (pv->ray_count > 0)     /* 1. RT trace */
    optixLaunch(..., ctx->compute_stream, &ctx->sbt_rt, ...);

  if (pv->cp_count > 0)      /* 2. CP batch */
    optixLaunch(..., ctx->compute_stream, &ctx->sbt_cp, ...);

  if (pv->enc_locate_count > 0) {
    optixLaunch(..., ctx->compute_stream, &ctx->sbt_cp, ...);  /* enc Step 1: CP */
    optixLaunch(..., ctx->compute_stream, &ctx->sbt_rt, ...);  /* enc Step 2: RT */
  }

  /* ── D2H on transfer_stream ── */
  cudaEventRecord(ctx->evt_kernels_done, ctx->compute_stream);
  cudaStreamWaitEvent(ctx->transfer_stream, ctx->evt_kernels_done, 0);
  download_all_results_async(ctx, pv, ctx->transfer_stream);

  pv->gpu_pending = 1;
  return RES_OK;
}
```

**enc_locate Step 2 不依赖 Step 1**: 当前 unified_tracer.cpp 在两步之间
做 `cudaStreamSynchronize` 以下载 CP 结果、在 CPU 上构建射线方向。
但射线方向实际是固定的 (+X, tmin=1e-6, tmax=1e30), 不使用 CP 结果。
因此两步可以背靠背发射到 compute_stream, 无需 inter-step sync。

CP 结果 (nearest prim + distance) 和 RT 结果 (hit/miss + normal) 都在 D2H
之后由 `gpu_wait_d2h_all()` 中 CPU 后处理合并为 EnclosureResult (side 判定)。

**overhead 评估** (详见 analysis.md §5.7.5):
- 每次 optixLaunch API call: ~5-10μs
- 大多轮: 仅 1 launch (RT), 与当前持平
- 有 enc/cp 的轮: 2-4 launches, 额外 10-30μs, negligible
- 不需要 CUDA Graph

**所需新增 pinned buffer (pool_view):**
```c
struct pool_view {
    /* ... existing Plan E fields ... */

    /* enc/cp pinned buffers */
    s3d_cp_request*         cp_pinned;           /* cudaHostAlloc */
    s3d_hit*                cp_hits_pinned;       /* D2H results */
    s3d_enc_locate_request* enc_pinned;           /* cudaHostAlloc */
    CPResult*               enc_cp_results_pinned;/* D2H: CP step */
    HitResult*              enc_rt_results_pinned;/* D2H: RT step */
};
```

### Step 9: compact_indices 轻量化

新增一个只扫描 hot_arr 的轻量 compact (不扫描 path_state):

```c
/**
 * compact_indices — lightweight index rebuild from hot_arr only.
 * hot_arr = 20K × 8B = 160 KB → L2 内, 无 L3 thrash.
 */
static void
compact_indices(struct wavefront_pool* pool, struct pool_view* pv)
{
  size_t base = pv->base;
  size_t end = base + pv->view_size;
  size_t i;

  pv->active_compact = 0;

  for(i = base; i < end; i++) {
    if(pool->hot_arr[i].active) {
      pv->active_indices[pv->active_compact++] = (uint32_t)i;
    }
  }
  /* bucket indices 不再需要 — merged_pass 自行判断 bucket 类型 */
}
```

**注意**: 如果选择保留 bucket 排序 (Step 4 选项 B), compact_indices 仍需要构建 bucket arrays。

---

## 4. 代码变更详解

### 4.1 变更文件清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `sdis_solve_persistent_wavefront.c` | 核心变更 | 新增 merged_pass, 修改 pool_run_single/dual |
| `sdis_solve_persistent_wavefront.h` | 可能变更 | 新增 pool_view 字段 (prev_ray_count 等) |
| `sdis_wf_steps.h` | 可能变更 | 如果提取 inline 函数声明 |

### 4.2 新增结构

```c
/* thread-local ray collect buffer (避免 pinned memory atomic 竞争) */
struct tl_ray_entry {
  uint32_t  slot;
  uint8_t   bucket;
  uint8_t   ray_count;
  uint8_t   pad[2];
  double    origin[3];
  double    dir[3];
  double    tmin, tmax;
};

struct tl_ray_buf {
  struct tl_ray_entry* entries;
  size_t count;
  size_t capacity;
};
```

### 4.3 新增 pool_view 字段

```c
/* sdis_solve_persistent_wavefront.h */
struct pool_view {
  /* ... existing fields ... */

  /* O11: merged_pass 需要的上一轮状态 */
  size_t prev_ray_count;           /* 上一轮实际 trace 的 ray 数, 0 = 首轮 */
};
```

### 4.4 被删除/替代的函数调用

| 原调用 | 替代 | 位置 |
|--------|------|------|
| `cpu_pre_gpu()` 中的 `compact_active_paths()` | `compact_indices()` (轻量) | pool_run_single/dual |
| `cpu_pre_gpu()` 中的 `pool_collect_ray_requests_bucketed()` | 合并到 merged_pass | merged_pass Phase C |
| `gpu_postprocess()` 中的 `pool_distribute_ray_results()` | 拆为纯写 + step 吸收 | Phase A (纯写) + Phase B (含 step) |
| `gpu_postprocess()` 中的 `enc_locate + cp batch` | `post_batch_enc_cp()` | 延迟一轮, 与 RT 对称 |
| `cpu_between()` 中的 `pool_cascade_non_ray_steps_compact()` | 合并到 merged_pass | merged_pass Phase B |
| `cpu_between()` 中的 `compact_active_paths()` | `compact_indices()` | pool_run_single |
| `cpu_between()` 中的 `harvest_completed_paths()` | 合并到 merged_pass | merged_pass Phase D |

`cpu_pre_gpu()`, `gpu_postprocess()`, `cpu_between()` 的调用点
改为 `merged_pass()` + `post_batch_enc_cp()` + `compact_indices()` + `refill_pool()`。
原 distribute 中的 step 逻辑移入 cascade, distribute 退化为纯写者。
被替代的原函数可删除或标记 `UNUSED` 以减少代码量。

---

## 5. 验证流程

### 5.1 逐步验证 (推荐)

| 阶段 | 验证内容 | 方法 |
|------|---------|------|
| V1 | merged_pass 编译通过 | `cmake --build . --config Release > build.log 2>&1` |
| V2 | 单 path 正确性 | 用 spp=1 pool=1 的极简场景, 对比输出 |
| V3 | 多 path bit-exact | pool=128 spp=4 img=64x64, 对比 IR 输出文件 |
| V4 | 完整场景功能正确性 | porous 320×320×32 pool=20000, 对比 IR 增量 (enc/cp 延迟一轮导致 RNG 偏移, 不要求 bit-exact) |
| V5 | 性能测量 | 同场景, 对比 wall/cascade/distribute 等计时 |

### 5.2 功能正确性验证命令

由于 enc/cp 延迟一轮导致 total_steps 可能略变, RNG 序列偏移, 不保证 bit-exact。
验证标准: 统计指标一致 (total_rays, completed_paths, failed_paths) + IR 图像视觉一致。

```bash
# 基线
<stardis-exe-main> -M porous.txt -t 32 -V 3 -R spp=32:img=320x320:... > baseline.ht

# O11
<stardis-exe-merge> -M porous.txt -t 32 -V 3 -R spp=32:img=320x320:... > merged.ht

# 对比统计指标 (应接近)
findstr "total_rays completed_paths failed_paths" baseline.log merged.log

# 视觉对比 (IR 图像应无明显差异)
```

### 5.3 不变量检查

在 merged_pass 完成后断言:
```c
assert(pool->cascade_total_iterations == expected_iterations);
assert(pool->total_rays_traced == expected_rays);
assert(pool->paths_completed + pool->paths_failed == expected_total_tasks);
```

---

## 附录 A: 已知陷阱

### A.1 distribute 中 enc_query 的 6-ray 预传递

当前 distribute Phase 3 (L2204-2247) 中, enc_query paths 需要从 `ray_hits` 中预先复制 6 个 hit 到 `enc_arr[slot].dir_hits[]`:

```c
if(ph_before == PATH_ENC_QUERY_EMIT && hot->ray_count_ext == 6) {
  for(j = 0; j < 6; j++) {
    pool->enc_arr[i].dir_hits[j] =
      pv->ray_hits[pool->enc_arr[i].batch_indices[j]];
  }
}
```

这必须在 `advance_one_step_with_ray()` 之前完成。在 merged_pass 中, 确保 `distribute_single_other()` 正确处理此逻辑。

### A.2 bnd_ss 4-ray 预传递

类似地, boundary solid-solid 的 4-ray (L2230-2240):

```c
if(ph_before == PATH_BND_SS_REINJECT_SAMPLE && hot->ray_count_ext == 4) {
  p->locals.bnd_ss.ray_frt[0] = pv->ray_hits[p->locals.bnd_ss.batch_idx_frt0];
  /* ... 3 more ... */
}
```

### A.3 OMP schedule 选择

cascade 用 `schedule(dynamic, 64)` (因为 for(;;) 内循环次数不均)。
distribute 用 `schedule(static)` (因为每 path 工作量固定)。

merged_pass 中两者合并, 应使用 `schedule(dynamic, 64)` ——因为 cascade 的不均匀性主导。

### A.4 pinned memory 写入顺序

当前 bucketed collect 保证同一 bucket 的 rays 连续存储 (warp coherence)。
merged_pass 中 rays 按 slot 顺序写入, bucket 混杂。

**影响**: GPU BVH traversal warp coherence 略降。
**量化**: 当前 GPU throughput 1667 Mrays/s, 远超需求 (瓶颈在 CPU)。
即使降到 800 Mrays/s, gpu_kernel 从 7.7s 增到 16s, 仍在 pipeline 中被 CPU 隐藏。

### A.5 refill 中的 advance_path_to_first_ray

新 path 初始化后立即调用 `advance_path_to_first_ray()`, 这会推进状态机直到产生第一条 ray 或完成。
这段逻辑在 refill 的 OMP 阶段执行, 与 merged_pass 无冲突 (不同的 slots)。

---

## 附录 B: 计时修改

merged_pass 合并了多个 phase, 原有的 per-phase 计时 (`time_cascade_s`, `time_distribute_s`, `time_collect_s`) 不再可分。

**方案**: 新增 `time_merged_pass_s`, 保留原 per-phase 计时为 0 (或通过内部子计时近似):

```c
struct time t_merged_0, t_merged_1;
time_current(&t_merged_0);
merged_pass(pool, pv, scn);
time_current(&t_merged_1);
pool->time_merged_pass_s += time_elapsed_sec(&t_merged_0, &t_merged_1);
```

在输出摘要中新增:
```
timing: merged_pass=42.0s  enc_locate=2.2s  cp=2.2s  compact=2.0s  refill=3.0s
        gpu_sync=1.4s  gpu_launch=21.4s
```

---

## 附录 C: enc/cp 延迟安全性验证

### C.0 背景: 上次重构失败的根因

之前尝试将 enc/cp 延迟一轮时, cascade 在运行时报错。根因分析:

**失败模式**: distribute 将 hot->phase 写为 `PATH_ENC_LOCATE_RESULT` 或 `PATH_CND_WOS_*_RESULT`,
但 GPU batch 尚未执行 (或结果未分发), 导致 cascade 中的 step 函数读到 **陈旧/未初始化数据**:

| 场景 | 读取的字段 | 后果 |
|------|-----------|------|
| enc_arr 未写入 | `enc->locate.prim_id` (可能是上轮残留值或随机值) | `scene_get_enclosure_ids(scn, garbage_id, ...)` → 越界/错误几何 |
| cached_hit 未写入 | `hit->distance` (可能是 0 或 NaN) | `S3D_HIT_NONE(hit)` 返回 false → WOS 使用垃圾距离 |

**核心不变量**: 相位 (phase) 转换到 RESULT 时, 对应的数据字段 **必须已被 GPU 结果覆写**。
这是一个 **写-读因果序** (write-before-read causality) 约束, 违反即产生运行时错误。

### C.1 Phase-Data 耦合不变量

每种外部查询的 PENDING → RESULT 转换都有严格的数据前置条件:

| Phase 转换 | 前置写入 | 消费读取 (step 函数) | 违反后果 |
|------------|---------|---------------------|---------|
| `PATH_ENC_LOCATE_PENDING` → `PATH_ENC_LOCATE_RESULT` | `enc_arr[slot].locate.{prim_id, side, distance}` = GPU result | `step_enc_locate_result()` 读 `prim_id`, `side` → `scene_get_enclosure_ids()` | 无效 prim_id → 数组越界 |
| `PATH_CND_WOS_CLOSEST` → `PATH_CND_WOS_CLOSEST_RESULT` | `p->locals.cnd_wos.cached_hit` = GPU cp result | `step_cnd_wos_closest_result()` 读 `cached_hit.distance`, 传整个 hit 给 `wf_setup_hit_wos()` | 垃圾 distance → WOS 路径发散 |
| `PATH_CND_WOS_DIFFUSION_CHECK` → `PATH_CND_WOS_DIFFUSION_CHECK_RESULT` | `p->locals.cnd_wos.cached_hit` = GPU cp result | `step_cnd_wos_diffusion_check_result()` 读 `cached_hit.prim.prim_id` → `scene_get_enclosure_ids()` | 无效 prim_id → 越界 |

**不变量公式**: ∀ slot s, 若 `hot_arr[s].phase ∈ {*_RESULT}`, 则对应数据域已被当轮 GPU 结果覆写。

### C.2 三个危险区域

在 O11 合并设计中, 以下三个位置可能违反 C.1 的不变量:

#### Zone A: distribute 纯写者跳过 enc/cp 数据写入

在 `distribute_write_results()` (Step 3) 中, ray_hits 由 distribute 主动写入 slot-local。
但 enc/cp 结果由 `gpu_wait_d2h_all()` 的后处理直接写入 `enc_arr` 和 `cnd_wos`。

**验证点**: `gpu_wait_d2h_all()` 的后处理 **必须在 merged_pass 之前完成**, 且写入的 slot
集合与 `pv->enc_locate_to_slot[]` / `pv->cp_to_slot[]` 完全一致。

```
✅ 安全序列:  gpu_wait_d2h_all() [写 enc_arr + cnd_wos + phase] → merged_pass() [读]
❌ 危险序列:  merged_pass() → gpu_wait_d2h_all()  (读到上轮残留)
```

#### Zone B: RESULT phase 但 batch count = 0

若某轮没有任何 enc/cp 请求 (count=0), distribute 不会覆写任何 slot。
**但如果 hot->phase 残留了上轮的 RESULT 值** (因为 cascade 未消费), cascade 会
再次调用 step_enc_locate_result() 读取 enc_arr — 此时 enc_arr 是上一轮的已消费过的旧数据。

**验证点**: cascade 消费 RESULT 后, step 函数必须将 phase 转换为非 RESULT 状态。
当前代码在 `step_enc_locate_result()` L105 中做了:
```c
hot->phase = (uint8_t)enc->locate.return_state;  /* 恢复为原始 phase */
```
这保证了 RESULT phase 是 **一次性的**, 不会残留到下轮。✅ 安全。

#### Zone C: slot 复用竞争

`refill()` 将新 path 分配到已完成的 slot。如果某 slot 进入 DONE 并被 refill 复用,
但 GPU 仍在处理该 slot 的上轮 enc/cp 请求,
`gpu_wait_d2h_all()` 会将旧结果写入已被新 path 占用的 slot。

**验证点**: refill 只能操作 `active=0` 的 slot; `gpu_wait_d2h_all()` 通过
`pv->enc_locate_to_slot[]` 索引写入, 该数组在 `collect` 阶段构建, 反映的是
collect 时刻的 slot 状态。只要 collect → gpu_dispatch → gpu_wait → merged_pass → refill
的顺序不变, slot 复用发生在 refill, 晚于 gpu_wait 的写入。✅ 安全。

**但需要额外检查**: refill 的新 path 是否将 hot->phase 初始化为非 PENDING/RESULT。
当前 `advance_path_to_first_ray()` 从 PATH_INIT 开始推进, 不会产生残留 RESULT phase。✅ 安全。

### C.3 静态代码验证检查清单

在实现每个 Step 时, 必须逐项检查以下条件:

#### 检查 1: 写-读因果序

```
[ ] gpu_wait_d2h_all() 中的后处理按 pv->enc_locate_to_slot[] 写入 enc_arr[slot].locate.*
[ ] gpu_wait_d2h_all() 中的后处理按 pv->cp_to_slot[] 写入 p->locals.cnd_wos.cached_hit
[ ] gpu_wait_d2h_all() 中的后处理设置 hot_arr[slot].phase = *_RESULT
[ ] merged_pass() 在 gpu_wait_d2h_all() 返回之后调用
[ ] cascade_advance_single_path_unified() Phase 2 的 advance_one_step_no_ray()
    在读取 enc_arr / cnd_wos 之前, 能确认 phase 为 *_RESULT (由 switch/case 保证)
```

#### 检查 2: Phase 一次性消费

```
[ ] step_enc_locate_result() 在 return 前将 hot->phase 设为 return_state (非 RESULT)
[ ] step_cnd_wos_closest_result() 在 return 前将 hot->phase 推进 (PATH_CND_WOS_TIME_TRAVEL 或后续)
[ ] step_cnd_wos_diffusion_check_result() 在 return 前将 hot->phase 推进
[ ] 以上三个 step 函数均为幂等安全的 (不会重复消费同一 RESULT)
```

#### 检查 3: collect 与 dispatch 一致性

```
[ ] collect 阶段写入的 pv->enc_locate_count 与 gpu_dispatch_all 发射的 enc batch size 一致
[ ] collect 阶段写入的 pv->cp_count 与 gpu_dispatch_all 发射的 cp batch size 一致
[ ] pv->enc_locate_to_slot[k] 在 [0, pool_size) 范围内
[ ] pv->cp_to_slot[k] 在 [0, pool_size) 范围内
[ ] count=0 时 gpu_dispatch_all 跳过对应 batch (不发射空 kernel)
```

#### 检查 4: refill 隔离

```
[ ] refill 仅修改 active=0 且 phase==PATH_DONE 的 slot
[ ] refill 的 advance_path_to_first_ray() 将 phase 推进到 needs_ray=1 或 PATH_DONE
[ ] refill 不会将 phase 设为任何 *_PENDING 或 *_RESULT
```

### C.4 Debug 断言 (开发阶段)

在开发和调试阶段嵌入以下断言, 验证通过后可用 `#ifdef SDIS_DEBUG_CHECKS` 条件编译:

```c
/* ═══ 断言 1: merged_pass 入口 — 确认 RESULT phase 有对应的 GPU 写入 ═══ */
#ifdef SDIS_DEBUG_CHECKS
static void
assert_result_phases_backed(struct wavefront_pool* pool,
                            struct pool_view*      pv)
{
  size_t i;
  /* enc_locate: 所有 RESULT 必须在 enc_locate_to_slot 中 */
  for(i = pv->base; i < pv->base + pv->view_size; i++) {
    if(pool->hot_arr[i].phase == (uint8_t)PATH_ENC_LOCATE_RESULT) {
      /* 搜索 pv->enc_locate_to_slot[] 确认 slot i 在其中 */
      size_t k;
      int found = 0;
      for(k = 0; k < pv->enc_locate_count; k++) {
        if(pv->enc_locate_to_slot[k] == (uint32_t)i) { found = 1; break; }
      }
      ASSERT(found && "RESULT phase without GPU write: enc_locate");
    }
    if(pool->hot_arr[i].phase == (uint8_t)PATH_CND_WOS_CLOSEST_RESULT
    || pool->hot_arr[i].phase == (uint8_t)PATH_CND_WOS_DIFFUSION_CHECK_RESULT) {
      size_t k;
      int found = 0;
      for(k = 0; k < pv->cp_count; k++) {
        if(pv->cp_to_slot[k] == (uint32_t)i) { found = 1; break; }
      }
      ASSERT(found && "RESULT phase without GPU write: cp");
    }
  }
}
#endif

/* 在 merged_pass() 入口调用: */
#ifdef SDIS_DEBUG_CHECKS
  assert_result_phases_backed(pool, pv);
#endif
```

```c
/* ═══ 断言 2: merged_pass 出口 — 确认无残留 RESULT phase ═══ */
#ifdef SDIS_DEBUG_CHECKS
static void
assert_no_residual_results(struct wavefront_pool* pool,
                           struct pool_view*      pv)
{
  size_t i;
  for(i = pv->base; i < pv->base + pv->view_size; i++) {
    if(!pool->hot_arr[i].active) continue;
    ASSERT(pool->hot_arr[i].phase != (uint8_t)PATH_ENC_LOCATE_RESULT
        && "Residual ENC_LOCATE_RESULT after merged_pass");
    ASSERT(pool->hot_arr[i].phase != (uint8_t)PATH_CND_WOS_CLOSEST_RESULT
        && "Residual CND_WOS_CLOSEST_RESULT after merged_pass");
    ASSERT(pool->hot_arr[i].phase != (uint8_t)PATH_CND_WOS_DIFFUSION_CHECK_RESULT
        && "Residual CND_WOS_DIFFUSION_CHECK_RESULT after merged_pass");
  }
}
#endif

/* 在 merged_pass() 出口调用: */
#ifdef SDIS_DEBUG_CHECKS
  assert_no_residual_results(pool, pv);
#endif
```

```c
/* ═══ 断言 3: gpu_dispatch_all 前 — 确认无 *_RESULT phase 残留 ═══ */
/* 所有 RESULT 应被 cascade 消费。若有残留, 说明 cascade 未触达该 slot。 */
#ifdef SDIS_DEBUG_CHECKS
static void
assert_no_pending_result_before_dispatch(struct wavefront_pool* pool,
                                          struct pool_view*      pv)
{
  size_t i;
  for(i = pv->base; i < pv->base + pv->view_size; i++) {
    if(!pool->hot_arr[i].active) continue;
    enum path_phase ph = (enum path_phase)pool->hot_arr[i].phase;
    ASSERT(ph != PATH_ENC_LOCATE_RESULT
        && ph != PATH_CND_WOS_CLOSEST_RESULT
        && ph != PATH_CND_WOS_DIFFUSION_CHECK_RESULT
        && "RESULT phase still present at gpu_dispatch_all");
  }
}
#endif
```

```c
/* ═══ 断言 4: enc_locate 写入后毒化 — 防止重复读取 ═══ */
/* 在 step_enc_locate_result() 消费 enc_arr 后, 毒化 prim_id 防止二次消费 */
#ifdef SDIS_DEBUG_CHECKS
  enc->locate.prim_id = -99999;  /* 毒值, 二次读取时 scene_get_enclosure_ids 必崩 */
  enc->locate.side = -99999;
#endif
```

### C.5 首轮特殊处理

第一轮主循环时尚无任何 GPU 结果, distribute 没有数据可写, cascade 没有 RESULT 可消费。

**验证点**: 确认首轮行为正确:

```
Round 0:
  gpu_wait_d2h_all() — 无结果 (enc_count=0, cp_count=0, ray_count=0)
  merged_pass():
    Phase A (distribute): has_pending_hit=0 → 跳过; 无 RESULT phase → 跳过
    Phase B (cascade): 从 PATH_INIT 推进, 产生 needs_ray
    Phase C (collect): 收集 ray requests
    Phase D (harvest): 无完成的 path
  compact_indices()
  refill(): 所有 slot 已在初始化时填充, 无需 refill
  gpu_dispatch_all(): 仅发射 RT batch

Round 1+:
  gpu_wait_d2h_all() — 有 ray_hits, 可能有 enc/cp results
  merged_pass():
    Phase A: ray_hits 写入 slot-local
    Phase B: cascade 消费 ray RESULT → 推进 → 可能产生 enc/cp PENDING
    Phase C: 收集 ray + enc + cp requests  (enc/cp 延迟到 Round 2 才有 RESULT)
    Phase D: harvest 完成的 paths
```

**关键**: Round 1 的 cascade 产生的 enc/cp PENDING, 在 Round 1 的 collect 中被收集,
Round 1 的 gpu_dispatch_all 中发射, Round 2 的 gpu_wait_d2h_all 中分发回来。
这就是 "延迟一轮" 的正常语义, cascade guard 在 Round 1 末尾自然 break 在 PENDING 上,
Round 2 distribute 写入 RESULT 后 cascade 继续。

### C.6 逻辑链完整性验证 (人工走查)

对每种外部查询, 走完以下完整链路, 确认每一步的前置条件:

#### enc_locate 完整链路

```
Round N:
  cascade: advance_one_step_no_ray → 某 step 设置
           hot->phase = PATH_ENC_LOCATE_PENDING
           + 写入 enc_arr[slot].locate.query_pos
           + 写入 enc_arr[slot].locate.return_state
    → cascade for(;;) 在 guard L2429 break

  collect: 检测 phase == PATH_ENC_LOCATE_PENDING
           将 slot 加入 pv->enc_locate_to_slot[count]
           将 enc_arr[slot].locate.query_pos 写入 pv->enc_locate_requests[count]
           count++

  gpu_dispatch_all: 发射 enc_locate batch (Step1 CP + Step2 RT)

Round N+1:
  gpu_wait_d2h_all (后处理):
    for k in [0, enc_locate_count):
      slot = pv->enc_locate_to_slot[k]
      enc_arr[slot].locate.prim_id  = GPU_result[k].prim_id
      enc_arr[slot].locate.side     = GPU_result[k].side
      enc_arr[slot].locate.distance = GPU_result[k].distance
      hot_arr[slot].phase = PATH_ENC_LOCATE_RESULT       ← 数据先写, phase 后写

  merged_pass:
    Phase A: (enc_locate 无需 distribute 额外操作)
    Phase B: cascade → guard L2429 不 break (phase 是 RESULT 不是 PENDING)
             → advance_one_step_no_ray → case PATH_ENC_LOCATE_RESULT:
               → step_enc_locate_result()
                  读 enc_arr[slot].locate.{prim_id, side} ← 已由后处理写入 ✅
                  写 enc_arr[slot].locate.resolved_enc_id
                  hot->phase = enc->locate.return_state   ← 退出 RESULT ✅
```

#### cp (cnd_wos) 完整链路

```
Round N:
  cascade: advance_one_step_no_ray → 某 step (如 step_conductive_wos_*)
           设置 hot->phase = PATH_CND_WOS_CLOSEST (或 DIFFUSION_CHECK)
           + 写入 p->locals.cnd_wos.query_pos
    → cascade for(;;) 在 guard L2430 break

  collect: 检测 path_phase_is_cp_pending(phase)
           将 slot 加入 pv->cp_to_slot[count]
           将 cnd_wos.query_pos 写入 pv->cp_requests[count]
           count++

  gpu_dispatch_all: 发射 cp batch (CP SBT)

Round N+1:
  gpu_wait_d2h_all (后处理):
    for k in [0, cp_count):
      slot = pv->cp_to_slot[k]
      p->locals.cnd_wos.cached_hit = GPU_cp_hits[k]      ← 数据先写
      hot_arr[slot].phase = *_RESULT (根据原 phase 判断)   ← phase 后写

  merged_pass:
    Phase A: (cp 无需 distribute 额外操作)
    Phase B: cascade → guard L2430 不 break (phase 是 *_RESULT 不是 *_CLOSEST/DIFFUSION_CHECK)
             → advance_one_step_no_ray → case PATH_CND_WOS_CLOSEST_RESULT:
               → step_cnd_wos_closest_result()
                  读 p->locals.cnd_wos.cached_hit.distance  ← 已由后处理写入 ✅
                  传 hit 给 wf_setup_hit_wos()               ← 数据完整 ✅
                  推进 phase 离开 RESULT                      ← 一次性消费 ✅
```

### C.7 实施时的卡点标记

在代码注释中嵌入以下标记, 方便代码审查和 grep:

```c
/* O11_SAFETY: write-before-read — data must be written before phase → RESULT */
/* O11_SAFETY: one-shot — RESULT phase consumed exactly once, then transitioned */
/* O11_SAFETY: collect-dispatch-match — collect count must equal dispatch count */
/* O11_SAFETY: slot-isolation — refill only touches DONE/inactive slots */
```

在 merged_pass, gpu_dispatch_all, gpu_wait_d2h_all 的关键位置加上这些标记,
使得 `grep O11_SAFETY` 可以快速定位所有安全相关代码。

---

*文档版本: v1.1 | O11-MergePhase 开发指南 — 新增附录 C enc/cp 延迟安全性验证*
