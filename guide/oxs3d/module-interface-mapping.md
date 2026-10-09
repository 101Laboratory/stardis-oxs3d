# custar-3d ↔ oxstar-3d 完整接口映射表

**版本**: 0.1  
**日期**: 2026-02-20  
**关联**: [architecture.md](architecture.md) §5 模块详细设计

---

## 1. 公共 API 映射 (s3d.h — 100% 不变)

| s3d.h 函数 | 签名 | oxstar-3d 变更 |
|-----------|------|---------------|
| `s3d_device_create` | `(logger*, mem_allocator*, int, s3d_device**)` | 内部调用 `oxs3d_device_create` |
| `s3d_device_ref_get/put` | `(s3d_device*)` | 不变 |
| `s3d_device_get_gpu_sm_count` | `(s3d_device*) → int` | 不变 |
| `s3d_scene_create` | `(s3d_device*, s3d_scene**)` | 不变 |
| `s3d_scene_ref_get/put` | `(s3d_scene*)` | 不变 |
| `s3d_scene_instantiate` | `(s3d_scene*, s3d_shape**)` | 不变 |
| `s3d_scene_attach_shape` | `(s3d_scene*, s3d_shape*)` | 不变 |
| `s3d_scene_detach_shape` | `(s3d_scene*, s3d_shape*)` | 不变 |
| `s3d_scene_clear` | `(s3d_scene*)` | 不变 |
| `s3d_scene_get_device` | `(s3d_scene*, s3d_device**)` | 不变 |
| `s3d_scene_get_shapes_count` | `(s3d_scene*, size_t*)` | 不变 |
| `s3d_scene_view_create` | `(s3d_scene*, int, s3d_scene_view**)` | oxs3d 后端初始化 |
| `s3d_scene_view_create2` | `(s3d_scene*, int, s3d_accel_struct_conf*, s3d_scene_view**)` | oxs3d 后端初始化 |
| `s3d_scene_view_ref_get/put` | `(s3d_scene_view*)` | 不变 |
| `s3d_scene_view_get_mask` | `(s3d_scene_view*, int*)` | 不变 |
| `s3d_scene_view_trace_ray` | `(scnview, origin[3], dir[3], range[2], ray_data, s3d_hit*)` | 调用 oxs3d_trace |
| `s3d_scene_view_trace_rays` | `(scnview, nrays, mask, origins, dirs, ranges, ...)` | 调用 oxs3d_trace batch |
| `s3d_scene_view_closest_point` | `(scnview, pos[3], radius, query_data, s3d_hit*)` | 调用 oxs3d_closest_point |
| `s3d_scene_view_sample` | `(scnview, u, v, w, primitive*, st[2])` | 不变 (纯 CPU) |
| `s3d_scene_view_get_primitive` | `(scnview, iprim, s3d_primitive*)` | 不变 (纯 CPU) |
| `s3d_scene_view_primitives_count` | `(scnview, size_t*)` | 不变 (纯 CPU) |
| `s3d_scene_view_compute_area` | `(scnview, float*)` | 不变 (纯 CPU) |
| `s3d_scene_view_compute_volume` | `(scnview, float*)` | 不变 (纯 CPU) |
| `s3d_scene_view_get_aabb` | `(scnview, lower[3], upper[3])` | 从 oxs3d_accel 获取 |
| `s3d_shape_ref_get/put` | `(s3d_shape*)` | 不变 |
| `s3d_shape_get_id` | `(s3d_shape*, unsigned*)` | 不变 |
| `s3d_shape_enable` | `(s3d_shape*, char)` | 不变 |
| `s3d_shape_is_enabled` | `(s3d_shape*, char*)` | 不变 |
| `s3d_shape_flip_surface` | `(s3d_shape*)` | 不变 |
| `s3d_primitive_get_attrib` | `(prim, attr, st[2], s3d_attrib*)` | 不变 (纯 CPU) |
| `s3d_primitive_has_attrib` | `(prim, attr, char*)` | 不变 (纯 CPU) |
| `s3d_primitive_sample` | `(prim, u, v, st[2])` | 不变 (纯 CPU) |
| `s3d_primitive_compute_area` | `(prim, float*)` | 不变 (纯 CPU) |
| `s3d_primitive_get_transform` | `(prim, float[12])` | 不变 (纯 CPU) |
| `s3d_triangle_get_vertex_attrib` | `(prim, ivert, usage, s3d_attrib*)` | 不变 (纯 CPU) |
| `s3d_shape_create_sphere` | `(s3d_device*, s3d_shape**)` | 不变 |
| `s3d_sphere_setup` | `(shape, pos[3], radius)` | 不变 |
| `s3d_sphere_set_hit_filter_function` | `(shape, func, data)` | 不变 |
| `s3d_sphere_get_hit_filter_data` | `(shape, void**)` | 不变 |
| `s3d_shape_create_mesh` | `(s3d_device*, s3d_shape**)` | 不变 |
| `s3d_mesh_setup_indexed_vertices` | `(shape, ntris, get_indices, nverts, attribs, nattribs, data)` | 不变 |
| `s3d_mesh_copy` | `(src, dst)` | 不变 |
| `s3d_mesh_get_vertices_count` | `(shape, unsigned*)` | 不变 |
| `s3d_mesh_get_vertex_attrib` | `(shape, ivert, usage, attrib)` | 不变 |
| `s3d_mesh_get_triangles_count` | `(shape, unsigned*)` | 不变 |
| `s3d_mesh_get_triangle_indices` | `(shape, itri, ids[3])` | 不变 |
| `s3d_mesh_set_hit_filter_function` | `(shape, func, data)` | 不变 |
| `s3d_mesh_get_hit_filter_data` | `(shape, void**)` | 不变 |
| `s3d_instance_set_position` | `(shape, pos[3])` | 不变 |
| `s3d_instance_translate` | `(shape, space, translation[3])` | 不变 |
| `s3d_instance_set_transform` | `(shape, float[12])` | 不变 |
| `s3d_instance_transform` | `(shape, space, float[12])` | 不变 |

