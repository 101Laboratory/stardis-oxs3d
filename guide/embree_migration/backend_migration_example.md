# 后端抽象层迁移实例

**日期**: 2026-02-02  
**示例文件**: `s3d_device.c`  
**难度级别**: ★☆☆☆☆ (最简单)  
**预计耗时**: 30分钟

---

## 示例文件：s3d_device.c 完整迁移

### 迁移前代码

```c
#include "s3d_device_c.h"
#include "s3d_backend.h"

static INLINE const char*
rtc_error_string(const enum RTCError err)
{
    /* ... */
}

static void
rtc_error_func(void* uptr, enum RTCError code, const char* str)
{
    /* ... */
}

static void
device_release(struct s3d_device* dev)
{
    ASSERT(dev && dev->ref == 0);

    rtcReleaseDevice(dev->rtc);  /* <- Embree调用 */

    /* ... */
}

res_T
s3d_device_create(struct s3d_device** dev_ptr, ...)
{
    /* ... */
    
    dev->rtc = rtcNewDevice(embree_opts);  /* <- Embree调用 */
    
    const enum RTCError err = rtcGetDeviceError(NULL);  /* <- Embree调用 */
    if(err != RTC_NO_ERROR) {
        /* ... */
    }
    
    rtcSetDeviceErrorFunction(dev->rtc, rtc_error_func, dev);  /* <- Embree调用 */
    
    /* ... */
}
```

### 迁移后代码

```c
#include "s3d_device_c.h"
#include "s3d_backend.h"

static INLINE const char*
backend_error_string(const int err)
{
    /* 保持原有错误处理逻辑，但使用后端无关的错误码 */
    /* 错误码映射可由后端内部处理 */
}

static void
backend_error_func(void* uptr, int code, const char* str)
{
    struct s3d_device* dev = (struct s3d_device*)uptr;
    
    if(dev->verbose) {
        log_msg(dev->logger, LOG_ERROR, "%s: %s\n",
                backend_error_string(code), str ? str : "");
    }
}

static void
device_release(struct s3d_device* dev)
{
    ASSERT(dev && dev->ref == 0);

    s3d_backend_device_release(dev->backend);  /* <- 使用抽象API */

    /* ... */
}

res_T
s3d_device_create(struct s3d_device** dev_ptr, ...)
{
    /* ... */
    
    dev->backend = s3d_backend_device_create(embree_opts);  /* <- 使用抽象API */
    
    const int err = s3d_backend_device_get_error(NULL);  /* <- 使用抽象API */
    if(err != 0) {  /* 0 = 无错误 */
        /* ... */
    }
    
    s3d_backend_device_set_error_function(  /* <- 使用抽象API */
        dev->backend, backend_error_func, dev);
    
    /* ... */
}
```

### 关键修改点总结

| 修改点 | 迁移前 | 迁移后 |
|--------|--------|--------|
| **头文件** | `#include <embree4/rtcore.h>` | `#include "s3d_backend.h"` |
| **设备类型** | `RTCDevice rtc` | `s3d_backend_device* backend` |
| **创建设备** | `rtcNewDevice(opts)` | `s3d_backend_device_create(opts)` |
| **释放设备** | `rtcReleaseDevice(dev->rtc)` | `s3d_backend_device_release(dev->backend)` |
| **错误查询** | `rtcGetDeviceError(NULL)` | `s3d_backend_device_get_error(NULL)` |
| **错误回调** | `rtcSetDeviceErrorFunction(...)` | `s3d_backend_device_set_error_function(...)` |
| **错误类型** | `enum RTCError` | `int` |

**代码行数变化**: 0行（仅API替换，逻辑不变）

---

## 通用迁移模式

### 模式1：设备句柄替换

```c
struct RTCDevice rtc;
```
→
```c
s3d_backend_device* backend;
```

### 模式2：场景句柄替换

```c
RTCScene rtc_scn;
```
→
```c
s3d_backend_scene* backend_scene;
```

### 模式3：几何体句柄替换

```c
RTCGeometry rtc;
```
→
```c
s3d_backend_geometry* backend_geom;
```

### 模式4：枚举类型替换

