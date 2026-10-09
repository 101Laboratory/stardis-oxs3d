# Wavefront 求解器 CPU-GPU 流水线优化方案

**创建日期**: 2026-02-17  
**状态**: 待实施  
**目标**: 将 persistent wavefront 主循环从 CPU-GPU 串行执行改为双缓冲流水线，消除互等空闲

---

## 一、现状分析

### 1.1 当前执行模型

主循环 `solve_camera_persistent_wavefront()` 每轮完全串行——CPU 完成所有准备工作后阻塞等 GPU，GPU 完成后再阻塞等 CPU：

```
Step N:
  A. compact_active_paths()              ← CPU: 重建 active/need_ray/done 索引
  B. pool_collect_ray_requests_bucketed() ← CPU: 2-pass radix scatter 收集光线请求
  C. s3d_scene_view_trace_rays_batch_ctx()← GPU: H2D→kernel→D2H (3次cudaStreamSynchronize阻塞)
  D. pool_distribute_ray_results()        ← CPU: 将hit结果分发回path_state + advance
  D2. pool_collect_enc_locate_requests()  ← CPU: 收集ENC查询
      + GPU enc_locate batch             ← GPU: 阻塞
      + pool_distribute_enc_locate_results()
  E. pool_cascade_non_ray_steps_compact() ← CPU: 推进不需要光线的path
  F+G. harvest + refill                   ← CPU: 收割完成路径 + 补充新路径
```

时间线：
```
CPU:  ──[compact+collect]──▶[空闲等GPU]──▶[distribute+cascade+harvest+refill]──▶[空闲等GPU]──
GPU:  ──[空闲等CPU]──▶[trace(阻塞)]──▶[空闲等CPU]──▶[enc(阻塞)]──▶[空闲等CPU]──
```

**CPU 利用率 ≈ 50%，GPU 利用率 ≈ 50%，两者从不同时工作。**

### 1.2 GPU 批量追踪内部同步瓶颈

`trace_rays_batch_impl()`（`s3d_scene_view_batch_trace.cpp`）内部有 **3+1 个同步点**：

| 序号 | 操作 | 同步 |
|------|------|------|
| 1 | CPU AoS→SoA + `malloc` 临时数组 + `gpu_buffer_float3_upload()` × 3 | `cudaStreamSynchronize` #1 |
| 2 | `cus3d_trace_ray_batch_multi()` kernel launch | `cudaStreamSynchronize` #2 |
| 3 | `cudaMemcpyAsync` D2H | `cudaStreamSynchronize` #3 |
| 4 | CPU Top-K filter + fixup（串行遍历每条光线的K个候选） | — |

`transfer_stream` 在 `cus3d_device` 中已声明但 **完全未使用**——所有操作走单一 `dev->stream`。

### 1.3 CPU 瓶颈的实验证据

**关键现象**：降低 pool_size 从 10240→4096 时，GPU 占用率 **反而上升**。

这意味着 $T_{cpu}$ 的缩减比例 **显著大于** $T_{gpu}$ 的缩减比例：

$$\text{GPU\_util} = \frac{T_{gpu}}{T_{cpu} + T_{gpu}}$$

当 pool_size 减半时：
- $T_{gpu}$ 下降但不到线性（GPU kernel 有固有启动开销 + SM 填充阈值）
- $T_{cpu}$ **超线性下降**，原因：

| CPU阶段 | 超线性下降机制 |
|---------|--------------|
| **compact + collect** | `path_state` ~2.2KB/slot → 10240 slots ≈ 22MB 逼近/超出 L2 cache；4096 slots ≈ 9MB 完全在 L3 内，cache miss 大幅减少 |
| **cascade** | 对每个 active path 反复调用 `advance_one_step_no_ray()`，涉及大量分支+间接访问，path 越少分支预测越准 |
| **distribute** | radix scatter + 桶分发，内存访问模式随 N 增大变差 |
| **harvest + refill** | 路径槽位扫描，与 pool_size 线性相关 |

**结论**：实际 CPU/GPU 时间比可能接近 **60/40 甚至更偏**，而非理想的 50/50。

---

## 二、优化方案：CPU-GPU 双缓冲流水线

### 2.1 核心思路

