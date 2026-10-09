# Phase 3: 主循环流水线化

**前置依赖**: Phase 1 (submit/wait) + Phase 2 (双缓冲)  
**预计工时**: 2-3 天  
**验证方式**: `STARDIS_PIPELINE=0` vs `STARDIS_PIPELINE=1` 输出 bit-exact + ctest 全通过

---

## 一、目标

将 `solve_camera_persistent_wavefront()` 的主循环从 A→B→**C(阻塞)**→D→E→F→G 串行模式重构为双缓冲流水线，使 GPU trace(N) 与 CPU distribute(N-1)+cascade+harvest+compact+collect(N) 同时执行。

### 目标时间线

```
串行 (当前):
  CPU: ─[A.compact][B.collect]──────[空闲]────────[D.distrib][D2.enc][E.cascade][F+G.harvest]──▶
  GPU: ───────────[空闲]──────[C.trace(阻塞)]──────────────────[空闲]──────────────────────────▶
                                     ↑ 唯一GPU工作段

流水线 (目标):
  GPU: ──[trace(N)]──────────────────────[trace(N+1)]──────────────────────[trace(N+2)]──▶
  CPU: ──[wait(N-1)+distrib+enc+         [wait(N)+distrib+enc+
          cascade+harvest+refill+          cascade+harvest+refill+
          compact+collect → submit(N)]     compact+collect → submit(N+1)]
```

---

## 二、模式开关

### 2.1 环境变量控制

```c
/* 在 solve_camera_persistent_wavefront() 初始化段 */
int use_pipeline = 0;
{
    const char* env = getenv("STARDIS_PIPELINE");
    if (env) use_pipeline = atoi(env);
}
```

**默认关闭** (`STARDIS_PIPELINE=0` 或未设置)。

### 2.2 串行模式保留

当 `use_pipeline == 0` 时，执行原有串行逻辑（利用 Phase 2 的双缓冲基础设施，但 `buf_curr = buf_prev = 0`）。原始主循环代码完整保留在 `else` 分支中。

```c
if (use_pipeline) {
    /* === 流水线主循环 (Phase 3) === */
    // ...
} else {
    /* === 串行主循环 (原始逻辑) === */
    // ... 原始代码，引用 ray_requests[0], batch_ctx[0] 等
}
```

---

## 三、流水线主循环设计

### 3.1 完整伪代码

