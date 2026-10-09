## Persistent Wavefront — 单条射线堵塞问题分析与可行修复方案

生成时间: 2026-02-11

概要
-
本文件汇总对 `persistent_wavefront`（Phase B-3）在实际运行中出现的性能退化问题的深入分析、调用链定位、射线调用开销量化、数据依赖性分析，以及若干可行的修复方案与推荐实施路线。

问题概述
-
1. 现象：在参数 `64x64 spp=8 pool_size=10240 total_tasks=32768` 下，GPU 运行远慢于 CPU：CPU 可在几分钟完成而 GPU 在 40 分钟仍未完成。日志显示进入 drain phase 后大量路径滞留在 `cond`（导电）桶且 `rad_bucket=0`。
2. Profiling Take 1：`solve_camera_persistent_wavefront` 占99.95%；其中 `pool_distribute_ray_results`（36%）→ `step_conductive_ds_process`（36%）→ `scene_get_enclosure_id_in_closed_boundaries`（35%）→ `s3d_scene_view_trace_ray`（≈35%）为主热链路；`advance_one_step_no_ray` 的 `step_boundary` 路径也大量调用 `s3d_scene_view_trace_ray`（≈49% 的热耗）——最终瓶颈集中在 `s3d_scene_view_trace_ray` 单条追踪上。

调用链与热路径（摘要）
-
四条主要热路径(依据Profiling Take 2, 时间较短，数据保守)：
```markdown
solve_camera_persistent_wavefront
├── pool_cascade_non_ray_steps_compact
│   └── advance_one_step_no_ray
│       ├── step_boundary
│       │   └── boundary_path_3d
│       │       └── solid_fluid_boundary_picard1_path_3d
│       │           └── find_reinjection_ray_and_check_validity_3d
│       │               ├── find_reinjection_ray_3d
│       │               │   └── s3d_scene_view_trace_ray -----------------------> 热路径 A (38.65%)
│       │               └── scene_get_enclosure_id_in_closed_boundaries
│       │                   └── scene_get_enclosure_id_in_closed_boundaries_3d
│       │                       └── s3d_scene_view_trace_ray -------------------> 热路径 B (14.38%)
│       └── step_conductive
│           └── scene_get_enclosure_id_in_closed_boundaries
│               └── scene_get_enclosure_id_in_closed_boundaries_3d
│                   └── s3d_scene_view_trace_ray -------------------------------> 热路径 C (14.04%)
└── pool_distribute_ray_results
    └── step_conductive_ds_process
        └── scene_get_enclosure_id_in_closed_boundaries
            └── scene_get_enclosure_id_in_closed_boundaries_3d
                └── s3d_scene_view_trace_ray -----------------------------------> 热路径 D (22.61%)
```

- 热路径 A, B: `step_boundary` → `boundary_path_3d`（及其子路径）
  - `solid_solid` / `solid_fluid` 等子函数会进入 `sample_reinjection_step`、`find_reinjection_ray`、`find_reinjection_ray_and_check_validity`。其中 `find_reinjection_ray_3d` 会发射 2 条独立 reinjection 射线（dir0/dir1），并在必要时通过 `scene_get_enclosure_id_in_closed_boundaries` 进行 enclosure 验证（最多 6 条）——累积调用极多。

- 热路径 C: `step_conductive` 初始化
  - `ds_initialized==false` 时需要一次 `scene_get_enclosure_id_in_closed_boundaries` 查询（最多 6 条射线）。

- 热路径 D: `step_conductive_ds_process`（Delta-sphere 后处理）
  - 处理由 batch 提交的 2 条 delta-sphere 射线结果后，可能需要调用 `scene_get_enclosure_id_in_closed_boundaries`（最多向 6 个方向发射检查射线）以确认 enclosure id。

注：不同 path 之间完全独立；但在同一 path 内存在短串行依赖（例如先得到 hit，再查 enclosure），但在多数场景中，同一 step 内的多个辅助射线（reinjection 的 dir0/dir1、enclosure 的 6 个方向）是相互独立或可改为并行/批量发射的。

单条 GPU trace 的开销（量化）
-
在 `custar-3d/cus3d_trace.cu` 中，单条 GPU trace（`cus3d_trace_ray_single_multi` / `cus3d_trace_ray_single`）的典型流程包含：

- `cudaMallocAsync`（若无常驻缓冲）
- `cudaMemcpyAsync` 上传 origin/dir/range
- kernel 启动（针对 1 条或 TOP-K）
- `cudaStreamSynchronize`（等待完成）
- `cudaMemcpyAsync` 下载结果
- `cudaFreeAsync`

实测/估算：单条调用开销约 15–25 μs（具体取决于实例化处理等），而 CPU Embree 单条 BVH 遍历仅 ~0.3–0.5 μs。因此，GPU 在单条调用场景反而比 CPU 慢得多，导致大量串行单条调用把整个程序堵死。

数据依赖性要点
-
- 不同路径（不同 slot/path）之间完全独立，可以并行化。
- 路径内部有短依赖链（例如：先获得 hit，再决定是否请求 enclosure 查询），但在多数场景中，同一 step 内的多个辅助射线（reinjection 的 dir0/dir1、enclosure 的 6 个方向）是相互独立或可改为并行/批量发射的。

可行方案（按修改量与收益排序）
-
下面列出若干可能的修复方向，从低侵入到高收益：

