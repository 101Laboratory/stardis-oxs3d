# Phase 2: 双缓冲基础设施

**前置依赖**: Phase 1 (submit/wait 拆分完成)  
**预计工时**: 1-2 天  
**验证方式**: 编译通过 + 串行模式 ctest 全通过

---

## 一、目标

为 `wavefront_pool` 添加双缓冲支持，使同一时刻可以有两套独立的 ray I/O 缓冲——一套供 GPU 执行中的 inflight 操作使用，另一套供 CPU 准备下一轮数据使用。

## 二、核心概念

```
buf_curr (CPU正在写入):
  ┌──────────────────────────────────┐
  │ ray_requests[buf_curr][]         │ ← compact + collect 写入
  │ ray_to_slot[buf_curr][]          │
  │ ray_slot_sub[buf_curr][]         │
  │ ray_count[buf_curr]              │
  │ bucket_offsets[buf_curr][]       │
  │ bucket_counts[buf_curr][]        │
  │ batch_ctx[buf_curr]              │ ← submit 使用
  └──────────────────────────────────┘

buf_prev (GPU inflight / 等待处理):
  ┌──────────────────────────────────┐
  │ ray_hits 在 batch_ctx[buf_prev]  │ ← GPU 正在写入 / wait 读取
  │ ray_count[buf_prev]              │ ← distribute 使用的光线数
  │ ray_to_slot[buf_prev][]          │ ← distribute 需要
  │ ray_slot_sub[buf_prev][]         │ ← distribute 需要
  │ bucket_offsets[buf_prev][]       │ ← distribute 需要
  └──────────────────────────────────┘

slots[] — 始终单份，同一时刻只有一个 distribute 或 cascade 操作
```

## 三、结构体修改

### 3.1 wavefront_pool 修改

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h`

#### 当前结构 (单缓冲，相关字段)

```c
struct wavefront_pool {
    // ...
    struct s3d_ray_request* ray_requests;    /* [max_rays] */
    uint32_t*           ray_to_slot;         /* [max_rays] */
    uint32_t*           ray_slot_sub;        /* [max_rays] */
    struct s3d_hit*     ray_hits;            /* [max_rays] */
    size_t              ray_count;
    size_t              max_rays;
    
    size_t              bucket_offsets[RAY_BUCKET_COUNT + 1];
    size_t              bucket_counts[RAY_BUCKET_COUNT];
    
    struct s3d_batch_trace_context* batch_ctx;
    // ...
};
```

#### 改造后结构 (双缓冲)

```c
/* --- 双缓冲常量 --- */
#define WF_NBUF 2

struct wavefront_pool {
    // ... 所有非 ray I/O 字段保持不变 ...
    
    /* === 双缓冲 Ray I/O (Phase 2) === */
    struct s3d_ray_request* ray_requests[WF_NBUF];    /* [max_rays] × 2 */
    uint32_t*           ray_to_slot[WF_NBUF];         /* [max_rays] × 2 */
    uint32_t*           ray_slot_sub[WF_NBUF];        /* [max_rays] × 2 */
    size_t              ray_count[WF_NBUF];
    size_t              max_rays;                      /* 不变，共享容量 */
    
    size_t              bucket_offsets[WF_NBUF][RAY_BUCKET_COUNT + 1];
    size_t              bucket_counts[WF_NBUF][RAY_BUCKET_COUNT];
    
    struct s3d_batch_trace_context* batch_ctx[WF_NBUF];
    
    /* ray_hits 不需要双缓冲 — 结果存在 batch_ctx 内部的
     * h_multi_results 中，distribute 从 batch_ctx[buf_prev] 读取，
     * 经 CPU Top-K filter 后直接写入 path_state。
     * 但 distribute 需要通过 ray_to_slot[buf_prev] 索引，
     * 所以 ray_to_slot/ray_slot_sub 必须双缓冲。
     *
     * ray_hits[] 本身在 Phase 1 改造后不再作为中间缓冲 —
     * wait() 直接输出到调用者提供的 hits[]。
     * 但当前的 distribute 仍从 pool->ray_hits 读取。
     * → 需要双缓冲 ray_hits 或改变 distribute 的数据源。
     */
    struct s3d_hit*     ray_hits[WF_NBUF];             /* [max_rays] × 2 */
    
    /* === 双缓冲控制 === */
    int                 buf_curr;      /* 当前写入缓冲索引 (0 or 1) */
    int                 buf_prev;      /* 上一轮 GPU 结果缓冲索引 */
    int                 pipeline_active; /* GPU 是否有 inflight 工作 */
    
