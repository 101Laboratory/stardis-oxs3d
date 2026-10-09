# CPU vs GPU 路径深度对比实验

**创建日期**: 2026-02-18  
**目的**: 验证 GPU wavefront 求解器与 CPU 原始求解器在路径深度（步数）上的一致性  
**背景**: 实验3发现 GPU 版本最大路径深度达 467,682 步。需要确认 CPU 原始版本是否存在
相同量级的超深路径，排除 GPU 版本实现 bug 导致的虚假深循环。

---

## 1. 问题定义

### 1.1 为什么需要这个对比

实验3 观察到：
- GPU wavefront 最大路径深度（`max_wavefront_depth`）= **467,682**
- 68.1% 射线是 `step_pair`（delta-sphere 导热步进），暗示大量计算花在深循环
- `cascade` 阶段占 CPU 时间 56%，被推测是 delta-sphere 深循环主导

如果 CPU 原始版本的同一场景**没有**相同量级的超深路径，说明 GPU 版本存在实现错误
（如死循环、重复状态转换等），优化方向应改为修 bug 而非优化 cascade。

### 1.2 对比的难点：指标定义不对等

| 维度 | CPU 原始版本 | GPU Wavefront 版本 |
|------|-------------|-------------------|
| **执行模型** | 深度优先递归（函数指针链） | 广度优先 wavefront（显式状态机） |
| **步数统计** | **无** — 仅有 `istep`/`nbounces` 等局部 debug 变量 | `steps_taken`（每次 phase 转换 +1） |
| **粒度** | 每个子路径函数可包含数千次内部迭代 | 每个 phase 转换 = 1 step |
| **统计收集** | **无任何统计** | 完整统计（射线分类、终止原因、最大深度） |

**关键**: GPU 的 `steps_taken` 计每次 `advance_one_step_*()` = phase transition，
不等于 CPU 的物理迭代数。需要定义**可直接对比**的通用指标。

---

## 2. 可对比指标设计

### 2.1 三层指标层级

```
Level 0: total_func_calls  — sample_coupled_path 主循环 while(!T->done) 的迭代数
         即 T->func() 被调用的总次数。
         对应 GPU: boundary_path / conductive_path / radiative_path / convective_path
         等"顶层函数"被进入的次数。

Level 1: sub_path_steps    — 每个子路径函数内部的物理迭代数
         ├─ ds_steps:  delta-sphere do/while 循环迭代数
         ├─ wos_steps: WoS for(;;) 循环迭代数
         ├─ rad_bounces: trace_radiative_path for(;;) 反弹次数
         └─ cnv_steps: convective_path for(;;) null-collision 迭代数

Level 2: total_physical_iterations = Σ(所有 sub_path_steps)
         全路径的总物理迭代次数 — 真正可比的"工作量"指标。
```

### 2.2 GPU 侧的对应关系

GPU `steps_taken` ≈ Level 0 (func_calls) + Level 1 (sub_path_steps) 的**混合**。
因为 GPU 将每一步都展开为细粒度 phase：

| CPU 函数 | CPU 内部循环 | GPU phase 序列 |
|---------|-------------|---------------|
| `conductive_path_delta_sphere` | 1 次 `T->func()` 调用，内含 N 次 do/while | `PATH_CND_DS_CHECK_TEMP` → `PATH_CND_DS_STEP_RAY` → `PATH_CND_DS_STEP_RAY_WAIT` → `PATH_CND_DS_STEP_VOLPOW` → `PATH_CND_DS_STEP_REWIND` → `PATH_CND_DS_STEP_ENC_VERIFY` → `PATH_CND_DS_STEP_ADVANCE` → 循环回 `CHECK_TEMP`。每次循环 ≈ 7 step transitions |
| `trace_radiative_path` | 1 次 `T->func()` 调用，内含 N 次 for(;;) | `PATH_RAD_TRACE` → `PATH_RAD_TRACE_WAIT` → `PATH_RAD_BOUNCE` 等。每次反弹 ≈ 3-4 step transitions |
| `boundary_path` | 1 次 `T->func()` 调用 | `PATH_BND_DISPATCH` → `PATH_BND_SS_*` / `PATH_BND_SF_*`。约 5-20 step transitions |

**因此**: GPU `steps_taken` / 7 ≈ CPU `ds_steps`（粗略换算，因 delta-sphere 占绝对多数）。

**精确对比**: 我们需要采集 Level 2，即两边的总物理迭代数。

### 2.3 最终对比指标矩阵

