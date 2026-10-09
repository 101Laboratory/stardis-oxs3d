# oxstar-3d 架构设计文档

**版本**: 0.1  
**日期**: 2026-02-20  
**状态**: 设计方案  
**目标**: 基于 NVIDIA OptiX 7 实现 star-3d 兼容的光线追踪后端

---

## 1. 项目定位

### 1.1 背景

`custar-3d` 项目已成功将 Embree (CPU) 光追后端迁移到 CUDA + cuBQL (GPU)，并保持了与原 `star-3d` 完全一致的 `s3d.h` 公共 C API（~70 个函数）。

`oxstar-3d` 是第四种后端实现，使用 **NVIDIA OptiX 7+** 作为底层光追引擎，面向 NVIDIA GPU 的硬件 RT Core 加速。

### 1.2 目标

- **对外接口 100% 一致**: 复用 `s3d.h` 定义的全部公共 API（~70 个函数），上层求解器代码**零修改**
- **功能 100% 一致**: 射线追踪、最近点查询、包壳定位、批量操作、实例化、球体支持等全部能力
- **OptiX 硬件加速**: 利用 NVIDIA RT Core 硬件加速的 BVH 构建/遍历/三角形交叉
- **与 custar-3d 互补**: 共享 CUDA 生态，OptiX 提供硬件 BVH 替代 cuBQL 软件 BVH

### 1.3 与现有后端的关系

```
                ┌─────────────────────────────────────────┐
                │         s3d.h (公共 C API，不变)          │
                │  ~70 个函数 + 结构体/枚举/常量定义         │
                └──────────────────┬──────────────────────┘
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        │               │                │                    │
┌───────▼────────┐ ┌────▼──────────┐ ┌───▼───────────┐ ┌─────▼──────────┐
│  star-3d (CPU) │ │ custar-3d     │ │ dxrstar-3d    │ │ oxstar-3d      │
│  Embree 后端    │ │ CUDA+cuBQL    │ │ DXR 后端      │ │ OptiX 后端     │
│  原始实现       │ │ 已完成        │ │ 设计中        │ │ 本文档目标     │
└────────────────┘ └───────────────┘ └───────────────┘ └────────────────┘
```

### 1.4 适用范围

| 维度 | custar-3d | dxrstar-3d | oxstar-3d |
|------|-----------|------------|-----------|
| **GPU 平台** | NVIDIA only (CUDA) | NVIDIA+AMD+Intel (DX12) | NVIDIA only (OptiX) |
| **操作系统** | Windows + Linux | Windows only | Windows + Linux |
| **光追 API** | cuBQL (软件 BVH) | DXR 1.1 (硬件 RT Core) | OptiX 7 (硬件 RT Core) |
| **计算着色** | CUDA Kernel | Compute Shader (HLSL) | CUDA Kernel |
| **内存模型** | CUDA Device Memory | D3D12 Resources | CUDA Device Memory |
| **构建系统** | CUDA + CMake | HLSL (dxc) + CMake | CUDA (nvcc/ptx) + CMake |
| **调试工具** | Nsight, cuda-gdb | PIX, Nsight Graphics | Nsight, cuda-gdb |
| **双精度** | 原生支持 | HLSL double (SM 6.0+) | CUDA 原生支持 |
| **BVH 控制** | 完全自定义 (cuBQL) | 驱动管理 (DXR AS) | 驱动管理 (OptiX GAS/IAS) |

### 1.5 为什么选择 OptiX

| 优势 | 说明 |
|------|------|
| **硬件 RT Core** | OptiX 直接驱动 NVIDIA RT Core，BVH 遍历和三角形交叉由硬件加速 |
| **与 CUDA 无缝集成** | OptiX 运行在 CUDA 上下文中，与现有 custar-3d 的 CUDA 代码可以共享内存和流 |
| **跨平台** | Windows + Linux 均支持，与 DXR 的 Windows-only 限制不同 |
| **成熟的 API** | OptiX 7 API 稳定成熟，文档丰富，社区活跃 |
| **自定义交叉** | 原生支持 Intersection Program，球体自定义交叉效率高 |
| **多级实例化** | 原生 IAS/GAS 两级结构，天然支持实例化 |
| **CUDA 内核协作** | 非射线追踪操作（closest point 等）直接复用 CUDA kernel |

---

## 2. 总体架构

### 2.1 层次图

```
┌───────────────────────────────────────────────────────────────┐
│                  s3d.h (公共 C API，C89 兼容)                   │  ← 用户面
├───────────────────────────────────────────────────────────────┤
│              不变的主机端模块 (纯 CPU，与后端无关)                │
│   s3d_scene.cpp   s3d_shape.cpp   s3d_mesh.cpp                │
│   s3d_sphere.cpp  s3d_instance.cpp s3d_primitive.cpp           │
│   (场景图管理, 引用计数, CDF 计算, AABB 计算)                    │
├───────────────────────────────────────────────────────────────┤
│              场景视图协调器 (桥接 CPU/GPU，与 custar-3d 同构)     │
│   s3d_scene_view.cpp                (核心同步流水线)             │
│   s3d_scene_view_trace_ray.cpp      (单射线 + Filter)          │
│   s3d_scene_view_batch_trace.cpp    (批量射线)                  │
│   s3d_scene_view_closest_point.cpp  (单点/批量最近点)            │
│   s3d_scene_view_find_enclosure.cpp (批量包壳定位)               │
├───────────────────────────────────────────────────────────────┤
│                 OptiX + CUDA 模块层                             │
│   ┌───────────┐ ┌──────────┐ ┌──────────────────┐             │
│   │ oxs3d_    │ │ oxs3d_   │ │ oxs3d_           │             │
│   │ device    │ │ mem      │ │ geom_store       │             │
│   │ OptiX上下文│ │ GPU资源   │ │ 平坦化几何数据    │             │
│   │ Pipeline  │ │ 上传/回读  │ │ GAS Build Input  │             │
│   └─────┬─────┘ └────┬─────┘ └───────┬──────────┘             │
│         │            │               │                         │
│   ┌─────┴────────────┴───────────────┴─────────────────┐       │
│   │ oxs3d_accel         (加速结构管理: GAS + IAS)        │       │
│   │ ├─ GAS (Geometry AS，per-shape 或 merged)            │       │
│   │ ├─ IAS (Instance AS，实例化)                          │       │
│   │ └─ Build / Update / Compact                          │       │
│   └────────────────┬───────────────────────────────────┘       │
│         ┌──────────┼──────────────┐                            │
│   ┌─────┴─────┐ ┌──┴───────┐ ┌───┴───────────────┐            │
│   │ oxs3d_    │ │ oxs3d_   │ │ oxs3d_            │            │
│   │ trace     │ │ closest_ │ │ find_enclosure    │            │
│   │ OptiX     │ │ point    │ │ OptiX或CUDA       │            │
│   │ Launch    │ │ CUDA Kern│ │ Kernel            │            │
│   └─────┬─────┘ └──────────┘ └───────────────────┘            │
│         │                                                      │
│   ┌─────┴───────────┐                                          │
│   │ oxs3d_prim      │  oxs3d_trace_util.h (UV/法线修正)         │
│   │ 命中结果转换      │  oxs3d_math.cuh (设备端数学)             │
│   └─────────────────┘                                          │
├───────────────────────────────────────────────────────────────┤
│   外部依赖:                                                     │
│     CUDA Runtime | OptiX 7.x SDK (header-only)                 │
│     rsys (基础运行时)                                            │
└───────────────────────────────────────────────────────────────┘
```

### 2.2 核心设计原则

1. **模块 1:1 映射**: `oxs3d_*` 模块与 `cus3d_*` 模块一一对应，保持相同的职责边界
2. **接口签名一致**: 内部 C/C++ API 签名与 `cus3d_*` 尽可能保持一致（仅替换 cuBQL 特定类型）
3. **CUDA 生态共享**: OptiX 运行在 CUDA 之上，`oxs3d_mem` 直接复用 `cus3d_mem` 的 CUDA 内存管理
4. **过滤器 CPU 端执行**: 延续 custar-3d 的 Top-K + CPU Filter 策略
5. **GAS/IAS 天然两级**: 利用 OptiX 原生的 GAS + IAS 替代 cuBQL 手动两级 BVH

---

## 3. OptiX 关键概念映射

### 3.1 cuBQL → OptiX 概念映射

