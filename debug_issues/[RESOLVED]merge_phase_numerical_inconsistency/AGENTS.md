# merge_phase_numerical_inconsistency — merged_pass 架构数值不一致 + M5_SF_FAIL 频率升高

**状态**: ✅ 已解决  
**发现日期**: 2026-03-11  
**解决日期**: 2026-03-11  
**分支**: stardis-cus3d-merge-phase  
**严重度**: 高 — 影响数值正确性，阻塞 merge-phase 合并  
**根因**: `merged_pass_distribute_step()` case `RAY_BUCKET_RADIATIVE` 缺少 phase 守卫，将非 `PATH_RAD_TRACE_PENDING` 的 radiative 路径错误路由到 `step_radiative_trace()`  
**关联**: `[RESOLVED]M5_SF_FAIL`（主分支 serial fallback 修复，与本问题无关）

---

## 问题描述

`stardis-cus3d-merge-phase` 分支的 solver 输出与主 `stardis-cus3d` 分支存在数值不一致，并伴有显著较高的 M5_SF_FAIL 警告（警告 log_warn 已注释，但 `RES_BAD_OP_IRRECOVERABLE` 仍执行，路径静默失败）。

**关键约束**: **单缓冲模式（STARDIS_PIPELINE=0）结果也不一致**，排除了 dual-buffer merge 机制作为唯一根因。根因在 `merged_pass()` 架构本身与主分支 separate-phase 逻辑的不等价。

---

## 架构差异分析

merge-phase 将主分支的 4 阶段 pipeline（distribute → cascade → collect → harvest）合并为单次 OMP 扫描 `merged_pass()`（Phase A→B→C→D per-path 内联执行）。

| 维度 | main cus3d | merge-phase |
|------|-----------|-------------|
| **cascade 频率** | **1×/迭代**（gpu_wait_and_postprocess 中；cpu_between 的 cascade 是 refill 后独立路径，不重复） | **1×/迭代**（Phase B） |
| **STEP_PAIR dispatch** | phase 分桶：仅 CND_DS_STEP_TRACE / COUPLED_COND_DS_PENDING → `step_conductive_ds_process`，其余 → `advance_one_step_with_ray` | 条件分支等价（V1/V2 验证） |
| **distribute 策略** | 分桶 OMP 双阶段（radiative / conductive 各自独立并行） | 内联 per-path Phase A，switch(ray_bucket) |
| **collect 策略** | 3-pass 零原子桶化 + prefix sum → 直写 pinned | 线程本地缓冲 + atomic flush → merged_pass_fixup_batch_idx |
| **执行顺序** | 全局 distribute → 全局 cascade → 全局 collect | 逐路径 distribute → cascade → collect |
| **refill 频率** | 2×/迭代（cpu_pre_gpu + cpu_between） | 1×/迭代（merged_pass 后） |

---

## 假说列表

### H1：STEP_PAIR dispatch 路径不等价（✅ 已排除）

**验证结果 (V1+V2)**: 主分支 `pool_distribute_ray_results()` 用 **phase（而非 ray_bucket）** 分桶。`bucket_conductive` 仅含 `PATH_CND_DS_STEP_TRACE` 和 `PATH_COUPLED_COND_DS_PENDING`，BND_SF/SS_REINJECT 走 `bucket_other` → `advance_one_step_with_ray()`，与 merge-phase 的条件分支逻辑**完全等价**。H1 排除。

<details>
<summary>原始 H1 分析（已过时）</summary>

**核心差异**: 主分支 `pool_distribute_ray_results()` Phase 2 将所有 `bucket_conductive[]` 路径统一调用 `step_conductive_ds_process(p, hot, scn, h0, h1, &pool->enc_arr[slot])`。

merge-phase `merged_pass_distribute_step()` 对 `RAY_BUCKET_STEP_PAIR` 做条件分支：
```c
// sdis_solve_persistent_wavefront.c @3975 (merge-phase)
case RAY_BUCKET_STEP_PAIR:
  if(ph_before == PATH_CND_DS_STEP_TRACE
  || ph_before == PATH_COUPLED_COND_DS_PENDING) {
    lr = step_conductive_ds_process(p, hot, scn, h0, h1, &pool->enc_arr[slot]);
  } else {
    // ⚠️ 此路径在主分支中不存在——主分支所有 STEP_PAIR 统一走 step_conductive_ds_process
    lr = advance_one_step_with_ray(p, hot, scn, h0, h1, pool, (size_t)slot);
  }
  break;
```

