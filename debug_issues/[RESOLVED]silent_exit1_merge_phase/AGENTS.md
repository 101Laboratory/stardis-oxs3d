# silent_exit1_merge_phase — merged_pass 线程局部射线缓冲区溢出

**分支**: `cus3d-merge-phase`  
**日期**: 2026-03-11  
**严重度**: 高  
**状态**: ✅ 已定位根因，已修复

## 问题描述

merge-phase 分支运行时静默崩溃：
- 退出码: 1 (EXIT_FAILURE)
- 不触发断言（assert）
- 调试环境（MSVC debugger）不会捕获/中断
- 无 stderr 错误信息输出（原因：solver 错误码静默传播无日志）
- 根因定位：`merged_pass` Phase C 线程局部射线缓冲区溢出

## 根因分析

### 错误传播链

```
merged_pass() Phase C: tl_ray_count + nrays > TL_RAY_BUF_MAX
  → tl_fatal = 1
  → had_fatal = 1
  → return RES_UNKNOWN_ERR (=4)
  → pool_tick_merged() propagates error
  → sdis_solve_persistent_wavefront() returns error
  → compute_boundary() returns error
  → stardis_compute() → ERR macro → goto error → EXIT_FAILURE
  → main() returns 1
```

### 触发条件

`TL_RAY_BUF_MAX = 8192`（硬编码常量），每线程预分配 8192 条射线的堆缓冲区。

原始注释假设 "pool=20K paths, 32 threads → 每线程 ~625 paths × max 6 rays = 3750 << 8192"。

**但实际运行时**：
- step 82001: **8192/8192 active paths**, 15936 rays
- `schedule(dynamic, 64)` — 不保证严格均分
- 某线程累积 `tl_ray_count=8188`，再遇到 6-ray enclosure 查询 → 8194 > 8192 → fatal

### 根本原因

1. **TL_RAY_BUF_MAX 是硬编码安全上限**而非弹性容量
2. **溢出处理为 fatal error**而非 flush-and-continue
3. 当 `active_paths / omp_nthreads` 比例不利时（少线程、多 active paths），单线程可轻松超限

## 修复方案

**中途 flush**：当 `tl_ray_count + nrays > TL_RAY_BUF_MAX` 时，使用原子操作预留写入偏移，
将当前缓冲区 flush 到 pinned buffer，然后重置 `tl_ray_count = 0` 继续收集。

修改文件：`stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

```c
/* Mid-loop flush: if buffer would overflow, flush to pinned
 * and reset.  Atomic reservation ensures no overlap. */
if(tl_ray_count + nrays > TL_RAY_BUF_MAX) {
  size_t ray_base = (size_t)_InterlockedExchangeAdd64(
    (volatile long long*)&total_rays, (long long)tl_ray_count);
  merged_pass_flush_tl_rays(pool, pv, tl_rays, tl_ray_count, ray_base);
  tl_ray_count = 0;
}
```

**正确性保证**：
- `merged_pass_flush_tl_rays()` 只写入 `pv->ray_pinned/filter_pinned/ray_to_slot/ray_slot_sub`
- 这些数组的每个偏移位置只被一个线程写入（原子预留保证）
- `merged_pass_fixup_batch_idx()` 在所有线程的所有 flush 完成后才调用，遍历 `total_rays`
- 同一线程多次 flush 不影响后续 fixup 逻辑

---

## Q&A：mid-loop flush 线程安全性详解

> **Q**: 线程 A 预计在 `[a, b)` 范围写入，线程 B 在 `[b, c)` 写入。如果 A 在中途移动了自己的缓冲区指针（`tl_ray_count = 0`），会导致 B 的内容被覆盖吗？

### 两层缓冲区，性质不同

| 缓冲区 | 地址 | 归属 | "移动指针"的含义 |
|--------|------|------|----------------|
| `tl_rays` | `pool->tl_ray_bufs[thread_id]` | **per-thread 私有** `malloc` 块 | `tl_ray_count = 0` 是 OMP 并行块内的栈变量，只影响本线程的写入偏移 |
| `pv->ray_pinned[]` | 全局 pinned 内存 | 共享 | 由原子 `_InterlockedExchangeAdd64` 预留不重叠区间后写入 |

`tl_ray_count = 0` 重置的是线程 A 自己的**私有局部变量**。线程 B 有自己独立的 `tl_ray_count`（也是栈变量），且 `tl_rays` 指向的是完全不同的 `malloc` 块（`pool->tl_ray_bufs[B_id]`）。A 的重置不触及 B 的任何内存。

### 全局 pinned buffer 的区间不重叠保证

每次 flush（无论 mid-loop 还是 end-of-loop）都是相同模式：

```c
ray_base = _InterlockedExchangeAdd64(&total_rays, tl_ray_count);  // 原子预留 [ray_base, ray_base+count)
merged_pass_flush_tl_rays(..., tl_rays, tl_ray_count, ray_base);   // 向该区间写入
```

`_InterlockedExchangeAdd64` 的原子加法保证：不论多少线程同时 flush（包括 mid-loop 与 end-of-loop 混合），每次预留的 `[ray_base, ray_base+count)` 区间唯一且互不重叠。

### 单 slot 的射线不会被 flush 切断

overflow 检查发生在 `merged_pass_collect_ray()` **之前**：

```c
if(tl_ray_count + nrays > TL_RAY_BUF_MAX) {
    // 先 flush 旧数据并重置
    tl_ray_count = 0;
}
// 之后才写入当前 slot 的全部 nrays 条射线
added = merged_pass_collect_ray(..., tl_rays, tl_ray_count);
tl_ray_count += added;
```

一个 slot 的 `sub=0,1,2,…,5` 始终在同一批次内写入，不会被切分到两段 pinned 区间。

### `merged_pass_fixup_batch_idx` 不依赖射线的空间连续性

fixup 对每条全局射线索引 `r` 独立读 `ray_to_slot[r]` + `ray_slot_sub[r]`，将 `r` 写回对应 `batch_idx`/`batch_indices[sub]`。它不要求属于同一 slot 的射线在 pinned buffer 中连续，也不要求按线程分组排列。

**结论**：mid-loop flush 是线程安全的。`tl_rays` 是 per-thread 私有内存，全局 pinned 区间由原子操作保证互不重叠，fixup 阶段不假设射线的空间局部性。