    // ... 其余字段不变 ...
};
```

### 3.2 不需要双缓冲的字段

以下字段仍保持单份，原因：

| 字段 | 原因 |
|------|------|
| `slots[]` | 路径状态，同一时刻只有一个操作在修改（distribute OR cascade，不会并发） |
| `active_indices[]` | compact 结果，在同一 CPU 阶段内使用 |
| `need_ray_indices[]` | 同上 |
| `done_indices[]` | 同上 |
| `bucket_radiative[]` / `bucket_conductive[]` | distribute 使用，紧跟在 compact 后，单份即可 |
| `enc_locate_*` | ENC 查询在 distribute 后同步执行 |
| `task_queue[]` | 全局任务队列，只有 refill 写 `task_next` |

### 3.3 ENC 双缓冲 (预留)

当前 ENC 查询仍同步执行（Phase 1.5 桩实现），不需要双缓冲。预留字段：

```c
    /* === ENC 双缓冲 (预留, Phase 2 不实施) === */
    /* struct s3d_batch_enc_context* enc_batch_ctx[WF_NBUF]; */
    /* struct s3d_enc_locate_request* enc_locate_requests[WF_NBUF]; */
    /* struct s3d_enc_locate_result*  enc_locate_results[WF_NBUF]; */
    /* uint32_t* enc_locate_to_slot[WF_NBUF]; */
    /* size_t    enc_locate_count[WF_NBUF]; */
