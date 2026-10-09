# SOP 第 8 章节"接口审查"完善建议

**基于**: RSys 0.15 模块接口审计实战验证  
**日期**: 2026-01-18  
**状态**: 建议待采纳  

---

## 验证结论

✅ **SOP 第 8 章节基本可行** - 通过 RSys 模块的 58 个导出接口审计，验证了以下能力：
- 成功识别所有 RSYS_API 标记的导出接口
- 成功检测 4 个 FILE* 违规接口
- 成功检测 4 个 va_list 违规接口
- 成功识别 errno 内部使用（虽未跨模块传递）
- 确认 malloc 对象管理符合规范（create/destroy 模式）

⚠️ **需要完善的方面**:
1. 内联函数中的 CRT 资源访问未明确规定
2. va_list 同模块调用的豁免条款缺失
3. FILE* 批量修复策略不够详细
4. 修复后验证流程缺失

---

## 建议 1: 增加内联函数 CRT 绑定检查规则

**插入位置**: 第 8 章节"包括但不限于以下类型"列表后

**新增内容**:

```markdown
### 8.1 特殊场景：内联函数中的 CRT 资源

内联函数会在调用方模块中展开，因此内联函数体内的 CRT 资源访问实际发生在调用方的 CRT 环境中。

**需要检查的情况**:
- 内联函数中读取/写入 `errno`
- 内联函数中调用 CRT 函数（如 `malloc`, `fprintf`, `strtod`）
- 内联函数中使用 `va_arg` 宏

**处理规则**:
1. 如果内联函数访问 errno，考虑以下方案之一：
   - **方案 A**: Windows 分支禁用内联，确保 errno 访问在定义模块内
   - **方案 B**: 重写为不依赖 errno 的版本（使用返回值判断错误）
2. 如果内联函数调用 CRT 分配函数，必须在同一内联函数内释放，或改为非内联函数
3. 文档必须明确说明该内联函数的 CRT 依赖

**示例**:
```c
/* ❌ 原内联函数（不安全） */
static INLINE res_T cstr_to_double(const char* str, double* value) {
  char* end;
  errno = 0;  /* ⚠️ errno 访问会在调用方 CRT 中展开 */
  *value = strtod(str, &end);
  if(errno == ERANGE) return RES_BAD_ARG;
  return RES_OK;
}

/* ✅ 修复方案 A: Windows 禁用内联 */
#ifdef OS_WINDOWS
  #define CSTR_INLINE  /* 禁用内联，确保 errno 访问在模块内 */
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

/* ✅ 修复方案 B: 重写为不依赖 errno（推荐） */
static INLINE res_T cstr_to_double_safe(const char* str, double* value) {
  char* end;
  *value = strtod(str, &end);
  /* 使用返回值和 HUGE_VAL 检测错误，无需 errno */
  if(end == str || *value == HUGE_VAL || *value == -HUGE_VAL)
    return RES_BAD_ARG;
  return RES_OK;
}
```

**RSys 实际案例**:
- `rsys/cstr.h` 的 `cstr_to_double` 和 `cstr_to_uint` 内联函数使用 errno
- 建议采用方案 B 重写，消除 errno 依赖
```

---

## 建议 2: 增加 va_list 同模块调用的豁免条款

**插入位置**: 第 8 章节"操作步骤"第 3 步"判定为不安全后"之前

**新增内容**:

```markdown
### 8.2 va_list 特殊判定规则

`va_list` 接口通常用于支持可变参数包装器，存在以下两种场景：

| 场景 | 是否违规 | 处理方式 |
|------|----------|----------|
| 跨模块调用（外部模块传入 va_list） | 是 | 必须修复 |
| 同模块内调用（仅本模块内部使用） | 否 | 可豁免，但需文档说明 |

**判定方法**:
1. 搜索项目中所有调用该 va_list 接口的位置（使用 grep/ast-grep）
2. 检查调用方是否与接口定义在同一编译单元/模块
3. 如果所有调用都在同一模块内，可标记为"内部接口，无需修改"
4. 在接口注释中增加警告：
   ```c
   /**
    * @internal This function is for internal use only within the [ModuleName] module.
    * External modules should use [alternative_function]() instead.
    * 
    * @warning On Windows, va_list cannot be passed across module boundaries due to CRT isolation.
    */
   ```

**示例**:
```c
/* ✅ 内部接口标注（豁免修复） */
/**
 * @internal This function is for internal use only within the RSys module.
 * External modules should use logger_print() instead.
 * 
 * @warning On Windows, va_list cannot be passed across module boundaries due to CRT isolation.
 */
RSYS_API res_T logger_vprint(
    struct logger* logger, enum log_type type, const char* log, va_list vargs);
