# Star-3D 后端抽象层设计文档

**创建时间**: 2026-02-02  
**项目**: STARDIS-GPU Embree迁移  
**目标**: 包装Embree API，支持Embree/cuBQL双后端切换  
**影响范围**: `s3d_backend.h`, 新建 `s3d_backend.c`

---

## 1. 执行摘要

当前 `s3d_backend.h` 直接暴露 Embree API（`#include <embree4/rtcore.h>`），所有使用该头文件的代码都直接依赖 Embree 类型和函数。本设计将：

1. **隐藏Embree类型**: 用不透明指针包装所有Embree类型
2. **抽象API接口**: 提供统一的后端函数接口
3. **支持多后端**: 设计为可扩展架构，支持Embree和cuBQL
4. **保持兼容性**: 外部API（`s3d.h`）保持不变

**预期收益**:
- 解耦Embree依赖，支持替代实现
- 简化cuBQL迁移路径
- 保持公共API稳定性
- 支持运行时后端选择（可选）

---

## 2. 当前架构分析

### 2.1 Embree API使用统计

基于对star-3d代码的分析，识别出78个Embree API调用点：

| 文件 | Embree API调用数 | 主要功能 |
|------|------------------|----------|
| `s3d_device.c` | 4 | 设备管理 |
| `s3d_scene_view.c` | 44 | 几何体注册、场景构建 |
| `s3d_scene_view_trace_ray.c` | 4 | 射线查询 |
| `s3d_scene_view_closest_point.c` | 2 | 点查询 |
| `s3d_geometry.c` | 间接使用 | 自定义几何体回调 |

### 2.2 直接暴露的Embree类型

当前 `s3d_backend.h` 暴露的类型：
```c
// 来自 <embree4/rtcore.h>
- RTCDevice          // 设备句柄
- RTCScene           // 场景句柄
- RTCGeometry        // 几何体句柄
- RTCBuffer          // 缓冲区句柄
- RTCRay             // 射线数据结构
- RTCHit             // 命中数据结构
- RTCRayHit          // 射线+命中组合
- RTCPointQuery      // 点查询数据结构
- RTCFilterFunctionNArguments  // 过滤器参数
- 各种枚举类型 (RTCBuildQuality, RTCError, ...)
```

**问题**: 这些类型在整个star-3d代码库中直接使用，导致：
1. 无法替换Embree实现
2. 编译时强依赖Embree头文件
3. 类型泄漏到公共API

---

## 3. 后端抽象层设计

### 3.1 设计原则

1. **不透明指针（Opaque Pointers）**: 隐藏所有后端实现细节
2. **统一接口**: 提供与后端无关的API
3. **最小侵入**: 尽量保持现有代码结构
4. **零开销抽象**: 编译时后端选择，无运行时开销
5. **渐进迁移**: 支持分步骤迁移

### 3.2 架构图

```
┌─────────────────────────────────────────────────────┐
│              公共API (s3d.h)                         │
│  s3d_device, s3d_scene, s3d_scene_view, ...         │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│          内部实现 (s3d_device_c.h, ...)              │
│  struct s3d_device { s3d_backend_device* backend; } │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│       后端抽象层 (s3d_backend.h/c) - 新设计          │
│  s3d_backend_*() 函数族 + 不透明类型                 │
└─────────────────────────────────────────────────────┘
                        ↓
        ┌───────────────┴───────────────┐
        ↓                               ↓
┌──────────────────┐          ┌──────────────────┐
│ Embree 实现       │          │ cuBQL 实现        │
│ (s3d_backend.c)  │          │ (s3d_backend_    │
│                  │          │  cubql.cu)       │
└──────────────────┘          └──────────────────┘
```

### 3.3 类型包装策略

#### 3.3.1 不透明句柄类型

```c
/* s3d_backend.h - 新设计 */

/* 不透明后端类型 - 隐藏实现细节 */
typedef struct s3d_backend_device s3d_backend_device;
typedef struct s3d_backend_scene s3d_backend_scene;
typedef struct s3d_backend_geometry s3d_backend_geometry;
typedef struct s3d_backend_buffer s3d_backend_buffer;

/* 几何体ID类型 */
typedef unsigned int s3d_backend_geom_id;
#define S3D_BACKEND_INVALID_GEOM_ID ((s3d_backend_geom_id)-1)
```

**优势**:
- 完全隐藏后端实现
- 允许不同后端使用不同的内部表示
- 编译时类型安全

#### 3.3.2 数据结构包装

对于需要在API边界传递的数据结构（如射线、命中信息），定义后端无关的版本：

```c
/* s3d_backend.h */

/* 后端无关的射线数据结构 */
struct s3d_backend_ray {
    float org_x, org_y, org_z;    /* 射线起点 */
    float dir_x, dir_y, dir_z;    /* 射线方向 */
    float tnear;                   /* 近裁剪面 */
    float tfar;                    /* 远裁剪面 */
    int mask;                      /* 掩码 */
    float time;                    /* 时间 */
    unsigned int id;               /* 射线ID */
};

/* 后端无关的命中数据结构 */
struct s3d_backend_hit {
    float Ng_x, Ng_y, Ng_z;       /* 几何法线 */
    float u, v;                    /* 重心坐标 */
    s3d_backend_geom_id geomID;   /* 几何体ID */
    unsigned int primID;           /* 图元ID */
    unsigned int instID[1];        /* 实例ID栈 */
};

/* 后端无关的射线+命中组合 */
struct s3d_backend_rayhit {
    struct s3d_backend_ray ray;
    struct s3d_backend_hit hit;
};
```

