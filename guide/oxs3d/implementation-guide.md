# oxstar-3d 实现指南

**版本**: 0.1  
**日期**: 2026-02-20  
**前置**: [architecture.md](architecture.md), [module-interface-mapping.md](module-interface-mapping.md)

---

## 1. OptiX 7 编程要点

### 1.1 OptiX 7 初始化流程

```cpp
// 1. 初始化 CUDA
cudaFree(0);  // 强制初始化 CUDA context
CUcontext cuda_ctx;
cuCtxGetCurrent(&cuda_ctx);

// 2. 初始化 OptiX
optixInit();  // 加载 optix 函数表 (optix_stubs.h)

// 3. 创建 Device Context
OptixDeviceContextOptions ctx_options = {};
ctx_options.logCallbackFunction = optix_log_cb;
ctx_options.logCallbackLevel = 4;  // 0=disable, 4=verbose
OptixDeviceContext optix_ctx;
optixDeviceContextCreate(cuda_ctx, &ctx_options, &optix_ctx);
```

### 1.2 OptiX Module 编译

Device Programs (.cu) 编译为 PTX 后，在运行时通过 `optixModuleCreate` 加载：

```cpp
OptixModuleCompileOptions module_opts = {};
module_opts.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
module_opts.optLevel   = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
module_opts.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;

OptixPipelineCompileOptions pipeline_opts = {};
pipeline_opts.usesMotionBlur        = false;
pipeline_opts.traversableGraphDepth = 2;  // GAS + IAS
pipeline_opts.numPayloadValues      = 8;  // payload 寄存器数量
pipeline_opts.numAttributeValues    = 2;  // triangle barycentrics (u, v)
pipeline_opts.exceptionFlags        = OPTIX_EXCEPTION_FLAG_NONE;
pipeline_opts.pipelineLaunchParamsVariableName = "params";

OptixModule module;
optixModuleCreate(
    optix_ctx,
    &module_opts, &pipeline_opts,
    ptx_string, ptx_size,
    log_buffer, &log_size,
    &module);
```

### 1.3 Program Group 创建

```cpp
// RayGen
OptixProgramGroupDesc pg_desc_raygen = {};
pg_desc_raygen.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
pg_desc_raygen.raygen.module = module;
pg_desc_raygen.raygen.entryFunctionName = "__raygen__trace";

optixProgramGroupCreate(optix_ctx, &pg_desc_raygen, 1,
    &pg_opts, log, &log_size, &pg_raygen);

// ClosestHit + Intersection (三角形)
OptixProgramGroupDesc pg_desc_hit_tri = {};
pg_desc_hit_tri.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
pg_desc_hit_tri.hitgroup.moduleCH = module;
pg_desc_hit_tri.hitgroup.entryFunctionNameCH = "__closesthit__triangle";
pg_desc_hit_tri.hitgroup.moduleIS = nullptr;  // 内置三角形交叉
pg_desc_hit_tri.hitgroup.entryFunctionNameIS = nullptr;

// ClosestHit + Intersection (球体)
OptixProgramGroupDesc pg_desc_hit_sphere = {};
pg_desc_hit_sphere.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
pg_desc_hit_sphere.hitgroup.moduleCH = module;
pg_desc_hit_sphere.hitgroup.entryFunctionNameCH = "__closesthit__sphere";
pg_desc_hit_sphere.hitgroup.moduleIS = module;
pg_desc_hit_sphere.hitgroup.entryFunctionNameIS = "__intersection__sphere";

// Miss
OptixProgramGroupDesc pg_desc_miss = {};
pg_desc_miss.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
pg_desc_miss.miss.module = module;
pg_desc_miss.miss.entryFunctionName = "__miss__default";
```

### 1.4 Pipeline 链接

```cpp
OptixPipelineLinkOptions link_opts = {};
link_opts.maxTraceDepth = 1;  // 不需要递归

OptixProgramGroup program_groups[] = {
    pg_raygen, pg_miss, pg_hit_tri, pg_hit_sphere
};

optixPipelineCreate(
    optix_ctx,
    &pipeline_opts, &link_opts,
    program_groups, 4,
    log, &log_size,
    &pipeline);

// 设置 stack sizes
optixPipelineSetStackSize(pipeline,
    2048,  // direct callable stack from traversal
    2048,  // direct callable stack from state
    2048,  // continuation stack
    1);    // max traversable depth for IAS->GAS
```

