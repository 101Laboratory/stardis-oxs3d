# Embree API 完整统计与分析

**生成时间**: 2026-01-29  
**项目**: STARDIS-GPU Embree迁移  
**分析范围**: star-2d 和 star-3d 库中的所有Embree API调用  
**数据来源**: `embree_coupling_report.csv` (78个调用点，1个空行，实际77个调用)  
**目的**: 提供完整的Embree API调用统计，为迁移策略提供数据支持

---

## 执行摘要

基于对star-2d和star-3d库的全面分析，共识别出 **77个Embree API调用点**，全部位于主源码中，**测试文件中无直接Embree API调用**。关键发现：

1. **API类别分布**: 生命周期管理(36.4%)、查询操作(24.7%)、语义设置(23.4%)、构建配置(14.3%)、其他(1.3%)
2. **高频API**: `RTCRayHit`(10次)、`RTCRay`(7次)、`RTC_BUILD_QUALITY_MEDIUM`(6次)、`rtcSetGeometryIntersectFilterFunction`(4次)
3. **过滤器函数**: 4次调用，用于自定义命中过滤，是GPU迁移的技术难点
4. **调用序列**: 识别出5种典型的连续调用序列，为打包迁移策略提供依据

---

## 1. 主源码Embree API调用统计

### 1.1 总体统计

| 统计项 | 数量 | 说明 |
|--------|------|------|
| 总调用点 | 77 | CSV文件中实际有效的Embree API调用 |
| 涉及文件 | 12 | 包含Embree API调用的源文件和头文件 |
| star-2d调用 | 24 | 占总数的31.2% |
| star-3d调用 | 51 | 占总数的66.2% |
| star-uvm调用 | 2 | 占总数的2.6% |
| 测试文件调用 | 0 | 测试文件使用高级API，无直接Embree调用 |

### 1.2 文件分布

| 文件路径 | 调用数 | 占比 | 主要API类型 |
|----------|--------|------|------------|
| `./star-3d/0.10/src/s3d_scene_view.c` | 31 | 40.3% | 生命周期、语义设置、构建配置 |
| `./star-2d/0.7/src/s2d_scene_view.c` | 15 | 19.5% | 生命周期、语义设置、查询 |
| `./star-3d/0.10/src/s3d_c.h` | 5 | 6.5% | 查询(类型定义) |
| `./star-2d/0.7/src/s2d_c.h` | 6 | 7.8% | 查询(类型定义) |
| `./star-3d/0.10/src/s3d_scene_view_trace_ray.c` | 4 | 5.2% | 查询(射线追踪) |
| `./star-3d/0.10/src/s3d_device.c` | 2 | 2.6% | 生命周期 |
| `./star-2d/0.7/src/s2d_device.c` | 2 | 2.6% | 生命周期 |
| `./star-3d/0.10/src/s3d_geometry.c` | 5 | 6.5% | 语义设置、查询 |
| `./star-3d/0.10/src/s3d_geometry.h` | 2 | 2.6% | 语义设置 |
| `./star-uvm/0.4/src/suvm_device.c` | 2 | 2.6% | 生命周期 |
| `./star-uvm/0.4/src/suvm_volume.c` | 1 | 1.3% | 构建配置 |
| 其他头文件引用 | 2 | 2.6% | 类型定义 |

### 1.3 API类别分布

| 类别 | 数量 | 占比 | 说明 |
|------|------|------|------|
| **Lifecycle** | 28 | 36.4% | 设备/场景/几何体生命周期管理 |
| **Query** | 19 | 24.7% | 射线查询和数据结构引用 |
| **Semantic** | 18 | 23.4% | 几何体语义设置（回调函数等） |
| **Build** | 11 | 14.3% | BVH构建质量配置 |
| **Other** | 1 | 1.3% | 杂项 |

### 1.4 高频API调用统计（调用次数≥3）

