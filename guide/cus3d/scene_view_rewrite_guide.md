# s3d_scene_view 重写详细指南

**日期**: 2026-02-07
**基于审查**: custar-3d/0.10/src/ 全部现有代码
**目标**: 将 s3d_scene_view.cpp、s3d_scene_view_trace_ray.cpp、s3d_scene_view_closest_point.cpp 从 Embree 后端迁移到 cuBQL 后端

---

## 1. 现状审计

### 1.1 已完成的 cus3d 模块

| 模块 | 文件 | 状态 | 说明 |
|------|------|------|------|
| **cus3d_device** | `.h` + `.cpp` | ✅ 完成 | CUDA 设备管理、双流（compute + transfer） |
| **cus3d_mem** | `.h` + `.cpp` | ✅ 完成 | 5 种类型化 GPU buffer（float3/float2/uint3/uint32/box3f） |
| **cus3d_geom_store** | `.h` + `.cpp` + `.cu` | ✅ 完成 | 几何扁平化、AABB kernel、shape 遍历 |
| **cus3d_bvh** | `.h` + `.cu` + `_internal.h` | ✅ 完成 | cuBQL BVH 构建/销毁、TLAS、bounds 读取 |
| **cus3d_trace** | `.h` + `.cu` | ✅ 完成 | 单级/两级 trace kernel、单射线/批量接口 |
| **cus3d_prim** | `.h` + `.cpp` | ✅ 完成 | GPU hit → s3d_primitive/s3d_hit 转换 |
| **cus3d_types** | `.h` | ✅ 完成 | 共享类型定义（box3f, sphere_gpu, hit_result 等） |
| **cus3d_math** | `.cuh` + `.cu` | ✅ 完成 | 设备端数学函数（三角形/球体求交） |

### 1.2 已迁移的 s3d 模块

| 模块 | 状态 | 关键变化 |
|------|------|----------|
| **s3d_device_c.h** | ✅ 完成 | `RTCDevice rtc` → `cus3d_device* gpu` |
| **s3d_device.cpp** | ✅ 完成 | 使用 `cus3d_device_create/destroy` |
| **s3d_scene_view_c.h** | ✅ 完成 | 结构体已添加 `geom_store*`, `bvh*`, `build_quality`, `gpu_dirty` 字段 |
| **s3d_geometry.h** | ⚠️ 部分 | `struct geometry` 已移除 `rtc`, `rtc_id`, `rtc_build_quality`, `embree_outdated_mask` 等 Embree 字段 |
| **s3d_geometry.cpp** | ❌ 未迁移 | 仍包含 `geometry_rtc_sphere_bounds`, `geometry_rtc_sphere_intersect`, `sphere_ray_hit_setup` 等大量 Embree 代码 |

### 1.3 需要重写的文件（场景视图三件套）

| 文件 | 行数 | Embree 依赖程度 | 重写工作量 |
|------|------|-----------------|-----------|
| **s3d_scene_view.cpp** | 1577 | 🔴 重度 | **大**（核心编排逻辑） |
| **s3d_scene_view_trace_ray.cpp** | ~300 | 🔴 完全依赖 | **中**（替换为 cus3d_trace 调用） |
| **s3d_scene_view_closest_point.cpp** | 472 | 🔴 完全依赖 | **中**（替换 rtcPointQuery） |
| **s3d_geometry.cpp** | 233 | 🔴 重度 | **小**（移除 Embree sphere 回调） |

---

## 2. 架构差异分析

### 2.1 Embree 版本的 scene_view 工作流程

```
scene_view_create → scene_view_sync:
  ├── 遍历 scene->shapes
  │   ├── MESH → scene_view_register_mesh
  │   │   ├── geometry_create → 创建 struct geometry
  │   │   ├── 复制 index/vertex buffer 引用（ref counting）
  │   │   ├── 设置 embree_outdated_mask（VERTICES/INDICES/ENABLE/FILTER）
  │   │   └── 存入 htable_geom（以 shape_id 为 key）
  │   ├── SPHERE → scene_view_register_sphere
  │   │   ├── geometry_create → 创建 struct geometry
  │   │   ├── 复制 sphere 参数 (radius, pos)
  │   │   └── 存入 htable_geom
  │   └── INSTANCE → scene_view_register_instance
  │       ├── 递归创建子 scene_view
  │       ├── geometry_create → 创建 struct geometry(INSTANCE)
  │       ├── 复制 transform
  │       └── 存入 htable_geom
  ├── scene_view_setup_embree（如果 mask & S3D_TRACE）
  │   ├── 遍历 cached_geoms
  │   ├── embree_geometry_register（创建/更新 RTCGeometry）
  │   ├── embree_geometry_setup_positions（rtcNewSharedBuffer + rtcSetGeometryBuffer）
  │   ├── embree_geometry_setup_indices（同上）
  │   ├── embree_geometry_setup_enable_state（rtcEnable/DisableGeometry）
  │   ├── embree_geometry_setup_filter_function（rtcSetGeometryIntersectFilterFunction）
  │   ├── embree_geometry_setup_transform（rtcSetGeometryTransform，仅 instance）
  │   ├── rtcCommitGeometry（每个已修改的 geometry）
  │   └── rtcCommitScene（如果有任何更新）
  ├── scene_view_compute_cdf（如果 mask & S3D_SAMPLE）
  ├── scene_view_compute_nprims_cdf（总是）
  ├── scene_view_compute_scene_aabb（如果 aabb_update）
  │   ├── 无 S3D_TRACE → 手动遍历 geometry 计算
  │   └── 有 S3D_TRACE → rtcGetSceneBounds
  └── scnview->mask = mask
```

### 2.2 cuBQL 版本的目标工作流程

```
scene_view_create → scene_view_sync:
  ├── 遍历 scene->shapes（与 Embree 版本完全相同）
  │   ├── MESH → scene_view_register_mesh_cu    ★ 简化版
  │   ├── SPHERE → scene_view_register_sphere_cu ★ 简化版
  │   └── INSTANCE → scene_view_register_instance_cu ★ 简化版
  ├── scene_view_setup_cubql（如果 mask & S3D_TRACE）★ 新函数，替换 setup_embree
  │   ├── cus3d_geom_store_sync（扁平化所有几何体 → GPU）
  │   ├── cus3d_geom_store_compute_bounds（GPU kernel 计算 AABB）
  │   ├── cus3d_bvh_build（cuBQL GPU builder）
  │   └── 如果有 instance：
  │       ├── 为每个 instance 子 scene 构建 child BVH
  │       └── cus3d_bvh_build_tlas
  ├── scene_view_compute_cdf（如果 mask & S3D_SAMPLE）→ 不变
  ├── scene_view_compute_nprims_cdf → 不变
  ├── scene_view_compute_scene_aabb
  │   ├── 无 S3D_TRACE → 手动遍历（不变）
  │   └── 有 S3D_TRACE → cus3d_bvh_get_bounds ★ 替换 rtcGetSceneBounds
  └── scnview->mask = mask
```

### 2.3 关键差异总结

| 方面 | Embree 版本 | cuBQL 版本 |
|------|-------------|-----------|
| **几何缓存** | `htable_geom` 中每个 shape 对应一个 `struct geometry`（含 RTCGeometry） | `htable_geom` 仍然存在（用于 CDF/sampling/primitive 查询），但 geometry 不再持有 RTCGeometry |
| **增量更新** | `embree_outdated_mask` 位掩码追踪各属性变化 | `geom_store->needs_rebuild` 整体脏标记（初始阶段不做增量更新） |
| **BVH 构建** | `rtcCommitScene` 隐式处理 | 显式 `cus3d_geom_store_sync` + `cus3d_bvh_build` |
| **射线追踪** | `rtcIntersect1` 单射线 CPU 调用 | `cus3d_trace_ray_single` 启动 GPU kernel |
| **过滤函数** | `rtcSetGeometryIntersectFilterFunction` 回调 | 主机端后过滤（trace → check filter → re-trace if rejected） |
| **最近点查询** | `rtcPointQuery` 内置 | 需要自实现 GPU closest-point kernel 或退化为主机端暴力搜索 |

---

## 3. `struct geometry` 的角色变化

### 3.1 当前状态问题

`s3d_geometry.h` 中的 `struct geometry` 已经移除了 Embree 字段，但 `s3d_geometry.cpp` 仍然包含大量 Embree 回调代码：
- `geometry_rtc_sphere_bounds()` - Embree sphere bounds 回调
- `geometry_rtc_sphere_intersect()` - Embree sphere 求交回调
- `sphere_ray_hit_setup()` - Embree hit 结果设置

### 3.2 cuBQL 版本中 `struct geometry` 的保留价值

`struct geometry` 在 cuBQL 版本中 **仍然需要保留**，因为它不仅是 Embree 的包装器，更是 scene_view 内部的缓存和查找结构：

1. **CDF 计算** (`scene_view_compute_cdf`): 遍历 `htable_geom` 中的 geometry，计算面积 CDF
2. **采样** (`s3d_scene_view_sample`): 通过 `htable_geom` 查找 geometry，执行采样
3. **primitives_count** / **get_primitive**: 遍历 geometry 计数/查找
4. **primitive 属性查询** (`s3d_primitive_get_attrib`): `prim->shape__` 指向 `struct geometry`
5. **AABB 计算**: 遍历 geometry 计算场景包围盒（非 S3D_TRACE 路径）
6. **体积计算**: 遍历 geometry 计算体积
7. **面积计算**: 遍历 geometry 计算面积
8. **shape detach 处理**: 通过 `htable_geom` 管理几何体生命周期

**结论**: `struct geometry` 保留，但清除所有 Embree 特定代码。

### 3.3 所需变更

**s3d_geometry.h** — 当前已经是清理后的状态（无 RTC 字段），**无需修改**。

**s3d_geometry.cpp** — 需要大幅清理：
- ❌ 删除 `sphere_ray_hit_setup()`（整个函数）
- ❌ 删除 `geometry_rtc_sphere_bounds()`（整个函数）
- ❌ 删除 `geometry_rtc_sphere_intersect()`（整个函数）
- ❌ 删除 `rtc_hit_filter_wrapper` 的前向声明
- ❌ 删除对 `RTC_INVALID_GEOMETRY_ID`, `RTC_MAX_INSTANCE_LEVEL_COUNT` 等的引用
- ✅ 保留 `geometry_create()`（移除 rtc/rtc_id/rtc_build_quality 初始化）
- ✅ 保留 `geometry_release()`
- ✅ 保留 `geometry_ref_get()` / `geometry_ref_put()`

---

## 4. 分步重写计划

### Phase 1: 清理 s3d_geometry.cpp（预备工作）

**目标**: 从 `struct geometry` 和 `s3d_geometry.cpp` 中移除所有 Embree 残留。

**操作清单**:

1. **s3d_geometry.h**: 确认无 Embree 引用（✅ 已完成）
2. **s3d_geometry.cpp**:
   - 删除 `#include "s3d_scene_view_c.h"`（仅因 `rtc_hit_filter_wrapper` 声明需要）
   - 删除 `sphere_ray_hit_setup()` 函数（~40行）
   - 删除 `geometry_rtc_sphere_bounds()` 函数（~15行）
   - 删除 `geometry_rtc_sphere_intersect()` 函数（~40行）
   - 删除 `rtc_hit_filter_wrapper` 外部声明
   - 在 `geometry_create()` 中删除 `geom->rtc = NULL`、`geom->rtc_id = RTC_INVALID_GEOMETRY_ID`、`geom->rtc_build_quality = RTC_BUILD_QUALITY_MEDIUM`
   - 删除所有 `#include <embree4/...>` 如果有的话

