# 后端抽象层迁移速查表

**快速参考**: Embree → 后端抽象层 API 映射  
**使用场景**: 迁移 star-3d 代码时快速查找替换

---

## 类型替换

```c
RTCDevice                  → s3d_backend_device*
RTCScene                   → s3d_backend_scene*
RTCGeometry                → s3d_backend_geometry*
RTCBuffer                  → s3d_backend_buffer*
RTCRayHit                  → struct s3d_backend_rayhit
RTCRay                     → struct s3d_backend_ray
RTCHit                     → struct s3d_backend_hit
RTCPointQuery              → struct s3d_backend_point_query
RTCBuildQuality            → enum s3d_backend_build_quality
RTCGeometryType            → enum s3d_backend_geometry_type
RTCBufferType              → enum s3d_backend_buffer_type
RTCFormat                  → enum s3d_backend_format
RTCSceneFlags              → enum s3d_backend_scene_flags
RTC_INVALID_GEOMETRY_ID    → S3D_BACKEND_INVALID_GEOM_ID
```

## 设备管理

```c
rtcNewDevice(config)                           → s3d_backend_device_create(config)
rtcReleaseDevice(device)                       → s3d_backend_device_release(device)
rtcSetDeviceErrorFunction(dev, func, udata)    → s3d_backend_device_set_error_function(dev, func, udata)
rtcGetDeviceError(device)                      → s3d_backend_device_get_error(device)
```

## 场景管理

```c
rtcNewScene(device)                       → s3d_backend_scene_create(device)
rtcReleaseScene(scene)                    → s3d_backend_scene_release(scene)
rtcSetSceneFlags(scene, flags)            → s3d_backend_scene_set_flags(scene, flags)
rtcSetSceneBuildQuality(scene, quality)   → s3d_backend_scene_set_build_quality(scene, quality)
rtcCommitScene(scene)                     → s3d_backend_scene_commit(scene)
rtcGetSceneBounds(scene, &bounds)         → s3d_backend_scene_get_bounds(scene, lower, upper)
```

## 几何体管理

```c
rtcNewGeometry(dev, type)                 → s3d_backend_geometry_create(dev, type)
rtcReleaseGeometry(geom)                  → s3d_backend_geometry_release(geom)
rtcSetGeometryBuildQuality(geom, q)       → s3d_backend_geometry_set_build_quality(geom, q)
rtcSetGeometryUserData(geom, data)        → s3d_backend_geometry_set_user_data(geom, data)
rtcGetGeometryUserData(geom)              → s3d_backend_geometry_get_user_data(geom)
rtcAttachGeometry(scene, geom)            → s3d_backend_geometry_attach(scene, geom)
rtcDetachGeometry(scene, id)              → s3d_backend_geometry_detach(scene, id)
rtcEnableGeometry(geom)                   → s3d_backend_geometry_set_enabled(geom, 1)
rtcDisableGeometry(geom)                  → s3d_backend_geometry_set_enabled(geom, 0)
rtcCommitGeometry(geom)                   → s3d_backend_geometry_commit(geom)
```

## 缓冲区管理

```c
rtcNewSharedBuffer(dev, data, size)       → s3d_backend_buffer_create_shared(dev, data, size)
rtcReleaseBuffer(buffer)                  → s3d_backend_buffer_release(buffer)
rtcSetGeometryBuffer(geom, type, ...)     → s3d_backend_geometry_set_buffer(geom, type, ...)
rtcUpdateGeometryBuffer(geom, type, slot) → s3d_backend_geometry_update_buffer(geom, type, slot)
```

## 实例化

```c
rtcSetGeometryInstancedScene(geom, scene) → s3d_backend_geometry_set_instanced_scene(geom, scene)
rtcSetGeometryTransform(geom, t, fmt, xf) → s3d_backend_geometry_set_transform(geom, t, fmt, xf)
```

## 自定义几何体

```c
rtcSetGeometryUserPrimitiveCount(geom, n)      → s3d_backend_geometry_set_user_primitive_count(geom, n)
rtcSetGeometryBoundsFunction(geom, func, data) → s3d_backend_geometry_set_bounds_function(geom, func, data)
rtcSetGeometryIntersectFunction(geom, func)    → s3d_backend_geometry_set_intersect_function(geom, func)
```