```

**RSys 实际案例**:
- `logger_vprint` / `str_vprintf` 系列函数主要被模块内部的可变参数版本（`logger_print`）调用
- 经审计，未发现跨模块传递 va_list 的情况
- 建议标注为内部接口，增加文档警告
```

---

## 建议 3: 增加 FILE* 批量修复策略

**插入位置**: 第 8 章节"示例：接口与调用同步修改"部分后

**新增内容**:

```markdown
### 8.3 FILE* 接口批量修复模式

对于有多个 FILE* 相关接口的模块（如图像库、文本读取库），建议采用**三层接口**设计：

**层级 1: 文件路径接口**（推荐，跨平台安全）
```c
RSYS_API res_T image_write_ppm_file(
    const struct image* img, int bin, const char* path);
```
- ✅ 无 CRT 资源跨模块传递
- ✅ 最简单的调用方式
- ❌ 灵活性较低（无法写入网络流、内存缓冲区等）

**层级 2: 回调接口**（高级，最大灵活性）
```c
typedef size_t (*write_callback)(const void* data, size_t size, void* ctx);
RSYS_API res_T image_write_ppm_callback(
    const struct image* img, int bin, write_callback write_fn, void* ctx);
```
- ✅ 无 CRT 资源跨模块传递
- ✅ 最大灵活性（支持任意输出目标）
- ❌ 调用稍复杂，需要编写回调函数

**层级 3: FILE* 接口**（仅 Unix，条件编译）
```c
#ifndef OS_WINDOWS
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
#endif
```
- ✅ 保持 Unix 平台向后兼容
- ❌ Windows 不可用
- ❌ 存在 CRT 资源跨模块风险（仅限 Unix）

---

**迁移策略**:
1. 新增层级 1 和层级 2 接口（Windows + Unix 通用）
2. 将层级 3 接口标记为 Unix-only（条件编译）
3. 更新文档，推荐使用层级 1，高级用户使用层级 2
4. 逐步废弃层级 3（Linux 保留兼容性，Windows 直接禁用）

---

**调用方迁移示例**:
```c
/* ❌ 原调用（不安全） */
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_stream(&img, 0, f);
fclose(f);

/* ✅ 迁移方案 1: 使用文件路径接口（推荐） */
image_write_ppm_file(&img, 0, "output.ppm");

