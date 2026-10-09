# CPU Wait-for-GPU 瓶颈诊断与优化计划

**创建日期**: 2026-03-03
**状态**: 规划态 (Planned)
**关联分析**: `guide/performance/pool_size_scaling_timeline_analysis.md`, `guide/oxs3d/rt_query_service_potential.md`

---

## 0. 问题陈述

### 表面矛盾

| 数据来源 | 场景 | 32K rays 延迟 | 130K rays 延迟 |
|----------|------|---------------|----------------|
| RT 潜力文档 §4.2 (`real_a_width`) | 10K tri, SH, kernel-only | 0.044ms | 0.085ms |
| 池大小时序文档 §3.1/3.4 | 13K tri, 求解器完整管线 | ~1.0ms (半 cycle) | ~5.1ms (半 cycle) |
| **差距** | | **23×** | **60×** |

### 根因分析（上一轮确认）

两份文档测量的不是同一个东西：

```
潜力基准:  [cudaEvent-start] → optixLaunch(SH, SBT_RT) → [cudaEvent-stop]
                纯 RT Core kernel，设备驻留光线，单次命中，无传输

求解器实际: [AoS→SoA] → [H2D upload] → [optixLaunch(MH K=8, SBT_MH)]
            → [cudaStreamSync] → [D2H download MultiHitResult[N×K]]
            → [CPU OMP filter K candidates/ray] → [retrace if all rejected]
            → [enc_locate batch] → [cp batch] → [distribute results]
```

差距来源拆解（pool=8192 半池 ~32K rays 估算）:

| 组件 | 估算耗时 | 占比 |
|------|----------|------|
| Multi-Hit K=8 kernel (vs SH 0.044ms) | ~0.15-0.25ms | ~20% |
| H2D upload (Ray[] async) | ~0.05-0.10ms | ~8% |
| D2H download (MultiHitResult[32K×8]) | ~0.10-0.20ms | ~15% |
| cudaStreamSync overhead | ~0.02ms | ~2% |
| CPU filter eval (OMP, 32K×K candidates) | ~0.20-0.40ms | ~35% |
| enc_locate batch (sync GPU call) | ~0.05-0.10ms | ~8% |
| retrace 回旋 (~8% fallback) | ~0.05-0.10ms | ~8% |
| 其他 (stats, distribute) | ~0.03ms | ~4% |
| **合计** | **~0.65-1.17ms** | **100%** |

**核心结论**: RT Core kernel 仅占求解器 GPU-wait 相位的 ~20%。真正的瓶颈是 CPU 后处理 (filter eval ~35%) 和数据传输 (H2D+D2H ~23%)。GPU RT 服务远未饱和。

---

## 1. 目标

1. **精确度量**: 在求解器运行时获取 GPU-wait 相位的 **子阶段分解计时**，消除估算
2. **建立真实瓶颈排名**: 确认 CPU filter / D2H / MH kernel / enc_locate 的实际占比
3. **确定优化优先级**: 基于实测数据选择 ROI 最高的优化路径
4. **MH 基准**: 用 Multi-Hit K=8 模式建立 GPU kernel 真实基线吞吐

---

## 2. 验证标准

| 编号 | 标准 | 通过条件 |
|------|------|----------|
| V1 | 子阶段计时一致性 | sum(子阶段) ≈ 原始 wait 时间 (误差 <5%) |
| V2 | 瓶颈排名稳定性 | 3 次独立运行排名一致 |
| V3 | MH vs SH 倍率合理 | K=8 MH kernel ≈ 3-8× SH kernel (同射线数) |
| V4 | 构建不回退 | 所有现有测试仍通过 |

---

## 3. 终止触发条件

- 子阶段 instrumentation 引入 >5% 总耗时开销（计时本身太贵）
- MH 基准显示 GPU kernel 已是主要瓶颈（>60% wait 时间），则 filter 移 GPU 方案无意义
- 修改影响求解器数值结果（任何 bit-exact 偏移）

---

## 4. 实施步骤

### Phase A: Instrumentation（诊断）

#### A1. 扩展 `s3d_batch_trace_stats` 结构体

**文件**: `stardis-cus3d/oxstar-3d/0.10/s3d_wrapper/s3d.h` (L378-388)
**同步**: `stardis-cus3d/custar-3d/0.10/src/s3d.h` (L651-663)

