# OptiX Throughput Validation — 架构文档

**项目**: OptiX 9.1.0 基础光追管线 + 最近点查询 + 吞吐量基准测试  
**GPU**: NVIDIA RTX 3070 Laptop (sm_86, 8 GB)  
**峰值光追吞吐量**: ~3.8 GRays/s @ 64M rays  
**峰值 NN 吞吐量**: ~29 MQueries/s @ 1M queries, 100K points  
**编写时间**: 2026-02

---

## 1. 项目概览

本项目使用 **OptiX 9.1.0** 纯推荐功能实现完整的光追管线，涵盖：

- CUDA / OptiX 设备初始化
- GPU 缓冲区生命周期管理（RAII）
- BVH 加速结构构建（GAS + IAS，含压缩）
- 三角形几何与实例管理
- 单次 / 批量光线追踪请求
- **最近点查询（基于 RTNN 的 RT Core AABB 遍历）**
- 多流并发吞吐量测试

### 目录结构

```
optix-throughput-validation/
├── CMakeLists.txt           # 构建配置（PTX + Host 双编译）
├── README.md
├── ARCHITECTURE.md          # ← 本文件
├── include/
│   ├── optix_check.h        # CUDA / OptiX 错误检查宏
│   ├── ray_types.h          # Ray / HitResult 共享结构体
│   ├── launch_params.h      # LaunchParams / HitGroupData / RayType
│   ├── nn_types.h           # NNResult 共享结构体
│   └── nn_launch_params.h   # NNLaunchParams（NN 管线参数）
├── device/
│   ├── programs.cu          # OptiX 设备程序（→ PTX）
│   ├── kernels.h            # CUDA kernel 声明
│   ├── kernels.cu           # 光线生成 / 命中统计 kernel
│   ├── nn_programs.cu       # NN OptiX 设备程序（→ nn_programs.ptx）
│   ├── nn_kernels.h         # NN CUDA kernel 声明
│   └── nn_kernels.cu        # AABB 生成 / 查询生成 / 命中统计 kernel
└── src/
    ├── device_manager.h/cpp   # 设备初始化
    ├── buffer_manager.h       # CudaBuffer<T> 模板（header-only）
    ├── geometry_manager.h/cpp # 几何场景生成
    ├── accel_manager.h/cpp    # GAS / IAS 构建与压缩
    ├── pipeline_manager.h/cpp # Module → PG → Pipeline → SBT
    ├── ray_tracer.h/cpp       # 追踪请求分发与计时
    ├── nn_query.h/cpp         # 最近点查询模块（独立管线 + GAS + 分发）
    └── main.cpp               # 基准测试编排
```

---

## 2. 模块 API 速览

### 2.1 共享头文件（`include/`）

#### `optix_check.h` — 错误检查

| 宏 | 用途 |
|---|---|
| `CUDA_CHECK(call)` | 包裹 CUDA API，失败抛 `std::runtime_error` |
| `CUDA_SYNC_CHECK()` | `cudaDeviceSynchronize` + `cudaGetLastError` |
| `OPTIX_CHECK(call)` | 包裹 OptiX API |
| `OPTIX_CHECK_LOG(call)` | 同上，附带编译日志（`s_optix_log[4096]`） |

#### `ray_types.h` — 光线 / 命中结构

```cpp
struct Ray {
    float3 origin;    float tmin;
    float3 direction; float tmax;
};
struct HitResult {
    float t;           // < 0 = miss
    float bary_u, bary_v;
    unsigned int prim_idx;
};
```

#### `launch_params.h` — OptiX Launch 参数

```cpp
struct LaunchParams {
    OptixTraversableHandle handle;
    Ray*       rays;
    HitResult* hits;
    unsigned int num_rays;
};
struct HitGroupData { float3* vertices; uint3* indices; };
enum RayType { RAY_TYPE_RADIANCE = 0, RAY_TYPE_COUNT };
```

#### `nn_types.h` — NN 结果结构

```cpp
struct NNResult {
    float        distance;     // Euclidean distance; < 0 = no match
    unsigned int point_idx;    // index into point cloud
};
```