**预期结果**: `s3d_geometry.cpp` 缩减到约 60-80 行，仅含创建/销毁/引用计数。

---

### Phase 2: 重写 s3d_scene_view.cpp

这是最大的工作量。按功能区块逐步替换。

#### Phase 2.1: 删除 Embree 辅助函数

以下函数 **整体删除**，由新的 cuBQL 流程替代：

| 函数 | 行范围(大约) | 替代方案 |
|------|-------------|----------|
| `scene_view_destroy_geometry()` | ~5行 | 新版本不需要 detach RTCGeometry |
| `accel_struct_quality_to_rtc_build_quality()` | ~15行 | 使用 `cus3d_build_quality` 枚举 |
| `accel_struct_mask_to_rtc_scene_flags()` | ~10行 | 不再需要（cuBQL 无对应概念） |
| `embree_geometry_register()` | ~60行 | 由 `cus3d_geom_store_sync` 替代 |
| `embree_geometry_setup_positions()` | ~20行 | 由 `cus3d_geom_store_sync` 替代 |
| `embree_geometry_setup_indices()` | ~20行 | 由 `cus3d_geom_store_sync` 替代 |
| `embree_geometry_setup_enable_state()` | ~8行 | 由 `geom_entry.is_enabled` 替代 |
| `embree_geometry_setup_filter_function()` | ~10行 | 由 `geom_entry.filter_func` 替代 |
| `embree_geometry_setup_transform()` | ~8行 | 由 instance BVH transform 替代 |
| `scene_view_setup_embree()` | ~60行 | 由 `scene_view_setup_cubql()` 替代 |

#### Phase 2.2: 简化 shape 注册函数

Embree 版本的 `scene_view_register_mesh/sphere/instance` 做了很多 Embree 相关的事情（设置 `embree_outdated_mask`，管理 RTCGeometry 生命周期）。cuBQL 版本大幅简化，因为 **GPU 几何数据由 `cus3d_geom_store` 统一管理**，geometry 只是主机端缓存。

##### scene_view_register_mesh_cu（替代 scene_view_register_mesh）

```cpp
static res_T
scene_view_register_mesh_cu
  (struct s3d_scene_view* scnview,
   struct s3d_shape* shape)
{
  struct geometry** pgeom = NULL;
  struct geometry* geom = NULL;
  size_t iattr;
  unsigned shape_id;
  int is_valid;
  res_T res = RES_OK;
  ASSERT(scnview && shape && shape->type == GEOM_MESH);

  is_valid = shape->data.mesh->indices
          && shape->data.mesh->attribs[S3D_POSITION];

  S3D(shape_get_id(shape, &shape_id));
  pgeom = htable_geom_find(&scnview->cached_geoms, &shape_id);

  if(pgeom) {
    geom = *pgeom;
    if(!is_valid) {
      /* Mesh became invalid, remove from cache */
      geometry_ref_put(geom);
      htable_geom_erase(&scnview->cached_geoms, &shape_id);
      scnview->gpu_dirty = 1;
      scnview->aabb_update = 1;
      goto exit;
    }
  } else if(is_valid) {
    /* New mesh, create geometry cache entry */
    res = geometry_create(scnview->scn->dev, &geom);
    if(res != RES_OK) goto error;
    res = mesh_create(scnview->scn->dev, &geom->data.mesh);
    if(res != RES_OK) goto error;
    geom->type = GEOM_MESH;
    res = htable_geom_set(&scnview->cached_geoms, &shape_id, &geom);
    if(res != RES_OK) goto error;
    geom->name = shape->id.index;
    scnview->gpu_dirty = 1;
  }

  if(!is_valid) goto exit;

  /* Sync index buffer reference */
  if(geom->data.mesh->indices != shape->data.mesh->indices) {
    if(geom->data.mesh->indices)
      index_buffer_ref_put(geom->data.mesh->indices);
    index_buffer_ref_get(shape->data.mesh->indices);
    geom->data.mesh->indices = shape->data.mesh->indices;
    scnview->gpu_dirty = 1;
  }

  /* Sync vertex attribute buffer references */
  FOR_EACH(iattr, 0, S3D_ATTRIBS_COUNT__) {
    if(geom->data.mesh->attribs[iattr] == shape->data.mesh->attribs[iattr])
      continue;

    if(geom->data.mesh->attribs[iattr])
      vertex_buffer_ref_put(geom->data.mesh->attribs[iattr]);

    if(!shape->data.mesh->attribs[iattr]) {
      geom->data.mesh->attribs[iattr] = NULL;
      continue;
    }

    vertex_buffer_ref_get(shape->data.mesh->attribs[iattr]);
    geom->data.mesh->attribs[iattr] = shape->data.mesh->attribs[iattr];
    geom->data.mesh->attribs_type[iattr] = shape->data.mesh->attribs_type[iattr];

    if(iattr == S3D_POSITION)
      scnview->gpu_dirty = 1;
  }

  /* Sync enable state */
  if(geom->is_enabled != shape->is_enabled) {
    geom->is_enabled = shape->is_enabled;
    scnview->gpu_dirty = 1;
    scnview->aabb_update = 1;
  }

  /* Sync filter function */
  geom->data.mesh->filter = shape->data.mesh->filter;

  /* Sync flip_surface */
  geom->flip_surface = shape->flip_surface;

exit:
  return res;
error:
  goto exit;
}
```

**与原版差异**:
- 不设置 `embree_outdated_mask`，改为设置 `scnview->gpu_dirty = 1`
- 不调用任何 `rtc*` 函数
- 仅同步引用（用于主机端 CDF/sampling/primitive 查询）

##### scene_view_register_sphere_cu（替代 scene_view_register_sphere）

```cpp
static res_T
scene_view_register_sphere_cu
  (struct s3d_scene_view* scnview,
   struct s3d_shape* shape)
{
  struct geometry** pgeom = NULL;
  struct geometry* geom = NULL;
  unsigned shape_id;
  int is_valid;
  res_T res = RES_OK;
  ASSERT(scnview && shape && shape->type == GEOM_SPHERE);

  is_valid = !sphere_is_degenerated(shape->data.sphere);

  S3D(shape_get_id(shape, &shape_id));
  pgeom = htable_geom_find(&scnview->cached_geoms, &shape_id);

  if(pgeom) {
    geom = *pgeom;
    if(!is_valid) {
      geometry_ref_put(geom);
      htable_geom_erase(&scnview->cached_geoms, &shape_id);
      scnview->gpu_dirty = 1;
      scnview->aabb_update = 1;
      goto exit;
    }
  } else if(is_valid) {
    res = geometry_create(scnview->scn->dev, &geom);
    if(res != RES_OK) goto error;
    res = sphere_create(scnview->scn->dev, &geom->data.sphere);
    if(res != RES_OK) goto error;
    geom->type = GEOM_SPHERE;
    res = htable_geom_set(&scnview->cached_geoms, &shape_id, &geom);
    if(res != RES_OK) goto error;
    geom->name = shape->id.index;
    scnview->gpu_dirty = 1;
  }

  if(!is_valid) goto exit;

  /* Sync sphere parameters */
  if(geom->data.sphere->radius != shape->data.sphere->radius) {
    geom->data.sphere->radius = shape->data.sphere->radius;
    scnview->gpu_dirty = 1;
  }
  if(!f3_eq(geom->data.sphere->pos, shape->data.sphere->pos)) {
    f3_set(geom->data.sphere->pos, shape->data.sphere->pos);
    scnview->gpu_dirty = 1;
  }

  if(geom->is_enabled != shape->is_enabled) {
    geom->is_enabled = shape->is_enabled;
    scnview->gpu_dirty = 1;
    scnview->aabb_update = 1;
  }

  geom->data.sphere->filter = shape->data.sphere->filter;
  geom->flip_surface = shape->flip_surface;

exit:
  return res;
error:
  goto exit;
}
```

##### scene_view_register_instance_cu（替代 scene_view_register_instance）

```cpp
static res_T
scene_view_register_instance_cu
  (struct s3d_scene_view* scnview,
   struct s3d_shape* shape,
   const int mask)
{
  struct geometry** pgeom = NULL;
  struct geometry* geom = NULL;
  struct s3d_scene_view** pview = NULL;
  struct s3d_scene_view* view = NULL;
  unsigned shape_id;
  res_T res = RES_OK;
  ASSERT(scnview && shape && shape->type == GEOM_INSTANCE);

  /* Only one level of instancing supported */
  if(shape->data.instance->scene->instances_count != 0) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Recursively create scnview on the scene to instantiate */
  pview = htable_instview_find
    (&scnview->instviews, &shape->data.instance->scene);
  if(pview) {
    view = *pview;
  } else {
    res = s3d_scene_view_create(shape->data.instance->scene, mask, &view);
    if(res != RES_OK) goto error;
    res = htable_instview_set
      (&scnview->instviews, &shape->data.instance->scene, &view);
    if(res != RES_OK) goto error;
  }

  S3D(shape_get_id(shape, &shape_id));
  pgeom = htable_geom_find(&scnview->cached_geoms, &shape_id);
  if(pgeom) {
    geom = *pgeom;
  } else {
    res = geometry_create(scnview->scn->dev, &geom);
    if(res != RES_OK) goto error;
    geom->type = GEOM_INSTANCE;
    res = instance_create(shape->data.instance->scene, &geom->data.instance);
    if(res != RES_OK) goto error;
    res = htable_geom_set(&scnview->cached_geoms, &shape_id, &geom);
    if(res != RES_OK) goto error;
    geom->name = shape->id.index;
    scnview->gpu_dirty = 1;
  }
  ASSERT(geom->data.instance->scene == shape->data.instance->scene);
  geom->data.instance->scnview = view;

  /* Sync transform */
  if(!f33_eq(shape->data.instance->transform, geom->data.instance->transform)
  || !f3_eq(shape->data.instance->transform+9, geom->data.instance->transform+9)) {
    f33_set(geom->data.instance->transform, shape->data.instance->transform);
    f3_set(geom->data.instance->transform+9, shape->data.instance->transform+9);
    scnview->gpu_dirty = 1;
    scnview->aabb_update = 1;
  }

  if(geom->is_enabled != shape->is_enabled) {
    geom->is_enabled = shape->is_enabled;
    scnview->gpu_dirty = 1;
    scnview->aabb_update = 1;
  }

  geom->flip_surface = shape->flip_surface;

exit:
  return res;
error:
  goto exit;
}
```

#### Phase 2.3: 新增 scene_view_setup_cubql（替代 scene_view_setup_embree）

```cpp
static res_T
scene_view_setup_cubql
  (struct s3d_scene_view* scnview,
   const struct s3d_accel_struct_conf* accel_struct_conf)
{
  cus3d_build_quality quality;
  res_T res = RES_OK;
  ASSERT(scnview);

  /* Map accel struct quality to cuBQL quality */
  switch(accel_struct_conf->quality) {
    case S3D_ACCEL_STRUCT_QUALITY_LOW:    quality = CUS3D_BUILD_LOW;    break;
    case S3D_ACCEL_STRUCT_QUALITY_HIGH:   quality = CUS3D_BUILD_HIGH;   break;
    case S3D_ACCEL_STRUCT_QUALITY_MEDIUM:
    default:                              quality = CUS3D_BUILD_MEDIUM; break;
  }

  /* Create geom_store and BVH if not yet created */
  if(!scnview->geom_store) {
    res = cus3d_geom_store_create(&scnview->geom_store);
    if(res != RES_OK) goto error;
  }
  if(!scnview->bvh) {
    res = cus3d_bvh_create(&scnview->bvh);
    if(res != RES_OK) goto error;
  }

  /* Only rebuild if dirty */
  if(!scnview->gpu_dirty && cus3d_bvh_is_valid(scnview->bvh))
    goto exit;

  /* Step 1: Flatten all scene geometry into GPU arrays */
  res = cus3d_geom_store_sync(scnview->geom_store,
                               scnview->scn,
                               scnview->scn->dev->gpu);
  if(res != RES_OK) goto error;

  /* Step 2: Compute per-primitive bounding boxes on GPU */
  res = cus3d_geom_store_compute_bounds(scnview->geom_store,
                                         scnview->scn->dev->gpu);
  if(res != RES_OK) goto error;

  /* Step 3: Build BVH from bounding boxes */
  res = cus3d_bvh_build(scnview->bvh,
                         scnview->geom_store,
                         scnview->scn->dev->gpu,
                         quality);
  if(res != RES_OK) goto error;

  /* TODO: Instance support
   * For each instance in the scene:
   *   1. Get the child scene_view's geom_store
   *   2. Build child BVH (cus3d_bvh_build_instance)
   *   3. Compute transformed AABB for TLAS
   * Then build top-level AS (cus3d_bvh_build_tlas)
   */

  scnview->gpu_dirty = 0;
  scnview->build_quality = quality;

exit:
  return res;
error:
  goto exit;
}
```

