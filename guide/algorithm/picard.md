# Picard迭代算法在STARDIS中的实现与光追耦合

**生成时间**: 2026-01-21 22:03  
**分析范围**: stardis-cpu/stardis-solver/0.16.2  
**目的**: 为GPU迁移提供Picard算法的完整技术文档  

---

## 执行摘要

**Picard是什么**: 迭代数值方法，用于求解**非线性辐射传输**问题（radiative transfer）。在热辐射中，温度-辐射关系是 **T^4** 非线性的（Stefan-Boltzmann定律），Picard算法通过迭代逼近精确解。

**在项目中的形态**: 
- **配置参数**: `picard_order` (无量纲整数，≥1)
  - `picard_order = 1`: 线性化辐射传输（使用固定参考温度Tref）
  - `picard_order > 1`: 非线性辐射传输（递归采样，逐步收敛到T^4准确解）
  
- **实现机制**: 通过**递归热路径分支**（heat path branching）实现
  - `max_branchings = picard_order - 1` （代码中的核心关系）
  - 每个分支采样一条新的辐射路径，使用前次迭代的温度作为参考

**与光追的耦合**: 
- **紧密耦合**: Picard迭代**嵌入**在Monte Carlo光线追踪循环中
- **数据流**: 固体边界 → 采样辐射路径 → 更新参考温度 → 递归采样（如果picard_order>1）
- **实现位置**: `sdis_heat_path_boundary_Xd_solid_fluid_picard<1|N>.h` （区分picard1和picardN实现）

**与GPU迁移的关系**:
- ⚠️ **递归深度可变**: `picard_order`可运行时配置（典型值1-3），GPU需要处理动态递归
- ⚠️ **性能关键**: 每个分支都采样完整的光线路径，`picard_order=3` 可能导致路径数指数增长
- ⚠️ **引用温度场**: 需要维护时空变化的参考温度场（Tref），GPU需要高效访问
- ✅ **并行友好**: 不同Monte Carlo实现（realisation）之间独立，可并行GPU线程
- ⚠️ **限制条件**: picardN模式（order>1）不支持imposed flux和external source（代码中显式禁止）

---

## 1. Picard算法的物理意义

### 1.1 为什么需要Picard迭代

在热辐射传输中，辐射功率密度遵循Stefan-Boltzmann定律：

```
q_rad = ε·σ·T^4
```

其中 `ε` 是发射率，`σ` 是Stefan-Boltzmann常数。**T^4的非线性**导致标准Monte Carlo方法需要特殊处理。

### 1.2 线性化vs非线性

**Picard Order = 1 (线性化)**:
```
q_rad ≈ ε·σ·(4·Tref^3·T - 3·Tref^4)
```
使用固定参考温度 `Tref` 将T^4线性化，简化计算但精度受限。

**Picard Order > 1 (非线性)**:
通过迭代逼近准确的T^4关系：
```
第1次迭代: 使用Tref采样路径，得到T1
第2次迭代: 使用T1作为新Tref，再次采样，得到T2
...
收敛到准确解（理论上）
```

### 1.3 官方文档引用

来自README.md (Version 0.13):

