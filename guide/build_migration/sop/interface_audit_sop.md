# Interface Audit SOP (Cross-Module / Cross-CRT)

> 适用对象：LLM / 自动迁移代理 / 人工审计
>
> 适用场景：跨模块、跨平台（Linux → Windows）、跨编译器（GCC → MSVC）迁移

---

## 1. 接口审查总则（LLM 必须执行）

**核心原则**：

> **凡是跨模块边界的接口，禁止传递任何由 CRT 管理或隐式绑定 CRT 状态的资源。**

LLM 在迁移或重构过程中，**必须假设：Windows + MSVC + 不同 CRT = 不兼容**。

一旦接口违反规则，**必须修改接口定义，并同步修改所有调用点**。

---

## 2. 禁止跨模块传递的资源类型

包括但不限于：

- `FILE*` / `FILE`
- `va_list`
- `jmp_buf`
- `errno`（直接或间接访问）
- 由 `malloc` / `calloc` / `realloc` 分配、但在其他模块释放的对象

> 说明：即使在 Linux + GCC 下“看起来可行”，在 Windows + MSVC 下也**不被视为合法设计**。

---

## 3. LLM 执行步骤（强制顺序）

1. 扫描 **本模块调用的所有外部接口**，检查是否使用上述资源类型。
2. 扫描 **本模块导出的所有接口**，检查是否暴露上述资源类型。
3. 一旦判定为不安全：
   - **直接修改接口定义**（允许破坏 ABI）
   - Unix / Linux 下可用条件编译保留旧接口
   - Windows 下必须提供安全接口
4. 将资源型参数替换为：
   - 文件路径（`const char* path`）
   - 回调（`fn + ctx`）
   - 纯数据结构（POD）
5. **在同一提交中同步修改所有调用点**。
6. 在 `api_conflicts.md` 中记录：
   - 原接口
   - 新接口
   - 调用替换示例
7. 在 `migration_exception.md` 中记录修改原因。

---

## 4. 特殊判定规则（LLM 不得省略）

### 4.1 内联函数中的 CRT 绑定

**规则**：

> 内联函数会在调用方模块展开，其内部的 CRT 访问视为发生在调用方模块。

必须额外检查：
- 内联函数中读写 `errno`
- 内联函数中使用 `va_arg`
- 内联函数中调用 CRT API（`strtod`, `fprintf`, `malloc` 等）

**处理方式（三选一）**：
1. Windows 分支禁用内联（宏控制）
2. 重写为不依赖 CRT 状态的实现
3. 改为非内联函数，仅在模块内部实现

未处理则视为 **接口审查失败**。

---

### 4.2 `va_list` 的豁免规则

`va_list` **默认视为违规接口**，除非同时满足：

- 所有调用点均在同一模块内
- 接口被明确标注为 `@internal`
- 文档中明确禁止跨模块调用

否则：
- 必须修改接口（移除 `va_list`）
- 或提供 Windows 专用替代接口

LLM **不得自行假设调用范围**。

---

### 4.3 `FILE*` 接口的批量修复模式

当模块中存在 **多个 FILE\*** 接口时，LLM 必须采用统一替换策略，禁止零散修复。

**三层推荐模型**：

1. **路径接口（首选，跨平台安全）**
2. **回调接口（高级用法）**
3. **FILE\* 接口（仅 Unix，条件编译）**

Windows 平台：
- 禁止导出 FILE\* 接口
- 禁止返回 FILE\*

---

## 5. 常见禁止接口 → 安全替代速查表（附录 A）

| 禁止类型 | 风险原因 | 推荐替代方案 |
|---------|----------|--------------|
| FILE* | CRT 内部结构不兼容 | `const char* path` / 写回调 |
| va_list | ABI / 表示不兼容 | 可变参数接口 / 参数数组 |
| errno | CRT 状态隔离 | 返回码 / 显式错误值 |
| malloc 对象 | 跨 CRT free 崩溃 | create/destroy 成对接口 |
| jmp_buf | 栈/ABI 不兼容 | 显式错误返回 |

---

## 6. api_conflicts.md 记录模板（附录 B）

```markdown
### image_write_ppm_stream

**原接口（不安全）**
```c
ares_T image_write_ppm_stream(const struct image*, int, FILE*);
```

**问题原因**
- 使用 FILE* 跨模块边界
- Windows + MSVC 不同 CRT 下必然崩溃

**新接口（安全）**
```c
ares_T image_write_ppm_file(const struct image*, int, const char* path);
```

**调用替换示例**
```c
// 原调用
image_write_ppm_stream(&img, 0, stdout);

// 新调用
image_write_ppm_file(&img, 0, "output.ppm");
```
```

---

## 7. 审查完成判定标准

接口审查仅在以下条件全部满足时视为完成：

- [ ] Windows 分支无 FILE\* / va_list / errno / jmp_buf 跨模块接口
- [ ] 所有违规接口均已修改定义
- [ ] 所有调用点已同步更新
- [ ] `api_conflicts.md` 已完整记录
- [ ] Linux / Windows 均可成功构建

---

> 本 SOP 用于 **约束 LLM 的接口修改行为**，防止“表面可编译、运行期必崩”的跨 CRT 设计。
>
> 若与历史接口兼容性冲突，**以运行时安全为最高优先级**。

