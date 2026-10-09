# Makefile → CMake + Windows (MSVC) 迁移 SOP v2.0  
**目标：测试全部通过，支持 VSCode 调试**  
**新增：深度平台绑定代码的迁移策略**
**新增：接口审查规则**
**修改：暂不执行接口审查**

---

## 0. 迁移边界与验收标准（强制）

### 0.1 迁移目标
- 将基于 Makefile 的 Linux 项目迁移至 CMake
- 在 Windows 上使用 **MSVC 工具链**
- 审查模块使用和导出的接口（见章节8.接口审查）
- 在 **VSCode** 中支持单步调试
- **所有测试必须通过**

### 0.2 明确不追求的目标
- 不追求 ELF / GNU / glibc / objcopy 行为等价
- 不追求 hardened flags 等价
- 不追求二进制布局一致
- 不追求编译器扩展的精确语义等价

### 0.3 唯一验收标准
- **Linux + CMake：测试全绿**
- **Windows + MSVC + CMake：测试全绿**

### 0.4 行为准则（LLM 强制约束）

- LLM **必须严格依据本文档中的明确文本与规则执行迁移**
- **不得做任何形式的假设、补全或无根据的推断**
- **不得引入文档中未明确给出的映射关系或平台语义**
- 当遇到以下情况之一时：
  - 原始 Makefile / config.mk 中存在但本文档 **未定义处理规则**
  - 构建行为无法在当前平台下确定等价或可替代语义
  - 映射结果存在多种可能且文档未明确指定

  **LLM 必须停止自动迁移该部分，并将异常记录到 `migration_exception.md`**

- `migration_exception.md` 中每条异常记录必须至少包含：
  - 原始规则或片段（原文）
  - 所属文件（如 Makefile / config.mk / .h）
  - 无法迁移的原因（客观描述，不做推断）
  - 影响范围（构建 / 运行 / 测试）

### 0.5 新增：深度绑定代码迁移原则
- **对编译器扩展的代码必须提供跨平台实现**
- **原子操作必须保留其跨平台语义保证**
- **内存对齐要求必须保持跨平台一致性**
- **符号可见性必须适配目标平台**
- **SIMD指令检测需适配目标编译器**

---

## 1. 输入工件（必须已存在）

- 原始 `Makefile`
- 原始 `config.mk`
- **项目头文件**（如 `rsys.h` 等包含平台绑定的头文件）
- 可运行的测试集（**测试即规格**）

---

## 2. 阶段一：Windows CMake 迁移 + Linux 验证（实战流程）

### 2.1 构建目标
- 在 **Windows** 上创建带平台隔离的 CMakeLists.txt
- 使用 MSVC 工具链
- 同时支持 Windows 和 Linux 分支（通过 `#ifdef` 隔离）

### 2.2 操作步骤
1. 在 Windows 上创建 `CMakeLists.txt`，包含：
   - 平台检测（`if(WIN32)` / `if(UNIX)`）
   - 构建目标声明（library / executable）
   - 源文件列表
2. 将 Makefile 构建规则映射为 CMake：
   - `CFLAGS_*` → `target_compile_options`
   - `-DXXX` → `target_compile_definitions`
   - `LIBS` → `target_link_libraries`
   - `LDFLAGS_*` → `target_link_options`
3. 平台特化行为：
   - **Linux 分支**：保留 `objcopy / strip`、hardened linker flags
   - **Windows 分支**：使用 MSVC flags (`/W4`, `/O2`, `/Zi`)
4. 在 Windows 上完成测试修复（按照测试修复循环 SOP）
5. **验证步骤**：将修复后的源文件拷贝回 Linux 原始环境

### 2.3 Linux 验证流程（拷贝回验证）

**重要**：Linux 环境**保持使用原有 Makefile 构建**，不需要迁移到 CMake。

#### 验证步骤
1. **拷贝修复文件回 Linux**：
   ```bash
   # 从 Windows 拷贝到 WSL
   cp /mnt/d/Works/Projects/<project>/src/<modified_file>.c \
      /home/<user>/<original_linux_path>/src/
   ```

2. **使用原有 Makefile 构建**：
   ```bash
   cd /home/<user>/<original_linux_path>
   make clean
   make
   ```

3. **运行关键测试**（需设置 LD_LIBRARY_PATH）：
   ```bash
   export LD_LIBRARY_PATH=.
   ./test_math
   ./test_float33
   ./test_double33
   # 或其他关键测试
   ```

