# 00 — GPU-Driven Stardis 总体方案

## 1. 背景与动机

### 1.1 当前架构（merge-phase 基线）

当前 stardis-oxs3d-merge-phase 采用 **CPU 波前调度 + GPU 光追** 的混合架构：

```
┌──────────────────────────────────────────────────────┐
│ CPU：波前调度器 (5541 LOC)                            │
│  ┌──────────────────────────────────────────────┐    │
│  │ merged_pass (OMP 并行)                        │    │
│  │  Phase A: 分发上一轮 GPU 结果 → step_*()      │    │
│  │  Phase B: 级联推进（无需光线的纯计算步骤）      │    │
│  │  Phase C: 收集新的光线/enc/cp 请求             │    │
│  │  Phase D: 收割完成路径 + 回填                  │    │
│  └──────────────────────────────────────────────┘    │
│                        ↕ PCIe                         │
│  ┌──────────────────────────────────────────────┐    │
│  │ GPU：OptiX 光追 + 最近点查询                   │    │
│  │  • batch_trace (H2D → Kernel → D2H)           │    │
│  │  • batch_enc_locate (BVH 点在哪个 enclosure)   │    │
│  │  • batch_cp (最近点距离查询)                    │    │
│  └──────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────┘
```

**瓶颈分析（来自 profiling）**：

| 环节 | 耗时占比 | 说明 |
|------|---------|------|
| CPU merged_pass 计算 | ~40–50% | step 函数纯计算、Green 函数累积、路径管理 |
| PCIe 传输 | ~5–10% | H2D/D2H 双向传输（已用 pinned buffer + 双流优化） |
| GPU 光追 kernel | ~20–30% | OptiX 光追 batch + post-process |
| CPU post-process | ~10–15% | HitResult→s3d_hit 转换、filter 应用、UV fixup |
| 同步等待 | ~5–10% | CPU 等 GPU 完成、GPU 等 CPU 收集请求 |

**核心问题**：每一轮迭代都有 CPU↔GPU 同步点。即使用 O13 异步提交 + 双缓冲流水线，CPU 侧的 40ms merged_pass 仍是不可压缩的串行环节。

### 1.2 目标架构

```
┌──────────────────────────────────────────────────────┐
│ GPU：全 GPU 波前求解器                                │
│  ┌──────────────────────────────────────────────┐    │
│  │ Persistent Kernel (单池调度)                    │    │
│  │  1. OptiX 光追 batch (inline, 无需 PCIe)       │    │
│  │  2. 分发光追结果 → device step_*()             │    │
│  │  3. 级联推进（纯计算 step, 全 GPU 执行）        │    │
│  │  4. 收集新光线请求 → 回到步骤 1                 │    │
│  │  5. 完成路径: 原子累积到 device 估算器           │    │
│  │  6. 回填新任务直到全部完成                       │    │
│  └──────────────────────────────────────────────┘    │
│                                                       │
│  只读场景数据：                                        │
│  • 材料参数表 (constant/texture memory)               │
│  • 网格几何 (OptiX GAS/IAS, 已在 GPU)                │
│  • enclosure 映射 (device 哈希表/数组)                │
│  • H 函数表 (constant memory)                        │
│                                                       │
│  读写路径状态：                                        │
│  • path_state[] (device global memory, SoA)           │
│  • RNG state (per-lane Threefry CBRNG)                │
│  • Green 函数累积器 (固定大小 device buffer)           │
│  • 估算器 (per-warp 局部累积 + 全局归约)              │
└──────────────────────────────────────────────────────┘
           ↑ 单次 H2D（场景 + 初始化）
           ↓ 单次 D2H（最终结果归约）
```

### 1.3 预期收益

| 指标 | 当前 | GPU-driven 预期 | 依据 |
|------|------|----------------|------|
| PCIe 传输 | 每轮 2×batch_size×64B | 仅初始化和结果 | 消除中间传输 |
| CPU↔GPU 同步 | 每轮 2 次事件等待 | 0 次（仅最终同步） | 消除迭代内同步 |
| CPU post-process | 5–10ms/M rays | 0ms（GPU inline） | 消除 CPU 后处理 |
| 路径状态传输 | 0（已在 CPU） | 0（全在 GPU） | 无迁移开销 |
| 有效 GPU 利用率 | ~60–70%（受 CPU 制约） | ~85–95% | 消除 GPU 空闲等待 |

## 2. 约束条件

### 2.1 不变量

1. **数值一致性**: 双精度计算，3σ 统计一致性验证（对比 CPU 基线）
2. **确定性 RNG**: 保持 per-(pixel,sample,seed) Threefry CBRNG 确定性
3. **算法正确性**: 所有物理路径（radiative, conductive, convective, boundary）行为不变
4. **OptiX 依赖**: 继续使用 OptiX 7+ 光追（GAS/IAS 已在 GPU）

### 2.2 设计约束

