# CPU Phase 优化计划：Distribute / Cascade / Refill

**创建日期**: 2026-03-04  
**基准代码**: `main` @ `f99d20f` (Merge opt/dual-stream: L2+L3+L4)  
**状态**: 待实施  
**核心文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` (4758 行)  
**目标**: 降低 CPU cascade+distribute 的 0.6ms/cycle 瓶颈，将 pool_size 甜蜜点从 8K 推至 32K

---

## 0. 当前 Pipeline 同步关系（main @ f99d20f）

### 0.1 L3 Dual-Stream Camera 主循环时序

一个完整的 A+B 双池 cycle（L3828-L4126）：

```
Phase 1 (view A)                              Phase 2 (view B)
─────────────────                              ─────────────────
1a. gpu_sync_kernel(A)                         2a. gpu_sync_kernel(B)
    ↓ [等 GPU kernel 完成]                        ↓
1b. gpu_start_d2h(A)                           2b. gpu_start_d2h(B)  
    ↓ [启动异步 D2H，立即返回]                     ↓
1c. gpu_launch_async(B)                        2c. gpu_launch_async(A)
    ↓ [H2D(B)+kernel(B) 与 D2H(A) 重叠]          ↓ [H2D(A)+kernel(A) 与 D2H(B) 重叠]
1d. gpu_wait_d2h(A)                            2d. gpu_wait_d2h(B)
    ↓ [等 D2H 完成 + CPU filter/retrace]          ↓
1e. gpu_postprocess(A)                         2e. gpu_postprocess(B)
    ├─ distribute_ray_results ──▶ SYNC A          ├─ distribute_ray_results ──▶ SYNC A
    ├─ collect_enc_locate + GPU batch             ├─ collect_enc_locate + GPU batch
    ├─ distribute_enc_locate_results              ├─ distribute_enc_locate_results
    ├─ collect_cp + GPU batch                     ├─ collect_cp + GPU batch
    ├─ distribute_cp_results                      ├─ distribute_cp_results
    └─ P1 SYNC POINT A                           └─ P1 SYNC POINT A
1f. cpu_between(A)                             2f. cpu_between(B)
    ├─ cascade_non_ray_steps_compact              ├─ cascade_non_ray_steps_compact
    ├─ P1 SYNC POINT B                           ├─ P1 SYNC POINT B
    ├─ compact_active_paths                       ├─ compact_active_paths
    ├─ harvest_completed_paths                    ├─ harvest_completed_paths
    └─ refill_pool                                └─ refill_pool
    cpu_pre_gpu(A)                                cpu_pre_gpu(B)
    ├─ compact_active_paths                       ├─ compact_active_paths
    └─ collect_ray_requests_bucketed              └─ collect_ray_requests_bucketed
