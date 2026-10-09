# UnifiedTracer 扩展需求文档

**基线**: `optix-throughput-validation/src/unified_tracer.h` 当前 API  
**目标**: 将 UnifiedTracer 从基准测试工具升级为产品级几何查询引擎  
**日期**: 2026-02-21  
**最后更新**: 2026-02-26

---

## 实施状态总览

| 扩展 | 状态 | 说明 |
|------|------|------|
| **E1** 扩展命中结果 | ✅ 已完成 | 9 payload slots (spec 建议 7-9), normal 使用 3 独立 float slot |
| **E2** 三角面最近点查询 | ✅ 已完成 | 复用 nn_programs.cu + 第 4 套 SBT (CP SBT), 非第 3 PTX 模块 |
| **E3** Multi-Hit | ✅ 已完成 | any-hit + global memory, MAX_MULTI_HITS=2 |
| **E4** 球体几何 | ✅ 已完成 | API 使用 `SphereMesh` 结构而非 `vector<SphereData>` |
| **E5** 包壳定位查询 | 🔶 数据结构已定义 | `EnclosureQuery`/`EnclosureResult` 定义完成, `findEnclosureBatch` 调度函数未实现 |
| **E6** 多 GAS + IAS 场景管理 | ✅ 已完成 | `rebuildScene()` 取代 `rebuildIAS()`, `addGeometrySphere` 接受 `SphereMesh` |

---

## E1. 扩展命中结果（必须）  ✅ 已完成

当前 `HitResult` 仅返回 `{t, bary_u, bary_v, prim_idx}`，缺少几何体和实例标识。

### 要求

- `HitResult` 新增字段：`geom_id`（uint）、`inst_id`（uint，无实例时 `0xFFFFFFFF`）、`normal[3]`（float，未归一化几何法线）
- closest-hit 程序通过 `optixGetInstanceId()` 获取 `inst_id`
- `geom_id` 通过 SBT hitgroup record 携带的 per-geometry 元数据获取
- `prim_idx` 语义不变：GAS 内局部图元索引
- 多 GAS/IAS 模式（见 E6）下，每个 GAS 的 hitgroup record 包含该几何体的 `geom_id` 和 `prim_offset`
- Payload 扩展为 7 slots：t, bary_u, bary_v, prim_idx, geom_id, inst_id, normal_packed（法线可用 `__float_as_uint` 编码为 2 个 slot，或扩展到 9 slots）

### 验证

- 单 GAS 场景：`geom_id` 固定，`inst_id = 0xFFFFFFFF`，`prim_idx` 与当前一致
- IAS 场景：N 实例各有正确 `inst_id`，`geom_id` 正确映射
- 法线方向与三角形顶点顺序一致

### 实施偏差

- **Payload 9 slots**（非 spec 建议的 7）: t, bary_u, bary_v, prim_idx, geom_id, inst_id, normal[0], normal[1], normal[2] — 法线未压缩，使用 3 个独立 float slot，代码清晰
- `geom_id` 从 SBT `HitGroupData.geom_id` 读取（spec 一致）
- `inst_id` 通过 `optixGetInstanceId()` 获取（spec 一致）
- 三角形法线通过 `HitGroupData.vertices/indices` 在 closest-hit 程序中计算; 球体法线通过 `hit_point - center` 计算
- 管线配置 `numPayloadValues = 9`，所有 SBT/raygen 程序均传 9 slot

### 涉及文件

- `include/ray_types.h` — `HitResult` 新增 `geom_id`, `inst_id`, `normal[3]`
- `include/unified_params.h` — `HitGroupData` 新增 `geom_id`, `geom_type`, `vertices`, `indices`, `sphere_centers`, `sphere_radii`
- `device/programs.cu` — `__closesthit__ch` 使用 9 payload slots

---

## E2. 三角面最近点查询（必须）  ✅ 已完成

当前 `queryBatch` 实现的是点云最近邻（RTNN），与"三角形表面上最近点投影"语义不同。

### 要求

新增一组 closest-point-on-surface 查询接口（与现有 RTNN 接口共存）：

