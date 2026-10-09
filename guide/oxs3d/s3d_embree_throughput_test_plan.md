# star-3d (Embree 4) CPU 吞吐量测试方案

**日期**: 2026-03-01  
**目标**: 建立 CPU Embree 后端的性能基线，与 oxstar-3d (OptiX) GPU 实测数据形成对比  
**参考文档**: `guide/oxs3d/rt_query_service_potential.md` §9  
**API 规范**: `stardis-cpu/star-3d/0.10/src/s3d.h`  
**后端实现**: Embree 4 (`rtcIntersect1`，无 SIMD 数据包)

---

## 0. 设计原则

1. **对齐 oxstar-3d 测试结构** — 测试 A/B/C 与 GPU 端一一对应，便于直接数值对比
2. **纯 C89 实现** — 与 star-3d 代码标准一致，使用 `s3d.h` 公共 API，不引入 C++ 依赖
3. **外部线程管理** — star-3d 不内置线程池，测试程序自行创建 OS 线程 (Windows `_beginthreadex` / POSIX `pthread_create`)
4. **计时方法**: `QueryPerformanceCounter` (Windows) / `clock_gettime(CLOCK_MONOTONIC)` (POSIX)，精度 <1μs
5. **场景构造**: 使用 `s3d.h` API 构建与 oxstar-3d 测试相同的随机 10K 三角形场景，确保几何完全等价
6. **CSV 输出格式**: 与 `perf_diag/optix_throughput_results.csv` 表头对齐，添加 `nthreads` 列

---

## 1. 测试环境

### 1.1 硬件平台

| 组件 | 规格 | 备注 |
|------|------|------|
| CPU (主测) | Intel Core i9-13900K | 8P+16E=24T, L3=36MB, PBP=125W |
| CPU (对照) | RTX 3070L 笔记本 CPU | 用于验证可移植性 |
| 内存 | DDR5-5600 2×16GB | 双通道 |
| OS | Windows 11 23H2 | VS 2022 构建 |

### 1.2 软件配置

| 组件 | 版本 |
|------|------|
| Embree | 4.x (与 stardis-cpu 构建配置一致) |
| 编译器 | MSVC 2022 (19.34+) |
| 优化级别 | `/O2 /GL` (Release) |
| C 标准 | C89 + pedantic |
| 构建系统 | CMake → VS 2022 x64 Release |

### 1.3 构建集成

测试程序作为 **stardis-cpu 项目的新 CMake 目标**，链接 star-3d 和 rsys 库:

```cmake
add_executable(s3d_throughput
    star-3d/0.10/src/test_s3d_throughput.c
)
target_link_libraries(s3d_throughput PRIVATE star-3d rsys)
```

---

## 2. 场景构造

### 2.1 标准测试场景

与 oxstar-3d 测试完全对齐，使用相同的伪随机种子生成相同几何:

| 场景 | 三角形数 | 描述 | 用途 |
|------|----------|------|------|
| `trivial` | 30 | Cornell Box | 对齐 GPU CornellBox 基准 |
| `standard` | 10,000 | 随机三角形 | 主测场景，对齐 GPU Realistic A/B/C |
| `complex` | 100,000 | 随机三角形 | 高 BVH 深度 |

### 2.2 场景构建流程 (s3d API)

```c
struct s3d_device* dev;
struct s3d_scene* scn;
struct s3d_scene_view* scnview;
struct s3d_shape* mesh;

s3d_device_create(NULL, NULL, 0, &dev);
s3d_scene_create(dev, &scn);
s3d_shape_create_mesh(dev, &mesh);
s3d_mesh_setup_indexed_vertices(mesh, ntris, get_indices_cb, nverts, attribs, nattribs, ctx);
s3d_scene_attach_shape(scn, mesh);

struct s3d_accel_struct_conf cfg = { S3D_ACCEL_STRUCT_QUALITY_HIGH, S3D_ACCEL_STRUCT_FLAG_ROBUST };
s3d_scene_view_create2(scn, S3D_TRACE, &cfg, &scnview);
```

### 2.3 射线生成

- 同 oxstar-3d: 从场景 AABB 外部均匀采样原点，朝 AABB 中心方向添加随机扰动
- 使用固定种子的 RNG (如 `rand_r` 或自定义 LCG) 确保可重复性
- 预生成射线数组，避免测量中包含 RNG 开销

---

## 3. Test A: 单线程射线计数扫描

### 3.1 目标

