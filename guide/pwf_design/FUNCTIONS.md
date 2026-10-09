# Persistent Wavefront Pool (PWF) - 函数参考表

---

## 调试和诊断函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `pixel_trace_file()` | 行 73-85 | 初始化/返回像素跟踪文件指针（STARDIS_PIXEL_TRACE env） |
| `pt_enabled()` | 行 101-116 | Debug版本检查路径跟踪是否启用 |
| `pt_log()` | 行 118-157 | Debug版本每步记录路径状态 |

---

## 内存管理函数

### Pool View 管理

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_view_init()` | O16更新 | 创建单个view及其所有缓冲区；初始化Plan E pinned布局 |
| `pool_view_destroy()` | — | 释放pool view的所有资源 |

**缓冲区布局** (pool_view 结构):
```c
void*        base              // view的基地址
size_t       view_size         // view包含的slot范围
size_t       capacity          // 最大同时活跃路径数

// Index arrays (按 capacity 分配)
uint32_t*    active_indices[]           // 活跃路径的slot索引
uint32_t*    need_ray_indices[]         // 需要光线的路径
uint32_t*    done_indices[]             // 完成的路径
uint32_t*    bucket_radiative[]         // 辐射路径桶
uint32_t*    bucket_conductive[]        // 热传导路径桶
uint32_t*    bucket_other[]             // 其他路径桶

// Ray buffers (按 max_rays = capacity×6 分配)
struct s3d_ray_request*    ray_requests[]
uint32_t*                  ray_to_slot[]
uint32_t*                  ray_slot_sub[]
struct s3d_hit*            ray_hits[]
struct s3d_filter_per_ray* filter_per_ray[]

// GPU Pinned buffers
struct s3d_ray_pinned*     ray_pinned      // borrowed from batch_ctx
struct s3d_filter_per_ray* filter_pinned   // borrowed from batch_ctx

// Enclosure locate
struct s3d_enc_locate_request*  enc_locate_requests[]
struct s3d_enc_locate_result*   enc_locate_results[]
uint32_t*                       enc_locate_to_slot[]

// Closest point
struct s3d_cp_request*     cp_requests[]
struct s3d_hit*            cp_hits[]
uint32_t*                  cp_to_slot[]
```

---

## 动态合并/分割函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `should_merge()` | 行 326-342 | 检查是否需要从双→单缓冲合并 (条件: 任一view活跃 < 12.5%) |
| `should_split()` | 行 343-358 | 检查是否需要从单→双缓冲分割 (条件: 活跃 > 75% && 任务待分发) |
| `merge_to_single_pool()` | 行 359-390 | 将两个views合并成一个全range view<br/>对V1所有 `active && needs_ray` 路径设 `batch_idx=(uint32_t)-1` 避免跨缓冲区读取 |
| `split_to_dual_pool()` | 行 391-442 | 将单view分割成两个半range views |

**状态转换**:
```
Initial: num_active_views = 1 or 2

┌─── dual buffer ─┐
│                 ▼
└──→ should_merge() ──→ merge_to_single_pool() → single buffer
                                                      ↓
                                            (到达drain阶段)
                                                      │
                                          should_split()?
                                                (通常否)
                                                      │
                                              final cleanup
```

---

## 模式操作接口 (Wavefront Ops)

### 接口定义
```c
struct wavefront_ops {
  res_T (*generate_tasks)(struct wavefront_pool* pool, const void* mode_ctx);
  res_T (*init_path)(struct path_state* p, struct path_hot* hot,
                     struct ssp_rng* rng, const struct pixel_task* task,
                     struct sdis_scene* scn, unsigned enc_id,
                     const void* mode_ctx, const double* time_range,
                     size_t picard_order, enum sdis_diffusion_algorithm diff_algo,
                     uint32_t path_id, uint64_t global_seed);
  void (*accumulate_result)(const struct path_state* p, void* result_ctx);
};
```

### Camera Mode (图像渲染)

| 函数 | 约束 | 功能 |
|------|------|------|
| `camera_generate_tasks()` | 行 872-878 | 按Morton顺序生成W×H×spp个像素任务 |
| `camera_init_path()` | 行 880-892 | 从像素任务初始化摄像机射线路径 |
| `camera_accumulate_result()` | 行 894-918 | 累积温度到estimator_buffer[px][py] |
| `wf_ops_camera` | 行 920-924 | 摄像机模式vtable实例 |

### Probe Mode (单点热传输)

| 函数 | 约束 | 功能 |
|------|------|------|
| `probe_generate_tasks()` | 行 930-963 | 生成nrealisations个线性任务 |
| `probe_init_path()` | 行 965-1090 | 从单个probe位置初始化路径 |
| `probe_accumulate_result()` | 行 1092-1107 | 累积温度到单个struct accum |
| `wf_ops_probe` | 行 1109-1113 | 探测模式vtable实例 |

### Probe Batch Mode (多点热传输)

| 函数 | 约束 | 功能 |
|------|------|------|
| `probe_batch_generate_tasks()` | 行 1119-1161 | nprobes×nrealisations个交错任务 |
| `probe_batch_init_path()` | 行 1163-1268 | 从批量数组读取per-probe参数 |
| `probe_batch_accumulate_result()` | 行 1270-1283 | 按probe_idx路由累积结果 |
| `wf_ops_probe_batch` | 行 1285-1289 | 探测批处理vtable实例 |

---

## 光线请求处理函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_collect_ray_requests_bucketed()` | Plan E + OMP3-pass | OMP 3-pass无原子分桶；Pass2 Scatter直接写入CUDA pinned内存 |

