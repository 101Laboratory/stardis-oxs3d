# CPU 侧插桩指南

**目标文件**: `stardis-cpu/stardis-solver/0.16.2/src/` 下的源文件  
**头文件**: 将 `sdis_path_depth_stats.h` 复制到该 src 目录下  
**控制开关**: 环境变量 `STARDIS_PATH_DEPTH_CSV=<path>`  

本文档列出所有需要修改的位置和具体代码。每处修改都很小（1-3 行）。

---

## 0. 前置：复制头文件

```powershell
Copy-Item optimization\path_depth_comparison\sdis_path_depth_stats.h `
  stardis-cpu\stardis-solver\0.16.2\src\sdis_path_depth_stats.h
```

---

## 1. `sdis_heat_path.h` — rwalk_context 新增字段

**文件**: `sdis_heat_path.h` (约 L36-78 的 `struct rwalk_context`)

在 `struct rwalk_context` 末尾、最后一个字段之后，`};` 之前，新增：

```c
  /* Path depth instrumentation (NULL when not profiling) */
  struct path_depth_stats* depth_stats;
```

同时在文件顶部新增 forward declaration（不需要 #include，因为只用指针）：

```c
struct path_depth_stats; /* Forward decl for depth profiling */
```

在 `RWALK_CONTEXT_NULL` 宏中追加 `,NULL`（对应 depth_stats 初始值）。

---

## 2. `sdis_realisation_Xd.h` — sample_coupled_path 计数

**文件**: `sdis_realisation_Xd.h` 的 `XD(sample_coupled_path)` 函数

在 `while(!T->done)` 循环体**开头**（`const struct rwalk rwalk_bkp = *rwalk;`
之前），插入：

```c
    /* Path depth profiling: count top-level function calls */
    if(ctx->depth_stats) ctx->depth_stats->total_func_calls++;
```

---

## 3. `sdis_heat_path_conductive_delta_sphere_Xd.h` — delta-sphere 计数

**文件**: `sdis_heat_path_conductive_delta_sphere_Xd.h`

### 3a. 函数入口（函数开头，`res_T res = RES_OK;` 之后）

```c
  /* Path depth profiling */
  if(ctx->depth_stats) ctx->depth_stats->ds_entries++;
