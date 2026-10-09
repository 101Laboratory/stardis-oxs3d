# 射线空间排序设计方案 — 降低 L2 Crossbar 压力

**创建日期**: 2026-02-20
**目标**: 通过基于射线 origin + direction 的混合 Morton-code 空间排序，改善 GPU warp 内射线的 BVH 子树重合度，降低 L2 crossbar 压力并减少线程分歧
**先决条件**: Experiment 4 ncu 测试已完成
**影响范围**: `sdis_solve_persistent_wavefront.c` (求解器) + 新增 `sdis_ray_sort.c/h`

---

## 1. 问题回顾

| 瓶颈 | 实测值 | 来源 |
|------|-------|------|
| L2 Crossbar 利用率 | **78%** SOL Memory | ncu SpeedOfLight |
| 主 Warp 停顿原因 | **Long Scoreboard (L1TEX op)** | ncu WarpStateStatistics |
| Est. Speedup (消除此停顿) | **~39.56%** | ncu |
| Avg. Active Threads / Warp | **6.45 / 32 (20%)** | ncu SchedulerStatistics |
| Thread Divergence Est. Speedup | **~7%** | ncu |

**根因**: 同一 warp 内的 32 条射线 origin 散布在场景不同区域，方向完全随机。BVH 遍历时每条射线进入完全不同的子树 → ~80% 线程被 predicate-off → L2 请求无法合并。

## 2. 方案概述

### 2.1 核心思路

在 `pool_collect_ray_requests_bucketed()` 完成桶分类后，**忽略桶边界，对全部 `ray_requests[0..ray_count-1]` 按 origin Morton code + direction octant 全局排序**，使空间邻近且方向一致的射线落入同一 warp（相邻 32 个线程）。

桶排序（按射线类型分类）仅服务于 CPU 端结果筛选，对 GPU kernel 性能无帮助。下游分发通过 `batch_idx` 直接索引 `ray_hits[]`，不依赖桶边界（已代码验证，详见 §5.3）。

### 2.2 算法选择: 30-bit Origin+Octant 混合 Morton Code + Radix Sort

| 维度 | 选择 | 理由 |
|------|------|------|
| **空间编码** | 30-bit 混合 Morton code (uint32_t) | 27-bit origin (9 bits/axis, 512³) + 3-bit direction octant |
| **排序算法** | CPU 8-bit radix sort (4 pass) | $O(N)$ 复杂度；对 ~128K 射线 < 0.1ms；与 origin-only 方案开销完全相同 |
| **主排序键** | origin Morton code (MSB 27 bits) | origin 决定射线进入 BVH 的初始子树路径；9 bits/axis → 512³ 精度 |
| **次排序键** | direction octant (LSB 3 bits) | 3 bits 完全编码 BVH 每一层的 front-to-back child 顺序，已足够 |
| **不用卦限内细分** | 边际收益低 | 3-bit octant 已捕获 front-to-back 决策的全部信息；卦限内方向差异不影响 traversalStack 顺序 |

### 2.3 方向联合排序的收益分析

#### 2.3.1 为什么 direction 对 BVH 遍历很重要

BVH 遍历有两个关键决策点：

1. **进入哪棵子树** — 由射线 origin 与节点 AABB 的相交测试决定 → **origin** 主导
2. **先访问哪个子节点** — cuBQL `shrinkingRayQuery` 使用 front-to-back ordering，根据射线方向与分裂平面的关系决定 → **direction** 主导

同一 warp 内，两条射线若 origin 相同但方向相反：
- 在 BVH **顶层**：相交同一节点 ✓（origin coherence）
- 在 BVH **中层**：front-to-back 顺序翻转 → traversalStack 状态分歧 → warp 分裂
- 每层约 50% 概率分歧 → 遍历 $D$ 层后，仅 $2^{-D}$ 的线程仍活跃

量化分析：BVH 深度 ~20 层，纯 origin 排序在前 ~5 层保持 coherence，之后因 direction 分歧丢失。若加入 direction 排序，可额外保持 ~3–5 层 coherence。

#### 2.3.2 按射线类型的方向分布特征

| 射线类型 | 典型方向分布 | direction 排序收益 |
|---------|-------------|------------------|
| **SHADOW** | 指向光源，高度一致 | **极高** — 同一表面上的 shadow 射线方向几乎相同，octant 排序将它们完美聚合 |
| **RADIATIVE** | 余弦加权半球采样，法线主导 | **高** — 同一表面法线确定 1 个半球，octant 覆盖 1–2 个卦限 |
| **STEP_PAIR** | 配对方向，有限随机 | **中** — 部分规律性可被 octant 捕获 |
| **ENCLOSURE** | 6 个固定方向（±X, ±Y, ±Z）| **极高** — 6 个方向恰好对应 6 个 octant，octant 排序完美分组 |
| **STARTUP** | 场景依赖，通常半球 | **中–高** |

