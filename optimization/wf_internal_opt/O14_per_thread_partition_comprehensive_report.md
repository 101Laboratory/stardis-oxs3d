# O14: per-thread 固定分区 — 综合分析报告

**创建日期**: 2026-03-18  
**基于**: O9 综合报告实测经验 + oxs3d-merge-phase 代码深度探索 + pool scaling 实验  
**场景**: porous 320×320 spp=32，merge-phase 架构 (O11+O12+O13)  
**当前基准**: 119.5s wall (pool=16384, 32线程)，merged_pass=78.5s (65.7%)  
**状态**: ❌ 结题关闭 — 实测 v1(直扫)+v2(分区内compact) 均全面劣化，见末尾实验结论  

---

## TL;DR

merged_pass 在 pool=32K+ 时出现超线性性能劣化（L3 cache miss 主导），阻止 pool_size 进一步增大提供更长 GPU hiding 窗口和更大 GPU batch。O14 的核心机制是**per-thread 静态分区**：每个 OMP 线程被永久绑定到池子的一个 slot 区间，从而将 slots[] 的跨步 L2/L3 命中率从 **~0% 提升至 ~99.97%**（基于每步 0.03% 的 slot turnover），彻底解耦 per-iteration 成本与 pool_size。

---

## 1. 优化动机：pool_size 推高的阻碍

### 1.1 当前架构的 GPU hiding 瓶颈

O13 异步提交线程实现了 GPU submit 与对侧 half-cycle CPU 工作的完整重叠：

```
Half-A:  [merged_pass ≈78s] [compact+refill ≈9s] [housekeeping]
                ↕ 完全重叠
Half-B:  ······· [gpu_submit: H2D→kernel→D2H ≈30s] ·······
```

**已达到当前隐藏下限**：mp 时间 78.5s 已无法继续压缩——再压缩 mp 就会暴露 gpu_wait，浪费 GPU 计算时间（用户实验确认，未归档）。

### 1.2 推高 pool_size 的价值

增大 pool_size 有两层收益：

1. **延长 GPU hiding 窗口** → mp 时间增加 → gpu_submit 被更彻底隐藏
2. **提升 GPU 单批次并行宽度** → kernel occupancy 更高 → throughput 更高

理论上，若 mp 时间保持线性增长（与 pool_size 等比例增加），总任务不变（MC 采样数固定），wall time 应保持不变或因 GPU throughput 提升而下降。

### 1.3 超线性劣化机制

当前 `merged_pass` 使用 `#pragma omp for schedule(dynamic, 64)` 跨全部 active slot：

```
Step N:   Thread 0 → active_indices[0..63]    → slots[71, 204, 389, ...]  (散布)
Step N+1: Thread 0 → active_indices[0..63]    → 完全不同的 slots（dynamic 重分配）
```

32 个线程通过 dynamic scheduling 在每步之间重新分配 chunk，导致：

| pool_size | slots[] 总量 | 32线程并发足迹 | L3 (36MB) 状态 |
|-----------|-------------|---------------|----------------|
| 16K | 32MB | full access | ✅ 勉强 warm（跨步依赖 O7 prefetch） |
| 32K | 63MB | full access | ⚠️ L3 thrash，线程互相驱逐 |
| 64K | 126MB | full access | ❌ 全面 DRAM |

**关键机制**：dynamic scheduling 每步为线程分配不同 slot 序号 → 上一步温热的 L2/L3 cache line 在下一步被其他线程的 slot 覆盖 → **跨步 L2 命中率 ~0%**。

O9 报告的 pool=32K 实测数据（原始 stardis-cus3d 架构）：
- cascade: pool=16K → 40.2s；pool=32K → **106.6s (2.65×)**
- distribute: pool=16K → 32.9s；pool=32K → **108.8s (3.31×)**

---

## 2. O14 核心机制：per-thread 固定分区

### 2.1 方案定义

```
pool_size = N 个 slot
nthreads  = T 个 OMP 线程
分区大小  P = N / T

Thread i 被永久绑定到 slots[base + i×P .. base + (i+1)×P)
```

merged_pass 中每个线程直扫自己的分区：

