# RSys 0.15 审计验证报告

**验证日期**: 2026-01-18  
**验证范围**: RSys 0.15 模块接口审计建议实施验证  
**参考文档**: 
- `sop/interface_audit_sop_v3_simplified.md`
- `cross_project_audit/api_conflicts.md`
- `cross_project_audit/rsys_interface_audit_report.md`

---

## 执行摘要

### 🚨 **关键发现：审计建议未完全实施**

本次验证针对 RSys 0.15 模块的 9 个已识别接口违规项进行代码级检查，发现：

| 违规类型 | 文档记录数量 | 代码实施数量 | 实施率 | 状态 |
|---------|-------------|-------------|-------|------|
| **FILE* 接口** | 4 个 | **0 个** | **0%** | ❌ 未实施 |
| **va_list 接口** | 3 个 | **0 个** | **0%** | ❌ 未实施 |
| **errno 内联函数** | 2 个 | **0 个** | **0%** | ❌ 未实施 |
| **malloc/free 管理** | 0 个（符合规范） | N/A | N/A | ✅ 合规 |

**结论**: `api_conflicts.md` 中记录的所有修复建议**均未在代码中实施**。文档与代码存在严重不一致。

---

## 详细验证结果

### 1. FILE* 接口违规（4个）- ❌ 未实施

#### 1.1 image_read_ppm_stream

**文档声称的修复**:
```c
// api_conflicts.md 行 75-77 声称：
// "修复状态": ✅ 条件编译（Unix 保留，Windows 禁用）
#ifndef OS_WINDOWS
RSYS_API res_T image_read_ppm_stream(struct image* img, FILE* stream);
#endif
```

**实际代码状态** (`src/image.h:75-77`):
```c
RSYS_API res_T
image_read_ppm_stream
  (struct image* image,
   FILE* stream);
```

**验证结果**: ❌ **无条件编译守卫，接口在所有平台暴露**

---

#### 1.2 image_write_ppm_stream

**文档声称的修复**:
```c
// api_conflicts.md 行 77 声称：
// "修复状态": ✅ 条件编译（Unix 保留，Windows 禁用）
```

**实际代码状态** (`src/image.h:86-89`):
```c
RSYS_API res_T
image_write_ppm_stream
  (const struct image* image,
   const int binary,
   FILE* stream);
```

**验证结果**: ❌ **无条件编译守卫，接口在所有平台暴露**

---

#### 1.3 txtrdr_stream

**文档声称的修复**:
```c
// api_conflicts.md 行 129 声称：
// "修复状态": ✅ 条件编译（Unix 保留，Windows 禁用）
```

**实际代码状态** (`src/text_reader.h:34-39`):
```c
RSYS_API res_T
txtrdr_stream
  (struct mem_allocator* allocator, /* May be NULL <=> default allocator */
   FILE* stream,
   const char* name, /* Stream name. May be NULL */
   const char comment, /* Char preceeding a comment. 0 <=> no comment char */
   struct txtrdr** txtrdr);
```

**验证结果**: ❌ **无条件编译守卫，接口在所有平台暴露**

---

#### 1.4 txtrdr_get_stream

**文档声称的修复**:
```c
// api_conflicts.md 行 176 声称：
// "修复状态": ✅ 条件编译（Unix 保留，Windows 禁用）
```

**实际代码状态** (`src/text_reader.h:84-86`):
```c
RSYS_API FILE*
txtrdr_get_stream
  (const struct txtrdr* txtrdr);
```

**验证结果**: ❌ **无条件编译守卫，接口返回 FILE* 在所有平台暴露**

---

### 2. va_list 接口违规（3个）- ❌ 未实施

#### 2.1 logger_vprint

**文档声称的修复**:
```c
// api_conflicts.md 行 244 声称：
// "修复状态": ✅ 豁免修复（增加警告注释）
/**
 * @internal This function is for internal use only within this module.
 * @warning On Windows, va_list cannot be passed across module boundaries.
 */
```

**实际代码状态** (`src/logger.h:132-138`):
```c
/* The value of vargs is undefined after the call of logger_vprint */
RSYS_API res_T
logger_vprint
  (struct logger* logger,
   const enum log_type type,
   const char* log,
   va_list vargs);
```

**验证结果**: ❌ **无 @internal 或 @warning 注释**

---

#### 2.2 str_vprintf

**文档声称的修复**:
```c
// api_conflicts.md 行 286 声称：
// "修复状态": ✅ 豁免修复（增加警告注释）
```

**实际代码状态** (`src/str.h:130-134`):
```c
RSYS_API res_T
str_vprintf
  (struct str* str,
   const char* fmt,
   va_list vargs_list);
```

**验证结果**: ❌ **无 @internal 或 @warning 注释**

---

#### 2.3 str_append_vprintf

**文档声称的修复**:
```c
// api_conflicts.md 行 323 声称：
// "修复状态": ✅ 豁免修复（增加警告注释）
```

**实际代码状态** (`src/str.h:136-140`):
```c
RSYS_API res_T
str_append_vprintf
  (struct str* str,
   const char* fmt,
   va_list vargs_list);
```