| cuBQL 概念 | OptiX 等价 | 说明 |
|-----------|----------|------|
| `cuBQL::BinaryBVH<float,3>` | `OptixTraversableHandle` (GAS/IAS) | OptiX 加速结构是不透明的驱动管理对象 |
| `cuBQL::gpuBuilder()` | `optixAccelBuild()` | GPU 上构建加速结构 |
| `cuBQL::BuildConfig` | `OptixAccelBuildOptions` | 构建参数（质量/flag 等） |
| `cuBQL::shrinkingRayQuery::forEachPrim()` | `optixTrace()` + CH/AH/IS Programs | BVH 遍历由 OptiX 管线驱动 |
| `cuBQL::free()` | `cudaFree(d_gas_buffer)` | 释放 AS 占用的 device memory |
| `cuBQL::box_t<float,3>` | `OptixAabb` (struct) | 自定义几何 AABB |
| 单级 BVH (BLAS) | GAS (Geometry Acceleration Structure) | 几何加速结构 |
| 两级 BVH (BLAS + TLAS) | GAS + IAS (Instance Acceleration Structure) | OptiX 原生支持 |
| 伪实例 (Pseudo Instance) | `OptixInstance` with identity transform | IAS 中的实例描述 |

### 3.2 CUDA → OptiX 资源映射

| CUDA 概念 | OptiX 等价 | 说明 |
|-----------|-----------|------|
| `cudaStream_t` | `CUstream` (相同) | OptiX launch 在 CUDA stream 上执行 |
| `cudaMalloc` / `cudaFree` | 同上 | OptiX 共享 CUDA 内存 |
| `__global__ kernel<<<>>>` | `optixLaunch()` | OptiX pipeline 启动 |
| CUDA Device Memory | `CUdeviceptr` | OptiX 使用 `CUdeviceptr` 指向 device memory |
| `blockIdx * blockDim + threadIdx` | `optixGetLaunchIndex()` | 在 OptiX program 中获取线程索引 |

### 3.3 几何数据格式映射

| cus3d / cuBQL | OptiX | 说明 |
|-------------|-------|------|
| `float3` 顶点数组 | `OptixBuildInputTriangleArray` 的 vertex buffer | OptiX 原生三角形格式 |
| `uint3` 索引数组 | `OptixBuildInputTriangleArray` 的 index buffer | 32-bit 索引 |
| 球体 (`center + radius`) | `OptixBuildInputCustomPrimitiveArray` | 自定义几何 + Intersection Program |
| `geom_entry` 元数据 | SBT (Shader Binding Table) record data | 每个 geometry 的用户数据 |
| `prim_to_geom` 映射 | `optixGetSbtGASIndex()` + SBT record | GAS 内的 SBT 索引即为 geometry 索引 |

---

## 4. 与 custar-3d 的关键差异

### 4.1 加速结构管理

**custar-3d**: 手动管理 cuBQL BinaryBVH，需要显式构建子 BVH、管理伪实例、手动两级遍历。  
**oxstar-3d**: OptiX 原生支持 GAS/IAS 两级结构，**极大简化实例化代码**。

```
custar-3d (手动两级遍历):                oxstar-3d (OptiX 原生两级):
┌─────────────────────┐                  ┌─────────────────────┐
│ 手动构建主 BLAS       │                  │ optixAccelBuild GAS  │
│ 手动构建子 BVH (N个)  │                  │   per geometry       │
│ 手动组装 TLAS         │                  │ optixAccelBuild IAS  │
│ 手动管理 tlas_to_orig │                  │   OptixInstance[]    │
│ 伪实例机制            │                  │   (OptiX 原生实例化)  │
│ 内核内手动二级遍历     │                  │ optixTrace 自动遍历  │
└─────────────────────┘                  └─────────────────────┘
```

### 4.2 射线追踪编程模型

**custar-3d**: CUDA Kernel 内手写 BVH 遍历 + Möller-Trumbore 三角形交叉。  
**oxstar-3d**: OptiX Pipeline 管线驱动 — 通过 Program Groups (RayGen/ClosestHit/AnyHit/Miss/Intersection) 定义行为。

```
custar-3d (CUDA Kernel + cuBQL):
  __global__ void trace_rays_kernel(...) {
      // 手写 BVH 遍历
      cuBQL::shrinkingRayQuery::forEachPrim(
          intersect_lambda, bvh, org, tmax);
  }

oxstar-3d (OptiX Pipeline):
  // RayGen Program: 发射射线
  extern "C" __global__ void __raygen__trace() {
      float3 org = params.origins[idx];
      float3 dir = params.directions[idx];
      optixTrace(params.handle, org, dir, tmin, tmax, ...);
  }

  // ClosestHit Program: 处理最近命中
  extern "C" __global__ void __closesthit__triangle() {
      // 读取三角形 UV / 法线, 写入 payload
  }

  // Intersection Program: 球体自定义交叉
  extern "C" __global__ void __intersection__sphere() {
      // 解析球体几何, 计算交叉, 调用 optixReportIntersection()
  }
```

### 4.3 OptiX Pipeline vs Inline RT 的选择

OptiX 7 同时支持 **Pipeline 模式** (optixTrace + Program Groups) 和在 CUDA kernel 中直接调用 `optixTraverse()` (OptiX 8.1+ 的 Inline PT)。考虑到：

1. **OptiX 7.x 的主流稳定性** — Pipeline 模式是 OptiX 7 的标准模式
2. **球体自定义交叉** — 需要 Intersection Program, Pipeline 模式天然支持
3. **Top-K 多命中** — 通过 AnyHit Program 实现 K-nearest collection
4. **与 custar-3d 的对应性** — Pipeline 的 RayGen 相当于 CUDA launch kernel

**结论: 使用 OptiX Pipeline 模式 (`optixTrace` + Program Groups)**。

### 4.4 球体支持

**custar-3d**: 球体与三角形统一编排在同一连续数组中，通过 `primID < tri_count` 区分。  
**oxstar-3d**: OptiX 支持两种 Build Input：
- **三角形**: `OPTIX_BUILD_INPUT_TYPE_TRIANGLES` — 硬件加速交叉
- **自定义 AABB**: `OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES` — 配合 Intersection Program

球体映射为 Custom Primitive + Intersection Program (`__intersection__sphere`)。

### 4.5 内存管理

**custar-3d**: 5 种类型化 `gpu_buffer_<T>` 封装 `cudaMalloc` / `cudaFree`。  
**oxstar-3d**: **完全复用** `cus3d_mem.h` 的 GPU buffer 管理。OptiX 运行在 CUDA 之上，`CUdeviceptr` 与 CUDA device pointer 完全兼容。

### 4.6 Closest Point Query

**custar-3d**: 使用 cuBQL 的 `shrinkingRadiusQuery::forEachPrim` 实现。  
**oxstar-3d**: OptiX 没有原生最近点查询。两种策略：

- **策略 A (推荐): 复用 cuBQL 最近点** — 构建一份 cuBQL BVH 仅用于 closest point（代码复用最大）
- **策略 B: 射线方法近似** — 用多方向射线从查询点出发做近似搜索（不精确）
- **策略 C: 自建 BVH 搜索** — CUDA kernel 手写 BVH 最近点遍历（开发量大）

**推荐策略 A**: oxstar-3d 同时持有 OptiX AS (用于射线追踪) 和 cuBQL BVH (用于 closest point / enclosure)。两个 BVH 共享底层几何数据（顶点/索引 GPU buffer），仅 BVH 索引结构不同。

---

## 5. 模块详细设计

### 5.1 模块对照表

