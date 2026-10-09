# STARDIS Green函数数据结构与用途分析

**生成时间**: 2026-01-21 19:47:00  
**目标**: 理解Green函数在STARDIS求解器中的作用、数据结构和生成使用流程  
**基于**: `stardis-cpu` 项目代码分析 + `ray_realisation_analysis.md`  

---

## 一、Green函数概述

### **1.1 什么是Green函数（在STARDIS中）**

在STARDIS蒙特卡洛辐射传输求解器中，**Green函数**（Green's function）表示热传输的**脉冲响应**（impulse response）：

```
G(x,t; x',t') = 在位置 x、时间 t 处的温度响应，
                 由位置 x'、时间 t' 的单位热源引起
```

**物理意义**:
- 当在 (x', t') 处施加 **1瓦特（W）** 的热源
- Green函数 G(x,t; x',t') 给出在 (x,t) 处产生的 **温度变化（Kelvin）**
- 因此单位是 **[K/W]**（开尔文每瓦特）

### **1.2 为什么需要Green函数**

**应用场景**:

| 用途 | 说明 | 优势 |
|------|------|------|
| **线性热传输分析** | 当系统满足线性假设（Picard阶数=1）时，可用叠加原理 | 计算一次Green函数，多次求解不同边界条件无需重新追踪射线 |
| **参数化研究** | 改变材质热导率、界面热流等参数 | 不需要重新运行蒙特卡洛采样，直接用Green函数计算 |
| **灵敏度分析** | 分析温度对热源位置/强度的敏感性 | Green函数提供解析梯度 |
| **逆问题求解** | 从温度测量推断热源分布 | Green函数作为正向算子的核 |

**限制条件**:
- 仅适用于 **Picard阶数 = 1** （线性化假设）
- 高阶Picard迭代（处理辐射-传导耦合非线性）无法使用Green函数

---

## 二、核心数据结构

### **2.1 Green函数主结构 (`struct sdis_green_function`)**

📍 位置: `stardis-solver/0.16.2/src/sdis_green.c:338-354`

```c
struct sdis_green_function {
    // ════════════════════════════════════════════════════
    // 路径存储（核心数据）
    // ════════════════════════════════════════════════════
    struct darray_green_path paths;  // 动态数组，存储所有采样路径
    
    // ════════════════════════════════════════════════════
    // 介质和界面注册表（哈希表）
    // ════════════════════════════════════════════════════
    struct htable_medium media;      // 介质ID → 介质指针
    struct htable_interf interfaces; // 界面ID → 界面指针
    
    // ════════════════════════════════════════════════════
    // 统计信息
    // ════════════════════════════════════════════════════
    size_t npaths_valid;    // 有效路径数量
    size_t npaths_invalid;  // 拒绝的路径数量（击中错误）
    
    // ════════════════════════════════════════════════════
    // 签名和版本
    // ════════════════════════════════════════════════════
    hash256_T signature;    // 场景的哈希签名（用于验证一致性）
    
    // ════════════════════════════════════════════════════
    // 性能统计
    // ════════════════════════════════════════════════════
    struct accum realisation_time;  // 每次实现的时间累积器
    
    // ════════════════════════════════════════════════════
    // 随机数生成器状态（用于可重现性）
    // ════════════════════════════════════════════════════
    enum ssp_rng_type rng_type;  // RNG类型
    FILE* rng_state;             // RNG状态文件
    
    // ════════════════════════════════════════════════════
    // 引用计数和场景指针
    // ════════════════════════════════════════════════════
    ref_T ref;                   // 引用计数（智能指针）
    struct sdis_scene* scn;      // 场景指针
};
```

**内存占用估算**:
```
每个Green函数实例基础大小: ~200-300 bytes
+ 路径数组: npaths × sizeof(struct green_path) ≈ npaths × 300 bytes
+ 哈希表: O(n_media + n_interfaces)

示例（10万条路径）: ~30 MB
```

---

### **2.2 Green路径结构 (`struct green_path`)**

📍 位置: `stardis-solver/0.16.2/src/sdis_green.c:102-119`

```c
struct green_path {
    // ════════════════════════════════════════════════════
    // 时间信息
    // ════════════════════════════════════════════════════
    double elapsed_time;  // 路径的运行时间
    
    // ════════════════════════════════════════════════════
    // 三种贡献项（动态数组）
    // ════════════════════════════════════════════════════
    struct darray_power_term power_terms;      // 体积功率项
    struct darray_flux_term flux_terms;        // 界面热流项
    struct darray_extflux_terms extflux_terms; // 外部热流项
    
    // ════════════════════════════════════════════════════
    // 路径终点信息（联合体）
    // ════════════════════════════════════════════════════
    union {
        struct sdis_rwalk_vertex vertex;          // 终止于体积
        struct sdis_interface_fragment fragment;  // 终止于界面
        struct sdis_radiative_ray ray;            // 终止于辐射环境
    } limit;
    
    unsigned limit_id;  // 终止介质/界面的ID
    enum sdis_green_path_end_type end_type;  // 终止类型
    
    // ════════════════════════════════════════════════════
    // 缓存优化（加速查找）
    // ════════════════════════════════════════════════════
    uint16_t ilast_medium;  // 上次访问的介质索引
    uint16_t ilast_interf;  // 上次访问的界面索引
};
```