```c
/* ============================================================
 * 流水线模式主循环
 * ============================================================ */

pool.buf_curr = 0;
pool.buf_prev = 1;
pool.pipeline_active = 0;

/* ---- Prologue: 首轮提交，无前序GPU结果 ---- */
compact_active_paths(&pool);
pool.ray_count[pool.buf_curr] = 0;
res = pool_collect_ray_requests_bucketed(&pool);
if (res != RES_OK) goto cleanup;

if (pool.ray_count[pool.buf_curr] > 0) {
    res = s3d_scene_view_trace_rays_batch_submit(
        scn->s3d_view,
        pool.batch_ctx[pool.buf_curr],
        pool.ray_requests[pool.buf_curr],
        pool.ray_count[pool.buf_curr]);
    if (res != RES_OK) goto cleanup;
    pool.pipeline_active = 1;
}
pool_flip_buffers(&pool);  /* curr=1, prev=0 */

/* ---- 稳态循环 ---- */
while (pool.active_count > 0 || pool.task_next < pool.task_count) {

    pool.total_steps++;

    /* ==== Phase A: 等待上一轮GPU + 处理结果 ==== */
    
    time_current(&t_phase0);
    
    if (pool.pipeline_active) {
        /* A1: 等待 GPU trace(N-1) 完成 */
        struct s3d_batch_trace_stats stats;
        memset(&stats, 0, sizeof(stats));
        
        res = s3d_scene_view_trace_rays_batch_wait(
            scn->s3d_view,
            pool.batch_ctx[pool.buf_prev],
            pool.ray_hits[pool.buf_prev],
            pool.ray_count[pool.buf_prev],
            &stats);
        if (res != RES_OK) goto cleanup;
        
        pool.total_rays_traced += pool.ray_count[pool.buf_prev];
        /* 累积 batch trace stats (同现有代码) */
        pool.trace_call_count++;
        pool.trace_batch_size_sum += pool.ray_count[pool.buf_prev];
        // ... 其余 stats 累积 ...
    }
    
    time_current(&t_phase1);
    pool.time_trace_s += time_elapsed_sec(&t_phase0, &t_phase1);
    
    /* A2: Distribute ray results (使用 buf_prev 的数据) */
    time_current(&t_phase0);
    if (pool.pipeline_active) {
        res = pool_distribute_ray_results(&pool, scn);
        if (res != RES_OK) goto cleanup;
    }
    time_current(&t_phase1);
    pool.time_distribute_s += time_elapsed_sec(&t_phase0, &t_phase1);
    
    /* A3: ENC locate batch (仍同步) */
    time_current(&t_phase0);
    res = pool_collect_enc_locate_requests(&pool);
    if (res != RES_OK) goto cleanup;
    if (pool.enc_locate_count > 0) {
        struct s3d_batch_enc_stats enc_stats;
        memset(&enc_stats, 0, sizeof(enc_stats));
        res = s3d_scene_view_find_enclosure_batch_ctx(
            scn->s3d_view, pool.enc_batch_ctx,
            pool.enc_locate_requests, pool.enc_locate_count,
            pool.enc_locate_results, &enc_stats);
        if (res != RES_OK) goto cleanup;
        res = pool_distribute_enc_locate_results(&pool);
        if (res != RES_OK) goto cleanup;
        pool.enc_locates_total += pool.enc_locate_count;
        pool.enc_locates_resolved += enc_stats.resolved;
        pool.enc_locates_degenerate += enc_stats.degenerate;
    }
    time_current(&t_phase1);
    /* time_enc_locate_s += ... */
    
    /* A4: Cascade non-ray steps */
    time_current(&t_phase0);
    res = pool_cascade_non_ray_steps_compact(&pool, scn);
    if (res != RES_OK) goto cleanup;
    time_current(&t_phase1);
    pool.time_cascade_s += time_elapsed_sec(&t_phase0, &t_phase1);
    
    /* A5: Harvest + Refill */
    time_current(&t_phase0);
    compact_active_paths(&pool);
    res = harvest_completed_paths(&pool, buf);
    if (res != RES_OK) goto cleanup;
    {
        size_t refill_count = 0;
        res = refill_pool(&pool, &refill_count);
        if (res != RES_OK) goto cleanup;
    }
    time_current(&t_phase1);
    pool.time_harvest_s += time_elapsed_sec(&t_phase0, &t_phase1);
    
    /* ==== Phase B: 准备下一轮 + 异步提交GPU ==== */
    
    /* B1: Stream compaction */
    time_current(&t_phase0);
    compact_active_paths(&pool);
    time_current(&t_phase1);
    pool.time_compact_s += time_elapsed_sec(&t_phase0, &t_phase1);
    
    /* B2: Collect ray requests */
    time_current(&t_phase0);
    pool.ray_count[pool.buf_curr] = 0;
    res = pool_collect_ray_requests_bucketed(&pool);
    if (res != RES_OK) goto cleanup;
    time_current(&t_phase1);
    pool.time_collect_s += time_elapsed_sec(&t_phase0, &t_phase1);
    
    /* B3: Async submit */
    if (pool.ray_count[pool.buf_curr] > 0) {
        res = s3d_scene_view_trace_rays_batch_submit(
            scn->s3d_view,
            pool.batch_ctx[pool.buf_curr],
            pool.ray_requests[pool.buf_curr],
            pool.ray_count[pool.buf_curr]);
        if (res != RES_OK) goto cleanup;
        pool.pipeline_active = 1;
    } else {
        pool.pipeline_active = 0;
    }
    
    /* B4: Flip buffers */
    pool_flip_buffers(&pool);
    
    /* ---- 更新 active count + diagnostics + progress (同原始代码) ---- */
    pool_update_active_count(&pool);
    pool_update_diagnostics(&pool);
    // ... 进度报告、drain阶段检测、安全检查 ...
}

/* ---- Epilogue: 处理最后一轮GPU结果 ---- */
if (pool.pipeline_active) {
    struct s3d_batch_trace_stats stats;
    memset(&stats, 0, sizeof(stats));
    
    res = s3d_scene_view_trace_rays_batch_wait(
        scn->s3d_view,
        pool.batch_ctx[pool.buf_prev],
        pool.ray_hits[pool.buf_prev],
        pool.ray_count[pool.buf_prev],
        &stats);
    if (res != RES_OK) goto cleanup;
    
    pool.total_rays_traced += pool.ray_count[pool.buf_prev];
    
    /* 分发最后一轮结果 */
    res = pool_distribute_ray_results(&pool, scn);
    if (res != RES_OK) goto cleanup;
    
    /* ENC + cascade + harvest (与循环内相同) */
    res = pool_collect_enc_locate_requests(&pool);
    if (res != RES_OK) goto cleanup;
    if (pool.enc_locate_count > 0) {
        // ... 同 A3 ...
    }
    
    res = pool_cascade_non_ray_steps_compact(&pool, scn);
    if (res != RES_OK) goto cleanup;
    
    compact_active_paths(&pool);
    res = harvest_completed_paths(&pool, buf);
    if (res != RES_OK) goto cleanup;
    
    pool.pipeline_active = 0;
}
```