在 GPU 执行 `trace(N)` 期间，CPU 同时处理上一轮结果 `distribute(N-1)` + `cascade(N-1)` + `harvest(N-1)` 并准备下一轮 `compact(N)` + `collect(N)`。通过双缓冲 `ray_requests` / `ray_hits` / `batch_ctx` 实现内存隔离。

目标时间线（稳态）：
```
GPU:  ──[trace(N)]──────────────────[trace(N+1)]────────────────[trace(N+2)]──▶
CPU:  ──[wait(N-1)+distrib+cascade  ──[wait(N)+distrib+cascade  ──[wait(N+1)+...
        +harvest+refill+compact       +harvest+refill+compact
        +collect(N)]                   +collect(N+1)]
```

### 2.2 理论加速估算

$$\text{Speedup} = \frac{T_{cpu} + T_{gpu}}{\max(T_{cpu},\, T_{gpu})}$$

| CPU占比 | GPU占比 | 串行总时间 | 流水线总时间 | 理论加速比 |
|---------|---------|------------|-------------|-----------|
| 50% | 50% | 100% | 50% | **2.00x** |
| 55% | 45% | 100% | 55% | **1.82x** |
| 60% | 40% | 100% | 60% | **1.67x** |
| 70% | 30% | 100% | 70% | **1.43x** |

### 2.3 实际折扣因素

| 因素 | 预计损失 | 说明 |
|------|---------|------|
| 流水线首尾无法重叠 | -2% | prologue/epilogue 各1轮串行 |
| `cudaEvent` 同步 + 双缓冲管理开销 | -3% | 比 `cudaStreamSynchronize` 略重 |
| ENC查询仍同步 | -5% | 取决于场景 enc 比例 |
| 双缓冲内存增加 cache pressure | -2% | 对CPU阶段有轻微影响 |
| Drain阶段batch变小 | -3% | 任务队列耗尽后重叠效率下降 |

**预计实际加速：1.3x – 1.8x**（取决于真实 CPU/GPU 比例）

---

## 三、实施步骤

### Phase 1: GPU 后端改造（消除内部串行瓶颈）

#### Step 1.1 预分配 GPU 结果缓冲

**文件**: `stardis-cus3d/custar-3d/0.10/src/cus3d_trace.cu`

当前 `cus3d_trace_ray_batch_multi()` 内部每次调用都 `cudaMallocAsync` / `cudaFreeAsync` 分配 `d_results`。改为在 `cus3d_ray_batch` 或 `s3d_batch_trace_context` 上预分配持久缓冲。

#### Step 1.2 预分配 AoS→SoA 临时缓冲

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

当前每次调用 `trace_rays_batch_impl()` 都 `malloc`/`free` 三个临时数组（`h_origins`, `h_directions`, `h_ranges`）。改为在 `s3d_batch_trace_context` 中增加持久的 host 缓冲。

#### Step 1.3 拆分 batch trace 为 submit + wait

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

将 `trace_rays_batch_impl()` 拆为两个函数：

```c
/* 异步提交：AoS→SoA + upload + kernel launch，不做sync，记录event */
res_T s3d_scene_view_trace_rays_batch_submit(
    struct s3d_scene_view* view,
    struct s3d_batch_trace_context* ctx,
    const struct s3d_ray_request* requests, size_t nrays);

/* 同步等待 + 后处理：cudaEventSynchronize + D2H + Top-K filter + fixup */
res_T s3d_scene_view_trace_rays_batch_wait(
    struct s3d_scene_view* view,
    struct s3d_batch_trace_context* ctx,
    struct s3d_hit* hits, size_t nrays,
    struct s3d_batch_trace_stats* stats);
```

#### Step 1.4 利用 `transfer_stream`

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

- H2D 上传用 `transfer_stream`
- Kernel 用 `stream`（通过 `cudaEvent` 等待上传完成）
- D2H 用 `transfer_stream`（通过 `cudaEvent` 等待 kernel 完成）
- 这可以 overlap H2D/D2H 传输与 kernel 计算

