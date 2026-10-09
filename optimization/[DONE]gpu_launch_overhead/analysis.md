# gpu_launch 89.8s 开销根因分析

**日期**: 2026-03-05  
**基准**: timing_coverage_baseline.md (porous 320×320 spp=32 pool=16384)  
**状态**: ✅ 方案 E 已实施并结题 (branch: opt/pinned-write)

**结果**: gpu_launch 92.9s → 27.5s (-65.4s)，墙钟 262.5s → 210.1s (-52.4s, -20%)。  
净改善 52.4s (vs 目标 60s) 差距由 M5 bug 修复前 baseline drain 步数偏少 ~3万步解释。  
方案 E 本身贡献 65.4s，超过 60s 目标。

---

## 1. 问题陈述

timing baseline 显示 `gpu_launch` 耗时 89.8s（28.4%），是最大单项开销。名义上是"GPU 启动"，但 CPU 线程全程阻塞。

## 2. gpu_launch 内部工作

每次 `gpu_launch_async()` 调用路径（`sdis_solve_persistent_wavefront.c` → `ox_s3d_scene_view.cpp::batch_trace_filtered_async_impl`）：

| 步骤 | 内容 | 同步/异步 |
|------|------|-----------|
| ① AoS→SoA 转换 | CPU for 循环: `s3d_ray_request`(36B) → `Ray`(32B) + `FilterPerRayData`(16B) 写入 pinned buffer | 同步（单线程） |
| ② memcpy filter | `memcpy(h_filter_pinned, filter_per_ray, nrays*16)` | 同步 |
| ③ H2D upload | `cudaMemcpyAsync` ×2 + `cudaEventRecord` + `cudaStreamWaitEvent` | GPU 侧异步，CPU 侧 driver dispatch ~10-20μs/call |
| ④ Kernel launch | `optixLaunch` + `cudaEventRecord` | GPU 侧异步，CPU 侧 driver dispatch ~20-50μs/call |

## 3. 关键发现：84s 是数据搬运而非 driver overhead

### 3.1 实测证据

| pool_size | steps | gpu_launch 耗时 |
|-----------|-------|-----------------|
| 16384 | 450K | ~92s |
| 较小池 | 250K | ~84s |

steps 减半但时间仅降 8s → **绝大部分耗时与 launch 次数无关，与总射线数成正比**。

### 3.2 开销分解

| 组分 | 耗时估算 | 缩放依据 |
|------|----------|----------|
| ① AoS→SoA 转换循环 | **~80-84s** | ∝ 总射线数 (12.9B) |
| ③④ CUDA/OptiX driver overhead | **~6-8s** | ∝ launch 次数 (900K × ~7μs) |

### 3.3 转换循环为何如此慢

```
12.9B rays × (读 s3d_ray_request 52B + 写 Ray 32B + 写 Filter 16B) = ~1.2TB 内存流量
单线程内存带宽 ~15GB/s (DDR4) → 理论 ~80s
```

这是一个连续内存的纯搬运循环，带宽受限，符合实测。

## 4. CPU postprocess 22s 的根因

### 4.1 L4 GPU inline filter 已消除的部分

- ~~CPU filter（自相交/共边/包壳检查）~~ → 移至 GPU anyhit
- ~~retrace 往返~~ → 从 48% → 0
- D2H 数据量从 120B/ray → 40B/ray（66% 缩减）

### 4.2 仍存在的 22s

L4 路径下 `batch_trace_filtered_wait_d2h_impl` 中：

```cpp
#pragma omp parallel for ...
for (int ii = 0; ii < count; ii++) {
    resolve_shape(sv, hr.geom_id, shape_id);     // unordered_map::find ×2
    hitresult_to_s3d_hit(sv, hr, shape, ...);     // 球体: acosf/atan2f; mesh: fixup
}
```

- `resolve_shape()`: `std::unordered_map::find` 哈希表查找，cache miss 严重
- `hitresult_to_s3d_hit()`: 球体 UV 需 `acosf`/`atan2f` 三角函数，mesh 需 `trace_hit_fixup`
- **不可移至 GPU 的原因**: `resolve_shape()` 返回 CPU 端 `s3d_shape*` 指针，上层求解器 distribute/cascade 广泛使用

## 5. Pinned 直写（方案 E）可行性分析

### 5.1 核心思路

collect Pass 2 scatter 中直接写入 GPU pinned buffer，消除中间 `ray_requests[]` + 整个 AoS→SoA 循环。

### 5.2 写侧不会引入散射 I/O

当前 collect Pass 2 的写模式：
- 写 `ray_requests[cursor]`：cursor 按 thread-local bucket 递增，顺序写
- 直写 `h_rays_pinned[cursor]`：**完全相同的访问模式**

`cudaHostAllocDefault` 在 x86 上是 Write-Back cached，与普通 `malloc` 内存无区别。

### 5.3 散射读是固有的，与目标 buffer 无关

```
path_state (~2040B) 中 ray_req 字段偏移 ~200:
  origin[3]    12B ──┐
  direction[3] 12B   ├─ 连续 32B，1 条 cache line
  range[2]      8B ──┘
  ...
filter_data    偏移 ~800: 再 1 条 cache line
```

每个 slot 读 2 条 cache line (128B)，不论写到哪。

### 5.4 总内存流量对比

| 方案 | 散射读 | 顺序写 | 额外顺序读 | 额外顺序写 | 合计/ray |
|------|--------|--------|-----------|-----------|---------|
| 当前两步 | 128B | 52B (requests+filter) | 52B (launch读) | 48B (pinned) | **280B** |
| 直写一步 | 128B | 48B (pinned ray+filter) | 0 | 0 | **176B** |

总流量减少 37%。

### 5.5 预期耗时

| 配置 | collect+launch 合计 | 相比当前 109s |
|------|-------------------|---------------|
| 直写 + 现有 OMP (~4-8T) | ~25-28s | -75% |
| 直写 + 12-16T OMP | ~18-20s | -82% |
| 理论极限 (DRAM BW 饱和) | ~14-16s | -85% |

硬底线：12.9B 射线散射读 ~8-10s + driver 6s = ~14-16s 不可突破。

---

*分析完成: 2026-03-05 | 下一步: 方案 E 实施计划 → implementation_plan.md*
