# Phase B-1 实现指南：custar-3d 批量追踪封装层

**生成时间**: 2026-02-09  
**前置文档**: [batch_api_feasibility.md](batch_api_feasibility.md)  
**范围**: custar-3d 0.10 内部新增真正的批量追踪API  
**修改文件数**: 4个（1个新文件 + 3个修改）  
**预计代码量**: ~500行

---

## 一、目标

在 custar-3d 内部创建一个真正的批量射线追踪封装 `s3d_scene_view_trace_rays_batch`，它：

1. 接收 N 条射线请求（含可选 filter_data）
2. 使用 `cus3d_trace_ray_batch`（`<<<grid, 256>>>`）一次性批量追踪
3. CPU 后处理：`cus3d_hit_to_s3d_hit` + `trace_hit_fixup`
4. 对 filter 拒绝的射线，fallback 到 `cus3d_trace_ray_single_multi`（Top-K）
5. 输出标准 `s3d_hit` 数组

**不修改任何现有函数签名**，仅新增API。Phase B-2（solver wavefront）将调用此API。

---

## 二、新增数据结构

### 2.1 `s3d_ray_request` — 射线请求描述符

```c
/* 定义位置: s3d.h 或新头文件 s3d_batch.h */

struct s3d_ray_request {
    float    origin[3];       /* 射线起点 */
    float    direction[3];    /* 射线方向（必须归一化） */
    float    range[2];        /* [tmin, tmax) */
    void*    filter_data;     /* NULL = 无filter; 非NULL = hit_filter_data* */
    uint32_t user_id;         /* 调用者标识（用于结果分发） */
};
```

**设计决策**：
- `filter_data` 作为 `void*` 保持与现有 `s3d_hit_filter_function_T` 签名兼容
- `user_id` 由调用者设置，用于将结果映射回对应的路径状态（Phase B-2使用）
- 结构体 36 字节（不含 padding），紧凑且缓存友好

### 2.2 `s3d_batch_trace_context` — 批量追踪上下文（内部）

```c
/* 定义位置: s3d_scene_view_batch_trace.cpp（文件内部static） */

struct s3d_batch_trace_context {
    struct cus3d_ray_batch  gpu_batch;    /* 预分配的GPU射线缓冲区 */
    struct cus3d_hit_result* h_results;   /* 主机端结果缓冲区 */
    size_t  max_rays;                     /* 预分配容量 */
    int     initialized;                  /* 是否已初始化 */
};
```

### 2.3 `s3d_batch_trace_stats` — 追踪统计（可选，调试用）

```c
struct s3d_batch_trace_stats {
    size_t  total_rays;           /* 总射线数 */
    size_t  batch_accepted;       /* batch直接接受的数量 */
    size_t  filter_rejected;      /* filter拒绝需要re-trace的数量 */
    size_t  retrace_accepted;     /* re-trace后接受的数量 */
    size_t  retrace_missed;       /* re-trace后仍miss的数量 */
    double  batch_time_ms;        /* GPU batch耗时 */
    double  postprocess_time_ms;  /* CPU后处理耗时 */
    double  retrace_time_ms;      /* re-trace耗时 */
};
```

---

## 三、新增API签名

### 3.1 主API — `s3d_scene_view_trace_rays_batch`

```c
/* 声明位置: s3d.h（追加到 s3d_scene_view_trace_rays 声明之后） */

/**
 * @brief 批量射线追踪 — 真正利用GPU并行度
 *
 * 与 s3d_scene_view_trace_rays 不同，本函数将所有射线打包为一次GPU batch调用，
 * 然后在CPU侧进行UV/法线修正和filter处理。
 *
 * @param scnview   场景视图（必须设置 S3D_TRACE 标志）
 * @param requests  射线请求数组
 * @param nrays     射线数量
 * @param hits      输出命中结果数组（调用者分配，至少nrays个元素）
 * @param stats     可选统计输出（NULL则不统计）
 * @return RES_OK 成功, RES_ERR 失败
 */
S3D_API res_T
s3d_scene_view_trace_rays_batch
  (struct s3d_scene_view* scnview,
   const struct s3d_ray_request* requests,
   size_t nrays,
   struct s3d_hit* hits,
   struct s3d_batch_trace_stats* stats);
```

### 3.2 上下文管理API（可选，用于预分配避免重复malloc）