新增字段：
```c
struct s3d_batch_trace_stats {
  /* --- 现有字段 (不变) --- */
  size_t  total_rays;
  size_t  batch_accepted;
  size_t  filter_rejected;
  size_t  retrace_accepted;
  size_t  retrace_missed;
  double  batch_time_ms;        /* 重新定义: sync + download 合计 */
  double  postprocess_time_ms;
  double  retrace_time_ms;

  /* --- 新增: GPU-wait 子阶段分解 --- */
  double  sync_wait_ms;         /* cudaStreamSynchronize 纯等待 */
  double  download_ms;          /* D2H MultiHitResult 下载 */
  double  filter_eval_ms;       /* CPU OMP filter 评估 (从 postprocess 中拆出) */
  double  uv_fixup_ms;          /* hit 结果 UV/normal 修正 (本身在 filter loop 内，可选) */
};
```

**影响范围**: 结构体大小增加 32 bytes (4 × double)。`memset(stats, 0, sizeof)` 已有的写法自动兼容零初始化。

#### A2. 在 `batch_trace_wait_impl` 中插入计时点

**文件**: `stardis-cus3d/oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` (L1658-2012)

当前结构：
```
t0 = now_ms()
  Phase 1: cudaStreamSynchronize + download
t1 = now_ms()         → batch_time_ms = t1 - t0
  Phase 2: OMP filter loop
t2 = now_ms()         → postprocess_time_ms = t2 - t1
  Phase 3: Retrace
t3 = now_ms()         → retrace_time_ms = t3 - t2
```

修改为：
```
t0 = now_ms()
  cudaStreamSynchronize(ctx->stream)
t0b = now_ms()        → sync_wait_ms = t0b - t0     ★ 新增
  d_multi_hits.download(...)
t1 = now_ms()         → download_ms = t1 - t0b       ★ 新增
                      → batch_time_ms = t1 - t0       (不变，兼容)
  Phase 2: OMP filter loop
t2 = now_ms()         → filter_eval_ms = t2 - t1      ★ 新增 (= postprocess_time_ms)
                      → postprocess_time_ms = t2 - t1  (不变，兼容)
  Phase 3: Retrace
t3 = now_ms()         → retrace_time_ms = t3 - t2     (不变)
```

#### A3. 在 `batch_trace_async_impl` 中计时 AoS→SoA 转换

**文件**: 同 A2

AoS→SoA 转换是同步 CPU 操作，包含在 `gpu_launch_async` 的 `launchB`/`launchA` 时间标记中。
此阶段不需要额外 stats 字段 — pipeline log 的 launch 相位已包含。

但可选：在 launch 日志中细化 `[AoS2SoA Xms | upload+launch Yms]`。

