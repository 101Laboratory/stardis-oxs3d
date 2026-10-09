# merge_phase_M5_SF_FAIL_on_merge — 双池合并触发批量 M5_SF_FAIL

**状态**: ✅ 已修复（merge_to_single_pool 内 batch_idx sentinel 化）  
**发现日期**: 2026-03-12  
**修复日期**: 2026-03-13  
**分支**: stardis-oxs3d-merge-phase  
**严重度**: 高 — 一次性触发数十个 M5_SF_FAIL，大量路径不可恢复失败  
**关联**: `[TODO]merge_phase_enc_residual`（同一根因的不同表现）

---

## 问题描述

persistent wavefront solver 在 dual-buffer pool（`num_active_views == 2`）触发合并（`should_merge()` → `merge_to_single_pool()`）后，紧接着出现**批量 M5_SF_FAIL**——一次性触发几十个。

M5_SF_FAIL 的直接触发条件是 boundary surface reinject 路径的 `chosen_dst ≤ 0`。在正常运行中偶发少量 M5_SF_FAIL 可接受（DRAIN 末期噪声），但 merge 事件一次性引爆几十个是系统性故障。

---

## 根因分析

### ~~初始假说（已排除）~~

~~直接原因：merge 时未处理完 enc/cp 残留请求，导致后续迭代访问到垃圾值。~~

**插桩数据否定了此假说**：merge 时 `enc=0, cp=0, enc_pend=0, cp_pend=0`，双池均无 enc/cp 残留。

### 真实根因：batch_idx 跨 per-view 缓冲区失效

**核心问题**：每个 pool_view 拥有独立的 `ray_hits[]` 缓冲区，路径的 `batch_idx` 是该缓冲区内的索引。Merge 将 View 1 路径并入 View 0，但 **batch_idx 仍指向 View 1 的缓冲区空间**，而 `merged_pass` 统一用 View 0 的 `pv->ray_hits[]` 读取——导致读到错误的光线结果。

### 详细因果链

```
should_merge() 触发合并（某半池 active_compact < view_size/8）
│
├→ gpu_wait_download_all(pv_a) + gpu_wait_download_all(pv_b)
│    ├→ V0 光线结果写入 views[0].ray_hits[0..4965]
│    └→ V1 光线结果写入 views[1].ray_hits[0..4979]
│
├→ merge_to_single_pool()
│    ├→ views[0].view_size = pool_size     （扩展为全池）
│    ├→ views[0].ray_count = 0             （重置计数器）
│    └→ num_active_views = 1
│
├→ 进入 single-pool 循环: pv = &pool.views[0]
│
├→ 第一次 merged_pass(pool, pv=views[0], scn):
│    ├→ Phase A: merged_pass_distribute_step() 对每个 active 路径执行:
│    │    h0 = &pv->ray_hits[p->ray_req.batch_idx]
│    │                ↑ pv = views[0]
│    │                ↑ 但原 V1 路径的 batch_idx 是 V1 缓冲区的索引!
│    │
│    ├→ V0 路径: batch_idx 指向 views[0].ray_hits → ✅ 正确
│    ├→ V1 路径: batch_idx 指向 views[0].ray_hits → ❌ 读到 V0 路径的光线结果!
│    │
│    ├→ batch_idx 碰撞: V0 和 V1 有 627 个重叠的 batch_idx 值
│    │   → V1 路径读到的 hit 属于完全不同的 V0 路径
│    │
│    ├→ step_conductive_ds_process() 使用错误的 hit 位置/法向
│    ├→ 后续 enclosure 查询得到错误的 enc_id
│    ├→ enc0==enc1（同 enclosure）→ dst=0
│    └→ M5_SF_FAIL（retry 10次后 RES_BAD_OP_IRRECOVERABLE）
│
└→ 所有受影响的 V1 路径在同一迭代暴露 → 批量触发
```

### 关键代码位置

| 文件 | 行号 | 说明 |
|------|------|------|
| `sdis_solve_persistent_wavefront.c` | @3921 | `&pv->ray_hits[p->ray_req.batch_idx]` — 跨缓冲区读取发生点 |
| `sdis_solve_persistent_wavefront.c` | @360-376 | `merge_to_single_pool()` — 未迁移 ray_hits |
| `sdis_solve_persistent_wavefront.c` | @5880-5912 | single-pool loop 用 `pv=views[0]` 调用 merged_pass |
| `sdis_wf_steps_bnd_sf.c` | @314 | M5_SF_FAIL 日志输出 |