## 射线查询

```c
rtcIntersect1(scene, &rayhit, &args)      → s3d_backend_intersect_1(scene, &rayhit)
rtcInitIntersectArguments(&args)          → 不再需要（后端内部处理）
rtcInitRayQueryContext(&ctx)              → s3d_backend_init_ray_query_context(&ctx)
```

## 点查询

```c
rtcInitPointQueryContext(&ctx)                 → s3d_backend_init_point_query_context(&ctx)
rtcPointQuery(scene, &query, &ctx, func, data) → s3d_backend_point_query(scene, &query, &ctx, func, data)
```

## 过滤器函数

```c
rtcSetGeometryIntersectFilterFunction(geom, func) → s3d_backend_geometry_set_intersect_filter_function(geom, func)
```

## 枚举值替换

```c
RTC_BUILD_QUALITY_LOW      → S3D_BACKEND_BUILD_QUALITY_LOW
RTC_BUILD_QUALITY_MEDIUM   → S3D_BACKEND_BUILD_QUALITY_MEDIUM
RTC_BUILD_QUALITY_HIGH     → S3D_BACKEND_BUILD_QUALITY_HIGH

RTC_GEOMETRY_TYPE_TRIANGLE → S3D_BACKEND_GEOMETRY_TYPE_TRIANGLE
RTC_GEOMETRY_TYPE_USER     → S3D_BACKEND_GEOMETRY_TYPE_USER
RTC_GEOMETRY_TYPE_INSTANCE → S3D_BACKEND_GEOMETRY_TYPE_INSTANCE

RTC_BUFFER_TYPE_VERTEX     → S3D_BACKEND_BUFFER_TYPE_VERTEX
RTC_BUFFER_TYPE_INDEX      → S3D_BACKEND_BUFFER_TYPE_INDEX

RTC_FORMAT_FLOAT3          → S3D_BACKEND_FORMAT_FLOAT3
RTC_FORMAT_UINT3           → S3D_BACKEND_FORMAT_UINT3

RTC_SCENE_FLAG_NONE        → S3D_BACKEND_SCENE_FLAG_NONE
RTC_SCENE_FLAG_DYNAMIC     → S3D_BACKEND_SCENE_FLAG_DYNAMIC
```

## 回调函数签名替换

### 相交回调
```c
void func(const struct RTCIntersectFunctionNArguments* args)
```
→
```c
void func(const struct s3d_backend_intersect_function_args* args)
```

### 边界回调
```c
void func(const struct RTCBoundsFunctionArguments* args)
```
→
```c
void func(const struct s3d_backend_bounds_function_args* args)
```

### 过滤器回调
```c
void func(const struct RTCFilterFunctionNArguments* args)
```
→
```c
void func(const struct s3d_backend_filter_function_args* args)
```

### 点查询回调
```c
int func(struct RTCPointQueryFunctionArguments* args)
```
→
```c
int func(const struct s3d_backend_point_query_function_args* args)
```

## 回调参数访问

### Embree辅助函数（删除）
```c
rtc_rayN_get_ray(args->ray, args->N, 0, &ray)      → 直接访问 args->rayhit->ray
rtc_hitN_get_hit(args->hit, args->N, 0, &hit)      → 直接访问 args->rayhit->hit
rtc_rayN_set_ray(args->ray, args->N, 0, &ray)      → 直接修改 args->rayhit->ray
rtc_hitN_set_hit(args->hit, args->N, 0, &hit)      → 直接修改 args->rayhit->hit
```

### 用户数据访问
```c
args->geometryUserPtr  → args->geometry_userdata
args->userPtr          → args->userdata
```

## 字段名映射

### 数据结构字段保持一致，可直接替换变量类型

```c
struct s3d_backend_ray {
    float org_x, org_y, org_z;     /* 与RTCRay一致 */
    float dir_x, dir_y, dir_z;
    float tnear, tfar;
    int mask;
    float time;
    unsigned int id;
};

struct s3d_backend_hit {
    float Ng_x, Ng_y, Ng_z;        /* 与RTCHit一致 */
    float u, v;
    s3d_backend_geom_id geomID;
    unsigned int primID;
    unsigned int instID[1];
};
```

