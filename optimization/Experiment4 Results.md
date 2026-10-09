# Wavefront 求解器硬件利用率测试-实验4结果

**创建日期**: 2026-02-18  
**状态**: 已完成  
**目的**: 确认 GPU kernel 内部是计算受限、显存带宽受限、还是延迟受限  
**GPU**: NVIDIA GeForce RTX 4090 (CC 8.9, 128 SMs, 48 warps/SM max)  
**Kernel**: `trace_rays_topk_kernel` — 500 次 launch, 8 replayer passes/launch  
**采集 sections**: SpeedOfLight, LaunchStats, Occupancy, WorkloadDistribution  
**未采集 sections**: MemoryWorkloadAnalysis, WarpStateStatistics, InstructionStatistics, SchedulerStatistics, SourceCounters

## 1. 测试方法

```powershell
cd Stardis-Starter-Pack\porous
$env:STARDIS_POOL_SIZE="32768"

ncu --set full --kernel-name "trace_rays_topk_kernel" `
    --launch-count 500 --launch-skip 0 `
    ..\..\stardis-cus3d\build\bin\Release\stardis.exe `
    -M porous.txt -t 4 -V 3 `
    -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0
```

## 2. 结果

### 2.1 报告文件

[report.ncu-rep](../profile%20reports/kernelprofile.ncu-rep)（Nsight Compute 生成的完整报告）  
[kernel_detail.csv](../profile%20reports/rt_kernel_ncu.csv)（trace_rays_topk_kernel 的详细指标）

### 2.2 Speed of Light 总览

| ID | Duration (ms) | SOL Compute (%) | SOL Memory (%) | 瓶颈类型 |
|----|---------------|-----------------|----------------|----------|
| 0 (冷启) | 0.228 | 8.72 | 22.47 | Memory |
| 50 | 0.566 | 25.57 | **75.72** | **Memory** |
| 100 | 0.625 | 24.33 | **72.12** | **Memory** |
| 200 | 0.656 | 22.85 | **76.30** | **Memory** |
| 300 | 0.656 | 23.60 | **76.46** | **Memory** |
| 400 | 0.671 | 22.93 | **77.47** | **Memory** |
| 499 | 0.661 | 22.93 | **77.47** | **Memory** |

**总耗时**: 319.7 ms（500 次 launch 合计）  
**稳态单次耗时**: ~0.65–0.67 ms

### 2.3 Occupancy（占用率）

| 指标 | ID=0 (冷启) | ID=50 | ID=100 | ID=200 | ID=300 | ID=400 | ID=499 |
|------|------------|-------|--------|--------|--------|--------|--------|
| **理论占用率** | 33.33% | 33.33% | 33.33% | 33.33% | 33.33% | 33.33% | 33.33% |
| **实际占用率** | **13.29%** | 27.02% | 27.30% | 27.55% | 27.31% | 27.96% | 27.96% |
| **活跃 warps/SM** | 6.38 | 12.97 | 13.11 | 13.22 | 13.11 | 13.42 | 13.42 |
| **最大 warps/SM** | 16 | 16 | 16 | 16 | 16 | 16 | 16 |

**占用率限制因素**:

| 限制项 | Warps/SM | Blocks/SM | 是否主限制 |
|--------|----------|-----------|-----------|
| **寄存器** (86 regs/thread) | **16** | **2** | **← 主限制** |
| Warps | 48 | 6 | |
| 共享内存 | 48 | 16 | |
| SM 限制 | 48 | 24 | |

### 2.4 Throughput & Utilization（吞吐量与利用率）

| 指标 | ID=0 | ID=50 | ID=100 | ID=200 | ID=300 | ID=400 | ID=499 |
|------|------|-------|--------|--------|--------|--------|--------|
| **SM/Compute (%)** | 8.72 | 25.57 | 24.33 | 22.85 | 23.60 | 22.93 | 22.93 |
| **Memory (%)** | 22.47 | **75.72** | **72.12** | **76.30** | **76.46** | **77.47** | **77.47** |
| **DRAM (%)** | 1.38 | 5.27 | 3.73 | 2.93 | 3.44 | 2.87 | 3.01 |
| **L1/TEX (%)** | 24.30 | 37.68 | — | — | — | — | — |
| **L2 (%)** | 22.47 | 75.72 | 72.12 | 76.30 | 76.46 | 77.47 | 77.47 |

**L2 Crossbar 详细数据**:

| 指标 | ID=0 | ID=50 | ID=250 |
|------|------|-------|--------|
| `lts__xbar2lts_cycles_active` (%) | 26.26 | **78.10** | **75.90** |
| `lts__d_sectors` (%) | 5.95 | 17.94 | 17.32 |
| `lts__t_tag_requests` (%) | 14.22 | 42.68 | 41.30 |

### 2.5 Pipeline 利用率（指令类型分布）

| Pipeline | ID=0 | ID=50 | ID=250 | 说明 |
|----------|------|-------|--------|------|
| **LSU** (Load/Store) | 8.34% | **24.45%** | **18.55%** | **最活跃 pipeline** |
| **ALU** (Integer) | 7.97% | **23.31%** | **17.71%** | 紧随其后 |
| **FMA** (FP32 乘加) | 2.50% | 7.29% | 5.53% | |
| **CBU** (分支/控制) | 2.28% | 6.83% | 5.25% | |
| **FMA Heavy** | 0% | 0% | 0% | 无 DFMA/DMUL |
| **FP64** | 0% | 0% | 0% | **无双精度计算** |
| **Tensor** | 0% | 0% | 0% | **无 Tensor Core** |

