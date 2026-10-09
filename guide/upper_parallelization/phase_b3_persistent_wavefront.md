# Phase B-3: Persistent Wavefront Pool — 架构分析与实施方案

**生成时间**: 2026-02-10  
**前置**: Phase B-1 (batch trace API) ✅, Phase B-2 (per-tile wavefront) ✅  
**目标**: 解决 per-tile wavefront 的 GPU 利用率瓶颈，实现全图像级 persistent wavefront  
**修改文件数**: ~5 个（2 新文件 + 3 修改）  
**预计代码量**: ~800-1000 行新代码 + ~100 行修改

---

## 一、现有架构剖析（从代码出发）

### 1.1 三层调度结构

当前代码的执行调度由三个文件构成完整链路：

```
sdis_solve_camera.c          ← 外层: OMP tile 调度
  └─ sdis_solve_wavefront.c  ← 中层: per-tile wavefront 循环
      └─ s3d_scene_view_batch_trace.cpp → cus3d_trace.cu  ← 底层: GPU batch trace
```

#### 第一层: `sdis_solve_camera()` — OMP tile 调度

**文件**: `sdis_solve_camera.c` L600-690

```c
omp_set_num_threads((int)scn->dev->nthreads);
#pragma omp parallel for schedule(static, 1)
for(mcode = mcode_1st; mcode < (int64_t)ntiles_adjusted; mcode += mcode_incr) {
    // 每个 OMP 线程:
    //   1. 创建 tile (L649)
    //   2. 计算 tile_org, tile_sz (L665-668)
    //   3. 调用 solve_tile_wavefront() 或 solve_tile() (L672-677)
    //   4. 更新 progress (L685)
}
```

**关键参数**:
- `TILE_SIZE = 4`（定义于 `sdis_tile.h` L27: `#define TILE_SIZE 4`）
- OMP 线程数 = `scn->dev->nthreads`（典型值 8-16）
- `per_thread_rng[ithread]`：每个 OMP 线程独立的 RNG

**tile 结果写入流程**:
1. `solve_tile_wavefront()` 将结果写入 `struct tile` 的 `pixel[x][y].acc_temp`
2. OMP 循环结束后，`gather_tiles()` 调用 `write_list_of_tiles()` (L325)
3. `write_tile()` 遍历 tile 中每个 pixel → `setup_estimator_from_pixel()` 写入 `estimator_buffer`

#### 第二层: `solve_tile_wavefront()` — per-tile wavefront

**文件**: `sdis_solve_wavefront.c` L1382-1540

核心循环 (L1437-1505):

```c
while(wf.active_count > 0) {
    wf.total_steps++;
    // Step A: collect_ray_requests(&wf)      → 收集所有 active path 的射线请求
    // Step B: s3d_scene_view_trace_rays_batch_ctx(scn->s3d_view, wf.batch_ctx, ...)
    // Step C: distribute_and_advance(&wf, scn) → 分发结果,推进状态机
    // Step D: update_active_count(&wf)         → 统计剩余 active paths
}
```

**path 池大小**: `total_paths = tile_size[0] × tile_size[1] × spp`

**ray 缓冲大小**: `max_rays = total_paths × 2`（每 path 最多 2 条射线/步）

#### 第三层: GPU batch trace

**文件**: `cus3d_trace.cu` L763-870

```c
const uint32_t block_size = 256;
uint32_t grid_size = (num_rays + block_size - 1) / block_size;
trace_rays_kernel<<<grid_size, block_size, 0, s>>>(...);
cudaStreamSynchronize(s);
```

**所有调用共享 `dev->stream`**（`cus3d_device` L42: `cudaStream_t stream`），全局唯一。

### 1.2 数据流全景

```
per_thread_rng[ithread]
        │
        ▼
init_all_paths()              ← 初始化 path_state[N], N = 16×spp
        │
        ▼
┌── wavefront main loop ──┐
│                          │
│  collect_ray_requests()  │ → ray_requests[]: float origin/dir/range
│         │                │
│         ▼                │
│  batch_trace()           │ → ray_hits[]: struct s3d_hit
│         │                │
│         ▼                │
│  distribute_and_advance()│ → path.phase 状态转换
│         │                │     path.T.value 温度累加
│         │                │     path.rwalk.vtx.P 位置更新
│         │                │
│  update_active_count()   │ → active_count--
│         │                │
└────┬────┘                │
     │ active_count == 0   │
     ▼                     │
collect_results()          ← tile.pixel[x][y].acc_temp += T.value
     │
     ▼
write_tile() → estimator_buffer  ← 最终输出
```

### 1.3 path_state 结构分析

**定义于**: `sdis_solve_wavefront.h` L107-197, 约 600 bytes/path

```c
struct path_state {
    // 身份标识 (12B)
    uint32_t path_id, realisation_idx;
    uint16_t pixel_x, pixel_y;
    size_t   ipix_image[2];

    // 生命周期 (8B)
    enum path_phase phase;
    int active;

    // 随机游走核心 (~200B)
    struct rwalk         rwalk;    // position, time, hit, enc_id
    struct rwalk_context ctx;      // Tmin/That/branchings

    // 温度累加 (~24B)
    struct temperature T;          // func ptr + value + done

    // 辐射路径暂存 (20B)
    float rad_direction[3];
    int rad_bounce_count, rad_retry_count;

    // delta-sphere 暂存 (~180B)
    float ds_dir0[3], ds_dir1[3];
    struct s3d_hit ds_hit0, ds_hit1;
    double ds_delta_solid;
    ...

    // 射线请求输出 (~64B)
    struct path_ray_request ray_req;
    int needs_ray;

    // RNG (非拥有指针, 8B)
    struct ssp_rng* rng;
};
```

**关键观察**: `rng` 字段为非拥有指针，指向 per-thread RNG。当前所有 path 共享同一个
RNG（同一个 OMP 线程的 `per_thread_rng[ithread]`）。

---

## 二、性能瓶颈量化分析

### 2.1 GPU 利用率计算

| 参数 | 值 | 来源 |
|------|------|------|
| TILE_SIZE | 4 | `sdis_tile.h:27` |
| 像素/tile | 16 (4×4) | |
| 典型 SPP | 32 | 用户配置 |
| initial paths/tile | 512 | 16 × 32 |
| max rays/step | 1024 | 512 × 2 |
| GPU block_size | 256 | `cus3d_trace.cu:763` |
| initial blocks | 2-4 | 512÷256 ~ 1024÷256 |
| RTX 4090 SM count | 128 | 硬件 |
| **GPU 占用率** | **1.5-3%** | 4 / 128 |

### 2.2 Wavefront 宽度衰减模型

#### 2.2.1 理论衰减曲线

蒙特卡洛路径的步数分布极不均匀：

```
Step 0:    512 paths ─── 所有路径发射 camera ray (radiative)
Step 1:    ~400 paths ── radiative miss 退出 (~20% 直接看到环境)
Step 2-5:  ~300 paths ── 更多 radiative 路径到达 boundary→known temperature
Step 5-20: ~150 paths ── boundary→conductive 进入 delta-sphere 循环
Step 20-50: ~80 paths ── delta-sphere 深层游走
Step 50-200: ~20 paths ─ 长尾路径 (复杂几何中的深层传导)
Step 200+:  ~5 paths ── 极端路径 (retry + robust fallback)
```

**平均步数加权射线数**:

假设一个 tile 总共 512 条 path，平均 30 步/path：
- 初始射线: 512
- 中期: ~200
- 后期: ~30
- 加权平均: ~170 rays/step

**170 rays → 1 个 block → GPU 利用率 ~0.8%**

#### 2.2.2 实际运行日志验证 — tile 间 200,000 倍耗时差异

以下是同一场景连续 tile 的实际执行日志（4×4 tile, spp=32, 512 initial paths）：