```c
/**
 * @brief 创建批量追踪上下文（预分配GPU缓冲区）
 *
 * 如果不使用context，trace_rays_batch 内部会自动创建临时缓冲区。
 * 使用context可避免每次调用的分配开销。
 *
 * @param ctx       输出上下文指针
 * @param max_rays  最大射线数（预分配容量）
 * @return RES_OK 成功
 */
S3D_API res_T
s3d_batch_trace_context_create
  (struct s3d_batch_trace_context** ctx,
   size_t max_rays);

S3D_API void
s3d_batch_trace_context_destroy
  (struct s3d_batch_trace_context* ctx);

/**
 * @brief 使用预分配上下文的批量追踪
 */
S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx
  (struct s3d_scene_view* scnview,
   struct s3d_batch_trace_context* ctx,
   const struct s3d_ray_request* requests,
   size_t nrays,
   struct s3d_hit* hits,
   struct s3d_batch_trace_stats* stats);
```

---

## 四、实现步骤

### Step 1: 在 `s3d.h` 中添加新类型和函数声明

**修改文件**: `custar-3d/0.10/src/s3d.h`  
**插入位置**: 在现有 `s3d_scene_view_trace_rays` 声明（约L358）之后

```c
/* ---- Batch Ray Tracing (GPU-accelerated) ---- */

struct s3d_ray_request {
    float    origin[3];
    float    direction[3];
    float    range[2];
    void*    filter_data;     /* NULL = no filter */
    uint32_t user_id;
};

struct s3d_batch_trace_stats {
    size_t  total_rays;
    size_t  batch_accepted;
    size_t  filter_rejected;
    size_t  retrace_accepted;
    size_t  retrace_missed;
    double  batch_time_ms;
    double  postprocess_time_ms;
    double  retrace_time_ms;
};

struct s3d_batch_trace_context;

S3D_API res_T
s3d_scene_view_trace_rays_batch
  (struct s3d_scene_view* scnview,
   const struct s3d_ray_request* requests,
   size_t nrays,
   struct s3d_hit* hits,
   struct s3d_batch_trace_stats* stats);

S3D_API res_T
s3d_batch_trace_context_create
  (struct s3d_batch_trace_context** ctx,
   size_t max_rays);

S3D_API void
s3d_batch_trace_context_destroy
  (struct s3d_batch_trace_context* ctx);

S3D_API res_T
s3d_scene_view_trace_rays_batch_ctx
  (struct s3d_scene_view* scnview,
   struct s3d_batch_trace_context* ctx,
   const struct s3d_ray_request* requests,
   size_t nrays,
   struct s3d_hit* hits,
   struct s3d_batch_trace_stats* stats);
```

**注意事项**:
- `struct s3d_batch_trace_context` 在此处仅做前向声明
- `S3D_API` 宏处理动态库导出（已有定义）
- 保持与现有代码风格一致（C89兼容签名）

---

### Step 2: 创建 `s3d_scene_view_batch_trace.cpp`

**新文件**: `custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp`

#### 2.1 文件头和includes

```cpp
/**
 * @file s3d_scene_view_batch_trace.cpp
 * @brief GPU-accelerated batch ray tracing with CPU post-processing
 *
 * Wraps cus3d_trace_ray_batch (<<<grid, 256>>>) and adds:
 *   - cus3d_hit_to_s3d_hit conversion
 *   - trace_hit_fixup (UV/normal convention transform)
 *   - CPU-side filter evaluation
 *   - Selective re-trace via cus3d_trace_ray_single_multi for rejected rays
 */

#include "s3d.h"
#include "s3d_c.h"
#include "s3d_device_c.h"
#include "s3d_scene_view_c.h"
#include "cus3d_trace.h"
#include "cus3d_prim.h"
#include "cus3d_geom_store.h"
#include "cus3d_bvh.h"

#include <rsys/float3.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>  /* QueryPerformanceCounter for timing */
#else
#include <time.h>
#endif
```

#### 2.2 内部辅助：精确计时器

```cpp
static double get_time_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart * 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}
```

#### 2.3 `trace_hit_fixup` — 提取为共享代码【已完成】