### 2.6 Launch Configuration（启动配置）

| 参数 | 值 |
|------|-----|
| **Block Size** | (256, 1, 1) = 256 threads |
| **Registers/Thread** | 86 (allocated) |
| **Static Shared Mem** | 0 bytes |
| **Dynamic Shared Mem** | 0 bytes |
| **Driver Shared Mem** | 1,024 bytes |

| ID | Grid Size | Waves/SM | 说明 |
|----|-----------|----------|------|
| 0 | (173,1,1) | 0.68 | 冷启动，不足以填满 GPU |
| 50 | (537,1,1) | 2.10 | 稳态 |
| 100 | (507,1,1) | 1.98 | |
| 200 | (501,1,1) | 1.96 | |
| 300 | (503,1,1) | 1.97 | |
| 400 | (517,1,1) | 2.02 | |
| 499 | (503,1,1) | 1.97 | |

### 2.7 Nsight Compute 内置诊断规则

**SOL Bottleneck**:
> *This kernel exhibits low compute throughput and high memory throughput. The kernel is memory bound. The memory-related breakdown is: L2 75.72%, L1/TEX 37.68%, DRAM 5.27%.*

**Theoretical Occupancy**:
> *The difference between calculated theoretical occupancy (33.3%) and the peak theoretical occupancy (100%) is limited by the number of required registers. The number of registers required is 86.*

**Achieved Occupancy**:
> *The difference between theoretical (33.3%) and achieved (27.6%) can be the result of warp scheduling overheads or workload imbalances during execution.*

**Workload Distribution (SM Imbalance)**:
> *The difference between min and max SM active cycles is significantly high (25% above/19% below average for ID=50). There are 4 SM units that are underperforming compared to the rest.*

### 2.8 Warp State & Scheduler（补全 profile 结果）

补全 WarpStateStatistics / InstructionStatistics / SchedulerStatistics 后获得以下数据：

**Warp Stall 主因**:

| 指标 | 值 | 说明 |
|------|-----|------|
| **主停顿原因** | **Long Scoreboard (L1TEX op)** | Warp 等待 L1TEX（全局内存 / BVH 节点加载）返回 |
| **Est. Speedup (消除此停顿)** | **~39.56%** | ncu 基于采样的加速上界估算 |

**Scheduler 统计**:

| 指标 | 值 | 说明 |
|------|-----|------|
| Warp Cycles / Issued Instruction | **6.30** | 每发射一条指令的平均周期数，>1 说明存在大量停顿 |
| Warp Cycles / Executed Instruction | **6.36** | ≈ Issued，说明 replay 极少 |
| Avg. Active Threads / Warp | **6.45 / 32** | **仅 20.2% 线程活跃** — 极严重的线程分歧 |
| Avg. Not Predicated-Off Threads / Warp | **6.27 / 32** | 19.6% 未被 predicate 屏蔽 |
| Thread Divergence Est. Speedup | **~7%** | ncu 估算消除分歧可获得的加速 |

**关键发现**：

1. **Long Scoreboard 停顿（~40% speedup headroom）** 直接印证了 §3.1 的 L2 crossbar bound 结论——warp 在等待 BVH 节点加载从 L1TEX → L2 crossbar 返回，crossbar 拥塞导致延迟增大。
2. **Avg. Active Threads = 6.45/32（20%）** 揭示了极端的 **warp 内线程分歧**：BVH 遍历过程中，32 条线程的射线沿完全不同的子树路径前进，在每个 if-else 节点选择处产生分支分歧，大量线程被 predicate-off。这是随机方向蒙特卡洛射线在 BVH 遍历中的固有特征。
3. **6.45 active threads × 6.3 WC/inst** 意味着 GPU 的有效计算吞吐仅为理论值的 $\frac{6.45}{32} \times \frac{1}{6.3} \approx 3.2\%$。

### 2.9 原始数据缺失项（已部分补全）

| 类别 | 状态 | 说明 |
|------|------|------|
| **Warp Stall Reasons** | ✅ 已补全 | §2.8 |
| **Scheduler Statistics** | ✅ 已补全 | §2.8 |
| **完整指令计数** | ❌ 待补全 | `sm__sass_thread_inst_executed_op_*` |
| **内存 byte/sector 级数据** | ❌ 待补全 | `dram__bytes_read/write`, L1/L2 hit/miss sector 数 |

---

## 3. 分析

### 3.1 核心结论：L2 Crossbar Bound + 极端线程分歧

Kernel 的 Speed of Light 分布明确指向 **Memory-bound**（SOL Memory ~77% vs SOL Compute ~23%）。进一步细化：

| 内存层级 | 吞吐占比 | 是否瓶颈 |
|---------|---------|---------|
| **L2 Crossbar** | **76–78%** | **← 硬件带宽瓶颈** |
| L1/TEX | ~38% | 中等 |
| DRAM | 3–5% | 极低 |

结合 §2.8 补全数据，kernel 存在**两重叠加瓶颈**：

