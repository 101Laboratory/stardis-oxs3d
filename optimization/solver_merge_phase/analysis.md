# Solver Merge-Phase 优化方案分析

**创建日期**: 2026-03-06  
**状态**: 分析完成, 待实施  
**代号**: O11-MergePhase  
**基线**: pool=20000, i9-13900 + RTX 4090, porous 320×320×32  

---

## 1. 问题陈述

### 1.1 当前性能

| 配置 | 墙钟 | 对比 |
|------|------|------|
| CPU Embree (32T) | **100s** | 基准 |
| Hybrid Wavefront (pool=20K) | **208s** | 2.08× 慢于 CPU |

### 1.2 208s 的详细分解 (pool=20K 实测)

| Phase | 时间 (s) | 占比 | 操作模式 |
|-------|---------|------|---------|
| cascade | 38.8 | 18.7% | OMP for: path_state[] 读写 (状态机推进) |
| distribute | 34.4 | 16.6% | OMP for: path_state[] 散射写 (光追结果) |
| harvest+refill | 28.6 | 13.8% | OMP for: path_state[] 读写 (收获+初始化) |
| collect | 22.2 | 10.7% | OMP for: path_state[] 读 → pinned buf 写 |
| cpu_postprocess | 22.4 | 10.8% | OMP for: filter 结果应用 |
| compact | 18.2 | 8.8% | OMP for: hot_arr[] 扫描, 构建 indices |
| gpu_launch | 21.4 | 10.3% | API 开销: H2D + optixLaunch (地板) |
| gpu_kernel | 7.7 | 3.7% | GPU CUDA event 实测 trace 时间 |
| d2h_wait | 5.6 | 2.7% | D2H 传输耗时 |
| gpu_sync | 1.4 | 0.7% | GPU 等待时间 (隐藏后极小) |
| housekeeping | 1.1 | 0.5% | 杂项 |
| **合计** | **208.1** | | |

### 1.3 根本原因: L3 重复扫描

每轮 wavefront 循环执行 **5-6 个独立 OMP parallel for**，每个都完整扫描 `path_state[20K]` (40 MB):

```
每轮 L3 流量 = 5 × 40 MB = 200 MB
总轮次 = 411,194
L3 总流量 = 411K × 200 MB = 82.2 TB
```

path_state 数组 (40 MB) 超出 i9-13900 L3 容量 (36 MB)，每次独立 OMP loop 都触发完整的 L3 冷载入。

---

## 2. 方案核心思想

### 2.1 合并所有 per-path 操作到单次 OMP 扫描

当前架构：
```
while (active > 0) {
    compact(pool)           ←─ OMP 扫描 #1: hot_arr
    collect(pool)           ←─ OMP 扫描 #2: path_state → pinned buf
    gpu_launch()            ←─ H2D + kernel
    gpu_wait_d2h()          ←─ 同步 + D2H
    distribute(pool)        ←─ OMP 扫描 #3: path_state 散射写
    cpu_postprocess(pool)   ←─ OMP 扫描 #4: filter 结果
    cascade(pool)           ←─ OMP 扫描 #5: path_state 状态机
    harvest+refill(pool)    ←─ OMP 扫描 #6: path_state + 新 path
}
```

合并后架构：
```
while (active > 0) {
    merged_pass(pool)       ←─ OMP 扫描 ×1: distribute(纯写) + cascade(含step) + collect(3类) + harvest
    compact_indices()       ←─ 轻量扫描 hot_arr (160KB, L2 内)
    refill()                ←─ 串行 pre-alloc + OMP init (<100 slots)
    gpu_dispatch_all()      ←─ RT + CP + enc(CP+RT) 全部异步发射, CPU 单次调用
    gpu_wait_d2h_all()      ←─ 同步 D2H + enc 后处理
}
```