### 批量射线 API (GPU 加速扩展)

| s3d.h 函数 | oxstar-3d 变更 |
|-----------|---------------|
| `s3d_scene_view_trace_rays_batch` | 调用 oxs3d_trace batch + Pipeline |
| `s3d_batch_trace_context_create/destroy` | 预分配 GPU buffer |
| `s3d_scene_view_trace_rays_batch_ctx` | 复用上下文的批量追踪 |
| `s3d_scene_view_closest_point_batch` | 调用 oxs3d_closest_point (cuBQL) |
| `s3d_batch_cp_context_create/destroy` | 预分配 GPU buffer |
| `s3d_scene_view_closest_point_batch_ctx` | 复用上下文的批量最近点 |
| `s3d_scene_view_find_enclosure_batch` | 调用 oxs3d_find_enclosure (OptiX 6-ray) |
| `s3d_batch_enc_context_create/destroy` | 预分配 GPU buffer |
| `s3d_scene_view_find_enclosure_batch_ctx` | 复用上下文的批量包壳 |

---

## 2. 内部 GPU 模块接口映射

### 2.1 Device 模块

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `cus3d_device_create(device_id, out)` | `oxs3d_device_create(device_id, out)` | 额外: optixInit + optixDeviceContextCreate |
| `cus3d_device_destroy(dev)` | `oxs3d_device_destroy(dev)` | 额外: optixDeviceContextDestroy |
| `cus3d_device_sync(dev)` | `oxs3d_device_sync(dev)` | 相同 |
| `cus3d_get_last_error()` | `oxs3d_get_last_error()` | 相同 |
| `struct cus3d_device` | `struct oxs3d_device` | 新增: `OptixDeviceContext optix_ctx` |

### 2.2 Memory 模块 (完全复用)

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `gpu_buffer_float3_alloc/free/upload/download` | 直接复用 | 无 |
| `gpu_buffer_float2_alloc/free/upload/download` | 直接复用 | 无 |
| `gpu_buffer_uint3_alloc/free/upload/download` | 直接复用 | 无 |
| `gpu_buffer_uint32_alloc/free/upload/download` | 直接复用 | 无 |
| `gpu_buffer_box3f_alloc/free/upload/download` | 直接复用 | 无 |

### 2.3 Types 模块