#### 2.3.3 错误认知纠正："蒙特卡洛方向本质随机"

这个论断**不完全正确**。蒙特卡洛求解器中：
- 方向是 **条件随机**：给定表面法线 $\hat{n}$，方向采样限制在法线半球 $\{\omega : \omega \cdot \hat{n} > 0\}$
- 同一网格面片上的所有射线共享相同法线 → **相邻 origin 的射线方向天然集中在同一半球**
- 这意味着 origin 排序已隐式地部分聚合了方向；显式 direction 排序进一步收紧

反例：SHADOW 射线方向完全确定性（指向光源），ENCLOSURE 射线方向完全确定性（6 个轴方向）。这些射线占总光线量的相当比例。

### 2.4 "联合排序复杂度过高" 的反驳

原文断言联合排序复杂度过高。以下逐条反驳：

| 关于复杂度的担忧 | 实际情况 |
|----------------|----------|
| 6D Morton code 需要 60 bits (uint64_t) | ❌ 不需要等精度。**Origin 27 bits + Octant 3 bits = 30 bits**，uint32_t 即可 |
| uint64_t radix sort 需要 8 pass | ❌ 30-bit 混合编码只需 4 pass，**与 origin-only 完全相同** |
| Direction 编码复杂（球面→笛卡尔） | ❌ **3-bit octant** = `sign(dx) << 2 | sign(dy) << 1 | sign(dz)`，3 条整数指令 |
| 精度分配困难 | ❌ 层级天然分离：origin 27 bits 捕获顶层，octant 3 bits 捕获中层 |
| 排序开销增加 | ❌ **零额外开销** — octant 编码仅 3 条整数指令/射线（<< 0.001ms） |
| 卦限内需要更细分方向 | ❌ 3-bit octant 已完全编码 front-to-back 决策；卦限内方向差异不影响 traversalStack 顺序 |

**结论**: 混合编码 **零额外排序成本**，仅增加 3 条整数指令的 octant 编码开销，且将节省的 5 bits 用于提升 origin 精度（8→9 bits/axis）。

### 2.5 32-bit 混合 Morton Code 位布局

```
 31          5  4  3  2    0
┌───────────┬──────┬─────┐
│ origin    │ oct  │ pad │
│  27 bits  │3 bits│2 b  │
└───────────┴──────┴─────┘
MSB ────────────────── LSB
```

- **Bits [31..5]**: Origin 27-bit Morton code（9 bits/axis，512³ = 134M cells）
  - porous 场景尺度 ~0.1m → 空间精度 ~0.2 mm，绰绰有余
  - 比 8 bits/axis 的 256³ 方案精度提高 8×
- **Bits [4..2]**: Direction 八象限编码（`sign(dx)<<2 | sign(dy)<<1 | sign(dz)`）
  - 8 个象限，完全编码 BVH 每一层的 front-to-back child 顺序
  - 卦限内方向差异不影响 traversalStack 顺序，无需细分
- **Bits [1..0]**: 保留（置 0）

#### 层级排序语义

Radix sort 按 LSB → MSB 排序，最终结果的排列顺序由 MSB 主导：

1. **首先** 按 origin 空间位置分组（MSB 27 bits）→ 同一空间 cell 的射线相邻
2. **其次** 在同一 cell 内按 direction 八象限分组（3 bits）→ 同 front-to-back 顺序的射线相邻

这正好对应 BVH 遍历的决策层级：顶层由 origin 决定（进入哪棵子树），中层由 direction octant 决定（先访问哪个 child）。

> **为什么 3-bit octant 就够了**: 卦限内方向的差异仅影响射线与子节点 AABB 的相交判断（是否需要访问两个子节点），但不影响访问顺序。而相交判断主要由 origin 决定。因此 octant 已捕获了 direction 对 BVH coherence 的几乎全部贡献。

### 2.6 预期收益（更新）

| 指标 | 当前值 | origin-only 预期 | origin+octant 预期 | 增量收益 |
|------|--------|------------------|----------------------|----------|
| Avg. Active Threads | 6.45/32 | ~10–16/32 | **~12–20/32** | +2–4 threads |
| $\bar{C}_{coal}$ (L2 合并因子) | ~1.3 | ~3–6 | **~4–8** | +30%–50% |
| kernel 时间 (稳态) | ~0.65 ms | ~0.25–0.45 ms | **~0.20–0.35 ms** | ~15%–25% |
| 额外排序开销 | — | ~0.11 ms | **~0.11 ms** | **无增量** |

Octant 排序的增量收益估算：
- 3-bit octant 完全消除 BVH 中层的 front-to-back 分歧 → traversalStack 顺序一致 → Active Threads 提升 ~20%–30%
- 这是 origin-only 排序上的 **lock-on 改善**，边际成本仅为 3 条整数指令/射线