测量**单线程**下不同射线数量的 RT 和 CP 吞吐，建立 per-ray 延迟基线。

对齐: oxstar-3d `benchmarkSingleBatchWidth` (GPU 侧 `real_a_width`)

### 3.2 测试参数

| 参数 | 值域 | 步进 |
|------|------|------|
| 射线数 (`nrays`) | 256, 1K, 4K, 16K, 64K, 256K, 1M, 4M, 16M | ×4 |
| 线程数 | **1** (固定) |
| 场景 | `standard` (10K 三角形) |
| 迭代次数 | 5 (取中位数) |
| BVH 质量 | HIGH + ROBUST |

### 3.3 测量指标

每个 `nrays` 数据点记录:

| 列名 | 说明 | 计算方法 |
|------|------|---------|
| `nrays` | 射线数量 | 输入 |
| `wall_time_ms` | 总耗时 (ms) | QPC 测量 `trace_rays` 调用 |
| `per_ray_ns` | 单射线延迟 (ns) | `wall_time_ms * 1e6 / nrays` |
| `throughput_mrays` | 吞吐 (MRays/s) | `nrays / wall_time_ms / 1000` |
| `hit_rate` | 命中率 | `nhits / nrays` |

### 3.4 预期观察

- **小 nrays (≤4K)**: per-ray 延迟接近恒定 (~100-500ns)，受函数调用与参数校验开销主导
- **大 nrays (≥1M)**: L3 缓存压力显现，per-ray 延迟可能上升 (射线数据 > L3 容量时)
- **无 "W_sat" 概念**: CPU 不存在 GPU 式饱和宽度，吞吐应线性增长后因缓存效应平台化

### 3.5 额外实验: BVH 质量对比

对 `standard` 场景，分别用 `LOW` / `MEDIUM` / `HIGH` 构建 BVH:

| 行标 | BVH 质量 | 测量 |
|------|----------|------|
| `bvh_low` | LOW | 构建时间 + 1M 射线吞吐 |
| `bvh_med` | MEDIUM | 同上 |
| `bvh_high` | HIGH | 同上 |

目的: 量化 BVH 构建质量对追踪速度的 trade-off。

### 3.6 最近点查询 (CP) 扫描

同 Test A 结构，但使用 `s3d_scene_view_closest_point`:

| 参数 | 值域 |
|------|------|
| 查询数 | 256, 1K, 4K, 16K, 64K, 256K, 1M |
| 搜索半径 | AABB 对角线 10% (与 GPU 测试对齐) |

---

## 4. Test B: 多线程扩展性

### 4.1 目标

测量 star-3d 在**多线程并发**下的 RT 吞吐扩展性和效率。

对齐: oxstar-3d `benchmarkMultiBatchFrequency` (GPU 侧 `real_b1_async`, `real_b2_interval`)

### 4.2 线程模型

star-3d 自身**无线程池**。测试程序创建 N 个 OS 线程，每线程:
1. 获得独立的射线子集 `[istart, iend)`
2. 循环调用 `s3d_scene_view_trace_ray` (逐条)
3. 所有线程共享同一个 `s3d_scene_view*` (Embree 保证 commit 后并发安全)

```c
/* 线程工作函数 */
unsigned __stdcall thread_func(void* arg) {
    struct thread_ctx* ctx = (struct thread_ctx*)arg;
    for (size_t i = ctx->istart; i < ctx->iend; ++i) {
        s3d_scene_view_trace_ray(ctx->scnview,
            &ctx->origins[i*3], &ctx->dirs[i*3],
            &ctx->ranges[i*2], NULL, &ctx->hits[i]);
    }
    return 0;
}
```

### 4.3 Test B1: 线程计数扫描

| 参数 | 值域 | 说明 |
|------|------|------|
| 线程数 (`nthreads`) | 1, 2, 4, 8, 12, 16, 24 | 覆盖 P-core only → P+E-core |
| 总射线数 | **4,194,304** (4M, 固定) | 每线程 `4M / nthreads` 射线 |
| 场景 | `standard` (10K 三角形) |
| 迭代次数 | 5 |

测量指标:

| 列名 | 说明 |
|------|------|
| `nthreads` | 线程数 |
| `total_rays` | 总射线数 |
| `wall_time_ms` | 最慢线程完成的总耗时 |
| `throughput_mrays` | 总吞吐 (MRays/s) |
| `speedup` | 相对单线程的加速比 |
| `efficiency` | `speedup / nthreads` |

### 4.4 预期扩展曲线

