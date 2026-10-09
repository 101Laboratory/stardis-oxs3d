# Filter函数外部调用分析报告

**生成时间**: 2026-01-30  
**分析目标**: 识别从star-3d模块外部对filter函数的具体调用案例  
**搜索范围**: star-enclosure-3d(senc3d), stardis-solver, stardis应用程序  
**方法**: 并行搜索 + 直接grep分析

---

## 执行摘要

通过对stardis-cpu代码库的全面搜索，**发现2个主要的外部调用案例和多个测试用例**。关键发现：

### ✅ 主要外部调用者
1. **star-enclosure-3d (senc3d)** - 几何分析模块，使用复杂的`self_hit_filter`
2. **stardis主应用程序** - 探针计算，使用`hit_filter`确定边界条件

### ❌ stardis-solver未直接调用
- stardis-solver模块本身不直接调用filter函数
- 但通过stardis应用程序间接使用（stardis使用solver）

### 📊 测试用例丰富
- 多个测试文件包含完整的filter函数实现
- 涵盖简单验证到复杂场景过滤

---

## 详细调用分析

### 1. star-enclosure-3d (senc3d) 模块

#### 调用位置
- **文件**: `star-enclosures-3d/0.7.2/src/senc3d_scene_analyze.c`
- **行号**: 1170
- **调用**: `OK(s3d_mesh_set_hit_filter_function(s3d_shp, self_hit_filter, NULL));`

#### Filter函数: `self_hit_filter`
```c
static int self_hit_filter(
  const struct s3d_hit* hit,
  const float ray_org[3],
  const float ray_dir[3],
  const float ray_range[2],
  void* ray_data,
  void* filter_data)
{
  struct filter_ctx* ctx = ray_data;
  (void)ray_org; (void)ray_range; (void)filter_data;
  
  switch (ctx->type) {
    case CTX2:
      /* 用于沿无限射线计算某些组件上的命中次数 */
      const struct triangle_comp* trg_comp;
      const component_id_t* hit_comp;
      trg_comp = darray_triangle_comp_cdata_get(ctx->triangles_comp);
      hit_comp = trg_comp[hit->prim.prim_id].component;
      if(hit_comp[SENC3D_FRONT] == ctx->origin_component
        || hit_comp[SENC3D_BACK] == ctx->origin_component) {
        ctx->cpt++;  // 计数递增
      }
      return 1; /* 拒绝以继续计数 */
      
    case CTX0:
      /* 用于确定CTX1主计算的搜索半径 */
      // ... 复杂几何分析逻辑
      return 0; /* 接受命中 */
      
    case CTX1:
      /* 主计算：评估组件连通性 */
      // ... 复杂几何分析逻辑
      return 0; /* 接受命中 */
  }
}
```

#### 上下文数据结构
```c
struct filter_ctx {
  enum ctx_type type;  // CTX0, CTX1, CTX2
  struct senc3d_scene* scn;
  struct s3d_scene_view* view;
  component_id_t origin_component;
  struct darray_triangle_comp* triangles_comp;
  struct darray_ptr_component_descriptor* components;
  
  /* 跨filter调用使用的临时数据 */
  double current_6volume;
  int cpt;
  float s;
  
  /* CTX1命中的结果 */
  component_id_t hit_component;
  float hit_dir[3], hit_dist;
  struct s3d_primitive hit_prim;
};
```

#### 用途分析
- **目的**: 几何包围体提取和组件分析
- **复杂度**: 高 - 维护跨多个filter调用的状态
- **状态管理**: 通过`filter_ctx`结构体共享数据
- **几何查询**: 访问三角形组件数据和几何属性

---

### 2. stardis主应用程序

#### 调用位置
- **文件**: `stardis/0.12/src/stardis-compute.c`
- **行号**: 228
- **调用**: `ERR(s3d_mesh_set_hit_filter_function(s3d_shp, hit_filter, NULL));`

