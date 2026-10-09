# GPU Postprocess Kernel — 消除 cpu_postprocess 瓶颈

**状态**: TODO  
**目标**: 将 20s `cpu_postprocess`（纯格式转换）迁移至 GPU kernel，D2H 直出 `s3d_hit`  
**预期收益**: wall time 120s → ~101s（-16%）  
**Worktree**: `stardis-oxs3d-merge-phase`  
**基线 commit**: `1a86f88` (refactor: unify persistent wavefront execution via run_pool)

---

## 背景

O13 pipeline timing（porous 320×320×32）：
```
submit       =  29.5s [24.6%]  (async thread, fully hidden)
wait_d2h     =  27.2s [22.6%]
  trace_stall=   3.2s           (device: kern=3.4s d2h=3.1s, 50% hidden)
  trace_post =  20.0s           (host: post=20.0s retrace=0.0s)  ← TARGET
merged_pass  =  78.9s [65.7%]  ← PRIMARY BOTTLENECK (cascade, too complex to migrate)
compact+rfill=   8.9s [ 7.4%]
wall=120.1s  coverage=120.8%
```

cascade 复杂度过高不便迁移。`cpu_postprocess` 是当前 ROI 最高的优化点。

### 关键发现

L4 GPU filter **已全量启用**（`use_gpu_filter=1`）。20s 的 `cpu_postprocess` 不含任何 filter/retrace，是**纯格式转换**：

| 每条 ray（~1.5ns × 16 OMP threads）| 操作 |
|---|---|
| pp_table[geom_id] | O(1) array lookup → shape_id, inst_id, shape_type, flip_surface |
| hitresult_to_s3d_hit | miss 检测 → 字段赋值 → mesh: UV swap + normal negate / sphere: atan2+acos |
| 写 s3d_hit | 含 shape\_\_/inst\_\_ 指针（均为 dead field，从不解引用） |

总量：12.9B rays × ~1.5ns = 19.5 core-seconds → 20s wall @ 16 OMP threads。纯内存带宽瓶颈。

---

## 当前代码结构（1a86f88 run_pool 重构后）

重构将三个入口（camera/probe/probe_batch）统一为 `run_pool()` 调度器（L4742），
内部按 `pool->num_active_views` 分派到 `pool_run_dual()` / `pool_run_single()`。

### Pipeline 函数链（Solver 层）

| 函数 | 行号 | 职责 |
|------|------|------|
| `gpu_submit_all()` | L3040 | `gpu_launch_all` + `gpu_start_d2h_all`，异步提交线程调用 |
| `gpu_launch_all()` | L2818 | H2D + OptiX kernel（filtered/legacy），设 `gpu_pending` 标志 |
| `gpu_start_d2h_all()` | L2917 | 三路异步 D2H start（RT / enc / cp） |
| `gpu_wait_d2h()` | L2962 | sync transfer_stream → **cpu_postprocess** → 累计 stats |
| `merged_pass()` | L3400 | 四阶段 OMP 主循环（distribute → cascade → collect → harvest） |

### Pipeline 函数链（oxs3d 层）

| 函数 | 行号（ox_s3d_scene_view.cpp）| 职责 |
|------|------|------|
| `batch_trace_filtered_async_impl()` | L2544 | H2D rays+filter → `traceBatchMultiHitFiltered` kernel launch |
| `batch_trace_filtered_start_d2h_impl()` | L2695 | `d_hits_filtered.downloadAsync` → `h_hits_pinned`（40B HitResult/ray） |
| `batch_trace_filtered_wait_d2h_impl()` | L2711 | sync → **OMP postprocess loop (L2768-2795)** → stats |

### CPU postprocess 热循环（**消除目标**）

`batch_trace_filtered_wait_d2h_impl()` L2768-2795：
```cpp
#pragma omp parallel for num_threads(pp_nthreads) schedule(static)
for (int ii = 0; ii < (int)count; ii++) {
    const HitResult& hr = h_hits[ii];
    if (hr.t < 0.0f) { hits[ii] = S3D_HIT_NULL; continue; }
    // pp_table O(1) lookup → shape, shape_id, inst
    hitresult_to_s3d_hit(sv, hr, shape, shape_id, hr.prim_idx, &hits[ii], inst);
}
```
启用条件：`nrays >= 256 && pp_nthreads >= 2`，可被 `STARDIS_POSTPROCESS_OMP=0` 禁用。

### Pinned Buffer 借用（Plan E 先例）

