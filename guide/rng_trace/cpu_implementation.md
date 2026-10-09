# CPU 平台实现方案

**工作目录**: `stardis-cpu-rngtrace/`  
**目标**: 在 CPU 深度优先求解器中集成 per-path CBRNG + RNG Trace。

---

## 1. 现有 CPU RNG 架构

### 1.1 调用链

```
sdis_solve_camera()                        ← sdis_solve_camera.c
  └── create_per_thread_rng()              ← sdis.c L303
        ├── ssp_rng_proxy_create2(proxy)   ← ssp_rng_proxy.c
        └── ssp_rng_proxy_create_rng(rng[thread_id])
              └── bucket → pool 机制
  └── solve_tile()                         ← sdis_solve_camera.c L197
        └── FOR_EACH pixel(Morton Z-curve)
              └── solve_pixel(scn, rng, ...)  ← 传入 per-thread rng
                    └── FOR_EACH irealisation(0..spp-1)
                          ├── sample_time(rng, time_range)      ← RNG call #0
                          ├── ssp_rng_canonical(rng)            ← RNG call #1 (pixel sub_x)
                          ├── ssp_rng_canonical(rng)            ← RNG call #2 (pixel sub_y)
                          ├── camera_ray(cam, samp, ...)
                          └── ray_realisation_3d(scn, &args, &w) ← RNG calls #3..#N
                                ├── sample_next_step() → ssp_ran_sphere_uniform_float()
                                ├── boundary handling → ssp_rng_canonical() × M
                                ├── brdf_sample() → ssp_ran_hemisphere_cos()
                                └── conductive delta-sphere → ssp_ran_sphere_uniform_float()
```

### 1.2 关键约束

- `solve_pixel` 中所有 `nrealisations` 条路径**串行执行**
- 所有路径共享同一个 `rng` 指针
- 路径 i 消耗的随机数紧接在路径 i-1 之后
- 每条路径消耗的随机数数量不固定（取决于 bounce 数、retry 数等）

---

## 2. 改造方案

### 2.1 新增文件

#### `star-sp/0.15/src/ssp_path_rng.h`

```c
/* Per-path Counter-Based RNG using Threefry4x64.
 *
 * Each Monte-Carlo path is identified by (pixel_x, pixel_y, spp_idx).
 * The identity is encoded into the Threefry key; the counter increments
 * monotonically within the path.  This guarantees that:
 *   - The same (pixel, spp) path produces the same RNG sequence
 *     regardless of execution order (depth-first or wavefront).
 *   - Different paths have statistically independent streams
 *     (Threefry guarantees 2^256 period per key). */

#ifndef SSP_PATH_RNG_H
#define SSP_PATH_RNG_H

#include <stdint.h>

/* Threefry4x64 C API from Random123 */
#include <Random123/threefry.h>

struct path_rng {
  uint64_t key[2];     /* [0] = pixel_linear_id                        */
                       /* [1] = spp_idx ^ (global_seed << 32)          */
  uint64_t counter;    /* Threefry invocation counter (0, 1, 2, ...)   */
  uint64_t buf[4];     /* Output buffer from last Threefry call        */
  int      buf_idx;    /* Next unused element in buf (0..3); 4 = empty */

#if SDIS_RNG_TRACE
  uint64_t total_calls; /* Total RNG values consumed (for trace)       */
#endif
};

/* Initialise a per-path RNG.
 * pixel_x, pixel_y: image-space pixel coordinates.
 * image_width: image width (for linear index computation).
 * spp_idx: realisation index (0..spp-1).
 * global_seed: deterministic seed (typically 0 or from command line). */
static inline void
path_rng_init(struct path_rng* prng,
              uint32_t pixel_x, uint32_t pixel_y,
              uint32_t image_width, uint32_t spp_idx,
              uint64_t global_seed)
{
  prng->key[0] = (uint64_t)pixel_y * image_width + pixel_x;
  prng->key[1] = (uint64_t)spp_idx ^ (global_seed << 32);
  prng->counter = 0;
  prng->buf_idx = 4;  /* force refill on first call */
#if SDIS_RNG_TRACE
  prng->total_calls = 0;
#endif
}

/* Generate next raw uint64_t */
static inline uint64_t
path_rng_next_u64(struct path_rng* prng)
{
  if(prng->buf_idx >= 4) {
    threefry4x64_ctr_t ctr = {{prng->counter, 0, 0, 0}};
    threefry4x64_key_t key = {{prng->key[0], prng->key[1], 0, 0}};
    threefry4x64_ctr_t out = threefry4x64(ctr, key);
    prng->buf[0] = out.v[0];
    prng->buf[1] = out.v[1];
    prng->buf[2] = out.v[2];
    prng->buf[3] = out.v[3];
    prng->counter++;
    prng->buf_idx = 0;
  }
#if SDIS_RNG_TRACE
  prng->total_calls++;
#endif
  return prng->buf[prng->buf_idx++];
}

/* [0, 1) double — matches ssp_rng_canonical normalisation */
static inline double
path_rng_canonical(struct path_rng* prng)
{
  /* 53-bit mantissa: shift right by 11, then multiply by 2^-53.
   * This is the same normalisation used by std::generate_canonical
   * for 64-bit engines with double output. */
  return (double)(path_rng_next_u64(prng) >> 11) * 0x1.0p-53;
}

/* [0, 1) float — matches ssp_rng_canonical_float normalisation */
static inline float
path_rng_canonical_float(struct path_rng* prng)
{
  /* 24-bit mantissa: shift right by 40, then multiply by 2^-24. */
  return (float)(path_rng_next_u64(prng) >> 40) * 0x1.0p-24f;
}

#endif /* SSP_PATH_RNG_H */
```