| # | custar-3d 模块 | oxstar-3d 模块 | 文件 | 替换内容 |
|---|---------------|---------------|------|---------|
| 1 | `cus3d_device` | `oxs3d_device` | `.h/.cpp` | CUDA ctx + OptiX DeviceContext |
| 2 | `cus3d_mem` | `oxs3d_mem` (复用) | `.h/.cpp` | **完全复用** cus3d_mem |
| 3 | `cus3d_types` | `oxs3d_types` | `.h` | 类型定义 (大部分复用) |
| 4 | `cus3d_geom_store` | `oxs3d_geom_store` | `.h/.cpp/.cu` | 几何平坦化 + OptiX Build Input |
| 5 | `cus3d_bvh` | `oxs3d_accel` | `.h/.cpp` | cuBQL BVH → OptiX GAS/IAS |
| 6 | `cus3d_trace` | `oxs3d_trace` | `.h/.cpp` + `.cu` | CUDA kernel → OptiX Pipeline |
| 7 | `cus3d_closest_point` | `oxs3d_closest_point` | `.h/.cpp/.cu` | 复用 cuBQL (策略A) 或 CUDA kernel |
| 8 | `cus3d_find_enclosure` | `oxs3d_find_enclosure` | `.h/.cpp` + `.cu` | OptiX多射线或复用cuBQL |
| 9 | `cus3d_prim` | `oxs3d_prim` (复用) | `.h/.cpp` | **完全复用** cus3d_prim |
| 10 | `cus3d_trace_util` | `oxs3d_trace_util` (复用) | `.h` | **完全复用** UV/法线修正 |
| 11 | `cus3d_math.cuh` | `oxs3d_math.cuh` | `.cuh` | 复用大部分数学函数 |
| 12 | — | `oxs3d_pipeline` (新增) | `.h/.cpp` | OptiX Pipeline/SBT 管理 |
| 13 | — | `oxs3d_programs.cu` (新增) | `.cu` | OptiX Device Programs (PTX) |

---

## 6. 各模块接口设计

### 6.1 Module 1: Device & OptiX Context (`oxs3d_device`)

**文件**: `oxs3d_device.h`, `oxs3d_device.cpp`  
**替换**: `cus3d_device`  
**额外职责**: 初始化 OptiX DeviceContext

```cpp
// oxs3d_device.h
#ifndef OXS3D_DEVICE_H
#define OXS3D_DEVICE_H

#include <cuda_runtime.h>
#include <optix.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RES_OK
  typedef int res_T;
  #define RES_OK       0
  #define RES_BAD_ARG  1
  #define RES_MEM_ERR  2
  #define RES_ERR      (-1)
#endif

struct oxs3d_device {
    /* CUDA 部分 (与 cus3d_device 对齐) */
    int            cuda_device_id;
    cudaStream_t   stream;
    cudaStream_t   transfer_stream;
    size_t         total_mem;
    int            sm_count;
    int            max_threads_per_block;

    /* OptiX 部分 (新增) */
    OptixDeviceContext  optix_ctx;        /* OptiX 设备上下文 */
};

res_T oxs3d_device_create(int device_id, struct oxs3d_device** out);
void  oxs3d_device_destroy(struct oxs3d_device* dev);
void  oxs3d_device_sync(struct oxs3d_device* dev);
const char* oxs3d_get_last_error(void);

#ifdef __cplusplus
}
#endif
#endif /* OXS3D_DEVICE_H */
```

**实现要点:**
1. 继承 `cus3d_device_create()` 的全部 CUDA 初始化逻辑
2. 额外调用 `optixInit()` + `optixDeviceContextCreate()` 创建 OptiX 上下文
3. 设置 OptiX 日志回调对接 `log_error` / `log_warning`

### 6.2 Module 2: GPU Memory Manager (`oxs3d_mem`)

**完全复用** `cus3d_mem.h` / `cus3d_mem.cpp`。OptiX 与 CUDA 共享内存空间，所有 `gpu_buffer_*` 类型和操作不需要修改。仅在 `#include` 路径上做别名。

```cpp
// oxs3d_mem.h — 直接转发
#ifndef OXS3D_MEM_H
#define OXS3D_MEM_H
#include "cus3d_mem.h"   /* 完全复用 */
#endif
```

### 6.3 Module 3: Common Types (`oxs3d_types`)

大部分类型与 `cus3d_types.h` 完全一致。新增 OptiX 特定的加速结构描述。

```cpp
// oxs3d_types.h
#ifndef OXS3D_TYPES_H
#define OXS3D_TYPES_H

#include "cus3d_types.h"  /* 复用 prim_type, box3f, sphere_gpu,
                             cus3d_hit_result, cus3d_multi_hit_result,
                             geom_gpu_entry, instance_gpu_data,
                             cus3d_build_quality 等全部类型 */

/* OptiX 特定: SBT Record 数据 */
struct oxs3d_hit_group_data {
    /* 当前 geometry 在 geom_store 中的索引 */
    uint32_t    geom_idx;
    /* 指向全局 GPU 数据的指针 (device pointers) */
    float3*     vertices;
    uint3*      indices;
    struct sphere_gpu*  spheres;
    struct geom_gpu_entry* geom_entries;
    uint32_t    tri_count;
    int         flip_surface;
    int         is_enabled;
};

/* OptiX Launch Parameters (通过 optixLaunch 传递) */
struct oxs3d_launch_params {
    OptixTraversableHandle handle;       /* 顶层 AS */
    /* 射线数据 (SoA, device pointers) */
    float3*     ray_origins;
    float3*     ray_directions;
    float2*     ray_ranges;
    uint32_t    num_rays;
    /* 输出 (device pointer) */
    struct cus3d_hit_result*  results;
    /* 几何元数据 */
    struct geom_gpu_entry*    geom_entries;
    unsigned int*             prim_to_geom;
    uint32_t                  tri_count;
};

#endif /* OXS3D_TYPES_H */
```

### 6.4 Module 4: Geometry Data Store (`oxs3d_geom_store`)

**文件**: `oxs3d_geom_store.h`, `oxs3d_geom_store.cpp`, `oxs3d_geom_store.cu`  
**基于**: `cus3d_geom_store` — 继承平坦化几何数据管理，额外生成 OptiX Build Input

**与 cus3d_geom_store 的差异:**

| 功能 | cus3d_geom_store | oxs3d_geom_store |
|------|-----------------|-----------------|
| 几何平坦化 | ✅ 相同 | ✅ 相同 |
| GPU buffer 管理 | ✅ 相同 | ✅ 相同 |
| AABB 计算 | CUDA kernel → `d_boxes` | 三角形无需(OptiX原生); 球体需→`d_sphere_aabbs` |
| Build Input 生成 | 无 | 新增: 生成 `OptixBuildInput` 数组 |

```cpp
// oxs3d_geom_store.h
#ifndef OXS3D_GEOM_STORE_H
#define OXS3D_GEOM_STORE_H

#include "oxs3d_device.h"
#include "cus3d_mem.h"
#include "cus3d_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct s3d_scene;
struct s3d_shape;

#ifndef S3D_H
typedef int (*s3d_hit_filter_function_T)(
    const struct s3d_hit*, const float*, const float*, const float*, void*, void*);
#endif

/* 复用 cus3d_geom_store 的 geom_entry 定义 */

struct oxs3d_geom_store {
    /* ---- 主机端元数据 (与 cus3d_geom_store 完全一致) ---- */
    struct geom_entry*   entries;
    size_t               entry_count;
    size_t               entry_capacity;

    uint32_t             total_tris;
    uint32_t             total_spheres;
    uint32_t             total_prims;
    uint32_t             total_verts;

    /* ---- GPU 几何数据 (与 cus3d_geom_store 一致) ---- */
    struct gpu_buffer_float3   d_vertices;
    struct gpu_buffer_uint3    d_indices;
    struct gpu_buffer_box3f    d_boxes;     /* 仅球体需要; 三角形 OptiX 自动计算 */

    struct sphere_gpu*         d_spheres;
    size_t                     d_spheres_capacity;

    struct gpu_buffer_uint32   d_prim_to_geom;
    struct geom_gpu_entry*     d_geom_entries;
    size_t                     d_geom_entries_capacity;

    /* ---- OptiX 特有: 球体 AABB buffer ---- */
    CUdeviceptr                d_sphere_aabbs;   /* OptixAabb[] for custom prims */
    size_t                     d_sphere_aabbs_capacity;

    /* ---- Dirty tracking ---- */
    int                  needs_rebuild;
};

res_T oxs3d_geom_store_create(struct oxs3d_geom_store** store);
void  oxs3d_geom_store_destroy(struct oxs3d_geom_store* store,
                               struct oxs3d_device* dev);

res_T oxs3d_geom_store_sync(
    struct oxs3d_geom_store* store,
    struct s3d_scene* scene,
    struct oxs3d_device* dev);

/* 计算球体 AABB (用于 Custom Primitive Build Input) */
res_T oxs3d_geom_store_compute_sphere_aabbs(
    struct oxs3d_geom_store* store,
    struct oxs3d_device* dev);

/* 查找 geom_entry */
const struct geom_entry* oxs3d_geom_store_lookup(
    const struct oxs3d_geom_store* store,
    uint32_t primID);

#ifdef __cplusplus
}
#endif
#endif /* OXS3D_GEOM_STORE_H */
```