**路径终止类型** (`enum sdis_green_path_end_type`):

| 终止类型 | 说明 | 温度来源 |
|---------|------|---------|
| `SDIS_GREEN_PATH_END_AT_INTERFACE` | 路径击中界面 | 界面温度边界条件 |
| `SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV` | 路径射向辐射环境（天空） | 辐射环境温度 |
| `SDIS_GREEN_PATH_END_IN_VOLUME` | 路径终止于体积内 | 介质初始温度 |
| `SDIS_GREEN_PATH_END_ERROR` | 路径无效（错误） | 不贡献 |

---

### **2.3 Green贡献项结构**

#### **功率项** (`struct power_term`)
📍 `stardis-solver/0.16.2/src/sdis_green.c:45-48`

```c
struct power_term {
    double term;      // Green函数系数 [K/W]
    unsigned id;      // 介质ID
};
```

**物理意义**:
- 当介质 `id` 内有体积功率 `P [W]` 时
- 对温度的贡献 = `term × P [K]`

#### **热流项** (`struct flux_term`)
📍 `stardis-solver/0.16.2/src/sdis_green.c:66-70`

```c
struct flux_term {
    double term;          // Green函数系数 [K/W/m²]
    unsigned id;          // 界面ID
    enum sdis_side side;  // 界面侧面（FRONT/BACK）
};
```

**物理意义**:
- 当界面 `id` 的 `side` 侧有热流 `q [W/m²]` 时
- 对温度的贡献 = `term × q [K]`

#### **外部热流项** (`struct sdis_green_external_flux_terms`)
📍 `stardis-solver/0.16.2/src/sdis.h:508-520`

```c
struct sdis_green_external_flux_terms {
    double term_wrt_power;               // 相对于源功率的项 [K/W]
    double term_wrt_diffuse_radiance;    // 相对于漫射辐射的项 [K/W/m²/sr]
    double time;                         // 时间 [s]
    double dir[3];                       // 方向（用于漫射辐射）
};
```

**物理意义**:
- `term_wrt_power`: 外部源功率 `P [W]` 的Green系数
- `term_wrt_diffuse_radiance`: 漫射辐射 `L [W/m²/sr]` 的Green系数

---

### **2.4 热路径追踪结构 (`struct sdis_heat_path`)**

📍 位置: `stardis-solver/0.16.2/src/sdis_heat_path.h:134-142`

```c
struct sdis_heat_path {
    // ════════════════════════════════════════════════════
    // 顶点序列（记录随机游走轨迹）
    // ════════════════════════════════════════════════════
    struct darray_heat_vertex vertices;  // 动态数组，存储所有顶点
    
    // ════════════════════════════════════════════════════
    // 分支点标记（Picard迭代）
    // ════════════════════════════════════════════════════
    struct darray_size_t breaks;  // 记录分支点的索引
    
    // ════════════════════════════════════════════════════
    // 状态标志
    // ════════════════════════════════════════════════════
    enum sdis_heat_path_flag status;  // NONE / SAVE_GEOMETRY / ...
};
```

#### **热顶点** (`struct sdis_heat_vertex`)
📍 `stardis-solver/0.16.2/src/sdis.h:446-454`

```c
struct sdis_heat_vertex {
    double P[3];      // 空间位置 [m]
    double time;      // 时间 [s]
    double weight;    // 温度权重 [K]
    
    enum sdis_heat_vertex_type type;  // 顶点类型
    int branch_id;    // 分支ID（Picard迭代）
};
```

**顶点类型** (`enum sdis_heat_vertex_type`):

| 类型 | 说明 |
|------|------|
| `SDIS_HEAT_VERTEX_RADIATIVE` | 辐射传输步骤 |
| `SDIS_HEAT_VERTEX_CONDUCTION` | 热传导步骤 |
| `SDIS_HEAT_VERTEX_CONVECTION` | 对流步骤 |

**用途对比**:

| `heat_path` | `green_path` |
|-------------|--------------|
| 记录几何轨迹（调试、可视化） | 记录Green函数贡献项（求解） |
| 可选功能（可禁用） | Green函数必需 |
| 存储顶点位置、时间 | 存储功率/热流系数 |

---

## 三、Green函数生成流程

### **3.1 整体流程图**