```

### 0.2 SYNC POINT 一致性清单

| Sync Point | 位置 | 方向 | 范围 | 触发条件 |
|------------|------|------|------|----------|
| **A** | `gpu_postprocess()` 末尾 (L3131-3143) | AoS→SoA | `need_ray_indices` + `enc_locate_to_slot` + `cp_to_slot` | distribute 完成后 |
| **B** | `cpu_between()` cascade后 (L3298-3302) | AoS→SoA | **全部 `active_indices`** | cascade 完成后 |
| **C** | `refill_pool()` 逐个 (L2671) | AoS→SoA | 新初始化的 slot | 每个 refill 后 |

**关键发现**: Sync Point A 已经是 targeted 同步（只同步修改过的 slot），Sync Point B 是 **全量同步**（遍历全部 active slots），Sync Point C 是逐个 slot 串行同步。

### 0.3 Pool 级共享状态（跨 view A/B 的竞争分析）

双池模式下 A/B 的 slot 范围完全不重叠（`views[0].base=0, view_size=pool_size/2`; `views[1].base=pool_size/2`），但以下 pool 级字段在 Phase 1/2 交替期间**串行访问**（因为 A 的 cpu_between 在 Phase 1 执行，B 的在 Phase 2 执行，不并行）：

| 字段 | 写入位置 | 竞争风险 |
|------|----------|----------|
| `pool->paths_completed/failed` | `harvest_(L2609)`, `cascade(critical)` | 串行，安全 |
| `pool->task_next` | `refill_pool(L2667)` | 串行，安全 |
| `pool->next_path_id` | `refill_pool(L2663)` | 串行，安全 |
| `pool->dsoa.*` | SYNC A/B/C | A/B 操作不同 slot 范围，安全 |
| `pool->time_*_s` | 各阶段计时 | 串行累加，安全 |

**安全结论**: 当前 L3 pipeline 中 A/B 的所有 CPU 阶段是**完全串行执行的**（不存在 A 和 B 的 CPU 代码同时运行），因此所有 pool 级计数器无竞争。

### 0.4 enc_locate / cp Batch 的 GPU 同步

`gpu_postprocess()` 中的 enc_locate 和 cp batch 是**阻塞式 GPU 调用**（L3090-L3122），不使用 async launch。这些调用在 `cpu_between` 之前完成，因此不影响 cascade/harvest/refill 的执行。但它们占用了 GPU 时间——在对方 view 的 GPU kernel 可能仍在运行时。

**注意**: `s3d_scene_view_find_enclosure_batch_ctx` 和 `s3d_scene_view_closest_point_batch_ctx` 使用哪个 CUDA stream 需要验证。如果使用与 trace 不同的 stream，则可与对方 trace 并行；如果共享 stream，则会排队等待。

---

## 1. Distribute 阶段优化

### 1.1 Phase 3 Fallback 消除冗余扫描 [低风险/中收益]

**当前问题** (L2169-2244): Phase 3 OMP 循环遍历**全部** `need_ray_indices`（可能数千条），用 4 个 `continue` 跳过 radiative + conductive + 已处理路径，只处理剩余的 "other" 路径。

```c
/* 当前代码 */
for(kk = 0; kk < (int)pv->need_ray_count; kk++) {  /* 遍历全部 */
  if(p->phase == PATH_RAD_TRACE_PENDING) continue;   /* 跳过 */
  if(p->phase == PATH_COUPLED_COND_DS_PENDING) continue;
  if(p->phase == PATH_CND_DS_STEP_TRACE) continue;
  if(!p->needs_ray) continue;
  /* ... 实际处理 ... */
}
```

**优化**:
1. 在 `compact_active_paths()` (L1220-1280) 中新增 `bucket_other[]` + `bucket_other_n`
2. Phase 3 循环改为 `for(kk = 0; kk < (int)pv->bucket_other_n; kk++)`
3. 串行 fallback 路径同步修改

**影响文件**:
- `sdis_solve_persistent_wavefront.h`: `struct pool_view` 新增 `uint32_t* bucket_other; size_t bucket_other_n;`
- `sdis_solve_persistent_wavefront.c`: `pool_view_init` 分配 + `compact_active_paths` 填充 + distribute Phase 3 改用

**收益评估**: typical "other" 路径比例 5-15%。当 need_ray_count=8000 但 other=400 时，Phase 3 从扫描 8000 降为扫描 400。

### 1.2 Distribute 的 Prefetch [中风险/中收益]

Phase 1/2 的 OMP 循环中，`pool->slots[i]` 与 `pv->ray_hits[batch_idx]` 都是随机访问。插入软件 prefetch：

```c
/* Phase 1 inner loop */
for(kk = 0; kk < (int)pv->bucket_radiative_n; kk++) {
  uint32_t i = pv->bucket_radiative[kk];
  /* prefetch 下4个 slot */
  if(kk + 4 < (int)pv->bucket_radiative_n)
    _mm_prefetch((const char*)&pool->slots[pv->bucket_radiative[kk+4]],
                 _MM_HINT_T0);
  struct path_state* p = &pool->slots[i];
  /* ... */
}
```

**前置**: 需 `#include <immintrin.h>` 或 `#include <xmmintrin.h>`，C89 模式下可能需要编译器扩展开关。

### 1.3 Collect 阶段 critical → atomic [低风险/低收益]

`pool_collect_ray_requests_bucketed()` 的 OMP 末尾（L1783-1790）使用 `#pragma omp critical` 归并 7 个 `pending_rays_*` 计数器。改为各自独立的 `#pragma omp atomic`：

```c
/* 替换 #pragma omp critical { ... } 为: */
#pragma omp atomic
pv->pending_rays_radiative += tl_rays_radiative;
#pragma omp atomic
pv->pending_rays_conductive_ds += tl_rays_cond_ds;
/* ... 其余 5 个同理 ... */
```

---

## 2. Cascade 阶段优化

### 2.1 逐步计时改为编译开关 [低风险/高收益]

