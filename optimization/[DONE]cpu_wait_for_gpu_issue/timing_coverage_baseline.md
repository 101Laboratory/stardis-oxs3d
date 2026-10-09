# Persistent Wavefront 全阶段计时基准数据

**日期**: 2026-03-04  
**场景**: porous 320×320 spp=32 pool=16384  
**代码**: main (L2-L4 merged) + timing instrumentation  
**路径**: dual-buffer refill → single-pool drain  

---

## 1. 全局计时数据

```
persistent_wavefront DONE: 320x320 spp=32 pool=16384
  elapsed=5 mins 22 secs
  steps=450089  rays=12,908,449,802  avg_width=14,503.8
  rays: rad=866M  cond_ds=2490M(retry=144)  shadow=0  enc=9552M  startup=0
  paths: completed=3,276,800  failed=115  max_depth=278,340
```

## 2. 完整阶段计时（coverage=99.8%）

```
timing: compact=18.072s  collect=19.263s  trace=27.414s(gpu=5.036s cpu=21.350s)
        distribute=32.883s  enc_locate=10.254s  cp=5.734s
        sync_a=20.965s  cascade=40.234s  sync_b=24.969s
        harvest+refill=23.511s  housekeeping=1.264s
        gpu_sync=0.916s  gpu_launch=89.763s
        total_timed=315.242s  wall=315.741s  coverage=99.8%
```

## 3. 阶段分解（按耗时排序）

| # | 阶段 | 耗时(s) | 占比 | 性质 | 说明 |
|---|------|---------|------|------|------|
| 1 | **gpu_launch** | 89.8 | 28.4% | CPU→GPU | H2D memcpy + kernel launch，450K次×~0.2ms |
| 2 | **cascade** | 40.2 | 12.7% | 纯CPU | 状态机非光线步推进（OMP并行） |
| 3 | **distribute** | 32.9 | 10.4% | 纯CPU | 光线结果分发到路径槽位 |
| 4 | **trace** | 27.4 | 8.7% | CPU+GPU | gpu=5.0s(kernel+D2H), cpu=21.4s(filter) |
| 5 | **sync_b** | 25.0 | 7.9% | 纯CPU | cascade后dsoa同步（遍历active_compact） |
| 6 | **harvest+refill** | 23.5 | 7.4% | 纯CPU | 路径收割+初始化新路径+推进到首次射线 |
| 7 | **sync_a** | 21.0 | 6.6% | 纯CPU | distribute后dsoa同步（need_ray+enc+cp） |
| 8 | **collect** | 19.3 | 6.1% | 纯CPU | 射线请求收集（2-pass radix bucketing） |
| 9 | **compact** | 18.1 | 5.7% | 纯CPU | 流压缩（活跃路径索引重建） |
| 10 | **enc_locate** | 10.3 | 3.3% | CPU+GPU | 包壳定位批量查询 |
| 11 | **cp** | 5.7 | 1.8% | CPU+GPU | 最近点批量查询 |
| 12 | **housekeeping** | 1.3 | 0.4% | 纯CPU | pool_update_active_count+诊断+进度 |
| 13 | **gpu_sync** | 0.9 | 0.3% | GPU等待 | kernel完成同步等待 |

**合计**: 315.2s / 315.7s wall = **99.8% coverage**

## 4. CPU vs GPU 时间分割

| 类别 | 耗时(s) | 占比 |
|------|---------|------|
| **纯CPU工作** | ~264s | 83.6% |
| **CPU→GPU传输 (gpu_launch)** | 89.8s | 28.4% |
| **GPU计算 (kernel + D2H)** | ~6.0s | 1.9% |
| **CPU端过滤 (trace cpu)** | 21.4s | 6.8% |

> GPU kernel 已完全被隐藏在 CPU 工作之后。**CPU 是唯一瓶颈**，占据 ~98% 的 wall-clock。

## 5. 关键发现

### 5.1 gpu_launch 是最大单项（89.8s = 28.4%）

`gpu_launch` 计时覆盖 dual-buffer 循环中每个 Phase 的 3 步：
- `gpu_sync_kernel()` 后到 `gpu_start_d2h()` 结束（startD2h 启动 async D2H）
- `gpu_launch_async()` 全过程（H2D memcpy + kernel launch）