> Uses a new [iterative numerical method](https://hal.archives-ouvertes.fr/tel-03266863/) 
> to estimate radiative transfer. With a recursion level of 1, this is equivalent to a 
> linearization of the radiative transfer but with a reference temperature that can vary 
> in time and space. By using a higher-order recursion, one can converge towards a 
> rigorous estimate that takes into account the non-linearity of the radiative transfer; 
> the higher the recursion order, the better the convergence, but with the counterpart 
> of an increase in calculation time.

---

## 2. Picard在代码中的形态

### 2.1 数据结构

#### 核心参数: `picard_order`

定义位置（多处）:
```c
// sdis_realisation.h:66
struct probe_realisation_args {
    size_t picard_order; /* Picard order to estimate radiative temperature */
    // ... 其他字段
};

// sdis.h:630, 686, 749, 787, 824, 854, 882 (多个solve函数参数结构)
struct sdis_solve_probe_args {
    size_t picard_order;
    // ...
};
```

#### 内部转换: `max_branchings`

从 `picard_order` 派生的内部参数：

```c
// sdis_heat_path.h:50-51
/* Maximum branchings i.e. the maximum number of times XD(sample_coupled_path)
 * can be called. It controls the number of ramifications of the heat path and
 * currently is correlated to the Picard order used to estimate the radiative
 * temperature. max_branchings == picard_order-1 */
size_t max_branchings;

// sdis_heat_path.h:80-84
static INLINE size_t
get_picard_order(const struct rwalk_context* ctx)
{
    ASSERT(ctx);
    return ctx->max_branchings + 1;
}
```

**关键关系**: `max_branchings = picard_order - 1`

- `picard_order = 1` → `max_branchings = 0` → 不递归，只采样一条路径
- `picard_order = 2` → `max_branchings = 1` → 每个边界可分支一次
- `picard_order = 3` → `max_branchings = 2` → 可递归两层

#### 随机游走上下文: `rwalk_context`

```c
// sdis_heat_path.h:34-62
struct rwalk_context {
    struct green_path_handle* green_path;
    struct sdis_heat_path* heat_path;
    
    double Tmin;   /* Lower bound temperature */
    double Tmin2;  /* Tmin^2 */
    double Tmin3;  /* Tmin^3 */
    
    double That;   /* Upper bound temperature */
    double That2;  /* That^2 */
    double That3;  /* That^3 */
    
    size_t max_branchings;  // ← 从picard_order派生
    size_t nbranchings;     // ← 当前已分支次数
    
    size_t irealisation;
    enum sdis_diffusion_algorithm diff_algo;
};
```

**状态追踪**: `nbranchings` 从0开始，每次递归采样+1，当 `nbranchings == max_branchings` 时停止递归。

### 2.2 实现文件结构

Picard实现被拆分为两个专门的头文件：

```
sdis_heat_path_boundary_Xd_solid_fluid_picard1.h  // picard_order = 1
sdis_heat_path_boundary_Xd_solid_fluid_picardN.h  // picard_order > 1
```

**为什么分开**: 
- `picard1`: 性能优化，单次采样，支持imposed flux和external source
- `picardN`: 递归实现，不支持imposed flux和external source（代码显式检查并拒绝）

#### 代码引用点

```c
// sdis_heat_path_boundary.c:27-33
#include "sdis_heat_path_boundary_Xd_solid_fluid_picard1.h"  // 2D
#include "sdis_heat_path_boundary_Xd_solid_fluid_picard1.h"  // 3D
#include "sdis_heat_path_boundary_Xd_solid_fluid_picardN.h"  // 2D
#include "sdis_heat_path_boundary_Xd_solid_fluid_picardN.h"  // 3D
```

### 2.3 初始化流程

`picard_order` 参数如何传播到求解器核心：

```c
// sdis_realisation.c:29, 83
res_T sdis_realisation(..., const struct realisation_args* args, ...) {
    if(args->picard_order <= 0) return RES_BAD_ARG;  // 验证
    
    // ...
    ctx.max_branchings = args->picard_order - 1;  // ← 转换关系
    // ...
}
```

调用链（简化）：
```
应用层: stardis-app.c:271
    stardis->picard_order = args->picard_order;

计算层: stardis-compute.c:412, 572, 711, ...
    args.picard_order = stardis->picard_order;

求解器层: sdis_solve_*.c
    realis_args.picard_order = args->picard_order;

实现层: sdis_realisation.c:83
    ctx.max_branchings = args->picard_order - 1;

热路径: sdis_heat_path_boundary_Xd.h:78-81
    if(ctx->nbranchings == ctx->max_branchings) { 停止递归 }
```

---

## 3. Picard与光线追踪的耦合

### 3.1 耦合架构图

```
Monte Carlo主循环 (sdis_solve_camera, sdis_solve_probe, ...)
    ↓
    [每个realisation独立并行]
    ↓
    probe_realisation_Xd() / boundary_realisation_Xd()
        ↓
        sample_coupled_path()  ← 核心采样函数
            ↓
            遇到固体-流体边界 →
                ↓
                [判断picard_order]
                    ↓                         ↓
        picard_order = 1           picard_order > 1
                    ↓                         ↓
    solid_fluid_boundary_picard1_path    solid_fluid_boundary_picardN_path
                    ↓                         ↓
        [采样辐射路径]               [递归采样辐射路径]
                    ↓                         ↓
            获取Tref                  第1层: 获取Tref
                    ↓                         ↓
        采样radiative path        sample_coupled_path() → 递归
                    ↓                   (nbranchings++)
        计算T_radiative                       ↓
                    ↓                  第2层: 使用上层T作为新Tref
        返回加权温度                           ↓
                                      ... 直到nbranchings == max_branchings
                                              ↓
                                      返回加权温度（所有分支贡献累加）
```

### 3.2 代码级耦合点

#### 耦合点1: 边界类型判断与分发

```c
// sdis_heat_path_boundary_Xd.h:78-82
if(get_picard_order(ctx) == 1) {
    res = XD(solid_fluid_boundary_picard1_path)(scn, ctx, &frag, rwalk, rng, T);
} else if(ctx->nbranchings == ctx->max_branchings) {
    // picardN但已达到最大分支，退化为picard1
    res = XD(solid_fluid_boundary_picard1_path)(scn, ctx, &frag, rwalk, rng, T);
} else {
    ASSERT(ctx->nbranchings < ctx->max_branchings);
    res = XD(solid_fluid_boundary_picardN_path)(scn, ctx, &frag, rwalk, rng, T);
}
```

**关键逻辑**: 
- 即使是 `picard_order > 1`，当递归到最后一层时，也会调用 `picard1_path`（因为不再需要分支）
- 这是性能优化，避免最后一层不必要的递归检查

#### 耦合点2: picard1的参考温度获取

```c
// sdis_heat_path_boundary_Xd_solid_fluid_picard1.h:32-79
static INLINE res_T XD(rwalk_get_Tref)(...) {
    double Tref = SDIS_TEMPERATURE_NONE;
    
    if(T->done) {
        // 路径到达无穷远（radiative environment）
        struct sdis_radiative_ray ray = ...;
        ray.time = rwalk->vtx.time;
        Tref = radiative_env_get_reference_temperature(scn->radenv, &ray);
    } else {
        // 路径到达流体边界
        struct sdis_interface* interf = scene_get_interface(scn, rwalk->hit.prim_id);
        Tref = interface_side_get_reference_temperature(interf, &frag);
    }
    
    return res;
}
```

**数据依赖**: 
- `Tref` 可能来自：
  1. **边界条件**: 流体边界的参考温度（可时空变化）
  2. **辐射环境**: 无穷远处的环境温度
- GPU需要高效访问这两种温度源（可能需要纹理缓存或专用buffer）

#### 耦合点3: picardN的递归采样

```c
// sdis_heat_path_boundary_Xd_solid_fluid_picardN.h:67-121
static INLINE res_T XD(sample_path)(...) {
    struct rwalk rwalk = RWALK_NULL;
    
    // 初始化随机游走
    rwalk.vtx = rwalk_from->vtx;
    rwalk.enc_id = rwalk_from->enc_id;
    rwalk.hit = rwalk_from->hit;
    rwalk.hit_side = rwalk_from->hit_side;
    
    // 启动新的热路径分支记录
    if(ctx->heat_path) {
        struct sdis_heat_vertex heat_vtx = ...;
        heat_vtx.branch_id = (int)ctx->nbranchings + 1;  // ← 分支深度标记
        res = heat_path_restart(ctx->heat_path, &heat_vtx);
    }
    
    // 递归采样耦合路径（包括传导、对流、辐射）
    res = XD(sample_coupled_path)(scn, ctx, &rwalk, rng, T);
    
    // 检查返回的温度是否在合理范围
    ASSERT(T->done);
    if(T->value < scn->tmin || scn->tmax < T->value) {
        // 错误处理
    }
    
    return res;
}
```

**递归机制**: 
- `sample_coupled_path()` 会再次遇到边界 → 再次判断 `nbranchings < max_branchings` → 继续递归
- 每次递归前 `nbranchings++`（在picardN_path中实现）
- **终止条件**: `nbranchings == max_branchings` 时退化为picard1

#### 耦合点4: picardN的约束检查

```c
// sdis_heat_path_boundary_Xd_solid_fluid_picardN.h:34-59
static INLINE res_T check_net_flux(..., const size_t picard_order) {
    double phi;
    ASSERT(scn && interf && frag && picard_order > 1);
    
    phi = interface_side_get_flux(interf, frag);
    if(phi != SDIS_FLUX_NONE && phi != 0) {
        log_err(scn->dev,
          "%s: invalid flux '%g' W/m^2. Could not manage a flux != 0 when the "
          "picard order is not equal to 1; Picard order is currently set to %lu.\n",
          FUNC_NAME, phi, (unsigned long)picard_order);
        res = RES_BAD_ARG;
        goto error;
    }
    // ...
}
```

**约束原因**: 
- imposed flux和external source在线性假设下才可准确计算
- picardN的非线性迭代无法保证这些项的收敛性（代码注释: "does not make sense when dealing with non-linearities"）

类似检查在多处出现:
- `sdis_solve_boundary_Xd.h:234-238` (Green function)
- `sdis_heat_path_conductive_delta_sphere_Xd.h:241-246` (体积功率)
- `sdis_heat_path_boundary_Xd_handle_external_net_flux.h:70-74` (外部源)

### 3.3 数据流图

```
输入参数:
    picard_order (用户配置, 典型值1-3)
        ↓
转换:
    max_branchings = picard_order - 1
        ↓
初始化:
    ctx.max_branchings = max_branchings
    ctx.nbranchings = 0
        ↓
Monte Carlo循环 (N个realisation并行):
    ↓
    对每个realisation:
        ↓
        sample_coupled_path(ctx, rwalk, rng)
            ↓
            [传导采样] → 遇到边界 →
                ↓
                判断: get_picard_order(ctx) == 1?
                    ↓ YES                    ↓ NO
            [Picard1路径]            [PicardN路径]
                    ↓                        ↓
            获取Tref(时空)          ctx.nbranchings++
                    ↓                        ↓
            采样辐射路径            判断: nbranchings < max_branchings?
                    ↓                    ↓ YES        ↓ NO
            返回T_radiative     递归调用      退化为Picard1
                                sample_coupled_path()
                                        ↓
                                [嵌套采样]
                                    ↓
                                获取新Tref(前次T)
                                    ↓
                                再次采样辐射路径
                                    ↓
                                累加所有分支的T
                                    ↓
                                返回加权平均T
        ↓
    累加所有realisation的结果
        ↓
输出:
    估计的温度 T ± 标准差
```

---

## 4. 与GPU迁移的关系

### 4.1 GPU友好的特性

#### ✅ 并行独立性
- **Monte Carlo realisations之间完全独立**
- 每个GPU线程可处理一个realisation，无需同步
- 对应CPU代码的OpenMP并行区

```c
// sdis_solve_camera.c:656 (简化)
#pragma omp parallel for
for(size_t i = 0; i < args->spp; i++) {  // spp = samples per pixel
    realisation_args.picard_order = picard_order;
    res = pixel_realisation(..., &realisation_args, ...);
}
```

**GPU映射**: 
```cuda
__global__ void camera_kernel(int spp, int picard_order, ...) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if(tid < spp) {
        // 每个线程独立采样，无需共享内存同步
        realisation_args.picard_order = picard_order;
        pixel_realisation_gpu(..., &realisation_args, ...);
    }
}
```

#### ✅ 数值计算密集
- Picard1中的线性化计算适合GPU ALU:
  ```
  h_radi_hat = 4·σ·ε·Tref^3  // 预计算
  T_contrib = h_radi_hat · T_sampled
  ```
- 大量浮点运算，无分支预测问题（除了递归深度判断）

### 4.2 GPU挑战的特性

#### ⚠️ 动态递归深度

**问题**: `picard_order` 可运行时配置，GPU递归深度不能静态确定。

**影响**: 
- GPU硬件递归栈有限（CUDA: ~32层，但取决于寄存器使用）
- `picard_order=3` 理论上可产生指数级路径数（实际受Monte Carlo采样限制）

**缓解策略**:
1. **限制picard_order上限**（推荐 ≤3）:
   ```c
   #define GPU_MAX_PICARD_ORDER 3
   if(picard_order > GPU_MAX_PICARD_ORDER) {
       // 使用CPU fallback或拒绝
   }
   ```

2. **迭代展开**（如果picard_order固定）:
   ```cuda
   // 编译时特化
   template<int PicardOrder>
   __device__ double sample_with_picard() {
       if constexpr (PicardOrder == 1) {
           return sample_picard1();
       } else if constexpr (PicardOrder == 2) {
           return sample_picard2();
       } else if constexpr (PicardOrder == 3) {
           return sample_picard3();
       }
   }
   ```

3. **尾递归优化**（手动转换为循环）:
   ```cuda
   __device__ double sample_picard_iterative(int picard_order) {
       double T = 0;
       for(int depth = 0; depth < picard_order; depth++) {
           T = sample_layer(T);  // 使用前层T作为Tref
       }
       return T;
   }
   ```
   ⚠️ 这需要重新设计算法流程，偏离CPU实现。

#### ⚠️ 参考温度场访问

**问题**: `Tref` 可能时空变化，需要频繁随机访问。

**CPU实现**: 通过函数指针获取:
```c
// sdis_interface_c.h (简化)
typedef double (*get_reference_temperature_func)(
    struct sdis_interface*, 
    const struct sdis_interface_fragment*
);
```

**GPU挑战**: 
- 函数指针在CUDA中可用，但性能不佳（无法内联）
- 纹理缓存假设空间局部性，但Monte Carlo路径是随机的

**缓解策略**:
1. **扁平化Tref查找表**（如果时空离散）:
   ```cuda
   __constant__ double Tref_table[NUM_SURFACES][NUM_TIMES];
   
   __device__ double get_Tref_gpu(int surface_id, double time) {
       int t_idx = (int)(time / dt);
       return Tref_table[surface_id][t_idx];
   }
   ```

2. **统一参考温度**（picard_order=1的特殊情况）:
   ```cuda
   __constant__ double Tref_global = 300.0;  // K
   // 所有表面使用相同Tref，简化访问
   ```

3. **缓存最近访问的Tref**（利用时间局部性）:
   ```cuda
   __device__ double cached_Tref[8];  // 每线程缓存
   __device__ int cached_surface_id[8];
   ```

#### ⚠️ 路径数指数增长

**问题**: 
- `picard_order = 1`: 每个realisation采样1条路径
- `picard_order = 2`: 可能分支 → 最多2条路径
- `picard_order = 3`: 可能再次分支 → 理论上最多4条路径（2^(N-1)）

**实际情况**: 
- 不是所有边界都会分支（只有solid-fluid边界且需要辐射计算）
- Monte Carlo采样限制了实际路径数
- 但在复杂几何中，仍可能显著增加计算量

**GPU性能影响**:
- 线程束发散（warp divergence）: 不同线程的路径数不同
- 内存占用增加（需要存储多条路径状态）

**缓解策略**:
1. **固定路径数预算**（截断递归）:
   ```cuda
   #define MAX_PATHS_PER_REALISATION 4
   __device__ int path_budget = MAX_PATHS_PER_REALISATION;
   
   __device__ void sample_branch() {
       if(path_budget > 0) {
           path_budget--;
           // 采样分支
       } else {
           // 使用当前Tref，不再递归
       }
   }
   ```

2. **动态负载均衡**（不同picard_order放入不同kernel）:
   ```cuda
   // Kernel 1: picard_order = 1 (快速路径)
   __global__ void fast_path_kernel(...) { }
   
   // Kernel 2: picard_order > 1 (慢速路径)
   __global__ void slow_path_kernel(...) { }
   ```

#### ⚠️ 条件限制的运行时检查

**问题**: picardN不支持imposed flux和external source，需要运行时检查。

**CPU实现**: 
```c
if(picard_order > 1 && phi != 0) {
    log_err(..., "could not manage flux != 0 when picard order > 1");
    return RES_BAD_ARG;
}
```

**GPU挑战**: 
- 错误处理路径复杂（不能简单return）
- 需要传播错误状态到主机

**缓解策略**:
1. **预验证**（在CPU端提前检查）:
   ```cpp
   // GPU启动前验证
   if(scene.has_imposed_flux() && picard_order > 1) {
       throw std::invalid_argument("PicardN does not support imposed flux");
   }
   ```

2. **简化假设**（GPU版本不支持某些特性）:
   ```cuda
   // 文档明确: GPU实现仅支持picard_order=1
   __global__ void gpu_kernel(...) {
       assert(picard_order == 1);  // 编译时检查
   }
   ```

### 4.3 验证策略

#### CPU-GPU逐像素对比

由于Picard算法的复杂性，验证需要分层进行：

**Level 1: 单路径验证**
```cpp
// 固定随机数种子，采样单条路径
CPU_Path cpu = sample_path_cpu(seed=42, picard_order=1);
GPU_Path gpu = sample_path_gpu(seed=42, picard_order=1);

// 逐顶点对比
for(int i = 0; i < path.vertices.size(); i++) {
    ASSERT_NEAR(cpu.vertices[i].T, gpu.vertices[i].T, 1e-6);
    ASSERT_NEAR(cpu.vertices[i].weight, gpu.vertices[i].weight, 1e-6);
}
```

**Level 2: 单realisation验证**
```cpp
// 固定RNG状态，完整realisation
double T_cpu = realisation_cpu(rng_state, picard_order=1);
double T_gpu = realisation_gpu(rng_state, picard_order=1);

ASSERT_NEAR(T_cpu, T_gpu, 1e-6);
```

**Level 3: 统计收敛验证**
```cpp
// 多realisation，比较统计量
Stats cpu_stats = solve_cpu(spp=10000, picard_order=1);
Stats gpu_stats = solve_gpu(spp=10000, picard_order=1);

// 均值应一致（在统计误差内）
ASSERT_NEAR(cpu_stats.mean, gpu_stats.mean, 3*cpu_stats.stddev);
// 标准差应一致（Monte Carlo的方差特性）
ASSERT_NEAR(cpu_stats.stddev, gpu_stats.stddev, 0.1*cpu_stats.stddev);
```

**Level 4: Picard高阶验证**
```cpp
// 仅在Level 1-3全部通过后测试
for(int order = 2; order <= 3; order++) {
    // 简单场景（避免复杂边界条件）
    Scene simple_scene = create_simple_box_scene();
    
    double T_cpu = solve_cpu(simple_scene, picard_order=order);
    double T_gpu = solve_gpu(simple_scene, picard_order=order);
    
    ASSERT_NEAR(T_cpu, T_gpu, 1e-5);  // 稍微放宽容差
}
```

---

## 5. 代码关键路径摘要

### 5.1 初始化路径
```
stardis-app.c:271
  stardis->picard_order = args->picard_order

→ stardis-compute.c:412
  args.picard_order = stardis->picard_order

→ sdis_solve_camera.c:656
  realisation_args.picard_order = args->picard_order

→ sdis_realisation.c:83
  ctx.max_branchings = args->picard_order - 1
```

### 5.2 运行时判断路径
```
sdis_heat_path_boundary_Xd.h:78-82
  if(get_picard_order(ctx) == 1)
      → solid_fluid_boundary_picard1_path()
  else if(ctx->nbranchings == ctx->max_branchings)
      → solid_fluid_boundary_picard1_path()  // 递归终止
  else
      → solid_fluid_boundary_picardN_path()
```

### 5.3 递归采样路径 (picardN)
```
sdis_heat_path_boundary_Xd_solid_fluid_picardN.h:105
  XD(sample_coupled_path)(scn, ctx, &rwalk, rng, T)

→ （递归回到）sdis_heat_path_boundary_Xd.h:78
  判断 nbranchings < max_branchings
      ↓ YES
  ctx->nbranchings++
  再次调用 solid_fluid_boundary_picardN_path()
      ↓ NO
  调用 solid_fluid_boundary_picard1_path()  // 终止递归
```

### 5.4 关键函数清单

| 函数 | 文件 | 作用 |
|------|------|------|
| `get_picard_order()` | sdis_heat_path.h:80 | 从max_branchings反推picard_order |
| `solid_fluid_boundary_picard1_path()` | sdis_heat_path_boundary_Xd_solid_fluid_picard1.h:85 | picard_order=1的边界处理 |
| `solid_fluid_boundary_picardN_path()` | sdis_heat_path_boundary_Xd_solid_fluid_picardN.h:127 | picard_order>1的递归边界处理 |
| `XD(rwalk_get_Tref)()` | picard1.h:32 | 获取参考温度Tref（时空变化） |
| `XD(sample_path)()` | picardN.h:67 | 递归采样辅助函数 |
| `check_net_flux()` | picardN.h:34 | 验证picardN约束条件 |

---

## 6. GPU实现建议路线图

### 阶段1: Picard1实现 (2周)

**目标**: 支持 `picard_order = 1`（线性化辐射传输）

**工作内容**:
1. 实现 `solid_fluid_boundary_picard1_path_gpu()`
2. 实现 `rwalk_get_Tref_gpu()`（访问参考温度场）
3. 验证单路径结果与CPU一致

**验证标准**:
- 简单场景（单立方体+均匀Tref）逐像素误差 < 1e-6
- 性能: GPU加速比 > 10x

### 阶段2: PicardN基础实现 (2周)

**目标**: 支持 `picard_order = 2`（单层递归）

**工作内容**:
1. 实现 `solid_fluid_boundary_picardN_path_gpu()`
2. 手动展开递归（depth=1）
3. 实现 `nbranchings` 状态追踪

**验证标准**:
- 简单场景（无imposed flux）picard_order=2结果与CPU一致
- 性能: 相比picard1降低 < 2x

### 阶段3: 高阶Picard支持 (1周)

**目标**: 支持 `picard_order = 3`（两层递归）

**工作内容**:
1. 扩展递归展开到depth=2
2. 优化路径状态存储（减少寄存器使用）
3. 动态负载均衡（不同order使用不同kernel）

**验证标准**:
- 中等复杂场景picard_order=3结果与CPU一致
- 性能: 可接受的性能下降（<5x相比picard1）

### 阶段4: 性能优化 (1周)

**目标**: 达到10-100x总体加速比

**工作内容**:
1. Tref访问缓存优化
2. 共享内存使用（如果有重复访问）
3. 线程束占用率优化（减少寄存器溢出）

**验证标准**:
- 复杂场景（多面体+多光源）加速比 > 50x
- 内存占用 < 24GB (RTX 4090限制)

---

## 7. 风险与缓解

### 风险1: 递归深度限制

**描述**: GPU硬件栈深度有限，复杂场景+picard3可能溢出。

**影响**: 程序崩溃或结果错误。

**缓解**: 
- 限制picard_order ≤ 3（用户文档明确说明）
- 实现栈深度监控，超限自动降级到CPU
- 考虑尾递归优化（需要算法重写）

### 风险2: 性能不达标

**描述**: PicardN的分支开销可能导致GPU加速比<10x。

**影响**: GPU版本失去性能优势。

**缓解**: 
- 提供性能模式选项（fast mode仅支持picard1）
- 动态选择CPU/GPU（根据picard_order和场景复杂度）
- 文档说明性能最佳实践

### 风险3: 数值精度问题

**描述**: GPU双精度运算略有差异，可能导致蒙特卡洛路径分歧。

**影响**: 验证失败，结果不可信。

**缓解**: 
- 使用相同RNG算法（Threefry，CPU/GPU一致）
- 放宽容差到1e-5（仍满足物理精度要求）
- 实现bit-exact模式（性能换精度）

---

## 8. 测试用例设计

### 测试用例1: Picard1 基础验证

**场景**: 
- 单个立方体固体
- 两个流体边界（左右侧，T=280K和350K）
- 均匀Tref=300K
- picard_order=1

**期望**: 
- GPU结果与CPU逐像素误差 < 1e-6
- 温度梯度线性（符合线性化假设）

**代码参考**: `test_sdis_picard.c:717-718`

### 测试用例2: Picard2 递归验证

**场景**: 
- 同测试用例1
- picard_order=2
- 使用T^4作为参考（非均匀Tref）

**期望**: 
- GPU结果与CPU一致（容差1e-5）
- 温度分布更接近T^4准确解（相比picard1）

**代码参考**: `test_sdis_picard.c:745-746`

### 测试用例3: Picard3 高阶验证

**场景**: 
- 同测试用例1
- picard_order=3

**期望**: 
- GPU结果与CPU一致（容差1e-5）
- 收敛性: |T_picard3 - T_analytical| < |T_picard2 - T_analytical|

**代码参考**: `test_sdis_picard.c:766-767`

### 测试用例4: 约束条件拒绝

**场景**: 
- 设置imposed flux = 100 W/m²
- picard_order=2

**期望**: 
- GPU版本在启动前拒绝（抛出异常）
- 错误消息: "PicardN does not support imposed flux"

**代码参考**: `sdis_heat_path_boundary_Xd_solid_fluid_picardN.h:46-50`

---

## 9. 参考资料

### 官方文档
- [Picard算法博士论文](https://hal.archives-ouvertes.fr/tel-03266863/) - 理论基础
- STARDIS README Version 0.13 - 算法说明
- `test_sdis_picard.c` - 完整测试套件

### 关键源文件
- `sdis_heat_path.h` - Picard核心数据结构
- `sdis_heat_path_boundary_Xd_solid_fluid_picard1.h` - Picard1实现
- `sdis_heat_path_boundary_Xd_solid_fluid_picardN.h` - PicardN实现
- `sdis_realisation.c` - Picard初始化

### 相关文档
- `IR_arch.md` - 整体架构
- `ray_realisation_analysis.md` - Monte Carlo实现分析

---

**文档维护者**: Sisyphus (LLM Agent)  
**最后更新**: 2026-01-21 22:03  
**状态**: 完整技术分析 - 可用于GPU实现规划
