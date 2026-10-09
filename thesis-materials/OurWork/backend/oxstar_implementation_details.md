# OxStar-3D OptiX 后端实现细节
> 来源: `D:\Stardis-GPU\stardis-oxs3d-merge-phase\oxstar-3d\0.10\`  
> 用于论文第四章撰写  
> 生成日期: 2026-03-20

---

## 1. 总体架构

### 架构概述
- **单统一 Pipeline**：将光追（RT）与最近点查询（CP/NN）合并到同一 OptiX Pipeline，避免重复构建
- **两个 PTX 模块**：`programs.cu`（RT 模块）和 `nn_programs.cu`（NN 模块）
- **六张 SBT**：分别对应不同查询类型（见下文）
- **双 GAS 架构**：
  - **场景 GAS/IAS**：每个几何体独立 Triangle GAS，所有 GAS 汇入单层 IAS，用于光追（RT/MH/MHF）
  - **AABB GAS**：为每个三角形生成膨胀 AABB 的自定义 primitive GAS（`m_aabb_gas_handle`），用于 CP/NN 查询
- **traversal 深度**：IAS → GAS，`maxTraversalDepth = 2`

```
                  optixLaunch
                      |
               UnifiedParams (lp)
                      |
          ┌───────────┼───────────────┐
       RT SBT      CP SBT         MH SBT
    (m_sbt_rt)  (m_sbt_cp)    (m_sbt_mh/mhf)
          |           |               |
       m_ias_handle   m_aabb_gas_handle
          |
    IAS (single-level)
     ├── GAS₀ (Geometry 0, triangle/sphere)
     ├── GAS₁ (Geometry 1)
     └── GASₙ ...
```

---

## 2. 关键数据结构

### `ray_types.h` — 跨 CPU/GPU 共享类型

```cpp
// 射线（与 s3d_ray_pinned 完全兼容，32 bytes）
struct Ray {
    float3 origin;    float tmin;
    float3 direction; float tmax;
};

// 单次命中结果
struct HitResult {
    float        t;           // 命中距离；< 0 表示未命中
    float        bary_u, bary_v;
    unsigned int prim_idx;    // GAS 内局部 primitive 索引
    unsigned int geom_id;     // 来自 SBT 的几何体 ID
    unsigned int inst_id;     // 实例 ID（IAS 实例化时由 optixGetInstanceId 返回）
    float        normal[3];   // 未归一化几何法向量
};

// 多命中结果（Top-K，K=2）
#define MAX_MULTI_HITS 2u
struct MultiHitResult {
    unsigned int count;
    HitResult    hits[MAX_MULTI_HITS]; // 按距离升序排列
};

// GPU 内联过滤器的逐射线数据
struct FilterPerRayData {
    unsigned int hit_from_prim_id; // 自身 prim 索引（0xFFFFFFFF=无）
    unsigned int hit_from_geom_id; // 自身 geom_id
    unsigned int enc_id;           // 当前包壳 ID（0xFFFFFFFF=跳过包壳过滤）
    float        epsilon;          // 近距离自相交阈值
};
```

### `nn_types.h` — 最近点与包壳查询类型

```cpp
// 增强最近点查询（E2）
struct CPQuery  { float3 position; float radius; };
struct CPResult {
    float        distance;         // < 0 表示未命中
    float        normal[3];        // 三角形几何法向量
    float        uv[2];            // 最近点重心坐标
    float        closest_pos[3];   // GPU 计算的最近点坐标
    unsigned int prim_idx, geom_id, inst_id;
};

// 包壳查询（E5）
struct EnclosureQuery  { float3 position; };
struct EnclosureResult {
    int          prim_idx; // 最近面；-1 = 未找到
    float        distance;
    int          side;     // 0=正面(外部), 1=背面(内部), -1=退化
    unsigned int geom_id, inst_id;
};
```

### `unified_params.h` — 统一 Launch 参数（设备侧 `__constant__`）

```cpp
struct UnifiedParams {
    OptixTraversableHandle handle;     // 当前 GAS/IAS
    unsigned int           count;      // 射线/查询数量

    // RT 查询字段
    Ray*       rays;
    HitResult* hits;

    // 多命中字段（scratch buffer）
    MultiHitResult* multi_hits;