如果存在 `ray_bucket == STEP_PAIR && ph_before ∉ {CND_DS_STEP_TRACE, COUPLED_COND_DS_PENDING}` 的路径，merge-phase 会走 `advance_one_step_with_ray`（通用 step dispatcher），而主分支走 `step_conductive_ds_process`（专用 conductive 处理），两者有不同的物理计算逻辑。

**已知 STEP_PAIR 使用者**（代码注释 @3974）：
> BND_SF_REINJECT、BND_SS_REINJECT 也设 ray_bucket=STEP_PAIR 用于 GPU batching，但它们不是 conductive DS 路径。

**影响链**:
```
STEP_PAIR 的非 CND_DS 路径
  → merge-phase: advance_one_step_with_ray（通用 dispatcher）
  → main:        step_conductive_ds_process（conductive 专用）
    → 不同物理计算结果
      → 系统性数值偏差 + M5_SF_FAIL 概率升高
```

**预期特征**:
- 偏差取决于 BND_SF/SS reinject 路径被 STEP_PAIR 标记的频率
- BND_SF_REINJECT 恰好是 M5_SF_FAIL 的触发根源——如果 dispatch 不等价，retry_count 累积模式会不同

</details>

### H1a：RAY_BUCKET_RADIATIVE dispatch 不等价（✅ 确认为根因）⭐

**在 V2 调查中发现的新 bug**（原始假说未覆盖）。

**核心差异**: 主分支 `compact_active_paths()` 用 **phase** 分桶——仅 `PATH_RAD_TRACE_PENDING` 进 `bucket_radiative[]` → `step_radiative_trace()`。其余具有 `ray_bucket == RAY_BUCKET_RADIATIVE` 的 phase 进 `bucket_other[]` → `advance_one_step_with_ray()`：

| phase | 主分支路由 | merge-phase (修复前) |
|-------|-----------|---------------------|
| `PATH_RAD_TRACE_PENDING` | `step_radiative_trace()` ✅ | `step_radiative_trace()` ✅ |
| `PATH_BND_SF_NULLCOLL_RAD_TRACE` | `advance_one_step_with_ray()` ✅ | `step_radiative_trace()` ❌ |
| `PATH_BND_SFN_RAD_TRACE` | `advance_one_step_with_ray()` ✅ | `step_radiative_trace()` ❌ |
| `PATH_BND_EXT_DIFFUSE_TRACE` | `advance_one_step_with_ray()` ✅ | `step_radiative_trace()` ❌ |
| `PATH_CND_WOS_FALLBACK_TRACE` | `advance_one_step_with_ray()` ✅ | `step_radiative_trace()` ❌ |

**影响链**:
```
BND_SF_NULLCOLL_RAD_TRACE 等 phase + RAY_BUCKET_RADIATIVE
  → merge-phase(修复前): step_radiative_trace()（物理计算不等价，对这些 phase 无意义）
  → main:                advance_one_step_with_ray()（正确的 per-phase dispatcher）
    → M5_SF_FAIL 概率升高 + 系统性数值偏差
```

**修复**: `case RAY_BUCKET_RADIATIVE:` 添加 phase 守卫——仅 `PATH_RAD_TRACE_PENDING` 走 `step_radiative_trace()`，其余 fallthrough 到 `advance_one_step_with_ray()`。修复后 M5_SF_FAIL 消失，数值与主分支一致。

主分支每迭代运行 cascade 两次：
1. `gpu_wait_and_postprocess` 中：distribute 后立即 cascade
2. `cpu_between` 中：harvest/refill 后再次 cascade

merge-phase 仅 1 次（Phase B）。缺少第 2 次 cascade 意味着需要额外非射线步骤的路径被延迟到下一迭代处理。

**影响分析**:
- 纯延迟不应影响最终正确性（路径终态相同，只是多走一轮迭代）
- **但**如果 enc_locate 结果分发后需要即时 cascade 推进（如 PATH_ENC_LOCATE_RESULT → cascade → re-emit），merge-phase 的单次 cascade 可能在错误时间点访问状态
- 需要确认 enc_locate distribute (`pool_distribute_enc_locate_results`) 在 merge-phase 的调用位置

