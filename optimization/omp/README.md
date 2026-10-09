# Cascade OpenMP 并行化

## 概述

对 wavefront 求解器的 **cascade（级联）循环** 进行 OpenMP 并行化。cascade 循环在每个 wavefront 步骤中推进所有不需要光线追踪的路径状态，占 CPU 总时间的 ~65%（标准 320×320 spp=32 基准测试中约 85.7s/132.8s）。

## 原理

wavefront 求解器的主循环：
```
while (有活动路径) {
    A. compact (压缩活动路径索引)
    B. collect (收集需要光线的路径)
    C. trace   (GPU 光线追踪)          ← GPU 工作
    D. distribute (分发光线追踪结果)
    E. enc     (包壳定位)
    F. **cascade** (推进非光线步骤)    ← CPU 密集，~65% 时间
    G. harvest (收割完成的路径)
    H. refill  (填充新任务)
}
```

cascade 步骤是 embarrassingly parallel 的：
- 每条路径独立推进，无跨路径数据依赖
- `advance_one_step_no_ray()` 及其 ~30 个子步骤函数均为线程安全（无静态可变状态、无动态分配）
- 每路径 RNG 是 Counter-Based (Threefry4x64)，嵌入 `path_state`，完全独立
- 仅 6 个 `pool->` 诊断计数器写入需要线程局部累加 + 归约

## 实现细节

### 修改文件
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

### 新增函数
1. **`cascade_advance_single_path()`** — 提取的单路径 cascade 逻辑
   - 接受线程局部累加器指针（6 个计数器 + phase_count/phase_time 数组）
   - 返回 0=成功，1=致命错误

2. **`pool_cascade_non_ray_steps_compact()`** — OMP 并行化版本
   - 读取 `STARDIS_CASCADE_OMP` 环境变量（默认=1=开启，设为 "0" 关闭）
   - 使用 `scn->dev->nthreads` 控制线程数（与 CPU 版本的 `-t` 标志一致）
   - 阈值：活动路径 < 64 时退化为串行（避免 OMP 开销）
   - 调度策略：`schedule(dynamic, 64)` 负载均衡（cascade 深度变化大）
   - 致命错误通过 `#pragma omp atomic write` 信号传递
   - 归约：`#pragma omp critical` 合并线程局部计数器

### 线程控制（与 CPU 版本一致）

| 参数 | CPU 版本 | GPU 版本（本实现） |
|------|----------|-------------------|
| 线程数来源 | `scn->dev->nthreads` | `scn->dev->nthreads` |
| CLI 控制 | `-t <N>` | `-t <N>` |
| 默认值 | `SDIS_NTHREADS_DEFAULT (~0u)` → 所有核心 | 同左 |
| 初始化 | `dev->nthreads = MMIN(args->nthreads_hint, nthreads_max)` | 同左 |

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `STARDIS_CASCADE_OMP` | `1`（开启） | 设为 `0` 关闭 cascade OMP 并行化 |

启动时会在 stderr 日志中输出：
```
Cascade OMP: ENABLED, threads=4 (STARDIS_CASCADE_OMP=<unset,default=1>)
```
或
```
Cascade OMP: DISABLED, threads=1 (STARDIS_CASCADE_OMP=0)
```

## 验证脚本

### 01_bitexact_validation.ps1
**目的**：验证 OMP 并行化不影响计算结果的确定性。

```powershell
# 运行（默认 4 线程）
.\01_bitexact_validation.ps1

# 自定义线程数
$env:CASCADE_THREADS = 8; .\01_bitexact_validation.ps1
```

比较 `STARDIS_CASCADE_OMP=0`（串行）和 `STARDIS_CASCADE_OMP=1`（并行）的 `.ht` 输出，使用 `fc /B` 进行二进制比较。预期结果：bit-exact 一致。

### 02_performance_benchmark.ps1
**目的**：测量不同线程数下的性能加速比。

```powershell
# 运行（默认 320x320 spp=32，每配置 3 次）
.\02_performance_benchmark.ps1

# 自定义参数
$env:BENCH_IMG = "128x128"; $env:BENCH_SPP = 4; $env:BENCH_RUNS = 5
.\02_performance_benchmark.ps1
```

测试配置：串行基线 → OMP t=1/2/4/8，输出 CSV 和终端摘要表。

## 预期收益

- **目标加速**：17-22% 整体加速（cascade 约占 65% 时间，多核并行后该部分 ~3-4x 提速）
- **无精度损失**：bit-exact 一致（同 RNG 序列、同路径逻辑）
- **零风险回退**：`STARDIS_CASCADE_OMP=0` 即可完全回到串行

## MSVC 兼容性

- MSVC 仅支持 OpenMP 2.0
- `schedule(dynamic, 64)` ✅ 支持
- `#pragma omp atomic write` ❌ **不支持**（需 `/openmp:llvm`）→ 使用 plain write 替代（x86 aligned-int 写入天然原子）
- OMP for 循环变量必须为 `int`（非 `size_t`）✅ 已处理
- CMake 已配置 `OpenMP::OpenMP_C` 链接 ✅

### CMake OBJECT 库 OpenMP 陷阱

**根因**：CMake Visual Studio 生成器对 OBJECT 库 (`add_library(... OBJECT ...)`) 的 OpenMP imported target 传递存在缺陷。仅使用 `target_link_libraries(sdis_obj PUBLIC OpenMP::OpenMP_C)` **不会** 在 vcxproj 中生成 `<OpenMPSupport>true</OpenMPSupport>`，导致所有 `#pragma omp` 被静默忽略。

**修复**：必须额外添加 `target_compile_options(sdis_obj PRIVATE ${OpenMP_C_FLAGS})` 显式传递 `/openmp` 编译选项。

```cmake
# stardis-solver/0.16.2/CMakeLists.txt
if(OpenMP_C_FOUND)
    target_link_libraries(sdis_obj PUBLIC OpenMP::OpenMP_C)
    target_compile_options(sdis_obj PRIVATE ${OpenMP_C_FLAGS})  # 关键！
endif()
```

## 文件结构

```
optimization/omp/
├── README.md                        # 本文档
├── 01_bitexact_validation.ps1       # 正确性验证脚本
├── 02_performance_benchmark.ps1     # 性能基准脚本
└── results/                         # 运行结果输出目录
    ├── bitexact/                    # 01 脚本输出
    └── performance/                 # 02 脚本输出 + CSV
```
