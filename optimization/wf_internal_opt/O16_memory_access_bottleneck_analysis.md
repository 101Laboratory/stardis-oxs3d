# O16: 访存瓶颈定位与优化

**创建日期**: 2026-03-18  
**基于**: VTune Memory Access 分析（oxs3d-merge-phase, pool=8192 / pool=32768）  
**场景**: porous 320×320 spp=32，merge-phase 架构 (O11+O12+O13)  
**硬件**: 24 核 / 32 线程，L2 = 32 MB，L3 = 36 MB  
**状态**: 🔄 进行中  

---

## 背景

O14（调度策略）和 O15（TLB）均被排除后，VTune Memory Access profiling 确认：

- **pool=8192**: 轻度 L3 瓶颈
- **pool=32768**: 严重 DRAM 瓶颈

本文档汇总 VTune 定位的各函数高访存开销代码行，作为后续优化决策的依据。

## 综合报告：

### pool_size = 8192

```plaintext
Elapsed Time:	14.570s
    CPU Time:	347.960s
    Performance-core (P-core):	
    Memory Bound:	20.0%
    L1 Bound:	14.5%  <--------- mild
    L2 Bound:	1.6%
    L3 Bound:	10.1%  <--------- mild
    DRAM Bound:	5.6%
    Store Bound:	5.6%
    Efficient-core (E-core):	
    Memory Bound:	45.4%
    L1 Bound:	3.9%
    L2 Bound:	0.2%
    L3 Bound:	0.5%
    DRAM Bound:	0.3%
    Uncore:	
    DRAM Bandwidth Bound:	90.5%
    Loads:	372,954,788,308
    Stores:	253,092,992,562
    LLC Miss Count:	185,189,231
    Total Thread Count:	36
    Paused Time:	0.027s
```

### pool_size = 32768

```plaintext
Elapsed Time:	15.004s
    CPU Time:	372.535s
    Performance-core (P-core):	
    Memory Bound:	58.0%
    L1 Bound:	8.3%
    L2 Bound:	0.5%
    L3 Bound:	6.4%
    DRAM Bound:	49.3%  <-------- servere
    Store Bound:	7.5%
    Efficient-core (E-core):	
    Memory Bound:	57.3%
    Uncore:	
    DRAM Bandwidth Bound:	95.6%
    Loads:	200,810,824,144
    Stores:	135,067,651,908
    LLC Miss Count:	1,134,710,308
    Total Thread Count:	36
    Paused Time:	0.030s

```

---



## 高开销代码行汇总

sdis_solve_persistent_wavefront.c

``` c
    rp->direction_z = e->direction[2];
    rp->tmin         = e->tmin;
    rp->tmax         = e->tmax;

    pv->filter_pinned[ray_idx] = e->filter;
    pv->ray_to_slot[ray_idx]   = e->slot;
    pv->ray_slot_sub[ray_idx]  = e->sub; // L3541
  }
}
```
```c
  /* O11_SAFETY: first-round guard — if batch_idx is still the sentinel
   * value from init, no GPU trace has completed for this path yet.
   * Skip distribute and let Phase C collect the ray for the first time. */
  if(p->ray_req.batch_idx == (uint32_t)-1) return 0; // L3291

  {
    /* Fetch ray hit(s) from previous GPU trace via batch_idx */
    const struct s3d_hit* h0 = &pv->ray_hits[p->ray_req.batch_idx];
    const struct s3d_hit* h1 = NULL;
    enum path_phase ph_before = (enum path_phase)hot->phase;
    res_T lr;

    if(p->ray_req.ray_count >= 2)
      h1 = &pv->ray_hits[p->ray_req.batch_idx2];

    /* Pre-deliver multi-ray results for special phases */
    if(ph_before == PATH_ENC_QUERY_EMIT && hot->ray_count_ext == 6) {
      int j;
      for(j = 0; j < 6; j++) {
        pool->enc_arr[slot].dir_hits[j] =  // L3307
          pv->ray_hits[pool->enc_arr[slot].batch_indices[j]];
      }
    }
    if(ph_before == PATH_ENC_QUERY_FB_EMIT) {
      pool->enc_arr[slot].fb_hit = pv->ray_hits[p->ray_req.batch_idx];
    }
```

