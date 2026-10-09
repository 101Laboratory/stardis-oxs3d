# Persistent Wavefront Pool (PWF) - 数据流与执行流程

---

## 整体流程架构

```
┌─────────────────────────────────────────────────────────────────┐
│ sdis_solve_camera_to_wavefront() / probe_to_wavefront()         │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ▼
        ┌────────────────────────────┐
        │ persistent_wavefront_main() │ ← 初始化 pool、RNG、batch ctx
        └────────┬───────────────────┘
                 │
    ┌────── generate_tasks() ◄──────┐  (mode_ctx → ops vtable)
    │     (Morton/Linear/Interleave) │
    │                                 │
    ▼─────► task_queue[task_count]   │
             pool_create()            │
             pool_create_views()      │
             batch_ctx_create()       │
    └────────────────────────────────┘
                 │
                 ▼
        ┌───────────────────────────┐
        │ fill_pool()               │  ← 初始化路径
        └────────┬──────────────────┘
                 │
    ┌────────────┴──────────────┐
    │ for each task:            │
    │   path_id = task_idx      │
    │   slot = task_idx % size  │
    │   init_path(→ ops)        │
    │   state = PATH_INIT       │
    │   rng_seed(cbrng)         │
    └───────────────┬───────────┘
                    │
                    ▼
         ┌──────────────────────┐
         │ pool_run()           │  ← 主执行引擎
         └──────────┬───────────┘
                    │
         ┌──────────┴──────────────┐
         │ if dual-buffer mode:    │
         │   pool_run_dual()       │  ← GPU↔CPU重叠
         │   (可动态→单缓冲)       │
         └──────────┬──────────────┘
                    │
         ┌──────────▼──────────────┐
         │ pool_run_single()       │  ← 简单循环
         └──────────┬──────────────┘
                    │
                    ▼
        ┌───────────────────────────┐
        │ pool_destroy()            │  ← 清理
        └───────────────────────────┘
```

---

## Phase 详细执行流

### I. 初始化阶段

#### 1. Pool 创建
```c
pool_create(pool, pool_size, total_tasks)
├─ 分配 path_state[pool_size]
├─ 分配 path_hot[pool_size]
├─ 分配 pool_view[num_views]  (1或2个)
├─ pool_create_views()
│  ├─ for each view:
│  │  ├─ pool_view_create()
│  │  │  ├─ 分配 active_indices[capacity]
│  │  │  ├─ 分配 need_ray_indices[capacity]
│  │  │  ├─ 分配 done_indices[capacity]
│  │  │  ├─ 分配 bucket_radiative[capacity]
│  │  │  ├─ 分配 bucket_conductive[capacity]
│  │  │  ├─ 分配 bucket_other[capacity]
│  │  │  ├─ 分配 ray_requests[max_rays = capacity×6]
│  │  │  ├─ 分配 ray_to_slot[max_rays]
│  │  │  ├─ 分配 ray_slot_sub[max_rays]
│  │  │  ├─ 分配 ray_hits[max_rays]
│  │  │  ├─ 分配 filter_per_ray[max_rays]
│  │  │  ├─ s3d_batch_trace_context_create()
│  │  │  │  └─ pinned buffer: ray_pinned[], filter_pinned[]
│  │  │  ├─ 分配 enc_locate_requests[capacity]
│  │  │  ├─ 分配 enc_locate_results[capacity]
│  │  │  ├─ s3d_batch_enc_context_create()
│  │  │  ├─ 分配 cp_requests[capacity]
│  │  │  ├─ 分配 cp_hits[capacity]
│  │  │  └─ s3d_batch_cp_context_create()
└─ pool->ops = ops_vtable  (camera/probe/probe_batch)
```

#### 2. 任务队列生成
```c
generate_tasks(pool, mode_ctx)  ← ops_vtable 虚函数
├─ CAMERA mode:
│  └─ Morton顺序: for spp, for y, for x → pixel_task[W×H×spp]
├─ PROBE mode:
│  └─ 线性: nrealisations 个任务，都指向同一位置
└─ PROBE_BATCH mode:
   └─ 交错: for realis, for probe → nprobes×nrealisations 任务
```