```cpp
/*
 * 此函数已提取到共享头文件 `cus3d_trace_util.h`。
 *
 * 原始位置: s3d_scene_view_trace_ray.cpp L62-L92
 * 当前用途: s3d_scene_view_trace_ray.cpp, s3d_scene_view_batch_trace.cpp
 */
#include "cus3d_trace_util.h"
```


#### 2.4 `s3d_batch_trace_context` 实现

```cpp
struct s3d_batch_trace_context {
    struct cus3d_ray_batch  gpu_batch;
    struct cus3d_hit_result* h_results;
    size_t  max_rays;
    int     initialized;
};

res_T
s3d_batch_trace_context_create
  (struct s3d_batch_trace_context** out_ctx,
   size_t max_rays)
{
    struct s3d_batch_trace_context* ctx;

    if (!out_ctx) return RES_BAD_ARG;
    *out_ctx = NULL;

    ctx = (struct s3d_batch_trace_context*)calloc(1, sizeof(*ctx));
    if (!ctx) return RES_MEM_ERR;

    res_T res = cus3d_ray_batch_create(&ctx->gpu_batch, max_rays);
    if (res != RES_OK) {
        free(ctx);
        return res;
    }

    ctx->h_results = (struct cus3d_hit_result*)malloc(
        max_rays * sizeof(struct cus3d_hit_result));
    if (!ctx->h_results) {
        cus3d_ray_batch_destroy(&ctx->gpu_batch);
        free(ctx);
        return RES_MEM_ERR;
    }

    ctx->max_rays    = max_rays;
    ctx->initialized = 1;
    *out_ctx = ctx;
    return RES_OK;
}

void
s3d_batch_trace_context_destroy
  (struct s3d_batch_trace_context* ctx)
{
    if (!ctx) return;
    cus3d_ray_batch_destroy(&ctx->gpu_batch);
    free(ctx->h_results);
    free(ctx);
}
```

#### 2.5 核心实现 — `trace_rays_batch_impl`