**此函数有两种执行路径**:
- OMP路径 (need_ray_count ≥ 128): Pass1计数 → Prefix Sum → Pass2 Scatter
- Serial fallback: 小规模或 `STARDIS_COLLECT_OMP=0`

### 光线分桶类型

```c
enum ray_bucket_type {
  RAY_BUCKET_RADIATIVE,        // PATH_RAD_* → 单光线
  RAY_BUCKET_STEP_PAIR,        // PATH_CND_DS_* / PATH_BND_SF_REINJECT → 1-2光线
  RAY_BUCKET_SHADOW,           // 影子光线
  RAY_BUCKET_ENCLOSURE,        // PATH_ENC_QUERY_EMIT → 6光线
  RAY_BUCKET_STARTUP,          // PATH_INIT → 1光线
  RAY_BUCKET_OTHER,            // 混合/特殊
  RAY_BUCKET_COUNT
};
```

---

## 批处理函数

### Enclosure Locate

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_distribute_enc_locate_results()` | 行 2237-2258 | 分发GPU包围体定位结果，转→PATH_ENC_LOCATE_RESULT |

### Closest Point (WoS)

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_distribute_cp_results()` | 行 2301-2330 | 分发CP结果，映射→_CLOSEST_RESULT或_DIFFUSION_CHECK_RESULT |

**注**: enc/cp 请求收集已整合入 `merged_pass` 的 Phase C

---

## 级联处理函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `cascade_advance_single_path()` | 行 2332-2540 | 在单条路径上执行非光线步骤的主循环 |

**thread-local累积器**:
```c
size_t local_iterations           // do-while循环计数
size_t local_advances             // 成功推进计数
size_t local_paths_failed         // 失败路径计数
size_t local_enc_degenerate_null  // enclosure退化null计数
#ifdef SDIS_CASCADE_PROFILE
  size_t  local_phase_count[]     // per-phase调用计数
  double  local_phase_time[]      // per-phase耗时 (秒)
#endif
```

**关键逻辑**:
```c
do {
  if(hot->needs_ray) break;                         // 需要光线 → 退出
  if(hot->phase == PATH_DONE/ERROR) break;          // 路径完成 → 退出
  
  // M8: SFN stack处理
  if(hot->phase == PATH_DONE && sfn_stack_depth > 0)
    resume from sfn stack
  
  // 执行phase-specific步骤
  advance = path_cascade_step(p, hot, scn, ...)
  
  if(!advanced) break;                              // 无进展 → 退出
} while(iterations < MAX_CASCADE_ITERATIONS)
```

---

## Merged Pass 函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `merged_pass()` | —— | 四相主处理：分发+级联+收集+收割 |
| `merged_pass_distribute_step()` | `INLINE` | Phase A: 对单條路径分发GPU结果并调用step函数 |
| `merged_pass_collect_ray()` | `INLINE` | Phase C: 从单条路径收集光线到thread-local缓冲 |
| `merged_pass_flush_tl_rays()` | O16 NT Store | Phase C flush: 经由SSE2 NT指令将TL缓冲写入pinned内存（绕过CPU cache） |
| `merged_pass_fixup_batch_idx()` | —— | Phase C 后: 设置batch_idx指针供下轮分发使用 |

**merged_pass 四相**:
```
Phase A: distribute 所有 GPU 结果
  ├─ A-enc: pool_distribute_enc_locate_results() (batch 级, 循环外)
  ├─ A-cp:  pool_distribute_cp_results()         (batch 级, 循环外)
  ├─ 重置计数器
  └─ A-rt:  merged_pass_distribute_step()        (per-path, 循环内)
Phase B: cascade_advance_single_path() (OMP parallel)
Phase C: 收集新 ray/enc/cp 请求
Phase D: harvest 完成路径
```
Y
---

