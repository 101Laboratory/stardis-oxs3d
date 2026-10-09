# CSV 数据输出改造指南 — 架构一致性证明用

**生成时间**: 2026-02-28  
**关联文档**: [../numerical_correctness_test_checklist.md](../numerical_correctness_test_checklist.md)  
**目标**: 为最小完备覆盖集的 10 个 GPU wavefront 测试和 10 个 CPU depth-first 测试添加 CSV 输出能力，用于画对比曲线图证明架构一致性

---

## 一、概述

### 1.1 背景

为完备证明 CPU depth-first（Embree 后端）和 GPU wavefront（OptiX 后端）两种架构的数值一致性，需要从最小完备覆盖集（10 个测试，覆盖 37/38 个特征维度）中导出结构化数据点，用于绘制对比曲线图。

### 1.2 两个对比项目

| 项目 | 目录 | 光追后端 | 求解器架构 | 求解 API |
|------|------|---------|-----------|---------|
| **stardis-cpu** | `stardis-cpu/` | Embree 4（CPU） | depth-first | `sdis_solve_probe()` |
| **stardis-cus3d** | `stardis-cus3d/` | OptiX（oxstar-3d, GPU） | wavefront 状态机 | `sdis_solve_wavefront_probe()` |

> **注意**: stardis-cus3d 的 3D 光追后端是 OptiX（通过 oxstar-3d 封装），而非 cuBQL（通过 custar-3d 封装）。cuBQL 仅用于早期原型验证。

### 1.3 设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| CSV 触发方式 | **环境变量 `SDIS_CSV_DIR`** | 无需重编译即可在 ctest 中启用；未设置时零开销 |
| CPU 基准来源 | **stardis-cpu**（Embree 后端） | 做对比实验须在原始实现上测试 |
| CPU 探针位置 | **保留随机探测** | CSV 中记录实际坐标，画图时作为散点 |
| WoS 变体输出 | **同一 CSV 文件，`diff_algo` 列区分** | 每个探测点输出 DS + WoS 两行 |

---

## 二、统一 CSV 格式

### 2.1 列定义

```
test_id,sub_id,solver,diff_algo,probe_x,probe_y,probe_z,time,picard_order,nrealisations,E,SE,ref,sigma,pass
```

| 列 | 类型 | 说明 |
|---|---|---|
| `test_id` | string | 测试标识：`A3`, `A7`, `B3`, `B5`, `C3`, `D2`, `E5`, `F1`, `F2`, `I1` |
| `sub_id` | string | 子配置标识（如 A3 的 `R=0.01`，C3 的 `picard1_constTref`，E5 的 `fluid`/`solid`，其余为 `default`） |
| `solver` | string | `cpu_df`（CPU depth-first, Embree）或 `gpu_wf`（GPU wavefront, OptiX） |
| `diff_algo` | string | `DS`（Delta-Sphere）或 `WoS`（Walk-on-Spheres） |
| `probe_x` | double | 探测 X 坐标 |
| `probe_y` | double | 探测 Y 坐标 |
| `probe_z` | double | 探测 Z 坐标 |
| `time` | double | 时间参数（稳态为 `inf`，瞬态为实际 t [s]） |
| `picard_order` | int | Picard 阶数 |
| `nrealisations` | int | 采样数 |
| `E` | double | MC 估计值 [K] |
| `SE` | double | 标准误差 [K] |
| `ref` | double | 参考值 [K] |
| `sigma` | double | `|E - ref| / SE`（SE=0 时为 0） |
| `pass` | int | 0 或 1 |

### 2.2 示例

```csv
test_id,sub_id,solver,diff_algo,probe_x,probe_y,probe_z,time,picard_order,nrealisations,E,SE,ref,sigma,pass
A3,R=0.01,gpu_wf,DS,0.4875,2.0,2.0,inf,1,10000,49.756,0.21,49.85,0.45,1
A3,R=0.01,gpu_wf,WoS,0.4875,2.0,2.0,inf,1,10000,49.812,0.19,49.85,0.20,1
E5,fluid,gpu_wf,DS,1.5,1.5,1.5,1000,1,10000,301.23,0.15,301.5,1.80,1
E5,fluid,gpu_wf,WoS,1.5,1.5,1.5,1000,1,10000,301.18,0.16,301.5,2.00,1
```

