# CPU 管理阶段复杂度分析与优化方案

**创建日期**: 2026-02-19  
**目标文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`  
**头文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h`

---

## 1. 主循环阶段总览

每步主循环执行以下阶段：

```
Step A: compact_active_paths()              ← O(P) 全扫描
Step B: pool_collect_ray_requests_bucketed() ← O(W) 2-pass radix
Step C: GPU trace_rays_batch_ctx()           ← GPU 工作
Step D: pool_distribute_ray_results()        ← O(W) bucketed dispatch
Step D2: enc_locate collect+dispatch+dist    ← O(A)
Step E: pool_cascade_non_ray_steps_compact() ← O(A) OMP 并行
Step F: compact_active_paths()  [第2次!]     ← O(P) 全扫描
Step F: harvest_completed_paths()            ← O(done_count)
Step G: refill_pool()                        ← O(done_count)
Step H: pool_update_active_count()           ← O(P) 全扫描
Step I-L: drain/diagnostics/progress/safety  ← O(1)
```

**O(P) 总调用**: 每步 3 次全扫描（compact ×2 + update_active_count）

用 pool=4096 OMP 基准数据标注：

| 阶段 | 复杂度 | 耗时(s) | 占比 | 每步均摊(μs) |
|------|--------|---------|------|-------------|
| compact ×2 | O(P) ×2 | 5.5 | 3.6% | 44.5 |
| collect | O(W) | 14.3 | 9.4% | 115.6 |
| trace | GPU | 89.6 | 58.7% | 724 |
| distribute | O(W) | 16.8 | 11.0% | 135.8 |
| enc_locate | O(A) | — | <1% | — |
| cascade | O(A) OMP | 11.0 | 7.2% | 88.9 |
| harvest+refill | O(done) | 8.6 | 5.6% | 69.5 |
| update_active | O(P) | (含在其他) | — | — |

---

## 2. 各阶段详细分析

### 2.1 compact_active_paths() — O(P) 全扫描 ×2

```c
static void compact_active_paths(struct wavefront_pool* pool)
{
  size_t i;
  pool->active_compact = pool->need_ray_count = pool->done_count = 0;
  pool->bucket_radiative_n = pool->bucket_conductive_n = 0;

  for(i = 0; i < pool->pool_size; i++) {
    struct path_state* p = &pool->slots[i];
    // ... 分类到 active/need_ray/done/bucket 数组
  }
}
```

**问题**：
1. 每步调用 **两次**：Step A（collect 前）和 Step F（harvest 前）
2. 遍历所有 P 个槽位，包括大量非活跃的 PATH_HARVESTED 槽位
3. 同时构建 5 个索引数组（active, need_ray, done, bucket_rad, bucket_cond）

**第二次调用的原因**：cascade（Step E）可能将路径推进到 PATH_DONE，
必须重新扫描才能发现新完成的路径供 harvest 使用。

### 2.2 pool_update_active_count() — O(P) 全扫描

```c
static void pool_update_active_count(struct wavefront_pool* pool)
{
  size_t i, count = 0;
  for(i = 0; i < pool->pool_size; i++) {
    if(pool->slots[i].active) count++;
  }
  pool->active_count = count;
}
```

**问题**：纯计数操作，完全可以用增量维护替代。

### 2.3 pool_collect_ray_requests_bucketed() — O(W) 2-pass radix

遍历 `need_ray_indices[0..need_ray_count-1]`，执行：
1. **Pass 1**: 按类型统计 bucket_counts，计算 bucket_offsets
2. **Pass 2**: scatter 到 ray_requests[]，填充 ray_to_slot/ray_slot_sub

复杂度 O(W)，W ≈ 2000，合理。但 14.3s 说明常数因子较大（每条路径拷贝射线数据）。

### 2.4 pool_distribute_ray_results() — O(W) bucketed dispatch

按类型分发射线结果：
1. 先处理 radiative bucket（调用 `step_radiative_result`）
2. 再处理 conductive bucket（调用 `step_conductive_ds_result`）
3. 最后处理剩余的 fallback（boundary reinject 等）

复杂度 O(W)，16.8s 主要因为 step 函数本身有 non-trivial 计算。

### 2.5 harvest_completed_paths() — O(done_count)

遍历 `done_indices[0..done_count-1]`，写入 estimator buffer，标记 PATH_HARVESTED。
成本正比于完成路径数，不可避免。

### 2.6 refill_pool() — O(done_count)

遍历 `done_indices`，对每个空槽调用 `init_single_path` + `advance_path_to_first_ray`。
`init_single_path` 涉及 path_state 初始化，`advance_path_to_first_ray` 推进到首次需要射线。
8.6s 中 refill 占大部分（init 成本不可压缩）。