```
tile (112,104) 4x4 spp=32:  elapsed=303 usecs     steps=1      rays=512       (rad=512  cond_ds=0       ds_retry=0)     done: rad=512 temp=0 bnd=0  fail=0   max_depth=2
tile (116,104) 4x4 spp=32:  elapsed=335 usecs     steps=1      rays=512       (rad=512  cond_ds=0       ds_retry=0)     done: rad=512 temp=0 bnd=0  fail=0   max_depth=2
tile (112,108) 4x4 spp=32:  elapsed=8.16 secs     steps=7878   rays=75407     (rad=513  cond_ds=74728   ds_retry=166)   done: rad=496 temp=0 bnd=14 fail=2   max_depth=18195
tile (116,108) 4x4 spp=32:  elapsed=62.8 secs     steps=16494  rays=576972    (rad=520  cond_ds=574788  ds_retry=1664)  done: rad=427 temp=1 bnd=84 fail=0   max_depth=38701
tile (120,104) 4x4 spp=32:  elapsed=361 usecs     steps=1      rays=512       (rad=512  cond_ds=0       ds_retry=0)     done: rad=512 temp=0 bnd=0  fail=0   max_depth=2
tile (124,104) 4x4 spp=32:  elapsed=2.03 msecs    steps=1      rays=512       (rad=512  cond_ds=0       ds_retry=0)     done: rad=512 temp=0 bnd=0  fail=0   max_depth=2
```

**量化分析**:

| 指标 | 纯辐射 tile<br>(112,104) | 导热 tile<br>(112,108) | 极端导热 tile<br>(116,108) | 倍数差 |
|------|------------------------|-----------------------|---------------------------|--------|
| 耗时 | 303 µs | 8.16 s | 62.8 s | **~207,000×** |
| 总步数 | 1 | 7,878 | 16,494 | 16,494× |
| 总射线 | 512 | 75,407 | 576,972 | 1,127× |
| 导热射线占比 | 0% | 99.1% | 99.6% | — |
| max_depth | 2 | 18,195 | 38,701 | 19,350× |
| 每 path 平均步 | ~1 | ~147 | ~1,128 | — |

**关键发现**: 相邻的两行 tile（y=104 vs y=108），仅差 4 像素，耗时从微秒级
跃升到分钟级。这直接反映了场景的空间异构性——y=104 行的 camera ray 全部命中
空气/环境（radiative miss），而 y=108 行穿入固体材料，触发了大量 delta-sphere
导热随机游走。

#### 2.2.3 tile 内部的路径深度异构性

以 tile (116,108) 为例进一步分析：

- **512 条 path，max_depth = 38,701**
- 这意味着至少 1 条路径独自走了 38,701 步
- 辐射路径仅占 520/576,972 = **0.09%** 的射线
- done: rad=427 表明 ~83% 的路径最终以辐射方式终结
  （但它们在中间经历了长时间的导热游走）

**wavefront 宽度随步数的实际衰减**（推算）：

```
Step 1:          512 paths (100%)  ── 全部发射 camera ray
Step 2-10:       ~500 paths (98%) ── 大部分命中 boundary → 进入 conductive
Step 10-100:     ~400 paths (78%) ── delta-sphere 游走中
Step 100-1000:   ~200 paths (39%) ── 逐渐到达 boundary/temperature
Step 1000-5000:  ~50 paths  (10%) ── 长尾 deep conductive paths
Step 5000-16494: ~10 paths  (2%)  ── 极端长尾，1 个 path 拖住整个 tile
Step 16494:      1 path     (0.2%)── 最后一条路径完成
```

**wavefront 后期 GPU 利用率**: 最后 ~11,000 步（步数 5000→16494）中，
每步仅 ~10×2 = 20 射线。20 / 256 = 0.08 个 block，GPU 利用率 **< 0.001%**。
这段时间占总耗时的 **~60%**（约 38 秒），整个 GPU 基本空转。

### 2.3 OMP 多线程 ≠ GPU 并发

| OMP 线程 | 各自 wavefront | 共享 CUDA stream |
|----------|---------------|-----------------|
| Thread 0 | tile (0,0): 512 paths | ↘ |
| Thread 1 | tile (1,0): 512 paths | → `dev->stream` (唯一) |
| Thread 2 | tile (0,1): 512 paths | ↗ |
| ... | ... | |

由于所有 `s3d_scene_view_trace_rays_batch_ctx()` 调用都走同一个 CUDA stream
（见 `s3d_scene_view_batch_trace.cpp` L139: `cudaStream_t s = dev->stream`），
多 OMP 线程的 kernel 在 GPU 上**串行执行**。

更严重的是，`cudaStreamSynchronize(s)` 会跨线程阻塞——线程 A 的 sync 可能
等到线程 B 的 kernel 完成才返回，造成**数据竞争**（线程 A 读到线程 B 的结果）。

### 2.4 总瓶颈汇总

| 瓶颈 | 原因 | 影响 | 可解决性 |
|------|------|------|----------|
| **wavefront 宽度 = TILE_SIZE² × SPP** | per-tile 隔离 | GPU 利用率 <3% | persistent pool（§3） |
| **wavefront 衰减** | 路径深度不均 | 后期 <1% 利用率 | path refill（§3.5） |
| **shared CUDA stream** | `cus3d_device` 全局唯一 | OMP 无法真并发 | 单线程统一调度（§3.1） |
| **tile→pixel→estimator 间接写入** | 先写 tile, 后 gather | 增加延迟,MPI 耦合 | 直写 estimator（§3.6） |
| **场景空间异构性** | 空气/固体 tile 耗时差 200,000× | tile 间负载极端不均 | 跨 tile 混合调度（§3.1） |
| **路径类型异构性** | radiative 1-2 步 vs conductive 38,000 步 | pool 内有效宽度快速坍缩 | stream compaction + 类型分桶（§3.7, §3.8） |
| **长尾路径主导运行时间** | 最慢 2% 路径占 60%+ 耗时 | refill 在 drain phase 失效 | drain 阶段特殊处理（§3.9） |

### 2.5 路径类型异构性与长尾效应 — 深层分析

> **核心洞察**: Persistent pool + refill 解决了"初始 wavefront 宽度不足"和
> "衰减后无法补充"两个问题，但**未解决 pool 内部路径类型异构导致的有效并行
> 宽度坍缩问题**。这是一个独立于 pool size 和 refill 策略之外的第三类瓶颈。

#### 2.5.1 两类根本不同的路径

从日志数据可以明确区分两类路径特征：

| 特征 | 辐射类路径 (Radiative) | 导热类路径 (Conductive DS) |
|------|----------------------|---------------------------|
| 典型步数 | 1-5 | 100-38,000+ |
| 每步射线数 | 1 | 2 |
| 状态机阶段 | `PATH_RAD_TRACE_PENDING` | `PATH_COUPLED_COND_DS_PENDING` |
| CPU 端计算量/步 | 低（BRDF 采样） | 高（solid_get_properties, time_rewind, volumic_power） |
| 出现条件 | camera ray 命中空气 | camera ray 穿入固体 |
| 场景空间分布 | 连续区域（空气侧） | 连续区域（固体侧） |

> 注：`step_radiative_trace` 仅需 1 次 hit + BRDF 判定（~10 次浮点运算），
> 而 `step_conductive_ds_process` 需要 2 次 hit + delta 计算 + enclosure 验证 +
> solid properties + volumic power 积分 + time rewind（~200 次浮点运算）。
> **即使射线数相同，CPU 端工作量也差 ~20×**。

#### 2.5.2 Persistent Pool 中的类型组成动态变化

假设 pool_size = 32768，图像中 50% 像素看到固体、50% 看到空气：

