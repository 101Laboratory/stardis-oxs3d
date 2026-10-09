# 03 — oxs3d 波前实现 Green 函数复刻对比

## 1. 波前步骤文件与 CPU 模块映射

oxs3d 将 CPU 的 `sdis_heat_path_*.h` 模板代码重构为显式状态机，拆分到 `sdis_wf_steps_*.c` 文件中。

| CPU 模块 | CPU 文件 | oxs3d WF 文件 |
|----------|---------|--------------|
| BND entry | `boundary_Xd.h` | 分散至 `wf_steps_cnv.c` 等（Dirichlet 检查内联） |
| BND-SF | `boundary_Xd_solid_fluid_picard1.h` | `sdis_wf_steps_bnd_sf.c` |
| BND-SFN | `boundary_Xd_solid_fluid_picardN.h` | `sdis_wf_steps_bnd_sfn.c` |
| BND-SS | `boundary_Xd_solid_solid.h` | `sdis_wf_steps_bnd_ss.c` |
| BND-EXT | `boundary_Xd_handle_external_net_flux.h` | `sdis_wf_steps_bnd_ext.c` |
| BND 通用 | `boundary_Xd_c.h` | `handle_net_flux_3d()` 共享（通过 include） |
| CND (δ-sphere + WoS) | `conductive_delta_sphere_Xd.h`, `conductive_wos_Xd.h` | `sdis_wf_steps_cnd.c` |
| CNV | `convective_Xd.h` | `sdis_wf_steps_cnv.c` |
| RAD | `radiative_Xd.h` | **无独立文件** — 辐射光线追踪内联于 BND-SF |
| ENC | （无 green_path） | `sdis_wf_steps_enc.c`（无 green_path） |
| Core dispatch | `heat_path.c` | `sdis_wf_steps_core.c` |

## 2. 逐模块 `green_path` 对比

### 2.1 BND-SF (`sdis_wf_steps_bnd_sf.c`) — 3 引用

| 行号 | 调用 | 对应 CPU |
|------|------|---------|
| 513 | `net_flux_args.green_path = p->ctx.green_path` | picard1.h 传递给 `handle_net_flux` ✅ |
| 940-941 | `green_path_reset_limit(p->ctx.green_path)` | picard1.h null-collision 循环重入 ✅ |

**间接消费**：`handle_net_flux_3d()` 被直接调用（line 518），内部的 `green_path_add_flux_term` 正常工作 ✅。

### 2.2 BND-SFN (`sdis_wf_steps_bnd_sfn.c`) — 2 引用

| 行号 | 调用 | 对应 CPU |
|------|------|---------|
| 99 | `green_path_reset_limit(p->ctx.green_path)` | PicardN 重入清除 ✅ |

CPU 中 PicardN 也是 0 个直接引用，一致 ✅。

### 2.3 BND-SS (`sdis_wf_steps_bnd_ss.c`) — 0 引用

CPU 中同样 0 引用，一致 ✅。SS 模块仅做概率分流。

### 2.4 BND-EXT (`sdis_wf_steps_bnd_ext.c`) — 4 引用

| 行号 | 调用 | 对应 CPU |
|------|------|---------|
| 655 | `green_path_add_external_flux_terms(ext->green_path, &green)` | `handle_external_net_flux.h` ✅ |

其余引用为条件检查和字段传递。CPU 有 2 个引用，oxs3d 有 4 个引用（多了条件分支），逻辑一致 ✅。

### 2.5 CND (`sdis_wf_steps_cnd.c`) — 20 引用

此文件同时包含 δ-sphere 和 WoS 两种传导路径的状态机。

**Wave-specific helper** `wf_update_green_path()` (line 302-331):
```c
static res_T wf_update_green_path(
    struct green_path_handle* green_path, struct rwalk* rwalk,
    struct sdis_medium* mdm, const struct solid_props* props,
    const double power_term, const struct temperature* T)
{
    if(!green_path) goto exit;
    if(props->power != SDIS_VOLUMIC_POWER_NONE) {
        res = green_path_add_power_term(green_path, mdm, &rwalk->vtx, power_term);
    }
    if(T->done) {
        res = green_path_set_limit_vertex(green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);
    }
    // ...
}
```