**设计决策**: 
- 字段名和布局与RTCRay/RTCHit兼容，方便转换
- 使用后端无关的类型（如 `s3d_backend_geom_id`）
- 支持简单的内存拷贝转换（C结构体兼容性）

#### 3.3.3 枚举类型包装

```c
/* s3d_backend.h */

/* 后端无关的构建质量枚举 */
enum s3d_backend_build_quality {
    S3D_BACKEND_BUILD_QUALITY_LOW,
    S3D_BACKEND_BUILD_QUALITY_MEDIUM,
    S3D_BACKEND_BUILD_QUALITY_HIGH
};

/* 后端无关的几何体类型 */
enum s3d_backend_geometry_type {
    S3D_BACKEND_GEOMETRY_TYPE_TRIANGLE,
    S3D_BACKEND_GEOMETRY_TYPE_USER,       /* 自定义几何体 */
    S3D_BACKEND_GEOMETRY_TYPE_INSTANCE
};

/* 后端无关的缓冲区类型 */
enum s3d_backend_buffer_type {
    S3D_BACKEND_BUFFER_TYPE_VERTEX,
    S3D_BACKEND_BUFFER_TYPE_INDEX
};

/* 后端无关的数据格式 */
enum s3d_backend_format {
    S3D_BACKEND_FORMAT_FLOAT3,
    S3D_BACKEND_FORMAT_UINT3,
    S3D_BACKEND_FORMAT_FLOAT4
};

/* 后端无关的场景标志 */
enum s3d_backend_scene_flags {
    S3D_BACKEND_SCENE_FLAG_NONE = 0,
    S3D_BACKEND_SCENE_FLAG_DYNAMIC = (1 << 0),
    S3D_BACKEND_SCENE_FLAG_COMPACT = (1 << 1),
    S3D_BACKEND_SCENE_FLAG_ROBUST = (1 << 2)
};
```

---

## 4. 后端抽象API设计

### 4.1 设备管理API

```c
/* s3d_backend.h */

/**
 * 创建后端设备
 * @param config 配置字符串（可选，传NULL使用默认配置）
 * @return 设备句柄，失败返回NULL
 */
s3d_backend_device* s3d_backend_device_create(const char* config);

/**
 * 释放后端设备
 * @param device 设备句柄
 */
void s3d_backend_device_release(s3d_backend_device* device);

/**
 * 设置设备错误回调
 * @param device 设备句柄
 * @param func 错误回调函数
 * @param userdata 用户数据指针
 */
typedef void (*s3d_backend_error_function)(
    void* userdata,
    int error_code,
    const char* error_message);

void s3d_backend_device_set_error_function(
    s3d_backend_device* device,
    s3d_backend_error_function func,
    void* userdata);

/**
 * 获取设备最后一次错误
 * @param device 设备句柄（NULL检查全局错误）
 * @return 错误码，0表示无错误
 */
int s3d_backend_device_get_error(s3d_backend_device* device);
```

### 4.2 场景管理API

```c
/* s3d_backend.h */

/**
 * 创建后端场景
 * @param device 设备句柄
 * @return 场景句柄，失败返回NULL
 */
s3d_backend_scene* s3d_backend_scene_create(s3d_backend_device* device);

/**
 * 释放后端场景
 * @param scene 场景句柄
 */
void s3d_backend_scene_release(s3d_backend_scene* scene);

/**
 * 设置场景标志
 * @param scene 场景句柄
 * @param flags 标志组合（enum s3d_backend_scene_flags）
 */
void s3d_backend_scene_set_flags(
    s3d_backend_scene* scene,
    enum s3d_backend_scene_flags flags);

/**
 * 设置场景构建质量
 * @param scene 场景句柄
 * @param quality 构建质量
 */
void s3d_backend_scene_set_build_quality(
    s3d_backend_scene* scene,
    enum s3d_backend_build_quality quality);

/**
 * 提交场景（构建加速结构）
 * @param scene 场景句柄
 */
void s3d_backend_scene_commit(s3d_backend_scene* scene);

/**
 * 获取场景边界框
 * @param scene 场景句柄
 * @param bounds_lower 输出下界[3]
 * @param bounds_upper 输出上界[3]
 */
void s3d_backend_scene_get_bounds(
    s3d_backend_scene* scene,
    float bounds_lower[3],
    float bounds_upper[3]);
```

### 4.3 几何体管理API

```c
/* s3d_backend.h */

/**
 * 创建几何体
 * @param device 设备句柄
 * @param type 几何体类型
 * @return 几何体句柄，失败返回NULL
 */
s3d_backend_geometry* s3d_backend_geometry_create(
    s3d_backend_device* device,
    enum s3d_backend_geometry_type type);

/**
 * 释放几何体
 * @param geometry 几何体句柄
 */
void s3d_backend_geometry_release(s3d_backend_geometry* geometry);

/**
 * 设置几何体构建质量
 */
void s3d_backend_geometry_set_build_quality(
    s3d_backend_geometry* geometry,
    enum s3d_backend_build_quality quality);

/**
 * 设置几何体用户数据
 */
void s3d_backend_geometry_set_user_data(
    s3d_backend_geometry* geometry,
    void* userdata);

/**
 * 获取几何体用户数据
 */
void* s3d_backend_geometry_get_user_data(
    s3d_backend_geometry* geometry);

/**
 * 附加几何体到场景
 * @return 几何体ID，失败返回 S3D_BACKEND_INVALID_GEOM_ID
 */
s3d_backend_geom_id s3d_backend_geometry_attach(
    s3d_backend_scene* scene,
    s3d_backend_geometry* geometry);

/**
 * 从场景分离几何体
 */
void s3d_backend_geometry_detach(
    s3d_backend_scene* scene,
    s3d_backend_geom_id geom_id);

/**
 * 启用/禁用几何体
 */
void s3d_backend_geometry_set_enabled(
    s3d_backend_geometry* geometry,
    int enabled);

/**
 * 提交几何体（应用更改）
 */
void s3d_backend_geometry_commit(s3d_backend_geometry* geometry);
```

