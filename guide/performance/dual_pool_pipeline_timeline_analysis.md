# 双池交替求解器（Dual-Pool Pipeline）执行时序分析

**生成时间**: 2026-03-03  
**数据来源**: `Stardis-Starter-Pack/porous/log_stardis-oxs3d.txt`（OptiX 后端完整 IR 渲染）  
**阶段**: Phase B-3 Persistent Wavefront, dual-buffer pipeline

---

## 一、运行配置

| 参数 | 值 |
|------|------|
| 图像分辨率 | 512×512 |
| SPP | 128 |
| 总任务数 | 33,554,432 |
| 池大小(pool_size) | 24,576 paths |
| 半池(half/view_size) | 12,288 paths |
| views[0].capacity | 24,576 |
| views[1].capacity | 12,288 |
| OMP cascade 线程 | 32 |
| 场景三角形 | 13,010 |
| 总步数 | 2,806,950 |
| 总射线 | 132,100,910,238 (132.1B) |
| 总耗时 | 1h 24m 31s (5,071.8s) |

---

## 二、双池交替管线结构

### 2.1 单周期时序（TIMELINE 格式）

日志中 `[TIMELINE]` 每隔 500 步记录一次完整周期的 6 个阶段：

```
|waitA|launchB|cpuA|waitB|launchA|cpuB|cycle=
```

**一个完整周期包含两个半周期，交替处理 Pool A 和 Pool B：**

```
── Half 1: 处理 Pool A 结果 ──────────────────────────

  waitA    : cudaStreamSync(A) — 等待 GPU 流 A 完成上一轮 trace
  launchB  : collect(B) + H→D upload(B) + optixLaunch(B) — 异步启动 GPU 处理 B
  cpuA     : distribute_and_advance(A) + cascade(A) + harvest+refill(A) — CPU 后处理 A

── Half 2: 处理 Pool B 结果 ──────────────────────────

  waitB    : cudaStreamSync(B) — 等待 GPU 流 B 完成
  launchA  : collect(A) + H→D upload(A) + optixLaunch(A) — 异步启动 GPU 处理 A
  cpuB     : distribute_and_advance(B) + cascade(B) + harvest+refill(B) — CPU 后处理 B
```

### 2.2 GPU-CPU 重叠时序图

```
Time ──────────────────────────────────────────────────────────────────▶

          Half 1 (Pool A)                   Half 2 (Pool B)
    ┌──────────────────────────┐     ┌──────────────────────────┐
CPU │ waitA │launchB│  cpuA    │     │ waitB │launchA│  cpuB    │
    └───────┴───────┴──────────┘     └───────┴───────┴──────────┘

GPU │←A完成→│  │←─── B 运行 ──────────────→│  │←─── A 运行 ─────────…
    └───────┘  └──────────────────────────────┘  └─────────────────

    ← ─ ─ ─ ─ ─ ─ ─ ─ cycle ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ →

重叠区间:
  ■ cpuA 与 GPU B 并行执行
  ■ cpuB 与 GPU A 并行执行
  □ waitA, waitB = CPU 空闲等待 GPU（无重叠）
```

核心原理：
- GPU `launchB` 后异步执行，CPU 立刻转入 `cpuA` 处理 Pool A 结果
- `cpuA` 期间 GPU B 在并行运行 → **有效重叠**
- `cpuA` 结束后 CPU 进入 `waitB` 等待 GPU B → **若 GPU B 未完成则 CPU 空闲**
- `waitB` 返回后重复对称的半周期

---

## 三、稳态周期详细分析

### 3.1 典型 TIMELINE 采样（稳态 refill 阶段）

**最佳情况** (cycle ≈ 3.0-3.1ms)：
```
step= 2000: waitA=0.78 launchB=0.24 cpuA=0.50 waitB=0.78 launchA=0.24 cpuB=0.50 cycle=3.04ms raysA=48689 raysB=48916
step= 3500: waitA=0.78 launchB=0.23 cpuA=0.51 waitB=0.77 launchA=0.27 cpuB=0.51 cycle=3.07ms raysA=48932 raysB=49362
step= 8000: waitA=0.79 launchB=0.25 cpuA=0.51 waitB=0.79 launchA=0.24 cpuB=0.51 cycle=3.09ms raysA=48472 raysB=48794
step=11000: waitA=0.78 launchB=0.24 cpuA=0.51 waitB=0.80 launchA=0.24 cpuB=0.50 cycle=3.05ms raysA=48878 raysB=49313
```

