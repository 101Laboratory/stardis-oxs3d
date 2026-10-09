# enc/cp延迟审计——distribute步迁入cascade冲突分析

**日期**: 2026-03-10  
**背景**: O11 merged_pass已实现单OMP扫描（distribute+cascade+collect+harvest）。当前 `merged_pass_distribute_step()` 在Phase A中调用step函数，这些step函数可能产生enc_locate PENDING或cp PENDING。为了保持L1局部性，enc/cp批量查询在 `post_merged_enc_cp()` 中**已在cascade之后**执行。

**核心问题**: Phase A的step函数可能产生enc/cp PENDING状态。这些PENDING在当前轮的merged_pass中不会被消费（cascade会break出来，Phase C2/C3收集请求）。下一轮的 `post_merged_enc_cp` 写回RESULT后，再由下一轮的cascade消费。L1局部性已经天然保持——因为 Phase A+B 是对同一个slot的连续操作。

**目标**: 审计 distribute 中所有与enc/cp交互的step函数，确认它们在当前merged_pass架构下**不存在因果冲突**。

---

## §1 当前数据流（merge-phase worktree已实现）

```
Round N:
  merged_pass:
    per-path {
      Phase A: distribute_step() — 消费round N-1的ray_hits，调用step函数
               可能产生 enc_locate PENDING 或 cp PENDING
      Phase B: cascade — no-ray loop，遇到PENDING则break
      Phase C: collect ray + enc_locate + cp 请求
      Phase D: harvest
    }
  compact + refill
  post_merged_enc_cp:  — 同步执行enc_locate和cp batch查询
                         写回RESULT到path_state
  gpu_launch + gpu_wait: — ray trace
  
Round N+1:
  merged_pass:
    per-path {
      Phase A: distribute_step() — 消费round N的ray_hits
               如果该path上一轮是 ENC_LOCATE_RESULT / CND_WOS_*_RESULT，
               则 hot->needs_ray == 0，Phase A跳过
      Phase B: cascade — 消费ENC_LOCATE_RESULT / CND_WOS_*_RESULT
               (这些RESULT由post_merged_enc_cp在round N末尾写入)
      ...
    }
```

---

## §2 distribute中所有step函数分类

### §2.1 advance_one_step_with_ray switch表（15个phase case）

| Phase | Step函数 | 访问enc_arr | 访问cp | 产生enc PENDING | 产生cp PENDING | 多光线预交付 | 分桶 |
|-------|----------|------------|--------|----------------|----------------|-------------|------|
| `PATH_RAD_TRACE_PENDING` | `step_radiative_trace` | ✗ | ✗ | ✗ | ✗ | 1-ray | RADIATIVE |
| `PATH_COUPLED_COND_DS_PENDING` | `step_conductive_ds_process` | ✓写enc->query_pos | ✗ | ✗ | ✗ | 2-ray | STEP_PAIR |
| `PATH_CND_DS_STEP_TRACE` | `step_conductive_ds_process` | ✓写enc->query_pos | ✗ | ✗ | ✗ | 2-ray | STEP_PAIR |
| `PATH_BND_SS_REINJECT_SAMPLE` | `step_bnd_ss_reinject_process` | ✓读enc_arr | ✗ | ✗ | ✗ | 4-ray预交付 | STEP_PAIR |
| `PATH_BND_SF_REINJECT_SAMPLE` | `step_bnd_sf_reinject_process` | ✓读/写enc_arr | ✗ | **✓可能** | ✗ | 2-ray | STEP_PAIR |
| `PATH_BND_SF_NULLCOLL_RAD_TRACE` | `step_bnd_sf_nullcoll_rad_trace` | ✗ | ✗ | ✗ | ✗ | 1-ray | OTHER |
| `PATH_BND_EXT_DIRECT_TRACE` | `step_bnd_ext_direct_result` | ✗ | ✗ | ✗ | ✗ | 1-ray | OTHER |
| `PATH_BND_EXT_DIFFUSE_TRACE` | `step_bnd_ext_diffuse_result` | ✗ | ✗ | ✗ | ✗ | 1-ray | OTHER |
| `PATH_BND_EXT_DIFFUSE_SHADOW_TRACE` | `step_bnd_ext_diffuse_shadow_result` | ✗ | ✗ | ✗ | ✗ | 1-ray | OTHER |
| `PATH_BND_SFN_RAD_TRACE` | `step_bnd_sfn_rad_trace` | ✗ | ✗ | ✗ | ✗ | 1-ray | OTHER |
| `PATH_CND_WOS_FALLBACK_TRACE` | `step_cnd_wos_fallback_result` | ✗ | ✗ | ✗ | ✗ | 1-ray | OTHER |
| `PATH_CNV_STARTUP_TRACE` | `step_cnv_startup_result` | ✗ | ✗ | ✗ | ✗ | 1-ray | OTHER |
| `PATH_ENC_QUERY_EMIT` | `step_enc_query_resolve` | ✓读dir_hits | ✗ | ✗间接 | ✗ | 6-ray预交付 | ENCLOSURE |
| `PATH_ENC_QUERY_FB_EMIT` | `step_enc_query_fb_resolve` | ✓读fb_hit | ✗ | **✓直接** | ✗ | 1-ray预交付 | ENCLOSURE |
| `PATH_CND_INIT_ENC` | (未激活) | — | — | — | — | — | — |