**当前问题** (L2384-2401): `cascade_advance_single_path()` 每次迭代调用 2 次 `time_current()`（Windows 上为 `QueryPerformanceCounter`），并维护 `local_phase_count[PATH_PHASE_COUNT]`（~45 ints）和 `local_phase_time[PATH_PHASE_COUNT]`（~45 doubles）数组。

```c
/* 当前代码 — 每次 cascade 迭代 */
time_current(&t_step0);
res = advance_one_step_no_ray(...);
time_current(&t_step1);
local_phase_count[(int)phase_before]++;
local_phase_time[(int)phase_before] += time_elapsed_sec(&t_step0, &t_step1);
```

**优化 — 用 `#ifdef SDIS_CASCADE_PROFILE` 包裹**:

```c
#ifdef SDIS_CASCADE_PROFILE
  time_current(&t_step0);
#endif
  res = advance_one_step_no_ray(...);
#ifdef SDIS_CASCADE_PROFILE
  time_current(&t_step1);
  if((int)phase_before >= 0 && (int)phase_before < PATH_PHASE_COUNT) {
    local_phase_count[(int)phase_before]++;
    local_phase_time[(int)phase_before] += time_elapsed_sec(&t_step0, &t_step1);
  }
#endif
```

**联动清理**:
- `cascade_advance_single_path` 签名：在非 profile 模式下移除 `local_phase_count[]` 和 `local_phase_time[]` 参数
- `pool_cascade_non_ray_steps_compact` 的 OMP critical 归并：
  - **Profile 模式**: 保持现有归并（45 int + 45 double）
  - **Release 模式**: 只归并 4 个标量（`iterations/advances/paths_failed/enc_degenerate_null`），改为 `#pragma omp atomic`

**收益评估**: 消除每 cascade 迭代 2 次 QPC 调用。假设每步 cascade 平均 500K 迭代（32K pool × ~15 iterations/path），每次 QPC ~50ns → 每步节省 ~50ms（从 ~600μs/cycle 的角度，这是一个显著的减少）。

**重要**: `pool->cascade_phase_count/time` 在 `log_drain_phase_report` 中使用。非 profile 模式下这些统计不可用——在日志输出函数中加 guard。

### 2.2 SYNC POINT B Dirty Bitmap [中风险/高收益]

**当前问题** (L3298-3302): cascade 后的 SYNC POINT B 遍历**全部 active_indices**，对每个 slot 执行 `dispatch_soa_sync_from_path()`（5 个 store），即使大多数路径在 cascade 中只 break on needs_ray 而**没有改变 phase/active/needs_ray**。

```c
/* 当前: O(active_compact) 全量同步 */
for(k = 0; k < pv->active_compact; k++) {
  uint32_t idx = pv->active_indices[k];
  dispatch_soa_sync_from_path(&pool->dsoa, idx, &pool->slots[idx]);
}
```

**优化**: 在 cascade 中标记哪些 slot 的 dispatch 字段发生了变化，只同步 dirty slots。

**实现方案**:

1. 在 `struct dispatch_soa` 中新增 `uint64_t* dirty_bitmap;`（每 64 slots 一个 uint64_t）
   - 32K slots → 512 个 uint64_t = 4KB（可忽略的额外内存）

2. 在 `cascade_advance_single_path()` 中，当 path 的 dispatch 字段发生变化时，设置 dirty bit：
   ```c
   /* cascade 结束后（循环外），检查 dispatch 字段是否变化 */
   if(p->phase != dsoa->phase[slot_idx]
   || p->active != dsoa->active[slot_idx]
   || p->needs_ray != dsoa->needs_ray[slot_idx]) {
     /* 使用 atomic OR 设置 dirty bit（线程安全） */
     uint64_t mask = (uint64_t)1 << (slot_idx & 63);
     _InterlockedOr64((volatile long long*)&dsoa->dirty_bitmap[slot_idx >> 6], mask);
   }
   ```

3. SYNC POINT B 改为扫描 dirty_bitmap：
   ```c
   for(w = 0; w < dsoa->count >> 6; w++) {
     uint64_t bits = dsoa->dirty_bitmap[w];
     if(!bits) continue;  /* 跳过全零 word — 大多数 */
     while(bits) {
       int bit = __builtin_ctzll(bits);  /* MSVC: _BitScanForward64 */
       uint32_t idx = (uint32_t)(w * 64 + bit);
       dispatch_soa_sync_from_path(dsoa, idx, &pool->slots[idx]);
       bits &= bits - 1;  /* 清除最低位 */
     }
     dsoa->dirty_bitmap[w] = 0;  /* 重置 */
   }
   ```

