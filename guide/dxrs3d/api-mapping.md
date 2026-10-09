# dxrstar-3d API 映射表

**版本**: 0.1  
**日期**: 2026-02-20  
**前置文档**: [architecture.md](architecture.md), [module-design.md](module-design.md)

---

## 1. s3d.h 公共 API → 后端调用映射

下表列出 `s3d.h` 中所有公共 API 的三种后端实现对照。  
标记说明: ✅ = 直接复用 | 🔄 = 需改写 | ➕ = 新增

### 1.1 设备管理 (4 函数)

| s3d 公共 API | star-3d (CPU) | custar-3d (CUDA) | dxrstar-3d (DXR) | 改动 |
|-------------|--------------|-----------------|-----------------|------|
| `s3d_device_create()` | `rtcNewDevice()` | `cus3d_device_create()` | `dxrs3d_device_create()` | 🔄 |
| `s3d_device_ref_get()` | 引用计数++ | 引用计数++ | 引用计数++ | ✅ |
| `s3d_device_ref_put()` | → `cus3d_device_destroy()` | → `cus3d_device_destroy()` | → `dxrs3d_device_destroy()` | 🔄 |
| `s3d_device_get_gpu_sm_count()` | N/A | `dev->gpu->sm_count` | `dev->gpu->dedicated_video_memory / 估算` | 🔄 |

### 1.2 场景管理 (7 函数)

| s3d 公共 API | 后端调用 | 改动 |
|-------------|---------|------|
| `s3d_scene_create()` | 纯主机端，不涉及后端 | ✅ |
| `s3d_scene_ref_get()` | 引用计数 | ✅ |
| `s3d_scene_ref_put()` | 引用计数 | ✅ |
| `s3d_scene_instantiate()` | 纯主机端 | ✅ |
| `s3d_scene_attach_shape()` | 哈希表插入 | ✅ |
| `s3d_scene_detach_shape()` | 哈希表移除 | ✅ |
| `s3d_scene_clear()` | 清空形状列表 | ✅ |
| `s3d_scene_get_device()` | 返回 device 指针 | ✅ |
| `s3d_scene_get_shapes_count()` | 返回计数 | ✅ |

### 1.3 场景视图 — 核心查询 (12 函数)

| s3d 公共 API | custar-3d 内部调用链 | dxrstar-3d 内部调用链 | 改动 |
|-------------|-------------------|---------------------|------|
| `s3d_scene_view_create()` | `cus3d_geom_store_create()` + `cus3d_bvh_create()` + `scene_view_sync()` | `dxrs3d_geom_store_create()` + `dxrs3d_accel_create()` + `scene_view_sync()` | 🔄 |
| `s3d_scene_view_create2()` | 同上 + accel_struct_conf | 同上 + build_quality 映射 | 🔄 |
| `s3d_scene_view_ref_get()` | 引用计数 | 引用计数 | ✅ |
| `s3d_scene_view_ref_put()` | → destroy geom_store + bvh | → destroy geom_store + accel | 🔄 |
| `s3d_scene_view_get_mask()` | 返回 mask | 返回 mask | ✅ |
| `s3d_scene_view_trace_ray()` | `cus3d_trace_ray_single_multi()` + CPU filter | `dxrs3d_trace_ray_single_multi()` + CPU filter | 🔄 |
| `s3d_scene_view_trace_rays()` | 循环调用 trace_ray 或 batch | 循环调用 trace_ray 或 batch | 🔄 |
| `s3d_scene_view_closest_point()` | `cus3d_closest_point_batch(N=1)` | `dxrs3d_closest_point_batch(N=1)` | 🔄 |
| `s3d_scene_view_sample()` | CDF + primitive_sample (纯主机端) | CDF + primitive_sample (纯主机端) | ✅ |
| `s3d_scene_view_get_primitive()` | 主机端查找 | 主机端查找 | ✅ |
| `s3d_scene_view_primitives_count()` | 返回计数 | 返回计数 | ✅ |
| `s3d_scene_view_compute_area()` | 主机端累加 | 主机端累加 | ✅ |
| `s3d_scene_view_compute_volume()` | 主机端计算 | 主机端计算 | ✅ |
| `s3d_scene_view_get_aabb()` | `cus3d_bvh_get_bounds()` | `dxrs3d_accel_get_bounds()` | 🔄 |

### 1.4 批量射线追踪 API (6 函数)