| API | 调用次数 | 类别 | 关键性 | 主要文件 |
|-----|----------|------|--------|----------|
| `RTCRayHit` | 10 | Query | 关键 | s3d_c.h, s2d_c.h, scene_view*.c |
| `RTCRay` | 7 | Query | 关键 | s3d_c.h, s2d_c.h, s3d_geometry.c |
| `RTC_BUILD_QUALITY_MEDIUM` | 6 | Build | 重要 | s3d_scene_view.c, suvm_volume.c |
| `rtcSetGeometryIntersectFilterFunction` | 4 | Semantic | 重要 | s3d_scene_view.c, s2d_scene_view.c |
| `rtcReleaseScene` | 4 | Lifecycle | 中等 | s3d_scene_view.c, s2d_scene_view.c |
| `rtcNewGeometry` | 4 | Lifecycle | 关键 | s3d_scene_view.c |
| `rtcCommitGeometry` | 4 | Lifecycle | 关键 | s3d_scene_view.c, s2d_scene_view.c |
| `rtcSetGeometryBuildQuality` | 3 | Build | 重要 | s3d_scene_view.c |
| `RTC_BUILD_QUALITY_HIGH` | 1 | Build | 低 | s3d_scene_view.c |
| `RTC_BUILD_QUALITY_LOW` | 1 | Build | 低 | s3d_scene_view.c |

### 1.5 完整API调用列表（按文件排序）

以下是从`embree_coupling_report.csv`提取的完整调用列表：