#### `nn_launch_params.h` — NN OptiX Launch 参数

```cpp
struct NNLaunchParams {
    OptixTraversableHandle handle;   // AABB GAS
    float3*       queries;           // 查询点缓冲区
    float3*       points;            // 点云数据
    NNResult*     results;           // 输出结果
    unsigned int  num_queries;       // 查询数量
};
```

---

### 2.2 Host 模块（`src/`）

#### `DeviceManager`

| 方法 | 说明 |
|---|---|
| `init(device_id, enable_validation)` | CUDA 设备 + OptiX context 初始化 |
| `shutdown()` | 销毁 OptiX context |
| `getContext()` | 返回 `OptixDeviceContext` |
| `printDeviceInfo()` | 打印 GPU 名称 / 显存 / 计算能力 |

#### `CudaBuffer<T>`（header-only RAII 模板）

| 方法 | 说明 |
|---|---|
| `alloc(count)` / `free()` | `cudaMalloc` / `cudaFree` |
| `upload` / `uploadAsync` | 同步 / 异步 H→D |
| `download` / `downloadAsync` | 同步 / 异步 D→H |
| `zero()` / `zeroAsync()` | `cudaMemset` 清零 |
| `get()` / `devicePtr()` | 裸指针 / `CUdeviceptr` |

不可复制，仅可移动；析构自动释放。

#### `GeometryManager`

纯静态方法，返回 `TriangleMesh { vertices, indices, bbox_min, bbox_max }`：

| 方法 | 几何量 |
|---|---|
| `createSingleTriangle()` | 1 三角形 |
| `createCornellBox()` | ~30 三角形（5 墙 + 2 盒子） |
| `createRandomTriangles(N)` | N 个随机小三角形 |
| `createTriangleGrid(grid)` | $2 \times \text{grid}^2$ 三角形均匀网格 |

#### `AccelManager`

| 方法 | 说明 |
|---|---|
| `buildGAS(ctx, mesh, compact=true)` | 构建 GAS（含可选压缩） |
| `buildIAS(ctx, instances)` | 构建 IAS |
| `cleanup()` | 释放所有 tracked 缓冲区 |

返回 `AccelStructure { handle, buffer, buffer_size }`。

#### `PipelineManager`

| 方法 | 说明 |
|---|---|
| `create(ctx, ptx, payloads=4, attribs=2, depth=1, instancing=false)` | 完整管线创建 |
| `cleanup()` | 按序销毁 SBT→Pipeline→PG→Module |

#### `RayTracer`

| 方法 | 说明 |
|---|---|
| `init(pipeline, sbt, handle)` | 绑定管线 / SBT / 遍历句柄 |
| `traceSingle(ray)` | 调试用单光线追踪 |
| `traceBatch(d_rays, d_hits, count, stream)` | 批量追踪（设备指针） |
| `traceBatchTimed(d_rays, d_hits, count, iters, warm_up, stream)` | 带计时批量追踪 |

返回 `TraceResult { num_rays, trace_time_ms, mrays_per_sec, grays_per_sec, num_hits, hit_rate }`。

#### `NNQuery`（最近点查询模块）

自包含模块，管理独立的 OptiX 管线（custom primitives）、AABB GAS、查询分发。基于 RTNN (PPoPP 2022) 算法。

| 方法 | 说明 |
|---|---|
| `init(context, ptx, instancing)` | 创建 custom primitives 管线（`nn_params`） |
| `setPointCloud(points, radius)` | 上传点云 + 生成 AABB + 构建/压缩 GAS |
| `querySingle(query_point)` | 同步单点查询（调试用） |
| `queryBatch(d_queries, d_results, count, stream)` | 高吞吐批量查询（设备指针） |
| `queryBatchTimed(d_queries, d_results, count, iters, warm_up, stream)` | 带 GPU 计时的批量查询 |
| `queryBatch(vector<float3>)` | 便捷接口（含上传/下载） |
| `cleanup()` | 按序释放 SBT → Pipeline → PG → Module → GAS → Points |

