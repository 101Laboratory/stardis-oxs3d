# dxrstar-3d 架构设计文档

**版本**: 0.1  
**日期**: 2026-02-20  
**状态**: 设计草案  
**目标**: 基于 DirectX Raytracing (DXR) 实现 star-3d 兼容的光线追踪后端

---

## 1. 项目定位

### 1.1 背景

`custar-3d` 项目已成功将 Embree (CPU) 光追后端迁移到 CUDA + cuBQL (GPU)，并保持了与原 `star-3d` 完全一致的 `s3d.h` 公共 C API。

`dxrstar-3d` 是第三种后端实现，使用 **DirectX Raytracing (DXR 1.1)** 作为底层光追引擎，面向 Windows 平台的 DirectX 12 GPU（支持 NVIDIA、AMD、Intel 硬件光追单元）。

### 1.2 目标
- **对外接口 100% 一致**: 复用 `s3d.h` 定义的全部公共 API（~70 个函数），上层求解器代码**零修改**
- **功能 100% 一致**: 射线追踪、最近点查询、包壳定位、批量操作、实例化、球体支持等全部能力
- **DXR 原生加速**: 利用 RT Core 硬件加速（NVIDIA RTX、AMD RDNA2+、Intel Arc）
- **跨厂商兼容**: 不依赖 CUDA，可在 AMD/Intel GPU 上运行

### 1.3 与 custar-3d 的关系

```
                ┌─────────────────────────────────────────┐
                │         s3d.h (公共 C API，不变)          │
                │  ~70 个函数 + 结构体/枚举/常量定义         │
                └──────────────────┬──────────────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
    ┌─────────▼─────────┐  ┌──────▼──────────┐  ┌─────▼──────────┐
    │    star-3d (CPU)   │  │  custar-3d (GPU) │  │ dxrstar-3d(GPU)│
    │    Embree 后端      │  │  CUDA+cuBQL 后端  │  │  DXR 后端      │
    │    原始实现          │  │  已完成           │  │  本文档目标     │
    └────────────────────┘  └─────────────────┘  └────────────────┘
```

### 1.4 适用范围

