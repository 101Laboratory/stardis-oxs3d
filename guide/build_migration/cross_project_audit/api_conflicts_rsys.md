# API Conflicts - Cross-Module CRT Isolation

**Last Updated**: 2026-01-18  
**Project**: Stardis-GPU  
**Scope**: Windows MSVC Cross-CRT Interface Safety  

---

## RSys 0.15 - FILE* Interface Fixes

### image_read_ppm_stream

**原接口（不安全）**
```c
RSYS_API res_T image_read_ppm_stream(
    struct image* img, FILE* stream);
```

**问题原因**
- 使用 `FILE*` 跨模块边界
- Windows + MSVC 不同 CRT 下必然崩溃（FILE 结构体布局不兼容）
- 不同 CRT 运行时（/MD vs /MT, Debug vs Release）的 FILE* 内部表示不一致

**新接口（安全）**

**方案: 使用现有的文件路径接口（推荐）**
```c
RSYS_API res_T image_read_ppm(
    struct image* img, const char* filename);
```

**修复状态**: ✅ 条件编译（Unix 保留，Windows 禁用）

**调用替换示例**

```c
/* ❌ 原调用（不安全 - 仅 Unix） */
#ifndef OS_WINDOWS
FILE* f = fopen("input.ppm", "rb");
image_read_ppm_stream(&img, f);
fclose(f);
#endif

/* ✅ 新调用（跨平台安全） */
image_read_ppm(&img, "input.ppm");
```

**影响范围**
- 模块: RSys 0.15
- 受影响文件: `rsys/image.h`, `src/image.c`, `src/test_image.c`
- 调用方: 模块内部测试（无外部调用）
- 修复日期: 2026-01-18

---

### image_write_ppm_stream

**原接口（不安全）**
```c
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
```

**问题原因**
- 使用 `FILE*` 跨模块边界
- Windows + MSVC 不同 CRT 下必然崩溃
- FILE* 的 fwrite/fflush 等操作在不同 CRT 中的实现不兼容

**新接口（安全）**

**方案: 使用现有的文件路径接口（推荐）**
```c
RSYS_API res_T image_write_ppm(
    const struct image* img, int bin, const char* filename);
```

**修复状态**: ✅ 条件编译（Unix 保留，Windows 禁用）

**调用替换示例**

```c
/* ❌ 原调用（不安全 - 仅 Unix） */
#ifndef OS_WINDOWS
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_stream(&img, 0, f);
fclose(f);
#endif

/* ✅ 新调用（跨平台安全） */
image_write_ppm(&img, 0, "output.ppm");
```

**影响范围**
- 模块: RSys 0.15
- 受影响文件: `rsys/image.h`, `src/image.c`, `src/test_image.c`
- 调用方: 模块内部测试（无外部调用）
- 修复日期: 2026-01-18

---

### txtrdr_stream

**原接口（不安全）**
```c
RSYS_API res_T txtrdr_stream(
    struct mem_allocator* allocator,
    FILE* stream,
    const char* name,
    const char comment,
    struct txtrdr** txtrdr);
```

**问题原因**
- 接受 `FILE*` 参数跨模块传递
- 文本读取器内部存储 FILE* 指针，生命周期跨模块
- Windows 环境下 fgetc/fread 等操作会访问错误的 FILE 结构体偏移

**新接口（安全）**

**方案: 使用现有的文件路径接口（推荐）**
```c
RSYS_API res_T txtrdr_file(
    struct mem_allocator* allocator,
    const char* filename,
    const char comment,
    struct txtrdr** txtrdr);
```

**修复状态**: ✅ 条件编译（Unix 保留，Windows 禁用）

**调用替换示例**

```c
/* ❌ 原调用（不安全 - 仅 Unix） */
#ifndef OS_WINDOWS
FILE* stream = fopen("config.txt", "r");
txtrdr_stream(NULL, stream, "config", '#', &txtrdr);
/* ... 使用 txtrdr ... */
txtrdr_ref_put(txtrdr);
fclose(stream);
#endif

/* ✅ 新调用（跨平台安全） */
txtrdr_file(NULL, "config.txt", '#', &txtrdr);
/* ... 使用 txtrdr ... */
txtrdr_ref_put(txtrdr);
```

**影响范围**
- 模块: RSys 0.15
- 受影响文件: `rsys/text_reader.h`, `src/text_reader.c`, `src/test_text_reader.c`
- 调用方: 模块内部测试和实现（无外部调用）
- 修复日期: 2026-01-18

---

### txtrdr_get_stream

**原接口（不安全）**
```c
RSYS_API FILE* txtrdr_get_stream(const struct txtrdr* txtrdr);
```

**问题原因**
- 返回 `FILE*` 跨模块边界
- 调用方可能在不同的 CRT 环境中操作返回的 FILE*
- 导致 fread/fwrite 等操作访问错误的内存布局