### 6.5 Module 5: Acceleration Structure (`oxs3d_accel`)

**文件**: `oxs3d_accel.h`, `oxs3d_accel.cpp`  
**替换**: `cus3d_bvh` (cuBQL BVH → OptiX GAS/IAS)

这是与 custar-3d 差异最大的模块。

```cpp
// oxs3d_accel.h
#ifndef OXS3D_ACCEL_H
#define OXS3D_ACCEL_H

#include <optix.h>
#include "oxs3d_device.h"
#include "oxs3d_geom_store.h"
#include "cus3d_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct geometry;

struct oxs3d_accel {
    /* ---- GAS (Geometry AS) ---- */
    OptixTraversableHandle   gas_handle;
    CUdeviceptr              d_gas_buffer;
    size_t                   gas_buffer_size;

    /* ---- IAS (Instance AS, 用于实例场景) ---- */
    OptixTraversableHandle   ias_handle;
    CUdeviceptr              d_ias_buffer;
    size_t                   ias_buffer_size;
    CUdeviceptr              d_instances;     /* OptixInstance[] */
    uint32_t                 instance_count;

    /* ---- 每实例子 GAS ---- */
    struct oxs3d_instance_entry {
        OptixTraversableHandle  child_gas;
        CUdeviceptr             d_child_gas_buffer;
        size_t                  child_gas_size;
        float                   transform[12];
        float                   inv_transform[12];
        struct oxs3d_geom_store* child_store;
        struct geometry*         inst_geom;   /* s3d 实例指针回溯 */
    };
    struct oxs3d_instance_entry* instance_entries;
    size_t                       instance_entries_count;
    size_t                       instance_entries_capacity;

    /* ---- 辅助 cuBQL BVH (用于 closest point / enclosure) ---- */
    void*                    cubql_bvh;       /* cus3d_bvh* 的 opaque 指针 */

    /* ---- 顶层 handle (GAS 或 IAS，取决于是否有实例) ---- */
    OptixTraversableHandle   top_handle;

    int                      valid;
    float                    lower[3], upper[3]; /* 场景 AABB */
};

/* ---- 构建质量映射 ---- */
/* 复用 cus3d_build_quality 枚举 */

/* ---- 生命周期 ---- */
res_T oxs3d_accel_create(struct oxs3d_accel** accel);
void  oxs3d_accel_destroy(struct oxs3d_accel* accel, struct oxs3d_device* dev);

/* ---- GAS 构建 ---- */
res_T oxs3d_accel_build_gas(
    struct oxs3d_accel* accel,
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev,
    cus3d_build_quality quality);

/* ---- 实例 GAS 构建 ---- */
res_T oxs3d_accel_build_instance_gas(
    struct oxs3d_accel* accel,
    uint32_t instance_idx,
    const struct oxs3d_geom_store* child_store,
    struct oxs3d_device* dev);

/* ---- 实例变换设置 ---- */
res_T oxs3d_accel_set_instance_transform(
    struct oxs3d_accel* accel,
    uint32_t instance_idx,
    const float forward[12],
    const float inverse[12]);

/* ---- IAS 构建 ---- */
res_T oxs3d_accel_build_ias(
    struct oxs3d_accel* accel,
    struct oxs3d_device* dev);

/* ---- 伪实例注册 ---- */
res_T oxs3d_accel_register_pseudo_instance(
    struct oxs3d_accel* accel,
    uint32_t instance_idx,
    struct oxs3d_geom_store* parent_store);

/* ---- 查询 ---- */
void oxs3d_accel_get_bounds(
    const struct oxs3d_accel* accel,
    float lower[3], float upper[3]);

int oxs3d_accel_is_valid(const struct oxs3d_accel* accel);

/* ---- 实例回溯 ---- */
res_T oxs3d_accel_set_instance_geometry(
    struct oxs3d_accel* accel,
    unsigned instance_idx,
    struct geometry* geom);

struct geometry* oxs3d_accel_get_instance_geometry(
    const struct oxs3d_accel* accel,
    unsigned tlas_idx);

res_T oxs3d_accel_set_instance_child_store(
    struct oxs3d_accel* accel,
    unsigned instance_idx,
    struct oxs3d_geom_store* child_store);

const struct oxs3d_geom_store* oxs3d_accel_get_instance_store(
    const struct oxs3d_accel* accel,
    unsigned tlas_idx);

res_T oxs3d_accel_reset_instances(
    struct oxs3d_accel* accel,
    struct oxs3d_device* dev);

#ifdef __cplusplus
}
#endif
#endif /* OXS3D_ACCEL_H */
```

**GAS 构建流程:**

```
oxs3d_accel_build_gas():
  1. 收集 OptixBuildInput 数组:
     a. 每个 mesh geometry → OptixBuildInput (TRIANGLES)
        - vertexBuffers[] = &store->d_vertices.data + vertex_offset
        - indexBuffer     = store->d_indices.data + prim_offset
        - flags           = OPTIX_GEOMETRY_FLAG_NONE
     b. 每个 sphere geometry → OptixBuildInput (CUSTOM_PRIMITIVES)
        - aabbBuffers[]   = &store->d_sphere_aabbs + offset
        - flags           = OPTIX_GEOMETRY_FLAG_NONE
  2. 设置 OptixAccelBuildOptions (quality 映射):
     LOW    → OPTIX_BUILD_FLAG_PREFER_FAST_BUILD
     MEDIUM → OPTIX_BUILD_FLAG_PREFER_FAST_TRACE (默认)
     HIGH   → OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | ALLOW_COMPACTION
  3. optixAccelComputeMemoryUsage() → 计算 temp/output buffer 大小
  4. cudaMalloc temp/output buffers
  5. optixAccelBuild() → 构建 GAS
  6. (HIGH) optixAccelCompact() → 压缩 GAS
  7. 释放 temp buffer
  8. 存储 gas_handle, d_gas_buffer
  9. 计算场景 AABB (从 optixAccelGetProperty 或从几何数据计算)
```

### 6.6 Module 6: OptiX Pipeline Manager (`oxs3d_pipeline`) — 新增

**文件**: `oxs3d_pipeline.h`, `oxs3d_pipeline.cpp`  
**职责**: 管理 OptiX Pipeline、Module、Program Group、SBT

这是 oxstar-3d 新增的模块，custar-3d/dxrstar-3d 没有对应项。

```cpp
// oxs3d_pipeline.h
#ifndef OXS3D_PIPELINE_H
#define OXS3D_PIPELINE_H

#include <optix.h>
#include "oxs3d_device.h"

#ifdef __cplusplus
extern "C" {
#endif

struct oxs3d_pipeline {
    OptixModule           module;         /* PTX/OptiX IR module */
    OptixPipeline         pipeline;       /* Compiled pipeline */

    /* Program Groups */
    OptixProgramGroup     pg_raygen_trace;       /* 射线追踪 RayGen */
    OptixProgramGroup     pg_raygen_trace_multi; /* Top-K 多命中 RayGen */
    OptixProgramGroup     pg_miss;               /* Miss program */
    OptixProgramGroup     pg_hitgroup_tri;       /* 三角形 ClosestHit + AnyHit */
    OptixProgramGroup     pg_hitgroup_sphere;    /* 球体 Intersection + CH + AH */
    OptixProgramGroup     pg_hitgroup_tri_multi; /* Top-K 三角形 AnyHit */
    OptixProgramGroup     pg_hitgroup_sphere_multi; /* Top-K 球体 AnyHit */

    /* SBT (Shader Binding Table) */
    OptixShaderBindingTable  sbt_trace;       /* 单命中 SBT */
    OptixShaderBindingTable  sbt_trace_multi; /* Top-K 多命中 SBT */

    /* SBT 缓冲 (device memory, 需要随场景更新) */
    CUdeviceptr           d_raygen_record;
    CUdeviceptr           d_miss_record;
    CUdeviceptr           d_hitgroup_records;
    size_t                hitgroup_record_count;
    size_t                hitgroup_record_stride;
};

/* 创建 pipeline (编译 PTX, 创建 program groups) */
res_T oxs3d_pipeline_create(
    struct oxs3d_pipeline* pipe,
    struct oxs3d_device* dev);

/* 销毁 pipeline */
void oxs3d_pipeline_destroy(
    struct oxs3d_pipeline* pipe);

/* 构建/更新 SBT (需要在 GAS 构建后调用，因为 SBT 记录数取决于几何数量) */
res_T oxs3d_pipeline_build_sbt(
    struct oxs3d_pipeline* pipe,
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev);

#ifdef __cplusplus
}
#endif
#endif /* OXS3D_PIPELINE_H */
```