| 指标 | 当前值 | 预期优化后 | 改善 |
|------|--------|-----------|------|
| Avg. Active Threads | 6.45/32 | ~10–16/32 | **1.5×–2.5×** |
| $\bar{C}_{coal}$ (L2 合并因子) | ~1.3 | ~3–6 | **2×–4×** |
| kernel 时间 (稳态) | ~0.65 ms | ~0.25–0.45 ms | **1.5×–2.5×** |
| 端到端 GPU 阶段 | 基线 | ~75%–85% 基线 | **~20%** |

## 3. 数据流分析

### 3.1 当前流程 (Main Loop Step B)

```
compact_active_paths()
    ↓
pool_collect_ray_requests_bucketed()     ← 按 ray_bucket_type 桶分类
    ↓                                       输出: ray_requests[0..ray_count-1]
    │                                              ray_to_slot[ray_idx] → slot_id
    │                                              ray_slot_sub[ray_idx] → 0/1/2..5
    │                                              path_state.ray_req.batch_idx = ray_idx
    │                                              path_state.ray_req.batch_idx2 = ray_idx
    │                                              path_state.enc_query.batch_indices[j]
    ↓
s3d_scene_view_trace_rays_batch_ctx()    ← GPU kernel, 1 thread/ray
    ↓                                       输入: ray_requests[0..ray_count-1]
    │                                       输出: ray_hits[0..ray_count-1]
    ↓
pool_distribute_ray_results()            ← 通过 batch_idx 索引 ray_hits[]
```

### 3.2 排序插入点

排序应在 `pool_collect_ray_requests_bucketed()` **之后**、`s3d_scene_view_trace_rays_batch_ctx()` **之前**插入。

关键约束：**排序重排 `ray_requests[]` 后，所有 `batch_idx` 映射必须同步更新**，否则 `pool_distribute_ray_results()` 将通过过时的 `batch_idx` 读取错误的命中结果。

### 3.3 受影响的索引映射

排序会改变射线在 `ray_requests[]` 中的位置，以下映射必须更新：

| 映射 | 方向 | 存储位置 | 更新方式 |
|------|------|---------|---------|
| `ray_to_slot[ray_idx]` | ray → slot | `pool->ray_to_slot[]` | 按排序顺序重排 |
| `ray_slot_sub[ray_idx]` | ray → sub-ray | `pool->ray_slot_sub[]` | 按排序顺序重排 |
| `batch_idx` | slot → ray | `path_state.ray_req.batch_idx` | **反向查找更新** |
| `batch_idx2` | slot → ray | `path_state.ray_req.batch_idx2` | **反向查找更新** |
| `enc_query.batch_indices[0..5]` | slot → ray | `path_state.enc_query.batch_indices[]` | **反向查找更新** |

## 4. 详细设计

### 4.1 新增模块: `sdis_ray_sort.h / sdis_ray_sort.c`

放置于 `stardis-cus3d/stardis-solver/0.16.2/src/` 目录。

#### 4.1.1 头文件接口

```c
/* sdis_ray_sort.h — Morton-code spatial ray sorting for L2 pressure reduction
 *
 * Sorts ray_requests[] by origin spatial proximity (Morton code) within
 * each ray-type bucket, so that adjacent GPU threads (same warp) tend to
 * traverse similar BVH sub-trees.
 */
#ifndef SDIS_RAY_SORT_H
#define SDIS_RAY_SORT_H

#include <stdint.h>
#include <stddef.h>

struct wavefront_pool;  /* forward declaration */

/* Sort ray_requests[0..ray_count-1] by origin Morton code.
 * Updates all batch_idx / ray_to_slot / ray_slot_sub mappings.
 *
 * scene_lower[3], scene_upper[3]: scene AABB for coordinate normalization.
 *
 * scratch: caller-provided scratch buffer, size >= ray_count * sizeof(uint32_t).
 *          If NULL, function allocates internally (slower).
 *
 * Returns RES_OK on success. */
res_T
pool_sort_rays_by_morton(struct wavefront_pool* pool,
                         const float scene_lower[3],
                         const float scene_upper[3],
                         uint32_t* scratch);

#endif /* SDIS_RAY_SORT_H */
```

#### 4.1.2 Morton Code 计算（Origin 27-bit + Direction Octant 3-bit）