---

## 三、最小完备覆盖集（10 个测试）

### 3.1 总览

| # | 测试 ID | 物理场景 | 唯一覆盖维度 | 数据点数 |
|---|---------|---------|-------------|---------|
| 1 | **A3** | 双材料 + 接触热阻 | F10(热阻), SM3(ss重注入) | 5R × 8探针 = 40 |
| 2 | **A7** | 内嵌透明对象 | F14(透明界面) | 1 |
| 3 | **B3** | 边界通量内部剖面 | F17(CF/RF/TF), BC2(Neumann) | 5 |
| 4 | **B5** | 三线性场批量探针 | M2(批量API), F12(非凸) | 10 |
| 5 | **C3** | Picard 多阶数 | F6(PicardN), SM5/SM11(递归栈) | 6 配置 |
| 6 | **D2** | 非均匀对流 | F4(非均匀h) | 5 |
| 7 | **E5** | 瞬态 + 大气多介质 | F13(大气), T2(瞬态) | 11时间 × 2探针 = 22 |
| 8 | **F1** | 外部通量（太阳源） | F7(太阳源) | 2 |
| 9 | **F2** | 漫射辐照 | F8(漫射源) | 1 |
| 10 | **I1** | 嵌套体积功率 | F15(嵌套包壳) | 8 |

**GPU CSV 总行数**: (40+1+5+10+6+5+22+2+1+8) × 2 algo = **200 行**

### 3.2 结构性覆盖缺口

唯一未覆盖维度：**SM8（WoS 导热路径）** — 当前 22 个 `test_sdis_wf_*.c` 均使用 `SDIS_DIFFUSION_DELTA_SPHERE` 进入导热状态机。WoS 路径 (`PATH_CND_WOS_*`) 仅通过 `test_sdis_b4_m9_wos` 的 T9.8/T9.9 端到端测试覆盖。

**建议**: 新增 `test_sdis_wf_wos_basic.c`，复用 WF-A1 场景但设 `diff_algo = SDIS_DIFFUSION_WOS`，使覆盖率达到 38/38。

---

## 四、实施步骤

### Step 1: 创建共享 CSV 工具头文件

在两个项目各放一份相同的 `test_sdis_csv_utils.h`：
- `stardis-cus3d/stardis-solver/0.16.2/src/test_sdis_csv_utils.h`
- `stardis-cpu/stardis-solver/0.16.2/src/test_sdis_csv_utils.h`

```c
/* test_sdis_csv_utils.h — CSV data output for architecture consistency proof.
 *
 * Usage:
 *   FILE* csv = csv_open("A3");          // reads SDIS_CSV_DIR env var
 *   csv_row(csv, "A3", "R=0.01", "gpu_wf", "DS",
 *           x, y, z, INFINITY, 1, 10000, mc.E, mc.SE, ref);
 *   csv_close(csv);
 *
 * If SDIS_CSV_DIR is not set, csv_open returns NULL and csv_row is a no-op.
 * Pure C89, header-only, static functions. */

#ifndef TEST_SDIS_CSV_UTILS_H
#define TEST_SDIS_CSV_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define CSV_HEADER \
  "test_id,sub_id,solver,diff_algo,probe_x,probe_y,probe_z," \
  "time,picard_order,nrealisations,E,SE,ref,sigma,pass\n"

static FILE*
csv_open(const char* test_id)
{
  const char* dir = getenv("SDIS_CSV_DIR");
  char path[512];
  FILE* fp;
  if(!dir || !dir[0]) return NULL;

  /* Build path: {SDIS_CSV_DIR}/{test_id}.csv */
#if defined(_MSC_VER)
  _snprintf(path, sizeof(path), "%s\\%s.csv", dir, test_id);
#else
  snprintf(path, sizeof(path), "%s/%s.csv", dir, test_id);
#endif
  path[sizeof(path) - 1] = '\0';

  fp = fopen(path, "w");
  if(!fp) return NULL;
  fprintf(fp, CSV_HEADER);
  fflush(fp);
  return fp;
}

static void
csv_row(
  FILE*       fp,
  const char* test_id,
  const char* sub_id,
  const char* solver,
  const char* diff_algo,
  double      px, double py, double pz,
  double      time_val,
  int         picard_order,
  int         nrealisations,
  double      E,
  double      SE,
  double      ref)
{
  double sigma_val;
  int pass_val;
  if(!fp) return;

  sigma_val = (SE > 1e-15) ? fabs(E - ref) / SE : 0.0;
  pass_val  = (SE > 1e-15) ? (sigma_val <= 3.0) : (fabs(E - ref) < 1e-10);

  fprintf(fp,
    "%s,%s,%s,%s,%.8g,%.8g,%.8g,%.8g,%d,%d,%.10g,%.6e,%.10g,%.4f,%d\n",
    test_id, sub_id, solver, diff_algo,
    px, py, pz, time_val,
    picard_order, nrealisations,
    E, SE, ref, sigma_val, pass_val);
  fflush(fp);
}

static void
csv_close(FILE* fp)
{
  if(fp) fclose(fp);
}

#endif /* TEST_SDIS_CSV_UTILS_H */
```

