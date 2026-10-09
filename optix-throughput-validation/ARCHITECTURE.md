# OptiX Throughput Validation — 架构文档

**项目**: OptiX 9.1.0 统一管线 — 光追 + 最近面查询 (Closest-Point-on-Triangle) + Multi-Hit + 球体 + 多几何体/IAS + 吞吐量基准测试  
**GPU**: NVIDIA RTX 3070 Laptop (sm_86, 8 GB)  
**峰值光追吞吐量**: ~3.3 GRays/s @ 4M rays  
**峰值 CP 吞吐量**: ~2.2 GQuery/s @ 1M queries, 1 tri (intersection-bound ~12.6 MQuery/s @ 100K tris)  
**IAS 峰值**: ~3.2 GRays/s @ 1 instance, ~2.7 GRays/s @ 10K instances (300K tris)  
**架构**: 统一管线 — 1 次初始化, 2 modules, **12 PGs**, **4 SBTs** (RT/MH/NN/CP), 多几何体 IAS, 9-slot payload  
**扩展状态**: E1 ✅ E2 ✅ E3 ✅ E4 ✅ E5 🔶(数据结构) E6 ✅  
**编写时间**: 2026-02  
**最后更新**: 2026-02-26

---

## 1. 项目概览

本项目使用 **OptiX 9.1.0** 实现**统一管线架构**：一次初始化即可同时响应光线追踪、Multi-Hit 追踪、最近面查询 (Closest-Point-on-Triangle) 和增强 CP 查询请求，支持多种几何体（三角形 + 球体）、查询批次间的场景修改和 IAS 多几何体场景。

核心能力：

- **单管线多模态**：一个 `OptixPipeline` 包含 RT + NN(Closest-Point) 两个模块, 12 个 Program Groups, 4 套 SBT
- **多几何体管理 (E6)**：`addGeometryMesh` / `addGeometrySphere` / `removeGeometry` / `enableGeometry` / `rebuildScene`，每个几何体独立 GAS → IAS 组合
- **扩展命中结果 (E1)**：`HitResult` 含 `geom_id`, `inst_id`, `normal[3]`，9-slot payload
- **Multi-Hit (E3)**：`traceBatchMultiHit` 返回 Top-K 命中（any-hit + global memory）
- **球体几何 (E4)**：custom AABB primitive + intersection program，支持 RT 和 CP 查询
- **增强 CP (E2)**：`closestPointBatch(CPQuery → CPResult)` 含法线、UV、几何 ID
- **包壳查询 (E5)**：数据结构已定义（`EnclosureQuery`/`EnclosureResult`），dispatch 函数待实现
- **高吞吐批量请求**：缓冲区驱动的 `optixLaunch`，支持多流并发
- 完整的 GPU 计时基准测试（中位数统计）

### 目录结构

```
optix-throughput-validation/
├── CMakeLists.txt           # 构建配置（PTX + Host 双编译）
├── README.md
├── ARCHITECTURE.md          # ← 本文件
├── include/
│   ├── optix_check.h        # CUDA / OptiX 错误检查宏
│   ├── ray_types.h          # Ray / HitResult 共享结构体
│   ├── nn_types.h           # NNResult 共享结构体（最近面查询结果）
│   └── unified_params.h     # UnifiedParams / HitGroupData / RayType（双模块共享）
├── device/
│   ├── programs.cu          # RT 设备程序（→ programs.ptx）
│   ├── kernels.h/cu         # 光线生成 / 命中统计 kernel
│   ├── nn_programs.cu       # CP 设备程序 — 最近面查询（→ nn_programs.ptx）
│   └── nn_kernels.h/cu      # 三角形 AABB 生成 / 查询生成 / 命中统计 kernel
└── src/
    ├── device_manager.h/cpp   # 设备初始化
    ├── buffer_manager.h       # CudaBuffer<T> 模板（header-only RAII）
    ├── geometry_manager.h/cpp # 几何场景生成
    ├── unified_tracer.h/cpp   # 统一追踪器（管线 + GAS + 分发 + 场景管理）
    └── main.cpp               # 基准测试编排
```

> **旧文件**（保留但不再参与构建）：`pipeline_manager.*`, `accel_manager.*`, `ray_tracer.*`, `nn_query.*`, `launch_params.h`, `nn_launch_params.h`

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

#### `ray_types.h` — 光线 / 命中 / 球体结构

```cpp
struct Ray {
    float3 origin;    float tmin;
    float3 direction; float tmax;
};
struct HitResult {             /* E1: extended */
    float t;                   /* < 0 = miss */
    float bary_u, bary_v;
    unsigned int prim_idx;     /* GAS-local primitive index */
    unsigned int geom_id;      /* geometry ID from SBT record */
    unsigned int inst_id;      /* instance ID (0xFFFFFFFF = no instancing) */
    float normal[3];           /* un-normalized geometric normal */
};
struct MultiHitResult {        /* E3 */
    unsigned int count;                    /* [0, MAX_MULTI_HITS] */
    HitResult    hits[MAX_MULTI_HITS];     /* sorted ascending by t */
};
struct SphereData {            /* E4 */
    float3 center;
    float  radius;
};
```

#### `nn_types.h` — 最近面 / 增强 CP / 包壳查询结构

