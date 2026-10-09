# Phase B-2 实现指南：stardis-solver Wavefront 重构

**生成时间**: 2026-02-09  
**前置文档**: [phase_b1_implementation.md](phase_b1_implementation.md)（必须先完成B-1）  
**范围**: stardis-solver 0.16.2 — `solve_tile` 从串行像素循环改为 wavefront 批量步进  
**修改文件数**: ~8个（1个新文件 + 7个修改）  
**预计代码量**: ~1000-1200行

---

## 一、目标

将 `solve_tile` 内的路径处理从"逐像素跑完整条路径"改为"所有像素的所有路径同步步进一步"模式，使得同一步的射线请求被收集并通过 Phase B-1 的 `s3d_scene_view_trace_rays_batch` 一次性发射。

**约束条件**：
- 给定相同 RNG 序列，结果必须与原始串行模式 **bit-exact** 一致
- 不修改物理算法，仅修改执行顺序
- 保持 OMP tile-level 并行不变
- 每个 OMP 线程独立运行 wavefront，线程间无共享状态

---

## 二、架构对比

### 2.1 当前架构（串行 pixel-at-a-time）

```
sdis_solve_camera
  └─ #pragma omp parallel for (tile-level)
      └─ solve_tile(tile)
          └─ FOR_EACH(pixel_mcode, Morton序)
              └─ solve_pixel(pixel)
                  └─ FOR_EACH(irealisation, 0, spp)
                      └─ ray_realisation_3d(scn, &args, &w)
                          ├─ trace_radiative_path_3d()    ← for(;;) 弹射循环
                          │    └─ find_next_fragment()     ← trace_ray() → kernel<<<1,1>>>
                          └─ sample_coupled_path_3d()      ← while(!T.done) 状态机
                               └─ T->func()
                                   ├─ boundary_path()      ← find_reinjection_ray() → 2× trace_ray
                                   ├─ conductive_path()    ← delta_sphere: 2× trace_ray
                                   ├─ convective_path()    ← (表面采样，偶发trace_ray)
                                   └─ radiative_path()     ← trace_radiative_path_3d()
```

**问题**: 每次 `trace_ray` → `kernel<<<1,1>>>` → sync → 返回。整个路径是深度优先、完全串行。

### 2.2 目标架构（Wavefront lockstep）

```
sdis_solve_camera
  └─ #pragma omp parallel for (tile-level)
      └─ solve_tile_wavefront(tile)
          ├─ 初始化 path_state[TILE_SIZE² × SPP] 所有路径
          └─ while(any_path_active)  ← wavefront主循环
              ├─ Step A: 收集射线请求
              │   └─ FOR_EACH(active_path) → append_ray_request()
              ├─ Step B: 批量追踪
              │   └─ s3d_scene_view_trace_rays_batch(view, requests, N, hits)
              ├─ Step C: 分发结果 + 推进一步
              │   └─ FOR_EACH(active_path) → advance_one_step(path, hit) 
              │       → 设置 path.needs_ray / path.done
              └─ Step D: 压缩活跃路径（可选）
```

---

## 三、核心数据结构

### 3.1 `struct path_state` — 路径外化状态

这是整个重构的核心。需要将 `ray_realisation_3d` → `trace_radiative_path` → `sample_coupled_path` → `T->func()` 调用链中的**所有栈上中间状态**提取到一个结构体中。

```c
/* 新文件: sdis_solve_wavefront.h */

#include "sdis_scene_c.h"
#include "sdis_tile.h"
#include "sdis_estimator.h"

/* 路径阶段枚举 */
enum path_phase {
    /* === 初始化 === */
    PATH_INIT,                       /* 未开始 */

    /* === 辐射路径阶段 === */
    PATH_RAD_TRACE_PENDING,          /* 已提交trace_ray请求，等待结果 */
    PATH_RAD_PROCESS_HIT,            /* 已获得hit，处理BRDF/发射率 */

    /* === 耦合路径状态机 === */
    PATH_COUPLED_BOUNDARY,           /* 边界路径 */
    PATH_COUPLED_BOUNDARY_REINJECT,  /* 边界: find_reinjection_ray 等待trace结果 */
    PATH_COUPLED_CONDUCTIVE,         /* 传导路径 */
    PATH_COUPLED_COND_DS_PENDING,    /* 传导: delta_sphere 2-ray 等待trace结果 */
    PATH_COUPLED_CONVECTIVE,         /* 对流路径 */
    PATH_COUPLED_RADIATIVE,          /* 耦合内辐射路径（bounce进入） */

    /* === 射线等待（通用） === */
    PATH_AWAITING_RAY_RESULT,        /* 通用: 等待batch trace返回 */

    /* === 终止 === */
    PATH_DONE                        /* 路径完成，结果在 weight 中 */
};

/* 每条路径的射线请求描述 */
struct path_ray_request {
    float    origin[3];
    float    direction[3];
    float    range[2];
    void*    filter_data;

    /* 部分调用点需要2条射线（delta_sphere, find_reinjection_ray） */
    float    direction2[3];
    float    range2[2];
    void*    filter_data2;
    int      ray_count;     /* 1 或 2 */

    /* 结果分发索引 */
    uint32_t batch_idx;     /* 在batch中的起始索引 */
    uint32_t batch_idx2;    /* 第2条射线在batch中的索引 */
};

/* 完整路径状态 */
struct path_state {
    /* --- 标识 --- */
    uint32_t  path_id;              /* 全局唯一ID */
    uint16_t  pixel_x, pixel_y;     /* 像素坐标 */
    uint32_t  realisation_idx;      /* 第几次realisation */

    /* --- 生命周期 --- */
    enum path_phase  phase;         /* 当前阶段 */
    int              active;        /* 是否活跃 */

    /* --- 随机游走核心状态 --- */
    double  position[3];            /* 当前位置 */
    double  time;                   /* 当前时间 */

    /* --- 命中信息 --- */
    struct s3d_hit  hit_3d;         /* 当前表面交点 */
    int             hit_side;       /* 0=front, 1=back */
    unsigned        enc_id;         /* 当前enclosure ID */

    /* --- 温度/权重累加 --- */
    double  T_value;                /* 温度值 */
    int     T_done;                 /* 路径是否终止 */
    double  weight;                 /* 最终权重（输出到estimator） */

    /* --- 辐射路径状态 --- */
    float   rad_direction[3];       /* 当前辐射路径方向 */
    int     rad_bounce_count;       /* 弹射次数 */
    int     rad_in_find_next_frag;  /* 是否在find_next_fragment内 */
    int     rad_retry_count;        /* find_next_fragment重试计数 */

    /* --- 耦合路径状态 --- */
    int     coupled_nbranchings;     /* 分支计数 */
    int     coupled_max_branchings;  /* 最大分支 */

    /* --- 传导路径状态 (delta_sphere) --- */
    float   ds_dir0[3], ds_dir1[3]; /* 正反探测方向 */
    struct s3d_hit ds_hit0, ds_hit1; /* 正反探测结果 */
    double  ds_delta_solid;          /* 固体delta */

    /* --- 边界路径状态 --- */
    struct s3d_hit bnd_hit0, bnd_hit1; /* 重注入探测结果 */
    double  bnd_reinject_distance;     /* 最大重注入距离 */
    unsigned bnd_solid_enc_id;         /* 目标固体enclosure */
    int     bnd_retry_count;           /* 重注入重试计数 */

    /* --- filter_data (用于当前射线请求) --- */
    struct hit_filter_data  filter_data_storage;

    /* --- 射线请求输出 --- */
    struct path_ray_request ray_req;
    int    needs_ray;               /* 本步是否需要射线追踪 */

    /* --- RNG --- */
    struct ssp_rng* rng;            /* 每路径独立RNG（实际是per-thread） */

    /* --- 上下文引用 --- */
    struct rwalk_context*  ctx_template; /* 模板（只读） */

    /* --- 热路径注册 --- */
    int    register_paths;          /* 是否注册热路径顶点 */
};
```