| s3d 公共 API | custar-3d | dxrstar-3d | 改动 |
|-------------|----------|-----------|------|
| `s3d_scene_view_trace_rays_batch()` | SoA打包 → `cus3d_trace_ray_batch_multi()` → CPU filter | SoA打包 → `dxrs3d_trace_ray_batch_multi()` → CPU filter | 🔄 |
| `s3d_batch_trace_context_create()` | 预分配 GPU 缓冲 | 预分配 D3D12 缓冲 | 🔄 |
| `s3d_batch_trace_context_destroy()` | 释放 GPU 缓冲 | 释放 D3D12 缓冲 | 🔄 |
| `s3d_scene_view_trace_rays_batch_ctx()` | 复用上下文 → batch_multi | 复用上下文 → batch_multi | 🔄 |

### 1.5 批量最近点 API (6 函数)

| s3d 公共 API | custar-3d | dxrstar-3d | 改动 |
|-------------|----------|-----------|------|
| `s3d_scene_view_closest_point_batch()` | `cus3d_closest_point_batch()` | `dxrs3d_closest_point_batch()` | 🔄 |
| `s3d_batch_cp_context_create()` | 预分配 | 预分配 | 🔄 |
| `s3d_batch_cp_context_destroy()` | 释放 | 释放 | 🔄 |
| `s3d_scene_view_closest_point_batch_ctx()` | 复用上下文 | 复用上下文 | 🔄 |

### 1.6 批量包壳定位 API (6 函数)

| s3d 公共 API | custar-3d | dxrstar-3d | 改动 |
|-------------|----------|-----------|------|
| `s3d_scene_view_find_enclosure_batch()` | `cus3d_find_enclosure_batch()` | `dxrs3d_find_enclosure_batch()` | 🔄 |
| `s3d_batch_enc_context_create()` | 预分配 | 预分配 | 🔄 |
| `s3d_batch_enc_context_destroy()` | 释放 | 释放 | 🔄 |
| `s3d_scene_view_find_enclosure_batch_ctx()` | 复用上下文 | 复用上下文 | 🔄 |

### 1.7 形状管理 (6 函数) — 全部纯主机端

| s3d 公共 API | 改动 |
|-------------|------|
| `s3d_shape_ref_get()` | ✅ |
| `s3d_shape_ref_put()` | ✅ |
| `s3d_shape_get_id()` | ✅ |
| `s3d_shape_enable()` | ✅ |
| `s3d_shape_is_enabled()` | ✅ |
| `s3d_shape_flip_surface()` | ✅ |

### 1.8 图元操作 (6 函数) — 全部纯主机端

| s3d 公共 API | 改动 |
|-------------|------|
| `s3d_primitive_get_attrib()` | ✅ |
| `s3d_primitive_has_attrib()` | ✅ |
| `s3d_primitive_sample()` | ✅ |
| `s3d_primitive_compute_area()` | ✅ |
| `s3d_primitive_get_transform()` | ✅ |
| `s3d_triangle_get_vertex_attrib()` | ✅ |

### 1.9 球体 API (4 函数) — 全部纯主机端

| s3d 公共 API | 改动 |
|-------------|------|
| `s3d_shape_create_sphere()` | ✅ |
| `s3d_sphere_setup()` | ✅ |
| `s3d_sphere_set_hit_filter_function()` | ✅ |
| `s3d_sphere_get_hit_filter_data()` | ✅ |

### 1.10 网格 API (9 函数) — 全部纯主机端

| s3d 公共 API | 改动 |
|-------------|------|
| `s3d_shape_create_mesh()` | ✅ |
| `s3d_mesh_setup_indexed_vertices()` | ✅ |
| `s3d_mesh_copy()` | ✅ |
| `s3d_mesh_get_vertices_count()` | ✅ |
| `s3d_mesh_get_triangles_count()` | ✅ |
| `s3d_mesh_get_vertex_attrib()` | ✅ |
| `s3d_mesh_get_triangle_indices()` | ✅ |
| `s3d_mesh_set_hit_filter_function()` | ✅ |
| `s3d_mesh_get_hit_filter_data()` | ✅ |

### 1.11 实例 API (4 函数) — 全部纯主机端

| s3d 公共 API | 改动 |
|-------------|------|
| `s3d_instance_set_position()` | ✅ |
| `s3d_instance_translate()` | ✅ |
| `s3d_instance_set_transform()` | ✅ |
| `s3d_instance_transform()` | ✅ |

---

## 2. 改动统计