**要点**:
- 纯 C89 兼容（`_snprintf` for MSVC, `snprintf` for others）
- Header-only，`static` 函数避免链接冲突
- `csv_open` 返回 NULL 时 `csv_row` 为 no-op，零性能开销
- 每次 `csv_row` 后 `fflush` 确保 ctest 超时杀进程时不丢数据

### Step 2: 修改 10 个 GPU WF 测试（stardis-cus3d）

每个测试添加以下修改模式：

```c
#include "test_sdis_csv_utils.h"

/* 在 main() 或测试函数入口 */
FILE* csv = csv_open("A3");  /* 测试 ID */

/* 在每个探测点求解后，紧接 sdis_estimator_get_temperature() */
csv_row(csv, "A3", sub_id, "gpu_wf",
        args.diff_algo == SDIS_DIFFUSION_WOS ? "WoS" : "DS",
        args.position[0], args.position[1], args.position[2],
        args.time_range[0], (int)args.picard_order,
        (int)args.nrealisations, mc_wf.E, mc_wf.SE, T_ref);

/* 追加 WoS/DS 互补变体 */
{
  enum sdis_diffusion_algorithm alt_algo =
    (args.diff_algo == SDIS_DIFFUSION_WOS)
      ? SDIS_DIFFUSION_DELTA_SPHERE
      : SDIS_DIFFUSION_WOS;
  struct sdis_solve_probe_args args2 = args;
  struct sdis_estimator* est2 = NULL;
  struct sdis_mc mc2;
  args2.diff_algo = alt_algo;
  OK(sdis_solve_wavefront_probe(scn, &args2, &est2));
  OK(sdis_estimator_get_temperature(est2, &mc2));
  csv_row(csv, "A3", sub_id, "gpu_wf",
          alt_algo == SDIS_DIFFUSION_WOS ? "WoS" : "DS",
          args2.position[0], args2.position[1], args2.position[2],
          args2.time_range[0], (int)args2.picard_order,
          (int)args2.nrealisations, mc2.E, mc2.SE, T_ref);
  OK(sdis_estimator_ref_put(est2));
}

/* 在测试结束时 */
csv_close(csv);
```

### 逐测试修改详情

| 测试 | 文件 | 修改要点 | 数据行数 |
|------|------|---------|---------|
| **A3** | `test_sdis_wf_a3_contact_resistance.c` | 5 R值 × 8 探针循环中插入；`sub_id` = `"R=%.2g"` 格式化 R 值 | 80 |
| **A7** | `test_sdis_wf_a7_solve_probe3.c` | 单点求解后插入 | 2 |
| **B3** | `test_sdis_wf_b3_boundary_flux.c` | 5 探针 x 轴扫描处插入 | 10 |
| **B5** | `test_sdis_wf_b5_probe_list.c` | 10 探针循环处插入 | 20 |
| **C3** | `test_sdis_wf_c3_picard_multi.c` | 6 配置循环处；已混合 DS/WoS，互补变体各取反 | 12 |
| **D2** | `test_sdis_wf_d2_convection_nonuniform.c` | 5 探针循环处插入 | 10 |
| **E5** | `test_sdis_wf_e5_unsteady_atm.c` | 11 时间 × 2 探针类型循环处；`sub_id` = `"fluid"` / `"solid"` | 44 |
| **F1** | `test_sdis_wf_f1_external_flux.c` | 2 子测试（Lambertian/Specular）处插入 | 4 |
| **F2** | `test_sdis_wf_f2_diffuse_radiance.c` | 单点处插入 | 2 |
| **I1** | `test_sdis_wf_i1_volumic_power2.c` | 8 探针 y 轴扫描处插入；`sub_id` = `"y=%.2f"` | 16 |