**内存估算**: 每个 `path_state` 约 400-500 字节（含padding）。

一个tile的路径数量 = `TILE_SIZE² × SPP` = `16 × 64` = 1024 条，总计 ~500KB per tile。在栈上可能过大，需要堆分配。

### 3.2 `struct wavefront_context` — Wavefront调度器上下文

```c
struct wavefront_context {
    /* 路径池 */
    struct path_state*  paths;          /* 所有路径状态 */
    size_t  total_paths;                /* 总路径数 */
    size_t  active_count;               /* 当前活跃路径数 */

    /* 射线请求收集 */
    struct s3d_ray_request*  ray_requests;   /* 射线请求数组 */
    uint32_t* ray_to_path;                   /* 射线→路径映射 */
    uint32_t* ray_slot;                      /* 射线→路径内slot(0/1) */
    size_t  ray_count;                       /* 当前收集的射线数 */
    size_t  max_rays;                        /* 预分配容量 */

    /* 追踪结果 */
    struct s3d_hit*  ray_hits;               /* 追踪结果数组 */

    /* 批量追踪上下文（Phase B-1） */
    struct s3d_batch_trace_context* batch_ctx;

    /* 估计器 */
    struct sdis_estimator*  estimator;

    /* 统计 */
    size_t  total_steps;                     /* 总步数 */
    size_t  total_rays_traced;               /* 总射线数 */
};
```

---

## 四、Wavefront 主循环实现

### 4.1 `solve_tile_wavefront` — 顶层入口

```c
static res_T solve_tile_wavefront(
    struct sdis_scene* scn,
    struct ssp_rng* rng,
    const unsigned enc_id,
    const struct sdis_camera* cam,
    const double time_range[2],
    const size_t tile_org[2],
    const size_t tile_size[2],
    const size_t spp,
    const int register_paths,
    const double pix_sz[2],
    const size_t picard_order,
    const enum sdis_diffusion_algorithm diff_algo,
    struct sdis_estimator_buffer* buf,
    struct tile* tile)
{
    res_T res;
    const size_t npixels = tile_size[0] * tile_size[1];
    const size_t total_paths = npixels * spp;

    /* ====== 分配wavefront上下文 ====== */
    struct wavefront_context wf;
    memset(&wf, 0, sizeof(wf));

    wf.total_paths = total_paths;
    wf.paths = (struct path_state*)calloc(total_paths, sizeof(struct path_state));
    if (!wf.paths) return RES_MEM_ERR;

    /* 最大射线数: 每条路径最多2条射线 */
    wf.max_rays = total_paths * 2;
    wf.ray_requests = (struct s3d_ray_request*)malloc(
        wf.max_rays * sizeof(struct s3d_ray_request));
    wf.ray_to_path = (uint32_t*)malloc(wf.max_rays * sizeof(uint32_t));
    wf.ray_slot    = (uint32_t*)malloc(wf.max_rays * sizeof(uint32_t));
    wf.ray_hits    = (struct s3d_hit*)malloc(wf.max_rays * sizeof(struct s3d_hit));

    if (!wf.ray_requests || !wf.ray_to_path || !wf.ray_slot || !wf.ray_hits) {
        /* cleanup and return */
        goto cleanup;
    }

    /* 创建batch追踪上下文（预分配GPU缓冲区） */
    res = s3d_batch_trace_context_create(&wf.batch_ctx, wf.max_rays);
    if (res != RES_OK) goto cleanup;

    /* ====== 初始化所有路径 ====== */
    res = init_all_paths(&wf, scn, rng, enc_id, cam, time_range,
                         tile_org, tile_size, spp, register_paths,
                         pix_sz, picard_order, diff_algo);
    if (res != RES_OK) goto cleanup;

    /* ====== Wavefront 主循环 ====== */
    while (wf.active_count > 0) {
        wf.total_steps++;

        /* Step A: 收集所有活跃路径的射线请求 */
        wf.ray_count = 0;
        res = collect_ray_requests(&wf);
        if (res != RES_OK) goto cleanup;

        /* Step B: 批量追踪 */
        if (wf.ray_count > 0) {
            struct s3d_batch_trace_stats stats;
            res = s3d_scene_view_trace_rays_batch_ctx(
                scn->s3d_view, wf.batch_ctx,
                wf.ray_requests, wf.ray_count,
                wf.ray_hits, &stats);
            if (res != RES_OK) goto cleanup;
            wf.total_rays_traced += wf.ray_count;
        }

        /* Step C: 分发结果并推进每条路径一步 */
        res = advance_all_paths(&wf, scn, rng);
        if (res != RES_OK) goto cleanup;

        /* Step D: 更新活跃计数 */
        update_active_count(&wf);
    }

    /* ====== 收集结果到 tile ====== */
    res = collect_results(&wf, buf, tile, tile_org, tile_size);

cleanup:
    s3d_batch_trace_context_destroy(wf.batch_ctx);
    free(wf.paths);
    free(wf.ray_requests);
    free(wf.ray_to_path);
    free(wf.ray_slot);
    free(wf.ray_hits);
    return res;
}
```

### 4.2 `init_all_paths` — 路径初始化

```c
/*
 * 为tile内每个像素的每次realisation创建一条路径。
 *
 * 对应原始代码中 solve_pixel() 的以下操作:
 *   1. 采样时间 t ∈ time_range（均匀分布）
 *   2. 像素内采样点 (sub_x, sub_y)（均匀分布）
 *   3. camera_ray() 生成光线起点和方向
 *   4. 填充 ray_realisation_args
 *
 * 关键: RNG调用顺序必须与原始 solve_pixel 完全一致！
 *
 * 原始调用顺序（per pixel, per realisation）:
 *   rng_uniform() → time
 *   rng_uniform() → sub_x
 *   rng_uniform() → sub_y
 *   camera_ray()  → ... (内部可能用RNG)
 *   然后 ray_realisation_3d 内部的RNG调用
 *
 * Wavefront版本必须保持相同的RNG顺序。
 */
static res_T init_all_paths(
    struct wavefront_context* wf,
    struct sdis_scene* scn,
    struct ssp_rng* rng,
    const unsigned enc_id,
    const struct sdis_camera* cam,
    const double time_range[2],
    const size_t tile_org[2],
    const size_t tile_size[2],
    const size_t spp,
    const int register_paths,
    const double pix_sz[2],
    const size_t picard_order,
    const enum sdis_diffusion_algorithm diff_algo)
{
    size_t path_idx = 0;

    /*
     * 遍历顺序必须与原始 solve_tile 一致:
     * FOR_EACH(pixel_mcode, Morton序)
     *   FOR_EACH(irealisation, 0, spp)
     *
     * 这确保每条路径使用的RNG状态与原始代码完全相同。
     */
    size_t npixels = tile_size[0] * tile_size[1];

    for (size_t pixel_mcode = 0; pixel_mcode < npixels; pixel_mcode++) {
        /* Morton码解码 → 像素坐标 (需要复用原始的morton_decode) */
        size_t px, py;
        morton_decode(pixel_mcode, &px, &py);  /* tile内局部坐标 */

        for (size_t irealisation = 0; irealisation < spp; irealisation++) {
            struct path_state* p = &wf->paths[path_idx];
            memset(p, 0, sizeof(*p));

            p->path_id = (uint32_t)path_idx;
            p->pixel_x = (uint16_t)px;
            p->pixel_y = (uint16_t)py;
            p->realisation_idx = (uint32_t)irealisation;
            p->active = 1;
            p->rng = rng;  /* 注意: 所有路径共享同一RNG（per-thread） */
            p->register_paths = register_paths;

            /* === 采样时间 === */
            double u_time = ssp_rng_uniform(rng);
            p->time = time_range[0] + u_time * (time_range[1] - time_range[0]);

            /* === 像素内子采样 === */
            double sub_x = ssp_rng_uniform(rng);
            double sub_y = ssp_rng_uniform(rng);

            /* === 生成相机射线 === */
            float ray_origin[3], ray_direction[3];
            size_t global_px = tile_org[0] + px;
            size_t global_py = tile_org[1] + py;
            /* 调用camera_ray获取起点和方向 */
            camera_ray(cam, global_px, global_py, sub_x, sub_y,
                       pix_sz, ray_origin, ray_direction);

            /* === 设置初始状态 === */
            p->position[0] = (double)ray_origin[0];
            p->position[1] = (double)ray_origin[1];
            p->position[2] = (double)ray_origin[2];
            p->enc_id = enc_id;
            p->hit_3d = S3D_HIT_NULL;

            /* 初始射线方向 */
            p->rad_direction[0] = ray_direction[0];
            p->rad_direction[1] = ray_direction[1];
            p->rad_direction[2] = ray_direction[2];

            /* 路径控制 */
            p->coupled_nbranchings = 0;
            p->coupled_max_branchings = 50;  /* 从ctx获取 */

            /* === 阶段: 初始辐射路径 → 等待第一次trace === */
            p->phase = PATH_RAD_TRACE_PENDING;

            /* 填充第一条射线请求 */
            p->needs_ray = 1;
            p->ray_req.ray_count = 1;
            /* trace_ray 包装: 设置 filter_data */
            setup_initial_ray_request(p, scn, ray_origin, ray_direction, enc_id);

            path_idx++;
        }
    }

    wf->active_count = path_idx;
    return RES_OK;
}
```

