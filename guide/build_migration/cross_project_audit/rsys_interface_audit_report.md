# RSys 0.15 接口审计报告

**审计时间**: 2026-01-18  
**审计范围**: RSys 0.15 模块所有导出接口  
**审计目标**: 验证 SOP 第 8 章节"接口审查"规则的可行性和完善性  

---

## 执行摘要

本次审计对 RSys 0.15 模块的 **58 个导出 API 函数**进行了全面扫描，重点识别**跨模块 CRT 资源传递**的违规接口。

**关键发现**:
- ✅ **无 jmp_buf 使用** - 未发现任何接口传递 jmp_buf
- ⚠️ **FILE* 违规**: 4 个接口直接传递 FILE* 跨模块边界
- ⚠️ **va_list 违规**: 4 个接口直接传递 va_list 跨模块边界  
- ⚠️ **errno 内部使用**: 1 个模块（cstr.h）在内联函数中读取 errno（虽未跨模块传递，但存在 CRT 绑定风险）
- ✅ **malloc 对象管理**: 所有堆分配通过自定义 mem_allocator 抽象，符合规范

**SOP 合规性**: **部分合规** - 需要修复 8 个违规接口

---

## 1. CRT 资源类型使用情况

### 1.1 FILE* 违规接口（4 个）

| 接口 | 文件 | 违规类型 | 风险等级 |
|------|------|----------|----------|
| `image_read_ppm_stream(struct image*, FILE*)` | `rsys/image.h:75` | 接受 FILE* 参数 | 高 |
| `image_write_ppm_stream(const struct image*, int, FILE*)` | `rsys/image.h:86` | 接受 FILE* 参数 | 高 |
| `txtrdr_stream(struct mem_allocator*, FILE*, const char*, char, struct txtrdr**)` | `rsys/text_reader.h:34` | 接受 FILE* 参数 | 高 |
| `txtrdr_get_stream(const struct txtrdr*)` | `rsys/text_reader.h:84` | 返回 FILE* | 高 |

**违规说明**:  
这些接口直接在模块边界传递 `FILE*`，违反 Windows MSVC 的 CRT 隔离要求。不同模块使用不同 CRT 运行时时，FILE* 对象的内存布局和管理函数不兼容，会导致运行时崩溃。

**修复建议**（按 SOP 第 8 章节规则）:  
```c
// 原接口（不安全 - Unix 分支保留）
#ifndef OS_WINDOWS
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
#endif

// 新接口（安全 - Windows 分支）
#ifdef OS_WINDOWS
RSYS_API res_T image_write_ppm_file(
    const struct image* img, int bin, const char* path);
#endif

// 或使用回调模式（跨平台）
typedef size_t (*stream_write_fn)(const void* data, size_t size, void* ctx);
RSYS_API res_T image_write_ppm_callback(
    const struct image* img, int bin, stream_write_fn write_fn, void* ctx);
```

---

### 1.2 va_list 违规接口（4 个）

| 接口 | 文件 | 违规类型 | 风险等级 |
|------|------|----------|----------|
| `logger_vprint(struct logger*, enum log_type, const char*, va_list)` | `rsys/logger.h:134` | 接受 va_list 参数 | 高 |
| `str_vprintf(struct str*, const char*, va_list)` | `rsys/str.h:131` | 接受 va_list 参数 | 高 |
| `str_append_vprintf(struct str*, const char*, va_list)` | `rsys/str.h:137` | 接受 va_list 参数 | 高 |
| （内部）`str_common_vprintf` | 实现文件 | 接受 va_list 参数 | 中 |

**违规说明**:  
`va_list` 是 CRT 管理的可变参数列表，其内部表示在不同编译器/平台间不兼容。跨模块传递 `va_list` 会导致参数读取错误。

**特殊情况**:  
- `logger_vprint` 和 `str_vprintf` 系列函数是为了支持 wrapper 函数（如用户自定义日志包装器）而设计的
- 这些函数通常在**同一模块内**被调用，而非跨模块调用
- 但 API 形式上仍然违反 SOP 规则

**修复建议**:  
```c
// 方案 1: 条件编译保留 Unix 接口，Windows 禁用
#ifndef OS_WINDOWS
RSYS_API res_T logger_vprint(
    struct logger* logger, enum log_type type, const char* log, va_list vargs);
#endif

// 方案 2: 提供格式字符串 + 参数数组的替代接口（Windows）
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
#endif

// 方案 3: 仅保留可变参数版本（logger_print），移除 vprint
// （这是最简单的方案，因为用户很少直接调用 vprint）
```