| 类别 | 总函数数 | ✅ 复用 | 🔄 改写 | 改写比例 |
|------|---------|---------|---------|---------|
| 设备管理 | 4 | 1 | 3 | 75% |
| 场景管理 | 8 | 8 | 0 | 0% |
| 场景视图 | 12 | 5 | 7 | 58% |
| 批量射线 | 4 | 0 | 4 | 100% |
| 批量最近点 | 4 | 0 | 4 | 100% |
| 批量包壳 | 4 | 0 | 4 | 100% |
| 形状管理 | 6 | 6 | 0 | 0% |
| 图元操作 | 6 | 6 | 0 | 0% |
| 球体 | 4 | 4 | 0 | 0% |
| 网格 | 9 | 9 | 0 | 0% |
| 实例 | 4 | 4 | 0 | 0% |
| **合计** | **~65** | **~43** | **~22** | **~34%** |

**结论**: 约 2/3 的公共 API 为纯主机端代码，可直接复用。只有约 1/3 的 API（设备、场景视图、批量操作）需要改写后端实现。

---

## 3. 内部 API 映射: cus3d_* → dxrs3d_*

### 3.1 设备模块

| cus3d 函数 | dxrs3d 函数 | 实现差异 |
|-----------|------------|---------|
| `cus3d_device_create(device_id, out)` | `dxrs3d_device_create(adapter_idx, out)` | CUDA init → D3D12 创建 |
| `cus3d_device_destroy(dev)` | `dxrs3d_device_destroy(dev)` | cudaDestroy → COM Release |
| `cus3d_device_sync(dev)` | `dxrs3d_device_sync(dev)` | cudaSync → Fence wait |
| `cus3d_get_last_error()` | `dxrs3d_get_last_error()` | TLS 错误缓冲 (一致) |

### 3.2 内存模块

| cus3d 函数模式 | dxrs3d 函数 | 差异 |
|--------------|------------|------|
| `gpu_buffer_float3_alloc(buf, N, s)` | `dxrs3d_buffer_create(dev, buf, N*12, DEFAULT)` | 类型化 → 字节大小 |
| `gpu_buffer_float3_free(buf, s)` | `dxrs3d_buffer_destroy(dev, buf)` | cudaFree → Release |
| `gpu_buffer_float3_upload(dst, src, N, s)` | `dxrs3d_buffer_upload(dev, dst, src, N*12)` | cudaMemcpy → 上传堆+Copy |
| `gpu_buffer_float3_download(dst, src, N, s)` | `dxrs3d_buffer_download(dev, dst, src, N*12)` | cudaMemcpy → 回读堆+Copy |
| `gpu_buffer_float3_resize(buf, N, s)` | `dxrs3d_buffer_resize(dev, buf, N*12)` | 重分配 |

### 3.3 几何存储模块

| cus3d 函数 | dxrs3d 函数 | 差异 |
|-----------|------------|------|
| `cus3d_geom_store_create(store)` | `dxrs3d_geom_store_create(store)` | 一致 |
| `cus3d_geom_store_destroy(store, dev)` | `dxrs3d_geom_store_destroy(store, dev)` | 一致 |
| `cus3d_geom_store_sync(store, scn, dev)` | `dxrs3d_geom_store_sync(store, scn, dev)` | 一致（平坦化逻辑不变） |
| `cus3d_geom_store_compute_bounds(store, dev)` | `dxrs3d_geom_store_compute_sphere_bounds(store, dev)` | 仅需球体 AABB |
| `cus3d_geom_store_lookup(store, primID)` | `dxrs3d_geom_store_lookup(store, primID)` | 一致 |
| `cus3d_geom_store_get_entry(store, idx)` | `dxrs3d_geom_store_get_entry(store, idx)` | 一致 |

### 3.4 加速结构模块

| cus3d_bvh 函数 | dxrs3d_accel 函数 | 差异 |
|---------------|------------------|------|
| `cus3d_bvh_create(bvh)` | `dxrs3d_accel_create(accel)` | 不需 cuBQL 初始化 |
| `cus3d_bvh_destroy(bvh, dev)` | `dxrs3d_accel_destroy(accel, dev)` | 释放 COM 资源 |
| `cus3d_bvh_build(bvh, store, dev, q)` | `dxrs3d_accel_build_blas(accel, store, dev, q)` | cuBQL → DXR Build 命令 |
| `cus3d_bvh_build_instance(bvh,idx,child,dev)` | `dxrs3d_accel_build_instance_blas(accel,idx,child,dev)` | 同上 |
| `cus3d_bvh_build_tlas(bvh, dev)` | `dxrs3d_accel_build_tlas(accel, dev)` | InstanceDesc 组装 |
| `cus3d_bvh_set_instance_transform(bvh,idx,f,i)` | `dxrs3d_accel_set_instance_transform(accel,idx,f,i)` | TLAS Instance Transform |
| `cus3d_bvh_get_bounds(bvh, lo, hi)` | `dxrs3d_accel_get_bounds(accel, lo, hi)` | 一致 |
| `cus3d_bvh_is_valid(bvh)` | `dxrs3d_accel_is_valid(accel)` | 一致 |
| `cus3d_bvh_register_pseudo_instance(bvh,idx,s)` | `dxrs3d_accel_register_pseudo_instance(accel,idx,s)` | 恒等变换 Instance |
| `cus3d_bvh_reset_instances(bvh, dev)` | `dxrs3d_accel_reset_instances(accel, dev)` | 一致 |

