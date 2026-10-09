# 01 — 单池 GPU 调度器设计

## 1. 现有调度器回顾

### 1.1 merge-phase CPU 调度器核心循环

当前 `sdis_solve_persistent_wavefront.c` (~7000 LOC) 实现了以下调度流程：

```
pool_run()
  ├── pool_run_dual()          // 双 view 流水线（O13 异步提交） 
  │    └── 循环:
  │         wait_d2h(View A) → merged_pass(View A) → compact → refill → signal_submit(A)
  │         wait_d2h(View B) → merged_pass(View B) → compact → refill → signal_submit(B)
  │                                     ↑ 异步提交线程在后台执行 gpu_submit_all()
  │
  └── pool_run_single()        // 单 view 循环（dual 池合并后或小任务）
       └── 循环:
            gpu_submit_all() → gpu_wait_d2h_all() → merged_pass() → compact → refill
```

### 1.2 merged_pass 的四阶段结构

```c
merged_pass(pool):
  #pragma omp parallel for
  for each slot_idx in active_indices[]:
    slot = &pool->slots[slot_idx]
    
    // Phase A: Distribute — 处理上一轮 GPU 结果
    if (slot->has_pending_result) {
      advance_one_step_with_ray(slot, hit_results[slot->ray_batch_idx])
      slot->has_pending_result = 0
    }
    
    // Phase B: Cascade — 纯计算步骤循环推进
    while (!slot->needs_ray && !slot->pending_gpu && !slot->done) {
      advance_one_step_no_ray(slot)  // 大 switch 分发到 step_*()
    }
    
    // Phase C: Collect — 收集新光线请求
    if (slot->needs_ray) {
      ray_requests[collect_idx] = slot->ray_request
    }
    
    // Phase D: Harvest — 完成路径收割 + 结果累积
    if (slot->done) {
      accumulate_result(slot, estimator)
      mark_for_refill(slot)
    }
```

### 1.3 关键数据流

```
path_state[N]         // 每路径 ~2KB (hot 8B + core ~500B + cold SoA)
active_indices[M]     // M ≤ N, 压缩后的活跃索引 (~256KB)
ray_requests[K]       // K ≤ M × max_rays_per_step, 5 桶 radix 排序
hit_results[K]        // 对应光追结果
enc_locate_batch[L]   // BVH enclosure 查询
cp_batch[L']          // closest-point 查询
task_queue[T]         // 待执行任务（probe: position+enc_id; camera: pixel+spp）
```

## 2. 单池 GPU 调度器设计

### 2.1 设计原则

1. **消除双 view 复杂度**: 单设备不需要 ping-pong，所有路径在同一个池中
2. **消除 PCIe 往返**: 光追结果常驻 device memory，直接由 solver kernel 读取
3. **分离 OptiX launch 和 solver advance**: 两个独立 kernel，通过 device memory 通信
4. **保留 bucketed dispatch**: 按 path_phase 分组以减少 warp divergence

### 2.2 调度循环（Host 侧）

```cpp
// =========== Host-Side Main Loop ===========
void gpu_solver_main_loop(
    DevicePathPool* d_pool,       // GPU 路径池
    DeviceSceneData* d_scene,     // GPU 场景数据
    DeviceEstimator* d_est,       // GPU 估算器
    OptixPipeline pipeline,       // OptiX 光追管线
    cudaStream_t stream)          // CUDA 流
{
    uint32_t* d_active_count;    // device-side 活跃路径计数
    uint32_t h_active_count;
    
    // 初始填充
    fill_pool_kernel<<<grid, block, 0, stream>>>(d_pool);
    
    while (true) {
        // ─── Step 1: Advance + Collect ───
        // 分发上一轮光追结果 + 级联推进 + 收集新光线请求
        solver_advance_kernel<<<grid, block, 0, stream>>>(
            d_pool, d_scene, d_est);
        
        // ─── Step 2: Stream Compaction ───
        // 把 needs_ray 的路径压缩到连续数组，供 OptiX 光追
        compact_ray_requests_kernel<<<grid, block, 0, stream>>>(
            d_pool);
        
        // ─── Step 3: Read active count (小量 D2H) ───
        cudaMemcpyAsync(&h_active_count, d_active_count, 
                        sizeof(uint32_t), cudaMemcpyDeviceToHost, stream);
        cudaStreamSynchronize(stream);
        
        if (h_active_count == 0) break;  // 所有路径完成
        
        // ─── Step 4: OptiX 光追 ───
        optixLaunch(pipeline, stream,
                    d_pool->launch_params_ptr,
                    sizeof(LaunchParams),
                    &sbt,
                    h_active_count,  // ray count
                    1, 1);           // 1D launch
        
        // ─── Step 5: Refill ───
        // 完成路径的槽位从任务队列取新任务
        refill_kernel<<<grid, block, 0, stream>>>(d_pool);
    }
    
    // 最终归约
    reduce_estimators_kernel<<<...>>>(d_est, d_output);
}
```