#### Step 1.5 同样改造 enclosure batch

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_find_enclosure.cpp`

执行相同的 submit/wait 拆分（预留接口，初始版本 enc 仍可同步调用）。

#### Step 1.6 消除全局诊断计数器竞争

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

10个 `static size_t g_diag_*` 累积计数器在 `BATCH_TRACE_DIAG=1` 时有数据竞争风险。改为 per-context 计数或 `_Atomic`。

---

### Phase 2: 双缓冲基础设施

#### Step 2.1 扩展 `wavefront_pool` 结构

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h`

添加双缓冲成员：

```c
/* --- 双缓冲 ray I/O --- */
#define WF_NBUF 2

struct s3d_ray_request* ray_requests[WF_NBUF];   /* [max_rays] */
int*                    ray_to_slot[WF_NBUF];     /* [max_rays] */
int*                    ray_slot_sub[WF_NBUF];    /* [max_rays] */
struct s3d_hit*         ray_hits[WF_NBUF];        /* [max_rays] */
size_t                  ray_count[WF_NBUF];
size_t                  bucket_offsets[WF_NBUF][RAY_BUCKET_COUNT + 1];
size_t                  bucket_counts[WF_NBUF][RAY_BUCKET_COUNT];

struct s3d_batch_trace_context* batch_ctx[WF_NBUF];

/* ENC双缓冲（如适用） */
/* ... */

int                     buf_curr;  /* 当前写入缓冲 (0 or 1) */
int                     buf_prev;  /* 上一轮GPU结果缓冲 */
int                     pipeline_active; /* GPU是否有inflight工作 */
```

**内存增量估算**（pool_size=32768, max_rays=196608）：

| 缓冲 | 单份大小 | 双缓冲增量 |
|------|---------|-----------|
| `ray_requests[]` | ~196608 × 64B ≈ 12MB | +12MB |
| `ray_hits[]` | ~196608 × 48B ≈ 9MB | +9MB |
| `ray_to_slot[]` + `ray_slot_sub[]` | ~196608 × 8B ≈ 1.5MB | +1.5MB |
| `batch_ctx` (含GPU端缓冲) | ~15MB | +15MB |
| **总计** | | **≈ +38MB** |

RTX 4090（24GB VRAM）完全可承受。

---

### Phase 3: 主循环流水线化

#### Step 3.1 重构主循环

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

从：
```c
while (active > 0 || tasks_remaining) {
    compact();
    collect(ray_requests, &ray_count);
    trace_batch_sync(ray_requests, ray_count, ray_hits);   /* 阻塞 */
    distribute(ray_hits, ray_count);
    enc_locate_sync();                                      /* 阻塞 */
    cascade();
    harvest(); refill();
}
```

重构为：
```c
/* Prologue: 首轮无前序GPU结果 */
compact();
collect(ray_requests[buf_curr], &ray_count[buf_curr]);
trace_batch_submit(ray_requests[buf_curr], ray_count[buf_curr], batch_ctx[buf_curr]);
pipeline_active = 1;
flip(buf_curr, buf_prev);  /* curr=1, prev=0 */

while (active > 0 || tasks_remaining) {
    /* --- Phase A: 等待上一轮GPU + 处理结果 --- */
    if (pipeline_active) {
        trace_batch_wait(batch_ctx[buf_prev], ray_hits[buf_prev]);
        distribute(ray_hits[buf_prev], ray_count[buf_prev]);
        
        /* ENC查询（同步，使用当前缓冲） */
        collect_enc();
        enc_locate_sync();
        distribute_enc();
    }
    
    cascade();
    harvest(); refill();
    
    /* --- Phase B: 准备下一轮 + 异步提交GPU --- */
    compact();
    collect(ray_requests[buf_curr], &ray_count[buf_curr]);
    
    if (ray_count[buf_curr] > 0) {
        trace_batch_submit(ray_requests[buf_curr], ray_count[buf_curr], batch_ctx[buf_curr]);
        pipeline_active = 1;
    } else {
        pipeline_active = 0;
    }
    
    flip(buf_curr, buf_prev);
}

/* Epilogue: 处理最后一轮 */
if (pipeline_active) {
    trace_batch_wait(batch_ctx[buf_prev], ray_hits[buf_prev]);
    distribute(ray_hits[buf_prev], ray_count[buf_prev]);
    cascade(); harvest();
}
```

#### Step 3.2 处理数据依赖

关键依赖关系与解决：