| 指标名 | CPU 来源 | GPU 来源 | 可比性 |
|--------|---------|---------|--------|
| `total_func_calls` | `sample_coupled_path` while 循环迭代数 | 进入 `PATH_COUPLED_*` 的次数 | ✅ 直接可比 |
| `ds_steps` | `conductive_path_delta_sphere` do/while 迭代数 | `PATH_CND_DS_CHECK_TEMP` 进入次数 | ✅ 直接可比 |
| `wos_steps` | `conductive_path_wos` for(;;) 迭代数 | `PATH_CND_WOS_*` 进入次数 | ✅ 直接可比 |
| `rad_bounces` | `trace_radiative_path` for(;;) 迭代数 | `PATH_RAD_BOUNCE` 进入次数 | ✅ 直接可比 |
| `cnv_steps` | `convective_path` for(;;) 迭代数 | `PATH_CNV_*` 循环进入次数 | ✅ 直接可比 |
| `total_physical_iters` | Σ(ds_steps + wos_steps + rad_bounces + cnv_steps) | 同 | ✅ 直接可比 |
| `done_reason` | 路径终止原因分类 | `path_state.done_reason` | ✅ 直接可比 |
| `T_value` | 终值温度 | `T.value` | ✅ 直接可比（已有 pixel_trace） |

---

## 3. 数据收集方案

### 3.1 CPU 侧插桩

CPU 原始版本完全没有统计收集。需要新增一个轻量数据结构在 per-path 级别记录。

**策略**: 新增 `struct path_depth_stats`，通过 `rwalk_context` 传递到各子路径函数。

#### 3.1.1 数据结构（新增头文件）

**文件**: `stardis-cpu/stardis-solver/0.16.2/src/sdis_path_depth_stats.h`

```c
/* Per-path depth statistics for CPU/GPU comparison experiment.
 * Activated by STARDIS_PATH_DEPTH_STATS=<output.csv> environment variable.
 */
#ifndef SDIS_PATH_DEPTH_STATS_H
#define SDIS_PATH_DEPTH_STATS_H

#include <stddef.h>

struct path_depth_stats {
  /* Level 0: top-level function calls */
  size_t total_func_calls;       /* T->func() invocations in sample_coupled_path */

  /* Level 1: sub-path physical iterations */
  size_t ds_steps;               /* delta-sphere do/while iterations */
  size_t wos_steps;              /* WoS for(;;) iterations */
  size_t rad_bounces;            /* radiative path bounces */
  size_t cnv_steps;              /* convective null-collision iterations */

  /* Level 1b: sub-path entry counts */
  size_t ds_entries;             /* times conductive_path_delta_sphere entered */
  size_t wos_entries;            /* times conductive_path_wos entered */
  size_t rad_entries;            /* times trace_radiative_path entered */
  size_t cnv_entries;            /* times convective_path entered */
  size_t bnd_entries;            /* times boundary_path entered */

  /* Level 2: rays */
  size_t rays_ds;                /* delta-sphere rays (2 per ds_step) */
  size_t rays_rad;               /* radiative rays */
  size_t rays_ds_retry;          /* delta-sphere robust retry rays */

  /* Terminated conditions */
  int    done_reason;            /* 0=none, 1=rad_miss, 2=temp_known,
                                    3=boundary_done, 4=time_rewind, -1=failed */

  /* Path identity */
  size_t pixel_x, pixel_y;      /* pixel coordinates */
  size_t spp_idx;                /* realisation index */
};
#define PATH_DEPTH_STATS_NULL {0,0,0,0,0, 0,0,0,0,0, 0,0,0, 0, 0,0,0}

#endif /* SDIS_PATH_DEPTH_STATS_H */
```

#### 3.1.2 插桩位置汇总

| # | 文件 | 函数 | 插桩点 | 记录字段 |
|---|------|------|--------|---------|
| 1 | `sdis_realisation_Xd.h` | `sample_coupled_path` | `while(!T->done)` 每次迭代 | `total_func_calls++` |
| 2 | `sdis_heat_path_conductive_delta_sphere_Xd.h` | `conductive_path_delta_sphere` | 函数入口 + `do/while` 每次迭代 | `ds_entries++`, `ds_steps++` |
| 3 | `sdis_heat_path_conductive_wos_Xd.h` | `conductive_path_wos` | 函数入口 + `for(;;)` 每次迭代 | `wos_entries++`, `wos_steps++` |
| 4 | `sdis_heat_path_radiative_Xd.h` | `trace_radiative_path` | 函数入口 + `for(;;)` 每次迭代 | `rad_entries++`, `rad_bounces++` |
| 5 | `sdis_heat_path_convective_Xd.h` | `convective_path` | 函数入口 + `for(;;)` 每次迭代 | `cnv_entries++`, `cnv_steps++` |
| 6 | `sdis_heat_path_boundary_Xd.h` | `boundary_path` | 函数入口 | `bnd_entries++` |
| 7 | `sdis_realisation.c` | `ray_realisation_3d` | T.done 后分类 | `done_reason` |
| 8 | `sdis_solve_camera.c` | solve 循环 | per-path 完成后 | 写 CSV |