```c
// 新的 merged_pass inner loop（概念伪码）
#pragma omp parallel num_threads(nthreads)
{
  int tid = omp_get_thread_num();
  size_t p_begin = pv->base + (size_t)tid * partition_size;
  size_t p_end   = p_begin + partition_size;

  for (size_t slot = p_begin; slot < p_end; slot++) {
    if (!pool->hot_arr[slot].active) continue;
    /* Phase A: distribute */
    /* Phase B: cascade   */
    /* Phase C: collect   */
    /* Phase D: harvest   */
  }
  // implicit barrier at omp parallel exit
}
```

**不再使用 `active_indices[]` array**（compact 的输出变成 per-partition done list）。compact 本身也按分区并行，线程 i 只扫描自己分区写 `tl_done[tid]`。

### 2.2 跨步 L2 热度定量分析

**每步 slot turnover 率**（基于 porous 场景实测数据）：

```
pool=32K:  完成路径数/步 = 3,276,800 / 290,719 = 11.3 paths/step
per-partition (P=1024): 11.3 / 32 = 0.35 paths/step
turnover 率 = 0.35 / 1024 = 0.034%
```

相邻两步之间，同一分区内 **99.97% 的 slot** 是同一条路径（未完成，未 refill）。

| 度量 | dynamic,64（当前）| per-partition（O14）|
|------|------------------|--------------------------|
| 跨步 L2 slot 命中率 | ~0%（完全重分配） | **~99.97%** |
| 每步每线程冷加载量 | 1024×2040B = **2.1MB** | 0.35×2040B = **714B** |
| 相对冷加载减少 | 基准 | **~2800×** |

这是**量级而非比例的改善**，一次 cold start 换来几乎无限重用。

### 2.3 分区内存足迹 vs cache 层级

| 数据结构 | per-partition (P=1024) | L2 1MB fit | L2 2MB fit |
|---------|----------------------|------------|------------|
| `hot_arr` 分区 | 1024×8B = **8KB** | ✅ L1 | ✅ L1 |
| `slots[]` DS 有效触碰 (712B) | 1024×712B = **695KB** | ✅ | ✅ |
| `slots[]` 全量 (2040B) | 1024×2040B = **2.0MB** | ⚠️ 边界 | ✅ |
| `enc_arr` 分区 | 1024×596B = **596KB** | ✅ | ✅ |
| `ext_arr` 分区 | 1024×360B = **360KB** | ✅ | ✅ |
| `sfn_arr` 分区 | 1024×3700B = **3.6MB** | ❌ → L3 | ❌ → L3 |

DS 路径（cascade 时间占比 85%+，每次触碰 path_core 480B + cnd_ds 232B = 712B）的有效工作集 695KB 完整 fit in L2。`sfn_arr` 仅 Picard-N 路径使用（极罕见），L3 cool-down 可接受。

### 2.4 pool_size 增大时的线性 scaling

当 pool_size 加倍时，per-partition 大小 P 也加倍（nthreads 不变），但**跨步 L3 warmth 机制仍然成立**：

```
pool=64K, P=2048:
  slots[] per-partition: 2048×2040B = 4.1MB → L3 (36MB 总量, 32 分区 ≠ 竞争)
  有效 DS 触碰: 2048×712B = 1.4MB → L2 fit（2MB核）
  跨步冷加载: 0.35×2040B ≈ 714B（与 pool=32K 相同！turnover 不变）
```

**每步冷加载绝对量与 pool_size 无关**（由 turnover 率决定，turnover 率 ≈ 总完成率/T = 常数）。这正是解耦 per-iteration 成本与 pool_size 的关键。

---

## 3. 负载均衡分析

### 3.1 固定分区的统计均衡性

cascade 成本由路径类型决定。DS(80%) 均匀分布于 slot 空间时，P=1024 分区内成本方差：

```
DS count ~ Binomial(1024, 0.80)
σ = √(1024 × 0.80 × 0.20) = 12.8
CV = 12.8 / 820 = 1.6%
99.9% CL 最坏分区比 = 1 + 3×1.56% ≈ 1.047×
```

**~5% 的分区间不均衡**，对应 barrier 等待 ≈ 78.5s × 5% = 3.9s。  
相比于消除超线性 L3 miss（pool=32K 节省 ~47s+），代价完全可接受。

### 3.2 warm-up 期的相关性风险