#### `star-sp/0.15/src/ssp_path_rng_trace.h`

```c
/* RNG Trace instrumentation for per-path CBRNG.
 *
 * When SDIS_RNG_TRACE is defined and the runtime environment variables
 * STARDIS_TRACE_PIXEL=x,y and STARDIS_TRACE_SPP=n are set, every RNG
 * call for the target path is logged with:
 *   - Call index (monotonic within the path)
 *   - Raw uint64 value
 *   - Caller function name and line number
 *   - Current state machine phase (wavefront only; CPU uses DEPTH_FIRST)
 *
 * Compile with -DSDIS_RNG_TRACE=1 to enable. */

#ifndef SSP_PATH_RNG_TRACE_H
#define SSP_PATH_RNG_TRACE_H

#include "ssp_path_rng.h"
#include <stdio.h>

#if SDIS_RNG_TRACE

/* Global trace filter (initialised from env vars at startup) */
struct rng_trace_config {
  int      enabled;        /* 1 = trace active                            */
  uint32_t pixel_x;
  uint32_t pixel_y;
  uint32_t spp_idx;
  FILE*    output;         /* trace output stream (default: stderr)       */
};

/* Call once at program startup (before any path_rng usage) */
void rng_trace_init_from_env(struct rng_trace_config* cfg);

/* Close output file if not stderr */
void rng_trace_cleanup(struct rng_trace_config* cfg);

/* Check if a path matches the trace filter */
static inline int
rng_trace_matches(const struct rng_trace_config* cfg,
                  uint32_t px, uint32_t py, uint32_t spp)
{
  return cfg->enabled
      && cfg->pixel_x == px
      && cfg->pixel_y == py
      && cfg->spp_idx == spp;
}

/* Log one RNG call.  'phase_name' is a string for the current state
 * (e.g. "PATH_INIT", "BND_DISPATCH", or "DEPTH_FIRST" for CPU). */
static inline void
rng_trace_log(const struct rng_trace_config* cfg,
              const struct path_rng* prng,
              uint64_t value,
              const char* phase_name,
              const char* func, int line)
{
  fprintf(cfg->output,
    "RNG[%6llu] = 0x%016llx  phase=%-28s  %s:%d\n",
    (unsigned long long)(prng->total_calls - 1),
    (unsigned long long)value,
    phase_name, func, line);
}

/* Traced version of path_rng_canonical.
 * Usage:  val = PATH_RNG_CANONICAL_TRACED(&prng, &g_trace, px, py, spp, phase) */
#define PATH_RNG_CANONICAL_TRACED(prng, cfg, px, py, spp, phase_str) \
  path_rng_canonical_traced_impl(prng, cfg, px, py, spp, phase_str, __func__, __LINE__)

static inline double
path_rng_canonical_traced_impl(
  struct path_rng* prng,
  const struct rng_trace_config* cfg,
  uint32_t px, uint32_t py, uint32_t spp,
  const char* phase_str,
  const char* func, int line)
{
  uint64_t raw = path_rng_next_u64(prng);
  if(rng_trace_matches(cfg, px, py, spp)) {
    rng_trace_log(cfg, prng, raw, phase_str, func, line);
  }
  return (double)(raw >> 11) * 0x1.0p-53;
}

/* Convenience macros for use in solver code.  g_rng_trace_cfg must be
 * a visible struct rng_trace_config.
 *
 * In CPU depth-first code (state machine not used):
 *   double r = PATH_RNG(prng, "DEPTH_FIRST");
 *
 * In GPU wavefront code:
 *   double r = PATH_RNG(prng, path_phase_name(p->phase));
 */
#define PATH_RNG(p_prng, p_phase_str) \
  PATH_RNG_CANONICAL_TRACED(p_prng, &g_rng_trace_cfg, \
    g_trace_px, g_trace_py, g_trace_spp, p_phase_str)

#else /* !SDIS_RNG_TRACE */

/* No-op when tracing is disabled */
#define PATH_RNG(p_prng, p_phase_str) path_rng_canonical(p_prng)

#endif /* SDIS_RNG_TRACE */

#endif /* SSP_PATH_RNG_TRACE_H */
```

