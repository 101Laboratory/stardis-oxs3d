# STARDIS COMPUTE_IR模式计算流程架构

**生成时间**: 2026-01-20  
**模式**: MODE_COMPUTE_IMAGE_IR (红外成像渲染)  
**用途**: 蒙特卡洛热辐射传输模拟，生成红外图像  

---

## 整体流程图

```
┌─────────────────────────────────────────────────────────────────┐
│                      应用层 (Application Layer)                   │
│                   stardis-cpu/stardis/0.12/src/                  │
└─────────────────────────────────────────────────────────────────┘

                            ┌─────────────┐
                            │   main()    │
                            │stardis-main.c│
                            └──────┬──────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │ 1. 参数解析 (parse_args)      │
                    │    stardis-args.h/c          │
                    │    mode = MODE_COMPUTE_IMAGE_IR│
                    └──────────────┬──────────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │ 2. 场景初始化 (stardis_init) │
                    │    stardis-app.h/c           │
                    │    - 加载几何模型             │
                    │    - 创建sdis_scene          │
                    │    - 解析相机参数             │
                    └──────────────┬──────────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │ 3. 计算入口                   │
                    │    stardis_compute()         │
                    │    stardis-compute.c:1129    │
                    └──────────────┬──────────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │ 4. 相机模式路由               │
                    │    compute_camera()          │
                    │    stardis-compute.c:529     │
                    └──────────────┬──────────────┘
                                   │
┌─────────────────────────────────┴─────────────────────────────────┐
│                       求解器层 (Solver Layer)                       │
│              stardis-cpu/stardis-solver/0.16.2/src/               │
└───────────────────────────────────────────────────────────────────┘
                                   │
                    ┌──────────────┴──────────────────┐
                    │ 5. 相机求解器                    │
                    │    sdis_solve_camera()          │
                    │    sdis.h:1709                   │
                    │                                  │
                    │  输入: sdis_solve_camera_args    │
                    │  ├─ cam (相机参数)                │
                    │  ├─ time_range[2]                │
                    │  ├─ image_definition[2] (分辨率) │
                    │  ├─ spp (每像素采样数)            │
                    │  ├─ picard_order (Picard迭代阶数)│
                    │  ├─ diff_algo (扩散算法)          │
                    │  └─ rng_state (随机数生成器)      │
                    │                                  │
                    │  输出: sdis_estimator_buffer**   │
                    └──────────────┬──────────────────┘
                                   │
            ┌──────────────────────┴──────────────────────┐
            │                                              │
     ┌──────┴──────┐                              ┌───────┴───────┐
     │ 6A. 相机设置 │                              │ 6B. 场景准备   │
     │ sdis_camera │                              │ sdis_scene     │
     │ .h:24       │                              │                │
     │ - axis_x[3] │                              │ - 几何数据     │
     │ - axis_y[3] │                              │ - 材料属性     │
     │ - axis_z[3] │                              │ - 边界条件     │
     │ - position  │                              │ - 辐射环境     │
     │ - fov_x     │                              │                │
     └──────┬──────┘                              └───────┬───────┘
            │                                              │
            └──────────────────┬───────────────────────────┘
                               │
            ┌──────────────────┴─────────────────────┐
            │ 7. 像素并行蒙特卡洛追踪                   │
            │    For each pixel(ix, iy):              │
            │    For each sample(1..spp):             │
            │      ├─ 生成相机光线 (camera_ray)        │
            │      ├─ 执行热传输路径追踪                │
            │      └─ 累积辐射估计                     │
            └──────────────────┬─────────────────────┘
                               │
┌──────────────────────────────┴──────────────────────────────────┐
│                    核心传输层 (Transport Core)                    │
│                                                                   │
│         三种热传输模式的蒙特卡洛随机游走 (Random Walk)             │
└───────────────────────────────────────────────────────────────────┘
                               │
        ┌──────────────────────┼──────────────────────┐
        │                      │                      │
 ┌──────┴──────┐      ┌────────┴────────┐    ┌───────┴────────┐
 │ 8A. 辐射传输  │      │ 8B. 传导传输     │    │ 8C. 对流传输    │
 │ sdis_heat_  │      │ sdis_heat_path_  │    │ sdis_heat_path_│
 │ path_       │      │ conductive_Xd.h  │    │ convective_Xd.h│
 │ radiative_  │      │                  │    │                │
 │ Xd.h        │      │ 扩散算法:         │    │ - 流体介质处理  │
 │             │      │ - Delta Sphere   │    │ - 对流系数     │
 │ - 射线追踪   │      │ - Walk on Sphere │    │                │
 │ - 辐射环境   │      │                  │    │                │
 │ - 边界温度^4 │      │ - Laplace求解器  │    │                │
 └──────┬──────┘      └────────┬────────┘    └───────┬────────┘
        │                      │                      │
        └──────────────────────┼──────────────────────┘
                               │
                    ┌──────────┴──────────┐
                    │ 9. 边界处理           │
                    │ sdis_heat_path_      │
                    │ boundary_Xd.h        │
                    │                      │
                    │ 处理三种边界类型:      │
                    │ - 固体-固体界面       │
                    │ - 固体-流体界面       │
                    │ - 外部边界条件        │
                    │                      │
                    │ Picard迭代:           │
                    │ - picard1: 线性化     │
                    │ - picardN: 高阶迭代   │
                    └──────────┬──────────┘
                               │
                    ┌──────────┴──────────┐
                    │ 10. 温度估计          │
                    │ rwalk_context        │
                    │ sdis_heat_path.h:35  │
                    │                      │
                    │ - Tmin/That边界      │
                    │ - max_branchings     │
                    │ - 扩散算法选择        │
                    │ - 随机游走状态        │
                    └──────────┬──────────┘
                               │
┌──────────────────────────────┴──────────────────────────────────┐
│                       结果处理层 (Result Layer)                    │
└───────────────────────────────────────────────────────────────────┘
                               │
                    ┌──────────┴──────────┐
                    │ 11. 结果累积          │
                    │ sdis_estimator_buffer│
                    │                      │
                    │ - 每像素蒙特卡洛估计   │
                    │ - 方差估计            │
                    │ - 统计收敛性          │
                    └──────────┬──────────┘
                               │
                    ┌──────────┴──────────┐
                    │ 12. 图像输出          │
                    │ stardis-output.h/c   │
                    │                      │
                    │ - dump_vtk_image()   │
                    │ - dump_ht_image()    │
                    │                      │
                    │ 格式: VTK或HT        │
                    └──────────┬──────────┘
                               │
                            ┌──┴───┐
                            │ 完成  │
                            └──────┘
```

