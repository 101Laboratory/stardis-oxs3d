# 1-Width RT Retrace 问题分析与解决路径

**日期**: 2026-02-18  
**优先级**: 低（遗留问题）  
**状态**: 已分析，待实施  

---

## 1. 问题描述

`s3d_scene_view_batch_trace.cpp` 的 `trace_rays_batch_impl` Step 4 对 batch 中所有 K 个 Top-K 候选均被 filter 拒绝的射线，逐条调用 `s3d_scene_view_trace_ray` 做同步 retrace。每条 retrace 射线启动 `<<<1,1>>>` GPU kernel，产生极高的 per-ray 开销。

### 触发条件

当 batch GPU Top-K trace 返回的 `multi->count == BATCH_TOPK_COUNT`（K 槽全满）且所有 K 个候选均被 CPU 侧 filter 拒绝时，该射线被标记 `needs_retrace=1`。

### 根本原因

`CUS3D_MAX_MULTI_HITS = 2`（K=2），每条射线仅返回 2 个候选。在蒙特卡洛辐射传输中，self-intersection filter 是最常见的拒绝原因：
- 候选[0] = self-hit → 被 filter 拒绝
- 候选[1] = 共边/背面/退化命中 → 也被拒绝
- 2 个槽位全满 → `needs_retrace=1`

### 架构问题

s3d（底层光追库）不应为 filter 拒绝的 trace 行为兜底。重试策略是上层 solver 的职责。当前实现将 "重试决策" 嵌入了 s3d 层，违反了关注点分离原则。

---

## 2. 实测数据

**场景**: porous 256×256×8 (524,288 任务)

```
fallback_retrace:  total=1218.0ms (0.5%)  accepted=11  missed=18  rejected=100200
```

| 指标 | 值 |
|------|-----|
| filter 拒绝总射线数 | 100,200 |
| 其中 BVH 穷尽 (count < K, 无需 retrace) | ~100,171 |
| 触发 retrace (count == K) | **29** |
| retrace 后接受 | 11 |
| retrace 后仍 miss | 18 |
| retrace 总耗时 | **1,218 ms** |
| 平均每条 retrace 耗时 | **~42 ms** |
| 占总 trace 时间比例 | 0.5% |

### 单条 retrace 开销分解

每条 retrace 内部递归最多 `MAX_FALLBACK_DEPTH=4` 层（共 5 次尝试）：

```
s3d_scene_view_trace_ray()
  └─ trace_ray_impl(depth=0)
       ├─ cus3d_trace_ray_single_multi() ← <<<1,1>>> kernel
       │    ├─ 6× cudaMallocAsync
       │    ├─ cudaMemcpyAsync (H2D × 3)
       │    ├─ trace_rays_instanced_topk_kernel<<<1,1>>>
       │    ├─ cudaStreamSynchronize × 2
       │    ├─ cudaMemcpyAsync (D2H)
       │    └─ 6× cudaFreeAsync
       ├─ CPU: filter 评估 → 全部拒绝
       └─ trace_ray_impl(depth=1, range[0] = hits[K-1].dist + 1e-6)
            └─ ... (递归，最多到 depth=4)
```

每层 ~8ms (launch + sync + malloc + memcpy)，29 × ~5 × ~8ms ≈ 1160ms，与实测 1218ms 吻合。

---

## 3. 涉及文件

| 文件 | 角色 |
|------|------|
| `custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp` L358-385 | Step 4: retrace 循环（问题核心） |
| `custar-3d/0.10/src/s3d_scene_view_trace_ray.cpp` L63-155 | `trace_ray_impl` 递归 retrace |
| `custar-3d/0.10/src/cus3d_trace.cu` L1224-1400 | `cus3d_trace_ray_single_multi` (`<<<1,1>>>` kernel) |
| `custar-3d/0.10/src/cus3d_types.h` L50 | `CUS3D_MAX_MULTI_HITS = 2` |
| `custar-3d/0.10/src/cus3d_trace.h` | batch/single trace API 定义 |
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` L1533 | 调用 `s3d_scene_view_trace_rays_batch_ctx` |

---

## 4. 解决方案

### 方案 A：提高 K 值（最简，正交优化）

将 `CUS3D_MAX_MULTI_HITS` 从 2 提高到 4 或 8。

**修改**:
- `cus3d_types.h`: `#define CUS3D_MAX_MULTI_HITS 4`（或 8）
- `cus3d_multi_hit_result` 结构自动扩大