**RNG一致性 — 关键约束**:

原始代码中，每条路径内部的 RNG 调用是完全串行的。Wavefront 模式下，同一 tile 的路径初始化顺序必须严格匹配原始的 Morton 序 + SPP 循环顺序。

**但有一个本质问题**: 原始代码中，路径 0 的所有 RNG 调用完成后才开始路径 1。Wavefront 模式下，路径 0 的 `init` 阶段 RNG 调用完成后就进入路径 1 的 `init`，但路径 0 后续步骤的 RNG 调用（如 BRDF 采样）被延迟到了后面的 wavefront step。

**解决方案**: 每条路径需要独立的 RNG 状态。

### 4.3 **RNG挑战与解决方案** （★重要★）

#### 问题陈述

原始代码中只有 1 个 RNG per thread:

```c
// 原始: 串行执行
for(irealisation = 0; irealisation < spp; irealisation++) {
    // rng 调用序列: R0, R1, R2, ... (路径0的所有RNG)
    ray_realisation_3d(scn, &args, &w);  // 内部消耗 RNG: R0..Rk
}
// rng 调用序列: Rk+1, Rk+2, ... (路径1的所有RNG)
```

```c
// Wavefront: 交错执行
init_path(0, rng);  // 消耗 RNG: R0..R3 (time, sub_x, sub_y, camera)
init_path(1, rng);  // 消耗 RNG: R4..R7

// step 1: 
advance(path_0, rng);  // 消耗 RNG: R8..R12 (BRDF采样等)
advance(path_1, rng);  // 消耗 RNG: R13..R17

// 与原始的 R0..Rk, Rk+1..R2k 序列不同！
```

**如果共享同一个RNG，Wavefront模式的结果将与原始不同**（RNG调用顺序被打乱）。

#### 解决方案选项

| 方案 | 复杂度 | bit-exact | 说明 |
|------|--------|-----------|------|
| **A: Per-path独立RNG** | 低 | ❌ | 用 path_id 作为seed，新结果仍然统计正确但不bit-exact |
| **B: RNG序列预生成** | 高 | ✅ | 提前按原始顺序生成所有随机数，按路径分发 |
| **C: 保持原始执行顺序** | 无 | ✅ | 路径按原始顺序串行消耗RNG（回退到串行，失去意义） |
| **D: Per-path Fork RNG** | 中 | ❌ | 用 rng_fork(rng, path_id) 创建子RNG流 |

**推荐方案A**（务实选择）:

```c
/*
 * 为每条路径创建独立的RNG，使用 (base_seed + path_id) 初始化。
 * 这改变了随机数序列，但蒙特卡洛估计的期望值不变。
 * 方差可能有微小差异（同一统计精度下）。
 *
 * 验证: 用大SPP(≥1000)对比平均温度，差异应在统计误差范围内。
 */
struct ssp_rng path_rng;
ssp_rng_init(&path_rng, base_seed + path_id);
p->rng = &wf->path_rngs[path_idx];  /* 预分配的RNG数组 */
*p->rng = path_rng;
```

**如果需要bit-exact验证 — 使用方案B**:

```c
/*
 * Phase 1 验证期间，可使用方案B进行正确性验证:
 * 1. 先用原始串行模式运行，记录每条路径的所有RNG调用值
 * 2. 在wavefront模式中，按记录的值"回放"
 * 3. 确认像素温度完全一致
 *
 * 验证通过后切换到方案A用于生产。
 */
```

### 4.4 `collect_ray_requests` — 射线收集

```c
/*
 * 遍历所有活跃路径，将需要射线追踪的路径的请求加入batch。
 * 同时记录射线→路径的映射关系。
 */
static res_T collect_ray_requests(struct wavefront_context* wf)
{
    size_t ray_idx = 0;

    for (size_t i = 0; i < wf->total_paths; i++) {
        struct path_state* p = &wf->paths[i];
        if (!p->active || !p->needs_ray) continue;

        /* 第1条射线 */
        struct s3d_ray_request* req = &wf->ray_requests[ray_idx];
        memcpy(req->origin,    p->ray_req.origin,    3 * sizeof(float));
        memcpy(req->direction, p->ray_req.direction,  3 * sizeof(float));
        memcpy(req->range,     p->ray_req.range,      2 * sizeof(float));
        req->filter_data = p->ray_req.filter_data;
        req->user_id     = (uint32_t)i;

        wf->ray_to_path[ray_idx] = (uint32_t)i;
        wf->ray_slot[ray_idx]    = 0;
        p->ray_req.batch_idx     = (uint32_t)ray_idx;
        ray_idx++;

        /* 第2条射线（如果有） */
        if (p->ray_req.ray_count >= 2) {
            req = &wf->ray_requests[ray_idx];
            memcpy(req->origin,    p->ray_req.origin,     3 * sizeof(float));
            memcpy(req->direction, p->ray_req.direction2,  3 * sizeof(float));
            memcpy(req->range,     p->ray_req.range2,      2 * sizeof(float));
            req->filter_data = p->ray_req.filter_data2;
            req->user_id     = (uint32_t)i;

            wf->ray_to_path[ray_idx] = (uint32_t)i;
            wf->ray_slot[ray_idx]    = 1;
            p->ray_req.batch_idx2    = (uint32_t)ray_idx;
            ray_idx++;
        }
    }

    wf->ray_count = ray_idx;
    return RES_OK;
}
```

### 4.5 `advance_all_paths` — 路径步进