### 3.5 射线追踪模块

| cus3d_trace 函数 | dxrs3d_trace 函数 | 差异 |
|-----------------|------------------|------|
| `cus3d_trace_ray_batch(bvh,store,dev,rays,res)` | `dxrs3d_trace_ray_batch(accel,store,dev,rays,res)` | CUDA kernel → Compute Shader |
| `cus3d_trace_ray_batch_multi(bvh,store,dev,rays,k,res)` | `dxrs3d_trace_ray_batch_multi(accel,store,dev,rays,k,res)` | 同上 |
| `cus3d_trace_ray_single(bvh,store,dev,o,d,r,res)` | `dxrs3d_trace_ray_single(accel,store,dev,o,d,r,res)` | 同上 |
| `cus3d_trace_ray_single_multi(bvh,store,dev,o,d,r,k,res)` | `dxrs3d_trace_ray_single_multi(accel,store,dev,o,d,r,k,res)` | 同上 |
| `cus3d_ray_batch_create(batch, max)` | `dxrs3d_ray_batch_create(batch, max)` | cudaMalloc → D3D12 Buffer |
| `cus3d_ray_batch_destroy(batch)` | `dxrs3d_ray_batch_destroy(batch)` | cudaFree → Release |

### 3.6 最近点查询模块

| cus3d 函数 | dxrs3d 函数 | 差异 |
|-----------|------------|------|
| `cus3d_cp_batch_create(batch, max)` | `dxrs3d_cp_batch_create(batch, max)` | 类型替换 |
| `cus3d_cp_batch_destroy(batch)` | `dxrs3d_cp_batch_destroy(batch)` | 类型替换 |
| `cus3d_closest_point_batch(bvh,store,dev,q,res)` | `dxrs3d_closest_point_batch(accel,store,dev,q,res)` | cuBQL query → 多射线近似 |

### 3.7 包壳查询模块

| cus3d 函数 | dxrs3d 函数 | 差异 |
|-----------|------------|------|
| `cus3d_enc_batch_create(batch, max)` | `dxrs3d_enc_batch_create(batch, max)` | 类型替换 |
| `cus3d_enc_batch_destroy(batch)` | `dxrs3d_enc_batch_destroy(batch)` | 类型替换 |
| `cus3d_find_enclosure_batch(bvh,store,dev,q,res)` | `dxrs3d_find_enclosure_batch(accel,store,dev,q,res)` | CUDA kernel → Compute+InlineRT |

### 3.8 命中转换模块

| cus3d 函数 | dxrs3d 函数 | 差异 |
|-----------|------------|------|
| `cus3d_hit_to_primitive(store,bvh,hit,prim)` | `dxrs3d_hit_to_primitive(store,accel,hit,prim)` | 类型替换 |
| `cus3d_hit_to_s3d_hit(store,bvh,hit,s3d_hit)` | `dxrs3d_hit_to_s3d_hit(store,accel,hit,s3d_hit)` | 类型替换 |
| `trace_hit_fixup()` | `trace_hit_fixup_dxr()` | UV 坐标转换差异 |

---

## 4. 枚举/常量映射

### 4.1 构建质量

| s3d 枚举 | custar-3d (cuBQL) | dxrstar-3d (DXR) |
|---------|-------------------|------------------|
| `S3D_ACCEL_STRUCT_QUALITY_LOW` | `cuBQL::BuildConfig` 快速 | `PREFER_FAST_BUILD` |
| `S3D_ACCEL_STRUCT_QUALITY_MEDIUM` | Spatial Median (默认) | `BUILD_FLAG_NONE` (默认) |
| `S3D_ACCEL_STRUCT_QUALITY_HIGH` | SAH-based | `PREFER_FAST_TRACE` |

### 4.2 加速结构标志