```

---

## 四、pool_create / pool_destroy 修改

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

### 4.1 pool_create() 修改

**当前** (L85-145，单缓冲分配)：

```c
pool->ray_requests = (struct s3d_ray_request*)malloc(max_rays * sizeof(struct s3d_ray_request));
pool->ray_to_slot  = (uint32_t*)malloc(max_rays * sizeof(uint32_t));
pool->ray_slot_sub = (uint32_t*)malloc(max_rays * sizeof(uint32_t));
pool->ray_hits     = (struct s3d_hit*)malloc(max_rays * sizeof(struct s3d_hit));
```

**改造后** (双缓冲分配)：

```c
for (int b = 0; b < WF_NBUF; b++) {
    pool->ray_requests[b] = (struct s3d_ray_request*)malloc(max_rays * sizeof(struct s3d_ray_request));
    pool->ray_to_slot[b]  = (uint32_t*)malloc(max_rays * sizeof(uint32_t));
    pool->ray_slot_sub[b] = (uint32_t*)malloc(max_rays * sizeof(uint32_t));
    pool->ray_hits[b]     = (struct s3d_hit*)malloc(max_rays * sizeof(struct s3d_hit));
    
    if (!pool->ray_requests[b] || !pool->ray_to_slot[b] || 
        !pool->ray_slot_sub[b] || !pool->ray_hits[b]) {
        /* 清理已分配的缓冲 ... */
        return RES_MEM_ERR;
    }
    
    pool->ray_count[b] = 0;
}
```

### 4.2 batch_ctx 双缓冲创建

**当前** (L1494):

```c
res = s3d_batch_trace_context_create(&pool.batch_ctx, pool.max_rays);
```

**改造后**:

```c
for (int b = 0; b < WF_NBUF; b++) {
    res = s3d_batch_trace_context_create(&pool.batch_ctx[b], pool.max_rays);
    if (res != RES_OK) {
        /* 清理 ... */
        return res;
    }
}
```

### 4.3 pool_destroy() 修改

**当前** (L147-193):

```c
s3d_batch_trace_context_destroy(pool->batch_ctx);
free(pool->ray_requests);
free(pool->ray_to_slot);
free(pool->ray_slot_sub);
free(pool->ray_hits);
```

**改造后**:

```c
for (int b = 0; b < WF_NBUF; b++) {
    s3d_batch_trace_context_destroy(pool->batch_ctx[b]);
    free(pool->ray_requests[b]);
    free(pool->ray_to_slot[b]);
    free(pool->ray_slot_sub[b]);
    free(pool->ray_hits[b]);
}
```

### 4.4 初始化控制变量

```c
pool->buf_curr = 0;
pool->buf_prev = 1;
pool->pipeline_active = 0;
```

---

## 五、引用更新 — 所有使用 ray I/O 的函数

双缓冲后，所有访问 `pool->ray_requests` 等的代码需要改为 `pool->ray_requests[buf]`。

### 5.1 需要修改的函数

| # | 函数 | 行号 | 当前引用 | 改为 | buf 来源 |
|---|------|------|---------|------|---------|
| 1 | `pool_collect_ray_requests_bucketed()` | L652-797 | `pool->ray_requests` | `pool->ray_requests[pool->buf_curr]` | 当前写入缓冲 |
| 2 | `pool_collect_ray_requests_bucketed()` | L652-797 | `pool->ray_to_slot` | `pool->ray_to_slot[pool->buf_curr]` | 同上 |
| 3 | `pool_collect_ray_requests_bucketed()` | L652-797 | `pool->ray_slot_sub` | `pool->ray_slot_sub[pool->buf_curr]` | 同上 |
| 4 | `pool_collect_ray_requests_bucketed()` | L652-797 | `pool->bucket_offsets` | `pool->bucket_offsets[pool->buf_curr]` | 同上 |
| 5 | `pool_collect_ray_requests_bucketed()` | L652-797 | `pool->bucket_counts` | `pool->bucket_counts[pool->buf_curr]` | 同上 |
| 6 | `pool_collect_ray_requests_bucketed()` | L796 | `pool->ray_count = ...` | `pool->ray_count[pool->buf_curr] = ...` | 同上 |
| 7 | `pool_distribute_ray_results()` | L846-955 | `pool->ray_hits` | `pool->ray_hits[pool->buf_prev]` | 上一轮结果 |
| 8 | `pool_distribute_ray_results()` | L846-955 | `pool->ray_to_slot` (读取) | `pool->ray_to_slot[pool->buf_prev]` | 上一轮映射 |
| 9 | `pool_distribute_ray_results()` | L846-955 | `pool->bucket_offsets` | `pool->bucket_offsets[pool->buf_prev]` | 上一轮桶偏移 |
| 10 | 主循环 Step C | L1553-1577 | `pool->ray_requests`, `pool->ray_count`, `pool->ray_hits`, `pool->batch_ctx` | 对应 `[buf_curr]` | 当前轮 |

### 5.2 重构模式

为了最小化改动，引入局部别名：

```c
/* 在 pool_collect_ray_requests_bucketed() 开头 */
const int buf = pool->buf_curr;
struct s3d_ray_request* requests = pool->ray_requests[buf];
uint32_t* to_slot  = pool->ray_to_slot[buf];
uint32_t* slot_sub = pool->ray_slot_sub[buf];
size_t* offsets    = pool->bucket_offsets[buf];
size_t* counts     = pool->bucket_counts[buf];
```

```c
/* 在 pool_distribute_ray_results() 开头 */
const int buf = pool->buf_prev;
const struct s3d_hit* hits   = pool->ray_hits[buf];
const uint32_t* to_slot      = pool->ray_to_slot[buf];
const uint32_t* slot_sub     = pool->ray_slot_sub[buf];
const size_t* offsets        = pool->bucket_offsets[buf];
```

### 5.3 buf_flip() 工具函数

```c
static INLINE void pool_flip_buffers(struct wavefront_pool* pool)
{
    int tmp = pool->buf_curr;
    pool->buf_curr = pool->buf_prev;
    pool->buf_prev = tmp;
}
```

或更简单的异或翻转：

```c
static INLINE void pool_flip_buffers(struct wavefront_pool* pool)
{
    pool->buf_curr ^= 1;
    pool->buf_prev ^= 1;
}
```

---

## 六、内存增量估算

以 pool_size=4096, max_rays=24576 计算：

| 缓冲 | 单份大小 | 双缓冲增量 |
|------|---------|-----------|
| `ray_requests[]` | 24576 × `sizeof(s3d_ray_request)` ≈ 24576 × 64B = **1.5MB** | +1.5MB |
| `ray_to_slot[]` | 24576 × 4B = **96KB** | +96KB |
| `ray_slot_sub[]` | 24576 × 4B = **96KB** | +96KB |
| `ray_hits[]` | 24576 × `sizeof(s3d_hit)` ≈ 24576 × 48B = **1.1MB** | +1.1MB |
| `bucket_offsets[]` | 小 (~40B) | 可忽略 |
| `bucket_counts[]` | 小 (~32B) | 可忽略 |
| `batch_ctx` (含 GPU 缓冲) | ~15MB (SoA GPU buffers + host results) | **+15MB** |
| **CPU 小计** | | **+2.8MB** |
| **GPU 小计** | | **+15MB** |
| **总计** | | **≈ +18MB** |

> pool_size=32768 时总增量 ≈ +38MB。RTX 4090 (24GB) 完全可承受。

---

## 七、兼容性设计 — 串行模式

双缓冲基础设施不改变执行语义。在串行模式 (Phase 3 未实施或 `STARDIS_PIPELINE=0`) 下：

```c
/* 串行模式: 始终使用 buf_curr=0, buf_prev=0 */
pool->buf_curr = 0;
pool->buf_prev = 0;   /* 指向同一缓冲 */
pool->pipeline_active = 0;
```

此时所有函数读写 `[0]` 缓冲，行为与改造前完全一致。

**仅当 Phase 3 启用流水线时**，才设置 `buf_curr=0, buf_prev=1` 并在每轮 flip。

---

## 八、文件修改清单

| # | 文件 | 行号范围 | 修改内容 | 影响函数 |
|---|------|---------|---------|---------|
| 1 | `sdis_solve_persistent_wavefront.h` | L130-145 | ray I/O 字段改为 `[WF_NBUF]` 数组 + 新增控制变量 | 所有引用 pool 的代码 |
| 2 | `sdis_solve_persistent_wavefront.c` | L85-145 | `pool_create()`: 双缓冲分配 | `pool_create` |
| 3 | `sdis_solve_persistent_wavefront.c` | L147-193 | `pool_destroy()`: 双缓冲释放 | `pool_destroy` |
| 4 | `sdis_solve_persistent_wavefront.c` | L652-797 | `pool_collect_ray_requests_bucketed()`: 引用 `[buf_curr]` | `pool_collect_*` |
| 5 | `sdis_solve_persistent_wavefront.c` | L846-955 | `pool_distribute_ray_results()`: 引用 `[buf_prev]` | `pool_distribute_*` |
| 6 | `sdis_solve_persistent_wavefront.c` | L1494 | batch_ctx 创建: `[WF_NBUF]` 循环 | `solve_camera_*` |
| 7 | `sdis_solve_persistent_wavefront.c` | L1553-L1577 | 主循环 Step C: batch_ctx/ray_requests/ray_hits 索引 | 主循环 |
| 8 | `sdis_solve_persistent_wavefront.c` | 新增 | `pool_flip_buffers()` 工具函数 | 新增 |

**预计修改: ~150 行** (主要是引用替换)

---

## 九、实施检查点

```
Step 2.1: 修改 wavefront_pool 结构体
  └─ ✅ 编译通过（此步暂不修改使用方）

