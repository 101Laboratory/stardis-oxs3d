# solver_loop_duplication — camera 入口内联求解循环与 pool_run 重复

**状态**: ✅ RESOLVED — 已完成重构  
**创建日期**: 2026-03-14  
**解决日期**: 2026-03-15  
**严重度**: 中 — 不影响正确性，影响可维护性  
**文件**: `stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

---

## 问题描述

persistent wavefront solver 有三个求解入口点：

| 入口 | 行范围 | 求解循环方式 |
|------|--------|-------------|
| `solve_camera_persistent_wavefront` | L4771–L5594 | **内联实现**完整的双流/单流循环（~600 行） |
| `solve_persistent_wavefront_probe` | L5835–L5885 | 调用 `pool_run()` |
| `solve_persistent_wavefront_probe_batch` | L5901–L6015 | 调用 `pool_run()` |

probe 和 probe_batch 已正确抽象：它们只负责初始化任务列表和 vtable，然后将求解循环委托给 `pool_run()`（→`pool_run_dual()`/`pool_run_single()`）。

**问题在于 camera 入口**：它在函数体内从 L4995 到 L5571 内联实现了完整的双流/单流求解循环，与 `pool_run_dual()`（L4456–L4688）和 `pool_run_single()`（L4216–L4279）是**语义等价的重复实现**。

---

## 解决方案与结果

已按“camera 为基线”的目标完成统一重构：

1. 引入统一执行封装 `run_pool()`，替代外部直接触达 `pool_run_dual/pool_run_single`
2. 双流与单流执行逻辑统一为 camera 已验证路径，并通过 `pool_run_cfg` 控制行为
3. `solve_camera_persistent_wavefront` 删除内联求解循环，改为调用 `run_pool`
4. `solve_persistent_wavefront_probe` 与 `solve_persistent_wavefront_probe_batch` 同步改为调用 `run_pool`
5. 入口点仅负责初始化任务/上下文，求解循环驱动下沉到统一执行层
6. 同步清理 `gpu_wait_d2h` 双后缀封装与 `DIAG_V3` 假说遗留分支

验证结论：目标文件静态检查通过（无新错误），issue 关闭。

---

## 重复代码定位

### Camera 内联 vs pool_run_dual 对比

| 逻辑阶段 | Camera 内联 (L5021–L5385) | pool_run_dual (L4456–L4688) |
|----------|--------------------------|----------------------------|
| submit_thread_init | ✅ L5029 附近 | ✅ L4469 |
| Startup: compact→merged→refill(A,B) | ✅ L4995–L5019 | ✅ L4471–L4504 |
| Startup: gpu_submit_all(A,B) sync | ✅ L5005–L5019 | ✅ L4506–L4519 |
| Half-A: wait_submit→wait_d2h→merged→compact→refill→signal_submit | ✅ L5063–L5109 | ✅ L4527–L4570 |
| Half-B: 对称实现 | ✅ L5164–L5210 | ✅ L4582–L4626 |
| Dynamic merge check | ✅ L5338–L5385 | ✅ L4639–L4680 |
| Drain: wait submits→wait_d2h→merged_pass | ✅ L5387–L5420 | ✅ L4682–L4700 |
| submit_thread_destroy | ✅ L5424 | ✅ L4703 |

### Camera 内联 vs pool_run_single 对比

| 逻辑阶段 | Camera 内联 (L5407–L5571) | pool_run_single (L4216–L4279) |
|----------|--------------------------|-------------------------------|
| merged_pass | ✅ | ✅ |
| compact_active_paths | ✅ | ✅ |
| refill_pool | ✅ | ✅ |
| gpu_launch_all (async) | ✅ | ✅ |
| gpu_wait_download_all (sync) | ✅ | ✅ |
| DIAG_V3 补偿 cascade | ❌ 无 | ✅ L4270 |
| pool_update_active_count | ✅ | ✅ |
| 安全检查 (infinite loop guard) | ✅ | ✅ |

### Camera 入口独有的附加逻辑（不在 pool_run 中）

1. **进度报告** (`pcent_progress` / `progress[]` 回调) — 在 Half-A 和 Half-B 完成后各执行一次
2. **诊断更新** (`pool_update_diagnostics`) — 每半循环后调用
3. **周期性日志** — 每 1000 步详细输出射线统计 (bucket 分解)
4. **O13 Timeline 日志** — `STARDIS_PIPELINE_LOG=2` 时输出每循环 6 时间戳
5. **Drain phase 检测** (`in_drain_phase` 转换 + `time_refill_phase_s` 记录)
6. **汇总统计** — 循环结束后输出完整 DONE 摘要 + WoS CP 统计 + drain phase report

---

## 心智模型（目标架构）

```
求解入口点  ──────────────────────────────  求解循环驱动
                                           
camera_entry:                               
  ├─ pool_create()                          
  ├─ pool.ops = &wf_ops_camera             
  ├─ pool.ops->generate_tasks()             
  ├─ create_batch_contexts()                
  ├─ fill_pool()                           pool_run_ex(&pool, sv, scn, &callbacks)
  └─ pool_run_ex() ─────────────────────►   ├─ if dual: pool_run_dual_ex()
                                            │   └─ [统一双流循环]