三种外部查询 (RT, enc_locate, cp) 全部延迟一轮, 完全对称。
distribute 退化为纯写者, cascade 成为唯一状态机驱动。
所有 GPU 操作通过 gpu_dispatch_all 统一分发, 延迟隐藏在下一轮 merged_pass 中。

### 2.2 为什么可以合并

所有被合并的 phase 操作的是 **同一个 path_state[slot]**。在单次 OMP 循环中，一个 slot 被加载到 L1 后 (2 KB)，所有操作都在 L1 内完成：

| 操作 | 在 2KB path_state 上访问 | L1 代价 |
|------|------------------------|---------|
| distribute (纯写结果) | `p->hit_result`, `enc_arr`, `p->locals.cnd_wos` (56B) | ~1ns |
| cascade (状态机 + step) | `p->rwalk, p->T, ...` (~218B) + step_*() | **主体** (不变) |
| collect (三种请求) | `p->ray_req` + enc/cp 字段 (50B) | ~0.5ns |
| harvest (检查 done) | `p->done_reason` (4B) | ~0.1ns |

cascade 的 `for(;;)` 内循环访问 path_state 时产生的 L3 miss penalty (~40-80ns) **足以隐藏**所有 L1 边际操作。

### 2.3 compact 为什么不合并

compact 在合并 loop **之后**运行, 因为需要看到 cascade 更新后的 `hot_arr` 状态:
- cascade 完成后部分 path 变为 DONE / 变为 needs_ray = 1
- compact 根据最终状态构建 `active_indices`, `bucket_radiative[]`, `bucket_conductive[]` 等

compact 扫描的是 `hot_arr` (20K × 8B = 160 KB)，完全在 L2 内，不触及 path_state，开销极小。

---

## 3. 性能预估

### 3.1 CPU Phase 时间

| 操作 | 当前(s) | 合并后(s) | 节省(s) |
|------|---------|----------|---------|
| distribute | 34.4 | 0 (L1 hitched) | 34.4 |
| cpu_postprocess | 22.4 | 0 (L1 hitched) | 22.4 |
| collect | 22.2 | 0 (L1 hitched) | 22.2 |
| harvest+refill | 28.6 | ~3 (refill init 不可消除) | 25.6 |
| compact | 18.2 | ~2 (仅 hot_arr 160KB) | 16.2 |
| cascade | 38.8 | 38.8 (不变) | 0 |
| **合计** | **164.6** | **~43.8** | **120.8** |

### 3.2 GPU Pipeline (不变)

| 操作 | 时间(s) |
|------|---------|
| gpu_launch (API 地板) | 21.4 |
| gpu_kernel (trace) | 7.7 |
| d2h_wait | 5.6 |
| gpu_sync | 1.4 |
| **合计** | **~36.1** |

### 3.3 总墙钟 (Pipeline 重叠)

```
CPU merged_pass:  43.8s (per round: 43.8s / 411K = 106 μs)
GPU pipeline:     36.1s (per round: 36.1s / 411K = 87.8 μs)

Pipeline cycle = max(106, 87.8) = 106 μs  ← CPU-bound
Total = 411K × 106μs = 43.6s + drain ~4s = ~47s
```

### 3.4 PCIe 带宽校验

```
Per round H2D: s3d_ray_pinned × avg_batch = 32B × 31402 = 1.00 MB
Per round D2H: s3d_filter_per_ray × avg_batch = 16B × 31402 = 0.50 MB
Total PCIe: 411K × 1.50 MB = 617 GB

PCIe 4.0 x16 全双工: 50 GB/s
总传时间: max(413/25, 207/25) = 16.5s  ← 完全在 pipeline 中隐藏
```

### 3.5 方案对比

| 架构 | 墙钟 | vs CPU Embree |
|------|------|--------------|
| CPU Embree (32T) | 100s | 1.00× |
| 当前 Hybrid | 208s | 0.48× |
| **O11 Merge-Phase** | **~47s** | **2.13× 加速** |
| 全 GPU Wavefront (理论) | ~81s | 1.23× |

---

## 4. 关键不变量