#### 3.1.3 传递机制

`path_depth_stats` 指针通过 `rwalk_context` 传递（新增一个字段）:

```c
struct rwalk_context {
  /* ... 现有字段 ... */
  struct path_depth_stats* depth_stats;  /* NULL if not profiling */
};
```

所有子路径函数已经接收 `rwalk_context* ctx`，因此只需查看 `ctx->depth_stats`
是否非 NULL 即可决定是否记录。

#### 3.1.4 输出格式 (CSV)

环境变量 `STARDIS_PATH_DEPTH_CSV=cpu_path_depth.csv` 开启。

```
px,py,spp,func_calls,ds_steps,wos_steps,rad_bounces,cnv_steps,ds_entries,wos_entries,rad_entries,cnv_entries,bnd_entries,rays_ds,rays_rad,rays_ds_retry,total_physical_iters,done_reason,T_value,T_done
160,160,0,12,4523,0,3,0,5,0,2,0,5,9046,3,0,4526,2,298.150000,1
```

#### 3.1.5 聚合统计（程序退出时打印）

```
=== Path Depth Statistics (CPU) ===
total_paths:         3,276,800
completed_paths:     3,270,412
failed_paths:        6,388

--- Depth Distribution ---
  ds_steps:   min=0  max=???  mean=???  median=???  p95=???  p99=???  p999=???
  wos_steps:  min=0  max=???  ...
  rad_bounces: ...
  cnv_steps:  ...
  total_physical_iters: min=0  max=???  ...

--- Histogram (log2 buckets) ---
  [    1,     2): NNN paths
  [    2,     4): NNN paths
  [    4,     8): NNN paths
  ...
  [262144, 524288): NNN paths
  [524288,     ∞): NNN paths
```

### 3.2 GPU 侧插桩

GPU 版本已有 `steps_taken`（phase transitions）和完整统计。需要**新增**与 CPU
可直接对比的 Level 1 指标。

#### 3.2.1 新增字段

在 `struct path_state` 中追加:

```c
/* Path depth comparison fields (Level 1 physical iterations) */
size_t  ds_steps_L1;             /* delta-sphere loop iterations */
size_t  wos_steps_L1;            /* WoS loop iterations */
size_t  rad_bounces_L1;          /* radiative bounces */
size_t  cnv_steps_L1;            /* convective null-collision iterations */
size_t  func_calls_L0;           /* top-level sub-path entries */
```

#### 3.2.2 插桩位置

| # | 文件 | Phase | 动作 |
|---|------|-------|------|
| 1 | `sdis_wf_steps.c` | `PATH_CND_DS_CHECK_TEMP` 入口 | `p->ds_steps_L1++` |
| 2 | `sdis_wf_steps.c` | `PATH_CND_WOS_SAMPLE` 入口 | `p->wos_steps_L1++` |
| 3 | `sdis_wf_steps.c` | `PATH_RAD_TRACE` / `PATH_RAD_BOUNCE` | `p->rad_bounces_L1++` |
| 4 | `sdis_wf_steps.c` | `PATH_CNV_SAMPLE` 循环入口 | `p->cnv_steps_L1++` |
| 5 | `sdis_wf_steps.c` | `PATH_COUPLED_BOUNDARY`/`CONDUCTIVE`/`RADIATIVE`/`CONVECTIVE` | `p->func_calls_L0++` |

#### 3.2.3 输出

扩展现有 `STARDIS_PIXEL_TRACE` CSV:

```
path_id,px,py,spp,T_value,T_done,done_reason,steps,phase,func_calls,ds_steps,wos_steps,rad_bounces,cnv_steps
```

---

## 4. 数据解读与对比方案

### 4.1 精确对比框架

使用相同场景、相同参数运行两个版本，产出两份 CSV。

#### 运行命令

```powershell
# === CPU 版本 ===
cd Stardis-Starter-Pack\porous
$env:STARDIS_PATH_DEPTH_CSV="..\..\optimization\path_depth_comparison\cpu_path_depth.csv"
$env:STARDIS_PIXEL_TRACE_CPU="..\..\optimization\path_depth_comparison\cpu_pixel_trace.csv"
..\..\stardis-cpu\build\bin\Release\stardis.exe `
  -M porous.txt -t 1 -V 3 `
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "..\..\optimization\path_depth_comparison\cpu_IR.ht"

# === GPU wavefront 版本 ===
$env:STARDIS_PIXEL_TRACE="..\..\optimization\path_depth_comparison\gpu_pixel_trace.csv"
$env:STARDIS_POOL_SIZE="4096"
..\..\stardis-cus3d\build\bin\Release\stardis.exe `
  -M porous.txt -t 1 -V 3 `
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "..\..\optimization\path_depth_comparison\gpu_IR.ht"
```