1. **L2 Crossbar 拥塞**（SOL Memory 78%）—— 128 个 SM 同时向 L2 发出大量随机 BVH 节点读请求。Long Scoreboard on L1TEX op 是主停顿原因，消除此停顿的 Est. Speedup ~40%。
2. **极端线程分歧**（Avg. Active Threads = 6.45/32 = 20%）—— BVH 遍历的 if-else 路径选择导致同一 warp 内 ~80% 的线程被 predicate-off。这意味着**每条 warp 指令仅有 ~6 个线程在做有效工作**。

两者互相放大：分歧导致 warp 实际执行更多指令（因为同一 warp 需要依次执行所有分支路径），每条指令都可能触发不同的 BVH 节点加载，使 L2 请求数进一步膨胀。

> **BVH 数据（335.5 MB）的工作集大部分驻留在 L2 缓存中**，kernel 不需要频繁访问 DRAM。瓶颈不是显存带宽（DRAM bandwidth），而是 **L2 crossbar 的请求处理能力**。

### 3.2 占用率分析

$$\text{Theoretical Occupancy} = \frac{16\text{ warps}}{48\text{ warps/SM}} = 33.33\%$$

**限制因素**: 每线程 86 个寄存器 → 每 SM 最多 2 个 block (16 warps)。

实际占用率仅 ~27-28%（低于理论 33.33%），差距来源于：
1. **SM 间负载不均衡** — ncu 报告 min/max SM 活跃周期差异达 25%/19%，有 4 个 SM 明显偏低
2. **Waves/SM ≈ 2.0** — Grid≈500 blocks / 128 SMs / 2 blocks-per-SM ≈ 2 波，最后一波仅覆盖部分 SM（tail effect）

**占用率对 L2-bound kernel 的影响**：低占用率在此场景下并非全负面——更少的并发 warp 意味着更少的 L2 请求竞争。但在 27% 占用率下 L2 crossbar 已达 78%，提升占用率反而可能加剧拥塞。这是**经典的 latency-hiding vs bandwidth-contention 权衡**。

**占用率与线程分歧的联合效应**：实际活跃线程仅 6.45/32，等效占用率为 $27\% \times \frac{6.45}{32} \approx 5.4\%$。GPU 对 warp 的调度是按完整 warp 计算占用率的，但有效工作线程远低于名义占用率。这意味着**即使直觉上还有大量空闲算力，实际增加占用率后新 warp 的线程分歧同样严重**，不会改善有效吞吐——反而增加 L2 请求。

### 3.3 指令 Pipeline 特征

```
LSU (24.5%) ≈ ALU (23.3%) >> FMA (7.3%) > CBU (6.8%)
FP64 = 0%    Tensor = 0%
```

| 特征 | 含义 |
|------|------|
| LSU ≈ ALU | BVH 遍历的典型特征：大量指针/索引计算（ALU）伴随数据加载（LSU） |
| FMA 低 | 几何计算（射线-三角形交叉）占比不高，大部分时间在遍历树 |
| FP64 = 0 | 所有浮点运算均为 FP32，双精度未被使用 |
| CBU ~7% | 分支控制占一定比例（BVH 遍历的 if-else 路径选择），但不是主瓶颈 |

### 3.4 对 Wavefront 求解器的影响

结合之前的 profile 数据（pool=4096: T_gpu=1187.5s, 912,610 次 batch trace 调用）：

| 指标 | 值 | 计算 |
|------|-----|------|
| 稳态单次 kernel 耗时 | ~0.65 ms | ncu 直测 |
| Kernel 纯执行时间 | 500 × 0.65ms = 325ms → 外推至 912K 调用 ≈ **593s** | |
| 实测 T_gpu | 1187.5s | profile 数据 |
| **非 kernel 开销** | 1187.5 - 593 ≈ **594s (50%)** | H2D + sync + D2H + CPU post |

> **kernel 执行仅占 GPU 阶段的 ~50%**，另外 ~50% 是数据传输 + 同步 + CPU 后处理。

### 3.5 优化方向

#### ~~优先级 P0：流水线化（CPU-GPU overlap）~~ — 已否决

> **否决原因**: 蒙特卡洛随机游走的计算过程存在**严格顺序依赖**——每条光线的追踪结果（交点位置、法线、材质属性）决定下一条光线的发射方向和起点（cascade 过程）。CPU 端的 cascade 计算必须等待本轮 GPU 光追结果 D2H 完成后才能确定下一轮光线参数，因此**无法将 H2D/kernel/D2H 与下一轮 CPU cascade 进行时间重叠**。双缓冲流水线的前提——连续 batch 之间的独立性——在本求解器架构中不成立。
>
> 非 kernel 开销（~50%）的构成主要是：① 每次 batch 的 H2D 传输（射线参数上传）；② cudaStreamSynchronize 等待；③ D2H 传输（命中结果下载）；④ CPU 端后处理（命中结果解析、材质查询、cascade 光线生成）。这些步骤与 kernel 执行之间形成严格的串行链：`CPU prepare → H2D → kernel → D2H → CPU cascade → 下一轮`。

#### 优先级 P1：降低寄存器压力（代码级优化）

86 regs/thread → 33% 理论占用率。以下是基于 `trace_rays_topk_kernel` 源码的详细寄存器消耗分析。

