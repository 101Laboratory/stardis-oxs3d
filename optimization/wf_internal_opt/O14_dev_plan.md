# O14: per-thread 固定分区 — 开发计划

**创建日期**: 2026-03-18  
**基于**: O14_per_thread_partition_comprehensive_report.md  
**目标分支**: `stardis-oxs3d-o14`（基于 `stardis-oxs3d-merge-phase`）  
**工作目录**: `stardis-oxs3d-o14/stardis-solver/0.16.2/src/`  
**状态**: ❌ 结题关闭 — 实测全面劣化  

---

## 0. 设计约束

- **data layout 不变**：`path_state` 2040B AoS，`hot_arr` 8B SoA，冷块 SoA (`sfn_arr`/`enc_arr`/`ext_arr`) 均不动
- **step_* 函数零改动**：全部 `step_*` / `advance_one_step_*` 函数签名和实现不修改
- **dual-buffer 兼容**：所有分区计算以 `pv->base` 为偏移起点，同时支持 view_A/view_B

---

## 1. 核心数据结构变更

### 1.1 新增：per-thread done list

```c
/* wavefront_pool 中新增字段（sdis_solve_persistent_wavefront.h） */
struct wavefront_pool {
  /* ... 现有字段 ... */

  /* O14: per-thread done list，避免 harvest 阶段的原子竞争 */
  uint32_t** tl_done_indices;   /* tl_done_indices[tid][0..tl_done_count[tid]) */
  size_t*    tl_done_count;     /* 每线程 done slot 数量 */
  size_t     tl_done_capacity;  /* 每线程 done list 容量 = partition_size */

  /* O14: 分区参数（运行时计算，不涉及内存分配） */
  size_t     o14_partition_size;  /* = pool_size / omp_nthreads，向下取整 */
};
```

`pool_create()` 中分配：
```c
pool->tl_done_indices = (uint32_t**)calloc(nthreads, sizeof(uint32_t*));
pool->tl_done_count   = (size_t*)calloc(nthreads, sizeof(size_t));
for(int t = 0; t < nthreads; t++) {
  pool->tl_done_indices[t] = (uint32_t*)malloc(partition_size * sizeof(uint32_t));
}
pool->o14_partition_size = pool_size / nthreads;  /* 余数 slot 归入最后一个线程 */
```

`pool_destroy()` 中释放：
```c
for(int t = 0; t < nthreads; t++) free(pool->tl_done_indices[t]);
free(pool->tl_done_indices);
free(pool->tl_done_count);
```

### 1.2 移除：merged_pass 中 active_indices 的使用

merged_pass 的 inner loop 从 `for(ph = 0; ph < n; ph++) { slot = pv->active_indices[ph]; }` 改为直扫分区。`active_indices[]` 数组本身保留（compact 仍用于其他目的，如 enc_locate/cp batch dispatch），但 merged_pass 主循环不再读它。

---

## 2. 逐函数改动清单

### 2.1 `merged_pass()` — 核心改动

**文件**: `sdis_solve_persistent_wavefront.c`  
**改动范围**: OMP parallel region（current ~lines 3933-4133）

**改动前（概要）**：
```c
#pragma omp for schedule(dynamic, 64)
for(ph = 0; ph < (int)n; ph++) {
  uint32_t slot = pv->active_indices[ph];
  struct path_state* p = &pool->slots[slot];
  struct path_hot*  hot = &pool->hot_arr[slot];
  if(!hot->active) continue;
  /* prefetch ph+4 via active_indices */
  /* Phase A/B/C/D */
  /* harvest: atomic _InterlockedExchangeAdd64 on pv->done_count */
}
```

**改动后**：
```c
int tid = omp_get_thread_num();
size_t p_begin = pv->base + (size_t)tid * pool->o14_partition_size;
size_t p_end   = p_begin + pool->o14_partition_size;
/* 最后一个线程承接余数 */
if(tid == omp_get_num_threads() - 1)
  p_end = pv->base + pv->view_size;

pool->tl_done_count[tid] = 0;  /* 重置 per-thread done list */

for(size_t slot = p_begin; slot < p_end; slot++) {
  struct path_hot* hot = &pool->hot_arr[slot];
  if(!hot->active) continue;

  struct path_state* p = &pool->slots[slot];

  /* O7: prefetch slot+4（连续，比 active_indices 间接预取更准确）*/
  if(slot + 4 < p_end) {
    PREFETCH_T0(&pool->slots[slot + 4]);
    PREFETCH_T0(&pool->hot_arr[slot + 4]);
  }

  /* Phase A: distribute（不变） */
  /* Phase B: cascade（不变） */
  /* Phase C: collect（不变，tl_ray_bufs 机制不变） */

  /* Phase D: harvest — 写 tl_done 而非全局 done_count */
  if(!tl_fatal && (hot->phase == PATH_DONE || ...) && !hot->active) {
    if(hot->phase == PATH_DONE && pool->sfn_arr[slot].depth > 0) {
      /* M8 resumption（不变） */
    } else {
      pool->ops->accumulate_result(p, pool->result_ctx);
      hot->active = 0;
      hot->phase  = (uint8_t)PATH_HARVESTED;
      /* O14: 写入 per-thread done list，不再用 atomic */
      pool->tl_done_indices[tid][pool->tl_done_count[tid]++] = (uint32_t)slot;
    }
  }
}
/* tl_ray_buf flush（不变，在 parallel region 出口） */
```