```cpp
struct NNResult {              /* legacy simple CP result */
    float        distance;     /* < 0 = no match */
    unsigned int prim_idx;
};
struct CPQuery {               /* E2: enhanced CP input */
    float3 position;
    float  radius;             /* max search radius */
};
struct CPResult {              /* E2: enhanced CP output */
    float        distance;     /* < 0 = miss */
    float        normal[3];    /* geometric normal */
    float        uv[2];        /* barycentric coords of closest point */
    unsigned int prim_idx;
    unsigned int geom_id;
    unsigned int inst_id;      /* 0xFFFFFFFF if none */
};
struct EnclosureQuery {        /* E5 */
    float3 position;
};
struct EnclosureResult {       /* E5 */
    int          prim_idx;     /* -1 = miss */
    float        distance;
    int          side;         /* 0=front, 1=back, -1=degenerate */
    unsigned int geom_id;
    unsigned int inst_id;
};
```

#### `unified_params.h` — 统一 Launch 参数

```cpp
/* Geometry type constants (E4/E6) */
#define GEOM_TYPE_TRIANGLE 0u
#define GEOM_TYPE_SPHERE   1u

struct UnifiedParams {
    OptixTraversableHandle handle;  /* Current GAS or IAS */
    unsigned int           count;   /* num_rays or num_queries */

    /* RT fields */
    Ray*       rays;
    HitResult* hits;

    /* Multi-hit fields (E3) */
    MultiHitResult* multi_hits;

    /* Closest-point query fields */
    float3*    queries;
    float3*    nn_vertices;
    uint3*     nn_indices;
    NNResult*  results;

    /* Enhanced CP fields (E2) */
    CPQuery*   cp_queries;
    CPResult*  cp_results;

    /* Sphere CP data (E4) */
    float3*    sphere_centers;
    float*     sphere_radii;

    /* Enclosure fields (E5) */
    EnclosureResult* enclosure_results;
};

struct HitGroupData {          /* E1/E4/E6: per-geometry SBT data */
    unsigned int geom_id;
    unsigned int geom_type;    /* GEOM_TYPE_TRIANGLE or GEOM_TYPE_SPHERE */
    float3*      vertices;     /* triangle vertices (MESH only) */
    uint3*       indices;      /* triangle indices  (MESH only) */
    float3*      sphere_centers; /* SPHERE only */
    float*       sphere_radii;   /* SPHERE only */
};
enum RayType { RAY_TYPE_RADIANCE = 0, RAY_TYPE_COUNT };
```

两个 PTX 模块共享同一 `UnifiedParams` 结构体，每个模块只读取自己需要的字段。

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

纯静态方法，返回 `TriangleMesh { vertices, indices, bbox_min, bbox_max }` 或 `SphereMesh { centers, radii }`：

| 方法 | 几何量 |
|---|---|
| `createSingleTriangle()` | 1 三角形 |
| `createCornellBox()` | ~30 三角形（5 墙 + 2 盒子） |
| `createRandomTriangles(N)` | N 个随机小三角形 |
| `createTriangleGrid(grid)` | $2 \times \text{grid}^2$ 三角形均匀网格 |
| `createSingleSphere(center, radius)` | 1 球体 (E4) |
| `createRandomSpheres(N)` | N 个随机球体 (E4) |

#### `UnifiedTracer`（核心统一追踪器）

管理单个 OptiX 管线（2 modules, **12 PGs**, **4 SBTs**, 多几何体 IAS），提供光追、Multi-Hit、CP 和增强 CP 查询的统一接口。

##### 初始化

| 方法 | 说明 |
|---|---|
| `init(context, rt_ptx, nn_ptx)` | 一次性创建统一管线（2 modules → 12 PGs → 1 pipeline → 4 base SBTs） |
| `cleanup()` | 按序释放所有资源 |

##### 多几何体场景管理 (E6)

| 方法 | 说明 |
|---|---|
| `addGeometryMesh(mesh, transform, compact)` | 添加三角形几何 → 构建 GAS → 返回 `geom_id` |
| `addGeometrySphere(spheres, transform)` | 添加球体几何 → 构建 GAS → 返回 `geom_id` |
| `removeGeometry(geom_id)` | 释放 GAS + 设备数据 → 重建 IAS/SBTs |
| `enableGeometry(geom_id, enable)` | enable/disable → 重建 IAS/SBTs |
| `rebuildScene()` | 构建/重建 IAS + 动态重建 RT/MH SBT |
| `clearAllGeometry()` | 清空所有几何体 + 释放 IAS |

##### Legacy 便利接口

| 方法 | 说明 |
|---|---|
| `setTriangleMesh(mesh, compact)` | `clearAllGeometry` + `addGeometryMesh` 简写 |
| `setTriangleInstances(mesh, transforms, N)` | 多实例（内部 N×addGeometryMesh） |

##### 查询网格管理（Closest-Point）

| 方法 | 说明 |
|---|---|
| `setQueryMesh(mesh, radius)` | 全量替换：复制 → 上传顶点/索引 → 生成三角形 AABB → 构建 GAS |
| `clearQueryMesh()` | 清空并释放 GAS + 设备顶点/索引数据 |
| `setSearchRadius(radius)` | 修改搜索半径（需后续 `rebuildQueryGAS`） |
| `rebuildQueryGAS(compact)` | 重新上传顶点 → 生成三角形 AABB → 重建 GAS |

##### 光线追踪（单命中）

| 方法 | 说明 | 返回 |
|---|---|---|
| `traceSingle(ray)` | 单光线追踪（调试） | `HitResult` (含 geom_id, inst_id, normal) |
| `traceBatch(d_rays, d_hits, count, stream)` | 批量追踪（设备指针） | — |
| `traceBatchTimed(d_rays, d_hits, count, iters, warm_up, stream)` | 带计时批量追踪 | `TraceResult` |
| `traceBatch(vector<Ray>)` | 便捷接口（含上传/下载） | `vector<HitResult>` |

##### Multi-Hit 查询 (E3)