---

## 2. Shader Binding Table (SBT) 详解

### 2.1 SBT Record 内存布局

每个 SBT Record = `OPTIX_SBT_RECORD_HEADER_SIZE` (32 bytes) + 用户数据。

```cpp
template<typename T>
struct SbtRecord {
    __align__(OPTIX_SBT_RECORD_ALIGNMENT)
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T data;
};

typedef SbtRecord<RayGenData>      RayGenSbtRecord;
typedef SbtRecord<MissData>        MissSbtRecord;
typedef SbtRecord<oxs3d_hit_group_data> HitGroupSbtRecord;
```

### 2.2 SBT 构建

按 `geom_store` 中每个 `geom_entry` 生成一条 HitGroup Record：

```cpp
res_T oxs3d_pipeline_build_sbt(oxs3d_pipeline* pipe,
                               const oxs3d_geom_store* store,
                               oxs3d_device* dev)
{
    // 1. RayGen Record
    RayGenSbtRecord rg_record;
    optixSbtRecordPackHeader(pipe->pg_raygen_trace, &rg_record);
    cudaMemcpy(pipe->d_raygen_record, &rg_record, sizeof(rg_record), H2D);

    // 2. Miss Record
    MissSbtRecord ms_record;
    optixSbtRecordPackHeader(pipe->pg_miss, &ms_record);
    cudaMemcpy(pipe->d_miss_record, &ms_record, sizeof(ms_record), H2D);

    // 3. HitGroup Records — 每个 geometry 一条
    size_t n = store->entry_count;
    HitGroupSbtRecord* hg_records = (HitGroupSbtRecord*)malloc(n * sizeof(HitGroupSbtRecord));

    for (size_t i = 0; i < n; i++) {
        const geom_entry* ge = &store->entries[i];

        if (ge->type == PRIM_TRIANGLE) {
            optixSbtRecordPackHeader(pipe->pg_hitgroup_tri, &hg_records[i]);
        } else {
            optixSbtRecordPackHeader(pipe->pg_hitgroup_sphere, &hg_records[i]);
        }

        hg_records[i].data.geom_idx      = (uint32_t)i;
        hg_records[i].data.vertices       = store->d_vertices.data;
        hg_records[i].data.indices        = store->d_indices.data;
        hg_records[i].data.spheres        = store->d_spheres;
        hg_records[i].data.geom_entries   = store->d_geom_entries;
        hg_records[i].data.tri_count      = store->total_tris;
        hg_records[i].data.flip_surface   = ge->flip_surface;
        hg_records[i].data.is_enabled     = ge->is_enabled;
    }

    cudaMemcpy(pipe->d_hitgroup_records, hg_records, n * sizeof(HitGroupSbtRecord), H2D);
    free(hg_records);

    // 4. 填充 SBT 结构
    pipe->sbt_trace.raygenRecord                = pipe->d_raygen_record;
    pipe->sbt_trace.missRecordBase              = pipe->d_miss_record;
    pipe->sbt_trace.missRecordStrideInBytes      = sizeof(MissSbtRecord);
    pipe->sbt_trace.missRecordCount              = 1;
    pipe->sbt_trace.hitgroupRecordBase           = pipe->d_hitgroup_records;
    pipe->sbt_trace.hitgroupRecordStrideInBytes   = sizeof(HitGroupSbtRecord);
    pipe->sbt_trace.hitgroupRecordCount          = (uint32_t)n;

    return RES_OK;
}
```

### 2.3 SBT 索引与 GAS Build Input 的对应关系

OptiX 在 `optixTrace` 中通过以下公式选择 SBT record:

```
SBT_index = SBT_instance_offset       // 由 OptixInstance 设置
          + SBT_GAS_index * SBT_stride // GAS 内的 geometry 索引
          + ray_type_offset            // 射线类型 (0=primary)
```