#### 3. 路径初始化
```c
fill_pool(pool)
├─ for slot = 0 to active_count:
│  ├─ task = task_queue[task_next++]
│  ├─ p = pool->slots[slot]
│  ├─ p->path_id = path_id_counter++
│  ├─ wf_rng_seed(p->rng_state, ...)
│  ├─ init_path(p, hot, rng, task, scn, enc_id, ...)  ← ops vtable
│  │  ├─ memset(p, 0, sizeof(*p))  [O8: cache warming]
│  │  ├─ 设置 p->rwalk (位置、时间、enclosure)
│  │  ├─ 设置 p->ctx (温度限制、Picard)
│  │  ├─ 设置 hot->phase = PATH_INIT / PATH_COUPLED_* (根据模式)
│  │  ├─ p->ray_req.batch_idx = (uint32_t)-1  [O11_SAFETY: sentinel]
│  │  └─ p->rng 指向 wf_rng thin wrapper
│  ├─ pool->ops->accumulate_result()  [初始化accum为0]
│  └─ p->steps_taken = 0
└─ pool->task_next 指向下一个待填充任务
```

---

### II. 主循环：Merged Pass (三相结构)

```
每次 merged_pass(pool, pv, scn) 迭代分为三个阶段：

┌─────────────────────────────────────────────────────────┐
│ MERGED PASS - 一次完整迭代                               │
└─────────────────────────────────────────────────────────┘

Phase A: 分发上一轮所有 GPU 结果 (RT + enc + cp)
│
├─ A-enc: pool_distribute_enc_locate_results()  ← batch 级，循环外
│  └─ 遍历 enc_locate_results[0..count-1]，写回 enc_arr，设 PATH_ENC_LOCATE_RESULT
│
├─ A-cp: pool_distribute_cp_results()            ← batch 级，循环外
│  └─ 遍历 cp_hits[0..count-1]，写回 cached_hit，设 RESULT phase
│
├─ 重置所有计数器 (ray_count, enc_locate_count, cp_count, done_count, ...)
│
└─ A-rt: merged_pass_distribute_step() (per-path) ← 循环内
   ├─ for 每个活跃路径:
   │  ├─ if !needs_ray: 跳过
   │  ├─ if batch_idx == sentinel: 跳过
   │  ├─ h0 = pv->ray_hits[p->batch_idx]
   │  ├─ 根据 ray_bucket 分发到对应 step 函数
   │  └─ p->steps_taken++
   └─ [OMP parallel OR serial fallback]

Phase B: 级联处理非光线步骤 (cascade)
├─ #pragma omp parallel
│  └─ #pragma omp for schedule(static)
│     └─ for slot in active_indices:
│        └─ cascade_advance_single_path(p, scn, pool, ...)
│           ├─ do {
│           │  ├─ if hot->needs_ray: break
│           │  ├─ if hot->phase == PATH_DONE/ERROR: break
│           │  ├─ 执行非光线 phase-specific 步骤
│           │  ├─ p->steps_taken++
│           │  └─ 记录 phase时间 (if SDIS_CASCADE_PROFILE)
│           │ } while(advanced && 迭代 < MAX)
│           │
│           └─ 返回成功/失败
│
└─ [OMP barrier - 隐式同步]

Phase C: 收集新光线请求 (collect)
├─ compact_active_paths(pool, pv)  ← 压缩活跃索引
├─ pool_collect_ray_requests_bucketed()  ← 关键收集步骤 (OMP 3-pass + Plan E)
│  ├─ Pass 1: 每线程统计各bucket光线数
│  ├─ Prefix Sum: 计算全局bucket_offsets + 每线程写封基
│  └─ Pass 2: Scatter——Plan E直接写入 pv->ray_pinned[]/filter_pinned[]
│     (CUDA pinned内存，无额外中间拷贝)
│
└─ refill_pool()  ← 用新任务填充已完成的slot
   ├─ if task_next < task_count:
   │  └─ for completed_slot in done_indices:
   │     ├─ init_path()  [新任务]
   │     └─ task_next++
   └─ 重建 active_indices
```

---

### III. GPU交互流程

