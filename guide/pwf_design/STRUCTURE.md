# Persistent Wavefront Pool (PWF) 代码结构

**文件**: `stardis-oxs3d-o16/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`
**总行数**: ~5600
**迭代版本**: Phase B-3 M3 + O12/O13/O16 + Plan E · P0_OPT · P1 · P2

---

## 项目发展历程

```
M1/M2:     Fixed-size pool with refill (wavefront width constant)
M2.5:      Stream compaction + bucket index arrays
M3 (当前): Bucketed step dispatch + adaptive pool sizing + enhanced diagnostics
O11-O13:   双缓冲 + Stream Auto-Ordering + Async Submit (性能基准)
O16/Plan E: DRAM IO优化——NT Store Pinned Writes + Pinned Buffer直接写入
P0_OPT/P1/P2: Hot/Cold SoA分离 + Pool View抽象
```

---

## 代码结构概览

### 1. **头部和包含** (行 1-70)
- 版权声明 (GNU GPLv3)
- 功能概述注释
- 包含的依赖头文件
- 软件预取宏定义 (PREFETCH_T0)

#### 1.1 头部和包含 (O16新增)
```c
- sdis_wf_steps.h           // 内联step函数
- <emmintrin.h>              // O16: SSE2 NT store 支持
```

#### 2.1 像素跟踪 (Pixel Trace)
```c
- pixel_trace_file(): 初始化/返回跟踪文件
- 输出格式: path_id, px, py, spp, T_value, T_done, done_reason, steps, phase
- 激活方式: STARDIS_PIXEL_TRACE=<file_path>
```

#### 2.2 路径状态跟踪 (Path Trace O11_DIAG)
```c
- pt_enabled(): 检查是否启用（仅Debug版本）
- pt_log(): 每步记录路径状态
- 激活方式: STARDIS_PATH_TRACE=<max_lines>
- 格式: [PT] tag round=R s=SLOT pid=PID ph=BEFORE->AFTER act=A ...
```

### 3. **内存管理与池数据结构** (行 163-400)

#### 3.1 Pool View 初始化和销毁
```c
pool_view_create(struct pool_view* pv, void* base, size_t view_size, size_t capacity)
pool_view_destroy(struct pool_view* pv)
```

**分配的缓冲区**:
- Index arrays (按 capacity):
  - `active_indices[]` - 活跃路径索引
  - `need_ray_indices[]` - 需要光线的路径
  - `done_indices[]` - 完成的路径
  - `bucket_radiative[]` - 辐射路径桶
  - `bucket_conductive[]` - 热传导路径桶
  - `bucket_other[]` - 其他路径桶

- Ray buffers (按 max_rays = capacity × 6):
  - `ray_requests[]` - GPU光线请求
  - `ray_to_slot[]` - 光线→slot映射
  - `ray_slot_sub[]` - slot内的光线子索引
  - `ray_hits[]` - GPU光线命中结果
  - `filter_per_ray[]` - L4 GPU内联滤波器

- GPU Batch Context:
  - `batch_ctx` - 光线追踪上下文
  - `ray_pinned` - Plan E: 从batch_ctx借用的CUDA pinned光线缓冲指针
  - `filter_pinned` - Plan E: 从batch_ctx借用的CUDA pinned滤波器指针

- Enclosure Locate buffers (按 capacity):
  - `enc_locate_requests[]`
  - `enc_locate_results[]`
  - `enc_locate_to_slot[]`
  - `enc_batch_ctx`

- Closest Point buffers (按 capacity):
  - `cp_requests[]`
  - `cp_hits[]`
  - `cp_to_slot[]`
  - `cp_batch_ctx`

### 4. **动态合并/分割** (行 401-500)

```c
should_merge(pool)        → 检查是否从双缓冲合并到单缓冲
should_split(pool)        → 检查是否从单缓冲分割到双缓冲
merge_to_single_pool()    → 将两个view合并成一个
split_to_dual_pool()      → 将单个view分割成两个
```

