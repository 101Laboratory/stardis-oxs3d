# Phase 4: 验证与调优

**前置依赖**: Phase 3 (流水线主循环实现)  
**预计工时**: 2-3 天  
**目标**: 确保正确性、量化性能收益、调优参数

---

## 一、正确性验证

### 1.1 Bit-exact 一致性验证

**原理**: 双缓冲流水线只改变 CPU-GPU 的执行重叠方式，不改变计算逻辑和随机数序列。因此串行模式和流水线模式的输出必须**完全一致**。

#### 验证命令

```powershell
cd Stardis-Starter-Pack\porous

# 串行模式 baseline
$env:STARDIS_POOL_SIZE="4096"
$env:STARDIS_PIPELINE="0"
..\..\stardis-cus3d\build\bin\Release\stardis.exe `
  -M porous.txt -t 4 -V 3 `
  -R spp=4:img=256x256:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "serial_256x256x4.ht"

# 流水线模式
$env:STARDIS_PIPELINE="1"
..\..\stardis-cus3d\build\bin\Release\stardis.exe `
  -M porous.txt -t 4 -V 3 `
  -R spp=4:img=256x256:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "pipeline_256x256x4.ht"

# 二进制比较
fc /B serial_256x256x4.ht pipeline_256x256x4.ht
# 期望: FC: no differences encountered
```

#### 验证矩阵

| 场景 | 分辨率 | spp | pool_size | 预期结果 |
|------|--------|-----|-----------|---------|
| porous | 64×64 | 1 | 4096 | bit-exact |
| porous | 256×256 | 4 | 4096 | bit-exact |
| porous | 320×320 | 32 | 4096 | bit-exact |
| porous | 320×320 | 32 | 10240 | bit-exact |
| porous | 320×320 | 32 | 32768 | bit-exact |

### 1.2 ctest 回归测试

```powershell
cd stardis-cus3d\build

# 串行模式
$env:STARDIS_PIPELINE="0"
ctest -C Release --output-on-failure

# 流水线模式
$env:STARDIS_PIPELINE="1"
ctest -C Release --output-on-failure
```

两种模式下所有测试必须通过。

### 1.3 边界条件测试

| 条件 | 测试方法 | 期望行为 |
|------|---------|---------|
| **首轮 ray_count=0** | 构造无需光线的初始路径（都是conductive） | prologue skip submit, pipeline_active=0 |
| **循环中 ray_count=0** | 自然发生在 drain 尾部 | pipeline_active=0, skip submit |
| **单步完成** | spp=1, 1×1 image | prologue→1轮→epilogue |
| **pool_size=1** | STARDIS_POOL_SIZE=1 | 退化为逐路径，但仍正确 |
| **GPU 错误** | 人工注入 (如 invalid stream) | cleanup 中 wait inflight, 返回错误 |

---

## 二、性能验证

### 2.1 加速比测量

#### 基准测试命令

```powershell
cd Stardis-Starter-Pack\porous

# 基准: 320×320 spp=32, pool=4096
$env:STARDIS_POOL_SIZE="4096"

# 串行模式 (3次取平均)
$env:STARDIS_PIPELINE="0"
foreach ($i in 1..3) {
    Measure-Command {
        ..\..\stardis-cus3d\build\bin\Release\stardis.exe `
          -M porous.txt -t 4 -V 3 `
          -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
          > "serial_run$i.ht"
    } | Select-Object TotalSeconds
}

# 流水线模式 (3次取平均)
$env:STARDIS_PIPELINE="1"
foreach ($i in 1..3) {
    Measure-Command {
        ..\..\stardis-cus3d\build\bin\Release\stardis.exe `
          -M porous.txt -t 4 -V 3 `
          -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
          > "pipeline_run$i.ht"
    } | Select-Object TotalSeconds
}
```

#### 预期结果

| pool_size | 串行耗时 | 流水线耗时 | 加速比 | 通过标准 |
|----------:|--------:|-----------:|-------:|---------|
| **4096** | ~40min | **~20min** | **2.0×** | ≥ 1.5× |
| 10240 | ~46min | ~26min | 1.7× | ≥ 1.3× |
| 32768 | ~59min | ~39min | 1.5× | ≥ 1.2× |

### 2.2 Nsight Systems 时间线验证

```powershell
cd Stardis-Starter-Pack\porous
$env:STARDIS_POOL_SIZE="4096"
$env:STARDIS_PIPELINE="1"

nsys profile --trace=cuda,nvtx --output=pipeline_timeline `
    ..\..\stardis-cus3d\build\bin\Release\stardis.exe `
    -M porous.txt -t 4 -V 3 `
    -R spp=8:img=256x256:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0