sdis_wf_steps_cnd.c

```c
  if(enc->resolved_enc_id != p->locals.cnd_ds.enc_id) {  // L177
```

oxs3d_scene_view.cpp

```c++
        #pragma omp parallel for num_threads(pp_nthreads) \
          schedule(static) reduction(+: omp_accepted)
        for (int ii = 0; ii < (int)count; ii++) {
            const HitResult& hr = h_hits[ii];
            if (hr.t < 0.0f) { // L2773
                hits[ii] = S3D_HIT_NULL;
                continue;
            }
            unsigned int shape_id = 0;
            s3d_shape* shape = nullptr;
            s3d_shape* inst  = nullptr;
            if (pp && hr.geom_id < (unsigned)pp_sz) { // L2780
```

---

## 内存占用全景

根据源码验证的结构体大小，各主要数组的内存占用如下：

| 组件 | 单元大小 | 个数 | P=8192 | P=32768 |
|------|---------|------|--------|---------|
| `slots` (path_state) | ~2048B | P | 16 MB | 64 MB |
| `hot_arr` (path_hot) | 8B | P | 64 KB | 256 KB |
| `sfn_arr` (path_sfn_data) | ~1700B | P | 13.3 MB | 53.2 MB |
| `enc_arr` (path_enc_data) | ~600B | P | 4.7 MB | 18.8 MB |
| `ext_arr` (path_ext_data) | ~368B | P | 2.9 MB | 11.5 MB |
| `ray_pinned` (CUDA pinned) | 32B | 6P | 1.5 MB | 6 MB |
| `filter_pinned` (CUDA pinned) | 16B | 6P | 0.75 MB | 3 MB |
| `ray_to_slot` | 4B | 6P | 192 KB | 768 KB |
| `ray_slot_sub` | 4B | 6P | 192 KB | 768 KB |
| `ray_hits` (s3d_hit) | 56B | 6P | 2.6 MB | 10.5 MB |
| 索引/bucket 数组 | ~36B | P | 288 KB | 1.15 MB |
| **合计** | | | **~42 MB** | **~169 MB** |

> **L3 = 36 MB**。P=8192 时总分配 42 MB 已轻微超出 L3；P=32768 时 169 MB 超出 L3 近 5 倍。

---

## VTune 关键信号

| 指标 | P=8192 | P=32768 | 变化 |
|------|--------|---------|------|
| P-core DRAM Bound | 5.6% | **49.3%** | ×8.8 |
| P-core L3 Bound | **10.1%** | 6.4% | ↓ (被 DRAM 掩盖) |
| LLC Miss Count | 185M | **1,135M** | **×6.1** |
| Loads | 373B | 201B | ×0.54 |
| Stores | 253B | 135B | ×0.53 |
| DRAM BW Bound (Uncore) | 90.5% | 95.6% | 均饱和 |

P=32768 的 **Loads/Stores 总量仅为 P=8192 的 ~54%**（更大 pool → 更少轮次 → 更少总指令），但 LLC miss 增长 6.1 倍。典型的 **cache capacity miss 爆发**：每次访存更贵，吃掉了减少轮次带来的收益。

---

## 瓶颈归因

### 热点 1：L3541 — `merged_pass_flush_tl_rays`（ray packing 写入）

```c
pv->filter_pinned[ray_idx] = e->filter;
pv->ray_to_slot[ray_idx]   = e->slot;
pv->ray_slot_sub[ray_idx]  = e->sub; // L3541 — VTune 样本命中点
```

**访问模式**: 每条 ray 写 4 个 SoA 数组（`ray_pinned` 32B, `filter_pinned` 16B, `ray_to_slot` 4B, `ray_slot_sub` 4B）。写入逐线程顺序(原子 offset 预留 → 线程内 ray_idx 连续)。