```
╔═══════════════════╤════════════════╤════════════════╤══════════════════════╗
║ 阶段              │ 辐射路径       │ 导热路径       │ 有效并行度           ║
╠═══════════════════╪════════════════╪════════════════╪══════════════════════╣
║ 初始 fill         │ 16384 (50%)    │ 16384 (50%)    │ 32768 (100%)         ║
║ Refill 稳态       │ ~16000 (翻转快)│ 16384 (持续)   │ 32768 (100%) ✅      ║
║ 任务队列告急       │ ↘ 递减        │ ~16384 (残留)  │ ↘ 递减               ║
║ 任务队列耗尽      │ 0（无新任务）  │ ~3000 残留     │ 3000 (↓91%) ⚠       ║
║ Drain 中期        │ 0              │ ~500 深度游走  │ 500 (↓98%) ⚠⚠      ║
║ Drain 尾部        │ 0              │ ~50 极端长尾   │ 50 (↓99.8%) ❌      ║
║ 最后一条路径      │ 0              │ 1              │ 1 (0.003%) ❌       ║
╚═══════════════════╧════════════════╧════════════════╧══════════════════════╝
```

**refill 机制的有效区间**: 当 `task_queue` 尚有任务时，refill 能有效补充
完成的快路径（辐射类），保持 pool 满载。但 task_queue 一旦耗尽，pool 中
只剩下**清一色的长尾导热路径**，有效并行宽度不可逆地坍缩。

#### 2.5.3 Drain Phase 耗时估算

假设总任务数 = 256×256×32 = 2,097,152（典型配置），pool_size = 32768：

- **Refill 阶段**: 快路径 ~2 步完成，持续被新任务替换。由于 refill 不断
  补入快路径，GPU 持续满载。每步 ~32K-65K 射线，GPU SM 占用率 ~100%。

- **Drain 阶段**: task_queue 为空后，pool 中残留的全是高步数导热路径。
  假设 ~5% 的总路径（~105,000 条）平均需要 500 步，max_depth 路径需要
  38,000 步。这些路径**无法被补充**，wavefront 宽度从 32768 指数衰减至 1。

```
  Drain phase 总射线估算:
    105,000 paths × avg 500 steps × 2 rays/step = 105,000,000 rays

  对比 Refill phase 总射线（快路径被 refill 驱动）:
    (2,097,152 - 105,000) fast_paths × avg 2 steps × 1 ray/step ≈ 4,000,000 rays

  → Drain phase 射线量是 Refill phase 的 ~26 倍!
    但 drain phase 的平均 batch size 远小于 refill phase。
```

> **关键结论**: 对于包含固体导热区域的场景，**drain phase 是运行时间的绝对
> 主体**（>80% 的射线、>90% 的墙钟时间），而 refill 机制在 drain phase
> 完全失效。Persistent pool 架构必须额外解决 drain phase 的并行效率问题。

#### 2.5.4 对 GPU 并行优化的三层影响

**第一层 — Tile 间不均衡**（persistent pool 已解决 ✅）：
- 跨 tile 混合消除了 tile (112,104) 空转等待 tile (116,108) 的问题
- 所有像素的路径在同一个 pool 中竞争 slot

**第二层 — Pool 内路径类型异构**（需 stream compaction + 分桶 §3.7-3.8）：
- 同一步中，radiative 路径执行 `step_radiative_trace`（branch A），
  conductive 路径执行 `step_conductive_ds_process`（branch B）
- 若未来 step 函数移至 GPU kernel，warp 内线程执行不同分支 → warp divergence
- 即使在 CPU 端，遍历 32768 个 slot 但只有 500 个 active → 缓存不友好
- `collect_ray_requests` 需扫描全部 slot → O(pool_size) 而非 O(active_count)

**第三层 — 长尾路径主导运行时间**（需 drain 优化策略 §3.9）：
- 最慢的 ~2% 路径决定了整体完成时间
- 在 drain phase，每步的 batch trace kernel launch 开销可能 > 实际 ray 计算时间
- 需要考虑：batch size < 阈值时是否值得继续使用 GPU？

---

## 三、新架构: Persistent Wavefront Pool

### 3.1 核心思想

**取消 tile 粒度的 wavefront 隔离，将全图像的所有 pixel×SPP 任务放入一个全局池，
由单线程 CPU 调度器驱动 GPU 执行。完成的 path 立即被新任务替换，保持 wavefront
宽度恒定。**

### 3.2 架构对比

```
     现有 (Phase B-2)                      新 (Phase B-3)
    ═══════════════                       ═══════════════
sdis_solve_camera                     sdis_solve_camera
  │                                     │
  ├─ OMP parallel for                   ├─ 【不再 OMP】
  │   ├─ Thread 0:                      │
  │   │   solve_tile_wavefront          └─ solve_camera_persistent_wavefront
  │   │   (512 paths, own batch)              │
  │   ├─ Thread 1:                            ├─ wavefront_pool (32768 slots)
  │   │   solve_tile_wavefront                │   ├─ task_queue: 全部 pixel×SPP
  │   │   (512 paths, own batch)              │   ├─ path_state[32768]
  │   └─ ...                                  │   └─ batch_ctx (one, 65536 rays max)
  │                                           │
  │   各线程独立 batch_ctx                     ├─ while(tasks remain || active > 0)
  │   共享 CUDA stream ✗                      │   ├─ refill: done→new task
  │                                           │   ├─ collect rays (~32K)
  │                                           │   ├─ batch trace (<<<128, 256>>>)
  │                                           │   ├─ distribute + advance
  │                                           │   └─ write done paths → estimator
  │                                           │
  gather_tiles() → write_tile()               └─ finalize_estimator_buffer()
```

### 3.3 GPU 利用率提升

| 指标 | Phase B-2 (per-tile) | Phase B-3 (persistent) | B-3 + compaction/drain | 提升倍数 |
|------|---------------------|----------------------|----------------------|---------|
| wavefront 宽度 | 512 → 30 (衰减) | 32768 (恒定 refill) | 32768 (refill) → CPU fallback (drain) | **60-1000×** |
| rays/step | 512 → 30 | 32768 → 65536 | refill: 65536 / drain: CPU | **60-2000×** |
| GPU blocks/step | 2 → 0.1 | 128 → 256 | refill: 256 / drain: 0 (CPU) | **64-2560×** |
| GPU SM 占用率 | 1.5% → 0.08% | refill: **100%** / drain: ↘0% | refill: **100%** / drain: N/A (CPU) | **66-1250×** |
| CPU 遍历效率 | O(512) per step | O(32768) per step | O(active_compact) per step | **drain: 1-1000×** |
| batch_ctx 数量 | N (per OMP thread) | **1** | **1** | 无 stream 竞争 |
| warp divergence (未来 GPU step) | N/A | 混合类型 → 50% | 分桶 → ~0% | **2×** |

### 3.4 数据结构设计

#### 3.4.1 全局任务队列

```c
/* 一个 pixel×realisation 的 work item */
struct pixel_task {
    size_t   ipix_image[2];  /* 图像空间坐标          */
    uint16_t tile_x, tile_y; /* tile 坐标 (用于 tile 写入) */
    uint16_t pix_x, pix_y;  /* tile 内坐标            */
    uint32_t spp_idx;        /* realisation index      */
};
```

**Morton 序遍历保证**：任务队列按原始 tile→pixel Morton→SPP 顺序生成，
与原版 `solve_tile` 的遍历顺序完全一致。`refill` 按 FIFO 顺序从队列取任务。

#### 3.4.2 Persistent Wavefront Pool