### Step 3: 修改 10 个 CPU 测试（stardis-cpu）

CPU 测试使用 `sdis_solve_probe()`（Embree depth-first），保留随机探测位置，CSV 记录实际坐标。

修改模式：

```c
#include "test_sdis_csv_utils.h"

/* 在 main 入口 */
FILE* csv = csv_open("A3");

/* 在现有 printf("...temperature...") 后紧接 */
csv_row(csv, "A3", sub_id, "cpu_df", "DS",
        solve_args.position[0], solve_args.position[1], solve_args.position[2],
        solve_args.time_range[0], 1, N, T.E, T.SE, ref);

/* 测试结束时 */
csv_close(csv);
```

**关键差异**:
- `solver` 列固定为 `"cpu_df"`
- `diff_algo` 列固定为 `"DS"`（CPU 默认算法）
- 不追加 WoS 互补变体（CPU 版 WoS 不在对比范围内）
- 随机探测位置通过 CSV 的 `probe_x/y/z` 列如实记录

### 逐测试修改详情

| 测试 | stardis-cpu 文件 | 探针位置 | API | 特殊注意 |
|------|-----------------|---------|-----|---------|
| **A3** | `test_sdis_contact_resistance.c` | 随机 | `sdis_solve_probe` | 2D+3D 各运行一次，`sub_id` 区分 `"3D_R=%.2g"` / `"2D_R=%.2g"` |
| **A7** | `test_sdis_solve_probe3.c` | 确定性 (0.5,0.5,0.5) | `sdis_solve_probe` | |
| **B3** | `test_sdis_solve_boundary_flux.c` | 确定性 (prim,uv) | `sdis_solve_probe_boundary_flux` | ⚠️ 边界探针 API 不同于 WF 版；仅记录温度分量 |
| **B5** | `test_sdis_solve_probe_list.c` | 随机 | `sdis_solve_probe_list` | 记录每个探针实际坐标 |
| **C3** | `test_sdis_picard.c` | 确定性 (0.05,0,0) | `sdis_solve_probe` | `diff_algo` 按配置：已有参数 |
| **D2** | `test_sdis_convection_non_uniform.c` | 确定性 | `sdis_solve_probe` | 有时间序列（瞬态+稳态） |
| **E5** | `test_sdis_unsteady_atm.c` | 半随机 (iprim 确定, uv 随机) | `sdis_solve_probe_boundary` | 记录实际 (x,y,z) from position |
| **F1** | `test_sdis_external_flux.c` | 确定性 | `sdis_solve_probe` | ⚠️ 未注册 CMake，需先补注册 |
| **F2** | `test_sdis_external_flux_with_diffuse_radiance.c` | 确定性 | `sdis_solve_boundary` | ⚠️ 未注册 CMake，需先补注册 |
| **I1** | `test_sdis_volumic_power2.c` | 确定性 | `sdis_solve_probe` | |

### Step 4: 补注册 CPU CMake 测试

5 个测试文件存在于 stardis-cpu 但未在 CMakeLists.txt 中注册：

```cmake
# stardis-cpu/stardis-solver/0.16.2/CMakeLists.txt 需补充:
# SDIS_REGULAR_TESTS += 
#   test_sdis_solve_probe3
#   test_sdis_solve_boundary_flux
#   test_sdis_solve_probe_list
#   test_sdis_external_flux
#   test_sdis_external_flux_with_diffuse_radiance
```

---

## 五、运行命令

### GPU 侧（stardis-cus3d, OptiX 后端）