| cus3d 类型 | oxs3d 类型 | 差异 |
|-----------|-----------|------|
| `prim_type` (TRIANGLE/SPHERE) | 复用 | 无 |
| `cus3d_build_quality` | 复用 | 无 |
| `struct box3f` | 复用 | 无 |
| `struct sphere_gpu` | 复用 | 无 |
| `struct cus3d_hit_result` | 复用 | 无 |
| `struct cus3d_multi_hit_result` | 复用 | 无 |
| `struct geom_gpu_entry` | 复用 | 无 |
| `struct instance_gpu_data` | 复用 | 无 |
| — | `struct oxs3d_hit_group_data` | **新增**: SBT record 用户数据 |
| — | `struct oxs3d_launch_params` | **新增**: OptiX launch 参数 |

### 2.4 Geometry Store 模块

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `cus3d_geom_store_create(store)` | `oxs3d_geom_store_create(store)` | 结构体新增 `d_sphere_aabbs` |
| `cus3d_geom_store_destroy(store, dev)` | `oxs3d_geom_store_destroy(store, dev)` | 额外释放 d_sphere_aabbs |
| `cus3d_geom_store_sync(store, scene, dev)` | `oxs3d_geom_store_sync(store, scene, dev)` | 逻辑一致 |
| `cus3d_geom_store_compute_bounds(store, dev)` | `oxs3d_geom_store_compute_sphere_aabbs(store, dev)` | **简化**: 仅球体 AABB; 三角形由 OptiX 自动处理 |
| `cus3d_geom_store_lookup(store, primID)` | `oxs3d_geom_store_lookup(store, primID)` | 逻辑一致 |

### 2.5 BVH/Accel 模块 (变更最大)

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `cus3d_bvh_create(bvh)` | `oxs3d_accel_create(accel)` | 分配 OptiX 资源结构 |
| `cus3d_bvh_destroy(bvh, dev)` | `oxs3d_accel_destroy(accel, dev)` | cudaFree GAS/IAS buffer |
| `cus3d_bvh_build(bvh, store, dev, quality)` | `oxs3d_accel_build_gas(accel, store, dev, quality)` | cuBQL gpuBuilder → optixAccelBuild |
| `cus3d_bvh_build_instance(bvh, idx, store, dev)` | `oxs3d_accel_build_instance_gas(accel, idx, store, dev)` | 子 GAS 构建 |
| `cus3d_bvh_build_tlas(bvh, dev)` | `oxs3d_accel_build_ias(accel, dev)` | 手动 TLAS → OptiX IAS |
| `cus3d_bvh_set_instance_transform(...)` | `oxs3d_accel_set_instance_transform(...)` | 相同签名 |
| `cus3d_bvh_get_bounds(bvh, lower, upper)` | `oxs3d_accel_get_bounds(accel, lower, upper)` | 相同签名 |
| `cus3d_bvh_is_valid(bvh)` | `oxs3d_accel_is_valid(accel)` | 相同签名 |
| `cus3d_bvh_set_instance_geometry(...)` | `oxs3d_accel_set_instance_geometry(...)` | 相同签名 |
| `cus3d_bvh_get_instance_geometry(...)` | `oxs3d_accel_get_instance_geometry(...)` | 相同签名 |
| `cus3d_bvh_set_instance_child_store(...)` | `oxs3d_accel_set_instance_child_store(...)` | 相同签名 |
| `cus3d_bvh_get_instance_store(...)` | `oxs3d_accel_get_instance_store(...)` | 相同签名 |
| `cus3d_bvh_reset_instances(bvh, dev)` | `oxs3d_accel_reset_instances(accel, dev)` | 相同签名 |
| `cus3d_bvh_register_pseudo_instance(...)` | `oxs3d_accel_register_pseudo_instance(...)` | 相同签名 |

### 2.6 Trace 模块

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `cus3d_trace_ray_batch(bvh, store, dev, rays, results)` | `oxs3d_trace_ray_batch(accel, store, dev, pipe, rays, results)` | **新增 pipe 参数** |
| `cus3d_trace_ray_single(bvh, store, dev, ...)` | `oxs3d_trace_ray_single(accel, store, dev, pipe, ...)` | **新增 pipe 参数** |
| `cus3d_trace_ray_single_multi(bvh, store, dev, ..., max_hits, result)` | `oxs3d_trace_ray_single_multi(accel, store, dev, pipe, ..., max_hits, result)` | **新增 pipe 参数** |
| `cus3d_trace_ray_batch_multi(bvh, store, dev, rays, max_hits, results)` | `oxs3d_trace_ray_batch_multi(accel, store, dev, pipe, rays, max_hits, results)` | **新增 pipe 参数** |
| `cus3d_ray_batch_create(batch, max_rays)` | `oxs3d_ray_batch_create(batch, max_rays)` | 相同 |
| `cus3d_ray_batch_destroy(batch)` | `oxs3d_ray_batch_destroy(batch)` | 相同 |