    // CP/NN 查询字段
    CPQuery*   cp_queries;
    CPResult*  cp_results;
    float3*    queries;           // 简单 NN 接口
    NNResult*  results;
    float3*    nn_vertices;       // 查询网格顶点
    uint3*     nn_indices;        // 查询网格索引

    // 包壳查询（E5）
    EnclosureQuery*  enclosure_queries;
    EnclosureResult* enclosure_results;

    // L4 GPU 内联过滤器
    FilterPerRayData* filter_data; // NULL = 无过滤
};

struct HitGroupData {
    unsigned int geom_id;       // 几何体 ID（per-geometry）
    unsigned int geom_type;     // GEOM_TYPE_TRIANGLE / GEOM_TYPE_SPHERE
    unsigned int flip_normal;   // E7: 法向量翻转标志
    float3*      vertices;
    uint3*       indices;
    float3*      sphere_centers;
    float*       sphere_radii;
    unsigned int* enc_front;    // L4: 逐 prim 前向包壳 ID
    unsigned int* enc_back;     // L4: 逐 prim 后向包壳 ID
};
```

### `unified_tracer.h` — 主控类关键字段

```cpp
class UnifiedTracer {
    // 模块
    OptixModule m_rt_module;  // programs.cu PTX
    OptixModule m_nn_module;  // nn_programs.cu PTX

    // Pipeline（全量唯一）
    OptixPipeline m_pipeline;

    // 程序组（15 个）
    OptixProgramGroup m_raygen_rt_pg;     // __raygen__rg
    OptixProgramGroup m_raygen_mh_pg;     // __raygen__mh
    OptixProgramGroup m_raygen_mhf_pg;    // __raygen__mh_filtered (L4)
    OptixProgramGroup m_raygen_nn_pg;     // __raygen__nn
    OptixProgramGroup m_raygen_cp_pg;     // __raygen__cp (E2)
    OptixProgramGroup m_miss_rt_pg;       // __miss__ms
    OptixProgramGroup m_miss_nn_pg;       // __miss__nn
    OptixProgramGroup m_hitgroup_tri_pg;          // __closesthit__ch
    OptixProgramGroup m_hitgroup_sphere_pg;       // __intersection__sphere + ch
    OptixProgramGroup m_hitgroup_tri_mh_pg;       // __anyhit__mh
    OptixProgramGroup m_hitgroup_sphere_mh_pg;    // IS + __anyhit__mh
    OptixProgramGroup m_hitgroup_tri_mhf_pg;      // __anyhit__mh_filtered (L4)
    OptixProgramGroup m_hitgroup_sphere_mhf_pg;   // IS + filtered AH (L4)
    OptixProgramGroup m_hitgroup_aabb_pg;         // __intersection__nn
    OptixProgramGroup m_hitgroup_aabb_sphere_pg;  // __intersection__nn_sphere

    // 六张 SBT
    OptixShaderBindingTable m_sbt_rt;   // 单命中 RT
    OptixShaderBindingTable m_sbt_mh;   // 多命中（K=2）
    OptixShaderBindingTable m_sbt_mhf;  // 多命中+过滤 (L4)
    OptixShaderBindingTable m_sbt_nn;   // 简单 NN
    OptixShaderBindingTable m_sbt_cp;   // 增强 CP（共享 NN 的 miss+hitgroup）
    OptixShaderBindingTable m_sbt_enc;  // 包壳（仅 raygen 不同）

    // 场景 GAS/IAS
    std::map<unsigned int, GeometryDesc> m_geometries;
    OptixTraversableHandle m_ias_handle;
    CUdeviceptr            m_ias_buffer;

    // AABB GAS（CP/NN 查询专用）
    OptixTraversableHandle m_aabb_gas_handle;
    CUdeviceptr            m_aabb_gas_buffer;
    float                  m_search_radius;
};
```

---

## 3. Pipeline 构建

### 模块编译选项
```cpp
m_pipeline_compile_options.numPayloadValues   = 10;   // E1+E2: 9 RT + 1 CP
m_pipeline_compile_options.numAttributeValues = 2;
m_pipeline_compile_options.usesPrimitiveTypeFlags =
    OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE | OPTIX_PRIMITIVE_TYPE_FLAGS_CUSTOM;
m_pipeline_compile_options.traversableGraphFlags =
    OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