`refill_pool` 按顺序从 `task_queue` 分配 task 到 done slot，意味着初始填充时：
- 相邻 slot → 相邻 pixel task → 可能相邻像素的路径类型有短期相关性
- **持续 ~10-20 步后衰减**（各 slot 的随机游走路径快速去相关）

缓解方案（可选）：初始 `fill_pool` 时对 task 分配做 stride 交织：
```c
// stride 分配：slot i 获取 task[i % nstride × (pool_size/nstride) + i/nstride]
```
成本极低，彻底消除初期相关性。

---

## 4. 与现有架构的兼容性

### 4.1 组件影响矩阵

| 组件 | 当前行为 | O14 变更 | 兼容性 |
|------|---------|---------|-------|
| `merged_pass` 内层循环 | `omp for` 按 `active_indices[ph]` | 直扫 `slots[p_begin..p_end]` | ✅ 局部改动 |
| `compact_active_paths` | 串行扫全 pool → `active_indices[]` | 并行各扫分区 → `tl_done[tid][]` | ✅ 分区化重写 |
| `refill_pool` | 串行扫 `done_indices[]` + parallel init | per-thread 用 `tl_done[tid][]` | ✅ 分区化适配 |
| O7 software prefetch | `active_indices[ph+4]` | `slot + 4`（分区内连续） | ✅ 更简单 |
| `tl_ray_bufs` / ray flush | per-thread，`_InterlockedExchangeAdd64` | 不变 | ✅ 无变化 |
| GPU submit pipeline (O13) | 独立提交线程 | 不变 | ✅ 无变化 |
| dual-buffer (P2/O12) | `pv->base` 偏移 | 分区 base = `pv->base + tid×P` | ✅ 适配 pv->base |
| `step_*` 函数全部 | 操作 `path_state*` | 不变（data layout 不变） | ✅ 零改动 |
| `path_state` 布局 | 2040B AoS | **不变** | ✅ 规避 O9 所有失败根因 |

**data layout 完全不变**——这是 O14 相对 O9 的决定性优势：O9 因改 layout 触发的 TLB/prefetch/memset 失败在 O14 中完全不适用。

### 4.2 O14 与 O9 失败根因的关系

| O9 失败根因 | O14 是否触发 | 原因 |
|------------|------------|------|
| refill: 8× 散射 memset (6652B) | ❌ 不触发 | layout 不变，仍是单次 `memset(slots[i], 2040B)` |
| compact: 4B SoA 退化为 480B stride | ❌ 不触发 | compact 仍读 `hot_arr`（8B SoA），不变 |
| collect: 双数组随机访问 TLB 翻倍 | ❌ 不触发 | collect 仍从单一 AoS 读 ray_req |
| cascade: 5 数组 TLB/prefetcher 耗竭 | ❌ 不触发 | 始终是单一 `slots[]` 地址流 |
| pool=32K 超线性劣化 | ❌ **完全消除** | 分区隔离防止线程间 cache 驱逐 |

---

## 5. 预期收益模型

### 5.1 mp 时间 vs pool_size 的 scaling 改善

```
                当前              O14
pool=16K   78.5s (基准)      78.5s (基准，+cold start overhead ≈0）
pool=32K   ~125-160s (超线性) ~81-84s (≈1.04-1.07×，仅 barrier+initload 增量)
pool=64K   ~250-350s (发散)   ~84-90s (≈1.07-1.15×，L3 boundary 效应)
```

*注：pool=32K/64K 时 O14 mp 时间略高于 pool=16K，来自：*
1. *更多 slot → 扫 inactive slot 的空转轮询成本（每 slot ~1ns 的 hot_arr 读）*
2. *partition 初始冷加载（仅每 cycle 第 1 步，steady state 不计）*

### 5.2 整体 wall time 预期

| pool_size | 当前 wall | O14 wall | 变化 |
|-----------|----------|---------|------|
| 16K | 119.5s（基准）| ~119.5s | 中性 |
| 32K | ~190-260s（超线性）| **~95-105s** | **-55~-165s** |
| 64K | 不可用 | **~90-100s** | GPU batch更大→throughput ↑ |

pool=64K 的 GPU 收益来自：fewer steps × larger batch → GPU kernel occupancy 更高 → 实际 throughput 提升（需实测验证具体幅度）。

---

## 6. 与其他优化的关系