---

## 关键数据结构

### 1. 输入数据结构

#### 应用层相机配置
**文件**: `stardis-cpu/stardis/0.12/src/stardis-args.h`

```c
struct stardis {
  struct {
    double pos[3];              // 相机位置 [m]
    double tgt[3];              // 观察目标 [m]
    double up[3];               // 上向量 (归一化)
    double fov;                 // 视场角 [度]
    int img_width, img_height;  // 图像分辨率 [像素]
    int spp;                    // 每像素采样数 (samples per pixel)
    double time_range[2];       // 观测时间范围 [s]
  } camera;
  
  unsigned picard_order;        // Picard迭代阶数 (处理辐射非线性)
  enum sdis_diffusion_algorithm diff_algo;  // 扩散算法类型
  // ... 其他场景数据
}
```

#### 求解器参数
**文件**: `stardis-cpu/stardis-solver/0.16.2/src/sdis.h:875`

```c
struct sdis_solve_camera_args {
  struct sdis_camera* cam;              // 相机对象
  double time_range[2];                 // 观测时间 [s]
  size_t picard_order;                  // Picard递归阶数
  size_t image_definition[2];           // [width, height]
  size_t spp;                           // 每像素采样数
  int register_paths;                   // 路径记录标志位
  struct ssp_rng* rng_state;            // 随机数生成器状态
  enum ssp_rng_type rng_type;           // RNG类型 (默认: SSP_RNG_THREEFRY)
  enum sdis_diffusion_algorithm diff_algo;  // 扩散算法
};

// 默认值
#define SDIS_SOLVE_CAMERA_ARGS_DEFAULT {
  NULL,                           // 相机需要显式设置
  {DBL_MAX, DBL_MAX},             // 时间范围
  1,                              // Picard阶数=1 (线性化)
  {512, 512},                     // 默认512x512分辨率
  256,                            // 默认每像素256采样
  SDIS_HEAT_PATH_NONE,            // 不记录路径
  NULL,                           // 自动生成RNG状态
  SSP_RNG_THREEFRY,               // Threefry RNG
  SDIS_DIFFUSION_DELTA_SPHERE     // Delta Sphere扩散算法
}
```

### 2. 核心传输数据结构

#### 随机游走上下文
**文件**: `stardis-cpu/stardis-solver/0.16.2/src/sdis_heat_path.h:35`