合并方案不改变以下行为 / 数值：

| 不变量 | 值 | 验证方法 |
|--------|---|---------|
| total_rays | 12,912,150,630 | `pool->total_rays_traced` |
| cascade_iterations | 5,379,253,197 | `pool->cascade_total_iterations` |
| total_steps (rounds) | 411,194 (方案 A 可能略增) | `pool->total_steps` |
| completed paths | 3,276,800 | `pool->paths_completed` |
| failed paths | 11 | `pool->paths_failed` |
| 像素结果 | bit-exact | IR 输出文件对比 |

---

## 5. 数据流依赖分析

### 5.1 当前架构的完整 Round k 顺序

```
Round k (gpu_postprocess):
  ① distribute_ray_results()       ← 读 ray_hits[k-1], 调用 step_*(), 写 path_state
     │ 副作用: step 函数可能产生 PATH_ENC_LOCATE_PENDING / PATH_CND_WOS_CLOSEST
  ② collect_enc_locate()           ← 扫描 hot_arr 找 PENDING (含 ① 产生的)
  ③ GPU enc_locate batch           ← 批量空间查询
  ④ distribute_enc_locate_results() ← 写回 PATH_ENC_LOCATE_RESULT
  ⑤ collect_cp()                   ← 扫描 hot_arr 找 cp PENDING
  ⑥ GPU cp batch                   ← 批量最近点查询
  ⑦ distribute_cp_results()        ← 写回 RESULT

Round k (cpu_between):
  ⑧ cascade                       ← 处理 ④⑦ 写回的 RESULT, 继续推进
     │ 遇到 PENDING 状态 → break 跳过
  ⑨ harvest + refill
  ⑩ compact + collect
  ⑪ gpu_launch + gpu_wait
```

**架构不对称**: RT 已经是延迟一轮模式 (round k cascade 产生 ray_req，
round k+1 distribute 写回 hit)，但 enc/cp 在同一轮内同步求解。
这种同步求解迫使合并设计必须在 distribute↔cascade 之间插入扫描，
破坏 L1 驻留。解决方案: 统一外部查询模型 (见 §5.2)。

### 5.2 关键洞察: 统一外部查询模型

当前架构中, **RT (光线追踪)** 已经使用"延迟一轮"模式:
- Round k cascade 产生 ray request → collect 收集 → gpu_launch 追踪
- Round k+1 distribute 写回 ray_hits → cascade 消费结果

但 **enc_locate 和 cp** 被特殊处理: 在 distribute 和 cascade 之间同步求解,
不延迟。这造成了架构不对称:
- 为了在 distribute → cascade 之间插入 enc/cp 批量操作, 合并设计被迫拆分
- 要么接受延迟 (path idle 一轮), 要么拆成两次扫描, 要么找 per-path CPU API

**更优解**: 将 enc_locate 和 cp 视为与 RT **完全相同的外部查询**:
- cascade 产生 enc/cp 请求 → collect 收集 → post_batch 批量求解
- 下一轮 distribute 写回 enc/cp 结果 → cascade 消费

三种外部查询统一处理, distribute 蜕变为纯写者, cascade 吸收所有 step 逻辑。

### 5.3 统一设计: distribute 为纯写者, cascade 为唯一状态机

**当前 distribute 做了两件事:**
1. 写回 ray hit 数据 (纯写)
2. 调用 `step_radiative_trace()`, `step_conductive_ds_process()` 等推进状态机

**新设计拆分:**
- **distribute (纯写者)**: 仅将三种结果写入 slot-local 存储:
  - ray_hits → `p->hit_result` (来自 GPU 光追)
  - enc_results → `p->enc_result` (来自 enc_locate batch)
  - cp_results → `p->cp_result` (来自 cp batch)
- **cascade (唯一状态机驱动)**: 吸收 distribute 原有的 step 函数:
  - 先消费 pending 结果 (ray/enc/cp), 调用对应 step 函数推进状态
  - 然后继续 no-ray 内循环直到产生新请求或 path 完成