**SBT 布局:**

```
SBT Record Layout:
┌────────────────────────────────┐
│ RayGen Record                  │  1 个
│   header (OPTIX_SBT_RECORD_   │
│           HEADER_SIZE)         │
│   data: launch_params_ptr      │
├────────────────────────────────┤
│ Miss Records                   │  1 个 (per ray type)
│   header + (empty data)        │
├────────────────────────────────┤
│ HitGroup Records               │  N 个 (每个 geometry 一个)
│   [0] type=TRI  geom_idx=0     │
│   [1] type=TRI  geom_idx=1     │
│   ...                          │
│   [M] type=SPH  geom_idx=M     │
│   每条记录包含:                  │
│     header + oxs3d_hit_group_   │
│     data (geom_idx, *vertices,  │
│     *indices, flip_surface ...) │
└────────────────────────────────┘
```

### 6.7 Module 7: Ray Tracing Engine (`oxs3d_trace`)

**文件**: `oxs3d_trace.h`, `oxs3d_trace.cpp`  
**替换**: `cus3d_trace` (CUDA kernel → OptiX optixLaunch)

```cpp
// oxs3d_trace.h
#ifndef OXS3D_TRACE_H
#define OXS3D_TRACE_H

#include "oxs3d_device.h"
#include "oxs3d_accel.h"
#include "oxs3d_geom_store.h"
#include "oxs3d_pipeline.h"
#include "cus3d_types.h"
#include "cus3d_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 复用 cus3d_ray_batch 定义 */
struct oxs3d_ray_batch {
    struct gpu_buffer_float3   d_origins;
    struct gpu_buffer_float3   d_directions;
    struct gpu_buffer_float2   d_ranges;
    struct gpu_buffer_uint32   d_ray_data_offsets;
    size_t                     count;
};

res_T oxs3d_trace_ray_batch(
    const struct oxs3d_accel* accel,
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev,
    struct oxs3d_pipeline* pipe,
    const struct oxs3d_ray_batch* rays,
    struct cus3d_hit_result* h_results);

res_T oxs3d_trace_ray_single(
    const struct oxs3d_accel* accel,
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev,
    struct oxs3d_pipeline* pipe,
    const float origin[3],
    const float direction[3],
    const float range[2],
    struct cus3d_hit_result* result);

res_T oxs3d_trace_ray_single_multi(
    const struct oxs3d_accel* accel,
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev,
    struct oxs3d_pipeline* pipe,
    const float origin[3],
    const float direction[3],
    const float range[2],
    int max_hits,
    struct cus3d_multi_hit_result* result);

res_T oxs3d_trace_ray_batch_multi(
    const struct oxs3d_accel* accel,
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev,
    struct oxs3d_pipeline* pipe,
    const struct oxs3d_ray_batch* rays,
    int max_hits,
    struct cus3d_multi_hit_result* h_results);

res_T oxs3d_ray_batch_create(struct oxs3d_ray_batch* batch, size_t max_rays);
void  oxs3d_ray_batch_destroy(struct oxs3d_ray_batch* batch);

#ifdef __cplusplus
}
#endif
#endif /* OXS3D_TRACE_H */
```

**射线追踪调用流程:**

```
oxs3d_trace_ray_batch():
  1. 上传射线数据到 GPU (d_origins, d_directions, d_ranges)
  2. 分配 d_results (device memory)
  3. 填充 oxs3d_launch_params:
     - handle = accel->top_handle
     - ray_origins/directions/ranges = device ptrs
     - num_rays = batch->count
     - results = d_results
  4. cudaMemcpy launch_params → d_launch_params (H2D)
  5. optixLaunch(pipe->pipeline, dev->stream,
                 d_launch_params, sizeof(params),
                 &pipe->sbt_trace,
                 batch->count,  /* width */
                 1, 1);         /* height, depth */
  6. cudaMemcpy d_results → h_results (D2H)
  7. 释放临时 buffer (或复用)
```

### 6.8 Module 8: OptiX Device Programs (`oxs3d_programs.cu`) — 新增

**文件**: `oxs3d_programs.cu` (编译为 PTX, 由 OptiX 加载)

