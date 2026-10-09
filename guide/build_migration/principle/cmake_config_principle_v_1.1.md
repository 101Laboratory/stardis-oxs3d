# CMake 源码级依赖配置规范 v1.0

> 适用场景：多项目 / 多版本并行，Visual Studio 2022，支持跨项目单步调试（F11），不复制源码。

---

## 0. 设计目标（Design Goals）

1. 支持 **Visual Studio 2022** 下的跨项目单步调试（F11 进入依赖源码）
2. 不复制任何源码或头文件，保证单一事实源（Single Source of Truth）
3. 所有构建产物必须位于 `build/` 目录
4. 项目目录保持干净，仅包含源码与配置文件
5. 支持多项目、多版本并行存在
6. 所有依赖通过 **源码级方式** 接入，统一纳入一个 CMake 构建图

---

## 1. 支持环境（Hard Constraints）

- 构建系统：CMake
- 生成器：**Visual Studio 17 2022**
- 平台：x64

**强制规则：**
- 如果检测到非 Visual Studio 2022 生成器，配置阶段必须失败

---

## 2. 目录结构规范（Mandatory）

### 2.1 顶层结构

```
<ROOT>/
  <ProjectA>/
    <VersionA>/
      <ProjectDir>/
        CMakeLists.txt
        src/
        build/
```

### 2.2 强制规则

- `build/` 必须为 out-of-source build
- 项目目录中：
  - 允许：`CMakeLists.txt`、`src/`
  - 禁止：任何构建中间产物
- `.sln`、`.vcxproj` 等生成文件必须位于 `build/` 中

---

## 3. 项目与模块命名规则（Mandatory）

### 3.1 模块名来源

- 模块名（MODULE_NAME）必须来源于：

```
project(<MODULE_NAME> VERSION x.y.z)
```

- 模块名不要求与目录名一致
- 模块名在整个工程树中必须唯一

---

## 4. 源码组织规则（Mandatory）

### 4.1 源码位置

- 所有源码（`.c/.cpp/.h`）必须位于：

```
<project>/src/
```

- 不强制区分 include / source / internal 子目录

### 4.2 源码加入 target 的方式（强制）

- 必须扫描 `src/` 下所有源码文件并加入 `target_sources`
- 允许使用 `GLOB_RECURSE`，但 **必须限制在 `src/` 目录内**

**目的：**
- 在 Visual Studio 中将 `.h` 正确显示为“头文件”而非“外部依赖项”

### 4.3 明确禁止

- 禁止拷贝源码
- 禁止拷贝头文件
- 禁止使用 `file(COPY ...)` 传播头文件

---

## 5. 头文件暴露与 include 规则（Mandatory）

### 5.1 头文件可见性的唯一合法方式

> 依赖项目 **只能** 通过 target 的 PUBLIC / INTERFACE include 目录暴露头文件。

- 每个库 target 必须显式声明：
  - 对外 API 所在的 include 路径

### 5.2 关键原则

- `target_sources()`：
  - 负责 **IDE 结构展示**（VS 中的“头文件”节点）
- `target_include_directories()`：
  - 负责 **编译器 include 行为**

二者职责不同，必须同时存在。

### 5.3 推荐做法（规范级建议）

- 依赖模块：
  - 使用 `PUBLIC` 暴露头文件路径
- 使用方模块：
  - 仅通过 `target_link_libraries` 连接依赖
  - **不得**手动添加依赖的 include 路径

### 5.4 明确禁止

- 禁止使用全局 `include_directories()`
- 禁止在使用方直接操作依赖的 include 路径

---

## 6. 依赖项目发现与接入（Mandatory）

### 6.1 依赖根变量

- 每个依赖模块必须定义：

```
<MODULE_NAME>_ROOT
```

- 含义：

```
<Project>/<Version>/
```

### 6.2 依赖发现顺序（强制）

1. 用户通过 `-D<MODULE>_ROOT=...` 显式指定
2. 否则：
   - 仅在当前项目的 **兄弟目录** 中搜索
3. 搜索失败：配置阶段直接失败

### 6.3 搜索校验

- 被发现的依赖项目必须满足：
  - 存在 `CMakeLists.txt`
  - `project()` 名称与依赖模块名一致
- 多重匹配：配置失败

---

## 7. 依赖接入方式（核心规则）

### 7.1 唯一允许的依赖接入方式

> **所有依赖项目必须使用 `add_subdirectory()` 接入**

- 依赖项目与主项目必须处于同一个 CMake 构建图中
- 禁止使用：
  - `ExternalProject_Add`
  - 预构建二进制依赖作为主路径

### 7.2 行为保证

- 所有项目出现在同一个 `.sln` 中
- Debug / Release 配置统一
- 支持跨项目断点与单步调试

---

## 8. Target 与链接规则（Mandatory）