### 2.3 为什么不用 mega-kernel / persistent thread

| 方案 | 优点 | 缺点 |
|------|------|------|
| **Persistent thread** | 最小 launch 开销 | 寄存器压力极大，占用率低，调试困难 |
| **Mega-kernel** | 无中间同步 | 编译时间极长，step 分支导致寄存器溢出 |
| **多轮 kernel launch** | 寄存器独立、各 kernel 可调优、可调试 | launch 开销 ~5μs/次 |

选择**多轮 kernel launch**: 
- Launch 开销 ~5μs × 100 轮 = 0.5ms，对比总计算 >>100ms 可忽略
- 每个 kernel 寄存器需求独立优化
- OptiX launch 本身就是独立 kernel

### 2.4 solver_advance kernel 内部结构

```cuda
__global__ void solver_advance_kernel(DevicePathPool* pool, ...) {
    // 每个线程处理一条路径
    uint32_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= pool->active_count) return;
    
    uint32_t slot_idx = pool->active_indices[tid];
    DevicePathState* ps = &pool->states[slot_idx];
    
    // ── Phase A: Distribute ──
    // 如果有上一轮光追结果，处理之
    if (ps->has_pending_hit) {
        DeviceHitResult hit = pool->hit_results[ps->ray_batch_idx];
        device_advance_with_ray(ps, &hit, pool->scene, ...);
        ps->has_pending_hit = 0;
    }
    
    // ── Phase B: Cascade ──
    // 纯计算步骤循环（上限防止无限循环）
    int cascade_limit = 64;
    while (!ps->needs_ray && !ps->done && cascade_limit-- > 0) {
        device_advance_no_ray(ps, pool->scene, ...);
    }
    
    // ── Phase C: Collect ──
    // needs_ray 的路径把光线请求写入 ray_buffer
    if (ps->needs_ray) {
        uint32_t ray_idx = atomicAdd(&pool->ray_count, ps->ray_count_needed);
        for (int r = 0; r < ps->ray_count_needed; r++) {
            pool->ray_origins[ray_idx + r] = ps->ray_requests[r].origin;
            pool->ray_dirs[ray_idx + r]    = ps->ray_requests[r].direction;
        }
        ps->ray_batch_idx = ray_idx;
    }
    
    // ── Phase D: Harvest ──
    if (ps->done) {
        device_accumulate_result(ps, pool->estimator);
        // 标记为可回填
        uint32_t refill_idx = atomicAdd(&pool->refill_count, 1);
        pool->refill_slots[refill_idx] = slot_idx;
    }
}
```

**与 CPU merged_pass 的映射**:

| CPU merged_pass | GPU solver_advance | 差异 |
|----------------|-------------------|------|
| Phase A: OMP parallel distribute | Phase A: per-thread distribute | 线程模型不同 |
| Phase B: 内循环 cascade | Phase B: 限制深度 cascade | GPU 需防止 stackless 无限 |
| Phase C: 3-pass radix bucketed | Phase C: atomicAdd 收集 | 无需排序（OptiX 不关心顺序） |
| Phase D: 累积到 CPU accum | Phase D: atomicAdd 到 device | 原子开销较高，需 warp 归约 |

## 3. 任务调度

### 3.1 任务队列

```cuda
struct DeviceTaskQueue {
    // 任务数据（只读，初始化时 H2D 上传）
    double*    positions;       // [3 * total_tasks] probe 坐标
    uint32_t*  enc_ids;         // [total_tasks] enclosure ID
    uint32_t*  probe_indices;   // [total_tasks] batch probe 索引
    
    // 调度状态
    uint32_t total_tasks;
    uint32_t next_task;         // atomicAdd 分配下一个任务
};
```

Host 侧初始化时生成任务并 H2D 上传。GPU kernel 中通过 `atomicAdd(&queue->next_task, 1)` 原子分配。

### 3.2 路径池生命周期

```
状态转换:
  EMPTY → ACTIVE → DONE → EMPTY（循环）

  fill/refill:  atomicAdd(next_task) → 初始化 path_state → ACTIVE
  advance:      ACTIVE → step 推进 → needs_ray / done
  trace:        OptiX 填充 hit → has_pending_hit
  harvest:      done → 累积结果 → EMPTY → refill
```

### 3.3 compact & refill 策略

每个 kernel launch 后执行两个轻量 kernel：