```cuda
// oxs3d_programs.cu — OptiX Device Programs
#include <optix_device.h>
#include "oxs3d_types.h"

extern "C" {
    __constant__ struct oxs3d_launch_params params;
}

/* ---- RayGen: 批量射线追踪 (单命中) ---- */
extern "C" __global__ void __raygen__trace()
{
    const uint3 idx = optixGetLaunchIndex();
    const uint32_t ray_idx = idx.x;
    if (ray_idx >= params.num_rays) return;

    float3 origin    = params.ray_origins[ray_idx];
    float3 direction = params.ray_directions[ray_idx];
    float2 range     = params.ray_ranges[ray_idx];

    /* Payload: 通过寄存器传递命中信息 */
    uint32_t p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0;

    /* 初始化 payload 为 miss */
    p0 = __float_as_uint(-1.0f);  /* prim_id = -1 */

    optixTrace(
        params.handle,
        origin, direction,
        range.x, range.y,          /* tmin, tmax */
        0.0f,                      /* rayTime */
        OptixVisibilityMask(0xFF),
        OPTIX_RAY_FLAG_NONE,
        0,  /* SBT offset (ray type 0) */
        1,  /* SBT stride (1 ray type) */
        0,  /* missSBTIndex */
        p0, p1, p2, p3, p4, p5, p6, p7);

    /* 解码 payload → cus3d_hit_result */
    struct cus3d_hit_result result;
    result.prim_id  = __uint_as_int(p0);
    result.geom_idx = __uint_as_int(p1);
    result.inst_id  = __uint_as_int(p2);
    result.distance = __uint_as_float(p3);
    result.normal[0] = __uint_as_float(p4);
    result.normal[1] = __uint_as_float(p5);
    result.normal[2] = __uint_as_float(p6);
    /* uv 打包在 p7 中 (2x float16) 或使用额外 payload */
    uint16_t uv_packed_lo = (uint16_t)(p7 & 0xFFFF);
    uint16_t uv_packed_hi = (uint16_t)(p7 >> 16);
    result.uv[0] = __half2float(*((__half*)&uv_packed_lo));
    result.uv[1] = __half2float(*((__half*)&uv_packed_hi));

    params.results[ray_idx] = result;
}

/* ---- ClosestHit: 三角形 ---- */
extern "C" __global__ void __closesthit__triangle()
{
    /* 获取 SBT 数据 */
    const oxs3d_hit_group_data* data =
        (const oxs3d_hit_group_data*)optixGetSbtDataPointer();

    const uint32_t prim_idx  = optixGetPrimitiveIndex();
    const uint32_t geom_idx  = data->geom_idx;
    const float2   bary      = optixGetTriangleBarycentrics();
    const float    t_hit     = optixGetRayTmax();

    /* 计算几何法线 */
    const uint3  tri  = data->indices[prim_idx];
    const float3 v0   = data->vertices[tri.x];
    const float3 v1   = data->vertices[tri.y];
    const float3 v2   = data->vertices[tri.z];
    const float3 e1   = make_float3(v1.x-v0.x, v1.y-v0.y, v1.z-v0.z);
    const float3 e2   = make_float3(v2.x-v0.x, v2.y-v0.y, v2.z-v0.z);
    float3 N = make_float3(
        e1.y*e2.z - e1.z*e2.y,
        e1.z*e2.x - e1.x*e2.z,
        e1.x*e2.y - e1.y*e2.x);

    if (data->flip_surface) {
        N.x = -N.x; N.y = -N.y; N.z = -N.z;
    }

    /* 写入 payload */
    const uint32_t global_prim_id = data->geom_entries[geom_idx].prim_offset + prim_idx;
    optixSetPayload_0(__int_as_uint((int32_t)global_prim_id));
    optixSetPayload_1(__int_as_uint((int32_t)geom_idx));
    optixSetPayload_2(__int_as_uint(optixGetInstanceId()));  /* inst_id */
    optixSetPayload_3(__float_as_uint(t_hit));
    optixSetPayload_4(__float_as_uint(N.x));
    optixSetPayload_5(__float_as_uint(N.y));
    optixSetPayload_6(__float_as_uint(N.z));
    /* Pack UV (bary.x=u, bary.y=v) */
    __half hu = __float2half(bary.x);
    __half hv = __float2half(bary.y);
    uint32_t uv_packed = (uint32_t)(*(uint16_t*)&hu) |
                         ((uint32_t)(*(uint16_t*)&hv) << 16);
    optixSetPayload_7(uv_packed);
}

/* ---- Intersection: 球体 ---- */
extern "C" __global__ void __intersection__sphere()
{
    const oxs3d_hit_group_data* data =
        (const oxs3d_hit_group_data*)optixGetSbtDataPointer();

    const uint32_t prim_idx = optixGetPrimitiveIndex();
    const sphere_gpu sp = data->spheres[prim_idx];

    const float3 org = optixGetObjectRayOrigin();
    const float3 dir = optixGetObjectRayDirection();
    const float  tmin = optixGetRayTmin();
    const float  tmax = optixGetRayTmax();

    /* 射线-球体交叉测试 */
    const float3 oc = make_float3(org.x - sp.cx, org.y - sp.cy, org.z - sp.cz);
    const float a = dir.x*dir.x + dir.y*dir.y + dir.z*dir.z;
    const float b = 2.0f * (oc.x*dir.x + oc.y*dir.y + oc.z*dir.z);
    const float c = oc.x*oc.x + oc.y*oc.y + oc.z*oc.z - sp.radius*sp.radius;
    const float disc = b*b - 4.0f*a*c;

    if (disc >= 0.0f) {
        const float sqrtd = sqrtf(disc);
        const float inv2a = 0.5f / a;
        float t = (-b - sqrtd) * inv2a;
        if (t < tmin || t > tmax) {
            t = (-b + sqrtd) * inv2a;
        }
        if (t >= tmin && t <= tmax) {
            optixReportIntersection(t, 0 /* hitKind */);
        }
    }
}

/* ---- ClosestHit: 球体 ---- */
extern "C" __global__ void __closesthit__sphere()
{
    const oxs3d_hit_group_data* data =
        (const oxs3d_hit_group_data*)optixGetSbtDataPointer();

    const uint32_t prim_idx = optixGetPrimitiveIndex();
    const sphere_gpu sp = data->spheres[prim_idx];
    const float t_hit = optixGetRayTmax();

    /* 计算命中点和法线 */
    const float3 org = optixGetWorldRayOrigin();
    const float3 dir = optixGetWorldRayDirection();
    const float3 hit_pos = make_float3(
        org.x + t_hit * dir.x,
        org.y + t_hit * dir.y,
        org.z + t_hit * dir.z);
    float3 N = make_float3(
        hit_pos.x - sp.cx,
        hit_pos.y - sp.cy,
        hit_pos.z - sp.cz);

    if (data->flip_surface) {
        N.x = -N.x; N.y = -N.y; N.z = -N.z;
    }

    /* 球面 UV (与 custar-3d sphere_normal_to_uv 一致) */
    float len = sqrtf(N.x*N.x + N.y*N.y + N.z*N.z);
    if (len > 0.0f) { N.x /= len; N.y /= len; N.z /= len; }
    float u_val = atan2f(N.z, N.x) / (2.0f * M_PIf) + 0.5f;
    float v_val = asinf(fmaxf(-1.0f, fminf(1.0f, N.y))) / M_PIf + 0.5f;

    uint32_t global_prim_id = data->geom_entries[data->geom_idx].prim_offset + prim_idx;
    optixSetPayload_0(__int_as_uint((int32_t)global_prim_id));
    optixSetPayload_1(__int_as_uint((int32_t)data->geom_idx));
    optixSetPayload_2(__int_as_uint(optixGetInstanceId()));
    optixSetPayload_3(__float_as_uint(t_hit));
    optixSetPayload_4(__float_as_uint(N.x * len));  /* 保留 un-normalized 法线 */
    optixSetPayload_5(__float_as_uint(N.y * len));
    optixSetPayload_6(__float_as_uint(N.z * len));
    __half hu = __float2half(u_val);
    __half hv = __float2half(v_val);
    uint32_t uv_packed = (uint32_t)(*(uint16_t*)&hu) |
                         ((uint32_t)(*(uint16_t*)&hv) << 16);
    optixSetPayload_7(uv_packed);
}

/* ---- Miss ---- */
extern "C" __global__ void __miss__default()
{
    /* Miss: 设置 prim_id = -1 */
    optixSetPayload_0(__int_as_uint(-1));
    optixSetPayload_3(__float_as_uint(FLT_MAX));
}
```

### 6.9 Module 9: Closest Point Query (`oxs3d_closest_point`)

**策略**: 复用 cuBQL BVH 进行最近点查询（OptiX 不提供原生 closest point）

```cpp
// oxs3d_closest_point.h — 接口与 cus3d_closest_point.h 完全一致
#ifndef OXS3D_CLOSEST_POINT_H
#define OXS3D_CLOSEST_POINT_H

#include "oxs3d_device.h"
#include "oxs3d_accel.h"
#include "oxs3d_geom_store.h"
#include "cus3d_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 复用 cus3d_cp_result, cus3d_cp_batch 定义 */

res_T oxs3d_cp_batch_create(struct cus3d_cp_batch* batch, size_t max_queries);
void  oxs3d_cp_batch_destroy(struct cus3d_cp_batch* batch);

res_T oxs3d_closest_point_batch(
    const struct oxs3d_accel* accel,   /* 内部访问 accel->cubql_bvh */
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev,
    const struct cus3d_cp_batch* queries,
    struct cus3d_cp_result* h_results);

#ifdef __cplusplus
}
#endif
#endif
```

**实现**: 内部通过 `accel->cubql_bvh` 指针访问 cuBQL BVH，调用与 `cus3d_closest_point.cu` 相同的 CUDA kernel。几何数据（顶点/索引/球体）从 `oxs3d_geom_store` 获取，与 OptiX GAS 共享同一份 GPU buffer。

### 6.10 Module 10: Find Enclosure (`oxs3d_find_enclosure`)

**策略选择:**

- **策略 A: OptiX 6-ray 方案** — 用 OptiX optixLaunch 替代 CUDA kernel 发射 6 条轴对齐射线
- **策略 B: 复用 cuBQL 方案** — 与 closest point 类似，复用 cuBQL BVH 做最近点 + dot(N) 方向判断

**推荐策略 A** (OptiX 射线方案)，因为：
1. Enclosure 定位本质是射线追踪操作（6-ray cast）
2. OptiX 的射线追踪比 cuBQL 快 2-5x
3. 避免为 enclosure 维护额外的 cuBQL BVH

```cpp
// oxs3d_find_enclosure.h — 接口与 cus3d_find_enclosure.h 一致
#ifndef OXS3D_FIND_ENCLOSURE_H
#define OXS3D_FIND_ENCLOSURE_H

#include "oxs3d_device.h"
#include "oxs3d_accel.h"
#include "oxs3d_geom_store.h"
#include "oxs3d_pipeline.h"
#include "cus3d_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 复用 cus3d_enc_result, cus3d_enc_batch 定义 */

res_T oxs3d_enc_batch_create(struct cus3d_enc_batch* batch, size_t max_queries);
void  oxs3d_enc_batch_destroy(struct cus3d_enc_batch* batch);

res_T oxs3d_find_enclosure_batch(
    const struct oxs3d_accel* accel,
    const struct oxs3d_geom_store* store,
    struct oxs3d_device* dev,
    struct oxs3d_pipeline* pipe,
    const struct cus3d_enc_batch* queries,
    struct cus3d_enc_result* h_results);

#ifdef __cplusplus
}
#endif
#endif
```

### 6.11 Module 11: Primitive Query (`oxs3d_prim`)

**完全复用** `cus3d_prim.h` / `cus3d_prim.cpp`。命中结果转换逻辑不依赖底层 BVH 实现，仅依赖 `geom_entry` 元数据。