1. **单设备**: 不考虑多 GPU / NVLink，简化调度
2. **单池**: 不需要双流 ping-pong（消除 merge-phase 的 dual-view 复杂度）
3. **C89/C++ 混合**: solver 核心是 C89，step 函数需通过 CUDA `__device__` 入口点桥接
4. **OptiX + CUDA 协同**: OptiX kernel 和自定义 CUDA kernel 共享 device 内存

### 2.3 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| 路径发散（warp divergence） | GPU 利用率低 | 按 path_phase 分组排序（bucketed dispatch） |
| PicardN 递归栈溢出 | GPU 本地内存压力 | 固定深度栈 + overflow 回退到 host 子求解 |
| 内存压力 | path_state ~2KB/path × 池大小 | SoA 布局 + 分级存储（hot/cold） |
| Green 函数动态数组 | GPU 无法高效 malloc | 预分配固定大小 + 溢出写回 host |
| 调试困难 | CUDA kernel 调试 | 单线程 CPU 模拟模式 + validation 框架 |

## 3. 总体架构

### 3.1 三层结构

```
Layer 3: Host Controller (CPU)
  ├── 场景加载、mesh上传、OptiX BVH 构建
  ├── 任务生成（probe/camera/batch 模式）
  ├── Launch persistent kernel
  ├── 等待完成 + 结果 D2H
  └── 后处理、输出

Layer 2: GPU Scheduler Kernel (persistent, grid-wide)
  ├── 任务队列管理（atomic dequeue + refill）
  ├── path_state 生命周期（init → advance → done → recycle）
  ├── 光追请求收集 + OptiX 启动（或 inline 光追）
  ├── 结果分发 + step cascade
  └── 估算器累积 + 最终归约

Layer 1: Device Step Functions (__device__)
  ├── step_init, step_radiative_trace, step_boundary, ...
  ├── 调用 device-side: brdf, green, interface, medium, rng, ...
  └── 返回 {next_phase, ray_request[], done_flag}
```

### 3.2 执行模型

不采用全 persistent thread 方案（fragile, 占用率低），也不采用 mega-kernel（excessive register pressure），而是采用 **多轮 kernel launch** 方案：

```
Host loop:
  while (active_paths > 0) {
    // 1. 光追 kernel（OptiX pipeline launch）
    optixLaunch(trace_pipeline, stream, rays, hits, n_active_rays);

    // 2. 求解器推进 kernel（自定义 CUDA kernel）
    solver_advance_kernel<<<grid, block, 0, stream>>>(
        path_states, hits, scene_data, estimators, ...);

    // 3. compact + refill kernel
    compact_and_refill_kernel<<<...>>>(
        path_states, task_queue, active_indices, ...);
  }

  // 4. 归约 kernel
  reduce_estimators_kernel<<<...>>>(estimators, output);

  // 5. D2H 最终结果
  cudaMemcpy(host_result, device_output, ...);
```

**关键优化**：OptiX launch 和 solver kernel 之间无需 D2H/H2D，都操作同一片 device memory。

### 3.3 与现有架构对比

| 方面 | merge-phase (当前) | GPU-driven (目标) |
|------|-------------------|------------------|
| 路径状态 | CPU `path_state[]` (malloc) | GPU device global (SoA, 预分配) |
| 调度 | CPU merged_pass (OMP) | GPU solver kernel (CUDA grid) |
| 光追集成 | 异步 batch (H2D→Kernel→D2H) | 同步 inline (device→device) |
| Step 函数 | CPU 函数调用 | `__device__` 函数调用 |
| 结果累积 | CPU accum (mutex) | GPU atomicAdd / warp 归约 |
| 场景数据 | CPU hash table + pointer | GPU 扁平数组 + constant memory |
| RNG | CPU Threefry (嵌入 path_state) | GPU Threefry (嵌入 device path_state) |
| 双缓冲 | 双 view ping-pong | 不需要（单池） |

## 4. 分阶段实施

### Phase 0: 基础设施（预备）
- GPU 场景数据扁平化 + 上传
- device path_state SoA 布局设计
- device RNG (Threefry CBRNG) 验证
- 单元素 CPU↔GPU 数据一致性测试框架

### Phase 1: 核心路径 GPU 化
- `step_init` + `step_radiative_trace` → `__device__`
- BRDF 采样、interface/medium 查询 → `__device__`
- 基本 solver_advance kernel + OptiX inline 光追
- 单 probe 端到端验证（3σ 一致性）

### Phase 2: 全物理路径
- Boundary 步骤 (M3/M5/M7/M8) → `__device__`
- Conductive 步骤 (M4 delta-sphere, M9 WoS) → `__device__`
- Convective 步骤 (M6) → `__device__`
- Enclosure 查询 (M1/M10) → device inline
- Green 函数固定大小累积器

### Phase 3: 性能优化
- 按 path_phase bucketed 分组（减少 warp divergence）
- Estimator warp-level 归约
- Stream compaction kernel 优化
- 多 probe batch 模式

### Phase 4: 集成与验证
- Camera 模式集成（像素 tile + 进度报告）
- 全测试套件（60+ wavefront 测试）3σ 一致性
- 性能基准对比

---

*下一步详细设计*: → [01_SCHEDULING.md](01_SCHEDULING.md)