| 依赖 | 说明 | 解决方式 |
|------|------|---------|
| `distribute(N)` 依赖 `trace(N)` 结果 | 必须等GPU完成 | `trace_batch_wait()` 在 distribute 之前 |
| `collect(N+1)` 依赖 `cascade(N)` + `distribute(N)` | 需要知道哪些path要新光线 | cascade+distribute 在 collect 之前完成 |
| `harvest(N)` + `refill(N)` 改变 slots 占用 | 影响 `compact(N+1)` | harvest+refill 在 compact 之前完成 |
| **`slots[]`** 被 distribute 写入 | 不能同时被两个缓冲的 distribute 访问 | 双缓冲隔离 ray_requests/ray_hits，slots 仍单份——同一时刻只有一个 distribute 运行 |

---

### Phase 4: 验证与调优

#### Step 4.1 双模式开关

```c
/* 环境变量控制 */
int use_pipeline = getenv("STARDIS_PIPELINE") 
                   ? atoi(getenv("STARDIS_PIPELINE")) : 0;
```

默认关闭（`0`），方便 A/B 对比。

#### Step 4.2 流水线诊断计时

新增计时变量：

```
timing: compact=N.NNNs  collect=N.NNNs  submit=N.NNNs  wait=N.NNNs  
        distribute=N.NNNs  cascade=N.NNNs  harvest+refill=N.NNNs
overlap_effective: NN.N%   (T_overlap / T_total)
pipeline_stalls: NNN       (wait时GPU已完成的次数 → CPU是瓶颈的证据)
```

#### Step 4.3 正确性验证

- 双缓冲不改变计算逻辑和随机数序列，结果应与串行模式 **bit-exact 一致**
- 运行 `ctest -C Release --output-on-failure`
- 对比 `STARDIS_PIPELINE=0` 和 `STARDIS_PIPELINE=1` 的 IR 渲染输出

#### Step 4.4 性能验证

- Nsight Systems timeline 可视化确认 CPU/GPU 重叠
- 检查 `pipeline_stalls` 计数——如果频繁 stall（wait时GPU早已完成），说明 CPU 是瓶颈，需进一步优化 CPU 阶段
- 检查 drain 阶段效率下降程度

---

## 四、CPU 瓶颈补充优化（可选后续）

基于 pool_size 10240→4096 GPU占用率反升的实验证据，CPU 可能是显著瓶颈。如果 Phase 3 实施后 `pipeline_stalls` 频繁，可考虑以下补充优化：

### 4.1 cascade 优化

`pool_cascade_non_ray_steps_compact()` 对每个 active path 反复调用 `advance_one_step_no_ray()` 直到需要 ray 或完成。优化方向：

- **限制每次 cascade 最大迭代数**，将深度递归的 path 推迟到下一轮
- **预排序 path 按 phase**，让相同状态的 path 连续处理，改善分支预测
- **将 cascade 中的纯算术步骤（概率计算、权重累积）向量化**

### 4.2 compact + collect 优化

- **stream compaction 改用 SIMD 或 GPU**——`compact_active_paths()` 是线性扫描，可用 `_mm256` 指令加速
- **collect 的 radix scatter 可用多线程**——2-pass scatter 天然可并行

### 4.3 pool_size 自适应

根据运行时 `pipeline_stalls` 频率动态调整 pool_size：
- stalls 频繁 → 减小 pool_size（减轻 CPU 负担，让 CPU 跟上 GPU）
- stalls 为零 → 增大 pool_size（GPU 有余量，增大 batch 提高 GPU 利用率）

---

## 五、线程安全评估

| 组件 | 状态 | 风险 |
|------|------|------|
| `sdis_wf_steps.c` 所有 step 函数 | ✅ 纯函数，只操作传入的 `path_state*` | 无 |
| `static const` 查找表 (`base_dirs`, `fb_dir`) | ✅ 只读 | 无 |
| `g_diag_*` 全局计数器 (`s3d_scene_view_batch_trace.cpp`) | ⚠️ 非原子读写 | 仅 `BATCH_TRACE_DIAG=1` 时触发，需改为 `_Atomic` |
| `sdis_scene*` 场景数据 | ✅ 只读 | 无 |
| `path_state slots[]` | ⚠️ 但每个 slot 独立 | 只要不同 phase 操作不同 slot 即可——双缓冲设计保证 |
| `cudaMallocAsync`/`cudaFreeAsync` in trace kernel | ⚠️ 多 stream 场景下需注意 | Phase 1 改为预分配消除 |