---

### 1.3 errno 内部使用（非跨模块传递）

| 接口/函数 | 文件 | 使用方式 | 风险等级 |
|-----------|------|----------|----------|
| `cstr_to_double`（内联） | `rsys/cstr.h:70-80` | 读取 errno 检测 strtod 溢出 | 低 |
| `cstr_to_uint`（内联） | `rsys/cstr.h:102-112` | 读取 errno 检测 strtoul 溢出 | 低 |

**代码示例**:
```c
static INLINE res_T
cstr_to_double(const char* str, double* value)
{
  char* end = NULL;
  errno = 0;
  *value = strtod(str, &end);
  if(end == str || errno == ERANGE)
    return RES_BAD_ARG;
  // ...
}
```

**违规说明**:  
虽然 `errno` 未作为参数跨模块传递，但在**内联函数**中读取 errno 时，实际的 errno 访问会在**调用方模块**中展开。如果调用方和定义方使用不同的 CRT，可能访问到错误的 errno 副本。

**修复建议**:  
```c
// 方案 1: 禁用内联，确保 errno 访问在 RSys 模块内部
#ifdef OS_WINDOWS
  #define CSTR_INLINE  /* 空，禁用内联 */
#else
  #define CSTR_INLINE INLINE
#endif

static CSTR_INLINE res_T
cstr_to_double(const char* str, double* value) { /* ... */ }

// 方案 2: 使用标准库的错误返回机制（推荐）
// C11 的 strtod 可以通过返回值 + endptr 检测错误，无需 errno
static INLINE res_T
cstr_to_double_safe(const char* str, double* value)
{
  char* end = NULL;
  *value = strtod(str, &end);
  if(end == str || *value == HUGE_VAL || *value == -HUGE_VAL)
    return RES_BAD_ARG;
  return RES_OK;
}
```

---

### 1.4 jmp_buf 使用

**结果**: ✅ **未发现任何 jmp_buf 使用**  
RSys 模块不使用 setjmp/longjmp 异常处理机制。

---

### 1.5 malloc 对象管理

**分析**:  
RSys 通过 `struct mem_allocator` 抽象层管理所有堆内存分配，提供以下接口：
- `mem_alloc` / `mem_calloc` / `mem_realloc` / `mem_rm`
- `mem_alloc_aligned` （对齐分配）
- 自定义分配器：`mem_init_proxy_allocator`, `mem_init_lifo_allocator`

**合规性**: ✅ **完全合规**  
所有导出的指针类型（如 `struct mutex*`, `struct cond*`, `struct txtrdr*`）都通过配对的 `create/destroy` 函数管理生命周期，分配和释放在**同一模块内**完成。

**示例**:
```c
// 创建和销毁在同一模块（RSys）内完成
RSYS_API struct mutex* mutex_create(void);      // 分配
RSYS_API void mutex_destroy(struct mutex* m);   // 释放
```

**无需修复** - 当前设计已符合 SOP 要求。

---

## 2. 外部接口调用情况

RSys 模块**不调用其他项目模块**的接口，它是基础库，位于依赖树的底层。

**外部依赖**（仅标准库）:
- C 标准库: `stdio.h`, `stdlib.h`, `string.h`, `math.h`, `stdarg.h`, `errno.h`
- POSIX（Unix 分支）: `pthread.h`, `dlfcn.h`, `time.h`
- Windows API（Windows 分支）: `windows.h`（用于原子操作、线程同步、动态库加载）

**合规性**: ✅ **符合基础库定位**  
RSys 作为底层工具库，不依赖其他项目模块，避免了跨模块 CRT 资源传递的风险。

---

## 3. 违规接口汇总与修复优先级

### 高优先级（必须修复）

| 序号 | 接口 | 违规类型 | 影响范围 |
|------|------|----------|----------|
| 1 | `image_write_ppm_stream` | FILE* 参数 | 所有调用此接口写入 PPM 图像到流的模块 |
| 2 | `image_read_ppm_stream` | FILE* 参数 | 所有调用此接口从流读取 PPM 图像的模块 |
| 3 | `txtrdr_stream` | FILE* 参数 | 所有使用流初始化文本读取器的模块 |
| 4 | `txtrdr_get_stream` | 返回 FILE* | 所有需要访问底层流的调用方 |

### 中优先级（建议修复）

