# GPU 侧插桩指南

**目标文件**: `stardis-cus3d/stardis-solver/0.16.2/src/` 下的源文件  
**控制开关**: 现有环境变量 `STARDIS_PIXEL_TRACE=<path>` 扩展输出列  

GPU wavefront 版本已有 `steps_taken`（phase transition 计数）和完整统计。
本插桩新增 Level 1 物理迭代计数，使之与 CPU 采集数据可直接对比。

---

## 1. `sdis_wf_state.h` — path_state 新增字段

**文件**: `sdis_wf_state.h` 的 `struct path_state`

在 `size_t steps_taken;` 和 `int done_reason;` 之间（或 Diagnostics 区域末尾），
新增:

```c
  /* Level 1 physical iteration counts (for CPU/GPU comparison) */
  size_t  ds_steps_L1;              /* delta-sphere loop iterations         */
  size_t  wos_steps_L1;             /* WoS loop iterations                  */
  size_t  rad_bounces_L1;           /* radiative bounce iterations          */
  size_t  cnv_steps_L1;             /* convective null-collision iterations */
  size_t  func_calls_L0;            /* top-level sub-path entries (boundary/
                                       conductive/radiative/convective)     */
```

---

## 2. 初始化（两处）

在 `sdis_solve_wavefront.c` 和 `sdis_solve_persistent_wavefront.c` 中，
路径初始化位置（`p->steps_taken = 0; p->done_reason = 0;` 附近），新增:

```c
      p->ds_steps_L1    = 0;
      p->wos_steps_L1   = 0;
      p->rad_bounces_L1 = 0;
      p->cnv_steps_L1   = 0;
      p->func_calls_L0  = 0;
```

搜索所有 `p->steps_taken = 0` 位置（至少 3 处：camera init、probe init、
persistent wavefront init），全部追加上述初始化。

---

## 3. `sdis_wf_steps.c` — 在关键 phase 处插桩

### 3a. Delta-sphere 循环入口

找到处理 `PATH_CND_DS_CHECK_TEMP`（或对应的 delta-sphere 循环入口 phase）的
case 分支，在开头加:

```c
    p->ds_steps_L1++;
```

### 3b. WoS 循环入口

找到处理 `PATH_CND_WOS_SAMPLE`（或 WoS 循环核心 phase）的 case，在开头加:

```c
    p->wos_steps_L1++;
```

### 3c. 辐射反弹

找到 `PATH_RAD_TRACE` 或 `PATH_RAD_BOUNCE`（处理辐射射线发射的 phase），加:

```c
    p->rad_bounces_L1++;
```

注意只在实际发射射线并处理反弹的 phase 计数，不要在 `PATH_RAD_TRACE_WAIT`
（等待 GPU 结果）中计数。

### 3d. 对流循环入口

找到 `PATH_CNV_SAMPLE` 或 `PATH_CNV_LOOP`（对流 null-collision 循环 phase），加:

```c
    p->cnv_steps_L1++;
```

### 3e. 顶层子路径入口

在以下 phase 的处理中各加一次 `p->func_calls_L0++`:

| Phase | 含义 |
|-------|------|
| `PATH_COUPLED_BOUNDARY` | 进入 boundary_path |
| `PATH_COUPLED_CONDUCTIVE` | 进入 conductive_path |
| `PATH_COUPLED_RADIATIVE` | 进入 radiative (from boundary) |
| `PATH_COUPLED_CONVECTIVE` | 进入 convective_path |

---

## 4. CSV 输出扩展

### 4a. `sdis_solve_persistent_wavefront.c` — 扩展 pixel_trace CSV 头

修改 `pixel_trace_file()` 中的 header 行:

```c
/* 原来: */
"path_id,px,py,spp,T_value,T_done,done_reason,steps,phase\n"

/* 改为: */
"path_id,px,py,spp,T_value,T_done,done_reason,steps,phase,"
"func_calls,ds_steps,wos_steps,rad_bounces,cnv_steps\n"
```

### 4b. 扩展 per-path 写入

修改 `pixel_trace_file()` 的 `fprintf` 调用:

```c
/* 原来: */
fprintf(tf, "%u,%u,%u,%u,%.17g,%d,%d,%lu,%d\n", ...);

/* 改为: */
fprintf(tf, "%u,%u,%u,%u,%.17g,%d,%d,%lu,%d,%lu,%lu,%lu,%lu,%lu\n",
  (unsigned)p->path_id,
  (unsigned)p->ipix_image[0], (unsigned)p->ipix_image[1],
  (unsigned)p->realisation_idx,
  p->T.value, (int)p->T.done, p->done_reason,
  (unsigned long)p->steps_taken, (int)p->phase,
  (unsigned long)p->func_calls_L0,
  (unsigned long)p->ds_steps_L1,
  (unsigned long)p->wos_steps_L1,
  (unsigned long)p->rad_bounces_L1,
  (unsigned long)p->cnv_steps_L1);
```

### 4c. Summary 日志扩展（可选）

在 wavefront summary log_info 中追加汇总统计:

```c
/* 在 collect_results 之后、cleanup 之前 */
{
  size_t i;
  size_t sum_ds = 0, sum_wos = 0, sum_rad = 0, sum_cnv = 0;
  size_t max_ds = 0, max_wos = 0, max_rad = 0, max_cnv = 0;
  for(i = 0; i < wf.total_paths; i++) {
    sum_ds  += wf.paths[i].ds_steps_L1;
    sum_wos += wf.paths[i].wos_steps_L1;
    sum_rad += wf.paths[i].rad_bounces_L1;
    sum_cnv += wf.paths[i].cnv_steps_L1;
    if(wf.paths[i].ds_steps_L1  > max_ds)  max_ds  = wf.paths[i].ds_steps_L1;
    if(wf.paths[i].wos_steps_L1 > max_wos) max_wos = wf.paths[i].wos_steps_L1;
    if(wf.paths[i].rad_bounces_L1 > max_rad) max_rad = wf.paths[i].rad_bounces_L1;
    if(wf.paths[i].cnv_steps_L1 > max_cnv) max_cnv = wf.paths[i].cnv_steps_L1;
  }
  log_info(scn->dev,
    "L1 phys iters: ds=%lu(max=%lu) wos=%lu(max=%lu) "
    "rad=%lu(max=%lu) cnv=%lu(max=%lu)\n",
    (unsigned long)sum_ds, (unsigned long)max_ds,
    (unsigned long)sum_wos, (unsigned long)max_wos,
    (unsigned long)sum_rad, (unsigned long)max_rad,
    (unsigned long)sum_cnv, (unsigned long)max_cnv);
}
```

---

## 5. 定位 Phase 名称

GPU phase 枚举定义在 `sdis_wf_types.h` 中。grep 以下关键字定位精确 phase:

```powershell
grep -n "PATH_CND_DS_CHECK_TEMP\|PATH_CND_WOS\|PATH_RAD_TRACE\|PATH_RAD_BOUNCE\|PATH_CNV\|PATH_COUPLED" `
  stardis-cus3d\stardis-solver\0.16.2\src\sdis_wf_types.h
```

在 `sdis_wf_steps.c` 中搜索对应 case 即可定位插桩点:

```powershell
grep -n "case PATH_CND_DS_CHECK_TEMP\|case PATH_CND_WOS\|case PATH_RAD_TRACE\|case PATH_RAD_BOUNCE\|case PATH_CNV\|case PATH_COUPLED" `
  stardis-cus3d\stardis-solver\0.16.2\src\sdis_wf_steps.c
```

---

## 6. 验证

1. 运行 `spp=1 img=4x4`，设 `STARDIS_PIXEL_TRACE=test_trace.csv`
2. 检查 CSV 新增列非零
3. 检查 `ds_steps_L1 * 7 ≈ steps_taken`（大致换算验证）
4. 检查 `func_calls_L0 > 0`

---

## 7. 注意事项

- `path_state` 增大约 40 bytes（5 × size_t = 40B on 64-bit）
- 对于 pool=4096: 增加 160 KB，可忽略
- 对于 pool=32768: 增加 1.3 MB，仍可忽略
- 每次 phase transition 新增一次 `size_t++`（~1 ns），总开销 < 0.1%