```c
struct wavefront_pool {
    /* --- Path pool (固定大小) --- */
    struct path_state*  slots;
    size_t              pool_size;       /* e.g. 32768 */
    size_t              active_count;

    /* --- 全局任务队列 --- */
    struct pixel_task*  task_queue;
    size_t              task_count;      /* image_w × image_h × spp */
    size_t              task_next;       /* 下一个未分配的 task */

    /* --- RNG pool --- */
    struct ssp_rng**    slot_rngs;       /* per-slot 独立 RNG */

    /* --- Stream Compaction 索引 (§3.7) --- */
    uint32_t*           active_indices;  /* 活跃 slot 索引的紧凑数组 */
    size_t              active_compact;  /* active_indices 的有效长度 */

    uint32_t*           need_ray_indices;/* 需要射线的 slot 索引 */
    size_t              need_ray_count;  /* need_ray_indices 的有效长度 */

    uint32_t*           done_indices;    /* 已完成的 slot 索引 */
    size_t              done_count;      /* done_indices 的有效长度 */

    /* --- 路径类型分桶 (§3.8) --- */
    uint32_t*           bucket_radiative;   /* 辐射类 slot 索引 */
    size_t              bucket_radiative_n;
    uint32_t*           bucket_conductive;  /* 导热类 slot 索引 */
    size_t              bucket_conductive_n;

    /* --- 射线请求缓冲 --- */
    struct s3d_ray_request* ray_requests;
    uint32_t*           ray_to_slot;     /* ray → slot index */
    uint32_t*           ray_slot_sub;    /* ray → sub-ray (0 or 1) */
    struct s3d_hit*     ray_hits;
    size_t              ray_count;
    size_t              max_rays;        /* pool_size × 2 */

    /* --- Batch trace context --- */
    struct s3d_batch_trace_context* batch_ctx;

    /* --- 直写 estimator buffer --- */
    struct sdis_estimator_buffer* buf;

    /* --- Tile 收集 (兼容 gather_tiles 流程) --- */
    struct tile**       tiles;           /* 预分配的 tile 数组 */
    size_t              ntiles;

    /* --- Drain phase 控制 (§3.9) --- */
    int                 in_drain_phase;  /* task_queue 耗尽标志 */
    size_t              drain_step_count;/* drain phase 内的步数 */
    size_t              drain_fallback_threshold; /* batch_size < 此值时切 CPU */

    /* --- 统计 --- */
    size_t total_steps;
    size_t total_rays_traced;
    size_t paths_completed;
    size_t paths_failed;
    size_t max_path_depth;

    /* --- 诊断 (§3.7.3) --- */
    size_t diag_min_batch;    /* 记录最小 batch size */
    size_t diag_max_batch;    /* 记录最大 batch size */
    size_t diag_refill_count; /* refill 次数统计 */
    size_t diag_drain_rays;   /* drain phase 总射线数 */
    size_t diag_refill_rays;  /* refill phase 总射线数 */
};
```

#### 3.4.3 Pool Size 选择依据

```
pool_size = min(image_w × image_h × spp, GPU_TARGET_OCCUPANCY)

GPU_TARGET_OCCUPANCY 计算:
  RTX 4090: 128 SM × 8 warps/SM × 32 threads/warp = 32768 concurrent threads
  每 thread 1 path → pool_size = 32768

  但每步最多 2 rays/path → max_rays = 65536
  65536 / 256 = 256 blocks → 刚好 128 SM × 2 blocks/SM

  内存: 32768 × 600 B (path_state) ≈ 20 MB
         65536 × ~80 B (ray_request + hit) ≈ 5 MB
         batch GPU buffers ≈ 10 MB
         总计 ≈ 35 MB << 24 GB VRAM
```

### 3.5 主循环伪代码

```c
res_T solve_camera_persistent_wavefront(scn, per_thread_rng, nthreads,
    enc_id, cam, time_range, image_def, spp, register_paths,
    pix_sz, picard_order, diff_algo, buf,
    progress, pcent_progress, progress_label)
{
    /* 1. 创建 estimator_buffer (由 solve_camera 预分配, 通过 buf 传入) */

    /* 2. 生成全局任务队列 (tile Morton → pixel Morton → SPP) */
    task_queue = generate_task_queue(image_w, image_h, spp);

    /* 3. 创建 pool */
    wavefront_pool_create(&pool, POOL_SIZE, max_rays);

    /* 4. 共享 per_thread_rng 到所有 pool slots (round-robin + ref_get) */
    for(i = 0; i < POOL_SIZE; i++) {
        pool.slot_rngs[i] = per_thread_rng[i % nthreads];
        SSP(rng_ref_get(pool.slot_rngs[i]));
    }

    /* 5. Initial fill: 从 task_queue 取 POOL_SIZE 个 task 填入 slots */
    initial_fill(&pool, scn, cam, ...);

    /* 6. 初始 non-ray step (PATH_INIT → PATH_RAD_TRACE_PENDING) */
    for(i = 0; i < pool.pool_size; i++)
        advance_one_step_no_ray(&pool.slots[i], scn, &advanced);

    /* 7. Main loop */
    while(pool.active_count > 0 || pool.task_next < pool.task_count) {

        /* 7a. Stream compaction: 构建活跃路径紧凑索引 */
        compact_active_paths(&pool);
        /* → pool.active_indices[0..active_compact-1] = 活跃 slot 索引
         * → pool.need_ray_indices[0..need_ray_count-1] = 需射线的 slot 索引
         * → pool.done_indices[0..done_count-1] = 已完成的 slot 索引 */

        /* 7b. Collect ray requests (仅遍历 need_ray_indices) */
        collect_ray_requests_compact(&pool);

        /* 7c. Batch trace */
        if(pool.ray_count > 0) {
            s3d_scene_view_trace_rays_batch_ctx(
                scn->s3d_view, pool.batch_ctx,
                pool.ray_requests, pool.ray_count,
                pool.ray_hits, &stats);
            pool.total_rays_traced += pool.ray_count;
        }

        /* 7d. Distribute + advance (仅遍历 need_ray_indices) */
        distribute_and_advance_compact(&pool, scn);

        /* 7e. Cascade non-ray steps (仅遍历 active_indices) */
        cascade_non_ray_steps_compact(&pool, scn);

        /* 7f. Harvest done paths → write to estimator_buffer */
        harvest_completed_paths(&pool, buf);

        /* 7g. Refill: 用新 task 替换 done slots */
        refill_pool(&pool, scn, cam, ...);

        /* 7h. Detect drain phase transition */
        if(!pool.in_drain_phase && pool.task_next >= pool.task_count) {
            pool.in_drain_phase = 1;
            log_info(dev, "entering drain phase: %lu active paths remain\n",
                     (unsigned long)pool.active_count);
        }

        /* 7i. Diagnostics */
        update_diagnostics(&pool);

        pool.total_steps++;
    }

    /* 8. Finalize */
    print_progress_completion(dev, progress, progress_label);
    log_drain_phase_report(&pool);
    finalize_estimator_buffer(buf, rng_proxy, spp);
    *out_buf = buf;

    wavefront_pool_destroy(&pool);
}
```

### 3.6 关键函数说明

#### `harvest_completed_paths()` — 替代 collect_results + write_tile

```c
static res_T
harvest_completed_paths(struct wavefront_pool* pool,
                        struct sdis_estimator_buffer* buf)
{
    for(i = 0; i < pool->pool_size; i++) {
        struct path_state* p = &pool->slots[i];
        if(p->phase != PATH_DONE) continue;

        /* 直接写入 estimator_buffer，跳过 tile 中间层 */
        struct sdis_estimator* est =
            estimator_buffer_grab(buf, p->ipix_image[0], p->ipix_image[1]);

        if(p->T.done) {
            est->temperature.sum  += p->T.value;
            est->temperature.sum2 += p->T.value * p->T.value;
            est->nrealisations    += 1;
        }

        pool->paths_completed++;
        p->active = 0;
    }
}
```

**与 tile 机制的关系**: harvest 直接写入 `estimator_buffer`，不再经过
tile→write_tile→setup_estimator_from_pixel 的间接路径。这使得结果在 path
完成时立即可用，无需等待整个 tile 完成。

但为了兼容 MPI gather_tiles 流程（如果需要），可选择保留 tile 写入模式。
非 MPI 场景下直接写 estimator 更高效。

#### `refill_pool()` — 保持 wavefront 宽度恒定

```c
static res_T
refill_pool(struct wavefront_pool* pool,
            struct sdis_scene* scn,
            const struct sdis_camera* cam,
            const double time_range[2],
            const double pix_sz[2], ...)
{
    for(i = 0; i < pool->pool_size; i++) {
        struct path_state* p = &pool->slots[i];
        if(p->active) continue;
        if(pool->task_next >= pool->task_count) continue;

        /* 从任务队列取下一个 */
        struct pixel_task* task = &pool->task_queue[pool->task_next++];

        /* 初始化 path (与 init_all_paths 中单个 path 初始化相同) */
        init_single_path(p, task, pool->slot_rngs[i], scn, cam,
                         time_range, pix_sz, ...);

        p->active = 1;
        pool->active_count++;
    }
}
```