```c
/*
 * 将batch trace结果分发给各路径，每条路径根据当前phase执行一步。
 * 步进后路径设置新的needs_ray/phase/done状态。
 */
static res_T advance_all_paths(
    struct wavefront_context* wf,
    struct sdis_scene* scn,
    struct ssp_rng* rng /* unused if per-path rng */)
{
    for (size_t i = 0; i < wf->total_paths; i++) {
        struct path_state* p = &wf->paths[i];
        if (!p->active) continue;

        /* 分发射线结果 */
        struct s3d_hit hit0 = S3D_HIT_NULL;
        struct s3d_hit hit1 = S3D_HIT_NULL;

        if (p->needs_ray) {
            hit0 = wf->ray_hits[p->ray_req.batch_idx];
            if (p->ray_req.ray_count >= 2) {
                hit1 = wf->ray_hits[p->ray_req.batch_idx2];
            }
        }

        /* 重置射线请求 */
        p->needs_ray = 0;

        /* 根据当前phase执行一步 */
        res_T res;
        switch (p->phase) {
            case PATH_RAD_TRACE_PENDING:
                res = step_radiative_trace(p, scn, &hit0);
                break;
            case PATH_COUPLED_COND_DS_PENDING:
                res = step_conductive_delta_sphere(p, scn, &hit0, &hit1);
                break;
            case PATH_COUPLED_BOUNDARY_REINJECT:
                res = step_boundary_reinject(p, scn, &hit0, &hit1);
                break;
            case PATH_COUPLED_BOUNDARY:
                res = step_boundary(p, scn);
                break;
            case PATH_COUPLED_CONDUCTIVE:
                res = step_conductive_begin(p, scn);
                break;
            case PATH_COUPLED_CONVECTIVE:
                res = step_convective(p, scn);
                break;
            case PATH_COUPLED_RADIATIVE:
                res = step_coupled_radiative_begin(p, scn);
                break;
            case PATH_DONE:
                continue;
            default:
                res = RES_ERR;
                break;
        }

        if (res != RES_OK) return res;

        /* 检查路径是否完成 */
        if (p->T_done || p->phase == PATH_DONE) {
            p->active = 0;
            p->phase = PATH_DONE;
        }
    }

    return RES_OK;
}
```

---

## 五、路径步进函数分解

### 5.1 设计原则

每个 `step_*` 函数遵循以下约定：

```
输入:  path_state* p, sdis_scene* scn, s3d_hit* (0-2个trace结果)
输出:  修改 p->phase, p->needs_ray, p->ray_req
       如果路径结束: p->T_done = 1, p->weight = <value>
```

每个函数执行**恰好一步**物理逻辑，然后返回。如果该步需要射线追踪，设置 `p->needs_ray = 1` 和 `p->ray_req`，在下一轮wavefront步骤中获得结果。

### 5.2 `step_radiative_trace` — 辐射路径处理trace结果

对应原始代码: `trace_radiative_path_Xd.h` 中 `find_next_fragment` → `trace_ray` 返回后的处理 + BRDF决策。

```c
/*
 * 收到trace_ray结果后:
 * 1. 如果miss → 辐射环境温度 → PATH_DONE
 * 2. 如果hit → 获取interface/BRDF
 *    a. 以emissivity概率 → 吸收 → 转boundary_path → PATH_COUPLED_BOUNDARY
 *    b. 否则 → BRDF采样新方向 → 再次发射trace → PATH_RAD_TRACE_PENDING
 */
static res_T step_radiative_trace(
    struct path_state* p,
    struct sdis_scene* scn,
    const struct s3d_hit* trace_hit)
{
    /* --- Miss处理 --- */
    if (S3D_HIT_NONE(trace_hit)) {
        /*
         * 对应: trace_radiative_path_Xd.h 中
         *   if(SXD_HIT_NONE(&hit_3d))
         *     → set_limit_radiative_temperature()
         *     → T->done = 1
         */
        p->T_value = sdis_get_radiative_env_temperature(scn, p->time);
        p->T_done = 1;
        p->phase = PATH_DONE;
        return RES_OK;
    }

    /* --- Hit处理 --- */
    p->hit_3d = *trace_hit;

    /* 确定hit_side (front/back) */
    float dot_nd = trace_hit->normal[0] * p->rad_direction[0]
                 + trace_hit->normal[1] * p->rad_direction[1]
                 + trace_hit->normal[2] * p->rad_direction[2];
    p->hit_side = (dot_nd > 0) ? 1 : 0;  /* >0 = back face */

    /* 更新位置 */
    for (int d = 0; d < 3; d++)
        p->position[d] += (double)trace_hit->distance * (double)p->rad_direction[d];

    /* 获取interface和fragment */
    struct sdis_interface* interf = NULL;
    struct sdis_interface_fragment frag;
    res_T res = check_interface(scn, trace_hit, p->enc_id, &interf, &frag);
    if (res != RES_OK || !interf) {
        /* find_next_fragment重试逻辑 */
        p->rad_retry_count++;
        if (p->rad_retry_count < 10) {
            /* 微调位置重试 */
            setup_radiative_retry_ray(p, scn);
            p->phase = PATH_RAD_TRACE_PENDING;
            p->needs_ray = 1;
            return RES_OK;
        }
        /* 超过重试次数 → 错误 */
        return RES_ERR;
    }
    p->rad_retry_count = 0;

    /* BRDF决策 */
    struct sdis_brdf brdf;
    sdis_interface_get_brdf(interf, &frag, p->time, p->hit_side, &brdf);

    /* 以emissivity概率吸收 → 转boundary */
    double xi = ssp_rng_uniform(p->rng);
    if (xi < brdf.emissivity) {
        /* 吸收 → boundary path */
        p->phase = PATH_COUPLED_BOUNDARY;
        return RES_OK;
    }

    /* BRDF采样反射方向 */
    p->rad_bounce_count++;
    float new_dir[3];
    sdis_brdf_sample(&brdf, p->rad_direction, trace_hit->normal,
                     p->rng, new_dir);

    /* 设置新的射线请求 */
    memcpy(p->rad_direction, new_dir, 3 * sizeof(float));
    setup_radiative_trace_ray(p, scn);
    p->phase = PATH_RAD_TRACE_PENDING;
    p->needs_ray = 1;

    return RES_OK;
}
```

### 5.3 `step_conductive_delta_sphere` — 处理2-ray探测结果

对应原始代码: `sdis_heat_path_conductive_delta_sphere_Xd.h` 中 `sample_next_step` 返回后的处理。

```c
/*
 * 收到 delta_sphere 的2条射线结果后:
 * 1. 计算 delta = min(hit0.distance, hit1.distance, delta_solid)
 * 2. 移动位置
 * 3. 如果到达界面 → PATH_COUPLED_BOUNDARY
 * 4. 否则 → 继续delta_sphere → 采样新方向 → PATH_COUPLED_COND_DS_PENDING
 */
static res_T step_conductive_delta_sphere(
    struct path_state* p,
    struct sdis_scene* scn,
    const struct s3d_hit* hit0,
    const struct s3d_hit* hit1)
{
    /* 保存结果 */
    p->ds_hit0 = *hit0;
    p->ds_hit1 = *hit1;

    /* 计算delta */
    float d0 = S3D_HIT_NONE(hit0) ? FLT_MAX : hit0->distance;
    float d1 = S3D_HIT_NONE(hit1) ? FLT_MAX : hit1->distance;
    float delta = fminf(fminf(d0, d1), (float)p->ds_delta_solid);

    /* snap-to-boundary逻辑 */
    float snap_threshold = delta * 0.01f;
    int snapped = 0;

    if (d0 < d1 && d0 <= snap_threshold) {
        /* snap到hit0方向 */
        delta = d0;
        p->hit_3d = *hit0;
        snapped = 1;
    } else if (d1 <= snap_threshold) {
        delta = d1;
        p->hit_3d = *hit1;
        snapped = 1;
    }

    /* 移动位置 (沿dir0方向移动delta的delta_sphere步) */
    /* 注意: 实际上delta_sphere是以position为球心的球面上移动 */
    /* 具体的移动逻辑需要精确复用原始代码 */
    /* ... (需要从原始代码精确提取) ... */

    if (snapped || !S3D_HIT_NONE(hit0) || !S3D_HIT_NONE(hit1)) {
        /* 到达界面 → 进入boundary_path */
        p->phase = PATH_COUPLED_BOUNDARY;
    } else {
        /* 继续delta_sphere: 采样新方向并发射2条射线 */
        setup_delta_sphere_rays(p, scn);
        p->phase = PATH_COUPLED_COND_DS_PENDING;
        p->needs_ray = 1;
    }

    return RES_OK;
}
```