```powershell
cd stardis-cus3d/build
cmake --build . --config Release > build.log 2>&1

$env:SDIS_CSV_DIR = "D:\Works\Projects\Stardis-GPU\csv_output\gpu"
New-Item -ItemType Directory -Force -Path $env:SDIS_CSV_DIR

ctest -C Release -R "test_sdis_wf_(a3|a7|b3|b5|c3|d2|e5|f1|f2|i1)" -V --timeout 300
```

### CPU 侧（stardis-cpu, Embree 后端）

```powershell
cd stardis-cpu/build
cmake --build . --config Release > build.log 2>&1

$env:SDIS_CSV_DIR = "D:\Works\Projects\Stardis-GPU\csv_output\cpu"
New-Item -ItemType Directory -Force -Path $env:SDIS_CSV_DIR

ctest -C Release -R "test_sdis_(contact_resistance|solve_probe3|solve_boundary_flux|solve_probe_list|picard|convection_non_uniform|unsteady_atm|external_flux|volumic_power2)" -V --timeout 600
```

### 输出文件

```
csv_output/
├── gpu/
│   ├── A3.csv     # 80 行 (5R × 8探针 × 2algo)
│   ├── A7.csv     # 2 行
│   ├── B3.csv     # 10 行
│   ├── B5.csv     # 20 行
│   ├── C3.csv     # 12 行
│   ├── D2.csv     # 10 行
│   ├── E5.csv     # 44 行
│   ├── F1.csv     # 4 行
│   ├── F2.csv     # 2 行
│   └── I1.csv     # 16 行
└── cpu/
    ├── A3.csv     # ~40 行 (随机位置)
    ├── A7.csv     # 1 行
    ├── B3.csv     # ~5 行
    ├── B5.csv     # ~10 行 (随机位置)
    ├── C3.csv     # ~6 行
    ├── D2.csv     # ~5 行
    ├── E5.csv     # ~22 行 (半随机)
    ├── F1.csv     # ~2 行
    ├── F2.csv     # ~1 行
    └── I1.csv     # ~8 行
```

---

## 六、画图数据合并方案

```python
import pandas as pd
import glob

# 加载所有 CSV
gpu_dfs = {f.stem: pd.read_csv(f) for f in Path("csv_output/gpu").glob("*.csv")}
cpu_dfs = {f.stem: pd.read_csv(f) for f in Path("csv_output/cpu").glob("*.csv")}

# 合并同一测试的 GPU + CPU 数据
for tid in gpu_dfs:
    combined = pd.concat([gpu_dfs[tid], cpu_dfs.get(tid, pd.DataFrame())])
    combined.to_csv(f"csv_output/combined_{tid}.csv", index=False)
```

### 推荐曲线图组合

用于论文/报告展示的最优 3 张图（覆盖稳态空间/瞬态时间/多物理耦合）：

1. **A3 — T(x) 多 R 值空间分布**  
   X: `probe_x`, Y: `E`, 分组: `sub_id` (R 值)  
   3 条线: 解析解(实线), CPU(圆圈), GPU-DS(三角), GPU-WoS(方块)

2. **E5 — T(t) 双探针瞬态响应**  
   X: `time`, Y: `E`, 分组: `sub_id` (fluid/solid)  
   22 数据点 × 2 探针类型 × 3 来源

3. **C3 — Picard 多阶/多算法柱状图**  
   X: `sub_id` (6 配置), Y: `E`, 分组: `solver` + `diff_algo`  
   展示 DS/WoS 在不同 Picard 阶数下的一致性

---

## 七、验证标准

1. **编译**: 无 `SDIS_CSV_DIR` 时行为完全不变（`csv_open` 返回 NULL，零开销）
2. **ctest**: 原有 PASS/FAIL 判定不受影响
3. **CSV 格式**: 所有文件列数一致（15 列），可被 `pandas.read_csv()` 正确解析
4. **WoS 变体**: GPU CSV 中每个探测点有 DS 和 WoS 两行
5. **数据完整性**: 每次 `csv_row` 后 `fflush`，ctest 超时杀进程不丢已写数据

---

*文档更新: 2026-02-28 | 关联: [../numerical_correctness_test_checklist.md](../numerical_correctness_test_checklist.md)*