/* ✅ 迁移方案 2: 使用回调接口（高级） */
size_t my_write(const void* data, size_t size, void* ctx) {
  /* 自定义写入逻辑（网络、内存、压缩流等） */
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

**RSys 实际案例**:
需要修复的 FILE* 接口：
1. `image_read_ppm_stream` → 新增 `image_read_ppm_file`（已存在）
2. `image_write_ppm_stream` → 新增 `image_write_ppm_file` + `image_write_ppm_callback`
3. `txtrdr_stream` → 新增 `txtrdr_file_ex`（已存在 `txtrdr_file`，功能类似）
4. `txtrdr_get_stream` → 条件编译为 Unix-only，文档说明 Windows 不可用
```

---

## 建议 4: 增加修复验证流程

**插入位置**: 第 8 章节末尾，新增章节

**新增内容**:

```markdown
## 9. 接口修复验证流程

修复完成后，必须执行以下验证步骤，确保跨平台兼容性和功能正确性。

---

### 9.1 编译验证

**Windows MSVC**:
```bash
cd D:\Works\Projects\Stardis-GPU\stardis-cpu\<module>\<version>
cmake -G "Visual Studio 17 2022" -A x64 -D<MODULE>_BUILD_TESTS=ON .
cmake --build . --config Debug
cmake --build . --config Release
```

**Linux GCC**:
```bash
cd /home/<user>/star-build/cache/<module>/<version>
make clean && make
```

**验收标准**:
- [ ] Windows Debug 构建成功，无编译错误
- [ ] Windows Release 构建成功，无编译错误
- [ ] Linux 构建成功（使用原有 Makefile）

---

### 9.2 接口兼容性测试

**创建测试用例**，覆盖新旧接口的功能等价性：

```c
/* test_interface_migration.c */

void test_file_path_interface(void) {
  struct image img;
  /* 测试新接口（文件路径） */
  CHK(image_write_ppm_file(&img, 0, "test_output.ppm") == RES_OK);
  
  /* 验证输出文件存在且内容正确 */
  CHK(file_exists("test_output.ppm"));
}

#ifndef OS_WINDOWS
void test_stream_interface_unix_only(void) {
  struct image img;
  FILE* f = fopen("test_output_stream.ppm", "wb");
  /* Unix 平台保持旧接口兼容性 */
  CHK(image_write_ppm_stream(&img, 0, f) == RES_OK);
  fclose(f);
}
#endif

void test_callback_interface(void) {
  struct image img;
  /* 测试新接口（回调） */
  size_t write_fn(const void* data, size_t size, void* ctx) {
    return fwrite(data, 1, size, (FILE*)ctx);
  }
  FILE* f = fopen("test_output_callback.ppm", "wb");
  CHK(image_write_ppm_callback(&img, 0, write_fn, f) == RES_OK);
  fclose(f);
}
```

**验收标准**:
- [ ] 新接口功能与旧接口等价（输出文件内容一致）
- [ ] Windows 使用新接口测试通过
- [ ] Linux 旧接口兼容性测试通过

---

### 9.3 跨模块调用测试（针对 FILE*/va_list 修复）

**目的**: 验证修复后的接口在不同 CRT 配置下行为一致

**创建测试项目**（模拟外部模块）:
```
test_cross_module/
├── module_a/  （使用 /MD 编译）
│   └── call_rsys_api.c
└── module_b/  （使用 /MT 编译）
    └── call_rsys_api.c
```

**测试代码**:
```c
/* call_rsys_api.c */
#include <rsys/image.h>

void external_module_calls_rsys(void) {
  struct image img;
  /* ✅ 安全：使用文件路径接口 */
  image_write_ppm_file(&img, 0, "output.ppm");
  
  /* ❌ 不安全：Windows 不应编译通过 */
#ifdef OS_WINDOWS
  /* FILE* f = fopen(...); */
  /* image_write_ppm_stream(&img, 0, f); // 应产生编译错误 */
#endif
}
```

**验收标准**:
- [ ] 模块 A（/MD）调用新接口成功
- [ ] 模块 B（/MT）调用新接口成功
- [ ] Windows 编译器阻止 FILE* 接口调用（条件编译生效）

---

### 9.4 文档更新

**必须更新以下文档**:

1. **api_conflicts.md** - 记录所有修复的接口
   ```markdown
   ## [Module Name] - FILE* 接口修复
   
   | 原接口 | 新接口（Windows） | 迁移说明 |
   |--------|------------------|----------|
   | `image_write_ppm_stream(img, bin, stream)` | `image_write_ppm_file(img, bin, path)` | 使用文件路径替代 FILE* |
   ```

2. **模块 README.md** - 说明平台差异
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

**验收标准**:
- [ ] api_conflicts.md 完整记录所有修复
- [ ] 模块文档说明平台差异
- [ ] 头文件注释标注废弃/限制

---

### 9.5 回归测试

**Windows**:
```bash
cd D:\Works\Projects\Stardis-GPU\stardis-cpu\<module>\<version>
ctest -C Debug --output-on-failure
ctest -C Release --output-on-failure
```

**Linux**:
```bash
cd /home/<user>/star-build/cache/<module>/<version>
export LD_LIBRARY_PATH=.
./test_<module>
./test_<module>_all  # 如果有完整测试套件
```

**验收标准**:
- [ ] Windows Debug 所有测试通过
- [ ] Windows Release 所有测试通过
- [ ] Linux 所有测试通过（包括兼容性测试）
- [ ] 修复未引入新的 regression

---

### 9.6 最终验收清单

| 检查项 | Windows | Linux | 状态 |
|--------|---------|-------|------|
| 编译成功（Debug） | ✅ | ✅ | |
| 编译成功（Release） | ✅ | ✅ | |
| 单元测试通过 | ✅ | ✅ | |
| 接口兼容性测试通过 | ✅ | ✅ | |
| 跨模块调用测试通过 | ✅ | N/A | |
| 文档更新完成 | ✅ | ✅ | |
| api_conflicts.md 记录完整 | ✅ | ✅ | |
| 回归测试通过 | ✅ | ✅ | |

**迁移完成标准**: 所有检查项标记为 ✅
```

---

## 实施建议

**对于 SOP 维护者**:

1. **高优先级（建议立即采纳）**:
   - ✅ 建议 1: 内联函数 CRT 绑定检查规则 - 填补规则空白
   - ✅ 建议 4: 修复验证流程 - 提升质量保证

2. **中优先级（建议采纳）**:
   - ✅ 建议 3: FILE* 批量修复策略 - 提升实用性
   - ⚠️ 建议 2: va_list 豁免条款 - 降低不必要的修改工作量

3. **低优先级（可选）**:
   - 补充更多实际案例和截图

**对于 LLM 执行者**:

- 在执行接口审计时，参考本文档的完善建议
- 对于内联函数中的 errno，主动检查并建议修复方案
- 对于 va_list 接口，先判定是否跨模块调用，再决定是否修复
- 修复完成后，必须执行建议 4 中的验证流程

---

## 附录：RSys 审计完整报告

详见 `cross_project_audit/rsys_interface_audit_report.md`
