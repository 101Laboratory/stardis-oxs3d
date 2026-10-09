# Star-Enclosures-3D (senc3d) 项目架构分析

## 概述

Star-Enclosures-3D是一个C语言库，用于从原始几何体中提取围栏(enclosures)。围栏是包围给定体积的一组三角形集合。该库支持开放围栏（无内外之分）的检测，并为每个检测到的围栏提供涉及的三角形面、涉及的介质集合以及一些度量（三角形数量、体积、面积等）。

## UML类图

```mermaid
classDiagram
    %% 公共API数据结构
    class senc3d_enclosure_header {
        +unsigned enclosure_id
        +unsigned primitives_count
        +unsigned unique_primitives_count
        +unsigned vertices_count
        +unsigned enclosed_media_count
        +int is_infinite
        +double volume
        +double area
    }
    
    enum senc3d_side {
        SENC3D_FRONT
        SENC3D_BACK
    }
    
    enum senc3d_convention {
        SENC3D_CONVENTION_NORMAL_FRONT
        SENC3D_CONVENTION_NORMAL_BACK
        SENC3D_CONVENTION_NORMAL_INSIDE
        SENC3D_CONVENTION_NORMAL_OUTSIDE
        SENC3D_DUMP_COMPONENTS_STL
        SENC3D_LOG_COMPONENTS_INFORMATION
    }
    
    %% 核心管理类
    class senc3d_device {
        -struct logger* logger
        -struct mem_allocator* allocator
        -int verbose
        -int nthreads
        -ref_T ref
    }
    
    class senc3d_scene {
        -int convention
        -struct darray_triangle_in triangles_in
        -struct darray_position vertices
        -trg_id_t ntris
        -vrtx_id_t nverts
        -struct darray_side_range media_use
        -struct descriptor analyze
        -ref_T ref
        -struct senc3d_device* dev
    }
    
    class senc3d_enclosure {
        -const struct enclosure_data* data
        -struct senc3d_scene* scene
        -ref_T ref
    }
    
    %% 内部数据结构
    class descriptor {
        -enclosure_id_t enclosures_count
        -struct darray_triangle_enc triangles_enc
        -struct darray_enclosure enclosures
        -struct darray_enc_ids_array enc_ids_array_by_medium
        -struct darray_frontier_edge frontiers
        -struct darray_trg_id overlapping_ids
    }
    
    class enclosure_data {
        -struct senc3d_enclosure_header header
        -struct darray_sides_enc sides
        -struct darray_vrtx_id vertices
        -struct darray_uchar tmp_enclosed_media
        -struct darray_media enclosed_media
        -component_id_t cc_count
        -component_id_t first_component
        -struct side_range side_range
        -side_id_t side_count
    }
    
    class triangle_in {
        -vrtx_id_t vertice_id[3]
        -medium_id_t medium[2]
    }
    
    class triangle_comp {
        -component_id_t component[2]
    }
    
    class triangle_enc {
        -enclosure_id_t enclosure[2]
    }
    
    class trgside {
        -side_id_t facing_side_id[3]
        -medium_id_t medium
    }
    
    class side_range {
        -side_id_t first
        -side_id_t last
    }
    
    class frontier_edge {
        -trg_id_t trg
        -vrtx_id_t vrtx0
        -vrtx_id_t vrtx1
    }
    
    %% 关系定义
    senc3d_device "1" -- "*" senc3d_scene : 管理
    senc3d_scene "1" -- "1" descriptor : 包含
    senc3d_scene "1" -- "*" senc3d_enclosure : 生成
    senc3d_enclosure "1" -- "1" enclosure_data : 引用
    descriptor "1" -- "*" enclosure_data : 管理
    senc3d_scene "1" -- "*" triangle_in : 包含
    descriptor "1" -- "*" triangle_enc : 包含
    descriptor "1" -- "*" frontier_edge : 包含
    enclosure_data "1" -- "*" trgside : 包含
```

## 模块功能职责与对外接口

### 1. Device模块 (`senc3d_device_c.h`)
**功能职责**：作为库的入口点，管理日志记录器、内存分配器、线程数和详细输出级别。提供条件日志记录功能，控制库的详细输出级别。

**对外接口**：
- `senc3d_device_create()` - 创建设备对象
- `senc3d_device_ref_get()` / `senc3d_device_ref_put()` - 引用计数管理
- `log_err()` / `log_warn()` / `log_info()` - 条件日志记录

**依赖关系**：依赖于`rsys`库的内存分配器和引用计数机制。

### 2. Scene模块 (`senc3d_scene_c.h`, `senc3d_scene.c`)
**功能职责**：管理几何场景，包括三角形、顶点和介质信息。负责从用户提供的几何数据创建场景，执行围栏提取分析，并提供查询接口访问场景和围栏信息。