**验证结果**: ❌ **无 @internal 或 @warning 注释**

---

### 3. errno 内联函数违规（2个）- ❌ 未实施

#### 3.1 cstr_to_long

**文档声称的修复** (api_conflicts.md 行 354-367):
```c
// 修复状态: ✅ 已重写实现
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

**实际代码状态** (`src/cstr.h:69-84`):
```c
static INLINE res_T
cstr_to_long(const char* str, long* dst)
{
  char* end;
  ASSERT(dst);
  if(!str) return RES_BAD_ARG;
  errno = 0;  /* ❌ 仍然设置 errno */
  *dst = strtol(str, &end, 10/* base */);
  if(end == str || errno == ERANGE)  /* ❌ 仍然无条件检查 errno */
    return RES_BAD_ARG;
  for(;*end != '\0'; ++end) {
    if(*end != ' ' && *end != '\t')
      return RES_BAD_ARG;
  }
  return RES_OK;
}
```

**验证结果**: ❌ **文档声称的优化未实施，仍使用原始 errno = 0 + 无条件检查模式**

---

#### 3.2 cstr_to_ulong

**文档声称的修复**:
```c
// api_conflicts.md 同样声称已重写实现
```

**实际代码状态** (`src/cstr.h:101-117`):
```c
static INLINE res_T
cstr_to_ulong(const char* str, unsigned long* dst)
{
  char* end;
  ASSERT(dst);
  if (!str) return RES_BAD_ARG;
  errno = 0;  /* ❌ 仍然设置 errno */
  *dst = strtoul(str, &end, 10/* base */);
  if(end == str || errno == ERANGE)  /* ❌ 仍然无条件检查 errno */
    return RES_BAD_ARG;
  ASSERT(errno == 0);  /* ❌ 甚至断言 errno */
  for(; *end != '\0'; ++end) {
    if(*end != ' ' && *end != '\t')
      return RES_BAD_ARG;
  }
  return RES_OK;
}
```

**验证结果**: ❌ **文档声称的优化未实施，且存在 errno 断言**

---

### 4. malloc/free 管理 - ✅ 合规（无违规）

**验证结果**: 
- 所有堆内存分配通过 `struct mem_allocator` 抽象层管理
- 所有对象遵循 `create/destroy` 配对模式
- 无跨模块 malloc/free 暴露
- **无需修复，设计合规**

---

## 根因分析

### 为什么文档与代码不一致？

| 可能原因 | 证据 |
|---------|------|
| **1. 文档先行，代码未跟进** | api_conflicts.md 创建于 2026-01-18，但代码最后修改时间 2025 年 |
| **2. 修复计划而非实施记录** | 文档使用"修复状态"而非"实施状态"，可能是设计文档 |
| **3. 审计与实施分离** | rsys_interface_audit_report.md 明确提出"修复计划"（预计 4.5-5.5 天），暗示未实施 |
| **4. 多版本管理混乱** | rsys/0.15 目录包含大量 vcxproj 文件，可能是从 Unix 复制未修改 |

---

## 对比：审计报告 vs 实际代码

### rsys_interface_audit_report.md (行 492-529) 明确指出：

```markdown
### 5.3 RSys 模块具体修复计划

**阶段 1: FILE* 接口重构**（预计 2-3 天）
**阶段 2: va_list 接口评估**（预计 1 天）
**阶段 3: errno 内联函数优化**（预计 0.5 天）
**阶段 4: 验证与文档**（预计 1 天）

**总计**: 4.5-5.5 天工作量
```

**结论**: 这是**修复计划**，而非**实施记录**。api_conflicts.md 的"修复状态 ✅"是**误导性标记**。

---

## 影响评估

### 当前代码状态的风险

| 违规类型 | Windows 平台风险 | Unix 平台风险 |
|---------|-----------------|--------------|
| **FILE* 接口** | 🔴 **高风险** - 跨 CRT 调用必然崩溃 | 🟢 无风险 |
| **va_list 接口** | 🔴 **高风险** - 参数读取错误/崩溃 | 🟢 无风险 |
| **errno 内联函数** | 🟡 **中风险** - 可能读取错误 errno 副本 | 🟢 无风险 |

### 如果在 Windows 上编译当前代码会发生什么？

1. **编译阶段**: ✅ 编译通过（FILE*/va_list 在语法上合法）
2. **链接阶段**: ✅ 链接通过（符号存在）
3. **运行阶段**: ❌ **崩溃**
   - 调用 `image_write_ppm_stream` 时访问无效 FILE* 内存布局
   - 调用 `logger_vprint` 时读取错误的 va_list 参数
   - `cstr_to_long` 读取错误 CRT 的 errno

---

## 修复建议

### 立即执行（阻塞 Windows 移植）

#### 优先级 1: FILE* 接口条件编译

```c
// src/image.h
#ifndef OS_WINDOWS
RSYS_API res_T image_read_ppm_stream(struct image* image, FILE* stream);
RSYS_API res_T image_write_ppm_stream(const struct image* image, const int binary, FILE* stream);
#endif