```c
struct rwalk_context {
  struct green_path_handle* green_path;  // Green函数路径句柄
  struct sdis_heat_path* heat_path;      // 热路径数据
  
  // 温度边界及其预计算幂次 (用于Picard迭代)
  double Tmin;                  // 温度下界 [K]
  double Tmin2;                 // Tmin^2
  double Tmin3;                 // Tmin^3
  double That;                  // 温度上界 [K]
  double That2;                 // That^2
  double That3;                 // That^3
  
  // Picard迭代控制
  size_t max_branchings;        // 最大分支数 = picard_order - 1
  size_t nbranchings;           // 当前分支计数
  
  size_t irealisation;          // 实现ID (调试用)
  enum sdis_diffusion_algorithm diff_algo;  // 扩散算法选择
};

// Picard阶数关系: order = max_branchings + 1
// order=1 (线性化): max_branchings=0, 不分支
// order=3: max_branchings=2, 最多分支2次
```

#### 随机游走状态
**文件**: `stardis-cpu/stardis-solver/0.16.2/src/sdis_heat_path.h:90`

```c
struct rwalk {
  struct sdis_rwalk_vertex vtx;  // 当前位置和时间
  unsigned enc_id;               // 所在包壳(enclosure)ID
  struct s2d_hit hit_2d;         // 2D碰撞信息
  struct s3d_hit hit_3d;         // 3D碰撞信息
  double dir[3];                 // 到达辐射环境的方向
  double elapsed_time;           // 累积经过时间 [s]
  enum sdis_side hit_side;       // 碰撞面 (FRONT/BACK)
  // ... 其他状态
};

// 时空顶点定义
struct sdis_rwalk_vertex {
  double P[3];                   // 世界空间位置 [m]
  double time;                   // 顶点时间 [s]
};
```

#### 相机对象
**文件**: `stardis-cpu/stardis-solver/0.16.2/src/sdis_camera.h:24`

```c
struct sdis_camera {
  // 正交相机坐标系 (右手坐标系)
  double axis_x[3];              // 右向量 (归一化)
  double axis_y[3];              // 上向量 (归一化)
  double axis_z[3];              // 前向量 (归一化)
  
  double position[3];            // 相机位置 [m]
  double fov_x;                  // 水平视场角 [rad]
  double rcp_proj_ratio;         // 投影比 height/width
  
  ref_T ref;                     // 引用计数
  struct sdis_device* dev;       // 所属设备
};

// 生成相机光线 (内联函数)
static FINLINE void camera_ray(
  const struct sdis_camera* cam,
  const double sample[2],        // 像素内归一化坐标 [0,1)
  double org[3],                 // 输出: 光线原点
  double dir[3]                  // 输出: 光线方向 (归一化)
);
```

### 3. 输出数据结构

#### 估计器缓冲区
**文件**: `stardis-cpu/stardis-solver/0.16.2/src/sdis.h` (估算)

```c
// 图像估计器缓冲区 (每像素一个估计器)
struct sdis_estimator_buffer {
  size_t definition[2];          // 图像分辨率 [width, height]
  struct sdis_estimator** estimators;  // 估计器数组 [width*height]
  // 访问方式: estimators[iy * width + ix]
};
```

#### 蒙特卡洛估计器
**文件**: `stardis-cpu/stardis-solver/0.16.2/src/sdis.h` (推断)

```c
struct sdis_estimator {
  struct sdis_mc temperature;    // 温度的MC估计 [K]
  struct sdis_mc variance;       // 方差估计
  size_t nrealisations;          // 已完成的实现次数
  // ... 路径记录数据
};

// 蒙特卡洛统计量
struct sdis_mc {
  double mean;                   // 均值
  double stddev;                 // 标准差
  double variance;               // 方差
};
```

---

## 核心算法详解

### 1. 蒙特卡洛热传输路径追踪

#### 基本原理
对于每个像素的每次采样:
1. **生成光线**: 从相机出发，穿过像素中心(带抖动)
2. **射线追踪**: 找到场景中第一个相交的表面
3. **热传输模拟**: 从相交点开始，执行随机游走直到:
   - 到达已知温度边界
   - 逃逸到无穷远
   - 超过最大步数
4. **温度估计**: 根据路径累积的贡献计算温度
5. **辐射计算**: 使用Stefan-Boltzmann定律: L = ε·σ·T^4