### 6.12 Trace Utility (`oxs3d_trace_util`)

**完全复用** `cus3d_trace_util.h`。UV/法线修正逻辑与后端无关。

---

## 7. Scene View 集成

### 7.1 修改的内部结构

```cpp
// s3d_scene_view_c.h (oxstar-3d 版本)
struct s3d_scene_view {
    /* ---- 不变的主机端字段 (与 custar-3d 一致) ---- */
    struct list_node           node;
    struct htable_geom         cached_geoms;
    struct darray_fltui        cdf;
    struct darray_nprims_cdf   nprims_cdf;
    struct htable_instview     instviews;
    struct darray_uint         detached_shapes;
    float                      lower[3], upper[3];
    scene_shape_cb_T           on_shape_detach_cb;
    int                        aabb_update;
    int                        mask;
    ref_T                      ref;
    struct s3d_scene*          scn;

    /* ---- OptiX 后端状态 (替换 cus3d_* 指针) ---- */
    struct oxs3d_geom_store*   geom_store;
    struct oxs3d_accel*        accel;
    struct oxs3d_pipeline*     pipeline;   /* 新增: OptiX Pipeline */
    int                        build_quality;
    int                        gpu_dirty;
};
```

### 7.2 Sync Pipeline (与 custar-3d 同构)

```
scene_view_sync(scnview):
  1. 遍历所有 shapes (与 custar-3d 一致)
  2. oxs3d_geom_store_sync(store, scene, dev)
     → 平坦化几何到 GPU (与 custar-3d 一致)
  3. oxs3d_geom_store_compute_sphere_aabbs(store, dev)
     → 计算球体 AABB (OptiX Custom Primitive 需要)
  4. oxs3d_accel_build_gas(accel, store, dev, quality)
     → 构建 OptiX GAS (替代 cus3d_bvh_build)
  5. 如果有实例:
     a. oxs3d_accel_build_instance_gas(...)
     b. oxs3d_accel_set_instance_transform(...)
     c. oxs3d_accel_build_ias(...)
  6. oxs3d_pipeline_build_sbt(pipeline, store, dev)
     → 构建/更新 SBT (新增步骤)
  7. 计算 CDF / nprims_cdf (不变)
  8. 获取场景 AABB
  9. scnview->gpu_dirty = false
```

---

## 8. Top-K 多命中实现

### 8.1 OptiX AnyHit 方案

OptiX 的 AnyHit Program 在每次交叉命中时被调用，可以选择接受或拒绝。利用此机制实现 Top-K：

```cuda
// __anyhit__triangle_multi — Top-K 收集器
extern "C" __global__ void __anyhit__triangle_multi()
{
    /* payload p0-p1 存储: hits_ptr (device pointer 到 per-ray Top-K 缓冲区) */
    /* payload p2: 当前已收集的命中数 */

    const float t_hit = optixGetRayTmax();
    // ... 获取 SBT data, prim_id, normal, uv ...

    // 将当前命中插入到按距离排序的 Top-K 数组中
    // 如果 count < K:
    //   插入并递增 count
    //   optixIgnoreIntersection()  ← 继续遍历寻找更多命中
    // 如果 count == K:
    //   如果 t_hit >= hits[K-1].distance:
    //     optixIgnoreIntersection()  ← 比最远的还远，丢弃
    //   否则:
    //     替换 hits[K-1], 重新排序
    //     设置 tmax = hits[K-1].distance (shrink range)
    //     optixIgnoreIntersection()  ← 继续

    // 注意: AnyHit 中不能直接缩短 tmax，
    // 需要通过 payload 传递新的 tmax 给后续遍历
}
```

### 8.2 替代方案: 多次 optixTrace

如果 AnyHit 方案过于复杂，可以采用简单的多次调用方案：

```
for k = 0..K-1:
  trace ray with tmin = t_last + epsilon
  if miss: break
  store hit[k]
  t_last = hit[k].distance
```

由于 STARDIS 典型场景下 K=2 且 filter 通过率 >95%，多次调用的额外开销可接受。

---

## 9. 文件结构预览

```
oxstar-3d/0.10/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── s3d.h                              # 复用 (与 custar-3d 共享)
│   ├── s3d_*.h / s3d_*.cpp                # 复用 (主机端模块，不变)
│   ├── s3d_scene_view*.cpp                # 改写 (调用 oxs3d_* 替代 cus3d_*)
│   ├── s3d_device_c.h                     # 改写 (包含 oxs3d_device*)
│   ├── s3d_scene_view_c.h                 # 改写 (包含 oxs3d_* 指针)
│   │
│   ├── oxs3d_device.h / .cpp              # CUDA + OptiX 设备管理
│   ├── oxs3d_mem.h                        # 转发到 cus3d_mem (复用)
│   ├── oxs3d_types.h                      # 类型定义 (含 SBT record)
│   ├── oxs3d_geom_store.h / .cpp / .cu    # 几何数据存储 + 球体 AABB
│   ├── oxs3d_accel.h / .cpp               # OptiX GAS/IAS 管理
│   ├── oxs3d_pipeline.h / .cpp            # OptiX Pipeline + SBT 管理
│   ├── oxs3d_trace.h / .cpp               # 射线追踪 Host API (optixLaunch)
│   ├── oxs3d_closest_point.h / .cpp / .cu # 最近点查询 (复用 cuBQL)
│   ├── oxs3d_find_enclosure.h / .cpp      # 包壳定位 (OptiX 6-ray)
│   ├── oxs3d_prim.h / .cpp               # 命中结果转换 (复用 cus3d_prim)
│   ├── oxs3d_trace_util.h                 # UV/法线修正 (复用)
│   ├── oxs3d_math.cuh                     # 设备端数学 (复用)
│   │
│   └── device_programs/                   # OptiX Device Programs (.cu → PTX)
│       ├── oxs3d_programs.cu              # RayGen / ClosestHit / AnyHit / Miss
│       ├── oxs3d_programs_multi.cu        # Top-K 多命中版本
│       ├── oxs3d_sphere_intersection.cu   # 球体 Intersection Program
│       └── oxs3d_enclosure_programs.cu    # 包壳定位专用 Programs
│
├── test/                                  # 测试文件 (从 custar-3d 移植)
│   ├── test_s3d_trace_ray.c
│   ├── test_s3d_batch_trace.c
│   ├── test_s3d_closest_point.c
│   └── ...
```

---

## 10. 构建系统集成

### 10.1 CMake 配置