#### GPU提交 (O12: gpu_submit_all)
```c
gpu_submit_all(pool, pv, sv)
// 在单次调用中按 H2D→Kernel→D2H 顺序发出所有异步调用
├─ 如果 pv->ray_count > 0:
│  ├─ cudaMemcpyAsync(H2D)       // 异步上传 ray + filter
│  ├─ optixLaunch(kernel)         // 异步kernel
│  ├─ cudaStreamWaitEvent(gate)   // 门控: 等kernel完成
│  ├─ cudaMemcpyAsync(D2H)       // 异步下载 ray_hits
│  ├─ cudaEventRecord(d2h_done)  // 记录完成事件
│  └─ pv->gpu_pending = 1
│
├─ 如果 pv->enc_locate_count > 0:
│  ├─ (同样 H2D→Kernel→D2H 模式)
│  └─ pv->enc_gpu_pending = 1
│
└─ 如果 pv->cp_count > 0:
   ├─ (同样 H2D→Kernel→D2H 模式)
   └─ pv->cp_gpu_pending = 1
```

#### GPU等待下载 (O12: gpu_wait_d2h_all)
```c
gpu_wait_d2h_all(pool, pv, sv)
├─ cudaEventSynchronize(rt_d2h_done)   // 等待trace下载
├─ cudaEventSynchronize(enc_d2h_done)  // 等待enc下载
├─ cudaEventSynchronize(cp_d2h_done)   // 等待cp下载
├─ pool_distribute_enc_locate_results() // 分发enc结果
├─ pool_distribute_cp_results()         // 分发cp结果
└─ 已准备好进行 merged_pass
```

#### 旧接口 (drain/merge场景)
```c
gpu_wait_download_all(pool, pv, sv)     // 完整同步 + 下载
gpu_launch_all(pool, pv, sv)            // 仅 H2D + kernel (单池用)
```

**Pinned Buffer优化** (Plan E):
```
传统方式:                          Plan E (当前):
CPU → malloc → GPU copy           CPU → pinned buffer → GPU copy
      (额外分配)                       (无额外分配)

从 batch_ctx 借用指针:
  s3d_batch_trace_context_get_pinned_buffers()
  → pv->ray_pinned, pv->filter_pinned 直接指向batch_ctx的内存
```

---

### IV. 双缓冲重叠 (O11+O12+O13)

```
双View流水线 (O13 async submit):
┌─────────────────────────────────────────────→ 时间
│
│  Submit Thread (后台):                Main Thread:
│  ┌─────────────────────┐              ┌──────────────────────┐
│  │ WaitForMultipleObjects             │ startup:             │
│  │  → gpu_submit_all(vi)              │   merged_pass(A,B)   │
│  │  → SetEvent(done[vi])              │   gpu_submit_all(A,B)│
│  └─────────────────────┘              └──────────────────────┘
│                                              ↓
│  主循环:
│  ┌─── Half-A ────────────────────────────────────────────────┐
│  │ ensure_done(view=0)  // A 上一轮submit早已完成            │
│  │ wait_d2h(A)          // 等待 D2H 完成                     │
│  │ merged_pass(A)       // CPU 工作                          │
│  │ compact(A) + refill(A)                                    │
│  │ signal_submit(A)     // 异步触发，立即返回                │
│  └───────────────────────────────────────────────────────────┘
│  ┌─── Half-B ────────────────────────────────────────────────┐
│  │ ensure_done(view=1)  // B 上一轮submit早已完成            │
│  │ wait_d2h(B)                                               │
│  │ merged_pass(B)       // submit(A) 在此期间并行执行        │
│  │ compact(B) + refill(B)                                    │
│  │ signal_submit(B)                                          │
│  └───────────────────────────────────────────────────────────┘
│  if should_merge() → break
│
动态合并:
if should_merge(pool):
  ├─ submit_thread_wait()  [等待所有pending submit完成]
  ├─ gpu_wait_download_all(A,B)  [完整同步]
  ├─ merge_to_single_pool()  [展开pool到单缓冲]
  │    ├─ 对V1所有 active && needs_ray 路径: batch_idx ← (uint32_t)-1
  │    └─ 避免跨缓冲区读取 (V1 batch_idx → V0 ray_hits)
  └─ break → pool_run_single()
```

**重叠关键 (三层)**:
1. **O11 双缓冲**: GPU(A)执行时CPU处理merged_pass(B)
2. **O12 Stream Auto-Ordering**: H2D→K→D2H在stream上自动排序，D2H与下一view的H2D可重叠
3. **O13 Async Submit**: optixLaunch主机开销(~13ms/call)在后台线程执行，不阻塞主线程
4. **O16 NT Store Pinned Writes**: `merged_pass_flush_tl_rays()`中使用SSE2 NT指令将TL光线缓冲嵌入CUDA pinned内存，绕过CPU cache减少cascade阶段cache污染