| 方法 | 说明 | 返回 |
|---|---|---|
| `traceBatchMultiHit(d_rays, d_multi_hits, count, stream)` | 批量 Top-K 追踪（设备指针） | — |
| `traceBatchMultiHit(vector<Ray>)` | 便捷接口（含上传/下载） | `vector<MultiHitResult>` |

##### 最近面查询 — 简单接口 (NNResult)

| 方法 | 说明 | 返回 |
|---|---|---|
| `querySingle(query_point)` | 单点查询（调试） | `NNResult` |
| `queryBatch(d_queries, d_results, count, stream)` | 批量查询（设备指针） | — |
| `queryBatchTimed(d_queries, d_results, count, iters, warm_up, stream)` | 带计时批量查询 | `NNQueryResult` |
| `queryBatch(vector<float3>)` | 便捷接口（含上传/下载） | `vector<NNResult>` |

##### 增强 Closest-Point 查询 (E2: CPQuery → CPResult)

| 方法 | 说明 | 返回 |
|---|---|---|
| `closestPointBatch(d_queries, d_results, count, stream)` | 批量增强 CP（设备指针） | — |
| `closestPointBatch(vector<CPQuery>)` | 便捷接口（含上传/下载） | `vector<CPResult>` |

##### 访问器

| 方法 | 说明 |
|---|---|
| `getNumGeometries()` | 当前几何体数量 |
| `hasScene()` / `hasQueryMesh()` | 是否有活跃场景 / 查询网格 |
| `getNumQueryTriangles()` | 查询网格三角形数量 |
| `getSearchRadius()` | 当前 CP 搜索半径 |
| `getSceneBBoxMin/Max()` / `getQueryBBoxMin/Max()` | 包围盒 |
| `hasTriangles()` / `getTriBBoxMin/Max()` | Legacy 别名 |

---

### 2.3 Device 模块（`device/`）

#### `programs.cu`（编译为 PTX）

| 入口函数 | 说明 |
|---|---|
| `__raygen__rg` | 读 `params.rays[idx]` → `optixTrace` (`DISABLE_ANYHIT`) → 写 `params.hits[idx]` |
| `__raygen__mh` | 读 `params.rays[idx]` → `optixTrace` (`NONE`, 允许 any-hit) → 读 `params.multi_hits[idx]` (E3) |
| `__miss__ms` | 设置 t = −1, prim_idx = 0xFFFFFFFF, geom_id = 0xFFFFFFFF, inst_id = 0xFFFFFFFF |
| `__closesthit__ch` | 记录 t / barycentrics / prim_idx / geom_id / inst_id / normal; 通过 `optixIsTriangleHit()` 分支处理三角形 vs 球体 |
| `__anyhit__mh` | Multi-Hit (E3): 记录候选到 `params.multi_hits[idx]`, 按距离排序保留 Top-K |
| `__intersection__sphere` | 球体解析求交 (E4): 射线-球体 quadratic 求解 → `optixReportIntersection` |

声明：`extern "C" { __constant__ UnifiedParams params; }`

**Payload 布局**（**9** × uint32 寄存器）：

| Slot | 内容 |
|---|---|
| p0 | `__float_as_uint(t)` |
| p1 | `__float_as_uint(bary_u)` |
| p2 | `__float_as_uint(bary_v)` |
| p3 | `prim_idx` |
| p4 | `geom_id` (E1) |
| p5 | `inst_id` (E1) |
| p6 | `__float_as_uint(normal[0])` (E1) |
| p7 | `__float_as_uint(normal[1])` (E1) |
| p8 | `__float_as_uint(normal[2])` (E1) |

#### `nn_programs.cu`（编译为 PTX）— 最近面 + 增强 CP 查询

| 入口函数 | 说明 |
|---|---|
| `__raygen__nn` | 读 `params.queries[idx]` → 发射零长度光线 → 读 payload → 写 `params.results[idx]` |
| `__raygen__cp` | 读 `params.cp_queries[idx]` → 发射零长度光线 → 读 payload → 写 `params.cp_results[idx]` (E2) |
| `__intersection__nn` | 计算查询点到三角形面的最近距离²; Voronoi region 分类, 返回 u,v 重心坐标; 不调用 `optixReportIntersection` |
| `__intersection__nn_sphere` | 计算查询点到球面最近距离 (E4); point-to-sphere projection |
| `__miss__nn` | 空操作（结果已在 payload 中） |

声明：`extern "C" { __constant__ UnifiedParams params; }`（与 RT 模块共享变量名）

**NN Payload 布局**（p0–p8, 匹配管线 `numPayloadValues = 9`）：

| Slot | 内容 |
|---|---|
| p0 | `__float_as_uint(best_dist_sq)` — 当前最小面距离² |
| p1 | `prim_idx` — 最近三角形/球体索引（0xFFFFFFFF = 未找到） |
| p2 | `__float_as_uint(normal[0])` (E2) |
| p3 | `__float_as_uint(normal[1])` (E2) |
| p4 | `__float_as_uint(normal[2])` (E2) |
| p5 | `__float_as_uint(uv[0])` (E2) |
| p6 | `__float_as_uint(uv[1])` (E2) |
| p7 | `geom_id` (E2) |
| p8 | `inst_id` (E2) |

**Closest-Point-on-Triangle 算法**：

`__intersection__nn` 读取 `params.nn_vertices` 和 `params.nn_indices`，通过 `optixGetPrimitiveIndex()` 获取当前三角形索引，使用 Voronoi region 分类计算查询点到三角面的最近点（边/顶点/内部投影），返回重心坐标 (u,v) 和法线。取距离²与 payload p0 比较，更小则更新所有 payload slots。