```
struct CPResult {
    float    distance;      // 到最近表面的欧氏距离；< 0 = miss
    float    normal[3];     // 最近面的未归一化几何法线
    float    uv[2];         // 最近点在三角形上的重心坐标
    unsigned prim_idx;      // GAS 内局部图元索引
    unsigned geom_id;       // 几何体 ID
    unsigned inst_id;       // 实例 ID（无实例时 0xFFFFFFFF）
};

struct CPQuery {
    float3 position;        // 查询点
    float  radius;          // 最大搜索半径
};
```

- `closestPointSingle(CPQuery) → CPResult`
- `closestPointBatch(CPQuery* d_queries, CPResult* d_results, uint count, CUstream)`
- `closestPointBatch(vector<CPQuery>) → vector<CPResult>`

### 实现约束

- 使用自定义 AABB primitive：每个三角形生成一个 AABB，作为 custom primitive 注册
- intersection program 中执行双精度三角面投影（Voronoi region 分类算法），通过 shrinking `tmax` 实现搜索半径缩减
- 需要第 3 个 PTX 模块 + 第 3 套 SBT + 独立的 AABB GAS（基于三角形几何，非点云）
- intersection program 不调用 `optixReportIntersection()`（穷举遍历所有重叠 AABB，取最近）— 与现有 RTNN 模式一致

### 验证

- 查询点为三角形顶点 → distance ≈ 0，UV 对应该顶点
- 查询点在三角形正上方 → distance = 垂直距离，UV 正确
- 查询点在边投影区域 → 返回边上最近点
- 与 CPU 参考实现逐结果对比，容差 1e-5

### 实施偏差

- **不使用第 3 个 PTX 模块**：复用 `nn_programs.cu`，新增 `__raygen__cp` 入口点
- **4 套 SBT** 而非 3 套：SBT_RT, SBT_MH, SBT_NN, SBT_CP — CP SBT 的 raygen 指向 `__raygen__cp`，miss/hitgroup 共享 NN SBT 的程序组
- `closestPointSingle()` 未实现（仅批量接口）
- intersection 程序使用 float（非 double）Voronoi region 分类，返回 `u,v` 重心坐标
- 支持球体 CP：`__intersection__nn_sphere` 计算点到球面最近点

### 涉及文件

- `include/nn_types.h` — `CPQuery`, `CPResult` 定义
- `include/unified_params.h` — `cp_queries`, `cp_results` 字段
- `device/nn_programs.cu` — `__raygen__cp`, `closestPointOnTriangle()` (Voronoi), `__intersection__nn_sphere`
- `src/unified_tracer.h/cpp` — `closestPointBatch()` 接口, SBT_CP 创建

---

## E3. Multi-Hit 支持（必须）  ✅ 已完成

当前 `traceBatch` 仅返回最近一次命中（closest-hit）。

### 要求

新增 Top-K 批量追踪接口：

```
struct MultiHitResult {
    unsigned count;                   // 实际命中数 [0, K]
    HitResult hits[MAX_MULTI_HITS];   // 按距离升序排列
};
```

- `traceBatchMultiHit(Ray* d_rays, MultiHitResult* d_results, uint count, CUstream)`
- `MAX_MULTI_HITS` 编译期常量，初始值 = 2
- any-hit program 记录候选命中、按距离排序，仅当候选列表满且新命中更远时 ignore
- 现有 `traceBatch`（单命中）接口保持不变

### 验证

- 射线穿透两个不重叠三角形 → `count = 2`，`hits[0].t < hits[1].t`
- 射线仅命中一个面 → `count = 1`
- 射线 miss → `count = 0`

### 实施偏差

- 实现与 spec 完全一致
- any-hit 使用 global memory (`params.multi_hits`) 存储候选列表（非 payload 压缩）
- `__raygen__mh` 使用 `OPTIX_RAY_FLAG_NONE`（允许 any-hit 执行），`__raygen__rg` 使用 `OPTIX_RAY_FLAG_DISABLE_ANYHIT`
- GAS 构建使用 `OPTIX_GEOMETRY_FLAG_NONE`（非 `DISABLE_ANYHIT`），兼容两种模式
- SBT_MH 独立于 SBT_RT，hitgroup 使用 `m_hitgroup_tri_mh_pg` / `m_hitgroup_sphere_mh_pg`（含 any-hit 程序）

### 涉及文件

- `include/ray_types.h` — `MultiHitResult`, `MAX_MULTI_HITS`
- `include/unified_params.h` — `multi_hits` 指针
- `device/programs.cu` — `__raygen__mh`, `__anyhit__mh`
- `src/unified_tracer.h/cpp` — `traceBatchMultiHit()`, SBT_MH 管理