```cpp
/*
 * 核心批量追踪实现。
 *
 * 流程：
 *   1. 将 s3d_ray_request[] 打包为 float3/float2 staging 缓冲区，
 *      通过 gpu_buffer_*_upload 上传到 cus3d_ray_batch 预分配的GPU缓冲区
 *   2. 调用 cus3d_trace_ray_batch → cus3d_hit_result[]（nearest hit）
 *   3. CPU 逐条后处理:
 *      a. cus3d_hit_to_s3d_hit（GPU命中 → s3d格式，含 primitive/instance 解析）
 *      b. trace_hit_fixup（Moller-Trumbore UV → s3d barycentric，三角形法线取反）
 *      c. 检查 filter_func — 通过则接受，拒绝则标记需要 re-trace
 *   4. 对被 filter 拒绝的射线，逐条 fallback 到 s3d_scene_view_trace_ray
 *      （Top-K multi-hit + 递归 filter），保证与原始逐条追踪完全一致
 *
 * 使用 needs_retrace[] 位数组显式跟踪被拒绝的射线，
 * 避免 Step 3 与 Step 4 之间的状态耦合和重复 filter 评估。
 */
static res_T
trace_rays_batch_impl
  (struct s3d_scene_view* view,
   struct cus3d_ray_batch* gpu_batch,
   struct cus3d_hit_result* h_results,
   const struct s3d_ray_request* requests,
   size_t nrays,
   struct s3d_hit* hits,
   struct s3d_batch_trace_stats* stats)
{
    struct cus3d_geom_store* store = view->geom_store;
    struct cus3d_bvh* bvh = view->bvh;
    struct s3d_device* dev_s3d = view->scn->dev;
    struct cus3d_device* dev = dev_s3d->gpu;
    res_T res;
    size_t i;
    double t0, t1;

    size_t stat_accepted = 0, stat_rejected = 0;
    size_t stat_retrace_ok = 0, stat_retrace_miss = 0;

    /* 拒绝标记数组 — calloc 零初始化，0=无需re-trace，1=需要 */
    uint8_t* needs_retrace = (uint8_t*)calloc(nrays, 1);
    if (!needs_retrace) return RES_MEM_ERR;

    /* =================================================================
     * Step 1: 填充 GPU 射线缓冲区
     *
     * cus3d_ray_batch 的 d_origins/d_directions/d_ranges 是 SoA 布局
     * 的 GPU buffer（float3[], float3[], float2[]）。
     * s3d_ray_request 是 AoS 布局，需要转置到 staging 缓冲区后上传。
     * ================================================================= */
    t0 = stats ? get_time_ms() : 0;
    {
        float3* h_origins    = (float3*)malloc(nrays * sizeof(float3));
        float3* h_directions = (float3*)malloc(nrays * sizeof(float3));
        float2* h_ranges     = (float2*)malloc(nrays * sizeof(float2));

        if (!h_origins || !h_directions || !h_ranges) {
            free(h_origins); free(h_directions); free(h_ranges);
            free(needs_retrace);
            return RES_MEM_ERR;
        }

        for (i = 0; i < nrays; i++) {
            h_origins[i]    = make_float3(requests[i].origin[0],
                                          requests[i].origin[1],
                                          requests[i].origin[2]);
            h_directions[i] = make_float3(requests[i].direction[0],
                                          requests[i].direction[1],
                                          requests[i].direction[2]);
            h_ranges[i]     = make_float2(requests[i].range[0],
                                          requests[i].range[1]);
        }

        cudaStream_t s = dev->stream;
        gpu_buffer_float3_upload(&gpu_batch->d_origins, h_origins, nrays, s);
        gpu_buffer_float3_upload(&gpu_batch->d_directions, h_directions, nrays, s);
        gpu_buffer_float2_upload(&gpu_batch->d_ranges, h_ranges, nrays, s);
        cudaStreamSynchronize(s);

        free(h_origins);
        free(h_directions);
        free(h_ranges);
    }
    gpu_batch->count = nrays;

    /* =================================================================
     * Step 2: GPU 批量追踪
     *
     * 内部使用 <<<(nrays+255)/256, 256>>> 启动 kernel。
     * 根据 bvh->tlas_valid 自动选择单级/两级(instanced) kernel。
     * 返回 cus3d_hit_result[nrays] — 每条射线的 nearest hit。
     * 注意: 不含 filter，nearest hit 可能是自交 — 由 Step 3 判定。
     * ================================================================= */
    res = cus3d_trace_ray_batch(bvh, store, dev, gpu_batch, h_results);
    if (res != RES_OK) { free(needs_retrace); return res; }

    t1 = stats ? get_time_ms() : 0;
    if (stats) stats->batch_time_ms = t1 - t0;

    /* =================================================================
     * Step 3: CPU 后处理 — 类型转换 + fixup + filter 评估
     *
     * 三种结果:
     *   (1) gpu miss (prim_id < 0)      → 输出 S3D_HIT_NULL，计入 accepted
     *   (2) hit 通过 filter（或无filter）→ 输出转换后的 s3d_hit，计入 accepted
     *   (3) hit 被 filter 拒绝           → 标记 needs_retrace[i]=1，计入 rejected
     * ================================================================= */
    t0 = stats ? get_time_ms() : 0;

    for (i = 0; i < nrays; i++) {
        const struct cus3d_hit_result* gpu_hit = &h_results[i];

        /* --- Miss --- */
        if (gpu_hit->prim_id < 0) {
            hits[i] = S3D_HIT_NULL;
            stat_accepted++;
            continue;
        }

        /* --- 解析几何存储（处理 instanced hit） --- */
        const struct cus3d_geom_store* resolved_store = store;
        if (gpu_hit->inst_id >= 0) {
            const struct cus3d_geom_store* cs =
                cus3d_bvh_get_instance_store(bvh, (unsigned)gpu_hit->inst_id);
            if (cs) resolved_store = cs;
        }

        /* --- (a) 类型转换: cus3d_hit_result → s3d_hit --- */
        cus3d_hit_to_s3d_hit(resolved_store, bvh, gpu_hit, &hits[i]);

        /* --- (b) UV/法线约定修正 --- */
        const struct geom_entry* ge =
            &resolved_store->entries[gpu_hit->geom_idx];
        trace_hit_fixup(&hits[i], ge);

        /* --- (c) Filter 检查 --- */
        if (requests[i].filter_data != NULL && ge->filter_func != NULL) {
            int rejected = ge->filter_func(
                &hits[i],
                requests[i].origin,
                requests[i].direction,
                requests[i].range,
                requests[i].filter_data,  /* query_data (hit_filter_data*) */
                ge->filter_data);         /* 几何体注册时设置的 global_data */

            if (rejected) {
                needs_retrace[i] = 1;
                stat_rejected++;
                continue;
            }
        }

        stat_accepted++;
    }

    t1 = stats ? get_time_ms() : 0;
    if (stats) stats->postprocess_time_ms = t1 - t0;

    /* =================================================================
     * Step 4: 选择性 Re-trace — 对 filter 拒绝的射线回退到 Top-K
     *
     * 使用现有的 s3d_scene_view_trace_ray（内部调用
     * cus3d_trace_ray_single_multi, TOPK=4, MAX_FALLBACK_DEPTH=4），
     * 保证与原始逐条追踪完全一致的结果。
     *
     * 预期 re-trace 比例: <10%（参见 §5.2 分析）
     * ================================================================= */
    t0 = stats ? get_time_ms() : 0;

    for (i = 0; i < nrays; i++) {
        if (!needs_retrace[i]) continue;

        res = s3d_scene_view_trace_ray(
            view,
            requests[i].origin,
            requests[i].direction,
            requests[i].range,
            requests[i].filter_data,
            &hits[i]);

        if (res != RES_OK) {
            free(needs_retrace);
            return res;
        }

        if (S3D_HIT_NONE(&hits[i]))
            stat_retrace_miss++;
        else
            stat_retrace_ok++;
    }

    t1 = stats ? get_time_ms() : 0;
    if (stats) stats->retrace_time_ms = t1 - t0;

    /* ---------- 填充统计 ---------- */
    if (stats) {
        stats->total_rays       = nrays;
        stats->batch_accepted   = stat_accepted;
        stats->filter_rejected  = stat_rejected;
        stats->retrace_accepted = stat_retrace_ok;
        stats->retrace_missed   = stat_retrace_miss;
    }

    free(needs_retrace);
    return RES_OK;
}
```