Step 2.2: 修改 pool_create / pool_destroy
  └─ ✅ 双缓冲分配/释放正确

Step 2.3: 修改所有引用函数 (collect, distribute, 主循环)
  ├─ 串行模式: buf_curr = buf_prev = 0
  └─ ✅ ctest 全通过, 结果 bit-exact

Step 2.4: 新增 pool_flip_buffers()
  └─ Phase 3 调用，Phase 2 仅定义

Step 2.5: 验证串行模式不中断
  ├─ 运行 porous 320×320 spp=32
  ├─ 确认输出与改造前 bit-exact
  └─ ✅ 检查点: Phase 2 完成
```

---

## 十、风险与注意事项

| 风险 | 缓解 |
|------|------|
| `distribute` 读取 `buf_prev`，`collect` 写入 `buf_curr` — 串行模式下两者相同 | Phase 2 保持 `buf_curr = buf_prev = 0`，Phase 3 才分离 |
| `path_state.ray_req.batch_idx` 存储的是全局索引 | `batch_idx` 在 collect 时设置，在 distribute 时读取 — 同一轮内一致，双缓冲不影响 |
| `bucket_radiative[]` / `bucket_conductive[]` 在 compact 和 distribute 间共享 | 这些不参与 GPU I/O，不需要双缓冲 |
| 双缓冲增加了 pool 的内存占用 | 对 pool=4096 仅增 ~18MB，远低于 VRAM 容量 |
| `ray_hits[WF_NBUF]` 是否真正需要 | 是 — distribute 从 `ray_hits[buf_prev]` 读取，而 wait 写入 `ray_hits[buf_prev]`，所以 submit/collect 同时操作 `ray_hits[buf_curr]` 不冲突 |

---

*Phase 3 详见 → [03_phase3_pipeline_loop.md](03_phase3_pipeline_loop.md)*