### 4.4 缓冲区管理API

```c
/* s3d_backend.h */

/**
 * 创建共享缓冲区（包装现有内存）
 * @param device 设备句柄
 * @param data 数据指针（由调用者管理生命周期）
 * @param byte_size 字节大小
 * @return 缓冲区句柄，失败返回NULL
 */
s3d_backend_buffer* s3d_backend_buffer_create_shared(
    s3d_backend_device* device,
    void* data,
    size_t byte_size);

/**
 * 释放缓冲区
 */
void s3d_backend_buffer_release(s3d_backend_buffer* buffer);

/**
 * 设置几何体缓冲区
 * @param geometry 几何体句柄
 * @param buffer_type 缓冲区类型（顶点/索引）
 * @param slot 槽位（通常为0）
 * @param format 数据格式
 * @param buffer 缓冲区句柄
 * @param byte_offset 字节偏移
 * @param byte_stride 字节步长
 * @param item_count 元素数量
 */
void s3d_backend_geometry_set_buffer(
    s3d_backend_geometry* geometry,
    enum s3d_backend_buffer_type buffer_type,
    unsigned int slot,
    enum s3d_backend_format format,
    s3d_backend_buffer* buffer,
    size_t byte_offset,
    size_t byte_stride,
    size_t item_count);

/**
 * 更新几何体缓冲区（通知后端数据已变更）
 */
void s3d_backend_geometry_update_buffer(
    s3d_backend_geometry* geometry,
    enum s3d_backend_buffer_type buffer_type,
    unsigned int slot);
```

### 4.5 实例化API

```c
/* s3d_backend.h */

/**
 * 设置实例化场景
 * @param geometry 几何体句柄（类型必须为INSTANCE）
 * @param instanced_scene 被实例化的场景
 */
void s3d_backend_geometry_set_instanced_scene(
    s3d_backend_geometry* geometry,
    s3d_backend_scene* instanced_scene);

/**
 * 设置实例变换矩阵
 * @param geometry 几何体句柄（类型必须为INSTANCE）
 * @param time_step 时间步（通常为0）
 * @param format 矩阵格式（FLOAT3X4_COLUMN_MAJOR等）
 * @param xform 变换矩阵指针
 */
void s3d_backend_geometry_set_transform(
    s3d_backend_geometry* geometry,
    unsigned int time_step,
    enum s3d_backend_format format,
    const void* xform);
```

### 4.6 自定义几何体API

```c
/* s3d_backend.h */

/* 自定义几何体边界回调 */
typedef void (*s3d_backend_bounds_function)(
    const struct s3d_backend_bounds_function_args* args);

struct s3d_backend_bounds_function_args {
    void* geometry_userdata;
    unsigned int prim_id;
    float time_step;
    /* 输出：边界框 */
    float* bounds_lower;  /* [3] */
    float* bounds_upper;  /* [3] */
};

/* 自定义几何体相交回调 */
typedef void (*s3d_backend_intersect_function)(
    const struct s3d_backend_intersect_function_args* args);

struct s3d_backend_intersect_function_args {
    int valid[1];             /* 输入：有效掩码；输出：命中掩码 */
    void* geometry_userdata;
    unsigned int prim_id;
    /* 输入/输出：射线和命中信息 */
    struct s3d_backend_rayhit* rayhit;
};

/**
 * 设置自定义几何体图元数量
 */
void s3d_backend_geometry_set_user_primitive_count(
    s3d_backend_geometry* geometry,
    unsigned int count);

/**
 * 设置自定义几何体边界回调
 */
void s3d_backend_geometry_set_bounds_function(
    s3d_backend_geometry* geometry,
    s3d_backend_bounds_function func,
    void* userdata);

/**
 * 设置自定义几何体相交回调
 */
void s3d_backend_geometry_set_intersect_function(
    s3d_backend_geometry* geometry,
    s3d_backend_intersect_function func);
```

### 4.7 射线查询API

```c
/* s3d_backend.h */

/**
 * 单射线相交查询
 * @param scene 场景句柄
 * @param rayhit 输入/输出射线+命中信息
 */
void s3d_backend_intersect_1(
    s3d_backend_scene* scene,
    struct s3d_backend_rayhit* rayhit);

/**
 * 初始化射线查询上下文（为过滤器函数准备）
 * @param context 输出上下文指针
 */
struct s3d_backend_ray_query_context;

void s3d_backend_init_ray_query_context(
    struct s3d_backend_ray_query_context* context);
```

### 4.8 点查询API