**源文件路径**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/`

---

## 与 `[TODO]merge_phase_enc_residual` 的关系

该 issue 与 `merge_phase_enc_residual` 可能共享同一类根因（per-view 缓冲区在 merge 时失效），但本 issue 的主要失败路径是 **ray batch_idx 跨缓冲区**，与 enc/cp 残留无关。

enc_residual 问题可能在某些场景中独立存在（当 merge 时确实有 enc/cp 残留），但在当前 porous 场景中并非触发因素。

---

## 推荐修复方案

### ~~F1（已排除）：在 `should_merge()` 添加 enc/cp 残留检查~~

**不能修复此问题**。插桩数据证明 merge 时 enc/cp 均为零。真实根因是 ray batch_idx 跨缓冲区。

### F4（推荐）：Merge 前对双池各执行一次 merged_pass 消费光线结果

在 `merge_to_single_pool()` 之前，对 V0 和 V1 各执行一次完整的 merged_pass + compact，消费掉已下载的 ray_hits，使路径推进到下一阶段（不再持有 batch_idx 引用）：

```c
/* ── Merge 前：消费双池各自的 ray_hits ── */
if(pv_a->gpu_pending || pv_a->enc_gpu_pending || pv_a->cp_gpu_pending) {
  res = gpu_wait_download_all(pool, pv_a, sv);
  if(res != RES_OK) return res;
}
if(pv_b->gpu_pending || pv_b->enc_gpu_pending || pv_b->cp_gpu_pending) {
  res = gpu_wait_download_all(pool, pv_b, sv);
  if(res != RES_OK) return res;
}

/* 各 view 消费自己的 ray_hits（batch_idx 在正确的缓冲区空间内） */
pool->total_steps++;
res = merged_pass(pool, pv_a, scn);  /* V0 distribute 用 V0 ray_hits */
if(res != RES_OK) return res;
compact_active_paths(pool, pv_a);

pool->total_steps++;
res = merged_pass(pool, pv_b, scn);  /* V1 distribute 用 V1 ray_hits */
if(res != RES_OK) return res;
compact_active_paths(pool, pv_b);