**合并条件**: 任一view的活跃路径数 < 12.5% × view_size
**分割条件**: 活跃路径数 > 75% × pool_size 且有未分发的任务

#### 4.1 Merge的batch_idx处理

每个 pool_view 拥有独立的 `ray_hits[]` 缓冲区，路径的 `batch_idx` 是该缓冲区内的索引。  
合并时，View 1路径并入View 0，但原V1路径的 `batch_idx` 仍指向View 1缓冲区。  
为避免跨缓冲区读取，`merge_to_single_pool()` 对V1所有 `active && needs_ray` 路径将 `batch_idx` 设为 **sentinel值 `(uint32_t)-1`**：

| Phase | 行为 | 效果 |
|-------|------|------|
| **Phase A** (distribute) | 检测到 `batch_idx==sentinel` → 跳过 | 保持 `needs_ray=1` |
| **Phase B** (cascade) | 检测到 `needs_ray==1` → break | 路径不推进 |
| **Phase C** (collect) | `needs_ray==1` → 重新收集 | 新 `batch_idx` 指向合并后 `views[0]` 缓冲区 |

受影响路径需额外一轮GPU追踪（< 12.5% pool），merge是低频事件，开销可忽略。

### 5. **Wavefront Ops 和模式** (行 740-1400)

#### 5.1 操作接口结构
```c
struct wavefront_ops {
  res_T (*generate_tasks)(struct wavefront_pool*, const void* mode_ctx);
  res_T (*init_path)(struct path_state*, ...);
  void (*accumulate_result)(const struct path_state*, void* result_ctx);
};
```

#### 5.2 三种模式实现

| 模式 | 说明 | 用途 |
|------|------|------|
| **CAMERA** | 摄像机模式 | 图像渲染 (按Morton顺序W×H×spp) |
| **PROBE** | 单探测点 | 单点热传输 |
| **PROBE_BATCH** | 多探测点 | K个探测点×M次实现 |

**Camera模式关键函数**:
- `camera_generate_tasks()` - Morton顺序生成像素任务
- `camera_init_path()` - 从像素任务初始化路径
- `camera_accumulate_result()` - 将结果累积到estimator_buffer

**Probe模式关键函数**:
- `probe_generate_tasks()` - 生成 nrealisations 个线性任务
- `probe_init_path()` - 从探测位置初始化路径
- `probe_accumulate_result()` - 累积到 struct accum

**Probe Batch模式关键函数**:
- `probe_batch_generate_tasks()` - nprobes × nrealisations 任务（交错）
- `probe_batch_init_path()` - 从批量数组读取probe配置
- `probe_batch_accumulate_result()` - 按probe索引路由到不同accum

### 6. **光线请求收集与分发** (行 1400-2300)

#### 6.1 光线收集 (Ray Collection)
```c
pool_collect_ray_requests_bucketed(pool, pv)  // OMP 3-pass + Plan E pinned写入
  → 按路径阶段对光线请求进行桶划分：
    - RAY_BUCKET_RADIATIVE (单光线)
    - RAY_BUCKET_STEP_PAIR (1-2条光线)
    - RAY_BUCKET_SHADOW (单光线)
    - RAY_BUCKET_ENCLOSURE (6条光线)
    - RAY_BUCKET_STARTUP (单光线)
    - RAY_BUCKET_OTHER (混合)
```

### 7. **Enclosure 和 Closest Point 批处理** (行 2200-2400)

```c
pool_distribute_enc_locate_results(pool, pv)
  → 分发GPU结果，转换为PATH_ENC_LOCATE_RESULT

pool_distribute_cp_results(pool, pv)
  → 分发CP结果，映射到对应的_RESULT阶段
```

**注**: enc/cp 请求收集已整合入 merged_pass 的 Phase C (collect)

### 8. **级联处理（非光线步骤）** (行 2400-2900)