**关键**: 使用 `-t 1`（单线程）确保 RNG 顺序一致。但注意两个版本的 RNG 实现不同
（CPU 使用 `ssp_rng`，GPU 使用 `wf_rng` CBRNG），路径不会逐条匹配。
对比的是**统计分布**而非逐条匹配。

### 4.2 对比维度与判定标准

#### D1: 最大深度对比

| 判定 | 标准 |
|------|------|
| ✅ 一致 | CPU `max(ds_steps)` 和 GPU `max(ds_steps_L1)` 在同一数量级（差异 < 2×） |
| ⚠️ 可疑 | 差异在 2×–10× |
| ❌ 异常 | 差异 > 10× — GPU 版本可能有 bug |

#### D2: 分布形态对比

对 `total_physical_iters` 的分布进行比较：

1. **直方图形态**: 两边的 log2 bucket 直方图应当形状近似
2. **百分位数**: p50, p95, p99, p999 应同数量级
3. **均值**: 平均物理迭代数应近似（允许 10% 差异，因 RNG 差异）

#### D3: 子路径类型分布

| 指标 | CPU | GPU | 判定 |
|------|-----|-----|------|
| `ds_steps / total_physical_iters` | x% | y% | 占比差异 < 5% 为一致 |
| `rad_bounces / total_physical_iters` | x% | y% | |
| `cnv_steps / total_physical_iters` | x% | y% | |

#### D4: 终止原因分布

| done_reason | CPU 占比 | GPU 占比 | 判定 |
|-------------|---------|---------|------|
| 1 (radiative miss) | x% | y% | |
| 2 (temperature known) | x% | y% | |
| 3 (boundary done) | x% | y% | |
| 4 (time rewind) | x% | y% | |
| -1 (failed) | x% | y% | |

#### D5: GPU `steps_taken` vs CPU `total_physical_iters` 换算验证

理论上 GPU `steps_taken` ≈ `total_physical_iters` × 7（因为每个 delta-sphere
物理迭代对应约 7 个 phase transitions）。验证这个换算因子是否稳定。

### 4.3 分析脚本

产出 CSV 后使用 Python 分析（脚本在 `analyze_path_depth.py` 中）。

---

## 5. 风险与预期

### 5.1 预期结果

基于物理分析，CPU 和 GPU 应当有**相同量级**的超深路径：
- porous 场景含多孔介质，delta-sphere 步长很小
- 热传导路径需要穿越整个多孔结构到达已知温度边界
- 这是物理性质决定的，与实现无关

预期 CPU `max(ds_steps)` 在 **50,000–100,000** 级别（GPU 467K 对应约 67K 物理步
÷ 7 phase/step），两者应同数量级。

### 5.2 如果不一致

| 场景 | 可能原因 | 行动 |
|------|---------|------|
| GPU 比 CPU 深 10× 以上 | GPU 状态机有循环 bug | 排查 phase transition graph 是否有环 |
| GPU 比 CPU 浅 10× 以上 | CPU delta-sphere 步长参数不同 | 检查 `props.delta` 是否一致 |
| 分布形态完全不同 | RNG 或采样算法实现差异 | 排查 `sample_next_step_robust` vs GPU 版本 |
| 终止原因分布差异大 | 边界判断逻辑不同 | 排查 boundary_path 实现 |

### 5.3 开销估算

| 版本 | 原始运行时间 | 插桩开销 | 插桩后预计 |
|------|------------|---------|-----------|
| CPU (`-t 1`) | ~160min (单线程) | < 1%（仅 size_t++） | ~161min |
| GPU (`pool=4096`) | ~40min | < 1%（仅 size_t++ per path） | ~40min |

CSV 文件大小：320×320×32 = 3,276,800 行 × ~150 bytes/行 ≈ **470 MB**。
如果过大，可选择 `-R spp=8` 减至 ~120 MB，或仅采样部分像素。

**建议首次运行**: 使用 `spp=8, img=64x64`（131,072 paths）快速验证，
CSV 约 19 MB，运行时间约 3min（CPU）和 1min（GPU）。

---

## 6. 文件清单

```
optimization/path_depth_comparison/
├── README.md                              ← 本文件
├── cpu_instrumentation.patch              ← CPU 侧插桩代码（diff 格式）
├── gpu_instrumentation.patch              ← GPU 侧插桩代码（diff 格式）
├── analyze_path_depth.py                  ← Python 分析脚本
├── cpu_path_depth.csv                     ← CPU 运行结果（运行后生成）
├── gpu_pixel_trace.csv                    ← GPU 运行结果（运行后生成）
└── comparison_report.md                   ← 对比报告（分析后生成）
```

---

*文档完成: 2026-02-18*
