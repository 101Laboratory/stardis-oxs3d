# d2h_enc_cp_distribute_misalignment — D2H 函数越权执行 enc/cp distribute

**状态**: ✅ 已修复  
**发现日期**: 2026-03-12  
**修复日期**: 2026-03-13  
**分支**: stardis-oxs3d-merge-phase  
**严重度**: 低 — 代码架构不对齐，不影响正确性  
**关联**: `[RESOLVED]merge_phase_M5_SF_FAIL_on_merge/`（同一代码区域的排查过程中发现）

---

## 问题描述

`gpu_wait_d2h_all()` 是 D2H 下载函数，但在下载 enc/cp 结果后**越权执行了 distribute**（将结果写回 `path_state` 并设置 RESULT phase）。RT 结果的 distribute 已正确放在 `merged_pass()` Phase A 中，enc/cp 应当遵循同样的架构。

### 当前架构（不对齐）

```
gpu_wait_d2h_all():
  ├─ RT:  下载 ray_hits              ← 仅下载（正确）
  ├─ enc: 下载 + distribute ✗       ← 越权：设置 PATH_ENC_LOCATE_RESULT
  └─ cp:  下载 + distribute ✗       ← 越权：设置 PATH_CND_WOS_*_RESULT

merged_pass():
  ├─ 重置 enc_locate_count=0, cp_count=0
  ├─ Phase A: RT distribute（per-path, batch_idx → ray_hits）
  ├─ Phase B: cascade（消费 RESULT phases）
  ├─ Phase C: 收集新 ray/enc/cp 请求
  └─ Phase D: harvest
```

### 目标架构（对齐）

```
gpu_wait_d2h_all():
  ├─ RT:  仅下载
  ├─ enc: 仅下载                     ← 移除 distribute
  └─ cp:  仅下载                     ← 移除 distribute

merged_pass():
  ├─ Phase A: distribute all GPU results
  │    ├─ enc distribute（batch 级，循环外）
  │    ├─ cp distribute（batch 级，循环外）
  │    ├─ 重置所有计数器
  │    └─ per-path RT distribute + step（循环内）
  ├─ Phase B: cascade（消费 RESULT phases）
  ├─ Phase C: 收集新 ray/enc/cp 请求
  └─ Phase D: harvest
```

Phase A 统一为"distribute 所有 GPU 结果"：enc/cp 和 RT 是对等的三种 GPU 结果类型，
只是实现层面 enc/cp 为 batch 级（遍历结果数组）、RT 为 per-path 级（遍历活跃路径检查 needs_ray），
这是实现细节而非架构层级差异。

---

## 根因

历史演化遗留。enc/cp GPU batch 最初是同步调用（`post_merged_enc_cp()`），distribute 紧跟在 GPU 返回后。迁移到 O11 异步流水线时，将 enc/cp 的 GPU 调用拆到了异步路径，但 distribute 被直接搬入 `gpu_wait_d2h_all()` 而非对齐到 `merged_pass()` Phase A。RT 的 distribute 是重新设计的（`merged_pass_distribute_step()`），所以从一开始就在正确位置。

---

## 影响评估

**当前不影响正确性**：
- enc/cp distribute 在 D2H 中提前执行 → RESULT phase 在 merged_pass 之前就已设置
- Phase B cascade 消费 RESULT phase → 逻辑正确，只是时序提前
- batch_idx sentinel 修复已独立解决 merge 后的跨缓冲区问题

**架构风险**：
- D2H 函数承担了不属于自己的职责，增加维护理解成本
- 如果未来 merged_pass 需要在 Phase A 中对 enc/cp RESULT 做额外处理（如统计、诊断），当前架构无法拦截

---

## 修复方案

### 涉及代码位置

| 文件 | 行号 | 内容 |
|------|------|------|
| `sdis_solve_persistent_wavefront.c` | ~3827 | `gpu_wait_d2h_all()` 内 `pool_distribute_enc_locate_results()` 调用 |
| `sdis_solve_persistent_wavefront.c` | ~3851 | `gpu_wait_d2h_all()` 内 `pool_distribute_cp_results()` 调用 |
| `sdis_solve_persistent_wavefront.c` | ~4320 | `merged_pass()` 函数体，计数器重置段 |

### distribute 函数

- `pool_distribute_enc_locate_results()` (L2098): 遍历 `pv->enc_locate_results[0..count-1]`，写 `enc_arr[slot].locate`，设 `PATH_ENC_LOCATE_RESULT`
- `pool_distribute_cp_results()` (L2171): 遍历 `pv->cp_hits[0..count-1]`，写 `p->locals.cnd_wos.cached_hit`，设 RESULT phase

### 修改步骤

**M1**: `gpu_wait_d2h_all()` — 删除 enc distribute 调用

```c
// 删除这两行：
res = pool_distribute_enc_locate_results(pool, pv);
if(res != RES_OK) return res;
```

**M2**: `gpu_wait_d2h_all()` — 删除 cp distribute 调用

```c
// 删除这两行：
res = pool_distribute_cp_results(pool, pv);
if(res != RES_OK) return res;
```

**M3**: `merged_pass()` — 扩展 Phase A，在计数器重置前 distribute enc/cp

```c
/* ── Phase A: Distribute all GPU results ── */

/* A-enc: distribute enc_locate results (batch-level, before counter reset) */
if(pv->enc_locate_count > 0 && !pv->enc_gpu_pending) {
  res = pool_distribute_enc_locate_results(pool, pv);
  if(res != RES_OK) return res;
}

/* A-cp: distribute cp results (batch-level, before counter reset) */
if(pv->cp_count > 0 && !pv->cp_gpu_pending) {
  res = pool_distribute_cp_results(pool, pv);
  if(res != RES_OK) return res;
}

/* Reset per-view counters (enc/cp counts consumed above) */
pv->ray_count = 0;
pv->enc_locate_count = 0;
pv->cp_count = 0;
pv->done_count = 0;
/* ... existing counter resets ... */
```

循环内的 `merged_pass_distribute_step()` 即为 A-rt，无需修改。

### 关键约束

1. **distribute 必须在计数器重置前** — `pool_distribute_enc_locate_results()` 遍历 `pv->enc_locate_count` 个结果
2. **`!pv->enc_gpu_pending` 检查** — 确保 GPU 已完成（D2H 已回），否则结果未就绪
3. **`post_merged_enc_cp()`** (L4868/4890) 是同步 batch 路径的独立调用点，内部自行 dispatch + distribute，不受本次修改影响

### 其他调用点审计

| 调用点 | 行号 | 状态 | 说明 |
|--------|------|------|------|
| `gpu_wait_d2h_all()` | 3827/3851 | **本次移除** | 异步 D2H 路径 |
| `post_merged_enc_cp()` | 4868/4890 | 保留 | 同步 batch，自包含 dispatch+distribute |
| `gpu_postprocess()` | 3444/3469 | 待确认 | 旧的 pre-merged_pass 路径，可能已无活跃调用 |

---

## 验证计划

1. 构建通过
2. porous 场景运行，确认无 M5_SF_FAIL 和 enc invalid
3. 数值一致性：与修改前版本对比（单缓冲模式），3σ 检验

---

## 验证结果

- ✅ 构建通过（零错误）
- ✅ porous 场景运行，无 M5_SF_FAIL、无 enc invalid
- ✅ 性能无回归（总 cycle time 不变，distribute 工作量仅在 time_enc_locate_s/time_cp_s → time_cascade_s 之间转移）

---

*创建: 2026-03-13*  
*关闭: 2026-03-13*
