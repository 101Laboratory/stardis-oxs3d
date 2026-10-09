# 批量光追 Batch Trace 显存带宽压力分析

**日期**: 2026-02-14  
**硬件**: RTX 3070 Laptop (GA104, 5120 CUDA cores, 40 SMs, GDDR6 384 GB/s, 4MB L2, PCIe Gen4 ×16 ~25 GB/s)  
**触发**: NVidia Compute Profiler 显示满宽度并发时计算占用率不足，带宽瓶颈  
**分析范围**: `cus3d_trace_ray_batch_multi` + `trace_rays_batch_impl` 完整调用链

---

## 瓶颈 1（严重）：Top-K 输出数据量爆炸 — 每条射线 292B

`cus3d_types.h` 中结构体大小：

| 结构体 | 大小 | 说明 |
|--------|------|------|
| `cus3d_hit_result` | **36B** | 单次命中 (int32×3 + float×6) |
| `cus3d_multi_hit_result` | **4 + 8×36 = 292B** | Top-K=8 命中 |

Kernel 写回时（`cus3d_trace.cu` topk kernel），**无论实际命中数多少，都写满 8 个 slot**：

```c
results[tid].count = num_hits;
for (int i = 0; i < num_hits; i++)
    results[tid].hits[i] = slots[i];
for (int i = num_hits; i < CUS3D_MAX_MULTI_HITS; i++)
    results[tid].hits[i].prim_id = -1;  // 仍然写 8 slots
```

**对比**: 单命中 kernel 只写 36B/ray。Top-K 版本 **8.1× 写带宽膨胀**。

以 N=10,000 射线为例：
- 单命中输出: 10K × 36B = **360 KB**
- Top-K 输出: 10K × 292B = **2.92 MB**

---

## 瓶颈 2（严重）：Top-K slots 寄存器溢出到 Local Memory

`trace_rays_topk_kernel` / `trace_rays_instanced_topk_kernel` 中每个线程维护：

```c
struct cus3d_hit_result slots[CUS3D_MAX_MULTI_HITS]; // 8 × 36B = 288B per thread
```

GA104 每个 SM 有 **64KB 寄存器文件**（65536B = 256 个 32-bit 寄存器/线程 × 最大线程数）。288B ≈ 72 个 float 寄存器，加上 BVH 遍历变量、lambda 捕获变量（ray, org, dir, tmin, tmax, hit 状态等约 30+ 个寄存器），**总寄存器需求 ~100+ 个/线程**。

后果：
- 编译器会将 `slots[]` 数组 **spill 到 local memory**（实际存储在 global memory，通过 L1/L2 缓存）
- 每次 insertion-sort `slots[pos] = slots[pos-1]` 产生 local memory **读+写各一次**（36B 搬移）
- Block 大小 256 时，每个 block 的 local memory 占用 = 256 × 288B = **73.7 KB**
- 这些 spill load/store **直接消耗全局显存带宽**，形成带宽竞争

**直接影响 Occupancy**: 高寄存器压力 → 每 SM 活跃 warp 数下降 → **无法隐藏内存延迟** → 计算单元空转等 DRAM。

---

## 瓶颈 3（严重）：AoS 写回导致非合并访问

Warp 中 32 个连续线程写 `results[tid]`，每个 292B，AoS 布局：

```
Thread 0: results[0]  → byte [0, 292)        → 占 3 条 cache lines
Thread 1: results[1]  → byte [292, 584)      → 占 3 条 cache lines
...
Thread 31: results[31] → byte [9052, 9344)   → 占 3 条 cache lines
```

每个线程的 292B 写跨越 3 条 cache line（0-127, 128-255, 256-291），第 3 条 cache line 只用了 36B / 128B = **28%** 利用率。Warp 整体 cache line 利用率约 **76%**。

---

## 瓶颈 4（中等）：D2H 全量下载 vs. 实际使用

`cus3d_trace_ray_batch_multi` 中：

```c
cudaMemcpyAsync(h_results, d_results,
                num_rays * sizeof(struct cus3d_multi_hit_result),  // N × 292B
                cudaMemcpyDeviceToHost, s);
```

而 CPU 后处理 Step 3 中（`s3d_scene_view_batch_trace.cpp`），对于**每条射线只使用第一个通过 filter 的 candidate**（诊断数据显示 >90% 接受 `candidate[0]`）。

**实际浪费**：下载 292B/ray，有效使用 ~40B/ray → **PCIe 带宽浪费 ~86%**。

N=10,000 时: 下载 2.92 MB，有效数据 ~400 KB，浪费 2.52 MB PCIe 带宽。虽然 PCIe Gen4 有 25 GB/s，但频繁小量传输的 **latency** 才是真正问题。

---

## 瓶颈 5（中等）：每次 batch 调用 `cudaMallocAsync(d_results)`

`cus3d_trace_ray_batch_multi` 内部：

```c
struct cus3d_multi_hit_result* d_results = NULL;
cudaMallocAsync(&d_results,
                num_rays * sizeof(struct cus3d_multi_hit_result), s);
// ... kernel ...
cudaFreeAsync(d_results, s);
```

尽管上层 `s3d_batch_trace_context` 预分配了射线输入缓冲区，**结果缓冲区仍在 `cus3d_trace_ray_batch_multi` 内部每次动态分配**。问题：
1. `cudaMallocAsync` overhead: ~10μs/call
2. 新分配内存不在 L2 cache → kernel 首次写入必须 miss → 额外 DRAM 流量
3. 无法跨 batch 复用结果缓冲区

---

## 瓶颈 6（中等）：Instance 数据每次 batch 调用重新上传