---

## E4. 球体几何支持（必须）  ✅ 已完成

当前仅支持三角形。

### 要求

- 新增球体几何管理接口：

```
struct SphereData {
    float3 center;
    float  radius;
};

void addSpheres(const std::vector<SphereData>& spheres);
void clearSpheres();
void rebuildSphereGAS(bool compact = true);
```

- 球体作为 custom AABB primitive 实现，intersection program 执行射线-球体解析求交
- 球体 GAS 与三角形 GAS 通过 IAS 组合（见 E6），共享同一管线
- 球体命中返回完整 `HitResult`（含 `geom_id`、`inst_id`、法线）
- closest-point-on-surface（E2）同样支持球体：投影到球面，返回法线 = 球心→投影点方向

### 验证

- 射线与球体相交 → 命中距离 = 解析解
- 法线 = 归一化(hit_point - center)
- closest-point 到球心 → distance = radius

### 实施偏差

- **API 使用 `SphereMesh` 结构**（`geometry_manager.h` 中定义）而非 `vector<SphereData>`：
  ```cpp
  struct SphereMesh {
      std::vector<float3> centers;
      std::vector<float>  radii;
  };
  ```
- 不提供独立的 `addSpheres` / `clearSpheres` / `rebuildSphereGAS` — 球体通过 E6 多几何体系统管理：
  ```cpp
  unsigned int addGeometrySphere(const SphereMesh& spheres, const float* transform_3x4 = nullptr);
  ```
- 球体使用 custom AABB primitive + `__intersection__sphere` 解析求交
- 球体法线 = `normalize(hit_point - center)`，在 `__closesthit__ch` 中通过 `optixIsTriangleHit()` 分支处理
- CP 球体查询通过 `__intersection__nn_sphere` 实现（点到球面投影）

### 涉及文件

- `include/ray_types.h` — `SphereData` 定义
- `include/unified_params.h` — `HitGroupData` sphere 字段
- `device/programs.cu` — `__intersection__sphere`
- `device/nn_programs.cu` — `__intersection__nn_sphere`
- `device/nn_kernels.h/cu` — `generateSphereAABBsDevice`
- `src/geometry_manager.h/cpp` — `SphereMesh`, `createSingleSphere`, `createRandomSpheres`

---

## E5. 包壳定位查询（必须）  🔶 部分完成

给定查询点，找到最近的表面图元，判断点在表面的哪一侧（front/back）。

### 要求

```
struct EnclosureResult {
    int      prim_idx;     // 最近图元索引；-1 = miss
    float    distance;     // 到最近表面的距离
    int      side;         // 0=front, 1=back, -1=degenerate
    unsigned geom_id;      // 几何体 ID
    unsigned inst_id;      // 实例 ID
};

struct EnclosureQuery {
    float3 position;       // 查询点（世界空间）
};
```

- `findEnclosureBatch(EnclosureQuery* d_queries, EnclosureResult* d_results, uint count, CUstream)`
- `findEnclosureBatch(vector<EnclosureQuery>) → vector<EnclosureResult>`

### 实现约束

- 建立在 E2（closest-point-on-surface）之上
- 找到最近表面后，计算法线与 (查询点 - 最近点) 的点积判断 front/back
- dot > 0 → front (side=0)；dot < 0 → back (side=1)；|dot| < ε → degenerate (side=-1)
- 支持三角形和球体（E4）

### 验证

- 封闭立方体内部点 → side=1 (back)
- 封闭立方体外部点 → side=0 (front)
- 点在面上 → side=-1 (degenerate)

### 实施状态

- ✅ 数据结构已定义：`EnclosureQuery`, `EnclosureResult`（`include/nn_types.h`）
- ✅ `enclosure_results` 指针已加入 `UnifiedParams`（`include/unified_params.h`）
- ❌ `findEnclosureBatch` 调度函数**未实现** — 需在 `unified_tracer.h/cpp` 中添加
- ❌ `__raygen__enclosure` 设备程序**未实现** — 需在 `device/nn_programs.cu` 中添加
- ❌ 验证测试**未编写**

### 依赖