```c
/* s3d_backend.h */

/* 点查询数据结构 */
struct s3d_backend_point_query {
    float x, y, z;      /* 查询点位置 */
    float radius;       /* 查询半径 */
    float time;         /* 查询时间 */
};

/* 点查询上下文 */
struct s3d_backend_point_query_context;

void s3d_backend_init_point_query_context(
    struct s3d_backend_point_query_context* context);

/* 点查询回调 */
typedef int (*s3d_backend_point_query_function)(
    const struct s3d_backend_point_query_function_args* args);

struct s3d_backend_point_query_function_args {
    void* userdata;
    s3d_backend_geom_id geom_id;
    unsigned int prim_id;
    /* 输入/输出：查询参数 */
    struct s3d_backend_point_query* query;
};

/**
 * 点查询
 * @param scene 场景句柄
 * @param query 查询参数
 * @param context 查询上下文
 * @param func 回调函数
 * @param userdata 用户数据
 */
void s3d_backend_point_query(
    s3d_backend_scene* scene,
    struct s3d_backend_point_query* query,
    struct s3d_backend_point_query_context* context,
    s3d_backend_point_query_function func,
    void* userdata);
```

### 4.9 过滤器函数API

```c
/* s3d_backend.h */

/* 过滤器函数回调 */
typedef void (*s3d_backend_filter_function)(
    const struct s3d_backend_filter_function_args* args);

struct s3d_backend_filter_function_args {
    int valid[1];         /* 输入：有效掩码；输出：命中掩码 */
    void* geometry_userdata;
    void* context;        /* 查询上下文（由用户设置） */
    /* 输入/输出：射线和命中信息 */
    struct s3d_backend_rayhit* rayhit;
};

/**
 * 设置几何体相交过滤器函数
 * @param geometry 几何体句柄
 * @param func 过滤器函数（NULL清除过滤器）
 */
void s3d_backend_geometry_set_intersect_filter_function(
    s3d_backend_geometry* geometry,
    s3d_backend_filter_function func);
```

---

## 5. 实现策略

### 5.1 编译时后端选择

使用预处理器宏选择后端实现：

```c
/* s3d_backend.c */

/* 编译时后端选择 */
#if defined(S3D_BACKEND_EMBREE)
    #define BACKEND_IMPL embree
#elif defined(S3D_BACKEND_CUBQL)
    #define BACKEND_IMPL cubql
#else
    #error "No backend selected. Define S3D_BACKEND_EMBREE or S3D_BACKEND_CUBQL"
#endif

/* 宏拼接辅助 */
#define BACKEND_FUNC_IMPL(name) CONCAT3(s3d_backend_, BACKEND_IMPL, name)
#define CONCAT3(a, b, c) CONCAT3_IMPL(a, b, c)
#define CONCAT3_IMPL(a, b, c) a ## b ## _ ## c

/* 示例：s3d_backend_device_create 映射到 s3d_backend_embree_device_create */
s3d_backend_device* s3d_backend_device_create(const char* config) {
    return BACKEND_FUNC_IMPL(device_create)(config);
}
```

### 5.2 Embree后端实现（s3d_backend.c）