**RTNN 核心技巧**：不调用 `optixReportIntersection()`，OptiX 继续遍历所有重叠 AABB → 穷举搜索。`optixTrace` 必须传 **9** 个 payload 参数（匹配统一管线配置）。

#### `kernels.cu` / `nn_kernels.cu`（编译为 CUDA object）

| 函数 | 说明 |
|---|---|
| `generateRandomRaysDevice(...)` | Marsaglia 球面均匀方向 + 球面外起点 |
| `countHitsDevice(...)` | 两级 atomicAdd 统计命中数 |
| `generateTriangleAABBsDevice(...)` | 每三角形 → 6 floats AABB (顶点 min/max ∓ radius 膨胀) |
| `generateSphereAABBsDevice(...)` | 每球体 → 6 floats AABB (中心 ± radius) (E4) |
| `generateRandomQueriesDevice(...)` | bbox 内均匀随机查询点 |
| `countNNHitsDevice(...)` | 统计 distance ≥ 0 的有效结果数 |

---

## 3. 数据流

```
                          ┌───────────────────────────────────────────────┐
                          │            UnifiedTracer                      │
                          │                                               │
  GeometryManager ──►     │  addGeometryMesh()  ──►  Per-Geometry GAS    │
  TriangleMesh            │  addGeometrySphere()──►  Sphere GAS          │
  SphereMesh              │  rebuildScene()     ──►  IAS + RT/MH SBTs   │
                          │  removeGeometry()   ──►  free GAS + rebuild  │
                          │  enableGeometry()   ──►  rebuild IAS/SBTs   │
                          │                                               │
  GeometryManager ──►     │  setQueryMesh()     ──►  AABB GAS (CP query) │
  TriangleMesh            │  rebuildQueryGAS()  ──►  rebuild AABB GAS    │
                          │                                               │
  DeviceManager ──►       │  init(ctx, rt_ptx, nn_ptx)                   │
  OptixDeviceContext      │    └► createModules (2)                       │
                          │    └► createProgramGroups (12)                │
                          │    └► createPipeline (1)                      │
                          │    └► createBaseSBTs (4)                      │
                          │                                               │
  kernels.cu              │  traceBatch(d_rays, d_hits, N)               │
  generateRandomRays ──►  │    └► optixLaunch(pipeline, sbt_rt, ias)     │
  Ray* (device)           │    └► HitResult* {t,bary,prim,geom,inst,n}   │
                          │                                               │
                          │  traceBatchMultiHit(d_rays, d_mh, N)  (E3)  │
                          │    └► optixLaunch(pipeline, sbt_mh, ias)     │
                          │    └► MultiHitResult* {count, hits[K]}       │
                          │                                               │
  nn_kernels.cu           │  queryBatch(d_queries, d_results, N)         │
  generateRandomQueries ► │    └► optixLaunch(pipeline, sbt_nn, aabb_gas)│
  float3* (device)        │    └► NNResult* {distance, prim_idx}         │
                          │                                               │
                          │  closestPointBatch(d_cpq, d_cpr, N)   (E2)  │
                          │    └► optixLaunch(pipeline, sbt_cp, aabb_gas)│
                          │    └► CPResult* {dist,normal,uv,ids}         │
                          └───────────────────────────────────────────────┘
```

**关键数据流向**：

1. `UnifiedTracer::init` → 创建 2 modules + **12 PGs** + 1 pipeline + **4 base SBTs**（一次性）
2. `addGeometryMesh` / `addGeometrySphere` → 构建 per-geometry GAS → `rebuildScene()` 重建 IAS + 动态 RT/MH SBT
3. `traceBatch` → 设 `params.handle = m_ias_handle` (IAS), 使用 `sbt_rt` → `optixLaunch`
4. `traceBatchMultiHit` → 设 `params.handle = m_ias_handle`, 使用 `sbt_mh` → `optixLaunch` (E3)
5. `queryBatch` → 设 `params.handle = aabb_gas`, `params.nn_vertices/nn_indices` = 设备指针, 使用 `sbt_nn` → `optixLaunch`
6. `closestPointBatch` → 设 `params.handle = aabb_gas`, `params.cp_queries/cp_results`, 使用 `sbt_cp` → `optixLaunch` (E2)
7. 场景修改 → `addGeometry*` / `removeGeometry` / `enableGeometry` → `rebuildScene()` → 下次 launch 使用新 IAS

---

## 4. 控制流 — main.cpp