### §2.2 三类分桶

distribute按 `hot->ray_bucket` 分桶已在merged_pass中内联合并：
- **RADIATIVE**: 调用 `step_radiative_trace` → 纯辐射，不涉及enc/cp
- **STEP_PAIR**: 调用 `step_conductive_ds_process` (DS路径) 或 fallback到 `advance_one_step_with_ray` (SF/SS路径)
- **OTHER/ENCLOSURE**: 调用 `advance_one_step_with_ray` → 可能产生enc PENDING

---

## §3 关键enc/cp生产链审计

### §3.1 enc_locate PENDING生产路径

**唯一直接生产者**: `step_enc_locate_submit()` (sdis_wf_steps_enc.c L56-75)
- 写入 `enc->locate.query_pos[3]`、`enc->locate.return_state`
- 设置 `hot->phase = PATH_ENC_LOCATE_PENDING`
- 设置 `hot->needs_ray = 0`

**在distribute（Phase A）中能触发此调用的step函数**:

1. **`step_enc_query_fb_resolve()`** (sdis_wf_steps_enc.c L264-306)
   - 条件: 6-ray主查询全部失败 → 1-ray fallback也失败 → 调用 `step_enc_locate_submit()`
   - 频率: 极低（`enc_query_escalated_to_m10` 统计）
   - **结论**: Phase A调用此step后 `hot->phase = PATH_ENC_LOCATE_PENDING`，Phase B的cascade的 `path_phase_is_enc_locate_pending()` guard立即break，Phase C2收集enc_locate请求 → **无冲突**

2. **`step_bnd_sf_reinject_process()`** (sdis_wf_steps_bnd_sf.c L220+)
   - 中间路径: → `step_enc_query_emit()` → 设置 `hot->needs_ray=1` → Phase B cascade不进入（needs_ray guard） → Phase C收集ray请求
   - 但 `step_enc_query_emit` 产生的是 `PATH_ENC_QUERY_EMIT`（需要新的6-ray查询），不是 `PATH_ENC_LOCATE_PENDING`。enc_locate PENDING只在后续round的 `step_enc_query_fb_resolve` 中可能产生。
   - **结论**: 本轮不直接产生enc_locate PENDING → **无冲突**

### §3.2 cp PENDING（closest_point）生产路径

**cp PENDING状态**: `PATH_CND_WOS_CLOSEST`、`PATH_CND_WOS_DIFFUSION_CHECK`

**distribute中的step函数**: 所有15个step函数均**不产生cp PENDING**。

**cp PENDING的唯一生产者**:
- `step_cnd_wos_closest()` (sdis_wf_steps_cnd.c L614-627) → 被 `step_cnd_wos_check_temp()` 调用
- `step_cnd_wos_diffusion_check()` (sdis_wf_steps_cnd.c L695-708) → 被 `step_cnd_wos_closest_result()` 调用

这两者都在 `advance_one_step_no_ray()` 中调用，即**仅在cascade（Phase B）内部产生**。

**结论**: Phase A永远不产生cp PENDING → Phase C3的cp收集完全由Phase B产出 → **无冲突**

### §3.3 `step_conductive_ds_process()` enc写入分析

(sdis_wf_steps_core.c L540-635)

- **写入**: `enc->query_pos[3]`（用于后续 `PATH_CND_DS_STEP_ENC_VERIFY`）
- **产出phase**: `PATH_CND_DS_STEP_ENC_VERIFY`（enc验证，计算型）或 `PATH_CND_DS_CHECK_TEMP`（温度检查）或 `PATH_CND_DS_STEP_ADVANCE`（前进一步）
- **不产生enc_locate PENDING**

**但存在间接链**:
```
Phase A: step_conductive_ds_process → PATH_CND_DS_STEP_ENC_VERIFY
Phase B: cascade → advance_one_step_no_ray → step_cnd_ds_step_enc_verify
         → 可能调用 step_enc_query_emit → PATH_ENC_QUERY_EMIT (needs_ray=1)
         → cascade breaks (needs_ray guard)
Phase C: collect ray request for enc query
...后续rounds的 enc_query_resolve → enc_query_fb_resolve → enc_locate_submit
```

**结论**: `enc->query_pos` 的写入在Phase A，消费在Phase B（同一slot连续执行，L1 cache热）。enc_locate PENDING在Phase A不会直接产生，只有通过多轮ray查询→fallback链才会最终触发。**无冲突**。

---

## §4 预交付（pre-copy）需求分析

Phase A中3种特殊phase需要从 `pv->ray_hits[]` 预投递到slot-local存储：

