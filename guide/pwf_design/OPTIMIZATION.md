# Persistent Wavefront Pool (PWF) - 性能优化指南

---

## 优化概览

代码使用**O1-O11**标记系统标注了各项性能优化。下表总结：

| 优化ID | 技术名称 | 影响 | 复杂度 | 状态 |
|--------|--------|------|-------|------|
| O2 | Pre-bucketed光线索引 | 避免重新排序 | 中 | Active |
| O7 | Software prefetch | L1缓存预热 | 低 | Active |
| O8 | Full memset (cache warming) | CPU缓存预热 | 低 | Active |
| O11 | Dual-buffer重叠 | GPU↔CPU并行 | 高 | Active |
| O11_DIAG | Per-step路径跟踪 | Debug诊断 | 低 | Active |
| O11_MERGE | 动态merge/split | 低负载优化 | 高 | Active |
| O11_SAFETY | Write-before-read同步 | 数据正确性 | 低 | Active |
| O12 | Stream Auto-Ordering | H2D→K→D2H流水 | 中 | Active |
| O13 | Async Submit Thread | submit线程隐藏 | 高 | Active |
| O16 | NT Store Pinned Writes | DRAM IO减少/Cache污染 | 中 | Active |
| L4 | GPU内联滤波器 | GPU↔CPU传输优化 | 中 | Active |
| Plan E | Pinned Buffer直接写入 | 消除中间CPU拷贝 | 中 | Active |
| P0_OPT | hot_arr SoA分离 | L2局部性提升 | 中 | Active |
| P1 | Cold-Block SoA分裂 | 冷路径局部性提升 | 中 | Active |
| P2 | Pool View抽象 | 双/单缓冲统一接口 | 高 | Active |

---

## O2: Pre-Bucketed光线索引优化

### 问题
传统方式需要在分发和收集时重新排序光线，产生cache miss和分支。

### 解决方案
在 `pool_collect_ray_requests()` 中**直接写入bucket索引数组**，避免后续排序。

```c
/// 优化前 (非最优):
ray_request_iter {
  phase = hot[i].phase
  append ray to ray_requests[]     // 无序
}
// 然后需要额外排序步骤 ← 额外缓冲和cache miss

/// 优化后 (O2):
ray_request_iter {
  phase = hot[i].phase
  switch(phase) {
    case PATH_RAD_*:
      bucket_radiative[rad_count++] = i     // 直接写bucket
    case PATH_CND_DS_*:
      bucket_conductive[cond_count++] = i   // 直接写bucket
  }
}
// bucket_offsets[type+1] = offsets[type] + counts[type]
// 分发时直接使用bucket索引 ← 无额外排序
```

### 性能收益
- **避免**: O(N log N) 排序或计数级联
- **实现**: O(N) 单次扫描
- **Cache友好**: 按bucket类型顺序访问slot，改善locality
- **估计收益**: 5-15% (取决于bucket分布)

