# P0: 调度层热字段 SoA 分离 — 开发指南 ✅ 已完成

**创建时间**: 2026-02-27  
**阶段**: SoA 零期工程（原 P1，已实施完毕）  
**状态**: ✅ 已完成  
**目标**: 从 `path_state` AoS 中剥离调度层热字段为独立 SoA 数组，**不改** step_* 函数签名  
**预期收益**: compact/collect 阶段 cache 利用率提升 ~200x；cascade 中每路径上万次迭代的 phase 读写从 4KB stride 降至 4B stride

---

## 0. 关键前提修正

### 导热 cascade 迭代量级

`cascade_advance_single_path()` 对每条路径执行一个 `for(;;)` 无上限循环（[sdis_solve_persistent_wavefront.c#L1680](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1680)），直到路径需要光追、完成、或无法推进。对于热传导路径（Delta-Sphere / WoS），每条路径在此循环中可执行 **上万次迭代**——因为导热随机游走需要大量 non-ray 步骤才能收敛到已知温度或命中边界。

这意味着：
- **每个 wavefront step 中，cascade 是 CPU 端绝对主时间消耗者**
- cascade 循环内部每次迭代都读 `p->phase`（4B）但因 AoS 布局实际加载整条 cache line（64B），stride = `sizeof(path_state)` ≈ 4KB
- 当 OMP 多线程并行时（`schedule(dynamic, 64)`），不同线程访问不同 slot 的 `phase` 字段，L1/L2 cache thrashing 严重
- **P1 SoA 分离对 cascade 性能的影响比预期更大**，因为迭代次数不是 "几次" 而是 "上万次"

### cascade 中访问的 path_state 字段

`cascade_advance_single_path` 内部读写的字段（直接 + 通过 `advance_one_step_no_ray` 间接）：

| 字段 | cascade 直接访问 | cascade 间接通过 step_* 访问 |
|------|-----------------|---------------------------|
| `phase` | R+W（每次迭代） | R+W（step 分派 + 状态转移） |
| `needs_ray` | R（循环出口判断） | W（step 里设置） |
| `active` | R+W（失败时） | W |
| `sfn_stack_depth` | R（picardN 拦截） | R+W |
| `done_reason` | W（失败时） | W |
| `steps_taken` | W（每次推进+1） | - |
| 其他全部字段 | - | 由具体 step_* 函数访问 |

**关键观察**: cascade 的循环体本身只直接读写 `phase`、`needs_ray`、`active`、`sfn_stack_depth`、`done_reason`、`steps_taken` 这 6 个字段。其余字段由被调用的 `advance_one_step_no_ray → step_*` 链路访问，但这些函数接收 `struct path_state* p`，不影响调度层字段的 SoA 分离。

---

## 1. SoA 字段选择

### 1.1 P1 提取的字段（调度层热字段）

以下字段从 `path_state` 中**镜像**到独立 SoA 数组：

| 字段 | 类型 | 大小 | 访问方 | 访问模式 |
|------|------|------|--------|---------|
| `phase` | `enum path_phase` (int) | 4B | compact, collect, cascade, distribute, harvest | 每次循环迭代 R+W |
| `active` | `int` | 4B | compact, cascade, harvest, refill | 循环出口 + 失败 |
| `needs_ray` | `int` | 4B | compact, collect, cascade | 循环出口 + collect 判断 |
| `ray_bucket` | `enum ray_bucket_type` (int) | 4B | compact (分桶), collect (桶写入) | 每次需光追时 |
| `ray_count_ext` | `int` | 4B | collect (6射线 ENC 判断) | 每次需光追时 |

**合计**: 20B / path × 32768 paths = **640 KB**（5 个数组）

### 1.2 P1 暂不提取的字段

| 字段 | 理由 |
|------|------|
| `ray_req` (~60B) | P2 范围，需额外处理 filter_data + 6-ray ENC 路径 |
| `filter_data_storage` (~64B) | collect 中取 `&p->filter_data_storage` 地址赋给 `rr->filter_data`，SoA 化后指针仍有效但需验证 |
| `rwalk`, `T`, `ctx` 等 | P3 范围，需改 step_* 签名 |
| `sfn_stack_depth` | 仅在 cascade/compact 的 picardN 拦截分支读取，频率低，不值得单独提取 |

---

## 2. 数据结构设计

### 2.1 新增 SoA 结构

```c
/* sdis_wf_soa.h — dispatch-layer SoA arrays */
#ifndef SDIS_WF_SOA_H
#define SDIS_WF_SOA_H

#include "sdis_wf_types.h"   /* enum path_phase, enum ray_bucket_type */
#include <stdint.h>

/*******************************************************************************
 * dispatch_soa — SoA mirror of dispatch-hot fields from path_state
 *
 * These arrays are indexed by slot_id [0..pool_size).  Values are kept
 * in sync with the corresponding path_state fields by the dispatch layer.
 *
 * Step functions continue to use struct path_state* and do NOT touch
 * these arrays directly.  Sync happens at well-defined boundaries:
 *   - After cascade:   phase, active, needs_ray  → written back to SoA
 *   - After init:      all 5 fields              → written to SoA
 *   - Before collect:  read from SoA (not path_state)
 *   - Before compact:  read from SoA (not path_state)
 ******************************************************************************/
struct dispatch_soa {
  enum path_phase*        phase;          /* [pool_size] */
  int*                    active;         /* [pool_size] */
  int*                    needs_ray;      /* [pool_size] */
  enum ray_bucket_type*   ray_bucket;     /* [pool_size] */
  int*                    ray_count_ext;  /* [pool_size] */
  size_t                  count;          /* = pool_size */
};

/* Lifecycle */
int  dispatch_soa_alloc(struct dispatch_soa* soa, size_t pool_size);
void dispatch_soa_free (struct dispatch_soa* soa);

/* Single-slot sync: path_state → SoA (after init / after cascade per-path) */
static inline void
dispatch_soa_sync_from_path(struct dispatch_soa* soa,
                            uint32_t idx,
                            const struct path_state* p)
{
  soa->phase[idx]         = p->phase;
  soa->active[idx]        = p->active;
  soa->needs_ray[idx]     = p->needs_ray;
  soa->ray_bucket[idx]    = p->ray_bucket;
  soa->ray_count_ext[idx] = p->ray_count_ext;
}

/* Bulk sync: SoA → path_state (if needed before step_* that reads these) */
static inline void
dispatch_soa_sync_to_path(const struct dispatch_soa* soa,
                          uint32_t idx,
                          struct path_state* p)
{
  p->phase         = soa->phase[idx];
  p->active        = soa->active[idx];
  p->needs_ray     = soa->needs_ray[idx];
  p->ray_bucket    = soa->ray_bucket[idx];
  p->ray_count_ext = soa->ray_count_ext[idx];
}

#endif /* SDIS_WF_SOA_H */
```

### 2.2 整合到 wavefront_pool

```c
/* 在 struct wavefront_pool 中新增 */
struct wavefront_pool {
  /* ... existing ... */
  struct dispatch_soa  dsoa;    /* P1: dispatch-layer SoA mirror */
};
```

---

## 3. 同步策略（核心设计）

P1 采用 **AoS 主、SoA 镜像** 的双写策略。`path_state` 内的原始字段保持不变，`dispatch_soa` 是只读加速视图。所有写操作写 AoS，在特定同步点刷新到 SoA。

### 3.1 同步点定义

```
Wavefront 主循环每轮:

  ┌─ cpu_pre_gpu ──────────────────────────────────────────────┐
  │  compact_active_paths(pool, pv)     ← P1: 从 dsoa 读       │
  │  pool_collect_ray_requests(pool, pv)← P1: 从 dsoa 读       │
  └────────────────────────────────────────────────────────────┘
  ↓
  gpu_launch_async → gpu_wait_and_postprocess
    pool_distribute_ray_results        ← writes path_state 
    (+ enc_locate / cp batches)        ← writes path_state
    ─── SYNC POINT A: distribute 后刷新 dsoa ───
  ↓
  ┌─ cpu_between ──────────────────────────────────────────────┐
  │  cascade_advance (OMP)             ← reads/writes path_state│
  │  ─── SYNC POINT B: cascade 后刷新 dsoa ───                  │
  │  compact_active_paths(pool, pv)    ← P1: 从 dsoa 读        │
  │  harvest_completed_paths           ← reads dsoa.phase/active│
  │  refill_pool → init_single_path   ← writes path_state      │
  │  ─── SYNC POINT C: refill 后刷新 dsoa (新路径) ───          │
  └────────────────────────────────────────────────────────────┘
```

### 3.2 同步点实现

**SYNC POINT A** — distribute 结束后, 遍历涉及的 slot 同步:
```c
/* distribute 结束后 */
for(k = 0; k < pv->need_ray_count; k++) {
  uint32_t idx = pv->need_ray_indices[k];
  dispatch_soa_sync_from_path(&pool->dsoa, idx, &pool->slots[idx]);
}
```

**SYNC POINT B** — cascade 结束后, 遍历活跃 slot 同步:
```c
/* cascade_advance 后 (可合并到 cascade 函数末尾单独 pass) */
for(k = 0; k < pv->active_compact; k++) {
  uint32_t idx = pv->active_indices[k];
  dispatch_soa_sync_from_path(&pool->dsoa, idx, &pool->slots[idx]);
}
```

**SYNC POINT C** — refill 每条新路径后立即同步:
```c
/* init_single_path 结尾 */
dispatch_soa_sync_from_path(&pool->dsoa, slot_idx, p);
```

### 3.3 cascade 内部的特殊处理

cascade 内循环每次迭代直接读写 `p->phase`、`p->needs_ray`、`p->active`。**P1 不改 cascade 内部代码**——cascade 仍操作 AoS `path_state`，只在 cascade 结束后执行 SYNC POINT B 批量同步到 SoA。

理由：
- cascade 对每条路径独立执行上万次迭代，同一路径的 `p->phase` 在 L1 cache 中保持热状态
- cascade 的 cache 问题不在于 stride（单路径连续访问），而在于多线程间不同路径的 cache line 竞争
- **真正受益于 SoA 的是 compact 和 collect**——它们遍历所有路径的同一字段

---

## 4. 修改清单（按文件分类）

### 4.1 新建文件

| 文件 | 描述 |
|------|------|
| `sdis_wf_soa.h` | `struct dispatch_soa` 定义 + inline sync 函数 |
| `sdis_wf_soa.c` | `dispatch_soa_alloc` / `dispatch_soa_free` 实现 |

### 4.2 修改 sdis_solve_persistent_wavefront.h

| 改动 | 行范围 | 描述 |
|------|--------|------|
| 新增 `#include "sdis_wf_soa.h"` | 头部 | |
| `struct wavefront_pool` 新增成员 | ~L154 | `struct dispatch_soa dsoa;` |
| `count_path_rays` 改为从 SoA 读 | ~L326 | 可选：此内联函数目前读 `p->ray_count_ext`，改为读 `dsoa.ray_count_ext[idx]` |

**估计改动**: ~15 行

### 4.3 修改 sdis_solve_persistent_wavefront.c

**4.3.1 pool 生命周期** (~20 行)

| 函数 | 改动 |
|------|------|
| `wavefront_pool_create` (~L329) | 新增 `dispatch_soa_alloc(&pool->dsoa, pool_size)` |
| `wavefront_pool_destroy` (~L454) | 新增 `dispatch_soa_free(&pool->dsoa)` |

**4.3.2 compact_active_paths 重写** (~50 行)

当前实现 ([L765-L816](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L765-L816)):
```c
for(i = base; i < end; i++) {
    struct path_state* p = &pool->slots[i];     // 加载 4KB
    if(p->phase == PATH_DONE || ...)             // 只需要 phase (4B)
    if(!p->active) continue;                     // 只需要 active (4B)
    if(p->needs_ray && p->ray_req.ray_count > 0) // needs_ray (4B)
      if(p->phase == PATH_RAD_TRACE_PENDING)     // phase again
```

P1 改为从 SoA 读:
```c
for(i = base; i < end; i++) {
    enum path_phase ph = pool->dsoa.phase[i];     // 4B, coalesced
    int act = pool->dsoa.active[i];                // 4B, coalesced
    int nr  = pool->dsoa.needs_ray[i];             // 4B, coalesced

    if(ph == PATH_DONE || ph == PATH_ERROR || ph == PATH_HARVESTED) {
      if(ph == PATH_DONE && pool->slots[i].sfn_stack_depth > 0) {
        /* picardN: 需要访问 AoS 修改 phase+active */
        pool->slots[i].phase = PATH_BND_SFN_COMPUTE_Ti_RESUME;
        pool->slots[i].active = 1;
        pool->dsoa.phase[i] = PATH_BND_SFN_COMPUTE_Ti_RESUME;
        pool->dsoa.active[i] = 1;
        ph = PATH_BND_SFN_COMPUTE_Ti_RESUME;
        act = 1;
      } else {
        pv->done_indices[pv->done_count++] = (uint32_t)i;
        continue;
      }
    }
    if(!act) continue;

    pv->active_indices[pv->active_compact++] = (uint32_t)i;

    if(nr) {
      /* 仍需从 AoS 读 ray_req.ray_count 判断，
         但大多数路径 needs_ray=0 不会走到这里 */
      struct path_state* p = &pool->slots[i];
      if(p->ray_req.ray_count > 0) {
        pv->need_ray_indices[pv->need_ray_count++] = (uint32_t)i;
        if(ph == PATH_RAD_TRACE_PENDING)
          pv->bucket_radiative[pv->bucket_radiative_n++] = (uint32_t)i;
        else if(ph == PATH_COUPLED_COND_DS_PENDING
             || ph == PATH_CND_DS_STEP_TRACE)
          pv->bucket_conductive[pv->bucket_conductive_n++] = (uint32_t)i;
      }
    }
}
```

**效果**: 对 `needs_ray==0` 的路径（大多数），完全不加载 `path_state`。三个 SoA 数组总计 12B/path，一条 cache line 覆盖 5 个 path（vs 原来 1 path/cache line）。

**4.3.3 collect Pass 1 从 SoA 读桶 ID** (~20 行)

当前:
```c
struct path_state* p = &pool->slots[i];
int bkt = (int)p->ray_bucket;
```

P1:
```c
int bkt = (int)pool->dsoa.ray_bucket[i];
```

注意：collect Pass 2 仍需从 AoS 读 `ray_req.*` 字段（origin/direction/range），这些在 P2 处理。

**4.3.4 同步点注入** (~30 行)

在 `gpu_wait_and_postprocess` 末尾、`pool_cascade_non_ray_steps_compact` 末尾、`refill_pool` 的每条新路径后，插入 `dispatch_soa_sync_from_path` 调用。

**4.3.5 harvest 从 SoA 读** (~10 行)

```c
/* harvest_completed_paths 中 */
enum path_phase ph = pool->dsoa.phase[idx];
if(ph != PATH_DONE && ph != PATH_ERROR) continue;
```

**估计总改动**: ~150 行

### 4.4 修改 sdis_ray_sort.c

| 函数 | 改动 |
|------|------|
| `update_batch_indices` (~L210) | `p->ray_bucket` 改从 SoA 读（可选，此函数调用频率低） |

**估计改动**: ~10 行

### 4.5 测试文件

P1 **不影响现有测试**——`struct path_state` 结构体定义不变，step_* 函数签名不变。仅需：

| 改动 | 描述 |
|------|------|
| 新增 `test_sdis_dispatch_soa.c` | 验证 alloc/free/sync 正确性 |
| `test_sdis_b4_m2_ray_bucketing.c` | 可选：在现有 pool 分配后验证 dsoa 一致性 |

---

## 5. 实施步骤

### Step 1: 新建 sdis_wf_soa.h / sdis_wf_soa.c

- 定义 `struct dispatch_soa` 和 alloc/free
- 定义 `dispatch_soa_sync_from_path` / `dispatch_soa_sync_to_path` inline 函数
- 添加到 CMakeLists.txt

### Step 2: 整合到 wavefront_pool 生命周期

- `wavefront_pool_create` → `dispatch_soa_alloc`
- `wavefront_pool_destroy` → `dispatch_soa_free`
- `fill_pool` → 初始填充后批量 sync SoA

### Step 3: 改写 compact_active_paths

- 主循环从 `dsoa.phase/active/needs_ray` 读取
- picardN 分支同时写 AoS + SoA
- 需要 ray 时才 fall through 到 AoS

### Step 4: 改写 collect Pass 1 桶计数

- `p->ray_bucket` → `dsoa.ray_bucket[i]`
- `count_path_rays(p)` → 基于 `dsoa.ray_count_ext[i]` 的纯 SoA 版本
- Pass 2 scatter 仍从 AoS 读 ray_req（P2 范围）

### Step 5: 插入同步点

- SYNC A: `gpu_wait_and_postprocess` 末尾
- SYNC B: `pool_cascade_non_ray_steps_compact` 末尾
- SYNC C: `init_single_path` 末尾

### Step 6: 验证

- 运行全量 CTest（CPU 端一致性）
- 用 IR rendering 场景验证端到端结果不变
- 检查 `pool->cascade_total_iterations` 统计确认 cascade 迭代次数未变

---

## 6. 性能分析预期

### 6.1 compact_active_paths

| 度量 | AoS 当前 | SoA P1 |
|------|---------|--------|
| 遍历 path 读取字节 | `32768 × 4KB` = **128 MB** (stride) | `32768 × 12B` = **384 KB** (contiguous) |
| L2 cache lines (128B) | 32768 | 3072 |
| 预期时间减少 | baseline | **~90%+** |

### 6.2 collect Pass 1 桶计数

| 度量 | AoS 当前 | SoA P1 |
|------|---------|--------|
| 每路径读取 | `sizeof(path_state)` 加载 + `ray_bucket` | `4B` (ray_bucket SoA) |
| 预期改善 | baseline | **~10x** |

### 6.3 cascade（不直接受益，间接受益）

cascade 内循环不改，但 cascade 前后的 compact/collect 加速使得 **wavefront 轮次间的 CPU 端开销大幅降低**，从而提升 GPU 利用率（减少 GPU 等待 CPU 完成调度的 stall）。

---

## 7. 风险与缓解

| 风险 | 概率 | 缓解 |
|------|------|------|
| AoS/SoA 双写不一致 | 中 | 开发期增加 `#ifndef NDEBUG` 一致性断言，每次 compact 前验证 `dsoa.phase[i] == p->phase` |
| picardN 分支写 SoA 遗漏 | 低 | compact 中 picardN 拦截路径需同时写 AoS + SoA，用 ASSERT 保护 |
| OMP cascade 与 SoA sync 的竞态 | 低 | cascade 是 OMP parallel for，SYNC B 在 parallel region 外执行，无竞态 |
| filter_data_storage 指针有效性 | 无风险 | P1 不移动 filter_data_storage，指针仍指向 AoS 内部 |

---

## 8. 建议的回归测试

```bash
# 1. 全量单元测试
cd stardis-cus3d/build
ctest -C Release --output-on-failure

# 2. IR rendering 端到端
cd Stardis-Starter-Pack/porous
<exe> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > IR_P1_test.ht
# diff against baseline (binary compare or tolerance check)

# 3. 性能基准（cascade iteration count 不应变化）
# 对比 pool->cascade_total_iterations 改前/改后值
```

---

## 9. 工作量估计

| 项 | 估计行数 | 估计时间 |
|------|---------|---------|
| 新文件 (sdis_wf_soa.h/c) | ~120 行 | 0.5 天 |
| persistent_wavefront.h 修改 | ~15 行 | 0.1 天 |
| persistent_wavefront.c 修改 | ~150 行 | 1.5 天 |
| CMakeLists.txt | ~5 行 | 0.1 天 |
| 测试文件 | ~100 行 | 0.5 天 |
| 调试 + 回归 | - | 1 天 |
| **总计** | **~390 行** | **~3.5 天** |

---

## 附录 A: compact_active_paths 访问的完整字段清单

从 [sdis_solve_persistent_wavefront.c#L765-L816](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L765-L816) 逐行提取：

| 行 | 访问的字段 | R/W | P1 来源 |
|----|-----------|-----|---------|
| L778 | `p->phase` | R | **dsoa** |
| L782 | `p->phase`, `p->sfn_stack_depth` | R | dsoa.phase + **AoS** (sfn 仅此处) |
| L783-784 | `p->phase`, `p->active` | W | **dsoa + AoS** 双写 |
| L790 | `p->active` | R | **dsoa** |
| L796 | `p->needs_ray` | R | **dsoa** |
| L797 | `p->ray_req.ray_count` | R | **AoS** (ray_req 在 P2) |
| L802 | `p->phase` | R | **dsoa** |
| L804-805 | `p->phase` | R | **dsoa** |

## 附录 B: collect Pass 1 访问的字段清单

从 [sdis_solve_persistent_wavefront.c#L993-L1000](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L993-L1000):

| 行 | 访问的字段 | R/W | P1 来源 |
|----|-----------|-----|---------|
| L996 | `p->ray_bucket` | R | **dsoa** |
| L997 | `count_path_rays(p)` → `p->ray_count_ext` | R | **dsoa** |

## 附录 C: cascade_advance_single_path 直接访问字段

从 [sdis_solve_persistent_wavefront.c#L1669-L1738](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1669-L1738):

| 行 | 访问的字段 | R/W | P1 处理 |
|----|-----------|-----|---------|
| L1687 | `p->needs_ray` | R | AoS（cascade 内部不改） |
| L1688 | `p->phase` | R | AoS |
| L1690 | `p->phase`, `p->sfn_stack_depth` | R | AoS |
| L1691 | `p->phase`, `p->active` | W | AoS |
| L1695-1697 | `p->phase` | R | AoS |
| L1709 | `p->phase` | R | AoS（统计用） |
| L1724 | `p->phase`, `p->active`, `p->done_reason` | W | AoS（失败路径） |
| L1729 | `p->steps_taken` | W | AoS |

**cascade 结束后执行 SYNC B 批量刷新到 dsoa。**