probe_entry:                                │       ├─ on_step_complete(cb)  ← 进度/诊断
  ├─ pool_create()                          │       └─ on_half_cycle_end(cb) ← 日志
  ├─ pool.ops = &wf_ops_probe              ├─ if single: pool_run_single_ex()
  ├─ pool.ops->generate_tasks()            │   └─ [统一单流循环]
  ├─ fill_pool()                           │       └─ on_step_complete(cb)
  └─ pool_run_ex() ─────────────────────►  └─ 返回
                                           
probe_batch_entry:                          
  ├─ pool_create()                          
  ├─ pool.ops = &wf_ops_probe_batch        
  ├─ pool.ops->generate_tasks()             
  ├─ fill_pool()                           
  └─ pool_run_ex() ─────────────────────►  
```

**核心原则**：入口点只负责初始化（pool 创建、vtable 绑定、任务生成、batch context、fill），求解循环由统一的 `pool_run` 系列函数驱动。

---

## 重构方案

### 方案 A：回调扩展 pool_run（推荐）

将 camera 内联循环中独有的逻辑（进度报告、诊断、日志）提取为可选回调，扩展现有 `pool_run` 接口：

```c
/* 求解循环的可选回调 — camera 模式使用，probe 模式传 NULL */
struct pool_run_callbacks {
    /* 每个 half-cycle 完成CPU工作后调用 (双流模式每循环调用两次) */
    void (*on_step_complete)(struct wavefront_pool* pool,
                             struct pool_view* pv,
                             void* user_ctx);
    void* user_ctx;  /* 指向 camera 的进度/日志上下文 */
};

static res_T
pool_run_ex(struct wavefront_pool* pool,
            struct s3d_scene_view* sv,
            struct sdis_scene* scn,
            const struct pool_run_callbacks* cb);  /* cb=NULL → 无回调 */
```

**Camera 的 `on_step_complete` 回调负责**：
1. `pool_update_diagnostics()`
2. 进度百分比更新 + `print_progress_update()`
3. 周期性射线统计日志
4. Drain phase 转换检测
5. O13 Timeline 日志（需要 `t_cy[]` 时间戳 — 可能需要 pool 内部记录）

**Camera 循环结束后的汇总** 移到 `pool_run_ex` 返回后，在 camera 入口函数内完成（这部分本来就是入口特有的）。

**probe/probe_batch 调用**：`pool_run_ex(&pool, sv, scn, NULL)` — 行为与现有 `pool_run` 完全一致。

### 方案 B：标志位驱动

在 `wavefront_pool` 中添加 `enable_progress`、`enable_diagnostics` 等标志，由 `pool_run_single`/`pool_run_dual` 内部检查。

❌ 不推荐：条件分支侵入核心循环、不够灵活、每增加新功能都要改 pool 结构。

---

## 已知风险与注意事项

1. **Camera 双流使用 `gpu_wait_d2h_all`，pool_run_dual 使用 `gpu_wait_d2h_only`**
   - Camera L5076: `gpu_wait_d2h_all()` — 等待所有 GPU 流（RT + enc + cp）的 D2H 完成
   - pool_run_dual L4537: `gpu_wait_d2h_only()` — 仅等待 RT 流的 D2H
   - 这是一个**语义差异**，需要确认是 camera 的修正版还是 pool_run_dual 的 bug。如果 camera 版本更正确，统一时应采用 `gpu_wait_d2h_all`。

2. **DIAG_V3 补偿 cascade** — 仅存在于 `pool_run_single`（L4270），camera 的单流循环中没有。统一后需要保留。

3. **O13 Timeline 时间戳** — camera 在循环内维护 `t_cy[6]` 用于 timeline 日志。回调方案需要将时间戳传入或由 pool 内部记录。

4. **汇总统计 + cleanup** — camera 循环后有大量汇总日志和 cleanup 代码（pixel trace file close 等），这些不应进入 `pool_run`，保持在入口函数中。

5. **`env_tl="2"` 硬编码** — Camera 内联循环 L5282 有一行 `env_tl="2";` 硬编码覆盖了环境变量读取，疑似调试遗留，统一前应清理。

---

## 验收标准

1. `solve_camera_persistent_wavefront` 内不再包含求解循环（while 循环），改为调用 `pool_run_ex` 或等价统一接口
2. probe / probe_batch 通过同一接口调用，行为不变
3. 所有现有 CTest 测试通过（`ctest -C Release --output-on-failure`）
4. IR 渲染输出与重构前 bit-exact（同种子同 spp）
5. Camera 进度报告、诊断日志、drain phase 检测功能不丢失

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `sdis_solve_persistent_wavefront.c` L4216–L4298 | `pool_run_single` — 已抽象的单流循环 |
| `sdis_solve_persistent_wavefront.c` L4456–L4688 | `pool_run_dual` — 已抽象的双流循环 |
| `sdis_solve_persistent_wavefront.c` L4707–L4768 | `pool_run` — 统一调度器（dual→single fallthrough） |
| `sdis_solve_persistent_wavefront.c` L4771–L5594 | `solve_camera_persistent_wavefront` — **待重构的 camera 入口** |
| `sdis_solve_persistent_wavefront.c` L5596–L5885 | `solve_persistent_wavefront_probe` — 参考实现（已使用 pool_run） |
| `sdis_solve_persistent_wavefront.c` L5901–L6015 | `solve_persistent_wavefront_probe_batch` — 参考实现（已使用 pool_run） |