## GPU同步和启动函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `gpu_submit_all()` | O12 | H2D→Kernel→D2H 连续异步调用（rays + enc + cp） |
| `gpu_wait_d2h_all()` | O12 | 等待 D2H 完成事件 + 分发 enc/cp 结果 |
| `gpu_wait_download_all()` | 旧接口 | 完整同步（drain/merge 场景用） |
| `gpu_launch_all()` | 旧接口 | 仅 H2D + kernel（无 D2H，单池用） |

**O12 GPU调用顺序 (gpu_submit_all)**:
```
gpu_submit_all():
  1. cudaMemcpyAsync(H2D)          // 异步上传
  2. optixLaunch(kernel)            // 异步kernel (~13ms host overhead)
  3. cudaStreamWaitEvent(d2h_gate)  // 门控等待kernel完成
  4. cudaMemcpyAsync(D2H)           // 异步下载
  5. cudaEventRecord(d2h_done)      // 记录完成事件
  (对 rays, enc_locate, cp 各执行一次)

gpu_wait_d2h_all():
  cudaEventSynchronize(d2h_done)   // 等待下载完成
  pool_distribute_enc_locate_results()  // 分发 enc 结果
  pool_distribute_cp_results()          // 分发 cp 结果
```

---

## O13 Async Submit Thread

| 函数 | 功能 |
|------|------|
| `submit_thread_init()` | 创建 per-view 事件 + 启动后台线程 |
| `submit_thread_destroy()` | 信号 shutdown + 等待线程退出 + 清理句柄 |
| `submit_thread_signal(pool, pv, sv, vi)` | 写入 pv[vi]/sv[vi]，触发 go[vi] |
| `submit_thread_wait_view(pool, vi)` | 等待 done[vi]，返回 result[vi] |
| `submit_thread_func()` | 线程入口：WaitForMultipleObjects 多路复用 |

**Per-view 通道设计**:
```
struct wavefront_pool {
  submit_evt_go[2]       // auto-reset，per-view 触发
  submit_evt_done[2]     // manual-reset，per-view 完成
  submit_pv[2]           // 独立参数槽，无共享可变状态
  submit_sv[2]
  submit_result[2]       // per-view 结果
  submit_elapsed_s[2]    // per-view 计时
};

// 线程多路复用:
WaitForMultipleObjects(2, go[], FALSE, INFINITE)
  → r = WAIT_OBJECT_0 + vi
  → gpu_submit_all(pv[vi], sv[vi])
  → SetEvent(done[vi])
```

---

## 主运行循环函数

### 双缓冲执行

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_run_dual()` | O11+O12+O13 | 双缓冲 + stream auto-order + async submit |

**流程 (O13)**:
```
初始化:
  submit_thread_init()
  merged_pass(A) → merged_pass(B)
  gpu_submit_all(A) + gpu_submit_all(B)  // 同步启动首批

主循环:
  Half-A:
    ensure_done(view=0)               // 等待 A 上一轮 submit
    wait_d2h(A)                       // 等待 D2H 完成
    merged_pass(A) → compact → refill
    signal_submit(A)                  // 异步触发，立即返回
  
  Half-B:
    ensure_done(view=1)               // 等待 B 上一轮 submit
    wait_d2h(B)
    merged_pass(B) → compact → refill
    signal_submit(B)
  
  检查动态合并 (should_merge)

清理:
  ensure_done() + gpu_wait_download_all()
  submit_thread_destroy()
```

**动态合并**:
```
if(should_merge(pool)):
  submit_thread_wait()  // 等待所有 pending submit
  gpu_wait_download_all(A) + gpu_wait_download_all(B)
  merge_to_single_pool()
  break → pool_run_single()
```

### 单缓冲执行

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_run_single()` | 行 4892-4925 | 简单循环：merged_pass → compact → refill → launch → wait |

**流程**:
```
while(active_count > 0 || task_next < task_count):
  merged_pass()
    ├─ Phase A: distribute (enc + cp + rt)
    ├─ Phase B: cascade
    ├─ Phase C: collect (ray + enc + cp)
    └─ Phase D: harvest
  
  compact_active_paths()
  refill_pool()
  GPU_launch_all()
  GPU_wait_download_all()
```

### 统一分发器

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_run()` | 行 4927-4970 | 根据num_active_views dispatch至dual或single |

```c
if(num_active_views == 2)
  pool_run_dual()  // 可能动态merge
then
  pool_run_single()  // 完成剩余工作