```
merged_pass (单次 OMP 扫描, 40MB):
  for each active path:
    ① distribute: 写回 ray/enc/cp 结果  (纯写, 无分支逻辑)
    ② cascade:    消费结果 + step推进 + no-ray循环 → 产生 ray/enc/cp 请求
    ③ collect:    收集所有请求类型 (ray + enc + cp) → pinned buffers
    ④ harvest:    如果 path 完成, 累积结果

compact (hot_arr scan 160KB)
refill

gpu_dispatch_all (async, CPU 单次调用):
    optixLaunch(RT) + optixLaunch(CP) + optixLaunch(enc CP) + optixLaunch(enc RT)
    async D2H all results

gpu_wait_d2h_all (sync):
    等待 D2H 完成 + enc后处理(CP+RT结果合并为EnclosureResult)
```

**三种外部查询全部延迟一轮, 完全对称:**
```
Round k cascade 产生 { ray_req, enc_req, cp_req }
                            ↓ collect 写入 pinned buffers
Round k gpu_dispatch_all 异步发射 { RT, CP, enc(CP+RT) }
                            ↓ CPU 立刻返回, GPU 在后台工作
Round k+1 gpu_wait_d2h_all → 三种结果就绪
                            ↓
Round k+1 distribute 写回 { ray_hits, enc_results, cp_results }
                            ↓
Round k+1 cascade 消费结果, 继续推进
```

### 5.4 对比分析

| | 当前架构 | 旧方案 A (接受延迟) | **统一查询模型** |
|---|---------|-------------------|-----------------|
| distribute 角色 | step推进 + 写入 | step推进 + 写入 | **纯写者** |
| cascade 角色 | no-ray 循环 | no-ray 循环 | **消费结果 + step + no-ray** |
| enc/cp 处理 | distribute↔cascade 之间同步 | 延迟一轮 (cascade skip) | 延迟一轮 (与 RT 对称) |
| L3 扫描/轮 | 5-6 | 1 | 1 |
| 额外 hot_arr 扫描 | 0 | enc/cp collect 2次 | enc/cp collect 2次 |
| distribute 分支逻辑 | 3 bucket switch | 3 bucket switch | **无 (纯写)** |
| step 逻辑位置 | 分散在 distribute + cascade | 分散在 distribute + cascade | **集中在 cascade** |
| 架构一致性 | RT ≠ enc/cp | RT ≠ enc/cp | **RT = enc = cp** |

**优势总结:**
1. distribute 无分支纯写 → IPC 更高, 更易向量化
2. 状态机逻辑集中到 cascade → 维护更简, 调试更易
3. enc/cp 与 RT 完全对称 → 无特殊路径, 无额外扫描插入 merged_pass 内部
4. 不破坏 L1 驻留: distribute→cascade→collect→harvest 连续操作同一 slot
5. 延迟一轮的代价: enc/cp 涉及 path 极少 (每轮 ~10-100, vs pool 20K), total_steps 增 <1%

### 5.5 实现要点

**distribute 纯写者需要的映射:**
```c
// 三种结果来源 → path_state slot-local 字段
ray_hits[batch_idx]      → p->hit_result (新增或复用现有字段)
enc_results[enc_idx]     → enc_arr[slot].locate_result
cp_results[cp_idx]       → p->locals.cnd_wos (直写)
```