---

## 六、风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| CPU 瓶颈远超 GPU（>70/30） | 流水线收益 < 1.43x | 中 | 先拿诊断数据确认比例；辅以第四章 CPU 优化 |
| ENC 查询频繁成为新瓶颈 | CPU 阶段加长 | 低-中 | 预留 enc submit/wait 接口，后续可异步化 |
| Drain 阶段 batch 变小 | 尾部 1-2 轮无重叠 | 低 | drain 段时间占比本身小，可接受 |
| Top-K CPU 后处理时间线性增长 | wait 阶段变长 | 低 | 可后续移至 GPU |
| 双缓冲增加内存 ~38MB | 压缩其他 GPU 缓冲空间 | 极低 | RTX 4090 24GB 完全可承受 |

---

## 七、实测诊断数据（i9-13900K + RTX 4090）

**测试场景**: Stardis-Starter-Pack porous, 320×320 spp=32  
**总射线数**: 33.83 亿条（恒定，与 pool_size 无关）

### 7.1 各阶段耗时

| pool_size | compact | collect | trace (GPU) | distribute | cascade | harvest+refill | **总耗时** |
|----------:|--------:|--------:|------------:|-----------:|--------:|---------------:|----------:|
| 4096 | 48.8s | 149.5s | **1187.5s** | 254.2s | 661.7s | 58.5s | **40m24s** |
| 10240 | 60.4s | 178.5s | **1136.8s** | 319.4s | 886.5s | 83.8s | **45m40s** |
| 32768 | 108.6s | 319.8s | **1059.5s** | 548.1s | 1170.8s | 191.7s | **58m38s** |

### 7.2 CPU vs GPU 时间

$T_{cpu} = \text{compact} + \text{collect} + \text{distribute} + \text{cascade} + \text{harvest+refill}$

| pool_size | T_cpu | T_gpu | CPU占比 | steps | avg_width |
|----------:|------:|------:|--------:|------:|----------:|
| 4096 | 1172.7s | 1187.5s | **49.7%** | 912,610 | 3,326 |
| 10240 | 1528.5s | 1136.8s | **57.3%** | 494,152 | 6,143 |
| 32768 | 2339.0s | 1059.5s | **68.8%** | 302,419 | 10,037 |

### 7.3 缩放趋势

pool_size 从 4096→32768（×8）：
- **T_cpu**: 1173s → 2339s（**×2.0**，超线性增长 — cache/分支预测恶化）
- **T_gpu**: 1188s → 1060s（**×0.89**，反而下降 — 更大batch提升SM占用率）
- GPU 吞吐量: 2.85M rays/s → 3.19M rays/s（+12%），但被 CPU 膨胀完全吞噬

### 7.4 CPU 各阶段占比

| pool_size | cascade占CPU% | collect占CPU% | distribute占CPU% | compact占CPU% | harvest占CPU% |
|----------:|--------------:|--------------:|-----------------:|--------------:|--------------:|
| 4096 | **56.4%** | 12.7% | 21.7% | 4.2% | 5.0% |
| 10240 | **58.0%** | 11.7% | 20.9% | 3.9% | 5.5% |
| 32768 | **50.1%** | 13.7% | 23.4% | 4.6% | 8.2% |

**cascade 在所有 pool_size 下占 CPU 时间 50–58%，是 CPU 侧最大单项瓶颈。**

### 7.5 流水线加速预估

$$\text{Pipeline time} \approx \max(T_{cpu},\, T_{gpu})$$

| pool_size | 串行耗时 | 流水线耗时 | **加速比** | 预计时间 |
|----------:|---------:|-----------:|----------:|---------:|
| **4096** | 2360s | 1188s | **1.99x** | **~20min** |
| 10240 | 2665s | 1529s | 1.74x | ~25.5min |
| 32768 | 3399s | 2339s | 1.45x | ~39min |

### 7.6 结论