```
┌─────────────────────────────────────────────────────────────┐
│ 用户调用: sdis_solve_probe_green_function()                 │
│           sdis_solve_boundary_green_function()               │
│           sdis_solve_medium_green_function()                 │
└───────────────────┬─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────┐
│ 步骤1: 创建每线程Green函数                                  │
│   create_per_thread_green_function()                        │
│   - 为每个OpenMP线程分配独立的green_function                │
│   - 初始化哈希表、路径数组                                  │
└───────────────────┬─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────┐
│ 步骤2: 蒙特卡洛采样循环（并行）                             │
│   for (i = 0; i < nrealisations; i++) {                     │
│       // 为当前射线创建路径句柄                             │
│       green_function_create_path(&green_path_handle);       │
│                                                             │
│       // 追踪射线并记录Green贡献                            │
│       ray_realisation_3d(scn, args, &weight,                │
│                          green_path_handle);                │
│   }                                                         │
└───────────────────┬─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────┐
│ 步骤3: 聚合每线程Green函数                                  │
│   gather_green_functions()                                  │
│   - 合并所有线程的路径到主Green函数                         │
│   - MPI情况下：聚合所有进程的结果到rank 0                   │
└───────────────────┬─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────┐
│ 步骤4: 最终化Green函数                                      │
│   green_function_finalize()                                 │
│   - 计算有效/无效路径数量                                   │
│   - 保存RNG状态（用于可重现性）                             │
│   - 输出统计信息                                            │
└───────────────────┬─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────┐
│ 输出: struct sdis_green_function*                           │
│   - 包含所有采样路径的Green函数                             │
│   - 可用于sdis_green_function_solve()求解温度              │
│   - 可序列化到文件（sdis_green_function_write()）          │
└─────────────────────────────────────────────────────────────┘
```

---

### **3.2 关键函数详解**

#### **A. 创建Green路径** (`green_function_create_path`)

📍 `stardis-solver/0.16.2/src/sdis_green.c:1688-1702`

```c
res_T green_function_create_path(
    struct sdis_green_function* green,
    struct green_path_handle* handle  // 输出句柄
) {
    size_t n = darray_green_path_size_get(&green->paths);
    
    // 扩展路径数组（+1）
    res = darray_green_path_resize(&green->paths, n+1);
    
    // 设置句柄指向新路径
    handle->green = green;
    handle->path = darray_green_path_data_get(&green->paths) + n;
    
    return RES_OK;
}
```

**调用位置**: 在每次 `ray_realisation_3d` 之前

---

#### **B. 记录功率项** (`green_path_add_power_term`)

📍 `stardis-solver/0.16.2/src/sdis_green.c:1767-1823`

```c
res_T green_path_add_power_term(
    struct green_path_handle* handle,
    struct sdis_medium* mdm,          // 介质
    const struct sdis_rwalk_vertex* vertex,  // 顶点（未使用）
    const double term                 // Green系数 [K/W]
) {
    struct power_term power_term;
    
    // 注册介质到Green函数
    res = ensure_medium_registration(handle->green, mdm);
    
    // 累积功率项
    power_term.id = medium_get_id(mdm);
    power_term.term = term;  // [K/W]
    
    res = darray_power_term_push_back(&path->power_terms, &power_term);
    
    return RES_OK;
}
```

**调用位置**: 在热传导/对流路径中（WoS算法、Delta-Sphere算法）

---

#### **C. 记录热流项** (`green_path_add_flux_term`)

📍 `stardis-solver/0.16.2/src/sdis_green.c:1824-1883`

```c
res_T green_path_add_flux_term(
    struct green_path_handle* handle,
    struct sdis_interface* interf,    // 界面
    const struct sdis_interface_fragment* fragment,  // 未使用
    const double term                 // Green系数 [K/W/m²]
) {
    struct flux_term flux_term;
    
    // 注册界面到Green函数
    res = ensure_interface_registration(handle->green, interf);
    
    // 累积热流项
    flux_term.id = interface_get_id(interf);
    flux_term.term = term;  // [K/W/m²]
    flux_term.side = fragment->side;
    
    res = darray_flux_term_push_back(&path->flux_terms, &flux_term);
    
    return RES_OK;
}
```

**调用位置**: 在边界条件处理中（`handle_net_flux`）

---

#### **D. 设置路径终点** (`green_path_set_limit_*`)

📍 `stardis-solver/0.16.2/src/sdis_green.c:1706-1757`

```c
// 终止于界面
res_T green_path_set_limit_interface_fragment(
    struct green_path_handle* handle,
    struct sdis_interface* interf,
    const struct sdis_interface_fragment* fragment,
    const double elapsed_time
) {
    // 注册界面
    res = ensure_interface_registration(handle->green, interf);
    
    // 设置终点类型和数据
    handle->path->end_type = SDIS_GREEN_PATH_END_AT_INTERFACE;
    handle->path->limit_id = interface_get_id(interf);
    handle->path->limit.fragment = *fragment;
    handle->path->elapsed_time = elapsed_time;
    
    return RES_OK;
}

// 终止于辐射环境
res_T green_path_set_limit_radiative_ray(...) {
    handle->path->end_type = SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV;
    // ...
}

// 终止于体积
res_T green_path_set_limit_vertex(...) {
    handle->path->end_type = SDIS_GREEN_PATH_END_IN_VOLUME;
    // ...
}
```

**调用位置**: 在路径追踪结束时（`trace_radiative_path`, `conductive_path`, 等）

---

### **3.3 路径追踪中的Green函数记录**

以辐射路径为例（`trace_radiative_path_3d`）:

```c
// 📍 stardis-solver/0.16.2/src/sdis_heat_path_radiative_Xd.h:197-321

for(;;) {  // 射线弹跳循环
    // A. 求交
    res = find_next_fragment(...);
    
    // B. 检查是否到达辐射环境
    if (SXD_HIT_NONE(&rwalk.hit_3d)) {
        // 设置Green路径终点
        if(ctx->green_path) {
            res = green_path_set_limit_radiative_ray(
                ctx->green_path, &ray, rwalk.elapsed_time
            );
        }
        T.done = 1;
        break;  // 退出循环
    }
    
    // C. 更新位置
    d3_set(rwalk.vtx.P, frag.P);
    
    // D. 记录热顶点（如果启用heat_path）
    res = register_heat_vertex(
        ctx->heat_path, &rwalk.vtx, T.value, 
        SDIS_HEAT_VERTEX_RADIATIVE, branch_id
    );
    
    // E. BRDF采样
    brdf_sample(&brdf, rng, wi, N, &bounce);
    
    // F. 俄罗斯轮盘赌
    if (ssp_rng_canonical(rng) < brdf.emissivity) {
        // 吸收：切换到边界路径
        T.func = boundary_path_3d;
        break;
    }
    
    // 继续弹跳
}
```

---

## 四、Green函数求解流程

### **4.1 求解单条路径**

📍 `stardis-solver/0.16.2/src/sdis_green.c:530-591`

```c
static res_T green_function_solve_path(
    struct sdis_green_function* green,
    const size_t ipath,           // 路径索引
    double* weight                // 输出：温度权重 [K]
) {
    const struct green_path* path = 
        darray_green_path_cdata_get(&green->paths) + ipath;
    
    // 跳过无效路径
    if(path->end_type == SDIS_GREEN_PATH_END_ERROR) {
        *weight = 0;
        return RES_OK;
    }
    
    // ════════════════════════════════════════════════════
    // 计算三种贡献
    // ════════════════════════════════════════════════════
    double power = green_path_power_contribution(green, path);
    double flux = green_path_flux_contribution(green, path);
    double external_flux = green_path_external_flux_contribution(green, path);
    
    // ════════════════════════════════════════════════════
    // 获取终点温度
    // ════════════════════════════════════════════════════
    double end_temperature = 0;
    
    switch(path->end_type) {
    case SDIS_GREEN_PATH_END_AT_INTERFACE:
        // 从界面温度边界条件获取
        interf = green_function_fetch_interf(green, path->limit_id);
        end_temperature = interface_get_temperature(
            interf, path->limit.fragment.side, &path->limit.fragment
        );
        break;
        
    case SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV:
        // 从辐射环境获取
        end_temperature = radiative_env_get_temperature(
            green->scn->radenv, &path->limit.ray
        );
        break;
        
    case SDIS_GREEN_PATH_END_IN_VOLUME:
        // 从介质初始温度获取
        medium = green_function_fetch_medium(green, path->limit_id);
        end_temperature = medium_get_temperature(
            medium, &path->limit.vertex
        );
        break;
    }
    
    // ════════════════════════════════════════════════════
    // 总温度 = 功率贡献 + 热流贡献 + 外部贡献 + 终点温度
    // ════════════════════════════════════════════════════
    *weight = power + flux + external_flux + end_temperature;
    
    return RES_OK;
}
```

**Green函数温度计算公式**:

```
T(x) = ∑[路径] ( G_power · P + G_flux · q + G_ext · S + T_boundary )

其中:
  G_power  : 功率Green系数 [K/W]
  P        : 体积功率 [W]
  G_flux   : 热流Green系数 [K/W/m²]
  q        : 界面热流 [W/m²]
  G_ext    : 外部源Green系数 [K/W] 或 [K/W/m²/sr]
  S        : 外部源强度 [W] 或 [W/m²/sr]
  T_boundary : 边界温度 [K]
```

---

### **4.2 求解整个Green函数**

📍 `stardis-solver/0.16.2/src/sdis_green.c:938-991`

```c
res_T sdis_green_function_solve(
    struct sdis_green_function* green,
    struct sdis_estimator** out_estimator  // 输出估计器
) {
    struct sdis_estimator* estimator = NULL;
    size_t npaths = darray_green_path_size_get(&green->paths);
    
    // 创建温度估计器
    res = estimator_create(
        green->scn->dev, 
        SDIS_ESTIMATOR_TEMPERATURE, 
        &estimator
    );
    
    // 遍历所有路径，累积温度
    FOR_EACH(ipath, 0, npaths) {
        double w = 0;
        
        res = green_function_solve_path(green, ipath, &w);
        if(res != RES_OK) goto error;
        
        // 累积到估计器
        estimator_add_realisation(estimator, w);
    }
    
    // 设置估计器的统计信息
    estimator_set_realisation_time(
        estimator, 
        green->realisation_time.sum, 
        green->realisation_time.sum2
    );
    
    *out_estimator = estimator;
    return RES_OK;
}
```

**输出**: `struct sdis_estimator` 包含:
- 平均温度: `sum / count`
- 标准差: `sqrt(sum2 / count - (sum/count)²)`
- 采样统计信息

---

## 五、Green函数序列化

### **5.1 写入文件**

📍 `stardis-solver/0.16.2/src/sdis_green.c:993-1055`