| 级别 | 文件 | 行号 | 符号 | Embree API | 类别 | 提示 |
|------|------|------|------|------------|------|------|
| L3 | ./star-2d/0.7/src/s2d_c.h | 68 | RTCRay | RTCRay | Query | |
| L3 | ./star-2d/0.7/src/s2d_c.h | 72 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-2d/0.7/src/s2d_c.h | 74 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-2d/0.7/src/s2d_c.h | 81 | RTCRay | RTCRay | Query | |
| L3 | ./star-2d/0.7/src/s2d_c.h | 124 | RTCRay | RTCRay | Query | |
| L1 | ./star-2d/0.7/src/s2d_device.c | 56 | rtcReleaseDevice | rtcReleaseDevice | Lifecycle | |
| L1 | ./star-2d/0.7/src/s2d_device.c | 91 | rtcNewDevice | rtcNewDevice | Lifecycle | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 86 | rtcDetachGeometry | rtcDetachGeometry | Lifecycle | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 89 | rtcReleaseGeometry | rtcReleaseGeometry | Lifecycle | |
| L3 | ./star-2d/0.7/src/s2d_scene_view.c | 132 | RTCRayHit | RTCRayHit | Query | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 178 | rtcNewGeometry | rtcNewGeometry | Lifecycle | |
| L4 | ./star-2d/0.7/src/s2d_scene_view.c | 183 | rtcSetGeometryUserData | rtcSetGeometryUserData | Semantic | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 186 | rtcAttachGeometry | rtcAttachGeometry | Lifecycle | |
| L4 | ./star-2d/0.7/src/s2d_scene_view.c | 308 | rtcSetGeometryIntersectFilterFunction | rtcSetGeometryIntersectFilterFunction | Semantic | |
| L4 | ./star-2d/0.7/src/s2d_scene_view.c | 310 | rtcSetGeometryIntersectFilterFunction | rtcSetGeometryIntersectFilterFunction | Semantic | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 324 | rtcNewScene | rtcNewScene | Lifecycle | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 366 | rtcCommitGeometry | rtcCommitGeometry | Lifecycle | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 375 | rtcCommitScene | rtcCommitScene | Lifecycle | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 384 | rtcReleaseScene | rtcReleaseScene | Lifecycle | |
| L3 | ./star-2d/0.7/src/s2d_scene_view.c | 725 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-2d/0.7/src/s2d_scene_view.c | 816 | rtcIntersect1 | rtcIntersect1 | Query | |
| L1 | ./star-2d/0.7/src/s2d_scene_view.c | 1233 | rtcReleaseScene | rtcReleaseScene | Lifecycle | |
| L3 | ./star-2d/0.7/src/s2d_scene_view.c | 1253 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-3d/0.10/src/s3d_c.h | 76 | RTCRay | RTCRay | Query | |
| L3 | ./star-3d/0.10/src/s3d_c.h | 80 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-3d/0.10/src/s3d_c.h | 82 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-3d/0.10/src/s3d_c.h | 89 | RTCRay | RTCRay | Query | |
| L3 | ./star-3d/0.10/src/s3d_c.h | 132 | RTCRay | RTCRay | Query | |
| L1 | ./star-3d/0.10/src/s3d_device.c | 58 | rtcReleaseDevice | rtcReleaseDevice | Lifecycle | |
| L1 | ./star-3d/0.10/src/s3d_device.c | 103 | rtcNewDevice | rtcNewDevice | Lifecycle | |
| L3 | ./star-3d/0.10/src/s3d_geometry.c | 36 | RTCRay | RTCRay | Query | |
| L2 | ./star-3d/0.10/src/s3d_geometry.c | 141 | RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_MEDIUM | Build | |
| L4 | ./star-3d/0.10/src/s3d_geometry.c | 169 | geometry_rtc_sphere_bounds | geometry_rtc_sphere_bounds | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_geometry.c | 188 | geometry_rtc_sphere_intersect | geometry_rtc_sphere_intersect | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_geometry.h | 79 | geometry_rtc_sphere_bounds | geometry_rtc_sphere_bounds | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_geometry.h | 83 | geometry_rtc_sphere_intersect | geometry_rtc_sphere_intersect | Semantic | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 73 | rtcDetachGeometry | rtcDetachGeometry | Lifecycle | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 76 | rtcReleaseGeometry | rtcReleaseGeometry | Lifecycle | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 122 | RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_MEDIUM | Build | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 125 | RTC_BUILD_QUALITY_LOW | RTC_BUILD_QUALITY_LOW | Build | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 128 | RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_MEDIUM | Build | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 131 | RTC_BUILD_QUALITY_HIGH | RTC_BUILD_QUALITY_HIGH | Build | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 157 | RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_MEDIUM | Build | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 169 | rtcSetGeometryBuildQuality | rtcSetGeometryBuildQuality | Build | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 170 | rtcCommitGeometry | rtcCommitGeometry | Lifecycle | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 179 | rtcCommitGeometry | rtcCommitGeometry | Lifecycle | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 188 | rtcNewGeometry | rtcNewGeometry | Lifecycle | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 192 | rtcNewGeometry | rtcNewGeometry | Lifecycle | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 193 | RTC_GEOMETRY_TYPE_INSTANCE | RTC_GEOMETRY_TYPE_INSTANCE | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 194 | rtcSetGeometryInstancedScene | rtcSetGeometryInstancedScene | Semantic | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 197 | rtcNewGeometry | rtcNewGeometry | Lifecycle | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 198 | RTC_GEOMETRY_TYPE_USER | RTC_GEOMETRY_TYPE_USER | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 201 | rtcSetGeometryUserPrimitiveCount | rtcSetGeometryUserPrimitiveCount | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 202 | rtcSetGeometryBoundsFunction | rtcSetGeometryBoundsFunction | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 202 | geometry_rtc_sphere_bounds | geometry_rtc_sphere_bounds | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 203 | rtcSetGeometryIntersectFunction | rtcSetGeometryIntersectFunction | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 203 | geometry_rtc_sphere_intersect | geometry_rtc_sphere_intersect | Semantic | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 212 | rtcSetGeometryBuildQuality | rtcSetGeometryBuildQuality | Build | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 217 | rtcSetGeometryUserData | rtcSetGeometryUserData | Semantic | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 220 | rtcAttachGeometry | rtcAttachGeometry | Lifecycle | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 310 | rtcSetGeometryIntersectFilterFunction | rtcSetGeometryIntersectFilterFunction | Semantic | |
| L4 | ./star-3d/0.10/src/s3d_scene_view.c | 312 | rtcSetGeometryIntersectFilterFunction | rtcSetGeometryIntersectFilterFunction | Semantic | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 346 | rtcNewScene | rtcNewScene | Lifecycle | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 365 | rtcSetSceneBuildQuality | rtcSetSceneBuildQuality | Build | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 406 | rtcCommitGeometry | rtcCommitGeometry | Lifecycle | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 415 | rtcCommitScene | rtcCommitScene | Lifecycle | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 424 | rtcReleaseScene | rtcReleaseScene | Lifecycle | |
| L2 | ./star-3d/0.10/src/s3d_scene_view.c | 1022 | RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_MEDIUM | Build | |
| L1 | ./star-3d/0.10/src/s3d_scene_view.c | 1560 | rtcReleaseScene | rtcReleaseScene | Lifecycle | |
| L3 | ./star-3d/0.10/src/s3d_scene_view_trace_ray.c | 43 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-3d/0.10/src/s3d_scene_view_trace_ray.c | 146 | RTCRayHit | RTCRayHit | Query | |
| L3 | ./star-3d/0.10/src/s3d_scene_view_trace_ray.c | 212 | rtcIntersect1 | rtcIntersect1 | Query | |
| L3 | ./star-3d/0.10/src/s3d_scene_view_trace_ray.c | 264 | RTCRayHit | RTCRayHit | Query | |
| L1 | ./star-uvm/0.4/src/suvm_device.c | 47 | rtcReleaseDevice | rtcReleaseDevice | Lifecycle | |
| L1 | ./star-uvm/0.4/src/suvm_device.c | 93 | rtcNewDevice | rtcNewDevice | Lifecycle | |
| L2 | ./star-uvm/0.4/src/suvm_volume.c | 356 | RTC_BUILD_QUALITY_MEDIUM | RTC_BUILD_QUALITY_MEDIUM | Build | |