**关键点**：
- 移除 `schedule(dynamic, 64)`，换成 `parallel` + 手动分区（`omp for` 整体被移除，改为每 thread 独立 for 循环）
- harvest 写 `tl_done_indices[tid]` 而不是 `pv->done_indices[atomic_idx]`
- `n = pv->active_compact` 不再需要（分区直扫替代了 compact 输出）

### 2.2 `compact_active_paths()` — 分区化

**文件**: `sdis_solve_persistent_wavefront.c`  
**改动范围**: full function rewrite

**改动前**：串行扫 `base..base+view_size` → 写 `pv->active_indices[]` 和 `pv->done_indices[]`

**改动后**：
```c
static void
compact_active_paths(struct wavefront_pool* pool, struct pool_view* pv)
{
  /* O14: compact 现在只用于 enc_locate/cp batch dispatch
   * 不再需要 active_count 给 merged_pass（merged_pass 直扫分区）
   * 但仍需 active_compact 供 enc/cp flush 判断，故保留 compact
   * done_indices 合并自 tl_done（由 merged_pass Phase D 填写）
   */

  /* 合并各 thread 的 done list 到 pv->done_indices */
  size_t total_done = 0;
  int t;
  for(t = 0; t < pool->omp_nthreads; t++) {
    size_t cnt = pool->tl_done_count[t];
    memcpy(&pv->done_indices[total_done],
           pool->tl_done_indices[t],
           cnt * sizeof(uint32_t));
    total_done += cnt;
  }
  pv->done_count = total_done;

  /* active_compact 仍需更新（供 enc/cp 路径判断用）
   * O14: 串行扫 hot_arr（80KB，L2 friendly，成本低）构造 active_compact count
   * 不再构造 active_indices 数组内容（merged_pass 不读它）*/
  size_t active = 0;
  size_t base = pv->base, end = base + pv->view_size;
  for(size_t i = base; i < end; i++) {
    enum path_phase ph = (enum path_phase)pool->hot_arr[i].phase;
    if(ph == PATH_DONE || ph == PATH_ERROR || ph == PATH_HARVESTED) continue;
    if(!pool->hot_arr[i].active) continue;
    active++;
    /* M8 resumption（保留逻辑不变）*/
  }
  pv->active_compact = active;
}
```

> **注**：`active_indices[]` 数组的实际填充可暂时保留（供 enc_locate/cp batch dispatch 用），但 merged_pass 主循环不再读取。若 enc/cp dispatch 也改为分区直扫，可后续移除。

### 2.3 `refill_pool()` — per-thread 分区内 refill

**文件**: `sdis_solve_persistent_wavefront.c`  
**改动范围**: Phase 1+3

**改动后（Phase 1）**：  
直接使用 `pv->done_indices[]`（已由 compact 合并自各 thread 的 `tl_done`），不变。

**改动后（Phase 3）**：  
`#pragma omp for schedule(static)` 不变——refill 成本均匀，static 合适。但注意：refill 分配的 slot 仍属于其 "所在分区" 的线程，下一步 merged_pass 对该 slot 的访问由对应 thread 负责，无需额外协调。

> refill 改动量极小：Phase 1 (compact done list) 和 Phase 3 (parallel init) 均不依赖 active_indices，不受 O14 影响。

### 2.4 pool_view 和 pool_create 适配

```c
/* pool_create 末尾：计算并存储分区参数 */
pool->omp_nthreads       = (int)nthreads;
pool->o14_partition_size = pool_size / (size_t)nthreads;
```

dual-buffer 时 `pv->base` 已区分 view_A 和 view_B，分区计算 `p_begin = pv->base + tid * P` 自动正确。

---

## 3. 实施步骤

### Step 1: 数据结构扩展（0.5 天）