```c
enum RTCBuildQuality rtc_scn_build_quality;
```
→
```c
enum s3d_backend_build_quality build_quality;
```

### 模式5：函数调用替换

```c
rtcNewGeometry(dev->rtc, RTC_GEOMETRY_TYPE_TRIANGLE)
```
→
```c
s3d_backend_geometry_create(dev->backend, S3D_BACKEND_GEOMETRY_TYPE_TRIANGLE)
```

### 模式6：数据结构转换

```c
struct RTCRayHit rayhit;
rayhit.ray.org_x = origin[0];
/* ... */
rtcIntersect1(scene, &rayhit, NULL);
```
→
```c
struct s3d_backend_rayhit rayhit;
rayhit.ray.org_x = origin[0];
/* ... */
s3d_backend_intersect_1(scene, &rayhit);
```

### 模式7：Invalid ID常量

```c
geom_id != RTC_INVALID_GEOMETRY_ID
```
→
```c
geom_id != S3D_BACKEND_INVALID_GEOM_ID
```

---

## 复杂场景：几何体注册迁移

### 原始代码（s3d_scene_view.c:188-220）

```c
static INLINE res_T
embree_geometry_register(
    struct s3d_scene_view* scnview,
    const enum RTCBuildQuality rtc_build_quality,
    struct geometry* geom)
{
    struct s3d_device* dev = scnview->scn->dev;
    RTCScene scene = scnview->rtc_scn;  /* <- Embree类型 */
    
    switch(geom->type) {
    case GEOM_MESH:
        geom->rtc = rtcNewGeometry(dev->rtc, RTC_GEOMETRY_TYPE_TRIANGLE);  /* <- Embree调用 */
        rtcSetGeometryBuildQuality(geom->rtc, rtc_build_quality);  /* <- Embree调用 */
        break;
        
    case GEOM_INSTANCE:
        geom->rtc = rtcNewGeometry(dev->rtc, RTC_GEOMETRY_TYPE_INSTANCE);  /* <- Embree调用 */
        rtcSetGeometryInstancedScene(geom->rtc,  /* <- Embree调用 */
            geom->data.instance->scnview->rtc_scn);
        break;
        
    case GEOM_SPHERE:
        geom->rtc = rtcNewGeometry(dev->rtc, RTC_GEOMETRY_TYPE_USER);  /* <- Embree调用 */
        rtcSetGeometryUserPrimitiveCount(geom->rtc, 1);  /* <- Embree调用 */
        rtcSetGeometryBoundsFunction(geom->rtc,  /* <- Embree调用 */
            geometry_rtc_sphere_bounds, NULL);
        rtcSetGeometryIntersectFunction(geom->rtc,  /* <- Embree调用 */
            geometry_rtc_sphere_intersect);
        rtcSetGeometryBuildQuality(geom->rtc, rtc_build_quality);  /* <- Embree调用 */
        break;
    }
    
    rtcSetGeometryUserData(geom->rtc, geom);  /* <- Embree调用 */
    geom->rtc_id = rtcAttachGeometry(scene, geom->rtc);  /* <- Embree调用 */
    
    return RES_OK;
}
```

### 迁移后代码