这个辅助函数合并了 CPU 中分散的 `add_power_term` + `set_limit_vertex` 逻辑。

| 子模块 | 调用 | 对应 CPU | 状态 |
|--------|------|---------|------|
| DS `check_solid_constant_properties` | `p->ctx.green_path != NULL` | DS `ctx->green_path != NULL` | ✅ |
| DS `step_advance` | `green_path_add_power_term()` (line 280, 314) | DS 体积功率累积 | ✅ |
| DS `step_advance` 到达边界 | `green_path_add_power_term()` 提交 | DS 边界提交 | ✅ |
| WoS `check_temp` | `wf_update_green_path()` (line 582) | WoS `set_limit_vertex` + `add_power_term` | ✅ |
| WoS step advance | `wf_update_green_path()` (line 913, 932) | WoS 累积 + 终止 | ✅ |

**⚠️ 缺口 — DS 已知温度终止** (line 119-127):

CPU `conductive_delta_sphere_Xd.h:395-403`:
```c
if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
    T->value += props.temperature;
    T->done = 1;
    if(ctx->green_path) {                                    // ← CPU 有
        res = green_path_set_limit_vertex(ctx->green_path, mdm, &rwalk->vtx, ...);
    }
}
```

oxs3d `wf_steps_cnd.c:119-127` (`PATH_CND_DS_CHECK_TEMP`):
```c
if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
    p->T.value += props.temperature;
    p->T.done = 1;
    if(p->ctx.heat_path) {                                   // heat_path 有
        heat_path_get_last_vertex(...)->weight = p->T.value;
    }
    // ❌ 缺少对应的 green_path_set_limit_vertex 调用
    // ❌ 累积的 green_power_term 也未提交
    hot->phase = PATH_DONE;
}
```

### 2.6 CNV (`sdis_wf_steps_cnv.c`) — 9 引用

| 行号 | 调用 | 对应 CPU |
|------|------|---------|
| 88 | `green_path_set_limit_vertex()` | `convective_Xd.h` 已知温度终止 ✅ |
| 127 | `green_path_set_limit_vertex()` | CNV 另一终止分支 ✅ |
| 386 | `green_path_set_limit_interface_fragment()` | Dirichlet BC 终止 ✅ |

CNV 完全覆盖 ✅

### 2.7 RAD — **无独立 WF 步骤文件**

CPU 中辐射路径终止在 `radiative_Xd.h:81-83`：
```c
if(ctx->green_path) {
    res = green_path_set_limit_radiative_ray
      (ctx->green_path, &ray, rwalk->elapsed_time);
}
```

oxs3d 中，辐射光线追踪的终止处理内联在 `sdis_wf_steps_bnd_sf.c:691-703`。此位置的代码：

```c
/* Set T for the sub-path */
p->T.value += trad;
p->T.done = 1;

/* Clear stale hit — matches CPU set_limit_radiative_temperature which
 * ASSERTs SXD_HIT_NONE and sets hit_side = SDIS_SIDE_NULL__. */
p->rwalk.hit_3d = S3D_HIT_NULL;
p->rwalk.hit_side = SDIS_SIDE_NULL__;

hot->phase = (uint8_t)PATH_BND_SF_NULLCOLL_DECIDE;
```

**⚠️ 缺口**：注释中提到了 CPU 的 `set_limit_radiative_temperature`，但**没有实际调用** `green_path_set_limit_radiative_ray()`。路径到达辐射环境终止时，Green 录制不记录端点类型。

全局搜索确认：`green_path_set_limit_radiative_ray` 在整个 oxs3d 源码中**仅出现 1 次**，即上述注释（非函数调用）。

### 2.8 ENC (`sdis_wf_steps_enc.c`) — 0 引用

CPU 中 ENC 同样无 green_path 引用。一致 ✅。

### 2.9 Core (`sdis_wf_steps_core.c`) — 0 引用

