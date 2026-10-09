# Ray Realisation 3D - Data Structures and Execution Stack

**生成时间**: 2026-01-21 19:42  
**源代码位置**: `stardis-cpu-cuda_impl_analysis/stardis-solver/0.16.2/src/`  
**目标**: 为GPU移植提供数据结构参考文档  

---

## 执行摘要

本文档详细分析了 `sdis::ray_realisation_3d` 函数的执行栈和依赖的所有数据结构。该函数是STARDIS蒙特卡洛辐射传输求解器的核心,负责沿光线追踪热辐射路径。

**关键发现**:
- 函数涉及 **15+ 核心数据结构**
- 调用栈深度 **3-5层** (radiative → boundary → conductive/convective)
- 主要计算密集操作: **射线追踪**、**BRDF采样**、**边界条件处理**
- GPU并行化潜力: **极高** (每条光线完全独立)

---

## 1. 函数签名和入口点

### 1.1 函数声明

```c
// 文件: sdis_realisation.h (line 208-212)
extern LOCAL_SYM res_T
ray_realisation_3d
  (struct sdis_scene* scn,
   struct ray_realisation_args* args,
   double* weight);
```

### 1.2 函数实现

```c
// 文件: sdis_realisation.c (line 57-108)
res_T
ray_realisation_3d
  (struct sdis_scene* scn,
   struct ray_realisation_args* args,
   double* weight)
{
  struct rwalk_context ctx = RWALK_CONTEXT_NULL;
  struct rwalk rwalk = RWALK_NULL;
  struct temperature T = TEMPERATURE_NULL;
  float dir[3];
  res_T res = RES_OK;

  // 1. 初始化随机游走状态
  d3_set(rwalk.vtx.P, args->position);
  rwalk.vtx.time = args->time;
  rwalk.hit_3d = S3D_HIT_NULL;
  rwalk.hit_side = SDIS_SIDE_NULL__;
  rwalk.enc_id = args->enc_id;

  // 2. 初始化上下文 (温度边界、Picard阶数等)
  ctx.heat_path = args->heat_path;
  ctx.Tmin  = scn->tmin;
  ctx.Tmin2 = ctx.Tmin * ctx.Tmin;
  ctx.Tmin3 = ctx.Tmin * ctx.Tmin2;
  ctx.That  = scn->tmax;
  ctx.That2 = ctx.That * ctx.That;
  ctx.That3 = ctx.That * ctx.That2;
  ctx.max_branchings = args->picard_order - 1;
  ctx.irealisation = args->irealisation;
  ctx.diff_algo = args->diff_algo;

  // 3. 注册起始顶点到热路径
  res = register_heat_vertex(
    args->heat_path, &rwalk.vtx, 0, SDIS_HEAT_VERTEX_RADIATIVE, 0);

  // 4. 追踪辐射路径 (核心计算)
  res = trace_radiative_path_3d(scn, dir, &ctx, &rwalk, args->rng, &T);

  // 5. 如果未解析温度,采样耦合路径 (传导/对流)
  if(!T.done) {
    res = sample_coupled_path_3d(scn, &ctx, &rwalk, args->rng, &T);
  }

  *weight = T.value;
  return res;
}
```

---

## 2. 核心数据结构详解

### 2.1 输入参数结构 - `ray_realisation_args`

**用途**: 光线采样的输入参数  
**定义位置**: `sdis_realisation.h` (line 183-193)

```c
struct ray_realisation_args {
  struct ssp_rng* rng;              // 随机数生成器 (每线程独立)
  unsigned enc_id;                   // 包壳ID (enclosure) - 光线起始位置所在的几何区域
  double position[3];                // 光线起点 [m]
  double direction[3];               // 光线方向 (归一化)
  double time;                       // 观测时间 [s]
  size_t picard_order;               // Picard迭代阶数 (处理非线性辐射传输)
  struct sdis_heat_path* heat_path;  // 热路径记录 (可选, 用于可视化/调试)
  size_t irealisation;               // 采样ID (用于调试)
  enum sdis_diffusion_algorithm diff_algo; // 扩散算法 (WoS/Delta Sphere)
};
```