两级 BVH 路径中（`cus3d_trace_ray_batch_multi` instanced 分支），**即使 instance 数据在整个求解过程中不变**：

```c
// 每次 batch 调用都执行:
malloc(h_blas_array);  malloc(h_instances);    // CPU alloc
// ... 填充 ...
cudaMallocAsync(d_blas_array); cudaMallocAsync(d_instances);  // GPU alloc
cudaMemcpyAsync(...H2D...);   cudaMemcpyAsync(...H2D...);    // 上传
// ... kernel ...
cudaFreeAsync(d_blas_array);  cudaFreeAsync(d_instances);     // GPU free
```

`instance_gpu_data` 约 **152B/instance**。10 个 instance × 每秒 1000 次 batch = **1.52 MB/s** 重复上传 + 4 次额外 CUDA API 调用/batch。

---

## 瓶颈 7（低-中等）：Pageable Host Memory H2D 传输

`trace_rays_batch_impl` 的 Step 1 中：

```c
float3* h_origins    = (float3*)malloc(...);  // pageable memory
// ...
gpu_buffer_float3_upload(...);  // cudaMemcpyAsync from pageable
```

`malloc` 分配的是 pageable 内存。CUDA 驱动在执行 H2D 时必须先将 pageable 页复制到 driver 的 staging buffer（pinned），再 DMA 到 GPU。**两次内存拷贝**（pageable → pinned staging → GPU）vs 直接 pinned → GPU 的一次。

---

## 瓶颈 8（固有）：BVH 遍历的非规则内存访问

cuBQL `shrinkingRayQuery::forEachPrim` 遍历时：
- 不同射线走不同的 BVH 路径 → **warp 内线程访问不同节点地址** → 非合并读
- BVH 节点 + 顶点/索引/prim_to_geom 的随机读取 → L2 cache miss 率高
- 这是光追的本质问题，无法完全消除，只能通过 **提高 occupancy** 来隐藏延迟

---

## 定量汇总：每条射线的显存流量估算

| 阶段 | 数据源/目标 | 字节/射线 | 说明 |
|------|------------|-----------|------|
| H2D 射线上传 | origins+dirs+ranges | **32B** | SoA 上传，合并良好 |
| Kernel 读射线 | d_origins/dirs/ranges | **32B** | 合并读 |
| Kernel 读 BVH 节点 | cuBQL BinaryBVH | **~200-500B** | 取决于树深度，非合并 |
| Kernel 读几何数据 | vertices, indices, prim_to_geom, geom_entries | **~60-120B** | 每个候选体的读取 |
| **Kernel Top-K spill** | **local mem (slots[])** | **~300-600B** | insertion-sort 搬移 |
| **Kernel 写结果** | **d_results (multi_hit)** | **292B** | AoS, 部分 cache line 浪费 |
| D2H 结果下载 | h_results | **292B** | 86% 数据被丢弃 |
| **总计** | | **~1.2-1.9 KB/ray** | 实际有效数据仅 ~200B |

**在 3070 Laptop 384 GB/s 带宽下**:
- 要填满 5120 CUDA 核心，需要超高 occupancy 来隐藏延迟
- 而 Top-K 的寄存器/local memory 压力直接削减 occupancy
- 带宽被 spill + 膨胀写回 + 下载浪费三重消耗
- **形成恶性循环**：低 occupancy → 延迟无法隐藏 → 计算单元空转等内存 → 看起来是"带宽不足"

---

## 建议优化方向（按收益排序）

| 优先级 | 优化 | 预期收益 | 复杂度 | 对应瓶颈 |
|--------|------|---------|--------|----------|
| **P0** | 降低 K 值或 **自适应 K**：无 filter 射线用 K=1 kernel，有 filter 才用 K>1 | 消除 80%+ 无意义 Top-K 开销 | 低 | #1, #2, #3 |
| **P0** | **预分配 `d_results`** 到 `s3d_batch_trace_context` 中，修改 `cus3d_trace_ray_batch_multi` 接受外部缓冲区 | 消除每次 alloc/free，热缓存复用 | 中 | #5 |
| **P1** | 结果缓冲区 **SoA 化**: 分离 distance[], prim_id[], geom_idx[] 等，提高写合并效率 | 写带宽减少 ~24% | 中 | #3 |
| **P1** | **Pinned memory** staging 缓冲区 (`cudaMallocHost`) | H2D 传输加速 ~2× | 低 | #7 |
| **P1** | Instance 数据 **持久化到 GPU**（场景 commit 时一次上传） | 消除重复上传开销 | 中 | #6 |
| **P2** | 仅下载 **实际命中数的 compact 结果**（GPU 端 stream compaction 或只下载 hit[0]） | D2H 减少 ~86% | 高 | #4 |
| **P2** | 将简单 filter（same prim_id → reject）**移入 GPU kernel**，减少 Top-K 需求 | 从根本层减少 K 需求 | 高（Phase C 范围） | #1, #2 |

### P0 "自适应 K" 方案要点

诊断数据已显示 >90% 射线没有 filter（`diag_no_filter` 计数），这些射线完全不需要 Top-K。

方案：在 `trace_rays_batch_impl` 中按 `requests[i].filter_data != NULL` 将射线分为两组：
1. **无 filter 组** → 调用 `cus3d_trace_ray_batch`（nearest-hit kernel, 36B/ray 写出, 无 spill）
2. **有 filter 组** → 调用 `cus3d_trace_ray_batch_multi`（Top-K kernel）

改动局限在 `s3d_scene_view_batch_trace.cpp` 内部，无需修改 kernel 层。

---

*文档生成: 2026-02-14 | 基于实际代码分析*