1. **pool=4096 + 流水线是最优组合** — CPU≈GPU 完美平衡，理论加速 2x，40min→**20min**
2. **不应盲目增大 pool_size** — 更大 pool 仅提升 GPU 吞吐 12%，却让 CPU 翻倍
3. 流水线后的**下一瓶颈是 cascade**（占 CPU 56%），进一步提速应优先优化它
4. pool=4096 时 drain 阶段仅占 0.6%（71s），流水线首尾损失可忽略

---

## 八、硬件利用率诊断实验

### 背景

观察到在任何 pool_size 配置下，CPU 总占用率和核占用率都很低，GPU 虽看似满载但频繁回落到较低占用率。这说明之前的分析（CPU 50% vs GPU 50%）仅是**时间分布**而非**硬件利用率**——两者可能都在"等待"而非"满载计算"。

| 现象 | 可能含义 |
|------|---------|
| CPU 占用率低 | 仅用单核；或单核内存受限（stall on cache miss） |
| CPU 核占用率低 | 单线程执行，13900K 的 24 核仅用 1 核 ≈ 4% |
| GPU 看似满载但频繁回落 | kernel 间有 idle gap；或 kernel 内 warp 利用率低 |
| 增大 pool_size 后 GPU 占用率未显著提升 | batch 已超过 SM 饱和点；或瓶颈不在计算而在显存带宽 |

需要系统化排查确定真正的细节瓶颈（单核、Cache、显存带宽等）。

### 实验 1: CPU 单核瓶颈确认 — 核心亲和性

**目的**: 确认 CPU 阶段是否被单核性能限制（而非内存/其他）

```powershell
# 将进程绑定到单个P-core，排除调度器干扰
cd Stardis-Starter-Pack\porous
$env:STARDIS_POOL_SIZE="4096"
Start-Process -FilePath "..\..\stardis-cus3d\build\bin\Release\stardis.exe" `
  -ArgumentList "-M porous.txt -t 4 -V 3 -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0" `
  -PassThru | ForEach-Object { $_.ProcessorAffinity = 0x01 }
# 输出重定向需在外部处理，或改用 cmd /c 方式
```

**观察**: 如果绑单核后总耗时不变 → 确认单线程瓶颈。如果变慢 → 存在隐式多核利用（unlikely但需排除）。

**优先级**: P3（低成本排除项）

### 实验 2: CPU Cache 瓶颈 — VTune / perf 采样

**目的**: 确认 cascade/collect/distribute 是计算受限还是内存受限

```powershell
cd Stardis-Starter-Pack\porous
$env:STARDIS_POOL_SIZE="4096"

# Intel VTune (若已安装)
vtune -collect memory-access -knob sampling-interval=1 -- `
  ..\..\stardis-cus3d\build\bin\Release\stardis.exe `
  -M porous.txt -t 4 -V 3 `
  -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "IR_rendering_256x256x8.ht"

# 或使用 Windows Performance Recorder
wpr -start CPU -start VirtualAllocation
..\..\stardis-cus3d\build\bin\Release\stardis.exe `
  -M porous.txt -t 4 -V 3 `
  -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "IR_rendering_256x256x8.ht"