| 序号 | 接口 | 违规类型 | 影响范围 |
|------|------|----------|----------|
| 5 | `logger_vprint` | va_list 参数 | 自定义日志包装器（通常在同一模块内） |
| 6 | `str_vprintf` | va_list 参数 | 自定义字符串格式化包装器 |
| 7 | `str_append_vprintf` | va_list 参数 | 自定义字符串格式化包装器 |

### 低优先级（可选修复）

| 序号 | 接口 | 违规类型 | 影响范围 |
|------|------|----------|----------|
| 8 | `cstr_to_double/uint` 内联函数中的 errno | errno 内部读取 | 跨 CRT 调用时可能出现错误检测失败 |

---

## 4. SOP 第 8 章节可行性评估

### 4.1 SOP 规则覆盖性分析

**SOP 规定的禁止类型**:
- ✅ FILE* - **明确覆盖**，审计发现 4 个违规接口
- ✅ va_list - **明确覆盖**，审计发现 4 个违规接口
- ✅ jmp_buf - **明确覆盖**，未发现使用
- ⚠️ errno - **部分覆盖**，SOP 列出但未详细说明内联函数中的使用场景
- ✅ malloc 对象 - **明确覆盖**，审计确认通过 create/destroy 模式安全管理

**结论**: SOP 规则**基本完整**，涵盖了主要的 CRT 资源类型。

---

### 4.2 SOP 执行流程可行性

**SOP 第 8 章节操作步骤**:

1. ✅ **扫描外部接口调用** - 通过 grep + explore 代理成功完成
2. ✅ **扫描导出接口定义** - 通过 ast-grep + explore 代理成功完成
3. ✅ **识别资源型参数** - 通过正则匹配和手动检查成功完成
4. ⚠️ **判定不安全后修改接口** - 需要人工决策（例如 va_list 函数是否真正跨模块调用）
5. ✅ **条件编译保留 Unix 接口** - 技术上可行，示例代码已提供
6. ✅ **记录到 api_conflicts.md** - 文档流程清晰，本报告可作为模板

**发现的流程缺陷**:

| 缺陷 | 描述 | 建议改进 |
|------|------|----------|
| **内联函数中的 errno** | SOP 未明确说明内联函数中间接使用 errno 的处理规则 | 增加内联函数的 CRT 绑定检查规则 |
| **va_list 的同模块调用** | SOP 未区分跨模块 vs 同模块的 va_list 传递 | 增加"如果 va_list 仅在模块内部使用，且有明确文档说明，可豁免修改"条款 |
| **FILE* 的条件编译示例** | SOP 提供了单个函数示例，但未说明批量修改策略 | 增加"对于 stream 系列函数，统一提供 file/callback 替代接口"的批量修复指南 |
| **修复验证流程** | SOP 未说明修复后如何验证跨平台兼容性 | 增加"修复后必须在 Windows + Linux 双平台测试"验证步骤 |

---

### 4.3 SOP 规则完善建议

基于本次审计，建议对 SOP 第 8 章节进行以下增强：

#### 建议 1: 增加内联函数 CRT 绑定检查规则

**在第 8 章节"包括但不限于以下类型"部分后增加**:

```markdown
### 特殊场景：内联函数中的 CRT 资源

内联函数会在调用方模块中展开，因此内联函数体内的 CRT 资源访问实际发生在调用方的 CRT 环境中。

**需要检查的情况**:
- 内联函数中读取/写入 `errno`
- 内联函数中调用 CRT 函数（如 `malloc`, `fprintf`, `strtod`）
- 内联函数中使用 `va_arg` 宏

**处理规则**:
1. 如果内联函数访问 errno，考虑禁用内联（Windows 分支）或重写为不依赖 errno 的版本
2. 如果内联函数调用 CRT 分配函数，必须在同一内联函数内释放，或改为非内联函数
3. 文档必须明确说明该内联函数的 CRT 依赖

**示例**:
\`\`\`c
// 原内联函数（不安全）
static INLINE res_T cstr_to_double(const char* str, double* value) {
  errno = 0;  // ⚠️ errno 访问在调用方 CRT 中展开
  *value = strtod(str, &end);
  if(errno == ERANGE) return RES_BAD_ARG;
}

// 修复方案 1: Windows 禁用内联
#ifdef OS_WINDOWS
  #define CSTR_INLINE  /* 禁用内联，确保 errno 访问在 RSys 模块内 */
#else
  #define CSTR_INLINE INLINE
#endif

// 修复方案 2: 重写为不依赖 errno
static INLINE res_T cstr_to_double_safe(const char* str, double* value) {
  char* end;
  *value = strtod(str, &end);
  // 使用返回值和 HUGE_VAL 检测错误，无需 errno
  if(end == str || *value == HUGE_VAL) return RES_BAD_ARG;
}
\`\`\`
```