4. **验证通过条件**：
   - 编译无错误（GCC 接受修改后的代码）
   - 关键精度测试通过（确认跨平台一致性）
   - 平台隔离代码（`#ifdef OS_WINDOWS`）不影响 Linux 构建

> 本阶段产物是 **Windows CMake 构建系统 + Linux 原生 Makefile 交叉验证**

---

## 3. 阶段二：平台语义解耦（核心阶段）

### 3.1 原则
- 构建逻辑与平台实现分离
- 测试定义行为边界
- 平台差异必须 **显式存在**
- **深度绑定代码必须提供平台适配层**

### 3.2 新增：编译器/平台绑定代码适配策略

#### 3.2.1 编译器检测宏迁移规则
```C
/* 原始 GCC 检测 */
#ifdef __GNUC__
#define COMPILER_GCC
#else
#error "Unsupported compiler"
#endif

/* 迁移后：支持跨平台 */
#if defined(__GNUC__)
  #define COMPILER_GCC
#elif defined(_MSC_VER)
  #define COMPILER_MSVC
#else
  #error "Unsupported compiler"
#endif
```

#### 3.2.2 原子操作跨平台适配
```C
/* GCC 原子操作 */
#define ATOMIC_INCR(A) __sync_add_and_fetch((A), 1)
#define ATOMIC_CAS(Atom, NewVal, Comparand) \
  __sync_val_compare_and_swap((Atom), (Comparand), (NewVal))

/* Windows/MSVC 原子操作适配 */
#if defined(COMPILER_MSVC)
  #include <windows.h>
  #define ATOMIC_INCR(A) InterlockedIncrement64(A)
  #define ATOMIC_CAS(Atom, NewVal, Comparand) \
    InterlockedCompareExchange64((Atom), (NewVal), (Comparand))
#endif
```

#### 3.2.3 符号可见性适配
```C
/* GCC/Linux 可见性 */
#if defined(COMPILER_GCC)
  #define EXPORT_SYM __attribute__((visibility("default")))
  #define LOCAL_SYM __attribute__((visibility("hidden")))
#endif

/* Windows/MSVC 可见性 */
#if defined(COMPILER_MSVC)
  #define EXPORT_SYM __declspec(dllexport)
  #define IMPORT_SYM __declspec(dllimport)
  #define LOCAL_SYM
#endif
```

#### 3.2.4 内联控制适配
```C
/* GCC 内联控制 */
#if defined(COMPILER_GCC)
  #define FINLINE __inline__ __attribute__((always_inline))
  #define NOINLINE __attribute__((noinline))
#endif

/* MSVC 内联控制 */
#if defined(COMPILER_MSVC)
  #define FINLINE __forceinline
  #define NOINLINE __declspec(noinline)
#endif
```

#### 3.2.5 数据对齐适配
```C
/* GCC 对齐控制 */
#if defined(COMPILER_GCC)
  #define ALIGN(Size) __attribute__((aligned(Size)))
  #define ALIGNOF(Type) __alignof__(Type)
#endif

/* MSVC 对齐控制 */
#if defined(COMPILER_MSVC)
  #define ALIGN(Size) __declspec(align(Size))
  #define ALIGNOF(Type) __alignof(Type)
#endif
```

### 3.3 操作步骤
1. 在 CMake 中引入平台分支：
```C
if(WIN32)
  # Windows behavior
else()
  # Linux behavior
endif()
```

2. 将构建要素分类为：
   - **平台无关**
     - 源文件
     - 公共头文件
     - 核心逻辑宏
   - **Linux-only**
     - pthread
     - dl
     - objcopy
     - ELF linker flags
   - **Windows-only**
     - MSVC 编译选项
     - Windows CRT 定义
     - Windows SDK 头文件
   - **平台适配层**
     - 原子操作实现
     - 符号可见性宏
     - 内联/对齐控制

3. 创建平台适配头文件（如 `platform_adapt.h`）
   - 集中管理所有平台相关宏
   - 提供统一的跨平台接口

4. 禁止在 Windows 分支中模拟 ELF / GNU 行为

---

## 4. 阶段三：Windows (MSVC) 构建实现

### 4.1 工具链选择（强制）
- MSVC
- CMake Generator：Visual Studio 17 2022
- IDE：VSCode