wpr -stop profile.etl
```

**关键指标**:

| 指标 | 计算受限 | 内存受限 |
|------|---------|---------|
| IPC (Instructions Per Cycle) | >2.0 | <1.0 |
| L1d miss rate | <5% | >10% |
| L3 miss rate | <1% | >5% |
| Backend Bound (TMA) | <30% | >50% |

**预期**: cascade 处理 `path_state`（2.2KB/slot）时有大量随机访问 `sdis_scene*` 材料表/几何数据，可能 **L3 miss → DRAM bound**。

**优先级**: P2

### 实验 3: CPU cascade 内部热点 — 函数级采样 ✅ 已完成

**目的**: 确认 cascade 662s（56% of CPU）花在哪些具体 step 函数

**实现状态**: 已完成。在 `pool_cascade_non_ray_steps_compact()` 中添加 per-phase `time_current()` 计时，使用 256×256 spp=4 @ pool=4096 运行获取数据。

**实测结果** (256×256 spp=4 @ pool=4096, total_iterations=702M):

| 排名 | Phase | 名称 | Count | Time | 占 cascade% |
|:----:|------:|------|------:|-----:|----------:|
| 1 | **34** | `PATH_CND_DS_CHECK_TEMP` | 163.2M | 14.955s | **20.2%** |
| 2 | **38** | `PATH_CND_DS_STEP_ADVANCE` | 163.2M | 14.114s | **19.1%** |
| 3 | **3** | `PATH_COUPLED_BOUNDARY` | 56.0M | 8.439s | **11.4%** |
| 4 | **16** | `PATH_BND_SF_PROB_DISPATCH` | 111.9M | 8.337s | **11.3%** |
| 5 | **37** | `PATH_CND_DS_STEP_ENC_VERIFY` | 118.3M | 6.693s | **9.0%** |

**结论**: DS 导热循环 (34+37+38) 占 cascade 48.3%，SF 边界 (3+16+15) 占 25.6%。详见 [Experiment3 Results.md](Experiment3%20Results.md)。

**优先级**: P0（已完成）

### 实验 4: GPU kernel 占用率 — Nsight Compute

**目的**: 确认 GPU kernel 内部是计算受限、显存带宽受限、还是延迟受限

```powershell
cd Stardis-Starter-Pack\porous
$env:STARDIS_POOL_SIZE="4096"

# Nsight Compute 单次 kernel profiling
ncu --set full --kernel-name "cus3d_trace_ray_batch_multi" `
    --launch-count 5 --launch-skip 10 `
    ..\..\stardis-cus3d\build\bin\Release\stardis.exe `
    -M porous.txt -t 4 -V 3 `
    -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0
```

**关键指标**:

| 指标 | 含义 |
|------|------|
| **Achieved Occupancy** | 实际 warp 占用率（<50% = 寄存器/共享内存瓶颈） |
| **SM Throughput** | SM 忙碌比例 |
| **DRAM Throughput** | 显存带宽利用率（>80% = 显存带宽瓶颈） |
| **Compute Throughput** | FP/INT 单元利用率 |
| **Warp Stall Reasons** | `stall_memory_dependency` 高 = 显存延迟受限 |
| **Branch Divergence** | BVH 遍历天然有分支 → warp 分化 |

**预期**: BVH 遍历是典型的 **latency-bound + branch-divergent** 负载，可能看到：
- Achieved Occupancy 40-60%
- DRAM Throughput 30-50%
- 主要 stall: `stall_memory_dependency`（等 L2/DRAM）

**优先级**: P1

### 实验 5: GPU kernel 间空隙 — Nsight Systems timeline

**目的**: 量化 GPU kernel 之间的 idle gap，获取全局 CPU/GPU 时间线视图

```powershell
cd Stardis-Starter-Pack\porous
$env:STARDIS_POOL_SIZE="4096"

nsys profile --trace=cuda,nvtx --output=wavefront_timeline ..\..\stardis-cus3d\build\bin\Release\stardis.exe -M porous.txt -t 4 -V 3 -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0
```

**观察**:
- 测量连续两次 `cus3d_trace_ray_batch_multi` kernel 之间的间隔
- 统计 H2D upload / D2H download 占比
- 确认是否有 `cudaStreamSynchronize` 导致的长空隙

**预期**: 会看到如下 pattern：
```
GPU: ─[kernel]─────────[idle gap]──────────[kernel]─────────[idle gap]──
              ↑ D2H+sync+CPU+H2D+sync ↑
```

idle gap 应约 ≈ $T_{cpu}/\text{steps}$ ≈ $1173/912610$ ≈ **1.29ms per step**。

**优先级**: P0（最高，一条命令即可获得全局视图）

### 实验 6: 显存带宽瓶颈 — 人工变量控制

**目的**: 隔离 GPU 是计算受限还是显存受限

**方法**: 修改 BVH 遍历 kernel，减少每射线的 BVH 访问次数（比如将 Top-K 从 8 降到 1），观察 kernel 时间变化：

| Top-K | BVH 遍历量 | 显存带宽 | 预期 kernel 时间 |
|-------|-----------|---------|-----------------|
| K=8 (当前) | 100% | 100% | baseline |
| K=1 | ~30% | ~30% | 若显存受限: ↓60-70%; 若计算受限: ↓30-40% |

**优先级**: P3（需改代码，作为隔离诊断手段）

### 实验 7: batch size 饱和点 — GPU 吞吐量曲线 ✅ 已插桩

**目的**: 精确找到 GPU batch size 的饱和点

**实现状态**: 已在主循环 Step C 后累积 `s3d_batch_trace_stats` 中的 per-call timing。

**输出内容**:
```
batch trace profiling: calls=NNN  avg_batch=NNNN  min=NNN  max=NNN
  gpu_kernel+upload: total=NNNNms (NN.N%)  avg=N.NNms/call
  cpu_postprocess:   total=NNNNms (NN.N%)  avg=N.NNms/call
  fallback_retrace:  total=NNNNms (NN.N%)  accepted=NNN  missed=NNN  rejected=NNN
  gpu_throughput: N.N Mrays/s  (kernel+upload only)