#### 伪代码
```
for each pixel (ix, iy):
  estimator = create_estimator()
  
  for sample_id in 1..spp:
    // 1. 生成相机光线
    u = (ix + random()) / width
    v = (iy + random()) / height
    ray = camera_ray(cam, [u, v])
    
    // 2. 场景相交
    hit = scene_intersect(ray)
    if not hit:
      continue  // 未击中任何物体
    
    // 3. 初始化随机游走
    rwalk = init_rwalk(hit.position, hit.normal, hit.time)
    ctx = init_rwalk_context(picard_order, diff_algo)
    
    // 4. 执行随机游走
    temperature = sample_heat_path(rwalk, ctx, scene)
    
    // 5. 累积估计
    estimator.accumulate(temperature)
  
  // 6. 计算统计量
  estimator.finalize()
  buffer[ix, iy] = estimator
```

### 2. 热传输模式

#### A. 辐射传输 (Radiative)
**文件**: `sdis_heat_path_radiative_Xd.h`

- **物理**: 热辐射遵循T^4定律 (Stefan-Boltzmann)
- **方法**: 射线追踪，考虑:
  - 表面发射率 (emissivity)
  - 视角因子 (view factor)
  - 辐射环境 (外部源、环境辐射)
- **非线性处理**: 使用Picard迭代逼近T^4项

**关键函数**:
```c
// 辐射边界处理
sample_radiative_boundary(
  struct rwalk* rwalk,
  struct rwalk_context* ctx,
  struct sdis_scene* scene
);
```

#### B. 传导传输 (Conductive)
**文件**: `sdis_heat_path_conductive_Xd.h`

- **物理**: Fourier定律, ∇²T = 0 (稳态)
- **扩散算法**:
  1. **Delta Sphere**: 在δ球面上采样，δ为网格步长
  2. **Walk on Sphere (WoS)**: 在最大内切球面上采样
- **求解器**: 蒙特卡洛Laplace方程求解

**关键参数**:
```c
enum sdis_diffusion_algorithm {
  SDIS_DIFFUSION_DELTA_SPHERE,  // 固定步长
  SDIS_DIFFUSION_WOS             // 自适应步长
};
```

#### C. 对流传输 (Convective)
**文件**: `sdis_heat_path_convective_Xd.h`

- **物理**: Newton冷却定律, q = h·(T_s - T_∞)
- **处理**: 流体介质内部的传热
- **耦合**: 固体-流体界面的热交换

### 3. Picard迭代处理辐射非线性

#### 问题
辐射热传输中的T^4项导致非线性方程。

#### 解决方案
使用Picard迭代逼近:

```
T^(n+1) = Solve_Linear(
  conduction + convection + ε·σ·(T^(n))^4
)
```

**迭代阶数含义**:
- **order = 1**: 线性化, T^4 ≈ (Tguess)^4 (常数)
- **order = 2**: 一次迭代修正
- **order = 3+**: 高阶迭代，更精确但计算量大

**实现**:
```c
// Picard分支控制
if (ctx->nbranchings < ctx->max_branchings) {
  // 允许分支: 采样耦合路径
  sample_coupled_path(...);
  ctx->nbranchings++;
} else {
  // 达到最大分支: 使用线性化温度
  use_linearized_temperature(...);
}
```

### 4. 边界条件处理

#### 三种边界类型
**文件**: `sdis_heat_path_boundary_Xd.h`

1. **固体-固体界面** (`solid_solid`):
   - 温度连续
   - 热流连续
   - 考虑接触热阻

2. **固体-流体界面** (`solid_fluid`):
   - Picard迭代版本:
     - `picard1`: 线性化处理
     - `picardN`: 完整迭代处理
   - 对流边界条件

3. **外部边界** (`handle_external_net_flux`):
   - Dirichlet边界 (已知温度)
   - Neumann边界 (已知热流)
   - Robin边界 (混合)

---

## 并行化策略

### 1. CPU并行 (现有实现)

#### 像素级并行
```c
// 使用OpenMP或线程池
#pragma omp parallel for collapse(2)
for (iy = 0; iy < height; iy++) {
  for (ix = 0; ix < width; ix++) {
    // 每个像素独立计算
    compute_pixel_estimator(ix, iy, ...);
  }
}
```

#### 采样级并行
```c
// 每像素内的采样也可并行
#pragma omp parallel for
for (sample_id = 0; sample_id < spp; sample_id++) {
  sample_heat_path(...);
}
```

### 2. GPU并行 (迁移目标)