- [ ] `sdis_solve_persistent_wavefront.h`：`wavefront_pool` 新增 `tl_done_indices`、`tl_done_count`、`tl_done_capacity`、`o14_partition_size`
- [ ] `pool_create()`：分配 `tl_done_indices[]` 数组（每线程 `partition_size` 容量）
- [ ] `pool_destroy()`：释放 `tl_done_indices[]`
- [ ] 编译验证（仅结构体改动，无逻辑变化，expect 0 errors）

### Step 2: merged_pass 内层循环重写（1 天）

- [ ] 找到 OMP parallel region 起点（~line 3933），backup 当前 `#pragma omp for schedule(dynamic,64)`
- [ ] 替换为 per-thread 分区直扫逻辑（见 2.1）
- [ ] Phase D harvest 改写 `tl_done_indices[tid]`（移除 `_InterlockedExchangeAdd64` on `pv->done_count`）
- [ ] O7 prefetch 从 `active_indices[ph+4]` 改为 `slot+4`
- [ ] serial fallback 路径（n < 64 分支）：保留原串行逻辑，或统一为 tid=0 单线程分区
- [ ] 编译 + porous 320×320×8 快速冒烟测试（预期零数值差异）

### Step 3: compact_active_paths 改写（0.5 天）

- [ ] 改为合并 `tl_done[tid]` → `pv->done_indices[]`（见 2.2）
- [ ] 保留 `active_compact` 的 hot_arr 串行计数（80KB scan，L2 friendly）
- [ ] 验证：`done_count` == 上一步 harvest 的路径数
- [ ] 编译 + 冒烟测试

### Step 4: 验证与调优（1 天）

- [ ] **pool_size sweep**：pool=8K / 16K / 32K / 64K 各一轮 porous 320×320×32
- [ ] 核验：mp 时间随 pool_size 增长应接近线性（target < 1.15× at 64K vs 16K）
- [ ] 核验：`cascade_total_iterations` 全池守恒（四个 pool_size 应给出相同总迭代量）
- [ ] **逐像素一致性**：pool=16K O14 结果 vs 原始 main，容差 1e-6
- [ ] 负载均衡监控：各线程 harvest count 的 CV，期望 < 10%

### Step 5: 可选优化（按需，0.5 天）

- [ ] 初始 `fill_pool` stride 分配（消除 warm-up 期路径类型相关性）
- [ ] `STARDIS_O14_PARTITION_SIZE` 环境变量支持（覆盖默认 `pool_size/nthreads`）
- [ ] pool_size 余数处理：目前归最后线程，考虑轮转分配更均匀

---

## 4. 风险与缓解

| # | 风险 | 影响 | 缓解 |
|---|------|------|------|
| 1 | **harvest done list 容量溢出** — 单步单分区 done 数 > `partition_size` | 低：turnover 0.03%，P=1024 时最大 done ≈ 3/step | 静态断言 + runtime check in debug build |
| 2 | **负载不均超预期** — BND_SF 复杂路径聚集在某分区 | 中低：初期相关性 ~20 步后消散 | warm-up 期监控，若持续 > 15% CV 则启用 stride 分配 |
| 3 | **enc_locate/cp dispatch 读 active_indices** — 若 enc/cp batch 仍走 active_indices 路径 | 中：active_indices 不再由 merged_pass 填写，内容过时 | compact 中保留 active_indices 填写逻辑（先不移除），明确标注 TODO |
| 4 | **dual-buffer view_B 分区计算偏移** — `pv->base` 在 view_B 时有偏移 | 中：已在 step 2 中通过 `p_begin = pv->base + tid×P` 覆盖 | 专项测试 view_B 路径 |
| 5 | **serial fallback 分支遗漏** — n < 64 时仍走老逻辑 | 低：n < 64 = pool 几乎耗尽，罕见 | 保留 serial fallback 原逻辑不动，O14 改动仅在 OMP 分支 |

---

## 5. 验收标准

| 标准 | 通过条件 |
|------|---------|
| 功能正确性 | porous 320×320×32 pool=16K，逐像素差 < 1e-6 vs main |
| 总迭代量守恒 | `cascade_total_iterations` 在 pool=16K/32K/64K 三组相同 (±0.1%) |
| Scaling 线性化 | mp(pool=64K) / mp(pool=16K) < 1.20 |
| 负载均衡 | 32 线程 harvest count CV < 10%（稳态步） |
| 墙钟提升 | pool=32K wall time < pool=16K+10%（即 < 131s） |

---

*计划创建: 2026-03-18 | 目标: `stardis-oxs3d-o14` worktree*