```
main(argc, argv)
│
├── 1. 解析 CLI（--no-validate  --max-rays  --iters  --device  --csv）
│
├── 2. device.init(device_id)
│      └── printDeviceInfo()
│
├── 3. findAndLoadPtx() + findAndLoadNNPtx()
│      └── tracer.init(context, rt_ptx, nn_ptx)    ← 统一管线一次性初始化
│
├── 4. 构建初始场景
│      ├── tracer.setTriangleMesh(CornellBox)
│      └── tracer.setQueryMesh(100K tris, radius)
│
├── 5. [可选] 验证测试
│      ├── runRTValidation(tracer)
│      │   ├── traceSingle(hit/miss) → 验证单光线
│      │   └── traceBatch(100 rays) → 验证批量
│      ├── runWatertightTest(tracer)
│      │   ├── seam ray (double/float xform) → 验证精确缝隙
│      │   ├── edge-targeted rays (21) → 共享对角线
│      │   ├── oblique edge rays (40) → 斜角
│      │   └── grid seam batch (81) → 网格内部顶点
│      ├── runInstanceValidation(tracer)
│      │   ├── instance 0/1 hit/miss → 平移实例
│      │   ├── instance seam ray → 旋转实例缝隙
│      │   └── batch (100 instances) → 网格布局
│      └── runCPValidation(tracer, query_mesh)
│          ├── querySingle(above/on-surface/on-vertex/far/floor)
│          └── queryBatch(100 queries) → 验证批量
│
├── 6. 光追基准
│      ├── 6a. benchmarkRayCountSweep (1K → max)
│      ├── 6b. benchmarkSceneComplexity (1 → 1M 三角形)
│      │      (E6 multi-geometry API)
│      ├── addGeometryMesh(1000 tris) → rebuildScene → RT 验证 → PASS
│      ├── addGeometrySphere(spheres) → rebuildScene → RT 验证 → PASS
│      ├── removeGeometry(geom_id) → RT 验证 → PASS
│      ├── enableGeometry(geom_id, false) → RT 验证 → PASS
│      └── clearAllGeometry → setTriangleMesh(single) → RT
├── 7. CP (Closest-Point) 基准
│      ├── 7a. benchmarkCPQuerySweep (1K → max 查询)
│      └── 7b. benchmarkCPTriangleSweep (1 → 1M 三角形)
│              └── 循环: tracer.setQueryMesh(mesh, radius) → queryBatchTimed
│
├── 8. 场景修改演示addGeometryMesh` / `addGeometrySphere` / `removeGeometry` / `enableGeometry` / `rebuildScene` 管理多几何体
│      ├── addTriangles(1000) → rebuildTriangleGAS → RT 验证 → PASS
│      ├── addQueryTriangles(1000) → rebuildQueryGAS → CP 验证 → PASS
│      ├── clearTriangles → setTriangleMesh(single) → RT 验证 → PASS
│      └── clearQueryMesh → setQueryMesh(10K) → CP 验证 → PASS
│
├── 9. csv.close() + summary
│
└── 10. tracer.cleanup() → device.shutdown()
```

**关键区别（vs 旧架构）**：不再为每组测试创建独立 PipelineManager/AccelManager/RayTracer/NNQuery，而是复用同一个 `UnifiedTracer`，通过 `setTriangleMesh` / `setQueryMesh` / `setTriangleInstances` 切换场景。

---

## 5. 统一管线架构

### 5.1 管线拓扑

```
  programs.cu ──► programs.ptx    nn_programs.cu ──► nn_programs.ptx
       │  optixModuleCreate            │  optixModuleCreate
       ▼                               ▼
  m_rt_module                     m_nn_module
       │                               │
       │  optixProgramGroupCreate      │  optixProgramGroupCreate
       ├──► raygen_rt_pg               ├──► raygen_nn_pg
       ├──► raygen_mh_pg  (E3)        ├──► raygen_cp_pg  (E2)
       ├──► miss_rt_pg                 ├──► miss_nn_pg
       ├──► hitgroup_tri_pg            ├──► hitgroup_aabb_pg
       ├──► hitgroup_tri_mh_pg  (E3)  └──► hitgroup_aabb_sphere_pg (E4)
       ├──► hitgroup_sphere_pg  (E4)
       └──► hitgroup_sphere_mh_pg (E3+E4)
                   │                               │
                   └────────────┬──────────────────┘
                                │
                   optixPipelineCreate (link all 12 PGs)
                                │
                                ▼
                        OptixPipeline
                                │
                   optixUtilComputeStackSizes
                   optixPipelineSetStackSize
                                ▼
                       Ready to launch