### 4.2 Windows 编译配置
- 使用 MSVC 原生 flags
- 禁用所有 Linux-only 行为：
  - `-fstack-protector-*`
  - `-Wl,-z,*`
  - `objcopy`
  - `strip`
- 添加 Windows 必要的编译定义

示例：
```CMake
if(MSVC)
  target_compile_options(my_target PRIVATE 
    /W4           # 警告级别
    /wd4200       # 禁用特定警告（如需要）
    /O2           # 优化级别
  )
  
  target_compile_definitions(my_target PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _WIN32_WINNT=0x0A00  # Windows 10
    NOMINMAX             # 防止 min/max 宏冲突
  )
  
  # Windows 动态库配置
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(my_target PRIVATE
      MYLIB_EXPORTS
    )
  endif()
endif()
```

### 4.3 动态库命名适配
```CMake
# CMake 中统一管理库命名
if(WIN32)
  set(SHARED_LIBRARY_SUFFIX ".dll")
  set(STATIC_LIBRARY_SUFFIX ".lib")
else()
  set(SHARED_LIBRARY_SUFFIX ".so")
  set(STATIC_LIBRARY_SUFFIX ".a")
endif()
```

### 4.4 Windows DLL 测试配置（关键！）

**问题根源**：Windows 上运行测试时，测试可执行文件需要在运行时找到依赖的 DLL 文件。CMake 生成的测试可执行文件位于独立的测试目录，而 DLL 在库的输出目录，导致加载失败。

**错误的做法**（仅设置 PATH 环境变量）：
```cmake
# ❌ 仅设置 PATH - 在某些配置下不可靠
if(WIN32)
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "PATH=${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>\;$ENV{PATH}"
    )
endif()
```

**正确的做法**（使用 `add_custom_command` 复制 DLL）：
```cmake
# ✅ 复制 DLL 到测试目录 - 可靠且明确
if(WIN32 AND BUILD_SHARED_LIBS)
    # 复制主库 DLL
    add_custom_command(TARGET ${TEST_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mylib>
            $<TARGET_FILE_DIR:${TEST_NAME}>
        COMMENT "Copying mylib.dll to ${TEST_NAME} directory"
    )
    
    # 复制依赖库 DLL（使用 $<TARGET_FILE:> 自动处理配置）
    add_custom_command(TARGET ${TEST_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rsys>
            $<TARGET_FILE_DIR:${TEST_NAME}>
        COMMENT "Copying rsys.dll to ${TEST_NAME} directory"
    )
    
    # 复制外部 DLL（如 embree4）
    set(EMBREE4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../embree4")
    file(GLOB EMBREE4_DLLS "${EMBREE4_ROOT}/bin/*.dll")
    if(EMBREE4_DLLS)
        foreach(dll ${EMBREE4_DLLS})
            add_custom_command(TARGET ${TEST_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${dll}
                    $<TARGET_FILE_DIR:${TEST_NAME}>
                COMMENT "Copying embree DLL to ${TEST_NAME} directory"
            )
        endforeach()
    endif()
endif()
```

**设置工作目录**：
```cmake
# 设置测试工作目录为可执行文件所在目录
set_tests_properties(${TEST_NAME} PROPERTIES
    WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_NAME}>
)
```

**完整测试配置示例**（参考 star-3d 和 star-enclosures-3d）：
```cmake
if(MYLIB_BUILD_TESTS)
    enable_testing()
    
    # 定义测试辅助函数
    function(add_mylib_test TEST_NAME)
        add_executable(${TEST_NAME} src/${TEST_NAME}.c)
        target_include_directories(${TEST_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${RSYS_INCLUDE_DIR}/src
        )
        target_link_libraries(${TEST_NAME} PRIVATE mylib rsys)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        
        # 设置工作目录
        set_tests_properties(${TEST_NAME} PROPERTIES
            WORKING_DIRECTORY $<TARGET_FILE_DIR:${TEST_NAME}>
        )
        
        # Windows DLL 复制（关键步骤）
        if(WIN32 AND BUILD_SHARED_LIBS)
            # 复制主库 DLL
            add_custom_command(TARGET ${TEST_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:mylib>
                    $<TARGET_FILE_DIR:${TEST_NAME}>
                COMMENT "Copying mylib.dll to ${TEST_NAME} directory"
            )
            
            # 复制所有依赖 DLL
            add_custom_command(TARGET ${TEST_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:rsys>
                    $<TARGET_FILE_DIR:${TEST_NAME}>
                COMMENT "Copying rsys.dll to ${TEST_NAME} directory"
            )
            
            # 如有外部 DLL（embree, tbb 等），也要复制
            set(EXTERNAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../external")
            file(GLOB EXTERNAL_DLLS "${EXTERNAL_ROOT}/bin/*.dll")
            if(EXTERNAL_DLLS)
                foreach(dll ${EXTERNAL_DLLS})
                    add_custom_command(TARGET ${TEST_NAME} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${dll}
                            $<TARGET_FILE_DIR:${TEST_NAME}>
                        COMMENT "Copying external DLL to ${TEST_NAME} directory"
                    )
                endforeach()
            endif()
        endif()
    endfunction()
    
    # 使用辅助函数定义测试
    add_mylib_test(test_feature1)
    add_mylib_test(test_feature2)
endif()
```

