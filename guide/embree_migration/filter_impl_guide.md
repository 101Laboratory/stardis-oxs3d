# 过滤器函数(Filter Function) GPU迁移实现指南

**生成时间**: 2026-01-30  
**分析项目**: stardis-cpu/star-3d  
**目的**: 指导Embree过滤器函数到cuBQL GPU实现的完整迁移  
**技术挑战**: CPU回调函数在GPU端无法直接执行

---

## 执行摘要

过滤器函数是star-3d的关键特性，允许用户在光线追踪过程中基于自定义条件过滤命中结果。当前Embree实现采用**CPU回调机制**，这在GPU迁移中面临**根本性技术挑战**。本指南提供完整的迁移方案，包括架构设计、API映射和实现细节。

### 核心迁移策略：**条件过滤GPU内核化**
- 将CPU回调转换为GPU端的**可编程条件判断**
- 通过**参数化过滤规则**替代任意用户代码
- 保持API兼容性的同时实现GPU优化

---

## 1. 当前CPU实现架构分析

### 1.1 过滤器函数调用链

```
用户代码 → s3d_mesh_set_hit_filter_function()
         → struct hit_filter { func, data }
         → embree_geometry_setup_filter_function()
         → rtcSetGeometryIntersectFilterFunction()
         → rtcIntersect1() → rtc_hit_filter_wrapper()
         → 用户filter_func() → 返回过滤决策
```

### 1.2 关键数据结构

#### 1.2.1 过滤器函数签名 (`s3d.h`)
```c
typedef int (*s3d_hit_filter_function_T)(
    const struct s3d_hit* hit,      // 命中信息
    const float org[3],             // 射线起点
    const float dir[3],             // 射线方向（从org到hit）
    const float range[2],           // 提交的射线范围
    void* query_data,               // 查询时提交的用户数据
    void* filter_data               // 过滤器设置时定义的数据
);
```

**返回值语义**：
- `0` → 不丢弃命中（接受）
- `非0` → 丢弃命中（过滤掉）

#### 1.2.2 过滤器数据结构 (`s3d_c.h`)
```c
struct hit_filter {
    s3d_hit_filter_function_T func;  // 函数指针
    void* data;                      // 用户自定义数据
};
```

### 1.3 Embree包装器实现 (`s3d_scene_view_trace_ray.c`)

#### 关键函数：`rtc_hit_filter_wrapper()`
```c
void rtc_hit_filter_wrapper(const struct RTCFilterFunctionNArguments* args)
{
    // 1. 从Embree参数提取射线/命中数据
    struct RTCRayHit ray_hit;
    rtc_rayN_get_ray(args->ray, args->N, 0, &ray_hit.ray);
    rtc_hitN_get_hit(args->hit, args->N, 0, &ray_hit.hit);
    
    // 2. 获取上下文和几何体
    struct intersect_context* ctx = CONTAINER_OF(args->context, ...);
    struct geometry* geom = args->geometryUserPtr;
    
    // 3. 获取对应的过滤器
    struct hit_filter* filter;
    switch(geom->type) {
        case GEOM_MESH:   filter = &geom->data.mesh->filter; break;
        case GEOM_SPHERE: filter = &geom->data.sphere->filter; break;
    }
    
    // 4. 设置s3d_hit结构
    struct s3d_hit hit;
    hit_setup(ctx->scnview, &ray_hit, &hit);
    
    // 5. 调用用户过滤器函数
    int is_hit_filtered = filter->func(
        &hit, ctx->ws_org, ctx->ws_dir, ctx->ws_range, 
        ctx->data, filter->data
    );
    
    // 6. 根据结果设置valid标志
    if(is_hit_filtered) {
        args->valid[0] = 0;  // 标记为无效命中
    }
}
```

### 1.4 过滤器设置流程 (`s3d_scene_view.c`)