```
吞吐 (MRays/s)
 │
 │              ●──────────── (24T) 饱和区
 │           ●/
 │        ●/                  L3/内存带宽瓶颈
 │     ●/
 │   ●/                       线性区 (P-core)
 │  ●
 │ ●
 │●
 └───────────────────── nthreads
  1  2  4  8 12 16 24
```

预期:
- 1→8 线程 (P-core): 接近线性 (效率 ~80-90%)
- 8→16 线程 (E-core 加入): 扩展效率下降 (~50-60% per E-core)
- 16→24 线程: 可能受 L3 带宽饱和 (BVH traversal 是内存密集操作)

### 4.5 Test B2: 核心亲和性实验

在 i9-13900K 上测试不同核心组合:

| 配置 | 线程数 | 核心类型 | 说明 |
|------|--------|---------|------|
| `P-only-8` | 8 | 仅 P-core | 最高单核性能 |
| `E-only-16` | 16 | 仅 E-core | 最高线程数量 |
| `mixed-24` | 24 | P+E 全核 | 默认调度 |
| `P-only-4HT` | 8 | 4P + HT | 超线程影响 |

使用 `SetThreadAffinityMask` 绑定线程到指定核心。

### 4.6 Test B3: 批间间隔模拟

模拟求解器 CPU 处理延迟对有效吞吐的影响 (对齐 GPU 侧 `real_b2_interval`):

| 参数 | 值域 |
|------|------|
| 线程数 | 8 (P-core only) |
| 射线数/批 | 64K |
| 批次数 | 64 |
| 批间间隔 (μs) | 0, 10, 50, 100, 500, 1000, 5000 |

每批之间插入 `Sleep(0)` 或 `SpinWait(interval_us)`，测量实际吞吐衰减。

---

## 5. Test C: 持续稳定性

### 5.1 目标

验证 CPU Embree 后端在长时间 (10 秒+) 持续负载下的吞吐稳定性和热行为。

对齐: oxstar-3d `benchmarkExtremeSustained` (GPU 侧 `real_c_sustained`)

### 5.2 测试参数

| 参数 | 值 |
|------|-----|
| 线程数 | 8 (P-core only) |
| 每批射线数 | 262,144 (256K) |
| 持续时间 | 10 秒 |
| 时间窗口 | 1 秒 |

### 5.3 执行流程

```
[主线程] 启动计时器 → 分发批次 → 等待全部线程完成 → 记录时间窗指标 → 循环直到 10s
```

每秒记录:

| 列名 | 说明 |
|------|------|
| `elapsed_s` | 已过时间 (s) |
| `cum_batches` | 累计批次数 |
| `inc_batches` | 本窗口批次数 |
| `throughput_mrays` | 本窗口吞吐 (MRays/s) |
| `cpu_temp_c` | CPU Package 温度 (如有 WMI/MSR 读取) |
| `freq_ghz` | 实时频率 (CPU-Z/HWINFO 辅助) |

### 5.4 预期行为

- **前 1-2 秒**: 频率在 Turbo Boost 上限 (5.4-5.8 GHz for P-core)
- **2-5 秒**: 若 Package 温度 >100°C，可能触发 Thermal Throttle → 频率下降
- **5-10 秒**: 稳态 — 频率和吞吐平台化
- **与 GPU 关键差异**: CPU 在持续满载时**几乎必然触发降频** (i9-13900K PBP=125W, MTP=253W, 散热器依赖)，而 4090 在 RT-only 负载下仅用 20% TDP 不会降频

### 5.5 CPU 温度与频率采集

由于 C89 代码不方便直接读 MSR，建议:
- 使用 HWiNFO64 或 Open Hardware Monitor 后台记录 (1s 间隔)
- 测试程序输出带时间戳的行标，手动对齐温度日志
- 或使用 WMI COM 调用 (仅 Windows, 需额外封装)

---

## 6. Test D: 场景复杂度扫描

### 6.1 目标

测量三角形数量对 RT 和 CP 吞吐的影响。

对齐: oxstar-3d `complexity` 和 `cp_complexity` 行。

### 6.2 测试参数

| 参数 | 值域 |
|------|------|
| 三角形数 | 1, 30, 1K, 10K, 100K, 1M |
| 射线数 | 1,048,576 (1M, 固定) |
| 线程数 | 1 (隔离 BVH 遍历效率) 和 8 (P-core 扩展) |
| BVH 质量 | HIGH + ROBUST |

