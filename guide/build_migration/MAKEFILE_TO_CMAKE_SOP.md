# Makefile → CMake 迁移标准操作流程 (SOP)

## 实验目标：test_sdis_camera

### 1. 选择实验目标标准
**原则**: 从依赖链末端开始，选择最简单的可执行目标
**标准**:
1. 依赖库最少（避免MPI、特殊依赖）
2. 代码逻辑简单（基础功能测试）
3. 在Makefile中明确独立构建规则

**选择结果**: `test_sdis_camera`
- 属于常规测试（非MPI，非特殊依赖）
- 仅测试相机基础API，不涉及复杂物理模拟
- 代码行数少（87行），逻辑清晰

### 2. Makefile规则分析流程

#### 步骤2.1: 定位目标构建规则
```makefile
# Makefile第396-398行
test_sdis_camera : config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS) -o $@ src/$@.o $(TEST_LIBS)
```

#### 步骤2.2: 分析依赖项
1. **config.mk**: 配置文件，定义版本、路径、编译选项
2. **sdis-local.pc**: pkg-config文件，动态生成
3. **$(LIBNAME)**: 主库 (libsdis.a 或 libsdis.so)
4. **src/test_sdis_utils.o**: 测试工具对象文件

#### 步骤2.3: 分析编译变量
```makefile
# 测试编译标志
TEST_CFLAGS = $(CFLAGS_EXE) $(SDIS_CFLAGS) $(RSYS_CFLAGS)

# 测试链接标志  
TEST_LIBS = src/test_sdis_utils.o $(LDFLAGS_EXE) $(SDIS_LIBS) $(RSYS_LIBS) -lm

# 主库依赖（通过pkg-config）
SDIS_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags sdis-local.pc)
SDIS_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs sdis-local.pc)
```

#### 步骤2.4: 追溯完整依赖链
```
test_sdis_camera
├── libsdis (主库)
│   ├── rsys >= 0.14
│   ├── s2d >= 0.7
│   ├── s3d >= 0.10
│   ├── star-sp >= 0.14
│   ├── senc2d >= 0.5 (私有)
│   ├── senc3d >= 0.7.2 (私有)
│   └── swf >= 0.0 (私有)
└── test_sdis_utils (测试工具)
    └── rsys >= 0.14
```

### 3. CMake配置创建流程

#### 步骤3.1: 创建基础CMakeLists.txt结构
```cmake
cmake_minimum_required(VERSION 3.15)
project(stardis-solver LANGUAGES C)

# 版本信息（从config.mk提取）
set(VERSION_MAJOR 0)
set(VERSION_MINOR 16)
set(VERSION_PATCH 2)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
```

#### 步骤3.2: 处理C标准差异
**问题**: MSVC不支持C89标准
**解决方案**:
```cmake
# 检查编译器类型
if(MSVC)
  set(CMAKE_C_STANDARD 99)  # MSVC最低支持C99
  add_compile_options(/TP)  # 编译为C代码
else()
  set(CMAKE_C_STANDARD 89)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
endif()
```

#### 步骤3.3: 映射编译选项
```cmake
# 从config.mk映射警告标志
set(WARNING_FLAGS
  /Wall          # MSVC等效于-Wall
  /W4            # 警告级别4
)

if(NOT MSVC)
  set(WARNING_FLAGS
    -Wall
    -Wcast-align
    -Wconversion
    -Wextra
    -Wmissing-declarations
    -Wmissing-prototypes
    -Wshadow
  )
endif()

# 安全加固标志
if(NOT MSVC)
  set(HARDENING_FLAGS
    -D_FORTIFY_SOURCES=2
    -fcf-protection=full
    -fstack-clash-protection
    -fstack-protector-strong
  )
endif()
```

#### 步骤3.4: 处理依赖库问题
**关键发现**: 所有依赖库（rsys, s2d, s3d等）都是Méso|Star内部库，Windows不可用

**临时解决方案**: 创建接口库存根
```cmake
# 创建存根库，允许配置阶段通过
add_library(rsys INTERFACE)
add_library(s2d INTERFACE)
# ... 其他库
```

**长期方案**:
1. 在Windows上构建这些库（需要源码）
2. 寻找功能等效的Windows库
3. 重构代码移除依赖（适用于GPU迁移）

### 4. Windows平台差异处理

#### 问题1: 共享库构建差异
**Makefile**: `-fPIC -shared`
**CMake跨平台**:
```cmake
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(sdis SHARED ${SOURCES})
```

#### 问题2: 可执行文件位置无关代码
**Makefile**: `-fPIE -pie`
**CMake**:
```cmake
set_target_properties(test_sdis_camera PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
```

#### 问题3: pkg-config不可用
**方案A**: 使用CMake的find_package
```cmake
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(RSYS REQUIRED rsys>=0.14)
endif()
```