**预期特征**:
- 路径总步数可能更多
- 如果仅是延迟，3σ 检验不应显示系统性偏差

### H2：单次 cascade vs 双次 cascade 时序差异（✅ 已排除）

**验证结果 (V3)**: 在 `merged_pass` 后补第 2 次 cascade（`STARDIS_DIAG_V3=1`），数值结果无额外改善，H2 排除。

另外重新分析确认：主分支 `pool_run_single` 中 `gpu_wait_and_postprocess` 仅含 1 次 cascade；`cpu_between` 的 cascade 是 harvest+refill 后的独立路径，merge-phase 的 Phase B 已对应覆盖。原始架构差异分析表中「2×」描述有误，已更正。

### H3：per-path 内联 vs 全局分离执行顺序差异（低置信度）

主分支: 所有路径 distribute → barrier → 所有路径 cascade → barrier → 所有路径 collect  
merge-phase: 每个路径 distribute→cascade→collect（OMP dynamic 调度）

如果 `accumulate_result`（Phase D）对共享缓冲区有竞态影响，或浮点累积顺序敏感，可能产生差异。

**预期特征**: 差异在统计波动范围内，不应导致 M5_SF_FAIL。

### H4：merged_pass serial fallback 缺陷（低-中置信度）

当 `n < 64 || omp_nthreads <= 1` 时触发 serial fallback。类似 `[RESOLVED]M5_SF_FAIL` 模式，需验证 serial fallback 的 Phase A/C 逻辑是否与 OMP 路径完全等价。

---

## 验证方案

### V1：STEP_PAIR dispatch 数据插桩（⭐ 最高优先级，定位 H1）

在 `merged_pass_distribute_step` 的 STEP_PAIR else 分支插入日志：

```c
// sdis_solve_persistent_wavefront.c @3984 (merge-phase)
case RAY_BUCKET_STEP_PAIR:
  if(ph_before == PATH_CND_DS_STEP_TRACE
  || ph_before == PATH_COUPLED_COND_DS_PENDING) {
    lr = step_conductive_ds_process(p, hot, scn, h0, h1, &pool->enc_arr[slot]);
  } else {
    /* DIAG_H1: 此路径在主分支统一走 step_conductive_ds_process */
    log_warn(scn->dev,
      "DIAG_H1: STEP_PAIR fallthrough ph=%d slot=%u pid=%u\n",
      (int)ph_before, (unsigned)slot, (unsigned)p->path_id);
    lr = advance_one_step_with_ray(p, hot, scn, h0, h1, pool, (size_t)slot);
  }
  break;
```

**判定**:
- 日志有输出 → H1 确认，进一步确认这些 phase 在主分支中经过 conductive 桶走的是哪个函数
- 日志无输出 → H1 排除，升级 H2/H3

**位置**: `stardis-cus3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` @3975

### V2：对比 bucket 赋值逻辑（与 V1 并行）

在主分支 `pool_collect_ray_requests_bucketed()` 中追踪哪些 phase 被归入 `bucket_conductive`。在 merge-phase 中追踪 `hot->ray_bucket = RAY_BUCKET_STEP_PAIR` 的赋值点。确认两端 phase→bucket 映射一致性。

**关键比较点**:
- 主分支 `pool_distribute_ray_results()` Phase 2: `bucket_conductive[kk]` → 这些路径的 phase 值
- merge-phase `cascade_advance_single_path()` 或 step 函数中 `hot->ray_bucket = RAY_BUCKET_STEP_PAIR` 赋值

### V3：双 cascade 等价性测试（定位 H2）

在 merge-phase `pool_run_single` 的 `merged_pass` 后、`compact` 前插入第 2 次 cascade：

```c
// sdis_solve_persistent_wavefront.c @4915 (merge-phase pool_run_single)
res = merged_pass(pool, pv, scn);
if(res != RES_OK) return res;

/* DIAG_H2: 补第 2 次 cascade 以匹配主分支 */
res = pool_cascade_non_ray_steps_compact(pool, pv, scn);
if(res != RES_OK) return res;

compact_active_paths(pool, pv);
```