**O13基线性能**: coverage=129.8%, submit=29.8s完全隐藏, wall=119.5s  
**O16消除缓存污染**: merged_pass收集层不再将pinned写入加入CPU cache，ascade热路径缓存常驼提升

---

### V. 主运行循环选择

#### 场景A: 双缓冲执行 (pool_run_dual, O13)
```
条件: pool->num_active_views == 2

submit_thread_init()
startup: merged_pass(A,B) → gpu_submit_all(A,B)

loop {
  ┌─ Half-A:
  │  ensure_done(view=0) ← A上一轮submit(已完成)
  │  wait_d2h(A) ← 等待D2H完成
  │  merged_pass(A) ← CPU工作
  │  compact(A) + refill(A)
  │  signal_submit(A) ← 异步，立即返回
  │
  ├─ Half-B:
  │  ensure_done(view=1) ← B上一轮submit(已完成)
  │  wait_d2h(B)
  │  merged_pass(B) ← submit(A)并行执行
  │  compact(B) + refill(B)
  │  signal_submit(B)
  │
  └─ if should_merge() → wait_submit + wait_all → merge → break
}

└─ 清理: ensure_done + wait_download + submit_thread_destroy()
```

#### 场景B: 单缓冲执行 (pool_run_single)
```
条件: pool->num_active_views == 1 (或从双缓冲合并)

loop {
  ├─ merged_pass()
  ├─ compact_active_paths()
  ├─ refill_pool()
  ├─ GPU_launch()
  └─ GPU_wait()
}

└─ 直到 active_count == 0 && task_next >= task_count
```

---

## 关键数据流约束

### O11_SAFETY: Write-Before-Read
```c
约束: 结果写入必须在phase变化之前完成

示例:
  pool->enc_arr[slot].locate.prim_id = result.prim_id;  // 写入
  pool->enc_arr[slot].locate.side = result.side;
  pool->hot_arr[slot].phase = PATH_ENC_LOCATE_RESULT;  // 最后改phase
```

### Sentinel值
```c
p->ray_req.batch_idx = (uint32_t)-1  [初始化标记]

含义: 此路径从未提交到GPU
用途: merged_pass Phase A 跳过distribute (无GPU结果可读)
```

### Bucket Indices 对齐
```c
pv->bucket_radiative[k]    ← radiative路径的slot_id
pv->bucket_conductive[k]   ← conductive路径的slot_id
...
pv->bucket_offsets[3]      ← conductive光线的起始位置

O2优化: 避免在distribute/collect中重新排序
```

---

## 路径生命周期流

```
初始化:          fill_pool() → PATH_INIT
                       ↓
第一次merged:   cascade() → 可能多个非光线步骤
                       ↓
光线启动:        merged_pass Phase C → pool_collect_ray_requests()
                       ↓
GPU提交:         gpu_submit_all() → GPU执行 (异步 H2D→K→D2H)
GPU等待:         gpu_wait_d2h_all() → D2H 完成
Submit:          submit_thread_signal() → 后台线程执行 gpu_submit_all
                       ↓
结果分发:        merged_pass Phase A → step_radiative_trace() / etc.
                       ↓
       ┌─────────────────┬──────────────────┐
       ▼                  ▼                   ▼
   PATH_DONE       PATH_ERROR        新阶段·继续循环
   (积累) →                                   ↑
   accumulate                                 │
   ↓                                    ← ─ ─ ┘
最终输出

refill: 完成的slot可被refill_pool()重新用于新任务
```

---

## 性能监控点

```
每个merged_pass记录:
├─ distribution + cascade + collection耗时
├─ ray_count (按bucket类型分解)
├─ active_count / 的active/完成路径
├─ GPU kernel时间 (trace_kernel_time_ms)
└─ GPU↔CPU数据传输时间 (trace_batch_time_ms)

汇总统计:
├─ total_steps (合并循环次数)
├─ total_rays_traced (总光线数)
├─ avg_wavefront_width = diag_total_active / total_steps
├─ per-phase wall-clock时间
└─ GPU vs CPU时间比例
```

---

## 环境变量控制

```bash
STARDIS_PIXEL_TRACE=output.csv        # 启用像素跟踪 (path per row)
STARDIS_PATH_TRACE=5000               # 启用路径跟踪 (max 5000 lines)
STARDIS_DISTRIBUTE_OMP=1               # 0禁用OMP并行分发
```