**关键要点**：
1. **使用 `$<TARGET_FILE:target>`** - 自动处理 Debug/Release 配置差异
2. **使用 `$<TARGET_FILE_DIR:target>`** - 获取目标文件所在目录
3. **每个测试都需要复制 DLL** - 因为每个测试在独立目录中
4. **复制所有依赖** - 包括直接依赖（rsys, s3d）和间接依赖（embree, tbb）
5. **使用 `copy_if_different`** - 避免不必要的复制，加快增量构建

---

## 5. 系统依赖适配策略（仅满足测试）

### 5.1 pthread
- 使用：
  - `std::thread`（C++11）
  - 或 Windows 线程封装
- 仅实现测试覆盖的行为子集

### 5.2 dlopen / dlsym
- 使用 `LoadLibrary / GetProcAddress`
- 行为以测试期望为准
- 创建统一的动态库加载抽象层

### 5.3 math library
- 使用 MSVC CRT / 编译器内建实现

### 5.4 新增：编译器内置函数适配
- `__builtin_expect` → 定义为空宏（仅影响性能）
- `__attribute__((unused))` → `__declspec(unused)` 或宏处理
- `__restrict__` → `__restrict`

---

## 6. 测试驱动迁移原则

### 6.1 允许的修改
- 最小化平台条件编译（`#ifdef _WIN32`）
- 屏蔽平台不可验证的断言
- 添加平台适配层抽象
- 修改头文件以支持跨平台编译

### 6.2 禁止的修改
- 修改测试期望值
- 放宽逻辑正确性判断
- 删除测试以"通过构建"
- 移除必要的平台检测和错误处理

### 6.3 新增：平台适配测试
- 为每个平台适配功能添加测试
- 验证原子操作的内存序保证
- 验证对齐保证的一致性
- 验证动态库加载/符号查找行为

### 6.4 测试修复循环 SOP（Windows）
- 按照《[Windows 环境测试修复循环 SOP](./windows_test_fix_loop.md)》执行：运行测试 → 读取报错 → 定位文件 → 最小修复 → 重复直到全绿

---

## 7. VSCode 调试接入

### 7.1 必要插件
- **C/C++**（`ms-vscode.cpptools`）
- **CMake Tools**

### 7.2 调试器配置
```JSON
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Windows Debug",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/Debug/myapp.exe",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "preLaunchTask": "build-debug"
    },
    {
      "name": "Linux Debug",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/myapp",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "build-debug"
    }
  ]
}
```

### 7.3 要求
- Debug 构建生成 PDB（Windows）/ Debug符号（Linux）
- 断点、变量、调用栈可用
- 支持源码级调试

---

## 8. 接口审查

**该步骤不适用，跳过此步骤**

LLM 在迁移时需要对模块使用和导出的接口进行审查，确保接口符合Windows跨CRT要求。
具体流程参见[接口审查 SOP](./interface_audit_sop_v3_simplified.md)。

---

## 9. 验收清单（Checklist）

### Windows（主要开发平台）
- [ ] CMake 配置成功（单模块：`-DENABLE_TESTS=ON`；Workspace：在根目录配置）
- [ ] MSVC Debug 构建成功
- [ ] MSVC Release 构建成功
- [ ] 平台适配头文件正确工作
- [ ] VSCode 可调试
- [ ] Debug 配置：测试全部通过
- [ ] Release 配置：测试全部通过
- [ ] 关键精度测试验证（test_real33.h, test_math.c）