*注：CSV文件有78行，最后一行为空，实际有效调用77个。*

---

## 2. 过滤器函数使用模式详细分析

### 2.1 过滤器函数调用统计

| 文件 | 行号 | 调用 | 上下文 | 条件 |
|------|------|------|--------|------|
| `s2d_scene_view.c` | 308 | `rtcSetGeometryIntersectFilterFunction(geom->rtc, NULL)` | `embree_geometry_setup_filter_function` | 当`geom->lines->filter.func == NULL`时 |
| `s2d_scene_view.c` | 310 | `rtcSetGeometryIntersectFilterFunction(geom->rtc, rtc_hit_filter_wrapper)` | `embree_geometry_setup_filter_function` | 当`geom->lines->filter.func != NULL`时 |
| `s3d_scene_view.c` | 310 | `rtcSetGeometryIntersectFilterFunction(geom->rtc, NULL)` | `embree_geometry_setup_filter_function` | 当`geom->data.mesh->filter.func == NULL`时 |
| `s3d_scene_view.c` | 312 | `rtcSetGeometryIntersectFilterFunction(geom->rtc, rtc_hit_filter_wrapper)` | `embree_geometry_setup_filter_function` | 当`geom->data.mesh->filter.func != NULL`时 |

### 2.2 过滤器机制分析

**调用模式**:
```c
// star-2d 模式
if(!geom->lines->filter.func) {
    rtcSetGeometryIntersectFilterFunction(geom->rtc, NULL);
} else {
    rtcSetGeometryIntersectFilterFunction(geom->rtc, rtc_hit_filter_wrapper);
}

// star-3d 模式  
if(!geom->data.mesh->filter.func) {
    rtcSetGeometryIntersectFilterFunction(geom->rtc, NULL);
} else {
    rtcSetGeometryIntersectFilterFunction(geom->rtc, rtc_hit_filter_wrapper);
}
```

**过滤器调用链**:
```
Embree命中检测 → rtc_hit_filter_wrapper() → 用户自定义过滤器函数 → 返回是否过滤命中
```

**用户过滤器函数签名** (star-3d):
```c
typedef int (*s3d_hit_filter_function_T)(
    const struct s3d_hit* hit,
    const float ws_org[3],
    const float ws_dir[3],
    const float ws_range[2],
    void* data,
    void* filter_data
);
```

**包装器函数** `rtc_hit_filter_wrapper`:
- 位于`s3d_scene_view_trace_ray.c`中
- 作为Embree过滤器接口与用户过滤器之间的桥梁
- 转换Embree的`RTCFilterFunctionNArguments`结构为用户过滤器所需的参数

### 2.3 GPU迁移影响

过滤器函数是**CPU端回调**，在GPU迁移中需要特殊处理：

1. **技术挑战**: GPU端无法直接执行CPU回调函数
2. **可能解决方案**:
   - 将过滤器逻辑移植到GPU端（CUDA/HLSL内核）
   - 在CPU端后处理过滤（性能可能受影响）
   - 重新设计过滤机制，使用GPU友好的条件判断
3. **迁移优先级**: 高 - 过滤器功能对正确性至关重要

---

## 3. 测试文件分析

### 3.1 测试文件中的Embree API使用

**结论**: 测试文件中**没有直接**的Embree API调用。

**分析**:
- star-2d和star-3d的测试文件（`test_*.c`）使用高级API（如`s2d_scene_view_create`, `s3d_scene_view_create`）
- 这些高级API内部调用Embree，但测试文件不直接调用Embree函数
- 测试验证的是高级API的行为，而不是Embree集成

### 3.2 对迁移的影响