**对外接口**：
- `senc3d_scene_create()` - 从顶点和三角形创建场景
- `senc3d_scene_get_convention()` - 获取场景使用的惯例标志
- `senc3d_scene_get_triangles_count()` - 获取三角形数量
- `senc3d_scene_get_triangle()` - 获取三角形顶点索引
- `senc3d_scene_get_triangle_media()` - 获取三角形两侧介质ID
- `senc3d_scene_get_vertices_count()` - 获取顶点数量
- `senc3d_scene_get_vertex()` - 获取顶点坐标
- `senc3d_scene_get_max_medium()` - 获取最大介质ID
- `senc3d_scene_get_enclosure_count()` - 获取围栏数量
- `senc3d_scene_get_enclosure_count_by_medium()` - 按介质获取围栏数量
- `senc3d_scene_get_enclosure()` - 获取指定索引的围栏
- `senc3d_scene_get_enclosure_by_medium()` - 按介质获取围栏
- `senc3d_scene_get_triangle_enclosures()` - 获取三角形两侧所属围栏
- `senc3d_scene_get_frontier_segments_count()` - 获取边界段数量
- `senc3d_scene_get_frontier_segment()` - 获取边界段信息
- `senc3d_scene_get_overlapping_triangles_count()` - 获取重叠三角形数量
- `senc3d_scene_get_overlapping_triangle()` - 获取重叠三角形ID
- `senc3d_scene_dump_enclosure_obj()` - 将围栏转储为OBJ文件
- `senc3d_scene_ref_get()` / `senc3d_scene_ref_put()` - 引用计数管理

**依赖关系**：依赖Device模块和Internal Types模块，使用`rsys`的动态数组和哈希表。

### 3. Enclosure模块 (`senc3d_enclosure_c.h`, `senc3d_enclosure.c`)
**功能职责**：作为围栏对象的句柄，提供围栏数据的访问接口。每个围栏有自己的顶点和三角形编号方案，与全局编号方案无关。

**对外接口**：
- `senc3d_enclosure_get_header()` - 获取围栏头部信息
- `senc3d_enclosure_get_triangle()` - 获取围栏三角形顶点索引
- `senc3d_enclosure_get_vertex()` - 获取围栏顶点坐标
- `senc3d_enclosure_get_triangle_id()` - 获取围栏三角形的全局ID和面侧
- `senc3d_enclosure_get_medium()` - 获取围栏包含的介质ID
- `senc3d_enclosure_ref_get()` / `senc3d_enclosure_ref_put()` - 引用计数管理

**依赖关系**：依赖Scene模块和Enclosure Data模块。

### 4. Descriptor模块 (`senc3d_scene_c.h`)
**功能职责**：存储场景分析结果，包括围栏集合、边界边和重叠三角形。作为场景分析过程的输出容器。

**内部结构**：
- `enclosures_count` - 围栏数量
- `triangles_enc` - 按三角形的围栏归属
- `enclosures` - 围栏数据数组
- `enc_ids_array_by_medium` - 按介质组织的围栏ID数组
- `frontiers` - 边界边数组
- `overlapping_ids` - 重叠三角形ID数组

**依赖关系**：依赖Internal Types模块和Enclosure Data模块。

### 5. Enclosure Data模块 (`senc3d_enclosure_data.h`)
**功能职责**：存储单个围栏的详细数据，包括几何信息、介质列表和组件信息。提供围栏数据的初始化和复制操作。

**内部结构**：
- `header` - 围栏头部信息（ID、计数、体积、面积等）
- `sides` - 围栏包含的三角形面数据
- `vertices` - 围栏顶点索引
- `tmp_enclosed_media` / `enclosed_media` - 围栏包含的介质
- `cc_count` - 涉及的组件数量
- `first_component` - 组件链表首项
- `side_range` - 三角形面范围
- `side_count` - 三角形面数量

**依赖关系**：依赖Internal Types模块，使用`rsys`的动态数组管理数据。

### 6. Geometry Processing模块 (`senc3d_scene_analyze.c`, `senc3d_descriptor.c`)
**功能职责**：执行围栏提取的核心算法，包括三角形去重、连接组件分析、组件分组和围栏生成。处理几何数据的预处理和后处理。

**内部函数**：
- 三角形去重和顶点映射
- 连接组件检测
- 组件分组算法
- 围栏构建
- 边界边检测
- 重叠三角形检测

**依赖关系**：依赖Scene模块和Descriptor模块。

### 7. Internal Types模块 (`senc3d_internal_types.h`)
**功能职责**：定义库内部使用的类型系统，包括ID类型（三角形、顶点、边、介质、围栏、组件）和相关操作宏。确保类型一致性并提供类型转换工具。

