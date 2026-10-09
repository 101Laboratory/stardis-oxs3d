# 双池管线时序分析与相位误标发现

**创建日期**: 2026-03-03 ~ 2026-03-04  
**状态**: 完成【数据已过期】
**关联文档**:
- `guide/performance/pool_size_scaling_timeline_analysis.md` — Pool Size 缩放 Mermaid 甘特图
- `guide/performance/dual_pool_pipeline_timeline_analysis.md` — dual-pool 24K 详细时序
- `Stardis-Starter-Pack/porous/timeline_data/` — 原始 CSV 数据

---

## 1. 背景

通过 OptiX 后端 `STARDIS_PIPELINE_LOG=2` 采集的 `[TIMELINE]` 日志，分析双池交替管线在不同 pool_size 下的执行时序特征。

### 1.1 原始 TIMELINE 格式（L1 时代）

```
[TIMELINE] step=N |waitA=Xms|launchB=Xms|cpuA=Xms|waitB=Xms|launchA=Xms|cpuB=Xms|cycle=Xms raysA=N raysB=N
```

6 个相位：`waitA → launchB → cpuA → waitB → launchA → cpuB`

---

## 2. Pool Size 缩放数据汇总

数据来源：4 组 pool_size (8192/12288/16384/32768) 的 TIMELINE 日志。

### 2.1 典型周期时序

| pool_size | cycle(ms) | waitA(ms) | launchB(ms) | cpuA(ms) | waitB(ms) | launchA(ms) | cpuB(ms) |
|-----------|-----------|-----------|-------------|----------|-----------|-------------|----------|
| 8,192     | 2.32      | 0.62      | 0.15        | 0.38     | 0.65      | 0.16        | 0.36     |
| 12,288    | 3.04      | 0.87      | 0.24        | 0.47     | 0.86      | 0.22        | 0.38     |
| 16,384    | 4.37      | 1.28      | 0.26        | 0.70     | 1.26      | 0.21        | 0.66     |
| 32,768    | 10.39     | 2.56      | 0.42        | 2.39     | 2.52      | 0.39        | 2.11     |

### 2.2 Wait 占比趋势

| pool_size | wait占比 | cpu占比 | 甜蜜点 |
|-----------|----------|---------|--------|
| 8,192     | 54.7%    | 31.9%   | ★ 最优 |
| 12,288    | 56.9%    | 28.0%   | 接近   |
| 16,384    | 58.1%    | 31.1%   | L3溢出 |
| 32,768    | 48.9%    | 43.3%   | L3严重溢出 |

> pool ≥ 16384 时 cpu 阶段超线性增长（cascade L3 thrashing），甜蜜点在 8192~12288。

---

## 3. 关键发现：TIMELINE 相位误标

### 3.1 问题

TIMELINE 中 `waitA`/`waitB` 测量的是整个 `gpu_wait_and_postprocess()` 函数，而非仅 GPU 等待。

```c
/* 测量代码 (sdis_solve_persistent_wavefront.c L3585-3592) */
time_current(&t_pl0);
res = gpu_wait_and_postprocess(&pool, pv_a, scn->s3d_view, scn);
time_current(&t_pl1);
pool.time_pipeline_wait_s += time_elapsed_sec(&t_pl0, &t_pl1);
t_cy[1] = t_pl1;  /* ← 这被标记为 "waitA" */
```

### 3.2 `gpu_wait_and_postprocess` 实际包含

| 子操作 | 类型 | 实际占比(8K) |
|--------|------|-------------|
| `cudaStreamSynchronize` + D2H download | GPU→CPU 传输 | ~53% |
| `pool_distribute_ray_results` | CPU 纯计算 | ~15% |
| `pool_collect_enc_locate_requests` + 批量 enc_locate | CPU+GPU | ~15% |
| `pool_collect_cp_requests` + 批量 closest_point | CPU+GPU | ~10% |
| `dispatch_soa_sync_from_path` (dsoa sync) | CPU 纯计算 | ~7% |

### 3.3 GPU Phase Breakdown 实测数据

来自 `gpu_phase_breakdown.md` 的 instrumentation 数据，真实 GPU-wait 子阶段分解：

| 子阶段 | pool=8K占比 | pool=32K占比 | 性质 |
|--------|-------------|-------------|------|
| `d2h_download` | **45.7%** | **43.0%** | PCIe 传输 |
| `fallback_rtrc` | 16.0% | 8.8% | CPU retrace |
| `aos2soa_upload` | 10.3% | 11.9% | PCIe 传输 |
| `cpu_filter_eval` | 10.6% | 13.4% | CPU 纯计算 |
| `cpu_postprocess` | 10.6% | 13.4% | CPU 纯计算 |
| `cuda_sync_wait` | **6.8%** | **9.6%** | **真正的 GPU 等待** |

### 3.4 修正后的 CPU 空闲率

| | 旧理解 (wait=GPU等待) | 修正后 |
|--|----------------------|--------|
| CPU 空闲占比 | ~54% | **~7-10%** (仅 cuda_sync_wait) |
| GPU 空闲占比 | ~32% (cpu阶段) | ~32% + 部分 post/distribute |

> **核心结论**: 真正的 CPU 空闲时间（等 GPU kernel）仅占周期的 3-5%。
> 绝大部分 "wait" 时间实际是 PCIe 传输 + CPU 后处理。

---

## 4. 优化层级定义

基于以上分析，定义 4 个管线优化层级：

| Level | 名称 | 特征 | 状态 |
|-------|------|------|------|
| L0 | 完全串行 | 单池，GPU trace 同步阻塞 | 历史基线 |
| L1 | 基础双缓冲 | 双池交替，wait+post 完成后才 launch | 已实现 (main) |
| L2 | Early-Launch | D2H 完成即 launch，post 与对面 GPU 重叠 | 已实现 (opt/early-launch) |
| L3 | 双 Stream | transfer/compute 分离，H2D↑/D2H↓ PCIe 全双工 | 设计完成 |

---

## 5. 详细甘特图

完整的 Mermaid 甘特图参见 `guide/performance/pool_size_scaling_timeline_analysis.md`。

CSV 原始数据在 `Stardis-Starter-Pack/porous/timeline_data/`:
- `timeline_8192.csv` (833 rows)
- `timeline_12244.csv` (5479 rows)  
- `timeline_16384.csv` (434 rows)
- `timeline_32768.csv` (235 rows)