因此，GAS Build Input 中各 geometry 的顺序必须与 SBT HitGroup Record 的顺序一致。

**关键约束**: `geom_store.entries[]` 的顺序 = GAS Build Input 数组的顺序 = SBT HitGroup Record 的顺序。

---

## 3. GAS 构建策略

### 3.1 三角形 GAS Build Input

每个 mesh `geom_entry` 对应一个 `OptixBuildInput`:

```cpp
std::vector<OptixBuildInput> build_inputs;
std::vector<uint32_t> flags_per_sbt;  // OPTIX_GEOMETRY_FLAG_NONE

for (size_t i = 0; i < store->entry_count; i++) {
    const geom_entry* ge = &store->entries[i];

    if (ge->type == PRIM_TRIANGLE) {
        OptixBuildInput input = {};
        input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;

        input.triangleArray.vertexFormat  = OPTIX_VERTEX_FORMAT_FLOAT3;
        input.triangleArray.numVertices   = ge->vertex_count;
        input.triangleArray.vertexBuffers =
            &((CUdeviceptr)(store->d_vertices.data + ge->vertex_offset));

        input.triangleArray.indexFormat   = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        input.triangleArray.numIndexTriplets = ge->prim_count;
        input.triangleArray.indexBuffer   =
            (CUdeviceptr)(store->d_indices.data + ge->prim_offset);

        flags_per_sbt.push_back(OPTIX_GEOMETRY_FLAG_NONE);
        input.triangleArray.flags         = &flags_per_sbt.back();
        input.triangleArray.numSbtRecords = 1;

        build_inputs.push_back(input);
    } else {
        // 球体: Custom Primitive (AABB)
        OptixBuildInput input = {};
        input.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;

        input.customPrimitiveArray.aabbBuffers =
            &(store->d_sphere_aabbs + ge->prim_offset * sizeof(OptixAabb));
        input.customPrimitiveArray.numPrimitives = ge->prim_count;

        flags_per_sbt.push_back(OPTIX_GEOMETRY_FLAG_NONE);
        input.customPrimitiveArray.flags         = &flags_per_sbt.back();
        input.customPrimitiveArray.numSbtRecords = 1;

        build_inputs.push_back(input);
    }
}
```

### 3.2 IAS 构建 (实例化场景)

```cpp
std::vector<OptixInstance> instances(accel->instance_entries_count);

for (uint32_t i = 0; i < accel->instance_entries_count; i++) {
    const oxs3d_instance_entry* ie = &accel->instance_entries[i];

    OptixInstance& inst = instances[i];
    memset(&inst, 0, sizeof(inst));

    // 3x4 行优先变换矩阵 (OptiX 要求行优先)
    // custar-3d 使用列优先 → 需要转置
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            inst.transform[r*4+c] = ie->transform[c*3+r];

    inst.instanceId        = i;
    inst.sbtOffset         = i * store->entry_count;  // 按实例偏移 SBT
    inst.visibilityMask    = 0xFF;
    inst.flags             = OPTIX_INSTANCE_FLAG_NONE;
    inst.traversableHandle = ie->child_gas;
}

// 上传到 GPU
CUdeviceptr d_instances;
cudaMalloc((void**)&d_instances, instances.size() * sizeof(OptixInstance));
cudaMemcpy(d_instances, instances.data(), ...);

// 构建 IAS
OptixBuildInput ias_input = {};
ias_input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
ias_input.instanceArray.instances    = d_instances;
ias_input.instanceArray.numInstances = (uint32_t)instances.size();

// ... optixAccelBuild for IAS ...
```

---

## 4. Payload 与全局内存权衡

### 4.1 Payload 方案 (推荐)

OptiX 7 payload 是通过寄存器传递的 `uint32_t` 值，最多 32 个 (推荐 ≤ 8 个以保持 occupancy)。

当前 `cus3d_hit_result` 需要的数据量：