**方案B**: 手动指定库路径（适用于Windows）
```cmake
if(WIN32)
  # 假设依赖库已安装在其他位置
  find_library(RSYS_LIB rsys PATHS "C:/libraries/rsys/lib")
  find_path(RSYS_INCLUDE rsys.h PATHS "C:/libraries/rsys/include")
endif()
```

### 5. 实验发现的关键问题

#### 5.1 阻塞性问题
1. **依赖库缺失**: rsys, s2d, s3d等库在Windows上不存在
2. **C89标准不兼容**: MSVC不支持C89，需要C99或更高
3. **Linux特定API**: 可能使用了POSIX特有函数

#### 5.2 非阻塞性问题
1. **编译选项映射**: -fPIC, -fPIE等在Windows需要不同处理
2. **链接器选项**: -Wl,-z,relro等在Windows无等效项
3. **安装路径**: Unix风格的/usr/local在Windows不适用

### 6. 迁移SOP模板

#### 步骤1: 目标分析
```markdown
1. 在Makefile中查找目标规则
2. 列出所有依赖项（文件、库、工具）
3. 分析编译和链接标志
4. 绘制完整依赖关系图
```

#### 步骤2: CMake基础配置
```cmake
# 1. 项目基本信息
# 2. 编译器标准设置  
# 3. 构建类型配置
# 4. 全局编译选项
```

#### 步骤3: 依赖处理
```cmake
# 1. 分类依赖：内部/外部、必需/可选
# 2. 平台适配：Windows/Linux/macOS
# 3. 备用方案：存根库/模拟实现
```

#### 步骤4: 目标创建
```cmake
# 1. 创建库目标
# 2. 创建可执行文件目标
# 3. 设置目标属性
# 4. 链接依赖项
```

#### 步骤5: 验证测试
```cmake
# 1. 尝试配置阶段
# 2. 尝试构建阶段
# 3. 记录失败原因
# 4. 迭代修复
```

### 7. 针对GPU迁移项目的特别建议

#### 7.1 短期策略（SOP验证）
1. **创建存根库**: 为缺失依赖创建最小接口
2. **禁用实际编译**: 专注于构建系统迁移，而非功能
3. **记录所有问题**: 建立迁移问题数据库

#### 7.2 中期策略（并行开发）
1. **构建工程师**: 继续CMake迁移，创建完整构建系统
2. **图形工程师**: 开发DX12框架，不依赖现有库
3. **接口定义**: 协调两者之间的数据交换格式

#### 7.3 长期策略（完整迁移）
1. **依赖库替代**: 为GPU实现寻找/创建Windows等效库
2. **算法重构**: 将CPU算法重新实现为GPU友好版本
3. **集成测试**: 逐步替换组件，保持功能一致性

### 8. 下一步行动

#### 8.1 立即行动
1. [x] 完成test_sdis_camera的SOP实验
2. [ ] 应用SOP到另一个测试目标（如test_sdis_data）
3. [ ] 创建依赖库状态跟踪表

#### 8.2 短期行动
1. [ ] 评估依赖库的Windows移植可行性
2. [ ] 设计GPU实现的替代依赖方案
3. [ ] 制定CMake架构最终设计

#### 8.3 长期行动
1. [ ] 完成所有测试目标的CMake迁移
2. [ ] 建立Windows CI/CD流水线
3. [ ] 文档化完整构建过程

---

## 附录：实验数据

### 编译尝试结果
```
1. 原始Makefile构建: 失败（依赖库缺失）
2. CMake配置: 失败（C89标准不兼容 + 依赖库缺失）
3. 简化构建（仅编译）: 失败（头文件缺失）
```

### 依赖库详细列表
| 库名 | 版本 | 用途 | Windows可用性 | 优先级 |
|------|------|------|---------------|--------|
| rsys | 0.14 | 基础系统库 | 否 | 高 |
| s2d | 0.7 | 2D几何 | 否 | 高 |
| s3d | 0.10 | 3D几何 | 否 | 高 |
| senc2d | 0.5 | 2D包围体 | 否 | 中 |
| senc3d | 0.7.2 | 3D包围体 | 否 | 中 |
| star-sp | 0.14 | 采样库 | 否 | 高 |
| swf | 0.0 | WoS函数 | 否 | 低 |

### 成功标准
- [ ] CMake配置通过（无错误）
- [ ] 至少一个目标成功编译
- [ ] 依赖管理方案确定
- [ ] 迁移SOP文档完整

---

*SOP版本: 1.0*  
*实验日期: 2026-01-16*  
*实验目标: test_sdis_camera*  
*状态: 完成分析，发现关键阻塞问题*  
*建议: 优先解决依赖库问题，调整C标准要求*