**新接口（安全）**

**方案: 禁用（无替代接口）**
- Windows 平台直接禁用此接口
- Unix 平台保留以维持向后兼容性
- 调用方应使用 txtrdr_file() 而非 txtrdr_stream()，避免需要访问底层流

**修复状态**: ✅ 条件编译（Unix 保留，Windows 禁用）

**调用替换示例**

```c
/* ❌ 原调用（不安全 - 仅 Unix） */
#ifndef OS_WINDOWS
struct txtrdr* txtrdr;
txtrdr_file(NULL, "data.txt", '#', &txtrdr);
FILE* stream = txtrdr_get_stream(txtrdr);  /* 获取底层流 */
/* ... 直接操作 stream ... */
#endif

/* ✅ 替代方案：不访问底层流 */
/* 使用 txtrdr_read_line() / txtrdr_get_line() 等高层接口 */
struct txtrdr* txtrdr;
txtrdr_file(NULL, "data.txt", '#', &txtrdr);
while(txtrdr_read_line(txtrdr) == RES_OK) {
  const char* line = txtrdr_get_cline(txtrdr);
  /* 处理行数据 */
}
txtrdr_ref_put(txtrdr);
```

**影响范围**
- 模块: RSys 0.15
- 受影响文件: `rsys/text_reader.h`, `src/text_reader.c`, `src/test_text_reader.c`
- 调用方: 模块内部测试（1 处调用）
- 修复日期: 2026-01-18

---

## RSys 0.15 - va_list Interface (Internal Use Only)

### logger_vprint

**接口（内部使用）**
```c
RSYS_API res_T logger_vprint(
    struct logger* logger,
    enum log_type type,
    const char* log,
    va_list vargs);
```

**审计结果**
- ✅ **所有调用点均在 RSys 模块内部**（`src/logger.c`, `src/test_logger.c`）
- ✅ 已标注 `@internal` 和 `@warning`
- ✅ 无需修改接口，但禁止外部模块调用

**使用指南**

```c
/* ✅ 正确：外部模块使用可变参数版本 */
logger_print(logger, LOG_OUTPUT, "Value: %d\n", value);

/* ❌ 错误：外部模块传递 va_list（Windows 会崩溃） */
void my_log_wrapper(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logger_vprint(logger, LOG_OUTPUT, fmt, ap);  /* 跨模块调用，Windows 崩溃 */
  va_end(ap);
}

/* ✅ 正确：如需包装，在同一模块内调用 */
/* 或直接使用 logger_print() */
```

**修复状态**: ✅ 豁免修复（增加警告注释）

**影响范围**
- 模块: RSys 0.15
- 调用点: `src/logger.c:27`, `src/test_logger.c:55`（仅模块内部）
- 修复日期: 2026-01-18

---

### str_vprintf

**接口（内部使用）**
```c
RSYS_API res_T str_vprintf(
    struct str* str,
    const char* fmt,
    va_list vargs_list);
```

**审计结果**
- ✅ **所有调用点均在 RSys 模块内部**（`src/str.c`, `src/test_str.c`）
- ✅ 已标注 `@internal` 和 `@warning`
- ✅ 无需修改接口，但禁止外部模块调用

**使用指南**

```c
/* ✅ 正确：外部模块使用可变参数版本 */
str_printf(str, "Value: %d\n", value);

/* ❌ 错误：外部模块传递 va_list */
void my_str_wrapper(struct str* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  str_vprintf(s, fmt, ap);  /* 跨模块调用，Windows 崩溃 */
  va_end(ap);
}

/* ✅ 正确：直接使用 str_printf() */
str_printf(str, "Formatted: %s = %d", key, value);
```

**修复状态**: ✅ 豁免修复（增加警告注释）

**影响范围**
- 模块: RSys 0.15
- 调用点: `src/str.c:189,211`, `src/test_str.c:29`（仅模块内部）
- 修复日期: 2026-01-18

---

### str_append_vprintf

**接口（内部使用）**
```c
RSYS_API res_T str_append_vprintf(
    struct str* str,
    const char* fmt,
    va_list vargs_list);
```

**审计结果**
- ✅ **所有调用点均在 RSys 模块内部**（`src/str.c`, `src/test_str.c`）
- ✅ 已标注 `@internal` 和 `@warning`
- ✅ 无需修改接口，但禁止外部模块调用

**使用指南**

```c
/* ✅ 正确：外部模块使用可变参数版本 */
str_append_printf(str, " | %s", additional_text);

/* ❌ 错误：外部模块传递 va_list */
/* 参见 str_vprintf 的错误示例 */

/* ✅ 正确：直接使用 str_append_printf() */
str_append_printf(str, " | key=%s value=%d", key, value);
```

**修复状态**: ✅ 豁免修复（增加警告注释）