#### A4. 扩展 Pipeline Log 格式

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` (L3765-3792)

在 `STARDIS_PIPELINE_LOG=3` (新级别) 时追加子阶段分解：
```
[TIMELINE] step=N |waitA=X.XXms(sync=A.AA+dl=B.BB+filt=C.CC+rt=D.DD)|launchB=X.XX|cpuA=X.XX|...
```

需要在 `gpu_wait_and_postprocess` 中将 stats 传出到 pipeline log 格式化代码。
当前 stats 通过 `pool->trace_*_sum` 累加，但 per-step 值也可在 timeline 输出处访问 — 
需确认 `pv->last_trace_stats` 或类似临时存储。

#### A5. 在 `gpu_wait_and_postprocess` 中分离 enc_locate/cp 计时

**文件**: 同 A4 (L2928-3060)

当前 `enc_locate` 和 `closest_point` batch 在 `gpu_wait_and_postprocess` 内部独立计时（`time_enc_locate_s`, `time_cp_s`），已与 trace timer 分离。✅ 无需修改。

### Phase B: Multi-Hit 基准测试

#### B1. 在 oxs3d_throughput 中新增 MH Width Sweep

**文件**: `stardis-cus3d/oxstar-3d/0.10/src/main.cpp` (~L1091)

新增函数 `benchmarkSingleBatchWidth_MH()`：
- 复制 `benchmarkSingleBatchWidth` 逻辑
- 替换 `traceBatch` → `traceBatchMultiHit`
- 替换 `sbtRT()` → `sbtMH()`
- 使用同样的 cudaEvent 分阶段计时 (params upload, kernel, total)
- CSV 行前缀: `real_a_width_mh`
- 使用同场景 (10K tris, PCG seed)

#### B2. 运行并收集 MH 基线数据

目标数据点（对应求解器 pool_size）：

| 射线数 | 对应 pool_size (半池 × ~4 rays/path) |
|--------|--------------------------------------|
| 32,768 | pool=8192 |
| 65,536 | pool=16384 |
| 131,072 | pool=32768 |
| 262,144 | 理论扩展 |

预期结果格式：
```csv
real_a_width_mh,32768,T_submit,T_params,T_kernel_ms,T_total_ms,ker_MRays_s,...
```

### Phase C: 数据对齐与瓶颈确认

#### C1. 生成对照表

同场景 (porous foam 13K tris) 运行求解器 pool=8192, pipeline_log=3：

| 组件 | 基准 (B1) | 求解器实测 (A) | 差距 | 来源 |
|------|-----------|----------------|------|------|
| MH kernel @32K rays | ? ms | ? ms (sync_wait_ms) | ? | A2 vs B1 |
| D2H @32K rays | N/A | ? ms (download_ms) | — | A2 |
| CPU filter @32K rays | N/A | ? ms (filter_eval_ms) | — | A2 |
| Retrace @32K rays | N/A | ? ms (retrace_time_ms) | — | existing |
| enc_locate @32K rays | N/A | ? ms (time_enc_locate) | — | existing |

#### C2. 更新 rt_query_service_potential.md

在 §6.1 添加"求解器有效吞吐"列，标注 `real_a_width` 数据为 **RT Core 理论上界**。

### Phase D: 优化实施（取决于 C1 结果）

根据瓶颈排名的优化路径（按预期可能性排序）：

| 排名 | 如果最大瓶颈是... | 优化方案 | 预期收益 |
|------|-------------------|----------|----------|
| 1 | CPU filter eval (>30%) | 将 filter 移至 GPU anyhit 程序 | 消除 K=8 D2H + CPU filter 往返 |
| 2 | D2H 传输 (>20%) | 减少 K (K=4/2) 或 GPU 端 filter 后仅返回 accepted hit | D2H 量减少 K× |
| 3 | MH kernel (>40%) | 回退到 SH + GPU 端 filter | 消除 anyhit Top-K 开销 |
| 4 | enc_locate (>15%) | 将 enc_locate 移入 pipeline overlap | 隐藏于 cpuB 阶段 |

**Phase D 的具体实施仅在 C1 数据确认后展开**，此处仅列出可能路径。

---

## 5. 文件修改清单 (Phase A+B)

| # | 文件 | 修改类型 | 行范围 |
|---|------|----------|--------|
| 1 | `oxstar-3d/0.10/s3d_wrapper/s3d.h` | 结构体扩展 | L378-388 |
| 2 | `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | 计时点插入 | L1674-1693 |
| 3 | `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | log 格式扩展 | L3765-3792 |
| 4 | `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | stats 传出 | L2928-2980 |
| 5 | `oxstar-3d/0.10/src/main.cpp` | 新增 MH sweep | ~L1280 (新函数) |
| 6 | `oxstar-3d/0.10/src/unified_tracer.h` | 声明 (如需) | ~L167 |

**不修改的文件**（确认无影响）：
- `custar-3d/` — stats 结构同步但此路径不在 worktree 范围
- `stardis-solver` 核心算法 — 纯 instrumentation，不改逻辑
- 设备程序 `programs.cu` — Phase D 才涉及

---

## 6. Worktree 规划

| 属性 | 值 |
|------|-----|
| 名称 | `stardis-cus3d-gpu-phase-diag` |
| 基线 | `stardis-cus3d` main HEAD (`bd0c248` "add test data collectors") |
| 分支 | `opt/gpu-wait-instrumentation` |
| 位置 | `d:\Works\Projects\Stardis-GPU\stardis-cus3d-gpu-phase-diag\` |
| 作用域 | Phase A (instrumentation) + Phase B (MH benchmark) |
| 预估工作量 | 2-3 轮实现+验证 |

---

## 7. 风险

| 风险 | 缓解 |
|------|------|
| `now_ms()` 精度不够（Windows `QueryPerformanceCounter` 通常 <1μs） | 确认 now_ms 实现，必要时用 CUDA events |
| OMP 计时在高线程数下噪声大 | 取中位数而非均值 |
| stats 传出到 pipeline log 需要新的中间存储 | 在 `pool_view` 中添加 `last_trace_stats` 字段 |
| Phase B MH sweep 需要 ref-oxs3d 的 main.cpp 也改 | worktree 基于 ref-oxs3d，直接改即可 |