450,089 步 × 每步 2 个 Phase = ~900K 次启动，每次包含：
- H2D: `ray_requests[]` 上传（~28K rays × sizeof(ray_request)）
- kernel launch 开销
- 部分 `cudaMemcpyAsync` 的 CPU 端同步等待

**这部分时间名义是"GPU启动"，但 CPU 线程是阻塞的**，是 H2D 数据搬运的 CPU 等待时间。

### 5.2 sync_a + sync_b = 46.0s（14.6%）— dsoa 同步开销

`dispatch_soa_sync_from_path()` 是 inline 函数，每次写 5 个散落字段：
```c
soa->phase[idx]         = p->phase;
soa->active[idx]        = p->active;
soa->needs_ray[idx]     = p->needs_ray;
soa->ray_bucket[idx]    = p->ray_bucket;
soa->ray_count_ext[idx] = p->ray_count_ext;
```

- sync_a: 每步遍历 need_ray_count + enc_locate_count + cp_count 个槽位
- sync_b: 每步遍历 active_compact 个槽位（含 O6 skip 优化）
- 合计: 450K 步 × 数万次/步 = **数十亿次内存写入**

**SoA 化 path_state 后可直接消除这 46s**——这是 AGENTS.md 规划的下一步优化的直接靶点。

### 5.3 trace 内部 CPU 占 81.6%

batch trace profiling 详情：
```
gpu_kernel:      4038.4ms (14.9%)  — GPU 计算
d2h_wait:         923.0ms  (3.4%)  — D2H 传输等待
cpu_postprocess: 22071.4ms (81.6%) — CPU filter 过滤
fallback_retrace:    0.0ms  (0.0%)
gpu_throughput: 3196.4 Mrays/s (kernel only)
```

trace=27.4s 中真正的 GPU 工作仅 5.0s，其余 21.4s 是 CPU 端 multi-hit 过滤判断。

## 6. 优化优先级（按预期收益排序）

| 优先级 | 靶点 | 耗时 | 优化方向 | 预期收益 |
|--------|------|------|----------|----------|
| **P0** | gpu_launch (89.8s) | 28.4% | pinned memory / 减小 ray_request / 合并批次 | 30-60% 该项 |
| **P1** | sync_a+sync_b (46.0s) | 14.6% | path_state SoA化，消除 dispatch_soa 层 | ~100% 该项 |
| **P2** | cascade (40.2s) | 12.7% | OMP 深度优化 / 减少状态转移次数 | 20-40% 该项 |
| **P3** | distribute (32.9s) | 10.4% | OMP 并行化 / 减少 per-ray 处理 | 20-40% 该项 |
| **P4** | trace cpu (21.4s) | 6.8% | GPU inline filter 深度优化 | 进一步减少 CPU filter |
| **P5** | collect (19.3s) | 6.1% | 增量式收集 / 减少数据拷贝 | 20-30% 该项 |
| **P6** | compact (18.1s) | 5.7% | 增量式 compact / free_list | 50%+ 该项 |

### 总潜力

P0+P1 合计 135.8s (43%)，如果各优化 50%，总时间可从 316s 降至 ~248s（21% 提升）。
P0-P3 全部优化后理论极限 ~180-200s（37-43% 提升）。

## 7. 计时基础设施

### 新增计时器（本次实现）

| 字段 | 位置 | 说明 |
|------|------|------|
| `time_sync_a_s` | pool struct | SYNC POINT A: distribute后dsoa同步 |
| `time_sync_b_s` | pool struct | SYNC POINT B: cascade后dsoa同步 |
| `time_housekeeping_s` | pool struct | Steps H-L: update_active+诊断+进度 |
| `time_gpu_sync_s` | pool struct | dual-buffer GPU kernel sync等待 |
| `time_gpu_launch_s` | pool struct | dual-buffer H2D+startD2h+launch |
| `time_enc_locate_s` | pool struct | 已有但未报告 → 现已加入输出 |
| `time_cp_s` | pool struct | 已有但未报告 → 现已加入输出 |

### 覆盖范围

- **Single-pool 循环**: 所有 Steps A-L 均被计时
- **Dual-buffer 循环**: 所有 helper 函数内部 + Phase 1/2 GPU ops + housekeeping A/B 均被计时
- **Summary log**: 输出 13 个计时器 + total_timed + wall + coverage%

---

*基准数据创建: 2026-03-04 | 代码变更: timing instrumentation (13 timers, 99.8% coverage)*