### 3.7 Stream Compaction — 稀疏活跃路径压缩

#### 3.7.1 问题

当前 `collect_ray_requests()` 和 `distribute_and_advance()` 通过
`for(i = 0; i < pool_size; i++)` 遍历全部 slot。在 drain phase，pool_size = 32768
但活跃路径可能仅 50 条 — 遍历效率 $\frac{50}{32768}$ = 0.15%。

更严重的是，稀疏遍历导致：
- **CPU cache miss**: 活跃 slot 分散在 32768×600B = 19.2MB 中，L3 cache 无法覆盖
- **GPU kernel 无效线程**: 如果未来 step 函数移至 GPU，空 slot 对应的线程
  在 `if(!p->active) return;` 后空转，浪费 warp 槽位

#### 3.7.2 解决方案：每步重建紧凑索引

```c
static void
compact_active_paths(struct wavefront_pool* pool)
{
    size_t i;
    pool->active_compact = 0;
    pool->need_ray_count = 0;
    pool->done_count     = 0;

    for(i = 0; i < pool->pool_size; i++) {
        struct path_state* p = &pool->slots[i];

        if(p->phase == PATH_DONE && !p->active) {
            /* 已完成：加入 done 列表供 harvest + refill */
            pool->done_indices[pool->done_count++] = (uint32_t)i;
            continue;
        }
        if(!p->active) continue;

        /* 活跃路径 */
        pool->active_indices[pool->active_compact++] = (uint32_t)i;

        /* 需要射线的子集 */
        if(p->needs_ray && p->ray_req.ray_count > 0) {
            pool->need_ray_indices[pool->need_ray_count++] = (uint32_t)i;
        }
    }
}
```

**内存开销**: 3 × pool_size × 4B = 3 × 32768 × 4 = 384 KB（可忽略）

**性能收益**:
- `collect_ray_requests`: O(need_ray_count) 而非 O(pool_size)
- `distribute_and_advance`: O(need_ray_count) 而非 O(pool_size)
- `cascade_non_ray_steps`: O(active_compact) 而非 O(pool_size)
- `harvest + refill`: O(done_count) 而非 O(pool_size)

#### 3.7.3 紧凑遍历的函数改写

```c
/* 替代原版遍历全部 slot 的 collect_ray_requests */
static res_T
collect_ray_requests_compact(struct wavefront_pool* pool)
{
    size_t ray_idx = 0;
    size_t k;

    for(k = 0; k < pool->need_ray_count; k++) {
        uint32_t i = pool->need_ray_indices[k];
        struct path_state* p = &pool->slots[i];
        /* ... 与原版相同的射线收集逻辑 ... */
        /* 但保证 p->active && p->needs_ray，无需检查 */
    }

    pool->ray_count = ray_idx;
    return RES_OK;
}

/* 替代原版遍历全部 slot 的 distribute_and_advance */
static res_T
distribute_and_advance_compact(struct wavefront_pool* pool,
                               struct sdis_scene* scn)
{
    size_t r, k;
    res_T res = RES_OK;

    /* 分发射线结果（与原版相同） */
    for(r = 0; r < pool->ray_count; r++) { /* ... */ }

    /* cascade non-ray steps: 仅遍历 active_indices */
    for(k = 0; k < pool->active_compact; k++) {
        uint32_t i = pool->active_indices[k];
        struct path_state* p = &pool->slots[i];
        if(!p->active) continue;
        /* ... run non-ray steps until needs_ray or done ... */
    }

    return res;
}
```

**Compaction 本身的开销**: 单次 `compact_active_paths` 遍历 pool_size 个 slot，
执行简单的条件判断 + 索引写入。在 pool_size = 32768 时约 30-50 µs（cache-friendly
sequential scan），远小于一次 GPU kernel launch（~5-20 µs）+ sync（~10 µs）。

### 3.8 路径类型分桶调度

#### 3.8.1 动机

从代码 `advance_one_step_with_ray()` 可见以下分支结构：

```c
switch(p->phase) {
case PATH_RAD_TRACE_PENDING:
    res = step_radiative_trace(p, scn, hit0);     /* Branch A: 轻量 */
    break;
case PATH_COUPLED_COND_DS_PENDING:
    res = step_conductive_ds_process(p, scn, hit0, hit1); /* Branch B: 重量 */
    break;
}
```

当 radiative 和 conductive 路径混在同一个遍历中时：
- **CPU**: if-else 分支预测失败率高（两种类型交替出现）
- **GPU (未来)**: 同一 warp 中线程走不同分支 → **warp divergence**,
  有效 SIMD 利用率降至 50% 或更低

#### 3.8.2 分桶方案

在 `compact_active_paths` 中同时构建类型分桶：

```c
static void
compact_active_paths(struct wavefront_pool* pool)
{
    size_t i;
    pool->active_compact     = 0;
    pool->need_ray_count     = 0;
    pool->done_count         = 0;
    pool->bucket_radiative_n = 0;
    pool->bucket_conductive_n = 0;

    for(i = 0; i < pool->pool_size; i++) {
        struct path_state* p = &pool->slots[i];

        if(p->phase == PATH_DONE) {
            pool->done_indices[pool->done_count++] = (uint32_t)i;
            continue;
        }
        if(!p->active) continue;

        pool->active_indices[pool->active_compact++] = (uint32_t)i;

        if(p->needs_ray && p->ray_req.ray_count > 0) {
            pool->need_ray_indices[pool->need_ray_count++] = (uint32_t)i;

            /* 按等待的射线类型分桶 */
            if(p->phase == PATH_RAD_TRACE_PENDING) {
                pool->bucket_radiative[pool->bucket_radiative_n++] = (uint32_t)i;
            } else if(p->phase == PATH_COUPLED_COND_DS_PENDING) {
                pool->bucket_conductive[pool->bucket_conductive_n++] = (uint32_t)i;
            }
        }
    }
}
```

#### 3.8.3 分桶的收益

**CPU 端（当前阶段，Phase B-3 M1-M3）**:

分桶后的 `distribute_and_advance` 可以分两个循环处理：

```c
/* 先处理所有 radiative paths（连续、cache-friendly） */
for(k = 0; k < pool->bucket_radiative_n; k++) {
    uint32_t i = pool->bucket_radiative[k];
    step_radiative_trace(&pool->slots[i], scn, &ray_hits[...]);
}

/* 再处理所有 conductive paths（连续、cache-friendly） */
for(k = 0; k < pool->bucket_conductive_n; k++) {
    uint32_t i = pool->bucket_conductive[k];
    step_conductive_ds_process(&pool->slots[i], scn, &ray_hits[...], ...);
}
```

收益：
- 分支预测命中率从 ~50% 提升至 ~99%
- `step_conductive_ds_process` 访问的 `solid_props`, `delta_sphere` 等数据
  在连续调用中有更好的 cache 局部性

**GPU 端（未来 Phase C）**:

分桶后可以为每种路径类型 launch 单独的 kernel：

```c
/* Radiative kernel: 轻量，高吞吐 */
step_radiative_kernel<<<grid_rad, 256>>>(..., bucket_radiative, bucket_radiative_n);

/* Conductive kernel: 重量，需要更多寄存器 */
step_conductive_kernel<<<grid_cond, 128>>>(..., bucket_conductive, bucket_conductive_n);
```

- 消除 warp divergence（同一 kernel 内所有线程走相同分支）
- 可以为不同 kernel 选择不同的 block_size 和寄存器配置
- Conductive kernel 可以使用 shared memory 缓存 `solid_props`

#### 3.8.4 分桶对射线 batch 的影响

当前所有类型的射线混入同一个 batch。分桶后可以选择：

**方案 A: 统一 batch（推荐，当前阶段）**