- 建立在 E2 (`closestPointBatch`) 之上 — E2 已完成，E5 的 dispatch 可调用 CP 路径后在 host 端计算 dot product 判断 side

---

## E6. 多 GAS + IAS 场景管理（必须）  ✅ 已完成

当前 `setTriangleMesh` 仅支持单个 mesh；`setTriangleInstances` 仅支持单 base mesh 的 N 实例。

### 要求

- 支持多个独立几何体（mesh + sphere），每个构建为独立 GAS
- 通过 IAS 组合所有 GAS 为统一遍历结构
- 每个几何体有独立的 `geom_id`，通过 SBT hitgroup record 传递
- 每个几何体支持独立的 3x4 仿射变换
- 支持 per-geometry enable/disable（通过 IAS instance mask 或重建时排除）

```
// 返回分配的 geom_id
unsigned addGeometryMesh(const TriangleMesh& mesh,
                         const float* transform_3x4 = nullptr);

unsigned addGeometrySphere(const SphereData& sphere,
                           const float* transform_3x4 = nullptr);

void removeGeometry(unsigned geom_id);
void enableGeometry(unsigned geom_id, bool enable);

// 重建 IAS（添加/删除/enable变更后调用）
void rebuildIAS(bool compact = true);

// 清空所有几何体
void clearAllGeometry();
```

### 内部结构

- 维护 `geom_id → {GAS handle, SBT offset, transform, enabled, prim_offset, prim_count}` 映射表
- `rebuildIAS` 从所有 enabled 的 GAS 构建 IAS
- E1/E2/E3/E4/E5 的所有查询接口使用 IAS 作为 traversable handle
- `prim_offset` / `prim_count` 由场景构建时确定，用于 `prim_idx` ↔ 全局图元索引的转换

### 验证

- 添加 3 个独立 mesh → 各自返回不同 `geom_id`
- 射线命中不同几何体 → `HitResult.geom_id` 正确区分
- disable 某几何体后 → 射线不再命中该几何体
- `removeGeometry` 后 → `geom_id` 可回收复用

---

## 实施顺序

```
E6 (多 GAS/IAS)  ──►  E1 (扩展 HitResult)  ──►  E3 (Multi-Hit)
                                                      │
E4 (球体)  ─────────────────────────────────────►  E2 (三角面最近点)  ──►  E5 (包壳定位)
```

E6 和 E1 是基础，所有后续功能依赖。E4 可与 E1/E3 并行开发。E5 依赖 E2。

**实际实施顺序（2026-02-25 完成）**: E1 → E2 → E3 → E4 → E5(数据结构) → E6

### E6 实施偏差

- **API 命名**: `rebuildScene()` 取代 `rebuildIAS()` — 语义更明确（内部调用 `buildIAS()`）
- **`addGeometrySphere`** 接受 `SphereMesh` 而非 `SphereData`：
  ```cpp
  unsigned int addGeometrySphere(const SphereMesh& spheres, const float* transform_3x4 = nullptr);
  ```
- **`setTriangleMesh`** 和 **`setTriangleInstances`** 保留为 legacy convenience wrappers（内部调用 E6 API）
- 内部使用 `std::map<unsigned int, GeometryDesc>` 管理几何体，`GeometryDesc` 包含 GAS handle, 设备数据指针, host 数据副本, transform, enabled 标志
- 每次 `rebuildScene()` 重建 IAS + RT SBT + MH SBT（动态 hitgroup record 数量）
- `removeGeometry()` 释放 GAS + 设备数据，然后调用 `rebuildScene()`
- `enableGeometry()` 修改 `enabled` 标志后调用 `rebuildScene()`

### 涉及文件

- `src/unified_tracer.h` — `GeometryDesc`, multi-geometry API
- `src/unified_tracer.cpp` — `buildMeshGAS`, `buildSphereGAS`, `buildIAS`, `rebuildRTSBT`, `rebuildMHSBT`, `rebuildScene`

---

## 不在本文档范围内

- Hit filter 回调逻辑（封装层实现，tracer 仅提供 multi-hit 原始结果）
- CPU 端数据重排（`s3d_ray_request` ↔ `Ray` 等）
- 引用计数 / 生命周期管理
- 表面均匀采样（CPU 端 CDF，不走 GPU）
- 顶点属性插值（CPU 端通过 `prim_idx` + 顶点数据实现）