| 维度 | custar-3d | dxrstar-3d |
|------|-----------|------------|
| **GPU 平台** | NVIDIA only (CUDA) | NVIDIA + AMD + Intel (DX12) |
| **操作系统** | Windows + Linux | Windows only |
| **光追 API** | cuBQL (软件 BVH) | DXR 1.1 (硬件 RT Core) |
| **计算着色** | CUDA Kernel | D3D12 Compute Shader / Inline RT |
| **内存模型** | CUDA Unified/Device Memory | D3D12 Committed/Placed Resources |
| **构建系统** | CUDA + CMake | HLSL + CMake (dxc) |
| **调试工具** | Nsight, cuda-gdb | PIX, Nsight Graphics, RenderDoc |
| **双精度** | 原生支持 | HLSL 支持 `double` (SM 6.0+) |

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
│                    DXR 模块层 (D3D12 + DXR 1.1)                │
│   ┌───────────┐ ┌──────────┐ ┌──────────────────┐             │
│   │ dxrs3d_   │ │ dxrs3d_  │ │ dxrs3d_          │             │
│   │ device    │ │ mem      │ │ geom_store       │             │
│   │ D3D12设备  │ │ GPU资源   │ │ 平坦化几何数据    │             │
│   │ 命令队列   │ │ 上传/回读  │ │ SRV/UAV 绑定     │             │
│   └─────┬─────┘ └────┬─────┘ └───────┬──────────┘             │
│         │            │               │                         │
│   ┌─────┴────────────┴───────────────┴─────────────────┐       │
│   │ dxrs3d_accel         (加速结构管理: DXR AS)         │       │
│   │ ├─ BLAS (Bottom-Level AS，per-geometry)             │       │
│   │ ├─ TLAS (Top-Level AS，实例化 + 伪实例)              │       │
│   │ └─ AS Build/Update/Compact                          │       │
│   └────────────────┬───────────────────────────────────┘       │
│         ┌──────────┼──────────────┐                            │
│   ┌─────┴─────┐ ┌──┴───────┐ ┌───┴───────────────┐            │
│   │ dxrs3d_   │ │ dxrs3d_  │ │ dxrs3d_           │            │
│   │ trace     │ │ closest_ │ │ find_enclosure    │            │
│   │ Inline RT │ │ point    │ │ 6-ray enclosure   │            │
│   │ Compute   │ │ Compute  │ │ Compute Shader    │            │
│   │ Shader    │ │ Shader   │ │                   │            │
│   └─────┬─────┘ └──────────┘ └───────────────────┘            │
│         │                                                      │
│   ┌─────┴───────────┐                                          │
│   │ dxrs3d_prim     │  dxrs3d_trace_util.h (UV/法线修正)        │
│   │ 命中结果转换      │  dxrs3d_math.hlsli (设备端数学)          │
│   └─────────────────┘                                          │
├───────────────────────────────────────────────────────────────┤
│   外部依赖:                                                     │
│     D3D12 Runtime + DXR 1.1 | dxcompiler (HLSL SM 6.5+)       │
│     D3D12 Memory Allocator (D3D12MA, 可选)                      │
│     rsys (基础运行时)                                            │
└───────────────────────────────────────────────────────────────┘
```

### 2.2 核心设计原则

1. **模块 1:1 映射**: dxrs3d_* 模块与 cus3d_* 模块一一对应，保持相同的职责边界
2. **接口签名一致**: 内部 C++ API 签名与 cus3d_* 保持一致（仅替换 CUDA 特定类型）
3. **GPU 操作透明**: 所有 DXR 操作隐藏在 scene_view sync/trace 边界之后
4. **过滤器 CPU 端执行**: 延续 custar-3d 的 Top-K + CPU Filter 策略
5. **Inline Ray Tracing**: 使用 DXR 1.1 Inline RT（`RayQuery`）而非 shader table pipeline

### 2.3 为什么选择 Inline Ray Tracing

| 维度 | DXR Pipeline (DispatchRays) | DXR Inline RT (RayQuery) |
|------|---------------------------|--------------------------|
| **着色器模型** | 需要 RayGen/ClosestHit/Miss/AnyHit 等多个着色器 | 单个 Compute Shader |
| **调度控制** | 由驱动调度 | 由开发者完全控制 |
| **SBT (Shader Binding Table)** | 必须构建和管理 SBT | 不需要 SBT |
| **复杂度** | 高（多着色器协调、SBT 布局、root signature 设计） | 低（单 shader，类似 CUDA kernel） |
| **与 custar-3d 对应** | 无明确对应 | 直接对应 CUDA kernel + cuBQL traversal |
| **控制灵活性** | 受限（不能中途退出/跳过） | 完全灵活（可实现 Top-K 多命中、自定义过滤） |
| **性能** | 适合复杂光照/材质场景 | 适合计算密集型/科学计算场景 |

**结论**: Inline RT 的编程模型与 custar-3d 的 CUDA kernel + cuBQL traversal 几乎一一对应，是最自然的选择。科学计算/热模拟场景不需要复杂的材质着色，Inline RT 的灵活性（Top-K 多命中、自定义过滤逻辑）完全匹配需求。

---

## 3. DXR 关键概念映射

### 3.1 cuBQL → DXR 概念映射

| cuBQL 概念 | DXR 等价 | 说明 |
|-----------|----------|------|
| `cuBQL::BinaryBVH<float,3>` | `ID3D12Resource` (AS buffer) | BVH 存储在 GPU 资源中 |
| `cuBQL::gpuBuilder()` | `BuildRaytracingAccelerationStructure()` | GPU 上构建 BVH |
| `cuBQL::BuildConfig` | `D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS` | 构建参数 |
| `cuBQL::shrinkingRayQuery` | HLSL `RayQuery<RAY_FLAG_NONE>` | Inline RT 查询对象 |
| `cuBQL::shrinkingRayQuery::forEachPrim()` | `RayQuery.TraceRayInline()` + `Proceed()` 循环 | BVH 遍历 |
| `cuBQL::shrinkingRayQuery::twoLevel` | DXR 原生 TLAS→BLAS 两级遍历 | **DXR 天然支持** |
| BLAS (cuBQL 手动构建) | `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL` | 自动 |
| TLAS (cuBQL 手动构建) | `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL` | 自动 |
| 伪实例 (Pseudo Instance) | `D3D12_RAYTRACING_INSTANCE_DESC` with 单位矩阵 | DXR 原生实例 |

### 3.2 CUDA → D3D12 资源映射

| CUDA 概念 | D3D12 等价 | dxrs3d 命名 |
|-----------|-----------|------------|
| `cudaStream_t` | `ID3D12CommandQueue` + `ID3D12CommandList` | `dxrs3d_cmd_context` |
| `cudaMalloc` / `cudaFree` | `CreateCommittedResource` / `Release` | `dxrs3d_buffer` |
| `cudaMemcpyAsync(H2D)` | Upload Heap → `CopyBufferRegion` | `dxrs3d_buffer_upload` |
| `cudaMemcpyAsync(D2H)` | Readback Heap → `CopyBufferRegion` | `dxrs3d_buffer_download` |
| `__global__ kernel<<<grid,block>>>` | `Dispatch(groupX, groupY, groupZ)` | Compute Shader |
| CUDA Device Memory | `D3D12_HEAP_TYPE_DEFAULT` | GPU-only 资源 |
| CUDA Pinned Memory | `D3D12_HEAP_TYPE_UPLOAD` / `READBACK` | 上传/回读堆 |
| `cudaDeviceSynchronize()` | Fence + `WaitForSingleObject` | `dxrs3d_device_sync` |
| `blockIdx * blockDim + threadIdx` | `SV_DispatchThreadID` | HLSL 全局线程 ID |

### 3.3 几何数据格式映射

| cus3d / cuBQL | DXR | 说明 |
|-------------|-----|------|
| `float3` 顶点数组 | `DXGI_FORMAT_R32G32B32_FLOAT` VertexBuffer | DXR 原生三角形格式 |
| `uint3` 索引数组 | `DXGI_FORMAT_R32_UINT` IndexBuffer | 32-bit 索引 |
| 球体 (`center + radius`) | AABB geometry + Intersection Shader | DXR 自定义几何 |
| `geom_entry` 元数据 | StructuredBuffer (SRV) | 绑定到 Compute Shader |
| `prim_to_geom` 映射 | StructuredBuffer (SRV) | primID 到 geom 的映射 |

---

## 4. 与 custar-3d 的关键差异

### 4.1 加速结构管理

**custar-3d**: 手动管理 cuBQL BVH，需要显式构建 BLAS、子 BVH、TLAS。  
**dxrstar-3d**: DXR 原生支持 BLAS/TLAS 两级结构，**极大简化代码**。

```
custar-3d (手动两级遍历):                dxrstar-3d (DXR 原生两级):
┌─────────────────────┐                  ┌─────────────────────┐
│ 手动构建主 BLAS       │                  │ D3D12 Build BLAS     │
│ 手动构建子 BVH (N个)  │                  │   per geometry       │
│ 手动组装 TLAS         │                  │ D3D12 Build TLAS     │
│ 手动管理 tlas_to_orig │                  │   InstanceDesc[]     │
│ 伪实例机制            │                  │   (DXR 原生实例化)    │
│ 内核内手动二级遍历     │                  │ RayQuery 自动二级遍历 │
└─────────────────────┘                  └─────────────────────┘
```

### 4.2 球体支持

**custar-3d**: 球体与三角形统一编排在同一连续数组中（三角形在前，球体在后），通过 `primID < tri_count` 在 kernel 中区分。

**dxrstar-3d**: DXR 支持两种几何类型：
- **三角形**: `D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES` — 硬件加速交叉
- **AABB**: `D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS` — 通过 Intersection Shader 自定义交叉

球体映射为 AABB 几何 + 自定义 Intersection Shader（或在 Inline RT 中通过 `COMMITTED_PROCEDURAL_PRIMITIVE_HIT` 处理）。

### 4.3 内存管理

**custar-3d**: 5 种类型化 `gpu_buffer_<T>` 封装 `cudaMalloc` / `cudaFree`。

**dxrstar-3d**: 统一 `dxrs3d_buffer` 封装 `ID3D12Resource`，按用途分类：
- **Default Heap**: GPU-only 存储（顶点、索引、AS 缓冲）
- **Upload Heap**: CPU→GPU 上传暂存
- **Readback Heap**: GPU→CPU 结果回读

### 4.4 Compute Shader 编程模型

**custar-3d**: CUDA Kernel，使用 `blockIdx`, `threadIdx`, `__syncthreads()` 等 CUDA 原语。

**dxrstar-3d**: HLSL Compute Shader (SM 6.5+)，使用 `SV_DispatchThreadID`，通过 `RayQuery` 对象进行 Inline Ray Tracing。

```hlsl
// custar-3d (CUDA kernel)
__global__ void trace_rays_kernel(
    cuBQL::BinaryBVH<float,3> bvh,
    float3* origins, float3* directions, float2* ranges,
    cus3d_hit_result* results, uint32_t nrays)
{
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= nrays) return;
    // cuBQL traversal ...
}