```

### 5.2 管线配置

| 选项 | 值 | 说明 |
|---|---|---|
| `usesPrimitiveTypeFlags` | `TRIANGLE \| CUSTOM` | 同时支持三角形和 AABB |
| `traversableGraphFlags` | `ALLOW_ANY` | 支持裸 GAS 和 IAS 两种模式 |
| `numPayloadValues` | **9** | max(RT 9, NN 9) — 扩展后 9 slots (E1: geom_id, inst_id, normal) |
| `numAttributeValues` | 2 | built-in 三角形重心坐标 |
| `maxTraceDepth` | 2 | 支持 IAS → GAS 两级遍历 |
| `pipelineLaunchParamsVariableName` | `"params"` | 两个模块共享 `__constant__ UnifiedParams params` |

### 5.3 四套 SBT 架构

每次 `optixLaunch` 使用不同的 SBT，选择对应的 raygen / miss / hitgroup 程序：

#### SBT_RT（单命中光线追踪）

| Section | Record | Program Group | 程序 |
|---|---|---|---|
| Raygen | `SbtRecord<EmptyData>` | raygen_rt_pg | `__raygen__rg` |
| Miss | `SbtRecord<EmptyData>` | miss_rt_pg | `__miss__ms` |
| HitGroup[N] | `SbtRecord<HitGroupData>` | hitgroup_tri_pg / hitgroup_sphere_pg | `__closesthit__ch` (+ `__intersection__sphere`) |

> N = 已注册几何体数量，每个 GAS 一条 hitgroup record，携带 `geom_id` / `geom_type` / `vertices` / `indices` / `sphere_centers` / `sphere_radii`

#### SBT_MH（Multi-Hit, E3）

| Section | Record | Program Group | 程序 |
|---|---|---|---|
| Raygen | `SbtRecord<EmptyData>` | raygen_mh_pg | `__raygen__mh` |
| Miss | `SbtRecord<EmptyData>` | miss_rt_pg | `__miss__ms` |
| HitGroup[N] | `SbtRecord<HitGroupData>` | hitgroup_tri_mh_pg / hitgroup_sphere_mh_pg | `__closesthit__ch` + `__anyhit__mh` (+ `__intersection__sphere`) |

#### SBT_NN（简单最近面查询）

| Section | Record | Program Group | 程序 |
|---|---|---|---|
| Raygen | `SbtRecord<EmptyData>` | raygen_nn_pg | `__raygen__nn` |
| Miss | `SbtRecord<EmptyData>` | miss_nn_pg | `__miss__nn` |
| HitGroup | `SbtRecord<EmptyData>` | hitgroup_aabb_pg | `__intersection__nn` |

#### SBT_CP（增强 Closest-Point, E2）

| Section | Record | Program Group | 程序 |
|---|---|---|---|
| Raygen | `SbtRecord<EmptyData>` | raygen_cp_pg | `__raygen__cp` |
| Miss | — | (共享 miss_nn_pg) | `__miss__nn` |
| HitGroup | — | (共享 hitgroup_aabb_pg) | `__intersection__nn` |

> CP SBT 的 miss 和 hitgroup 共享 NN SBT 的程序组

### 5.4 加速结构

| 结构 | 类型 | 持久设备数据 | 说明 |
|---|---|---|---|
| Per-Geometry GAS (三角形) | `OPTIX_BUILD_INPUT_TYPE_TRIANGLES` | `d_vertices` + `d_indices`（closesthit/normal 计算） | `addGeometryMesh` 构建 |
| Per-Geometry GAS (球体) | `OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES` | `d_centers` + `d_radii` | `addGeometrySphere` 构建 |
| IAS | `OPTIX_BUILD_INPUT_TYPE_INSTANCES` | instance buffer + 所有 child GAS | `rebuildScene` 从所有 enabled 几何体重建 |
| AABB GAS (CP) | `OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES` | `m_d_nn_vertices` + `m_d_nn_indices`（intersection 程序读取） | `setQueryMesh` / `rebuildQueryGAS` 替换 |

**IAS 架构 (E6)**：每个几何体的 GAS 作为 IAS 的 child，每个 `OptixInstance` 携带 3×4 行主序仿射变换矩阵，`sbtOffset` 按几何体顺序递增（每个 GAS 一条 hitgroup record），`instanceId = geom_id`。`rebuildScene()` 同时重建 IAS 和 RT/MH SBT hitgroup records。

GAS 构建使用 `OPTIX_GEOMETRY_FLAG_NONE`（不设置 `DISABLE_ANYHIT`），兼容单命中（通过 ray flag `DISABLE_ANYHIT`）和 Multi-Hit（通过 ray flag `NONE`）两种模式。

### 5.5 Launch 分发

```cpp
// RT dispatch (single-hit)
UnifiedParams lp = {};
lp.handle = m_ias_handle;      // IAS (multi-geometry)
lp.count  = num_rays;
lp.rays   = d_rays;
lp.hits   = d_hits;
optixLaunch(m_pipeline, stream, &lp, sizeof(lp), &m_sbt_rt, w, h, 1);

// Multi-Hit dispatch (E3)
UnifiedParams lp = {};
lp.handle     = m_ias_handle;
lp.count      = num_rays;
lp.rays       = d_rays;
lp.multi_hits = d_multi_hits;  // global memory for Top-K
optixLaunch(m_pipeline, stream, &lp, sizeof(lp), &m_sbt_mh, w, h, 1);

// CP (Closest-Point) dispatch (simple NNResult)
UnifiedParams lp = {};
lp.handle      = m_aabb_gas_handle;        // AABB GAS (tri bboxes)
lp.count       = num_queries;
lp.queries     = d_queries;
lp.nn_vertices = m_d_nn_vertices.get();    // triangle vertices
lp.nn_indices  = m_d_nn_indices.get();     // triangle indices
lp.results     = d_results;
optixLaunch(m_pipeline, stream, &lp, sizeof(lp), &m_sbt_nn, w, h, 1);