#### 计算模型
- **1个线程 = 1次采样**: 每个GPU线程处理一次蒙特卡洛采样
- **线程组织**: 
  ```
  ThreadGroup(8x8) → 64 threads
  Dispatch((width+7)/8, (height+7)/8, spp)
  ```
- **总线程数**: `width * height * spp`

#### 关键优化点
1. **随机数生成**: 每线程独立的RNG状态 (Threefry)
2. **内存访问**: 
   - 场景数据: 只读缓冲区 (常量内存)
   - 估计器: 原子操作累积
3. **分歧控制**: 减少warp内分支发散
4. **射线追踪**: 使用DXR或自定义BVH

---

## 关键代码文件索引

### 应用层 (Application)
| 文件 | 位置 | 功能 |
|------|------|------|
| `stardis-main.c` | `stardis/0.12/src/` | 主入口, 命令行处理 |
| `stardis-args.h/c` | `stardis/0.12/src/` | 参数解析, 模式定义 |
| `stardis-app.h/c` | `stardis/0.12/src/` | 场景初始化, 数据管理 |
| `stardis-compute.h/c` | `stardis/0.12/src/` | 计算调度, 模式路由 |
| `stardis-output.h/c` | `stardis/0.12/src/` | 结果输出 (VTK/HT格式) |
| `stardis-parsing.h/c` | `stardis/0.12/src/` | 文件解析 (几何/材料) |

**关键函数**:
- `main()`: 程序入口
- `stardis_compute()`: L1129, 计算模式分发
- `compute_camera()`: L529, 相机模式处理

### 求解器层 (Solver API)
| 文件 | 位置 | 功能 |
|------|------|------|
| `sdis.h` | `stardis-solver/0.16.2/src/` | 主API头文件 |
| `sdis_camera.h` | `stardis-solver/0.16.2/src/` | 相机对象定义 |
| `sdis_scene_c.h` | `stardis-solver/0.16.2/src/` | 场景内部结构 |
| `sdis_device_c.h` | `stardis-solver/0.16.2/src/` | 设备管理 |

**关键API**:
- `sdis_solve_camera()`: L1709, 相机求解入口
- `sdis_camera_create()`: 创建相机对象
- `camera_ray()`: 生成光线 (内联)

### 热传输核心 (Transport Core)
| 文件 | 位置 | 功能 |
|------|------|------|
| `sdis_heat_path.h` | `stardis-solver/0.16.2/src/` | 路径追踪框架 |
| `sdis_heat_path_radiative_Xd.h` | `stardis-solver/0.16.2/src/` | 辐射传输 |
| `sdis_heat_path_conductive_Xd.h` | `stardis-solver/0.16.2/src/` | 传导传输 (通用接口) |
| `sdis_heat_path_conductive_delta_sphere_Xd.h` | `stardis-solver/0.16.2/src/` | Delta Sphere算法 |
| `sdis_heat_path_conductive_wos_Xd.h` | `stardis-solver/0.16.2/src/` | Walk on Sphere算法 |
| `sdis_heat_path_convective_Xd.h` | `stardis-solver/0.16.2/src/` | 对流传输 |
| `sdis_heat_path_boundary_Xd.h` | `stardis-solver/0.16.2/src/` | 边界处理框架 |
| `sdis_heat_path_boundary_Xd_solid_solid.h` | `stardis-solver/0.16.2/src/` | 固-固界面 |
| `sdis_heat_path_boundary_Xd_solid_fluid_picard1.h` | `stardis-solver/0.16.2/src/` | 固-液界面(线性) |
| `sdis_heat_path_boundary_Xd_solid_fluid_picardN.h` | `stardis-solver/0.16.2/src/` | 固-液界面(完整) |
| `sdis_heat_path_boundary_Xd_handle_external_net_flux.h` | `stardis-solver/0.16.2/src/` | 外部边界 |

### 支撑库 (Support Libraries)
| 库 | 位置 | 功能 |
|-----|------|------|
| `star-3d` | `star-3d/0.10/src/` | 3D几何, 射线追踪, BVH |
| `star-sp` | `star-sp/0.12/src/` | 采样库, 随机数生成 |
| `rsys` | `rsys/0.15/src/` | 基础工具, 内存管理 |
| `s2d`, `s3d` | `s2d/`, `s3d/` | 2D/3D几何基础 |
| `senc2d`, `senc3d` | `senc2d/`, `senc3d/` | 包壳(enclosure)库 |

---

## GPU迁移关键点

### 1. 优先移植模块
按重要性和复杂度排序:

1. **相机光线生成** (最简单)
   - `camera_ray()` → GPU着色器函数
   - 每线程独立, 无状态