CPU 中 `heat_path.c` 同样无 green_path 引用（调度层不涉及录制）。一致 ✅。

## 3. 覆盖矩阵汇总

| 模块 | CPU green_path refs | oxs3d WF refs | 录制 API 覆盖 | 状态 |
|------|:---:|:---:|---|:---:|
| BND-SF | 4 | 3 | `reset_limit`, `add_flux`/`add_power` via `handle_net_flux` | ✅ |
| BND-SFN | 0 | 2 | `reset_limit` | ✅ |
| BND-SS | 0 | 0 | — | ✅ |
| BND-EXT | 2 | 4 | `add_external_flux_terms` | ✅ |
| CND-DS | 8 | ~10 | `add_power_term`, ~~`set_limit_vertex`~~ | **❌ 部分缺失** |
| CND-WoS | 9 | ~10 | `add_power_term`, `set_limit_vertex` via `wf_update_green_path` | ✅ |
| CNV | 3 | 9 | `set_limit_vertex`, `set_limit_interface_fragment` | ✅ |
| RAD | 1 | 0 | ~~`set_limit_radiative_ray`~~ | **❌ 缺失** |
| ENC | 0 | 0 | — | ✅ |
| Core | 0 | 0 | — | ✅ |

## 4. 已识别缺口详情

### 缺口 1：RAD 端点录制缺失

- **位置**: `sdis_wf_steps_bnd_sf.c:691-703`（辐射光线 null-collision 子路径终止）
- **缺失**: `green_path_set_limit_radiative_ray()` 未调用
- **影响**: Green 函数路径到达辐射环境后，`end_type` 未被设置为 `SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV`，回放时无法正确重建辐射环境端点
- **修复**: 在 `p->T.done = 1` 之后添加：
  ```c
  if(p->ctx.green_path) {
      struct sdis_radiative_ray ray = ...;  // 从当前状态构建
      res = green_path_set_limit_radiative_ray(
          p->ctx.green_path, &ray, p->rwalk.elapsed_time);
      if(res != RES_OK) goto error;
  }
  ```

### 缺口 2：CND δ-sphere 已知温度端点录制缺失

- **位置**: `sdis_wf_steps_cnd.c:119-127`（`PATH_CND_DS_CHECK_TEMP` 步骤）
- **缺失**: `green_path_set_limit_vertex()` 未调用，累积 `green_power_term` 未提交
- **影响**: δ-sphere 行走在起始点（或中途）遇到已知温度时，Green 路径缺少终止标记和功率贡献
- **风险等级**: 中 — δ-sphere 起始点通常无已知温度（来自 boundary 分流），但在材质属性空间变化时可能触发
- **修复**: 在 `p->T.done = 1` 之后添加：
  ```c
  if(p->ctx.green_path) {
      if(p->locals.cnd_ds.props_ref.power != SDIS_VOLUMIC_POWER_NONE) {
          green_path_add_power_term(p->ctx.green_path,
              p->locals.cnd_ds.medium, &p->rwalk.vtx,
              p->locals.cnd_ds.green_power_term);
      }
      green_path_set_limit_vertex(p->ctx.green_path,
          p->locals.cnd_ds.medium, &p->rwalk.vtx,
          p->rwalk.elapsed_time);
  }
  ```

### 未确认事项

- CND δ-sphere `step_advance` 中 `time_rewind` 导致 `T->done` 时是否需要额外 Green 终止判断 — CPU 中此场景同样未录制 Green 端点（`time_rewind` 不涉及 Green）

## 5. 结论

oxs3d 波前实现在大部分模块中正确复刻了 `green_path` 消费：BND（SF/SFN/SS/EXT）、CND-WoS、CNV 全部覆盖且逻辑一致。两个遗漏集中在路径终止时的端点录制：

1. **RAD 辐射环境端点** — 注释表明开发者知晓此对应关系，但未添加实际调用
2. **CND-DS 已知温度端点** — 概率上低频触发，但在体积功率非零场景下可能导致 Green 回放精度损失

修复量估计：各 ~5 行代码。