#### 2.6 公共API包装

```cpp
res_T
s3d_scene_view_trace_rays_batch
  (struct s3d_scene_view* scnview,
   const struct s3d_ray_request* requests,
   size_t nrays,
   struct s3d_hit* hits,
   struct s3d_batch_trace_stats* stats)
{
    if (!scnview || !requests || !hits)
        return RES_BAD_ARG;
    if (nrays == 0)
        return RES_OK;
    if (!(scnview->mask & S3D_TRACE))
        return RES_BAD_ARG;

    /* 临时分配GPU batch缓冲区和结果缓冲区 */
    struct cus3d_ray_batch gpu_batch;
    res_T res = cus3d_ray_batch_create(&gpu_batch, nrays);
    if (res != RES_OK) return res;

    struct cus3d_hit_result* h_results =
        (struct cus3d_hit_result*)malloc(nrays * sizeof(struct cus3d_hit_result));
    if (!h_results) {
        cus3d_ray_batch_destroy(&gpu_batch);
        return RES_MEM_ERR;
    }

    res = trace_rays_batch_impl(
        scnview, &gpu_batch, h_results, requests, nrays, hits, stats);

    free(h_results);
    cus3d_ray_batch_destroy(&gpu_batch);
    return res;
}

res_T
s3d_scene_view_trace_rays_batch_ctx
  (struct s3d_scene_view* scnview,
   struct s3d_batch_trace_context* ctx,
   const struct s3d_ray_request* requests,
   size_t nrays,
   struct s3d_hit* hits,
   struct s3d_batch_trace_stats* stats)
{
    if (!scnview || !ctx || !requests || !hits)
        return RES_BAD_ARG;
    if (nrays == 0)
        return RES_OK;
    if (!(scnview->mask & S3D_TRACE))
        return RES_BAD_ARG;
    if (nrays > ctx->max_rays)
        return RES_BAD_ARG;  /* 超出预分配容量 */

    return trace_rays_batch_impl(
        scnview, &ctx->gpu_batch, ctx->h_results,
        requests, nrays, hits, stats);
}
```

---

### Step 3: 修改 CMakeLists.txt

**修改文件**: `custar-3d/0.10/CMakeLists.txt`

在 C++ 源文件列表中添加新文件：

```cmake
# 现有行（约L68附近）:
#   src/s3d_scene_view_trace_ray.cpp
#   src/s3d_scene_view_closest_point.cpp

# 新增:
#   src/s3d_scene_view_batch_trace.cpp
```

找到 `set(S3D_SOURCES` 或类似的源文件列表，追加：