### 2.2 `solve_pixel` 改造

**文件**: `stardis-cpu-rngtrace/stardis-solver/0.16.2/src/sdis_solve_camera.c`

**改造前**（现有代码）：
```c
static res_T
solve_pixel
  (struct sdis_scene* scn,
   struct ssp_rng* rng,              /* ← per-thread 共享 RNG */
   const unsigned enc_id,
   const struct sdis_camera* cam,
   const double time_range[2],
   const size_t ipix[2],
   const size_t nrealisations,
   ...)
{
  ...
  FOR_EACH(irealisation, 0, nrealisations) {
    ...
    time = sample_time(rng, time_range);                    /* ← 共享 RNG */
    samp[0] = ((double)ipix[0] + ssp_rng_canonical(rng)) * pix_sz[0];
    samp[1] = ((double)ipix[1] + ssp_rng_canonical(rng)) * pix_sz[1];
    ...
    realis_args.rng = rng;                                  /* ← 传递共享 RNG */
    ...
    ray_realisation_3d(scn, &realis_args, &w);
  }
}
```

**改造后**：
```c
static res_T
solve_pixel
  (struct sdis_scene* scn,
   struct ssp_rng* rng,              /* ← 保留参数签名兼容性（unused when CBRNG） */
   const unsigned enc_id,
   const struct sdis_camera* cam,
   const double time_range[2],
   const size_t ipix[2],
   const size_t nrealisations,
   ...,
   const uint64_t global_seed,       /* ← 新增参数 */
   const uint32_t image_width)       /* ← 新增参数 */
{
  ...
  FOR_EACH(irealisation, 0, nrealisations) {
    struct path_rng prng;             /* ← 每条路径独立 CBRNG */
    ...

    /* Per-path CBRNG: key = (pixel, spp, seed) */
    path_rng_init(&prng,
      (uint32_t)ipix[0], (uint32_t)ipix[1],
      image_width, (uint32_t)irealisation,
      global_seed);

    time = sample_time_prng(&prng, time_range);             /* ← 用 path_rng */
    samp[0] = ((double)ipix[0] + path_rng_canonical(&prng)) * pix_sz[0];
    samp[1] = ((double)ipix[1] + path_rng_canonical(&prng)) * pix_sz[1];
    ...
    realis_args.prng = &prng;                               /* ← 传递 per-path RNG */
    ...
    ray_realisation_3d(scn, &realis_args, &w);
  }
}
```

### 2.3 `ray_realisation_3d` 及下游函数改造

**核心结构变更**：

```c
/* sdis_realisation.h — ray_realisation_args 新增字段 */
struct ray_realisation_args {
  struct ssp_rng* rng;            /* 保留（置 NULL 当使用 CBRNG） */
  struct path_rng* prng;          /* 新增：per-path CBRNG（非 NULL 时优先） */
  ...
};
```

**下游 RNG 调用点改造模式**（约 30 处）：

```c
/* 改造前 */
ssp_rng_canonical(rng)

/* 改造后 — 方案 1: 宏桥接（最小改动） */
#define RNG_CANONICAL(args) \
  ((args)->prng ? path_rng_canonical((args)->prng) : ssp_rng_canonical((args)->rng))

/* 改造后 — 方案 2: 统一接口（更干净） */
/* 所有函数签名中将 struct ssp_rng* rng 替换为 struct path_rng* prng */
path_rng_canonical(prng)
```

**推荐方案 2**：统一替换，避免双轨并存。CPU 版本在 `stardis-cpu-rngtrace` 分支
上操作，不影响主分支。

### 2.4 高级采样函数适配

以下 `ssp_ran_*` 函数内部调用 `ssp_rng_canonical`，需要适配：