```

**期望看到的 timeline**:

```
串行模式:
  GPU: ─[kernel]─────[idle]─────────[kernel]─────[idle]─────────
  CPU: ─[idle]───────[CPU work]──────[idle]──────[CPU work]──────

流水线模式:
  GPU: ─[kernel(N)]──[kernel(N+1)]──[kernel(N+2)]──[kernel(N+3)]──
  CPU: ─[CPU work ]──[CPU work   ]──[CPU work   ]──[CPU work   ]──
        (几乎无间隙)
```

**关键观察点**:
- GPU kernel 之间的 idle gap 应大幅缩小
- CPU 工作段应与 GPU kernel 重叠
- transfer_stream 上的 H2D/D2H 应与 kernel 重叠 (Phase 1.4)

### 2.3 流水线诊断指标分析

流水线模式输出的诊断日志：

```
pipeline diagnostics:
  mode=ON  pool_size=4096
  stalls=XXXXX (CPU>GPU)  waits=YYYYY (GPU>=CPU)
  stall_ratio=ZZ.Z%
  avg_wait_ms=N.NN  total_wait_s=NNN.N
  effective_overlap=NN.N%
  theoretical_speedup=N.NNx  actual_speedup=N.NNx
```

#### 指标解读

| 指标 | 健康值 | 问题值 | 含义 |
|------|--------|--------|------|
| `stall_ratio` | < 20% | > 50% | CPU 相对于 GPU 的瓶颈程度 |
| `avg_wait_ms` | > 0.5ms | < 0.01ms | GPU 需要等待的平均时间 |
| `effective_overlap` | > 70% | < 40% | CPU-GPU 时间重叠效率 |

#### 如果 stall_ratio > 50%

说明 CPU 是主要瓶颈，GPU 频繁空闲等 CPU。需要：
1. 减小 pool_size（降低 CPU 每轮工作量）
2. 优化 cascade（占 CPU 56%）
3. 考虑 OMP 并行化（见 `optimization/omp_TLDR.md`）

#### 如果 stall_ratio < 5%

说明 GPU 是瓶颈，CPU 从不等待。
1. 增大 pool_size 可提升 GPU 吞吐
2. 但 pool=4096 已平衡，增大反而让 CPU 超线性增长

---

## 三、参数调优

### 3.1 pool_size 与流水线效果的关系

基于已有数据，不同 pool_size 下的流水线理论/实际加速：

| pool_size | T_cpu | T_gpu | 理论加速 | 预计 stall_ratio |
|----------:|------:|------:|--------:|----------------:|
| **4096** | 1173s | 1188s | 2.0× | ~1% (CPU≈GPU) |
| 10240 | 1529s | 1137s | 1.7× | ~15% (CPU>GPU) |
| 32768 | 2339s | 1060s | 1.5× | ~55% (CPU>>GPU) |

**最优配置: pool_size=4096 + STARDIS_PIPELINE=1**

### 3.2 自适应 pool_size (可选后续优化)

```c
/* 运行时根据 stall_ratio 自适应调整 */
if (pool.total_steps % ADAPTIVE_INTERVAL == 0) {
    double ratio = (double)pool.pipeline_stalls
                 / (double)(pool.pipeline_stalls + pool.pipeline_waits + 1);
    
    if (ratio > 0.5 && pool.pool_size > MIN_POOL_SIZE) {
        /* CPU瓶颈 → 减小pool */
        target_pool_size = pool.pool_size * 3 / 4;
    } else if (ratio < 0.05 && pool.pool_size < MAX_POOL_SIZE) {
        /* GPU瓶颈 → 增大pool */
        target_pool_size = pool.pool_size * 5 / 4;
    }
}
```

> **Phase 4 中不实施自适应** — 仅作为后续优化记录。当前固定 pool_size=4096 已接近最优。

---

## 四、回归保护

### 4.1 CI 集成建议

```yaml
# GitHub Actions / 本地 CI 脚本
test_pipeline:
  steps:
    - name: Build Release
      run: cmake --build stardis-cus3d/build --config Release
    
    - name: Unit Test (serial)
      env: { STARDIS_PIPELINE: "0" }
      run: ctest -C Release --output-on-failure
    
    - name: Unit Test (pipeline)
      env: { STARDIS_PIPELINE: "1" }
      run: ctest -C Release --output-on-failure
    
    - name: Bit-exact comparison (small scene)
      run: |
        # 64×64 spp=1 fast comparison
        STARDIS_PIPELINE=0 stardis ... > serial.ht
        STARDIS_PIPELINE=1 stardis ... > pipeline.ht
        diff serial.ht pipeline.ht