### 方案 D — cus3d 内部消除 per-call 开销（必做、低风险，1-2 天）

在 `cus3d` 层对单条调用做优化：常驻 GPU 缓冲/host pinned memory、预上传 instance 数据、避免每次 malloc/free、必要时用 CUDA Graph。API 不变，上层透明。预期单条调用可从 ~20 μs 降至 ~8–10 μs，但仍无法完全解决串行大量调用的问题。

### 方案 A' — 局部小批量化（低改动、快速见效，3-5 天）

在热点函数内部以小 batch 替换多次单条调用：
- 将 `scene_get_enclosure_id_in_closed_boundaries` 的 6 次方向查询改为一次 `s3d_scene_view_trace_rays` / batch 调用。
- 将 `find_reinjection_ray_3d` 的 dir0/dir1 两条射线改为一次 batch。

风险低、语义基本不变、能显著减少那些局部循环产生的重复往返。

### 方案 C — cus3d 层异步队列 + GPU 调度线程（中等改动，1-2 周）

在 cus3d 内部维护线程安全的请求队列，单条 `trace_ray` 变为入队等待，后台调度线程按阈值/超时 flush 批量发射。对上层调用透明（仍同步返回但实际由队列阻塞/唤醒实现）。

问题：若上层仍然是单线程推进（当前 wavefront 为单线程），队列无法积累足够的请求；需要配合多线程推进或重构上层。

### 方案 E — 多线程推进 wavefront（与方案 C 结合，高收益、高改动，3–5 周）

将 `pool_distribute_ray_results` / `pool_cascade_non_ray_steps_compact` 等处理改为多线程（worker pools），并与 cus3d 的异步队列配合，使多个线程并发产生 trace 请求，达到高吞吐量的 batch 大小。

风险：需要保证 `path_state` 的线程安全、每 slot 的 RNG 独立、避免竞态；实现复杂且验证复杂。

### 方案 B — 把内部 trace 完整提升到 wavefront 主循环（状态机拆分，最终方案，2–4 周）

将 `boundary_path_3d` / `conductive` 等内部的 trace 调用拆分成新的 wavefront phase（例如 `PATH_BOUNDARY_REINJECT_PENDING`、`PATH_COND_ENCLOSURE_PENDING`），所有辅助射线都由主循环批量发送并在下一轮分发结果。

优点：从根本上把所有单条 trace 都变为批量 trace，实现最佳 GPU 利用率。缺点：需要重写/拆分大量状态机代码，工作量最大，但收益也最大。

### 方案 F — 完全压平（极端方案，1-2 月）

把所有嵌套递归逻辑完全展平为有限状态机，使每一次需要射线的点都成为独立状态，全部通过主 batch 发送并行处理。理论最优，但实现与验证成本极高。

推荐实施路线（分阶段、可回滚）
-
1. 阶段 1（立即，1–2 天）
   - 实施方案 D（cus3d 预分配/消除 malloc/free、预上传实例数据、pinned host buffer），降低单条调用开销并获取更准确的基准数据。该项风险最低，收益立竿见影。

2. 阶段 2（1 周，可并行）
   - 实施方案 A'：在 `scene_get_enclosure_id_in_closed_boundaries`、`find_reinjection_ray_3d`、`step_conductive_ds_process` 等处，将可并行的多个射线合并为单次小批量调用（6 条或 2 条 batch）。这一步改动局部、回归测试容易，能快速减少大量小往返。

3. 阶段 3（2–4 周）
   - 设计并实施方案 B（核心状态机拆分），优先拆分 `boundary_path_3d` 中占比最高的 reinjection 路径，逐步把辅助查询提到 wavefront 主循环的批量层。此阶段需要详细设计、分支实现并覆盖大量 unit/integration test。

4. 可选并行化（长期，3–5 周）
   - 若希望保留单线程 wavefront 但仍追求更高吞吐，可考虑方案 E（多线程推进），配合 C（cus3d 异步队列）。该方案复杂度高，需严格并发安全设计。

下一步建议
-
1. 我将首先实现并提交 `cus3d` 层的预分配/常驻缓冲优化（方案 D），并运行相同测试场景得到新的 profile 基线。
2. 在方案 D 的基础上，实施方案 A' 的局部小批量化（`scene_get_enclosure_id_in_closed_boundaries` 与 `find_reinjection_ray_3d`），复测并对比加速比例。
3. 根据阶段 1–2 的采样数据，评估是否进入方案 B 的深度重构。

参考代码位置（快速索引）
-
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` — persistent wavefront 主循环与分发逻辑
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` — step 函数（`advance_one_step_no_ray`, `step_boundary`, `step_conductive` 等）
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_types.h` — `enum path_phase`, `enum ray_bucket_type`
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_state.h` — `struct path_state`, `struct path_ray_request`
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.h` — step 函数声明
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_wavefront.c` — per-tile wavefront 调度器（collect/distribute）
- `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp` — 批量 trace 的实现
- `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_trace_ray.cpp` 与 `d:\Works\Projects\Stardis-GPU\stardis-cus3d\custar-3d\0.10\src\cus3d_trace.cu` — 单条 trace 的实现与 GPU 往返开销源头

文件已创建： d:\Works\Projects\Stardis-GPU\guide\upper_parallelization\persistent_wavefront_callchain_analysis.md

---
（结束）