```c
/* s3d_backend.c - Embree实现部分 */

#ifdef S3D_BACKEND_EMBREE

#include "s3d_backend.h"
#include <embree4/rtcore.h>
#include <stdlib.h>
#include <string.h>

/* ========== 类型映射 ========== */

/* 设备结构 */
struct s3d_backend_device {
    RTCDevice rtc_device;
    s3d_backend_error_function error_func;
    void* error_func_userdata;
};

/* 场景结构 */
struct s3d_backend_scene {
    RTCScene rtc_scene;
    s3d_backend_device* device;
};

/* 几何体结构 */
struct s3d_backend_geometry {
    RTCGeometry rtc_geometry;
    s3d_backend_device* device;
};

/* 缓冲区结构 */
struct s3d_backend_buffer {
    RTCBuffer rtc_buffer;
};

/* ========== 设备管理实现 ========== */

/* Embree错误回调包装器 */
static void embree_error_callback(void* userdata, enum RTCError code, const char* msg) {
    s3d_backend_device* device = (s3d_backend_device*)userdata;
    if (device && device->error_func) {
        device->error_func(device->error_func_userdata, (int)code, msg);
    }
}

s3d_backend_device* s3d_backend_embree_device_create(const char* config) {
    s3d_backend_device* device = (s3d_backend_device*)calloc(1, sizeof(s3d_backend_device));
    if (!device) return NULL;
    
    device->rtc_device = rtcNewDevice(config);
    if (!device->rtc_device) {
        free(device);
        return NULL;
    }
    
    /* 设置内部错误回调 */
    rtcSetDeviceErrorFunction(device->rtc_device, embree_error_callback, device);
    
    return device;
}

void s3d_backend_embree_device_release(s3d_backend_device* device) {
    if (!device) return;
    if (device->rtc_device) {
        rtcReleaseDevice(device->rtc_device);
    }
    free(device);
}

void s3d_backend_embree_device_set_error_function(
    s3d_backend_device* device,
    s3d_backend_error_function func,
    void* userdata)
{
    if (!device) return;
    device->error_func = func;
    device->error_func_userdata = userdata;
}

int s3d_backend_embree_device_get_error(s3d_backend_device* device) {
    RTCDevice rtc_dev = device ? device->rtc_device : NULL;
    return (int)rtcGetDeviceError(rtc_dev);
}

/* ========== 场景管理实现 ========== */

s3d_backend_scene* s3d_backend_embree_scene_create(s3d_backend_device* device) {
    if (!device) return NULL;
    
    s3d_backend_scene* scene = (s3d_backend_scene*)calloc(1, sizeof(s3d_backend_scene));
    if (!scene) return NULL;
    
    scene->rtc_scene = rtcNewScene(device->rtc_device);
    if (!scene->rtc_scene) {
        free(scene);
        return NULL;
    }
    scene->device = device;
    
    return scene;
}

void s3d_backend_embree_scene_release(s3d_backend_scene* scene) {
    if (!scene) return;
    if (scene->rtc_scene) {
        rtcReleaseScene(scene->rtc_scene);
    }
    free(scene);
}

/* 枚举映射辅助函数 */
static enum RTCSceneFlags map_scene_flags(enum s3d_backend_scene_flags flags) {
    enum RTCSceneFlags rtc_flags = RTC_SCENE_FLAG_NONE;
    if (flags & S3D_BACKEND_SCENE_FLAG_DYNAMIC)
        rtc_flags |= RTC_SCENE_FLAG_DYNAMIC;
    if (flags & S3D_BACKEND_SCENE_FLAG_COMPACT)
        rtc_flags |= RTC_SCENE_FLAG_COMPACT;
    if (flags & S3D_BACKEND_SCENE_FLAG_ROBUST)
        rtc_flags |= RTC_SCENE_FLAG_ROBUST;
    return rtc_flags;
}

static enum RTCBuildQuality map_build_quality(enum s3d_backend_build_quality quality) {
    switch (quality) {
        case S3D_BACKEND_BUILD_QUALITY_LOW:
            return RTC_BUILD_QUALITY_LOW;
        case S3D_BACKEND_BUILD_QUALITY_MEDIUM:
            return RTC_BUILD_QUALITY_MEDIUM;
        case S3D_BACKEND_BUILD_QUALITY_HIGH:
            return RTC_BUILD_QUALITY_HIGH;
        default:
            return RTC_BUILD_QUALITY_MEDIUM;
    }
}

void s3d_backend_embree_scene_set_flags(
    s3d_backend_scene* scene,
    enum s3d_backend_scene_flags flags)
{
    if (!scene || !scene->rtc_scene) return;
    rtcSetSceneFlags(scene->rtc_scene, map_scene_flags(flags));
}

void s3d_backend_embree_scene_set_build_quality(
    s3d_backend_scene* scene,
    enum s3d_backend_build_quality quality)
{
    if (!scene || !scene->rtc_scene) return;
    rtcSetSceneBuildQuality(scene->rtc_scene, map_build_quality(quality));
}

void s3d_backend_embree_scene_commit(s3d_backend_scene* scene) {
    if (!scene || !scene->rtc_scene) return;
    rtcCommitScene(scene->rtc_scene);
}

void s3d_backend_embree_scene_get_bounds(
    s3d_backend_scene* scene,
    float bounds_lower[3],
    float bounds_upper[3])
{
    if (!scene || !scene->rtc_scene) return;
    
    struct RTCBounds bounds;
    rtcGetSceneBounds(scene->rtc_scene, &bounds);
    
    bounds_lower[0] = bounds.lower_x;
    bounds_lower[1] = bounds.lower_y;
    bounds_lower[2] = bounds.lower_z;
    bounds_upper[0] = bounds.upper_x;
    bounds_upper[1] = bounds.upper_y;
    bounds_upper[2] = bounds.upper_z;
}

/* ========== 几何体管理实现 ========== */

static enum RTCGeometryType map_geometry_type(enum s3d_backend_geometry_type type) {
    switch (type) {
        case S3D_BACKEND_GEOMETRY_TYPE_TRIANGLE:
            return RTC_GEOMETRY_TYPE_TRIANGLE;
        case S3D_BACKEND_GEOMETRY_TYPE_USER:
            return RTC_GEOMETRY_TYPE_USER;
        case S3D_BACKEND_GEOMETRY_TYPE_INSTANCE:
            return RTC_GEOMETRY_TYPE_INSTANCE;
        default:
            return RTC_GEOMETRY_TYPE_TRIANGLE;
    }
}

s3d_backend_geometry* s3d_backend_embree_geometry_create(
    s3d_backend_device* device,
    enum s3d_backend_geometry_type type)
{
    if (!device) return NULL;
    
    s3d_backend_geometry* geometry = (s3d_backend_geometry*)calloc(
        1, sizeof(s3d_backend_geometry));
    if (!geometry) return NULL;
    
    geometry->rtc_geometry = rtcNewGeometry(
        device->rtc_device, map_geometry_type(type));
    if (!geometry->rtc_geometry) {
        free(geometry);
        return NULL;
    }
    geometry->device = device;
    
    return geometry;
}

void s3d_backend_embree_geometry_release(s3d_backend_geometry* geometry) {
    if (!geometry) return;
    if (geometry->rtc_geometry) {
        rtcReleaseGeometry(geometry->rtc_geometry);
    }
    free(geometry);
}

void s3d_backend_embree_geometry_set_build_quality(
    s3d_backend_geometry* geometry,
    enum s3d_backend_build_quality quality)
{
    if (!geometry || !geometry->rtc_geometry) return;
    rtcSetGeometryBuildQuality(geometry->rtc_geometry, map_build_quality(quality));
}

void s3d_backend_embree_geometry_set_user_data(
    s3d_backend_geometry* geometry,
    void* userdata)
{
    if (!geometry || !geometry->rtc_geometry) return;
    rtcSetGeometryUserData(geometry->rtc_geometry, userdata);
}

void* s3d_backend_embree_geometry_get_user_data(s3d_backend_geometry* geometry) {
    if (!geometry || !geometry->rtc_geometry) return NULL;
    return rtcGetGeometryUserData(geometry->rtc_geometry);
}

s3d_backend_geom_id s3d_backend_embree_geometry_attach(
    s3d_backend_scene* scene,
    s3d_backend_geometry* geometry)
{
    if (!scene || !scene->rtc_scene || !geometry || !geometry->rtc_geometry) {
        return S3D_BACKEND_INVALID_GEOM_ID;
    }
    return (s3d_backend_geom_id)rtcAttachGeometry(
        scene->rtc_scene, geometry->rtc_geometry);
}

void s3d_backend_embree_geometry_detach(
    s3d_backend_scene* scene,
    s3d_backend_geom_id geom_id)
{
    if (!scene || !scene->rtc_scene) return;
    rtcDetachGeometry(scene->rtc_scene, (unsigned int)geom_id);
}

void s3d_backend_embree_geometry_set_enabled(
    s3d_backend_geometry* geometry,
    int enabled)
{
    if (!geometry || !geometry->rtc_geometry) return;
    if (enabled) {
        rtcEnableGeometry(geometry->rtc_geometry);
    } else {
        rtcDisableGeometry(geometry->rtc_geometry);
    }
}

void s3d_backend_embree_geometry_commit(s3d_backend_geometry* geometry) {
    if (!geometry || !geometry->rtc_geometry) return;
    rtcCommitGeometry(geometry->rtc_geometry);
}

/* ========== 缓冲区管理实现 ========== */

s3d_backend_buffer* s3d_backend_embree_buffer_create_shared(
    s3d_backend_device* device,
    void* data,
    size_t byte_size)
{
    if (!device || !data) return NULL;
    
    s3d_backend_buffer* buffer = (s3d_backend_buffer*)calloc(
        1, sizeof(s3d_backend_buffer));
    if (!buffer) return NULL;
    
    buffer->rtc_buffer = rtcNewSharedBuffer(
        device->rtc_device, data, byte_size);
    if (!buffer->rtc_buffer) {
        free(buffer);
        return NULL;
    }
    
    return buffer;
}

void s3d_backend_embree_buffer_release(s3d_backend_buffer* buffer) {
    if (!buffer) return;
    if (buffer->rtc_buffer) {
        rtcReleaseBuffer(buffer->rtc_buffer);
    }
    free(buffer);
}

static enum RTCBufferType map_buffer_type(enum s3d_backend_buffer_type type) {
    return (type == S3D_BACKEND_BUFFER_TYPE_VERTEX) ?
        RTC_BUFFER_TYPE_VERTEX : RTC_BUFFER_TYPE_INDEX;
}

static enum RTCFormat map_format(enum s3d_backend_format format) {
    switch (format) {
        case S3D_BACKEND_FORMAT_FLOAT3:
            return RTC_FORMAT_FLOAT3;
        case S3D_BACKEND_FORMAT_UINT3:
            return RTC_FORMAT_UINT3;
        case S3D_BACKEND_FORMAT_FLOAT4:
            return RTC_FORMAT_FLOAT4;
        default:
            return RTC_FORMAT_FLOAT3;
    }
}

void s3d_backend_embree_geometry_set_buffer(
    s3d_backend_geometry* geometry,
    enum s3d_backend_buffer_type buffer_type,
    unsigned int slot,
    enum s3d_backend_format format,
    s3d_backend_buffer* buffer,
    size_t byte_offset,
    size_t byte_stride,
    size_t item_count)
{
    if (!geometry || !geometry->rtc_geometry || !buffer || !buffer->rtc_buffer) return;
    
    rtcSetGeometryBuffer(
        geometry->rtc_geometry,
        map_buffer_type(buffer_type),
        slot,
        map_format(format),
        buffer->rtc_buffer,
        byte_offset,
        byte_stride,
        item_count);
}

void s3d_backend_embree_geometry_update_buffer(
    s3d_backend_geometry* geometry,
    enum s3d_backend_buffer_type buffer_type,
    unsigned int slot)
{
    if (!geometry || !geometry->rtc_geometry) return;
    rtcUpdateGeometryBuffer(
        geometry->rtc_geometry,
        map_buffer_type(buffer_type),
        slot);
}

/* ========== 实例化实现 ========== */

void s3d_backend_embree_geometry_set_instanced_scene(
    s3d_backend_geometry* geometry,
    s3d_backend_scene* instanced_scene)
{
    if (!geometry || !geometry->rtc_geometry || 
        !instanced_scene || !instanced_scene->rtc_scene) return;
    
    rtcSetGeometryInstancedScene(
        geometry->rtc_geometry,
        instanced_scene->rtc_scene);
}

void s3d_backend_embree_geometry_set_transform(
    s3d_backend_geometry* geometry,
    unsigned int time_step,
    enum s3d_backend_format format,
    const void* xform)
{
    if (!geometry || !geometry->rtc_geometry || !xform) return;
    
    /* 简化：假设总是使用FLOAT3X4_COLUMN_MAJOR */
    rtcSetGeometryTransform(
        geometry->rtc_geometry,
        time_step,
        RTC_FORMAT_FLOAT3X4_COLUMN_MAJOR,
        xform);
}

/* ========== 射线查询实现 ========== */

/* 转换函数：s3d_backend_rayhit -> RTCRayHit */
static void convert_rayhit_to_rtc(
    const struct s3d_backend_rayhit* src,
    struct RTCRayHit* dst)
{
    /* 射线部分 */
    dst->ray.org_x = src->ray.org_x;
    dst->ray.org_y = src->ray.org_y;
    dst->ray.org_z = src->ray.org_z;
    dst->ray.dir_x = src->ray.dir_x;
    dst->ray.dir_y = src->ray.dir_y;
    dst->ray.dir_z = src->ray.dir_z;
    dst->ray.tnear = src->ray.tnear;
    dst->ray.tfar = src->ray.tfar;
    dst->ray.mask = src->ray.mask;
    dst->ray.time = src->ray.time;
    dst->ray.id = src->ray.id;
    dst->ray.flags = 0;
    
    /* 命中部分 */
    dst->hit.Ng_x = src->hit.Ng_x;
    dst->hit.Ng_y = src->hit.Ng_y;
    dst->hit.Ng_z = src->hit.Ng_z;
    dst->hit.u = src->hit.u;
    dst->hit.v = src->hit.v;
    dst->hit.geomID = (unsigned int)src->hit.geomID;
    dst->hit.primID = src->hit.primID;
    dst->hit.instID[0] = src->hit.instID[0];
}

/* 转换函数：RTCRayHit -> s3d_backend_rayhit */
static void convert_rtc_to_rayhit(
    const struct RTCRayHit* src,
    struct s3d_backend_rayhit* dst)
{
    /* 射线部分 */
    dst->ray.org_x = src->ray.org_x;
    dst->ray.org_y = src->ray.org_y;
    dst->ray.org_z = src->ray.org_z;
    dst->ray.dir_x = src->ray.dir_x;
    dst->ray.dir_y = src->ray.dir_y;
    dst->ray.dir_z = src->ray.dir_z;
    dst->ray.tnear = src->ray.tnear;
    dst->ray.tfar = src->ray.tfar;
    dst->ray.mask = src->ray.mask;
    dst->ray.time = src->ray.time;
    dst->ray.id = src->ray.id;
    
    /* 命中部分 */
    dst->hit.Ng_x = src->hit.Ng_x;
    dst->hit.Ng_y = src->hit.Ng_y;
    dst->hit.Ng_z = src->hit.Ng_z;
    dst->hit.u = src->hit.u;
    dst->hit.v = src->hit.v;
    dst->hit.geomID = (s3d_backend_geom_id)src->hit.geomID;
    dst->hit.primID = src->hit.primID;
    dst->hit.instID[0] = src->hit.instID[0];
}

void s3d_backend_embree_intersect_1(
    s3d_backend_scene* scene,
    struct s3d_backend_rayhit* rayhit)
{
    if (!scene || !scene->rtc_scene || !rayhit) return;
    
    struct RTCRayHit rtc_rayhit;
    convert_rayhit_to_rtc(rayhit, &rtc_rayhit);
    
    struct RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);
    
    rtcIntersect1(scene->rtc_scene, &rtc_rayhit, &args);
    
    convert_rtc_to_rayhit(&rtc_rayhit, rayhit);
}

/* 其他函数实现省略（点查询、过滤器等）... */

#endif /* S3D_BACKEND_EMBREE */
```