| Phase | 预交付目标 | 数据量 | 来源 |
|-------|-----------|--------|------|
| `PATH_ENC_QUERY_EMIT` (6-ray) | `enc_arr[slot].dir_hits[0..5]` | 6×s3d_hit | `pv->ray_hits[enc_arr.batch_indices[j]]` |
| `PATH_ENC_QUERY_FB_EMIT` (1-ray) | `enc_arr[slot].fb_hit` | 1×s3d_hit | `pv->ray_hits[batch_idx]` |
| `PATH_BND_SS_REINJECT_SAMPLE` (4-ray) | `p->locals.bnd_ss.ray_frt/bck[0..1]` | 4×s3d_hit | `pv->ray_hits[batch_idx_*]` |

这些预交付逻辑**已在** `merged_pass_distribute_step()` 中实现（L3700-3722）。

**关键限制**: 预交付必须在step函数调用**之前**完成，因为step函数会读取这些pre-delivered hit数据。当前实现正确：预交付代码在switch之前。

---

## §5 cascade guard分析

`cascade_advance_single_path()` (sdis_solve_persistent_wavefront.c L2393-2465) 的guard break条件**已全面覆盖**所有PENDING类型：

```c
if(hot->needs_ray) break;                             // ray-pending
if(PATH_DONE || PATH_ERROR) break;                    // 已完成
if(path_phase_is_ray_pending())  break;                // 显式ray-pending phase
if(path_phase_is_enc_locate_pending()) break;          // enc_locate PENDING
if(path_phase_is_cp_pending()) break;                  // cp PENDING
```

**含义**: 如果Phase A的step函数产生了 `PATH_ENC_LOCATE_PENDING`（仅 `step_enc_query_fb_resolve` 可能），Phase B的cascade会**立即break**，不会尝试advance。subsequent Phase C2会收集该请求。

---

## §6 RESULT消费分析

`post_merged_enc_cp()` 在merged_pass之后执行，写回：
- `enc_locate RESULT` → `enc_arr[slot].locate.{prim_id, side}` + `hot->phase = PATH_ENC_LOCATE_RESULT`
- `cp RESULT` → `p->locals.cnd_wos.{cp_hit}` + `hot->phase = PATH_CND_WOS_CLOSEST_RESULT / DIFFUSION_CHECK_RESULT`

这些RESULT在**下一轮** merged_pass的Phase B（cascade）中被 `advance_one_step_no_ray` 消费：
- `PATH_ENC_LOCATE_RESULT` → `step_enc_locate_result()` → 解析enc_id → 设置return_state
- `PATH_CND_WOS_CLOSEST_RESULT` → `step_cnd_wos_closest_result()` → 可能触发diffusion_check
- `PATH_CND_WOS_DIFFUSION_CHECK_RESULT` → `step_cnd_wos_diffusion_check_result()`

**关键**: 这些RESULT path的 `hot->needs_ray == 0`，所以Phase A（`merged_pass_distribute_step()`）会跳过它们（第一行 `if(!hot->needs_ray) return 0;`）。cascade直接消费。**无冲突**。

---

## §7 结论

### 7.1 已确认无冲突

| 条件 | 状态 | 说明 |
|------|------|------|
| Phase A step产生enc_locate PENDING | **安全** | 仅 `step_enc_query_fb_resolve` 可能；cascade guard立即break；Phase C2收集 |
| Phase A step产生cp PENDING | **不可能** | cp PENDING仅由cascade内部的no-ray步产生 |
| Phase A写enc->query_pos | **安全** | 写入和消费在同一slot的连续Phase A→B执行中，L1热 |
| RESULT消费顺序 | **安全** | `post_merged_enc_cp` 写回在merged_pass之后；下一轮Phase A跳过(needs_ray=0)；Phase B cascade消费 |
| 预交付完整性 | **安全** | 3种多光线预交付已在step调用前完成 |
| OMP线程安全 | **安全** | 每个path的Phase A-D操作在同一线程内顺序执行；enc_locate/cp收集使用atomic append |

### 7.2 当前merge-phase已实现的架构是正确的

当前 `merged_pass_distribute_step()` 将distribute的step函数调用合并到每path的Phase A中，随后Phase B cascade在同一线程内继续执行同一个slot。这是enc/cp延迟的自然结构——step函数不需要移动到cascade"内部"（因为cascade的 `advance_one_step_no_ray` 的switch表中没有这些ray-result phase的case），它们在Phase A中执行后设置的新phase由Phase B的cascade继续推进。

**实质**: distribute的step逻辑并非移动到cascade，而是作为cascade的"入口前奏"（Phase A），在同一L1 cache热区内连续执行。enc/cp请求被推迟到merged_pass结束后的 `post_merged_enc_cp()` 同步批执行。这正是用户要求的"延迟到cascade后"的语义。

### 7.3 无需额外代码变更

所有关键交互路径已被当前merge-phase worktree的实现正确处理：
1. **Phase A→B连续性**: 确保L1局部性
2. **PENDING guard完整性**: cascade的5个break条件覆盖所有PENDING类型
3. **RESULT跨轮消费**: `post_merged_enc_cp` → 下一轮Phase B
4. **预交付时序**: 在step调用前完成
5. **debug assertions**: `SDIS_DEBUG_CHECKS` 宏保护的安全断言已就位

---

*审计基于 stardis-cus3d-merge-phase worktree (opt/merge-phase branch) 和 stardis-cus3d main HEAD的源码。*