`pool_view_init()` L326-335：
```c
res_T rc = s3d_batch_trace_context_create(&pv->batch_ctx, max_rays);
s3d_batch_trace_context_get_pinned_buffers(
  pv->batch_ctx, &pv->ray_pinned, &pv->filter_pinned, NULL);
```
当前 API 暴露 ray_pinned + filter_pinned，**不含** hits。

### use_gpu_filter 设置点

三个入口均设 `pool.use_gpu_filter = 1`（L5021, L5230, L5373），
在 `gpu_launch_all` / `gpu_start_d2h_all` / `gpu_wait_d2h` 中 gate filtered vs legacy 路径。

---

## 方案设计

### Pipeline 变更

```
BEFORE: OptiX raygen → D2H(HitResult 40B) → CPU pp_table+UV+flip (20s) → s3d_hit in ray_hits
AFTER:  OptiX raygen → postprocess_kernel → D2H(s3d_hit 56B) → pv->ray_hits直读 (0s CPU)
```

### 架构：Standalone GPU Kernel + Plan E Pinned Borrow

- 新 CUDA kernel 挂在 compute_stream，紧接 OptiX raygen 之后
- 写 `s3d_hit`-compatible layout 到 device buffer → D2H 到 pinned memory
- `pv->ray_hits` 借用 pinned buffer（Plan E 先例：ray_pinned, filter_pinned）
- CPU postprocess → 0
- `use_gpu_filter=0` 时保留 legacy CPU fallback

---

## 实现步骤

### Phase 1: oxs3d 层（`oxstar-3d/0.10/`）

#### 1.1 GPU 数据结构（`ray_types.h`）

```c
/* Per-geometry postprocess entry, uploaded once at scene rebuild */
struct GpuPpEntry {          /* 12 bytes */
    uint32_t shape_id;       /* API-level shape_id → prim.geom_id */
    uint32_t inst_id;        /* inst->id or 0xFFFFFFFF → prim.inst_id */
    uint8_t  shape_type;     /* 0=mesh, 1=sphere → branch select */
    uint8_t  flip_surface;   /* For sphere UV un-flip */
    uint8_t  pad[2];
};

/* s3d_hit-compatible layout for GPU output, no void* pointers */
struct s3d_hit_raw {            /* 56 bytes, binary-compatible with s3d_hit */
    uint32_t prim_id;           /* 4 */
    uint32_t geom_id;           /* 4 */
    uint32_t inst_id;           /* 4 */
    uint32_t scene_prim_id;     /* 4 */
    uint64_t shape_pad;         /* 8  (= 0, replaces void* shape__) */
    uint64_t inst_pad;          /* 8  (= 0, replaces void* inst__) */
    float    normal[3];         /* 12 */
    float    uv[2];             /* 8 */
    float    distance;          /* 4 */
};
/* static_assert(sizeof(s3d_hit_raw) == sizeof(s3d_hit))
 * + offsetof checks for every field at compile time */
```

#### 1.2 pp_table 上传（`ox_s3d_scene_view.cpp:~592`）

场景 rebuild 时，紧接 `pp_table` vector 构建之后：
- 从 `sv->pp_table` 生成 `GpuPpEntry[]`
- 上传到 `sv->d_pp_table`（`CudaBuffer<GpuPpEntry>`）
- 典型大小：~100-1000 geoms × 12B = 1.2-12 KB（trivial）

#### 1.3 Postprocess Kernel（新文件 `postprocess.cu` 或追加到 `programs.cu`）