### 5.3 cuBQL后端实现（s3d_backend_cubql.cu）

```cpp
/* s3d_backend_cubql.cu - cuBQL实现部分 */

#ifdef S3D_BACKEND_CUBQL

#include "s3d_backend.h"
#include <cuBQL/bvh.h>
#include <cuda_runtime.h>

/* cuBQL后端实现结构 */
struct s3d_backend_device {
    /* cuBQL不需要显式设备对象，使用CUDA上下文 */
    s3d_backend_error_function error_func;
    void* error_func_userdata;
};

struct s3d_backend_scene {
    cuBQL::BinaryBVH<3, float>* bvh;
    s3d_backend_device* device;
    /* 几何数据存储 */
    float3* d_vertices;
    uint3* d_indices;
    int num_triangles;
};

/* 实现设备管理API */
s3d_backend_device* s3d_backend_cubql_device_create(const char* config) {
    s3d_backend_device* device = new s3d_backend_device();
    /* cuBQL使用当前CUDA上下文 */
    return device;
}

void s3d_backend_cubql_device_release(s3d_backend_device* device) {
    delete device;
}

/* 其他cuBQL函数实现... */

#endif /* S3D_BACKEND_CUBQL */
```

---

## 6. 迁移路径

### 6.1 步骤1：创建后端抽象层

