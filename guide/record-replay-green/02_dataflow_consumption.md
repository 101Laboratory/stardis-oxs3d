# 02 — 数据流与 CPU 参考实现消费清单

## 1. `green_path` 数据流总览

```
Solver entry
  sdis_solve_probe_green_function()
    └─▶ solve_probe_3d(scn, args, out_green, NULL)
          └─▶ probe_realisation_Xd()
                │ ctx.green_path = args->green_path   ← 唯一注入点
                └─▶ sample_coupled_path()
                      ├─▶ boundary_path()              ← BND 入口
                      │     ├─▶ solid_fluid_picard1()   ← BND-SF
                      │     │     └─▶ handle_net_flux() ← flux BC
                      │     ├─▶ solid_fluid_picardN()   ← BND-SFN
                      │     ├─▶ solid_solid_boundary()  ← BND-SS
                      │     └─▶ handle_external_net_flux() ← BND-EXT
                      ├─▶ conductive_path()             ← CND
                      │     ├─▶ delta_sphere loop       ← CND-DS
                      │     └─▶ wos loop                ← CND-WoS
                      ├─▶ convective_path()             ← CNV
                      └─▶ radiative_path()              ← RAD
```

`rwalk_context.green_path` (定义于 `sdis_heat_path.h:36`) 作为**第一个字段**在整个 MC 路径行走过程中透传。各模块通过 `ctx->green_path` 或 `args->green_path` 访问并调用录制函数。

## 2. Green 录制 API 汇总

📍 `sdis_green.c` / `sdis_green.h`

| 函数 | 作用 | 被调用位置 |
|------|------|-----------|
| `green_path_set_limit_vertex()` | 记录路径终止于已知温度顶点 | CND-DS、CND-WoS、CNV |
| `green_path_set_limit_interface_fragment()` | 记录路径终止于已知温度界面 | BND entry、CNV |
| `green_path_set_limit_radiative_ray()` | 记录路径终止于辐射环境 | RAD |
| `green_path_add_power_term()` | 记录体积功率项 (K/W) | CND-DS、CND-WoS、BND-SF via `handle_net_flux` |
| `green_path_add_flux_term()` | 记录界面通量项 | BND-SF via `handle_net_flux` |
| `green_path_add_external_flux_terms()` | 记录外部环境通量项 | BND-EXT |
| `green_path_reset_limit()` | 清除端点（null-collision 循环重入时） | BND-SF、BND-SFN |

## 3. CPU 参考实现逐模块消费清单

### 3.1 BND 入口 — `boundary_Xd.h`

```c
// line 62-65: Dirichlet BC 路径终止
if(ctx->green_path) {
    res = green_path_set_limit_interface_fragment
      (ctx->green_path, interf, &frag, rwalk->elapsed_time);
}
```
**1 次调用**：路径在已知温度界面处终止 → `END_AT_INTERFACE_FRAGMENT`

### 3.2 BND-SF Picard-1 — `boundary_Xd_solid_fluid_picard1.h`

**4 个 `green_path` 引用**：
- `net_flux_args.green_path = ctx->green_path` — 传递给 `handle_net_flux`
- `green_path_reset_limit()` — null-collision 循环重入时清除旧端点

**间接调用**（通过 `handle_net_flux`）：
- `green_path_add_flux_term()` — 记录界面通量贡献
- `green_path_add_power_term()` — 记录体积功率贡献

### 3.3 BND-SFN PicardN — `boundary_Xd_solid_fluid_picardN.h`

**0 个直接 `green_path` 引用**。PicardN 递归子路径内部通过共享的 `sample_coupled_path` 透传 `ctx->green_path`。

### 3.4 BND-SS Solid-Solid — `boundary_Xd_solid_solid.h`

**0 个 `green_path` 引用**。SS 模块仅做概率分流（选择注入侧），不直接录制。Green 录制发生在后续进入的 CND 模块。