**判定**: 结果对齐 main → H2 确认（单次 cascade 不足）。

### V4：PT trace 逐路径对比（精确定位分叉点）

merge-phase 内建了强大的逐路径逐步骤 log 工具（**Debug 构建下可用**），可用于精确定位两分支状态序列分叉点。

**工具配置**:
- 环境变量 `STARDIS_PATH_TRACE=<max_lines>` — 启用追踪，设置最大输出行数（如 5000）
- 环境变量 `STARDIS_PATH_TRACE_PID=<path_id>` — 过滤指定路径 ID（-1 = 全部）

**输出格式** (CSV to stderr):
```
[PT] seq,tag,round,slot,pid,ph_before,ph_after,active,needs_ray,batch_idx,ray_count,steps,hit_dist,norm_x,norm_y,norm_z,ray_bkt,done_reason,step_res,enc_resolved,flt_prim,flt_enc,sf_retry
```

**Tag 含义**: `A`=distribute, `B`=cascade, `F`=first_ray  
**额外输出**: `[PT-HIT]` 行记录 distribute 前的 ray hit 原始数据

**⚠️ 使用注意**:
- 仅在 **Debug 构建** 下有效（Release 构建 pt_log 为 no-op）
- Debug 构建性能慢 10-100×，建议用小场景 + 单路径过滤
- 输出到 stderr，需 `2> trace.csv` 重定向
- `STARDIS_PATH_TRACE_PID` 默认值为 486729（非 -1），即默认只追踪该特定路径

**对比方案**: 两分支使用相同 seed + 相同 path_id，比较 CSV 输出中 `ph_before→ph_after` 序列。分叉点即为根因位置。

### V5：统计测试量化

`wf_[abc]` 测试组两分支各跑 N≥3 次，3σ 检验量化偏差规模和方向。

---

## 修复方案（已实施）

| ID | 方案 | 针对假说 | 结果 |
|----|------|---------|--------|
| **F1a** | `case RAY_BUCKET_RADIATIVE:` 添加 phase 守卫：仅 `PATH_RAD_TRACE_PENDING` → `step_radiative_trace()`，其余 → `advance_one_step_with_ray()` | H1a | ✅ 根因修复，M5_SF_FAIL 消失，数值一致 |
| ~~F1~~ | ~~统一 STEP_PAIR dispatch~~ | ~~H1~~ | — H1 已排除，无需修复 |
| ~~F2~~ | ~~补第 2 次 cascade 扫描~~ | ~~H2~~ | — H2 已排除，无需修复 |
| **F3** | 无需修复（浮点排序差异属可接受统计噪声） | H3 | — |

---

## 验证结果总结

1. **V1**: STEP_PAIR dispatch 插桩 → **H1 排除**：主分支同样用 phase 分桶，逻辑等价
2. **V2**: 对比 bucket 赋值逻辑 → **发现真正 bug**：RAY_BUCKET_RADIATIVE dispatch 不等价（H1a 确认为根因）
3. **V3**: 补第 2 次 cascade（`STARDIS_DIAG_V3=1`）→ 无额外改善，**H2 排除**
4. **V4/V5**: 未执行（H1a 修复已解决问题）

**根因**: `merged_pass_distribute_step()` `case RAY_BUCKET_RADIATIVE:` 缺少 phase 守卫，将 BND_SF_NULLCOLL_RAD_TRACE 等 4 个 phase 错误路由到 `step_radiative_trace()`。  
**修复**: 添加 phase 守卫与主分支 `compact_active_paths()` bucket 分桶逻辑等价。

---

## 相关文件

| 文件 (merge-phase) | 关注点 |
|---------------------|--------|
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | `merged_pass_distribute_step()`@3901 (H1a RADIATIVE@3975), `merged_pass()`@4281, `pool_run_single()`@4896, `cascade_advance_single_path()`@2393, PT 工具@85-165 |
| `stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sf.c` | M5_SF_FAIL@312-345 |

| 文件 (main, 参考基准) | 关注点 |
|------------------------|--------|
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | `pool_distribute_ray_results()`@2092 (Phase 2 conductive), `pool_collect_ray_requests_bucketed()`@1514, `cpu_between()`@3508 (2nd cascade@3520) |

---

*创建: 2026-03-11*