#### Phase 2.4: 修改 scene_view_sync

```cpp
static res_T
scene_view_sync
  (struct s3d_scene_view* scnview,
   const int mask,
   const struct s3d_accel_struct_conf* accel_struct_conf)
{
  struct htable_shape_iterator it, end;
  res_T res = RES_OK;

  ASSERT(scnview && accel_struct_conf);

  /* Commit the scene shapes to the scnview */
  htable_shape_begin(&scnview->scn->shapes, &it);
  htable_shape_end(&scnview->scn->shapes, &end);
  while(!htable_shape_iterator_eq(&it, &end)) {
    struct s3d_shape** pshape = htable_shape_iterator_data_get(&it);
    struct s3d_shape* shape = *pshape;

    switch(shape->type) {
      case GEOM_INSTANCE:
        res = scene_view_register_instance_cu(scnview, shape, mask); /* ★ CU版 */
        break;
      case GEOM_MESH:
        res = scene_view_register_mesh_cu(scnview, shape);           /* ★ CU版 */
        break;
      case GEOM_SPHERE:
        res = scene_view_register_sphere_cu(scnview, shape);         /* ★ CU版 */
        break;
      default: FATAL("Unreachable code\n"); break;
    }
    if(res != RES_OK) goto error;
    htable_shape_iterator_next(&it);
  }

  /* ★ 替换: scene_view_setup_embree → scene_view_setup_cubql */
  if((mask & S3D_TRACE) != 0) {
    res = scene_view_setup_cubql(scnview, accel_struct_conf);
    if(res != RES_OK) goto error;
  }

  /* CDF 计算 — 不变 */
  if((mask & S3D_SAMPLE) != 0) {
    res = scene_view_compute_cdf(scnview);
    if(res != RES_OK) goto error;
  }

  /* AABB 计算 */
  if(scnview->aabb_update) {
    if((mask & S3D_TRACE) == 0) {
      /* 手动计算 — 不变 */
      f3_splat(scnview->lower, FLT_MAX);
      f3_splat(scnview->upper,-FLT_MAX);
      scene_view_compute_scene_aabb(scnview);
    } else {
      /* ★ 替换: rtcGetSceneBounds → cus3d_bvh_get_bounds */
      cus3d_bvh_get_bounds(scnview->bvh, scnview->lower, scnview->upper);
    }
    scnview->aabb_update = 0;
  }

  /* nprims_cdf 计算 — 不变 */
  res = scene_view_compute_nprims_cdf(scnview, (mask & S3D_GET_PRIMITIVE)!=0);
  if(res != RES_OK) goto error;

  scnview->mask = mask;

exit:
  return res;
error:
  goto exit;
}
```

#### Phase 2.5: 修改 on_shape_detach

删除 `scene_view_destroy_geometry()` 中对 `rtcDetachGeometry` / `rtcReleaseGeometry` 的调用。cuBQL 版本的 geometry detach 只需要：

```cpp
static INLINE void
scene_view_destroy_geometry_cu(struct s3d_scene_view* scnview, struct geometry* geom)
{
  ASSERT(geom);
  /* cuBQL: 不需要 detach RTCGeometry，标记 GPU 需要重建即可 */
  scnview->gpu_dirty = 1;
  geometry_ref_put(geom);
}
```

#### Phase 2.6: 修改 scene_view_create 和 scene_view_destroy

**scene_view_create** — 移除 `scnview->rtc_scn_build_quality = RTC_BUILD_QUALITY_MEDIUM;`，替换为：
```cpp
scnview->build_quality = CUS3D_BUILD_MEDIUM;
scnview->gpu_dirty = 1;
scnview->geom_store = NULL;
scnview->bvh = NULL;
```

**scene_view_destroy** — 移除 `if(scnview->rtc_scn) rtcReleaseScene(scnview->rtc_scn);`，替换为：
```cpp
if(scnview->geom_store)
  cus3d_geom_store_destroy(scnview->geom_store, scnview->scn->dev->gpu);
if(scnview->bvh)
  cus3d_bvh_destroy(scnview->bvh, scnview->scn->dev->gpu);
```

**scene_view_release** — 移除 `scnview->rtc_commit = 0;`，可选添加 `scnview->gpu_dirty = 1;`

#### Phase 2.7: 保持不变的函数

以下函数 **无需修改**（仅操作主机端 `htable_geom` 和 `struct geometry` 数据）：

- `cmp_float()`, `cmp_float_to_fltui()`, `cmp_size_t_to_nprims_cdf()` — 比较器
- `aabb_is_degenerated()` — 辅助函数
- `scene_view_compute_cdf()` — CDF 计算（遍历 geometry 缓存）
- `scene_view_compute_nprims_cdf()` — 不依赖 Embree
- `scene_view_compute_scene_aabb()` — 不依赖 Embree（遍历 geometry mesh/sphere）
- `scene_view_compute_volume()` — 不依赖 Embree
- `s3d_scene_view_create()` / `s3d_scene_view_create2()` — 仅调用 scene_view_sync
- `s3d_scene_view_ref_get()` / `s3d_scene_view_ref_put()` — 引用计数
- `s3d_scene_view_get_mask()` — 简单 getter
- `s3d_scene_view_sample()` — 使用 CDF + geometry，不依赖 Embree
- `s3d_scene_view_get_primitive()` — 使用 nprims_cdf + geometry，不依赖 Embree
- `s3d_scene_view_primitives_count()` — 遍历 geometry
- `s3d_scene_view_compute_area()` — 遍历 geometry
- `s3d_scene_view_compute_volume()` — 代理函数
- `s3d_scene_view_get_aabb()` — 读取 scnview->lower/upper

---

### Phase 3: 重写 s3d_scene_view_trace_ray.cpp

#### 3.1 删除的内容

- `struct intersect_context` 结构体（Embree 特有）
- `hit_setup()` 函数（RTCRayHit → s3d_hit 转换，由 `cus3d_hit_to_s3d_hit` 替代）
- `rtc_hit_filter_wrapper()` 函数（Embree 过滤回调）
- 所有 `RTC*` 类型引用

#### 3.2 新的 s3d_scene_view_trace_ray

```cpp
#include "s3d.h"
#include "s3d_c.h"
#include "s3d_device_c.h"
#include "s3d_scene_view_c.h"
#include "cus3d_trace.h"
#include "cus3d_prim.h"

#include <rsys/float3.h>
#include <float.h>

/* Maximum retries for filter function rejection */
#define MAX_FILTER_RETRIES 16

res_T
s3d_scene_view_trace_ray
  (struct s3d_scene_view* scnview,
   const float org[3],
   const float dir[3],
   const float range[2],
   void* ray_data,
   struct s3d_hit* hit)
{
  struct cus3d_hit_result gpu_hit;
  float cur_range[2];
  res_T res = RES_OK;

  if(!scnview || !org || !dir || !range || !hit)
    return RES_BAD_ARG;
  if(!f3_is_normalized(dir)) {
    log_error(scnview->scn->dev,
      "%s: unnormalized ray direction {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(dir));
    return RES_BAD_ARG;
  }
  if(range[0] < 0) {
    log_error(scnview->scn->dev,
      "%s: invalid ray range [%g, %g] - it must be in [0, INF).\n",
      FUNC_NAME, range[0], range[1]);
    return RES_BAD_ARG;
  }
  if((scnview->mask & S3D_TRACE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S3D_TRACE flag is not active onto the submitted scene view.\n",
      FUNC_NAME);
    return RES_BAD_OP;
  }
  if(range[0] > range[1]) {
    *hit = S3D_HIT_NULL;
    return RES_OK;
  }

  cur_range[0] = range[0];
  cur_range[1] = range[1];

  /* Filter retry loop: trace, check filter, re-trace if rejected */
  for (int attempt = 0; attempt < MAX_FILTER_RETRIES; attempt++) {
    res = cus3d_trace_ray_single(
      scnview->bvh,
      scnview->geom_store,
      scnview->scn->dev->gpu,
      org, dir, cur_range, &gpu_hit);
    if(res != RES_OK) return res;

    if(gpu_hit.prim_id < 0) {
      *hit = S3D_HIT_NULL;
      return RES_OK;
    }

    /* Convert GPU hit to s3d_hit */
    cus3d_hit_to_s3d_hit(scnview->geom_store, &gpu_hit, hit);

    /* ★ 关键差异: 处理 Embree 的 barycentric UV 约定 → s3d 约定 */
    /* cus3d_trace 已经返回 Moller-Trumbore (u,v)，需要转换为 s3d 的 (w, u) */
    {
      const struct geom_entry* ge = cus3d_geom_store_lookup(
        scnview->geom_store, (uint32_t)gpu_hit.prim_id);

      if(ge && ge->type == PRIM_TRIANGLE) {
        /* cus3d_trace returns Moller-Trumbore u,v (edge1 param, edge2 param)
         * s3d convention: uv[0] = 1-u-v (weight of v0), uv[1] = u (weight of v1)
         * The trace kernel stores u in uv[0] and v in uv[1] (M-T convention)
         * We need: s3d uv[0] = w = 1-u-v, s3d uv[1] = u
         */
        float mt_u = gpu_hit.uv[0];
        float mt_v = gpu_hit.uv[1];
        float w = 1.0f - mt_u - mt_v;
        if(w < 0.0f) {
          if(mt_u > mt_v) mt_u += w;
          else mt_v += w;
          w = 0.0f;
        }
        hit->uv[0] = w;     /* weight of vertex 0 */
        hit->uv[1] = mt_u;  /* weight of vertex 1 */

        /* s3d convention: negate normal (Embree3 returns opposite winding) */
        hit->normal[0] = -hit->normal[0];
        hit->normal[1] = -hit->normal[1];
        hit->normal[2] = -hit->normal[2];
      }
    }

    /* Check filter function */
    {
      const struct geom_entry* ge = cus3d_geom_store_lookup(
        scnview->geom_store, (uint32_t)gpu_hit.prim_id);

      if(ge && ge->filter_func) {
        int filtered = ge->filter_func(
          hit, org, dir, range, ray_data, ge->filter_data);
        if(filtered) {
          /* Rejected: advance tnear past this hit and retry */
          cur_range[0] = gpu_hit.distance + 1e-6f;
          if(cur_range[0] >= cur_range[1]) {
            *hit = S3D_HIT_NULL;
            return RES_OK;
          }
          continue; /* Retry */
        }
      }
    }

    /* Hit accepted */
    return RES_OK;
  }

  /* Max retries exceeded, treat as miss */
  *hit = S3D_HIT_NULL;
  return RES_OK;
}

res_T
s3d_scene_view_trace_rays
  (struct s3d_scene_view* scnview,
   const size_t nrays,
   const int mask,
   const float* origins,
   const float* directions,
   const float* ranges,
   void* rays_data,
   const size_t sizeof_ray_data,
   struct s3d_hit* hits)
{
  size_t iray;
  size_t org_step, dir_step, range_step, data_step;
  size_t iorg, idir, irange, idata;
  res_T res = RES_OK;

  if(!scnview) return RES_BAD_ARG;
  if(!nrays) return RES_OK;

  /* 初始阶段: 逐射线调用，后续可优化为 batch */
  org_step = mask & S3D_RAYS_SINGLE_ORIGIN ? 0 : 3;
  dir_step = mask & S3D_RAYS_SINGLE_DIRECTION ? 0 : 3;
  range_step = mask & S3D_RAYS_SINGLE_RANGE ? 0 : 2;
  data_step = (mask & S3D_RAYS_SINGLE_DATA) || !rays_data ? 0 : sizeof_ray_data;
  iorg = idir = irange = idata = 0;

  FOR_EACH(iray, 0, nrays) {
    res = s3d_scene_view_trace_ray(scnview,
      origins + iorg, directions + idir,
      ranges + irange, (char*)rays_data + idata, hits + iray);
    if(UNLIKELY(res != RES_OK)) break;
    iorg += org_step;
    idir += dir_step;
    irange += range_step;
    idata += data_step;
  }
  return res;
}
```