m_pipeline_compile_options.pipelineLaunchParamsVariableName = "params";
```

### Pipeline 链接与栈大小
```cpp
OptixPipelineLinkOptions link_options = {};
link_options.maxTraceDepth = 1;  // 不支持递归光追

optixPipelineCreate(m_context, &m_pipeline_compile_options,
    &link_options, pgs, 15, ..., &m_pipeline);

// 栈大小配置：IAS → GAS（深度 2）
optixPipelineSetStackSize(m_pipeline,
    dc_from_traversal, dc_from_state, continuation,
    2 /* maxTraversalDepth: IAS -> GAS */);
```

---

## 4. 加速结构构建

### 场景 GAS（三角网格）
```cpp
// OPTIX_GEOMETRY_FLAG_NONE — 不禁用 anyhit，支持多命中模式
const uint32_t flags[1] = { OPTIX_GEOMETRY_FLAG_NONE };

OptixBuildInput build_input;
build_input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
build_input.triangleArray.vertexFormat     = OPTIX_VERTEX_FORMAT_FLOAT3;
build_input.triangleArray.indexFormat      = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
// 默认使用压缩 GAS：OPTIX_BUILD_FLAG_ALLOW_COMPACTION | PREFER_FAST_TRACE
```

### 场景 IAS（多几何体实例）
```cpp
// 每个启用的 geometry 对应一个 OptixInstance
OptixInstance inst;
inst.instanceId        = geom_id;    // geom_id 作为实例 ID
inst.sbtOffset         = sbt_idx;    // 一个几何体 → 一条 hitgroup 记录
inst.visibilityMask    = 255;
inst.flags             = OPTIX_INSTANCE_FLAG_NONE;
inst.traversableHandle = desc.gas_handle;

// IAS 构建不启用压缩
build_options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
build_input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
```

### AABB GAS（CP/NN 查询代理）
```cpp
// 每个三角形 → 1 个膨胀 AABB（膨胀量 = search_radius）
generateTriAABBsDevice(d_aabbs, vertices, indices, num_tris, search_radius);

OptixBuildInput build_input;
build_input.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
build_input.customPrimitiveArray.numPrimitives = m_num_query_tris;
// 默认压缩：OPTIX_BUILD_FLAG_ALLOW_COMPACTION | PREFER_FAST_TRACE
```

---

## 5. optixLaunch 调用模式

所有查询均通过同一 `m_pipeline`，切换 SBT 选择不同功能：

```cpp
// 单命中 RT（traceBatch）
UnifiedParams lp = { .handle = m_ias_handle, .count = count,
                     .rays = d_rays, .hits = d_hits };
cudaMemcpyAsync(params_ptr, &lp, sizeof(UnifiedParams), H2D, stream);

// 2D launch 网格
unsigned int w = (count <= 65536) ? count : 8192;
unsigned int h = (count + w - 1) / w;
optixLaunch(m_pipeline, stream, params_ptr, sizeof(UnifiedParams),
            &m_sbt_rt, w, h, 1);

// 增强 CP 查询（closestPointBatch）
lp.handle   = m_aabb_gas_handle;   // 切换到 AABB GAS
lp.cp_queries = d_queries;
lp.cp_results = d_results;
optixLaunch(m_pipeline, stream, external_params_ptr, sizeof(UnifiedParams),
            &m_sbt_cp, w, h, 1);

// 多命中（traceBatchMultiHit）
lp.multi_hits = d_multi_hits;
optixLaunch(m_pipeline, stream, params_ptr, sizeof(UnifiedParams),
            &m_sbt_mh, w, h, 1);

// L4 过滤多命中（traceBatchMultiHitFiltered）
lp.filter_data = d_filter_data;
lp.multi_hits  = d_multi_hits;   // device-only scratch
lp.hits        = d_single_hits;  // 最终输出
optixLaunch(m_pipeline, stream, external_params_ptr, sizeof(UnifiedParams),
            &m_sbt_mhf, w, h, 1);