### 8.1 Target 要求

- 每个模块必须至少定义一个 target：
  - `add_library()` 或 `add_executable()`

### 8.2 可见性规则

- 必须显式使用：
  - `PRIVATE`
  - `PUBLIC`
  - `INTERFACE`

### 8.3 明确禁止

- 禁止通过全局变量传递 include / lib 路径
- 禁止直接修改 `CMAKE_CXX_FLAGS` 等全局编译选项

---

## 9. 输出目录规则（Mandatory）

- 所有模块统一输出到 build 目录下：

```
build/bin
build/lib
```

- 禁止输出任何文件到项目目录

---

## 10. 测试规则（Mandatory）

### 10.1 启用方式

- 使用选项变量控制测试启用：

```
ENABLE_TESTS
```

### 10.2 测试结构

- 测试程序必须为独立 `add_executable`
- 测试输出目录：

```
build/Tests
```

- 使用 `ctest`
- 测试 target：
  - 可以链接主项目的 **library target**
  - 不得依赖主项目 exe

---

## 11. 明确禁止项（Failure Conditions）

以下情况必须在配置阶段失败：

- 使用非 Visual Studio 2022 生成器
- 依赖模块未找到
- 依赖模块 `project()` 名称不匹配
- 构建产物写入项目目录
- 发现源码或头文件被复制

---

## 12. GPU / 加速计算相关规范补充（v1.0 扩展）

> 本节用于回应 GPU / 加速计算集成的工程需求，适用于 CUDA / DX12 / 计算着色器等场景。
> 本节内容为 **强约束补充**，不改变既有源码级依赖模型。

### 12.1 GPU 能力与工具链检测（Mandatory）

- 若项目启用 GPU 加速，必须在 **configure 阶段**完成能力检测
- 禁止在运行时才发现能力不满足

#### 12.1.1 CUDA 场景

- 必须显式启用 CUDA 语言：

```cmake
enable_language(CUDA)
```

- 必须检测以下能力：
  - CUDA Toolkit 是否存在
  - 最低支持架构（如 `sm_70`）
  - 是否支持双精度（FP64）

- 示例约束逻辑（概念级）：
  - 不支持 FP64 → 配置失败或自动降级

#### 12.1.2 DX12 / HLSL 场景

- DX12 项目必须：
  - 明确区分 CPU 代码与 GPU Shader 代码
  - Shader 文件（`.hlsl`）不得参与 C/C++ 编译

- 推荐：
  - 使用独立 target 或自定义命令管理 Shader 编译

---

### 12.2 GPU 相关源码组织建议（Best Practices）

- GPU 相关源码建议逻辑分组：
  - 物理上仍可位于 `src/`
  - 逻辑上区分：
    - cpu/
    - gpu/
    - shader/

- GPU 代码必须仍然遵循：
  - 不复制源码
  - 通过 target_sources 显式加入

---

## 13. 测试体系补充规范（v1.0 扩展）

> 本节用于补充测试架构的工程级指导，在不破坏原有测试规则的前提下增强可执行性。

### 13.1 测试层级划分（Recommended）

测试建议分为三类 target：

1. **单元测试（Unit Tests）**
   - 测试最小功能单元
   - 直接链接核心 library target

2. **集成测试（Integration Tests）**
   - 验证多个模块协同工作
   - 允许链接多个 library target

3. **系统测试（System / E2E Tests）**
   - 如存在，可通过脚本或独立 exe 驱动

---

### 13.2 测试 Target 规则（Mandatory）

- 每个测试必须是独立 `add_executable`
- 测试 target **不得**：
  - 修改主项目编译选项
  - 输出到非 `build/Tests` 目录

- 测试必须通过 `add_test()` 注册到 `ctest`

---

### 13.3 测试与主项目的关系（关键说明）

- 推荐结构：
  - 核心逻辑 → library target
  - exe → 壳
  - tests → 直接链接 library

- 禁止行为：
  - 测试通过执行主 exe 验证功能

---

### 13.4 GPU / 加速相关测试建议（Best Practices）

- GPU 测试应至少包含：
  - 能力检测测试（是否支持所需特性）
  - 正确性测试（数值或图像误差阈值）

- 若硬件条件不足：
  - 允许测试被标记为 SKIPPED
  - 不允许静默失败

---

## 14. 规范性建议（Best Practices，非强制）

1. **优先库 + 壳 exe 结构**
   - 将核心逻辑放入 library
   - exe 仅作为启动壳，便于测试与复用

2. **明确区分 public / private 头文件（逻辑层面）**
   - 即使物理上都在 `src/` 下

3. **Debug 场景优先保证可调试性**
   - 构建性能不是首要目标

---

> 本规范 v1.0（扩展）回应了 GPU 集成与测试体系的审计意见。
> 生成器限制（VS 2022）相关问题不在本次响应范围内。