### 6.3 测量指标

| 列名 | 说明 |
|------|------|
| `ntris` | 三角形数 |
| `build_time_ms` | BVH 构建时间 |
| `trace_time_ms` | 1M 射线追踪时间 |
| `throughput_mrays` | 吞吐 (MRays/s) |
| `hit_rate` | 命中率 |

---

## 7. CSV 输出格式

### 7.1 文件名

`perf_diag/s3d_embree_throughput_results.csv`

### 7.2 表头

```csv
# star-3d (Embree4) Throughput Test Results
# Platform: i9-13900K, DDR5-5600, Windows 11
# Embree version: 4.x
# Build: Release /O2 /GL
# Date: YYYY-MM-DD
#
test,nthreads,nrays_or_queries,ntris,build_ms,wall_time_ms,per_ray_ns,throughput_mrays,hit_rate,extra
```

### 7.3 行标约定

| 行标前缀 | 对齐 GPU 测试 | 说明 |
|----------|---------------|------|
| `a_width_1t` | `real_a_width` | Test A: 单线程射线扫描 |
| `a_cp_1t` | `cp_sweep` | Test A: 单线程 CP 扫描 |
| `a_bvh_quality` | — | Test A: BVH 质量对比 |
| `b1_thread_scale` | `real_b1_async` | Test B1: 线程计数扫描 |
| `b2_affinity` | — | Test B2: 核心亲和性 |
| `b3_interval` | `real_b2_interval` | Test B3: 批间间隔 |
| `c_sustained` | `real_c_sustained` | Test C: 10s 持续 |
| `d_complexity` | `complexity` | Test D: 场景复杂度 |
| `d_cp_complexity` | `cp_complexity` | Test D: CP 场景复杂度 |

---

## 8. 实现策略

### 8.1 文件位置

```
stardis-cpu/star-3d/0.10/src/test_s3d_throughput.c    # 主测试程序
stardis-cpu/star-3d/0.10/src/test_s3d_throughput.h    # 共享定义 (场景、计时)
```

### 8.2 实现优先级

| 阶段 | 内容 | 依赖 | 预估工时 |
|------|------|------|----------|
| P0 | 场景构造 + 计时框架 + CSV 输出 | star-3d 构建通过 | 2h |
| P1 | Test A (单线程 RT + CP 扫描) | P0 | 2h |
| P2 | Test D (场景复杂度) | P0 | 1h |
| P3 | Test B1 (多线程扫展) | P0 + 线程封装 | 3h |
| P4 | Test B2 (核心亲和性) | P3 | 1h |
| P5 | Test B3 (批间间隔) | P3 | 1h |
| P6 | Test C (持续稳定性) | P3 | 2h |
| P7 | BVH 质量对比 + 数据分析 | P1 | 1h |
| **总计** | | | **~13h** |

### 8.3 关键设计决策

1. **线程同步**: 使用 Windows Event 或 Barrier 控制批次边界对齐
2. **内存预分配**: 所有射线数据在测试前一次性分配，避免测量中混入 malloc 开销
3. **预热轮**: 每个测试点先跑 1 轮预热 (warm up BVH cache)，再开始正式计时
4. **结果输出**: 每测试完一个数据点立即 flush 到 CSV，避免崩溃丢失数据
5. **可复现种子**: RNG 种子 = `42` (与 oxstar-3d 测试一致)

### 8.4 命令行接口

```bash
# 完整测试
s3d_throughput.exe --all --iters 5

# 仅 Test A (快速验证)
s3d_throughput.exe --test-a --iters 3

# 仅 Test B1 (多线程扩展)
s3d_throughput.exe --test-b1 --max-threads 24 --iters 5

# 仅 Test C (持续稳定)
s3d_throughput.exe --test-c --duration 10 --threads 8

# 指定场景复杂度
s3d_throughput.exe --test-d --ntris 1,30,1000,10000,100000,1000000
```

---

## 9. 对比分析框架

测试完成后，在 `rt_query_service_potential.md` §9.3 和 §9.4 中将估算值替换为实测值，并生成以下对比图表:

### 9.1 核心对比表