### 3.2 Prologue / Epilogue 说明

| 阶段 | 说明 | 特殊处理 |
|------|------|---------|
| **Prologue** | 首轮无前序GPU结果，只做 compact+collect+submit | 不调用 wait/distribute |
| **稳态循环** | 每轮：wait(N-1) + distribute(N-1) + cascade + harvest + compact(N) + collect(N) + submit(N) | GPU trace(N) 与全部CPU工作并行 |
| **Epilogue** | 循环退出时可能还有一轮inflight GPU工作 | 必须 wait + distribute 最后一轮 |

### 3.3 Drain 阶段行为

drain 阶段 (`task_queue` 耗尽后) 特征：
- batch size 逐步缩小
- refill_count = 0
- 流水线仍有效——只要 `ray_count[buf_curr] > 0` 就继续 submit

当 `ray_count[buf_curr] == 0`（所有活跃路径都不需要光线）：
- `pipeline_active = 0`
- 下一轮 skip wait/distribute
- 只做 cascade + harvest 直到所有路径完成

---

## 四、数据依赖分析

### 4.1 依赖关系图

```
                 ┌─────────────────────┐
                 │  trace(N) [GPU]     │ ← submit(N) 启动
                 └────────┬────────────┘
                          │ wait(N)
                          ▼
               ┌──────────────────────┐
               │  distribute(N)       │ ← 读 ray_hits[buf_prev]
               │  [写 slots[].phase]  │    读 ray_to_slot[buf_prev]
               └────────┬─────────────┘
                        │
                        ▼
               ┌──────────────────────┐     ┌──────────────────────┐
               │  enc_locate(N)       │     │                      │
               │  [同步GPU调用]       │     │                      │
               └────────┬─────────────┘     │                      │
                        │                   │                      │
                        ▼                   │                      │
               ┌──────────────────────┐     │                      │
               │  cascade(N)          │     │                      │
               │  [推进 non-ray       │     │                      │
               │   phases in slots]   │     │   trace(N+1) [GPU]   │
               └────────┬─────────────┘     │   (并行执行)          │
                        │                   │                      │
                        ▼                   │                      │
               ┌──────────────────────┐     │                      │
               │  harvest+refill(N)   │     │                      │
               │  [写 estimator_buf,  │     │                      │
               │   重置 slots]        │     │                      │
               └────────┬─────────────┘     │                      │
                        │                   │                      │
                        ▼                   │                      │
               ┌──────────────────────┐     │                      │
               │  compact(N+1)        │     │                      │
               │  [重建索引数组]      │     │                      │
               └────────┬─────────────┘     │                      │
                        │                   │                      │
                        ▼                   │                      │
               ┌──────────────────────┐     │                      │
               │  collect(N+1)        │     │                      │
               │  [填充新 requests]   │     │                      │
               └────────┬─────────────┘     │                      │
                        │ submit(N+1)       │                      │
                        ▼                   └──────────────────────┘
```