```bash
# 创建新文件
touch stardis-cpu/star-3d/0.10/src/s3d_backend.c

# 修改头文件
vim stardis-cpu/star-3d/0.10/src/s3d_backend.h
```

### 6.2 步骤2：更新内部数据结构

修改 `s3d_device_c.h`:
```c
struct s3d_device {
  int verbose;
  struct logger* logger;
  struct mem_allocator* allocator;

  s3d_backend_device* backend; /* 替换 RTCDevice rtc */

  struct flist_name names;
  ref_T ref;
};
```

修改 `s3d_scene_view_c.h`:
```c
struct s3d_scene_view {
  /* ... */
  
  s3d_backend_scene* backend_scene; /* 替换 RTCScene rtc_scn */
  
  /* ... */
};
```

### 6.3 步骤3：逐文件迁移

**优先级顺序**:
1. `s3d_device.c` - 设备管理（最简单）
2. `s3d_scene_view.c` - 几何体和场景（最复杂，分步骤）
3. `s3d_scene_view_trace_ray.c` - 射线查询
4. `s3d_scene_view_closest_point.c` - 点查询
5. `s3d_geometry.c` - 自定义几何体回调

**迁移模板**:
```c
/* 迁移前 */
rtcNewDevice(NULL);

/* 迁移后 */
s3d_backend_device_create(NULL);
```