/* 现在所有路径的 batch_idx 已消费，可以安全 merge */
merge_to_single_pool(pool);
```

**优点**:
- 直击根因：merge 前消费光线结果，batch_idx 不再指向旧缓冲区
- merged_pass 中 Phase C 收集的新 ray 请求会在 merge 后的 single-pool 循环中正确处理
- 逻辑清晰，与现有 drain 路径一致（drain 也是 wait → merged_pass → compact）

**风险**:
- Merge 前多了两次 merged_pass，略增 merge 延迟（可接受，merge 是低频事件）
- 需确保 merged_pass 后 Phase C 新收集的 enc/cp 请求在 merge 后不丢失
  → enc/cp 请求存储在 per-view buffer 中，merge 后 views[0] 的 buffer 被复用
  → 需在 merge 前也处理 enc/cp batch（或在 merged_pass 后追加 post_merged_enc_cp）

### F5（备选）：Merge 时迁移 V1 ray_hits 到 V0

在 `merge_to_single_pool()` 中，将 V1 的 `ray_hits` 内容拷贝到 V0 缓冲区的偏移位置，并修正所有 V1 路径的 `batch_idx += v0_ray_count`。

**优点**: merge 后立即可用，无需额外 merged_pass  
**缺点**: 实现复杂（需遍历所有 V1 路径修正 batch_idx/batch_idx2/batch_indices[]），且 V0 ray_hits 缓冲区需足够大容纳两倍数据

### F6（已应用 ✅）：Merge 时标记 V1 路径的 batch_idx 为 sentinel

在 `merge_to_single_pool()` 内部，扫描 V1 范围内所有活跃且 `needs_ray==1` 的路径，将 `batch_idx` 设为 sentinel `(uint32_t)-1`。

**机制分析**:
- Phase A: `needs_ray=1`, `batch_idx==sentinel` → 函数立即返回，`needs_ray` 保持为 1
- Phase B: `cascade_advance_single_path()` 开头检查 `needs_ray` → 立即 break，不推进
- Phase C: `needs_ray=1` → 重新收集 ray 请求到合并后 views[0] 的缓冲区，`batch_idx` 指向正确空间
- 下一轮 GPU trace 使用正确的 views[0] 缓冲区 → 读取结果正确

**优点**: 实现简单，直接修改 `merge_to_single_pool()` 一处即可覆盖所有 merge 调用点  
**缺点**: V1 路径丢弃一轮已下载的 ray 结果（约 V1 active 数量，< 12.5% pool），merge 是低频事件，代价可忽略

**实现位置**: `sdis_solve_persistent_wavefront.c` → `merge_to_single_pool()` 函数开头

---

## 复现条件

- 分支: `stardis-oxs3d-merge-phase`
- 配置: dual-buffer 模式（`STARDIS_PIPELINE=1` 或默认）
- 场景: 任意含多 enclosure 的场景（porous 等）
- 触发: 等待其中一个半池 active count 跌破 12.5% 阈值，触发 merge
- 数据: `Stardis-Starter-Pack/porous/merge_state.csv`, `post_merge_failures.txt`

---

## 插桩方案 — Merge State Dump

### 目标

在 merge 触发时，输出两个池所有路径的 path_id 和状态，使报错路径可追溯至来源 pool view 和原始 phase。

### 环境变量

```
STARDIS_MERGE_DUMP=<file_path>     启用 merge 状态 dump（追加模式）
```

不设置时零开销，不影响正常运行。

### 实现

**函数**: `dump_pool_state_at_merge()` — 在 `merge_to_single_pool()` 之前调用

**输出格式**: CSV，每次 merge 事件写一个带注释头的块：

```csv
# MERGE step=12450 reason=dual_underutilized views=2 pool_size=20000
# V0: base=0 size=10000 active=1249 rays=4966 enc=0 cp=0 gpu_pend=0 enc_pend=0 cp_pend=0
# V1: base=10000 size=10000 active=1274 rays=4980 enc=0 cp=0 gpu_pend=0 enc_pend=0 cp_pend=0
slot,view,path_id,phase,active,needs_ray,ray_bucket,steps,batch_idx,enc_batch_idx,cp_batch_idx,ray_count_ext,ipix_x,ipix_y,realisation
13,0,3133399,54,1,1,4,42616,1304,0,2139095039,6,238,311,23
534,1,34567,35,1,1,1,201,462,0,2139095039,2,125,262,23
```

**关键字段**:
| 字段 | 含义 | 用途 |
|------|------|------|
| `slot` | 槽位编号 | 定位 hot_arr/enc_arr |
| `view` | 所属 pool view (0/1) | **核心**: 追踪路径来源池 |
| `path_id` | 路径唯一 ID | 与报错日志关联 |
| `phase` | 数值 phase | 匹配 `enum path_phase` |
| `batch_idx` | RT batch 索引 | sentinel=4294967295 表示未初始化 |
| `enc_batch_idx` | enc_locate batch 索引 | 检查残留 enc 请求 |
| `cp_batch_idx` | closest_point batch 索引 | 检查残留 cp 请求 |

**插桩点**: 两处 `merge_to_single_pool()` 调用前
1. `pool_run_dual()` 行 ~5150 — probe/probe_batch 模式
2. `sdis_solve_camera_ir_render_persistent_wavefront()` 行 ~5830 — camera 模式

**过滤**: 只输出 active 或 phase ∉ {PATH_HARVESTED} 的路径，减少噪声。

### Worktree

实现分支: `stardis-oxs3d-merge-phase` 上的 worktree `stardis-oxs3d-test-validation`

---

## 插桩数据分析（2026-03-13）

**数据来源**: `Stardis-Starter-Pack/porous/merge_state.csv` (156,699 行)
**错误日志**: `Stardis-Starter-Pack/porous/post_merge_failures.txt`

### Merge 事件

仅 **1 次 merge**，step=345038，pool_size=20000：

| View | base | size | active | rays | enc | cp | gpu_pend | enc_pend | cp_pend |
|------|------|------|--------|------|-----|-----|----------|----------|---------|
| V0 | 0 | 10000 | 1249 | 4966 | **0** | **0** | 0 | 0 | 0 |
| V1 | 10000 | 10000 | 1274 | 4980 | **0** | **0** | 0 | 0 | 0 |

**关键发现**: `enc=0, cp=0` — **初始假说 F1（enc/cp 残留）被否定**。

### 路径状态分布

2523 条活跃路径，**100% 处于 `needs_ray=1`（等待 GPU 光线结果）**：

| Phase | 值 | V0 | V1 | Bucket | 说明 |
|-------|------|------|------|--------|------|
| `PATH_ENC_QUERY_EMIT` | 54 | 617 | 608 | ENCLOSURE (4) | 6射线 enclosure 查询 |
| `PATH_CND_DS_STEP_TRACE` | 35 | 473 | 482 | STEP_PAIR (1) | 2射线导热 delta-sphere |
| `PATH_BND_SF_REINJECT_SAMPLE` | 14 | 159 | 184 | STEP_PAIR (1) | 2射线边界重注入 |

### batch_idx 冲突分析

| 指标 | V0 | V1 |
|------|------|------|
| batch_idx 范围 | [0, 4960] | [0, 4974] |
| 有效 batch_idx 数 | 1249 | 1274 |
| **跨 view 重叠值** | — | **627 个** |

**batch_idx 是 per-view `ray_hits[]` 的内部索引**。merge 后 V1 路径的 batch_idx 指向 V0 的 ray_hits → 读到错误的光线结果。

### M5_SF_FAIL 交叉验证

**26 条 M5_SF_FAIL，15 个唯一 path_id — 100% 来自 View 1**：

| path_id | slot | view | phase | batch_idx | 失败次数 |
|---------|------|------|-------|-----------|----------|
| 3091155 | 13754 | **1** | 35 (CND_DS_STEP_TRACE) | 1600 | 5 |
| 1974553 | 12500 | **1** | 35 (CND_DS_STEP_TRACE) | 40 | 3 |
| 3042685 | 13890 | **1** | 35 (CND_DS_STEP_TRACE) | 1682 | 3 |
| 2994516 | 15491 | **1** | 35 (CND_DS_STEP_TRACE) | 4672 | 2 |
| 2812917 | 16416 | **1** | 35 (CND_DS_STEP_TRACE) | 2916 | 2 |
| 2991936 | 15533 | **1** | 35 (CND_DS_STEP_TRACE) | 4696 | 2 |
| (其余9个) | ... | **1** | 35 | ... | 各1次 |

**共性**: 全部 view=1, phase=35, needs_ray=1, bucket=STEP_PAIR, ray_count_ext=2。

### M5_SF_FAIL 失败值分析

| enc0 / enc1 模式 | 次数 | 含义 |
|------------------|------|------|
| `enc0=0, enc1=0` | 22 | 两端查到同一 enclosure → dst=0 |
| `enc0=0xFFFFFFFF, enc1=0` | 2 | enc0 为**未初始化 sentinel** |
| `enc0=2, enc1=2` | 2 | 两端同一 enclosure → dst=0 |

所有 `dst=0`，所有 `h0miss=0`（24/26），2条 `h0miss=1`。

### 结论

1. **根因确认**: batch_idx 跨 per-view 缓冲区失效
2. **受影响路径**: 仅 View 1 路径（merge 后 batch_idx 指向 V0 ray_hits）
3. **触发 phase**: 100% 为 `PATH_CND_DS_STEP_TRACE`（2-ray conductive delta-sphere）
4. **失败机制**: 读到错误 hit → 错误位置 → 错误 enclosure → enc0==enc1 → dst=0 → M5_SF_FAIL
5. **原 F1 方案无效**: merge 时无 enc/cp 残留

---

## 验证计划

1. ✅ **插桩验证**: merge_state.csv 收集完成，根因已定位
2. ✅ **修复验证**: 应用 F6 补丁后，post-merge M5_SF_FAIL 和 enc invalid 全部消失（2026-03-13 验证通过）

---

*创建: 2026-03-12*  
*更新: 2026-03-13 — 添加插桩方案*  
*更新: 2026-03-13 — 数据分析完成，根因确认为 batch_idx 跨缓冲区失效，F1 排除，推荐 F4*  
*更新: 2026-03-13 — 应用 F6 修复：merge_to_single_pool() 内 sentinel 化 V1 batch_idx，构建通过*  
*更新: 2026-03-13 — 验证通过：post-merge M5_SF_FAIL + enc invalid 全部消失*