**定义的类型**：
- `trg_id_t` - 三角形ID类型
- `side_id_t` - 三角形面ID类型
- `vrtx_id_t` - 顶点ID类型
- `edge_id_t` - 边ID类型
- `medium_id_t` - 介质ID类型
- `enclosure_id_t` - 围栏ID类型
- `component_id_t` - 连接组件ID类型

**工具宏**：
- `TRGSIDE_2_TRG()` - 面ID转三角形ID
- `TRGSIDE_IS_FRONT()` - 判断是否为前面
- `TRGSIDE_2_SIDE()` - 面ID转面侧枚举
- `TRGIDxSIDE_2_TRGSIDE()` - 三角形ID和面侧转面ID
- `TRGSIDE_OPPOSITE()` - 获取对面面ID

**依赖关系**：基础模块，被所有其他模块依赖。

### 8. Helper模块 (`senc3d_side_range.h`, `senc3d_sXd_helper.h`)
**功能职责**：提供辅助数据结构和工具函数，支持几何处理算法。包括面范围管理、边操作和几何计算。

**包含内容**：
- `side_range`结构 - 管理面ID范围
- 边操作函数 - 边比较、设置、反转检测
- 三角形键生成 - 生成唯一的三角形标识键

**依赖关系**：依赖Internal Types模块。

## 设计特点

1. **引用计数管理**：所有主要对象（设备、场景、围栏）都使用引用计数，通过`ref_get()`和`ref_put()`函数管理生命周期。

2. **一次性几何输入**：从0.5版本开始，整个几何必须一次性提供，不允许重复的顶点或三角形，也不允许零面积三角形。这简化了内部处理并提高了性能。

3. **惯例系统**：支持用户定义的前/后面惯例以及输出法线方向惯例，提供几何处理的灵活性。

4. **连接组件分析**：使用连接组件算法将几何体分解为逻辑组件，然后将组件分组形成围栏。

5. **介质感知**：支持三角形两侧的介质定义，允许围栏包含多个介质，处理复杂的多材料场景。

6. **边界检测**：自动检测几何边界段（arity为1且连接不同介质的边），用于验证几何完整性。

7. **重叠检测**：检测共享边的重叠三角形，防止在无效几何上提取围栏。

8. **并行处理支持**：通过`nthreads_hint`参数支持多线程几何分析，提高大规模场景的处理性能。

9. **调试支持**：提供调试标志（`SENC3D_DUMP_COMPONENTS_STL`, `SENC3D_LOG_COMPONENTS_INFORMATION`）帮助诊断算法问题。

## 文件组织

```
star-enclosures-3d/0.7.2/src/
├── senc3d.h                    # 公共API头文件
├── senc3d_device_c.h          # 设备内部定义
├── senc3d_device.c            # 设备实现
├── senc3d_scene_c.h           # 场景内部定义
├── senc3d_scene.c             # 场景管理实现
├── senc3d_scene_analyze.c     # 场景分析算法
├── senc3d_scene_analyze_c.h   # 场景分析内部定义
├── senc3d_enclosure_c.h       # 围栏内部定义
├── senc3d_enclosure.c         # 围栏管理实现
├── senc3d_enclosure_data.h    # 围栏数据结构
├── senc3d_internal_types.h    # 内部类型定义
├── senc3d_side_range.h        # 面范围辅助
├── senc3d_sXd_helper.h        # 维度无关辅助
├── senc3d_descriptor.c        # 描述符管理
├── sencX3d.h                  # 维度通用宏
├── sencX3d_undefs.h           # 宏清理
└── test_*.c                   # 测试文件
```

## 核心算法流程

1. **几何预处理**：
   - 输入三角形和顶点数据
   - 去除重复顶点（哈希表去重）
   - 去除重复三角形（三角形键哈希）
   - 验证几何完整性

2. **连接组件分析**：
   - 为每个三角形面创建`trgside`记录
   - 通过共享边连接三角形面
   - 识别连接组件（连通区域）
   - 标记组件属性（介质、边界等）

3. **组件分组**：
   - 根据介质和几何关系分组连接组件
   - 处理开放围栏（无限围栏）
   - 构建围栏层次结构

4. **围栏构建**：
   - 为每个围栏收集三角形面
   - 计算围栏度量（面积、体积）
   - 建立顶点和三角形映射
   - 构建介质列表

5. **后处理**：
   - 检测边界段
   - 检测重叠三角形
   - 生成输出数据结构

这种设计使得Star-Enclosures-3D能够高效地从复杂3D几何中提取围栏结构，为热传输求解器提供必要的几何分区信息，支持介质边界条件和辐射交换计算。