**典型情况** (cycle ≈ 3.2-4.0ms)：
```
step= 1000: waitA=1.01 launchB=0.32 cpuA=0.57 waitB=1.03 launchA=0.34 cpuB=0.57 cycle=3.83ms raysA=49102 raysB=48713
step=15000: waitA=1.04 launchB=0.32 cpuA=0.60 waitB=1.07 launchA=0.33 cpuB=0.59 cycle=3.95ms raysA=48593 raysB=49033
step=30000: waitA=1.05 launchB=0.32 cpuA=0.60 waitB=1.06 launchA=0.32 cpuB=0.59 cycle=3.94ms raysA=48179 raysB=48406
```

**毛刺情况** (cycle ≈ 4.4-5.5ms，调度器干扰)：
```
step= 7500: waitA=1.12 launchB=0.37 cpuA=1.05 waitB=1.08 launchA=0.33 cpuB=0.97 cycle=4.91ms raysA=45879 raysB=45628
step=41500: waitA=1.66 launchB=0.49 cpuA=0.68 waitB=1.51 launchA=0.47 cpuB=0.68 cycle=5.50ms raysA=48737 raysB=48490
step=43500: waitA=1.29 launchB=0.37 cpuA=0.72 waitB=1.28 launchA=0.31 cpuB=1.16 cycle=5.14ms raysA=48263 raysB=48469
```

### 3.2 各阶段统计（稳态 refill phase）

| 阶段 | 最佳 (ms) | 典型 (ms) | 毛刺 (ms) | 占比(典型) | 含义 |
|------|-----------|-----------|-----------|-----------|------|
| waitA | 0.78 | 0.95-1.15 | 1.30-1.66 | **27%** | 等待 GPU 流 A |
| launchB | 0.23 | 0.24-0.33 | 0.37-0.49 | **7%** | 启动 GPU B |
| cpuA | 0.50 | 0.53-0.62 | 0.65-1.05 | **15%** | CPU 后处理 A |
| waitB | 0.77 | 0.80-1.10 | 1.08-1.61 | **27%** | 等待 GPU 流 B |
| launchA | 0.24 | 0.24-0.33 | 0.31-0.47 | **7%** | 启动 GPU A |
| cpuB | 0.50 | 0.52-0.62 | 0.59-1.16 | **15%** | CPU 后处理 B |
| **cycle** | **3.04** | **3.40-3.95** | **4.40-5.50** | **100%** | **完整周期** |

**对称性**: waitA ≈ waitB, cpuA ≈ cpuB, launchA ≈ launchB → 两个半周期高度对称。

### 3.3 射线负载

| 指标 | 值 |
|------|------|
| raysA (每半周期) | 47,800 - 49,400 |
| raysB (每半周期) | 47,600 - 49,400 |
| 每路径平均射线 | ~3.9 (= 48500 / 12288) |
| avg_batch (profiling) | 47,062 |
| max_batch | 54,009 |
| min_batch (drain phase) | 2 |

**射线类型分布**（稳态）：

| 类型 | 典型占比 | 示例 (step 2000) |
|------|---------|-----------------|
| enclosure (enc) | ~74% | 36,546 |
| conduction_ds (ds) | ~25% | 12,346 |
| radiative (rad) | ~0.05% | 24 |
| shadow (shd) | 0% | 0 |
| startup (st) | 0% | 0 |

enclosure 射线占主导 → 场景以多孔泡沫封闭腔辐射为主。

---

## 四、全局性能剖析

### 4.1 Wavefront 阶段总览

```
persistent wavefront summary:
  total_steps = 2,806,950
  total_rays  = 132,100,910,238 (132.1B)
  avg_wavefront_width = 23,849.6

  refill_phase:  rays = 131,677,274,124 (99.7%)   wall = 5031.325s
  drain_phase:   rays =     423,617,715 (0.3%)    wall =   40.204s
```