#### Filter函数: `hit_filter`
```c
static int hit_filter(
  const struct s3d_hit* hit,
  const float ray_org[3],
  const float ray_dir[3],
  const float ray_range[2],
  void* ray_data,
  void* filter_data)
{
  struct filter_ctx* filter_ctx = ray_data;
  float s, dir[3];
  enum sg3d_property_type prop;
  struct s3d_attrib interf_pos;
  const struct description* descriptions;
  unsigned descr[SG3D_PROP_TYPES_COUNT__];
  const struct stardis* stardis;
  
  (void)ray_org; (void)ray_dir; (void)ray_range; (void)filter_data;
  
  if(filter_ctx->dist == hit->distance && filter_ctx->desc) {
    /* 无法改进：保留先前结果！否则可能以NULL描述结束 */
    return 1; /* 跳过 */
  }
  
  stardis = filter_ctx->stardis;
  descriptions = darray_descriptions_cdata_get(&stardis->descriptions);
  
  CHK(s3d_primitive_get_attrib(&hit->prim, S3D_POSITION, hit->uv, &interf_pos) == RES_OK);
  
  // ... 复杂的边界条件判断逻辑
  
  /* 确定命中的边和要查看的属性 */
  prop = (s > 0) ? SG3D_BACK : SG3D_FRONT;
  
  // ... 更多逻辑
}
```

#### 上下文数据结构
```c
struct filter_ctx {
  const struct stardis* stardis;
  struct description* desc;
  unsigned prim;
  float pos[3];
  float dist;
  char outside;
  char probe_on_boundary;
};
```

#### 用途分析
- **目的**: 热模拟中的探针位置分析
- **关键功能**: 
  1. 确定几何命中点
  2. 判断边界条件（内部/外部）
  3. 选择正确的材质属性
  4. 处理探针在边界上的特殊情况
- **复杂度**: 中 - 涉及几何查询和属性查找

---

## 3. 测试用例分析

### 测试文件概览
| 测试文件 | Filter函数 | 用途 | 复杂度 |
|----------|------------|------|--------|
| `test_s3d_closest_point.c` | `sphere_filter` | 球体几何过滤测试 | 低 |
| `test_s3d_closest_point.c` | `cbox_filter` | Cornell Box场景过滤 | 中 |
| `test_s3d_trace_ray.c` | `filter_func` | 基本过滤功能测试 | 低 |
| `test_s3d_shape.c` | `filter_none` | API测试（不过滤） | 低 |
| `test_s3d_scene_view.c` | 未命名filter | 平面过滤测试 | 低 |
| `test_s3d_trace_ray_instance.c` | 未命名filter | 实例几何过滤测试 | 低 |

### 代表性测试用例

#### a) `sphere_filter` (test_s3d_closest_point.c)
```c
struct sphere_filter_data {
  float query_pos[3];
  float query_radius;
};

static int sphere_filter(...) {
  struct sphere_filter_data* data = query_data;
  // 验证filter_data魔数值
  CHK((intptr_t)filter_data == (intptr_t)0xDECAFBAD);
  // 验证查询位置和半径
  CHK(f3_eq_eps(data->query_pos, org, POSITION_EPSILON));
  CHK(range[0] == 0);
  CHK(range[1] == data->query_radius);
  return 1; // 总是过滤（测试目的）
}
```

#### b) `cbox_filter` (test_s3d_closest_point.c)
```c
struct cbox_filter_data {
  float query_pos[3];
  float query_radius;
  unsigned geom_to_filter[3];  // 要过滤的几何ID
};

static int cbox_filter(...) {
  struct cbox_filter_data* data = query_data;
  // 基于几何ID过滤
  return data->geom_to_filter[0] == hit->prim.geom_id
      || data->geom_to_filter[1] == hit->prim.geom_id
      || data->geom_to_filter[2] == hit->prim.geom_id;
}
```

#### c) `filter_none` (test_s3d_shape.c)
```c
static int filter_none(...) {
  (void)hit, (void)org, (void)dir, (void)range, (void)ray_data, (void)filter_data;
  return 0; // 从不过滤
}
```

---

## 4. 使用模式总结

### filter_data 使用模式
| 用例 | filter_data值 | 目的 |
|------|---------------|------|
| senc3d | `NULL` | 无filter-specific数据 |
| stardis | `NULL` | 无filter-specific数据 |
| 测试用例 | `(void*)((intptr_t)0xDECAFBAD)` | 验证filter_data传递 |
| API测试 | `(void*)((uintptr_t)0xDEADBEEF)` | 验证数据存储/检索 |