// Enhanced CP dispatch (E2: CPQuery → CPResult)
UnifiedParams lp = {};
lp.handle      = m_aabb_gas_handle;
lpMH Multi-Hit | `params.multi_hits`（设备指针，global memory） | D → D | any-hit 写入 → raygen 读取 (E3) |
| CP 查询/结果 | `params.queries` / `params.results`（设备指针） | D → D | kernel 生成 → raygen 读写 |
| Enhanced CP | `params.cp_queries` / `params.cp_results`（设备指针） | D → D | raygen_cp 读写 (E2) |
| CP 三角形数据 | `params.nn_vertices` + `params.nn_indices`（设备指针，持久） | D → D | intersection 读取 |
| SBT Records | `cudaMemcpy` 上传（4 套 SBT） | H → D | init + rebuildScene |
| RT/MH HitGroup SBT | 动态重建（per-geometry `HitGroupData`） | H → D | 每次 `rebuildScene()` |
| RT Payload | **9** × uint32 寄存器 (p0–p8) | 设备内部 | CH/AH/IS/Miss → Raygen |
| NN/CP Payload | **9** × uint32 寄存器 (p0–p8) | 设备内部 | IS → Raygen |
| GAS 构建输入 | `cudaMemcpy` (vertices, indices, aabbs, spherem_sbt_cp, w, h, 1);
```

同一管线、同一 `params` 变量名，通过传递不同 SBT 和 traversable handle 切换模态。RT 和 MH 使用 IAS handle；NN 和 CP 使用 AABB GAS handle。

---

## 6. Host ↔ Device 数据通道

| 通道 | 机制 | 方向 | 频率 |
|---|---|---|---|
| Launch Params | `optixLaunch(d_params, sizeof)` → `__constant__ params` | H → D | 每次 launch |
| RT 光线/命中 | `params.rays` / `params.hits`（设备指针） | D → D | kernel 生成 → raygen 读写 |
| CP 查询/结果 | `params.queries` / `params.results`（设备指针） | D → D | kernel 生成 → raygen 读写 |
| CP 三角形数据 | `params.nn_vertices` + `params.nn_indices`（设备指针，持久） | D → D | intersection 读取 |
| SBT Records | `cudaMemcpy` 上传（2 套 SBT） | H → D | 一次性 |
| RT Payload | 4 × uint32 寄存器 (p0–p3) | 设备内部 | CH/Miss → Raygen |
| CP Payload | 4 × uint32 寄存器 (p0–p1 有效, p2–p3 占位) | 设备内部 | IS → Raygen |
| GAS 构建输入 | `cudaMemcpy` (vertices, indices, aabbs) | H → D / D → D | 构建时 |

---

## 7. 内存管理

### 7.1 分配 / 释放责任

| 资源 | 分配者 | 释**12** PGs / Pipeline | `UnifiedTracer::init` | `UnifiedTracer::cleanup` | `optix*Destroy` |
| **4** 套 SBT 基础缓冲区 | `UnifiedTracer::createBaseSBTs` | `UnifiedTracer::cleanup` | `cudaFree` |
| RT/MH SBT hitgroup records | `rebuildRTSBT` / `rebuildMHSBT` | `freeIAS` / `rebuildScene` | `cudaFree` (动态大小) |
| Per-Geometry GAS 缓冲区 | `buildMeshGAS` / `buildSphereGAS` | `freeGeometryGAS` | `cudaFree` |
| Per-Geometry 设备数据 | `buildMeshGAS` / `buildSphereGAS` | `freeGeometryGAS` | `cudaFree` (vertices/indices/centers/radii) |
| Triangle 构建临时缓冲区 | `buildMeshGAS` | 同函数内 | `cudaFree` 即时释放 |
| IAS 缓冲区 | `buildIASBTs` | `UnifiedTracer::cleanup` | `cudaFree` |
| Triangle GAS 缓冲区 | `buildTriangleGASInternal` | `freeTriangleGAS` | `cudaFree` |
| Triangle 构建临时缓冲区 | `buildTriangleGASInternal` | 同函数内 | `cudaFree` 即时释放 |
| IAS 缓冲区 + child GAS | `buildIASInternal` | `freeIAS` | `cudaFree` |
| AABB GAS 缓冲区 (CP) | `buildQueryGASInternal` | `freeQueryGAS` | `cudaFree` |
| `m_d_nn_vertices` + `m_d_nn_indices`（持久） | `setQueryMesh` / `rebuildQueryGAS` | `clearQueryMesh` / `cleanup` | `CudaBuffer` RAII |
| Launch params | `traceBatch` / `queryBatch` | 同函数内 | 每次调用 malloc/free |
| 单查询缓冲区 | `traceSingle` / `querySingle` | `cleanup` | `CudaBuffer` 懒分配 |
| `CudaBuffer<T>` 临时缓冲区 | 各调用者 | 析构函数 | RAII |

### 7.2 模式总结

1. **RAII (`CudaBuffer<T>`)**：临时 GPU 缓冲区走析构释放
2. **即分即释 (`traceBatch`)**：每次 launch 分配释放 `UnifiedParams`
3. **懒分配 (`traceSingle`)**：单查询缓冲区首次调用分配，后续复用
4. **构建临时资源**：GAS 构建中间缓冲区（temp, vertices, indices, aabbs）函数内即释
5. **持久设备数据 (`m_d_nn_vertices`, `m_d_nn_indices`)**：intersection 程序持续读取，随场景生命周期

---

## 8. CUDA Stream 使用

### 8.1 默认流 (stream 0)

- GAS 构建：`optixAccelBuild(ctx, 0, ...)`
- `traceSingle` / `querySingle`：`optixLaunch(pipeline, 0, ...)`

### 8.2 Multi-Stream 并发

`traceBatch` / `queryBatch` 接受可选 `CUstream` 参数，支持多流并发 launch：

```
for s in 0..N-1:
    tracer.traceBatch(ray_bufs[s], hit_bufs[s], count, streams[s])
// 不同流的 launch 可并发执行
```

---

## 9. 构建系统

### 双编译模式

| 目标 | 源文件 | 编译方式 | 产物 |
|---|---|---|---|
| RT PTX | `device/programs.cu` | `nvcc --ptx -arch=compute_70` | `programs.ptx` |
| NN PTX | `device/nn_programs.cu` | `nvcc --ptx -arch=compute_70` | `nn_programs.ptx` |
| 主程序 | `src/main.cpp`, `src/device_manager.cpp`, `src/geometry_manager.cpp`, `src/unified_tracer.cpp` + `device/kernels.cu`, `device/nn_kernels.cu` | MSVC + nvcc (`sm_86`) | `optix_throughput.exe` |

### 关键配置

- **OptiX**: Header-only（`optix_stubs.h` 动态加载函数表），不链接 OptiX 库
- **PTX 路径**: 编译定义 `OPTIX_PTX_FILE_PATH` + `OPTIX_NN_PTX_FILE_PATH`，运行时多路径搜索
- **Post-build**: 两个 PTX 文件复制到可执行文件目录
- **NOMINMAX**: `unified_tracer.h` 中定义，避免 Windows `max` 宏与 `optix_stack_size.h` 冲突
- **PTX 依赖**: 两个 PTX target 均依赖 `unified_params.h`, `ray_types.h`, `nn_types.h`

### 构建命令

```bash
cd optix-throughput-validation
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

---

## 10. 基准测试结果

### 计时策略

- **CUDA Events** 精确 GPU 计时
- **Warm-up**: 3 次预热
- **统计**: 多次迭代取**中位数**
- **命中统计**: GPU 端 atomicAdd kernel

### RTX 3070 Laptop 基准结果 (2026-02-21)

#### 光追 — Scene Complexity Sweep (1M rays)

