# 接口审计 SOP v3.0 精简版（LLM可执行）

**版本**: 3.0 Simplified  
**日期**: 2026-01-18  
**目标**: 为LLM提供可立即执行的命令和步骤  
**基于**: v2.0（经RSys 0.15实战验证）

---

## 快速索引

| 你需要... | 跳转到 |
|----------|--------|
| **立即开始审计** | [执行流水线](#执行流水线) |
| **查找具体命令** | [命令速查表](#命令速查表) |
| **理解修复方法** | [修复模式速查](#修复模式速查) |
| **验证修复结果** | [验证清单](#验证清单) |

---

## 命令速查表

### 1. 审计范围定义（必须先执行）

```bash
# 设置模块路径（替换<module>和<version>）
export MODULE_NAME=rsys
export MODULE_VERSION=0.15
export MODULE_PATH=D:/Works/Projects/Stardis-GPU/stardis-cpu/${MODULE_NAME}/${MODULE_VERSION}

# 验证路径存在
ls "${MODULE_PATH}/include" "${MODULE_PATH}/src" || echo "ERROR: 路径不存在"
```

**关键约束**：
- ✅ **仅审计项目内部模块**（stardis-cpu/下的模块）
- ❌ **不审计外部依赖**（external/、third-party/）
- ❌ **不审计构建目录**（build/、Debug/、Release/）

### 2. 违规检测命令（按优先级排序）

#### 🔴 高优先级：FILE* 接口（必须修复）

```bash
# 在模块的include目录下搜索
cd "${MODULE_PATH}"
grep -rn "FILE\s*\*" --include="*.h" include/

# 预期输出：函数声明中包含FILE*参数的行
# 示例：include/rsys/image.h:123:RSYS_API res_T image_write_ppm_stream(..., FILE* stream);
```

#### 🟡 中优先级：va_list 接口（需判定跨模块调用）

```bash
# 搜索va_list声明
grep -rn "va_list" --include="*.h" include/

# 搜索调用点（判定是否跨模块）
grep -rn "<function_name>" --include="*.c" .

# 判定标准：
# - 所有调用在src/目录下 → 同模块内，可豁免
# - 有调用在其他模块（如../star-3d/）→ 跨模块，必须修复
```

#### 🟠 中优先级：内联函数中的CRT访问

```bash
# 搜索内联函数使用errno/malloc/fprintf等
grep -A 10 "static.*INLINE\|static inline" --include="*.h" include/ | grep -E "(errno|malloc|fprintf|strtod|printf)"

# 预期输出：内联函数中访问CRT函数的代码行
```

#### 🔵 低优先级：导出宏检测（上下文扫描）

```bash
# 搜索所有导出的函数
grep -rn "RSYS_API\|__declspec(dllexport)" --include="*.h" include/

# 手动检查这些函数的参数类型
```

### 3. AST级别精确搜索（可选，如有ast-grep）

```bash
# 搜索导出函数模式
ast-grep --pattern 'RSYS_API $RET $FUNC($$$)' --lang c include/

# 如果ast-grep不可用，跳过此步，依赖grep结果
```

### 4. 修复验证命令

#### Windows编译验证

```bash
cd "${MODULE_PATH}"

# 清理旧构建
rm -rf build_windows && mkdir build_windows && cd build_windows

# 配置并构建（Debug）
cmake -G "Visual Studio 17 2022" -A x64 -D${MODULE_NAME}_BUILD_TESTS=ON ..
cmake --build . --config Debug

# 配置并构建（Release）
cmake --build . --config Release

# 预期：Exit code 0，无编译错误
```

#### Windows测试验证

```bash
cd "${MODULE_PATH}/build_windows"

# 运行测试（Debug）
ctest -C Debug --output-on-failure

# 运行测试（Release）
ctest -C Release --output-on-failure

# 预期：100% tests passed
```

#### Linux编译验证（如有WSL）

```bash
cd "${MODULE_PATH}"
make clean && make

# 预期：Exit code 0
```

---

## 执行流水线

### 步骤 1: 范围识别（1分钟）

**输入**: 模块名和版本（如 rsys/0.15）

**操作**:
```bash
export MODULE_NAME=<模块名>
export MODULE_VERSION=<版本号>
export MODULE_PATH=D:/Works/Projects/Stardis-GPU/stardis-cpu/${MODULE_NAME}/${MODULE_VERSION}

# 验证路径
ls "${MODULE_PATH}/src" || exit 1
```

**输出**: 有效的模块路径

**决策**:
- ✅ 路径存在 → 继续步骤2
- ❌ 路径不存在 → 中止，检查模块名/版本

---

### 步骤 2: 违规扫描（2-5分钟）

**输入**: 模块路径

**操作**:
```bash
cd "${MODULE_PATH}"

# 扫描FILE*
grep -rn "FILE\s*\*" --include="*.h" src/ > violations_file.txt

# 扫描va_list
grep -rn "va_list" --include="*.h" src/ > violations_valist.txt

# 扫描内联函数CRT访问
grep -A 10 "static.*INLINE\|static inline" --include="*.h" src/ | \
  grep -E "(errno|malloc|fprintf|strtod|printf)" > violations_inline.txt
```

**输出**: 三个违规列表文件（violations_*.txt）

**决策**:
- 所有文件为空 → ✅ 无违规，跳转步骤7（文档更新）
- 有非空文件 → ⚠️ 有违规，继续步骤3

---

### 步骤 3: 违规分类（5-10分钟）

**输入**: violations_*.txt 文件

**操作**: 对每个违规项，记录：

| 文件 | 行号 | 函数名 | 违规类型 | 修复策略 |
|------|------|--------|----------|---------|
| include/rsys/image.h | 123 | image_write_ppm_stream | FILE* | 三层接口模型 |
| include/rsys/logger.h | 45 | logger_vprint | va_list | 判定调用范围 |
| ... | ... | ... | ... | ... |

**决策**:
- FILE* 违规 → 必须修复，使用[三层接口模型](#file-三层接口模型)
- va_list 违规 → 判定是否跨模块（运行调用点搜索）
  - 跨模块 → 必须修复
  - 同模块 → 可豁免，添加@internal注释
- 内联+CRT → 推荐修复，使用[内联函数修复](#内联函数crt访问修复)

**输出**: 修复计划表

---

### 步骤 4: 接口修改（时间视违规数量）

**输入**: 修复计划表

**操作**: 对每个违规项：

1. **修改头文件**（include/xxx.h）
   - 添加新接口（文件路径版或回调版）
   - 条件编译旧接口（`#ifndef OS_WINDOWS ... #endif`）

2. **修改实现文件**（src/xxx.c）
   - 实现新接口

3. **搜索所有调用点**
   ```bash
   grep -rn "<old_function_name>" --include="*.c" "${MODULE_PATH}"
   grep -rn "<old_function_name>" --include="*text_*.*" ../  
   ```

4. **修改调用点**（在**同一次提交**中）

**关键约束**:

- 必须在**同一次**修改接口定义和所有调用点
- 禁止分批修复（会导致构建失败）
- 禁止仅添加新接口而保留旧接口（用户会误用）

**输出**: 修改后的代码

---

### 步骤 5: 编译验证（5-10分钟）

**输入**: 修改后的代码

**操作**:
```bash
cd "${MODULE_PATH}"

# Windows Debug构建
mkdir -p build && cd build
cmake .. -A x64 -D${MODULE_NAME}_BUILD_TESTS=ON
cmake --build . --config Debug

# 检查退出码
if [ $? -ne 0 ]; then
  echo "ERROR: Debug构建失败"
  exit 1
fi

# Windows Release构建
cmake --build . --config Release

# 检查退出码
if [ $? -ne 0 ]; then
  echo "ERROR: Release构建失败"
  exit 1
fi
```

**决策**:

- 构建成功 → 继续步骤6
- 构建失败 → 回到步骤4，修复编译错误

---

### 步骤 6: 测试验证（5-10分钟）

**输入**: 编译成功的二进制文件

**操作**:
```bash
cd "${MODULE_PATH}/build"

# 运行所有测试
ctest -C Debug --output-on-failure
DEBUG_RESULT=$?

ctest -C Release --output-on-failure
RELEASE_RESULT=$?

# 检查结果
if [ $DEBUG_RESULT -ne 0 ] || [ $RELEASE_RESULT -ne 0 ]; then
  echo "ERROR: 测试失败"
  exit 1
fi
```

**决策**:
- 所有测试通过 → 继续步骤7
- 测试失败 → 分析失败原因
  - 预存在的失败 → 记录并继续（不修复）
  - 修复引入的失败 → 回到步骤4

---

### 步骤 7: 文档更新（5-10分钟）

**输入**: 验证通过的修复

**操作**: 更新以下文件：

#### 7.1 api_conflicts.md

```markdown
### <function_name>

**原接口（不安全）**
```c
RSYS_API res_T <function_name>(..., FILE* stream);
```

**问题原因**: 使用 FILE* 跨模块边界，Windows MSVC不兼容

**新接口（安全）**
```c
// 方案1：文件路径接口
RSYS_API res_T <function_name>_file(..., const char* path);

// 方案2：回调接口
typedef size_t (*write_callback)(const void*, size_t, void*);
RSYS_API res_T <function_name>_callback(..., write_callback fn, void* ctx);

// 方案3：Unix兼容（条件编译）
#ifndef OS_WINDOWS
RSYS_API res_T <function_name>(..., FILE* stream);
#endif
```

**修复日期**: YYYY-MM-DD
**影响范围**: 
- 模块: <module_name> <version>
- 受影响文件: <list_files>
- 调用方: <list_caller_modules>
```

#### 7.2 模块README.md

在README.md中添加平台差异说明：

```markdown
## Platform-Specific API Notes

### Windows
- `<old_function_name>` is NOT available on Windows due to CRT isolation.
- Use `<new_function_name>_file` or `<new_function_name>_callback` instead.

### Unix/Linux
- All APIs are available, including legacy `<old_function_name>`.
```

#### 7.3 头文件注释

在头文件中标注废弃：

```c
#ifndef OS_WINDOWS
/**
 * @deprecated Unix-only interface. Use <new_function_name>_file() on Windows.
 * @warning This function is NOT available on Windows due to CRT isolation.
 */
RSYS_API res_T <old_function_name>(..., FILE* stream);
#endif
```

**输出**: 更新的文档

---

### 步骤 8: 最终验收（2分钟）

**输入**: 所有修复和文档

**操作**: 检查验收清单（见下节）

**决策**:
- 所有检查项通过 → 审计完成
- 有未通过项 → 回到对应步骤修复

---

## 修复模式速查

### FILE* 三层接口模型

```c
/* 层级1: 文件路径接口（跨平台，推荐） */
RSYS_API res_T func_file(const struct data* d, const char* path);

/* 层级2: 回调接口（高级，最灵活） */
typedef size_t (*write_callback)(const void* data, size_t size, void* ctx);
RSYS_API res_T func_callback(const struct data* d, write_callback fn, void* ctx);

/* 层级3: FILE*接口（仅Unix，向后兼容） */
#ifndef OS_WINDOWS
RSYS_API res_T func_stream(const struct data* d, FILE* stream);
#endif
```

**调用方迁移**:
```c
/* 原调用 */
FILE* f = fopen("out.txt", "wb");
func_stream(&data, f);
fclose(f);

/* ✅ 新调用（方案1：文件路径） */
func_file(&data, "out.txt");

/* ✅ 新调用（方案2：回调） */
size_t my_write(const void* data, size_t size, void* ctx) {
  return fwrite(data, 1, size, (FILE*)ctx);
}
FILE* f = fopen("out.txt", "wb");
func_callback(&data, my_write, f);
fclose(f);
```

---

### va_list 修复

#### 场景A: 同模块内调用（豁免）

```bash
# 验证调用范围
grep -rn "logger_vprint" --include="*.c" .
# 输出：仅在src/logger.c中调用 → 同模块内

# 修复：添加@internal注释
```

```c
/**
 * @internal This function is for internal use only within this module.
 * @warning On Windows, va_list cannot be passed across module boundaries.
 */
RSYS_API res_T logger_vprint(struct logger* log, va_list args);
```

#### 场景B: 跨模块调用（必须修复）

```c
/* 移除va_list版本 */
// RSYS_API res_T logger_vprint(..., va_list args);  // 删除

/* 仅保留可变参数版本 */
RSYS_API res_T logger_print(struct logger* log, const char* fmt, ...);
```

---

### 内联函数CRT访问修复

#### 方案A: 条件禁用内联（Windows）

```c
#ifdef OS_WINDOWS
  #define FUNC_INLINE  /* 空，禁用内联 */
#else
  #define FUNC_INLINE INLINE
#endif

static FUNC_INLINE res_T cstr_to_double(const char* str, double* val) {
  errno = 0;
  *val = strtod(str, NULL);
  if(errno == ERANGE) return RES_BAD_ARG;
  return RES_OK;
}
```

#### 方案B: 重写为不依赖CRT状态（推荐）

```c
static INLINE res_T cstr_to_double_safe(const char* str, double* val) {
  char* end;
  *val = strtod(str, &end);
  /* 使用HUGE_VAL检测错误，无需errno */
  if(end == str || *val == HUGE_VAL || *val == -HUGE_VAL)
    return RES_BAD_ARG;
  return RES_OK;
}
```

#### 方案C: 改为非内联函数

```c
/* 在.h中声明 */
res_T cstr_to_double(const char* str, double* val);

/* 在.c中实现（不内联） */
res_T cstr_to_double(const char* str, double* val) {
  errno = 0;
  *val = strtod(str, NULL);
  if(errno == ERANGE) return RES_BAD_ARG;
  return RES_OK;
}
```

---

### malloc对象管理

#### ✅ 合规模式：配对create/destroy

```c
RSYS_API struct obj* obj_create(void);    /* 模块内分配 */
RSYS_API void obj_destroy(struct obj*);   /* 模块内释放 */

/* 用户调用 */
struct obj* o = obj_create();  /* 模块A分配 */
obj_destroy(o);                /* 模块A释放 → 安全 */
```

#### 违规模式：返回malloc对象期望用户free

```c
/* 错误 */
RSYS_API char* get_buffer(void);  /* 模块A分配 */
/* 用户代码 */
char* buf = get_buffer();
free(buf);  /* 模块B释放 → 崩溃 */

/* 修复方案1：返回只读指针 */
RSYS_API const char* get_buffer_readonly(void);

/* 修复方案2：用户提供缓冲区 */
RSYS_API res_T copy_buffer(char* buf, size_t size);

/* 修复方案3：提供释放函数 */
RSYS_API char* alloc_buffer(void);
RSYS_API void free_buffer(char* buf);
```

---

## 验证清单

### 编译验证

- [ ] Windows Debug 构建成功（exit code 0）
- [ ] Windows Release 构建成功（exit code 0）
- [ ] Linux 构建成功（如有）
- [ ] 无条件编译错误（Windows禁用FILE*接口时）

### 测试验证

- [ ] Windows Debug 测试全部通过
- [ ] Windows Release 测试全部通过
- [ ] Linux 测试全部通过（如有）
- [ ] 无新增测试失败（预存在失败已记录）

### 接口验证

- [ ] Windows分支无FILE*/va_list/errno跨模块接口（豁免的除外）
- [ ] 所有违规接口已修改定义
- [ ] 所有调用点已同步更新（无遗漏）
- [ ] 条件编译正确（`#ifndef OS_WINDOWS`）

### 文档验证

- [ ] `api_conflicts.md` 已记录所有修复
- [ ] 模块 README.md 已说明平台差异
- [ ] 头文件注释已标注 `@deprecated` 或平台限制
- [ ] 修复原因已记录（可选：`migration_exception.md`）

### 最终确认

- [ ] 接口定义和调用点在**同一次提交**中修改
- [ ] 未引入新的回归（回归测试通过）
- [ ] 豁免的va_list接口已添加`@internal`警告
- [ ] 内联函数CRT访问已修复或条件禁用

**全部通过 → 审计完成**

---

## 常见搜索关键词

### 检测违规接口

| 目标 | 关键词 | grep命令 |
|------|--------|---------|
| FILE*参数 | `FILE\s*\*` | `grep -rn "FILE\s*\*" --include="*.h" include/` |
| va_list参数 | `va_list` | `grep -rn "va_list" --include="*.h" include/` |
| errno访问 | `errno` | `grep -rn "errno" --include="*.h" include/` |
| malloc调用 | `malloc\|calloc\|realloc` | `grep -rn "malloc\|calloc\|realloc" --include="*.h" include/` |
| 内联函数 | `static.*INLINE\|static inline` | `grep -rn "static.*INLINE\|static inline" --include="*.h" include/` |
| 导出宏 | `RSYS_API\|__declspec(dllexport)` | `grep -rn "RSYS_API" --include="*.h" include/` |

### 检测调用点

| 目标 | 示例 |
|------|------|
| 特定函数调用 | `grep -rn "image_write_ppm_stream" --include="*.c" .` |
| 跨模块调用 | `grep -rn "image_write_ppm_stream" --include="*.c" ../` |
| 所有模块调用 | `grep -rn "image_write_ppm_stream" --include="*.c" ../../stardis-cpu/` |

---

## 替换范围明确指令

### 仅考虑模块内部（stardis-cpu/${MODULE_NAME}/）

```bash
# ✅ 正确：仅扫描stardis-cpu/${MODULE_NAME}下的文件
cd D:/Works/Projects/Stardis-GPU/stardis-cpu/${MODULE_NAME}/${MODULE_VERSION}
grep -rn "FILE\s*\*" --include="*.h" src/

# ✅ 正确：检查本模块的调用点
grep -rn "<function_name>" --include="*.c" src/

# ✅ 正确：检查模块测试代码的调用点
grep -rn "<function_name>" --include="test_*.*" src/


```

### 不考虑模块外部依赖

```bash
# ❌ 错误：检查其他模块的调用点（项目内）
grep -rn "<function_name>" --include="*.c" ../../*/*/src/
# ❌ 错误：不要扫描外部依赖
# grep -rn "FILE\s*\*" external/  # 不要执行

# ❌ 错误：不要扫描构建目录
# grep -rn "FILE\s*\*" build/     # 不要执行
# grep -rn "FILE\s*\*" Debug/     # 不要执行
# grep -rn "FILE\s*\*" Release/   # 不要执行
```

### 外部模块的审计依赖本审计的输出

如果外部模块（如star-3d）调用了本模块（如rsys）的接口：

1. **只完成本模块审计**（生成api_conflicts.md）
2. **不得审计外部模块**，只提供修复指南，不得审计外部模块。

### 本模块依赖前置模块的审计输出

如果本模块（如star-2d）调用了前置模块（如rsys）的接口：

1. **先检查前置模块审计结果**
   
```bash
# 示例：审计star-2d时，检查rsys的api_conflicts.md
cd ../../rsys/0.15
cat api_conflicts.md | grep "image_write_ppm_stream"
```

2. **本模块审计时**，参考前置模块的api_conflicts.md修复调用点
3. **不需要重新审计前置模块的接口**

```bash
# 示例：审计star-2d时，检查其对rsys接口的调用
cd ../../star-2d/0.20
grep -rn "image_write_ppm_stream" --include="*.c" src/

---

## 禁止类型速查

| 类型 | 风险 | 替换方案 |
|------|------|---------|
| `FILE*` | CRT结构不兼容 | `const char* path` 或 `write_callback fn + void* ctx` |
| `va_list` | ABI不兼容 | 仅保留 `func(...)` 可变参数版本，移除 `vfunc(va_list)` |
| `errno` | TLS地址不一致 | 返回错误码，不依赖全局状态 |
| `malloc对象` | 堆管理器不同 | `create/destroy` 配对，或用户提供缓冲区 |
| `jmp_buf` | 栈/ABI不兼容 | 显式错误返回（`res_T`） |

---

## LLM强制约束（重申）

| 约束类型 | 规则 |
|---------|------|
| ❌ 禁止假设 | Linux和Windows的CRT行为一致 |
| ❌ 禁止保留 | 仅添加新接口而保留违规旧接口 |
| ❌ 禁止分批 | 分批修改接口和调用点（必须同一提交） |
| ❌ 禁止自行判定 | va_list是否跨模块（必须代码搜索验证） |
| ❌ 禁止忽略 | 内联函数中的CRT资源访问 |
| ✅ 必须更新 | 修改接口后更新所有文档 |
| ✅ 必须执行 | 完整验证流程（编译+测试） |
| ✅ 必须记录 | 所有修复到api_conflicts.md |

**优先级**: 运行时安全 > 向后兼容性

---

## 附录：完整示例（RSys image模块）

### 检测违规

```bash
cd D:/Works/Projects/Stardis-GPU/stardis-cpu/rsys/0.15
grep -rn "FILE\s*\*" --include="*.h" include/

# 输出：
# include/rsys/image.h:245:RSYS_API res_T image_write_ppm_stream(const struct image* img, int bin, FILE* stream);
# include/rsys/image.h:250:RSYS_API res_T image_read_ppm_stream(struct image* img, FILE* stream);
```

### 修复接口（include/rsys/image.h）

```c
/* 新增：文件路径接口 */
RSYS_API res_T image_write_ppm_file(
    const struct image* img, int bin, const char* path);

RSYS_API res_T image_read_ppm_file(
    struct image* img, const char* path);

/* 新增：回调接口 */
typedef size_t (*image_write_callback)(const void* data, size_t size, void* ctx);

RSYS_API res_T image_write_ppm_callback(
    const struct image* img, int bin, image_write_callback fn, void* ctx);

/* 条件编译：Unix保留旧接口 */
#ifndef OS_WINDOWS
/**
 * @deprecated Unix-only. Use image_write_ppm_file() or image_write_ppm_callback() on Windows.
 * @warning NOT available on Windows due to CRT isolation.
 */
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);

RSYS_API res_T image_read_ppm_stream(
    struct image* img, FILE* stream);
#endif
```

### 实现新接口（src/image.c）

```c
res_T image_write_ppm_file(const struct image* img, int bin, const char* path) {
  FILE* f = fopen(path, "wb");
  if(!f) return RES_ERRNO;
  
  res_T result = image_write_ppm_stream_impl(img, bin, f);
  fclose(f);
  return result;
}

res_T image_write_ppm_callback(
    const struct image* img, int bin, image_write_callback fn, void* ctx) {
  /* 分块写入，调用回调 */
  /* ... 实现细节 ... */
}
```

### 修改调用点（test/test_image.c）

```c
/* ❌ 原调用 */
FILE* f = fopen("test.ppm", "wb");
CHK(image_write_ppm_stream(&img, 0, f) == RES_OK);
fclose(f);

/* ✅ 新调用 */
CHK(image_write_ppm_file(&img, 0, "test.ppm") == RES_OK);
```

### 验证编译

```bash
cd D:/Works/Projects/Stardis-GPU/stardis-cpu/rsys/0.15
mkdir -p build && cd build
cmake -A x64 -D rsys_BUILD_TESTS=ON ..
cmake --build . --config Debug
cmake --build . --config Release
```

### 验证测试

```bash
ctest -C Debug --output-on-failure
ctest -C Release --output-on-failure
```

### 更新文档（api_conflicts.md）

（参见步骤7.1的模板）

---

**版本历史**:
- v3.0 (2026-01-18): 精简版，LLM可直接执行的命令和流水线
- v2.0 (2026-01-18): 详细版，948行（包含大量解释）
- v1.0 (2026-01-16): 初版

**维护者**: Stardis-GPU项目团队  
**反馈**: 如命令无法执行，请报告具体错误信息