### query_data (ray_data) 使用模式
| 用例 | 数据结构 | 内容 |
|------|----------|------|
| senc3d | `struct filter_ctx` | 几何分析上下文 |
| stardis | `struct filter_ctx` | 探针分析上下文 |
| 测试用例 | `struct sphere_filter_data` 等 | 测试特定数据 |

### 返回值语义
- `0` = 接受命中（不过滤）
- `非0` = 拒绝命中（过滤掉）

### 常见操作
1. **几何查询**: `s3d_primitive_get_attrib()`
2. **位置计算**: 射线起点 + 方向 × 距离
3. **法向判断**: 点积计算确定命中边
4. **属性查找**: 基于几何ID查找材质/边界属性

---

## 5. GPU迁移影响分析

### 技术挑战

#### 挑战1: 复杂状态管理
- `self_hit_filter`维护跨多个调用的状态（`ctx->cpt++`, `ctx->type`切换）
- GPU上难以维护跨线程的共享状态

#### 挑战2: 几何查询依赖
- Filter函数调用`s3d_primitive_get_attrib()`等CPU API
- GPU需要预加载所有几何数据到显存

#### 挑战3: 动态控制流
- 基于命中结果的复杂条件判断
- GPU SIMD架构不擅长发散控制流

#### 挑战4: 数据结构访问
- 访问复杂的数据结构（`darray_triangle_comp`, `descriptions`）
- 需要GPU友好的数据布局

### 迁移建议

#### 级别1: 简单过滤规则
迁移`sphere_filter`, `cbox_filter`等简单测试用例：
- 基于几何ID的过滤
- 基于距离范围的过滤
- 静态条件判断

#### 级别2: 参数化复杂过滤
为`senc3d`和`stardis`用例设计专用规则：
- **senc3d规则**: 组件ID过滤 + 计数功能
- **stardis规则**: 边界判断 + 属性查找

#### 级别3: 混合执行策略
对最复杂的`self_hit_filter`：
- GPU预处理：快速几何筛选
- CPU后处理：精确状态维护

---

## 6. 实施优先级建议

### 高优先级 (P0)
1. **几何ID过滤** - 测试用例已覆盖，简单易实现
2. **距离范围过滤** - 测试用例已覆盖，GPU友好
3. **简单条件组合** (AND/OR逻辑) - 通用需求

### 中优先级 (P1)
1. **senc3d组件分析** - 需要状态维护，但模式固定
2. **stardis边界判断** - 复杂但模式可参数化

### 低优先级 (P2)
1. **完整`self_hit_filter`迁移** - 可能需要CPU回退
2. **动态几何查询** - 需要完整的几何数据GPU化

---

## 7. 测试策略

### 单元测试覆盖
1. **基础过滤规则**: 几何ID、距离、法向角度
2. **组合过滤**: AND/OR逻辑，嵌套规则
3. **数据传递**: filter_data和query_data验证

### 集成测试
1. **senc3d场景**: 验证组件分析功能
2. **stardis探针**: 验证边界条件判断
3. **性能对比**: GPU vs CPU filter执行时间

### 回归测试
1. **现有测试用例**: 确保所有测试通过
2. **精度验证**: 确保GPU结果与CPU一致（容差1e-6）

---

## 8. 结论

### 关键发现
1. **两个主要外部使用者**: senc3d（几何分析）和stardis（热模拟）
2. **测试用例丰富**: 提供完整的filter功能覆盖
3. **复杂度分层**: 从简单ID过滤到复杂状态维护
4. **数据模式固定**: filter_data通常为NULL，query_data传递上下文

### GPU迁移建议
1. **渐进式迁移**: 从简单规则开始，逐步支持复杂用例
2. **参数化设计**: 将复杂逻辑转换为可配置规则
3. **混合执行**: 对最复杂用例保留CPU回退路径
4. **性能优化**: 针对常见过滤模式优化GPU内核

### 后续步骤
1. 基于本报告更新`filter_impl_guide.md`中的迁移策略
2. 实现P0优先级的基础过滤规则
3. 创建GPU-CPU结果一致性验证框架
4. 逐步迁移测试用例，验证功能完整性

---

**文档版本**: 1.0  
**生成时间**: 2026-01-30  
**状态**: 分析完成，可用于指导GPU迁移实施