```c
static INLINE void embree_geometry_setup_filter_function(
    struct s3d_scene_view* scnview, struct geometry* geom)
{
    if(!geom->data.mesh->filter.func) {
        // 没有用户过滤器：设置NULL清除Embree过滤器
        rtcSetGeometryIntersectFilterFunction(geom->rtc, NULL);
    } else {
        // 有用户过滤器：设置包装器函数
        rtcSetGeometryIntersectFilterFunction(geom->rtc, rtc_hit_filter_wrapper);
    }
}
```

### 1.5 用户API接口

#### 网格过滤器设置 (`s3d_shape.c`)
```c
res_T s3d_mesh_set_hit_filter_function(
    struct s3d_shape* shape,
    s3d_hit_filter_function_T func,
    void* data)
{
    if(!shape || shape->type != GEOM_MESH) return RES_BAD_ARG;
    shape->data.mesh->filter.func = func;
    shape->data.mesh->filter.data = data;
    return RES_OK;
}
```

#### 球体过滤器设置 (类似实现)

---

## 2. GPU迁移技术挑战

### 2.1 根本性限制

| 限制 | 影响 | 解决方案方向 |
|------|------|--------------|
| **CPU回调无法在GPU执行** | 用户函数指针无法传输到GPU内核 | 转换为参数化条件判断 |
| **动态代码执行** | GPU需要预编译内核，不能动态加载用户代码 | 提供可配置的过滤规则集 |
| **任意用户数据** | `void* data` 需要确定的内存布局 | 序列化/描述符化用户数据 |
| **复杂控制流** | GPU SIMD架构不擅长复杂分支 | 简化过滤逻辑，减少分支 |

### 2.2 具体技术难点

1. **函数指针序列化**：如何将任意C函数转换为GPU可执行的表示
2. **用户数据传递**：`filter_data`和`query_data`的GPU内存管理
3. **状态维护**：过滤器可能需要的状态（计数器、历史记录等）
4. **性能影响**：每个命中点的过滤调用可能成为性能瓶颈

---

## 3. 推荐迁移策略

### 3.1 策略选择：**参数化条件过滤**

**核心思想**：将任意用户函数转换为预定义的过滤规则+参数

```
CPU模型：用户代码 → 函数指针 → 动态执行
GPU模型：过滤规则ID + 参数数据 → 预编译内核分支 → 静态执行
```

### 3.2 三级迁移方案

#### 级别1：基础条件过滤（必须实现）
- 支持简单数学条件（距离、法向、primID等）
- 固定参数，无动态数据

#### 级别2：可配置规则过滤（推荐实现）
- 支持常见过滤模式（材质ID、区域掩码等）
- 参数化规则，可组合

#### 级别3：有限脚本过滤（可选实现）
- 小型DSL描述过滤逻辑
- JIT编译为GPU代码

### 3.3 建议实施方案

**优先实现级别2**，提供：
1. 预定义过滤规则枚举
2. 规则参数数据结构
3. GPU端规则评估内核

---

## 4. GPU过滤器架构设计

### 4.1 新的过滤器数据结构

```c
// GPU兼容的过滤器描述符
struct gpu_filter_descriptor {
    FilterRuleType rule_type;      // 规则类型枚举
    union {
        struct {
            float min_distance;
            float max_distance;
        } distance_rule;
        
        struct {
            float3 normal_mask;     // 法向过滤条件
            float dot_threshold;    // 点积阈值
        } normal_rule;
        
        struct {
            uint32_t prim_id_mask;  // primID掩码
            uint32_t geom_id_mask;  // geomID掩码
        } id_rule;
        
        struct {
            uint32_t material_id;   // 材质ID
            uint32_t layer_mask;    // 层掩码
        } material_rule;
    } params;
    
    void* user_data_ptr;           // GPU内存中的用户数据指针
    size_t user_data_size;         // 用户数据大小
};
```

### 4.2 过滤规则类型枚举

