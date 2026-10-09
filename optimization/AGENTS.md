# optimization 知识库

性能优化历史记录目录。记录各优化阶段的设计方案、实验结果和结论。

## 已完成优化（时间线）

### Pipeline优化链 L2-L4（已合并main）
- **L2 early-launch**：GPU在cascade前提前启动，削减CPU-GPU串行等待
- **L3 dual-stream ping-pong**（`2-stream_solver/`）：双流交替发射，GPU利用率提升
- **L4 GPU inline filter**（`cpu_wait_for_gpu_issue/`）：GPU内联过滤，减少CPU干预  
- **结果**：porous 320×320×32 总时间 7m25s → 5m51s（-21%），GPU kernel 11 Grays/s，瓶颈转移至CPU

### O1-O8 CPU微观优化（已合并main）
cascade计时开关、bucket_other移除、原子操作替换、harvest/refill OMP并行化、SYNC B skip、prefetch预取、REFILL_STACK_MAX bug修复

### O9 path_state SoA域分解（结题搁置）
- **目标**：将2040B AoS的path_state拆为8个SoA域数组，提升缓存效率
- **实测**：sync_a+sync_b消除(-46s)被refill/compact/collect回退(+30s)抵消，净 -17.6s(5.6%)
- **失败原因**：pool=32K时cascade 2.87×超线性劣化（多数组TLB/prefetch失效），成本/收益比不可接受
- **worktree**：`stardis-cus3d-o9`（已搁置）

### O14 per-thread 固定分区（结题关闭）
- **目标**：每线程绑定固定 slot 分区，消除跨线程缓存争用，改善 pool_size scaling
- **实测 v1（直扫）**：merged_pass +7~14%，wall +2.7~10.2%，全面劣化
- **实测 v2（分区内compact list）**：cpuA/B +7~16%，回归幅度与 v1 一致
- **失败原因**：超线性 per-slot 成本增长来自 capacity miss（slots[] 总 footprint 溢出 L3），非 conflict miss。调度策略无法修复容量缺失。baseline `schedule(dynamic, 64)` 的窄扫过 front 反而是近似最优
- **worktree**：`stardis-oxs3d-o14`（已关闭）
- **后续发现**：瓶颈指向 cascade 子步中场景数据（BVH/材料）的 L3 命中率，有效方向需减小 path_state 对 L3 的占用或提升场景数据访问相干性

### O15 Huge Pages TLB 优化（结题关闭）
- **目标**：将 slots[]/sfn_arr/enc_arr/ext_arr 从 4KB 页改为 2MB 大页分配，消除 TLB miss
- **结果**：VTune 确认瓶颈为 L3/DRAM capacity miss 而非 TLB，未实施
- **文档**：`wf_internal_opt/O15_huge_pages_tlb_optimization.md`

### O16 访存瓶颈定位与优化（进行中）
- **目标**：基于 VTune 访存分析定位高开销代码行，针对性优化 L3/DRAM 压力
- **背景**：pool=8K 时轻度 L3 瓶颈，pool=32K 时严重 DRAM 瓶颈（VTune 实测确认）
- **文档**：`wf_internal_opt/O16_memory_access_bottleneck_analysis.md`

### O10 Plan E pinned直写（已合并main）
- **目标**：collect阶段直写GPU pinned buffer，消除AoS→SoA转换循环
- **实测**：gpu_launch 92.9s → 27.5s（-65.4s），墙钟 262.5s → 210.1s（-20%）
- **净效果**：52.4s（vs目标60s，差距由baseline M5 bug致drain步数偏少解释）
- **分支**：`opt/pinned-write`（已合并main）

### O11 Dual-Buffer Wavefront（已合并main）
- **目标**：双池交替调度，CPU-GPU流水线化
- **实测**：CPU与GPU工作重叠，瓶颈从串行等待转移至CPU merged_pass
- **分支**：`opt/merge-phase`

### O12 Stream Auto-Ordering（已合并merge-phase）
- **目标**：`gpu_submit_all` 按 H2D→Kernel→D2H 顺序提交连续异步调用，`cudaStreamWaitEvent` 门控 D2H
- **实测**：GPU hidden 83%，wait_d2h=29.5s
- **分支**：`opt/stream-auto-order`（已合并merge-phase）

### O13 Async Submit Thread（已合并merge-phase）
- **目标**：将 `gpu_submit_all` 卸载到专用线程，消除 optixLaunch 主机开销对主循环的阻塞
- **设计**：per-view 独立通道（`go[2]` auto-reset + `done[2]` manual-reset），`WaitForMultipleObjects` 多路复用
- **实测**：submit=29.8s 完全隐藏，coverage=129.8%，wall=119.5s（vs O12 137.7s，-13.2%）
- **分支**：`opt/merge-phase` ec873d2

## 子目录索引

| 目录 | 内容 |
|------|------|
| `[DONE]2-stream_solver/` | L3双流求解器：P0-P5验证实验 |
| `[DONE]cpu_wait_for_gpu_issue/` | L2/L4：CPU等待GPU问题根因与双流/提前启动方案 |
| `[DONE]gpu_launch_overhead/` | GPU启动开销分析与优化计划 |
| `[DONE]wf_pipeline/` | 波前管线化分阶段实现（Phase 1-4）及Nsight Timeline分析 |
| `[DONE]fix_ray_stats/` | 射线统计修复 |
| `[TODO]remove_ray_bucket/` | 射线桶移除（CPU collect简化） |
| `[TODO]stack_free_picard/` | PicardN 显式栈外化为 PENDING pool：解除 picard_order≤3 上限、瘦身 path_state、风险与渐进路线 |
| `GPU_PP/` | GPU postprocess kernel：消除 20s cpu_postprocess（HitResult→s3d_hit 格式转换迁移至 GPU） |
| `gpu_L2_pressure/` | GPU L2缓存压力优化：射线空间排序设计 |
| `omp/` | OpenMP性能对标与位精确验证 |
| `path_depth_comparison/` | 路径深度性能统计与分析工具 |
| `solver_merge_phase/` | 求解器相位合并：冲突分析与方案 |
| `solver_soa/` | SoA数据结构转换（O9）：P0-P2系列指南 |
| `wf_internal_opt/` | 波前内部优化与SoA综合报告 |