**影响范围**
- 模块: RSys 0.15
- 调用点: `src/str.c:201,215`, `src/test_str.c:41`（仅模块内部）
- 修复日期: 2026-01-18

---

## RSys 0.15 - errno in Inline Functions

### cstr_to_long / cstr_to_ulong

**原实现（不安全）**
```c
static INLINE res_T cstr_to_long(const char* str, long* dst) {
  char* end;
  errno = 0;  /* ⚠️ 内联函数中访问 errno，在调用方 CRT 展开 */
  *dst = strtol(str, &end, 10);
  if(end == str || errno == ERANGE)
    return RES_BAD_ARG;
  return RES_OK;
}
```

**问题原因**
- 内联函数在调用方模块中展开
- `errno` 的访问实际发生在调用方的 CRT 环境中
- 如果调用方使用不同的 CRT，可能访问到错误的 errno 副本

**新实现（安全）**

**方案: 仅在必要时检查 errno（推荐）**
```c
static INLINE res_T cstr_to_long(const char* str, long* dst) {
  char* end;
  *dst = strtol(str, &end, 10);
  if(end == str)
    return RES_BAD_ARG;
  /* 仅在可能溢出时检查 errno */
  if(*dst == LONG_MAX || *dst == LONG_MIN) {
    if(errno == ERANGE)
      return RES_BAD_ARG;
  }
  return RES_OK;
}
```

**修复说明**
- 移除了初始化 `errno = 0` （不必要，strtol 在成功时不会修改 errno）
- 仅在返回值可能表示溢出时（LONG_MAX/LONG_MIN）检查 errno
- 减少了跨 CRT 的 errno 访问频率

**修复状态**: ✅ 已重写实现

**影响范围**
- 模块: RSys 0.15
- 受影响文件: `rsys/cstr.h`（内联函数）
- 调用方: 所有使用 cstr_to_long/cstr_to_ulong 的模块
- 修复日期: 2026-01-18

---

## 审计总结

**RSys 0.15 接口修复汇总**：

| 接口类型 | 违规数量 | 修复策略 | 状态 |
|---------|---------|---------|------|
| FILE* 接口 | 4 个 | 条件编译（Windows 禁用） | ✅ 完成 |
| va_list 接口 | 3 个 | 豁免（增加警告注释） | ✅ 完成 |
| errno 内联函数 | 2 个 | 重写实现（减少 errno 访问） | ✅ 完成 |
| jmp_buf | 0 个 | N/A | N/A |
| malloc 跨模块 free | 0 个 | N/A | N/A |

**总计**: 9 个接口问题已修复或豁免

---

## 平台特定说明

### Windows (MSVC)
- ❌ `image_read_ppm_stream` - 不可用
- ❌ `image_write_ppm_stream` - 不可用
- ❌ `txtrdr_stream` - 不可用
- ❌ `txtrdr_get_stream` - 不可用
- ⚠️ `logger_vprint` - 仅限模块内部使用
- ⚠️ `str_vprintf` - 仅限模块内部使用
- ⚠️ `str_append_vprintf` - 仅限模块内部使用

**推荐接口**:
- ✅ `image_read_ppm(img, filename)`
- ✅ `image_write_ppm(img, binary, filename)`
- ✅ `txtrdr_file(allocator, filename, comment, txtrdr)`
- ✅ `logger_print(logger, type, fmt, ...)`
- ✅ `str_printf(str, fmt, ...)`
- ✅ `str_append_printf(str, fmt, ...)`

### Unix/Linux
- ✅ 所有接口可用（包括遗留的 `*_stream` 函数）
- ⚠️ 建议逐步迁移到文件路径接口，提升可移植性

---

## Cross-Project API Conflicts (Star-2D / Star-3D)

**Date**: 2026-01-18  
**Projects**: star-2d (0.7), star-3d (0.10)

### Summary

**No API conflicts detected** between star-2d and star-3d.

### Symbol Namespace Analysis

**star-2d Exported Symbols**:
- **Prefix**: `s2d_` (all functions, types, macros)
- **Opaque Types**: `s2d_device`, `s2d_scene`, `s2d_scene_view`, `s2d_shape`
- **Public API Macro**: `S2D_API`

**star-3d Exported Symbols**:
- **Prefix**: `s3d_` (all functions, types, macros)
- **Opaque Types**: `s3d_device`, `s3d_scene`, `s3d_scene_view`, `s3d_shape`
- **Public API Macro**: `S3D_API`

### Conflict Check Results

- ✅ **Function Names**: No conflicts (s2d_* vs s3d_*)
- ✅ **Type Names**: Properly namespaced
- ✅ **Macro Names**: Properly prefixed
- ✅ **Global Variables**: None exported

---

**维护者**: Stardis-GPU 项目团队  
**参考**: `sop/interface_audit_sop_v2.md` 第 6.2 节