```c
enum FilterRuleType {
    FILTER_RULE_NONE = 0,          // 不过滤
    FILTER_RULE_DISTANCE,          // 距离过滤
    FILTER_RULE_NORMAL,            // 法向过滤
    FILTER_RULE_PRIM_ID,           // 图元ID过滤
    FILTER_RULE_GEOM_ID,           // 几何体ID过滤
    FILTER_RULE_MATERIAL,          // 材质过滤
    FILTER_RULE_COMPOSITE_AND,     // 逻辑与组合
    FILTER_RULE_COMPOSITE_OR,      // 逻辑或组合
    FILTER_RULE_CUSTOM_CALLBACK,   // CPU回调（仅限CPU回退）
};
```

### 4.3 GPU过滤器评估内核

```cuda
__device__ bool evaluate_filter(
    const gpu_filter_descriptor* filter,
    const HitResultGPU* hit,
    const float3& ray_org,
    const float3& ray_dir,
    const float2& ray_range,
    const void* query_data)
{
    switch(filter->rule_type) {
        case FILTER_RULE_DISTANCE:
            return (hit->distance >= filter->params.distance_rule.min_distance &&
                    hit->distance <= filter->params.distance_rule.max_distance);
                    
        case FILTER_RULE_NORMAL:
            float3 normal = hit->normal;
            float dot_value = dot(normal, filter->params.normal_rule.normal_mask);
            return (dot_value >= filter->params.normal_rule.dot_threshold);
            
        case FILTER_RULE_PRIM_ID:
            return ((hit->primID & filter->params.id_rule.prim_id_mask) != 0);
            
        case FILTER_RULE_COMPOSITE_AND:
            // 递归评估子规则
            bool result = true;
            for(int i = 0; i < filter->composite_rule_count; i++) {
                result = result && evaluate_filter(
                    filter->composite_rules[i], hit, ray_org, ray_dir, ray_range, query_data);
            }
            return result;
            
        default:
            return true; // 默认不过滤
    }
}
```

---

## 5. 实施路线图

### 阶段1：基础实现（2-3周）
1. 定义GPU过滤器API头文件
2. 实现距离、法向、ID过滤规则
3. 创建CUDA评估内核
4. 基本测试验证

### 阶段2：高级功能（3-4周）
1. 组合过滤器支持（AND/OR逻辑）
2. 用户数据序列化系统
3. CPU回退机制
4. 性能优化

### 阶段3：优化完善（2-3周）
1. 内存访问优化
2. 内核调度优化
3. 复杂场景测试
4. 性能基准测试

### 阶段4：集成部署（1-2周）
1. 集成到stardis-gpu主项目
2. 文档完善
3. 生产环境测试

---

## 6. 测试验证策略

### 6.1 单元测试
- 验证每个过滤规则类型
- 测试组合过滤器逻辑
- 验证GPU-CPU结果一致性

### 6.2 性能测试
- 基准测试：无过滤 vs 各种过滤规则
- 内存带宽分析
- 内核执行时间测量

### 6.3 集成测试
- 与完整stardis求解器集成
- 复杂场景过滤器应用
- 长期稳定性测试

---

## 7. 结论

过滤器函数GPU迁移是Embree到cuBQL迁移中的关键技术挑战。通过**参数化规则系统**替代**CPU回调机制**，可以在保持功能性的同时实现GPU加速。推荐的迁移方案采用渐进式实施策略，从基础过滤规则开始，逐步扩展支持更复杂的过滤逻辑。

**关键成功因素**：
1. 清晰的规则定义和API设计
2. 高效的GPU内存管理
3. 完备的测试验证策略
4. 渐进式迁移路径

**建议行动**：
1. 从简单的距离过滤器开始实现
2. 建立GPU-CPU结果一致性验证
3. 逐步扩展支持的规则类型
4. 持续性能分析和优化

---

**文档版本**: 1.0  
**生成时间**: 2026-01-30  
**状态**: 迁移指南完成，准备实施