### 4.2 安全性论证

| 操作 | 读 | 写 | 与 GPU inflight 冲突? |
|------|------|------|------|
| `wait(N)` | GPU → `h_multi_results` | — | 否 — wait 在 distribute 之前 |
| `distribute(N)` | `ray_hits[buf_prev]`, `ray_to_slot[buf_prev]` | `slots[].phase/fields` | 否 — GPU 操作的是 `buf_curr` 的 GPU buffers |
| `cascade(N)` | `slots[].phase` | `slots[].phase/fields` | 否 — 不涉及 GPU 缓冲 |
| `harvest(N)` | `slots[]` → estimator | `slots[].phase=HARVESTED` | 否 — 不涉及 GPU 缓冲 |
| `refill(N)` | `task_queue` | `slots[].phase=new` | 否 — 新路径在下轮 compact 后才进入 collect |
| `compact(N+1)` | `slots[]` | `active_indices[]` | 否 — 只读 slots |
| `collect(N+1)` | `slots[].ray_req` → `ray_requests[buf_curr]` | `ray_requests[buf_curr]` | 否 — 写入 `buf_curr`，GPU 读的是 `buf_prev` 的 GPU 副本 |
| `submit(N+1)` | `ray_requests[buf_curr]` → GPU buffers | GPU buffers for `buf_curr` | 否 — GPU 上一轮 `buf_prev` 的 kernel 已完成 (wait 回来了) |

**关键安全保证**:
1. `slots[]` 始终单份——同一时刻只有 CPU 的一个操作在修改
2. `ray_requests/ray_hits` 双缓冲——CPU 写 `[buf_curr]`，GPU 结果在 `[buf_prev]`
3. GPU buffers 在 `batch_ctx[buf_curr]` 中——与 `batch_ctx[buf_prev]` 完全隔离
4. `submit(N+1)` 仅在 `wait(N)` 之后才执行——不会有两个 kernel 同时使用同一 batch_ctx

### 4.3 为什么不需要担心 slots 并发?

```
时间线:
  wait(N) → distribute(N)修改slots → cascade(N)修改slots → harvest(N)修改slots
                                                                     ↓
            submit(N+1)启动                ← compact(N+1)只读slots, collect(N+1)只读slots.ray_req

GPU trace(N+1) 只操作 GPU 端的 d_origins/d_directions/d_ranges/d_results
  → 与 CPU 端的 slots[] 内存完全隔离
```

---

## 五、具体代码改造

### 5.1 主循环 Step C 替换

**当前代码** (L1553-1577):

```c
/* Step C: Batch trace via Phase B-1 */
time_current(&t_phase0);
if(pool.ray_count > 0) {
    struct s3d_batch_trace_stats stats;
    memset(&stats, 0, sizeof(stats));
    res = s3d_scene_view_trace_rays_batch_ctx(
        scn->s3d_view, pool.batch_ctx,
        pool.ray_requests, pool.ray_count,
        pool.ray_hits, &stats);
    if(res != RES_OK) goto cleanup;
    pool.total_rays_traced += pool.ray_count;
    // ... stats 累积 ...
}
time_current(&t_phase1);
pool.time_trace_s += time_elapsed_sec(&t_phase0, &t_phase1);
```