所有射线仍然放入同一个 batch 进行 GPU trace。原因：BVH 遍历不区分射线用途，
统一 batch 保证最大的射线并行度。分桶仅影响 distribute 阶段的 step 函数调用。

**方案 B: 分离 batch（可选，仅在 GPU 端优化时考虑）**

radiative 射线和 conductive 射线分别 batch trace。优势：可以为两类射线设置
不同的 `range` 范围（radiative 通常 range 大，conductive delta-sphere 的 range
= delta_solid，很小），有助于 BVH early-exit 优化。

### 3.9 Drain Phase 分析与优化策略

#### 3.9.1 Drain Phase 定义

当 `pool.task_next >= pool.task_count`（全局任务队列耗尽）且 `pool.active_count > 0`
时，进入 drain phase。此后不再有新任务补入 pool，wavefront 宽度单调递减至 0。

#### 3.9.2 Drain Phase 的特征

```
refill phase    │ drain phase
(GPU 满载)      │ (GPU 利用率指数衰减)
                │
 ████████████████│█████████▓▓▓▓▒▒▒░░░···    ← 活跃路径数
 ████████████████│████████████████████████   ← 射线/步 (batch size)
                │          ↓↓↓
                │     batch size << GPU capacity
                │
────────────────┼──────────────────────────→ 时间
```

**从日志数据推算的 drain phase 特征**:

以 tile (116,108) 为类比（512 paths, max_depth=38701）：
- 最后 2% 的路径（~10 条）消耗了 ~60% 的步数
- 最后 0.2% 的路径（~1 条）消耗了 ~55% 的步数

放大到 persistent pool（32768 paths）：
- 最后 2%（~655 条）可能消耗 **>50% 的总步数**
- 此时每步 batch size ≈ 655×2 = 1310 射线 → 5 blocks → GPU 利用率 ~4%
- 最后 0.2%（~66 条）每步 132 射线 → 0.5 blocks → GPU 利用率 **~0.4%**

#### 3.9.3 Drain Phase 优化策略

**策略 1: GPU→CPU 自适应回退（Adaptive Fallback） — 不可行**

简短说明：`s3d` 的 CPU 与 GPU 实现以及相关数据结构/状态是**隔离**的，
项目无法维护两套后端（实现与数据）的一致性与同步，因此不可能在本项目内
安全地实现 GPU→CPU 的自适应回退。该方案在理论上可以减少小 batch 时的开销，
但在当前代码库与部署约束下不可采用。

**策略 2: 路径提前终止/截断（Path Truncation）**

对于步数极高的路径，可以在 drain phase 中设置更激进的截断阈值：

```c
/* 在 drain phase 中，降低 max_depth 限制 */
if(pool.in_drain_phase && p->steps_taken > DRAIN_MAX_DEPTH) {
    /* 强制终止：使用当前累积值作为最终温度估计 */
    p->T.done = 1;
    p->phase = PATH_DONE;
    p->active = 0;
    p->done_reason = -2; /* drain truncation */
    pool.paths_truncated++;
}
```

> **注意**: 路径截断会引入偏差（bias），仅在可接受的精度损失范围内使用。
> 可以通过统计 `done_reason = -2` 的路径比例和温度分布来评估偏差大小。
> 对于 spp 足够大的场景，少量截断对整体估计量的影响可忽略。

**策略 3: Drain Phase 合并步（Multi-step Advance）**

在 drain phase，CPU 端处理开销（compact + collect + distribute）占比增大。
可以在一次主循环迭代中执行多步 non-ray advance：

```c
if(pool.in_drain_phase) {
    /* 进入 drain 后，每步之后连续推进 non-ray 状态，
     * 减少主循环的迭代次数 */
    size_t multi_step_budget = 4; /* 每次最多推进 4 步 non-ray */
    for(k = 0; k < pool->active_compact; k++) {
        uint32_t idx = pool->active_indices[k];
        struct path_state* p = &pool->slots[idx];
        size_t s;
        for(s = 0; s < multi_step_budget && p->active && !p->needs_ray; s++) {
            int advanced = 0;
            advance_one_step_no_ray(p, scn, &advanced);
            if(!advanced) break;
            p->steps_taken++;
        }
    }
}
```

#### 3.9.4 Drain Phase 诊断指标

为准确评估 drain phase 的影响，添加以下诊断日志：

```c
static void
log_drain_phase_report(struct wavefront_pool* pool)
{
    double drain_ray_pct = (pool->diag_drain_rays * 100.0)
                         / (pool->total_rays_traced + 1);
    log_info(dev,
        "persistent wavefront summary:\n"
        "  total_steps=%lu  total_rays=%lu\n"
        "  refill_phase: rays=%lu (%.1f%%)\n"
        "  drain_phase:  rays=%lu (%.1f%%), steps=%lu\n"
        "  batch_size: min=%lu, max=%lu\n"
        "  paths: completed=%lu, failed=%lu, max_depth=%lu\n",
        (unsigned long)pool->total_steps,
        (unsigned long)pool->total_rays_traced,
        (unsigned long)pool->diag_refill_rays,
        100.0 - drain_ray_pct,
        (unsigned long)pool->diag_drain_rays,
        drain_ray_pct,
        (unsigned long)pool->drain_step_count,
        (unsigned long)pool->diag_min_batch,
        (unsigned long)pool->diag_max_batch,
        (unsigned long)pool->paths_completed,
        (unsigned long)pool->paths_failed,
        (unsigned long)pool->max_path_depth);
}
```

---

## 四、RNG 策略分析

### 4.1 现有 RNG 架构

```
sdis_solve_camera:
  create_per_thread_rng(dev, rng_state, rng_type, &rng_proxy, &per_thread_rng)
      │
      ├─ rng_proxy: 全局 RNG 代理（管理序列分配）
      └─ per_thread_rng[nthreads]: 每 OMP 线程一个独立 RNG
             │
             └─ 在 solve_tile_wavefront 中:
                    所有 path 共享 per_thread_rng[ithread]
                    RNG 调用顺序 = Morton(tile) → Morton(pixel) → SPP → path steps
```

**Phase B-2 的 RNG 行为**: 所有 path 共享一个 RNG，但因为 wavefront 的 lockstep
执行，RNG 调用顺序与原版不同（原版: 完整跑完 path0 再跑 path1；wavefront: 所有
path 交替步进）。结果: **蒙特卡洛期望值不变，但逐像素结果不再 bit-exact**。

### 4.2 Phase B-3 的 RNG 方案

**实际方案: 共享 solve_camera 的 per_thread_rng**

`solve_camera()` 在调用 persistent wavefront 之前已通过 `create_per_thread_rng()`
从 `rng_proxy` 为每个 bucket 创建了 RNG。此时所有 bucket 的 CAS 锁已从 0→1，
**不可能再从同一个 proxy 调用 `ssp_rng_proxy_create_rng()`**（会返回 `RES_BAD_ARG`）。

因此 persistent wavefront 直接接收 `per_thread_rng[]` 数组，round-robin 共享：

```c
/* solve_camera_persistent_wavefront 接收已创建的 per_thread_rng */
res_T solve_camera_persistent_wavefront(
  struct sdis_scene* scn,
  struct ssp_rng**   per_thread_rng,   /* 已创建的 nthreads 个 RNG */
  const size_t       nthreads,
  ...)
{
  /* round-robin 共享到所有 pool slots, 通过 ref_get 增加引用计数 */
  for(i = 0; i < pool.pool_size; i++) {
    pool.slot_rngs[i] = per_thread_rng[i % nthreads];
    SSP(rng_ref_get(pool.slot_rngs[i]));
  }
}
```

引用计数语义：
- 每个 RNG 初始 refcount 由 `create_per_thread_rng` 持有
- 每个 slot 的 `rng_ref_get` 增加 1
- `pool_destroy` 中每个 slot 做 `rng_ref_put` 归还
- RNG 本体仍归 `solve_camera` 的 `per_thread_rng` 持有