返回 `NNQueryResult { num_queries, query_time_ms, mqueries_per_sec, gqueries_per_sec, num_found, found_rate }`。

**核心算法**：将 N 个空间点表示为 N 个 AABB（立方体，半宽 = search_radius），查询点作为零长度光线发射，intersection program 计算精确欧氏距离并通过 payload 寄存器更新最近点。不调用 `optixReportIntersection` → BVH 遍历穷举所有重叠 AABB。

---

### 2.3 Device 模块（`device/`）

#### `programs.cu`（编译为 PTX）

| 入口函数 | 说明 |
|---|---|
| `__raygen__rg` | 读 `params.rays[idx]` → `optixTrace` → 写 `params.hits[idx]` |
| `__miss__ms` | 设置 t = −1, prim_idx = 0xFFFFFFFF |
| `__closesthit__ch` | 记录 t / barycentrics / prim_idx（built-in 三角形交叉） |

**Payload 布局**（4 × uint32 寄存器）：

| Slot | 内容 |
|---|---|
| p0 | `__float_as_uint(t)` |
| p1 | `__float_as_uint(bary_u)` |
| p2 | `__float_as_uint(bary_v)` |
| p3 | `prim_idx` |

**2D Launch 索引**：`linear_idx = idx.y × dim.x + idx.x`，超越 `num_rays` 时直接 return。

#### `kernels.cu`（编译为原生 CUDA object）

| 函数 | 说明 |
|---|---|
| `generateRandomRaysDevice(d_rays, count, bbox, seed, stream)` | Marsaglia 球面均匀方向 + 球面外起点 |
| `generateOrthoRaysDevice(d_rays, w, h, bbox, stream)` | 正交光线网格（+Z 方向） |
| `countHitsDevice(d_hits, count, d_count, stream)` | 两级 atomicAdd 统计命中数 |

#### `nn_programs.cu`（编译为 PTX）

| 入口函数 | 说明 |
|---|---|
| `__raygen__nn` | 读 `nn_params.queries[idx]` → 发射零长度光线 → 读 payload → 写 `nn_params.results[idx]` |
| `__intersection__nn` | 计算查询到点的欧氏距离²，与 payload 中当前最小值比较，更新 payload；不调用 `optixReportIntersection` |
| `__miss__nn` | 空操作（结果已在 payload 寄存器中） |

**Payload 布局**（2 × uint32 寄存器）：

| Slot | 内容 |
|---|---|
| p0 | `__float_as_uint(dist_sq)` — 当前最小距离² |
| p1 | `point_idx` — 最近点索引（0xFFFFFFFF = 未找到） |

**零长度光线**：方向 `(1,0,0)`, `tmin=0`, `tmax=1e-16`。光线原点 = 查询点位置。OptiX BVH 遍历找到所有包含该查询点的 AABB。

**RTNN 核心技巧**：不调用 `optixReportIntersection()`，因此 OptiX 认为没有交叉发生，继续遍历所有重叠 AABB → 穷举搜索。最近点通过 payload 寄存器在调用间传递。

#### `nn_kernels.cu`（编译为原生 CUDA object）

| 函数 | 说明 |
|---|---|
| `generateAABBsDevice(d_aabbs, d_points, count, radius, stream)` | 每点 → 6 floats AABB（min/max = point ∓ radius） |
| `generateRandomQueriesDevice(d_queries, count, bbox_min, bbox_max, seed, stream)` | bbox 内均匀随机查询点 |
| `countNNHitsDevice(d_results, count, d_count, stream)` | 两级 atomicAdd 统计 distance ≥ 0 的有效结果数 |

---

## 3. 数据流

