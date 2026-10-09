# L2 Early-Launch 实现记录

**创建日期**: 2026-03-04  
**状态**: 已实现，构建通过  
**Worktree**: `stardis-cus3d-early-launch`，分支 `opt/early-launch`，基于 `bd0c248`  
**构建配置**: `-DS3D_BACKEND=optix -DENABLE_TESTS=OFF`，Release

---

## 1. 优化动机

L1 主循环时序：
```
waitA(sync+d2h+post+distribute+enc+cp+dsoa) → launchB → cpuA → waitB → launchA → cpuB
```

问题：`gpu_launch_async(B)` 必须等整个 `gpu_wait_and_postprocess(A)` 完成才发出。
但 `gpu_launch_async(B)` 仅依赖 `pv_b->ray_requests`（上一轮 `cpu_pre_gpu(B)` 已准备好），与 A 的后处理完全无关。

**关键依赖分析**: `gpu_launch_async()` 只读取 `pv->ray_requests` + 设置 `pv->gpu_pending`，不访问对面池的任何数据。

---

## 2. 设计：函数拆分 + 时序重排

### 2.1 函数拆分

将 `gpu_wait_and_postprocess()` 拆为两个函数：

| 函数 | 职责 | GPU 依赖 |
|------|------|---------|
| `gpu_wait_download()` | `cudaStreamSynchronize` + D2H + 统计累加 | **阻塞** — 等 GPU kernel |
| `gpu_postprocess()` | distribute + enc_locate + cp + dsoa_sync | **无** — 纯 CPU |

原 `gpu_wait_and_postprocess()` 保留为组合 wrapper，供 merge/drain 路径使用。

### 2.2 时序重排

```
L1 (旧):  wait+post(A) ─────────→ launch(B) → cpu(A) → wait+post(B) ─────────→ launch(A) → cpu(B)
L2 (新):  d2h(A) → launch(B) → post(A) → cpu(A) → d2h(B) → launch(A) → post(B) → cpu(B)
```

```
L2 时序图:
CPU:  ─[d2h(A)]─[launch(B)]─[post(A)]───[cpu(A)]───[d2h(B)]─[launch(A)]─[post(B)]───[cpu(B)]─
GPU:  ←A完成→               ═══════════[trace(B)]═══════════             ═══════════[trace(A)]═══
                             ↑GPU(B)在post(A)+cpu(A)全程并行运行
```

**收益**: GPU(B) kernel 提前 ~0.28ms 启动（post 阶段时间），post(A)+cpu(A) 全程被 GPU(B) 遮盖。

### 2.3 TIMELINE 扩展

从 7 时间戳扩展到 9 时间戳：

```
L1: [0]=start [1]=waitA [2]=launchB [3]=cpuA [4]=waitB [5]=launchA [6]=cpuB
L2: [0]=start [1]=d2hA  [2]=launchB [3]=postA [4]=cpuA [5]=d2hB [6]=launchA [7]=postB [8]=cpuB
```

TIMELINE 输出格式：
```
[TIMELINE] step=N |d2hA=Xms|launchB=Xms|postA=Xms|cpuA=Xms|d2hB=Xms|launchA=Xms|postB=Xms|cpuB=Xms|cycle=Xms
```

---

## 3. 代码改动

**文件**: `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

### 3.1 新增 `gpu_wait_download()` (L2933)

```c
static res_T
gpu_wait_download(struct wavefront_pool* pool,
                  struct pool_view* pv,
                  struct s3d_scene_view* sv)
{
  /* 仅包含: batch trace wait (sync + D2H) + 统计累加 */
  /* 对应原 gpu_wait_and_postprocess 的前半部分直到 time_trace_s */
}
```

### 3.2 新增 `gpu_postprocess()` (L2998)

```c
static res_T
gpu_postprocess(struct wavefront_pool* pool,
                struct pool_view* pv,
                struct sdis_scene* scn)
{
  /* distribute + enc_locate + cp + dsoa_sync */
  /* 对应原 gpu_wait_and_postprocess 的后半部分 */
}
```

### 3.3 保留 `gpu_wait_and_postprocess()` wrapper (L3091)

```c
static res_T
gpu_wait_and_postprocess(...)
{
  res_T res = gpu_wait_download(pool, pv, sv);
  if(res != RES_OK) return res;
  return gpu_postprocess(pool, pv, scn);
}
```

### 3.4 主循环改动 (L3615-L3860)

Phase 1: `gpu_wait_download(A)` → `gpu_launch_async(B)` → `gpu_postprocess(A)` → `cpu_between(A)` + `cpu_pre_gpu(A)`  
Phase 2: `gpu_wait_download(B)` → `gpu_launch_async(A)` → `gpu_postprocess(B)` → `cpu_between(B)` + `cpu_pre_gpu(B)`

> merge/drain 路径不变，继续使用原 `gpu_wait_and_postprocess()` wrapper。

---

## 4. 预期收益

基于 pool=8192 的 phase breakdown 数据：

| 项目 | L1 耗时 | L2 节省 |
|------|---------|---------|
| post 被 GPU 遮盖 | ~0.28ms/半周期 | post(distribute+enc+cp+dsoa) 与 GPU 并行 |
| 半周期缩短 | ~1.16ms → ~0.88ms | ~24% |
| 整周期缩短 | ~2.32ms → ~1.76ms | ~24% (理论上限) |

> 实际收益取决于 GPU kernel 时间是否足够长以完全遮盖 post 阶段。
> 若 GPU-bound（kernel > post+cpu），post 被完全遮盖；若 CPU-bound，仍有剩余。

---

## 5. 构建验证

```
Worktree: d:\Works\Projects\Stardis-GPU\stardis-cus3d-early-launch
Branch:   opt/early-launch (bd0c248)
CMake:    -G "Visual Studio 17 2022" -A x64 -DS3D_BACKEND=optix -DENABLE_TESTS=OFF
Build:    cmake --build . --config Release → exit code 0
Output:   build/bin/Release/stardis.exe ✓, htpp.exe ✓
Warnings: C4005 "ERROR" macro redefine (sstl vs wingdi.h) — 已知，无影响
```