### Workspace 架构（v2.0 新增）
- [ ] 工程级 CMakeLists.txt 正确加载所有模块
- [ ] ProjectOptions.cmake 统一管理编译选项
- [ ] HelperFunctions.cmake 辅助函数正确工作
- [ ] 单模块构建和 Workspace 构建均成功
- [ ] ENABLE_TESTS 全局控制测试生成

### 跨平台一致性
- [ ] 浮点精度标准统一（如 rotation matrix epsilon = 1.e-6）
- [ ] volatile 除零处理在两平台均正确
- [ ] 原子操作测试通过（多线程安全）
- [ ] 内存对齐测试通过
- [ ] 动态库加载测试通过
- [ ] 编译器扩展功能测试通过

---

## 9. 迁移完成定义

> 当且仅当：  
> - **Windows CMake + MSVC 测试全部通过**（Debug & Release）  
> - **修复文件拷贝回 Linux 环境后，原有 Makefile 构建成功**  
> - **Linux 关键精度测试通过**（验证跨平台一致性）  
> - **平台适配层完整实现**  
> - **跨平台行为一致性得到验证**  
> 本迁移视为成功。

### 9.1 实战流程总结
```
Windows 开发 → Linux 验证
    ↓              ↓
CMake构建      Makefile构建
MSVC测试       GCC关键测试
    ↓              ↓
   修复 ────拷贝───→ 验证
    ↑              ↓
    └──────反馈─────┘
```

---

## 10. 工程声明（推荐写入文档）

> *Windows 平台迁移以测试覆盖行为作为一致性标准，  
> 平台相关的二进制、安全与工具链差异已显式隔离。  
> 深度绑定代码通过平台适配层实现跨平台兼容，  
> 核心语义（原子性、内存序、对齐）保持跨平台一致。*

---

## 附录A：常见编译器扩展迁移对照表

| GCC/Clang 扩展 | Windows/MSVC 等价物 | 备注 |
|----------------|---------------------|------|
| `__attribute__((visibility("default")))` | `__declspec(dllexport)` | 动态库导出 |
| `__attribute__((visibility("hidden")))` | (无直接等价物) | Windows默认隐藏 |
| `__sync_add_and_fetch` | `InterlockedAdd` | 原子操作 |
| `__sync_val_compare_and_swap` | `InterlockedCompareExchange` | 原子CAS |
| `__attribute__((aligned(N)))` | `__declspec(align(N))` | 数据对齐 |
| `__attribute__((always_inline))` | `__forceinline` | 强制内联 |
| `__attribute__((noinline))` | `__declspec(noinline)` | 禁止内联 |
| `__builtin_expect` | (无直接等价物) | 可定义为空宏 |
| `__restrict__` | `__restrict` | 限制指针别名 |
| `__alignof__(type)` | `__alignof(type)` | 类型对齐查询 |
| `__FUNCTION__` | `__FUNCTION__` | 通用 |
| `__VA_COPY` | `va_list` 可赋值 | 可变参数复制 |

## 附录B：迁移风险评估矩阵

| 风险项 | 影响范围 | 缓解措施 |
|--------|----------|----------|
| 原子操作语义差异 | 高（多线程安全） | 使用平台原生API，增加并发测试 |
| 内存对齐保证差异 | 中（数据结构布局） | 使用编译器对齐指令，验证结构体大小 |
| 符号可见性差异 | 低（动态库ABI） | 使用平台适配宏，测试动态库加载 |
| SIMD指令集检测 | 低（性能优化） | 条件编译，提供非SIMD回退路径 |
| 编译器内置函数缺失 | 中（编译错误） | 提供平台适配实现或空实现 |
| 动态库加载机制差异 | 中（插件系统） | 抽象动态库加载层，统一接口 |

---

## 附录C：实战流程示例（RSys 0.15 迁移）

### C.0 新架构说明（v2.0 - Workspace 统一管理）

**架构层级**：
```
Workspace 级（stardis-cpu_bak/）
├── CMakeLists.txt                   # 工程级主配置
├── cmake/
│   ├── ProjectOptions.cmake         # 统一编译选项（MSVC/GCC 适配）
│   ├── CompilerPolicy.cmake         # 编译器检查
│   ├── RuntimeDeps.cmake            # DLL 部署
│   ├── ThirdPartyRegistry.cmake     # 第三方库（Random123, MPI）
│   └── HelperFunctions.cmake        # 测试配置辅助函数
└── <module>/<version>/
    └── CMakeLists.txt               # 项目级配置（极简）
```