### 6.4 步骤4：构建系统集成

修改 `config.mk`:
```makefile
# 后端选择
S3D_BACKEND ?= embree

ifeq ($(S3D_BACKEND),embree)
    CFLAGS += -DS3D_BACKEND_EMBREE
    LDLIBS += -lembree4
else ifeq ($(S3D_BACKEND),cubql)
    CFLAGS += -DS3D_BACKEND_CUBQL
    LDLIBS += -lcubql -lcudart
else
    $(error Invalid backend: $(S3D_BACKEND))
endif
```

---

## 7. 测试验证策略

### 7.1 单元测试

为每个后端函数创建测试：

```c
/* test_s3d_backend_device.c */

#include "s3d_backend.h"
#include <assert.h>

void test_device_create_release() {
    s3d_backend_device* device = s3d_backend_device_create(NULL);
    assert(device != NULL);
    s3d_backend_device_release(device);
}

void test_device_error_function() {
    s3d_backend_device* device = s3d_backend_device_create(NULL);
    /* ... 测试错误回调 ... */
    s3d_backend_device_release(device);
}

int main() {
    test_device_create_release();
    test_device_error_function();
    return 0;
}
```

### 7.2 集成测试

运行现有的star-3d测试套件，确保抽象层不影响功能：

```bash
# 使用Embree后端运行测试
make S3D_BACKEND=embree test

# 使用cuBQL后端运行测试（当实现完成后）
make S3D_BACKEND=cubql test
```

### 7.3 性能基准测试

对比抽象层前后的性能：

```c
/* benchmark_backend_overhead.c */

void benchmark_ray_query() {
    /* 测试射线查询开销 */
    for (int i = 0; i < 100000; i++) {
        s3d_backend_intersect_1(scene, &rayhit);
    }
}
```

---

## 8. 已知限制和未来扩展

### 8.1 当前限制

1. **编译时后端选择**: 无法在运行时切换后端
2. **单后端构建**: 一次只能构建一个后端
3. **部分API覆盖**: 初版可能不包括所有Embree功能

### 8.2 未来扩展

1. **运行时后端选择**: 通过函数指针表实现动态分发
2. **多后端支持**: 同时编译多个后端，运行时选择
3. **后端特性检测**: API查询后端能力
4. **混合后端**: 不同场景使用不同后端

---

## 9. 检查清单

### 实施前检查

- [ ] 理解现有Embree API使用模式
- [ ] 识别所有需要包装的API（78个调用点）
- [ ] 设计完整的后端抽象API
- [ ] 审查设计文档，获得团队批准

### 实施中检查

- [ ] 创建 `s3d_backend.h` 新版本（不透明类型）
- [ ] 实现 `s3d_backend.c` Embree后端
- [ ] 更新内部数据结构（`s3d_device_c.h`等）
- [ ] 迁移 `s3d_device.c`
- [ ] 迁移 `s3d_scene_view.c`（分阶段）
- [ ] 迁移其他文件
- [ ] 更新构建系统

### 实施后检查

- [ ] 所有单元测试通过
- [ ] 集成测试通过
- [ ] 性能基准测试无退化
- [ ] 代码审查通过
- [ ] 文档更新完成

---

## 10. 附录

### 附录A：完整API映射表

| Embree API | 后端抽象API | 说明 |
|-----------|------------|------|
| `rtcNewDevice` | `s3d_backend_device_create` | 设备创建 |
| `rtcReleaseDevice` | `s3d_backend_device_release` | 设备释放 |
| `rtcNewScene` | `s3d_backend_scene_create` | 场景创建 |
| `rtcReleaseScene` | `s3d_backend_scene_release` | 场景释放 |
| `rtcNewGeometry` | `s3d_backend_geometry_create` | 几何体创建 |
| `rtcAttachGeometry` | `s3d_backend_geometry_attach` | 附加几何体 |
| `rtcIntersect1` | `s3d_backend_intersect_1` | 射线查询 |
| `rtcPointQuery` | `s3d_backend_point_query` | 点查询 |
| ... | ... | ... |

（完整映射表见附录文件）

### 附录B：文件修改清单

| 文件 | 修改类型 | 说明 |
|-----|---------|------|
| `s3d_backend.h` | **重写** | 新的抽象接口 |
| `s3d_backend.c` | **新建** | Embree实现 |
| `s3d_backend_cubql.cu` | **新建** | cuBQL实现（未来） |
| `s3d_device_c.h` | **修改** | 替换RTCDevice |
| `s3d_device.c` | **修改** | 使用抽象API |
| `s3d_scene_view_c.h` | **修改** | 替换RTCScene |
| `s3d_scene_view.c` | **修改** | 使用抽象API |
| `s3d_scene_view_trace_ray.c` | **修改** | 使用抽象API |
| `s3d_scene_view_closest_point.c` | **修改** | 使用抽象API |
| `s3d_geometry.c` | **修改** | 使用抽象API |
| `config.mk` | **修改** | 后端选择逻辑 |
| `Makefile` | **修改** | 编译s3d_backend.c |

---

**文档版本**: 1.0  
**创建日期**: 2026-02-02  
**状态**: 设计完成，待审查和实施

**下一步行动**:
1. 团队审查本设计文档
2. 创建 `s3d_backend.c` 骨架实现
3. 开始迁移 `s3d_device.c`（最简单的文件）
4. 逐步迁移其他文件