```
┌─────────────────┐    TriangleMesh (host)     ┌──────────────┐
│ GeometryManager │ ──────────────────────────►│ AccelManager │
│  (static)       │   vertices[], indices[]    │ buildGAS()   │
└─────────────────┘                            │ buildIAS()   │
                                               └──────┬───────┘
                                                      │ AccelStructure
                                                      │ {handle, buffer}
                                                      ▼
┌─────────────────┐  OptixDeviceContext  ┌──────────────────┐
│ DeviceManager   │ ───────────────────►│ PipelineManager  │
│ init()          │                     │ create(ctx, ptx)  │
│ getContext()    │                     └───────┬──────────┘
└─────────────────┘                             │ Pipeline + SBT
                                                ▼
  kernels.cu                         ┌───────────────────┐
  generateRandomRays ──► Ray* ─────►│    RayTracer       │
       (device)                      │ traceBatch()       │
                                     │ traceBatchTimed()  │
                                     └────────┬──────────┘
                                              │ optixLaunch
                                              ▼
                                     ┌───────────────────┐
                                     │   programs.cu     │
                                     │   (OptiX device)  │
                                     │ __raygen__rg      │
                                     │ __closesthit__ch  │
                                     │ __miss__ms        │
                                     └────────┬──────────┘
                                              │
                                              ▼
  kernels.cu                          HitResult* (device)
  countHitsDevice ◄──────────────────
       │
       ▼
  TraceResult (host)  ──►  CSV / stdout
```

### NN 查询数据流

```
  point_cloud (host)               findAndLoadNNPtx()
       │                                 │
       │  NNQuery::setPointCloud()        │  nn_programs.ptx (text)
       ▼                                  ▼
  ┌──────────────────┐            ┌──────────────────┐
  │   m_d_points     │            │  NNQuery::init()  │
  │   (device)       │            │  createPipeline()  │
  └────────┬─────────┘            └────────┬─────────┘
           │                               │
           │ generateAABBsDevice()         │ Module → PG → Pipeline → SBT
           ▼                               │ (nn_params, CUSTOM primitives)
  ┌──────────────────┐                     │
  │   d_aabbs        │                     │
  │   (device)       │                     │
  └────────┬─────────┘                     │
           │ optixAccelBuild               │
           │ (CUSTOM_PRIMITIVES)           │
           ▼                               ▼
  ┌──────────────────┐            ┌──────────────────┐
  │   AABB GAS       │            │  OptiX Pipeline   │
  │   (compacted)    │─────────►  │  (custom prims)   │
  └──────────────────┘            └────────┬─────────┘
                                           │
  nn_kernels.cu                            │
  generateRandomQueries ──► float3* ──────►│
       (device)                            │
                                           │ optixLaunch
                                           ▼
                                  ┌──────────────────┐
                                  │  nn_programs.cu   │
                                  │  __raygen__nn     │
                                  │  __intersection__nn│
                                  │  __miss__nn       │
                                  └────────┬─────────┘
                                           │
                                           ▼
  nn_kernels.cu                   NNResult* (device)
  countNNHitsDevice ◄────────────
       │
       ▼
  NNQueryResult (host)  ──►  CSV / stdout
```

**关键数据流向**：

1. **GeometryManager** → `TriangleMesh` (host) → **AccelManager** 消费并上传到 GPU 构建 BVH
2. **DeviceManager** → `OptixDeviceContext` → AccelManager + PipelineManager 消费
3. **PipelineManager** → `Pipeline` + `SBT` → **RayTracer** 消费
4. **AccelManager** → `AccelStructure.handle` → **RayTracer** 消费
5. **kernels.cu** → 设备端 `Ray*` 缓冲区 → **RayTracer** 通过 `optixLaunch` 传递给 raygen 程序
6. **programs.cu** → 设备端 `HitResult*` 缓冲区 → **kernels.cu** (`countHitsDevice`) 或 host 下载

---

## 4. 控制流 — main.cpp 精确调用序列