**瓶颈机制**:

1. **Write-allocate 开销** — `ray_pinned` 和 `filter_pinned` 是 `cudaHostAllocDefault`（WB 可缓存）。CPU 写入前必须先将目标 cache line 从主存拉入 L1（**即使马上全部覆写**）。每条 ray 触发 4 条 cache line 的 write-allocate fetch。

2. **多线程同时 flush** — OMP for 结束后，所有线程几乎同时进入 flush。32 线程各写自己的 ray_base 区段，在 4 个数组中产生 **128 条独立写入流**，L3 同时容纳 128 个活跃 stream 的 prefetch 窗口 → 容量争用。

3. **采样偏差** — 前面的写（`ray_pinned`）触发 write-allocate 后占满 store buffer；到 `ray_slot_sub`（第 4 条写）时 store buffer 满，CPU 必须等待 → stall 被采样捕获。**真正代价分摊在所有 4 条写上**。

4. **缓存污染** — 写完后 `ray_pinned`/`filter_pinned` 只被 GPU 通过 DMA 读取，CPU 永远不再读。把它们留在 CPU cache 是纯浪费，挤压下一轮 distribute 所需的 slots/enc_arr/ray_hits 数据。

**严重度**: 中等。P=8192 下是唯一的 L3 miss 压力点。

---

### 热点 2：L3291 + L3307 — `merged_pass_distribute_step`（hit 分发，**最严重**）

```c
if(p->ray_req.batch_idx == (uint32_t)-1) return 0;      // L3291
const struct s3d_hit* h0 = &pv->ray_hits[p->ray_req.batch_idx];
...
pool->enc_arr[slot].dir_hits[j] =                        // L3307
    pv->ray_hits[pool->enc_arr[slot].batch_indices[j]];
```

**访问模式**: OMP 循环按 `pv->active_indices[ph]` 迭代 slot。每个 slot 至少做：
1. 读 `pool->slots[slot].ray_req.batch_idx` — path_state ~2048B 步幅中的 4B 字段
2. 读 `pv->ray_hits[batch_idx]` — 56B 的 s3d_hit
3. 对 6-ray enc 路径：读 `pool->enc_arr[slot].batch_indices[0..5]` + 6 次读 `pv->ray_hits[idx]` + 6 次写 `pool->enc_arr[slot].dir_hits[0..5]`

**瓶颈机制 — 三重叠加**:

**(a) AoS 步幅浪费（stride waste）**
- `pool->slots[slot]` 步幅 ~2048B，读 `batch_idx`（4B）→ cache line 利用率 4/64 = **6.25%**
- `pool->enc_arr[slot]` 步幅 ~600B，读 `batch_indices[6]`（24B）→ ~38%（但需加载 10 条 cache line 跨越 600B 间隔）

**(b) ray_hits 随机读取（关键瓶颈）**
- Phase C 按 **ray bucket 类型** 排列 ray_idx（radiative, conductive, other），而 Phase A distribute 按 **slot 顺序** 遍历。slot 的 batch_idx 指向不同 bucket 区间 → **对 ray_hits 的读取全局随机**。
- P=8192：ray_hits = 2.6 MB → 大概率命中 L3
- P=32768：ray_hits = 10.5 MB → 每次随机读几乎必然 L3 miss → DRAM 延迟 ~100 ns/read

**(c) 双重间接寻址（double indirection）**
- L3307 路径：先读 `enc_arr[slot].batch_indices[j]`（间接层 1），再读 `ray_hits[batch_indices[j]]`（间接层 2）。两层都可能 miss。
- 6-ray enc 查询：6 个 `batch_indices` 值是不同 ray_idx → 6 次独立随机地址。
- 每个 enc 路径可触发 **最多 12 次 cache miss**（6× enc_arr 间接 + 6× ray_hits 随机）。

**严重度**: **极高**。这是 P=32768 DRAM bound 49.3% 的主要来源。

---

### 热点 3：L177 — `sdis_wf_steps_cnd.c`（导热路径 enc 检查）

