# GPU 平台实现方案（Persistent Wavefront）

**工作目录**: `stardis-cus3d/`  
**目标**: 在 Persistent Wavefront 求解器中集成 per-path CBRNG + RNG Trace + State Trace。

---

## 1. 现有 GPU Wavefront RNG 架构

### 1.1 当前实现

```
sdis_solve_persistent_wavefront()
  └── pool_create()
        └── pool->slot_rngs = calloc(pool_size, sizeof(struct ssp_rng*))
  └── build_task_queue()              ← Morton Z-curve 遍历生成 pixel_task
  └── refill_pool()
        └── FOR_EACH empty_slot
              └── init_single_path(p, slot_rngs[slot], task, ...)
                    ├── p->rng = rng                   ← 共享 per-slot RNG
                    ├── sample_time(p->rng, ...)        ← RNG call
                    ├── ssp_rng_canonical(p->rng) × 2   ← pixel sub-sample
                    └── p->phase = PATH_INIT
  └── WHILE active_count > 0
        ├── stream_compact()            ← 收集 active + need_ray 路径
        ├── collect_ray_requests()      ← 从 need_ray 路径提取射线
        ├── GPU batch trace             ← cuBQL / s3d
        ├── distribute_results()        ← 射线结果分发回路径
        ├── advance_with_ray()          ← 有射线结果的路径前进一步
        ├── cascade_no_ray()            ← 纯计算路径连续前进直到需要射线/完成
        ├── collect_enc_locate()        ← M10 enclosure 查询
        ├── GPU batch enc_locate
        ├── distribute_enc_results()
        └── refill_pool()              ← 补充已完成路径的 slot
```

### 1.2 RNG 问题

