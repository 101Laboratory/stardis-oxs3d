# 01 — CLI 入口与 Solver 库 API

## 1. CLI 层：stardis 可执行文件

### 1.1 模式枚举与 Green 兼容性

📍 `stardis-cpu/stardis/0.12/src/stardis-args.h`

```c
#define GREEN_COMPATIBLE_MODES \
  ( MODE_COMPUTE_GREEN | MODE_SOLVE_GREEN | \
    MODE_COMPUTE_PROBE | MODE_COMPUTE_BOUNDARY | \
    MODE_COMPUTE_MEDIUM )

#define CAN_DUMP_PATHS \
  ( MODE_COMPUTE_GREEN | MODE_SOLVE_GREEN | \
    MODE_COMPUTE_PROBE | MODE_COMPUTE_BOUNDARY | \
    MODE_COMPUTE_MEDIUM | \
    MODE_COMPUTE_IMAGE_IR )
```

**关键发现**：

| 标志 | `GREEN_COMPATIBLE_MODES` | `CAN_DUMP_PATHS` |
|------|:---:|:---:|
| `MODE_COMPUTE_PROBE` | ✅ | ✅ |
| `MODE_COMPUTE_BOUNDARY` | ✅ | ✅ |
| `MODE_COMPUTE_MEDIUM` | ✅ | ✅ |
| `MODE_COMPUTE_GREEN` | ✅ | ✅ |
| `MODE_SOLVE_GREEN` | ✅ | ✅ |
| **`MODE_COMPUTE_IMAGE_IR`** | **❌ 排除** | ✅ |

IR 渲染模式被排除在 Green 兼容模式外。这是**工程约束**而非物理限制（见 §1.4）。

### 1.2 CLI 参数

```
stardis -G bin scene.txt   # 录制 Green 函数（二进制）
stardis -G ascii scene.txt # 录制 Green 函数（文本）
stardis -R green.bin scene.txt  # 回放 Green 函数
```

参数解析在 `stardis-args.c` 中：
- `-G <format>`：设置 `MODE_GREEN_BIN` 或 `MODE_GREEN_ASCII`
- `-R <file>`：设置 `MODE_SOLVE_GREEN`，加载已录制的 Green 函数文件

### 1.3 compute 层入口

📍 `stardis-cpu/stardis/0.12/src/stardis-compute.c`

```c
// Probe 模式 — 支持 Green
static res_T compute_probe(struct stardis* stardis) {
    if(stardis->mode & (MODE_GREEN_BIN | MODE_GREEN_ASCII)) {
        // 录制路径
        res = sdis_solve_probe_green_function(scn, &args, &green);
    } else {
        // 普通求解
        res = sdis_solve_probe(scn, &args, &estim);
    }
}

// Camera 模式 — 拒绝 Green
static res_T compute_camera(struct stardis* stardis) {
    ASSERT(!(stardis->mode & (MODE_GREEN_BIN | MODE_GREEN_ASCII)));
    res = sdis_solve_camera(scn, &args, &estim);
}
```

Camera 路径通过 `ASSERT` 硬拒绝 Green 模式，不存在 `sdis_solve_camera_green_function()` API。

### 1.4 物理可行性

Bati et al. 2023（Zotero NRCB53QN）明确演示了 **IR 渲染 + Green 回放**的物理可行性：

- MC 路径权重 = **温度**（线性量）
- Planck 黑体辐射转换 `B(λ, T)` 是**后处理**步骤
- Green 函数线性叠加对温度有效 → 对 IR 渲染同样有效
- 论文数据：MC 采样 59 分钟 → Green 回放 6 秒

当前 IR 被排除是因为 Camera 光线追踪 API 缺少 `green_path` 字段管道（见 §2）。

---

## 2. Solver 库 API 层

📍 `stardis-solver/0.16.2/src/sdis.h`

### 2.1 现有 Green API