#### 3.3 UV 约定说明

这是最容易出错的地方。必须仔细对齐：

| 来源 | uv[0] | uv[1] |
|------|-------|-------|
| **Moller-Trumbore (cus3d_trace kernel)** | u (edge1参数, v1 的权重) | v (edge2参数, v2 的权重) |
| **Embree hit** | u (v1 的权重) | v (v2 的权重) |
| **s3d 内部约定** | w = 1-u-v (v0 的权重) | u (v1 的权重) |

Embree → s3d 的转换代码（原始代码中的）：
```cpp
hit->uv[1] = hit->uv[0];  // s3d[1] = embree.u (v1权重)
hit->uv[0] = w;            // s3d[0] = 1-u-v   (v0权重)
```

cuBQL → s3d 的转换需要同样的映射，因为 Moller-Trumbore 的 u,v 与 Embree 的含义相同。

---

### Phase 4: 重写 s3d_scene_view_closest_point.cpp

#### 4.1 策略选择

closest_point 功能在原版中深度依赖 Embree 的 `rtcPointQuery` API（含回调机制）。有三种策略：

| 策略 | 实现复杂度 | 性能 | 建议 |
|------|-----------|------|------|
| **A: 暴力搜索** | 低 | 低 | 初始阶段使用，快速启用功能 |
| **B: GPU BVH closest-point kernel** | 高 | 高 | 后续优化 |
| **C: 借现有 BVH 遍历** | 中 | 中 | 中期方案 |

**建议**: 先用策略 A 实现正确性，功能验证通过后再优化。

#### 4.2 策略 A: 暴力搜索实现

```cpp
#include "s3d.h"
#include "s3d_c.h"
#include "s3d_device_c.h"
#include "s3d_scene_view_c.h"
#include "s3d_geometry.h"
#include "s3d_mesh.h"
#include "s3d_sphere.h"
#include "s3d_instance.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/float33.h>
#include <float.h>

/* closest_point_triangle: 保留原始实现（纯数学，无 Embree 依赖） */

/* closest_point_mesh_bruteforce: 遍历所有三角形 */
static void
closest_point_mesh_bruteforce
  (const float query[3],
   struct geometry* geom,
   struct geometry* inst,
   float radius,
   void* query_data,
   struct s3d_hit* best_hit)
{
  /* ... 遍历 mesh 的所有三角形，对每个调用 closest_point_triangle ... */
  /* ... 如果距离 < best_hit->distance && < radius, 更新 best_hit ... */
  /* ... 应用 filter function 如果存在 ... */
}

/* closest_point_sphere_direct: 直接计算 */
static void
closest_point_sphere_direct
  (const float query[3],
   struct geometry* geom,
   struct geometry* inst,
   float radius,
   void* query_data,
   struct s3d_hit* best_hit)
{
  /* ... 保留原始 closest_point_sphere 逻辑（无 RTCPointQueryFunctionArguments） ... */
}

res_T
s3d_scene_view_closest_point
  (struct s3d_scene_view* scnview,
   const float pos[3],
   const float radius,
   void* query_data,
   struct s3d_hit* hit)
{
  struct htable_geom_iterator it, end;

  if(!scnview || !pos || radius <= 0 || !hit)
    return RES_BAD_ARG;
  if((scnview->mask & S3D_TRACE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S3D_TRACE flag is not active.\n", FUNC_NAME);
    return RES_BAD_OP;
  }

  *hit = S3D_HIT_NULL;

  htable_geom_begin(&scnview->cached_geoms, &it);
  htable_geom_end(&scnview->cached_geoms, &end);

  while(!htable_geom_iterator_eq(&it, &end)) {
    struct geometry* geom = *htable_geom_iterator_data_get(&it);
    htable_geom_iterator_next(&it);

    if(!geom->is_enabled) continue;

    float cur_radius = S3D_HIT_NONE(hit) ? radius : hit->distance;

    switch(geom->type) {
      case GEOM_MESH:
        closest_point_mesh_bruteforce(pos, geom, NULL, cur_radius, query_data, hit);
        break;
      case GEOM_SPHERE:
        closest_point_sphere_direct(pos, geom, NULL, cur_radius, query_data, hit);
        break;
      case GEOM_INSTANCE:
        /* Iterate child scnview's geometries with transform */
        /* ... */
        break;
      default: break;
    }
  }

  return RES_OK;
}
```

---

## 5. #include 变更速查表

### s3d_scene_view.cpp

```diff
 #include "s3d.h"
+#include "s3d_c.h"
 #include "s3d_device_c.h"
 #include "s3d_scene_c.h"
 #include "s3d_scene_view_c.h"
 #include "s3d_shape_c.h"
+#include "s3d_geometry.h"
+#include "s3d_mesh.h"
+#include "s3d_sphere.h"
+#include "s3d_instance.h"
+#include "cus3d_device.h"
+#include "cus3d_geom_store.h"
+#include "cus3d_bvh.h"

 #include <rsys/algorithm.h>
 #include <rsys/float3.h>
 #include <rsys/float33.h>
 #include <rsys/mem_allocator.h>

-/* 删除所有 embree4/ 相关 include */
```

### s3d_scene_view_trace_ray.cpp

```diff
 #include "s3d.h"
 #include "s3d_c.h"
 #include "s3d_device_c.h"
-#include "s3d_instance.h"
-#include "s3d_geometry.h"
-#include "s3d_mesh.h"
-#include "s3d_sphere.h"
 #include "s3d_scene_view_c.h"
+#include "cus3d_trace.h"
+#include "cus3d_prim.h"
+#include "cus3d_geom_store.h"

-#include <rsys/float33.h>
-#include <limits.h>
+#include <rsys/float3.h>
+#include <float.h>
```

### s3d_scene_view_closest_point.cpp

```diff
 #include "s3d.h"
+#include "s3d_c.h"
 #include "s3d_device_c.h"
 #include "s3d_instance.h"
 #include "s3d_geometry.h"
 #include "s3d_mesh.h"
-#include "s3d_scene_view_c.h"
 #include "s3d_sphere.h"
+#include "s3d_scene_view_c.h"

 #include <rsys/float2.h>
 #include <rsys/float3.h>
 #include <rsys/double2.h>
 #include <rsys/double3.h>
 #include <rsys/float33.h>

-/* 删除所有 embree4/ 相关 include */
-/* 删除 struct point_query_context（RTCPointQueryContext） */
+#include <float.h>
```

---

## 6. s3d_scene_view_c.h 变更确认

当前状态 **已正确**。以下字段已经到位：

```cpp
struct s3d_scene_view {
  /* ... 保留所有 host-side 字段（htable_geom, cdf, nprims_cdf 等）... */

  struct cus3d_geom_store* geom_store;  // ✅ 已添加
  struct cus3d_bvh*        bvh;          // ✅ 已添加
  int                      build_quality; // ✅ 已添加
  int                      gpu_dirty;     // ✅ 已添加

  ref_T ref;
  struct s3d_scene* scn;
};
```

需要确认的移除项：
- ~~`RTCScene rtc_scn`~~ — 已移除 ✅
- ~~`int rtc_scn_flags`~~ — 已移除 ✅
- ~~`int rtc_scn_update`~~ — 已移除 ✅
- ~~`int rtc_commit`~~ — 已移除 ✅
- ~~`enum RTCBuildQuality rtc_scn_build_quality`~~ — 已移除 ✅

---

## 7. 编译检查清单

重写完成后，确保以下内容不存在于任何源文件中（grep 检查）：

```bash
# 在 custar-3d/0.10/src/ 中不应出现：
grep -rn "RTC\|rtc\|embree\|Embree\|EMBREE" --include="*.cpp" --include="*.h" --include="*.c"
```

预期结果：仅在注释中出现（解释迁移来源），不在代码中出现。

**例外**：
- `s3d_geometry.h` 中的 `enum geometry_type` 值名称（`GEOM_MESH` 等）不含 RTC 前缀，保留
- 注释中说明"replaces RTCxxx"的文字可以保留

---

## 8. 执行顺序建议

```
Phase 0: cus3d 模块扩展（Top-K kernel + geom_entry 查找）
  │  预估: ~2 小时
  │  风险: 低（独立模块，不碰 s3d 代码）
  │  验证: 编译通过 + Top-K 单元测试
  │  可并行: 与 Phase 1 并行
  │
Phase 1: s3d_geometry.cpp 清理
  │  预估: ~30 分钟
  │  风险: 低（纯删除）
  │  验证: 编译通过
  │  可并行: 与 Phase 0 并行
  │
Phase 2.1-2.2: 删除 Embree 辅助函数 + 新 register 函数
  │  预估: ~2 小时
  │  风险: 中（CDF/sampling 依赖 htable_geom 必须正确工作）
  │  验证: 编译通过
  │  依赖: Phase 0 + Phase 1
  │
Phase 2.3-2.6: scene_view_setup_cubql + sync + 生命周期修改
  │  预估: ~1.5 小时
  │  风险: 中（GPU 管线首次集成）
  │  验证: 编译通过 + test_s3d_scene_view 能运行
  │
Phase 3: trace_ray 重写（Top-K + filter 后过滤）
  │  预估: ~2 小时
  │  风险: 高（UV 约定、法线方向、filter 连续判断、方案 A 回退）
  │  验证: test_s3d_trace_ray + test_s3d_trace_ray_sphere
  │       + test_s3d_trace_ray_filter (新增)
  │  依赖: Phase 0 (Top-K API) + Phase 2 (BVH 可用)
  │
Phase 4: closest_point 重写（含 filter 调用）
  │  预估: ~2 小时
  │  风险: 中（暴力搜索简单但 filter 集成需仔细）
  │  验证: test_s3d_closest_point + test_s3d_closest_point_filter (新增)
  │  依赖: Phase 2 (htable_geom)
  │  可并行: 与 Phase 3 并行
  │
Phase 5: 全面测试 + UV/法线/filter 回归验证
  │  预估: ~2 小时
  │  验证: Phase 5 测试矩阵全部通过（见附录 A.5）
  │  依赖: 全部 Phase 完成
```

---

## 9. 注意事项与陷阱

### 9.1 UV/法线约定陷阱（最高优先级）