**流水线模式**: Step C 拆为 submit (Phase B 末尾) 和 wait (Phase A 开头)，不再有独立的 Step C。

### 5.2 distribute 引用更新

**当前** distribute 读取 `pool->ray_hits` (单缓冲):

```c
struct s3d_hit* hits = pool->ray_hits;
```

**改造后** distribute 读取 `pool->ray_hits[pool->buf_prev]` (Phase 2 已完成引用更新)。

### 5.3 collect 引用更新

**当前** collect 写入 `pool->ray_requests` (单缓冲):

```c
struct s3d_ray_request* requests = pool->ray_requests;
```

**改造后** collect 写入 `pool->ray_requests[pool->buf_curr]` (Phase 2 已完成引用更新)。

### 5.4 Step L 周期日志修改

当前日志引用 `pool.ray_count` 和 `pool.bucket_counts[]`：

```c
(unsigned long)pool.ray_count,
(unsigned long)pool.bucket_counts[RAY_BUCKET_RADIATIVE],
```

流水线模式下需要明确是哪个缓冲的 ray_count：

```c
/* 使用 buf_prev 的数据（刚刚 distribute 完成的那一轮） */
(unsigned long)pool.ray_count[pool.buf_prev],
(unsigned long)pool.bucket_counts[pool.buf_prev][RAY_BUCKET_RADIATIVE],
```

---

## 六、Drain 阶段特殊处理

### 6.1 Drain 阶段的流水线行为

drain 阶段 (`task_next >= task_count`) 时 refill_count = 0，活跃路径逐步减少，batch size 缩小。

**流水线仍有效**: 只要有路径需要光线，submit 仍然异步。当 `ray_count[buf_curr] == 0` 时，`pipeline_active = 0`，下一轮只做 cascade + harvest。

### 6.2 Drain 尾部效率

pool=4096 时 drain 仅占 0.6% (71s)，即使 drain 阶段完全退化为串行，总影响 < 0.3%。

### 6.3 Drain 阶段检测

```c
/* 流水线模式的 drain 检测 — 放在 Phase B 末尾 */
if (!pool.in_drain_phase && pool.task_next >= pool.task_count) {
    pool.in_drain_phase = 1;
    // ... 同原始代码 ...
}
```

---

## 七、compact_active_paths 调用优化

### 当前问题

当前主循环中 `compact_active_paths()` 被调用 **2 次/轮**：
1. Step A (L1541): 为 collect 构建 `need_ray_indices`
2. Step F (L1619): 为 harvest 重建 `done_indices`  

流水线模式中需要考虑 compact 的时机：

### 优化方案

```
Phase A:
  wait → distribute → enc → cascade → compact → harvest → refill

Phase B:
  compact → collect → submit → flip
```

**共 2 次 compact/轮**（与原始相同）。第一次为 harvest 构建 done_indices，第二次为 collect 构建 need_ray_indices。

如果要减少到 1 次，可以合并：

```
Phase A:
  wait → distribute → enc → cascade

Phase B:
  compact (同时构建 done + need_ray) → harvest → refill → collect → submit → flip
```

但这改变了 cascade 和 harvest 的顺序关系——cascade 可能产生新的 PATH_DONE，必须在 harvest 之前被 compact 检测到。所以 **compact-after-cascade 是必须的**。

**结论**: 保持 2 次 compact/轮。

---

## 八、错误处理

### 8.1 GPU 错误

如果 `submit` 返回错误，`pipeline_active = 0`，`goto cleanup`。

如果 `wait` 返回错误，同样 `goto cleanup`。

### 8.2 cleanup 中的 inflight 处理