```
main(argc, argv)
│
├── 1. 解析 CLI（--no-validate  --max-rays  --iters  --device  --csv）
│
├── 2. device.init(device_id)
│      └── printDeviceInfo()
│
├── 3. findAndLoadPtx()
│      └── pipeline.create(context, ptx, 4, 2, 1, false)
│           └── createModule → createProgramGroups → createPipeline → createSBT
│
├── 4. GeometryManager::createCornellBox()
│      └── accel.buildGAS(context, mesh, compact=true)
│
├── 5. tracer.init(pipeline, sbt, gas.handle)
│
├── 6. [可选] runValidation()
│      ├── traceSingle(ray_hit)     → 验证命中
│      ├── traceSingle(ray_miss)    → 验证未命中
│      └── traceBatch(100 rays)     → 验证批量
│
├── 7. 基准测试
│      ├── 7a. benchmarkRayCountSweep
│      │       循环 {1K → max_rays}:
│      │         alloc → generateRandomRays → traceBatchTimed
│      │
│      ├── 7b. benchmarkSceneComplexity
│      │       循环 {1 → 1M} 三角形:
│      │         create geometry → buildGAS → traceBatchTimed
│      │
│      ├── 7c. benchmarkInstancing
│      │       pipeline.create(use_instancing=true)
│      │       循环 {1 → 256} 实例:
│      │         buildIAS → traceBatchTimed
│      │
│      └── 7d. benchmarkMultiStream
│              循环 {1, 2, 4, 8} 流:
│                cudaStreamCreate × N
│                生成光线 → warm-up → timed traceBatch × N
│                cudaStreamDestroy × N
│
├── 7. NN 最近点查询测试
│      ├── findAndLoadNNPtx()
│      │      └── nn.init(context, nn_ptx)
│      │
│      ├── generateRandomPointCloud(100K, extent=10)
│      │      └── nn.setPointCloud(points, radius=avg_spacing*3)
│      │           └── buildGAS (custom primitives, compaction)
│      │
│      ├── 7a. NN 验证
│      │       ├── querySingle(point_cloud[0]) → 自查询 (dist~0)
│      │       ├── querySingle(point_cloud[42] + offset) → 近查询
│      │       ├── querySingle(far_point) → 远查询 (miss)
│      │       └── queryBatch(100 known points) → 批量验证
│      │
│      ├── 7b. NN Query Count Sweep
│      │       循环 {1K → max_rays}:
│      │         alloc → generateRandomQueries → queryBatchTimed
│      │
│      └── 7c. NN Point Count Sweep
│              循环 {100 → 1M} 点:
│                generateRandomPointCloud → setPointCloud → buildGAS → queryBatchTimed
│
├── 8. csv.close()
│
└── 9. cleanup: nn → pipeline → accel → device.shutdown
```

---

## 5. OptiX 管线构建过程

### 5.1 管线拓扑

```
       programs.cu (source)
             │
             │  nvcc --ptx -arch=compute_70
             ▼
       programs.ptx (text)
             │
             │  optixModuleCreate
             ▼
       OptixModule
             │
             │  optixProgramGroupCreate × 3
     ┌───────┼───────────┐
     ▼       ▼           ▼
  Raygen    Miss     HitGroup
  PG        PG       PG
     │       │           │
     └───────┼───────────┘
             │  optixPipelineCreate
             ▼
       OptixPipeline
             │
             │  optixUtilComputeStackSizes
             │  optixPipelineSetStackSize
             ▼
       Ready to launch
```

### 5.2 关键管线选项

| 选项 | 值 | 说明 |
|---|---|---|
| `usesPrimitiveTypeFlags` | `TRIANGLE` | 仅使用 built-in 三角形交叉 |
| `traversableGraphFlags` | `ALLOW_SINGLE_GAS` 或 `ALLOW_SINGLE_LEVEL_INSTANCING` | 取决于 `use_instancing` |
| `numPayloadValues` | 4 | t + bary_u + bary_v + prim_idx |
| `numAttributeValues` | 2 | built-in 三角形重心坐标 |
| `maxTraceDepth` | 1 | 无递归追踪 |
| `pipelineLaunchParamsVariableName` | `"params"` | 对应设备端 `__constant__ LaunchParams params` |

### 5.3 SBT 布局