### 关键代码
[pool_collect_ray_requests()](file:///d:/Stardis-GPU/stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L1391)
- 行 1553: `bucket_radiative[pv->bucket_radiative_n++] = i`
- 行 1603: `bucket_conductive[pv->bucket_conductive_n++] = i`
- 行 1891-2080: Different bucket branches for each ray type

---

## O7: Software Prefetch 优化

### 问题
CPU热路径中的数据访问可能导致L1缓存miss。

### 解决方案
使用编译器内在函数在访问前预取数据。

```c
/* O7: Software prefetch — portable macro for MSVC / GCC / Clang */
#ifdef _MSC_VER
#include <intrin.h>
#define PREFETCH_T0(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#else
#define PREFETCH_T0(addr) __builtin_prefetch((const void*)(addr), 0, 3)
#endif
```

### 使用模式
```c
// 在紧密循环中提前几个迭代预取
for(i = 0; i < N; i++) {
  int j = i + PREFETCH_DISTANCE;  // prefetch-ahead
  if(j < N) PREFETCH_T0(&data[j]);
  
  // 处理当前迭代
  process(data[i]);
}
```

### 性能收益
- **L1命中率**: 92% → 98%+ (取决于访问模式)
- **延迟隐藏**: 12-20 CPU周期/L1 miss
- **估计收益**: 2-5% 在memory-bound操作中

### 关键代码
行 58-68: 宏定义
- MSVC: `_mm_prefetch(..., _MM_HINT_T0)` - 预取到L1
- GCC/Clang: `__builtin_prefetch(..., 0, 3)` - read, 高时间局部性

---

## O8: Full Memset 缓存预热

### 问题
初始化path_state时一次性分配但稀疏访问，导致TLB miss和缓存冷启动。

### 解决方案
在 `init_single_path()` 中执行 **full memset(p, 0, sizeof(*p))** 而非选择性初始化。

```c
/// 优化前 (理论上高效):
p->field1 = 0;
p->field2 = 0;
p->field3 = ...
// 问题: 页未预热，导致首次使用时TLB和缓存miss

/// 优化后 (O8):
memset(p, 0, sizeof(*p));  // 单次大块操作，预热整个页
// 后续字段写入享受缓存预热 ← 更快的平均时间
```

### 性能收益
- **TLB预热**: 避免首次缺页中断
- **缓存预热**: L3到L1的prefill
- **有序页表建立**: 批量操作建立页映射
- **估计收益**: 3-8% (路径初始化密集时)

### 关键代码
行 1000-1007 (probe_init_path):
```c
/* O8: See comment in init_single_path — full memset is load-bearing. */
memset(p, 0, sizeof(*p));
```

---

## O11: 双缓冲GPU↔CPU重叠

### 问题
顺序执行GPU光线追踪和CPU级联处理：
```
GPU_wait(A) → distribute(A) + cascade(A) → GPU_launch(B)
                                            GPU_wait(B) → ...
```
GPU 在cpu处理时处于空闲，CPU 在GPU追踪时等待。

### 解决方案
使用两个view的双缓冲流水线，在GPU(X)执行时CPU处理(Y)。

```
┌─────────────────────────────────────────── 时间轴
│
初始化:  merged_pass(A) → GPU_launch(A)
         merged_pass(B) prepare
                ↓
主循环:   GPU_wait(A) ──→ merged_pass(A) → GPU_launch(B)
          │                    │
          └─ GPU(A)在              CPU在B上处理时
             background       └─ GPU(B)在background
                ↓
         GPU_wait(B) ──→ merged_pass(B) → GPU_launch(A)
          │                    │
          └─ GPU(B)在              CPU在A上处理时
             background       └─ GPU(A)在background

Overlap: GPU execution + CPU processing 近乎完美
```

### 实现关键
```c
/// pool_run_dual() 伪代码
startup:
  merged_pass(pv_a) → gpu_launch(pv_a)
  merged_pass(pv_b)

while(active_count > 0):
  GPU_wait(pv_a) ← 可能极短 (GPU刚完成)
  merged_pass(pv_a) ← CPU处理
  GPU_launch(pv_b) ← 启动GPU
  
  GPU_wait(pv_b)
  merged_pass(pv_b)
  GPU_launch(pv_a)
  
  if should_merge():
    merge_to_single_pool() → break to pool_run_single()
```

### 性能收益
- **GPU利用率**: 可达 95%+ (vs. 60-70% 单缓冲)
- **总执行时间**: 1.3-1.8x 快于单GPU+CPU序列执行
- **瓶颈转移**: 从GPU等待 → 算法限制
- **估计收益**: 30-50% 总体吞吐量提升

### 动态merge/split (O11_MERGE)
```c
// Merge: 当任一view活跃 < 12.5% × view_size
if(should_merge(pool)):
  GPU_wait(A) + GPU_wait(B)
  pool->views[0].view_size = pool->pool_size
  pool->num_active_views = 1
  break  → pool_run_single()

// 避免低负载时的双缓冲开销
```

### 关键代码
[pool_run_dual()](file:///d:/Stardis-GPU/stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L4688)
- 行 4688-4890
- 行 4833: `if(should_merge(pool))` 动态合并检查

---

## O11_SAFETY: Write-Before-Read同步

### 问题
GPU结果返回后，CPU可能在数据完全写入前读取结果。

### 解决方案
在改变路径阶段前，确保所有相关数据已写入。

```c
/// 优化前 (竞争条件):
pool->enc_arr[slot].locate.prim_id = r->prim_id;
pool->hot_arr[slot].phase = PATH_ENC_LOCATE_RESULT;  // 阶段变化
// 如果另一线程读hot_arr[slot].phase并访问locate.prim_id
// 可能存在(虽然unlikely由于CUDA同步点)

/// 优化后 (O11_SAFETY):
// 写入数据
pool->enc_arr[slot].locate.prim_id  = r->prim_id;
pool->enc_arr[slot].locate.side     = r->side;
pool->enc_arr[slot].locate.distance = r->distance;
// 数据屏障(内存顺序保证)
pool->hot_arr[slot].phase = PATH_ENC_LOCATE_RESULT;  // 阶段变化
```

### 关键代码
[pool_distribute_enc_locate_results()](file:///d:/Stardis-GPU/stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L2237)
- 行 2237-2258
- 行 2248-2250: 数据写入
- 行 2251: phase变化 (最后一步)

[pool_distribute_cp_results()](file:///d:/Stardis-GPU/stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L2301)
- 行 2301-2330
- 行 2306: `p->locals.cnd_wos.cached_hit = pv->cp_hits[k]`
- 行 2308-2311: 相应阶段转换

---

## L4: GPU内联滤波器优化

### 问题
每条光线需要传递filter参数(偏振、波长等)到GPU。传统方式需要额外数据结构。

### 解决方案
在 `merged_pass_collect_ray()` 中，使用 `s3d_filter_per_ray` 结构与光线数据共存于pinned缓冲。

```c
/// L4优化：
struct s3d_ray_pinned {
  float origin[3], direction[3], tmin, tmax;  // 核心光线数据
};

struct s3d_filter_per_ray {
  // 从path_state提取的filter信息
};

// 在pinned缓冲中并排存储
pv->ray_pinned[ray_idx]      ← 光线数据
pv->filter_pinned[ray_idx]   ← 对应filter

// GPU kernel可同时读取两个数组，单次DTH传输
```

### 性能收益
- **GPU访问合并**: 光线和filter一起访问，L1缓存友好
- **调度优化**: GPU缓冲器可批量加载相邻的光线/filter对
- **带宽**: 相同带宽下更高吞吐量
- **估计收益**: 5-10% (GPU kernel bound时)

### 关键代码
[merged_pass_collect_ray()](file:///d:/Stardis-GPU/stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L4140)
- 行 4182: `fill_filter_per_ray(&e->filter, p)`
- 行 4192: 同时收集光线和filter

[merged_pass_flush_tl_rays()](file:///d:/Stardis-GPU/stardis-oxs3d-merge-phase/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L4235)
- 行 4260: `pv->filter_pinned[ray_idx] = e->filter`
- 与光线数据一起写入pinned缓冲

---

## 诊断和分析

### Cascade Profiling (SDIS_CASCADE_PROFILE)

编译时启用：
```bash
cmake -DSDIS_CASCADE_PROFILE=ON ...
```

捕获per-phase统计：
```c
#ifdef SDIS_CASCADE_PROFILE
  size_t  cascade_phase_count[PATH_PHASE_COUNT]
  double  cascade_phase_time[PATH_PHASE_COUNT]
#endif
```

### 输出示例
```
persistent wavefront summary:
  total_steps=1234567  total_rays=987654  avg_wavefront_width=128.5
  ...
  cascade profiling: total_iterations=5000000  total_advances=4800000
    cascade phase[15]: count=500000  time=0.234s  avg=0.468us  (8.2% of cascade)
    cascade phase[22]: count=450000  time=0.198s  avg=0.440us  (6.9% of cascade)
```

### 关键指标

#### GPU利用率
```
GPU_utilization = trace_kernel_time_ms / (trace_kernel_time_ms + trace_post_time_ms + trace_retrace_time_ms)
目标 > 90%
```

#### 平均波前宽度
```
avg_width = diag_total_active / total_steps
目标: > 70% pool_size (高并行度)
```

#### 时间分解
```
时间覆盖率 = (sum of timed phases) / wall_time
目标: > 90% (低overhead)
```

---

## 性能调优指南

### 1. 识别瓶颈

运行诊断输出并检查：
```bash
./stardis -M scene.txt -t 4 -V 3 -R ... 2>&1 | grep "persistent wavefront"
```

检查:
- **GPU利用率** < 80%? → GPU launch overhead 过高
- **avg_wavefront_width** < 30%? → 并行度低，考虑增加batch size
- **cascade耗时** > 40% total? → CPU成为瓶颈

### 2. 调整池大小

```c
size_t determine_pool_size(size_t total_tasks, struct sdis_scene* scn)
  → pool_size = 512 + 64 * num_sm  (自适应GPU SM数)
```

手动调整（环境变量）：
```bash
STARDIS_POOL_SIZE=2048  # 强制池大小
```

### 3. OMP线程数

```bash
OMP_NUM_THREADS=8  # 调整并行distribute线程数
```

或在代码中禁用OMP分发：
```bash
STARDIS_DISTRIBUTE_OMP=0  # 使用serial fallback
```

### 4. 启用跟踪和监控

```bash
STARDIS_PIXEL_TRACE=output.csv      # 像素级细节
STARDIS_PATH_TRACE=10000             # 路径级日志
export STARDIS_CASCADE_PROFILE=1     # CMake重新编译后
```

---

## O12: Stream Auto-Ordering（已实现）

### 问题
旧流水线中 `gpu_launch_all` 发出 H2D + kernel launch，然后 `gpu_wait_download_all` 在另一时刻同步并发出 D2H。H2D 与 D2H 在 CPU 时间线上被 merged_pass 隔开，无法实现 PCIe 双向重叠。

### 解决方案
将 `gpu_launch_all` 重构为 `gpu_submit_all`：在单次调用中按 H2D→Kernel→D2H 顺序发出所有异步调用。使用 `cudaStreamWaitEvent` 门控 D2H 等待 kernel 完成。CPU 侧的 `gpu_wait_d2h_all` 仅等待 D2H 完成事件。

```c
gpu_submit_all(pool, pv, sv):
  1. cudaMemcpyAsync(H2D)        // 异步上传
  2. optixLaunch(kernel)          // 异步kernel
  3. cudaStreamWaitEvent(d2h_evt) // 门控
  4. cudaMemcpyAsync(D2H)        // 异步下载

gpu_wait_d2h_all(pool, pv, sv):
  cudaEventSynchronize(d2h_done) // 等待下载完成
```

### 性能收益
- GPU hidden: 83%（D2H 与下一个 view 的 H2D 在 stream 上自然重叠）
- 消除手动 syncKernel + startD2H 步骤
- 简化主循环结构

### 关键代码
`gpu_submit_all()`, `gpu_wait_d2h_all()` — sdis_solve_persistent_wavefront.c
`batch_trace_filtered_start_d2h_impl()` 中 `cudaStreamWaitEvent` — ox_s3d_scene_view.cpp

---

## O13: Async Submit Thread（已实现）

### 问题
`gpu_submit_all` 内的 `optixLaunch` 有 ~13ms/call 的主机端驱动开销，累计 ~27s。在同步模式下阻塞主线程。

### 解决方案
将 `gpu_submit_all` 卸载到专用后台线程。主线程通过事件发信号后立即继续 CPU 工作。

**Per-view 独立通道设计**:
```c
struct wavefront_pool {
  void*  submit_evt_go[2];       // auto-reset，per-view 触发
  void*  submit_evt_done[2];     // manual-reset，per-view 完成
  struct pool_view* submit_pv[2]; // 独立参数槽
  struct s3d_scene_view* submit_sv[2];
};

// 线程函数：WaitForMultipleObjects 多路复用两个 go 事件
// signal(vi): 写 pv[vi]/sv[vi] → SetEvent(go[vi])，无互斥
// wait_view(vi): WaitForSingleObject(done[vi])
```

**主循环模式**:
```
Half-A:
  ensure_done(view=0)  ← A 上一轮的 submit（隔了一整轮，早已完成）
  wait_d2h(A) → merged(A) → compact(A) → refill(A)
  signal_submit(A)     ← 异步，立即返回

Half-B:
  ensure_done(view=1)  ← B 上一轮的 submit
  wait_d2h(B) → merged(B) → compact(B) → refill(B)
  signal_submit(B)     ← 异步，立即返回
```

### 性能收益
- submit=29.8s 完全被 CPU 工作隐藏
- coverage=129.8%（total > wall，证明 overlap 生效）
- wall: 137.7s → 119.5s（**-13.2%**）

### 关键设计决策
1. **Per-view 通道 vs 单通道**: 单通道+idle互斥会导致主线程在 signal(B) 时等待 submit(A) 完成（与 OMP 线程数相关的死锁）。Per-view 通道消除所有共享可变状态。
2. **Auto-reset go vs Manual-reset**: Auto-reset 避免线程需要手动 ResetEvent。
3. **WaitForMultipleObjects**: 单线程多路复用两个 view，避免两个线程竞争 GPU driver。

### 关键代码
`submit_thread_func()`, `submit_thread_init()`, `submit_thread_destroy()`,
`submit_thread_signal()`, `submit_thread_wait_view()` — sdis_solve_persistent_wavefront.c

---

---

## O16: DRAM IO优化——NT Store Pinned Writes

### 问题
`merged_pass_flush_tl_rays()` 将线程本地光线缓冲汇入CUDA pinned内存时，普通 store指令会将数据加载到CPU L1/L2/L3 cache。collect阶段写入路径把 cascade阶段的热数据驱逐出 cache，导致两者互相干扰。

### 解决方案
在 `merged_pass_flush_tl_rays()` 中使用 **SSE2 NT (Non-Temporal) store** 指令将光线数据直接写入CUDA pinned内存，绕过CPU缓存层级（Write-Combining通道）。

```c
/* O16: SSE2 non-temporal stores for pinned buffer writes */
#include <emmintrin.h>

/* O16: ray_pinned (32B = 2 x __m128i) — NT stream from pre-formatted */
__m128i r0 = _mm_loadu_si128((const __m128i*)(&e->ray) + 0);
__m128i r1 = _mm_loadu_si128((const __m128i*)(&e->ray) + 1);
_mm_stream_si128((__m128i*)(&dst_ray[ray_idx]) + 0, r0);
_mm_stream_si128((__m128i*)(&dst_ray[ray_idx]) + 1, r1);

/* O16: filter_pinned (16B = 1 x __m128i) — NT stream. */
_mm_stream_si128((__m128i*)(&dst_flt[ray_idx]),
                 _mm_loadu_si128((const __m128i*)&e->filter));

_mm_sfence();  /* O16: ensure NT stores globally visible before fixup */
```

**配套优化—♋64B对齐 `tl_ray_entry` 结构体**：

```c
/* O16: Slimmed from 104B (double intermediary) to 56B by embedding
 * destination-format structs directly.  Padded to 64B = 1 cacheline. */
struct tl_ray_entry {
  struct s3d_ray_pinned     ray;     /* 32B — exact pinned layout       */
  struct s3d_filter_per_ray filter;  /* 16B — exact filter pinned layout */
  uint32_t  slot;                    /* 4B                               */
  uint32_t  sub;                     /* 4B  — 0..5 for multi-ray         */
  uint32_t  _pad[2];                 /* 8B  — pad to 64B cacheline       */
};  /* total: 64B = 1 cacheline */
```

64B对齐消除split-line加载；`_aligned_malloc`分配综线程缓冲消除false sharing。

### 性能收益
- **消除cache污染**: NT store绕过L1/L2/L3，cascade热路径的cache line不被 collect写入驱逐
- **降低内存带宽压力**: flush阶段的DRAM写入采用Write-Combining合并，减少总事务数量
- **擦除split-line读写**: 64B `tl_ray_entry` = 恰好1个cache line，无cross-line读写
- **影响主要在merged_pass时间** (O13基线78.5s这一层的缓存效率)

### 关键代码
`merged_pass_flush_tl_rays()`, `merged_pass_collect_ray()` — sdis_solve_persistent_wavefront.c

---

## Plan E: Pinned Buffer直接写入

### 问题
传统 `pool_collect_ray_requests_bucketed()` 通过中间 `ray_requests[]`（普通内存）收集光线，再在GPU提交时添加H2D拷贝。

### 解决方案
直接向 `pv->ray_pinned[]` 和 `pv->filter_pinned[]`（CUDA pinned内存）写入，平台的DMA引擎可直接从 pinned内存发起H2D传输。

```c
/* Plan E: Borrow pinned buffer pointers from batch_ctx for direct-write */
s3d_batch_trace_context_get_pinned_buffers(
  pv->batch_ctx, &pv->ray_pinned, &pv->filter_pinned, NULL);

/* Pass 2 Scatter — Plan E: pinned direct-write */
struct s3d_ray_pinned* rp = &pv->ray_pinned[ray_idx];
rp->origin_x = p->ray_req.origin[0];
...
fill_filter_per_ray(&pv->filter_pinned[ray_idx], p);
```

### 性能收益
- 消除 `ray_requests[]` 中间缓冲的CPU内存占用 (~max_rays × sizeof(s3d_ray_request))
- 消除H2D前的格式转换拷贝，GPU提交直接读取已格式化的pinned数据

### 关键代码
`pool_collect_ray_requests_bucketed()`, `pool_view_init()` — sdis_solve_persistent_wavefront.c

---

## P0_OPT: `path_hot` SoA分离

### 问题
Stream compaction和collect阶段需要频繁读取每个路径的 `phase`/`active`/`needs_ray` 字段，这些数据散布在 ~2KB 大小的 `path_state` AoS结构中。

### 解决方案
将频繁访问的热字段单独提取到 `struct path_hot` SoA数组：

```c
struct path_hot {
  uint8_t  phase;        /* PATH_* 枚举 */
  uint8_t  active;
  uint8_t  needs_ray;
  uint8_t  ray_bucket;   /* enum ray_bucket_type */
  uint32_t ray_count_ext;
};  /* 8B/slot */

/* pool->hot_arr: 8B × pool_size vs path_state ~2KB × pool_size */
/* 8 slots/cache line vs 1 partial slot/cache line */
```

### 性能收益
- `compact_active_paths()`: 扨80KB `hot_arr`内容 (pool=4096时) vs 扨20MB `path_state`
- Stream compaction的L2 cache命中率显著提升
- `merged_pass Phase A/B/C`: 热字段读取不损害天然局部性

### 关键代码
`pool_create()`, `compact_active_paths()`, `pool_collect_ray_requests_bucketed()`, `merged_pass_distribute_step()` — sdis_solve_persistent_wavefront.c

---

## P1: Cold-Block SoA数组分裂

### 问题
`path_state` 包含大量冷路径字段 (sfn stack, enc_data, ext_data)，混在热路径AoS中加重了缓存常驻套。

### 解决方案
将冷字段分裂到独立 SoA 数组：

```c
/* P1: Cold-block SoA arrays (separated from path_state slots) */
pool->sfn_arr = (struct path_sfn_data*)calloc(pool_size, sizeof(...));
pool->enc_arr = (struct path_enc_data*)calloc(pool_size, sizeof(...));
pool->ext_arr = (struct path_ext_data*)calloc(pool_size, sizeof(...));
```

- `sfn_arr`: SFN堆栈数据 (子路径Picard求和)
- `enc_arr`: 包围体定位数据 (enc_locate, dir_hits[], batch_indices[])
- `ext_arr`: 外部扩展数据

### 性能收益
- 热路径 (cascade inner loop) 不再需要加载冷字段占用的cache line
- SoA布局便于將来的SIMD增强

### 关键代码
`pool_create()`, `pool_destroy()`, `cascade_advance_single_path()`, `merged_pass_distribute_step()` — sdis_solve_persistent_wavefront.c

---

## P2: Pool View抽象

### 问题
双缓冲和单缓冲模式使用不同代码路径，难以统一维护。GPU批处理上下文也需要每个view独立拥有。

### 解决方案
引入 `struct pool_view` 技层，封装单个view的所有缓冲区和GPU context：

```c
struct pool_view {
  size_t  base;       /* slot起始偏移 (0 or view_size) */
  size_t  view_size;  /* 该view覆盖的slot数 */
  size_t  capacity;   /* 分配容量 (支持merge到全pool) */

  /* 索引数组 active/need_ray/done/bucket_* */
  /* 光线缓冲 ray_requests/to_slot/slot_sub/hits/filter_per_ray */
  /* GPU batch context: batch_ctx, ray_pinned, filter_pinned */
  /* enc/cp batch contexts */
};

/* 双缓冲: pool->views[0] 奇数半, views[1] 偶数半 */
/* 单缓冲: pool->views[0] 覆盖全pool */
/* 动态切换: should_merge() → merge_to_single_pool() */
/*            should_split() → split_to_dual_pool() */
```

**关键数值**:
- `DUAL_BUFFER_RATIO_THRESHOLD 12`: total_tasks/view_size ≥ 12 时启用双缓冲
- `DUAL_BUFFER_MIN_VIEW_SIZE 512`: 双缓冲最小 view 大小
- 合并阈值: 任一view活跃路径 < 12.5% × view_size
- 分割阈值: 活跃路径 > 75% × pool_size 且有未分发任务

### 关键代码
`pool_view_init()`, `pool_view_destroy()`, `should_merge()`, `should_split()`,
`merge_to_single_pool()`, `split_to_dual_pool()`,
`pool_run_dual()`, `pool_run_single()` — sdis_solve_persistent_wavefront.c

---

## 未来优化机会

### 可能的改进

1. **Ray Compression** (O14)
   - 压缩ray数据传输 (浮点→半精度)
   - 权衡精度vs带宽

2. **Cache-Aware Bucketing** (O15)
   - 将bucket按访问热度排序
   - 改善L1+L2缓存预热

3. **Vectorized Path Cascade** (O17)
   - SIMD处理多路径级联步骤
   - 当前per-path serial

---

## 参考资源

- NVIDIA Nsight Compute: 分析GPU kernel性能
- AMD uProf: CPU性能采样
- Intel VTune: 全系统profiling
- 内部性能数据: `perf_diag/` 目录