```c
res_T sdis_green_function_write(
    struct sdis_green_function* green, 
    FILE* stream
) {
    // 写入版本号
    fwrite(&SDIS_GREEN_FUNCTION_VERSION, ...);
    
    // 写入场景哈希（验证一致性）
    res = scene_compute_hash(green->scn, hash);
    fwrite(&hash, ...);
    
    // 写入签名
    fwrite(&green->signature, ...);
    
    // 写入介质列表
    res = write_media(green, stream);
    
    // 写入界面列表
    res = write_interfaces(green, stream);
    
    // 写入所有路径
    res = write_paths_list(green, stream);
    
    // 写入统计信息
    fwrite(&green->npaths_valid, ...);
    fwrite(&green->npaths_invalid, ...);
    fwrite(&green->realisation_time, ...);
    
    // 写入RNG状态
    fwrite(&green->rng_type, ...);
    res = ssp_rng_write(rng, stream);
    
    return RES_OK;
}
```

**文件格式** (二进制):
```
┌────────────────────────────────────────┐
│ 版本号 (int)                           │
├────────────────────────────────────────┤
│ 场景哈希 (256位)                       │
├────────────────────────────────────────┤
│ 签名哈希 (256位)                       │
├────────────────────────────────────────┤
│ 介质数量 (size_t)                      │
│ 介质ID列表 (unsigned[])                │
├────────────────────────────────────────┤
│ 界面数量 (size_t)                      │
│ 界面ID列表 (unsigned[])                │
├────────────────────────────────────────┤
│ 路径数量 (size_t)                      │
│ ┌──────────────────────────────────┐  │
│ │ 路径0 (struct green_path序列化)   │  │
│ │ 路径1                            │  │
│ │ ...                              │  │
│ └──────────────────────────────────┘  │
├────────────────────────────────────────┤
│ 有效路径数 (size_t)                    │
│ 无效路径数 (size_t)                    │
│ 时间统计 (struct accum)                │
│ RNG类型 (enum)                         │
│ RNG状态 (依赖于RNG类型)                │
└────────────────────────────────────────┘
```

---

### **5.2 从文件读取**

📍 `stardis-solver/0.16.2/src/sdis_green.c:1057-1150`

```c
res_T sdis_green_function_create_from_stream(
    struct sdis_green_function_create_from_stream_args* args,
    struct sdis_green_function** out_green
) {
    // 读取版本号并验证
    fread(&version, ...);
    if(version != SDIS_GREEN_FUNCTION_VERSION) {
        log_err(..., "版本不匹配");
        return RES_BAD_ARG;
    }
    
    // 读取并验证场景哈希
    fread(&hash0, ...);
    res = scene_compute_hash(args->scene, hash1);
    if(!hash256_eq(hash0, hash1)) {
        log_err(..., "场景不一致");
        return RES_BAD_ARG;
    }
    
    // 验证签名
    fread(&signature, ...);
    if(!hash256_eq(signature, green->signature)) {
        log_err(..., "签名不匹配");
        return RES_BAD_ARG;
    }
    
    // 读取介质、界面、路径
    res = read_media(green, args->stream);
    res = read_interfaces(green, args->stream);
    res = read_paths_list(green, args->stream);
    
    // 读取统计和RNG状态
    fread(&green->npaths_valid, ...);
    fread(&green->rng_type, ...);
    res = ssp_rng_read(rng, stream);
    
    *out_green = green;
    return RES_OK;
}
```

---

## 六、GPU实现考虑

### **6.1 数据结构GPU化**

**挑战**:

| CPU特性 | GPU挑战 | 解决方案 |
|---------|---------|---------|
| 动态数组 | GPU不支持动态分配 | 预分配固定大小数组 + 原子计数 |
| 哈希表 | 内存跳转不友好 | 扁平化数组 + 索引映射 |
| 指针跳转 | 缓存miss严重 | Structure of Arrays (SoA) |
| 函数指针 | GPU间接跳转慢 | 枚举 + switch或多Kernel |

**GPU化改造方案**:

```cuda
// ════════════════════════════════════════════════════
// ❌ CPU: 动态数组
// ════════════════════════════════════════════════════
struct green_path {
    struct darray_power_term power_terms;  // 运行时增长
};

// ════════════════════════════════════════════════════
// ✅ GPU: 固定容量数组
// ════════════════════════════════════════════════════
#define MAX_POWER_TERMS 64

struct gpu_green_path {
    PowerTerm power_terms[MAX_POWER_TERMS];  // 预分配
    int power_count;  // 原子递增
};

__device__ void add_power_term(gpu_green_path* path, const PowerTerm* term) {
    int idx = atomicAdd(&path->power_count, 1);
    if (idx < MAX_POWER_TERMS) {
        path->power_terms[idx] = *term;
    }
    // 溢出处理
}
```

---

### **6.2 Green函数存储布局**

**方案A: 每射线独立存储（适合Wavefront）**