| 三角形数 | 构建 (ms) | 追踪 (ms) | MRays/s | GRays/s |
|---|---|---|---|---|
| 1 | 0.36 | 0.40 | 2,619 | 2.62 |
| 30 | 0.47 | 0.45 | 2,333 | 2.33 |
| 1K | 0.72 | 0.48 | 2,183 | 2.18 |
| 10K | 1.04 | 1.22 | 858 | 0.86 |
| 100K | 5.48 | 3.74 | 281 | 0.28 |
| 500K | 17.5 | 4.79 | 219 | 0.22 |
| 1M | 33.4 | 5.46 | **192** | 0.19 |

#### 光追 — Multi-Stream (4M rays total)

| 流数 | 总时间 (ms) | GRays/s |
|---|---|---|
| 1 | 1.38 | **3.03** |
| 2 | 1.30 | 3.24 |
| 4 | 1.52 | 2.76 |
| 8 | 2.05 | 2.05 |

#### 光追 — Instance Count Sweep (1M rays, base=30 tris)

| 实例数 | 总三角形数 | 构建 (ms) | 追踪 (ms) | GRays/s | 命中率 |
|---|---|---|---|---|---|
| 1 | 30 | 0.60 | 0.37 | 2.88 | 100% |
| 10 | 300 | 0.60 | 0.62 | 1.69 | 87% |
| 100 | 3K | 0.64 | 0.39 | 2.67 | 93% |
| 1K | 30K | 0.76 | 0.42 | 2.50 | 91% |
| 10K | 300K | 1.91 | 0.44 | **2.38** | 97% |

> **关键发现**: IAS 10K instances (300K tris) 追踪 2.38 GRays/s，而单 GAS 300K tris 仅 0.28 GRays/s — **IAS 快约 8.5×**，因硬件利用两级层次结构更高效。

#### CP — Query Count Sweep (100K 三角形)

| 查询数 | 时间 (ms) | MQuery/s |
|---|---|---|
| 1K | 1.33 | 0.77 |

#### CP — Triangle Count Sweep (1M queries)

| 三角形数 | GAS 构建 (ms) | 查询 (ms) | MQuery/s | GQuery/s |
|---|---|---|---|---|
| 1 | 0.55 | 0.47 | 2,212 | 2.21 |
| 30 | 0.43 | 2.24 | 468 | 0.47 |
| 1K | 0.56 | 26.3 | 39.9 | 0.04 |
| 10K | 0.86 | 48.0 | 21.9 | 0.02 |
| 100K | 5.62 | 83.2 | **12.6** | 0.01 |
| 500K | 23.7 | 154.7 | 6.78 | 0.007 |
| 1M | 45.1 | 186.2 | **5.63** | 0.006 |

#### 场景修改演示

| 操作 | 结果 |
|---|---|
| addTriangles(1000) → rebuildTriangleGAS → RT | **PASS** |
| addQueryTriangles(1000) → rebuildQueryGAS → CP | **PASS** |
| clearTriangles → setTriangleMesh(1 tri) → RT | **PASS** |
| clearQueryMesh → setQueryMesh(10K) → CP | **PASS** |

---

## 11. 已知的优化空间

1. **`traceBatch` / `que扩展后包含 multi_hits / cp_queries / cp_results / sphere / enclosure 字段，各 launch 模式仅使用子集
7. **SBT 动态重建开销**：每次 `rebuildScene` 重新分配/上传 RT + MH hitgroup records；高频场景修改可考虑预分配最大容量
8. **IAS 变换精度**：当前 `OptixInstance` 使用 float 3×4 矩阵，双精度场景需要外部坐标变换补偿
9. **Multi-Hit global memory**：`params.multi_hits` 使用全局内存（非 payload 压缩），高光线数时显存占用较大
10. **9-slot payload 寄存器压力**：payload 未压缩（normal 占 3 slot），可考虑法线压缩到 2 slot 节省寄存器

---

## 12. 扩展实施状态

| 扩展 | 描述 | 状态 | 涉及文件 |
|---|---|---|---|
| **E1** | 扩展 HitResult (geom_id, inst_id, normal) | ✅ 已完成 | ray_types.h, unified_params.h, programs.cu |
| **E2** | 增强 Closest-Point (CPQuery → CPResult) | ✅ 已完成 | nn_types.h, unified_params.h, nn_programs.cu, unified_tracer.h/cpp |
| **E3** | Multi-Hit Top-K | ✅ 已完成 | ray_types.h, unified_params.h, programs.cu, unified_tracer.h/cpp |
| **E4** | 球体几何 | ✅ 已完成 | ray_types.h, unified_params.h, programs.cu, nn_programs.cu, nn_kernels.h/cu, geometry_manager.h/cpp |
| **E5** | 包壳定位 (inside/outside) | 🔶 数据结构 | nn_types.h, unified_params.h — dispatch 函数待实现 |
| **E6** | 多 GAS + IAS 场景管理 | ✅ 已完成 | unified_tracer.h/cpp |
4. **CP 搜索半径自适应**：当前使用固定半径膨胀 AABB，可根据三角形大小局部调整
5. **GAS 增量更新**：当前每次重建完整 GAS，可考虑 `ALLOW_UPDATE` + `optixAccelBuild(UPDATE)` 用于少量修改
6. **UnifiedParams 大小**：64 bytes (含 `nn_vertices` + `nn_indices`)，CP 字段在 RT launch 时未使用，规模不构成问题
7. **SBT HitGroupData 未使用**：`vertices` / `indices` 为 NULL，需要逐图元着色时应填充
8. **IAS 变换精度**：当前 `OptixInstance` 使用 float 3×4 矩阵，双精度场景需要外部坐标变换补偿