---

## 3. 优化方案（P0-P3 优先级）

### P0: Delta 维护 active_count（~10 行，立即可做）

**目标**: 消除 `pool_update_active_count()` 的 O(P) 全扫描

**方案**:
- `harvest_completed_paths()` 末尾: `pool->active_count -= harvested_count`
- `refill_pool()` 末尾: `pool->active_count += refill_count`
- 删除 Step H 的 `pool_update_active_count()` 调用

**实现**:
```c
// harvest_completed_paths() 中
size_t harvested = 0;
for(k = 0; k < pool->done_count; k++) {
    // ... 原有逻辑 ...
    p->active = 0;
    p->phase = PATH_HARVESTED;
    harvested++;
}
pool->active_count -= harvested;  // NEW

// refill_pool() — 已有 refill_count
pool->active_count += refill_count;  // NEW

// 主循环 — 删除:
// pool_update_active_count(&pool);
```

**预期收益**: 每步省一次 O(P) scan（pool=4096 时约 1.8s，pool=16384 时约 6.6s）

**风险**: 低。delta 计数在 fill_pool() 初始化时校准，后续增量维护。
可保留 `pool_update_active_count()` 作为 debug-only assert 验证。

---

### P1: Cascade 内联 done 收集（消除第二次 compact）

**目标**: 消除 Step F 的第二次 `compact_active_paths()` 调用

**现状**: cascade（Step E）推进路径后可能产生新的 PATH_DONE，必须重新 compact 发现它们。

**方案**: 在 `cascade_advance_single_path()` 中，当路径到达 PATH_DONE/PATH_ERROR 时，
直接追加到线程本地 done 列表。OMP 并行结束后合并。

**实现草案**:
```c
// 在 pool_cascade_non_ray_steps_compact() 中

// 新增：cascade 产出的 done 列表
size_t cascade_done_count = 0;

#pragma omp parallel ...
{
    uint32_t local_done[256];  // 线程本地 done 缓冲
    int local_done_n = 0;
    
    #pragma omp for schedule(dynamic, 64)
    for(k = 0; k < (int)pool->active_compact; k++) {
        uint32_t idx = pool->active_indices[k];
        // ... cascade 推进 ...
        
        if(slot->phase == PATH_DONE || slot->phase == PATH_ERROR) {
            local_done[local_done_n++] = idx;
            if(local_done_n == 256) {
                // flush to shared (需要 critical 或 atomic 索引)
                #pragma omp critical
                {
                    memcpy(&pool->done_indices[pool->done_count + cascade_done_count],
                           local_done, local_done_n * sizeof(uint32_t));
                    cascade_done_count += local_done_n;
                }
                local_done_n = 0;
            }
        }
    }
    // flush remainder
    if(local_done_n > 0) {
        #pragma omp critical
        {
            memcpy(&pool->done_indices[pool->done_count + cascade_done_count],
                   local_done, local_done_n * sizeof(uint32_t));
            cascade_done_count += local_done_n;
        }
    }
}

pool->done_count += cascade_done_count;
```

然后主循环 Step F 中：
```c
// 删除: compact_active_paths(&pool);  // 第二次 compact
// 直接使用 cascade 更新后的 done_indices 进行 harvest
```

**预期收益**: 消除每步一次 O(P) scan（约 2.7s at pool=4096，~9.3s at pool=16384）

**注意**: harvest 只需要 done_indices，不需要第二次 compact 的 active/need_ray 分类
（那些会在下一步的 Step A compact 中重建）。所以此优化逻辑正确。

**风险**: 中等。需要处理 OMP critical section 的正确性和 done_indices 缓冲区边界。
MSVC OpenMP 2.0 不支持 `reduction` 自定义类型，必须用 critical。

---

### P2: 增量式 Compact（free_list + swap-remove）

**目标**: 将 Step A 的 `compact_active_paths()` 从 O(P) 降到 O(变更数)

**现状**: 每步扫描全部 P 个槽位，重建 5 个索引数组。

**方案**: 维护持久化数据结构：
- `free_list[]` (stack): 空闲槽位索引
- `active_set[]` (dense array + swap-remove): 活跃槽位
- 当路径完成 → 从 active_set swap-remove → 推入 free_list
- 当 refill → 从 free_list pop → 追加到 active_set

**增量 compact**: 每步只需遍历 active_set 分类 need_ray 和 buckets。
因为 active_set 只包含活跃路径，其大小是 A 而非 P，当 A << P 时（drain phase）
效果显著。

**预期收益**: compact 从 O(P) 降到 O(A)，在 drain phase 后期 A → 0 时趋近 O(1)

**复杂度**: 高。需要修改 pool 数据结构，增加 free_list 和 active_set，
重写 fill_pool、refill_pool、compact_active_paths 三个函数。