| 字段 | 类型 | 大小 | Payload 编码 |
|------|------|------|-------------|
| `prim_id` | int32 | 4B | p0 |
| `geom_idx` | int32 | 4B | p1 |
| `inst_id` | int32 | 4B | p2 |
| `distance` | float | 4B | p3 |
| `normal[0]` | float | 4B | p4 |
| `normal[1]` | float | 4B | p5 |
| `normal[2]` | float | 4B | p6 |
| `uv[0]` + `uv[1]` | 2×float | 8B | p7 (half-packed) |
| **合计** | | **32B** | **8 payloads** |

UV 使用 `__float2half` 打包到一个 `uint32_t` 中，精度足够（barycentric coords 范围 [0,1]，half 精度 ~3 位小数）。

### 4.2 全局内存方案 (备选)

如果需要更多数据或更高 UV 精度：

```cuda
// Launch params 中添加输出缓冲区
struct oxs3d_launch_params {
    // ...
    cus3d_hit_result* results;  // 1 per ray, 全局内存
};

// ClosestHit 中直接写全局内存
extern "C" __global__ void __closesthit__triangle() {
    uint32_t ray_idx = optixGetPayload_0();  // payload 仅传 ray index
    params.results[ray_idx].prim_id = ...;
    // ...
}
```

**权衡**: 全局内存写入有延迟，但 payload 数量减少到 1 个，大幅提升 occupancy。对于大批量射线（>10K），全局内存方案可能更快。

---

## 5. Hit Filter 实现策略

### 5.1 与 custar-3d 一致的 Top-K + CPU Filter 流程

```
s3d_scene_view_trace_ray(scnview, origin, dir, range, ray_data, hit):
  1. 启动 OptiX Pipeline (单射线 or 批量)
     → OptiX 在 GPU 上找到最近命中
     → 返回 cus3d_hit_result (prim_id, geom_idx, distance, normal, uv)

  2. CPU 端 trace_hit_fixup() — UV/法线修正 (复用 cus3d_trace_util.h)

  3. CPU 端 filter 检查:
     geom_entry* ge = &store->entries[gpu_hit.geom_idx];
     if (ge->filter_func != NULL) {
         构造 s3d_hit
         int rejected = ge->filter_func(&hit, origin, dir, range, ray_data, ge->filter_data);
         if (rejected) → 重试 (调整 range[0] = hit.distance + epsilon)
     }

  4. 重试策略 (同 custar-3d):
     - 单命中: 最多 MAX_FILTER_RETRIES 次重新 trace
     - Top-K: 先取 K 个候选，依次 filter，仅全部拒绝时重新 trace
```

### 5.2 OptiX AnyHit 实现 Top-K

```cuda
// __anyhit__triangle_multi: 在 GPU 上收集 Top-K 命中
extern "C" __global__ void __anyhit__triangle_multi()
{
    // payload p0 = ray index
    // payload p1 = 当前已收集数量 (count)
    // 命中数据写入全局内存: multi_hit_buffer[ray_idx]

    const uint32_t ray_idx = optixGetPayload_0();
    uint32_t count = optixGetPayload_1();
    const float t = optixGetRayTmax();

    // 获取当前命中信息 ...
    cus3d_hit_result hit;
    hit.prim_id  = ...;
    hit.distance = t;
    // ...

    // 插入排序到 Top-K 数组
    cus3d_multi_hit_result* mhr = &params.multi_results[ray_idx];

    if (count < CUS3D_MAX_MULTI_HITS) {
        // 有空位，直接插入
        mhr->hits[count] = hit;
        count++;
        // 插入排序
        for (int i = count - 1; i > 0; i--) {
            if (mhr->hits[i].distance < mhr->hits[i-1].distance) {
                cus3d_hit_result tmp = mhr->hits[i];
                mhr->hits[i] = mhr->hits[i-1];
                mhr->hits[i-1] = tmp;
            }
        }
        mhr->count = count;
        optixSetPayload_1(count);
    } else if (t < mhr->hits[count-1].distance) {
        // 比最远的更近，替换最后一个
        mhr->hits[count-1] = hit;
        // 重新排序
        for (int i = count - 1; i > 0; i--) {
            if (mhr->hits[i].distance < mhr->hits[i-1].distance) {
                cus3d_hit_result tmp = mhr->hits[i];
                mhr->hits[i] = mhr->hits[i-1];
                mhr->hits[i-1] = tmp;
            }
        }
    }

    // 始终调用 optixIgnoreIntersection() 继续遍历
    optixIgnoreIntersection();
}
```