```c
/* 9-bit integer expand to 27-bit Morton code via magic-number bit interleave.
 * x_8..x_0 → interleaved 27-bit code. */
static INLINE uint32_t
expand_bits_9(uint32_t v)
{
  v &= 0x1FFu;                                     /* mask to 9 bits */
  v = (v | (v << 16)) & 0x010000FFu;               /* 0000000_ABCDEFGHI -> 0000000A_00000000_BCDEFGHI */
  v = (v | (v <<  8)) & 0x0100F00Fu;               /* -> 0000000A_0000BCDE_0000FGHI */
  v = (v | (v <<  4)) & 0x010C30C3u;               /* -> 0000000A_00BC00DE_00FG00HI */
  v = (v | (v <<  2)) & 0x09249249u;               /* -> A00B00C00D00E00F00G00H00I */
  return v;
}

/* Compute 27-bit origin Morton code from 3 normalized [0,1] floats.
 * 9 bits/axis → 512³ cells. */
static INLINE uint32_t
morton_origin_27(float nx, float ny, float nz)
{
  uint32_t ix = (uint32_t)fminf(fmaxf(nx * 512.0f, 0.0f), 511.0f);
  uint32_t iy = (uint32_t)fminf(fmaxf(ny * 512.0f, 0.0f), 511.0f);
  uint32_t iz = (uint32_t)fminf(fmaxf(nz * 512.0f, 0.0f), 511.0f);
  return (expand_bits_9(ix) << 2)
       | (expand_bits_9(iy) << 1)
       |  expand_bits_9(iz);
}

/* Encode direction into 3-bit octant.
 * sign(dx) << 2 | sign(dy) << 1 | sign(dz)
 *
 * This completely determines front-to-back child visit order at every
 * BVH internal node. Rays in the same octant share identical
 * traversalStack ordering → zero warp divergence from direction. */
static INLINE uint32_t
encode_direction_octant(float dx, float dy, float dz)
{
  uint32_t oct = 0u;
  oct |= (dx >= 0.0f) ? 4u : 0u;
  oct |= (dy >= 0.0f) ? 2u : 0u;
  oct |= (dz >= 0.0f) ? 1u : 0u;
  return oct;  /* 3 bits [2..0] */
}

/* Compute full 32-bit hybrid sort key:
 * [31..5] = 27-bit origin Morton code  (9 bits/axis, 512³)
 * [4..2]  = 3-bit direction octant
 * [1..0]  = 0 (reserved) */
static INLINE uint32_t
ray_sort_key_32(float nx, float ny, float nz,
                float dx, float dy, float dz)
{
  uint32_t origin_key = morton_origin_27(nx, ny, nz);
  uint32_t oct_key    = encode_direction_octant(dx, dy, dz);
  return (origin_key << 5) | (oct_key << 2);
}
```

#### 4.1.3 排序实现

```c
/* Per-ray sort key: hybrid Morton code (sorting key) + original index (payload) */
struct ray_sort_entry {
  uint32_t key;       /* 32-bit: origin Morton(27b) | octant(3b) | pad(2b) */
  uint32_t orig_idx;  /* original index in ray_requests[] */
};

res_T
pool_sort_rays_by_morton(struct wavefront_pool* pool,
                         const float scene_lower[3],
                         const float scene_upper[3],
                         uint32_t* scratch)
{
  const size_t N = pool->ray_count;
  size_t i;
  float inv_extent[3];
  struct ray_sort_entry* entries = NULL;
  struct ray_sort_entry* temp   = NULL;

  if(N <= 32) return RES_OK;  /* warp-size or less: sorting is pointless */

  /* ---- 1. Compute scene extent inverse for normalization ---- */
  for(i = 0; i < 3; i++) {
    float ext = scene_upper[i] - scene_lower[i];
    inv_extent[i] = (ext > 1e-20f) ? 1.0f / ext : 0.0f;
  }

  /* ---- 2. Allocate sort entries ---- */
  entries = (struct ray_sort_entry*)malloc(N * sizeof(*entries));
  temp    = (struct ray_sort_entry*)malloc(N * sizeof(*temp));
  if(!entries || !temp) { free(entries); free(temp); return RES_MEM_ERR; }

  /* ---- 3. Compute hybrid sort keys (origin Morton + direction encoding) ---- */
  for(i = 0; i < N; i++) {
    const struct s3d_ray_request* rr = &pool->ray_requests[i];
    float nx = (rr->origin[0] - scene_lower[0]) * inv_extent[0];
    float ny = (rr->origin[1] - scene_lower[1]) * inv_extent[1];
    float nz = (rr->origin[2] - scene_lower[2]) * inv_extent[2];
    entries[i].key      = ray_sort_key_32(nx, ny, nz,
                                          rr->direction[0],
                                          rr->direction[1],
                                          rr->direction[2]);
    entries[i].orig_idx = (uint32_t)i;
  }

  /* ---- 4. Radix sort by hybrid key (4-pass, 8-bit radix) ---- */
  radix_sort_keys(entries, temp, N);

  /* ---- 5. Apply permutation to ray_requests[], ray_to_slot[], ray_slot_sub[] ---- */
  apply_ray_permutation(pool, entries, N);

  /* ---- 6. Update batch_idx back-references in path_state ---- */
  update_batch_indices(pool, N);

  free(entries);
  free(temp);
  return RES_OK;
}
```