**风险**: 较高。active_set + swap-remove 改变了槽位遍历顺序，
需要验证不影响 RNG 确定性。pool=4096 时效果有限（A ≈ 2000，P=4096，
节省约 50%），主要受益于 drain phase 和大 P 场景。

---

### P3: Pool 解耦（task_pool / ray_pool 独立尺寸）

**目标**: 允许 task_pool_size 和 ray_buffer_size 独立配置

**现状**: `max_rays = pool_size * 6`，射线缓冲区大小与路径池耦合。

```c
// pool_create() 中:
pool->max_rays = pool->pool_size * 6;
// → ray_requests, ray_hits, ray_to_slot, ray_slot_sub 都按 max_rays 分配
```

**分析**: 每步实际提交的射线数 = W × avg_rays_per_path ≈ 2000 × 4 = 8000。
但 pool_size=4096 时 max_rays = 4096×6 = 24576，利用率仅 33%。
pool_size=16384 时 max_rays = 98304，利用率仅 8%。

**方案**: 引入独立参数 `ray_pool_size`:
```c
struct wavefront_pool {
    size_t pool_size;        // 路径槽位数
    size_t ray_pool_size;    // 射线缓冲区大小（独立于 pool_size）
    // ...
};

// pool_create():
pool->max_rays = ray_pool_size;  // 不再 = pool_size * 6
```

`ray_pool_size` 可设为 $W \times 6$ ≈ 12000，无论 task_pool_size 多大。

**依赖**: P0-P2 完成后，pool_size 对应的 CPU 管理成本已降低，
解耦后可以安全增大 pool_size（更多路径在 cascade 中"排队"）
而不担心射线缓冲区浪费。但实际意义取决于 W 是否能提高——
从 benchmark 分析看 W ≈ 2000 是物理常数，增大 pool_size 不会增加 W。

**结论**: P3 的实际收益不大，除非有证据表明 W 可以通过架构改变而增长。
主要价值是减少内存占用（当 pool_size 较大时避免 O(P×6) 的射线缓冲区分配）。

---

## 4. 优先级总结

| 优先级 | 方案 | 复杂度 | 代码量 | 预期收益（pool=4096 OMP） | 风险 |
|-------|------|--------|--------|-------------------------|------|
| **P0** | Delta active_count | 低 | ~10 行 | ~1.8s (≈1.2%) | 极低 |
| **P1** | Cascade 内联 done | 中 | ~40 行 | ~2.7s (≈1.8%) | 中 |
| **P2** | 增量 compact | 高 | ~100 行 | ~2.7s refill + drain 受益 | 较高 |
| **P3** | Pool 解耦 | 中 | ~30 行 | 内存节省为主 | 低 |

### 推荐实施顺序

```
P0 (trivial) → P1 (消除第二次compact) → [benchmark验证] → P2 (按需) → P3 (按需)
```

P0+P1 合计可节约 ~4.5s（3%），并为后续 pool_size 调优打好基础。
更大的收益可能来自 **pool_size 调优**——将 pool_size 从 4096 降至 2048-3072，
直接减少所有 O(P) 阶段的成本，预计可再省 20-30%。

---

## 5. 附加优化建议

### 5.1 pool_size 调优测试矩阵

| pool_size | 预期 W | compact 预期(s) | 总预期改善 |
|-----------|--------|----------------|-----------|
| 1024      | ~1000  | ~1.4           | 可能 trace 退化 |
| 2048      | ~1900  | ~2.8           | ✓ 最优候选 |
| 2560      | ~1950  | ~3.4           | ✓ 最优候选 |
| 3072      | ~1970  | ~4.1           | ✓ 验证边界 |
| 4096      | 1970   | 5.5            | 基准 |

### 5.2 collect/distribute 的 OMP 并行化潜力

- collect (14.3s) 和 distribute (16.8s) 合计 31.1s (20.4%)
- 两者都是 O(W) 遍历，每个条目独立
- 但涉及共享数组写入（ray_requests 按 offset scatter），需要小心竞争
- 潜在加速：2-4x，但需要 bucketed 并行（每个 bucket 一个线程组）
- **评估**: 中等优先级，可在 P0-P1 之后考虑

### 5.3 harvest+refill 的 init_single_path 成本

- harvest+refill 的 8.6s 中，大部分来自 `init_single_path` + `advance_path_to_first_ray`
- 这些是 per-path 初始化，包含 RNG 设置、相空间初始化、首次状态推进
- 不易压缩，但可以用 OMP 并行化（各路径独立）
- **风险**: `advance_path_to_first_ray` 调用 `advance_one_step_no_ray`，需要确认线程安全