> **注意**: `__anyhit__` 中写全局内存有原子性风险 — 但在 OptiX 7 中，同一条射线的 AnyHit 调用是串行的（不会并发），所以直接写 `mhr[]` 是安全的。

---

## 6. 包壳定位 (Find Enclosure) OptiX 方案

### 6.1 6-Ray 方案

与 custar-3d 的 CUDA kernel 方案完全一致，区别是射线追踪由 OptiX 执行：

```
对每个查询点 P:
  1. 构造 6 条 PI/4 旋转的轴对齐射线 (与 CPU 算法一致)
  2. 发射 6 条射线 (通过 optixLaunch)
  3. 对第一条有效命中，计算 dot(ray_dir, hit_normal) 判断 front/back
  4. 输出 (prim_id, side)
```

两种实现选择：

**A. 合并为一次 optixLaunch (推荐):**
- 6 * N_queries 条射线合并为一次 launch
- RayGen 中按 `launch_index / N_queries` 选择射线方向
- 优点: 单次 launch，GPU 利用率高

**B. 6 次 optixLaunch:**
- 每个方向一次 launch，逐步过滤已解决的查询
- 优点: 支持 early-exit，实际平均 ~1.5 次 launch

```cuda
// 方案 A: 合并 launch
extern "C" __global__ void __raygen__find_enclosure()
{
    const uint3 idx = optixGetLaunchIndex();
    const uint32_t query_idx = idx.x;
    const uint32_t dir_idx   = idx.y;  // 0-5

    if (query_idx >= params.num_queries) return;

    // 已经解决的查询跳过
    if (params.enc_resolved[query_idx]) return;

    float3 pos = params.enc_positions[query_idx];
    float3 dir = get_rotated_axis_direction(dir_idx);

    // optixTrace...
    // 如果命中且 dot(dir, normal) 有效 → 写入结果并标记 resolved
}
```

---

## 7. 双精度支持策略

### 7.1 当前状况

custar-3d 使用 `float` (单精度) 进行所有 GPU 计算，与 Embree CPU 版本（也是 float）一致。

### 7.2 OptiX 限制

- **OptiX 内置三角形交叉** 仅支持 `float` (FP32)
- **Custom Primitive (球体)** 的 Intersection Program 中可以使用 `double`
- **RayGen / ClosestHit / AnyHit** 中可以使用 `double` 计算

### 7.3 策略

维持与 custar-3d 一致的 `float` 精度。如果未来需要 `double`:

1. 三角形: 使用 Custom Primitive 方式（AABB + Intersection Program），在 Intersection Program 中用 `double` 做 Möller-Trumbore
2. 球体: Intersection Program 中已经是自定义交叉，直接改用 `double`
3. 法线/UV 计算: 在 ClosestHit 中使用 `double`

---

## 8. 错误处理

### 8.1 OptiX 错误码

```cpp
#define OPTIX_CHECK(call)                                                 \
    do {                                                                   \
        OptixResult res = call;                                            \
        if (res != OPTIX_SUCCESS) {                                        \
            const char* msg = optixGetErrorName(res);                      \
            log_error(dev, "OptiX error: %s (%s:%d)", msg, __FILE__, __LINE__); \
            return RES_ERR;                                                \
        }                                                                  \
    } while(0)
```

### 8.2 与 s3d 错误模型对齐

所有 `oxs3d_*` 函数返回 `res_T` (与 `cus3d_*` 一致)，OptiX 错误映射：

| OptiX Result | s3d res_T | 说明 |
|-------------|-----------|------|
| `OPTIX_SUCCESS` | `RES_OK` | 成功 |
| `OPTIX_ERROR_OUT_OF_MEMORY` | `RES_MEM_ERR` | 内存不足 |
| `OPTIX_ERROR_INVALID_VALUE` | `RES_BAD_ARG` | 参数无效 |
| 其他 | `RES_ERR` | 通用错误 |