### 4.2 每步时间开销分解

来自 summary `timing:` 字段，除以总步数 2,806,950：

| 阶段 | 总耗时 (s) | 每步 (ms) | 占比 |
|------|-----------|----------|------|
| **trace** (GPU kernel+upload+post+retrace) | 1,890.1 | 0.673 | **37.3%** |
| cascade (OMP 并行温度推进) | 707.6 | 0.252 | **14.0%** |
| distribute (分发结果到状态机) | 475.4 | 0.169 | **9.4%** |
| harvest+refill (收割完成路径+补充新任务) | 220.8 | 0.079 | 4.4% |
| collect (收集射线请求) | 204.8 | 0.073 | 4.0% |
| compact (流压缩活跃路径) | 163.1 | 0.058 | 3.2% |
| **overhead** (GPU等待+调度+同步) | 1,409.9 | 0.503 | **27.8%** |
| **合计** | **5,071.8** | **1.807** | **100%** |

### 4.3 Batch Trace 内部分解

```
batch trace profiling: calls=2,806,951  avg_batch=47,062
  gpu_kernel+upload:   1,228,114.9ms (65.1%)  avg=0.44ms/call
  cpu_postprocess:       346,303.7ms (18.4%)  avg=0.12ms/call
  fallback_retrace:      310,727.7ms (16.5%)  accepted=16,154,267  missed=1,320,462
  gpu_throughput: 107.6 Mrays/s  (kernel+upload only)
```

| 子项 | 每步 (ms) | 说明 |
|------|-----------|------|
| GPU kernel + H→D upload | 0.44 | OptiX RT + NN 核函数 + 射线数据上传 |
| CPU postprocess | 0.12 | 命中结果解析 |
| Fallback retrace | 0.11 | 包壳查询自相交重试 |
| **trace 合计** | **0.67** | |

### 4.4 有效吞吐率

| 指标 | 值 |
|------|------|
| GPU 原始吞吐 (kernel+upload) | **107.6 Mrays/s** |
| 端到端有效吞吐 | 132.1B / 5071.8s = **26.0 Mrays/s** |
| GPU 利用效率 | 26.0 / 107.6 = **24.2%** |
| 管线重叠效率 | 1 − overhead/total = **72.2%** |

---

## 五、管线重叠效率分析

### 5.1 GPU-bound 判断

在稳态最佳情况下：
- cpuA ≈ 0.50ms，waitB ≈ 0.78ms  
- cpuB ≈ 0.50ms，waitA ≈ 0.78ms  

**cpuX < waitX → CPU 处理完后仍需等待 GPU → 管线为 GPU-bound。**

GPU B 的有效执行时间 = cpuA + waitB ≈ 0.50 + 0.78 = **1.28ms**  
（含异步核函数执行 + D→H download + sync 开销）

### 5.2 重叠时间占比

对最佳稳态周期 (3.04ms)：
```
有效重叠: cpuA + cpuB = 0.50 + 0.50 = 1.00ms  (32.9%)
GPU等待:  waitA + waitB = 0.78 + 0.78 = 1.56ms  (51.3%)
启动开销: launchA + launchB = 0.24 + 0.24 = 0.48ms (15.8%)
```

**CPU 处理时间中 100% 被 GPU 执行所重叠** — 管线设计正确。  
瓶颈在于 GPU 执行时间 > CPU 处理时间，导致约 51% 的周期在等待 GPU。

### 5.3 理论最优周期

若 GPU 执行时间 = cpuX（完美平衡），则 waitX → 0：
- 理论最优 cycle = 2 × (launchX + cpuX) = 2 × (0.24 + 0.50) = **1.48ms** 
- 当前 best cycle = 3.04ms
- 改进空间: 3.04 / 1.48 = **2.05×**

实现路径：增大 pool_size → 增多每批射线 → GPU 运行更长 → 但 CPU 也处理更多 → 两端同步增长

---

## 六、Drain 阶段时序

### 6.1 进入 drain

```
step 2716309: entering drain phase, 24474 active paths remain, refill_wall=5031.325s
```

### 6.2 衰减曲线