// dxrstar-3d (HLSL Compute Shader + Inline RT)
[numthreads(256, 1, 1)]
void TraceRaysCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= g_nrays) return;

    RayDesc ray;
    ray.Origin = g_origins[idx];
    ray.Direction = g_directions[idx];
    ray.TMin = g_ranges[idx].x;
    ray.TMax = g_ranges[idx].y;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(g_tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {
        // 处理候选命中 (三角形 / AABB)
    }
    // 写入结果 ...
}
```

---

## 5. DXR 的优势与挑战

### 5.1 优势

| 优势 | 说明 |
|------|------|
| **硬件 RT Core** | 利用 RT Core 硬件加速 BVH 遍历和三角形交叉测试，性能显著优于软件 BVH |
| **原生 TLAS/BLAS** | DXR 天然支持两级加速结构，无需手动管理伪实例、tlas_to_orig 映射等 |
| **跨厂商** | 支持 NVIDIA RTX、AMD RDNA2+、Intel Arc 全部现代 GPU |
| **AS Compaction** | DXR 原生支持 AS compact，减少内存占用 |
| **AS Update** | 支持 refit (update-in-place)，动态场景时避免完全重建 |
| **驱动优化** | 驱动负责 BVH 构建算法优化，无需自行调优 |

### 5.2 挑战

| 挑战 | 缓解策略 |
|------|----------|
| **D3D12 复杂性** | 封装 device/command list/fence/descriptor 管理，对上层透明 |
| **无 Closest Point Query** | DXR 不直接支持最近点查询，需自行实现（AABB 搜索 + Compute Shader） |
| **球体自定义交叉** | 使用 AABB geometry + Inline RT 的 `COMMITTED_PROCEDURAL_PRIMITIVE_HIT` |
| **双精度性能** | HLSL 支持 `double`，但性能因厂商而异；可提供 FP32/FP64 双版本 |
| **仅 Windows** | 可接受，stardis 主要目标平台为 Windows |
| **调试复杂度** | 使用 PIX + D3D12 Debug Layer + GPU Validation |
| **Root Signature 管理** | 设计统一的 root signature 布局，所有 shader 共享 |

---

## 6. 模块概览

### 6.1 模块对照表

| # | custar-3d 模块 | dxrstar-3d 模块 | 文件 | 替换内容 |
|---|---------------|----------------|------|---------|
| 1 | `cus3d_device` | `dxrs3d_device` | `.h/.cpp` | CUDA → D3D12 Device + Queue |
| 2 | `cus3d_mem` | `dxrs3d_mem` | `.h/.cpp` | `gpu_buffer_*` → `dxrs3d_buffer` |
| 3 | `cus3d_types` | `dxrs3d_types` | `.h` | CUDA 类型 → HLSL 兼容类型 |
| 4 | `cus3d_geom_store` | `dxrs3d_geom_store` | `.h/.cpp` | 几何数据 → D3D12 Buffer + SRV |
| 5 | `cus3d_bvh` | `dxrs3d_accel` | `.h/.cpp` | cuBQL BVH → DXR BLAS/TLAS |
| 6 | `cus3d_trace` | `dxrs3d_trace` | `.h/.cpp` + `.hlsl` | CUDA kernel → Compute+InlineRT |
| 7 | `cus3d_closest_point` | `dxrs3d_closest_point` | `.h/.cpp` + `.hlsl` | cuBQL CP → Compute Shader |
| 8 | `cus3d_find_enclosure` | `dxrs3d_find_enclosure` | `.h/.cpp` + `.hlsl` | CUDA enc → Compute+InlineRT |
| 9 | `cus3d_prim` | `dxrs3d_prim` | `.h/.cpp` | GPU hit → s3d 转换 (不变) |
| 10 | `cus3d_trace_util` | `dxrs3d_trace_util` | `.h` | UV/法线修正 (不变) |
| 11 | `cus3d_math.cuh` | `dxrs3d_math.hlsli` | `.hlsli` | CUDA 数学 → HLSL 数学 |
| 12 | — (新增) | `dxrs3d_shader_mgr` | `.h/.cpp` | PSO 编译/缓存管理 |
| 13 | — (新增) | `dxrs3d_descriptor` | `.h/.cpp` | Descriptor Heap 管理 |

### 6.2 文件结构预览

```
dxrstar-3d/0.10/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── s3d.h                              # 复用 (与 custar-3d 共享)
│   ├── s3d_*.h / s3d_*.cpp                # 复用 (主机端模块，不变)
│   ├── s3d_scene_view*.cpp                # 改写 (调用 dxrs3d_* 替代 cus3d_*)
│   ├── s3d_device_c.h                     # 改写 (包含 dxrs3d_device*)
│   ├── s3d_scene_view_c.h                 # 改写 (包含 dxrs3d_* 指针)
│   │
│   ├── dxrs3d_device.h / .cpp             # D3D12 设备管理
│   ├── dxrs3d_mem.h / .cpp                # GPU 资源管理
│   ├── dxrs3d_types.h                     # 公共类型定义
│   ├── dxrs3d_geom_store.h / .cpp         # 几何数据存储
│   ├── dxrs3d_accel.h / .cpp              # DXR 加速结构 (BLAS/TLAS)
│   ├── dxrs3d_trace.h / .cpp              # 射线追踪 Host API
│   ├── dxrs3d_closest_point.h / .cpp      # 最近点查询 Host API
│   ├── dxrs3d_find_enclosure.h / .cpp     # 包壳定位 Host API
│   ├── dxrs3d_prim.h / .cpp               # 命中结果转换
│   ├── dxrs3d_trace_util.h                # UV/法线修正
│   ├── dxrs3d_shader_mgr.h / .cpp         # PSO 管理
│   ├── dxrs3d_descriptor.h / .cpp         # Descriptor Heap 管理
│   │
│   └── shaders/                           # HLSL 着色器
│       ├── trace_rays.hlsl                # 射线追踪 Compute Shader
│       ├── trace_rays_topk.hlsl           # Top-K 多命中版本
│       ├── closest_point.hlsl             # 最近点查询 Compute Shader
│       ├── find_enclosure.hlsl            # 包壳定位 Compute Shader
│       ├── compute_bounds.hlsl            # AABB 计算 Compute Shader
│       ├── sphere_intersection.hlsli      # 球体交叉测试
│       ├── math_utils.hlsli               # 数学工具
│       └── common.hlsli                   # 公共定义
│
├── test/                                  # 测试文件 (从 custar-3d 移植)
│   ├── test_s3d_trace_ray.c
│   ├── test_s3d_batch_trace.c
│   ├── test_s3d_closest_point.c
│   └── ...
```

---

## 7. 性能预期

### 7.1 与 custar-3d 对比

| 场景 | custar-3d (cuBQL) | dxrstar-3d (DXR) | 预期加速比 |
|------|-------------------|-------------------|-----------|
| BVH 构建 | cuBQL gpuBuilder (软件) | DXR BuildAS (驱动+硬件) | 1.5-3x |
| 三角形交叉 | 软件 Moller-Trumbore | RT Core 硬件加速 | 2-5x |
| BVH 遍历 | cuBQL 软件栈遍历 | RT Core 硬件遍历 | 2-5x |
| 批量射线 | CUDA kernel | Compute + Inline RT | 2-5x |
| 最近点查询 | cuBQL shrinkingRadius | 自行实现 (无硬件加速) | ~1x |
| 包壳定位 | 6-ray CUDA kernel | 6-ray Inline RT | 2-5x |

### 7.2 性能优化机会

1. **AS Compaction**: 构建后压缩 AS，减少 30-50% 内存占用
2. **AS Refit**: 场景微小变化时使用 refit 替代完全重建
3. **Wave Intrinsics**: HLSL SM 6.0+ Wave 操作可优化归约/前缀和
4. **Descriptor Indexing**: Bindless 资源绑定减少 root signature 切换
5. **Stream Pipelining**: 多 Command Queue 并行执行构建/追踪/回读

---

## 8. 风险评估

| 风险 | 严重性 | 可能性 | 缓解措施 |
|------|-------|-------|----------|
| D3D12 API 复杂度过高 | 高 | 中 | 封装统一的 device/command 抽象层 |
| 最近点查询无硬件加速 | 中 | 确定 | 使用 Compute Shader + AABB 空间搜索 |
| 双精度性能不一致 | 中 | 高 | 提供 FP32/FP64 双版本，配置选择 |
| DXR 驱动 bug | 低 | 低 | 多厂商测试，回退软件路径 |
| 球体 Intersection Shader 兼容性 | 低 | 低 | 使用 Inline RT 的 AABB+自定义交叉 |
| DXR 不支持 shrinkingRadiusQuery | 高 | 确定 | 自行实现 BVH 遍历 Compute Shader |

---

*下一步: 参阅 [module-design.md](module-design.md) 获取各模块详细设计*