**GPU移植注意**:
- ✅ `rng`: 必须使用GPU友好的RNG (如Threefry, Philox)
- ✅ `position/direction`: 连续存储,适合GPU
- ⚠️ `heat_path`: 指针链表,GPU需改为索引+数组
- ✅ `enc_id`: 标量,适合GPU

---

### 2.2 随机游走状态 - `rwalk`

**用途**: 追踪光线当前状态 (位置、时间、相交信息)  
**定义位置**: `sdis_heat_path.h` (line 90-111)

```c
struct rwalk {
  struct sdis_rwalk_vertex vtx;  // 当前位置和时间
  unsigned enc_id;                // 当前包壳ID
  struct s2d_hit hit_2d;          // 2D相交结果
  struct s3d_hit hit_3d;          // 3D相交结果 ← 关键!
  double dir[3];                  // 到达辐射环境的方向
  double elapsed_time;            // 已用时间
  enum sdis_side hit_side;        // 相交面的正反面
};

struct sdis_rwalk_vertex {
  double P[3];    // 世界空间位置
  double time;    // 当前时间
};
```

**GPU移植注意**:
- ✅ 结构体扁平,无指针
- ✅ 双精度 `P[3]` 和 `time` (RTX 4090支持)
- ✅ 适合寄存器存储 (约64字节)

---

### 2.3 射线相交结果 - `s3d_hit`

**用途**: 存储射线与几何体的相交信息  
**定义位置**: `star-3d/0.10/src/s3d.h` (line 128-143)

```c
struct s3d_hit {
  struct s3d_primitive prim;  // 相交的几何图元
  float normal[3];            // 几何法线 (未归一化)
  float uv[2];                // 重心坐标
  float distance;             // 相交距离 (FLT_MAX表示未相交)
};

struct s3d_primitive {
  unsigned prim_id;         // 图元ID
  unsigned geom_id;         // 几何ID
  unsigned inst_id;         // 实例ID
  unsigned scene_prim_id;   // 场景中的图元ID
  void* shape__;            // ⚠️ 内部指针 (GPU需要去除)
  void* inst__;             // ⚠️ 内部指针 (GPU需要去除)
};
```

**GPU移植注意**:
- ✅ 相交检测是独立操作,高度并行
- ⚠️ `shape__/inst__`: 指针需要转换为索引
- ✅ `normal/uv/distance`: GPU友好
- 💡 **优化建议**: 使用DXR硬件加速 (RTX 4090的RT Cores)

---

### 2.4 随机游走上下文 - `rwalk_context`

**用途**: 存储求解参数和全局状态  
**定义位置**: `sdis_heat_path.h` (line 35-77)

```c
struct rwalk_context {
  struct green_path_handle* green_path;  // Green函数路径 (可选)
  struct sdis_heat_path* heat_path;      // 热路径记录 (可选)
  
  double Tmin;   // 下界温度
  double Tmin2;  // Tmin^2 (预计算)
  double Tmin3;  // Tmin^3 (预计算)
  
  double That;   // 上界温度
  double That2;  // That^2 (预计算)
  double That3;  // That^3 (预计算)
  
  size_t max_branchings;  // 最大分支数 = picard_order - 1
  size_t nbranchings;     // 当前分支计数
  size_t irealisation;    // 采样ID
  
  enum sdis_diffusion_algorithm diff_algo; // 扩散算法
};
```

**GPU移植注意**:
- ✅ 大部分是标量,适合常量内存
- ⚠️ `green_path/heat_path`: 指针,GPU需要重新设计
- 💡 **优化建议**: `Tmin2/Tmin3/That2/That3` 可放入常量缓冲区

---

### 2.5 温度状态 - `temperature`

**用途**: 追踪温度求解进度 (状态机模式)  
**定义位置**: `sdis_heat_path.h` (line 113-124)

```c
struct temperature {
  res_T (*func)(  // 函数指针: 下一步要调用的热传输函数
    struct sdis_scene* scn,
    struct rwalk_context* ctx,
    struct rwalk* rwalk,
    struct ssp_rng* rng,
    struct temperature* temp);
  
  double value;  // 当前温度值 [K]
  int done;      // 是否已解析
};
```

**GPU移植挑战**:
- ❌ **函数指针不支持GPU** (或性能极差)
- 💡 **解决方案**: 使用枚举 + switch语句替代函数指针:

```c
enum heat_transfer_type {
  HEAT_TRANSFER_RADIATIVE,
  HEAT_TRANSFER_CONDUCTIVE,
  HEAT_TRANSFER_CONVECTIVE,
  HEAT_TRANSFER_BOUNDARY
};

struct temperature_gpu {
  enum heat_transfer_type next_func;  // 替代函数指针
  double value;
  int done;
};
```

---

### 2.6 热路径记录 - `sdis_heat_path`

**用途**: 记录采样路径的几何和权重 (用于可视化/Green函数)  
**定义位置**: `sdis_heat_path.h` (line 134-142)

```c
struct sdis_heat_path {
  struct darray_heat_vertex vertices;  // 动态数组: 路径顶点
  struct darray_size_t breaks;         // 动态数组: 分支点索引
  enum sdis_heat_path_flag status;     // 路径状态
};

// 动态数组 (rsys库)
struct darray_heat_vertex {
  struct sdis_heat_vertex* data;  // 数据指针
  size_t size;                    // 当前大小
  size_t capacity;                // 容量
  struct mem_allocator* alloc;    // 分配器
};
```

**GPU移植挑战**:
- ❌ **动态内存分配** 在GPU内核中不可行
- 💡 **解决方案**: 预分配固定大小缓冲区:

```c
#define MAX_HEAT_PATH_VERTICES 1024

struct sdis_heat_path_gpu {
  struct sdis_heat_vertex vertices[MAX_HEAT_PATH_VERTICES];
  size_t vertex_count;
  size_t breaks[MAX_HEAT_PATH_VERTICES / 10];  // 假设最多10%是分支点
  size_t break_count;
  enum sdis_heat_path_flag status;
};
```

---

### 2.7 界面片段 - `sdis_interface_fragment`

**用途**: 存储光线与边界的交点信息  
**定义位置**: `sdis.h` (line 126-135)

```c
struct sdis_interface_fragment {
  double P[3];    // 世界空间位置
  double Ng[3];   // 几何法线 (归一化)
  double uv[2];   // 参数坐标
  double time;    // 当前时间
  enum sdis_side side;  // 正面/背面
};
```

**GPU移植注意**:
- ✅ 完全扁平,无指针
- ✅ 双精度支持
- ✅ 适合GPU寄存器

---

### 2.8 场景数据 - `sdis_scene`

**用途**: 存储完整的物理场景 (几何、材料、边界条件)  
**定义位置**: `sdis_scene_c.h` (复杂结构,~500行)

**关键成员** (简化):
```c
struct sdis_scene {
  struct sdis_device* dev;          // 设备上下文
  
  // 几何数据
  struct s3d_scene_view* s3d_view;  // 3D场景视图 (BVH加速结构)
  struct s2d_scene_view* s2d_view;  // 2D场景视图
  
  // 物理属性
  struct darray_interface interfaces;  // 边界条件数组
  struct darray_medium media;           // 介质数组 (固体/流体)
  struct sdis_radiative_env* radenv;    // 辐射环境
  struct sdis_source* source;           // 外部热源
  
  // 求解参数
  double tmin, tmax;    // 温度边界
  double fp_to_meter;   // 浮点数到米的转换系数
  
  // 编码结构 (enclosures)
  struct senc3d_scene* senc3d;  // 3D包壳场景
  struct senc2d_scene* senc2d;  // 2D包壳场景
};
```

**GPU移植挑战**:
- ❌ **指针森林**: 多层嵌套指针
- ❌ **动态数组**: CPU侧动态分配
- ❌ **BVH加速结构**: CPU格式不适合GPU
- 💡 **解决方案**:
  1. 扁平化所有数组 (SoA布局)
  2. 指针→索引转换
  3. BVH使用DXR的`ID3D12RaytracingAccelerationStructure`

---

## 3. 函数调用链

### 3.1 调用栈概览

```
sdis_solve_camera (主循环)
  ↓
ray_realisation_3d ← 本文档重点
  ↓
  ├─ trace_radiative_path_3d (辐射路径追踪)
  │   ├─ find_next_fragment_3d (查找下一个相交)
  │   │   └─ s3d_scene_view_trace_ray (射线追踪 - star-3d库)
  │   ├─ brdf_setup (BRDF材质设置)
  │   └─ brdf_sample (BRDF采样 - 漫反射/镜面)
  │
  └─ sample_coupled_path_3d (耦合路径采样)
      ├─ boundary_path_3d (边界路径)
      ├─ conductive_path_3d (传导路径 - 固体)
      │   ├─ Walk on Sphere (WoS)
      │   └─ Delta Sphere
      └─ convective_path_3d (对流路径 - 流体)
```