```
src/s3d_scene_view_batch_trace.cpp
```

---

### Step 4: `s3d_device` → `cus3d_device` 访问路径 ✅ 已验证

**结论**: `s3d_device_c.h` 已包含 `cus3d_device*` 字段，字段名为 `gpu`。

```c
// custar-3d/0.10/src/s3d_device_c.h（实际结构）
struct s3d_device {
  int verbose;
  struct logger* logger;
  struct mem_allocator* allocator;
  struct cus3d_device* gpu; /* cuBQL CUDA device (replaces RTCDevice) */
  struct flist_name names;
  ref_T ref;
};
```

完整访问路径：`view->scn->dev->gpu`

`s3d_scene_view_trace_ray.cpp` 中已有使用示例：
```c
cus3d_trace_ray_single_multi(
    scnview->bvh,
    scnview->geom_store,
    scnview->scn->dev->gpu,  /* <-- 访问路径 */
    ...);
```

**无需任何修改**，直接使用 `dev_s3d->gpu` 即可。

---

### Step 5: 编写单元测试

**新文件**: `custar-3d/0.10/test/test_s3d_batch_trace.c`

```c
/**
 * @file test_s3d_batch_trace.c
 * @brief 验证 s3d_scene_view_trace_rays_batch 与逐条追踪结果一致性
 *
 * 测试策略:
 *   1. 创建测试场景（三角形+球体）
 *   2. 生成 N 条随机射线
 *   3. 分别用 s3d_scene_view_trace_ray (逐条) 和
 *      s3d_scene_view_trace_rays_batch (批量) 追踪
 *   4. 比较每条射线的 hit 结果（distance, prim_id, uv, normal）
 *   5. 验证 bit-exact 一致性（无filter射线）
 *   6. 验证 filter 射线的结果也一致
 */
```

**测试用例设计**:

| 测试名 | 射线数 | filter | 验证内容 |
|--------|--------|--------|---------|
| `test_batch_vs_single_no_filter` | 256 | 无 | 距离/prim_id bit-exact |
| `test_batch_vs_single_with_filter` | 128 | 有 | 距离/prim_id 一致 |
| `test_batch_miss_consistency` | 64 | 无 | 全miss行为一致 |
| `test_batch_mixed_filter` | 512 | 混合 | 部分有filter部分无 |
| `test_batch_large` | 4096 | 混合 | 性能+正确性 |
| `test_batch_context_reuse` | 1024×10 | 无 | context多次复用 |
| `test_batch_stats_accuracy` | 256 | 混合 | 统计数字正确 |

**验证函数**:
```c
static int hits_equal(const struct s3d_hit* a, const struct s3d_hit* b)
{
    if (S3D_HIT_NONE(a) && S3D_HIT_NONE(b)) return 1;
    if (S3D_HIT_NONE(a) || S3D_HIT_NONE(b)) return 0;

    return a->prim.prim_id       == b->prim.prim_id
        && a->prim.geom_id       == b->prim.geom_id
        && a->prim.scene_prim_id == b->prim.scene_prim_id
        && fabsf(a->distance - b->distance) < 1e-6f
        && fabsf(a->uv[0] - b->uv[0]) < 1e-6f
        && fabsf(a->uv[1] - b->uv[1]) < 1e-6f
        && fabsf(a->normal[0] - b->normal[0]) < 1e-5f
        && fabsf(a->normal[1] - b->normal[1]) < 1e-5f
        && fabsf(a->normal[2] - b->normal[2]) < 1e-5f;
}
```

---

## 五、关键设计决策精解

### 5.1 为什么不在GPU上做filter？

当前 `hit_filter_function` 依赖的数据：
- `hit_from` — 来源hit（`s3d_hit*`），在调用者栈上
- `sdis_scene*` — 完整场景对象，包含 `prim_props[]` 数组
- `enc_id` — enclosure ID

将这些全部上传GPU需要：
1. 将 `prim_props[]`（每primitive的front/back enclosure ID）上传为GPU constant/global memory
2. 将 `hit_from` 的 `prim_id`（用于自交避免）作为per-ray数据上传
3. 实现GPU版 `hit_shared_edge`（需要顶点拓扑信息）
4. 编写新的GPU Top-K + filter kernel

**这是Phase C的工作范围**。Phase B-1 的目标是以最小改动获得batch加速，使用CPU post-filter + selective re-trace 是正确的务实选择。