```cuda
struct GPUGreenFunctionStream {
    // 每条射线的Green路径（SoA布局）
    int*    path_end_types;       // [N]
    uint32_t* path_limit_ids;     // [N]
    
    // 功率项（嵌套数组）
    PowerTerm* power_terms;       // [N * MAX_POWER_TERMS]
    int*       power_counts;      // [N]
    
    // 热流项
    FluxTerm* flux_terms;         // [N * MAX_FLUX_TERMS]
    int*      flux_counts;        // [N]
    
    // 外部热流项
    ExtFluxTerms* extflux_terms;  // [N * MAX_EXTFLUX_TERMS]
    int*          extflux_counts; // [N]
    
    int active_count;  // 活跃路径数
};
```

**方案B: 全局池 + 原子分配（适合大规模）**

```cuda
struct GPUGreenFunctionPool {
    // 全局路径池
    GreenPath* paths;             // [MAX_PATHS]
    int path_count;               // 原子计数器
    
    // 全局贡献项池
    PowerTerm* power_pool;        // [MAX_POWER_POOL]
    FluxTerm*  flux_pool;         // [MAX_FLUX_POOL]
    
    int power_pool_offset;        // 原子计数器
    int flux_pool_offset;
};

__global__ void allocate_green_path(GPUGreenFunctionPool* pool, int* path_id) {
    *path_id = atomicAdd(&pool->path_count, 1);
    if (*path_id >= MAX_PATHS) {
        // 错误：超出容量
    }
}
```

---

### **6.3 Wavefront路径追踪中的Green记录**

```cuda
__global__ void wavefront_trace_kernel(
    RayStream* rays,
    GPUGreenFunctionStream* green_paths,
    int N
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N) return;
    
    // ════════════════════════════════════════════════════
    // A. 射线-几何求交
    // ════════════════════════════════════════════════════
    HitInfo hit;
    intersect_scene(rays->origins[tid], rays->directions[tid], &hit);
    
    // ════════════════════════════════════════════════════
    // B. 判断终止条件
    // ════════════════════════════════════════════════════
    if (!hit.valid) {
        // 到达辐射环境
        green_paths->path_end_types[tid] = GREEN_PATH_END_AT_RADIATIVE_ENV;
        green_paths->path_limit_ids[tid] = RADIATIVE_ENV_ID;
        rays->active_mask[tid] = 0;  // 标记非活跃
        return;
    }
    
    // ════════════════════════════════════════════════════
    // C. 记录功率项（示例：传导路径）
    // ════════════════════════════════════════════════════
    if (material_is_solid(hit.material_id)) {
        int idx = tid * MAX_POWER_TERMS + green_paths->power_counts[tid];
        if (idx < MAX_POWER_TERMS) {
            green_paths->power_terms[idx].id = hit.material_id;
            green_paths->power_terms[idx].term = compute_green_coefficient(...);
            atomicAdd(&green_paths->power_counts[tid], 1);
        }
    }
    
    // ════════════════════════════════════════════════════
    // D. 更新射线状态（继续追踪）
    // ════════════════════════════════════════════════════
    rays->origins[tid] = hit.position;
    rays->directions[tid] = sample_brdf(...);
}
```

---

### **6.4 Green函数求解Kernel**

```cuda
__global__ void green_function_solve_kernel(
    GPUGreenFunctionStream* green_paths,
    double* temperatures,  // 输出：每条路径的温度
    int N
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N) return;
    
    double T = 0;
    
    // ════════════════════════════════════════════════════
    // A. 累积功率贡献
    // ════════════════════════════════════════════════════
    int power_count = green_paths->power_counts[tid];
    for (int i = 0; i < power_count; i++) {
        int idx = tid * MAX_POWER_TERMS + i;
        PowerTerm term = green_paths->power_terms[idx];
        
        double P = fetch_medium_power(term.id);  // 从材质表查询
        T += term.term * P;  // [K/W] * [W] = [K]
    }
    
    // ════════════════════════════════════════════════════
    // B. 累积热流贡献
    // ════════════════════════════════════════════════════
    int flux_count = green_paths->flux_counts[tid];
    for (int i = 0; i < flux_count; i++) {
        int idx = tid * MAX_FLUX_TERMS + i;
        FluxTerm term = green_paths->flux_terms[idx];
        
        double q = fetch_interface_flux(term.id, term.side);
        T += term.term * q;  // [K/W/m²] * [W/m²] = [K]
    }
    
    // ════════════════════════════════════════════════════
    // C. 加上终点温度
    // ════════════════════════════════════════════════════
    int end_type = green_paths->path_end_types[tid];
    uint32_t limit_id = green_paths->path_limit_ids[tid];
    
    switch (end_type) {
    case GREEN_PATH_END_AT_INTERFACE:
        T += fetch_interface_temperature(limit_id);
        break;
    case GREEN_PATH_END_AT_RADIATIVE_ENV:
        T += fetch_radiative_env_temperature();
        break;
    case GREEN_PATH_END_IN_VOLUME:
        T += fetch_medium_temperature(limit_id);
        break;
    }
    
    temperatures[tid] = T;
}
```

---

### **6.5 内存占用估算**

假设场景:
- 分辨率: 1920×1080 = 2,073,600 像素
- 每像素射线数: 100
- 总射线数: 207,360,000