| Section | Record | 数量 | Payload |
|---|---|---|---|
| Raygen | `SbtRecord<EmptyData>` | 1 | 仅 header |
| Miss | `SbtRecord<EmptyData>` | 1 | 仅 header |
| HitGroup | `SbtRecord<HitGroupData>` | 1 | header + `{vertices, indices}` |

**SBT 寻址**：`optixTrace` 参数 `sbtOffset = 0`, `sbtStride = 1`, `missSbtIndex = 0`。

### 5.4 NN 管线（Custom Primitives）

NN 查询使用独立管线，与光追管线完全隔离。

#### 管线拓扑

```
       nn_programs.cu (source)
             │  nvcc --ptx -arch=compute_70
             ▼
       nn_programs.ptx (text)
             │  optixModuleCreate
             ▼
       OptixModule
             │  optixProgramGroupCreate × 3
     ┌───────┼───────────┐
     ▼       ▼           ▼
  Raygen    Miss     HitGroup
  PG        PG       PG (IS only)
     └───────┼───────────┘
             │  optixPipelineCreate
             ▼
       OptixPipeline  ("nn_params")
```

#### NN 管线选项（与光追管线对比）

| 选项 | 光追管线 | **NN 管线** |
|---|---|---|
| `usesPrimitiveTypeFlags` | `TRIANGLE` | **`CUSTOM`** |
| `numPayloadValues` | 4 | **2** |
| `numAttributeValues` | 2 | **2** (最小值) |
| `pipelineLaunchParamsVariableName` | `"params"` | **`"nn_params"`** |
| HitGroup 入口 | CH (closesthit) | **IS (intersection)** — 无 CH/AH |
| `maxTraceDepth` | 1 | 1 |

#### NN GAS 构建

```
点云 float3[]  ──►  generateAABBsDevice()  ──►  6 floats/点 (min,max)
                                                     │
                         OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES
                         flags = GEOMETRY_FLAG_NONE
                         strideInBytes = 0 (tight packing)
                                                     │
                         optixAccelBuild + compact  ──►  AABB GAS
```

#### NN SBT 布局

| Section | Record | 数量 | Payload |
|---|---|---|---|
| Raygen | `NNSbtRecord<NNEmptyData>` | 1 | 仅 header |
| Miss | `NNSbtRecord<NNEmptyData>` | 1 | 仅 header |
| HitGroup | `NNSbtRecord<NNEmptyData>` | 1 | 仅 header（无 SBT 数据） |

---

## 6. Host ↔ Device 数据通道

| 通道 | 机制 | 方向 | 频率 |
|---|---|---|---|
| Launch Params | `optixLaunch(d_params, sizeof)` → `__constant__` | H → D | 每次 launch |
| 光线缓冲区 | `params.rays`（设备指针） | D → D | kernel 生成 → raygen 读取 |
| 命中结果 | `params.hits`（设备指针） | D → D / D → H | raygen 写入 → kernel / host 读取 |
| SBT Records | `cudaMemcpy` 上传 | H → D | 一次性 |
| Payload | 4 × uint32 寄存器 (p0–p3) | 设备内部 | CH/Miss → Raygen |
| GAS/IAS 输入 | `cudaMemcpy`（vertices, indices, instances） | H → D | 构建时 |
| **NN Launch Params** | `optixLaunch(d_params, sizeof)` → `__constant__ nn_params` | H → D | 每次 NN launch |
| **NN Payload** | 2 × uint32 寄存器 (p0–p1) | 设备内部 | IS → Raygen |
| **NN AABB 输入** | `generateAABBsDevice` kernel | D → D | GAS 构建时 |
| **NN 查询/结果** | `nn_params.queries` / `nn_params.results`（设备指针） | D → D / D → H | kernel 生成 → raygen 读/写 |

---

## 7. 内存管理模式

### 7.1 分配 / 释放责任