| 优化项 | O14 后状态 | 交互 |
|--------|----------|------|
| O7 software prefetch | `slot+4` 比 `active_indices[ph+4]` 简单，prefetch 精度更高 | ✅ 正向 |
| O9 path_state SoA 域分解 | O14 先验证，若 pool=64K 仍不满足再考虑 | 正交，可叠加 |
| O11/O12/O13 (merged_pass/async/dual-buffer) | 完全兼容 | ✅ 无干扰 |
| gpu_launch 优化（方案 E/F） | 独立，O14 不影响 GPU side | ✅ 独立正交 |

---

## 7. 结论

| 决策项 | 结论 |
|--------|------|
| 超线性劣化根因 | dynamic scheduling 导致跨步 L2 命中率 ~0%；是 pool scaling 的真正杀手 |
| O14 机制是否可行 | ✅ 是——per-thread 静态分区将跨步 L2 命中率提升至 ~99.97%，与 pool_size 解耦 |
| data layout 是否需要改变 | **否** — 规避 O9 所有失败根因 |
| 负载均衡风险 | 低（~5% 统计不均，vs 2800× cache miss 减少） |
| 实施复杂度 | **低** — merged_pass/compact/refill 局部改动，step_* 零改动 |
| 预期收益 | pool=32K wall time 从 ~190-260s → **~95-105s**；pool=64K 进一步优化 GPU throughput |
| 是否值得实施 | **是**，ROI 极高，代码改动量远小于 O9 |

---

*报告创建: 2026-03-18 | 状态: ❌ 结题关闭*

---

## 8. 实验结论（2026-03-18）

### 8.1 实测数据

**v1（分区直扫）** — commit `f64ea07`：

| Pool | cpuA Δ% | cpuB Δ% | cycle Δ% | compact+rfill |
|------|---------|---------|----------|---------------|
| 8K   | +7%     | +7%     | +5%      | -5% (改善)    |
| 16K  | +8%     | +8%     | +8%      | -8%           |
| 32K  | +12%    | +12%    | +12%     | -13%          |

**v2（分区内 compact list 迭代）** — commit `635d661`：

| Pool | cpuA Δ% | cpuB Δ% | cycle Δ% |
|------|---------|---------|----------|
| 8K   | +13.4%  | +16.0%  | +4.9%    |
| 16K  | +7.0%   | +8.7%   | +7.6%    |
| 32K  | +13.6%  | +14.3%  | +12.3%   |

两版均**全面劣化**，且回归幅度一致——v2 消除直扫开销后毫无改善。

### 8.2 根因修正

原分析假设"跨线程 L2/L3 缓存驱逐"是 dynamic scheduling 的核心缺陷。实验否定了这一假设。

**真实根因**：超线性 per-slot 成本增长来自 **L3 capacity miss**（slots[] 总 footprint 溢出 L3），而非 conflict miss。调度策略无法修复容量缺失——64MB 数据塞不进 36MB L3。

Baseline `schedule(dynamic, 64)` 的窄"扫过 front"（4 线程通过共享原子计数器隐式同步，在 slots[] 的相近区域工作）实际上最大化了 L3 有效热区的集中度。固定分区反而让 32 个线程同时访问 slots[] 的 32 个不同区域，L3 热区散布。

更重要的是：path_state 加载仅占 per-slot 时间的 ~0.35%（1600B/25 cache lines = 2.5μs vs per-slot 707~1930μs）。瓶颈是 cascade 子步中场景只读数据（BVH/材料/几何体）的 L3 争用——path_state 的 50MB footprint 挤占了 36MB L3，导致场景数据被完全驱逐。

### 8.3 结论

| 判断 | O14 分析预测 | 实验结果 | 修正 |
|------|------------|---------|------|
| 固定分区消除跨线程驱逐 | merged_pass 显著加速 | 全面劣化 | 分区散布 L3 热区，比窄 front 更差 |
| L2 跨步 99.97% 命中 | per-slot 成本与 pool_size 解耦 | per-slot 成本继续超线性增长 | 瓶颈不在 slot 重用，而在容量缺失 |
| pool=32K wall 95-105s | 大幅优于 baseline | 比 baseline 更差 | 预估完全无效 |

**O14 方向已证伪。有效方向需减小 path_state 对 L3 的占用（释放空间给场景数据），或降低 TLB 压力（Huge Pages），或提升场景数据访问相干性（空间排序）。**