**收益评估**: cascade 中只有少数路径改变 dispatch 状态。大部分路径要么 cascade 到 needs_ray（状态不变，因为 needs_ray 被 step 函数设置，但 SoA 中已经是 `needs_ray=0` → cascade 后变为 1），要么 break 在循环第一次检查（path_phase_is_ray_pending → 还是 pending 态）。

等等——需要修正：cascade 的每次 `advance_one_step_no_ray` **都可能修改 phase**（因为这就是状态机转移）。关键问题是"cascade 结束后 phase 是否与 SoA 中的值不同"。答案：cascade 前 SoA 中的 phase 是 distribute 之后的值（已被 SYNC POINT A 同步），cascade 后 phase 很可能已变化（至少经历了一次状态转移）。

**修正后的估算**: 如果所有 active 路径都通过了至少一步 cascade，dirty_bitmap 接近全满。但有一类重要的路径——**ray_pending 路径**——在 cascade 的第一个 `if(path_phase_is_ray_pending(p->phase)) break;` 就直接退出，它们的 dispatch 字段完全不变。这些路径在 SYNC POINT A 中已同步。

在 typical 运行中，cascade loop 处理的是 active 但 **不** needs_ray 的路径。`compact_active_paths` 后：
- `active_indices`= 全部活跃 slot
- `need_ray_indices` = 子集
- cascade 遍历 `active_indices`，但对 `need_ray` 路径直接 break

因此 **dirty 的路径 = active_compact - (那些在 cascade 中 break on 第一个 check 的)**。多数情况下这占 active 的多数——dirty bitmap 的收益可能有限。

**修订结论**: Dirty bitmap 对 SYNC POINT B 的收益**存在但有限**（可能 20-40% 减少）。真正的收益来源于跳过"cascade 中未走任何步的"路径，这只包括 ray_pending 和 enc_locate_pending 路径。

**实施建议**: 作为可选优化，在 cascade_advance_single_path 的 **没有任何 advance 的路径**（即第一次 break 就退出）设置一个 "skipped" flag，SYNC POINT B 遍历中检查并跳过。

### 2.3 OMP Critical → Atomic [低风险/低收益]

当前 cascade 的 OMP 归并（L2513-2524）使用 `#pragma omp critical` 合并：
- 4 个标量计数器
- PATH_PHASE_COUNT (~45) 个 ints（`cascade_phase_count`）
- PATH_PHASE_COUNT (~45) 个 doubles（`cascade_phase_time`）

**Release 模式下**（`SDIS_CASCADE_PROFILE` 未定义）：只需归并 4 个标量，每个改为 `#pragma omp atomic`，消除 critical section。

---

## 3. Refill 阶段优化

### 3.1 harvest OMP 并行化 [中风险/中收益]

**当前问题** (L2575-2636): `harvest_completed_paths()` 是串行 `for` 循环。

**线程安全分析**:
| 操作 | 竞争状态 |
|------|----------|
| `estimator_buffer_grab(buf, px, py)` | 只读索引，安全 |
| `estimator->temperature.sum += T.value` | **同像素 SPP 竞争**（不同路径可能写同一像素） |
| `pool->paths_completed++` | 共享计数器竞争 |
| `pixel_trace_file() + fprintf` | FILE* 竞争 |
| `pool->max_path_depth = max(...)` | 共享 max 竞争 |
| `pool->paths_done_*` | 共享计数器竞争 |
| `p->phase = PATH_HARVESTED` + SoA sync | per-slot 独立写入，安全 |

**并行化方案**:
```c
#pragma omp parallel num_threads(omp_nthreads)
{
  size_t tl_completed = 0, tl_failed = 0;
  size_t tl_done_rad = 0, tl_done_temp = 0, tl_done_bnd = 0;
  size_t tl_max_depth = 0;

  #pragma omp for schedule(static)
  for(kk = 0; kk < (int)pv->done_count; kk++) {
    /* ... accumulate_result 使用 atomic 保护 ... */
    /* ... per-path stats 累加到 thread-local ... */
  }

  #pragma omp atomic
  pool->paths_completed += tl_completed;
  /* ... 其余归并 ... */
}
```