```c
if(enc->resolved_enc_id != p->locals.cnd_ds.enc_id) {  // L177
```

**访问模式**: `enc` = `pool->enc_arr[slot]`，`p` = `pool->slots[slot]`。单次比较读两个不同大数组各一个字段。

**瓶颈机制**:
- `enc_arr[slot].resolved_enc_id`：偏移 ~28B / 600B 步幅 → 加载一条 cache line，用 4B
- `p->locals.cnd_ds.enc_id`：偏移 ~1200-1400B / 2048B 步幅 → 加载另一条 cache line，用 4B
- 此检查在 cascade 的导热路径中高频调用。如果导热路径在 active_indices 中与辐射路径穿插分布（通常如此），slot 不连续 → 每次调用大概率两条 cache line 都 miss。

**严重度**: 中高。单次两次加载，但频率高且 cache line 利用率极低（4/64 = 6.25%）。

---

### 热点 4：L2773/L2780 — `ox_s3d_scene_view.cpp::distribute_hits`（GPU hit 后处理）

```c++
const HitResult& hr = h_hits[ii];
if (hr.t < 0.0f) { ... }              // L2773
if (pp && hr.geom_id < (unsigned)pp_sz) { ... }  // L2780
```

**访问模式**: OMP parallel for，`schedule(static)`，每线程读写连续 chunk。输入 `h_hits[]`（HitResult ~40B），输出 `hits[]`（s3d_hit ~56B）。

**瓶颈机制**:
- 顺序访问 → 空间局部性良好，硬件 prefetcher 有效
- 但输入 + 输出 footprint：P=32768 时 ~18 MB，加上 `resolve_shape`/`resolve_instance` 的场景图指针追踪，总活跃集挤压 L3
- `resolve_shape(sv, hr.geom_id, shape_id)` 涉及场景图的指针跳转（`sv->shapes[geom_id]`），这些表在多线程下争用 L3 容量

**严重度**: 中低。本身局部性尚可，但总数据量大，对 L3 预算贡献显著。

---

## 综合归因总结

```
根本原因优先级排序：

1.【容量失配】pool_size 线性放大所有数组 → P=32768 工作集 169MB 远超 L3 36MB
   → LLC miss ×6.1，DRAM bound 从 5.6% 飙升到 49.3%

2.【ray_hits 随机读】bucket-based ray 排列 vs slot-ordered distribute 遍历 → 地址空间错配
   → 每条 ray 读取是全局随机的 capacity miss

3.【AoS 步幅膨胀】path_state 2KB + enc_arr 600B 步幅
   → cache line 利用率 6-10%，有效带宽放大 10-16×

4.【双重间接寻址】enc 6-ray 路径 enc_arr→batch_indices→ray_hits
   → 每个 enc 路径最多 12 次独立 cache miss

5.【写分配+容量争用】ray packing Phase C 写 ~2.6MB pinned 数组
   → 挤压 Phase A distribute 的读缓存
```

---

## 优化方向

详见 **O16_dev_plan.md**。

## 优化效果

### 热点1 L3541 方案A修复后：

pool_size = 8192 L3 热点降低
```plaintext
Elapsed Time:	15.001s
    CPU Time:	390.641s
    Performance-core (P-core):	
    Memory Bound:	17.2%
    L1 Bound:	15.0%
    L2 Bound:	1.8%
    L3 Bound:	9.1%
    DRAM Bound:	2.7%
    Store Bound:	4.6%
    Efficient-core (E-core):	
    Memory Bound:	34.1%
    L1 Bound:	3.5%
    L2 Bound:	0.1%
    L3 Bound:	0.4%
    DRAM Bound:	0.2%
    Uncore:	
    DRAM Bandwidth Bound:	95.8%
    Loads:	456,419,492,174
    Stores:	299,385,681,301
    LLC Miss Count:	139,525,206
    Total Thread Count:	34
    Paused Time:	0.175s
```

---

*报告创建: 2026-03-18 | 分析完成: 2026-03-19 | 状态: ✅ 归因完成，待优化*