| 资源 | 分配者 | 释放者 | 机制 |
|---|---|---|---|
| OptiX Context | `DeviceManager::init` | `DeviceManager::shutdown` | `optixDeviceContextDestroy` |
| Module / PG / Pipeline | `PipelineManager::create*` | `PipelineManager::cleanup` | `optix*Destroy` |
| SBT 设备缓冲区 | `PipelineManager::createSBT` | `PipelineManager::cleanup` | `cudaFree` |
| GAS / IAS 缓冲区 | `AccelManager::build*` | `AccelManager::cleanup` | tracked `m_allocated_buffers` |
| 临时构建缓冲区 | `AccelManager::build*` | 同函数内 | `cudaFree` 即时释放 |
| `CudaBuffer<T>` | 各调用者 | 析构函数（RAII） | `cudaFree` |
| Launch params (`traceBatch`) | `traceBatch` | `traceBatch` | 每次调用 malloc/free |
| **NN Pipeline / SBT** | `NNQuery::init` | `NNQuery::cleanup` | `optix*Destroy` + `cudaFree` |
| **NN AABB GAS** | `NNQuery::buildGAS` | `NNQuery::freeGAS` / `cleanup` | `cudaFree` |
| **NN 点云缓冲区** | `NNQuery::setPointCloud` | `NNQuery::cleanup` | `CudaBuffer` RAII |
| **NN Launch params** | `NNQuery::queryBatch` | `NNQuery::queryBatch` | 每次调用 malloc/free |

### 7.2 模式总结

1. **RAII (`CudaBuffer<T>`)**：所有临时 GPU 缓冲区走析构释放
2. **Tracked Pool (`AccelManager`)**：长生命周期 GAS/IAS 缓冲区由 `m_allocated_buffers` 统一管理
3. **即分即释 (`traceBatch`)**：每次 launch 分配释放 `LaunchParams`（潜在优化点）
4. **懒分配 (`traceSingle`)**：单光线缓冲区首次调用分配，后续复用
5. **构建临时资源**：`buildGAS` 中间缓冲区（temp, vertices, indices）函数内即释

---

## 8. CUDA Stream 使用

### 8.1 默认流 (stream 0)

大部分操作在默认流上执行：

- `buildGAS` / `buildIAS`：`optixAccelBuild(ctx, 0, ...)`
- `traceSingle`：`optixLaunch(pipeline, 0, ...)`
- `traceBatch`：接受可选 `CUstream` 参数

### 8.2 Multi-Stream 并发 (benchmarkMultiStream)

```
cudaStreamCreate(&streams[0..N-1])

// 各流独立生成光线
for s in 0..N-1:
    generateRandomRaysDevice(..., streams[s])
cudaDeviceSynchronize()

// Warm-up
for w = 0..2:
    for s: traceBatch(..., streams[s])
    sync

// 计时
for iter:
    cudaEventRecord(start, 0)
    for s: traceBatch(..., streams[s])   // 并发 launch
    cudaEventRecord(stop, 0)
    cudaEventSynchronize(stop)
    elapsed = cudaEventElapsedTime(start, stop)

cudaStreamDestroy × N
```

**注意**：计时 event 录在 stream 0 上，依赖默认流隐式同步语义。每个流有独立缓冲区，无跨流竞争。

### 8.3 Async 传输能力

`CudaBuffer` 提供 `uploadAsync` / `downloadAsync` / `zeroAsync`，当前项目未直接使用（光线在 GPU 端生成），接口为扩展保留。

---

## 9. 构建系统

### 双编译模式

| 目标 | 源文件 | 编译方式 | 产物 |
|---|---|---|---|
| 光追 PTX | `device/programs.cu` | `nvcc --ptx -arch=compute_70` | `programs.ptx` |
| NN PTX | `device/nn_programs.cu` | `nvcc --ptx -arch=compute_70` | `nn_programs.ptx` |
| 主程序 | `src/*.cpp` + `device/kernels.cu` + `device/nn_kernels.cu` | MSVC + nvcc (`sm_86`) | `optix_throughput.exe` |

### 关键配置

- **OptiX**: Header-only（`optix_stubs.h` 动态加载函数表），不链接 OptiX 库
- **PTX 路径**: 编译定义 `OPTIX_PTX_FILE_PATH` + `OPTIX_NN_PTX_FILE_PATH`，运行时多路径搜索
- **Post-build**: 两个 PTX 文件复制到可执行文件目录
- **NOMINMAX**: `pipeline_manager.cpp` 和 `nn_query.cpp` 中定义，避免 Windows `max` 宏冲突