1. **三角形法线方向**: Embree3 返回的法线方向与 s3d 约定相反，原代码中有 `f3_minus(hit->normal)` 翻转。cuBQL 的 trace kernel 使用 `cross(e1, e2)` 计算法线，需要确认与 s3d 的 CW 顶点序约定一致。**建议在 kernel 中改为 `cross(e2, e1)` 或在主机端翻转。**

2. **Barycentric UV 映射**: 如 Phase 3 详述，必须严格匹配 `s3d_primitive_get_attrib` 中的插值逻辑。

3. **球体 UV**: cuBQL `sphere_normal_to_uv` 的实现需与原 `s3d_sphere.h` 中的 `sphere_normal_to_uv` 一致。

### 9.2 Instance 支持（可延后）

当前 `cus3d_geom_store_sync` 跳过 `GEOM_INSTANCE` 类型。两级 BVH 支持可以在基础功能验证后再实现。

### 9.3 geometry 的 scene_prim_id_offset

`scene_view_compute_nprims_cdf` 设置每个 geometry 的 `scene_prim_id_offset`。这在 sampling 和 `s3d_scene_view_get_primitive` 中被使用。cuBQL 版本必须保持此逻辑不变。

### 9.4 htable_geom 与 cus3d_geom_store 的并存

`htable_geom` 用于主机端查找（CDF/sampling/primitive/area/volume），`cus3d_geom_store` 用于 GPU 几何和射线追踪。两者的数据必须保持同步。`cus3d_geom_store_sync` 从 `scene->shapes` 独立地收集数据，不依赖 `htable_geom`。

### 9.5 scene_view 缓存池机制

原始代码有一个 scene_view 缓存池（`scene->scnviews` 链表）。`scene_view_release` 不销毁 scene_view，而是放回池中。cuBQL 版本需要注意：
- 放回池时 **不要销毁** `geom_store` 和 `bvh`（它们可以被下次使用时重建）
- 只需标记 `gpu_dirty = 1`，下次 `scene_view_sync` 时重建

### 9.6 线程安全

原始代码不是线程安全的（单一 scene_view 不能并发 trace）。cuBQL 版本同样不需要考虑线程安全。

---

## 附录 A: 完整编辑动作清单（结合第 10 章审计结论）

> 本附录是实施的**唯一权威检查清单**。按 Phase 顺序排列，每个 Phase 内部按文件分组。
> 标注 `[10 章]` 的条目来自能力保留审计，若遗漏将导致功能回退。

---

### Phase 0: 新增 GPU 模块扩展（cus3d 层）

> 本 Phase 的修改均在 cus3d 模块内部，不碰任何 s3d 文件，可独立编译验证。

#### A.0.1 cus3d_types.h — 新增 Top-K 命中结构体 `[10 章 §10.5]`

| 动作 | 内容 |
|------|------|
| **新增** `CUS3D_MAX_MULTI_HITS` | `#define CUS3D_MAX_MULTI_HITS 8` |
| **新增** `struct cus3d_multi_hit_result` | `{ int32_t count; struct cus3d_hit_result hits[CUS3D_MAX_MULTI_HITS]; }` |

位置：在 `struct cus3d_hit_result` 定义之后、`struct geom_gpu_entry` 之前。

#### A.0.2 cus3d_trace.h — 新增 Top-K 单射线 API `[10 章 §10.5]`

| 动作 | 内容 |
|------|------|
| **新增** `#include "cus3d_types.h"` 中的 `cus3d_multi_hit_result` 前置声明 | 已通过 include 覆盖 |
| **新增** 函数声明 | `res_T cus3d_trace_ray_single_multi(const struct cus3d_bvh*, const struct cus3d_geom_store*, struct cus3d_device*, const float origin[3], const float direction[3], const float range[2], int max_hits, struct cus3d_multi_hit_result* result);` |

位置：在 `cus3d_trace_ray_single` 声明之后。

#### A.0.3 cus3d_trace.cu — 新增 Top-K kernel 及主机 API `[10 章 §10.5]`

| 动作 | 内容 |
|------|------|
| **新增** `trace_rays_topk_kernel` | GPU kernel，维护 K 大小插入排序数组，`ray.tMax` 收缩到 `slots[K-1].distance`。完整代码见 §10.5.4 |
| **新增** `cus3d_trace_ray_single_multi` | 主机端 API，分配 GPU 内存、启动 `trace_rays_topk_kernel<<<1,1>>>`、回传 `cus3d_multi_hit_result`。模式同现有 `cus3d_trace_ray_single` |

位置：在 `cus3d_trace_ray_single` 函数之前。

#### A.0.4 cus3d_geom_store.h — 确认 geom_entry 的 filter 字段已就位

| 字段 | 现状 | 动作 |
|------|------|------|
| `geom_entry::filter_func` | ✅ 已存在 | 无需修改 |
| `geom_entry::filter_data` | ✅ 已存在 | 无需修改 |
| `geom_gpu_entry::has_filter` | ✅ 已存在 | 无需修改 |

#### A.0.5 cus3d_geom_store.h — 新增 entry 查找函数 `[10 章 §10.5]`

| 动作 | 内容 |
|------|------|
| **新增** 函数声明 | `const struct geom_entry* cus3d_geom_store_get_entry(const struct cus3d_geom_store* store, uint32_t geom_idx);` |

> 供 `s3d_scene_view_trace_ray` 中根据 `hit.geom_idx` 查找对应 `geom_entry`，进而获取 `filter_func`。

#### A.0.6 cus3d_geom_store.cpp — 新增 entry 查找实现

| 动作 | 内容 |
|------|------|
| **新增** `cus3d_geom_store_get_entry` | 返回 `&store->entries[geom_idx]`（加越界检查） |

**Phase 0 验证**: 编译通过 + `cus3d_trace_ray_single_multi` 的单元测试（使用已有 Cornell Box 场景验证返回 K 个有序 hit）。

---

### Phase 1: s3d_geometry.cpp 清理

#### A.1.1 s3d_geometry.cpp — 删除 Embree 残留

| 项目 | 动作 | 说明 |
|------|------|------|
| `#include <embree4/...>` | ❌ 删除 | 所有 Embree 头文件引用 |
| `extern ... rtc_hit_filter_wrapper` | ❌ 删除 | 外部 Embree filter 包装器的前向声明 |
| `sphere_ray_hit_setup()` | ❌ 整体删除（~40行） | Embree hit 结果设置 + filter 调用 |
| `geometry_rtc_sphere_bounds()` | ❌ 整体删除（~15行） | Embree 球体 AABB 回调 |
| `geometry_rtc_sphere_intersect()` | ❌ 整体删除（~40行） | Embree 球体求交回调 |
| `geometry_create()` 内 `geom->rtc = ...` | ❌ 删除这3行 | `rtc`, `rtc_id`, `rtc_build_quality` 初始化 |

#### A.1.2 s3d_geometry.cpp — 保留不变

| 项目 | 说明 |
|------|------|
| `geometry_create()` 剩余部分 | 分配、初始化 name/type/is_enabled/flip_surface/ref |
| `geometry_release()` | 引用计数释放 |
| `geometry_ref_get()` / `geometry_ref_put()` | 引用计数操作 |

**Phase 1 验证**: 编译通过。

---

### Phase 2: s3d_scene_view.cpp 重写

#### A.2.1 #include 变更

| 动作 | 内容 |
|------|------|
| ❌ 删除 | 所有 `#include <embree4/...>` |
| ✅ 新增 | `#include "cus3d_device.h"` |
| ✅ 新增 | `#include "cus3d_geom_store.h"` |
| ✅ 新增 | `#include "cus3d_bvh.h"` |

#### A.2.2 删除 Embree 辅助函数（整体删除、无替代）

| 函数名 | 大约行数 |
|--------|---------|
| `accel_struct_quality_to_rtc_build_quality()` | ~15 |
| `accel_struct_mask_to_rtc_scene_flags()` | ~10 |
| `embree_geometry_register()` | ~60 |
| `embree_geometry_setup_positions()` | ~20 |
| `embree_geometry_setup_indices()` | ~20 |
| `embree_geometry_setup_enable_state()` | ~8 |
| `embree_geometry_setup_filter_function()` | ~10 |
| `embree_geometry_setup_transform()` | ~8 |

#### A.2.3 替换函数（用新版替代）

| 原函数 | 新函数 | 变化摘要 |
|--------|--------|---------|
| `scene_view_destroy_geometry()` | `scene_view_destroy_geometry_cu()` | 删除 `rtcDetachGeometry`/`rtcReleaseGeometry`，改为 `scnview->gpu_dirty = 1` + `geometry_ref_put` |
| `scene_view_register_mesh()` | `scene_view_register_mesh_cu()` | 删除 `embree_outdated_mask` 位操作，改为设置 `scnview->gpu_dirty = 1`；删除所有 `rtc*` 调用。同步 filter 引用。完整代码见 §Phase 2.2 |
| `scene_view_register_sphere()` | `scene_view_register_sphere_cu()` | 同上模式。同步 filter 引用。完整代码见 §Phase 2.2 |
| `scene_view_register_instance()` | `scene_view_register_instance_cu()` | 同上模式。递归创建子 scene_view 逻辑保留不变。完整代码见 §Phase 2.2 |
| `scene_view_setup_embree()` | `scene_view_setup_cubql()` | 替换全部 Embree 场景管理为 `cus3d_geom_store_sync` → `cus3d_geom_store_compute_bounds` → `cus3d_bvh_build`。完整代码见 §Phase 2.3 |

#### A.2.4 修改函数（编辑局部）

| 函数 | 修改位置 | 内容 |
|------|---------|------|
| `scene_view_sync()` | shape 注册调用 | `scene_view_register_mesh` → `scene_view_register_mesh_cu`，sphere/instance 同理 |
| `scene_view_sync()` | Embree setup | `scene_view_setup_embree(...)` → `scene_view_setup_cubql(...)` |
| `scene_view_sync()` | AABB 获取 | `rtcGetSceneBounds(scnview->rtc_scn, ...)` → `cus3d_bvh_get_bounds(scnview->bvh, scnview->lower, scnview->upper)` |
| `scene_view_create()` / `scene_view_pool_get()` | 初始化 | 删除 `rtc_scn_build_quality = RTC_BUILD_QUALITY_MEDIUM` 等 → 添加 `build_quality = CUS3D_BUILD_MEDIUM; gpu_dirty = 1; geom_store = NULL; bvh = NULL;` |
| `scene_view_destroy()` | 销毁 | 删除 `rtcReleaseScene(scnview->rtc_scn)` → 添加 `cus3d_geom_store_destroy(scnview->geom_store, ...)` + `cus3d_bvh_destroy(scnview->bvh, ...)` |
| `scene_view_release()` | 释放 | 删除 `scnview->rtc_commit = 0` → 添加 `scnview->gpu_dirty = 1` |
| `on_shape_detach()` `[10 章 §5]` | 几何清理 | 内部调用 `scene_view_destroy_geometry` → 改为调用 `scene_view_destroy_geometry_cu`（标记 `gpu_dirty`，不调用 rtcDetach） |

#### A.2.5 保持不变的函数