```

**关键点**：`external_params_ptr` 为调用方持有的独立设备内存，避免多流并发时的竞态。

---

## 6. CP 查询（最近面投影）实现

### 方法：基于 AABB 代理的 OptiX BVH 遍历（RTNN 方法）

参考算法：Zhu et al., *RTNN: Accelerating Nearest Neighbor Search Using RT Cores*, PPoPP 2022

**不使用 cuBQL**，完全基于 OptiX custom primitive BVH。

#### 关键思路
每个三角形建立一个膨胀 AABB（边长 = `2 × search_radius`），查询点以零长度射线形式发出（`tmax = 1e-16f`）。BVH 遍历找到所有包含查询点的 AABB，自定义相交程序计算精确距离，通过 payload 追踪全局最近面。

#### device 程序（`nn_programs.cu: __raygen__cp`）
```cpp
// 零长度射线：origin = 查询点，direction = (1,0,0)，tmax = 1e-16f
optixTrace(
    params.handle,           // AABB GAS
    q.position,              // origin = 查询点
    make_float3(1.0f, 0.0f, 0.0f),  // direction（任意方向，不影响 AABB 命中）
    0.0f,                    // tmin
    1e-16f,                  // tmax → 零长度射线
    0.0f,                    // time
    OptixVisibilityMask(255),
    OPTIX_RAY_FLAG_NONE,
    0, 1, 0,                 // SBT record indices
    p0, p1, ..., p9          // 10 个 payload 寄存器
);
// p0 = 最小 dist²（初始化为 1e30f）
// p1 = 最近 prim_idx（初始化为 0xFFFFFFFF）
// p2~p4 = 法向量 xyz
// p5~p6 = 重心坐标 (w, u)
// p7~p9 = 最近点坐标 xyz
```

#### 自定义相交程序（`__intersection__nn`）
```cpp
// 调用 Voronoi 区域最近点算法（双精度，基于 Ericson RTCD）
float dist_sq = closestPointOnTriangle(query, v0, v1, v2, uv_w, uv_u, proj);
const float current_min_sq = __uint_as_float(optixGetPayload_0());
if (dist_sq < current_min_sq) {
    optixSetPayload_0(__float_as_uint(dist_sq));   // 更新最小距离²
    optixSetPayload_1(prim_idx);
    // 写入法向量、重心坐标、最近点坐标到 payload 2~9
}
// 注意：不调用 optixReportIntersection，确保 BVH 遍历继续
```

#### 最近点算法（双精度 Voronoi 区域法）
- 基于 Ericson《Real-Time Collision Detection》的重心坐标 Voronoi 区域分类法
- 完全双精度运算，避免 Voronoi 面积测试的灾难性消除
- 输出约定：$P = w \cdot v_0 + u \cdot v_1 + (1-w-u) \cdot v_2$

---

## 7. ENC 查询（Enclosure 关系 / 包壳判断）

### 方法：最近点 + 单条射线法向量投影

**ENC 查询完全在 Host 端协调**（两轮 GPU 调用），无专用的单一 device program。

```
findEnclosureBatch:
  Step 1: closestPointBatch()      → 包含哪个几何体的哪个 prim，距离多远
  Step 2: traceBatch() +X 方向射线 → 射线方向与命中法向量的点积确定内外
  Step 3: CPU 聚合结果              → 构建 EnclosureResult
```

#### 具体实现（`unified_tracer.cpp: findEnclosureBatch`）
```cpp
// Step 1: 无限制半径 CP 查询
h_cp_queries[i].position = h_enc_queries[i].position;
h_cp_queries[i].radius   = 1e30f;  // 无限制
closestPointBatch(d_cp_queries, d_cp_results, count, stream);

// Step 2: 沿 +X 轴发射 1 条射线
h_rays[i].origin    = h_enc_queries[i].position;
h_rays[i].direction = make_float3(1.0f, 0.0f, 0.0f);  // +X 方向
h_rays[i].tmin      = 1e-6f;
h_rays[i].tmax      = 1e30f;
traceBatch(d_rays, d_hits, count, stream);   // 使用 m_sbt_rt