#### 4.1.4 Radix Sort (8-bit, 4-pass, LSB)

```c
/* 4-pass LSB radix sort on 32-bit hybrid keys.
 * Uses ray_sort_entry { key, orig_idx } pairs.
 * In-place via ping-pong between entries[] and temp[]. */
static void
radix_sort_keys(struct ray_sort_entry* entries,
                struct ray_sort_entry* temp,
                size_t N)
{
  size_t pass;
  for(pass = 0; pass < 4; pass++) {
    int shift = (int)(pass * 8);
    size_t counts[256];
    size_t offsets[256];
    size_t k;

    memset(counts, 0, sizeof(counts));

    /* Count */
    for(k = 0; k < N; k++) {
      uint8_t digit = (uint8_t)((entries[k].key >> shift) & 0xFF);
      counts[digit]++;
    }

    /* Prefix sum */
    offsets[0] = 0;
    for(k = 1; k < 256; k++)
      offsets[k] = offsets[k - 1] + counts[k - 1];

    /* Scatter */
    for(k = 0; k < N; k++) {
      uint8_t digit = (uint8_t)((entries[k].key >> shift) & 0xFF);
      temp[offsets[digit]++] = entries[k];
    }

    /* Swap pointers (copy back for next pass) */
    memcpy(entries, temp, N * sizeof(*entries));
  }
}
```

#### 4.1.5 重排与映射更新

```c
/* Apply sorted permutation to ray arrays.
 * entries[i].orig_idx = the original ray index that should now be at position i. */
static void
apply_ray_permutation(struct wavefront_pool* pool,
                      const struct ray_sort_entry* entries,
                      size_t N)
{
  size_t i;

  /* Allocate temporary copies */
  struct s3d_ray_request* tmp_rr = (struct s3d_ray_request*)
                                   malloc(N * sizeof(*tmp_rr));
  uint32_t* tmp_r2s = (uint32_t*)malloc(N * sizeof(*tmp_r2s));
  uint32_t* tmp_sub = (uint32_t*)malloc(N * sizeof(*tmp_sub));

  /* Permute into temporaries */
  for(i = 0; i < N; i++) {
    uint32_t src = entries[i].orig_idx;
    tmp_rr[i]  = pool->ray_requests[src];
    tmp_r2s[i] = pool->ray_to_slot[src];
    tmp_sub[i] = pool->ray_slot_sub[src];
  }

  /* Copy back */
  memcpy(pool->ray_requests, tmp_rr,  N * sizeof(*tmp_rr));
  memcpy(pool->ray_to_slot,  tmp_r2s, N * sizeof(*tmp_r2s));
  memcpy(pool->ray_slot_sub, tmp_sub, N * sizeof(*tmp_sub));

  free(tmp_rr);
  free(tmp_sub);
  free(tmp_r2s);
}

/* Update batch_idx references stored in path_state.
 * After permutation, ray_to_slot[new_idx] = slot_id, ray_slot_sub[new_idx] = sub.
 * We need: path_state.ray_req.batch_idx = new_idx (for sub==0)
 *          path_state.ray_req.batch_idx2 = new_idx (for sub==1)
 *          path_state.enc_query.batch_indices[sub] = new_idx (for sub>=0, enc) */
static void
update_batch_indices(struct wavefront_pool* pool, size_t N)
{
  size_t i;
  for(i = 0; i < N; i++) {
    uint32_t slot_id = pool->ray_to_slot[i];
    uint32_t sub     = pool->ray_slot_sub[i];
    struct path_state* p = &pool->slots[slot_id];

    if(sub == 0) {
      p->ray_req.batch_idx = (uint32_t)i;
    } else if(sub == 1) {
      p->ray_req.batch_idx2 = (uint32_t)i;
    }

    /* Enclosure query: batch_indices[0..5] map sub 0..5 */
    if(p->phase == PATH_ENC_QUERY_EMIT && p->ray_count_ext == 6) {
      p->enc_query.batch_indices[sub] = (uint32_t)i;
    }
  }
}
```

### 4.2 集成到主循环

修改 `sdis_solve_persistent_wavefront.c` 的 Step B 与 Step C 之间：