```
__global__ void postprocess_hits_kernel(
    const HitResult*   __restrict__ hits_in,
    s3d_hit_raw*       __restrict__ hits_out,
    const GpuPpEntry*  __restrict__ pp_table,
    uint32_t pp_table_size,
    uint32_t nrays)
{
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= nrays) return;

    const HitResult& hr = hits_in[idx];

    if (hr.t < 0.0f) {
        hits_out[idx] = S3D_HIT_RAW_NULL;    /* 精确复制 S3D_HIT_NULL 位模式 */
        return;
    }

    const GpuPpEntry& e = pp_table[hr.geom_id];   /* L2 cached, 几百个 geom */

    s3d_hit_raw out;
    out.prim_id       = hr.prim_idx;
    out.scene_prim_id = hr.prim_idx;
    out.geom_id       = e.shape_id;
    out.inst_id       = e.inst_id;
    out.shape_pad     = 0;
    out.inst_pad      = 0;
    out.distance      = hr.t;

    if (e.shape_type == 1) {  /* SPHERE */
        out.normal[0] = hr.normal[0];
        out.normal[1] = hr.normal[1];
        out.normal[2] = hr.normal[2];
        float nx = hr.normal[0], ny = hr.normal[1], nz = hr.normal[2];
        if (e.flip_surface) { nx = -nx; ny = -ny; nz = -nz; }
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }
        nz = fminf(fmaxf(nz, -1.0f), 1.0f);
        float theta = acosf(nz);
        float phi = atan2f(ny, nx);
        if (phi < 0.0f) phi += 2.0f * 3.14159265358979323846f;
        out.uv[0] = theta / 3.14159265358979323846f;
        out.uv[1] = phi / (2.0f * 3.14159265358979323846f);
    } else {  /* MESH */
        float u = hr.bary_u;
        float v = hr.bary_v;
        float w = 1.0f - u - v;
        w = fminf(fmaxf(w, 0.0f), 1.0f);
        u = fminf(fmaxf(u, 0.0f), 1.0f);
        out.uv[0] = w;
        out.uv[1] = u;
        out.normal[0] = -hr.normal[0];   /* CCW → CW */
        out.normal[1] = -hr.normal[1];
        out.normal[2] = -hr.normal[2];
    }

    hits_out[idx] = out;
}
```

#### 1.4 Buffer 分配（`ox_s3d_internal.h` — `batch_trace_context`）

在现有 `d_hits_filtered` / `h_hits_pinned` 旁新增：
- `CudaBuffer<s3d_hit_raw> d_s3d_hits`：capacity = max_rays
- `s3d_hit_raw* h_s3d_hits_pinned`：`cudaHostAlloc`，sizeof(s3d_hit_raw) × max_rays

在 `batch_trace_context_create()` 中分配，`batch_trace_context_destroy()` 中释放。
通过扩展 `s3d_batch_trace_context_get_pinned_buffers()` 暴露（新增第四参数 `out_hits`）。

#### 1.5 Pipeline 集成（`ox_s3d_scene_view.cpp`）

**`batch_trace_filtered_async_impl()` (L2544)** — OptiX launch（L2609）后追加：
```cpp
postprocess_hits_kernel<<<(nrays+255)/256, 256, 0, ctx->compute_stream>>>(
    ctx->d_hits_filtered.get(), ctx->d_s3d_hits.get(),
    sv->d_pp_table.get(), sv->pp_table.size(), count);
```

**`batch_trace_filtered_start_d2h_impl()` (L2695)** — D2H 源改为 `d_s3d_hits`（56B/ray）→ `h_s3d_hits_pinned`：
```cpp
ctx->d_s3d_hits.downloadAsync(
    ctx->h_s3d_hits_pinned, count, ctx->transfer_stream);
```

**`batch_trace_filtered_wait_d2h_impl()` (L2711)** — **删除 OMP postprocess 循环 (L2768-2795)**，
`h_s3d_hits_pinned` 已是 `s3d_hit` 格式，仅保留 sync + stats 更新 + `memcpy` 到 `hits[]`
（或直接暴露 pinned 指针，见 Phase 2）

### Phase 2: Solver 层（`stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`）

#### 2.1 Borrow pinned buffer
扩展 `get_pinned_buffers` 签名（当前：`s3d.h` L503），新增第四参数 `out_hits`。

`pool_view_init()` L326-335 改为：
```c
res_T rc = s3d_batch_trace_context_create(&pv->batch_ctx, max_rays);
s3d_batch_trace_context_get_pinned_buffers(
  pv->batch_ctx, &pv->ray_pinned, &pv->filter_pinned,
  (struct s3d_hit**)&pv->ray_hits,   /* NEW: borrow hits pinned */
  NULL);
```
移除 `pool_view_init` 中 `ray_hits` 的 `malloc` 和 `pool_view_destroy` 中对应的 `free`。

#### 2.2 distribute / step 代码零修改

`merged_pass()` (L3400) Phase A distribute_step 通过 `pv->ray_hits[batch_idx]` 读取，
格式不变（`s3d_hit`），所有 step 函数（`step_radiative_trace` / `step_conductive_ds_process` / 
`advance_one_step_with_ray`）仅使用 `prim_id`, `geom_id`, `normal`, `uv`, `distance`。

`shape__=NULL, inst__=NULL`：均为 dead field（已验证：从不解引用）。

#### 2.3 gpu_wait_d2h 简化