// Step 3: 内外判断
float dot_dn = h_hits[i].normal[0];  // ray_dir=(1,0,0)，dot = normal.x
er.side = (dot_dn < 0.0f) ? 0 : 1;  // 0=正面(外部), 1=背面(内部)
// 退化判断: distance < 1e-8f → side = -1
// 射线未命中 → outside (side = 0)
```

**关键点**：
- **每次 ENC 查询使用 1 条射线**（方向为 +X 轴）
- 使用 CP 结果确定最近面（prim_idx, geom_id），用射线法向量投影确定内外
- 可选的 GPU 内联过滤（L4 MHF 模式）也使用 `enc_front[prim_idx]`/`enc_back[prim_idx]` 表提供逐 prim 的包壳归属信息

---

## 8. 多命中（Multi-Hit）光追

### Top-K（K=2）Any-Hit 实现

```cpp
// __anyhit__mh：每次命中均写入，不调用 optixTerminateRay
MultiHitResult& result = params.multi_hits[linear_idx];
if (cnt < MAX_MULTI_HITS) {
    result.hits[cnt] = new_hit;
    result.count = cnt + 1;
} else {
    // 替换最远者（维护 Top-K）
    find farthest; if (t_new < max_t) result.hits[farthest] = new_hit;
}
optixIgnoreIntersection();  // 不接受命中 → BVH 遍历不截断 tMax
```

---

## 9. L4 GPU 内联过滤器（MHF 模式）

`__anyhit__mh_filtered` 在写入 Top-K 前执行三重过滤：

```cpp
// ① 自相交过滤：同一 prim 且同一 geom → 忽略
if (prim == filter.hit_from_prim_id && gid == filter.hit_from_geom_id)
    optixIgnoreIntersection(); return;

// ② 近距离 epsilon 过滤
if (t_new > 0.0f && t_new < filter.epsilon)
    optixIgnoreIntersection(); return;

// ③ 包壳边界过滤：HitGroupData 携带 enc_front/enc_back 表
if (filter.enc_id != 0xFFFFFFFF && data->enc_front) {
    if (enc_front[prim] != enc_id && enc_back[prim] != enc_id)
        optixIgnoreIntersection(); return;
}
```

`__raygen__mh_filtered` 在 trace 结束后将 Multi-Hit scratch buffer reduce 为单一 `HitResult`，写入 `params.hits[]`。

---

## 10. 关键设计决策摘要

| 设计点 | 实现方式 |
|--------|---------|
| **GAS 数量** | 双 GAS 类型：三角 GAS（RT）+ 自定义 AABB GAS（CP）；前者通过 IAS 组合多几何体 |
| **Pipeline 数量** | 单一 Pipeline，含 2 个 PTX 模块、15 个 Program Group、6 张 SBT |
| **CP 查询方法** | RTNN-style AABB 代理 BVH + 零长度射线；自定义相交程序双精度 Voronoi 最近点 |
| **ENC 查询方法** | 两步法：① CP 找最近面 ② 单条 +X 射线法向量投影；Host 端协调 |
| **多命中** | K=2 Any-Hit + `optixIgnoreIntersection()` 维持 BVH 遍历完整性 |
| **自相交处理** | MHF 模式：L4 GPU 内联三重过滤（prim/geom ID + epsilon + 包壳边界） |
| **Launch 参数** | `UnifiedParams` via `__constant__`；per-ctx 独立设备缓冲避免多流竞态 |
| **法向量** | 三角形：对象空间叉积 + `optixTransformNormalFromObjectToWorldSpace`；E7 翻转标志 |
| **IAS 实例化** | 每个几何体一个 instance，`instanceId = geom_id`，`sbtOffset = sbt_id` |
| **Payload 数量** | 10 个 uint32 寄存器（9 for RT hit + 1 extra，复用为 CP 的 closest_pos.z） |

---

## 11. 核心 API 调用链（面向求解器）

```
求解器批量查询
    │
    ▼
ox_s3d_scene_view.cpp
    ├── s3d_scene_view_trace_rays_batch_ctx_filtered_async()
    │       └── tracer.traceBatchMultiHitFiltered(..., m_sbt_mhf)
    │               └── optixLaunch(pipeline, stream, params, sbt_mhf, w, h, 1)
    │
    ├── s3d_scene_view_closest_point()
    │       └── tracer.closestPointBatch(...) → rebuildQueryGAS → optixLaunch(sbt_cp)
    │
    └── s3d_scene_view_find_enclosure_batch() [逻辑推断]
            └── tracer.findEnclosureBatch()
                    ├── closestPointBatch() → optixLaunch(sbt_cp)
                    └── traceBatch()        → optixLaunch(sbt_rt)
```

---

*注：以上所有代码摘录均来自实际源文件，未经修改。*