// src/text_reader.h
#ifndef OS_WINDOWS
RSYS_API res_T txtrdr_stream(struct mem_allocator* allocator, FILE* stream, const char* name, const char comment, struct txtrdr** txtrdr);
RSYS_API FILE* txtrdr_get_stream(const struct txtrdr* txtrdr);
#endif
```

**受影响调用点**: 需搜索整个代码库，迁移到 `*_file` 版本（已存在）

#### 优先级 2: va_list 接口添加警告

```c
// src/logger.h
/**
 * @internal For internal use only. External modules should use logger_print().
 * @warning Windows: va_list cannot cross module boundaries due to CRT isolation.
 */
RSYS_API res_T logger_vprint(struct logger* logger, const enum log_type type, const char* log, va_list vargs);

// src/str.h (同样处理)
```

**验证步骤**: 
1. 搜索跨模块调用点：`grep -r "logger_vprint" stardis-cpu/*/src/`
2. 如有跨模块调用，必须重构调用方

#### 优先级 3: errno 内联函数重写

```c
// src/cstr.h
static INLINE res_T
cstr_to_long(const char* str, long* dst)
{
  char* end;
  ASSERT(dst);
  if(!str) return RES_BAD_ARG;
  *dst = strtol(str, &end, 10);
  if(end == str)
    return RES_BAD_ARG;
  /* 仅在可能溢出时检查 errno */
  if(*dst == LONG_MAX || *dst == LONG_MIN) {
    if(errno == ERANGE)
      return RES_BAD_ARG;
  }
  for(;*end != '\0'; ++end) {
    if(*end != ' ' && *end != '\t')
      return RES_BAD_ARG;
  }
  return RES_OK;
}

// cstr_to_ulong 同样处理，移除 ASSERT(errno == 0)
```

---

## 验证清单（修复后执行）

参照 `interface_audit_sop_v3_simplified.md` 第 550-587 行：

- [ ] Windows Debug 构建成功（exit code 0）
- [ ] Windows Release 构建成功（exit code 0）
- [ ] Linux 构建成功
- [ ] 无条件编译错误（Windows 禁用 FILE* 接口时）
- [ ] Windows Debug 测试全部通过
- [ ] Windows Release 测试全部通过
- [ ] Linux 测试全部通过
- [ ] Windows 分支无 FILE*/va_list 跨模块接口（豁免除外）
- [ ] 所有违规接口已修改定义
- [ ] 所有调用点已同步更新
- [ ] 条件编译正确（`#ifndef OS_WINDOWS`）
- [ ] `api_conflicts.md` 更新为实际实施状态
- [ ] 模块 README.md 已说明平台差异
- [ ] 头文件注释已标注 `@deprecated` 或平台限制
- [ ] 接口定义和调用点在同一次提交中修改
- [ ] 未引入新的回归
- [ ] 豁免的 va_list 接口已添加 `@internal` 警告

---

## 对 SOP 的建议

### SOP 需要增加的条款

**第 8 章节末尾增加"审计验证流程"**:

```markdown
## 8.X 审计验证流程

完成审计并记录到 api_conflicts.md 后，**必须验证修复已在代码中实施**：

1. **代码级验证**
   ```bash
   # 对 api_conflicts.md 中每个"已修复"接口，检查实际代码
   grep -A 3 -B 3 "function_name" module/src/*.h
   ```

2. **编译验证**
   - Windows: 编译成功且无 FILE*/va_list 警告
   - Linux: 编译成功，Unix-only 接口可用

3. **文档一致性**
   - api_conflicts.md 的"修复状态"必须准确反映代码实际状态
   - 使用"✅ 已实施"（代码已修改）而非"✅ 已记录"（仅写文档）

4. **验证报告**
   - 生成 `<module>_audit_verification_report.md`
   - 列出所有验证项的通过/失败状态
```

---

## 结论

**RSys 0.15 模块的审计工作分为两个阶段**:

| 阶段 | 状态 | 产出 |
|------|------|------|
| **1. 违规识别** | ✅ 完成 | rsys_interface_audit_report.md（540 行，详尽分析） |
| **2. 修复实施** | ❌ 未完成 | 代码未修改，api_conflicts.md 状态标记不准确 |

**下一步行动**:

1. **立即执行** - 按本报告"修复建议"章节实施代码修改
2. **验证通过** - 执行本报告"验证清单"
3. **更新文档** - 修正 api_conflicts.md 的"修复状态"为实际实施状态
4. **提交审计** - 生成最终审计完成报告

**预计工作量**: 
- FILE* 接口修复: 2-3 小时（条件编译 + 调用点迁移）
- va_list 注释添加: 30 分钟
- errno 内联函数重写: 1 小时
- 验证与测试: 2 小时
- **总计: 5.5-6.5 小时**

---

**报告生成时间**: 2026-01-18 15:05:19  
**验证工具**: Explore agents + direct grep + file inspection  
**审计者**: Sisyphus (OhMyOpenCode AI Agent)