```
step 2716808:  14,851 active  29,342 rays   ← 40% drop in ~500 steps
step 2718000:  12,331 active  24,172 rays
step 2720000:  10,251 active  19,960 rays
step 2724000:   7,947 active  15,300 rays
step 2730000:   5,246 active  10,220 rays   ← 快速指数衰减
step 2740000:   2,580 active   5,012 rays
step 2755000:     472 active     894 rays
step 2770000:      82 active     296 rays
step 2785000:      16 active      64 rays
step 2795308:       2 active      12 rays
step 2796808:       1 active       2 rays   ← 长尾，单路径循环
... (1 active 持续约 10,000 步至 step 2806808)
```

### 6.3 Drain 效率

| 指标 | 值 |
|------|------|
| Drain 步数 | 90,642 (3.2% of total) |
| Drain 射线 | 423,617,715 (0.3% of total) |
| Drain wall time | 40.204s (0.8% of total) |
| Drain 平均宽度 | 4,673 (vs refill avg 23,850) |
| 长尾 (1 path active) | ~10,000 步 |

Drain 阶段总体占比很小（0.8% wall time），但**长尾现象严重**：  
单个路径 (max_depth=343,429) 在 drain 末期独占完整的 GPU launch cycle，  
每步仅处理 2-6 rays，GPU 利用率趋近于零。

---

## 七、瓶颈与优化方向

### 7.1 瓶颈排序

| 优先级 | 瓶颈 | 占比 | 根因 |
|--------|------|------|------|
| P0 | **GPU 等待 (waitA+waitB)** | ~51% cycle | pool_size 不足导致每批射线~47K，GPU 执行时间超过 CPU 处理时间 |
| P1 | **cascade 处理** | 14% per-step | OMP 32线程, path_state AoS 导致 L3 cache thrashing |
| P2 | **distribute** | 9.4% per-step | 状态机推进开销，可能有分支预测失败 |
| P3 | **launch 开销 (launchA+launchB)** | ~14% cycle | H→D 上传 + OptiX launch API 开销 |
| P4 | **fallback retrace** | 16.5% trace | 包壳自相交重试，rejected 17.5M |

### 7.2 优化路径

#### (1) 增大 pool_size（SoA 化前提）
- **当前**: pool=24576, half=12288, ~47K rays/batch
- **目标**: pool=32K-65K, ~80K-200K rays/batch
- **效果**: GPU 执行时间增加但更饱和，amortize launch 开销
- **瓶颈**: L3 cache 容量限制 cascade 效率 → 需先 SoA 化 path_state

#### (2) path_state SoA 化
- 当前 AoS 结构对 cascade 不友好（每次只访问少数字段但加载整个结构体）
- SoA 化后 compact/collect/distribute 均受益于顺序内存访问
- 预期 cascade 时间减少 30-50%

#### (3) Drain 长尾截断
- 当 active < threshold（如 16）时强制终止或降级为 CPU fallback
- 避免 1-path tail 浪费 ~10K 个 GPU launch cycle
- 对精度影响可忽略（剩余路径贡献极小）

#### (4) 双流并发
- 当前使用单 GPU 流，改为双流（stream_A, stream_B）可使 GPU 真正并发
- H→D upload(B) 与 kernel(A) 可以硬件级并行

---

## 八、基准数字（可引用）

| 指标 | 值 | 备注 |
|------|------|------|
| 最佳周期 | 3.04ms | waitA=waitB=0.78ms |
| 典型周期 | 3.5-4.0ms | 含调度器抖动 |
| 峰值周期 | 5.5ms | OS 调度干扰 |
| GPU 原始吞吐 | 107.6 Mrays/s | kernel+upload |
| 端到端吞吐 | 26.0 Mrays/s | 24.2% GPU 利用 |
| 平均每步射线 | 47,062 | half-cycle |
| 平均 Wavefront 宽度 | 23,850 | 97% of pool |
| cascade 占比 | 14.0% | per-step |
| GPU wait 占比 | 27.8% | per-step |
| trace 占比 | 37.3% | per-step |

---

*分析日期: 2026-03-03 | 数据: porous scene OptiX backend | 硬件: RTX 4090 + i9-13900K*