**效果**:
- K=4 时 retrace 触发条件收紧（需 4 个候选全部被拒绝），频率大幅下降
- GPU kernel 插入排序开销微增（K≤8 时几乎无感）
- `cus3d_multi_hit_result` 内存增加 ~2×（40B → 80B/ray），batch 中 D2H 传输量增加
- CPU 后处理多检查几个候选（可忽略）

**预期**: 29 条 retrace 降至 0-3 条，1218ms → <100ms

### 方案 B：将 retrace 上推到 solver wavefront（架构正道）

s3d batch API 不再做 retrace，将 "全部K候选被拒" 状态返回给上层 solver，solver 在下一轮 wavefront iteration 重新提交这些射线（带推进的 range）。

**修改 Layer 1 — s3d batch API**:

1. 新增 per-ray 状态结构：
   ```c
   struct s3d_batch_ray_status {
     int    accepted;     /* 1=hit accepted, 0=miss(exhausted), -1=all K rejected */
     float  reject_t_far; /* 当 accepted==-1 时，最远被拒候选的距离 */
   };
   ```

2. 扩展 `s3d_scene_view_trace_rays_batch[_ctx]` 签名，添加 `s3d_batch_ray_status* statuses` 输出参数

3. 移除 `trace_rays_batch_impl` 的 Step 4 retrace 循环

4. 在 `s3d_batch_trace_context` 中预分配 status 数组

**修改 Layer 2 — Solver distribute**:

5. `pool_distribute_ray_results` 中检查 per-ray status：
   - `accepted==1` / `accepted==0`: 照常推进
   - `accepted==-1`: 更新 `ray_req.range[0] = reject_t_far + 1e-6f`，保留 `needs_ray=1`，递增 `retrace_depth`

6. `path_state.ray_req` 添加 `uint8_t retrace_depth` 字段，达到 4 后强制接受 miss

7. `s3d_batch_trace_stats` 中的 `retrace_accepted`/`retrace_missed` 字段变为由 solver 统计

**估计工作量**: ~200 行代码修改，跨 s3d 和 solver 两层

### 方案 C：A + B 正交组合（推荐）

先做 A（提高 K=4）从源头降低 retrace 频率，再做 B（上推到 solver）消除残余 retrace 的 `<<<1,1>>>` 路径。两者独立实施，可分阶段推进。

---

## 5. 性能影响评估

| 维度 | 当前 | 方案 A (K=4) | 方案 B (上推) | 方案 C (A+B) |
|------|------|-------------|-------------|-------------|
| retrace 频率 | 29/batch_call | ~0-3 | 29 (不变) | ~0 |
| retrace 延迟 | 1218ms | <100ms | 延迟 1 iteration | ~0 |
| GPU 利用率 | `<<<1,1>>>` retrace | 同左但次数少 | 100% batch | 100% batch |
| API 变更 | 无 | 仅宏定义 | 新增结构+参数 | 两者 |
| 实现复杂度 | - | 极低 | 中等 | 中等 |
| `cus3d_multi_hit_result` 大小 | 40B | 80B (K=4) | 40B | 80B |

---

## 6. 当前缓解措施

`s3d_scene_view_batch_trace.cpp` 中已有 `nrays==1` 的 assert 守卫（debug 构型触发断点）防止其他路径引发单条射线 batch launch。retrace 路径不受此守卫影响，因为它调用的是 `s3d_scene_view_trace_ray`（单射线 API），不经过 batch API。

---

## 7. 决策记录

- 2026-02-18: 问题已分析。29 条 retrace/1218ms。0.5% 占比，当前优先级不高。
- 推荐实施顺序: 先 A（5 分钟，改一行宏），效果立竿见影；B 作为架构清理在后续迭代中实施。