**影响**: M1 单线程下所有 slot 实际共享同一个 RNG（nthreads=1 时），
逐像素结果与 Phase B-2 和原版均不同（RNG 流分配不同），
但蒙特卡洛估计量的统计性质（期望值、方差）完全一致。

**验证方式**: 统计对比（均值 ±1σ 一致性），不再要求 bit-exact。

### 4.3 替代方案: Per-task RNG（可选，更高重现性）

```c
/* 每个 task 的 RNG seed 由 (pixel_x, pixel_y, spp_idx) 确定性派生 */
uint64_t seed = hash64(pixel_x, pixel_y, spp_idx, global_seed);
ssp_rng_seed(rng, seed);
```

优势: 任意执行顺序下结果完全确定。  
劣势: 需要修改 RNG 初始化接口，增加 seed 哈希计算。

---

## 五、与现有代码的接口关系

### 5.1 可完全复用的代码（不需修改）

| 组件 | 文件 | 说明 |
|------|------|------|
| `path_state` 结构体 | `sdis_solve_wavefront.h` | 完全复用 |
| `step_init()` | `sdis_solve_wavefront.c` L430 | 完全复用 |
| `step_radiative_trace()` | `sdis_solve_wavefront.c` L441 | 完全复用 |
| `step_boundary()` | `sdis_solve_wavefront.c` L595 | 完全复用 |
| `step_conductive()` | `sdis_solve_wavefront.c` L669 | 完全复用 |
| `step_conductive_ds_process()` | `sdis_solve_wavefront.c` L746 | 完全复用 |
| `step_convective()` | `sdis_solve_wavefront.c` L988 | 完全复用 |
| `step_coupled_radiative_begin()` | `sdis_solve_wavefront.c` L1022 | 完全复用 |
| `advance_one_step_no_ray()` | `sdis_solve_wavefront.c` L1054 | 完全复用 |
| `advance_one_step_with_ray()` | `sdis_solve_wavefront.c` L1100 | 完全复用 |
| `collect_ray_requests()` | `sdis_solve_wavefront.c` L1138 | 需微调索引基准 |
| `setup_*_rays()` 系列 | `sdis_solve_wavefront.c` L276-380 | 完全复用 |
| batch trace API | `s3d_scene_view_batch_trace.cpp` | 完全复用 |
| CUDA kernel | `cus3d_trace.cu` | 完全复用 |

### 5.2 需要修改的代码

| 组件 | 文件 | 修改内容 |
|------|------|---------|
| `sdis_solve_camera()` | `sdis_solve_camera.c` L600-690 | 新增 `use_persistent_wavefront` 分支，绕过 OMP 循环 |
| `collect_ray_requests()` | `sdis_solve_wavefront.c` | 改为接受 pool 而非 wf context；或新建 pool 版本 |
| `distribute_and_advance()` | `sdis_solve_wavefront.c` | 同上 |

### 5.3 需要新增的代码

| 组件 | 文件 | 内容 |
|------|------|------|
| `wavefront_pool` 结构体 | `sdis_solve_persistent_wavefront.h` (新) | pool 定义,包含 task queue |
| `solve_camera_persistent_wavefront()` | `sdis_solve_persistent_wavefront.c` (新) | 主入口函数 |
| `generate_task_queue()` | 同上 | 按 Morton 序生成全部 pixel×SPP 任务 |
| `wavefront_pool_create/destroy()` | 同上 | pool 生命周期管理 |
| `refill_pool()` | 同上 | 从 task queue 填充空 slot |
| `harvest_completed_paths()` | 同上 | 完成的 path 写入 estimator_buffer |
| `init_single_path()` | 同上 | 初始化单个 path（从 init_all_paths 提取） |

> **M1 实施状态 (2026-02-11)**:
> - ✅ `sdis_solve_persistent_wavefront.h/c` 新建并编译通过
> - ✅ RNG 策略修正: `rng_proxy` → `per_thread_rng[]` + `nthreads` (round-robin + ref_get)
> - ✅ 进度条集成: `progress[]` + `pcent_progress` + `progress_label` 传入
> - ✅ `sdis_solve_camera.c` 调用点更新
> - 🔲 运行时验证待完成

---

## 六、分阶段实施路线

### 阶段 M1: 基础 Persistent Pool（无 refill） ✅ 已实现

**目标**: 验证 pool 架构可以替代 per-tile wavefront 产生相同的统计结果。

**做法**: pool_size = 全部 pixel×SPP（不做 refill），一次性加载所有 path，
运行到完成。等价于一个"巨型 tile"。

**已完成的修改**:
- 新增 `sdis_solve_persistent_wavefront.h/c`
- `sdis_solve_camera.c`: 增加 persistent wavefront 分支（默认启用）
- RNG 策略: 接收 `per_thread_rng[]` + `nthreads`，round-robin 共享（非从 proxy 新建）
- 进度条: 接收 `progress[]` + `pcent_progress` + `progress_label`，基于
  `(total_tasks - active_count) / total_tasks × 100` 百分比输出

**实现细节**:
- 函数签名: `solve_camera_persistent_wavefront(scn, per_thread_rng, nthreads,
  enc_id, cam, time_range, image_def, spp, register_paths, pix_sz,
  picard_order, diff_algo, buf, progress, pcent_progress, progress_label)`
- Step 函数通过 `#include "sdis_solve_wavefront.c"` + guard macro 复用
- 结果直写 `estimator_buffer`，跳过 tile 中间层

**验证**: 由用户手工验证统计温度均值 ±1σ 与 B-2 wavefront 一致。

**代码量**: ~500 行

### 阶段 M2: Path Refill + 恒定宽度

**目标**: 实现 refill 机制，保证 wavefront 宽度 = pool_size（恒定）。

**新增**:
- `refill_pool()` 函数
- `harvest_completed_paths()` 改为每步调用（当前在循环结束后一次性调用）

**验证**: 对比 M1 的统计结果（应完全一致，仅 RNG 序列不同）。

**预计代码量**: ~200 行

### 阶段 M2.5: Stream Compaction + 类型分桶

**目标**: 解决 pool 内路径类型异构性和稀疏遍历问题（§3.7, §3.8）。

**新增**:
- `compact_active_paths()` — 每步重建紧凑索引
- `collect_ray_requests_compact()` — 基于紧凑索引的射线收集
- `distribute_and_advance_compact()` — 基于分桶的 step 分发
- `wavefront_pool` 扩展字段（active_indices, bucket_*, done_indices）

**验证**:
- 统计结果与 M2 一致（相同 RNG，相同遍历顺序 → bit-exact）
- 性能 profile: compact 开销 < 总步时间的 5%
- drain phase 遍历效率提升验证（对比 active_compact vs pool_size）

**预计代码量**: ~150 行

### 阶段 M3: 直写 Estimator Buffer + 性能调优

**目标**: 消除 tile 中间层，直接写入 estimator_buffer。调优 pool_size。

**新增/修改**:
- `harvest_completed_paths()` 改为直写 estimator
- 添加自适应 pool_size 选择（基于 GPU SM 数和图像大小）
- 添加 wavefront 深度/宽度诊断日志（§3.9.4）

**验证**: 端到端性能基准测试，对比 Phase B-2。

### 阶段 M3.5: Drain Phase 优化

**目标**: 减少 drain phase 对整体运行时间的支配性影响（§3.9）。

**新增**:
- `drain_fallback_threshold` 配置和 GPU→CPU 自适应回退（策略 1）
- drain phase 进入/退出日志和诊断指标
- `log_drain_phase_report()` — drain vs refill 的射线量/时间对比报告
- 可选: drain phase multi-step advance（策略 3）
- 可选: path truncation 机制（策略 2，需精度评估）

**验证**:
- drain phase 诊断报告对比（with/without fallback）
- batch_size < threshold 时 CPU vs GPU 耗时对比
- 截断路径的温度偏差统计（如启用策略 2）

**预计代码量**: ~100 行

### 阶段 M4: MPI 兼容（仅在需要时）