```
┌───────────────────────────────────────────────────────────────────────┐
│            RT 吞吐对比 (10K 三角形, 1M 射线)                           │
├──────────────────────┬────────────────────┬───────────────────────────┤
│                      │ oxstar-3d (4090)   │ star-3d (i9-13900K, 8T)  │
├──────────────────────┼────────────────────┼───────────────────────────┤
│ 峰值吞吐 (MRays/s)   │     5,317          │     ???                   │
│ per-ray 延迟 (ns)    │     188            │     ???                   │
│ BVH 构建 (ms)        │     1.14           │     ???                   │
│ 功耗 (W)             │     ~85            │     ~125                  │
│ 能效 (MRays/s/W)     │     62.5           │     ???                   │
└──────────────────────┴────────────────────┴───────────────────────────┘
```

### 9.2 扩展性交叉点

找出 "GPU 何时胜出" 的临界点:
- 射线数 < X 时，CPU 延迟更低 (无 launch overhead)
- 射线数 > X 时，GPU 吞吐更高 (硬件并行度压制)
- 预期 X ≈ 1K-10K 射线 (GPU launch overhead ~20-60μs)

### 9.3 求解器 pool_size 决策支撑

| pool_size | GPU 吞吐 | CPU 吞吐 (8T) | GPU/CPU 比 | 推荐设备 |
|-----------|----------|---------------|-----------|---------|
| 1K | ??? | ??? | ??? | **CPU** (GPU launch overhead) |
| 32K | 746 MRays/s | ??? | ??? | ??? |
| 256K | 4,741 MRays/s | ??? | ??? | **GPU** |
| 8M | 5,146 MRays/s | ??? | ??? | **GPU** |

---

## 10. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| stardis-cpu 在 Windows 上构建不通过 | 测试无法执行 | 构建无已知问题 |
| Embree 4 DLL 缺失 | 运行时崩溃 | 作为s3d的test程序加入ctest,cmake自动处理依赖 |
| i9-13900K 散热不足导致严重降频 | Test C 数据失真 | 记录温度+频率曲线，标注降频区间 |
| L3 缓存压力在高线程数下严重 | B1 扩展性差 | 记录 LLC miss rate (perf stat)，区分带宽瓶颈与计算瓶颈 |
| `s3d_scene_view_trace_rays` 串行实现掩盖真实并行潜力 | 低估 Embree 性能 | 测试使用逐线程 `trace_ray` 而非 `trace_rays`，绕过串行循环 |
| 射线一致性 (coherence) 影响结果 | 不可比 | 使用与 oxstar-3d 完全相同的随机种子和射线分布 |

---

## 附录 A: 与 oxstar-3d 测试的对比映射

| oxstar-3d 测试 | oxstar-3d 函数 | star-3d 对应测试 | 关键差异 |
|---------------|----------------|------------------|---------|
| Realistic A (ray width sweep) | `benchmarkSingleBatchWidth` | Test A `a_width_1t` | GPU: T_submit/T_kernel 分解; CPU: 仅 wall_time |
| Realistic B1 (async dual-stream) | `benchmarkMultiBatchFrequency` B1 | Test B1 `b1_thread_scale` | GPU: 双流重叠; CPU: 多线程并行 |
| Realistic B2 (interval sweep) | `benchmarkMultiBatchFrequency` B2 | Test B3 `b3_interval` | GPU: 流间隔; CPU: 线程 sleep 间隔 |
| Realistic C (10s sustained) | `benchmarkExtremeSustained` | Test C `c_sustained` | GPU: 温度不变; CPU: 可能降频 |
| Complexity sweep | `traceBatch` × ntris | Test D `d_complexity` | 直接对齐 |
| CP sweep | — | Test A `a_cp_1t` | 直接对齐 |

## 附录 B: Embree SIMD 数据包接口 (未来扩展)

star-3d 当前仅使用 `rtcIntersect1` (标量接口)。Embree 4 提供:

| API | SIMD 宽度 | 说明 |
|-----|-----------|------|
| `rtcIntersect1` | 1 | 当前使用 |
| `rtcIntersect4` | SSE (4) | 4 射线并行 BVH 遍历 |
| `rtcIntersect8` | AVX2 (8) | 8 射线并行 |
| `rtcIntersect16` | AVX-512 (16) | 16 射线并行 (i9-13900K P-core 支持) |
| `rtcIntersect1M` | 流式 | 大批量乱序遍历，最优 L1/L2 利用 |

**如果 Test A 结果显著低于 Embree 官方基准**，应考虑在 star-3d 中集成 `rtcIntersect16` / `rtcIntersect1M` 并重新测试。这将作为 "star-3d 性能优化" 的独立任务，不在当前测试范围内。

---

*文档版本: v1.0 | 作者: GitHub Copilot | 日期: 2026-03-01*