| 函数 | 文件 | RNG 调用数 | 改造方式 |
|------|------|:----------:|---------|
| `ssp_ran_sphere_uniform_float` | `ssp_ran.c` | 2 | 新增 `path_ran_sphere_uniform_float(prng, ...)` |
| `ssp_ran_hemisphere_cos` | `ssp_ran.c` | 2 | 新增 `path_ran_hemisphere_cos(prng, ...)` |
| `ssp_ran_hemisphere_cos_float` | `ssp_ran.c` | 2 | 新增 `path_ran_hemisphere_cos_float(prng, ...)` |
| `ssp_ran_circle_uniform_float` | `ssp_ran.c` | 2 | 新增 `path_ran_circle_uniform_float(prng, ...)` |
| `brdf_sample` | `sdis_brdf.c` | 间接（调上面的） | 签名传递 `prng` |

**关键**：这些函数的数学逻辑完全不变，只是将 `ssp_rng_canonical(rng)` 替换为
 `path_rng_canonical(prng)`。可创建包装文件 `ssp_path_ran.h`：

```c
/* ssp_path_ran.h — sampling functions using per-path CBRNG */

static inline void
path_ran_sphere_uniform_float(struct path_rng* prng,
                              float dir[3], float* pdf)
{
  /* Identical math to ssp_ran_sphere_uniform_float, different RNG source */
  double z = 2.0 * path_rng_canonical(prng) - 1.0;
  double phi = 2.0 * M_PI * path_rng_canonical(prng);
  double r = sqrt(1.0 - z * z);
  dir[0] = (float)(r * cos(phi));
  dir[1] = (float)(r * sin(phi));
  dir[2] = (float)z;
  if(pdf) *pdf = (float)(1.0 / (4.0 * M_PI));
}

/* ... 其他采样函数类似 ... */
```

### 2.5 `sample_time` 处理

`sample_time` 的 RNG 消耗取决于时间范围：

```c
/* sdis_solve_camera.c 中的 sample_time */
static inline double
sample_time(struct ssp_rng* rng, const double time_range[2])
{
  if(time_range[0] == time_range[1])
    return time_range[0];                 /* steady-state: 不消耗 RNG */
  return ssp_rng_uniform_double(rng, time_range[0], time_range[1]);  /* 消耗 1 */
}
```

**改造**：
```c
static inline double
sample_time_prng(struct path_rng* prng, const double time_range[2])
{
  if(time_range[0] == time_range[1])
    return time_range[0];                 /* steady-state: 不消耗 RNG */
  /* path_rng_canonical returns [0,1), scale to [lo, hi) */
  return time_range[0] + path_rng_canonical(prng) * (time_range[1] - time_range[0]);
}
```

**⚠ 注意**：`ssp_rng_uniform_double` 的归一化方式必须与 `path_rng_canonical` 完全一致，
才能保证 bit-exact。需验证 `ssp_rng_uniform_double` 的实现：
```c
/* ssp_rng.c */
double ssp_rng_uniform_double(struct ssp_rng* rng, double lo, double hi) {
  return lo + ssp_rng_canonical(rng) * (hi - lo);
}
```
→ 数学上等价，只需确保 `path_rng_canonical` 的归一化与 `ssp_rng_canonical` 相同。

---

## 3. 改造影响清单

### 3.1 需要修改的文件

| 文件 | 改动类型 | 改动量 |
|------|---------|:------:|
| `star-sp/0.15/src/ssp_path_rng.h` | **新增** | ~80 行 |
| `star-sp/0.15/src/ssp_path_rng_trace.h` | **新增** | ~100 行 |
| `star-sp/0.15/src/ssp_path_rng_trace.c` | **新增** | ~50 行 |
| `star-sp/0.15/src/ssp_path_ran.h` | **新增** | ~120 行 |
| `stardis-solver/0.16.2/src/sdis_solve_camera.c` | 修改 | ~30 行 |
| `stardis-solver/0.16.2/src/sdis_realisation.h` | 修改 | ~5 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_radiative_Xd.h` | 修改 | ~10 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_boundary_Xd_solid_solid.h` | 修改 | ~8 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_boundary_Xd_handle_external_net_flux.h` | 修改 | ~8 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_conductive_delta_sphere_Xd.h` | 修改 | ~6 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_conductive_wos_Xd.h` | 修改 | ~10 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_convective_Xd.h` | 修改 | ~10 行 |
| `stardis-solver/0.16.2/src/sdis_solve_boundary_Xd.h` | 修改 | ~8 行 |
| `stardis-solver/0.16.2/src/sdis_brdf.c` | 修改 | ~5 行 |
| `stardis-solver/0.16.2/src/sdis_path_trace.h` | **新增** | ~80 行 |
| `stardis-solver/0.16.2/src/test_sdis_rng_trace.c` | **新增** | ~200 行 |
| `CMakeLists.txt` | 修改 | ~10 行 |