| 函数 | 原因 |
|------|------|
| `cmp_float()`, `cmp_float_to_fltui()`, `cmp_size_t_to_nprims_cdf()` | 纯比较器 |
| `aabb_is_degenerated()` | 辅助函数 |
| `scene_view_compute_cdf()` | 遍历 `htable_geom`，不依赖 Embree |
| `scene_view_compute_nprims_cdf()` | 同上 |
| `scene_view_compute_scene_aabb()`（手动路径） | 遍历 geometry mesh/sphere，不依赖 Embree |
| `scene_view_compute_volume()` | 遍历 geometry |
| `s3d_scene_view_create()` / `create2()` | 仅调用 `scene_view_sync` |
| `s3d_scene_view_ref_get()` / `ref_put()` | 引用计数 |
| `s3d_scene_view_get_mask()` | 简单 getter |
| `s3d_scene_view_sample()` | 使用 CDF + geometry，不依赖 Embree |
| `s3d_scene_view_get_primitive()` | 使用 nprims_cdf + geometry |
| `s3d_scene_view_primitives_count()` | 遍历 geometry |
| `s3d_scene_view_compute_area()` | 遍历 geometry |
| `s3d_scene_view_compute_volume()` | 代理函数 |
| `s3d_scene_view_get_aabb()` | 读取 scnview->lower/upper |

**Phase 2 验证**: 编译通过 + test_s3d_scene_view 能创建/同步/销毁 scene_view（不含 trace/closest_point）。

---

### Phase 3: s3d_scene_view_trace_ray.cpp 重写

#### A.3.1 #include 变更

| 动作 | 内容 |
|------|------|
| ❌ 删除 | 所有 `#include <embree4/...>` |
| ❌ 删除 | `#include "s3d_instance.h"`, `"s3d_geometry.h"`, `"s3d_mesh.h"`, `"s3d_sphere.h"`（不再需要在此文件直接访问这些类型） |
| ✅ 新增 | `#include "cus3d_trace.h"` |
| ✅ 新增 | `#include "cus3d_prim.h"` |
| ✅ 新增 | `#include "cus3d_geom_store.h"` |
| ✅ 保留 | `#include "s3d.h"`, `"s3d_c.h"`, `"s3d_device_c.h"`, `"s3d_scene_view_c.h"`, `<rsys/float3.h>` |

#### A.3.2 删除项

| 项目 | 说明 |
|------|------|
| `struct intersect_context` | Embree 特有上下文结构体 |
| `hit_setup()` | RTCRayHit → s3d_hit 转换，由 `cus3d_hit_to_s3d_hit` + UV 调整替代 |
| `rtc_hit_filter_wrapper()` | Embree filter 回调，由 CPU 后过滤替代 |

#### A.3.3 `s3d_scene_view_trace_ray()` — 整体重写 `[10 章 §10.5]`

**使用 Top-K 方案（方案 B）为主，方案 A 为回退**。完整代码见 §10.5.5。关键逻辑：

| 步骤 | 内容 | 来源 |
|------|------|------|
| 1 | 参数校验（保留原始所有检查） | 原始代码 |
| 2 | 调用 `cus3d_trace_ray_single_multi(K=4)` 获取 Top-K 候选 | `[10 章 §10.5]` |
| 3 | **遍历 `multi.hits[0..count-1]`** | `[10 章 §10.5]` |
| 3a | `cus3d_hit_to_s3d_hit()` 转换每个候选 | Phase 3 |
| 3b | **UV 约定转换**: Moller-Trumbore `(u,v)` → s3d `(w=1-u-v, u)`；**三角形法线取反** | §3.3 / §9.1 |
| 3c | **查找 `geom_entry::filter_func`**，若存在则调用 | `[10 章 §10.4/10.5]` |
| 3d | 若 filter 拒绝 → `continue` 尝试下一个候选 | `[10 章 §10.5]` |
| 3e | 若 filter 接受或无 filter → return 命中 | `[10 章 §10.5]` |
| 4 | K 个候选全被拒绝 → **回退方案 A**: `range[0] = t_K + ε`，递归调用 `s3d_scene_view_trace_ray` | `[10 章 §10.5.7]` |
| 5 | 超出范围或无命中 → `*hit = S3D_HIT_NULL` | 原始代码 |

#### A.3.4 `s3d_scene_view_trace_rays()` — 保留框架

| 修改 | 内容 |
|------|------|
| 循环体 | 调用新版 `s3d_scene_view_trace_ray()`（已包含 Top-K + filter 逻辑） |
| 其余 | step 计算（`org_step`, `dir_step` 等）和 mask 逻辑不变 |

**Phase 3 验证**: test_s3d_trace_ray + test_s3d_trace_ray_sphere。**额外验证**：设置 filter function（排除 geom_id == self），确认自相交避免正确工作。

---

### Phase 4: s3d_scene_view_closest_point.cpp 重写

#### A.4.1 #include 变更

| 动作 | 内容 |
|------|------|
| ❌ 删除 | 所有 `#include <embree4/...>` |
| ❌ 删除 | `struct point_query_context` 定义 |
| ✅ 新增 | `#include "s3d_c.h"`, `#include "cus3d_geom_store.h"` |
| ✅ 保留 | `"s3d.h"`, `"s3d_device_c.h"`, `"s3d_instance.h"`, `"s3d_geometry.h"`, `"s3d_mesh.h"`, `"s3d_sphere.h"`, `"s3d_scene_view_c.h"`, `<rsys/float*.h>`, `<rsys/double*.h>`, `<rsys/float33.h>` |
| ✅ 新增 | `#include <float.h>` |

#### A.4.2 保留项

| 项目 | 说明 |
|------|------|
| `closest_point_triangle()` | **完整保留**。纯数学（点-三角形最近点），无 Embree 依赖 |

#### A.4.3 重写项

| 原函数 | 新函数 | 变化摘要 |
|--------|--------|---------|
| `closest_point_mesh()` | `closest_point_mesh_bruteforce()` | 删除 `RTCPointQueryFunctionArguments*` 参数，改为 `(query[3], geom, inst, radius, query_data, best_hit)`。遍历 mesh 所有三角形，对每个调用 `closest_point_triangle`；**`[10 章]` 必须调用 `filter_func`**：命中后检查 `geom->data.mesh->filter.func`，若拒绝则跳过该候选 |
| `closest_point_sphere()` | `closest_point_sphere_direct()` | 删除 `RTCPointQueryFunctionArguments*` 参数，改为同上签名。保留球体最近点数学。**`[10 章]` 必须调用 `filter_func`**：检查 `geom->data.sphere->filter.func` |
| `closest_point()` (Embree dispatch 回调) | ❌ 整体删除 | scene_view_closest_point 直接遍历 htable_geom 分发 |

#### A.4.4 `s3d_scene_view_closest_point()` — 整体重写

| 步骤 | 内容 | 来源 |
|------|------|------|
| 1 | 参数校验 + `S3D_TRACE` mask 检查 | 原始代码 |
| 2 | `*hit = S3D_HIT_NULL` | 原始代码 |
| 3 | 遍历 `htable_geom`，跳过 `!is_enabled` | 原始的 Embree `rtcPointQuery` 路径内部也遍历所有 geometry |
| 4a | `GEOM_MESH` → `closest_point_mesh_bruteforce(pos, geom, NULL, cur_radius, query_data, hit)` | Phase 4 |
| 4b | `GEOM_SPHERE` → `closest_point_sphere_direct(pos, geom, NULL, cur_radius, query_data, hit)` | Phase 4 |
| 4c | `GEOM_INSTANCE` → 将 `pos` 变换到 instance local space，遍历子 scene_view 的 `htable_geom` | 原始逻辑适配 |
| 5 | Filter 调用 `[10 章]` — 在 4a/4b/4c 的每个候选中，保持原始 Embree 版的 filter 调用语义 | `[10 章 §10.6]` |

**关键 `[10 章]` 要求**: `closest_point_mesh_bruteforce` 和 `closest_point_sphere_direct` 中，在更新 `best_hit` 之前必须检查 filter。代码模式：

```cpp
/* 在 closest_point_mesh_bruteforce 内部，找到候选最近点后 */
if (candidate_dist < cur_radius) {
    /* 构造临时 s3d_hit */
    struct s3d_hit tmp_hit;
    /* ... 填充 tmp_hit ... */

    /* [10章] 调用 filter function */
    if (geom->data.mesh->filter.func) {
        float dummy_dir[3] = {0, 0, 0};
        float dummy_range[2] = {0, candidate_dist};
        int rejected = geom->data.mesh->filter.func(
            &tmp_hit, query, dummy_dir, dummy_range,
            query_data, geom->data.mesh->filter.data);
        if (rejected) continue;  /* 跳过此候选 */
    }

    *best_hit = tmp_hit;
    cur_radius = candidate_dist;
}
```

**Phase 4 验证**: test_s3d_closest_point。**额外验证**：设置 filter function，确认被过滤的 primitive 不会出现在最近点结果中。

---

### Phase 5: 全面测试 + 回归验证

#### A.5.1 编译清洁度检查

```bash
# 在 custar-3d/0.10/src/ 中搜索 Embree 残留
grep -rn "RTC\|rtc_\|embree\|Embree\|EMBREE" --include="*.cpp" --include="*.h" --include="*.c" --include="*.cu"
```

预期：仅在注释中出现解释性文字，代码中零残留。

#### A.5.2 功能测试矩阵

| 测试 | 验证内容 | 涉及 Phase |
|------|---------|-----------|
| test_s3d_scene_view | 创建/同步/销毁 scene_view | Phase 2 |
| test_s3d_trace_ray | 三角形射线追踪命中/未命中 | Phase 3 |
| test_s3d_trace_ray_sphere | 球体射线追踪命中/未命中 | Phase 3 |
| **test_s3d_trace_ray_filter** `[10 章]` | 设置 filter → 验证自相交避免 | Phase 0+3 |
| **test_s3d_trace_ray_topk** `[10 章]` | 多命中返回 + filter 连续判断 | Phase 0+3 |
| test_s3d_closest_point | 最近点查询正确性 | Phase 4 |
| **test_s3d_closest_point_filter** `[10 章]` | 最近点 + filter 正确拒绝 | Phase 4 |
| test_s3d_sample | 采样 CDF 正确性 | Phase 2（不变） |
| test_s3d_get_primitive | primitive 查询 | Phase 2（不变） |
| test_s3d_compute_area | 面积计算 | Phase 2（不变） |
| test_s3d_compute_volume | 体积计算 | Phase 2（不变） |
| test_s3d_aabb | AABB 计算（手动路径 + BVH 路径） | Phase 2 |

#### A.5.3 UV / 法线约定专项验证

| 检查项 | 方法 | 通过条件 |
|--------|------|---------|
| 三角形 UV 插值 | 追踪射线命中三角形中心 → `s3d_primitive_get_attrib(POSITION)` → 与手动计算的中心坐标比较 | 误差 < 1e-5 |
| 三角形法线方向 | 追踪射线命中三角形 → `hit.normal` 应指向射线正面（即 `dot(dir, normal) < 0`） | 全部满足 |
| 球体 UV | 追踪射线命中球体 → `hit.uv` 与 `sphere_normal_to_uv(hit.normal)` 一致 | 完全一致 |

#### A.5.4 Shape Detach 生命周期验证 `[10 章 §5]`

| 检查项 | 方法 | 通过条件 |
|--------|------|---------|
| detach 后 trace | 创建 scene_view → detach 一个 shape → re-sync → trace → 不应命中被 detach 的 geometry | 正确 miss |
| detach 期间使用 | 创建 scene_view → 开始 trace_rays → 在另一线程 detach shape → trace 不应崩溃 | 不崩溃（延迟删除） |
| 缓存池重用 | release scene_view → 重新 create → 确认 `gpu_dirty == 1`，sync 后 BVH 重建正确 | trace 结果正确 |

---

### 跨 Phase 依赖关系图

