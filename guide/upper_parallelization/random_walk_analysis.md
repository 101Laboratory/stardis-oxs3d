# Stardis 随机游走算法分析

**生成时间**: 2026-01-21  
**分析范围**: stardis-cpu-cuda_impl_analysis 项目  
**目标**: 理解随机游走算法在辐射传输模拟中的作用、调用时机和具体实现  

---

## 目录

1. [算法用途](#1-算法用途)
2. [调用时机与光追逻辑集成](#2-调用时机与光追逻辑集成)
3. [核心实现详解](#3-核心实现详解)
4. [数据结构](#4-数据结构)
5. [GPU移植要点](#5-gpu移植要点)

---

## 1. 算法用途

### 1.1 总体目标
Stardis的随机游走算法用于**蒙特卡洛热辐射传输模拟**，通过随机采样粒子路径来估计温度场和热流分布。这是一种无偏统计方法，适用于复杂几何和多物理场耦合的热传输问题。

### 1.2 物理模型
算法模拟三种热传输模式的耦合：

| 传输模式 | 物理机制 | 应用场景 |
|---------|---------|---------|
| **辐射传输** (Radiative) | 光子在流体介质中的散射/吸收/发射 | 流体域内的热辐射，视线方向传播 |
| **对流传输** (Convective) | 流体内的热对流 | 流体域内的对流热交换 |
| **导热传输** (Conductive) | 固体内的热传导 | 固体域内的热扩散 |

### 1.3 关键特性
- **Picard迭代**: 支持1到N阶迭代求解非线性耦合问题（默认1阶，线性化处理）
- **时间依赖**: 每次采样包含时间维度，支持瞬态热传输
- **边界条件**: 支持温度边界、对流边界、热流边界
- **收敛判据**: 通过多次实现（realisation）累积统计量，计算均值和方差

---

## 2. 调用时机与光追逻辑集成

### 2.1 高层调用链

```
stardis_compute()                          // 主应用入口
    └─> compute_probe()                    // 根据模式分发
        └─> sdis_solve_probe()            // 求解器API
            └─> solve_probe_3d()          // 3D实现
                └─> [OpenMP并行循环]
                    └─> probe_realisation_3d()     // 单次实现
                        └─> sample_coupled_path_3d() // 耦合路径采样
                            └─> [递归调用]
                                ├─> radiative_path_3d()   // 辐射路径
                                ├─> convective_path_3d()  // 对流路径
                                └─> conductive_path_3d()  // 导热路径
```

### 2.2 光追集成时机

#### 时机1: 初始化阶段（辐射路径起点）
**文件**: `sdis_realisation.c::ray_realisation_3d()`  
**时机**: 当需要从指定位置/方向启动辐射路径时

```c
// 伪代码示意
res = trace_radiative_path_3d(scn, dir, &ctx, &rwalk, rng, &T);
if (!T.done) {
    // 如果辐射路径未结束，继续采样耦合路径
    res = sample_coupled_path_3d(scn, &ctx, &rwalk, rng, &T);
}
```

**触发条件**:
- Camera rendering（相机渲染）
- 显式指定方向的辐射采样
- 边界出射辐射估计

#### 时机2: 探针求解（Probe Solve）
**文件**: `sdis_solve_probe_Xd.h::solve_probe_3d()`  
**时机**: 每个Monte Carlo实现的主循环

```c
// L316-L416: OpenMP并行循环
#pragma omp parallel for schedule(static)
for (irealisation = 0; irealisation < nrealisations; ++irealisation) {
    // 采样观测时间
    time = sample_time(rng, args->time_range);
    
    // 调用探针实现函数
    res = probe_realisation_3d(scn, &realis_args, &w);
    
    // 累积权重到统计量
    acc_temp->sum += w;
    acc_temp->sum2 += w*w;
    acc_temp->count += 1;
}
```

**特点**:
- 像素/探针并行：每个线程独立RNG
- 实现并行：多次采样统计平均
- 进度跟踪：实时更新完成百分比

#### 时机3: 辐射路径内部（BRDF采样）
**文件**: `sdis_heat_path_radiative_Xd.h::trace_radiative_path_3d()`  
**时机**: 每次光子与界面交互时

```c
// L223-L350: 辐射随机游走主循环
for (;;) {
    // 1. 光线追踪找下一个交点
    res = find_next_fragment(scn, pos, dir, &hit, ...);
    
    if (HIT_NONE(&hit)) {
        // 到达辐射环境边界，设置极限温度
        set_limit_radiative_temperature(...);
        break;
    }
    
    // 2. 检查界面类型
    res = check_interface(interf, &frag);
    
    // 3. BRDF采样新方向
    res = brdf_sample(&brdf, wi, rng, &bounce);
    
    // 4. 俄罗斯轮盘赌终止
    if (russian_roulette_terminates) break;
    
    // 5. 更新光线方向，继续追踪
    dir = bounce.wo;
    nbounces++;
}
```

**集成要点**:
- **光追频率**: 每次弹跳调用一次 `find_next_fragment()`
- **几何查询**: 使用 `star-3d` 库的 `s3d_scene_view_trace_ray()`
- **终止条件**: 到达边界、吸收、或超过最大弹跳数

#### 时机4: 对流/导热路径（域内采样）
**文件**: `sdis_heat_path_convective_Xd.h`, `sdis_heat_path_conductive_*.h`  
**时机**: 粒子进入固体或流体域后

```c
// 对流路径示例
res = convective_path_3d(scn, ctx, rwalk, rng, &T);

// 内部逻辑：
// 1. 检查流体温度是否已知
if (fluid_temperature_is_known) {
    T->value = temperature;
    T->done = 1;
    return;
}

// 2. 采样对流时间（指数分布）
dt = sample_convection_time(rng, hc_upper_bound);

// 3. 更新时间，检查是否到达初始条件
rwalk->vtx.time -= dt;
if (rwalk->vtx.time <= t0) {
    // 到达初始条件
}

// 4. 光线追踪找边界交点
res = trace_to_boundary(scn, &rwalk->hit);

// 5. 转入边界路径
T->func = boundary_path_3d;
```

### 2.3 调用流程图

```
[用户请求] → [stardis_compute()] → [sdis_solve_probe()]
                                          ↓
                        [OpenMP并行: N个实现]
                                          ↓
                            [probe_realisation_3d()]
                                    ↓
                        [初始化rwalk状态]
                                    ↓
                    ┌─────────────────────────┐
                    │ sample_coupled_path_3d() │ ←─┐ (递归)
                    └─────────┬───────────────┘   │
                              ↓                    │
                    根据介质类型选择路径函数:       │
                              ↓                    │
              ┌───────────────┼───────────────┐   │
              ↓               ↓               ↓   │
        radiative_path   convective_path  conductive_path
              │               │               │   │
              └───────────────┴───────────────┴───┘
                              ↓
                    [光追: trace_ray()]
                              ↓
                    [BRDF/边界采样]
                              ↓
                    [更新rwalk状态]
                              ↓
                    [累积温度权重]
```

---

## 3. 核心实现详解

### 3.1 主入口函数

#### `ray_realisation_3d()` - 辐射路径实现
**位置**: `sdis_realisation.c:57-108`

```c
res_T ray_realisation_3d(
    struct sdis_scene* scn,
    struct ray_realisation_args* args,
    double* weight)
{
    struct rwalk_context ctx = RWALK_CONTEXT_NULL;
    struct rwalk rwalk = RWALK_NULL;
    struct temperature T = TEMPERATURE_NULL;
    float dir[3];
    
    // 1. 初始化随机游走状态
    d3_set(rwalk.vtx.P, args->position);      // 起点
    rwalk.vtx.time = args->time;              // 初始时间
    rwalk.enc_id = args->enc_id;              // 包壳ID
    
    // 2. 初始化上下文（温度边界、Picard阶数等）
    ctx.Tmin = scn->tmin;
    ctx.Tmax = scn->tmax;
    ctx.max_branchings = args->picard_order - 1;
    
    // 3. 记录起点到热路径
    register_heat_vertex(args->heat_path, &rwalk.vtx, ...);
    
    // 4. 追踪辐射路径
    res = trace_radiative_path_3d(scn, dir, &ctx, &rwalk, rng, &T);
    if (res != RES_OK) goto error;
    
    // 5. 如果未完成，继续耦合路径采样
    if (!T.done) {
        res = sample_coupled_path_3d(scn, &ctx, &rwalk, rng, &T);
    }
    
    *weight = T.value;  // 返回温度估计值
    return res;
}
```

**关键点**:
- `trace_radiative_path_3d`: 光子在流体中传播，遇到界面时BRDF采样
- `sample_coupled_path_3d`: 粒子进入固体/流体域后的耦合路径
- `T.done`: 标志路径是否已到达已知温度条件

#### `probe_realisation_3d()` - 探针实现
**位置**: `sdis_realisation_Xd.h:173-268`

```c
res_T probe_realisation_3d(
    struct sdis_scene* scn,
    struct probe_realisation_args* args,
    double* weight)
{
    struct sdis_medium* mdm = NULL;
    struct rwalk rwalk = RWALK_NULL;
    struct temperature T = TEMPERATURE_NULL;
    
    // 1. 获取起点所在介质
    enc = scene_get_enclosure(scn, args->enc_id);
    res = scene_get_enclosure_medium(scn, enc, &mdm);
    
    // 2. 根据介质类型选择初始路径函数
    switch (sdis_medium_get_type(mdm)) {
        case SDIS_FLUID:
            T.func = convective_path_3d;
            get_temperature = fluid_get_temperature;
            break;
        case SDIS_SOLID:
            T.func = conductive_path_3d;
            get_temperature = solid_get_temperature;
            break;
    }
    
    // 3. 检查初始条件
    if (t0 >= rwalk.vtx.time) {
        // 时间早于初始时间，直接查询初始温度
        tmp = get_temperature(mdm, &rwalk.vtx);
        if (TEMPERATURE_IS_KNOWN(tmp)) {
            *weight = tmp;
            return RES_OK;
        }
    }
    
    // 4. 采样耦合路径
    res = sample_coupled_path_3d(scn, &ctx, &rwalk, rng, &T);
    *weight = T.value;
    return res;
}
```

### 3.2 耦合路径采样

#### `sample_coupled_path_3d()` - 递归核心
**位置**: `sdis_realisation_Xd.h:86-170`

```c
res_T sample_coupled_path_3d(
    struct sdis_scene* scn,
    struct rwalk_context* ctx,
    struct rwalk* rwalk,
    struct ssp_rng* rng,
    struct temperature* T)
{
    const size_t MAX_FAILS = 1;
    
    // 增加分支计数（Picard迭代层级）
    ctx->nbranchings += 1;
    if (ctx->nbranchings > ctx->max_branchings) {
        return RES_OK;  // 超过最大分支数
    }
    
    // 主循环：直到温度确定
    while (!T->done) {
        // 备份当前状态（用于失败回滚）
        const struct rwalk rwalk_bkp = *rwalk;
        const struct temperature T_bkp = *T;
        size_t nfails = 0;
        
        // 调用当前路径函数（可能改变T->func）
        do {
            res = T->func(scn, ctx, rwalk, rng, T);
            if (res == RES_BAD_OP) {
                // 回滚状态重试
                *rwalk = rwalk_bkp;
                *T = T_bkp;
            }
        } while (res == RES_BAD_OP && ++nfails < MAX_FAILS);
        
        if (res != RES_OK) {
            log_err("reject path (realisation: %lu)", ctx->irealisation);
            goto error;
        }
        
        // 更新第一个顶点的类型（boundary_path执行后）
        if (heat_vtx && !T->done && T->func != boundary_path) {
            if (T->func == conductive_path)
                heat_vtx->type = SDIS_HEAT_VERTEX_CONDUCTION;
            else if (T->func == convective_path)
                heat_vtx->type = SDIS_HEAT_VERTEX_CONVECTION;
            else if (T->func == radiative_path)
                heat_vtx->type = SDIS_HEAT_VERTEX_RADIATIVE;
        }
    }
    
    ctx->nbranchings -= 1;
    return res;
}
```

**关键机制**:
- **函数指针**: `T->func` 动态指向下一步路径函数
- **状态机**: 路径函数执行后可能改变 `T->func`，实现自动转换
- **错误恢复**: `RES_BAD_OP` 时回滚状态重试（最多1次）
- **递归深度**: 通过 `ctx->nbranchings` 控制Picard迭代层级

### 3.3 辐射路径

#### `trace_radiative_path_3d()` - 辐射随机游走
**位置**: `sdis_heat_path_radiative_Xd.h:197-350+`

```c
res_T trace_radiative_path_3d(
    struct sdis_scene* scn,
    const float ray_dir[3],
    struct rwalk_context* ctx,
    struct rwalk* rwalk,
    struct ssp_rng* rng,
    struct temperature* T)
{
    double N[3], dir[3], pos[3];
    size_t nbounces = 0;
    
    d3_normalize(dir, ray_dir);
    
    // 辐射随机游走主循环
    for (;;) {
        struct brdf brdf = BRDF_NULL;
        struct brdf_sample bounce = BRDF_SAMPLE_NULL;
        struct sdis_interface_fragment frag;
        struct sdis_interface* interf = NULL;
        
        d3_set(pos, rwalk->vtx.P);
        
        // 1. 光线追踪找下一个界面交点
        res = find_next_fragment(scn, pos, dir, &rwalk->hit, 
                                 rwalk->vtx.time, rwalk->enc_id,
                                 &interf, &frag);
        
        // 2. 到达辐射环境边界（无穷远）
        if (HIT_NONE(&rwalk->hit)) {
            res = set_limit_radiative_temperature(scn, ctx, rwalk, dir, T);
            ASSERT(T->done);
            break;
        }
        
        // 3. 检查界面有效性（禁止solid/solid）
        res = check_interface(interf, &frag);
        if (res != RES_OK) goto error;
        
        // 4. 记录热路径顶点
        if (ctx->heat_path) {
            register_heat_vertex(ctx->heat_path, &frag.vtx, ...);
        }
        
        // 5. 设置BRDF（根据界面材质）
        setup_interface_fragment(&frag, pos, dir, time, N, &hit);
        res = brdf_setup(&brdf, interf, &frag, &brdf_setup_args);
        
        // 6. BRDF采样出射方向
        res = brdf_sample(&brdf, wi, rng, &bounce);
        if (res == RES_BAD_OP) {
            // 完全吸收，转入边界路径
            T->func = boundary_path_3d;
            setup_fragment(&frag, pos, dir, time, N, &hit);
            rwalk->hit_side = frag.side;
            d3_set(rwalk->vtx.P, frag.P);
            break;
        }
        
        // 7. 俄罗斯轮盘赌终止
        if (bounce.throughput < russian_roulette_threshold) {
            double q = ssp_rng_f64(rng);
            if (q > bounce.throughput) {
                // 终止，转入边界路径
                T->func = boundary_path_3d;
                break;
            }
        }
        
        // 8. 更新光线方向，继续追踪
        d3_set(dir, bounce.wo);
        nbounces++;
    }
    
    return res;
}
```

**物理机制**:
- **几何光学**: 光子沿直线传播到界面
- **BRDF采样**: 反射/透射/散射方向采样
- **俄罗斯轮盘**: 低权重光子概率终止，保持无偏估计
- **吸收处理**: 完全吸收时转入边界温度查询

**关键函数**:
- `find_next_fragment()`: 封装 `s3d_scene_view_trace_ray()`
- `brdf_setup()`: 根据材质初始化BRDF
- `brdf_sample()`: Monte Carlo重要性采样

### 3.4 对流路径

#### `convective_path_3d()` - 流体域对流
**位置**: `sdis_heat_path_convective_Xd.h:174-350+`

```c
res_T convective_path_3d(
    struct sdis_scene* scn,
    struct rwalk_context* ctx,
    struct rwalk* rwalk,
    struct ssp_rng* rng,
    struct temperature* T)
{
    struct fluid_props props_ref;
    struct sdis_medium* mdm = NULL;
    const struct enclosure* enc = NULL;
    
    // 1. 获取流体介质
    enc = scene_get_enclosure(scn, rwalk->enc_id);
    res = scene_get_enclosure_medium(scn, enc, &mdm);
    ASSERT(sdis_medium_get_type(mdm) == SDIS_FLUID);
    
    // 2. 检查流体温度是否已知
    res = handle_known_fluid_temperature(ctx, rwalk, mdm, T);
    if (T->done) return RES_OK;
    
    // 3. 处理路径起点（如果在流体中开始）
    res = handle_convective_path_startup(scn, rwalk, &path_starts_in_fluid);
    
    // 4. 获取流体属性（密度、比热容）
    res = fluid_get_properties(mdm, &rwalk->vtx, &props_ref);
    
    // 5. 检查对流系数上界
    if (enc->hc_upper_bound == 0) {
        // 无对流，直接返回初始条件
        rwalk->vtx.time = props_ref.t0;
        res = handle_known_fluid_temperature(ctx, rwalk, mdm, T);
        return res;
    }
    
    // 6. 主循环：采样对流事件
    for (;;) {
        double hc, mu, dt;
        
        // 6a. 采样对流时间（指数分布）
        mu = enc->hc_upper_bound / (props_ref.rho * props_ref.cp);
        r = ssp_rng_f64(rng);
        dt = -log(1.0 - r) / mu;
        
        // 6b. 回退时间
        rwalk->vtx.time -= dt;
        
        // 6c. 检查是否到达初始条件
        if (rwalk->vtx.time <= props_ref.t0) {
            rwalk->vtx.time = props_ref.t0;
            res = handle_known_fluid_temperature(ctx, rwalk, mdm, T);
            break;
        }
        
        // 6d. 光线追踪找边界（任意方向）
        sample_uniform_direction(rng, dir);
        res = trace_ray(scn->view, rwalk->vtx.P, dir, &rwalk->hit);
        
        // 6e. 获取边界对流系数
        setup_fragment(&frag, rwalk->vtx.P, dir, time, N, &hit);
        hc = get_convection_coefficient(interf, &frag);
        
        // 6f. 接受-拒绝采样
        double p_accept = hc / enc->hc_upper_bound;
        if (ssp_rng_f64(rng) < p_accept) {
            // 接受：转入边界路径
            T->func = boundary_path_3d;
            rwalk->hit_side = frag.side;
            d3_set(rwalk->vtx.P, frag.P);
            break;
        }
        // 拒绝：继续循环
    }
    
    return res;
}
```

**数学原理**:
- **接受-拒绝采样**: 用上界 `hc_upper_bound` 采样，接受概率 `hc(x)/hc_upper_bound`
- **指数分布**: 时间间隔 `dt ~ Exp(μ)`，其中 `μ = hc/(ρ·cp)`
- **时间回退**: 从观测时间向初始时间回溯

### 3.5 导热路径

#### `conductive_path_3d()` - 固体域导热
**位置**: `sdis_heat_path_conductive_Xd.h` (包含多种算法)

**支持的算法**:
1. **Delta-Sphere** (`SDIS_DIFFUSION_DELTA_SPHERE`): 近边界使用δ-球体
2. **Walk-on-Spheres** (`SDIS_DIFFUSION_WOS`): 标准球内游走
3. **Custom**: 自定义扩散算法

**示例: Delta-Sphere算法**
```c
res_T conductive_path_delta_sphere_3d(
    struct sdis_scene* scn,
    struct rwalk_context* ctx,
    struct rwalk* rwalk,
    struct ssp_rng* rng,
    struct temperature* T)
{
    struct solid_props props;
    struct sdis_medium* mdm = NULL;
    double delta;  // δ参数（接近边界的阈值）
    
    // 1. 获取固体介质
    enc = scene_get_enclosure(scn, rwalk->enc_id);
    res = scene_get_enclosure_medium(scn, enc, &mdm);
    ASSERT(sdis_medium_get_type(mdm) == SDIS_SOLID);
    
    // 2. 检查温度是否已知
    res = handle_known_solid_temperature(ctx, rwalk, mdm, T);
    if (T->done) return RES_OK;
    
    // 3. 获取固体属性（热扩散率）
    res = solid_get_properties(mdm, &rwalk->vtx, &props);
    double kappa = props.k / (props.rho * props.cp);  // 热扩散率
    
    // 4. 主循环：Walk-on-Spheres + Delta-Sphere
    for (;;) {
        double r_nearest;  // 到最近边界的距离
        
        // 4a. 找最近边界
        res = find_nearest_boundary(scn, rwalk->vtx.P, &r_nearest, &hit);
        
        // 4b. 判断是否进入δ层
        if (r_nearest < delta * sqrt(kappa * dt_remaining)) {
            // 进入δ层，采样到达边界
            sample_boundary_hit(rng, rwalk->vtx.P, delta, &hit_pos);
            d3_set(rwalk->vtx.P, hit_pos);
            
            // 转入边界路径
            T->func = boundary_path_3d;
            setup_fragment(&frag, ...);
            rwalk->hit_side = frag.side;
            break;
        }
        
        // 4c. Walk-on-Spheres步骤
        // 采样球面上的点
        double r_sphere = r_nearest;  // 最大内切球半径
        sample_uniform_sphere_surface(rng, r_sphere, direction);
        d3_add(rwalk->vtx.P, rwalk->vtx.P, direction);
        
        // 更新时间
        double dt = r_sphere * r_sphere / (6 * kappa);
        rwalk->vtx.time -= dt;
        dt_remaining -= dt;
        
        // 4d. 检查初始条件
        if (rwalk->vtx.time <= props.t0) {
            rwalk->vtx.time = props.t0;
            res = handle_known_solid_temperature(ctx, rwalk, mdm, T);
            break;
        }
    }
    
    return res;
}
```

**算法特点**:
- **无网格**: 不需要空间离散化
- **高效**: 大步长跳跃（球半径）
- **精确**: 满足扩散方程的精确解

---

## 4. 数据结构

### 4.1 核心结构体

#### `struct rwalk` - 随机游走状态
```c
struct rwalk {
    struct sdis_rwalk_vertex vtx;    // 当前顶点（位置+时间）
    union {
        struct s2d_hit hit_2d;        // 2D碰撞信息
        struct s3d_hit hit_3d;        // 3D碰撞信息
    };
    enum sdis_side hit_side;          // 碰撞面朝向（FRONT/BACK）
    unsigned enc_id;                  // 当前包壳ID
    double dir[3];                    // 当前方向
    double elapsed_time;              // 已用时间
};
```

#### `struct temperature` - 温度累积器
```c
struct temperature {
    double value;                     // 累积温度值
    int done;                         // 是否完成（到达已知边界）
    res_T (*func)(                    // 下一步路径函数（函数指针）
        struct sdis_scene* scn,
        struct rwalk_context* ctx,
        struct rwalk* rwalk,
        struct ssp_rng* rng,
        struct temperature* T);
};
```

**状态转换表**:
| 当前 `func` | 转换条件 | 下一个 `func` |
|------------|---------|--------------|
| `radiative_path` | 完全吸收 | `boundary_path` |
| `radiative_path` | 俄罗斯轮盘终止 | `boundary_path` |
| `convective_path` | 到达边界 | `boundary_path` |
| `convective_path` | 到达初始条件 | `done=1` |
| `conductive_path` | 进入δ层 | `boundary_path` |
| `boundary_path` | 边界温度已知 | `done=1` |
| `boundary_path` | 固/流边界 | `radiative_path` 或 `convective_path` |

#### `struct rwalk_context` - 全局上下文
```c
struct rwalk_context {
    struct sdis_heat_path* heat_path;     // 热路径记录
    struct green_path_handle* green_path; // Green函数路径
    double Tmin, Tmin2, Tmin3;            // 最低温度及其幂次
    double That, That2, That3;            // 最高温度及其幂次
    size_t max_branchings;                // 最大分支数（Picard阶-1）
    size_t nbranchings;                   // 当前分支数
    size_t irealisation;                  // 当前实现编号
    enum sdis_diffusion_algo diff_algo;   // 扩散算法选择
};
```

### 4.2 输入参数

#### `struct ray_realisation_args` - 辐射实现参数
```c
struct ray_realisation_args {
    double position[3];               // 起点位置
    double direction[3];              // 初始方向
    double time;                      // 观测时间
    unsigned enc_id;                  // 起点包壳ID
    int picard_order;                 // Picard迭代阶数
    enum sdis_diffusion_algo diff_algo; // 扩散算法
    struct ssp_rng* rng;              // 随机数生成器
    struct sdis_heat_path* heat_path; // 输出热路径
    size_t irealisation;              // 实现编号
};
```

#### `struct probe_realisation_args` - 探针实现参数
```c
struct probe_realisation_args {
    double position[DIM];             // 探针位置
    double time;                      // 观测时间
    unsigned enc_id;                  // 包壳ID
    int picard_order;                 // Picard阶数
    enum sdis_diffusion_algo diff_algo;
    struct ssp_rng* rng;
    struct green_path_handle* green_path;
    struct sdis_heat_path* heat_path;
    size_t irealisation;
};
```

### 4.3 输出数据

#### `struct accum` - 统计累积器
```c
struct accum {
    double sum;      // Σw_i （权重和）
    double sum2;     // Σ(w_i)^2 （权重平方和）
    size_t count;    // 成功实现数
};
```

**统计估计**:
- **均值**: `mean = sum / count`
- **方差**: `var = (sum2 / count) - mean^2`
- **标准差**: `std = sqrt(var)`
- **置信区间**: `mean ± z * std / sqrt(count)`

#### `struct sdis_heat_path` - 热路径记录
```c
struct sdis_heat_path {
    darray_heat_vertex vertices;      // 顶点数组
    darray_size_t breaks;             // 线段分割点
    enum sdis_heat_path_flag status;  // 路径状态（SUCCESS/FAILURE）
};

struct sdis_heat_vertex {
    double P[3];                      // 位置
    double time;                      // 时间
    double weight;                    // 权重（温度贡献）
    enum sdis_heat_vertex_type type;  // 类型（RADIATIVE/CONDUCTIVE/CONVECTION）
    int branch_id;                    // 分支ID（Picard层级）
};
```

---

## 5. GPU移植要点

### 5.1 并行化策略

#### 层级1: 实现级并行（已有OpenMP）
```c
// CPU: OpenMP线程并行
#pragma omp parallel for schedule(static)
for (irealisation = 0; irealisation < nrealisations; ++irealisation) {
    res = probe_realisation_3d(scn, &args[irealisation], &weights[irealisation]);
}

// GPU等效: CUDA Kernel
__global__ void probe_realisation_kernel(
    sdis_scene_gpu* scn,
    probe_realisation_args_gpu* args,
    double* weights,
    int nrealisations)
{
    int irealisation = blockIdx.x * blockDim.x + threadIdx.x;
    if (irealisation >= nrealisations) return;
    
    // 每个线程独立RNG种子
    curandState rng;
    curand_init(seed, irealisation, 0, &rng);
    
    // 独立执行随机游走
    weights[irealisation] = probe_realisation_gpu(scn, &args[irealisation], &rng);
}
```

**启动配置建议**:
- Block size: 256-512线程
- Grid size: `(nrealisations + blockSize - 1) / blockSize`
- 总线程数: 数百万（适应RTX 4090的16384 CUDA核心）

#### 层级2: 像素级并行（Camera rendering）
```c
// CPU: 嵌套OpenMP（外层像素，内层采样）
#pragma omp parallel for collapse(2)
for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
        for (int s = 0; s < spp; ++s) {
            radiance[y][x] += trace_ray(x, y, s);
        }
    }
}

// GPU等效: 2D网格
__global__ void camera_render_kernel(
    sdis_scene_gpu* scn,
    float* radiance,
    int width, int height, int spp)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    
    curandState rng;
    curand_init(seed, y * width + x, 0, &rng);
    
    float pixel_radiance = 0.0f;
    for (int s = 0; s < spp; ++s) {
        pixel_radiance += trace_ray_gpu(scn, x, y, &rng);
    }
    radiance[y * width + x] = pixel_radiance / spp;
}
```

**启动配置**:
- Block: `dim3(16, 16)` (256线程)
- Grid: `dim3((width+15)/16, (height+15)/16)`

### 5.2 内存管理

#### 场景数据（只读，共享）
```c
// 常量内存（<64KB）
__constant__ sdis_scene_constants scn_const;

// 纹理内存（几何数据，缓存友好）
texture<float4, 1, cudaReadModeElementType> tex_vertices;
texture<int4, 1, cudaReadModeElementType> tex_indices;

// 全局内存（大型数据，BVH树）
struct bvh_node_gpu* d_bvh_nodes;
struct primitive_gpu* d_primitives;
```

#### 线程局部数据（寄存器/共享内存）
```c
__device__ double probe_realisation_gpu(...)
{
    // 栈上分配（寄存器溢出到Local Memory）
    struct rwalk rwalk;
    struct temperature T;
    struct rwalk_context ctx;
    
    // 共享内存（Block内共享，用于规约）
    __shared__ double block_sum[256];
    __shared__ double block_sum2[256];
}
```

#### 热路径输出（可选，内存密集）
```c
// 方案1: 预分配固定大小缓冲区
__device__ struct sdis_heat_vertex heat_vertices[MAX_VERTICES_PER_PATH];
int vertex_count = 0;

// 方案2: 原子计数器+全局缓冲区
__device__ int global_vertex_offset = atomicAdd(&d_vertex_counter, estimated_count);
d_heat_vertices[global_vertex_offset + i] = vertex;

// 方案3: 禁用热路径输出（仅计算温度）
args->heat_path = NULL;  // 大幅降低内存需求
```

### 5.3 光线追踪集成

#### 使用OptiX 7/8（推荐）
```c
// Host: 构建BVH加速结构
OptixTraversableHandle gas_handle;
optixAccelBuild(context, stream, &accel_options, 
                d_vertices, d_indices, &gas_handle);

// Device: 光线追踪查询
__device__ void find_next_fragment_gpu(...)
{
    // 设置光线参数
    float3 origin = make_float3(pos[0], pos[1], pos[2]);
    float3 direction = make_float3(dir[0], dir[1], dir[2]);
    float tmin = 0.0f;
    float tmax = FLT_MAX;
    
    // OptiX追踪
    unsigned int hit_prim_id = UINT_MAX;
    float hit_t = tmax;
    optixTrace(gas_handle, origin, direction, tmin, tmax, 0.0f,
               OptixVisibilityMask(1), OPTIX_RAY_FLAG_NONE,
               0, 1, 0,  // SBT偏移
               hit_prim_id, hit_t);
    
    if (hit_prim_id != UINT_MAX) {
        // 构造hit结构
        hit->distance = hit_t;
        hit->prim_id = hit_prim_id;
        // 计算法线、UV等
    }
}
```

#### 使用自定义BVH（备选）
```c
__device__ bool intersect_bvh_gpu(
    const struct bvh_node_gpu* nodes,
    const struct primitive_gpu* prims,
    const float3 ray_org,
    const float3 ray_dir,
    struct s3d_hit* hit)
{
    int stack[64];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;  // 根节点
    
    float closest_t = FLT_MAX;
    int closest_prim = -1;
    
    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const struct bvh_node_gpu* node = &nodes[node_idx];
        
        // AABB相交测试
        if (!intersect_aabb(ray_org, ray_dir, node->aabb_min, node->aabb_max))
            continue;
        
        if (node->is_leaf) {
            // 叶节点：测试所有图元
            for (int i = node->prim_start; i < node->prim_end; ++i) {
                float t = intersect_primitive(ray_org, ray_dir, &prims[i]);
                if (t > 0 && t < closest_t) {
                    closest_t = t;
                    closest_prim = i;
                }
            }
        } else {
            // 内部节点：压栈子节点
            stack[stack_ptr++] = node->left_child;
            stack[stack_ptr++] = node->right_child;
        }
    }
    
    if (closest_prim >= 0) {
        hit->distance = closest_t;
        hit->prim_id = closest_prim;
        return true;
    }
    return false;
}
```

### 5.4 随机数生成

#### cuRAND集成
```c
// 初始化（每个线程独立状态）
__global__ void init_rng_kernel(curandState* rng_states, unsigned long seed)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    curand_init(seed, idx, 0, &rng_states[idx]);
}

// 使用（替代ssp_rng_f64）
__device__ double sample_uniform(curandState* rng)
{
    return curand_uniform_double(rng);
}

__device__ float3 sample_uniform_sphere(curandState* rng)
{
    float z = 2.0f * curand_uniform(rng) - 1.0f;
    float r = sqrtf(1.0f - z * z);
    float phi = 2.0f * M_PI * curand_uniform(rng);
    return make_float3(r * cosf(phi), r * sinf(phi), z);
}
```

#### 自定义RNG（低质量但快速）
```c
// PCG Hash（无状态，基于线程ID和迭代数）
__device__ uint32_t pcg_hash(uint32_t seed)
{
    uint32_t state = seed * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ float sample_uniform_fast(uint32_t* seed)
{
    *seed = pcg_hash(*seed);
    return (*seed) / 4294967296.0f;
}
```

### 5.5 性能优化建议

#### 1. 减少分支发散
```c
// 不佳: 大量if-else
if (medium_type == SDIS_FLUID) {
    convective_path(...);
} else if (medium_type == SDIS_SOLID) {
    conductive_path(...);
} else {
    radiative_path(...);
}

// 优化: 预分类，批量处理
// Kernel 1: 仅处理流体路径
__global__ void process_fluid_paths(...);
// Kernel 2: 仅处理固体路径
__global__ void process_solid_paths(...);
```

#### 2. 合并内存访问
```c
// 不佳: 跨步访问
for (int i = threadIdx.x; i < n; i += blockDim.x) {
    result += data[i * stride];  // stride != 1
}

// 优化: 连续访问
int idx = blockIdx.x * blockDim.x + threadIdx.x;
if (idx < n) {
    result = data[idx];  // 合并访问
}
```

#### 3. 占用率优化
```bash
# 检查寄存器使用
nvcc --ptxas-options=-v kernel.cu
# 输出: ptxas info : Used 63 registers, 384 bytes smem

# 限制寄存器数量（提高占用率）
__global__ __launch_bounds__(256, 4)  // 每SM最少4个Block
void kernel(...) { ... }
```

#### 4. 使用Shared Memory缓存
```c
__global__ void trace_many_rays(...)
{
    // 缓存BVH节点到Shared Memory
    __shared__ struct bvh_node_gpu cached_nodes[256];
    
    // 协作加载
    if (threadIdx.x < 256) {
        cached_nodes[threadIdx.x] = d_bvh_nodes[blockIdx.x * 256 + threadIdx.x];
    }
    __syncthreads();
    
    // 使用缓存
    intersect_bvh_shared(cached_nodes, ...);
}
```

#### 5. 原子操作规约
```c
// 累积统计量（每个Block内规约）
__shared__ double block_sum[256];
block_sum[threadIdx.x] = local_weight;
__syncthreads();

// Warp级规约
for (int offset = 32; offset > 0; offset /= 2) {
    block_sum[threadIdx.x] += __shfl_down_sync(0xffffffff, block_sum[threadIdx.x], offset);
}

// Block级规约
if (threadIdx.x == 0) {
    atomicAdd(&d_global_sum, block_sum[0]);
    atomicAdd(&d_global_sum2, block_sum[0] * block_sum[0]);
}
```

### 5.6 DirectX 12 着色器移植

#### Compute Shader结构
```hlsl
// RandomWalk.hlsl
RWStructuredBuffer<RealisationArgs> g_Args : register(u0);
RWStructuredBuffer<double> g_Weights : register(u1);
StructuredBuffer<SceneData> g_Scene : register(t0);
RaytracingAccelerationStructure g_BVH : register(t1);

[numthreads(256, 1, 1)]
void ProbeRealisationCS(uint3 DTid : SV_DispatchThreadID)
{
    uint irealisation = DTid.x;
    if (irealisation >= g_NumRealisations) return;
    
    // 初始化随机数状态
    RngState rng = InitRng(g_Seed, irealisation);
    
    // 执行随机游走
    double weight = ProbeRealisation(g_Scene, g_Args[irealisation], rng);
    g_Weights[irealisation] = weight;
}

// 辅助函数
double ProbeRealisation(SceneData scene, RealisationArgs args, inout RngState rng)
{
    RWalk rwalk = InitRWalk(args.position, args.time, args.enc_id);
    Temperature T = (Temperature)0;
    T.func = FUNC_CONVECTIVE_PATH;  // 枚举，模拟函数指针
    
    // 主循环
    while (!T.done) {
        switch (T.func) {
            case FUNC_RADIATIVE_PATH:
                RadiativePath(scene, rwalk, rng, T);
                break;
            case FUNC_CONVECTIVE_PATH:
                ConvectivePath(scene, rwalk, rng, T);
                break;
            case FUNC_CONDUCTIVE_PATH:
                ConductivePath(scene, rwalk, rng, T);
                break;
            case FUNC_BOUNDARY_PATH:
                BoundaryPath(scene, rwalk, rng, T);
                break;
        }
    }
    
    return T.value;
}

// 光线追踪（DXR）
void TraceRadiativeRay(float3 origin, float3 direction, out HitInfo hit)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 1e30;
    
    HitPayload payload = (HitPayload)0;
    TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    
    hit.distance = payload.t;
    hit.prim_id = payload.prim_id;
    hit.normal = payload.normal;
}
```

#### C++ API调用
```cpp
// 分发Compute Shader
void StardisGPU::SolveProbe(const SolveProbeArgs& args)
{
    // 1. 上传参数到GPU
    m_argsBuffer->Upload(args.data(), args.size());
    
    // 2. 绑定资源
    m_commandList->SetComputeRootSignature(m_rootSignature.Get());
    m_commandList->SetComputeRootDescriptorTable(0, m_srvHeap->GetGPUHandle(0));
    m_commandList->SetComputeRootDescriptorTable(1, m_uavHeap->GetGPUHandle(0));
    
    // 3. 分发
    uint32_t numGroups = (args.nrealisations + 255) / 256;
    m_commandList->Dispatch(numGroups, 1, 1);
    
    // 4. UAV屏障
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_weightsBuffer.Get());
    m_commandList->ResourceBarrier(1, &barrier);
    
    // 5. 下载结果
    m_weightsBuffer->Download(results.data(), results.size());
}
```

### 5.7 验证与调试

#### 单像素对比
```cpp
// CPU参考
double cpu_weight = probe_realisation_3d(scene_cpu, args, rng_cpu);

// GPU结果
double gpu_weight = probe_realisation_gpu(scene_gpu, args, rng_gpu);

// 验证（容差1e-6）
double error = fabs(cpu_weight - gpu_weight);
if (error > 1e-6) {
    printf("ERROR: CPU=%.10f, GPU=%.10f, diff=%.2e\n", 
           cpu_weight, gpu_weight, error);
}
```

#### 统计分布对比
```cpp
// CPU: 10000次实现
std::vector<double> cpu_weights(10000);
for (int i = 0; i < 10000; ++i) {
    cpu_weights[i] = probe_realisation_3d(...);
}
double cpu_mean = Mean(cpu_weights);
double cpu_std = StdDev(cpu_weights);

// GPU: 10000次实现
std::vector<double> gpu_weights(10000);
SolveProbeGPU(10000, gpu_weights.data());
double gpu_mean = Mean(gpu_weights);
double gpu_std = StdDev(gpu_weights);

// Kolmogorov-Smirnov检验（分布一致性）
double ks_stat = KSTest(cpu_weights, gpu_weights);
assert(ks_stat < 0.05);  // 95%置信度
```

---

## 附录

### A. 关键文件清单

| 文件 | 功能 | 代码行数 |
|------|------|---------|
| `sdis_realisation.c` | 辐射实现入口 | 110 |
| `sdis_realisation_Xd.h` | 探针/边界实现 | 484 |
| `sdis_solve_probe_Xd.h` | 探针求解器 | 707 |
| `sdis_heat_path_radiative_Xd.h` | 辐射路径 | 350+ |
| `sdis_heat_path_convective_Xd.h` | 对流路径 | 350+ |
| `sdis_heat_path_conductive_Xd.h` | 导热路径 | 300+ |
| `sdis_heat_path_boundary_Xd.h` | 边界路径 | 400+ |
| `sdis_Xd_begin.h` | 维度通用宏 | 80 |

### B. 术语对照表

| 英文 | 中文 | 说明 |
|------|------|------|
| Realisation | 实现/采样 | 一次完整的Monte Carlo路径采样 |
| Random Walk | 随机游走 | 粒子的随机路径轨迹 |
| BRDF | 双向反射分布函数 | 描述表面反射特性 |
| Picard Iteration | Picard迭代 | 求解非线性耦合方程的迭代方法 |
| Russian Roulette | 俄罗斯轮盘赌 | 无偏的路径终止策略 |
| Walk-on-Spheres | 球内游走 | 求解扩散方程的Monte Carlo方法 |
| Delta-Sphere | δ-球体 | 近边界精确处理方法 |
| Green Function | Green函数 | 线性算子的基本解 |
| Enclosure | 包壳 | 几何上封闭的体积区域 |

### C. 参考文献

1. **Monte Carlo方法**:
   - *Veach, E. (1997). Robust Monte Carlo Methods for Light Transport Simulation.*
   - *Pharr, M., Jakob, W., & Humphreys, G. (2016). Physically Based Rendering (3rd ed.).*

2. **随机游走**:
   - *Sawhney, R., & Crane, K. (2020). Monte Carlo Geometry Processing. SIGGRAPH Course.*
   - *Müller, T., et al. (2017). Practical Path Guiding for Efficient Light-Transport Simulation.*

3. **热传输模拟**:
   - *Howell, J. R., Mengüç, M. P., & Siegel, R. (2015). Thermal Radiation Heat Transfer (6th ed.).*
   - *Modest, M. F. (2013). Radiative Heat Transfer (3rd ed.).*

---

**文档生成完毕** | 总字数: ~15000 | 更新时间: 2026-01-21