> **注意**: oxs3d_trace 所有接口新增 `oxs3d_pipeline* pipe` 参数，因为 OptiX launch 需要 Pipeline + SBT。custar-3d 中这些不存在，因为 CUDA kernel 不需要额外的管线状态。

### 2.7 Closest Point 模块

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `cus3d_cp_batch_create(batch, max)` | `oxs3d_cp_batch_create(batch, max)` | 相同 |
| `cus3d_cp_batch_destroy(batch)` | `oxs3d_cp_batch_destroy(batch)` | 相同 |
| `cus3d_closest_point_batch(bvh, store, dev, queries, results)` | `oxs3d_closest_point_batch(accel, store, dev, queries, results)` | 内部使用 accel->cubql_bvh |

### 2.8 Find Enclosure 模块

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `cus3d_enc_batch_create(batch, max)` | `oxs3d_enc_batch_create(batch, max)` | 相同 |
| `cus3d_enc_batch_destroy(batch)` | `oxs3d_enc_batch_destroy(batch)` | 相同 |
| `cus3d_find_enclosure_batch(bvh, store, dev, queries, results)` | `oxs3d_find_enclosure_batch(accel, store, dev, pipe, queries, results)` | **新增 pipe 参数** (OptiX 6-ray) |

### 2.9 Prim 模块 (完全复用)

| cus3d 接口 | oxs3d 接口 | 差异 |
|-----------|-----------|------|
| `cus3d_hit_to_primitive(store, bvh, gpu_hit, prim)` | 复用 | 无 |
| `cus3d_hit_to_s3d_hit(store, bvh, gpu_hit, hit)` | 复用 | 无 |

### 2.10 Pipeline 模块 (新增)

| oxs3d 接口 | 说明 |
|-----------|------|
| `oxs3d_pipeline_create(pipe, dev)` | 编译 PTX, 创建 Module/ProgramGroup/Pipeline |
| `oxs3d_pipeline_destroy(pipe)` | 释放 Pipeline + SBT 资源 |
| `oxs3d_pipeline_build_sbt(pipe, store, dev)` | 构建 SBT (需在 GAS 构建后) |

---

## 3. 构建质量映射

| s3d Quality | Embree | cuBQL | OptiX |
|-------------|--------|-------|-------|
| `S3D_ACCEL_STRUCT_QUALITY_LOW` | `RTC_BUILD_QUALITY_LOW` | fast linear | `OPTIX_BUILD_FLAG_PREFER_FAST_BUILD` |
| `S3D_ACCEL_STRUCT_QUALITY_MEDIUM` | `RTC_BUILD_QUALITY_MEDIUM` | spatial median | `OPTIX_BUILD_FLAG_PREFER_FAST_TRACE` |
| `S3D_ACCEL_STRUCT_QUALITY_HIGH` | `RTC_BUILD_QUALITY_HIGH` | SAH | `PREFER_FAST_TRACE \| ALLOW_COMPACTION` |

| s3d Flag | OptiX 映射 |
|----------|-----------|
| `S3D_ACCEL_STRUCT_FLAG_ROBUST` | 无直接对应 (OptiX 默认 robust) |
| `S3D_ACCEL_STRUCT_FLAG_DYNAMIC` | `OPTIX_BUILD_FLAG_ALLOW_UPDATE` |
| `S3D_ACCEL_STRUCT_FLAG_COMPACT` | `OPTIX_BUILD_FLAG_ALLOW_COMPACTION` |

---

## 4. 数据流对比

### 4.1 射线追踪数据流

```
custar-3d:
  host rays → cudaMemcpy H2D → CUDA kernel (cuBQL traverse) → cudaMemcpy D2H → host hits

oxstar-3d:
  host rays → cudaMemcpy H2D → launch_params → optixLaunch (Pipeline) → cudaMemcpy D2H → host hits
             [相同]                              [替换部分]                [相同]
```