2. **射线-场景相交** (中等)
   - `star-3d`射线追踪 → DXR或自定义BVH
   - 需要加速结构

3. **随机数生成** (中等)
   - `star-sp` RNG → GPU Threefry实现
   - 每线程独立状态

4. **辐射传输** (核心, 较简单)
   - 射线追踪为主
   - 边界查询

5. **传导传输** (核心, 复杂)
   - Delta Sphere/WoS算法
   - 需要几何距离查询

6. **边界条件** (核心, 最复杂)
   - 多种边界类型
   - Picard迭代控制

### 2. 数据结构适配

#### CPU → GPU映射
```cpp
// CPU: 动态分配, 指针链接
struct sdis_scene {
  struct sdis_medium** media;  // 动态数组
  // ...
};

// GPU: 扁平化, 索引访问
struct GpuScene {
  uint32_t numMedia;
  uint32_t mediaBufferIndex;  // 指向StructuredBuffer
  // ...
};
```

#### 推荐GPU布局
```hlsl
// 常量缓冲区
cbuffer SceneParams : register(b0) {
  float3 cameraPos;
  float3 cameraAxisX, cameraAxisY, cameraAxisZ;
  float fovX, rcpProjRatio;
  float2 timeRange;
  uint2 resolution;
  uint spp;
  uint picardOrder;
  // ...
};

// 结构化缓冲区
StructuredBuffer<Material> g_materials : register(t0);
StructuredBuffer<Boundary> g_boundaries : register(t1);
RaytracingAccelerationStructure g_scene : register(t2);

// UAV输出
RWStructuredBuffer<Estimator> g_estimators : register(u0);
```

### 3. 验证策略

#### 逐像素对比
```cpp
// 1. CPU运行: 保存每像素结果
cpu_run(scene, args, "cpu_result.bin");

// 2. GPU运行: 保存每像素结果
gpu_run(scene, args, "gpu_result.bin");

// 3. 对比
validate_results("cpu_result.bin", "gpu_result.bin", 
                 tolerance = 1e-6);
```

#### 渐进式验证
1. **阶段1**: 单像素单采样 (确保算法正确)
2. **阶段2**: 单像素多采样 (验证统计收敛)
3. **阶段3**: 全图像低采样 (验证并行正确性)
4. **阶段4**: 全图像高采样 (性能测试)

### 4. 性能目标

#### 预期加速比
| 场景复杂度 | CPU时间 | GPU目标 | 加速比 |
|-----------|---------|---------|--------|
| 简单 (1K面) | 10s | 0.1s | 100x |
| 中等 (10K面) | 100s | 2s | 50x |
| 复杂 (100K面) | 1000s | 50s | 20x |

*假设: 512x512分辨率, 256 spp*

---

## 术语表

| 术语 | 英文 | 含义 |
|------|------|------|
| 蒙特卡洛 | Monte Carlo | 基于随机采样的数值方法 |
| 随机游走 | Random Walk | 粒子在空间中的随机路径 |
| Picard迭代 | Picard Iteration | 求解非线性方程的迭代方法 |
| 视场角 | Field of View (FOV) | 相机可见的角度范围 |
| 每像素采样数 | Samples Per Pixel (SPP) | 每个像素的蒙特卡洛采样次数 |
| 扩散算法 | Diffusion Algorithm | 求解传导方程的算法 |
| Delta Sphere | δ球面 | 固定步长的球面采样 |
| Walk on Sphere (WoS) | 球面游走 | 自适应步长的球面采样 |
| 辐射环境 | Radiative Environment | 外部辐射源和环境辐射 |
| 包壳 | Enclosure | 封闭的辐射交换区域 |
| Green函数 | Green's Function | 热传输问题的基本解 |
| 估计器 | Estimator | 蒙特卡洛统计量累积器 |
| 实现 | Realisation | 一次蒙特卡洛采样 |

---

## 参考文献

- **物理模型**: 
  - Stefan-Boltzmann定律: L = ε·σ·T^4
  - Fourier传导定律: q = -k·∇T
  - Newton冷却定律: q = h·(T_s - T_∞)

- **数值方法**:
  - Monte Carlo Method for Radiative Transfer
  - Walk on Sphere for Laplace Equation
  - Picard Iteration for Nonlinear Problems

- **相关论文**:
  - [需补充具体论文引用]

---

*文档生成: 2026-01-20*  
*项目: Stardis-GPU*  
*状态: 分析完成, GPU实现待开始*