**cascade 吸收 step 函数:**
```c
cascade_advance_single_path(p, hot, scn, ...) {
  // NEW: 先消费 pending 外部查询结果
  if (hot->has_pending_hit) {
    switch (hot->ray_bucket) {
      case RAY_BUCKET_RADIATIVE:
        step_radiative_trace(p, hot, scn, &p->hit_result);
        break;
      case RAY_BUCKET_STEP_PAIR:
        step_conductive_ds_process(p, hot, scn, ...);
        break;
      default:
        advance_one_step_with_ray(p, hot, scn, ...);
        break;
    }
    hot->has_pending_hit = 0;
  }
  // enc_locate RESULT 和 cp RESULT 通过现有 no-ray 路径处理
  // (distribute 已写回 RESULT phase 和数据, cascade 的 advance_one_step_no_ray 自然消费)

  // EXISTING: no-ray cascade loop (不变)
  for (;;) {
    if (needs_ray || enc_pending || cp_pending || done) break;
    advance_one_step_no_ray(p, hot, scn, ...);
  }
}
```

**collect 收集三种请求:**
```c
// merged_pass Phase ③ collect per path:
if (hot->needs_ray) {
  collect_ray_to_pinned(pv, p, slot);       // 写 pinned buffer
}
if (hot->phase == PATH_ENC_LOCATE_PENDING) {
  collect_enc_request(pv, p, slot);          // 写 enc pending list
}
if (path_phase_is_cp_pending(hot->phase)) {
  collect_cp_request(pv, p, slot);           // 写 cp pending list
}
```

### 5.6 跨 round 依赖

```
distribute(k) 需要 ray_hits[k-1] + enc_results[k-1] + cp_results[k-1]
  → merged_pass 必须在 gpu_wait_d2h() 和 post_batch() 完成之后启动 ✓
```

这与当前架构的约束完全一致, 只是 distribute 的输入从 1 种 (ray) 扩展为 3 种。

### 5.7 GPU Pipeline 集成: enc/cp 延迟隐藏

#### 5.7.1 现状

enc_locate 和 cp 已经是 GPU 操作 (optixLaunch), 但使用**同步便利 API**:

```cpp
// closestPointBatch (cp): 1× optixLaunch + sync
std::vector<CPQuery> queries(N);     // 堆分配
d_queries.upload(queries);            // cudaMemcpy (同步)
optixLaunch(..., &m_sbt_cp, ...);     // GPU kernel
CUDA_SYNC_CHECK();                    // cudaDeviceSynchronize
d_results.download(results);          // cudaMemcpy (同步)

// findEnclosureBatch (enc_locate): 2× optixLaunch + 2× sync
//   Step 1: CP query → nearest surface + distance
//   cudaStreamSynchronize  ← CPU 阻塞
//   CPU: 根据 CP 结果构建 +X 方向射线
//   Step 2: RT trace → 确定 inside/outside
//   cudaStreamSynchronize  ← CPU 再次阻塞
```

**问题**: 每次 batch 调用都有完整的 alloc→upload→launch→sync→download 往返。
即使 batch size 很小 (10-100 queries), 驱动往返开销 (~100μs) 仍然存在。
更重要的是——这是 CPU 同步阻塞操作, 无法被 pipeline 隐藏。

#### 5.7.2 目标

三种外部查询全部通过 GPU async pipeline 执行, CPU 在下一轮 merged_pass
期间不等待任何 GPU 操作:

```
Round k:
  merged_pass(CPU, 43.8s):
    distribute → cascade → collect(ray+enc+cp) → harvest
  gpu_dispatch_all(async):           ← CPU 单次调用, 立即返回
    H2D rays → optixLaunch RT
    H2D cp_queries → optixLaunch CP
    H2D enc_queries → ??? (见下文)
    async D2H all results
  compact (CPU, 2s)

Round k+1: GPU 结果已就绪 → merged_pass 使用
```

CPU 关键路径 = merged_pass 43.8s + compact 2s ≈ 45.8s
GPU 关键路径 = RT 34.7s + CP ~0.5s + enc ~1s ≈ 36.2s
Pipeline cycle = max(45.8, 36.2) = 45.8s ← CPU-bound, GPU 完全隐藏

#### 5.7.3 核心问题: enc_locate 的 2-step 依赖

enc_locate = CP query + RT trace, 且 RT 的射线方向**取决于** CP 的结果:

```
Step 1: CP query (query_pos) → 找到最近面元 + 距离
Step 2: RT trace (query_pos → +X 方向) → 判断 inside/outside
```

Step 2 的射线方向是固定的 (+X), **不依赖** Step 1 的结果!
真正依赖 CP 结果的只是最终的 side 判定逻辑 (CPU 端后处理):
- `if distance < threshold → degenerate (side=-1)`
- `if ray hit → dot(ray_dir, normal) < 0 ? outside : inside`

因此 Step 1 和 Step 2 可以**并行发射**。

#### 5.7.4 解决方案: 统一 GPU Dispatch

**策略**: 一个 `gpu_dispatch_all()` 函数一次性排队所有 GPU 工作,
CPU 立刻返回。GPU 在 compute_stream 上顺序执行:

```
gpu_dispatch_all(pool, pv, sv):
  // ── H2D on transfer_stream ──
  upload_rays_async(pinned_ray_buf, count, transfer_stream)
  if (cp_count > 0)
    upload_cp_queries_async(pinned_cp_buf, cp_count, transfer_stream)
  if (enc_count > 0)
    upload_enc_queries_async(pinned_enc_buf, enc_count, transfer_stream)
  cudaEventRecord(evt_upload_done, transfer_stream)

  // ── Kernels on compute_stream ──
  cudaStreamWaitEvent(compute_stream, evt_upload_done)
  optixLaunch(pipeline, compute_stream, &sbt_rt, ...)      // RT trace
  if (cp_count > 0)
    optixLaunch(pipeline, compute_stream, &sbt_cp, ...)    // CP query
  if (enc_count > 0) {
    optixLaunch(pipeline, compute_stream, &sbt_cp, ...)    // enc Step 1: CP
    optixLaunch(pipeline, compute_stream, &sbt_rt, ...)    // enc Step 2: RT (+X)
  }

  // ── D2H on transfer_stream ──
  cudaEventRecord(evt_kernels_done, compute_stream)
  cudaStreamWaitEvent(transfer_stream, evt_kernels_done)
  download_all_results_async(transfer_stream)
```

**CPU 只调用一次 `gpu_dispatch_all()`**, 然后立刻进入下一轮 merged_pass。

#### 5.7.5 Per-launch 开销分析

| 项目 | 开销 | 频率 | 总成本 |
|------|------|------|--------|
| optixLaunch API call | ~5-10μs | 1/round (RT) | 2.1-4.1s |
| 额外 optixLaunch (CP) | ~5-10μs | 稀疏 (<10% rounds) | <0.4s |
| 额外 optixLaunch (enc×2) | ~10-20μs | 极稀疏 (<5% rounds) | <0.4s |
| H2D enc/cp queries | ~1μs | 稀疏, 数据量极小 (<10KB) | <0.1s |
| Per-launch cudaEventRecord | ~1μs | 2-4/round | <1.6s |

**最坏情况** (每轮都有 enc+cp): 4× optixLaunch = 20-40μs/round → 8.2-16.4s over 411K rounds。
**实际情况** (大多轮只有 RT): 1× optixLaunch + 偶尔 CP/enc = 平均 ~6-8μs/round → 与当前持平。

**结论**: 多 kernel launch 的 API 开销不是问题, 不需要 CUDA Graph。

#### 5.7.6 所需工程变更 (GPU 侧)

| 变更 | 描述 | 复杂度 |
|------|------|--------|
| enc/cp pinned buffer | 类似 Plan E, 预分配 pinned memory for enc/cp queries+results | 小 |
| enc CP+RT 并行化 | enc_locate 的 Step 1 (CP) 和 Step 2 (RT) 可同时发射到 compute_stream | 小 |
| 统一 dispatch 函数 | `gpu_dispatch_all()` 替代单独的 `gpu_launch_async()` + `post_batch_enc_cp()` | 中 |
| D2H 合并 | 三种结果在同一 transfer_stream 上连续异步下载 | 小 |

#### 5.7.7 修订后的主循环