---

## 9. 调试和验证

### 9.1 Debug 构建

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(OPTIX_COMPILE_OPT OPTIX_COMPILE_OPTIMIZATION_LEVEL_0)
  set(OPTIX_DEBUG_LEVEL OPTIX_COMPILE_DEBUG_LEVEL_FULL)
  target_compile_definitions(s3d PRIVATE OPTIX_DEBUG=1)
endif()
```

### 9.2 验证矩阵

与 custar-3d 共用同一套测试用例（`test_s3d_*.c`），验证 oxstar-3d 的输出与 custar-3d 一致：

| 测试 | 验证内容 | 容差 |
|------|---------|------|
| `test_s3d_trace_ray` | 单射线命中 prim_id, distance, uv | prim_id 精确; distance ≤ 1e-4 |
| `test_s3d_batch_trace` | 批量射线 | 同上 |
| `test_s3d_closest_point` | 最近点 distance, prim_id | prim_id 精确; distance ≤ 1e-4 |
| `test_s3d_scene_view` | 场景视图创建/销毁 | 无内存泄漏 |
| `test_s3d_sphere` | 球体交叉 | prim_id 精确; distance ≤ 1e-4 |
| `test_s3d_trace_ray_instance` | 实例化射线追踪 | 同上 |
| Cornell Box IR 渲染 | 像素级热力图对比 | 逐像素 ≤ 1e-6 |

> **注意**: OptiX 内置三角形交叉算法与 cuBQL 中的 Möller-Trumbore 实现不同（OptiX 使用 Plücker 坐标或 watertight），命中距离和 UV 可能存在微小差异（1e-5 ~ 1e-4 量级），需要适当放宽容差。

---

## 10. PTX 管理策略

### 10.1 编译时 PTX 内嵌

将 PTX 编译为 C 数组内嵌到二进制文件中，避免运行时文件查找：

```cmake
# 将 PTX 文件转换为 C 头文件
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/generated/oxs3d_programs_ptx.h
  COMMAND ${CMAKE_COMMAND}
    -DINPUT=${CMAKE_BINARY_DIR}/ptx/oxs3d_programs.ptx
    -DOUTPUT=${CMAKE_BINARY_DIR}/generated/oxs3d_programs_ptx.h
    -DVAR_NAME=oxs3d_programs_ptx
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2c.cmake
  DEPENDS ${CMAKE_BINARY_DIR}/ptx/oxs3d_programs.ptx
)
```

```cpp
// 运行时直接使用
#include "oxs3d_programs_ptx.h"
//  → const char oxs3d_programs_ptx[] = { ... };  // PTX 字符串
//  → const size_t oxs3d_programs_ptx_size = ...;
```

### 10.2 运行时 PTX 加载 (开发阶段)

```cpp
// 从文件加载 PTX (便于快速迭代)
std::ifstream ptx_file(OXS3D_PTX_DIR "/oxs3d_programs.ptx");
std::string ptx_source((std::istreambuf_iterator<char>(ptx_file)),
                        std::istreambuf_iterator<char>());
```

---

## 11. 性能调优建议

### 11.1 Occupancy

- 保持 payload 数量 ≤ 8
- 减少 Intersection/ClosestHit 中的寄存器使用
- 使用 `optixModuleCreateWithTasks` 进行异步编译

### 11.2 AS 构建

- 使用 `OPTIX_BUILD_FLAG_ALLOW_COMPACTION` 并执行 compaction（减少 30-50% 内存）
- 对动态场景使用 `OPTIX_BUILD_FLAG_ALLOW_UPDATE` + `optixAccelBuild` (refit)

### 11.3 内存访问

- SBT record 中的 device pointer 对齐到 16 bytes
- Launch params 结构体对齐到 128 bytes
- 射线数据使用 SoA 布局 (已是 custar-3d 的设计)

### 11.4 Launch 配置

- `optixLaunch` 的 width = 射线数量，height = depth = 1
- OptiX 内部自动管理线程调度，不需要手动配置 block size

---

*文档更新: 2026-02-20 | 关联: architecture.md, module-interface-mapping.md*