对于 `camera_accumulate_result` 中的同像素竞争，修改 `estimator->temperature.sum += T.value` 为 3 个 double atomic add（MSVC: `_InterlockedCompareExchange64` 模拟 atomic double add，或使用 `#pragma omp atomic` 直接作用于 double）。

**注意**: `pixel_trace_file()` 的 `fprintf` 需要 `#pragma omp critical` 保护（cold path，性能影响可忽略）。

### 3.2 Refill OMP 并行化 [中风险/中收益]

**当前问题** (L2639-2684): `refill_pool()` 是串行循环，包含 `pool->next_path_id++` 和 `pool->task_next++`。

**并行化方案**:
1. 预计算本次 refill 可分配的 path_id 和 task 范围：
   ```c
   size_t refill_avail = min(pv->done_count, pool->task_count - pool->task_next);
   uint32_t base_path_id = pool->next_path_id;
   size_t   base_task = pool->task_next;
   pool->next_path_id += (uint32_t)refill_avail;
   pool->task_next += refill_avail;
   ```
2. 预扫描 `done_indices` 建立 refillable slot 列表（排除 still-active slots）
3. OMP 并行 init_path + advance_path_to_first_ray + dispatch_soa_sync_from_path

**关键依赖**: `advance_path_to_first_ray` 内部调用 `advance_one_step_no_ray`，该函数访问 `pool->sfn_arr[slot_idx]`、`pool->enc_arr[slot_idx]` 等 per-slot 数组——由于每个线程处理不同 slot，无竞争。

但 `advance_path_to_first_ray` 内的 `pool->paths_failed++` 需改为 atomic。

### 3.3 消除 memset(0) [低风险/低收益]

检查 `camera_init_path` 和 `probe_init_path` 中是否有 `memset(p, 0, sizeof(*p))` 对整个 2KB 的 path_state 清零。如果有，替换为只清零必需字段：

```c
/* 替代 memset(p, 0, sizeof(*p)) */
p->phase = PATH_INIT;
p->active = 1;
p->needs_ray = 0;
p->steps_taken = 0;
p->done_reason = 0;
p->ray_bucket = RAY_BUCKET_NONE;
p->ray_count_ext = 0;
p->ray_req.ray_count = 0;
/* rwalk, ctx, T 由 init 逻辑显式赋值 */
```

---

## 4. 结构性优化（中期）

### 4.1 path_state 热/冷 SoA 拆分

**这是 P1 cold-block extraction 的延续。** 当前 `path_state` ~2040B/slot，cascade 工作集 = active_count × 2040B。

32K pool × 2040B = **63.8 MB** >> L3 (36 MB)

**拆分方案**（三层）:

| 层级 | 内容 | 大小/slot | 访问者 |
|------|------|-----------|--------|
| **Hot** | phase, active, needs_ray, rwalk, ctx, T, rng_state, steps_taken, done_reason | ~370B | cascade (必须), distribute, refill |
| **Warm** | ray_req, filter_data_storage, ray_bucket, ray_count_ext, rad_direction, rad_bounce/retry | ~250B | distribute, collect |
| **Cold** | locals union, ds_* scratch, bnd_* scratch, identity fields, ipix_image, coupled_* | ~1420B | 特定 step_* 函数 |

32K × 370B = **11.6 MB** < L3 → cascade 完全命中 L3  
32K × (370+250)B = **19.4 MB** < L3 → distribute+collect 也命中

**实施路径**: 
- P3 阶段工程，需修改 ~45 个 step_* 函数签名
- 参考 `optimization/solver_soa/P2_domain_decomposition_dev_guide.md`

### 4.2 Compact 去重

当前每步在 `cpu_between` 和 `cpu_pre_gpu` 中各调用一次 `compact_active_paths`：

```
cpu_between():
  cascade → SYNC B → compact → harvest → refill
cpu_pre_gpu():
  compact → collect
```

两次 compact 处理相同的 slot 范围，但中间 harvest+refill 可能改变 done/active 状态。如果将 harvest+refill 的 SoA 同步做到位（SYNC C 已经做了），第二次 compact 可以省略——但前提是 harvest 把 PATH_HARVESTED 正确同步到 SoA。

**当前情况**: `harvest_completed_paths` L2627-2629 已经做了 `dsoa.active[i]=0; dsoa.phase[i]=PATH_HARVESTED;`。`refill_pool` 的 SYNC C (L2671) 也同步了新路径。因此 `cpu_pre_gpu` 的 compact **理论上可以复用 cpu_between 的结果 + 增量更新**（只处理 harvest/refill 修改的 slot）。