**目标**: 保持与 MPI gather_tiles 流程的兼容性。

**做法**: persistent pool 按 tile 粒度分配给不同 MPI rank。每个 rank 运行自己的
persistent pool（包含该 rank 负责的所有 tile）。task_queue 只包含本 rank 的 tile。

---

## 七、修改范围精确划定

### 文件修改清单

```
新建文件:
  stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h   (~200 行)
  stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c   (~800 行)

修改文件:
  stardis-solver/0.16.2/src/sdis_solve_camera.c
    - L618-636: persistent wavefront 分支（默认启用）
    - 调用: solve_camera_persistent_wavefront(scn, per_thread_rng, nthreads,
            enc_id, cam, time_range, image_def, spp, register_paths,
            pix_sz, picard_order, diff_algo, buf,
            progress, pcent_progress, PROGRESS_MSG)
    - 循环后 print_progress_completion
    - 增加 #include "sdis_solve_persistent_wavefront.h"

  stardis-solver/0.16.2/src/sdis_solve_wavefront.h
    - 无修改（所有 step 函数签名不变）

  stardis-solver/0.16.2/src/sdis_solve_wavefront.c
    - 将 step_* / advance_* / setup_*_rays 等函数的 static 改为 extern
    - 或在 persistent_wavefront.c 中 #include "sdis_solve_wavefront.c"
      （与现有 _Xd.h 头文件包含模式一致）

  stardis-solver/0.16.2/src/CMakeLists.txt  (或等效构建配置)
    - 添加新源文件到编译列表

不修改的文件:
  custar-3d/* — batch trace API 完全复用,无修改
  sdis_solve_wavefront.h — path_state 结构无改动
  sdis_tile.h — TILE_SIZE 不变
  sdis_realisation.c — 不使用（wavefront 替代了 ray_realisation_3d）
```

### 函数级影响分析

```
solve_camera_persistent_wavefront() [新增]
  ├─ generate_task_queue()           [新增]
  ├─ wavefront_pool_create()         [新增]
  ├─ init_single_path()              [新增，从 init_all_paths 提取]
  ├─ compact_active_paths()          [新增, §3.7]
  ├─ collect_ray_requests_compact()  [新增, 基于紧凑索引]
  ├─ s3d_scene_view_trace_rays_batch_ctx()  [复用 custar-3d]
  ├─ distribute_and_advance_compact()[新增, 分桶调度 §3.8]
  ├─ cascade_non_ray_steps_compact() [新增, 基于 active_indices]
  ├─ harvest_completed_paths()       [新增]
  ├─ refill_pool()                   [新增]
  ├─ update_diagnostics()            [新增, §3.9.4]
  ├─ log_drain_phase_report()        [新增, §3.9.4]
  ├─ finalize_estimator_buffer()     [复用 sdis_solve_camera.c]
  └─ wavefront_pool_destroy()        [新增]
```

### 风险项

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| pool_size 过大导致 CPU 端遍历变慢 | 低 | 中 | stream compaction 消除遍历开销（§3.7） |
| per-slot RNG 消耗过多内存 | 低 | 低 | 每个 RNG ~48 bytes × 32768 ≈ 1.5 MB |
| `estimator_buffer_grab` 并发写入 | 无 | 无 | 单线程模式，无并发 |
| MPI 兼容性 | 中 | 中 | M4 阶段处理，可先不兼容 MPI |
| batch_ctx 容量不够 max_rays | 低 | 低 | 创建时指定 pool_size×2 |
| step 函数 static→extern 破坏封装 | 低 | 低 | 用 `#include .c` 或 internal linkage 头 |
| drain phase 主导运行时间（>80%） | **高** | **高** | CPU fallback + 分桶 + 可选截断（§3.9） |
| compaction 开销 > 收益（小场景） | 低 | 低 | 小场景可跳过 compaction（pool_size < 1024 时禁用） |
| 路径截断引入温度偏差 | 中 | 中 | 默认不启用，仅作可选配置；需统计验证 |

---

## 八、验证计划

### 8.1 正确性验证

| 测试 | 方法 | 通过标准 |
|------|------|---------|
| unit-cube 场景 | persistent vs per-tile wavefront (B-2) | 温度均值偏差 < 0.1% (3σ) |
| 纯辐射场景 | 无 conductive/convective | 温度均值 ±0.05% |
| 混合路径场景 | 辐射+传导+对流 | 温度均值 ±0.2% |
| 大图像 | 256×256 spp=64 | 完成无 crash |
| 内存检查 | Valgrind/ASAN | 无泄漏，无越界 |

### 8.2 性能验证

| 指标 | 测量方法 | 预期 |
|------|---------|------|
| refill phase batch size | wavefront 日志: rays/step | 均值 > 20000 |
| drain phase batch size | drain 诊断日志 | 从 pool_size 衰减至 0 |
| drain phase 射线占比 | `diag_drain_rays / total_rays` | 场景依赖，导热场景 >50% |
| GPU 时间占比（refill） | Nsight Systems profile | batch_time > 80% 总时间 |
| GPU 时间占比（drain） | Nsight Systems profile | 逐步下降；fallback 后 ~0% |
| compact 开销占比 | profile compact_active_paths | < 5% 每步总时间 |
| 分桶 step 吞吐 | radiative vs conductive 分开计时 | 分桶后 cache miss 减少 >30% |
| 总体加速比 | wall clock vs B-2 | 10-50× (refill 阶段 >100×) |
| GPU SM 占用率（refill） | Nsight Compute | > 60% |
| CPU fallback break-even | drain phase 对比 GPU vs CPU trace | batch < 256 时 CPU 更快 |

---

## 九、总结

Phase B-2 的 per-tile wavefront 解决了"逐射线 kernel 启动"的问题，但受限于
TILE_SIZE=4 和路径深度不均，GPU 利用率始终在 1-3%。

实际运行日志揭示了三个层次的并行效率问题：

1. **Tile 级初始宽度不足**（已识别）: 每 tile 仅 512 paths，GPU 占用率 <3%
2. **场景空间异构性**（新发现）: 空气 tile 300µs vs 固体 tile 62s，差距 200,000×
3. **路径类型异构性与长尾效应**（新发现）: 同一 pool 内 radiative 路径 1-2 步
   完成，conductive 路径需 38,000 步，最慢 2% 路径消耗 >60% 运行时间

Phase B-3 通过**四个层次的改造**解决这些问题：

1. **Persistent Pool**（§3.1-3.4）: 提升 wavefront 粒度到全图像级别，
   wavefront 宽度从 512 提升到 32768+，消除 tile 间负载不均

2. **Path Refill**（§3.5-3.6）: 完成的 path 立即被新任务替换，在 refill
   阶段保持 wavefront 宽度恒定、GPU 满载

3. **Stream Compaction + 类型分桶**（§3.7-3.8）: 每步重建活跃路径紧凑索引，
   避免遍历全部 pool_size 个 slot；按路径类型分桶消除 CPU 分支预测失败和
   未来 GPU warp divergence

4. **Drain Phase 优化**（§3.9）: 任务队列耗尽后的尾部处理策略，包括
   GPU→CPU 自适应回退（batch < 256 射线时改用 CPU trace）、可选的路径截断
   和多步合并，避免用整个 GPU 服务最后几条路径

代码改造量可控（~800-1000 行新代码），核心 step 函数全部复用 Phase B-2 实现，
风险集中在 RNG 管理、drain phase 策略选择和结果写入路径的调整上。

> **实施优先级建议**: M1 → M2 → M2.5 → M3 → M3.5 → M4
>
> 其中 M2.5（stream compaction）和 M3.5（drain phase）是本次分析新增的阶段，
> 特别针对路径类型异构性和长尾效应问题。M2.5 的代码量小（~150 行）但对 drain
> phase 的遍历性能影响巨大，建议紧跟 M2 之后实施。

---

*文档版本: v1.2 | 基于 stardis-cus3d 代码分析 + 运行日志长尾效应分析 | M1 实现同步 2026-02-11*