```c
static INLINE res_T
embree_geometry_register(
    struct s3d_scene_view* scnview,
    const enum s3d_backend_build_quality build_quality,  /* <- 后端枚举 */
    struct geometry* geom)
{
    struct s3d_device* dev = scnview->scn->dev;
    s3d_backend_scene* scene = scnview->backend_scene;  /* <- 后端类型 */
    
    switch(geom->type) {
    case GEOM_MESH:
        geom->backend_geom = s3d_backend_geometry_create(  /* <- 后端API */
            dev->backend, S3D_BACKEND_GEOMETRY_TYPE_TRIANGLE);
        s3d_backend_geometry_set_build_quality(  /* <- 后端API */
            geom->backend_geom, build_quality);
        break;
        
    case GEOM_INSTANCE:
        geom->backend_geom = s3d_backend_geometry_create(  /* <- 后端API */
            dev->backend, S3D_BACKEND_GEOMETRY_TYPE_INSTANCE);
        s3d_backend_geometry_set_instanced_scene(  /* <- 后端API */
            geom->backend_geom,
            geom->data.instance->scnview->backend_scene);
        break;
        
    case GEOM_SPHERE:
        geom->backend_geom = s3d_backend_geometry_create(  /* <- 后端API */
            dev->backend, S3D_BACKEND_GEOMETRY_TYPE_USER);
        s3d_backend_geometry_set_user_primitive_count(  /* <- 后端API */
            geom->backend_geom, 1);
        s3d_backend_geometry_set_bounds_function(  /* <- 后端API */
            geom->backend_geom,
            geometry_backend_sphere_bounds, NULL);
        s3d_backend_geometry_set_intersect_function(  /* <- 后端API */
            geom->backend_geom,
            geometry_backend_sphere_intersect);
        s3d_backend_geometry_set_build_quality(  /* <- 后端API */
            geom->backend_geom, build_quality);
        break;
    }
    
    s3d_backend_geometry_set_user_data(  /* <- 后端API */
        geom->backend_geom, geom);
    geom->backend_geom_id = s3d_backend_geometry_attach(  /* <- 后端API */
        scene, geom->backend_geom);
    
    return RES_OK;
}
```

**关键修改点**:
1. 参数类型: `RTCBuildQuality` → `s3d_backend_build_quality`
2. 场景变量: `RTCScene` → `s3d_backend_scene*`
3. 几何体字段: `geom->rtc` → `geom->backend_geom`
4. 设备字段: `dev->rtc` → `dev->backend`
5. 所有函数调用: `rtc*()` → `s3d_backend_*()`
6. 回调函数名: `geometry_rtc_*` → `geometry_backend_*`（需要同步修改回调函数）

---

## 回调函数迁移示例

### 自定义几何体相交回调

**原始代码** (`s3d_geometry.c`):
```c
static FINLINE void
geometry_rtc_sphere_intersect(const struct RTCIntersectFunctionNArguments* args)
{
    struct RTCRayHit ray_hit;
    struct RTCHit hit;
    struct RTCRay ray;
    
    rtc_rayN_get_ray(args->ray, args->N, 0, &ray);  /* Embree辅助函数 */
    /* ... 球体相交计算 ... */
    rtc_hitN_set_hit(args->hit, args->N, 0, &hit);  /* Embree辅助函数 */
}
```

**迁移后代码**:
```c
static FINLINE void
geometry_backend_sphere_intersect(
    const struct s3d_backend_intersect_function_args* args)
{
    struct s3d_backend_rayhit* rayhit = args->rayhit;
    
    /* 直接访问rayhit结构，无需辅助函数 */
    float org_x = rayhit->ray.org_x;
    float org_y = rayhit->ray.org_y;
    /* ... 球体相交计算 ... */
    
    rayhit->hit.geomID = args->geom_id;
    rayhit->hit.primID = args->prim_id;
    /* ... */
}
```

**简化点**:
- 无需使用 `rtc_rayN_get_ray` 等辅助函数
- 直接访问数据结构字段
- 参数结构更简洁

---

## 过滤器函数迁移示例

### 原始代码（s3d_scene_view_trace_ray.c）

```c
void
rtc_hit_filter_wrapper(const struct RTCFilterFunctionNArguments* args)
{
    struct RTCRayHit ray_hit;
    struct intersect_context* ctx;
    struct geometry* geom;
    struct hit_filter* filter;
    struct s3d_hit hit;
    
    rtc_rayN_get_ray(args->ray, args->N, 0, &ray_hit.ray);  /* Embree辅助 */
    rtc_hitN_get_hit(args->hit, args->N, 0, &ray_hit.hit);  /* Embree辅助 */
    
    ctx = CONTAINER_OF(args->context, struct intersect_context, rtc);
    geom = (struct geometry*)args->geometryUserPtr;
    
    /* ... 调用用户过滤器 ... */
    
    if(is_hit_filtered) {
        args->valid[0] = 0;
    }
}
```

### 迁移后代码