### 3.5 BND-EXT External — `boundary_Xd_handle_external_net_flux.h`

**2 个引用**：
```c
if(args->green_path) {
    res = green_path_add_external_flux_terms(args->green_path, &green);
}
```
记录外部环境（太阳直射 + 天空漫射）的通量贡献。

### 3.6 BND 通用 — `boundary_Xd_c.h`（`handle_net_flux`）

**3 个 `green_path` 引用**（都是结构体字段声明）。  
实际消费在 `handle_net_flux` 函数体内：
```c
// line 940-942
if(args->green_path) {
    res = green_path_add_flux_term
      (args->green_path, args->interf, args->frag, flux_term);
}
```

```c
// line 1052-1053
if(ctx->green_path) {
    res = green_path_set_limit_vertex(ctx->green_path, ...);
}
```

### 3.7 CND δ-sphere — `conductive_delta_sphere_Xd.h`

**8 个 `green_path` 引用**：

核心消费模式：
```c
// 路径在已知温度处终止
if(ctx->green_path) {
    res = green_path_set_limit_vertex
      (ctx->green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);  // line 400
}

// 体积功率累积
if(ctx->green_path && props.power != SDIS_VOLUMIC_POWER_NONE) {
    green_power_term += power_term;  // 逐步累积
}

// 到达边界时提交累积功率
if(ctx->green_path && props_ref.power != SDIS_VOLUMIC_POWER_NONE) {
    green_path_add_power_term(ctx->green_path, mdm, &rwalk->vtx, green_power_term);
}

// check_solid_constant_properties() 中使用 green_path != NULL 控制属性一致性检查严格程度
```

### 3.8 CND WoS — `conductive_wos_Xd.h`

**9 个 `green_path` 引用**。与 δ-sphere 类似的模式：属性检查、功率累积、端点录制。

### 3.9 CNV — `convective_Xd.h`

**3 个 `green_path` 引用**：
```c
if(ctx->green_path) {
    res = green_path_set_limit_vertex(ctx->green_path, ...);
}

if(ctx->green_path) {
    res = green_path_set_limit_interface_fragment(ctx->green_path, ...);
}
```
路径在已知温度的流体侧终止。

### 3.10 RAD — `radiative_Xd.h`

**1 个 `green_path` 引用**：
```c
// line 81-83: 路径到达辐射环境
if(ctx->green_path) {
    res = green_path_set_limit_radiative_ray
      (ctx->green_path, &ray, rwalk->elapsed_time);
}
```
记录路径终止类型为 `SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV`。这是唯一使用 `set_limit_radiative_ray` 的位置。

## 4. 消费统计摘要

| 模块 | 文件 | 引用数 | 调用的录制 API |
|------|------|:------:|---------------|
| BND entry | `boundary_Xd.h` | 1 | `set_limit_interface_fragment` |
| BND-SF | `boundary_Xd_solid_fluid_picard1.h` | 4 | `reset_limit` + 间接 `add_flux_term`/`add_power_term` |
| BND-SFN | `boundary_Xd_solid_fluid_picardN.h` | 0 | （透传至子路径） |
| BND-SS | `boundary_Xd_solid_solid.h` | 0 | （无直接录制） |
| BND-EXT | `boundary_Xd_handle_external_net_flux.h` | 2 | `add_external_flux_terms` |
| BND-c | `boundary_Xd_c.h` | 3 | `add_flux_term`, `set_limit_vertex` |
| CND-DS | `conductive_delta_sphere_Xd.h` | 8 | `set_limit_vertex`, `add_power_term` |
| CND-WoS | `conductive_wos_Xd.h` | 9 | `set_limit_vertex`, `add_power_term` |
| CNV | `convective_Xd.h` | 3 | `set_limit_vertex`, `set_limit_interface_fragment` |
| RAD | `radiative_Xd.h` | 1 | `set_limit_radiative_ray` |
| **合计** | | **31** | |