```c
cleanup:
    /* 如果有 inflight GPU 工作，必须等待完成后才能销毁资源 */
    if (pool.pipeline_active) {
        /* 忽略 wait 的错误 — 只是确保 GPU 完成 */
        s3d_scene_view_trace_rays_batch_wait(
            scn->s3d_view,
            pool.batch_ctx[pool.buf_prev],
            pool.ray_hits[pool.buf_prev],
            pool.ray_count[pool.buf_prev],
            NULL);
        pool.pipeline_active = 0;
    }
    pool_destroy(&pool);
    return res;
```

> **关键**: 如果 `pool_destroy` 在 GPU 仍有 inflight 工作时释放 GPU 缓冲，会导致 `cudaFree` 在活动流上操作——**未定义行为**。必须先 wait。

---

## 九、文件修改清单

| # | 文件 | 行号 | 修改内容 |
|---|------|------|---------|
| 1 | `sdis_solve_persistent_wavefront.c` | L1383-1430 | 新增 `use_pipeline` 变量 + 初始化 |
| 2 | `sdis_solve_persistent_wavefront.c` | L1530-1711 | 主循环：新增流水线分支 (prologue + 稳态 + epilogue) |
| 3 | `sdis_solve_persistent_wavefront.c` | L1749-1752 | cleanup: inflight GPU 等待 |
| 4 | `sdis_solve_persistent_wavefront.c` | L1667-1688 | 日志引用 ray_count/bucket_counts 更新为双缓冲 |
| 5 | `sdis_solve_persistent_wavefront.h` | — | 确保 `pipeline_active` 字段存在 (Phase 2 已添加) |

**预计新增代码**: ~200 行 (流水线分支) + ~30 行 (prologue/epilogue)

### 代码组织建议

为避免主函数过长（已 ~370 行），将流水线主循环提取为独立函数：

```c
static res_T
solve_loop_pipeline(struct wavefront_pool* pool,
                    struct sdis_scene* scn,
                    struct sdis_estimator_buffer* buf,
                    /* ... 其他参数 ... */);

static res_T
solve_loop_serial(struct wavefront_pool* pool,
                  struct sdis_scene* scn,
                  struct sdis_estimator_buffer* buf,
                  /* ... 其他参数 ... */);
```

主函数中：

```c
if (use_pipeline)
    res = solve_loop_pipeline(&pool, scn, buf, ...);
else
    res = solve_loop_serial(&pool, scn, buf, ...);
```

---

## 十、计时语义变化

### 10.1 time_trace_s 的含义变化

| 模式 | `time_trace_s` 含义 |
|------|-------------------|
| 串行 | CPU 等待 GPU 的时间 ≈ GPU 执行时间 |
| 流水线 | `wait()` 的等待时间 — 可能很短（如果 CPU 阶段比 GPU 长）或与串行相同（如果 GPU 是瓶颈） |

### 10.2 新增计时指标

```c
/* Phase 3 流水线诊断 */
size_t pipeline_stalls;        /* wait 时 GPU 已完成的次数 → CPU > GPU 的证据 */
size_t pipeline_waits;         /* wait 时 GPU 未完成需等待的次数 → GPU >= CPU */
double pipeline_wait_time_s;   /* 实际等待 GPU 的累积时间 */
double pipeline_overlap_s;     /* CPU阶段中与GPU重叠的时间 */
```

### 10.3 pipeline_stalls 检测

```c
/* 在 wait 之前检测 GPU 是否已完成 */
cudaError_t query = cudaEventQuery(ctx->evt_download_done);
if (query == cudaSuccess) {
    pool.pipeline_stalls++;    /* GPU 已完成 → CPU 是瓶颈 */
} else {
    pool.pipeline_waits++;     /* GPU 未完成 → 需要等待 */
}
```

> `cudaEventQuery` 是非阻塞的，不影响性能。

### 10.4 流水线诊断输出

```
pipeline_mode: ON  pool_size=4096
  stalls=12345 (CPU>GPU) / waits=67890 (GPU>=CPU)  ratio=15.4%
  avg_wait=0.12ms  total_wait=45.6s  overlap=78.3%
  effective_speedup: 1.87x  (serial_est=2360s, pipeline=1262s)
```