```
Phase 0 (cus3d 扩展)
  │   cus3d_types.h: multi_hit_result
  │   cus3d_trace.h/cu: trace_ray_single_multi + topk_kernel
  │   cus3d_geom_store: get_entry 查找
  │
  ├──→ Phase 3 (trace_ray) 依赖 Phase 0 的 Top-K API
  │
Phase 1 (geometry 清理) ←─── 无依赖，可与 Phase 0 并行
  │
Phase 2 (scene_view.cpp)
  │   依赖 Phase 1（geometry 头文件清洁）
  │   依赖 Phase 0（cus3d_geom_store/bvh API）
  │
Phase 3 (trace_ray)
  │   依赖 Phase 0（cus3d_trace_ray_single_multi）
  │   依赖 Phase 2（scene_view_setup_cubql 完成 → BVH 可用）
  │
Phase 4 (closest_point)
  │   依赖 Phase 2（htable_geom 正常工作）
  │   不依赖 Phase 0/3（暴力搜索不用 GPU trace）
  │
Phase 5 (验证)
  │   依赖全部 Phase 完成
```

**可并行的工作**:
- Phase 0 + Phase 1 （互不相关）
- Phase 3 + Phase 4 （前提: Phase 2 完成后可并行）

---

## 10. 能力保留审计：自定义函数 / 回调机制

### 10.1 原始 star-3d 中的所有回调/函数指针机制

| # | 机制 | 类型 | 签名 | 存储位置 | 运行时调用点 | cuBQL 状态 |
|---|------|------|------|----------|-------------|-----------|
| 1 | **Hit Filter (mesh)** | 用户回调 | `int (*)(s3d_hit*, org, dir, range, query_data, filter_data)` | `mesh::filter.func/data` | `trace_ray`, `closest_point` | 🔴 **数据已存但未调用** |
| 2 | **Hit Filter (sphere)** | 用户回调 | 同上 | `sphere::filter.func/data` | `trace_ray`, `closest_point` | 🔴 **数据已存但未调用** |
| 3 | Vertex Data 回调 | 用户回调 | `void (*)(ivert, value, ctx)` | `s3d_vertex_data::get` | 仅 `s3d_mesh_setup_indexed_vertices` | ✅ 无需迁移（CPU mesh setup） |
| 4 | Get Indices 回调 | 用户回调 | `void (*)(itri, ids[3], ctx)` | 函数参数 | 仅 `s3d_mesh_setup_indexed_vertices` | ✅ 无需迁移 |
| 5 | Shape Detach Signal | 内部信号 | `CLBK(ARG2(scn, shape))` | `scnview::on_shape_detach_cb` | shape 从 scene detach 时 | ✅ 保留（需替换清理代码） |
| 6 | RTC Sphere Bounds | Embree 内部 | `RTCBoundsFunction` | Embree 注册 | BVH 构建时 | ✅ 已被 `cus3d_geom_store.cu` GPU kernel 替代 |
| 7 | RTC Sphere Intersect | Embree 内部 | `RTCIntersectFunction` | Embree 注册 | trace 时 | ✅ 已被 `cus3d_trace.cu` GPU kernel 替代 |
| 8 | RTC Filter Wrapper | Embree 内部 | `RTCFilterFunction` | Embree 注册 | trace 时 | 🔴 **需等效方案** |

### 10.2 🔴 关键缺失：Hit Filter Function 未在 GPU trace 中调用

**现状**:
- `cus3d_geom_store` **正确保存**了 `filter_func`、`filter_data`（主机端 `geom_entry`）和 `has_filter`（GPU 端 `geom_gpu_entry`）
- 但 `cus3d_trace.cu` 的 `trace_rays_kernel` 和 `trace_rays_instanced_kernel` **完全不检查 `has_filter`**，不调用任何过滤逻辑
- `filter_func` 是主机端函数指针，**无法在 GPU kernel 中直接调用**

**影响**:
- 使用 `s3d_mesh_set_hit_filter_function()` 或 `s3d_sphere_set_hit_filter_function()` 的场景会产生**错误结果**
- stardis-solver 的蒙特卡洛路径追踪中，filter function 用于自相交避免和透明/遮罩表面处理 — **这是热辐射模拟的核心功能**

**原始 Embree 调用链（4 个调用点）**:

1. `trace_ray(mesh)` — `rtcSetGeometryIntersectFilterFunction` 注册 `rtc_hit_filter_wrapper`，Embree 在每个候选交点自动调用
2. `trace_ray(sphere)` — 球体是 user geometry，在 `sphere_ray_hit_setup()` 中手动调用 `rtc_hit_filter_wrapper`
3. `closest_point(mesh)` — 在 `closest_point_mesh()` 中直接调用 `filter->func`
4. `closest_point(sphere)` — 在 `closest_point_sphere()` 中直接调用 `filter->func`

### 10.3 Filter Function 迁移方案比较

| 方案 | 描述 | 实现复杂度 | 性能代价 | API 兼容性 | 建议 |
|------|------|-----------|---------|-----------|------|
| **A: CPU 后过滤重试** | GPU 返回最近交点 → CPU 调用 filter → 若拒绝则 `range[0] = t + ε` 重新 GPU trace | 低 | 🔴 高（多次 GPU-CPU 往返） | ✅ 完全 | **Phase 1 实现** |
| **B: GPU 多候选返回** | kernel 返回 top-K 个候选交点 → CPU 按序调 filter 直到通过 | 中 | 🟡 中（K 较小时可接受） | ✅ 完全 | **Phase 2 优化** |
| **C: GPU 枚举式 filter** | 将 filter 逻辑编译为 GPU 代码（enum 分发 + 预定义类型） | 高 | 🟢 最优 | ⚠️ 限于预定义类型 | 特定场景 |
| **D: per-ray 掩码过滤** | `ray_data` 传递 exclude_geom_id，kernel 内 skip | 中 | 🟢 优 | ⚠️ 仅覆盖自相交避免 | 可作为快速路径 |

**推荐实施路径**:

1. **初始阶段（方案 A）**: 在 `s3d_scene_view_trace_ray` 中实现 CPU 后过滤重试循环（本指南 Phase 3 的代码已包含此逻辑）。这保证 100% API 兼容性。
2. **性能优化（方案 B+D）**: 对 stardis-solver 的核心场景分析 filter 的实际使用模式：
   - 若主要是"排除某个 geom_id"（自相交避免），实现方案 D 作为快速路径
   - 若需要通用支持，实现方案 B（top-K 返回）

### 10.4 方案 A 的实现细节（Phase 3 trace_ray 中已给出框架）

```cpp
/* 关键代码路径：*/
for (int attempt = 0; attempt < MAX_FILTER_RETRIES; attempt++) {
    /* 1. GPU trace — 找到最近交点 */
    cus3d_trace_ray_single(..., cur_range, &gpu_hit);

    if (gpu_hit.prim_id < 0) { *hit = S3D_HIT_NULL; return; }

    /* 2. 转换 hit */
    cus3d_hit_to_s3d_hit(scnview->geom_store, &gpu_hit, hit);

    /* 3. 查找该 geometry 的 filter function */
    const struct geom_entry* ge = cus3d_geom_store_lookup(...);
    if (ge && ge->filter_func) {
        int rejected = ge->filter_func(hit, org, dir, range,
                                        ray_data, ge->filter_data);
        if (rejected) {
            /* 4. 拒绝 → 收缩 range, 重试 */
            cur_range[0] = gpu_hit.distance + 1e-6f;
            continue;
        }
    }
    /* 5. 接受 → 返回 */
    return RES_OK;
}
```

**注意**: `cus3d_trace_ray_single` 的 API 需要扩展以接收 `ray_data` 参数（当前签名中缺失），或者 `ray_data` 仅在主机端 filter 调用时使用（方案 A 不需要传入 GPU）。

### 10.5 方案 B 的具体设计（Top-K Multi-Hit, 推荐 side-attempt）

方案 A 的核心问题：每次 filter 拒绝后，需要重新启动 GPU kernel（修改 `range[0]`），产生完整的 kernel launch + sync + D2H transfer 往返。蒙特卡洛路径追踪中 filter 拒绝率可能较高（自相交避免几乎每条射线都会命中自身），这意味着**平均每条射线 2+ 次 GPU 往返**，延迟难以接受。

方案 B 的核心思路：**一次 BVH 遍历收集 K 个最近交点，全部回传 CPU，CPU 连续调用 filter 直到某个通过**。只需 1 次 kernel launch + 1 次 D2H transfer。

#### 10.5.1 为什么 cuBQL 的 shrinkingRayQuery 天然支持 Top-K

`shrinkingRayQuery::forEachPrim` 的 lambda 返回更新后的 `ray.tMax`，控制遍历剪枝。当前实现中：

```cuda
// 现有：找到更近的交点 → 立即收缩 tMax 到该交点
ray.tMax = t;
return ray.tMax;
```

Top-K 修改：**`ray.tMax` 收缩到第 K 个最近的交点（而非最近），保证遍历不会遗漏前 K 近的候选**。

```cuda
// Top-K：收缩 tMax 到第 K 近的交点
ray.tMax = slots[num_hits - 1].distance;  // 最远的候选
return ray.tMax;
```

BVH 遍历仍然能有效剪枝（跳过所有 t > 第K近 的子树），性能接近单次 trace。

#### 10.5.2 新增类型定义（cus3d_types.h）

```cpp
#define CUS3D_MAX_MULTI_HITS 8

struct cus3d_multi_hit_result {
    int32_t   count;                                     /* 实际命中数 [0, K] */
    struct cus3d_hit_result hits[CUS3D_MAX_MULTI_HITS];  /* 按 distance 升序排列 */
};
```

`K = 8` 的依据：stardis-solver 中 filter 主要用于自相交避免（排除自身 primitive），典型场景下 1-2 次拒绝后即可找到有效交点。K=8 提供了充裕的余量，同时每个 `cus3d_multi_hit_result` 大小仅 `4 + 8*(4*3 + 4*3 + 4*2) = 4 + 8*40 = 324 bytes`，完全可以放在寄存器/local memory 中。

#### 10.5.3 新增 API（cus3d_trace.h）

```cpp
/**
 * @brief Trace a single ray and return up to max_hits nearest intersections.
 *
 * Hits are returned sorted by distance (ascending).
 * CPU code iterates hits[0..count-1] calling filter function on each;
 * the first one that passes the filter is the accepted hit.
 *
 * @param max_hits  Desired hit count, clamped to CUS3D_MAX_MULTI_HITS.
 * @param result    Output: count + sorted hit array.
 */
res_T cus3d_trace_ray_single_multi(
    const struct cus3d_bvh* bvh,
    const struct cus3d_geom_store* store,
    struct cus3d_device* dev,
    const float origin[3],
    const float direction[3],
    const float range[2],
    int max_hits,
    struct cus3d_multi_hit_result* result);
```

#### 10.5.4 新增 GPU kernel（cus3d_trace.cu）