### 5.2 为什么re-trace比例很低？

分析 `hit_filter_function` 的拒绝逻辑：

1. **同一primitive自交** — 只有当射线从一个表面出发并恰好又击中同一三角形时才触发。在典型热传输场景中，从表面出发的射线方向朝向法线半球，**击中同一三角形的概率极低**（通常 <1%）。

2. **零距离/极近距离** — 需要 `hit->distance ≈ 0`，只有当射线起点恰好在表面上时才可能。由于 `tmin` 通常 > 0，这种情况罕见。

3. **Enclosure不匹配** — 需要射线穿过enclosure边界，在正确构建的场景中不应发生。

**实测预期**: >90% 的有filter射线通过nearest-hit就能满足filter，不需要re-trace。

### 5.3 `cus3d_trace_ray_batch` 的局限性

| 特性 | 支持情况 | 说明 |
|------|---------|------|
| Nearest hit | ✅ | 每条射线返回1个最近交点 |
| Top-K multi-hit | ❌ 仅 `single_multi` | batch版只有nearest |
| Instanced (TLAS+BLAS) | ✅ | 有 instanced kernel |
| Mixed instances | ✅ | per-instance GPU数组 |
| 预分配缓冲区 | ✅ | `cus3d_ray_batch_create` |
| Async compute | ⚠️ 内部sync | 每次batch调用后同步 |

### 5.4 Staging缓冲区的内存布局

`cus3d_ray_batch` 的GPU缓冲区是 SoA (Structure of Arrays) 布局：
- `d_origins[N]` — N个float3
- `d_directions[N]` — N个float3
- `d_ranges[N]` — N个float2

而 `s3d_ray_request` 是 AoS (Array of Structures)。因此需要转置操作。

**⚠️ 实现要求: GPU 缓冲区上传必须使用 `cus3d_mem.h` 封装的 API**

`cus3d_mem.h` 提供了类型安全的 upload/download 封装（已验证 2026-02-10）：

```c
/* 上传 — 替代直接调用 cudaMemcpyAsync(..., cudaMemcpyHostToDevice, ...) */
gpu_buffer_float3_upload(&gpu_batch->d_origins,    h_origins,    nrays, stream);
gpu_buffer_float3_upload(&gpu_batch->d_directions, h_directions, nrays, stream);
gpu_buffer_float2_upload(&gpu_batch->d_ranges,     h_ranges,     nrays, stream);

/* 下载 — 替代直接调用 cudaMemcpyAsync(..., cudaMemcpyDeviceToHost, ...) */
gpu_buffer_float3_download(h_dst, &src_buf, count, stream);
```

**禁止**在本层（`s3d_scene_view_batch_trace.cpp`）直接调用 `cudaMemcpyAsync` 或直接操作 `buf.data` 指针。理由：
1. upload/download 内含 NULL 和 count=0 防御检查
2. 类型匹配由编译器保证（`float3*` vs `float2*`）
3. 后续若切换到 pinned memory 或 staging pool，只需改 `cus3d_mem.cpp`，不影响调用方

相应地，staging 缓冲区应分配为 `float3*` / `float2*` 类型（而非 `float*`），用 `make_float3()` / `make_float2()` 填充，以与 upload API 的签名直接匹配。

**后续优化**: 如果Phase B-2的调用者可以直接提供SoA布局的射线数据，可以省去转置开销。

---

## 六、`cus3d_trace_ray_batch` 每次调用的开销分解

来自 `cus3d_trace.cu` 的内部流程分析：

```
cus3d_trace_ray_batch() 开销分解（N条射线）:
  ┌──────────────────────────────────────────────┐
  │ cudaMallocAsync(d_results, N×sizeof(hit))    │  ~10μs
  │ [如果instanced]:                              │
  │   malloc(h_blas/h_instances)                  │  ~2μs
  │   填充 instances[]                            │  ~1μs/instance
  │   cudaMallocAsync(d_blas/d_instances)          │  ~10μs
  │   cudaMemcpyAsync(blas+instances, H→D)         │  ~5-20μs
  │ kernel<<<(N+255)/256, 256>>>()                │  ~5-100μs(取决于N和场景)
  │ cudaStreamSynchronize()                        │  ~5μs
  │ cudaMemcpyAsync(results, D→H)                 │  ~5-20μs(取决于N)
  │ cudaStreamSynchronize()                        │  ~5μs
  │ cudaFreeAsync(d_results [+ blas + instances])  │  ~5-10μs
  └──────────────────────────────────────────────┘
  总固定开销:  ~50-80μs (非instanced) / ~80-120μs (instanced)
  可变开销:    ~(N/256)×5μs (kernel) + ~N×0.02μs (memcpy)
```