原 §5.3 中的 post_batch 被吸收到 gpu_dispatch_all:

```
merged_pass (单次 OMP 扫描, 40MB):
  for each active path:
    ① distribute: 写回 ray/enc/cp 结果  (纯写)
    ② cascade:    消费结果 + step + no-ray循环 → 产生 ray/enc/cp 请求
    ③ collect:    收集三种请求 → pinned buffers
    ④ harvest:    如果 path 完成, 累积结果

compact (hot_arr scan 160KB)
refill

gpu_dispatch_all (async, CPU 单次调用):
    RT trace + CP query + enc_locate (CP+RT)
    async D2H all results
```

**enc/cp collect 不再需要额外的 hot_arr 扫描** — 请求在 merged_pass ③ 中
per-path 内联收集, 直接写入 pinned buffer。post_batch 消失。

---

## 6. 风险评估

| 风险 | 严重程度 | 缓解措施 |
|------|---------|---------|
| distribute 需要 `batch_idx` 映射 — 在 merged loop 中如何获取? | 🟡 | 上一轮 collect 写入 `p->ray_req.batch_idx`, gpu_wait 后 ray_hits 就绪, 可直接用 |
| refill 需要 task_queue 同步 (串行分配 task_id) | 🟡 | 保持 refill 的 serial pre-allocation 阶段, 仅 OMP init 阶段合并 |
| pinned memory 写延迟在 merged loop 中放大 | 🟢 | 实测 pinned write ~2-3× 普通 write, 但 40B/path 即使 3× 也仅 ~1.5ns |
| enc_locate + cp batch 不可逐 path 做, 且依赖 distribute 产生的 PENDING | 🟢 | 统一外部查询模型 + gpu_dispatch_all 统一分发, 三种查询全部异步延迟一轮 |
| distribute 吸收 step 逻辑 → cascade 改造量大 | 🟡 | cascade 新增 "消费 pending 结果" 前置段, 不影响现有 no-ray 内循环 |
| cascade 与 distribute 共享 path_state 的字段覆盖 | 🟢 | distribute 纯写结果 → cascade 读结果作为输入, 写后读, 无 conflict |
| enc/cp 需要额外 pinned buffer + GPU dispatch 改造 | 🟡 | 复用 Plan E 模式, enc/cp 数据量极小 (<10KB/round), pinned alloc 一次性 |

### 6.1 enc_locate 和 closest_point 的处理 (统一查询模型)

**核心变更**: enc/cp 不再在 distribute 和 cascade 之间同步求解,
而是与 RT 完全对称, 作为外部查询延迟一轮处理。

**当前架构 (三种查询不对称):**
```
distribute: 调用 step_*() → 可能产生 enc/cp PENDING
  ↓ (同步)
enc/cp batch → RESULT
  ↓ (同步)
cascade: 消费 RESULT
```

**新架构 (三种查询完全对称, GPU 统一分发):**
```
Round k:
  cascade: 推进状态机 → 产生 ray/enc/cp 请求
  collect: 收集三种请求 → pinned buffers
  gpu_dispatch_all: RT + CP + enc(CP+RT) 全部异步发射, CPU 立刻返回

Round k+1:
  gpu_wait_d2h_all: 三种结果就绪 (enc 后处理: CP+RT → EnclosureResult)
  distribute: 写回 ray_hits + enc_results + cp_results (纯写)
  cascade: 消费结果, 继续推进
```

**结果**: 消灭了 distribute→cascade 之间的所有额外扫描和同步操作,
distribute 蜕变为无分支纯写者, cascade 成为唯一的状态机驱动。
三种外部查询通过 gpu_dispatch_all 统一异步分发, 延迟全部被隐藏在下一轮
merged_pass 的 CPU 时间中 (见 §5.7)。enc/cp 涉及的 path 极少,
延迟一轮导致的 total_steps 增 <1%。

---

*文档版本: v1.0 | 下次更新: 开发指南完成后*