**方案A（每射线存储）**:
```
单条路径大小:
  - end_type, limit_id: 8 bytes
  - power_terms[64]: 64 × 16 bytes = 1024 bytes
  - flux_terms[32]: 32 × 20 bytes = 640 bytes
  - extflux_terms[16]: 16 × 40 bytes = 640 bytes
  - 计数器: 12 bytes
  总计: ~2.3 KB/路径

总内存: 207M × 2.3 KB ≈ 476 GB  ❌ 超出RTX 4090 (24GB)
```

**优化方案**:

1. **分批处理（Tiling）**:
   - 每批 1M 射线
   - 每批内存: 2.3 GB ✅
   - 批次数: 208
   
2. **压缩存储**:
   - 功率项共享pool：64 → 8 项/路径（平均）
   - 内存: 207M × 300 bytes ≈ 62 GB ❌ 仍超
   
3. **增量写回到CPU**:
   - GPU保持活跃射线（~10M）: 23 GB ✅
   - 完成的路径立即传回CPU并清空

---

## 七、使用示例

### **7.1 生成Green函数**

```c
// ════════════════════════════════════════════════════
// 示例：求解探针位置的Green函数
// ════════════════════════════════════════════════════

#include <sdis/sdis.h>

int main() {
    struct sdis_scene* scn = NULL;
    struct sdis_green_function* green = NULL;
    
    // 1. 加载场景
    sdis_scene_create(..., &scn);
    
    // 2. 设置探针参数
    struct sdis_solve_probe_args probe_args = 
        SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    
    probe_args.position[0] = 0.5;  // x
    probe_args.position[1] = 0.5;  // y
    probe_args.position[2] = 0.5;  // z
    probe_args.time = 0.0;
    probe_args.picard_order = 1;  // ⚠️ 必须为1
    probe_args.nrealisations = 1000000;  // 1M采样
    probe_args.rng_type = SSP_RNG_THREEFRY;
    probe_args.signature = my_signature;  // 可选
    
    // 3. 生成Green函数
    res = sdis_solve_probe_green_function(scn, &probe_args, &green);
    
    // 4. 写入文件（可选）
    FILE* fp = fopen("probe_green.bin", "wb");
    sdis_green_function_write(green, fp);
    fclose(fp);
    
    // 5. 清理
    sdis_green_function_ref_put(green);
    sdis_scene_ref_put(scn);
    
    return 0;
}
```

---

### **7.2 使用Green函数求解温度**

```c
// ════════════════════════════════════════════════════
// 示例：加载Green函数并求解不同边界条件下的温度
// ════════════════════════════════════════════════════

int main() {
    struct sdis_scene* scn = NULL;
    struct sdis_green_function* green = NULL;
    struct sdis_estimator* estimator = NULL;
    
    // 1. 加载场景
    sdis_scene_create(..., &scn);
    
    // 2. 从文件加载Green函数
    struct sdis_green_function_create_from_stream_args args = 
        SDIS_GREEN_FUNCTION_CREATE_FROM_STREAM_ARGS_DEFAULT;
    
    FILE* fp = fopen("probe_green.bin", "rb");
    args.scene = scn;
    args.stream = fp;
    args.signature = my_signature;
    
    res = sdis_green_function_create_from_stream(&args, &green);
    fclose(fp);
    
    // 3. 修改边界条件（示例：改变界面温度）
    struct sdis_interface* interf = ...;
    sdis_interface_set_temperature(interf, SDIS_FRONT, 500.0);  // 500K
    
    // 4. 求解温度
    res = sdis_green_function_solve(green, &estimator);
    
    // 5. 获取结果
    double T_mean, T_std;
    estimator_get_temperature(estimator, &T_mean, &T_std);
    
    printf("温度: %.2f ± %.2f K\n", T_mean, T_std);
    
    // 6. 清理
    sdis_estimator_ref_put(estimator);
    sdis_green_function_ref_put(green);
    sdis_scene_ref_put(scn);
    
    return 0;
}
```

---

### **7.3 参数化研究示例**

```c
// ════════════════════════════════════════════════════
// 示例：使用同一Green函数研究不同热源配置
// ════════════════════════════════════════════════════

int main() {
    struct sdis_green_function* green = load_green_function(...);
    
    // 测试不同的体积功率
    double powers[] = {100, 200, 500, 1000};  // Watts
    
    for (int i = 0; i < 4; i++) {
        // 设置新的体积功率
        struct sdis_medium* solid = ...;
        sdis_medium_set_volumic_power(solid, powers[i]);
        
        // 求解温度（无需重新追踪射线！）
        struct sdis_estimator* est = NULL;
        sdis_green_function_solve(green, &est);
        
        double T_mean;
        estimator_get_temperature(est, &T_mean, NULL);
        
        printf("功率 %.0f W → 温度 %.2f K\n", powers[i], T_mean);
        
        sdis_estimator_ref_put(est);
    }
    
    sdis_green_function_ref_put(green);
    return 0;
}
```

**输出示例**:
```
功率 100 W → 温度 320.45 K
功率 200 W → 温度 340.90 K
功率 500 W → 温度 401.12 K
功率 1000 W → 温度 521.67 K
```

---