**关键观察**: `cus3d_trace_ray_batch` 内部每次调用仍会 `cudaMallocAsync(d_results)`。对于Phase B-1优化版，可以考虑将 `d_results` 也预分配在 `s3d_batch_trace_context` 中，但需要修改 `cus3d_trace_ray_batch` 签名（属于底层修改，可延后）。

---

## 七、实施顺序和验证里程碑

```
           Step 1                Step 2               Step 3          Step 4
  ┌─────────────────┐  ┌─────────────────────┐  ┌──────────┐  ┌──────────────┐
  │ s3d.h:          │  │ batch_trace.cpp:     │  │ CMake:   │  │ 测试:        │
  │ 类型声明        │→│ 核心实现             │→│ 添加文件 │→│ 编译运行     │
  │ API声明         │  │ 上传+batch+后处理    │  │          │  │ 对比验证     │
  └─────────────────┘  └─────────────────────┘  └──────────┘  └──────────────┘
```

### 里程碑 M1: 编译通过（~2小时）
- Step 1-3 完成
- 空实现（仅返回 `RES_OK`）
- 确认链接无符号冲突

### 里程碑 M2: 无filter批量追踪正确（~4小时）
- Step 2 实现 Step 1-2（上传 + batch trace + 类型转换 + fixup）
- 跳过 filter 处理
- 测试: `test_batch_vs_single_no_filter` 通过

### 里程碑 M3: 完整实现（~3小时）
- Step 2 实现 Step 3-4（filter + re-trace）
- 测试: 所有测试用例通过
- 统计输出验证

### 里程碑 M4: 性能基准（~2小时）
- Context 复用版本
- 与现有 `s3d_scene_view_trace_rays` 性能对比
- 确认batch size ≥ 256时性能提升显著

---

## 八、风险与缓解

| 风险 | 严重度 | 概率 | 缓解 |
|------|--------|------|------|
| ~~`s3d_device` 缺少 `cus3d_device*` 访问路径~~ | ~~高~~ | — | ✅ **已排除** (2026-02-10): 字段 `s3d_device::gpu` 已存在，路径 `view->scn->dev->gpu` |
| ~~`gpu_buffer_float3` 的 `data` 不是连续 `float3*`~~ | ~~高~~ | — | ✅ **已排除** (2026-02-10): `data` 是连续 `float3*`，通过 `cudaMallocAsync(count * sizeof(float3))` 分配 |
| Instanced 场景下 `geom_idx` 解析错误 | 高 | 低 | 完整参照 `trace_ray_impl` 的instance解析逻辑 |
| 大 batch size 导致 GPU OOM | 中 | 低 | `cus3d_ray_batch_create` 已处理分配失败 |
| `trace_hit_fixup` 与原始代码不一致 | 高 | 极低 | 复制原始代码，不做任何修改 |
| Filter re-trace 比例超预期（>30%） | 中 | 低 | 监控统计，必要时切换到batch Top-K |

---

## 九、后续优化方向（Phase B-1.1/B-1.2）

1. **预分配 `d_results`**: 修改 `cus3d_trace_ray_batch` 接受外部分配的结果缓冲区
2. **批量 Top-K**: 新增 `cus3d_trace_ray_batch_multi` 使用 `trace_rays_topk_kernel<<<grid, 256>>>`，消除re-trace
3. **GPU Filter 上移**: 将简单的自交避免filter（步骤1: same prim_id → reject）集成到kernel
4. **SoA 输入接口**: 新增 `s3d_scene_view_trace_rays_batch_soa` 接受分离的 origins/directions/ranges 数组
5. **Pinned Memory**: 使用 `cudaMallocHost` 分配staging缓冲区加速H↔D传输
6. **Double-buffering**: 在GPU trace kernel执行期间CPU处理上一batch的后处理

---

*文档更新: 2026-02-09 | Phase B-1 完整实现步骤指南*