`gpu_wait_d2h()` (L2962) 中 RT 分支调用 `batch_trace_filtered_wait_d2h`，
该函数内部 OMP 循环已在 Phase 1 消除，此处无需修改（透明受益）。

---

## 性能模型

| 指标 | Before | After | Delta |
|------|--------|-------|-------|
| cpu_postprocess | 20.0s | 0s | **-20.0s** |
| GPU kernel 总量 | 3.4s | ~5-7s (+pp kernel) | **隐藏**：75s GPU slack |
| D2H/ray | 40B | 56B (+40%) | +~1.2s d2h_wait |
| wait_d2h 总计 | 27.2s | ~8.4s | -18.8s |
| **Wall time** | **120.1s** | **~101s** | **-16%** |

GPU slack：merged_pass 78.9s vs GPU total ~7s → 剩余 72s，GPU 代价完全吸收。

---

## s3d_hit Field Liveness（下游消费分析）

| Field | 状态 | 热路径？ | 来源 |
|-------|------|---------|------|
| **prim_id** | ALIVE | YES | material interface / enclosure lookup |
| **normal[3]** | ALIVE | YES | hit side / boundary calc |
| **distance** | ALIVE | YES | position advance / delta |
| **uv[2]** | ALIVE | moderate | realisation / boundary |
| **geom_id** | ALIVE | filter path | GPU filter data prep |
| **scene_prim_id** | ALIVE | rare | fallback CP path |
| **inst_id** | DEAD | — | zero reads | 
| **shape\_\_** | DEAD | — | zero post-GPU dereferences |
| **inst\_\_** | DEAD | — | zero reads |

---

## 风险

| 风险 | 等级 | 缓解 |
|------|------|------|
| `s3d_hit_raw` / `s3d_hit` 布局不匹配 | 低 | 编译期 `static_assert` + `offsetof` |
| `S3D_HIT_NULL` 位模式不一致 | 低 | 查定义，GPU 精确复制 |
| pp_table 场景更新后过期 | 低 | 与 pp_table vector 同步上传 |
| pinned 缓冲区生命周期竞争 | 低 | Plan E 已验证模式 |
| `use_gpu_filter=0` 路径 | 低 | 保留 legacy CPU fallback |
| 411K kernel launch overhead (~2s) | 中 | 隐藏在 GPU slack；后续可 fuse 进 raygen |

---

## 决策记录

1. **Standalone kernel**（不 fuse 进 raygen）— 更安全、易验证，GPU slack 充足
2. **D2H 56B/ray**（+40% vs 40B HitResult）— 1.2s 带宽换 20s CPU，收益确定
3. **Plan E borrow pinned → pv->ray_hits** — 已有先例，零拷贝
4. **保留 legacy fallback** — `use_gpu_filter=0` 时走旧路径

---

## 后续方向

1. **Fuse into raygen** — 如 launch overhead 成为可见瓶颈，合并到 `__raygen__mh_filtered()` 消除额外 launch
2. **GPU-side slot reorder** — 如果 distribute scatter 访问成为瓶颈，GPU 按 slot 顺序重排输出（需上传 `ray_to_slot` 映射）
3. **inst_id / shape\_\_ 清理** — dead field 可从 `s3d_hit` 移除以缩小结构体

## 涉及文件（精确行号基于 1a86f88）

| 文件 | 操作 | 关键行号 |
|------|------|----------|
| `oxstar-3d/0.10/include/ray_types.h` | 新增 `GpuPpEntry`, `s3d_hit_raw` | — |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | pp_table 上传, kernel enqueue, D2H 改源, 删除 OMP pp loop | L592(上传), L2609(enqueue后), L2695(D2H源), L2768-2795(删除) |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h` | `batch_trace_context` 新增 `d_s3d_hits` + `h_s3d_hits_pinned`; `s3d_scene_view` 新增 `d_pp_table` | L277-360(ctx), L258(pp_entry) |
| `oxstar-3d/0.10/s3d_wrapper/s3d.h` | 扩展 `get_pinned_buffers` 签名（+out_hits） | L503 |
| `oxstar-3d/0.10/device/programs.cu` 或新 `postprocess.cu` | `postprocess_hits_kernel` 实现 | — |
| `stardis-solver/.../sdis_solve_persistent_wavefront.c` | `pool_view_init` borrow ray_hits, 移除 malloc/free | L326-335(borrow) |

---

*创建: 2026-03-14*  
*更新: 2026-03-15 — 同步 1a86f88 run_pool 重构后的代码结构*