#### 8.1 级联主函数
```c
cascade_advance_single_path(p, scn, pool, slot_idx,
                            local_iterations, local_advances, ...)
  → 在单条路径上连续循环：
    - 检查是否需要光线
    - 执行非光线阶段步骤
    - 直到需要光线、路径完成或无进展
```

**关键计数器**:
- `local_iterations` - 循环次数
- `local_advances` - 成功推进数
- `local_paths_failed` - 失败路径数

#### 8.2 级联监测 (SDIS_CASCADE_PROFILE)
- 编译时启用per-phase时间统计
- 追踪路径阶段热点

### 9. **Merged Pass - 统一处理循环** (行 3000-4500)

**四相结构**:

1. **Phase A: 分发所有 GPU 结果 (distribute)**
   - A-enc: batch 级 enc_locate 结果分发（循环外）
   - A-cp: batch 级 closest_point 结果分发（循环外）
   - 重置计数器
   - A-rt: per-path 光线结果分发 + step（循环内）

2. **Phase B: 级联处理 (cascade)**
   - 对每条路径执行非光线步骤
   - 直到需要新光线

3. **Phase C: 收集新请求 (collect)**
   - 收集新光线/enc/cp 请求
   - 准备下一轮GPU批处理

4. **Phase D: 收割 (harvest)**
   - 完成/失败路径收割
   - 记录到 done_indices 供 refill 使用

**辅助函数**:
```c
merged_pass_collect_ray()        - 从单条路径收集光线到thread-local缓冲
merged_pass_flush_tl_rays()      - O16: 将thread-local光线经 NT Store写入钉住缓冲
merged_pass_fixup_batch_idx()    - 设置batch_idx指针供下轮分发使用
```

### 10. **GPU同步与启动** (行 5000-5500)

```c
gpu_launch_all(pool, pv, sv)         - 启动所有GPU追踪/enc/cp批
gpu_wait_download_all(pool, pv, sv)  - 等待下载GPU结果 (仅下载，distribute 在 merged_pass Phase A 执行)
```

**Pinned Buffer优化**:
- 使用钉住内存用于直接CPU↔GPU写入
- Plan E: 从batch_ctx借用指针以避免额外分配

### 11. **主运行循环** (行 5500-6200)

#### 11.1 双缓冲 (pool_run_dual)
```
     Phase 1a          Phase 1b           Phase 2a         Phase 2b
    ┌─────────┐        ┌─────────┐       ┌─────────┐      ┌─────────┐
    │ gpu_wait│        │gpu_launch│      │ gpu_wait│      │gpu_launch│
    │   (A)   │→merged_│   (B)    │→.....→  (B)   │→merged─(A)     │
    │         │   pass │          │      │        │ pass  │         │
    └─────────┘(A)     └─────────┘      └─────────┘       └─────────┘
    │←─ 等待A的GPU ─→│              │←─ 等待B的GPU ─→│
        结果                            结果

Overlap: GPU(X)执行时，CPU在Y上执行merged_pass
```

**步骤**:
1. 初始化: 启动GPU(A)
2. 主循环:
   - 等待GPU(A) → merged_pass(A) → 启动GPU(B)
   - 等待GPU(B) → merged_pass(B) → 启动GPU(A)
3. 动态合并检查（小负载合并到单缓冲）
4. 清理：等待最后的GPU调用

#### 11.2 单缓冲 (pool_run_single)
```c
pool_run_single(pool, sv, scn)
  → 简单的 while(active_count > 0 || tasks remaining):
      merged_pass → compact → refill → gpu_launch → gpu_wait
```

#### 11.3 统一分发器
```c
pool_run(pool, sv, scn)
  → 如果dual: pool_run_dual() 可能动态合并到单缓冲
  → 然后: pool_run_single() 处理剩余任务
```

### 12. **诊断和监控** (行 2900-3500)