```

### 4.2 性能回归检测

在 `solve_camera_persistent_wavefront()` 的 summary 日志中已有 elapsed time。可添加：

```c
log_info(scn->dev,
    "pipeline_summary: mode=%s pool=%lu elapsed=%.1fs "
    "stalls=%lu waits=%lu overlap=%.1f%%\n",
    use_pipeline ? "PIPELINE" : "SERIAL",
    (unsigned long)pool.pool_size,
    total_elapsed_s,
    (unsigned long)pool.pipeline_stalls,
    (unsigned long)pool.pipeline_waits,
    effective_overlap_pct);
```

---

## 五、已知限制与后续优化

### 5.1 Phase 4 完成后的已知限制

| 限制 | 影响 | 后续方案 |
|------|------|---------|
| ENC 查询仍同步 | CPU 阶段增加 enc 时间 | 异步化 enc submit/wait (Phase 1.5 已预留接口) |
| cascade 未并行化 | CPU 56% 无法进一步缩短 | OMP 或 SIMD (见 omp_TLDR.md) |
| Top-K CPU filter 在 wait 中串行 | wait 阶段不受流水线加速 | 移至 GPU kernel 内 |
| 单线程 CPU 调度 | 核利用率 ~4% | OMP cascade + distribute |
| Drain 阶段退化为串行 | 尾部 0.6% 无加速 | 可接受 |

### 5.2 后续优化路线图

```
Phase 4 完成后 (预计 ~20min for 320×320 spp=32):
│
├── 5a. cascade OMP 并行化 (T_cpu: 1173s → ~700s)
│     └── 流水线: max(700, 1188) = 1188s = 20min (无进一步收益 — GPU 是瓶颈)
│
├── 5b. GPU kernel 优化 (占 GPU ~50%, 即 ~594s)
│     ├── 寄存器压力优化 (86→64 regs → 占用率 33%→50%)
│     ├── 共享内存 BVH 缓存
│     └── 流水线: max(1173, 800_new_gpu) = 1173s = 19.5min (0.5min↓)
│
├── 5c. 传输开销优化 (GPU ~25%, 即 ~297s)
│     ├── pinned memory for H2D/D2H
│     ├── SoA 直接布局 (消除 AoS→SoA)
│     └── 流水线: max(1173, 900_new_gpu) = 1173s ≈ same
│
└── 5d. cascade + distribute CUDA 化 (激进)
      ├── 将 cascade 的纯算术步骤移入 GPU kernel
      ├── T_cpu: 1173s → 400s, T_gpu: 1188s → 1400s
      └── 流水线: max(400, 1400) = 1400s = 23min → 不可取，除非 kernel 也优化
```

**最佳后续路径**: 先完成 Phase 4 验证，确认实际加速比。如果 stall_ratio > 30%，则优先 cascade OMP。如果 stall_ratio < 10%，则优先 GPU kernel 优化。

---

## 六、实施检查点

```
Step 4.1: Bit-exact 验证
  ├─ 64×64 spp=1 快速对比
  ├─ 256×256 spp=4 中等规模对比
  ├─ 320×320 spp=32 完整对比
  └─ ✅ 所有场景 bit-exact

Step 4.2: ctest 回归
  ├─ STARDIS_PIPELINE=0 所有测试通过
  ├─ STARDIS_PIPELINE=1 所有测试通过
  └─ ✅ 零回归

Step 4.3: 性能基准
  ├─ 串行 vs 流水线耗时对比
  ├─ 加速比 ≥ 1.5× (pool=4096)
  └─ ✅ 性能验收

Step 4.4: Nsight Systems 时间线
  ├─ 确认 CPU-GPU 重叠
  ├─ 确认 kernel 间 idle gap 缩小
  └─ ✅ 时间线验收

Step 4.5: 诊断指标
  ├─ pipeline_stalls / waits 合理
  ├─ effective_overlap > 60%
  └─ ✅ 诊断验收

Step 4.6: 文档更新
  ├─ 更新 wavefront_pipeline_optimization_plan.md 实验结果
  ├─ 记录实际加速比和诊断数据
  └─ ✅ Phase 4 完成
```

---

## 七、Phase 4 交付物

1. **bit-exact 验证报告** — 所有测试场景的 fc 对比结果
2. **性能数据表** — 不同 pool_size 下的串行 vs 流水线耗时
3. **Nsight Systems 截图** — 串行 vs 流水线 GPU timeline 对比
4. **诊断日志样本** — pipeline_stalls/waits/overlap 实际值
5. **AGENTS.md 更新** — 记录正式实施状态

---

*返回总览 → [00_overview.md](00_overview.md)*