```c
// 录制
res_T sdis_solve_probe_green_function(scn, args, &green);
res_T sdis_solve_probe_boundary_green_function(scn, args, &green);
res_T sdis_solve_boundary_green_function(scn, args, &green);
res_T sdis_solve_medium_green_function(scn, args, &green);

// 回放
res_T sdis_green_function_solve(green, scn, &estim);

// 序列化
res_T sdis_green_function_write(green, stream, format);
res_T sdis_green_function_create_from_stream(stream, &green);
```

**缺失**：无 `sdis_solve_camera_green_function()` — 这是扩展 IR Green 功能的 API 级缺口。

### 2.2 实现模式

📍 `stardis-solver/0.16.2/src/sdis_solve.c`

所有 Green 录制的 solver 入口函数共享同一模式：

```c
res_T sdis_solve_probe_green_function(scn, args, &out_green) {
    // 调用与普通求解 **完全相同** 的核心函数
    // 只是 green 槽非空、estimator 槽为空
    return solve_probe_3d(scn, args, out_green, NULL);
    //                              ^^^^^^^^   ^^^^
    //                              green输出   estim=NULL
}

res_T sdis_solve_probe(scn, args, &out_estim) {
    return solve_probe_3d(scn, args, NULL, out_estim);
    //                              ^^^^  ^^^^^^^^^
    //                              green=NULL  estim输出
}
```

核心 MC 路径行走代码完全共享，Green 与普通求解的唯一区别在于输出槽的选择。

### 2.3 Realisation Args 结构差异

📍 `stardis-solver/0.16.2/src/sdis_realisation.h`

```c
struct probe_realisation_args {
    struct green_path_handle* green_path;  // ✅ 有
    struct heat_path_handle*  heat_path;
    // ...
};

struct ray_realisation_args {
    // ❌ 无 green_path 字段
    struct heat_path_handle*  heat_path;
    // ...
};
```

📍 `sdis_realisation_Xd.h`（Probe realisation 入口）:
```c
// line 246
ctx.green_path = args->green_path;  // ✅ 传入 rwalk_context
```

📍 `sdis_realisation.c`（Camera/Ray realisation 入口）:
```c
// line 85
ctx.heat_path = args->heat_path;
// ❌ 没有 ctx.green_path = ... 这一行
// ctx.green_path 默认为 NULL（来自 RWALK_CONTEXT_NULL）
```

### 2.4 IR Green 扩展的最小修改路径

1. **`ray_realisation_args`** 添加 `struct green_path_handle* green_path;`
2. **`ray_realisation_3d()`** 添加 `ctx.green_path = args->green_path;`
3. **新建 `sdis_solve_camera_green_function()`**：需处理逐像素 Green buffer 管理（Camera 是二维采样阵列，而 Probe 是单点）

核心 MC 路径行走中的 `if(ctx->green_path)` 消费代码已全部就位（见 02 章），无需修改。

---

## 3. Green 函数文件格式

📍 `stardis-cpu/stardis/0.12/src/stardis-green-types.h`

```
┌──────────────────────────┐
│ green_file_header        │  版本号 (v4)
├──────────────────────────┤
│ green_description        │  材质/边界描述表
│   materials[]            │
│   boundaries[]           │
├──────────────────────────┤
│ green_sample_header      │  pw_count, fx_count, end 描述
├──────────────────────────┤
│ Per-path data ×N         │
│   end_type, end_id       │  路径终止类型/ID
│   power_terms[]          │  体积功率项（K/W）
│   flux_terms[]           │  表面通量项
└──────────────────────────┘
```

路径终止类型 (`end_type`):
- `SDIS_GREEN_PATH_END_AT_VERTEX` — 在已知温度顶点终止
- `SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV` — 在辐射环境终止
- `SDIS_GREEN_PATH_END_AT_INTERFACE_FRAGMENT` — 在已知温度界面碎片终止