### 5.4 `step_boundary_reinject` — 处理重注入射线结果

对应原始代码: `sdis_heat_path_boundary_Xd_c.h` 中 `find_reinjection_ray` 返回后的处理。

```c
static res_T step_boundary_reinject(
    struct path_state* p,
    struct sdis_scene* scn,
    const struct s3d_hit* hit0,
    const struct s3d_hit* hit1)
{
    /* 保存重注入探测结果 */
    p->bnd_hit0 = *hit0;
    p->bnd_hit1 = *hit1;

    /* 选择重注入方向和距离 */
    /* (需要精确复用 find_reinjection_ray 的选择逻辑) */

    /* 如果两个方向都无效 → 重试 */
    int valid0 = !S3D_HIT_NONE(hit0) && /* enclosure匹配检查 */;
    int valid1 = !S3D_HIT_NONE(hit1) && /* enclosure匹配检查 */;

    if (!valid0 && !valid1) {
        p->bnd_retry_count++;
        if (p->bnd_retry_count >= 20) {
            /* 超过最大重试: 错误或path终止 */
            p->T_done = 1;
            p->phase = PATH_DONE;
            return RES_OK;
        }
        /* 微调位置重新发射 */
        setup_reinject_retry_rays(p, scn);
        p->phase = PATH_COUPLED_BOUNDARY_REINJECT;
        p->needs_ray = 1;
        return RES_OK;
    }

    /* 执行重注入: 移动到新位置 */
    execute_reinjection(p, scn, hit0, hit1, valid0, valid1);

    /* 重置重试计数 */
    p->bnd_retry_count = 0;

    /* 设置下一阶段 (conductive或boundary) */
    p->phase = PATH_COUPLED_CONDUCTIVE;  /* 或根据interface决定 */
    return RES_OK;
}
```

### 5.5 `step_boundary` — 边界路径（无射线调用的处理）

对应原始代码: `boundary_path` 的温度查询和下一步决定。

```c
static res_T step_boundary(
    struct path_state* p,
    struct sdis_scene* scn)
{
    /* 获取interface和fragment */
    struct sdis_interface* interf = NULL;
    struct sdis_interface_fragment frag;
    /* ... */

    /* 检查边界温度是否已知 */
    if (sdis_interface_temperature_known(interf, &frag, p->time)) {
        p->T_value = sdis_interface_get_temperature(interf, &frag, p->time);
        p->T_done = 1;
        p->phase = PATH_DONE;
        return RES_OK;
    }

    /* 决定下一步: solid/fluid → reinject? conductive? convective? radiative? */
    unsigned front_enc_id, back_enc_id;
    sdis_scene_get_enclosure_ids(scn, p->hit_3d.prim.scene_prim_id,
                                 &front_enc_id, &back_enc_id);

    unsigned next_enc_id = (p->hit_side == 0) ? back_enc_id : front_enc_id;
    struct sdis_medium* next_medium = sdis_scene_get_medium(scn, next_enc_id);

    if (sdis_medium_is_solid(next_medium)) {
        /* solid → 传导路径 → 需要reinjection ray */
        p->bnd_solid_enc_id = next_enc_id;
        p->enc_id = next_enc_id;

        /* 采样重注入方向 */
        setup_reinject_rays(p, scn);
        p->phase = PATH_COUPLED_BOUNDARY_REINJECT;
        p->needs_ray = 1;
    } else if (sdis_medium_is_fluid(next_medium)) {
        /* fluid → 对流路径 */
        p->enc_id = next_enc_id;
        p->phase = PATH_COUPLED_CONVECTIVE;
    } else {
        /* 辐射 → 辐射路径 */
        p->phase = PATH_COUPLED_RADIATIVE;
    }

    return RES_OK;
}
```

### 5.6 射线请求设置辅助函数

```c
/*
 * 设置辐射路径trace_ray请求。
 * 对应: sdis_heat_path_radiative_Xd.h 中 trace_ray() 的filter_data设置
 */
static void setup_radiative_trace_ray(
    struct path_state* p,
    struct sdis_scene* scn)
{
    float pos[3] = {(float)p->position[0],
                    (float)p->position[1],
                    (float)p->position[2]};

    p->ray_req.origin[0] = pos[0];
    p->ray_req.origin[1] = pos[1];
    p->ray_req.origin[2] = pos[2];

    p->ray_req.direction[0] = p->rad_direction[0];
    p->ray_req.direction[1] = p->rad_direction[1];
    p->ray_req.direction[2] = p->rad_direction[2];

    p->ray_req.range[0] = 0.0f;
    p->ray_req.range[1] = FLT_MAX;

    /* 设置filter_data（自交避免 + enclosure过滤） */
    p->filter_data_storage.hit_3d = p->hit_3d;
    p->filter_data_storage.epsilon = 1.e-6;
    p->filter_data_storage.scn = scn;
    p->filter_data_storage.enc_id = p->enc_id;

    p->ray_req.filter_data = S3D_HIT_NONE(&p->hit_3d) ?
                             NULL : &p->filter_data_storage;

    p->ray_req.ray_count = 1;
    p->needs_ray = 1;
}

/*
 * 设置delta_sphere双向探测射线请求。
 * 对应: sdis_heat_path_conductive_delta_sphere_Xd.h 中 sample_next_step()
 */
static void setup_delta_sphere_rays(
    struct path_state* p,
    struct sdis_scene* scn)
{
    float pos[3] = {(float)p->position[0],
                    (float)p->position[1],
                    (float)p->position[2]};

    /* 随机球面方向 */
    float dir[3];
    ssp_rng_sphere_uniform(p->rng, dir);

    p->ds_dir0[0] = dir[0];
    p->ds_dir0[1] = dir[1];
    p->ds_dir0[2] = dir[2];
    p->ds_dir1[0] = -dir[0];
    p->ds_dir1[1] = -dir[1];
    p->ds_dir1[2] = -dir[2];

    /* 第1条射线 */
    memcpy(p->ray_req.origin, pos, 3 * sizeof(float));
    memcpy(p->ray_req.direction, p->ds_dir0, 3 * sizeof(float));
    p->ray_req.range[0] = 0.0f;
    p->ray_req.range[1] = (float)p->ds_delta_solid;
    p->ray_req.filter_data = NULL;  /* delta_sphere不用filter */

    /* 第2条射线 */
    memcpy(p->ray_req.direction2, p->ds_dir1, 3 * sizeof(float));
    p->ray_req.range2[0] = 0.0f;
    p->ray_req.range2[1] = (float)p->ds_delta_solid;
    p->ray_req.filter_data2 = NULL;

    p->ray_req.ray_count = 2;
    p->needs_ray = 1;
}
```

---

## 六、不需要射线的步骤处理

部分路径阶段不需要射线追踪（纯CPU计算），这些步骤在 `advance_all_paths` 中**一次处理完**（不等待下一轮wavefront）。

**关键**: 对于不需要射线的步骤，可以在同一轮中连续执行多步，直到遇到需要射线的步骤或路径结束。