---

## 十一、关键实施注意事项

### 11.1 buf_prev 在 prologue 后

prologue 结束时 `buf_curr=1, buf_prev=0`。循环第一轮 wait 使用 `batch_ctx[0]`（prologue 提交的）。正确。

### 11.2 pipeline_active 与 ray_count 的一致性

```c
/* 安全检查 */
assert(!pool.pipeline_active || pool.ray_count[pool.buf_prev] > 0);
```

`pipeline_active=1` 意味着 `buf_prev` 缓冲有 inflight GPU 工作，`ray_count[buf_prev]` 必须 > 0。

### 11.3 ENC 查询与 GPU trace 的关系

ENC 查询在 distribute 之后、cascade 之前执行。ENC 同步调用使用 `dev->stream`。

**潜在问题**: 如果 ENC 同步调用在 trace(N+1) submit 之后执行，两者使用同一 stream 会冲突。

**解决**: 在流水线布局中，ENC 查询必须在 submit(N+1) **之前**完成。查看 Phase A/B 顺序确认无冲突——ENC 在 Phase A 的 A3 步，submit 在 Phase B 的 B3 步，所以 ENC 先执行。✅

但 ENC 查询也使用 GPU。如果 trace(N) 的 GPU kernel 仍在执行（我们还没 wait），ENC 使用同一 `dev->stream` 会怎样？

**分析**: wait(N) 在 ENC 之前执行（A1 在 A3 之前），所以 trace(N) 已完成，stream 空闲。✅

### 11.4 ENC 查询使用哪个 stream?

ENC batch 使用 `dev->stream`。如果 trace(N+1) 已经在 submit 之后使用 `dev->stream` 运行 kernel...

**但 trace(N+1) 的 submit 在 Phase B（循环末尾），ENC 在 Phase A（循环开头）**。所以执行顺序是：

```
A1: wait(N)      ← GPU trace(N) 完成
A2: distribute   ← CPU
A3: ENC           ← 使用 dev->stream，此时 trace(N+1) 尚未提交 → 安全
A4: cascade       ← CPU
A5: harvest       ← CPU
B1: compact       ← CPU
B2: collect       ← CPU
B3: submit(N+1)   ← GPU trace 异步启动
B4: flip          ← CPU
```

ENC 和 submit 之间有 compact+collect 的 CPU 工作间隔。**安全**。

---

## 十二、实施检查点

```
Step 3.1: 添加 STARDIS_PIPELINE 环境变量解析
  └─ ✅ 编译通过

Step 3.2: 提取 solve_loop_serial() — 将原始主循环移入
  └─ ✅ STARDIS_PIPELINE=0 结果 bit-exact

Step 3.3: 实现 solve_loop_pipeline() — prologue
  └─ ✅ 首轮 submit 成功

Step 3.4: 实现稳态循环
  ├─ wait → distribute → enc → cascade → harvest → compact → collect → submit → flip
  └─ ✅ 小规模测试 (64×64 spp=1) 结果一致

Step 3.5: 实现 epilogue
  └─ ✅ 最后一轮 GPU 结果正确处理

Step 3.6: 实现 cleanup inflight 处理
  └─ ✅ 错误退出时 GPU 正确等待

Step 3.7: 添加流水线诊断计时
  ├─ pipeline_stalls / pipeline_waits 计数
  ├─ 诊断输出
  └─ ✅ 检查点: Phase 3 完成

Step 3.8: 全面验证
  ├─ ctest -C Release
  ├─ STARDIS_PIPELINE=0 vs 1 输出对比 (256×256 spp=4)
  ├─ 320×320 spp=32 完整运行
  └─ ✅ Phase 3 验收
```

---

*Phase 4 详见 → [04_phase4_validation.md](04_phase4_validation.md)*