### 3.2 关键函数详解

#### 3.2.1 `trace_radiative_path_3d`

**文件**: `sdis_heat_path_radiative_Xd.h` (line 196-300+)  
**用途**: 追踪光线直到击中边界或辐射环境

**核心循环**:
```c
for(;;) {
  // 1. 查找下一个相交
  res = find_next_fragment_3d(scn, pos, dir, &rwalk->hit_3d,
    rwalk->vtx.time, rwalk->enc_id, &rwalk->hit_3d, &interf, &frag);
  
  // 2. 如果击中辐射环境 → 退出
  if(S3D_HIT_NONE(&rwalk->hit_3d)) {
    res = set_limit_radiative_temperature(scn, ctx, rwalk, dir, branch_id, T);
    break;
  }
  
  // 3. 检查界面有效性 (必须是 fluid/solid 界面)
  res = check_interface(interf, &frag, verbose);
  
  // 4. 设置 BRDF
  res = brdf_setup(scn->dev, &brdf_setup_args, &brdf);
  
  // 5. 采样 BRDF → 决定下一步
  if(ssp_rng_canonical(rng) < brdf.emissivity) {
    T->func = boundary_path_3d;  // 切换到边界路径
    break;
  }
  
  // 6. 采样反射/折射方向
  res = brdf_sample(brdf, wi, rng, &bounce);
  d3_set(dir, bounce.wo);  // 更新方向
}
```

**GPU并行化潜力**: ⭐⭐⭐⭐⭐ (每条光线完全独立)

---

#### 3.2.2 `find_next_fragment_3d`

**用途**: 查找光线与几何的下一个相交点

**伪代码**:
```c
res_T find_next_fragment_3d(...) {
  // 1. 调用 star-3d 库的射线追踪
  trace_ray_3d(scn, in_pos, in_dir, FLT_MAX, enc_id, in_hit, out_hit);
  
  // 2. 如果未相交 → 返回
  if(S3D_HIT_NONE(out_hit)) return RES_OK;
  
  // 3. 获取界面信息
  *out_interf = scene_get_interface(scn, out_hit->prim.scene_prim_id);
  
  // 4. 构建界面片段
  setup_interface_fragment_3d(out_frag, &vtx, out_hit, side);
  
  return RES_OK;
}
```

**关键依赖**: `s3d_scene_view_trace_ray` (star-3d库)

---

#### 3.2.3 `sample_coupled_path_3d`

**用途**: 处理多物理耦合 (传导、对流、辐射)

**伪代码**:
```c
res_T sample_coupled_path_3d(...) {
  while(!T->done) {
    // 调用当前温度状态指定的函数
    res = T->func(scn, ctx, rwalk, rng, T);
    
    // 如果失败,重试最多 MAX_FAILS 次
    if(res == RES_BAD_OP) { 
      *rwalk = rwalk_bkp; 
      *T = T_bkp; 
    }
  }
  return res;
}
```

**GPU移植挑战**: 函数指针 `T->func` 需要改为枚举

---

## 4. 数据流图

```
输入: ray_realisation_args
  │
  ├─ position[3], direction[3], time
  ├─ rng (随机数生成器)
  └─ enc_id (包壳ID)
  │
  ↓
初始化 rwalk 和 ctx
  │
  ↓
trace_radiative_path_3d ← 核心计算
  │
  ├─→ find_next_fragment_3d
  │     ├─→ s3d_scene_view_trace_ray (射线追踪)
  │     └─→ 返回 s3d_hit + interface_fragment
  │
  ├─→ brdf_setup (查询材质属性)
  │     └─→ 返回 emissivity, specular_fraction
  │
  ├─→ brdf_sample (采样BRDF)
  │     └─→ 返回下一个方向 wo[3]
  │
  └─→ 循环直到击中边界或辐射环境
  │
  ↓
如果 !T.done → sample_coupled_path_3d
  │
  ├─→ boundary_path_3d (边界条件)
  ├─→ conductive_path_3d (固体传导)
  └─→ convective_path_3d (流体对流)
  │
  ↓
输出: weight (蒙特卡洛权重 = 温度估计)
```