```c
/*
 * 改进版 advance_all_paths: 对不需要射线的步骤立即推进
 */
static res_T advance_all_paths(struct wavefront_context* wf, struct sdis_scene* scn)
{
    for (size_t i = 0; i < wf->total_paths; i++) {
        struct path_state* p = &wf->paths[i];
        if (!p->active) continue;

        /* 分发射线结果 */
        if (p->needs_ray) {
            p->ray_result_0 = wf->ray_hits[p->ray_req.batch_idx];
            if (p->ray_req.ray_count >= 2)
                p->ray_result_1 = wf->ray_hits[p->ray_req.batch_idx2];
            p->needs_ray = 0;
        }

        /* 持续推进直到需要射线或路径结束 */
        int max_steps = 100;  /* 安全限制防止无限循环 */
        while (p->active && !p->needs_ray && max_steps-- > 0) {
            res_T res = advance_one_step(p, scn);
            if (res != RES_OK) return res;

            if (p->T_done) {
                p->active = 0;
                p->phase = PATH_DONE;
            }
        }
    }
    return RES_OK;
}
```

这样可以减少不必要的wavefront同步点，提高效率。典型的不需要射线的步骤：

- `PATH_COUPLED_BOUNDARY` → 检查温度/决定下一步 → 可能立即转到 conductive/convective
- `PATH_COUPLED_CONDUCTIVE` → 检查limit条件 → 可能立即完成
- `PATH_COUPLED_CONVECTIVE` → 表面采样（不需要trace_ray）→ 可能立即完成

---

## 七、详细实施步骤

### Step 1: 创建 `sdis_solve_wavefront.h` 头文件

**新文件**: `stardis-solver/0.16.2/src/sdis_solve_wavefront.h`  
**内容**: `path_state`, `wavefront_context`, `path_phase` 枚举, 辅助函数前向声明

### Step 2: 创建 `sdis_solve_wavefront.c` 实现

**新文件**: `stardis-solver/0.16.2/src/sdis_solve_wavefront.c`

实现以下函数:

| 函数 | 行数估计 | 说明 |
|------|---------|------|
| `solve_tile_wavefront` | ~100 | 主入口，分配/释放上下文 |
| `init_all_paths` | ~80 | 初始化所有路径（RNG, 相机射线） |
| `collect_ray_requests` | ~40 | 收集射线请求到batch |
| `advance_all_paths` | ~50 | 分发结果并推进 |
| `advance_one_step` | ~10 | switch dispatch |
| `step_radiative_trace` | ~80 | 辐射路径hit处理 + BRDF |
| `step_conductive_delta_sphere` | ~60 | delta_sphere 2-ray结果处理 |
| `step_boundary` | ~60 | 边界路径决策 |
| `step_boundary_reinject` | ~50 | 重注入射线结果处理 |
| `step_convective` | ~40 | 对流路径采样 |
| `step_coupled_radiative_begin` | ~30 | 从boundary转入辐射 |
| `setup_radiative_trace_ray` | ~30 | 设置辐射trace请求 |
| `setup_delta_sphere_rays` | ~30 | 设置2-ray请求 |
| `setup_reinject_rays` | ~30 | 设置重注入请求 |
| `collect_results` | ~30 | 收集结果到tile |
| `update_active_count` | ~10 | 统计活跃路径 |

总计: **~730行**

### Step 3: 修改 `sdis_solve_camera.c`

**修改文件**: `stardis-solver/0.16.2/src/sdis_solve_camera.c`

在 `solve_tile` 调用处添加 wavefront 模式分支:

```c
/* 在 sdis_solve_camera() 中 */
if (use_wavefront_mode) {
    res = solve_tile_wavefront(scn, per_thread_rng[ithread], enc_id,
                               cam, time_range, tile_org, tile_size,
                               spp, register_paths, pix_sz,
                               picard_order, diff_algo, buf, tile);
} else {
    res = solve_tile(scn, per_thread_rng[ithread], enc_id,
                     cam, time_range, tile_org, tile_size,
                     spp, register_paths, pix_sz,
                     picard_order, diff_algo, buf, tile);
}
```

**控制方式**: 通过环境变量或命令行参数:
```c
int use_wavefront_mode = (getenv("STARDIS_WAVEFRONT") != NULL);
```

或通过 `sdis_solve_camera_args` 新增字段:
```c
struct sdis_solve_camera_args {
    /* ... 现有字段 ... */
    int use_wavefront;    /* 新增: 是否使用wavefront模式 */
};
```

### Step 4: Per-path RNG 分配

**修改**: 在 `wavefront_context` 中添加 RNG 数组:

```c
struct wavefront_context {
    /* ... */
    struct ssp_rng*  path_rngs;  /* 每路径独立RNG */
};
```

初始化:
```c
wf.path_rngs = (struct ssp_rng*)malloc(total_paths * sizeof(struct ssp_rng));
for (size_t i = 0; i < total_paths; i++) {
    uint64_t seed = base_seed + (uint64_t)i * 0x9E3779B97F4A7C15ULL;
    ssp_rng_init(&wf.path_rngs[i], seed);
}
```

### Step 5: 修改 CMakeLists.txt

在 stardis-solver 的源文件列表中添加:
```cmake
src/sdis_solve_wavefront.c
```

### Step 6: 编写验证测试

**新文件**: `stardis-solver/0.16.2/test/test_sdis_wavefront.c`

```c
/*
 * 验证策略:
 * 1. 用固定seed的RNG运行原始solve_tile，记录每个像素的温度
 * 2. 用固定seed的per-path RNG运行wavefront solve_tile
 * 3. 对比像素温度（统计意义上相等：均值差 < 0.1%）
 *
 * 注意: 由于RNG序列不同（per-path vs shared），bit-exact验证
 *       只能用方案B（RNG回放）实现，常规测试用统计验证。
 */
```

### Step 7: 性能基准测试

```c
/*
 * 测试配置:
 * - 场景: stardis-solver test suite 的标准测试场景
 * - tile_size: 4×4
 * - SPP: 64, 256, 1024
 * - 模式: original vs wavefront
 * - 指标: 总耗时, 射线追踪耗时, GPU利用率
 */
```

---

## 八、步进函数与原始代码的精确映射

为确保物理正确性，每个 `step_*` 函数必须与原始代码精确对应。下表给出映射关系:

| step函数 | 原始代码位置 | 原始函数 | 映射段落 |
|----------|-------------|----------|---------|
| `step_radiative_trace` | `sdis_heat_path_radiative_Xd.h` | `trace_radiative_path_Xd` | L190-280: find_next_frag返回后 → miss/hit判断 → BRDF采样 |
| `step_conductive_delta_sphere` | `sdis_heat_path_conductive_delta_sphere_Xd.h` | `conductive_path_delta_sphere` | L90-175: sample_next_step返回后 → delta计算 → 移动 → 界面检查 |
| `step_boundary` | `sdis_heat_path_boundary_Xd.h` | `boundary_path` | L30-120: 获取interface → 温度已知? → solid/fluid判断 |
| `step_boundary_reinject` | `sdis_heat_path_boundary_Xd_c.h` | `find_reinjection_ray` 后的处理 | L307-450: 2-ray结果 → 选择方向 → 执行重注入 |
| `step_convective` | `sdis_heat_path_convective_Xd.h` | `convective_path` | L100-340: 表面采样 → hc判断 → 接受/拒绝 |
| `step_coupled_radiative_begin` | `sdis_heat_path_radiative_Xd.h` | `radiative_path` | L20-40: 从boundary出发 → 半球采样方向 → 发射trace |

**实施建议**: 逐个 `step_*` 函数实现，每完成一个立即与原始代码做单元级验证。不要一次性实现所有函数。

---

## 九、特殊情况处理

### 9.1 find_next_fragment 的重试逻辑

原始 `find_next_fragment` 最多重试10次（通过 `move_away_primitive_boundaries` 微调射线起点）。在wavefront模式下:

```c
/* 在 step_radiative_trace 中 */
if (interface_check_failed) {
    p->rad_retry_count++;
    if (p->rad_retry_count < 10) {
        /* 微调位置: 沿某方向偏移epsilon */
        move_away_primitive_boundaries(p);
        setup_radiative_trace_ray(p, scn);
        p->phase = PATH_RAD_TRACE_PENDING;
        p->needs_ray = 1;
        return RES_OK;  /* 下一轮wavefront重新trace */
    }
}
```

每次重试需要一个完整的 wavefront cycle。这是可以接受的，因为重试概率低（<5% 的射线需要重试）。

### 9.2 convective_path 的表面采样

对流路径的核心操作是 enclosure 表面采样（`s3d_scene_view_sample`），不需要 `trace_ray`。唯一需要 `trace_ray` 的是 startup 阶段获取初始 hit（当路径从体内开始时）。

在wavefront模式下，对流路径的大部分计算是无射线的CPU工作，可以在 `advance_all_paths` 的持续推进循环中快速完成。

### 9.3 WoS路径的closest_point

WoS 算法使用 `closest_point`（CPU暴力搜索）+ 偶发的 `trace_ray`。WoS 路径的wavefront适配需要:

1. `closest_point` 保持串行CPU调用（不在batch范围内）
2. WoS 的 `trace_ray` 按正常flow加入batch

如果WoS在场景中不常用，可以延后实现，先用串行fallback。

### 9.4 路径死亡和压缩

随着蒙特卡洛采样进行，路径逐渐完成。wavefront 后期活跃路径减少，batch size 降低，GPU 利用率下降。

**压缩策略**（可选优化）:
```c
/*
 * 当 active_count / total_paths < 0.25 时，
 * 压缩paths数组，将活跃路径移到前面。
 * 减少 collect_ray_requests 的遍历开销。
 */
static void compact_active_paths(struct wavefront_context* wf)
{
    size_t write = 0;
    for (size_t read = 0; read < wf->total_paths; read++) {
        if (wf->paths[read].active) {
            if (write != read)
                wf->paths[write] = wf->paths[read];
            write++;
        }
    }
    wf->active_count = write;
}
```

### 9.5 `RES_BAD_OP_IRRECOVERABLE` (-5) 错误处理缺陷（★已确认★）

> **状态**: 2026-02-11 确认。该缺陷在含固体导热的场景中已复现，
> 导致整个 tile 因单条路径失败而终止。

#### 9.5.1 问题描述

`solve_tile_wavefront` 在运行含固体导热区域的场景时，偶发返回 `-5`
（`RES_BAD_OP_IRRECOVERABLE`），导致整个 tile 的所有路径被丢弃。
该错误在原始串行 CPU 实现中**不会出现**。

错误码定义：

```c
#define RES_BAD_OP 5                         // rsys.h
#define RES_BAD_OP_IRRECOVERABLE (-RES_BAD_OP) // = -5, sdis_misc.h
```

#### 9.5.2 触发位置

| # | 位置 | 条件 | 含义 |
|---|------|------|------|
| 1 | `step_radiative_trace` L532 | `sdis_medium_get_type(chk_mdm) == SDIS_SOLID` | 辐射路径反弹后命中固体侧界面 |
| 2 | `step_conductive` L730 | `enc_id != p->rwalk.enc_id` | delta-sphere 传导路径的 enclosure 不一致 |

#### 9.5.3 原始串行代码的处理（正确行为）

原始代码在 `sample_coupled_path`（`sdis_realisation_Xd.h` L86-167）中有 **三层保护**：

```
层1: do-while 重试 + rwalk 状态恢复
  ┌────────────────────────────────────────────────────────┐
  │ const struct rwalk rwalk_bkp = *rwalk;  // 备份状态    │
  │ const struct temperature T_bkp = *T;                   │
  │ do {                                                   │
  │   res = T->func(scn, ctx, rwalk, rng, T);              │
  │   if(res == RES_BAD_OP) { *rwalk = rwalk_bkp; *T=T_bkp; }│
  │ } while(res == RES_BAD_OP && ++nfails < MAX_FAILS);    │
  └────────────────────────────────────────────────────────┘

层2: -5 → +5 降级（sample_coupled_path 出口）
  return res == RES_BAD_OP_IRRECOVERABLE ? RES_BAD_OP : res;
  // 所有上层调用者仅看到 RES_BAD_OP(+5)，永远看不到 -5

层3: solve_pixel 的失败隔离
  res_simul = ray_realisation_3d(scn, &realis_args, &w);
  if(res_simul != RES_OK && res_simul != RES_BAD_OP)
      → 致命退出（但+5不会触发）
  // +5: 路径标记为失败，不计入像素统计，继续下一个 realisation
```

**最终效果**：单路径的 `-5` 被降级为 `+5` → 路径标记失败 → 跳过该 realisation → **不影响其他路径和像素**。

#### 9.5.4 Wavefront 实现的处理（缺陷行为）

Wavefront 中 `distribute_and_advance` 缺少上述三层保护：

```c
// distribute_and_advance() L1272-1278（当前代码）
res = advance_one_step_with_ray(p, scn, h0, h1);  // step_radiative_trace 返回 -5
if(res != RES_OK && res != RES_BAD_OP) return res; // -5≠0 且 -5≠5 → return -5 !
if(res == RES_BAD_OP) {                            // 此分支仅处理 +5
    p->phase = PATH_DONE;
    p->active = 0;
    wf->paths_failed++;
    res = RES_OK;
}
```

`-5` 不等于 `RES_BAD_OP`(5)，所以直接 `return res` → `solve_tile_wavefront` 收到 `-5` → `goto cleanup` → **整个 tile 终止，所有剩余活跃路径丢失**。

#### 9.5.5 调用链等价性对比

| 层级 | 机制 | 原始串行 | Wavefront | 等价？ |
|------|------|---------|-----------|--------|
| 产生 | 介质类型检测 | `RES_BAD_OP_IRRECOVERABLE` | 同左 | ✅ |
| 降级 | `-5 → +5` 转换 | `sample_coupled_path` 出口 | **缺失** | ❌ |
| 重试 | `RES_BAD_OP` 后恢复 rwalk 再执行 | `do-while + rwalk_bkp` | **缺失** | ❌ |
| 失败隔离 | 单路径失败不影响其他路径 | `solve_pixel` 跳过 realisation | `-5` 终止整个 tile | ❌ |
| `check_interface` 重试 | 重试射线的起点 | 从**原始起点**微调 `rt_pos` | 从**hit 位置**微调 `p->rwalk.vtx.P` | ⚠️ |
| 位置来源 | hit 位置赋值 | `frag.P`（由 `find_next_fragment` 返回） | `pos + dir * distance`（自行重建） | ⚠️ |

#### 9.5.6 为何 RNG 差异是间接触发条件

Wavefront 中所有路径共享同一个 RNG（`p->rng = base_rng`），但由于执行顺序从
"深度优先"变为"广度优先"，RNG 调用顺序被打乱：

```
原始:     path0 全部RNG调用 → path1 全部RNG调用 → ...
Wavefront: path0_init → path1_init → ... → pathN_init → path0_brdf → path3_brdf → ...
```

不同的随机数序列导致路径走到不同的几何位置，增大了触碰边界数值敏感区域的概率。
但 **RNG 差异本身不是 bug**——即使用 per-path 独立 RNG，只要缺少 `-5` 降级机制，
同样的错误仍会以一定概率出现。

#### 9.5.7 修复方案

**修复 A（必须）：将 `-5` 等同于 `+5` 处理为路径失败**

在 `distribute_and_advance` 的两处错误检查中加入 `RES_BAD_OP_IRRECOVERABLE`：