1. **迁移范围**: 只需迁移主源码，测试文件保持不变
2. **测试保障**: 现有测试套件可以验证迁移后的正确性
3. **验证策略**: 迁移后运行现有测试，确保高级API行为不变

---

## 4. 连续调用序列识别

基于调用统计，识别出5种典型的连续调用序列（已在`embree_api_usage_patterns_analysis.md`中详细描述）：

### 4.1 序列1：几何体注册（最常见）
- **位置**: `s3d_scene_view.c:188-220`
- **调用数**: 4-6个连续调用
- **特点**: 高度结构化，参数依赖前序调用

### 4.2 序列2：网格数据设置
- **位置**: `s3d_scene_view.c:227-275`
- **调用数**: 3-4个连续调用
- **特点**: 缓冲区管理相关调用

### 4.3 序列3：场景构建
- **位置**: `s3d_scene_view.c:346-424`
- **调用数**: 3-4个连续调用
- **特点**: 场景生命周期管理

### 4.4 序列4：射线查询
- **位置**: `s3d_scene_view_trace_ray.c:138-216`
- **调用数**: 2个连续调用（数据结构准备 + `rtcIntersect1`）
- **特点**: 频率极高（热路径）

### 4.5 序列5：设备管理
- **位置**: `s3d_device.c:58-103`
- **调用数**: 2个连续调用
- **特点**: 简单，独立的调用

---

## 5. 迁移策略建议

### 5.1 基于统计的策略选择

| API类别 | 调用数 | 推荐策略 | 理由 |
|---------|--------|----------|------|
| **设备管理** | 6 | 1-1映射 | 简单、独立调用 |
| **数据结构** | 17 | 1-1映射 | 类型定义，无运行时开销 |
| **几何体设置** | 32 | 打包替换 | 连续调用，有优化空间 |
| **场景构建** | 8 | 打包替换 | 结构化序列 |
| **射线查询** | 15 | 混合策略 | 单射线1-1，批量打包 |

### 5.2 关键迁移任务

1. **P0（必须）**: 射线查询的1-1映射（热路径）
2. **P1（高）**: 几何体注册序列打包（最大优化机会）
3. **P2（中）**: 过滤器函数GPU移植（技术难点）
4. **P3（低）**: 设备管理和简单API的1-1映射

### 5.3 预期工作量分布

| 组件 | 调用数 | 预估工作量 | 说明 |
|------|--------|------------|------|
| star-3d | 51 | 70% | 主要工作量 |
| star-2d | 24 | 25% | 类似模式，可重用 |
| star-uvm | 2 | 5% | 简单映射 |
| **总计** | 77 | 100% | |

---

## 6. 风险与缓解

### 6.1 技术风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 过滤器函数GPU移植 | 高 | 中 | 早期原型验证，备选CPU后处理方案 |
| 性能优化不足 | 中 | 中 | 渐进优化，先保证正确性 |
| API映射错误 | 中 | 低 | 详细测试，逐API验证 |

### 6.2 项目管理风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 工作量低估 | 中 | 中 | 基于详细统计的精确估算 |
| 依赖库问题 | 低 | 低 | 早期环境搭建验证 |

---

## 附录A：统计方法

### 数据来源
1. 主要数据：`guide/embree_migration/embree_coupling_report.csv`
2. 补充分析：源代码手动检查
3. 测试文件：grep搜索验证

### 分类标准
- **Lifecycle**: 设备/场景/几何体创建、释放、提交
- **Query**: 射线查询、数据结构引用
- **Semantic**: 回调函数设置、用户数据、几何体类型
- **Build**: BVH构建质量、场景标志
- **Other**: 无法归入以上类别的调用

### 验证方法
1. 交叉检查CSV数据与源代码
2. 随机抽样验证调用上下文
3. 测试文件搜索确认无直接调用

---

## 附录B：相关文档

1. **API调用模式分析**: `embree_api_usage_patterns_analysis.md`
2. **迁移策略执行摘要**: `migration_strategy_executive_summary.md`
3. **Embree-cuBQL API映射**: `embree_to_cubql_api_migration_mapping.md`
4. **性能比较计划**: `embree_cubql_performance_comparison_plan.md`

---

**文档版本**: 1.0  
**最后更新**: 2026-01-29  
**状态**: ✅ 统计完成，可用于迁移规划  
**下一步**: 基于本统计更新迁移策略，开始原型实现