**但这增加了复杂性，收益有限**（一次 compact = 遍历 view_size × 20B SoA ≈ 16K × 20B = 320KB，~microseconds 级别）。**建议暂不实施**。

---

## 5. 实施优先级排序

| # | 优化项 | 风险 | 收益 | 工期 | 依赖 |
|---|--------|------|------|------|------|
| **O1** | Cascade 计时编译开关 (2.1) | 🟢低 | ⭐⭐⭐ 高 | 2h | 无 |
| **O2** | Phase 3 bucket_other (1.1) | 🟢低 | ⭐⭐ 中 | 2h | 无 |
| **O3** | Collect/Cascade atomic 替换 (1.3, 2.3) | 🟢低 | ⭐ 低 | 1h | O1 |
| **O4** | Harvest OMP 并行化 (3.1) | 🟡中 | ⭐⭐ 中 | 4h | 无 |
| **O5** | Refill OMP 并行化 (3.2) | 🟡中 | ⭐⭐ 中 | 4h | O4 |
| **O6** | SYNC B dirty bitmap / skip (2.2) | 🟡中 | ⭐ 低~中 | 3h | O1 |
| **O7** | Distribute prefetch (1.2) | 🟡中 | ⭐ 低~中 | 2h | 无 |
| **O8** | Refill memset 消除 (3.3) | 🟢低 | ⭐ 低 | 1h | 无 |
| **O9** | path_state 热/冷 SoA 拆分 (4.1) | 🔴高 | ~~⭐⭐⭐⭐ 极高~~ 实测不达预期 | 40h+ | O1-O8 | **❌ 结题搁置** |

**建议实施顺序**: O1 → O2 → O3 → O8 → O4 → O5 → O7 → O6 ~~→ O9~~

> **O9 结题说明 (2026-03-05)**: 经 stardis-cus3d-o9 worktree 完整实现与实测，O9 域分解的净收益仅 -17.6s (5.6%)，远低于预期的 -87~-106s (28-34%)。sync_a+sync_b 消除确实节省 46s，但多数组 SoA 布局导致：(1) refill 8×散射 memset 回退 +15.7s；(2) compact 480B-stride 退化 +3.8s；(3) collect 双数组随机访问 +4.9s。更严重的是 pool=32768 时 cascade 出现 2.87× 超线性劣化（5 个独立数组 TLB/prefetch 失效），pool_size 响应比不可接受。该方向成本过高，暂停。

---

## 6. 测试与验证

### 6.1 正确性验证

```bash
# CTest 回归
cd stardis-cus3d/build
cmake --build . --config Release > build.log 2>&1
ctest -C Release --output-on-failure

# IR 渲染一致性
cd Stardis-Starter-Pack-0.2.0/porous
<executable> -M porous.txt -t 4 -V 3 \
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > "IR_rendering_320x320x32.ht"
```

### 6.2 性能验证

- 各阶段耗时对比（`pool->time_cascade_s` / `time_distribute_s` / `time_harvest_s`）
- pool_size=8192 / 16384 / 32768 下的总时间对比
- L3 cache miss rate（VTune / perf stat）

### 6.3 逐步集成规则

- 每个优化项独立 commit + CTest 通过
- O1-O3 可作为一个 PR 合并
- O4-O5 (harvest/refill 并行化) 需要额外的并发正确性审查
- O9 (SoA 拆分) 需要独立长周期开发分支

---

## 7. 与已有优化文档的关系

| 文档 | 状态 | 本文依赖 |
|------|------|---------|
| `solver_soa/P0_dispatch_soa_dev_guide.md` | ✅ 已完成 | P0 是本文的基础 |
| `solver_soa/P1_cold_block_extraction_dev_guide.md` | ✅ 已完成 | P1 cold blocks 已实施 |
| `solver_soa/P2_domain_decomposition_dev_guide.md` | ❌ 搁置 (O9实测不达预期) | O9 是 P2 的前置 |
| `cpu_wait_for_gpu_issue/L4_gpu_inline_filter.md` | ✅ 已完成 | L4 确认 GPU fully hidden |
| `wavefront_pipeline_optimization_plan.md` | ✅ L2+L3 已完成 | 双缓冲流水线已合并 |

---

*文档创建: 2026-03-04 | 基于 main @ f99d20f*