**迁移便利**: 字段名不变，只需修改结构体类型名

---

## 常见代码模式

### 模式1：几何体创建和配置

**迁移前**:
```c
geom->rtc = rtcNewGeometry(dev->rtc, RTC_GEOMETRY_TYPE_TRIANGLE);
rtcSetGeometryBuildQuality(geom->rtc, quality);
rtcSetGeometryUserData(geom->rtc, geom);
geom->rtc_id = rtcAttachGeometry(scene, geom->rtc);
```

**迁移后**:
```c
geom->backend_geom = s3d_backend_geometry_create(dev->backend, S3D_BACKEND_GEOMETRY_TYPE_TRIANGLE);
s3d_backend_geometry_set_build_quality(geom->backend_geom, quality);
s3d_backend_geometry_set_user_data(geom->backend_geom, geom);
geom->backend_geom_id = s3d_backend_geometry_attach(scene, geom->backend_geom);
```

### 模式2：缓冲区设置

**迁移前**:
```c
buf = rtcNewSharedBuffer(dev->rtc, verts, sizeof(float[3])*nverts);
rtcSetGeometryBuffer(geom->rtc, RTC_BUFFER_TYPE_VERTEX, 0,
    RTC_FORMAT_FLOAT3, buf, 0, sizeof(float[3]), nverts);
rtcUpdateGeometryBuffer(geom->rtc, RTC_BUFFER_TYPE_VERTEX, 0);
rtcReleaseBuffer(buf);
```

**迁移后**:
```c
buf = s3d_backend_buffer_create_shared(dev->backend, verts, sizeof(float[3])*nverts);
s3d_backend_geometry_set_buffer(geom->backend_geom, S3D_BACKEND_BUFFER_TYPE_VERTEX, 0,
    S3D_BACKEND_FORMAT_FLOAT3, buf, 0, sizeof(float[3]), nverts);
s3d_backend_geometry_update_buffer(geom->backend_geom, S3D_BACKEND_BUFFER_TYPE_VERTEX, 0);
s3d_backend_buffer_release(buf);
```

### 模式3：场景构建

**迁移前**:
```c
scnview->rtc_scn = rtcNewScene(dev->rtc);
rtcSetSceneFlags(scnview->rtc_scn, rtc_scn_flags);
rtcSetSceneBuildQuality(scnview->rtc_scn, build_quality);
/* ... attach geometries ... */
rtcCommitScene(scnview->rtc_scn);
```

**迁移后**:
```c
scnview->backend_scene = s3d_backend_scene_create(dev->backend);
s3d_backend_scene_set_flags(scnview->backend_scene, scene_flags);
s3d_backend_scene_set_build_quality(scnview->backend_scene, build_quality);
/* ... attach geometries ... */
s3d_backend_scene_commit(scnview->backend_scene);
```

### 模式4：射线查询

**迁移前**:
```c
struct RTCRayHit rayhit;
rayhit.ray.org_x = origin[0];
/* ... */
rtcIntersect1(scnview->rtc_scn, &rayhit, NULL);
if(rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
    /* hit */
}
```

**迁移后**:
```c
struct s3d_backend_rayhit rayhit;
rayhit.ray.org_x = origin[0];
/* ... */
s3d_backend_intersect_1(scnview->backend_scene, &rayhit);
if(rayhit.hit.geomID != S3D_BACKEND_INVALID_GEOM_ID) {
    /* hit */
}
```

### 模式5：几何体查询（内联函数）

**迁移前**:
```c
static FINLINE struct geometry*
scene_view_geometry_from_embree_id(
    struct s3d_scene_view* scnview,
    const unsigned irtc)
{
    struct geometry* geom;
    RTCGeometry rtc_geom;
    ASSERT(scnview && irtc != RTC_INVALID_GEOMETRY_ID);
    rtc_geom = rtcGetGeometry(scnview->rtc_scn, irtc);
    ASSERT(rtc_geom);
    geom = rtcGetGeometryUserData(rtc_geom);
    ASSERT(geom);
    return geom;
}
```