## 八、GPU实现路线图

### **阶段1: CPU Green函数验证** ⏱️ 1周

- [ ] 运行现有测试用例（`test_sdis_solve_probe.c`）
- [ ] 理解Green函数输出格式
- [ ] 验证序列化/反序列化流程
- [ ] 建立基准测试数据集

### **阶段2: Green路径记录到GPU** ⏱️ 2周

- [ ] 设计GPU Green路径存储结构（SoA布局）
- [ ] 实现 `green_path_add_power_term` GPU版本
- [ ] 实现 `green_path_add_flux_term` GPU版本
- [ ] 实现 `green_path_set_limit_*` GPU版本
- [ ] 验证单条路径的记录正确性

### **阶段3: Wavefront路径追踪集成** ⏱️ 2周

- [ ] 在Wavefront kernel中插入Green记录调用
- [ ] 处理路径终止时的Green路径最终化
- [ ] 实现活跃路径压缩（避免记录无效路径）
- [ ] 优化内存布局（减少bank conflict）

### **阶段4: Green函数聚合与求解** ⏱️ 1周

- [ ] 实现路径从GPU到CPU的传输
- [ ] CPU端聚合Green路径（复用现有代码）
- [ ] 实现Green函数求解的GPU Kernel（可选）
- [ ] 验证Green函数求解结果与CPU一致

### **阶段5: 内存优化** ⏱️ 1周

- [ ] 分批处理（Tiling）支持大规模场景
- [ ] 增量写回完成的Green路径到CPU
- [ ] 压缩存储（共享介质/界面ID池）
- [ ] 性能profiling和瓶颈分析

### **阶段6: 集成测试** ⏱️ 1周

- [ ] 端到端测试：生成→序列化→求解
- [ ] 数值精度验证（与CPU对比，容差<1e-6）
- [ ] 性能测试（vs CPU Green函数生成）
- [ ] 大规模场景测试（10M+ 路径）

**总时间估算**: 8周（2个月）

---

## 九、常见问题

### **Q1: 为什么Green函数只能用于Picard阶数=1？**

**A**: Green函数基于线性叠加原理。当Picard阶数>1时，系统考虑高阶辐射-传导耦合项，这些是非线性的（温度出现在Stefan-Boltzmann项中作为T⁴）。非线性系统不满足叠加原理，因此Green函数方法失效。

---

### **Q2: Green函数和heat_path有什么区别？**

**A**:

| `green_path` | `heat_path` |
|--------------|-------------|
| 存储Green函数贡献项（功率/热流系数） | 存储几何轨迹（顶点位置） |
| 用于求解温度（线性分析） | 用于调试和可视化 |
| 必需（如果生成Green函数） | 可选 |
| 小内存（~300 bytes/路径） | 大内存（~50 bytes/顶点，数千顶点/路径） |

---

### **Q3: GPU实现时最大的挑战是什么？**

**A**: **内存容量**。每条Green路径需要~2 KB（包含功率、热流项）。1亿条路径需要~200 GB，远超RTX 4090的24 GB。解决方案：
1. 分批处理（Tiling）
2. 压缩存储（共享ID池）
3. 增量写回到CPU

---

### **Q4: 能否在GPU上直接求解Green函数？**

**A**: 可以！但收益有限。Green函数求解本质是：
```
T = ∑[路径] (∑[功率项] term_i × P_i + ∑[热流项] term_j × q_j + T_boundary)
```

这是简单的累加操作，GPU并行化收益不大（内存带宽限制）。建议CPU端求解，除非需要实时改变边界条件并求解（如参数扫描）。

---

### **Q5: Green函数文件有多大？**

**A**: 取决于路径数量和贡献项数量：

| 路径数 | 平均功率项 | 平均热流项 | 文件大小（估算） |
|--------|-----------|-----------|----------------|
| 10K | 2 | 1 | ~1 MB |
| 100K | 4 | 2 | ~15 MB |
| 1M | 8 | 4 | ~200 MB |
| 10M | 8 | 4 | ~2 GB |

实际大小还包括介质/界面元数据、RNG状态等。

---

## 十、参考资料

### **源文件位置**

| 文件 | 说明 |
|------|------|
| `stardis-solver/0.16.2/src/sdis_green.h` | Green函数API声明 |
| `stardis-solver/0.16.2/src/sdis_green.c` | Green函数实现（1900行） |
| `stardis-solver/0.16.2/src/sdis_heat_path.h` | 热路径数据结构 |
| `stardis-solver/0.16.2/src/sdis_solve_probe_Xd.h` | 探针Green函数求解 |
| `stardis-solver/0.16.2/src/sdis_solve_boundary_Xd.h` | 边界Green函数求解 |
| `stardis-solver/0.16.2/src/sdis_solve_medium_Xd.h` | 介质Green函数求解 |

### **相关文档**

- `guide/ray_realisation_analysis.md` — 射线追踪执行流程分析
- `guide/IR_arch_gpu_impl_guide.md` — GPU实现架构指南

---

**文档版本**: 1.0  
**最后更新**: 2026-01-21 19:47:00  
**维护者**: Sisyphus (AI Agent)
