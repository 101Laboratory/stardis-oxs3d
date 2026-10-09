# Windows 环境测试修复循环 SOP（Run → Read → Locate → Patch → Repeat）

> 目的：在 **Windows + MSVC + CMake** 环境下，将“测试全绿”作为唯一验收标准，通过标准化循环流程快速定位并修复平台差异问题。
>
> 本 SOP **与具体报错无关**，强调可重复、可自动化（LLM 可按步骤循环执行，直到测试通过）。

---

## 0. 成功标准（Success Criteria）

| 类型 | 标准 | 可验证方式 | Pass/Fail |
|---|---|---|---|
| Functional | 所有测试在 Windows 上能运行并通过 | `ctest` | `100% tests passed` |
| Observable | 失败时能得到可定位的错误信息（文件/行号/错误码/栈） | `--output-on-failure` / `-V` 日志 | 日志包含可操作线索 |
| Build | Debug 构建无编译/链接错误 | `cmake --build` | Exit code 0 |

> 任何“应该可以了”都不算完成：**必须以构建/测试输出为证据**。

---

## 1. 前置约定（必须遵守）

### 1.1 修改规范（非常重要）

1. **可回滚**：所有改动必须能通过版本控制系统回滚（不允许直接在二进制文件/生成目录中修改）。在构建前请确保工作目录干净并commit。
2. **平台隔离**：任何 Windows 特有行为必须使用条件编译包裹：
   - `#if defined(OS_WINDOWS)`（优先）
   - `#if defined(_MSC_VER)`（仅当确实与 MSVC 编译器绑定）
3. **优先修库代码，不优先改测试**：
   - 默认认为测试是“规格”。
   - 只有当测试本身依赖平台不成立的假设（路径/动态库后缀/线程 API/时钟 API/编码等）时，才允许在测试里加平台分支。
4. **最小改动**：一次迭代只解决一个直接失败点，避免顺手重构。
5. **禁止作弊式通过**：
   - 不删测试
   - 不降低断言语义（除非该语义在 Windows 不可验证，且必须加条件编译并写明原因）
   - 不通过关闭警告/放宽编译选项掩盖真实问题
6. **不引入“全局污染”**：
   - 避免在公共头文件里直接 `#include <windows.h>`（除非已经有统一平台层）。
   - Windows 专用头尽量放在 `src/windows/*` 或平台适配层中。

---

## 2. 标准循环流程（LLM 必须按此循环直到全绿）

> 循环结构：**构建 → 运行测试 → 读取错误 → 定位文件 → 最小修复 → 局部验证 → 全量验证**

### Step 0：配置项目（首次或清理后）

#### 0.1 单模块构建（单独测试一个库）
```powershell
cd <module>/<version>
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..
```

**示例**：
```powershell
cd stardis-cpu_bak/rsys/0.15
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..
```

#### 0.2 Workspace 全量构建（所有模块）
```powershell
cd stardis-cpu_bak
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..
```

#### 0.3 控制测试生成

**启用测试**（默认）：
```powershell
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=ON ..
```

**禁用测试**（仅构建库，加快构建速度）：
```powershell
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=OFF ..
```

**验收**：CMake 配置成功，无错误。

---

### Step A：构建（Debug）

#### A1. 多配置生成器（Visual Studio / Ninja Multi-Config）
```powershell
cmake --build . --config Debug
```

#### A2. 单配置生成器（Ninja / Unix Makefiles，out-of-source）
```powershell
cmake --build build --config Debug
```

**验收**：命令退出码为 0。

---

### Step B：跑全量测试（收敛基线）

#### B1. 单模块测试（在模块的 build 目录中）
```powershell
cd <module>/<version>/build
ctest -C Debug --output-on-failure
```

**示例**：
```powershell
cd stardis-cpu_bak/rsys/0.15/build
ctest -C Debug --output-on-failure
```

#### B2. Workspace 全量测试（在 Workspace 根目录的 build 中）
```powershell
cd stardis-cpu_bak/build
ctest -C Debug --output-on-failure
```

#### B3. 测试子集（按模块/功能过滤）
```powershell
# 只跑特定模块的测试（使用正则表达式）
ctest -C Debug -R "^rsys_" --output-on-failure

# 排除某些测试
ctest -C Debug -E "test_library" --output-on-failure
```

**验收**：若失败，输出必须包含失败测试名 + 失败原因（断言行号 / MSVC error code / 崩溃信息）。

---