```

### 3b. do/while 循环体开头（`do {` 之后的第一行）

```c
    /* Path depth profiling: count each delta-sphere iteration */
    if(ctx->depth_stats) { ctx->depth_stats->ds_steps++; ctx->depth_stats->rays_ds += 2; }
```

注意：delta-sphere 每步发射 2 条射线（dir0 和 dir1），所以 `rays_ds += 2`。

---

## 4. `sdis_heat_path_conductive_wos_Xd.h` — WoS 计数

**文件**: `sdis_heat_path_conductive_wos_Xd.h`

### 4a. `conductive_path_wos` 函数入口

```c
  if(ctx->depth_stats) ctx->depth_stats->wos_entries++;
```

### 4b. `for(;;)` 循环体开头（`double power_term = 0;` 之前）

```c
    if(ctx->depth_stats) ctx->depth_stats->wos_steps++;
```

---

## 5. `sdis_heat_path_radiative_Xd.h` — 辐射反弹计数

**文件**: `sdis_heat_path_radiative_Xd.h`

### 5a. `trace_radiative_path` 函数入口

```c
  if(ctx->depth_stats) ctx->depth_stats->rad_entries++;
```

### 5b. `for(;;)` 循环体开头（`struct brdf brdf = BRDF_NULL;` 之前）

```c
    if(ctx->depth_stats) { ctx->depth_stats->rad_bounces++; ctx->depth_stats->rays_rad++; }
```

---

## 6. `sdis_heat_path_convective_Xd.h` — 对流计数

**文件**: `sdis_heat_path_convective_Xd.h`

### 6a. `convective_path` 函数入口（`ASSERT` 之后）

```c
  if(ctx->depth_stats) ctx->depth_stats->cnv_entries++;
```

### 6b. `for(;;)` 循环体开头（`struct sdis_interface_fragment frag;` 之前）

```c
    if(ctx->depth_stats) ctx->depth_stats->cnv_steps++;
```

---

## 7. `sdis_heat_path_boundary_Xd.h` — 边界入口计数

**文件**: `sdis_heat_path_boundary_Xd.h`

### 7a. `boundary_path` 函数入口（第一个 `ASSERT` 之后）

```c
  if(ctx->depth_stats) ctx->depth_stats->bnd_entries++;
```

---

## 8. `sdis_realisation.c` — done_reason 分类 + 传递 depth_stats

**文件**: `sdis_realisation.c` 的 `ray_realisation_3d`

### 8a. 在文件顶部添加 include

```c
#include "sdis_path_depth_stats.h"
```

### 8b. 在 `ray_realisation_3d` 函数签名中新增参数

不修改签名。改为通过 `rwalk_context.depth_stats` 传递。
在 `ctx` 初始化后设置:

```c
  ctx.depth_stats = args->depth_stats; /* may be NULL */
```

这要求在 `struct ray_realisation_args` 中新增 `depth_stats` 字段。

### 8c. 在 `ray_realisation_3d` 返回前设置 done_reason

```c
  /* Classify done_reason for depth stats */
  if(args->depth_stats) {
    if(res != RES_OK)
      args->depth_stats->done_reason = -1;
    else if(T.done)
      args->depth_stats->done_reason = 2; /* default: temp_known */
    /* Note: finer classification requires checking T.func at termination,
     * but the function pointer is cleared after done. 2=temp_known is a
     * safe default since all paths ultimately find a temperature. */
  }
```

---

## 9. `sdis_realisation.h` — ray_realisation_args 新增字段

**文件**: `sdis_realisation.h`

在 `struct ray_realisation_args` 末尾新增:

```c
  struct path_depth_stats* depth_stats; /* NULL when not profiling */
```

在对应的 NULL 宏中追加 `,NULL`。

---

## 10. `sdis_solve_camera.c` — per-path CSV 写入 + 汇总

**文件**: `sdis_solve_camera.c`

### 10a. 顶部新增 include

```c
#include "sdis_path_depth_stats.h"
```

### 10b. 添加全局汇总变量（文件顶部的 static 区域）

```c
static struct path_depth_summary s_depth_summary = PATH_DEPTH_SUMMARY_NULL;
```

### 10c. per-pixel 循环中（`ray_realisation_3d` 调用前）初始化

在以下位置（`realis_args.*` 赋值区域）:

```c
    struct path_depth_stats depth_stats = PATH_DEPTH_STATS_ZERO;
    /* ... */
    realis_args.depth_stats = path_depth_csv_file() ? &depth_stats : NULL;
```

### 10d. `ray_realisation_3d` 调用后，写 CSV 行 + 累积汇总

```c
    /* Path depth profiling output */
    if(realis_args.depth_stats) {
      depth_stats.done_reason = (res_simul == RES_OK) ? depth_stats.done_reason : -1;
      #pragma omp critical(path_depth_csv)
      {
        path_depth_stats_write_row(&depth_stats,
          ipix[0], ipix[1], (size_t)irealisation, w, (int)(res_simul == RES_OK));
        path_depth_summary_add(&s_depth_summary, &depth_stats);
      }
    }
```

### 10e. 函数末尾（return 前）打印汇总 + 关闭文件

```c
  path_depth_summary_print(&s_depth_summary);
  path_depth_stats_close();
```

---

## 11. 编译注意事项

- `sdis_path_depth_stats.h` 是纯 C89 兼容的 header-only 文件
- 所有函数都是 `static`（inline），不影响链接
- 当 `STARDIS_PATH_DEPTH_CSV` 未设置时，`ctx->depth_stats` 为 NULL，
  所有 `if(ctx->depth_stats)` 分支不执行，零开销
- 需确保在 C89 模式下 `size_t` 的 `%lu` 格式匹配
  （使用 `(unsigned long)` 强转已处理）

---

## 12. 验证步骤

1. 先在最小场景验证：`spp=1 img=4x4`，确认 CSV 输出正确
2. 检查 CSV 行数 = 4×4×1 = 16
3. 检查每行 `func_calls > 0`（路径至少调用了一次 T->func）
4. 检查 `ds_steps >= ds_entries`（每次进入 delta-sphere 至少迭代一次）
5. 检查 `total_physical_iters = ds_steps + wos_steps + rad_bounces + cnv_steps`