```c
    /* Step B: Collect ray requests — bucketed (B-4 M2) */
    time_current(&t_phase0);
    pool.ray_count = 0;
    res = pool_collect_ray_requests_bucketed(&pool);
    if(res != RES_OK) goto cleanup;
    time_current(&t_phase1);
    pool.time_collect_s += time_elapsed_sec(&t_phase0, &t_phase1);

    /* Step B2: Spatial ray sort (Morton code) — NEW */
    time_current(&t_phase0);
    if(pool.ray_count > 32) {
      res = pool_sort_rays_by_morton(&pool, scene_lower, scene_upper, NULL);
      if(res != RES_OK) goto cleanup;
    }
    time_current(&t_phase1);
    pool.time_sort_s += time_elapsed_sec(&t_phase0, &t_phase1);

    /* Step C: Batch trace via Phase B-1 */
    ...
```

### 4.3 Scene AABB 获取

在循环开始前一次性获取：

```c
  float scene_lower[3], scene_upper[3];
  s3d_scene_view_get_aabb(scn->s3d_view, scene_lower, scene_upper);
```

场景 AABB 在 `s3d_scene_view_c.h` 中已有缓存字段 (`lower[3]`, `upper[3]`)，`s3d_scene_view_get_aabb()` 直接返回。

### 4.4 性能开销预算

| 操作 | 复杂度 | 估算耗时 (N=128K) | 说明 |
|------|-------|-------------------|------|
| Origin Morton 计算 | O(N) | ~0.02 ms | 3 × float→int + bit interleave (9-bit expand) |
| Direction octant | O(N) | ~0.001 ms | 3 × sign bit → 3 条整数指令/射线 |
| Radix sort (4-pass) | O(4N) | ~0.05 ms | 4 × 128K scatter + count (与 origin-only 完全相同) |
| 重排 ray_requests[] | O(N) | ~0.03 ms | memcpy 128K × 40B (s3d_ray_request 大小) |
| 更新 batch_idx | O(N) | ~0.01 ms | 128K 个 slot 索引写入 |
| **总计** | | **~0.111 ms** | 远小于单次 kernel 耗时 0.65 ms |

Octant 编码仅增加 ~0.001 ms（3 条整数指令/射线），**总开销与 origin-only 方案毫无差别**。

排序开销占 kernel 时间的 ~17%。若 kernel 加速 2×（降至 ~0.33ms），排序开销占比上升至 ~34%——仍显著正向。

## 5. 正确性保证

### 5.1 关键不变量

1. **ray_requests[i] 与 ray_hits[i] 一一对应** — GPU kernel 的 `tid = i` 处理 `ray_requests[i]`，结果写入 `ray_hits[i]`。排序改变了射线顺序，但 kernel 不关心原始顺序，仅需 input/output 数组位置匹配
2. **batch_idx 正确指向 ray_hits[]** — `distribute_ray_results()` 通过 `p->ray_req.batch_idx` 索引 `ray_hits[]`。`update_batch_indices()` 确保排序后引用更新
3. **ray_to_slot 正确反向映射** — `apply_ray_permutation()` 同步重排
4. **桶边界无需保持** — 全局排序破坏 `bucket_offsets[]`，但下游分发不依赖它（见 §5.3 分析）

### 5.2 验证策略

| 测试 | 方法 | 说明 |
|------|------|------|
| **排序前后射线集合相同** | 对比排序前后 ray_requests 的 origin/direction 集合（无序比较） | 确认无射线丢失或重复 |
| **batch_idx 一致性** | 排序后遍历所有 need_ray 路径，验证 `ray_requests[batch_idx].user_id == slot_id` | 确认映射正确 |
| **渲染结果 bit-exact** | 排序前后 IR 渲染输出应完全相同（蒙特卡洛相同种子） | 确认对求解无影响 |
| **enc_query 6-ray 完整性** | 排序后验证 enc_query.batch_indices[0..5] 均指向正确射线 | 6-ray 查询映射 |

### 5.3 桶排序与空间排序的关系（已代码验证）

**结论：全局排序，无需保持桶边界。**

桶排序（`pool_collect_ray_requests_bucketed`）的存在意义是为 **CPU 端结果筛选**服务，与 GPU kernel 性能无关。代码验证如下：

#### 下游的结果分发机制

`pool_distribute_ray_results()` 的三个分发阶段均**不依赖 `bucket_offsets[]`**：

| 分发阶段 | 索引来源 | 填充位置 | 与 `bucket_offsets` 的关系 |
|---------|---------|---------|---------------------|
| Phase 1: Radiative | `bucket_radiative[k]` → slot_id | `compact_active_paths()` 按 `p->phase == PATH_RAD_TRACE_PENDING` 分类 | ✘ 无关 |
| Phase 2: Conductive | `bucket_conductive[k]` → slot_id | `compact_active_paths()` 按 `p->phase == PATH_COUPLED_COND_DS_PENDING` 分类 | ✘ 无关 |
| Phase 3: Fallback | `need_ray_indices[k]` → slot_id | `compact_active_paths()` 按 `p->needs_ray` 分类 | ✘ 无关 |

关键观察：