```cuda
// 1. Stream compaction: 只保留 active 的路径
// 使用 CUB DeviceSelect::If 或 custom prefix-sum
compact_active_kernel:
    active_indices = compact(states, state != EMPTY && state != DONE)
    active_count = len(active_indices)

// 2. Refill: 把 DONE 的槽位换上新任务
refill_kernel:
    for each slot where state == DONE:
        task_idx = atomicAdd(&queue->next_task, 1)
        if task_idx < queue->total_tasks:
            init_path(slot, queue->tasks[task_idx])
            slot->state = ACTIVE
        else:
            slot->state = EMPTY  // 没有更多任务
```

### 3.4 池大小确定

沿用现有公式 `pool_size = sm_count × WARPS_PER_SM × 32`：
- RTX 4090: 128 SM × 8 × 32 = 32768 paths
- RTX 3080: 68 SM × 8 × 32 = 17408 paths
- 每路径 ~2KB → 32K paths ≈ 64MB（可接受）

## 4. 收集-排序-光追 子系统

### 4.1 光线收集

GPU solver_advance 中通过 `atomicAdd` 收集光线请求到 SoA 缓冲区：

```
d_ray_origins[N×3]       // float3, packed
d_ray_directions[N×3]    // float3, packed  
d_ray_ranges[N×2]        // float2 (tmin, tmax)
d_ray_slot_indices[N]    // 每条光线对应的 path slot
d_ray_count              // atomic 计数器
```

### 4.2 bucketed dispatch（可选优化）

在 Phase 3 优化中，可在光追前按 `ray_bucket` 排序：
- RADIATIVE rays → 连续 optixLaunch (大 batch, 高效)
- ENCLOSURE 6-ray queries → 连续 block
- STEP_PAIR (2-ray) → 连续 block

排序用 CUB DeviceRadixSort，开销 ~0.1ms/M keys。

### 4.3 OptiX 集成

```cpp
// LaunchParams 放在 device memory，通过 optixLaunch 的 pipelineParams 指定
struct SolverLaunchParams {
    // 输入
    float3*   ray_origins;
    float3*   ray_directions;
    float2*   ray_ranges;
    
    // 输出
    GpuHitResult* hit_results;   // 包含 geom_id, prim_id, normal, uv, distance
    
    // 场景
    OptixTraversableHandle traversable;
    
    // 滤波（L4 inline filter）
    uint32_t* prim_enc_front;    // prim → front enclosure
    uint32_t* prim_enc_back;     // prim → back enclosure
};
```

OptiX closest-hit program 直接写入 `hit_results[]`，无需 D2H。

### 4.4 enclosure 查询和 closest-point 查询

现有的 `batch_enc_locate` 和 `batch_cp` 也可以内化为 device 子系统：

**Enclosure locate (M10)**:
- 当前: CPU 收集 → GPU BVH closest_point → D2H → CPU 解析
- GPU-driven: device 函数直接查 device-side enclosure 映射表（O(1) 数组查找 by prim_id）
- 或用预计算的 voxel grid texture (最优, O(1) 查询)

**Closest-point (M9 WoS)**:
- 当前: CPU 收集 → GPU closest-point batch → D2H → CPU 解析
- GPU-driven: device 函数直接调用 OptiX 的 custom intersection 或 device-side 双精度 Voronoi

## 5. 对比 merge-phase 调度的简化

| merge-phase 特性 | GPU-driven 处理方式 |
|-----------------|-------------------|
| dual-view ping-pong | **删除** — 单池不需要 |
| O13 异步提交线程 | **删除** — 无 PCIe 传输需要异步 |
| pinned buffer 管理 | **删除** — 无 D2H/H2D |
| 3-pass radix bucketed collect | **替换**: atomicAdd 收集 + 可选 CUB sort |
| OMP parallel for | **替换**: CUDA grid 并行 |
| merge/split 动态视图管理 | **删除** — 单池 |
| CPU post-process (UV fixup) | **替换**: OptiX closest-hit program 直接输出 |
| active_indices compaction | **保留**: CUB DeviceSelect |

**净效果**: ~3000 LOC 的 dual-pool/async/pinned 代码变为 ~500 LOC 的 host launch loop。

## 6. 执行时间线对比

### merge-phase（当前）
```
Time →
CPU: [merged_pass 40ms][wait GPU][merged_pass 40ms][wait GPU] ...
GPU:          [trace 15ms]               [trace 15ms]
     ^^^^^^^^ idle ^^^^^^^^       ^^^^^^^^ idle ^^^^^^^^
PCIe:    [H2D 2ms][D2H 2ms]         [H2D 2ms][D2H 2ms]
```

### GPU-driven（目标）
```
Time →
Host: [launch advance][launch trace][launch advance][launch trace] ...
GPU:  [advance 5ms][trace 15ms][advance 5ms][trace 15ms] ...
      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
      连续执行，无 idle，无 PCIe

loop overhead: ~10μs launch latency per kernel (negligible)
```

---

*下一步*: → [02_STEP_MIGRATION.md](02_STEP_MIGRATION.md) (step 函数如何迁移到 `__device__`)