> **优先级说明**: 降低寄存器 → 提升占用率在 L2 crossbar 已达 78% 且 Long Scoreboard 为主停顿源的场景下，边际收益有限甚至可能负面（更多并发 warp → 更多 L2 请求 → 更严重的 crossbar 拥塞）。因此从 P0 降级为 P1，应在 L2 压力缓解后再实施。

##### 3.5.1 寄存器消耗来源拆解

Kernel 参数通过 constant memory 传入（不占寄存器），寄存器消耗完全来自线程局部状态：

**A. BVH 遍历状态（cuBQL `shrinkingRayQuery::forEachLeaf`）**

| 变量 | 类型 | 寄存器数 | 说明 |
|------|------|---------|------|
| `traversalStack[64]` | `Admin[64]` (64×8B) | **溢出到 local memory** | `Admin` = `uint64_t`（offset:48 + count:16 位域），512 bytes 远超寄存器容量，编译器必然溢出到 L1 cache 后备的 local memory |
| `*stackPtr` | 指针 | 1 | 栈顶指针 |
| `node` (当前节点) | `Admin` (8B) | 2 | 当前遍历节点的 offset+count |
| `rcp_dir` | `vec3f` | 3 | 射线方向倒数，整个遍历期间存活 |
| `n0`, `n1` (子节点对) | `Node` 结构体读取 | ~6 | 每次内层循环读取两个子节点的 bounds(6 floats)+admin(2 regs)，但 bounds 通常作为临时值 |
| `node_t0`, `node_t1` | float | 2 | 子节点射线-AABB 交点距离 |
| `o0`, `o1` | bool | ~0 | 编译器通常用 predicate 寄存器/flag |
| **小计** | | **~14** | + 512B local memory 溢出 |

**B. 射线状态**

| 变量 | 类型 | 寄存器数 | 说明 |
|------|------|---------|------|
| `org` | float3 | 3 | 射线原点（kernel 入口读入） |
| `dir` | float3 | 3 | 射线方向 |
| `tmin` | float | 1 | |
| `ray` (cuBQL) | `ray3f` | 8 | origin(3)+direction(3)+tMin(1)+tMax(1) |
| 重叠估计 | | -4 | `ray.origin`/`.direction` 与 `org`/`dir` 部分重叠（编译器可能复用） |
| **小计** | | **~11** | |

**注意**: `forEachLeaf` 接受 `ray` 为值传递，内部会创建 ray 副本。但编译器内联后可能优化掉重复。最坏情况下可能多占 ~4 regs。

**C. Top-K slots 数组（核心寄存器消耗者）**

`CUS3D_MAX_MULTI_HITS = 2`，每个 `cus3d_hit_result` 结构体:

```
struct cus3d_hit_result {
    int32_t   prim_id;      // 1 reg
    int32_t   geom_idx;     // 1 reg
    int32_t   inst_id;      // 1 reg  ← 单层追踪中恒为 -1
    float     distance;     // 1 reg
    float     normal[3];    // 3 regs
    float     uv[2];        // 2 regs
};  // = 9 regs/slot
```

| 变量 | 寄存器数 | 说明 |
|------|---------|------|
| `slots[0]` | 9 | 最近命中 |
| `slots[1]` | 9 | 次近命中 |
| `num_hits` | 1 | 当前命中计数 |
| `K` | 1 | 最大命中数（运行时 clamp 后的值） |
| **小计** | **20** | 在整个 BVH 遍历期间持续存活 |

**D. `intersect_prim` lambda 局部变量（叶节点处理时的瞬时峰值）**

| 变量 | 类型 | 寄存器数 | 说明 |
|------|------|---------|------|
| `geom_idx` | uint32 | 1 | |
| `ge` | `geom_gpu_entry` | 4 | prim_offset + prim_count + vertex_offset + flags |
| `t`, `u`, `v` | float | 3 | 交点参数 |
| `Nx`, `Ny`, `Nz` | float | 3 | 命中法线 |
| `did_hit` | bool | 1 | |
| `local_id` | uint32 | 1 | |
| `tri_idx` | uint3 | 3 | 三角形顶点索引 |
| `v0`, `v1`, `v2` | float3 | 9 | 三角形顶点坐标 |
| **小计** | | **~25** | |

**E. WBW（Watertight Barycentric Winding）射线-三角形交叉内部**

`ray_triangle_intersectWBW` 被内联后，其局部变量与 D 中 lambda 变量同时存活：

| 变量 | 类型 | 寄存器数 | 说明 |
|------|------|---------|------|
| `kx`, `ky`, `kz` | int | 3 | 主轴选择 |
| `Sx`, `Sy`, `Sz` | float | 3 | 剪切常数 |
| `A`, `B`, `C` | float3 | 9 | 平移后顶点 |
| `Ax`, `Ay`, `Bx`, `By`, `Cx`, `Cy` | float | 6 | 投影后 2D 坐标 |
| `U`, `V`, `W` | float | 3 | 边函数（缩放重心坐标） |
| `det`, `invDet`, `T`, `tHit` | float | 4 | |
| **小计** | | **~28** | 但大部分有短生命期，编译器可复用 |

**实际峰值**: 编译器会将 D+E 中短生命期变量重叠分配。估计 D+E 实际峰值 ~20-25 regs。