```

**后续分析**: 用不同 pool_size 运行，提取每次调用的 `(nrays, kernel_time_ms)` 绘制散点图：
```
GPU throughput (M rays/s)
    ^
    |        ╭────────────────  ← 饱和区
    |       /
    |      /
    |     /  ← 线性区
    |    /
    |   /
    +---+---+---+---+---+---→ batch size
    0   2K  4K  8K  16K 32K
```

- 饱和点 < 4K → pool=4096 已够
- 饱和点 ~8K-16K → 可微调获益
- 从不饱和 → GPU 一直在 memory stall

**优先级**: P1（已实现，运行即可获取数据）

### 实验 8: cudaStreamSynchronize 开销 — 隔离测试 ✅ 部分覆盖

**目的**: 量化当前 3 次 sync 的各自开销

`s3d_batch_trace_stats` 已记录 `batch_time_ms`（含 upload+kernel+sync）和 `postprocess_time_ms`（CPU filter）和 `retrace_time_ms`，通过实验 7 的插桩已可对比。

**进一步隔离**（如需精细数据）: 在 `s3d_scene_view_batch_trace.cpp` 的 3 次 `cudaStreamSynchronize` 前后加高精度计时:

| sync 位置 | 等什么 | 预期等待时间 |
|-----------|--------|------------|
| upload 后 | H2D DMA | ~0.1ms（数据量小） |
| kernel 后 | BVH 遍历 | 0.1-1ms（取决于 batch） |
| download 后 | D2H DMA | ~0.05ms |

**优先级**: P2

### 实验执行顺序

| 优先级 | 实验 | 成本 | 收益 | 状态 |
|--------|------|------|------|------|
| **P0** | 实验 5 (Nsight Systems timeline) | 低：一条命令 | 全局视图：CPU/GPU idle gap、kernel pattern | 待运行 |
| **P0** | 实验 3 (cascade 热点) | 低：已插桩 | 定位 CPU 56% 时间的具体去向 | **✅ 已完成** |
| **P1** | 实验 4 (Nsight Compute kernel) | 中：需读懂报告 | 确认 GPU 瓶颈类型 | 待运行 |
| **P1** | 实验 7 (batch 饱和点) | 低：已插桩 | 确定最优 pool_size | **✅ 已插桩，待运行** |
| **P2** | 实验 2 (VTune 内存) | 中：需 VTune | 确认 CPU cache 瓶颈 | 待运行 |
| **P2** | 实验 8 (sync 开销) | 低：部分覆盖 | 为流水线方案提供数据 | **✅ 部分覆盖** |
| **P3** | 实验 1 (核心亲和性) | 极低 | 排除项 | 待运行 |
| **P3** | 实验 6 (Top-K 变量控制) | 中：改代码 | 隔离诊断 | 待运行 |

### 实验插桩开销评估

| 插桩 | 调用次数 | 单次开销 | 总开销 | 占总时间 |
|------|---------|---------|--------|---------|
| cascade per-phase `time_current()` | ~10 亿 | ~50ns | ~50s | ~2% (pool=4096) |
| batch trace stats 累积 | ~91 万 | ~10ns | <0.01s | 可忽略 |

如插桩开销影响结果精度，可通过条件编译宏 `SDIS_CASCADE_PROFILE` 控制 cascade 插桩的开关。

---

*文档更新: 2026-02-18 | 新增第八章硬件利用率诊断实验方案*