| s3d 标志 | custar-3d | dxrstar-3d |
|---------|----------|-----------|
| `S3D_ACCEL_STRUCT_FLAG_ROBUST` | 无特殊处理 | 无特殊处理 |
| `S3D_ACCEL_STRUCT_FLAG_DYNAMIC` | 不支持 (总是重建) | `ALLOW_UPDATE` |
| `S3D_ACCEL_STRUCT_FLAG_COMPACT` | 不支持 | `ALLOW_COMPACTION` |

---

## 5. s3d_device_c.h 内部结构变更

### 5.1 custar-3d 版本

```c
struct s3d_device {
    /* ... 公共字段 (不变) ... */
    struct cus3d_device* gpu;   /* CUDA 设备 */
};
```

### 5.2 dxrstar-3d 版本

```c
struct s3d_device {
    /* ... 公共字段 (不变) ... */
    struct dxrs3d_device* gpu;  /* DXR 设备 */
};
```

---

## 6. s3d_scene_view_c.h 内部结构变更

### 6.1 custar-3d 版本

```c
struct s3d_scene_view {
    /* ... 公共字段 (不变) ... */
    struct cus3d_geom_store* geom_store;
    struct cus3d_bvh*        bvh;
    int                      build_quality;
    int                      gpu_dirty;
};
```

### 6.2 dxrstar-3d 版本

```c
struct s3d_scene_view {
    /* ... 公共字段 (不变) ... */
    struct dxrs3d_geom_store* geom_store;
    struct dxrs3d_accel*      accel;
    int                       build_quality;
    int                       gpu_dirty;
};
```

---

## 7. HLSL 着色器清单

| Shader 文件 | 入口函数 | 用途 | 使用 Inline RT |
|------------|---------|------|---------------|
| `trace_rays.hlsl` | `TraceRaysCS` | 批量单次最近命中 | ✅ |
| `trace_rays_topk.hlsl` | `TraceRaysTopKCS` | 批量 Top-K 多命中 | ✅ |
| `closest_point.hlsl` | `ClosestPointCS` | 批量最近点查询 | ✅ (多射线近似) |
| `find_enclosure.hlsl` | `FindEnclosureCS` | 批量包壳定位 | ✅ |
| `compute_bounds.hlsl` | `ComputeBoundsCS` | 球体 AABB 计算 | ❌ (纯 Compute) |
| `sphere_intersection.hlsli` | — (内联) | 球体交叉测试函数 | — |
| `math_utils.hlsli` | — (内联) | 向量/矩阵数学 | — |
| `common.hlsli` | — (内联) | 公共结构体/常量定义 | — |

---

## 8. 场景视图同步流水线对照

### 8.1 custar-3d (`scene_view_setup_cubql`)

```
1. cus3d_geom_store_create()
2. cus3d_bvh_create()
3. if (!gpu_dirty && bvh_valid) → skip
4. cus3d_geom_store_sync()           ← 平坦化
5. cus3d_geom_store_compute_bounds() ← GPU kernel
6. cus3d_bvh_build()                 ← cuBQL gpuBuilder
7. for each instance:
   a. recursive sync/build
   b. cus3d_bvh_build_instance()
   c. cus3d_bvh_set_instance_transform()
   d. cus3d_bvh_set_instance_geometry()
8. if (mixed) → cus3d_bvh_register_pseudo_instance()
9. if (has instances) → cus3d_bvh_build_tlas()
10. gpu_dirty = 0
```

### 8.2 dxrstar-3d (`scene_view_setup_dxr`)

```
1. dxrs3d_geom_store_create()
2. dxrs3d_accel_create()
3. if (!gpu_dirty && accel_valid) → skip
4. dxrs3d_geom_store_sync()                 ← 平坦化 (与custar-3d一致)
5. dxrs3d_geom_store_compute_sphere_bounds() ← 仅球体 AABB
6. dxrs3d_accel_build_blas()                 ← DXR BuildAS (BLAS)
7. for each instance:
   a. recursive sync/build
   b. dxrs3d_accel_build_instance_blas()     ← DXR BuildAS (子BLAS)
   c. dxrs3d_accel_set_instance_transform()  ← InstanceDesc.Transform
   d. dxrs3d_accel_set_instance_geometry()
8. if (mixed) → dxrs3d_accel_register_pseudo_instance()  ← 恒等变换 Instance
9. if (has instances) → dxrs3d_accel_build_tlas()         ← DXR BuildAS (TLAS)
10. gpu_dirty = 0
```

**差异分析**: 流程完全对称，仅步骤 5 有细微差异（DXR 三角形 AABB 由驱动自动计算）。

---

*下一步: 参阅 [implementation-roadmap.md](implementation-roadmap.md) 获取分阶段实施计划*