1. **`bucket_radiative[]` 和 `bucket_conductive[]` 是 slot 索引数组**，在 `compact_active_paths()` 中按 path phase 分类填充，与 ray_requests 的桶边界无关
2. **结果检索通过 `batch_idx`**：`pool->ray_hits[p->ray_req.batch_idx]` — 直接索引，不需要知道射线在哪个桶
3. **`bucket_offsets[]` 仅在 `pool_collect_ray_requests_bucketed()` 内部使用**，用于计算各桶起始偏移以实现 radix scatter，赋值后不再被读取（仅测试代码中作为断言检查）

#### 为什么全局排序比桶内排序更优

| 维度 | 全局排序 | 桶内排序 |
|------|---------|----------|
| **实现复杂度** | 一次 radix sort | 5 次独立 radix sort |
| **跨桶 warp coherence** | ✓ 同一位置的不同类型射线相邻 | ✘ 不同桶的射线不会相邻 |
| **GPU BVH 顶层收益** | 最大化 — 同一 origin 的 radiative + shadow 射线在同一 warp | 仅桶内射线受益 |
| **小桶浪费** | 无 | 小桶（如 startup < 32 条）排序无价值但仍有开销 |
| **对下游的影响** | 无 — 分发不依赖桶边界 | 无 |

全局排序的额外优势：同一表面点产生的 radiative 射线和 shadow 射线 origin 几乎相同，全局排序后它们会落入同一 warp，BVH 顶层遍历路径完全相同。桶内排序无法实现这一点。

## 6. 风险与缓解

| 风险 | 严重度 | 缓解措施 |
|------|-------|---------|
| **排序开销抵消收益** | 低 | 0.1ms 排序 vs 0.65ms kernel；设置 `N <= 32` 跳过阈值 |
| **batch_idx 映射错误** | 高 | 单元测试 + bit-exact 渲染对比 |
| **内存分配失败** | 低 | `entries` / `temp` 可预分配到 pool 结构体（避免逐帧 malloc） |
| **排序后 L2 无改善** | 中 | 可能原因: BVH 顶层已被 L2 缓存 → 中层才是瓶颈 → origin 排序仅改善部分层级。缓解: 补全 ncu MemoryWorkloadAnalysis 对比 |
| **Morton code 精度不足** | 极低 | 9 bits/axis = 512³ cells；porous 场景尺度 ~0.1m，精度 ~0.2mm，绰绰有余 |

## 7. 预分配优化（避免逐帧 malloc）

可在 `pool_create()` 时预分配排序缓冲区，避免每步循环的 malloc/free：

```c
/* 新增到 struct wavefront_pool */
struct ray_sort_entry* sort_entries;  /* [max_rays] */
struct ray_sort_entry* sort_temp;     /* [max_rays] */
struct s3d_ray_request* sort_rr_tmp;  /* [max_rays] 用于重排 */
uint32_t* sort_r2s_tmp;              /* [max_rays] */
uint32_t* sort_sub_tmp;              /* [max_rays] */
float scene_lower[3], scene_upper[3]; /* 缓存场景 AABB */
double time_sort_s;                   /* 排序累计时间统计 */
```

预分配大小 = `max_rays = pool_size × 6`（与 `ray_requests` 等分配一致）。

增量内存开销: `max_rays × (8 + 8 + 40 + 4 + 4) = max_rays × 64 bytes`。
对 pool_size=32768: `32768 × 6 × 64 = 12.6 MB`。可接受。

## 8. 诊断输出

在求解器结束时输出排序统计：

```
  [wavefront] sort: total=%.3fs  avg=%.3fms/step  sorted_rays=%lu
```

并在 `STARDIS_LOG_V >= 5` 时输出每步的 Morton code 分布直方图（前 8 位 = 256 桶）。

## 9. 环境变量开关

```c
/* STARDIS_RAY_SORT: 0 = disabled (baseline), 1 = enabled (default) */
int ray_sort_enabled = 1;
const char* env = getenv("STARDIS_RAY_SORT");
if(env && atoi(env) == 0) ray_sort_enabled = 0;
```

方便 A/B 对比测试。

## 10. 实施步骤

| 步骤 | 文件 | 说明 | 预计工时 |
|------|------|------|---------|
| **S1** | 新建 `sdis_ray_sort.h` | 头文件接口定义 | 15 min |
| **S2** | 新建 `sdis_ray_sort.c` | Morton code + radix sort + 重排 + batch_idx 更新 | 2 hr |
| **S3** | 修改 `sdis_solve_persistent_wavefront.h` | 新增 pool 预分配字段、time_sort_s | 15 min |
| **S4** | 修改 `sdis_solve_persistent_wavefront.c` | pool_create 预分配 + 主循环 Step B2 插入 + 诊断输出 | 1 hr |
| **S5** | 修改 `CMakeLists.txt` | 添加 `sdis_ray_sort.c` 到编译目标 | 5 min |
| **S6** | 新建 `test_sdis_ray_sort.c` | 单元测试: 排序正确性, batch_idx 一致性, 排列置换 | 2 hr |
| **S7** | 集成测试 | IR 渲染 bit-exact 对比: `STARDIS_RAY_SORT=0` vs `=1` | 30 min |
| **S8** | 性能验证 | ncu profile 排序前后对比: L2 crossbar, Active Threads, kernel 时间 | 1 hr |
| **总计** | | | **~7 hr** |

