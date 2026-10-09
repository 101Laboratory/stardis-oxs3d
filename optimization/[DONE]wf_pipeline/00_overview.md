# Wavefront 流水线化工程实施总览

**创建日期**: 2026-02-19  
**状态**: 待实施  
**模块**: stardis-cus3d (custar-3d + stardis-solver)

---

## 一、目标

将 persistent wavefront 求解器的 CPU-GPU 串行执行模型改为 **双缓冲流水线**，使 CPU 和 GPU 同时工作，理论加速 **2× @ pool=4096**（40min → 20min）。

## 二、现状简述

```
当前执行模型（每轮完全串行）:
  CPU: ──[compact+collect]─────────▶[空闲等GPU]──────▶[distribute+cascade+harvest]──▶
  GPU: ──[空闲等CPU]──────────────▶[trace(阻塞)]──────▶[空闲等CPU]──────────────────▶

CPU利用率 ≈ 50%, GPU利用率 ≈ 50%, 两者从不同时工作

目标执行模型（双缓冲流水线稳态）:
  GPU: ──[trace(N)]────────────────[trace(N+1)]──────────[trace(N+2)]──▶
  CPU: ──[wait(N-1)+distribute+    [wait(N)+distribute+  
          cascade+harvest+refill+    cascade+harvest+refill+
          compact+collect(N)]        compact+collect(N+1)]
```

## 三、实施阶段总览

| 阶段 | 标题 | 文档 | 涉及文件 | 预计工时 |
|:----:|------|------|---------|---------|
| **Phase 1** | GPU后端改造 | [01_phase1_gpu_backend.md](01_phase1_gpu_backend.md) | `s3d_scene_view_batch_trace.cpp`, `cus3d_trace.cu`, `cus3d_trace.h`, `s3d_scene_view_find_enclosure.cpp` | 3-4天 |
| **Phase 2** | 双缓冲基础设施 | [02_phase2_double_buffer.md](02_phase2_double_buffer.md) | `sdis_solve_persistent_wavefront.h`, `sdis_solve_persistent_wavefront.c` (pool_create/destroy) | 1-2天 |
| **Phase 3** | 主循环流水线化 | [03_phase3_pipeline_loop.md](03_phase3_pipeline_loop.md) | `sdis_solve_persistent_wavefront.c` (solve_camera主函数) | 2-3天 |
| **Phase 4** | 验证与调优 | [04_phase4_validation.md](04_phase4_validation.md) | 全部 + 测试 | 2-3天 |

**总预计工时: 8-12天**

## 四、依赖关系

```
Phase 1 ──────▶ Phase 2 ──────▶ Phase 3 ──────▶ Phase 4
 (GPU后端)        (双缓冲)        (主循环)        (验证)

Phase 1 独立可测: submit/wait 拆分后，原始串行模式仍可工作
Phase 2 依赖 Phase 1: 双缓冲需要 submit/wait 异步语义
Phase 3 依赖 Phase 2: 主循环重构需要双缓冲基础设施
Phase 4 贯穿全程: 每个 Phase 完成后都要做正确性回归
```

## 五、关键数据引用

### 实测性能数据 (320×320 spp=32, porous场景)

| pool_size | T_cpu | T_gpu | CPU% | 串行总时 | 流水线预估 | 加速比 |
|----------:|------:|------:|-----:|--------:|-----------:|-------:|
| **4096** | 1172.7s | 1187.5s | 49.7% | 40m24s | **~20min** | **2.0×** |
| 10240 | 1528.5s | 1136.8s | 57.3% | 45m40s | ~25.5min | 1.7× |
| 32768 | 2339.0s | 1059.5s | 68.8% | 58m38s | ~39min | 1.5× |

### CPU 阶段组成 (pool=4096)

| 阶段 | 时间 | 占CPU% | 函数 |
|------|-----:|-------:|------|
| cascade | 661.7s | 56.4% | `pool_cascade_non_ray_steps_compact()` |
| distribute | 254.2s | 21.7% | `pool_distribute_ray_results()` |
| collect | 149.5s | 12.7% | `pool_collect_ray_requests_bucketed()` |
| harvest+refill | 58.5s | 5.0% | `harvest_completed_paths()` + `refill_pool()` |
| compact | 48.8s | 4.2% | `compact_active_paths()` |

### GPU 内部耗时分布

| 组成 | 比例 | 说明 |
|------|------|------|
| Kernel 执行 | ~50% | BVH traversal, L2 Crossbar-bound |
| H2D/D2H 传输 | ~15% | AoS→SoA + upload + download |
| cudaStreamSynchronize | ~10% | 3次同步阻塞 |
| CPU 后处理 (Top-K filter) | ~20% | 串行遍历候选 |
| cudaMalloc/Free | ~5% | 每次 d_results + BLAS 分配释放 |

## 六、文件索引

### 核心源文件

| 文件 | 路径 | 行数 | 角色 |
|------|------|------|------|
| wavefront主循环 | `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | ~1753 | CPU调度、pool管理、所有阶段 |
| wavefront头文件 | `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h` | ~275 | pool结构体定义 |
| batch trace中间层 | `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp` | ~513 | AoS→SoA + Top-K filter + retrace |
| GPU trace kernel | `stardis-cus3d/custar-3d/0.10/src/cus3d_trace.cu` | ~1400 | CUDA kernel + host API |
| trace API头 | `stardis-cus3d/custar-3d/0.10/src/cus3d_trace.h` | ~88 | ray_batch, hit_result 定义 |
| enclosure batch | `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_find_enclosure.cpp` | ~283 | GPU包壳查询 |
| step函数 | `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` | ~4954 | 纯CPU step函数（不修改） |
| step类型定义 | `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_types.h` | ~202 | path_phase枚举 |

### 参考文档

| 文档 | 路径 |
|------|------|
| 现有优化总方案 | `optimization/wavefront_pipeline_optimization_plan.md` |
| 实验3结果 (cascade热点) | `optimization/Experiment3 Results.md` |
| 实验4结果 (GPU ncu) | `optimization/Experiment4 Results.md` |
| OMP可行性分析 | `optimization/omp_TLDR.md` |

## 七、风险与缓解

| 风险 | 影响 | 概率 | 缓解 |
|------|------|------|------|
| CPU瓶颈远超GPU (>70/30) | 加速 <1.43× | 中 | pool=4096时已49.7/50.3平衡 |
| submit/wait 拆分引入bug | 结果不一致 | 中 | bit-exact A/B对比验证 |
| ENC查询频繁成新瓶颈 | CPU阶段加长 | 低-中 | 预留async接口 |
| 双缓冲内存增加 ~38MB | VRAM压力 | 极低 | RTX 4090 24GB |
| Drain阶段无重叠 | 尾部效率下降 | 低 | pool=4096时drain仅0.6% |

## 八、验收标准

1. **正确性**: `STARDIS_PIPELINE=0` (串行) 和 `STARDIS_PIPELINE=1` (流水线) 输出 **bit-exact 一致**
2. **性能**: pool=4096, 320×320 spp=32 场景下 **≥1.5× 加速**
3. **回归**: 所有现有 ctest 测试通过
4. **诊断**: 流水线模式输出 overlap 比例、pipeline_stalls 计数等关键指标
5. **兼容**: 默认关闭流水线 (STARDIS_PIPELINE=0)，不影响现有用户

---

*Phase 1 详见 → [01_phase1_gpu_backend.md](01_phase1_gpu_backend.md)*