```cuda
/*******************************************************************************
 * Top-K single-level trace kernel
 *
 * Maintains a small sorted array of the K nearest hits.
 * ray.tMax shrinks to the K-th hit distance, enabling BVH culling.
 ******************************************************************************/
__global__ void trace_rays_topk_kernel(
    cuBQL::BinaryBVH<float, 3>     bvh,
    const float3* __restrict__     vertices,
    const uint3*  __restrict__     indices,
    const struct sphere_gpu* __restrict__ spheres,
    const unsigned int* __restrict__ prim_to_geom,
    const struct geom_gpu_entry* __restrict__ geom_entries,
    uint32_t                       tri_count,
    const float3* __restrict__     ray_origins,
    const float3* __restrict__     ray_dirs,
    const float2* __restrict__     ray_ranges,
    uint32_t                       num_rays,
    int                            max_k,
    struct cus3d_multi_hit_result*  results)
{
    uint32_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= num_rays) return;

    float3 org = ray_origins[tid];
    float3 dir = ray_dirs[tid];
    float  tmin = ray_ranges[tid].x;
    float  tmax = ray_ranges[tid].y;

    /* Top-K hit buffer — 局部变量，存寄存器/local memory */
    struct cus3d_hit_result slots[CUS3D_MAX_MULTI_HITS];
    int num_hits = 0;
    int K = max_k < CUS3D_MAX_MULTI_HITS ? max_k : CUS3D_MAX_MULTI_HITS;

    cuBQL::ray3f ray(
        cuBQL::vec3f(org.x, org.y, org.z),
        cuBQL::vec3f(dir.x, dir.y, dir.z),
        tmin, tmax);

    auto intersect_prim = [&](uint32_t primID) -> float {
        uint32_t geom_idx = prim_to_geom[primID];
        struct geom_gpu_entry ge = geom_entries[geom_idx];

        if (!ge.is_enabled)
            return ray.tMax;

        float t = -1.0f, u = 0.0f, v = 0.0f;
        float Nx = 0.0f, Ny = 0.0f, Nz = 0.0f;
        bool did_hit = false;

        if (primID < tri_count) {
            uint32_t local_id = primID - ge.prim_offset;
            uint3 tri_idx = indices[ge.prim_offset + local_id];
            float3 v0 = vertices[tri_idx.x];
            float3 v1 = vertices[tri_idx.y];
            float3 v2 = vertices[tri_idx.z];

            if (ray_triangle_intersect(org, dir, v0, v1, v2,
                                       tmin, ray.tMax, &t, &u, &v)) {
                float3 e1 = v1 - v0;
                float3 e2 = v2 - v0;
                float3 N = cross(e1, e2);
                if (ge.flip_surface) { N.x = -N.x; N.y = -N.y; N.z = -N.z; }
                Nx = N.x; Ny = N.y; Nz = N.z;
                did_hit = true;
            }
        } else {
            uint32_t sphere_idx = primID - tri_count;
            struct sphere_gpu sp = spheres[sphere_idx];
            float3 N;
            if (ray_sphere_intersect(org, dir, sp.cx, sp.cy, sp.cz,
                                     sp.radius, tmin, ray.tMax, &t, &N)) {
                if (ge.flip_surface) { N.x = -N.x; N.y = -N.y; N.z = -N.z; }
                float2 suv = sphere_normal_to_uv(N);
                u = suv.x; v = suv.y;
                Nx = N.x; Ny = N.y; Nz = N.z;
                did_hit = true;
            }
        }

        if (!did_hit)
            return ray.tMax;

        /* ---- 插入到 Top-K 有序数组 ---- */
        /* 场景: K 很小 (≤8), 用插入排序最优 */

        if (num_hits < K) {
            /* 未满: 直接插入到正确位置 */
            int pos = num_hits;
            while (pos > 0 && slots[pos - 1].distance > t) {
                slots[pos] = slots[pos - 1];
                pos--;
            }
            slots[pos].prim_id   = (int32_t)primID;
            slots[pos].geom_idx  = (int32_t)geom_idx;
            slots[pos].inst_id   = -1;
            slots[pos].distance  = t;
            slots[pos].normal[0] = Nx;
            slots[pos].normal[1] = Ny;
            slots[pos].normal[2] = Nz;
            slots[pos].uv[0]     = u;
            slots[pos].uv[1]     = v;
            num_hits++;

            /* 满了之后收缩 tMax */
            if (num_hits == K)
                ray.tMax = slots[K - 1].distance;

        } else if (t < slots[K - 1].distance) {
            /* 已满且当前 t 更近: 替换最远的并重新排序 */
            int pos = K - 1;
            while (pos > 0 && slots[pos - 1].distance > t) {
                slots[pos] = slots[pos - 1];
                pos--;
            }
            slots[pos].prim_id   = (int32_t)primID;
            slots[pos].geom_idx  = (int32_t)geom_idx;
            slots[pos].inst_id   = -1;
            slots[pos].distance  = t;
            slots[pos].normal[0] = Nx;
            slots[pos].normal[1] = Ny;
            slots[pos].normal[2] = Nz;
            slots[pos].uv[0]     = u;
            slots[pos].uv[1]     = v;

            /* 收缩 tMax 到新的第 K 近 */
            ray.tMax = slots[K - 1].distance;
        }

        return ray.tMax;
    };

    cuBQL::shrinkingRayQuery::forEachPrim(intersect_prim, bvh, ray);

    /* 写出结果 */
    results[tid].count = num_hits;
    for (int i = 0; i < num_hits; i++)
        results[tid].hits[i] = slots[i];
    /* 清零未使用的 slot（可选，防止未定义数据泄露） */
    for (int i = num_hits; i < CUS3D_MAX_MULTI_HITS; i++)
        results[tid].hits[i].prim_id = -1;
}
```

**设计要点**:
1. `slots[]` 数组保持按 `distance` 升序排列（插入排序，K≤8 时零开销）
2. `ray.tMax` 在 buffer 满后收缩到 `slots[K-1].distance`，cuBQL 自动跳过更远的子树
3. 与现有 `trace_rays_kernel` 共享完整的求交逻辑，仅替换"更新最近 hit"为"插入 Top-K"

#### 10.5.5 CPU 端使用方式（s3d_scene_view_trace_ray.cpp）

```cpp
#define TOPK_COUNT 4  /* 通常 4 就够了，自相交避免只需 2 */

res_T
s3d_scene_view_trace_ray
  (struct s3d_scene_view* scnview,
   const float org[3], const float dir[3],
   const float range[2], void* ray_data,
   struct s3d_hit* hit)
{
  struct cus3d_multi_hit_result multi;
  int i;

  /* ... 参数校验同前 ... */

  /* 一次 GPU 调用，获取 Top-K 候选交点 */
  res_T res = cus3d_trace_ray_single_multi(
    scnview->bvh, scnview->geom_store, scnview->scn->dev->gpu,
    org, dir, range, TOPK_COUNT, &multi);
  if (res != RES_OK) return res;

  /* CPU 连续判断：按 distance 升序遍历候选 */
  for (i = 0; i < multi.count; i++) {
    const struct cus3d_hit_result* candidate = &multi.hits[i];

    /* 转换为 s3d_hit */
    cus3d_hit_to_s3d_hit(scnview->geom_store, candidate, hit);
    /* UV 约定转换 ... (同 Phase 3) */

    /* 调用 filter function */
    const struct geom_entry* ge = cus3d_geom_store_get_entry(
      scnview->geom_store, (uint32_t)candidate->geom_idx);

    if (ge && ge->filter_func) {
      int rejected = ge->filter_func(
        hit, org, dir, range, ray_data, ge->filter_data);
      if (rejected)
        continue;  /* 被拒绝，尝试下一个候选 */
    }

    /* 命中被接受 */
    return RES_OK;
  }

  /* 所有 K 个候选都被 filter 拒绝 */
  /* 回退到方案 A: 从第 K 个命中之后继续搜索 */
  if (multi.count == TOPK_COUNT) {
    float fallback_range[2];
    fallback_range[0] = multi.hits[TOPK_COUNT - 1].distance + 1e-6f;
    fallback_range[1] = range[1];
    if (fallback_range[0] < fallback_range[1]) {
      return s3d_scene_view_trace_ray(scnview, org, dir,
                                       fallback_range, ray_data, hit);
    }
  }

  *hit = S3D_HIT_NULL;
  return RES_OK;
}
```

#### 10.5.6 性能分析

| 场景 | 方案 A 开销 | 方案 B(K=4) 开销 | 改善 |
|------|-----------|------------------|------|
| **无 filter** | 1 次 GPU 往返 | 1 次 GPU 往返（K=4 hit buffer 略增寄存器压力） | ≈ 持平 |
| **filter 拒绝 1 次** | 2 次 GPU 往返 | 1 次 GPU 往返 | **~2x** |
| **filter 拒绝 2-3 次** | 3-4 次 GPU 往返 | 1 次 GPU 往返 | **~3-4x** |
| **filter 全部拒绝** | K+1 次 GPU 往返 | 1 次 → 回退方案 A | ≫ 1x |
| **寄存器压力** | 低（1 个 hit_result） | 中（K 个，K=4 → ~160 bytes） | 可接受 |

**关键优势**: 蒙特卡洛路径追踪中自相交避免导致的"拒绝 1 次"是最常见的情况。方案 B 将其从 2 次往返优化到 1 次——这正是最大频率的热路径。

#### 10.5.7 与方案 A 的协同

方案 B 不是替换方案 A，而是**叠加**：
- **正常路径 (>99% 情况)**: K=4 的 Top-K 一次返回，CPU 线性扫描，O(1) 次 GPU 调用
- **极端路径 (~1% 情况)**: K 个候选全部被拒绝 → 回退到方案 A，从 `t_K + ε` 继续搜索

建议的实施顺序：
1. 先实现方案 A（简单，保证正确性）
2. 立即添加 `trace_rays_topk_kernel`（本节代码）
3. 将 `s3d_scene_view_trace_ray` 切换到 Top-K + 方案 A 回退

### 10.6 closest_point 中的 Filter 保留

closest_point 的 filter 调用较简单 — 原始代码在 CPU 端直接调用 `filter->func`，不经过 Embree。

无论使用暴力搜索（Phase 4 策略 A）还是 GPU 实现，filter 逻辑都在 CPU 端完成。暴力搜索版本中，在每个候选最近点处直接调用 `ge->filter_func`，若拒绝则跳过该候选。这与原始实现一致。

### 10.7 `cus3d_trace_ray_single` API 扩展需求

当前签名：
```cpp
res_T cus3d_trace_ray_single(
    struct cus3d_bvh* bvh,
    struct cus3d_geom_store* store,
    struct cus3d_device* dev,
    const float org[3], const float dir[3], const float range[2],
    struct cus3d_hit_result* result);
```

**无需扩展**（方案 A）: `ray_data` 不传入 GPU，仅在 CPU 后过滤时使用。当前 API 足够。

**方案 B（top-K）的新 API 已在 10.5 节详细设计** — 见 `cus3d_trace_ray_single_multi` 及 `trace_rays_topk_kernel`。

### 10.8 结论

| 能力 | 原始 star-3d | 当前 cuBQL 方案 | 差距 | 补救 |
|------|-------------|----------------|------|------|
| 三角形射线追踪 | ✅ | ✅ | 无 | — |
| 球体射线追踪 | ✅ via user geometry | ✅ via GPU kernel | 无 | — |
| **hit filter (trace_ray)** | ✅ Embree 内部调用 | ❌ 未实现 | **严重** | 方案 A: CPU 后过滤重试 |
| **hit filter (closest_point)** | ✅ CPU 直接调用 | ❌ closest_point 未实现 | **严重** | Phase 4 暴力搜索中直接调用 |
| BVH 增量更新 | ✅ 精细 mask（6种属性） | ⚠️ 整体 dirty 标记 | 性能降级（非功能缺失） | 后续可添加增量更新 |
| 最近点查询 | ✅ rtcPointQuery | ❌ 未实现 | **严重** | Phase 4 暴力搜索或新 GPU kernel |
| Instance (两级 BVH) | ✅ | ⚠️ 框架存在但未集成 | 延后 | 后续 Phase |
| 顶点/索引回调 | ✅ | ✅ 无需迁移 | 无 | — |
| Shape detach 通知 | ✅ | ✅ 框架保留 | 清理代码需替换 | Phase 2.5 |

**最终结论**: 当前方案在数据存储层面已正确保留了 filter function，但**运行时调用路径未实现**。本指南 Phase 3 的代码模板中已包含方案 A（CPU 后过滤重试循环），实施时需严格按照该模板实现，否则将损失 star-3d 的核心过滤能力。

---

*文档版本: 1.2 — 附录 A 重写，整合第 10 章审计结论*
*审查基础: custar-3d/0.10/src/ 截至 2026-02-07 全部源文件*