#### 建议 2: 增加 va_list 同模块调用的豁免条款

**在第 8 章节"操作步骤"第 3 步后增加**:

```markdown
**3.1 va_list 特殊判定规则**

`va_list` 接口通常用于支持可变参数包装器，存在以下两种场景：

| 场景 | 是否违规 | 处理方式 |
|------|----------|----------|
| 跨模块调用（外部模块传入 va_list） | 是 | 必须修复 |
| 同模块内调用（仅本模块内部使用） | 否 | 可豁免，但需文档说明 |

**判定方法**:
1. 搜索项目中所有调用该 va_list 接口的位置
2. 检查调用方是否与接口定义在同一编译单元/模块
3. 如果所有调用都在同一模块内，可标记为"内部接口，无需修改"
4. 在接口注释中增加 `@internal This function is for internal use only. Do not call across module boundaries.`

**示例**:
\`\`\`c
/**
 * @internal This function is for internal use only within the RSys module.
 * External modules should use logger_print() instead.
 * 
 * @warning On Windows, va_list cannot be passed across module boundaries due to CRT isolation.
 */
RSYS_API res_T logger_vprint(
    struct logger* logger, enum log_type type, const char* log, va_list vargs);
\`\`\`

如果审计发现确有跨模块调用，则必须按 SOP 标准流程修复。
```

#### 建议 3: 增加 FILE* 批量修复策略

**在第 8 章节"示例：接口与调用同步修改"部分后增加**:

```markdown
### FILE* 接口批量修复模式

对于有多个 FILE* 相关接口的模块（如图像库、文本读取库），建议采用**三层接口**设计：

**层级 1: 文件路径接口**（推荐，跨平台安全）
\`\`\`c
RSYS_API res_T image_write_ppm_file(
    const struct image* img, int bin, const char* path);
\`\`\`

**层级 2: 回调接口**（高级，最大灵活性）
\`\`\`c
typedef size_t (*write_callback)(const void* data, size_t size, void* ctx);
RSYS_API res_T image_write_ppm_callback(
    const struct image* img, int bin, write_callback write_fn, void* ctx);
\`\`\`

**层级 3: FILE* 接口**（仅 Unix，条件编译）
\`\`\`c
#ifndef OS_WINDOWS
RSYS_API res_T image_write_ppm_stream(
    const struct image* img, int bin, FILE* stream);
#endif
\`\`\`

**迁移策略**:
1. 新增层级 1 和层级 2 接口（Windows + Unix 通用）
2. 将层级 3 接口标记为 Unix-only（条件编译）
3. 更新文档，推荐使用层级 1，高级用户使用层级 2
4. 逐步废弃层级 3（Linux 保留兼容性，Windows 直接禁用）

**调用方迁移示例**:
\`\`\`c
// 原调用（不安全）
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_stream(&img, 0, f);
fclose(f);

// 迁移后（安全）
image_write_ppm_file(&img, 0, "output.ppm");

// 或使用回调（高级）
size_t my_write(const void* data, size_t size, void* ctx) {
  // 自定义写入逻辑（网络、内存、压缩流等）
  return fwrite(data, 1, size, (FILE*)ctx);
}
FILE* f = fopen("output.ppm", "wb");
image_write_ppm_callback(&img, 0, my_write, f);
fclose(f);
\`\`\`
```

#### 建议 4: 增加修复验证流程

**在第 8 章节末尾增加新章节**:

```markdown
### 接口修复验证流程

修复完成后，必须执行以下验证步骤：

**步骤 1: 编译验证**
\`\`\`bash
# Windows MSVC
cmake -G "Visual Studio 17 2022" -DRSYS_BUILD_TESTS=ON .
cmake --build . --config Release

# Linux GCC
cd /path/to/linux/env
make clean && make
\`\`\`

**步骤 2: 接口兼容性测试**
- 编写测试用例，覆盖新旧接口的功能等价性
- 确保 Windows 使用新接口，Linux 可选使用旧接口（保持兼容）

**步骤 3: 跨模块调用测试**（针对 FILE*/va_list 修复）
- 创建测试项目，模拟外部模块调用修复后的接口
- 验证不同 CRT 配置下（/MD vs /MT）接口行为一致

**步骤 4: 文档更新**
- 在 api_conflicts.md 中记录所有修复的接口
- 更新模块文档，说明 Windows 平台的接口差异
- 标注废弃的接口（Unix-only）

**步骤 5: 回归测试**
- 运行模块完整测试套件（Windows + Linux）
- 确认修复未引入新的 regression

**验收标准**:
- [ ] Windows 编译通过，所有测试通过
- [ ] Linux 编译通过，所有测试通过（包括使用旧接口的兼容性测试）
- [ ] 新接口文档完整，示例代码清晰
- [ ] api_conflicts.md 完整记录所有修复
\`\`\`
```