### 构建命令

```bash
cd optix-throughput-validation
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

---

## 10. 基准测试方法

### 计时策略

- **CUDA Events** 精确 GPU 计时（`cudaEventRecord` / `cudaEventElapsedTime`）
- **Warm-up**: 3 次预热，排除 JIT 编译 / 缓存冷启动
- **统计**: 多次迭代取**中位数**，消除异常值
- **命中统计**: 通过 `countHitsDevice` CUDA kernel 在 GPU 端完成

### 基准项

| 基准 | 变量 | 目的 |
|---|---|---|
| Ray Count Sweep | 1K → 64M 光线 | 吞吐量随批次规模的缩放行为 |
| Scene Complexity | 1 → 1M 三角形 | BVH 复杂度对追踪性能的影响 |
| Instancing | 1 → 256 实例 | IAS 层级开销 |
| Multi-Stream | 1 → 8 CUDA 流 | 并发 launch 收益 |
| **NN Query Sweep** | 1K → 1M 查询, 100K 点 | NN 吞吐量随查询规模的缩放行为 |
| **NN Point Sweep** | 100 → 1M 点, 1M 查询 | 点云规模对 NN 查询性能的影响 |

### RTX 3070 Laptop 基准结果摘要

#### 光追

| 场景 | 光线数 | 吞吐量 |
|---|---|---|
| Cornell Box, 1K rays | ~0.02 GRays/s | Launch 开销主导 |
| Cornell Box, 1M rays | ~3.2 GRays/s | 接近饱和 |
| Cornell Box, 64M rays | **~3.8 GRays/s** | 峰值 |
| 1M 三角形, 1M rays | ~2.5 GRays/s | BVH 遍历开销增加 |

#### NN 查询

**Query Count Sweep** (100K 点, radius ≈ 0.65):

| 查询数 | 时间 (ms) | MQuery/s | 命中率 |
|---|---|---|---|
| 1K | 0.572 | 1.79 | 100% |
| 16K | 0.683 | 24.0 | 100% |
| 1M | 35.6 | **29.5** | 100% |

**Point Count Sweep** (1M 查询):

| 点数 | GAS 构建 (ms) | 查询 (ms) | MQuery/s |
|---|---|---|---|
| 100 | 0.7 | 6.5 | 160 |
| 10K | 1.5 | 29.9 | 35 |
| 100K | 3.8 | 35.8 | 29 |
| 1M | 20.8 | 84.5 | **12.4** |

**观察**：NN 查询吞吐量比光追低 ~100×（~29 MQuery/s vs ~3800 MRays/s），因为 RTNN 算法需穷举遍历所有与查询点重叠的 AABB，每个 intersection program 调用涉及距离计算 + payload 更新。点数增大时吞吐量下降，因为搜索空间和 BVH 遍历深度增加。

---

## 11. 已知的优化空间

1. **`traceBatch` / `queryBatch` 每次调用 `cudaMalloc` / `cudaFree`**：Launch params 缓冲区应预分配复用
2. **Multi-stream 场景下 `cudaMalloc` 全局锁**：多流并发时内存分配序列化
3. **SBT HitGroupData 未使用**：`vertices` / `indices` 为 NULL，若需逐图元着色应填充
4. **PTX 使用 `compute_70`**：可提升到 `compute_86` 以利用 SM86 特性
5. **`CudaBuffer` async 接口**：已实现但未使用，可用于 H←→D 传输与计算重叠
6. **NN 搜索半径自适应**：当前使用固定 `3 × avg_spacing`，可根据点密度局部调整
7. **NN AABB GAS 重建开销**：每次 `setPointCloud` 完整重建，可考虑增量更新
8. **NN Pipeline 共享 Context**：`NNQuery` 接受外部 `OptixDeviceContext`，与光追管线共用设备初始化