---

## 5. GPU移植建议

### 5.1 数据结构转换

| CPU结构 | GPU挑战 | 解决方案 |
|---------|---------|----------|
| `temperature::func` (函数指针) | ❌ GPU不支持 | 枚举 + switch语句 |
| `sdis_heat_path` (动态数组) | ❌ 动态分配 | 预分配固定大小数组 |
| `sdis_scene` (指针森林) | ❌ 多层指针 | 扁平化数组 + 索引 |
| `s3d_primitive::shape__` | ❌ 指针 | 转换为`shape_index` |
| `rwalk/rwalk_context` | ✅ 结构体扁平 | 直接复制到GPU |

### 5.2 计算着色器设计

**HLSL伪代码** (DX12 Compute Shader):
```hlsl
// 常量缓冲区
cbuffer RayRealisationParams : register(b0) {
  float3 camera_position;
  float3 camera_direction;
  float time;
  float Tmin, Tmax;
  uint picard_order;
};

// 几何数据 (结构化缓冲区)
StructuredBuffer<Triangle> triangles : register(t0);
StructuredBuffer<Material> materials : register(t1);

// 输出缓冲区
RWStructuredBuffer<float> output_weights : register(u0);

[numthreads(8, 8, 1)]
void RayRealisationCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
  uint pixelIndex = dispatchThreadID.y * imageWidth + dispatchThreadID.x;
  
  // 初始化 RNG (每线程独立)
  uint rng_state = InitRNG(pixelIndex, frameCount);
  
  // 初始化 rwalk
  RWalk rwalk;
  rwalk.position = camera_position;
  rwalk.direction = camera_direction;
  rwalk.time = time;
  
  // 追踪辐射路径
  Temperature T = TraceRadiativePath(rwalk, rng_state);
  
  // 如果未完成,采样耦合路径
  if(!T.done) {
    T = SampleCoupledPath(rwalk, rng_state, T);
  }
  
  output_weights[pixelIndex] = T.value;
}
```

### 5.3 DXR光线追踪集成

**推荐架构**:
1. **Compute Shader** 处理蒙特卡洛采样逻辑
2. **DXR Ray Generation Shader** 调用硬件加速的射线追踪
3. **BLAS/TLAS** 加速结构由DX12管理

**Ray Generation Shader示例**:
```hlsl
RaytracingAccelerationStructure Scene : register(t0);

[shader("raygeneration")]
void TraceRay_RGS()
{
  RayDesc ray;
  ray.Origin = rwalk.position;
  ray.Direction = rwalk.direction;
  ray.TMin = 0.001f;
  ray.TMax = 10000.0f;
  
  HitInfo payload;
  TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
  
  // payload 包含 hit distance, normal, material_id
}
```

### 5.4 内存布局优化

**SoA (Structure of Arrays) 替代 AoS**:

```c
// CPU (AoS) - 不利于GPU合并访问
struct rwalk {
  double P[3];
  double time;
  uint enc_id;
  struct s3d_hit hit;
};

rwalk rwalk_array[1000];  // 内存跳跃访问

// GPU (SoA) - 适合合并访问
struct rwalk_soa {
  double* positions_x;     // 连续数组
  double* positions_y;
  double* positions_z;
  double* times;
  uint* enc_ids;
  struct s3d_hit_soa hits;
};
```

### 5.5 精度验证

**GPU/CPU一致性检查**:
1. 使用固定种子RNG
2. 单个像素单采样对比
3. 容差: `abs(gpu - cpu) < 1e-6`

---

## 6. 性能预估

### 6.1 CPU基准

- **单线程**: ~10-100 rays/sec (取决于场景复杂度)
- **OpenMP (16核)**: ~100-1000 rays/sec

### 6.2 GPU目标

- **RTX 4090**: 10240 CUDA核心
- **预期加速比**: 50-100x
- **目标**: 10,000-50,000 rays/sec

### 6.3 瓶颈分析