**关键变化**：
- ✅ 编译选项统一到 Workspace 级（不再每个模块重复配置）
- ✅ 测试控制统一为 `ENABLE_TESTS`（替代 `<MODULE>_BUILD_TESTS`）
- ✅ 辅助函数 `add_module_tests()` 简化测试配置
- ✅ 支持单模块构建和 Workspace 全量构建

### C.1 Windows 开发阶段

#### 步骤1：配置 CMake（两种模式）

**模式A：单模块构建**（开发/调试单个库）
```bash
cd D:\Works\Projects\Stardis-GPU\stardis-cpu_bak\rsys\0.15
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..
```

**模式B：Workspace 全量构建**（构建所有模块）
```bash
cd D:\Works\Projects\Stardis-GPU\stardis-cpu_bak
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..
```

**Workspace 全量构建**（所有模块）：
```bash
cd D:\Works\Projects\Stardis-GPU\stardis-cpu_bak
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..
```

**控制测试生成**：
```bash
# 启用测试（默认）
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..

# 禁用测试（仅构建库/可执行文件）
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=OFF ..
```

**注意**：新架构使用统一的 `ENABLE_TESTS` 选项，替代旧版本的 `<MODULE>_BUILD_TESTS`。

#### 步骤2：构建并测试（Debug + Release）
```bash
# Debug 构建
cmake --build . --config Debug
ctest -C Debug --output-on-failure

# Release 构建
cmake --build . --config Release
ctest -C Release --output-on-failure
```

#### 步骤3：测试修复循环
- 运行测试发现失败
- 定位失败文件和行号
- 应用最小化修复（使用 `#ifdef OS_WINDOWS` 隔离）
- 重新构建和测试
- 重复直到 40/40 通过

**实际修复示例**：
```c
// test_math.c - 除零运行时化
volatile float zero_f = 0.f;
volatile double zero_d = 0.0;
CHK(1.f/zero_f == (float)INF);  // 原为 1.f/0.f

// test_real33.h - 精度统一
CHK(REALXY_FUNC__(eq_eps)(a, expected, (REAL)1.e-6));  
// 移除了 Windows 的 1.e-4 松弛
```

### C.2 Linux 验证阶段(步骤不适用)

**步骤不适用，跳过/拒绝执行此步骤**

#### 步骤1：拷贝修复文件
```bash
# 在 Windows 上执行
wsl bash -c "cp /mnt/d/Works/Projects/Stardis-GPU/stardis-cpu/rsys/0.15/src/test_math.c \
             /home/eric/star-build/cache/rsys/0.15/src/"

wsl bash -c "cp /mnt/d/Works/Projects/Stardis-GPU/stardis-cpu/rsys/0.15/src/test_real33.h \
             /home/eric/star-build/cache/rsys/0.15/src/"
```

#### 步骤2：Linux 构建（使用原有 Makefile）
```bash
wsl bash -c "cd /home/eric/star-build/cache/rsys/0.15 && make clean && make"
```

#### 步骤3：运行关键测试
```bash
wsl bash -c "cd /home/eric/star-build/cache/rsys/0.15 && \
             export LD_LIBRARY_PATH=. && \
             ./test_math && \
             ./test_float33 && \
             ./test_double33"
```

**预期输出**：
```
test_math: PASS
test_float33: PASS
test_double33: PASS
```

### C.3 验证清单确认

| 检查项 | Windows | Linux | 状态 |
|--------|---------|-------|------|
| 构建系统 | CMake | Makefile | ✅ |
| 编译器 | MSVC 19.34 | GCC (Ubuntu) | ✅ |
| Debug 测试 | 40/40 通过 | N/A | ✅ |
| Release 测试 | 40/40 通过 | N/A | ✅ |
| 关键精度测试 | 通过 | test_math/float33/double33通过 | ✅ |
| 精度标准 | 1.e-6 | 1.e-6 | ✅ 统一 |
| volatile 除零 | 正确 | 正确 | ✅ |
| 平台隔离 | `#ifdef OS_WINDOWS` | `#else` 分支 | ✅ |

**结论**：跨平台迁移成功，GPU 开发可以开始