## 11. 验证计划

### 11.1 正确性验证

```powershell
cd Stardis-Starter-Pack\porous

# Baseline (排序关闭)
$env:STARDIS_RAY_SORT = "0"
stardis.exe -M porous.txt -t 4 -V 3 `
  -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > baseline.ht 2> baseline.log

# With sorting
$env:STARDIS_RAY_SORT = "1"
stardis.exe -M porous.txt -t 4 -V 3 `
  -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > sorted.ht 2> sorted.log

# 结果必须 bit-exact
fc.exe /B baseline.ht sorted.ht
```

### 11.2 性能验证

```powershell
# ncu profile — 排序后
ncu --set full --kernel-name "trace_rays_topk_kernel" `
    --launch-count 100 --launch-skip 50 `
    -o kernelprofile_sorted `
    stardis.exe -M porous.txt -t 4 -V 3 `
    -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0
```

关注指标:
- `lts__xbar2lts_cycles_active` (L2 crossbar): 目标 < 60% (当前 78%)
- `smsp__average_warps_issue_stalled_long_scoreboard`: 目标明显减少
- `smsp__thread_inst_executed_per_inst_executed`: (Active Threads/Warp) 目标 > 10
- SOL Memory %: 目标 < 60%
- kernel Duration: 目标 < 0.45 ms

## Appendix A: 编码方案对比

| 维度 | 方案 1: Origin-only 30b | 方案 2: Origin 27b + Octant 3b (推荐) | 方案 3: Full 64b |
|------|---------------------|--------------------------------------|--------------------|
| **sort key 类型** | uint32_t | uint32_t | uint64_t |
| **origin 精度** | 10 bits/axis (1024³) | 9 bits/axis (512³) | 10 bits/axis (1024³) |
| **direction 精度** | 无 | 3-bit octant (8 方向 bin) | 10 bits×2 球面坐标 (1M 方向 bin) |
| **radix sort passes** | 4 | 4 | 8 |
| **排序缓冲区大小** | N × 8B | N × 8B | N × 12B |
| **排序耗时** | ~0.11 ms | ~0.111 ms | ~0.22 ms |
| **BVH 顶层 coherence** | ★★★★★ | ★★★★★ | ★★★★★ |
| **BVH 中层 coherence** | ★☆☆☆☆ | ★★★★★ | ★★★★★ |
| **实现复杂度** | 极低 | **极低** | 中 |
| **总综合收益/成本比** | ★★★☆☆ | **★★★★★** | ★★★☆☆ |

**推荐方案 2** 的理由：

1. **零额外排序成本**: 与 origin-only 相同的 4-pass uint32_t radix sort
2. **最大化中层收益**: 3-bit octant 已完全编码 BVH 节点 front-to-back 顺序，内角细分无额外收益
3. **精度更优**: 去掉 5-bit 内角后，origin 提升至 9 bits/axis (512³)，比方案 1 仅差 1 bit/axis
4. **实现极简**: octant 编码仅 3 条整数指令，无浮点除法，无分支
5. **向下兼容**: 若实测显示需要更高 origin 精度，可升级为 64-bit (30b origin + 3b octant + 31b 保留)

## Appendix B: Direction octant 与 BVH front-to-back ordering 的关系

cuBQL BVH 遍历 (`shrinkingRayQuery::forEachLeaf()`) 在每个内部节点决定先访问的子节点：

```
if (ray_enters_left_child_first)
    push right, visit left
else
    push left, visit right
```

这个决策由射线方向与分裂轴的符号决定。具体地：

- 若分裂沿 X 轴，`sign(dx)` 决定 child 顺序
- 若分裂沿 Y 轴，`sign(dy)` 决定 child 顺序
- 若分裂沿 Z 轴，`sign(dz)` 决定 child 顺序

因此，3-bit octant (`sign(dx), sign(dy), sign(dz)`) **完全编码了 BVH 每一层的 front-to-back 决策**。同一 octant 内的所有射线在所有 BVH 节点处的 child 访问顺序完全相同 → traversalStack 状态一致 → warp 无分歧。

这解释了为什么仅 3 bits 的 direction octant 就能提供显著的 BVH 中层 coherence 收益。

---

*设计完成: 2026-02-20*
*状态: 待实施*