**F. 插入排序临时变量**: `pos`（1 reg），比较操作使用 slots 中已有值，额外 ~2 regs。

##### 3.5.2 寄存器预算总结

| 类别 | 估计寄存器数 | 生命期 |
|------|------------|--------|
| BVH 遍历固定状态 (A) | ~14 | 整个 kernel |
| 射线状态 (B) | ~11 | 整个 kernel |
| Top-K slots (C) | **20** | 整个 kernel |
| 叶节点处理峰值 (D+E) | ~22 (复用后) | 叶节点处理时 |
| 插入排序 (F) | ~2 | 命中时 |
| `tid`, 其他杂项 | ~3 | |
| **编译器分配总计** | **86** | （含编译器溢出/填充管理开销 ~14） |

**寄存器压力的核心问题**：slots[2] 数组（20 regs）在整个 BVH 遍历期间持续占用寄存器，无法与遍历状态和交叉测试变量做时间复用。这 20 个寄存器是"始终存活"的长生命期变量。

##### 3.5.3 具体优化手段与预期收益

**手段 1: slots[2] 移至 shared memory** — 预期节省 ~18 regs

将 Top-K 数组从寄存器移至 shared memory：

```cuda
__shared__ struct cus3d_hit_result s_slots[256 * CUS3D_MAX_MULTI_HITS]; // 256 threads/block
struct cus3d_hit_result* slots = &s_slots[threadIdx.x * CUS3D_MAX_MULTI_HITS];
```

| 指标 | 变化 |
|------|------|
| 节省寄存器 | -18 regs (9 regs/slot × 2 slots) |
| 新寄存器数 | 86 - 18 = **~68 regs/thread** |
| Shared memory 增加 | 256 × 2 × 36B = **18,432 bytes/block** |
| 新理论占用率 | 68×256 = 17,408 regs/block → floor(65536/17408) = **3 blocks → 24 warps → 50%** |
| Shared memory 限制 | 18,432 × 3 = 55,296 bytes < 100 KB (SM 8.9) → **不是限制因素** |
| **风险** | **低**。slots 访问模式为纯线程私有（无 bank conflict），shared memory 延迟 ~20 cycles vs 寄存器 0 cycles，但插入排序每次遍历叶节点才触发，非热路径 |

**手段 2: 去除 `inst_id` 字段（单层追踪专用 kernel）** — 预期节省 2 regs

在 `trace_rays_topk_kernel`（单层 BVH）中，`inst_id` 恒为 -1。在 slots 中移除该字段：

| 指标 | 变化 |
|------|------|
| 节省寄存器 | -2 regs (1/slot × 2 slots) |
| 实现 | 定义 `cus3d_hit_result_noinst`（8 fields），仅在单层 kernel 中使用 |
| **风险** | **低**。需要在 kernel 输出端补填 `inst_id = -1` |

**手段 3: WBW → Möller-Trumbore 切换** — 预期节省 ~8-10 regs

