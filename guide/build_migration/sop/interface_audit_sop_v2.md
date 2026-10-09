# 接口审计 SOP v2.0（跨模块 CRT 资源隔离）

**版本**: 2.0  
**日期**: 2026-01-18  
**验证状态**: ✅ 已通过 RSys 0.15 实战验证（58 个接口，8 个违规检出）  

> **适用对象**: LLM / 自动迁移代理 / 人工审计  
> **适用场景**: 跨模块、跨平台（Linux → Windows）、跨编译器（GCC → MSVC）迁移  

---

## 目录

1. [核心原则](#1-核心原则)
2. [禁止跨模块传递的资源类型](#2-禁止跨模块传递的资源类型)
3. [LLM 执行步骤](#3-llm-执行步骤)
4. [修复策略](#4-修复策略)
5. [验证流程](#5-验证流程)
6. [附录](#6-附录)

---

## 1. 核心原则

### 1.1 总则（LLM 必须执行）

> **凡是跨模块边界的接口，禁止传递任何由 CRT 管理或隐式绑定 CRT 状态的资源。**

**背景**：
- Windows MSVC 允许不同模块使用不同的 CRT 运行时（/MD vs /MT，Debug vs Release）
- 不同 CRT 的 `FILE*`、`malloc` 堆、`errno` 等内部表示和管理函数**不兼容**
- Linux GCC 环境"看起来可行"的代码，在 Windows 下会**运行时崩溃**

**强制要求**：
- LLM 在迁移或重构过程中，**必须假设：Windows + MSVC + 不同 CRT = 不兼容**
- 一旦接口违反规则，**必须修改接口定义，并同步修改所有调用点**
- **不允许"先迁移，后修复"**——接口安全是构建成功的前提
- **不得禁用涉及问题接口的测试** ——所有测试的行为必须一致且通过

### 1.2 适用范围

**需要审计的接口**：
- ✅ 所有标记为导出的函数（如 `RSYS_API`, `__declspec(dllexport)`, `__attribute__((visibility("default")))`）
- ✅ 所有公共头文件中声明的函数
- ✅ 内联函数（`inline`, `static inline`, `FINLINE`）——会在调用方模块展开
- ⚠️ 静态函数（通常不跨模块，但如果在头文件中定义，需要审查）

**无需审计的接口**：
- ❌ `.c` 文件中的 `static` 函数（仅模块内部可见）
- ❌ 匿名命名空间中的函数（C++）

---

## 2. 禁止跨模块传递的资源类型

### 2.1 基本禁止类型

| 类型 | 风险原因 | 典型崩溃场景 |
|------|----------|-------------|
| **FILE*** | CRT 内部结构布局不兼容 | 模块 A 打开文件，模块 B 读取 → 访问错误偏移 → 崩溃 |
| **va_list** | 可变参数 ABI 和表示不兼容 | 模块 A 传入 va_list，模块 B 使用 va_arg → 读取错误参数 → 崩溃 |
| **jmp_buf** | 栈帧和异常处理 ABI 不兼容 | 跨模块 longjmp → 栈破坏 → 崩溃 |
| **errno** | 线程本地存储（TLS）地址不一致 | 模块 A 设置 errno，模块 B 读取 → 读到错误值或崩溃 |
| **malloc 对象** | 堆管理器不同 | 模块 A 分配内存，模块 B 释放 → 堆损坏 → 崩溃 |

### 2.2 特殊场景：内联函数中的 CRT 资源

> **规则**：内联函数会在调用方模块中展开，其内部的 CRT 访问视为发生在调用方模块。

**需要额外检查的情况**：
- ❌ 内联函数中读取/写入 `errno`
- ❌ 内联函数中调用 CRT 函数（如 `malloc`, `fprintf`, `strtod`, `printf` 等）
- ❌ 内联函数中使用 `va_arg` 宏

**示例（不安全）**：
```c
/* ❌ 错误：内联函数访问 errno */
static INLINE res_T cstr_to_double(const char* str, double* value) {
  char* end;
  errno = 0;  /* ⚠️ errno 访问会在调用方的 CRT 中展开 */
  *value = strtod(str, &end);
  if(errno == ERANGE) return RES_BAD_ARG;
  return RES_OK;
}
```

**修复方案（三选一）**：

**方案 A: Windows 分支禁用内联**
```c
#ifdef OS_WINDOWS
  #define CSTR_INLINE  /* 空，禁用内联 */
#else
  #define CSTR_INLINE INLINE
#endif

static CSTR_INLINE res_T cstr_to_double(const char* str, double* value) {
  char* end;
  errno = 0;
  *value = strtod(str, &end);
  if(errno == ERANGE) return RES_BAD_ARG;
  return RES_OK;
}
```

**方案 B: 重写为不依赖 CRT 状态（推荐）**
```c
static INLINE res_T cstr_to_double_safe(const char* str, double* value) {
  char* end;
  *value = strtod(str, &end);
  /* 使用返回值和 HUGE_VAL 检测错误，无需 errno */
  if(end == str || *value == HUGE_VAL || *value == -HUGE_VAL)
    return RES_BAD_ARG;
  return RES_OK;
}
```

**方案 C: 改为非内联函数**
```c
/* 在 .c 文件中实现，不内联 */
res_T cstr_to_double(const char* str, double* value);
```

**未处理则视为接口审查失败**。

### 2.3 特殊场景：va_list 的豁免规则

`va_list` **默认视为违规接口**，除非同时满足以下**所有条件**：

| 豁免条件 | 检查方法 |
|----------|---------|
| ✅ 所有调用点均在同一模块内 | 使用 grep/ast-grep 搜索所有调用位置 |
| ✅ 接口被明确标注为 `@internal` | 检查头文件注释 |
| ✅ 文档中明确禁止跨模块调用 | 检查 README 和 API 文档 |

**判定流程**：
1. 搜索项目中所有调用该 va_list 接口的位置（使用 `grep -r "function_name"` 或 `ast-grep`）
2. 检查调用方是否与接口定义在同一编译单元/模块
3. 如果所有调用都在同一模块内，标记为"内部接口，可豁免"
4. 在接口注释中增加警告

**示例（豁免修复）**：
```c
/**
 * @internal This function is for internal use only within the RSys module.
 * External modules should use logger_print() instead.
 * 
 * @warning On Windows, va_list cannot be passed across module boundaries due to CRT isolation.
 * @warning DO NOT call this function from external modules.
 */
RSYS_API res_T logger_vprint(
    struct logger* logger, enum log_type type, const char* log, va_list vargs);
```

**否则必须修复**：
- 移除 `va_list` 参数，改为可变参数宏包装
- 或提供 Windows 专用替代接口（参数数组）

**LLM 不得自行假设调用范围** — 必须通过代码搜索验证。

---

## 3. LLM 执行步骤

### 步骤 1: 扫描外部接口调用

**目标**：识别本模块调用的其他模块接口中，是否传递了 CRT 资源

**操作**：
1. 在 `api_conflicts.md` 中查找已知的违规外部接口
2. 使用 grep 搜索对这些接口的调用
3. 按照 `api_conflicts.md` 中的修复指南，替换调用方式

**示例**：
```c
/* ❌ 原调用（假设 libfoo 的接口违规） */
foo_write_stream(data, size, stdout);

/* ✅ 修复后（使用 api_conflicts.md 中推荐的新接口） */
foo_write_file(data, size, "output.txt");
```

### 步骤 2: 扫描导出接口定义

**目标**：识别本模块导出的接口中，是否包含 CRT 资源类型

**操作**：
1. 使用 grep 搜索导出宏（如 `RSYS_API`）
   ```bash
   grep -rn "RSYS_API" --include="*.h" .
   ```
2. 使用 ast-grep 搜索 FILE*/va_list 参数
   ```bash
   ast-grep --pattern 'RSYS_API $RET $FUNC($$$)' --lang c
   grep -rn "FILE\s*\*" --include="*.h" .
   grep -rn "va_list" --include="*.h" .
   ```
3. 检查内联函数中的 errno/CRT 调用
   ```bash
   grep -A 10 "static.*INLINE" --include="*.h" . | grep -E "(errno|malloc|fprintf|strtod)"
   ```

### 步骤 3: 判定与分类

对每个检出的接口，判定：

| 判定 | 条件 | 处理方式 |
|------|------|---------|
| **高风险（必须修复）** | FILE*/malloc 跨模块传递 | 立即修改接口定义 |
| **中风险（建议修复）** | va_list 跨模块传递 | 判定是否真正跨模块，再决定 |
| **低风险（可选修复）** | errno 在内联函数中使用 | 评估影响，建议修复 |
| **合规** | 无 CRT 资源传递 | 无需修复 |

### 步骤 4: 修改接口定义

**强制要求**：
- ✅ **直接修改接口定义**（允许破坏 ABI，运行时安全优先）
- ✅ Unix/Linux 下可用条件编译保留旧接口
- ✅ Windows 下必须提供安全接口
- ❌ **禁止**仅添加新接口而保留旧接口（会导致调用方误用）

**修改模式**：
```c
/* Unix 保留旧接口 */
#ifndef OS_WINDOWS
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
#endif

/* 跨平台新接口 */
RSYS_API res_T image_write_ppm_file(
    const struct image* img, int bin, const char* path);
```

### 步骤 5: 替换 CRT 资源为安全类型

**替换规则**：

| 原类型 | 替换方案 |
|--------|---------|
| `FILE* stream` | **方案 1**: `const char* path` |
|                | **方案 2**: `write_callback fn + void* ctx` |
| `va_list args` | **方案 1**: 仅保留可变参数版本（`func(...)`），移除 vfunc |
|                | **方案 2**: `const variant_t* args, size_t arg_count` |
| `errno` | 通过返回值返回错误码，不依赖全局状态 |
| `malloc 对象` | 提供配对的 `create/destroy` 接口，在同一模块内管理生命周期 |

详见 [第 4 节：修复策略](#4-修复策略)。

### 步骤 6: 同步修改所有调用点

**强制要求**：
- ✅ 在**同一提交**中修改接口定义和所有调用点
- ✅ 使用 grep/ast-grep 确保没有遗漏的调用点
- ❌ **禁止**分批修改（会导致构建失败）

**检查清单**：
```bash
# 搜索旧接口的所有调用
grep -rn "image_write_ppm_stream" --include="*.c" .

# 确认已全部替换为新接口
grep -rn "image_write_ppm_file" --include="*.c" .
```

### 步骤 7: 记录到文档

**必须更新**：

1. **`api_conflicts.md`** - 记录修复的接口和替换示例
2. **`migration_exception.md`** - 记录修改原因和影响范围
3. **模块 README.md** - 说明平台差异
4. **头文件注释** - 标注 `@deprecated` 或平台限制

详见 [第 6.2 节：文档模板](#62-api_conflictsmd-记录模板)。

---

## 4. 修复策略

### 4.1 FILE* 接口批量修复模式

> **适用场景**：模块中存在多个 FILE* 接口（如图像库、文本读取库）

**禁止**：零散修复（例如只修复 `image_write` 而不修复 `image_read`）  
**要求**：采用统一的三层接口模型

#### 三层接口模型

**层级 1: 文件路径接口**（推荐，跨平台安全）

```c
RSYS_API res_T image_write_ppm_file(
    const struct image* img, int bin, const char* path);
```

**优点**：
- ✅ 无 CRT 资源跨模块传递
- ✅ 最简单的调用方式
- ✅ 跨平台一致

**缺点**：
- ❌ 灵活性较低（无法写入网络流、内存缓冲区、管道等）

---

**层级 2: 回调接口**（高级，最大灵活性）

```c
typedef size_t (*write_callback)(const void* data, size_t size, void* ctx);

RSYS_API res_T image_write_ppm_callback(
    const struct image* img, int bin, write_callback write_fn, void* ctx);
```

**优点**：
- ✅ 无 CRT 资源跨模块传递
- ✅ 最大灵活性（支持任意输出目标：文件、网络、内存、压缩流等）
- ✅ 用户可在回调中使用自己的 FILE*（在同一模块内）

**缺点**：
- ❌ 调用稍复杂，需要编写回调函数

**示例**：
```c
/* 用户可以在回调中使用 FILE*（同模块内，安全） */
size_t my_write_callback(const void* data, size_t size, void* ctx) {
  FILE* f = (FILE*)ctx;
  return fwrite(data, 1, size, f);  /* 在用户模块内调用 fwrite，安全 */
}

FILE* f = fopen("output.ppm", "wb");
image_write_ppm_callback(&img, 0, my_write_callback, f);
fclose(f);
```

---

**层级 3: FILE* 接口**（仅 Unix，条件编译）

```c
#ifndef OS_WINDOWS
/**
 * @deprecated Unix-only interface. Use image_write_ppm_file() or 
 *             image_write_ppm_callback() on Windows.
 * @warning This function is NOT available on Windows due to CRT isolation.
 */
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
#endif
```

**优点**：
- ✅ 保持 Unix 平台向后兼容

**缺点**：
- ❌ Windows 不可用
- ❌ 仍存在 CRT 资源跨模块风险（仅限 Unix）

---

#### 迁移策略

1. **新增**层级 1 和层级 2 接口（Windows + Unix 通用）
2. **条件编译**层级 3 接口（仅 Unix）
3. **更新文档**，推荐使用层级 1，高级用户使用层级 2
4. **逐步废弃**层级 3（Linux 保留兼容性，Windows 直接禁用）

#### 调用方迁移示例

```c
/* ❌ 原调用（不安全） */
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_stream(&img, 0, f);
fclose(f);

/* ✅ 迁移方案 1: 使用文件路径接口（推荐） */
image_write_ppm_file(&img, 0, "output.ppm");

/* ✅ 迁移方案 2: 使用回调接口（高级） */
size_t my_write(const void* data, size_t size, void* ctx) {
  return fwrite(data, 1, size, (FILE*)ctx);
}
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_callback(&img, 0, my_write, f);
fclose(f);

/* ✅ Unix 平台保持兼容（旧代码无需修改） */
#ifndef OS_WINDOWS
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_stream(&img, 0, f);  /* Unix 平台仍可用 */
fclose(f);
#endif
```

---

### 4.2 va_list 接口修复策略

#### 场景 1: 同模块内调用（豁免）

**检查**：
```bash
# 搜索 logger_vprint 的所有调用
grep -rn "logger_vprint" --include="*.c" .
```

**如果所有调用都在本模块内**：
1. 标记为 `@internal`
2. 增加警告注释
3. 无需修改接口

**示例**：
```c
/**
 * @internal This function is for internal use only within the RSys module.
 * External modules should use logger_print() instead.
 * 
 * @warning On Windows, va_list cannot be passed across module boundaries.
 */
RSYS_API res_T logger_vprint(
    struct logger* logger, enum log_type type, const char* log, va_list vargs);
```

#### 场景 2: 跨模块调用（必须修复）

**方案 A**: 仅保留可变参数版本，移除 vprint

```c
/* 移除 */
// RSYS_API res_T logger_vprint(..., va_list vargs);

/* 保留 */
RSYS_API res_T logger_print(
    struct logger* logger, enum log_type type, const char* log, ...);
```

**方案 B**: 提供 Windows 专用的参数数组接口

```c
#ifdef OS_WINDOWS
typedef union variant {
    int64_t i;
    double d;
    const char* s;
    void* p;
} variant_t;

RSYS_API res_T logger_print_args(
    struct logger* logger, enum log_type type, const char* fmt,
    const variant_t* args, size_t arg_count);
#else
RSYS_API res_T logger_vprint(
    struct logger* logger, enum log_type type, const char* log, va_list vargs);
#endif
```

---

### 4.3 errno 修复策略

#### 场景 1: 内联函数中使用 errno（推荐修复）

参见 [第 2.2 节](#22-特殊场景内联函数中的-crt-资源)。

#### 场景 2: 接口参数/返回值中使用 errno（极少见，必须修复）

**错误示例**：
```c
/* ❌ 错误：返回 errno 的地址 */
RSYS_API int* get_errno_ptr(void);
```

**修复**：
```c
/* ✅ 正确：返回错误码，不依赖 errno */
RSYS_API res_T get_last_error(int* error_code);
```

---

### 4.4 malloc 对象管理策略

#### 合规模式：create/destroy 配对

```c
/* ✅ 正确：分配和释放在同一模块内 */
RSYS_API struct mutex* mutex_create(void);      /* 模块内分配 */
RSYS_API void mutex_destroy(struct mutex* m);   /* 模块内释放 */
```

**用户调用**：
```c
struct mutex* m = mutex_create();  /* RSys 模块内分配 */
/* ... 使用 ... */
mutex_destroy(m);                  /* RSys 模块内释放 */
```

#### 违规模式：跨模块 free（必须修复）

```c
/* ❌ 错误：返回 malloc 对象，期望用户 free */
RSYS_API char* image_get_pixels(struct image* img);

/* 用户代码（会崩溃） */
char* pixels = image_get_pixels(&img);  /* RSys 模块分配 */
free(pixels);  /* 用户模块释放 → 堆损坏 → 崩溃 */
```

**修复**：
```c
/* ✅ 方案 1: 返回内部指针，用户不得释放 */
RSYS_API const char* image_get_pixels_readonly(const struct image* img);

/* ✅ 方案 2: 用户提供缓冲区 */
RSYS_API res_T image_copy_pixels(
    const struct image* img, char* buffer, size_t buffer_size);

/* ✅ 方案 3: 提供释放函数 */
RSYS_API char* image_alloc_pixels(struct image* img);
RSYS_API void image_free_pixels(char* pixels);
```

---

## 5. 验证流程

修复完成后，必须执行以下验证步骤，确保跨平台兼容性和功能正确性。

### 5.1 编译验证

#### Windows MSVC
```bash
cd D:\Works\Projects\Stardis-GPU\stardis-cpu\<module>\<version>
cmake -G "Visual Studio 17 2022" -A x64 -D<MODULE>_BUILD_TESTS=ON .
cmake --build . --config Debug
cmake --build . --config Release
```

#### Linux GCC
```bash
cd /home/<user>/star-build/cache/<module>/<version>
make clean && make
```

**验收标准**：
- [ ] Windows Debug 构建成功，无编译错误
- [ ] Windows Release 构建成功，无编译错误
- [ ] Linux 构建成功（使用原有 Makefile）
- [ ] 条件编译正确（Windows 禁用 FILE* 接口，Linux 保留）

---

### 5.2 接口兼容性测试

创建测试用例，覆盖新旧接口的功能等价性：

```c
/* test_interface_migration.c */

void test_file_path_interface(void) {
  struct image img;
  /* 初始化测试数据 */
  CHK(image_setup(&img, 10, 10, 30, IMAGE_RGB8, NULL) == RES_OK);
  
  /* 测试新接口（文件路径） */
  CHK(image_write_ppm_file(&img, 0, "test_output.ppm") == RES_OK);
  
  /* 验证输出文件存在且内容正确 */
  CHK(file_exists("test_output.ppm"));
  
  /* 读取回来验证 */
  struct image img2;
  CHK(image_read_ppm_file(&img2, "test_output.ppm") == RES_OK);
  CHK(images_equal(&img, &img2));
  
  image_release(&img);
  image_release(&img2);
}

#ifndef OS_WINDOWS
void test_stream_interface_unix_only(void) {
  struct image img;
  CHK(image_setup(&img, 10, 10, 30, IMAGE_RGB8, NULL) == RES_OK);
  
  FILE* f = fopen("test_output_stream.ppm", "wb");
  CHK(f != NULL);
  
  /* Unix 平台保持旧接口兼容性 */
  CHK(image_write_ppm_stream(&img, 0, f) == RES_OK);
  fclose(f);
  
  image_release(&img);
}
#endif

void test_callback_interface(void) {
  struct image img;
  CHK(image_setup(&img, 10, 10, 30, IMAGE_RGB8, NULL) == RES_OK);
  
  /* 测试新接口（回调） */
  size_t write_fn(const void* data, size_t size, void* ctx) {
    return fwrite(data, 1, size, (FILE*)ctx);
  }
  
  FILE* f = fopen("test_output_callback.ppm", "wb");
  CHK(f != NULL);
  CHK(image_write_ppm_callback(&img, 0, write_fn, f) == RES_OK);
  fclose(f);
  
  /* 验证输出与文件路径接口一致 */
  struct image img2, img3;
  CHK(image_read_ppm_file(&img2, "test_output.ppm") == RES_OK);
  CHK(image_read_ppm_file(&img3, "test_output_callback.ppm") == RES_OK);
  CHK(images_equal(&img2, &img3));
  
  image_release(&img);
  image_release(&img2);
  image_release(&img3);
}
```

**验收标准**：
- [ ] 新接口功能与旧接口等价（输出文件内容一致）
- [ ] Windows 使用新接口测试通过
- [ ] Linux 旧接口兼容性测试通过
- [ ] 回调接口提供足够灵活性

---

### 5.3 跨模块调用测试（针对 FILE*/va_list 修复）

**目的**：验证修复后的接口在不同 CRT 配置下行为一致

#### 创建测试项目（模拟外部模块）

```
test_cross_module/
├── CMakeLists.txt
├── module_a/  （使用 /MD 编译）
│   ├── CMakeLists.txt
│   └── call_rsys_api.c
└── module_b/  （使用 /MT 编译）
    ├── CMakeLists.txt
    └── call_rsys_api.c
```

**module_a/CMakeLists.txt**:
```cmake
add_executable(test_module_a call_rsys_api.c)
target_link_libraries(test_module_a rsys)
target_compile_options(test_module_a PRIVATE /MD)  # 动态 CRT
```

**module_b/CMakeLists.txt**:
```cmake
add_executable(test_module_b call_rsys_api.c)
target_link_libraries(test_module_b rsys)
target_compile_options(test_module_b PRIVATE /MT)  # 静态 CRT
```

**call_rsys_api.c**:
```c
#include <rsys/image.h>

int main(void) {
  struct image img;
  
  /* ✅ 安全：使用文件路径接口 */
  if(image_write_ppm_file(&img, 0, "output.ppm") != RES_OK) {
    return 1;
  }
  
  /* ❌ 不安全：Windows 不应编译通过 */
#ifdef OS_WINDOWS
  /* 以下代码应无法编译（FILE* 接口被条件编译禁用） */
  /* FILE* f = fopen("output.ppm", "wb"); */
  /* image_write_ppm_stream(&img, 0, f); */
#endif
  
  return 0;
}
```

**验收标准**：
- [ ] 模块 A（/MD）编译并运行成功
- [ ] 模块 B（/MT）编译并运行成功
- [ ] Windows 编译器阻止 FILE* 接口调用（条件编译生效）
- [ ] 不同 CRT 配置下行为一致

---

### 5.4 文档更新

#### 必须更新的文档

1. **`api_conflicts.md`** - 记录所有修复的接口

2. **模块 `README.md`** - 说明平台差异
   ```markdown
   ## Platform-Specific API Notes
   
   ### Windows
   - `image_write_ppm_stream` is NOT available on Windows due to CRT isolation.
   - Use `image_write_ppm_file` or `image_write_ppm_callback` instead.
   
   ### Unix/Linux
   - All APIs are available, including legacy `*_stream` functions.
   ```

3. **头文件注释** - 标注废弃/平台限制
   ```c
   #ifndef OS_WINDOWS
   /**
    * @deprecated Unix-only interface. Use image_write_ppm_file() on Windows.
    * @warning This function is NOT available on Windows due to CRT isolation.
    */
   RSYS_API res_T image_write_ppm_stream(
       const struct image* img, int bin, FILE* stream);
   #endif
   ```

4. **`migration_exception.md`** - 记录修改原因

**验收标准**：
- [ ] `api_conflicts.md` 完整记录所有修复
- [ ] 模块文档说明平台差异
- [ ] 头文件注释标注废弃/限制
- [ ] `migration_exception.md` 记录修改原因

---

### 5.5 回归测试

#### Windows
```bash
cd D:\Works\Projects\Stardis-GPU\stardis-cpu\<module>\<version>
ctest -C Debug --output-on-failure
ctest -C Release --output-on-failure
```

#### Linux
```bash
cd /home/<user>/star-build/cache/<module>/<version>
export LD_LIBRARY_PATH=.
./test_<module>
./test_<module>_all  # 如果有完整测试套件
```

**验收标准**：
- [ ] Windows Debug 所有测试通过
- [ ] Windows Release 所有测试通过
- [ ] Linux 所有测试通过（包括兼容性测试）
- [ ] 修复未引入新的 regression
- [ ] 关键精度测试通过（如浮点运算）

---

### 5.6 最终验收清单

| 检查项 | Windows | Linux | 状态 |
|--------|---------|-------|------|
| 编译成功（Debug） | ✅ | ✅ | |
| 编译成功（Release） | ✅ | ✅ | |
| 单元测试通过 | ✅ | ✅ | |
| 接口兼容性测试通过 | ✅ | ✅ | |
| 跨模块调用测试通过 | ✅ | N/A | |
| 文档更新完成 | ✅ | ✅ | |
| `api_conflicts.md` 记录完整 | ✅ | ✅ | |
| 回归测试通过 | ✅ | ✅ | |
| 条件编译正确（Windows 禁用 FILE*） | ✅ | N/A | |
| Unix 兼容性测试（旧接口可用） | N/A | ✅ | |

**迁移完成标准**: 所有检查项标记为 ✅

---

## 6. 附录

### 6.1 禁止接口 → 安全替代速查表

| 禁止类型 | 风险原因 | 推荐替代方案 | 示例 |
|---------|----------|--------------|------|
| **FILE*** | CRT 内部结构不兼容 | `const char* path` | `fopen("a.txt")` → `"a.txt"` |
|  |  | `write_callback fn + void* ctx` | 用户提供写入回调 |
| **va_list** | ABI/表示不兼容 | 仅保留可变参数接口 | 移除 `vprintf`，保留 `printf` |
|  |  | `variant_t* args + size_t count` | 参数数组 |
| **errno** | TLS 地址不一致 | 返回码 + 错误参数 | `int* error_code` |
|  |  | 不依赖 errno 的实现 | 使用 `HUGE_VAL` 检测错误 |
| **malloc 对象** | 堆管理器不同 | `create/destroy` 配对接口 | `mutex_create()` + `mutex_destroy()` |
|  |  | 用户提供缓冲区 | `get_data(buf, size)` |
|  |  | 返回内部只读指针 | `const char* get_data_readonly()` |
| **jmp_buf** | 栈/ABI 不兼容 | 显式错误返回 | `res_T` 返回码 |
|  |  | 错误回调 | `error_handler fn` |

---

### 6.2 `api_conflicts.md` 记录模板

```markdown
### image_write_ppm_stream

**原接口（不安全）**
```c
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
```

**问题原因**
- 使用 `FILE*` 跨模块边界
- Windows + MSVC 不同 CRT 下必然崩溃（FILE 结构体布局不兼容）

**新接口（安全）**

**方案 1: 文件路径接口（推荐）**
```c
RSYS_API res_T image_write_ppm_file(
    const struct image* img, int bin, const char* path);
```

**方案 2: 回调接口（高级）**
```c
typedef size_t (*write_callback)(const void* data, size_t size, void* ctx);
RSYS_API res_T image_write_ppm_callback(
    const struct image* img, int bin, write_callback write_fn, void* ctx);
```

**方案 3: Unix 兼容（条件编译）**
```c
#ifndef OS_WINDOWS
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
#endif
```

**调用替换示例**

```c
/* ❌ 原调用（不安全） */
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_stream(&img, 0, f);
fclose(f);

/* ✅ 新调用（方案 1：文件路径） */
image_write_ppm_file(&img, 0, "output.ppm");

/* ✅ 新调用（方案 2：回调） */
size_t my_write(const void* data, size_t size, void* ctx) {
  return fwrite(data, 1, size, (FILE*)ctx);
}
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_callback(&img, 0, my_write, f);
fclose(f);
```

**影响范围**
- 模块: RSys 0.15
- 受影响文件: `rsys/image.h`, `src/image.c`, `test_image.c`
- 调用方: star-3d, stardis-solver

**修复日期**: 2026-01-18
```

---

### 6.3 常见问题 FAQ

#### Q1: 为什么 Linux 能用的代码，Windows 不能用？

**A**: Linux 通常只有一个 glibc，所有模块共享同一个 CRT。但 Windows 允许不同模块使用不同的 CRT（/MD vs /MT，Debug vs Release），导致 `FILE*`、`malloc` 堆等内部表示不兼容。

#### Q2: 如果我的模块只在 Linux 上运行，需要遵守这个 SOP 吗？

**A**: 如果**确定**永远不会迁移到 Windows，可以不遵守。但建议遵守，因为：
1. 未来可能需要 Windows 支持
2. 避免隐式的 CRT 依赖，提升代码可移植性
3. 作为最佳实践，防止跨模块内存管理问题

#### Q3: 回调接口会不会影响性能？

**A**: 对于 I/O 操作（如文件写入），回调开销可忽略不计（远小于磁盘 I/O 时间）。对于热路径，可以使用内联回调或函数指针缓存。

#### Q4: 我可以用 `#ifdef _WIN32` 来修复吗？

**A**: 建议使用 `#ifdef OS_WINDOWS`（在 `rsys.h` 中定义），统一平台宏。避免使用 `_WIN32`、`WIN32`、`_MSC_VER` 等不一致的宏。

#### Q5: 如果外部库的接口违规，我该怎么办？

**A**: 
1. 在本模块内封装该外部库，不暴露违规接口
2. 在 `api_conflicts.md` 中记录该外部库的违规接口
3. 如果可能，向外部库提交补丁

---

### 6.4 参考资料

- **MSVC CRT 文档**: [CRT Library Features](https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features)
- **GCC 可见性文档**: [Symbol Visibility](https://gcc.gnu.org/wiki/Visibility)
- **RSys 实战审计报告**: `cross_project_audit/rsys_interface_audit_report.md`

---

## 7. 审查完成判定标准

接口审查仅在以下条件**全部满足**时视为完成：

- [ ] Windows 分支无 FILE*/va_list/errno/jmp_buf 跨模块接口（除非豁免）
- [ ] 所有违规接口均已修改定义
- [ ] 所有调用点已同步更新
- [ ] `api_conflicts.md` 已完整记录
- [ ] `migration_exception.md` 已记录修改原因
- [ ] 模块文档已更新（README、头文件注释）
- [ ] Linux 和 Windows 均可成功构建
- [ ] 所有测试通过（单元测试、接口兼容性测试、跨模块测试）
- [ ] 条件编译正确（Windows 禁用 FILE* 接口，Linux 保留）
- [ ] 最终验收清单（第 5.6 节）全部标记为 ✅

---

## 8. LLM 强制约束（重申）

> 本 SOP 用于**约束 LLM 的接口修改行为**，防止"表面可编译、运行期必崩"的跨 CRT 设计。

**LLM 必须遵守以下强制规则**：

1. ❌ **禁止**假设 Linux 和 Windows 的 CRT 行为一致
2. ❌ **禁止**仅添加新接口而保留违规的旧接口
3. ❌ **禁止**分批修改接口和调用点（必须在同一提交中完成）
4. ❌ **禁止**自行判断 va_list 接口是否跨模块调用（必须通过代码搜索验证）
5. ❌ **禁止**忽略内联函数中的 CRT 资源访问
6. ✅ **必须**在修改接口后更新所有文档
7. ✅ **必须**执行完整的验证流程（第 5 节）
8. ✅ **必须**在 `api_conflicts.md` 中记录所有修复

**若与历史接口兼容性冲突，以运行时安全为最高优先级。**

---

**版本历史**:
- v1.0 (2026-01-16): 初版，基本规则
- v2.0 (2026-01-18): 增加内联函数/va_list 豁免/FILE* 批量修复/验证流程，通过 RSys 实战验证

---

**维护者**: Stardis-GPU 项目团队  
**审核**: 通过 RSys 0.15 模块（58 个接口，8 个违规）实战验证  
**反馈**: 请在 `migration_exception.md` 中记录 SOP 执行中的问题