```c
void
backend_hit_filter_wrapper(
    const struct s3d_backend_filter_function_args* args)
{
    struct intersect_context* ctx;
    struct geometry* geom;
    struct hit_filter* filter;
    struct s3d_hit hit;
    
    ctx = (struct intersect_context*)args->context;
    geom = (struct geometry*)args->geometry_userdata;
    
    /* 直接访问rayhit数据 */
    struct s3d_backend_rayhit* rayhit = args->rayhit;
    
    /* ... 调用用户过滤器 ... */
    
    if(is_hit_filtered) {
        args->valid[0] = 0;
    }
}
```

---

## 数据结构迁移示例

### s3d_device_c.h 修改

**迁移前**:
```c
#include "s3d_backend.h"

struct s3d_device {
  int verbose;
  struct logger* logger;
  struct mem_allocator* allocator;

  RTCDevice rtc;  /* <- Embree类型直接暴露 */

  struct flist_name names;
  ref_T ref;
};
```

**迁移后**:
```c
#include "s3d_backend.h"

struct s3d_device {
  int verbose;
  struct logger* logger;
  struct mem_allocator* allocator;

  s3d_backend_device* backend;  /* <- 后端不透明指针 */

  struct flist_name names;
  ref_T ref;
};
```

### s3d_scene_view_c.h 修改

**迁移前**:
```c
struct s3d_scene_view {
  /* ... */
  
  int rtc_scn_flags;
  enum RTCBuildQuality rtc_scn_build_quality;  /* <- Embree枚举 */
  RTCScene rtc_scn;  /* <- Embree类型 */
  
  /* ... */
};
```

**迁移后**:
```c
struct s3d_scene_view {
  /* ... */
  
  int backend_scene_flags;
  enum s3d_backend_build_quality build_quality;  /* <- 后端枚举 */
  s3d_backend_scene* backend_scene;  /* <- 后端不透明指针 */
  
  /* ... */
};
```

---

## 迁移检查清单

### 对于每个文件：

1. **查找所有Embree API调用**
   ```bash
   grep -n "rtc[A-Z]" s3d_device.c
   ```

2. **识别调用模式**
   - 设备管理？场景构建？射线查询？
   - 参考 `embree_api_usage_patterns_analysis.md`

3. **替换API调用**
   - 使用API映射表（见实施总结文档）
   - 逐个替换，保持逻辑不变

4. **编译测试**
   ```bash
   make s3d_device.o
   ```

5. **单元测试**
   ```bash
   make test_s3d_device
   ./test_s3d_device
   ```

6. **提交修改**（仅当测试全部通过）
   ```bash
   git add s3d_device.c s3d_device_c.h
   git commit -m "backend: migrate s3d_device to backend abstraction"
   ```

---

## 常见问题和解决方案

### Q1: 编译错误 "unknown type RTCDevice"
**原因**: 忘记更新数据结构定义  
**解决**: 检查 `*_c.h` 文件，将所有Embree类型替换为后端类型

### Q2: 链接错误 "undefined reference to rtcNewDevice"
**原因**: 代码中仍有直接Embree调用  
**解决**: 使用 `grep -r "rtc[A-Z]" *.c` 查找遗漏的调用

### Q3: 测试失败，射线命中结果不一致
**原因**: 数据结构转换错误  
**解决**: 检查 `s3d_backend.c` 中的转换函数，确保字段一一对应

### Q4: 过滤器函数不工作
**原因**: 回调参数结构不匹配  
**解决**: 检查 `s3d_backend_filter_function_args` 定义与使用

### Q5: 自定义几何体（球体）不显示
**原因**: 回调函数签名未更新  
**解决**: 同步更新 `geometry_rtc_*` → `geometry_backend_*`

---

## 预期结果

### 功能性
- ✅ 所有现有测试通过
- ✅ 射线追踪结果与Embree一致
- ✅ 点查询结果与Embree一致
- ✅ 过滤器功能正常

### 性能
- ✅ 无性能退化（零开销抽象）
- ✅ 内联函数展开
- ✅ 编译器优化不受影响

### 可维护性
- ✅ Embree依赖完全隐藏
- ✅ 支持未来cuBQL迁移
- ✅ 代码结构更清晰

---

**文档版本**: 1.0  
**创建日期**: 2026-02-02  
**适用范围**: Star-3D库所有文件迁移