```

---

## 诊断和报告函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `pool_update_diag_stats()` | 行 2984-3031 | 收集每轮merged_pass的诗句统计 |
| `log_drain_phase_report()` | 行 3033-3200 | 输出详细最终报告 (timing, ray stats, etc.) |

**诊断统计**:
```c
// 光线计数
size_t rays_radiative         // PATH_RAD_*
size_t rays_conductive_ds     // PATH_CND_DS_*
size_t rays_conductive_ds_retry
size_t rays_shadow
size_t rays_enclosure
size_t rays_startup
size_t rays_other

// 路径计数
size_t paths_completed
size_t paths_failed
size_t paths_truncated
size_t max_path_depth

// 时间分解 (秒) — O12/O13 five mutually-exclusive phases
double time_submit_s           // O13: async thread submit wall-clock
double time_pipeline_wait_s    // wait_d2h (trace + enc + cp)
double time_cascade_s          // merged_pass (cascade + distribute)
double time_harvest_s          // compact + refill
double time_housekeeping_s     // stats, logging, progress

// 平均波前宽度
double avg_wavefront_width = diag_total_active / total_steps
```

**级联per-phase 热点**:
```c
#ifdef SDIS_CASCADE_PROFILE
  size_t cascade_phase_count[PATH_PHASE_COUNT]   // per-phase调用
  double cascade_phase_time[PATH_PHASE_COUNT]    // per-phase耗时
  
  // log_drain_phase_report() 输出前10个热点
```

---

## 公开API函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `sdis_solve_camera_to_wavefront()` | 行 5450-5500 | Camera渲染入口 |
| `sdis_solve_probe_to_wavefront()` | 行 5510-5541 | 单点Probe入口 |
| `sdis_solve_probe_batch_to_wavefront()` | 行 6200-6230 | 多点Probe批量入口 |

**通用参数**:
```c
struct sdis_scene* scn              // 场景
struct ssp_rng* base_rng            // 全局RNG种子
const double time_range[2]          // [t_min, t_max]
size_t picard_order                 // Picard迭代阶数
enum sdis_diffusion_algorithm diff_algo
```

---

## 工具函数

| 函数 | 约束 | 功能 |
|------|------|------|
| `compact_active_paths()` | 行 2544-2594 | 压缩active_indices，去除done/error路径 |
| `fill_pool()` | 行 2596-2662 | 用新任务填充可用slots |
| `refill_pool()` | 行 2664-2738 | 从task_queue重充已完成的slots |
| `pool_update_active_count()` | 行 2740-2751 | 重新计算pool->active_count |
| `mark_path_failed()` | 行 2753-2765 | 标记路径为失败并移除 |
| `pool_create()` | 行 527-707 | 创建整个pool (slots + views + batch contexts) |
| `pool_destroy()` | 行 709-738 | 销毁整个pool及所有资源 |
| `determine_pool_size()` | 行 740-766 | 根据任务数和GPU SM计数自适应pool_size |
| `init_single_path()` | 行 768-870 | 单条路径初始化（内部共用） |

---

## 宏和常量

| 定义 | 值/说明 |
|------|--------|
| `PREFETCH_T0(addr)` | Software prefetch (MSVC/GCC/Clang兼容) |
| `RAY_BUCKET_COUNT` | 6 (光线分桶种类数) |
| `PATH_PHASE_COUNT` | 路径阶段总数 (定义在sdis_wf_types.h) |
| `MAX_CASCADE_ITERATIONS` | 单路径级联最大迭代数 |

---

## 函数调用依赖图

```
Public API
├─ sdis_solve_camera_to_wavefront()
│  └─ persistent_wavefront_main() ← main entry
│     ├─ pool_create()
│     ├─ generate_tasks() [ops vtable]
│     ├─ fill_pool()
│     ├─ pool_run()
│     │  ├─ pool_run_dual()  (if num_views==2)
│     │  │  ├─ submit_thread_init()           ← O13
│     │  │  ├─ compact_active_paths()
│     │  │  ├─ merged_pass()
│     │  │  │  ├─ Phase A: distribute
│     │  │  │  ├─ Phase B: cascade
│     │  │  │  └─ Phase C: collect + refill
│     │  │  ├─ gpu_submit_all()               ← O12
│     │  │  ├─ submit_thread_signal()         ← O13 (async)
│     │  │  ├─ submit_thread_wait_view()      ← O13
│     │  │  ├─ gpu_wait_d2h_all()             ← O12
│     │  │  ├─ should_merge() → merge_to_single_pool()
│     │  │  └─ submit_thread_destroy()        ← O13
│     │  │
│     │  └─ pool_run_single()  (completion or merged)
│     │     └─ [similar but simpler loop]
│     │
│     ├─ pool_update_diag_stats()
│     ├─ log_drain_phase_report()
│     └─ pool_destroy()
│
├─ sdis_solve_probe_to_wavefront()
└─ sdis_solve_probe_batch_to_wavefront()
```