`init_single_path` ([sdis_solve_persistent_wavefront.c#L284](stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c#L284)):

```c
p->rng = rng;  /* per-slot ssp_rng — 但 slot 被重复使用！ */
```

**问题 1**: 同一 slot 的 RNG 被多条**不同路径**依次使用，路径间 RNG 状态连续
（与 CPU 不同，因为 refill 的时机取决于其他路径的完成时间）。

**问题 2**: `sdis_wf_steps.c` 中所有步函数通过 `p->rng` 访问 RNG：

```c
/* ~40 处调用，分布在 4679 行代码中 */
ssp_rng_canonical(p->rng)
ssp_ran_sphere_uniform_float(p->rng, ...)
ssp_ran_hemisphere_cos_float(p->rng, ...)
ssp_ran_hemisphere_cos(p->rng, ...)
ssp_rng_canonical_float(p->rng)
```

### 1.3 状态机中的 RNG 消耗点

| 状态/步函数 | RNG 调用 | 文件 : 行 |
|------------|---------|-----------|
| `step_init` → PATH_RAD_TRACE_PENDING | `ssp_ran_sphere_uniform_float` (2) | `sdis_wf_steps.c:159` |
| `step_radiative_trace` (emissivity dice) | `ssp_rng_canonical` (1) | `sdis_wf_steps.c:378` |
| `step_radiative_trace` (BRDF reflect) | `brdf_sample` → `ssp_ran_hemisphere_cos_float` (2) | `sdis_wf_steps.c:402` |
| `step_bnd_ss_reinject_sample` | `wf_sample_reinjection_dir_3d` → `ssp_ran_circle_uniform_float` × 4 | `sdis_wf_steps.c:1090` |
| `step_bnd_sf_prob_dispatch` | `ssp_rng_canonical` (1) | `sdis_wf_steps.c:1557` |
| `step_bnd_sf_reinject_sample` | `wf_sample_reinjection_dir_3d` | `sdis_wf_steps.c:1755` |
| `step_bnd_sfn_prob_dispatch` | `ssp_rng_canonical` (1) | `sdis_wf_steps.c:2099` |
| `step_bnd_sfn_rad_done` (emissivity) | `ssp_rng_canonical` (1) | `sdis_wf_steps.c:2373` |
| `step_bnd_sfn_rad_done` (BRDF) | `ssp_ran_hemisphere_cos_float` (2) | `sdis_wf_steps.c:2175` |
| `step_cnd_ds_step_process` (emissivity) | `ssp_rng_canonical` (1) | `sdis_wf_steps.c:2964` |
| `setup_delta_sphere_rays` | `ssp_ran_sphere_uniform_float` (2) | `sdis_wf_steps.c:159` |
| `step_bnd_ext_check` | `source_sample` (varies) | `sdis_wf_steps.c:3413` |
| `step_bnd_ext_diffuse_result` | `ssp_ran_hemisphere_cos` (2) | `sdis_wf_steps.c:3481,3552` |
| `step_bnd_ext_diffuse_shadow_result` | `ssp_rng_canonical` (1) | `sdis_wf_steps.c:3663` |
| `step_cnv_sample_loop` | `ssp_rng_canonical_float` × 3-6 | `sdis_wf_steps.c:4168-4218` |
| `step_cnv_startup_result` | `ssp_ran_hemisphere_cos_float` (2) | `sdis_wf_steps.c:4381` |

---

## 2. 改造方案

### 2.1 path_state 变更

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_state.h`

```c
struct path_state {
  ...
  /* --- RNG --- */
  /* 改造前: struct ssp_rng* rng;  (shared per-slot, non-owning pointer) */
  /* 改造后: */
  struct path_rng prng;           /* per-path CBRNG (owned, value type) */
  ...
};
```

**存储影响**: `sizeof(struct path_rng)` = 48 bytes → path_state 从 ~2.2KB 增加到 ~2.25KB（+2.2%）。
32K pool × 48B = 1.5MB 额外内存，可以忽略。

### 2.2 路径初始化改造

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`

```c
static res_T
init_single_path(
  struct path_state* p,
  /* struct ssp_rng* rng,   ← 移除此参数 */
  const struct pixel_task* task,
  struct sdis_scene* scn,
  const unsigned enc_id,
  const struct sdis_camera* cam,
  const double time_range[2],
  const double pix_sz[2],
  const size_t picard_order,
  const enum sdis_diffusion_algorithm diff_algo,
  uint32_t path_id,
  uint32_t image_width,       /* ← 新增 */
  uint64_t global_seed)       /* ← 新增 */
{
  ...
  /* Per-path CBRNG */
  path_rng_init(&p->prng,
    (uint32_t)task->ipix_image[0],
    (uint32_t)task->ipix_image[1],
    image_width,
    task->spp_idx,
    global_seed);

  /* Sample time */
  time = sample_time_prng(&p->prng, time_range);

  /* Pixel sub-sample */
  samp[0] = ((double)task->ipix_image[0]
            + path_rng_canonical(&p->prng)) * pix_sz[0];
  samp[1] = ((double)task->ipix_image[1]
            + path_rng_canonical(&p->prng)) * pix_sz[1];
  ...
}
```

### 2.3 步函数批量改造

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c`

**统一替换模式**（~40 处）：

```c
/* 改造前 */
ssp_rng_canonical(p->rng)
ssp_ran_sphere_uniform_float(p->rng, dir, NULL)
ssp_ran_hemisphere_cos_float(p->rng, N, dir, NULL)
ssp_ran_hemisphere_cos(p->rng, N, dir, NULL)
ssp_rng_canonical_float(p->rng)

/* 改造后 */
path_rng_canonical(&p->prng)
path_ran_sphere_uniform_float(&p->prng, dir, NULL)
path_ran_hemisphere_cos_float(&p->prng, N, dir, NULL)
path_ran_hemisphere_cos(&p->prng, N, dir, NULL)
path_rng_canonical_float(&p->prng)
```

**辅助函数所在文件也需要改造**（这些 `.h` 文件被 `sdis_wf_steps.c` include）：

| 文件 | 调用点数 |
|------|:--------:|
| `sdis_heat_path_radiative_Xd.h` | ~4 |
| `sdis_heat_path_boundary_Xd_solid_solid.h` | ~2 |
| `sdis_heat_path_boundary_Xd_handle_external_net_flux.h` | ~4 |
| `sdis_heat_path_conductive_delta_sphere_Xd.h` | ~2 |
| `sdis_heat_path_conductive_wos_Xd.h` | ~6 |
| `sdis_heat_path_convective_Xd.h` | ~8 |
| `sdis_solve_boundary_Xd.h` | ~4 |

### 2.4 slot_rngs 相关代码清理

per-path CBRNG 后，`wavefront_pool::slot_rngs` 数组不再需要：

```c
/* pool_create() 中移除 */
// pool->slot_rngs = (struct ssp_rng**)calloc(pool_size, sizeof(struct ssp_rng*));

/* refill_pool() 中移除 slot_rngs[slot] 的传递 */
// init_single_path(p, pool->slot_rngs[slot], task, ...);
// 改为：
init_single_path(p, task, scn, enc_id, cam, time_range, pix_sz,
                 picard_order, diff_algo, path_id,
                 image_width, global_seed);
```

**注意**：`wavefront_pool` 结构体中的 `slot_rngs` 字段和 `sdis_solve_wavefront.c`
（非 persistent 版本）中的 `p->rng = base_rng` 赋值也需要同步修改。

---

## 3. State Transition Trace（GPU 专有）

### 3.1 设计

GPU wavefront 的价值在于可以追踪状态机转换，这是 CPU 深度优先不具备的结构化信息。

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_path_trace.h`

```c
#ifndef SDIS_PATH_TRACE_H
#define SDIS_PATH_TRACE_H

#include "sdis_wf_types.h"
#include <stdio.h>

#if SDIS_RNG_TRACE

struct state_trace_config {
  int      enabled;
  uint32_t pixel_x, pixel_y, spp_idx;
  FILE*    output;
};

void state_trace_init_from_env(struct state_trace_config* cfg);
void state_trace_cleanup(struct state_trace_config* cfg);

static inline int
state_trace_matches(const struct state_trace_config* cfg,
                    const struct path_state* p)
{
  return cfg->enabled
      && cfg->pixel_x == p->pixel_x
      && cfg->pixel_y == p->pixel_y
      && cfg->spp_idx == p->realisation_idx;
}

/* Log state transition */
static inline void
state_trace_transition(const struct state_trace_config* cfg,
                       const struct path_state* p,
                       enum path_phase from_phase,
                       enum path_phase to_phase)
{
  if(!state_trace_matches(cfg, p)) return;
  fprintf(cfg->output,
    "[%4zu] %-28s → %-28s  ray=%d bucket=%-12s  "
    "P=(%.6g,%.6g,%.6g) enc=%u rng_calls=%llu\n",
    p->steps_taken,
    path_phase_name(from_phase),
    path_phase_name(to_phase),
    p->needs_ray,
    p->needs_ray ? ray_bucket_name(p->ray_bucket) : "-",
    p->rwalk.vtx.P[0], p->rwalk.vtx.P[1], p->rwalk.vtx.P[2],
    p->rwalk.enc_id,
    (unsigned long long)p->prng.total_calls);
}

/* Log path completion */
static inline void
state_trace_done(const struct state_trace_config* cfg,
                 const struct path_state* p)
{
  if(!state_trace_matches(cfg, p)) return;
  fprintf(cfg->output,
    "=== PATH DONE pixel=(%u,%u) spp=%u path_id=%u "
    "T=%.10g done_reason=%d steps=%zu rng_calls=%llu ===\n",
    p->pixel_x, p->pixel_y, p->realisation_idx, p->path_id,
    p->T.value, p->done_reason, p->steps_taken,
    (unsigned long long)p->prng.total_calls);
}

#else /* !SDIS_RNG_TRACE */
#define state_trace_transition(cfg, p, from, to) ((void)0)
#define state_trace_done(cfg, p) ((void)0)
#endif

#endif /* SDIS_PATH_TRACE_H */
```

### 3.2 集成位置

在 wavefront 主循环的关键位置插入 trace 调用：

```c
/* sdis_solve_persistent_wavefront.c — advance 循环中 */

/* 在 advance_one_step_no_ray() 调用前后 */
{
  enum path_phase old_phase = p->phase;
  advance_one_step_no_ray(p, scn, ...);
  state_trace_transition(&g_state_trace_cfg, p, old_phase, p->phase);
}

/* 在 advance_one_step_with_ray() 调用前后 */
{
  enum path_phase old_phase = p->phase;
  advance_one_step_with_ray(p, scn, ...);
  state_trace_transition(&g_state_trace_cfg, p, old_phase, p->phase);
}

/* 在路径完成时 */
if(p->phase == PATH_DONE || p->phase == PATH_ERROR) {
  state_trace_done(&g_state_trace_cfg, p);
}
```

---

## 4. Snapshot Checkpoint（GPU 专有）

### 4.1 设计

在指定状态转换时输出完整 `path_state` 快照。

```c
/* sdis_path_trace.h 扩展 */
struct snapshot_config {
  int      enabled;
  uint32_t pixel_x, pixel_y, spp_idx;
  enum path_phase phases[16];     /* 最多 16 个触发阶段 */
  int      phase_count;
  FILE*    output;
};

void snapshot_init_from_env(struct snapshot_config* cfg);

/* 在状态转换后调用 */
static inline void
snapshot_check_and_dump(const struct snapshot_config* cfg,
                        const struct path_state* p)
{
  int i;
  if(!cfg->enabled) return;
  if(cfg->pixel_x != p->pixel_x || cfg->pixel_y != p->pixel_y
  || cfg->spp_idx != p->realisation_idx) return;

  for(i = 0; i < cfg->phase_count; i++) {
    if(cfg->phases[i] == p->phase) {
      path_state_dump(p, cfg->output);
      return;
    }
  }
}
```

### 4.2 path_state_dump 输出格式

```
--- SNAPSHOT path_id=487 pixel=(5,3) spp=7 phase=BND_DISPATCH step=42 ---
  rwalk.P       = (2.31456789012345e-02, 1.05678901234567e-02, 3.45678901234567e-02)
  rwalk.time    = 0.00000000000000000e+00
  rwalk.enc_id  = 5
  rwalk.hit_side= FRONT
  rad_direction = (0.123456789, -0.456789012, 0.789012345)
  rad_bounce    = 3
  rad_retry     = 0
  T.value       = 0.00000000000000000e+00
  T.done        = 0
  rng.counter   = 12
  rng.buf_idx   = 2
  rng.total     = 47
  ds_initialized= 0
  ds_enc_id     = NULL
  bnd_solid_enc = 7
  coupled_nbranch= 0
--- END SNAPSHOT ---
```

---

## 5. 改造影响清单

### 5.1 需要修改的文件

| 文件 | 改动类型 | 改动量 |
|------|---------|:------:|
| `star-sp/0.15/src/ssp_path_rng.h` | **新增** | ~80 行 |
| `star-sp/0.15/src/ssp_path_rng_trace.h` | **新增** | ~100 行 |
| `star-sp/0.15/src/ssp_path_rng_trace.c` | **新增** | ~50 行 |
| `star-sp/0.15/src/ssp_path_ran.h` | **新增** | ~120 行 |
| `stardis-solver/0.16.2/src/sdis_wf_state.h` | 修改 | ~5 行 |
| `stardis-solver/0.16.2/src/sdis_wf_steps.c` | 修改 | ~80 行（40处替换） |
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h` | 修改 | ~10 行 |
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | 修改 | ~50 行 |
| `stardis-solver/0.16.2/src/sdis_solve_wavefront.h` | 修改 | ~5 行 |
| `stardis-solver/0.16.2/src/sdis_solve_wavefront.c` | 修改 | ~30 行 |
| `stardis-solver/0.16.2/src/sdis_path_trace.h` | **新增** | ~150 行 |
| `stardis-solver/0.16.2/src/sdis_path_trace.c` | **新增** | ~80 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_radiative_Xd.h` | 修改 | ~10 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_boundary_Xd_solid_solid.h` | 修改 | ~8 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_boundary_Xd_handle_external_net_flux.h` | 修改 | ~8 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_conductive_delta_sphere_Xd.h` | 修改 | ~6 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_conductive_wos_Xd.h` | 修改 | ~10 行 |
| `stardis-solver/0.16.2/src/sdis_heat_path_convective_Xd.h` | 修改 | ~8 行 |
| `stardis-solver/0.16.2/src/sdis_solve_boundary_Xd.h` | 修改 | ~8 行 |
| `stardis-solver/0.16.2/src/test_sdis_rng_trace.c` | **新增** | ~250 行 |
| `CMakeLists.txt` | 修改 | ~10 行 |

### 5.2 可回退的设计

- `struct path_rng` 是值类型，嵌入 `path_state` — 如果需要回退，只需恢复 `p->rng` 指针
- 所有改动在 `SDIS_RNG_TRACE` 开关保护下 — trace 部分可完全编译排除
- `ssp_rng` 和 `ssp_rng_proxy` 代码完全保留 — 用于非 CBRNG 的测试（如 KISS RNG 回归测试）

---

## 6. 与 non-persistent wavefront 的同步

`sdis_solve_wavefront.c`（Phase B-4 简单 wavefront）也需要同步改造：

```c
/* sdis_solve_wavefront.c — init_all_paths 中 */
/* 改造前: p->rng = base_rng; */
/* 改造后: */
path_rng_init(&p->prng,
  (uint32_t)ipix_image[0], (uint32_t)ipix_image[1],
  (uint32_t)image_def[0], (uint32_t)irealisation,
  global_seed);
```

这保证 B-4 wavefront 和 persistent wavefront 使用完全相同的 per-path RNG key，
可以互相对比验证。

---

## 7. 构建命令

```powershell
# 在 stardis-cus3d 目录下
cd stardis-cus3d
mkdir build-rngtrace; cd build-rngtrace
cmake -G "Visual Studio 17 2022" -A x64 -DSDIS_RNG_TRACE=ON ..
cmake --build . --config Release

# 运行带 trace 的测试
$env:STARDIS_TRACE_PIXEL="5,3"
$env:STARDIS_TRACE_SPP="7"
$env:STARDIS_TRACE_OUTPUT="gpu_rng_trace.log"
ctest -C Release -R "b4_e2e" --output-on-failure
```

---

*下一步：阅读 [cross_platform_validation.md](cross_platform_validation.md) 了解 CPU/GPU 对比验证方案*