```cmake
# oxstar-3d/0.10/CMakeLists.txt

cmake_minimum_required(VERSION 3.24)
enable_language(CUDA)

find_package(CUDAToolkit REQUIRED)

# OptiX SDK (header-only)
set(OPTIX_DIR "$ENV{OptiX_INSTALL_DIR}" CACHE PATH "Path to OptiX SDK")
if(NOT EXISTS "${OPTIX_DIR}/include/optix.h")
  message(FATAL_ERROR "OptiX SDK not found at ${OPTIX_DIR}")
endif()

# cuBQL (用于 closest point)
set(CUBQL_DIR "${CMAKE_SOURCE_DIR}/thirdparty/cuBQL")

# ------ PTX 编译 (Device Programs) ------
set(OXS3D_DEVICE_PROGRAMS
    src/device_programs/oxs3d_programs.cu
    src/device_programs/oxs3d_programs_multi.cu
    src/device_programs/oxs3d_sphere_intersection.cu
    src/device_programs/oxs3d_enclosure_programs.cu
)

# 编译为 PTX (不是 object file)
foreach(prog ${OXS3D_DEVICE_PROGRAMS})
  get_filename_component(prog_name ${prog} NAME_WE)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/ptx/${prog_name}.ptx
    COMMAND ${CUDAToolkit_NVCC_EXECUTABLE}
      -ptx
      -I${OPTIX_DIR}/include
      -I${CMAKE_CURRENT_SOURCE_DIR}/src
      --use_fast_math
      -arch=compute_70
      ${CMAKE_CURRENT_SOURCE_DIR}/${prog}
      -o ${CMAKE_BINARY_DIR}/ptx/${prog_name}.ptx
    DEPENDS ${prog}
    COMMENT "Compiling OptiX PTX: ${prog_name}"
  )
  list(APPEND OXS3D_PTX_FILES ${CMAKE_BINARY_DIR}/ptx/${prog_name}.ptx)
endforeach()

add_custom_target(oxs3d_ptx ALL DEPENDS ${OXS3D_PTX_FILES})

# ------ Host Library ------
set(OXS3D_CPP_SOURCES
    src/s3d_device.cpp
    src/s3d_scene.cpp
    src/s3d_scene_view.cpp
    src/s3d_shape.cpp
    src/s3d_mesh.cpp
    src/s3d_sphere.cpp
    src/s3d_instance.cpp
    src/s3d_primitive.cpp
    src/s3d_scene_view_trace_ray.cpp
    src/s3d_scene_view_batch_trace.cpp
    src/s3d_scene_view_closest_point.cpp
    src/s3d_scene_view_find_enclosure.cpp
    src/s3d_scene_view_batch_closest_point.cpp
)

set(OXS3D_CUDA_SOURCES
    src/oxs3d_device.cpp
    src/oxs3d_geom_store.cpp
    src/oxs3d_geom_store.cu
    src/oxs3d_accel.cpp
    src/oxs3d_pipeline.cpp
    src/oxs3d_trace.cpp
    src/oxs3d_closest_point.cu
    src/oxs3d_find_enclosure.cpp
    src/oxs3d_prim.cpp
    # 复用 cus3d_mem
    src/cus3d_mem.cpp
)

add_library(s3d SHARED ${OXS3D_CPP_SOURCES} ${OXS3D_CUDA_SOURCES})
add_dependencies(s3d oxs3d_ptx)

target_include_directories(s3d PRIVATE
    ${OPTIX_DIR}/include
    ${CUBQL_DIR}
    ${CUDAToolkit_INCLUDE_DIRS}
)

target_compile_definitions(s3d PRIVATE
    OXS3D_PTX_DIR="${CMAKE_BINARY_DIR}/ptx"
)

target_link_libraries(s3d PRIVATE
    CUDA::cudart
    CUDA::cuda_driver   # OptiX 需要 CUDA Driver API
    rsys
)

set_target_properties(s3d PROPERTIES
    CUDA_ARCHITECTURES "70;75;80;86;89;90"
)
```

### 10.2 OptiX SDK 依赖说明

OptiX 7.x 是 **header-only** 的（仅 `optix.h`, `optix_device.h`, `optix_stubs.h`）。运行时由 NVIDIA 驱动提供，不需要链接额外的 `.lib`。

需要用 `optix_stubs.h` 在运行时加载 OptiX 函数指针：

```cpp
// oxs3d_device.cpp 中
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

res_T oxs3d_device_create(int device_id, struct oxs3d_device** out) {
    // ... CUDA 初始化 (与 cus3d_device_create 一致) ...

    // OptiX 初始化
    OPTIX_CHECK(optixInit());

    OptixDeviceContextOptions options = {};
    options.logCallbackFunction = optix_log_callback;
    options.logCallbackLevel = 4;

    OPTIX_CHECK(optixDeviceContextCreate(
        cuda_context, &options, &dev->optix_ctx));

    // ...
}
```

---

## 11. 性能预期

### 11.1 与 custar-3d 对比

| 场景 | custar-3d (cuBQL) | oxstar-3d (OptiX) | 预期加速比 |
|------|-------------------|-------------------|-----------|
| BVH 构建 | cuBQL gpuBuilder (软件) | optixAccelBuild (驱动+硬件) | 1.5-3x |
| 三角形交叉 | 软件 Möller-Trumbore | RT Core 硬件加速 | 2-5x |
| BVH 遍历 | cuBQL 软件栈遍历 | RT Core 硬件遍历 | 2-5x |
| 批量射线 | CUDA kernel | optixLaunch | 2-5x |
| 最近点查询 | cuBQL shrinkingRadius | cuBQL (复用) | ~1x |
| 包壳定位 | 6-ray CUDA kernel | 6-ray OptiX | 2-5x |

### 11.2 性能优化机会

1. **AS Compaction**: 构建后 `optixAccelCompact()` 减少 30-50% 内存
2. **AS Refit**: 微小变化时 `OPTIX_BUILD_FLAG_ALLOW_UPDATE` + refit
3. **Motion Blur**: 如果需要动态场景，OptiX 原生支持 motion AS
4. **Multi-GPU**: OptiX 支持多 GPU 并行，每个 GPU 独立 GAS
5. **Persistent Launch**: 减少 launch overhead
6. **Payload 优化**: 减少 payload 寄存器数量，提升 occupancy

---

## 12. 与 dxrstar-3d 的对比

| 维度 | dxrstar-3d (DXR) | oxstar-3d (OptiX) |
|------|-------------------|-------------------|
| **API 风格** | D3D12 Compute Shader + Inline RT | CUDA + optixTrace Pipeline |
| **SBT/SRT** | 无 (Inline RT 不需要) | 需要 SBT 管理 |
| **球体支持** | AABB + Inline RT 自定义 | AABB + Intersection Program |
| **Closest Point** | 自行实现 Compute Shader | 复用 cuBQL (已有) |
| **代码复用** | 从 custar-3d 重写 | 从 custar-3d 大量复用 |
| **跨厂商** | NVIDIA + AMD + Intel | NVIDIA only |
| **跨平台** | Windows only | Windows + Linux |
| **调试** | PIX + D3D12 Debug Layer | Nsight + cuda-gdb (已熟悉) |
| **实现难度** | 高 (D3D12 复杂度) | 中 (CUDA 生态延续) |

**结论**: oxstar-3d 与 custar-3d 共享更多代码（内存管理、closest point、prim 转换、trace util），实现难度更低。dxrstar-3d 的优势是跨厂商支持。

---

## 13. 风险评估

| 风险 | 严重性 | 可能性 | 缓解措施 |
|------|-------|-------|----------|
| OptiX 版本兼容性 | 中 | 低 | 使用 OptiX 7.4+ 的稳定 API 子集 |
| SBT 管理复杂度 | 中 | 中 | 封装 `oxs3d_pipeline` 统一管理 |
| Payload 寄存器限制 (8 个) | 中 | 中 | UV 使用 half 精度打包，或使用全局内存 |
| cuBQL 依赖 (closest point) | 低 | 低 | cuBQL 是 header-only，编译集成简单 |
| NVIDIA-only 限制 | 中 | 确定 | 项目目标 GPU 是 NVIDIA RTX 4090，可接受 |
| PTX 编译/加载 | 低 | 低 | 内嵌 PTX 字符串或从文件加载 |
| 双精度性能 | 中 | 中 | OptiX 三角形交叉硬件仅 FP32; 自定义交叉可用 FP64 |

---

## 14. 实施路线图

### Phase 1: 基础设施 (预计 1 周)

1. [ ] `oxs3d_device` — CUDA + OptiX 初始化
2. [ ] `oxs3d_mem.h` — 转发 cus3d_mem
3. [ ] `oxs3d_types.h` — 类型定义
4. [ ] CMake 配置 — PTX 编译 + 链接

### Phase 2: 加速结构 (预计 1-2 周)

5. [ ] `oxs3d_geom_store` — 几何平坦化 + 球体 AABB
6. [ ] `oxs3d_accel` — GAS 构建（仅三角形）
7. [ ] `oxs3d_pipeline` — Pipeline + SBT (仅三角形)
8. [ ] 简单射线追踪测试 (Cornell Box 三角形)

### Phase 3: 射线追踪 (预计 1-2 周)

9. [ ] `oxs3d_programs.cu` — RayGen + ClosestHit + Miss
10. [ ] `oxs3d_trace` — 单射线 + 批量射线
11. [ ] 球体 Intersection Program + ClosestHit
12. [ ] 混合几何场景测试

### Phase 4: 高级查询 (预计 1 周)

13. [ ] `oxs3d_closest_point` — 复用 cuBQL
14. [ ] `oxs3d_find_enclosure` — OptiX 6-ray
15. [ ] Top-K 多命中 (AnyHit 方案)
16. [ ] Hit Filter + CPU post-processing

### Phase 5: 集成验证 (预计 1 周)

17. [ ] Scene View 集成
18. [ ] 全部测试移植并通过
19. [ ] GPU/CPU 结果一致性验证 (容差 1e-6)
20. [ ] 性能基准测试

---

*下一步: 参阅 [module-interface-mapping.md](module-interface-mapping.md) 获取 custar-3d ↔ oxstar-3d 完整接口映射表*