代码中已有 `ray_triangle_intersectMT` 实现（[cus3d_math.cu](stardis-cus3d/custar-3d/0.10/src/cus3d_math.cu#L34)）。MT 算法使用更少的中间变量：

| 算法 | 内联后中间变量（峰值） | 特性 |
|------|---------------------|------|
| WBW (当前) | ~28 regs | 精确防水密（watertight），无缝隙 |
| MT | ~18 regs | 经典算法，极端边缘情况可能有微小缝隙 |

| 指标 | 变化 |
|------|------|
| 节省寄存器 | -8~10 regs（中间变量生命期缩短） |
| 实现 | 修改 `ray_triangle_intersect` 转发到 `ray_triangle_intersectMT` |
| **风险** | **中**。WBW 保证相邻三角形间无缝隙（watertight），对蒙特卡洛随机游走的鲁棒性更好。切换 MT 后需验证 porous 场景下是否出现光线泄漏（通过 IR 渲染结果对比）。此手段不建议在无充分测试的情况下使用 |

**手段 4: 法线计算合并到交叉函数** — 预期节省 ~6 regs

当前流程：WBW 计算交点 → 返回后重新计算 `e1 = v1 - v0; e2 = v2 - v0; N = cross(e1, e2)`。`v0`, `v1`, `v2` 在交叉计算内部已存在但被丢弃。修改交叉函数签名，直接输出法线：

```cuda
__device__ bool ray_triangle_intersect(
    ..., float* t_out, float* u_out, float* v_out,
    float3* normal_out);  // 新增：直接输出非归一化法线
```

| 指标 | 变化 |
|------|------|
| 节省寄存器 | -6 regs（消除 `e1`, `e2` 重复计算时的 float3 重新分配） |
| 实现 | 修改 WBW 函数，末尾直接计算 cross product 并通过指针输出 |
| **风险** | **低**。纯局部重构 |

**手段 5: BVH 遍历栈深度缩减（64 → 32）** — 间接优化

| 指标 | 变化 |
|------|------|
| 节省 local memory | 512B → 256B/线程 |
| 节省寄存器 | 0（栈已溢出到 local memory） |
| **实际收益** | 降低 L1 cache 压力。当前 L1/TEX 利用率 ~38%，有一定改善空间 |
| **风险** | **低-中**。需确认场景 BVH 深度不超过 32。porous 场景三角形数 ~数万，BVH 深度 ~20-25，32 层足够 |
| **注意** | 需修改 cuBQL 库源码（`thirdparty/cuBQL/cuBQL/traversal/rayQueries.h` L357） |

**手段 6: 编译器 `--maxrregcount` 强制限制** — 快速实验

| 设置 | 寄存器数 | 占用率 | L2 crossbar 预估影响 |
|------|---------|-------- |---------------------|
| 当前 | 86 | 33.3% | 78% (实测) |
| `--maxrregcount=72` | ≤72 | ~41.7% (↑8%) | ~82% (轻微加剧) |
| `--maxrregcount=64` | ≤64 | ~50% (↑17%) | ~90% (可能拥塞) |
| `--maxrregcount=48` | ≤48 | ~66.7% (↑33%) | **可能超 95%** |

| 指标 | 说明 |
|------|------|
| **风险** | **中-高**。强制截断会导致编译器将溢出变量存入 local memory（L1 cache 后备），增加内存流量 |
| **建议** | 首先测试 `--maxrregcount=72`，观察性能变化方向。若正向则继续降低；若负向则不适用于本 kernel |

##### 3.5.4 组合优化方案与占用率预测

| 方案 | 手段组合 | 预期 regs/thread | 理论占用率 | blocks/SM |
|------|---------|-----------------|-----------|-----------|
| **基线** | 无 | 86 | 33.3% | 2 |
| **A (shared slots)** | 1 | ~68 | **50.0%** | 3 |
| **A + inst_id** | 1+2 | ~66 | 50.0% | 3 |
| **A + inst_id + normal** | 1+2+4 | ~60 | **50.0%** | 3 |
| **A + inst_id + normal + MT** | 1+2+3+4 | ~52 | **66.7%** | 4 |

> **注意**: 即使 regs 降到 52，在 L2 crossbar 已达 78% 的约束下，66.7% 占用率的额外 warp 可能无法充分利用——更多的并发内存请求可能导致 crossbar 拥塞，反而降低吞吐。**最佳方案可能是 A（shared slots），将占用率从 33.3% 提升到 50%，在 latency hiding 与 bandwidth contention 之间取得平衡。**
>
> **补充（基于 §2.8 新数据）**: Avg. Active Threads = 6.45/32 意味着占用率提升的边际收益更低——新增 warp 的线程活跃度同样仅 ~20%。提升占用率主要价值在于 latency hiding（更多 warp 可供调度器选择以掩盖 L2 延迟），而非增加有效并行度。
>
> **建议**: 先实施 P0（射线空间排序）降低 L2 压力和线程分歧后，再评估是否仍需通过降寄存器提升占用率。空间排序改善 L2 合并因子后，L2 crossbar 利用率可能从 78% 降至 40–50%，此时提升占用率才有正向收益。

#### 优先级 P0：减少 L2 压力（射线空间排序 + BVH 优化）

##### 3.5.5 当前射线排序的局限性分析

Persistent solver 已实现 **type-based 桶排序**（`pool_collect_ray_requests_bucketed`，B-4 M2），按 `ray_bucket_type` 将射线分为 5 类（RADIATIVE / STEP_PAIR / SHADOW / STARTUP / ENCLOSURE），使同类射线在 `ray_requests[]` 数组中连续排列。

然而，GPU 光追 kernel（`trace_rays_topk_kernel`）是**单一通用 kernel**——所有射线执行完全相同的 BVH 遍历 + 三角形/球体相交逻辑，**kernel 内部没有任何基于射线类型的分支**。bucketed 后的射线仍作为一整个连续数组传入同一个 `s3d_scene_view_trace_rays_batch_ctx`，并未按桶分离 launch。

| 分桶带来的效果 | 实际收益 | 是否空间相关 |
|---------------|---------|-------------|
| `range` 一致性（shadow 短/radiative ∞） | 轻微 — 减少同 warp 内遍历深度差异导致的空等 | **间接**，仅对齐 tMax |
| 方向分布（enclosure 轴对齐 vs radiative 随机） | 极低 — **桶内射线方向仍完全随机** | **否** |
| 起点局部性 | **无** — 未按 origin 排序 | **否** |

**结论**：type-based 桶排序对 BVH traversal 的 warp coherence 几乎无帮助。决定 warp 内线程是否遍历相同 BVH 节点的关键因素是**射线的空间属性**（origin 邻近性 + direction 相似性），而非语义类型。

**§2.8 新数据的佐证**：Avg. Active Threads = 6.45/32 直接量化了这一问题——当前 warp 内 ~80% 的线程在 BVH 遍历时被 predicate-off，根因是同一 warp 内的射线走向完全不同的 BVH 子树。空间排序的核心目标正是**提升 warp 内射线的子树重合度，从而提高 Active Threads 比例并减少 predicate-off 的浪费**。

##### 3.5.6 L2 压力的定量模型

**A. BVH 节点结构与内存访问模式**

cuBQL `BinaryBVH<float,3>::Node`（`CUBQL_ALIGN(16)`）：

| 字段 | 大小 | 说明 |
|------|------|------|
| `box_t<float,3> bounds` | 24 B | lower(3×4B) + upper(3×4B) |
| `Admin admin` (offset:48 + count:16) | 8 B | 子节点偏移 / 叶节点原语 |
| **合计** | **32 B** | = 1 L2 sector |

shrinkingRayQuery 每步加载 2 个子节点 `nodes[offset]` 与 `nodes[offset+1]`（内存连续），共 64 B = 2 sectors，恰好落入 1 条 L2 cache line（128 B）。

**B. 单射线 L2 读取量**

设 $\bar{V}$ 为每条射线平均访问的 BVH 内节点数（含叶节点的原语加载），$s = 32\text{B}$ 为 sector 大小：

$$B_{ray} = \bar{V} \times 2s + \bar{L} \times s_{leaf}$$

其中 $\bar{L}$ 为平均访问叶节点数，$s_{leaf}$ 为叶节点的原语数据加载量（`primIDs` + `vertices` + `indices` 等）。对于 porous 场景（BVH 深度 ~18-20，典型 $\bar{V} \approx 30$–$60$），保守估计：

$$B_{ray} \approx 40 \times 64\text{B} + 3 \times 128\text{B} \approx 2.9\text{KB}$$

**C. Warp 级 L2 请求合并**

定义 **warp 合并因子** $C_{coal}$：同一 warp 内平均有多少线程共享同一 sector 的加载（1 = 无共享，32 = 完全共享）。

单 warp 实际 L2 sector 请求量：

$$S_{warp} = \frac{32 \times \bar{V} \times 2}{C_{coal}}$$

$C_{coal}$ 随 BVH 层级而变：

| BVH 层级 $d$（0=root） | 该层节点数 $2^d$ | 随机射线 $C_{coal}$ | Morton 排序后 $C_{coal}$ |
|------------------------|-----------------|-------------------|------------------------|
| 0–4（顶层） | 1–16 | $\min(32, 32/2^d)$ ≈ 2–32 | 同 |
| 5–10（中层） | 32–1024 | **≈ 1**（各线程不同子树） | **4–16**（邻近射线共享子树） |
| 11–18（底层） | >2048 | **≈ 1** | **1–4** |

**关键**：顶层节点即使不排序也有良好共享（节点数 < 32）。类型桶排序对中底层无改善。只有空间排序能提升中层和底层的 $C_{coal}$。

**D. 加权平均合并因子**

将所有遍历层级的 $C_{coal}(d)$ 按访问频次加权平均：

$$\bar{C}_{coal} = \frac{\sum_{d=0}^{D} w_d \cdot C_{coal}(d)}{\sum_{d=0}^{D} w_d}$$

其中 $w_d$ 为第 $d$ 层的平均访问次数。对 porous 场景的估算：

| 排序方式 | $\bar{C}_{coal}$ 估算 | 理由 |
|---------|---------------------|------|
| **当前（type-based 桶）** | **~1.3** | 仅顶层 ~5 层有自然共享，中底层 ~15 层无共享 |
| **Morton-code 空间排序** | **~3–6** | 中层显著改善（~10 层 $C_{coal}$ 提升至 4–16），底层有限 |

**E. 总 L2 Sector 请求量与 Kernel 时间关系**

总 L2 sector 请求量：

$$S_{total} = \frac{N_{rays}}{32} \times S_{warp} = \frac{N_{rays} \times \bar{V} \times 2}{\bar{C}_{coal}}$$

kernel 在 L2-bound 时，执行时间由 L2 吞吐决定：

$$\boxed{T_{kernel} \approx \frac{S_{total} \times s}{BW_{L2,eff}} = \frac{N_{rays} \times \bar{V} \times 2s}{\bar{C}_{coal} \times BW_{L2,eff}}}$$

其中 $BW_{L2,eff}$ 为有效 L2 吞吐（包含 crossbar 拥塞损耗）。

**空间排序对 kernel 时间的改善比**：

$$\frac{T'_{kernel}}{T_{kernel}} \approx \frac{\bar{C}_{coal,\,before}}{\bar{C}_{coal,\,after}}$$

代入估算值：

$$\frac{T'_{kernel}}{T_{kernel}} \approx \frac{1.3}{4.5} \approx 0.29 \quad \Rightarrow \quad \textbf{约 3.5× 加速}$$

但这是纯 L2 受限部分的上界。考虑到：
1. Kernel 中还有不受 L2 约束的计算部分（SOL Compute ~23%）
2. 排序本身有 CPU 开销（Morton code 计算 + radix sort ≈ $O(N_{rays})$）
3. 蒙特卡洛求解器的射线 origin 分布不完全随机（cascade 过程中相邻探针的射线有空间局部性）

**实际预期加速**：kernel 部分 **1.5×–2.5×**。

**F. 线程分歧对 L2 请求的放大效应（基于 §2.8 数据）**

上述模型假设 warp 内 32 条线程都在做有效遍历。但实测 Avg. Active Threads = 6.45/32，意味着同一 warp 的遍历步骤中，实际只有 ~6 条线程在执行同一分支路径。这对 L2 请求的影响：

$$S_{warp,\,actual} = \frac{32 \times \bar{V} \times 2}{C_{coal}} \times \frac{32}{\bar{A}}$$

其中 $\bar{A} = 6.45$ 为平均活跃线程数。因为 warp 需要依次执行所有分支路径（每个分支 ~6 线程），总指令数膨胀 $32/\bar{A} \approx 5\times$，每条指令都可能触发不同的 BVH 节点加载。

然而，predicate-off 的线程不发出内存请求，因此 L2 sector 请求量并不完全按 $32/\bar{A}$ 倍放大——但总遍历步数增加意味着 kernel 执行时间依然按 $\sim 32/\bar{A}$ 膨胀。

空间排序对线程分歧的改善效应：

| 排序方式 | 预期 Avg. Active Threads | 改善比 |
|---------|------------------------|--------|
| 当前（type-based） | 6.45 | 1× |
| Morton-code 空间排序 | ~10–16 | **1.5×–2.5×** |
| Morton + direction octant | ~12–20 | **2×–3×** |

> 空间排序使线程分歧改善 + L2 合并改善**叠加**，kernel 实际加速可能达到 **2×–4×**。

**G. 对端到端 GPU 阶段的影响**

从 §3.4 知 kernel 执行占 GPU 阶段 ~50%（另 50% 是 H2D/D2H/sync）。设 kernel 加速 2×：

$$T'_{GPU} = 0.5 \times T_{GPU} \times \frac{1}{2} + 0.5 \times T_{GPU} = 0.75 \times T_{GPU}$$

**端到端 GPU 阶段预期改善 ~25%。**

##### 3.5.7 具体优化手段

- **空间射线排序（Morton code）**: 在 `pool_collect_ray_requests_bucketed` 之后（或替代之），对 `ray_requests[]` 按 origin 的 30-bit Morton code 做 radix sort。复杂度 $O(N_{rays})$，对 ~128K 射线估计 <0.1ms。实现路径：① 计算每条射线 origin 的 Morton code → ② 并行 radix sort → ③ 按排序后顺序重排 `ray_requests[]` 和 `ray_to_slot[]` 映射
- **BVH 布局优化**: 确保 cuBQL builder 的节点排列已采用 DFS/BFS 混合布局（cuBQL 默认采用 DFS 布局，对 L2 cache line 已较友好）。进一步优化可尝试 van Emde Boas 布局或 treelet reordering
- **Shared memory BVH 预缓存**: 将 BVH 顶层 $k$ 层（$2^{k+1}-1$ 个节点）预加载到 shared memory，消除顶层的 L2 请求。$k=6$：127 个节点 × 32B = 4,064 B/block，开销可接受。但注意顶层已有良好共享（$C_{coal}$ 高），收益有限（若实施 P1 手段 1 slots→shared mem，需协调 shared memory 预算）

#### 优先级 P2：补全内存细节数据

Warp Stall 数据已通过 §2.8 补全（Long Scoreboard on L1TEX op 是主因）。仍缺失 MemoryWorkloadAnalysis 的 sector 级数据（L1/L2 hit rate、miss rate），这对**验证空间排序后 L2 合并因子的实际改善**至关重要。建议在实施 P1 空间排序前后各 profile 一次以量化效果。

### 3.6 汇总

| 维度 | 发现 | 严重程度 | 可操作性 |
|------|------|---------|---------|
| **线程分歧** | Active Threads = 6.45/32 (20%)，warp 有效吞吐仅 3.2% | **极高** | **高（空间射线排序可提升至 10–16/32）** |
| **Long Scoreboard 停顿** | L1TEX op 主停顿，Est. Speedup ~40% | **高** | 高（与 L2 压力和分歧联合优化） |
| **L2 Crossbar 瓶颈** | 78% 利用率 | 高 | 中（射线排序 + BVH 布局） |
| **低占用率** | 27%/33%，受 86 regs 限制 | 中 | **高（slots 移 shared mem → 50%）** |
| **非 kernel 开销** | GPU 阶段 ~50% 是传输+同步 | 高 | **不可操作**（严格顺序依赖） |
| **FP64 = 0** | 未使用双精度 | — | 已最优 |
| **SM 负载不均** | 4 个 SM 偏低，tail effect | 低 | 低 |

### 3.7 推荐实施顺序

1. **P0 射线空间排序**: Morton-code radix sort（**最高优先级** — 同时改善线程分歧 + L2 合并，预期 kernel 2×–4× 加速）
2. **P0 BVH 顶层预缓存**: Shared memory 预加载 BVH 顶层节点（与排序互补）
3. **P1 手段 1**: slots → shared memory（低风险，latency hiding 改善——在 L2 压力缓解后实施收益更大）
4. **P1 手段 4**: 法线计算合并（低风险，中等收益）
5. **P1 手段 2**: 去除 inst_id（低风险，小收益）
6. **P2**: 补全 MemoryWorkloadAnalysis（排序前后对比量化效果）
7. **P1 手段 5**: 缩减 BVH 栈深度 64→32（低-中风险，L1 间接收益）
8. **P1 手段 6**: `--maxrregcount` 实验（快速验证，需排序后重新评估）
9. **P1 手段 3**: WBW → MT 切换（需严格验证，最后手段）

---

*分析完成: 2026-02-18*
*优化方案更新: 2026-02-19 — 否决 CPU-GPU overlap，新增寄存器压力详细分析*
*优先级调整: 2026-02-20 — 基于 WarpStall profile 数据，L2 压力(射线排序) P0↑，寄存器压力 P1↓*