**迁移后**:
```c
static FINLINE struct geometry*
scene_view_geometry_from_backend_id(
    struct s3d_scene_view* scnview,
    const s3d_backend_geom_id geom_id)
{
    struct geometry* geom;
    s3d_backend_geometry* backend_geom;
    ASSERT(scnview && geom_id != S3D_BACKEND_INVALID_GEOM_ID);
    backend_geom = s3d_backend_get_geometry(scnview->backend_scene, geom_id);
    ASSERT(backend_geom);
    geom = s3d_backend_geometry_get_user_data(backend_geom);
    ASSERT(geom);
    return geom;
}
```

---

## 搜索替换命令

### Vim/Neovim

```vim
:%s/RTCDevice/s3d_backend_device*/g
:%s/RTCScene/s3d_backend_scene*/g
:%s/RTCGeometry/s3d_backend_geometry*/g
:%s/rtcNewDevice/s3d_backend_device_create/g
:%s/rtcReleaseDevice/s3d_backend_device_release/g
:%s/RTC_INVALID_GEOMETRY_ID/S3D_BACKEND_INVALID_GEOM_ID/g
```

### sed（批量替换）

```bash
sed -i 's/RTCDevice/s3d_backend_device*/g' s3d_device.c
sed -i 's/rtcNewDevice/s3d_backend_device_create/g' s3d_device.c
sed -i 's/rtcReleaseDevice/s3d_backend_device_release/g' s3d_device.c
```

### 注意事项
⚠️ **不要盲目全局替换！** 
- 某些注释中的Embree API名不应替换
- 字符串常量中的名称不应替换
- 回调函数内部逻辑可能需要手动调整

---

## 验证检查点

### 每个文件迁移后：

```bash
# 1. 编译检查
make s3d_device.o

# 2. 搜索残留的Embree调用
grep -n "rtc[A-Z]" s3d_device.c

# 3. 搜索残留的Embree类型
grep -n "RTC[A-Z]" s3d_device.c

# 4. 运行单元测试
make test_s3d_device
./test_s3d_device

# 5. 检查LSP诊断
# （使用编辑器的LSP功能检查类型错误）
```

---

## 完整文件迁移顺序

| 序号 | 文件 | Embree调用数 | 预计耗时 | 前置依赖 |
|------|------|--------------|----------|----------|
| 1 | `s3d_device_c.h` | 0 (类型定义) | 5分钟 | 无 |
| 2 | `s3d_device.c` | 4 | 30分钟 | #1 |
| 3 | `s3d_scene_view_c.h` | 0 (类型定义) | 10分钟 | 无 |
| 4 | `s3d_scene_view.c` | 44 | 3-4小时 | #3 |
| 5 | `s3d_scene_view_trace_ray.c` | 4 | 1小时 | #3, #4 |
| 6 | `s3d_scene_view_closest_point.c` | 2 | 30分钟 | #3, #4 |
| 7 | `s3d_geometry.c` | 间接使用 | 1小时 | #4 |

**总预计耗时**: 6-8小时实际编码 + 4-6小时测试验证 = 1-2个工作日

---

## 关键技巧

### 技巧1：使用grep快速定位

```bash
# 查找所有需要修改的地方
grep -rn "dev->rtc" src/*.c

# 查找特定API使用
grep -rn "rtcNewGeometry" src/*.c

# 查找类型使用
grep -rn "RTCScene" src/*.{c,h}
```

### 技巧2：分步编译

不要等所有文件改完再编译，每改一个函数就编译测试：

```bash
# 增量编译
make s3d_device.o && echo "OK" || echo "FAIL"
```

### 技巧3：使用git跟踪进度

```bash
# 每完成一个文件就提交
git add s3d_device.c s3d_device_c.h
git commit -m "backend: migrate s3d_device.c"
```

### 技巧4：保留调试版本

```bash
# 如果遇到问题，对比修改前后
diff -u s3d_device.c.orig s3d_device.c
```

---

**文档版本**: 1.0  
**创建日期**: 2026-02-02  
**用途**: 快速参考，实际迁移时查阅