---

## 5. 结论与行动建议

### 5.1 SOP 可行性评级

| 评估维度 | 评级 | 说明 |
|----------|------|------|
| **规则完整性** | ⭐⭐⭐⭐☆ (4/5) | 覆盖主要 CRT 资源类型，但内联函数场景需补充 |
| **执行可行性** | ⭐⭐⭐⭐⭐ (5/5) | 通过工具 + 人工审查可完整执行 |
| **修复指导性** | ⭐⭐⭐☆☆ (3/5) | 单个接口示例清晰，但批量修复策略不足 |
| **验证流程** | ⭐⭐☆☆☆ (2/5) | 缺少修复后的验证步骤和质量保证 |

**总体评级**: ⭐⭐⭐⭐☆ (4/5) - **可行且有效，需要小幅完善**

---

### 5.2 行动建议

**对于 LLM 执行者**:

1. ✅ **可直接执行** - SOP 第 8 章节的核心流程（扫描、识别、修改）已验证可行
2. ⚠️ **需要人工决策** - 对于 va_list 接口，需判断是否真正跨模块调用
3. ✅ **可自动化** - 接口扫描和违规检测可通过工具（grep/ast-grep）完全自动化
4. ⚠️ **需要领域知识** - FILE* 的回调模式设计需要理解业务场景

**对于 SOP 维护者**:

1. **高优先级** - 增加"建议 1: 内联函数 CRT 绑定检查规则"（填补规则空白）
2. **中优先级** - 增加"建议 3: FILE* 批量修复策略"（提升实用性）
3. **中优先级** - 增加"建议 4: 修复验证流程"（提升质量保证）
4. **低优先级** - 增加"建议 2: va_list 豁免条款"（减少不必要的修改）

**对于 RSys 模块**:

1. **必须修复** - 4 个 FILE* 接口（高优先级）
2. **建议修复** - 4 个 va_list 接口（如确有跨模块调用）
3. **可选修复** - `cstr.h` 中的 errno 使用（低风险，但提升健壮性）

---

### 5.3 RSys 模块具体修复计划

**阶段 1: FILE* 接口重构**（预计 2-3 天）

1. 新增文件路径接口:
   - `image_read_ppm_file(img, path)`
   - `image_write_ppm_file(img, binary, path)`
   - `txtrdr_file_ex(allocator, path, comment, txtrdr)` - 已存在，无需新增

2. 条件编译旧接口:
   ```c
   #ifndef OS_WINDOWS
   RSYS_API res_T image_read_ppm_stream(struct image*, FILE*);
   RSYS_API res_T image_write_ppm_stream(const struct image*, int, FILE*);
   RSYS_API res_T txtrdr_stream(..., FILE*, ...);
   RSYS_API FILE* txtrdr_get_stream(const struct txtrdr*);
   #endif
   ```

3. 更新所有调用方（搜索代码库中的调用点）

**阶段 2: va_list 接口评估**（预计 1 天）

1. 搜索 `logger_vprint` / `str_vprintf` 的所有调用点
2. 确认是否存在跨模块调用
3. 如存在，增加警告注释或提供替代接口
4. 如不存在，标记为内部接口并文档说明

**阶段 3: errno 内联函数优化**（预计 0.5 天）

1. 重写 `cstr_to_double/uint` 为不依赖 errno 的版本
2. 或 Windows 分支禁用内联

**阶段 4: 验证与文档**（预计 1 天）

1. 双平台编译测试
2. 更新 api_conflicts.md
3. 更新模块文档

**总计**: 4.5-5.5 天工作量

---

## 6. 附录：完整接口清单

详见 explore 代理返回的 58 个接口列表（已在执行摘要中汇总）。

---

**报告完成** - 此报告可作为 SOP 第 8 章节可行性验证的证据和模板。