```c
/* distribute_and_advance() — 有射线结果的路径推进 */
res = advance_one_step_with_ray(p, scn, h0, h1);
if(res != RES_OK && res != RES_BAD_OP
&& res != RES_BAD_OP_IRRECOVERABLE) return res;    /* ← 新增 */
if(res == RES_BAD_OP || res == RES_BAD_OP_IRRECOVERABLE) { /* ← 修改 */
    p->phase = PATH_DONE;
    p->active = 0;
    p->done_reason = -1;
    wf->paths_failed++;
    res = RES_OK;
}

/* distribute_and_advance() — 无射线步骤的级联推进 */
res = advance_one_step_no_ray(p, scn, &advanced);
if(res != RES_OK && res != RES_BAD_OP
&& res != RES_BAD_OP_IRRECOVERABLE) return res;    /* ← 新增 */
if(res == RES_BAD_OP || res == RES_BAD_OP_IRRECOVERABLE) { /* ← 修改 */
    p->phase = PATH_DONE;
    p->active = 0;
    p->done_reason = -1;
    wf->paths_failed++;
    res = RES_OK;
    break;
}
```

同样修改 `solve_tile_wavefront` 的初始步进循环（L1430-1451）：

```c
res = advance_one_step_no_ray(p, scn, &advanced);
if(res != RES_OK && res != RES_BAD_OP
&& res != RES_BAD_OP_IRRECOVERABLE) goto cleanup;  /* ← 新增 */
if(res == RES_BAD_OP || res == RES_BAD_OP_IRRECOVERABLE) { /* ← 修改 */
    p->phase = PATH_DONE;
    p->active = 0;
    p->done_reason = -1;
    res = RES_OK;
    break;
}
```

**修复 B（可选改进）：位置计算对齐原始实现**

`step_radiative_trace` 中 hit 位置应使用 `frag.P` 而非自行重建 `pos + dir * distance`，
与原始 `trace_radiative_path` 中 `d3_set(rwalk->vtx.P, frag.P)` 保持一致。
`find_next_fragment` 注释中明确指出："Do not use the sampled direction and distance
to the hit point to calculate the new position"（sdis_heat_path_radiative_Xd.h L253-258）。

```c
/* step_radiative_trace() — 当前代码（有偏差） */
d3_set(pos_d, p->rwalk.vtx.P);
d3_add(pos_d, pos_d, d3_muld(vec, dir_d, trace_hit->distance));
d3_set(p->rwalk.vtx.P, pos_d);

/* 应改为使用 frag.P（与原始一致） */
d3_set(p->rwalk.vtx.P, frag.P);
```

此修改可减少 `check_interface` 重试触发概率，降低 `-5` 出现的频率。

**修复 C（可选改进）：`check_interface` 重试起点对齐**

原始 `find_next_fragment` 在重试时从**原始射线起点**（`rt_pos`，即发射射线前的位置）
微调后重新 trace。Wavefront 当前实现从**hit 位置**微调后重新 trace。应保存发射射线前
的位置到 `path_state`，重试时从该位置微调。

---

## 十、里程碑与验证计划

```
  M1: 骨架编译    M2: 辐射路径    M3: 传导路径    M4: 全路径     M5: 性能验证
  ───────┬─────────┬───────────────┬───────────────┬─────────────┬────────
        2h          8h              6h              6h            4h
```

### 里程碑 M1: 骨架编译通过（~2小时）

- `sdis_solve_wavefront.h/c` 创建，含 `path_state` 和 `wavefront_context` 定义
- `solve_tile_wavefront` 空实现（直接调用原始 `solve_tile` 作为fallback）
- CMake编译通过

### 里程碑 M2: 仅辐射路径的wavefront（~8小时）

- 实现 `step_radiative_trace` 完整逻辑
- 仅支持 `PATH_INIT → PATH_RAD_TRACE_PENDING → (bounce loop) → PATH_DONE`
- 测试: 纯辐射场景（无传导/对流），像素温度统计验证

### 里程碑 M3: 加入传导路径（~6小时）

- 实现 `step_conductive_delta_sphere`, `step_boundary`, `step_boundary_reinject`
- 支持 `radiative → boundary → conductive → boundary → ...` 循环
- 测试: 含固体传导的场景

### 里程碑 M4: 全路径类型支持（~6小时）

- 实现 `step_convective`, `step_coupled_radiative_begin`
- WoS 路径 fallback（如需要）
- 测试: 完整热传输场景，所有路径类型

### 里程碑 M5: 性能基准（~4小时）

- A/B对比测试: original vs wavefront
- batch size 统计收集
- 端到端加速比测量
- GPU利用率比较（nvidia-smi / Nsight Systems）

---

## 十一、风险与缓解

| 风险 | 严重度 | 概率 | 缓解 |
|------|--------|------|------|
| RNG序列不同导致物理结果不一致 | 低 | 确定 | per-path RNG产生不同序列是预期行为;用大SPP统计验证均值差<0.1% |
| **`RES_BAD_OP_IRRECOVERABLE`(-5) 未降级导致 tile 终止** | **高** | **已确认** | **§9.5 已分析并给出修复方案；必须将 -5 等同于 +5 处理为路径失败** |
| 路径状态提取遗漏导致计算错误 | 高 | 中 | 逐步骤与原始代码逐行对比;单步调试验证 |
| path_state过大导致内存不足 | 中 | 低 | 16×256=4096路径×500B≈2MB/tile，远在可接受范围 |
| wavefront step间状态不一致 | 高 | 中 | 每个step函数最后显式设置phase和needs_ray |
| 压缩后path_id变化导致映射混乱 | 高 | 低 | 压缩时同步更新ray_to_path映射;或禁用压缩 |
| 某些路径步骤极长（WoS循环） | 中 | 低 | WoS路径先用串行fallback |
| filter_data生命周期问题 | 高 | 中 | filter_data_storage存在path_state中，不会被释放 |

---

## 十二、后续优化方向

1. **批量Top-K**: 新增 `cus3d_trace_ray_batch_multi` 内核，消除filter re-trace
2. **路径排序**: 按phase排序活跃路径，提高cache命中率
3. **多tile合并**: 将多个tile的射线合并到同一batch，增大batch size
4. **异步计算流水线**: GPU trace与CPU后处理重叠执行
5. **GPU Filter**: 将filter逻辑上移到GPU内核
6. **Adaptive SPP**: 方差自适应采样率，提前终止低方差像素

---

## 十三、与 Phase B-1 的接口契约

Phase B-2 通过以下接口调用 Phase B-1:

```c
/* 创建时 */
s3d_batch_trace_context_create(&wf.batch_ctx, max_rays);

/* 每个wavefront步骤 */
s3d_scene_view_trace_rays_batch_ctx(
    scn->s3d_view,          /* 场景视图 */
    wf.batch_ctx,            /* 预分配上下文 */
    wf.ray_requests,         /* 射线请求数组 (s3d_ray_request[]) */
    wf.ray_count,            /* 本步射线数 */
    wf.ray_hits,             /* 输出: s3d_hit[] */
    &stats);                 /* 输出: 统计（可选） */

/* 销毁时 */
s3d_batch_trace_context_destroy(wf.batch_ctx);
```

**B-1保证**:
- `ray_hits[i]` 对应 `ray_requests[i]`，保持索引一致
- 无filter射线: 结果等价于 `s3d_scene_view_trace_ray(..., NULL, ...)`
- 有filter射线: 结果等价于 `s3d_scene_view_trace_ray(..., filter_data, ...)`（fallback到Top-K）
- 线程安全: 每个OMP线程使用独立的 `batch_ctx`，不共享状态

---

*文档更新: 2026-02-11 | Phase B-2 完整实现步骤指南（含 §9.5 RES_BAD_OP_IRRECOVERABLE 修复方案）*