### Step C：隔离失败（缩短迭代周期）

1. 只跑单测：
```powershell
ctest -C Debug -R "^test_name$" --output-on-failure
```

2. 查看更详细输出（强制 verbose）：
```powershell
ctest -C Debug -R "^test_name$" -V
```

3. 对“可能卡住/超时”的测试增加超时并确认是否为 hang：
```powershell
ctest -C Debug -R "^test_name$" --timeout 30 --output-on-failure
```

4. 将日志落盘（便于定位和复盘）：
```powershell
ctest -C Debug -R "^test_name$" -V --output-on-failure > test_name.windows.log
```

---

### Step D：读取错误并分类（只做客观归类）

按优先级判断失败类别（只选一个主因）：

1. **编译错误**：`error Cxxxx` / include 冲突 / 标准不兼容
2. **链接错误**：`LNKxxxx` / DLL 导出符号缺失 / 依赖库缺失
3. **运行崩溃**：Access Violation / abort / assertion
4. **断言失败**：宏 `CHK/OK/BA` 指向具体文件和行号
5. **超时**：测试没有退出（死锁 / 等待 I/O / 等待线程 / DLL 加载卡住等）

---

### Step E：定位文件与最小修复（遵守修改规范）

#### E1. 以“日志中的文件:行号”为第一定位点
- 断言失败通常已提供：`path:line`。
- 编译/链接错误按 MSVC 输出定位具体 `.c/.h`。

#### E2. 典型跨平台修复手段（不依赖具体报错）
- **线程/同步**：pthread → Win32（CRITICAL_SECTION / SRWLOCK / CONDITION_VARIABLE）封装层
- **动态库加载**：`dlopen/dlsym` → `LoadLibrary/GetProcAddress`（注意路径/后缀/当前工作目录）
- **时间/时钟**：`clock_gettime` → FILETIME / QueryPerformanceCounter（依据测试需求选用）
- **字符串 API**：`strtok_r` → `strtok_s`
- **格式化输出**：Windows 上 `long` 可能为 32 位，`int64_t` 用 `%lld` / `PRIi64`
- **路径/二进制模式**：文本/二进制打开差异、`\`/`/`、`O_BINARY`

> 每次只做一个最小改动，立刻进入 Step F。

---

### Step F：局部验证（只跑受影响的最小集合）

```powershell
ctest -C Debug -R "^test_name$" --output-on-failure
```

如果修的是库实现，建议跑一组相关测试（按模块名过滤）：
```powershell
ctest -C Debug -R "test_(module|feature)" --output-on-failure
```

---

### Step G：全量验证（必须）

```powershell
ctest -C Debug --output-on-failure
```

通过后必须满足：
- `100% tests passed`
- 没有新的“Not Run”（如果存在，必须解释为何未构建/未注册/被跳过，并补齐流程）

---

## 3. 处理超时 / hang 的专用 SOP

1. 先把超时缩短，确认是否 hang：
```powershell
ctest -C Debug -R "^test_name$" --timeout 10 --output-on-failure
```

2. 如果超时但日志指向断言行：优先修断言前置条件（例如 DLL 路径拼接、工作目录、依赖 DLL 缺失）。
3. 如果无输出直接卡住：
   - 优先怀疑：锁/条件变量等待、线程 join/等待、动态库加载等待、阻塞 I/O。

---

## 4. Windows 条件编译模板（推荐）

### 4.1 OS 维度
```c
#if defined(OS_WINDOWS)
  /* Windows-specific implementation */
#else
  /* Unix-like implementation */
#endif
```

### 4.2 编译器维度
```c
#if defined(_MSC_VER)
  /* MSVC-specific workaround */
#endif
```

> 选择原则：能用 OS 维度就不要用编译器维度。

---

## 5. 证据要求（每次循环必须留存）

每次修复完成，必须记录：
1. **你跑了什么命令**（完整命令行）
2. **输出关键摘要**（失败测试名/断言行号/错误码）
3. **你改了哪些文件**（路径列表）
4. **局部测试结果** + **全量测试结果**

---

## 6. 完成定义

当且仅当满足以下条件，本 SOP 循环结束：
- [ ] `cmake --build ... --config Debug` 成功
- [ ] `ctest -C Debug --output-on-failure` 显示 `100% tests passed`
- [ ] Windows 专用改动全部有条件编译隔离
- [ ] 未通过“修改测试预期值/删测试/降低正确性”来获得假绿