### 3.2 不需要修改的文件

- `ssp_rng.c` / `ssp_rng_proxy.c` — 保留原有代码，不删除
- `ssp_rng_c.h` — 保留原有定义
- MPI 相关代码 — 不涉及

---

## 4. 构建集成

### 4.1 CMakeLists.txt 变更

```cmake
# star-sp/0.15/CMakeLists.txt 新增
option(SDIS_RNG_TRACE "Enable per-path RNG trace instrumentation" OFF)

if(SDIS_RNG_TRACE)
  target_compile_definitions(star-sp PUBLIC SDIS_RNG_TRACE=1)
  target_sources(star-sp PRIVATE src/ssp_path_rng_trace.c)
endif()
```

### 4.2 构建命令

```powershell
# 在 stardis-cpu-rngtrace 目录下
cd stardis-cpu-rngtrace
mkdir build-rngtrace; cd build-rngtrace
cmake -G "Visual Studio 17 2022" -A x64 -DSDIS_RNG_TRACE=ON ..
cmake --build . --config Release

# 不带 trace 的构建（验证不影响性能）
cmake -G "Visual Studio 17 2022" -A x64 -DSDIS_RNG_TRACE=OFF ..
cmake --build . --config Release
```

---

## 5. 测试计划

### 5.1 单元测试：`test_sdis_rng_trace.c`

| 测试 | 验证内容 |
|------|---------|
| `test_path_rng_determinism` | 相同 key 两次初始化产生完全相同的序列 |
| `test_path_rng_independence` | 不同 key 产生不同的序列 |
| `test_path_rng_canonical_range` | `path_rng_canonical` 返回值 ∈ [0, 1) |
| `test_path_rng_canonical_float_range` | `path_rng_canonical_float` ∈ [0, 1) |
| `test_path_rng_cross_validate_threefry` | 手动计算 Threefry4x64 结果并对比 |
| `test_path_rng_order_independence` | 以不同顺序消费同一 path 的 RNG，序列相同 |

### 5.2 集成测试：现有测试 + per-path RNG

在 `stardis-cpu-rngtrace` 中运行现有 CPU 测试（`ctest -C Release`），
验证所有测试通过（统计容差内）。注意：因为 RNG 序列从共享变为 per-path，
测试的数值结果将不同于原始版本，但应在统计容差内。

### 5.3 回归验证

| 场景 | 方法 | 预期 |
|------|------|------|
| 32x32 @ 256 SPP 探针温度 | 比较原始 CPU 和 CBRNG CPU 的均值 | 在 2σ 内一致 |
| 解析温度场 | 与解析解比较 | 相对误差 < 1% |
| 渲染图像 | 视觉对比 | 没有明显伪影 |

---

## 6. RNG 消耗点完整清单（CPU 深度优先）

以下是单条路径在 `ray_realisation_3d` 中所有 RNG 消耗点的有序列表。
CPU 和 GPU 必须在每个消耗点使用相同的 `path_rng` 操作。

| # | 位置 | 函数 | 消耗数 | 说明 |
|:-:|------|------|:------:|------|
| 0 | `solve_pixel` | `sample_time_prng` | 0 或 1 | steady-state=0, transient=1 |
| 1 | `solve_pixel` | `path_rng_canonical` | 1 | pixel sub_x |
| 2 | `solve_pixel` | `path_rng_canonical` | 1 | pixel sub_y |
| 3 | `radiative_Xd.h` | `path_ran_sphere_uniform_float` | 2 | 初始辐射方向 |
| 4+ | `radiative_Xd.h` | `ssp_rng_canonical` → emissivity dice | 1/反弹 | 每次边界反弹 |
| 5+ | `radiative_Xd.h` | `path_ran_hemisphere_cos_float` | 2/反弹 | BRDF 反射方向 |
| 6+ | `boundary_solid_solid.h` | `ssp_rng_canonical` | 1 | 概率分支 |
| 7+ | `boundary_solid_solid.h` | `ssp_ran_circle_uniform_float` × 4 + retry | 变量 | 4 方向重注入 |
| 8+ | `conductive_delta_sphere_Xd.h` | `path_ran_sphere_uniform_float` × 2 | 4/步 | delta-sphere 步进 |
| 9+ | `convective_Xd.h` | `path_rng_canonical_float` × 3..6 | 3-6 | null-collision |
| 10+ | `boundary_ext_flux.h` | `path_ran_hemisphere_cos` + emissivity | 3+/源 | 外部净通量 |

---

*下一步：阅读 [gpu_implementation.md](gpu_implementation.md) 了解 GPU 平台（wavefront）的对应实现*