#### 12.1 统计收集
```c
pool_update_diag_stats(pool, pv)
  - 记录光线计数（按类型）
  - 追踪路径状态（完成/失败/截断）
  - 累积活跃路径宽度
```

#### 12.2 阶段时间追踪
```c
time_compact_s           - Index压缩
time_collect_s           - 光线请求收集
time_trace_s             - GPU光线追踪
time_distribute_s        - 结果分发
time_enc_locate_s        - 包围体定位GPU批处理
time_cp_s               - 最近点GPU批处理
time_cascade_s          - 级联处理非光线步骤
time_harvest_s          - 收获+重新填充
time_housekeeping_s     - 内务处理
time_gpu_sync_s         - GPU同步等待
time_gpu_launch_s       - GPU启动开销
```

#### 12.3 诊断报告
```c
log_drain_phase_report(dev, pool)
  → 输出详细统计:
    - 总步骤/光线/平均波前宽度
    - 重新填充/清空阶段统计
    - 光线类型分布
    - 路径完成/失败/截断计数
    - 每个阶段的时间分解
    - GPU vs CPU时间比例
    - 级联per-phase热点（使用SDIS_CASCADE_PROFILE）
    - 批处理统计
```

### 13. **公开API** (行 5450-5541)

#### 13.1 Camera Rendering API
```c
sdis_solve_camera_to_wavefront()
  → 设置CAMERA模式上下文
  → 调用persistent_wavefront_main()
```

#### 13.2 Probe Sampling API
```c
sdis_solve_probe_to_wavefront()
  → 单点热传输
  → 设置PROBE模式

sdis_solve_probe_batch_to_wavefront()
  → K个探测点批量处理
  → 设置PROBE_BATCH模式
```

**公开接口参数**:
- 场景、RNG、探测配置
- 时间范围、Picard阶数、扩散算法
- 输出累积器

---

## 关键优化

### O1-O16 优化标记

| 标记 | 优化 | 文件位置 |
|------|------|--------|
| O2 | Pre-bucketed光线索引 (避免重新排序) | 光线分发 |
| O7 | Software prefetch (MSVC/GCC/Clang) | 头部 |
| O8 | Full memset in init (cache warming) | 路径初始化 |
| O11 | Dual-buffer overlap (GPU↔CPU并行) | pool_run_dual |
| O11_DIAG | Per-step路径跟踪 | 头部 |
| O11_MERGE | 动态合并/分割 | 主循环 |
| O11_SAFETY | Write-before-read同步 | 结果分发 |
| O12 | Stream Auto-Ordering (H2D→K→D2H 流水) | gpu_submit_all |
| O13 | Async Submit Thread (submit线程隐藏) | submit_thread_* |
| O16 | NT Store Pinned Writes (DRAM IO / cache污染降低) | merged_pass_flush_tl_rays |
| L4 | GPU内联滤波器 | 光线收集 |
| Plan E | Pinned Buffer直接写入 (消除中间拷贝) | pool_collect_ray_requests_bucketed |
| P0_OPT | `path_hot` SoA分离 (8B/slot vs ~2KB) | compact_active_paths |
| P1 | Cold-Block SoA分裂 (sfn/enc/ext独立数组) | pool_create |
| P2 | Pool View抽象 (双/单缓冲统一接口) | pool_view_init |

### 设计权衡

1. **内存 vs. 速度**
   - 预分配桶索引阵列避免动态内存分配
   - 钉住缓冲消除额外复制

2. **简化 vs. 通用性**
   - 单一pool_size对所有模式
   - 指针-based result_ctx用于模式无关性

3. **并行度**
   - 双缓冲视图允许GPU↔CPU重叠
   - Dynamic merge避免低负载时的开销

---

## 引用

- 设计文档: `guide/upper-parallelization/phase_b3_persistent_wavefront.md`
- 相关系统: `sdis_wf_rng.h` (RNG), `sdis_wf_types.h` (类型)