| 操作 | CPU占比 | GPU加速比 | 备注 |
|------|---------|-----------|------|
| 射线追踪 | 40% | 100x | DXR硬件加速 |
| BRDF采样 | 20% | 80x | 纯数学计算 |
| 边界条件 | 15% | 50x | 条件分支较多 |
| 传导/对流 | 20% | 30x | 递归逻辑复杂 |
| 内存拷贝 | 5% | 1x | CPU↔GPU瓶颈 |

---

## 7. 依赖库清单

### 7.1 内部库

| 库名 | 版本 | 用途 | GPU移植难度 |
|------|------|------|-------------|
| `star-3d` | 0.10 | 3D射线追踪 | ⭐⭐⭐⭐ 高 (需要重写BVH) |
| `star-sp` | 0.12 | 采样和RNG | ⭐⭐ 中 (RNG需要GPU友好版本) |
| `rsys` | 0.15 | 基础工具 | ⭐ 低 (仅用简单函数) |
| `senc3d` | 0.5 | 3D包壳 | ⭐⭐⭐ 中高 (几何处理) |

### 7.2 关键函数依赖

**射线追踪**:
- `s3d_scene_view_trace_ray` (star-3d)
- `s3d_scene_view_closest_point` (star-3d)

**采样**:
- `ssp_rng_canonical` (star-sp) - 均匀分布[0,1)
- `ssp_sample_hemisphere_cosine` (star-sp) - 余弦加权半球采样

**几何**:
- `d3_dot`, `d3_cross`, `d3_normalize` (rsys)

---

## 8. 测试和验证

### 8.1 单元测试

**关键测试点**:
1. 射线-三角形相交精度
2. BRDF采样分布正确性
3. 温度边界条件处理
4. Picard迭代收敛性

### 8.2 集成测试

**场景复杂度梯度**:
1. 立方体 + 单光源 (100三角形)
2. 斯坦福兔子 + 多光源 (10K三角形)
3. 复杂机械 (100K+三角形)

### 8.3 验证标准

```python
def verify_gpu_cpu_consistency():
  for pixel in image:
    gpu_weight = ray_realisation_3d_gpu(pixel)
    cpu_weight = ray_realisation_3d_cpu(pixel)
    
    assert abs(gpu_weight - cpu_weight) < 1e-6, \
      f"Pixel {pixel}: GPU={gpu_weight}, CPU={cpu_weight}"
```

---

## 9. 附录

### 9.1 数据结构大小

| 结构体 | 大小 (字节) | 对齐 | GPU友好度 |
|--------|-------------|------|-----------|
| `ray_realisation_args` | 88 | 8 | ⚠️ (有指针) |
| `rwalk` | 128 | 8 | ✅ |
| `rwalk_context` | 96 | 8 | ⚠️ (有指针) |
| `temperature` | 24 | 8 | ❌ (函数指针) |
| `s3d_hit` | 48 | 4 | ⚠️ (有指针) |
| `sdis_interface_fragment` | 64 | 8 | ✅ |

### 9.2 关键常量

```c
#define SDIS_TEMPERATURE_NONE NaN
#define SDIS_XD_DIMENSION 3
#define S3D_HIT_NULL_DISTANCE FLT_MAX
#define MAX_FAILS 1  // 最大重试次数
```

### 9.3 代码位置速查

| 功能 | 文件 | 行号 |
|------|------|------|
| `ray_realisation_3d` 实现 | `sdis_realisation.c` | 57-108 |
| `trace_radiative_path_3d` | `sdis_heat_path_radiative_Xd.h` | 196-300+ |
| `rwalk` 定义 | `sdis_heat_path.h` | 90-111 |
| `s3d_hit` 定义 | `star-3d/0.10/src/s3d.h` | 128-143 |
| `ray_realisation_args` 定义 | `sdis_realisation.h` | 183-193 |

---

## 10. 参考资料

1. **论文**: [Monte-Carlo algorithms for thermal simulations](https://doi.org/10.1371/journal.pone.0283681)
2. **README**: `stardis-cpu-cuda_impl_analysis/README.md`
3. **实现文档**: `GPU_IMPLEMENTATION_FEASIBILITY_ANALYSIS.md`
4. **star-3d文档**: `star-3d/0.10/README.md`

---

**文档版本**: 1.0  
**最后更新**: 2026-01-21 19:42  
**作者**: Sisyphus (AI Agent)  
**审核状态**: 待人类确认