### 4.2 Scene Sync 数据流

```
custar-3d:
  shapes → flatten → GPU upload → compute_bounds (CUDA) → cuBQL::gpuBuilder → BVH ready

oxstar-3d:
  shapes → flatten → GPU upload → compute_sphere_aabbs → optixAccelBuild → GAS ready
                                                          ↘ (可选) cuBQL::gpuBuilder → cuBQL BVH (for CP)
           [相同]      [相同]       [简化: 仅球体]          [替换部分]
                                                           [新增: SBT build]
```

---

## 5. 内存共享分析

### 5.1 可共享的 GPU Buffer

以下 GPU buffer 在 OptiX GAS 和 cuBQL BVH 之间**可以共享**（同一份 device memory）:

| Buffer | 用途 | OptiX 使用 | cuBQL 使用 |
|--------|------|-----------|-----------|
| `d_vertices` (float3[]) | 三角形顶点 | GAS Build Input | closest_point kernel |
| `d_indices` (uint3[]) | 三角形索引 | GAS Build Input | closest_point kernel |
| `d_spheres` (sphere_gpu[]) | 球体数据 | Intersection Program | closest_point kernel |
| `d_prim_to_geom` (uint32[]) | 原始→几何映射 | SBT 或 launch params | closest_point kernel |
| `d_geom_entries` (geom_gpu_entry[]) | 几何元数据 | SBT record | closest_point kernel |

### 5.2 不可共享的 GPU Buffer

| Buffer | 拥有者 | 原因 |
|--------|-------|------|
| `d_gas_buffer` | OptiX | 加速结构内部格式，opaque |
| `d_ias_buffer` | OptiX | 实例加速结构 |
| `d_boxes` (box3f[]) | cuBQL | cuBQL BVH 构建输入 (所有原始的 AABB) |
| cuBQL BVH node buffer | cuBQL | BVH 节点结构 |
| `d_sphere_aabbs` (OptixAabb[]) | OptiX | 自定义原始 AABB (仅球体) |

### 5.3 内存开销对比

```
custar-3d 内存占用:
  顶点    + 索引    + 球体   + AABB(全部) + prim_to_geom + geom_entries + cuBQL BVH
  = 基本几何数据 + cuBQL overhead

oxstar-3d 内存占用 (仅 OptiX, 无 closest point):
  顶点    + 索引    + 球体   + sphere_AABB + prim_to_geom + geom_entries + OptiX GAS
  = 基本几何数据 + OptiX overhead
  (省去: 全部 AABB 数组, 因为三角形 AABB 由 OptiX 内部计算)

oxstar-3d 内存占用 (含 cuBQL closest point):
  上述 + 全部 AABB 数组 + cuBQL BVH
  = custar-3d 内存 + OptiX GAS overhead - cuBQL BVH (closest point 专用，可更小)
```

---

## 6. 关键接口差异总结

### 6.1 只有 oxstar-3d 需要的接口

| 接口/概念 | 原因 |
|----------|------|
| `oxs3d_pipeline` 全部接口 | OptiX 需要 Pipeline + SBT 管理 |
| `oxs3d_launch_params` 类型 | optixLaunch 的参数传递 |
| `oxs3d_hit_group_data` 类型 | SBT record 的用户数据 |
| `oxs3d_geom_store.d_sphere_aabbs` | OptiX Custom Primitive 需要显式 AABB |
| Trace 接口的 `pipe` 参数 | OptiX launch 需要 Pipeline |
| PTX 编译流程 | Device Programs 需预编译为 PTX |

### 6.2 custar-3d 有但 oxstar-3d 不需要的接口

| 接口/概念 | 原因 |
|----------|------|
| `cus3d_bvh_internal.h` (BVH 内部结构) | OptiX AS 是 opaque 的 |
| `cus3d_geom_store_compute_bounds` (全部 AABB) | 三角形 AABB 由 OptiX 自动计算 |
| CUDA kernel 的 `<<<grid, block>>>` 配置 | OptiX 由 optixLaunch 的 dimensions 控制 |
| `cuBQL::shrinkingRayQuery` (射线遍历) | 由 `optixTrace` 替代 |
| BLAS/TLAS 手动组装逻辑 | OptiX GAS/IAS 原生两级 |

---

*文档更新: 2026-02-20 | 关联: architecture.md*
