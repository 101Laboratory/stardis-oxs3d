# CMake 工程级模板与工具集（Workspace SOP）

本文件包含：

1. **工程根目录 CMakeLists.txt 模板**（Workspace / Orchestrator）
2. **项目级 CMakeLists.txt 模板**（Library / Executable 通用）
3. **必须 include 的 cmake 工具模块模板**（最小可运行集）

---

## 一、工程根目录 CMakeLists.txt（必需，唯一入口）

> 位置：`/<根目录>/CMakeLists.txt`

[template_cmakelists_workspace_level.txt](../templates/template_cmakelists_workspace_level.txt)

---

## 二、项目级 CMakeLists.txt 模板（库 / 可执行统一模板）

> 位置：`/项目名/版本号/项目目录/CMakeLists.txt`

[template_cmakelists_module_level.txt](../templates/template_cmakelists_module_level.txt)

## 三、cmake 工具模块模板（必须存在）

### 1. `cmake/ProjectOptions.cmake`

[template_cmake_module_project_options.cmake](../templates/template_cmake_module_project_options.cmake)

---

### 2. `cmake/CompilerPolicy.cmake`

[template_cmake_module_compiler_policy.cmake](../templates/template_cmake_module_compiler_policy.cmake)

---

### 3. `cmake/RuntimeDeps.cmake`

[template_cmake_module_runtime_deps.cmake](../templates/template_cmake_module_runtime_deps.cmake)

---

### 4. `cmake/ThirdPartyRegistry.cmake`

```cmake
# 当前仅作为第三方注册入口占位
# 未来可扩展 CUDA / DX12 / MKL / 平台检测逻辑
```

---

## 四、规范性总结（可写入总规范）

- 工程根 CMakeLists.txt 是 **唯一 cmake 入口**
- 第三方依赖 **只在工程级注册**
- 项目级 CMakeLists **只定义 target 与 link 行为**
- Runtime 处理 **仅发生在可执行目标阶段**
- 模板结构允许 LLM 自动生成且不破坏工程一致性

