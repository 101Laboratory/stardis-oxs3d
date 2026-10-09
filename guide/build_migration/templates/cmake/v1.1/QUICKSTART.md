# CMake v1.1 快速命令清单

**版本**: 1.1.0  
**用途**: 快速参考和验证  

---

## 🚀 立即应用（5分钟）

### 1. 更新核心文件
```bash
cp templates/cmake/v1.1/HelperFunctions.cmake <project>/cmake/
```

### 2. 运行验证
```bash
cd <project>
bash ../templates/cmake/v1.1/validate_cmake_modules.sh
```

### 3. 重新配置和构建
```bash
cd build
cmake .. && cmake --build . --config Release
```

---

## 📝 模块迁移（10分钟/模块）

### 最小迁移（添加验证）

在 `set(<LIB>_SOURCES ...)` 后添加：
```cmake
validate_library_sources(<LIB>_SOURCES)
```

### 完整迁移（使用新函数）

替换命名空间生成代码：
```cmake
# 删除这 20 行
set(<LIB>_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
foreach(header ${<LIB>_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(...)
endforeach()

# 替换为 3 行
generate_namespace_headers(
    TARGET <target> NAMESPACE star HEADERS ${<LIB>_PUBLIC_HEADERS}
)

# 删除 Include Directories 中的
$<BUILD_INTERFACE:${<LIB>_GENERATED_INCLUDE_DIR}>
```

---

## 🔍 验证命令

### 运行验证脚本
```bash
bash templates/cmake/v1.1/validate_cmake_modules.sh
```

### 检查特定模块
```bash
# 检查源文件
grep "set(.*_SOURCES" <module>/CMakeLists.txt

# 检查是否有验证
grep "validate_library_sources" <module>/CMakeLists.txt

# 检查命名空间生成
grep "generate_namespace_headers" <module>/CMakeLists.txt
```

### 构建特定模块
```bash
cd build
cmake --build . --config Release --target <target>
```

---

## 🛠️ 故障排查

### 问题: "validate_library_sources: command not found"
```bash
# 检查 HelperFunctions.cmake 版本
grep "v1.1" <project>/cmake/HelperFunctions.cmake

# 如果是旧版本
cp templates/cmake/v1.1/HelperFunctions.cmake <project>/cmake/

# 重新配置
cd build && cmake ..
```

### 问题: "generate_namespace_headers: command not found"
同上

### 问题: 编译错误 "test file in library sources"
```cmake
# 这是正确的！v1.1 检测到问题
# 解决方法：从 set(<LIB>_SOURCES) 中移除 test_*.c
set(LIB_SOURCES
    src/foo.c
    # src/test_foo.c  ← 删除这行
)
```

---

## 📊 验证输出解读

### ✅ 全部通过
```
[ CHECK 1 ] ✅ No dangerous file(GLOB) patterns found
[ CHECK 2 ] ✅ No test files found in library sources
[ CHECK 3 ] ✅ Test headers properly filtered
[ CHECK 4 ] ✅ Using generate_namespace_headers()
[ CHECK 5 ] ✅ No unused helper functions
[ CHECK 6 ] ✅ All modules require CMake 3.20+

Validation Summary: 0 critical issues, 0 warnings
🎉 All checks passed!
```

### ⚠️ 有警告
```
[ CHECK 4 ] ⚠️  Found 9 modules with manual namespace header generation

Validation Summary: 0 critical issues, 1 warning
```
**含义**: 有模块仍在使用手动代码，建议迁移到 `generate_namespace_headers()`

### ❌ 有错误
```
[ CHECK 1 ] ❌ Found dangerous file(GLOB) pattern:
   - star-foo/1.0/CMakeLists.txt

Validation Summary: 1 critical issue
```
**含义**: 必须修复，使用显式列表或 `glob_library_sources()`

---

## 🔧 常用片段

### 添加验证（复制粘贴）
```cmake
validate_library_sources(S3D_SOURCES)
```

### 生成命名空间（复制粘贴）
```cmake
generate_namespace_headers(
    TARGET s3d
    NAMESPACE star
    HEADERS ${S3D_PUBLIC_HEADERS}
)
```

### 安全的 glob（复制粘贴）
```cmake
glob_library_sources(S3D_SOURCES)
validate_library_sources(S3D_SOURCES)
```

### 过滤测试头文件（复制粘贴）
```cmake
file(GLOB ALL_HEADERS src/*.h)
filter_test_headers(PUBLIC_HEADERS ${ALL_HEADERS})
```

---

## 📁 文件位置速查

```
templates/cmake/v1.1/
├── HelperFunctions.cmake      ← 核心文件
├── CMakeLists_workspace.txt   ← Workspace 模板
├── CMakeLists_module.txt      ← Module 模板
├── validate_cmake_modules.sh  ← 验证脚本
├── README.md                   ← 完整文档
├── EXAMPLE_MIGRATION.md        ← 迁移示例
└── QUICKSTART.md               ← 本文档
```

---

## 🎯 快速检查清单

应用 v1.1 前：
- [ ] 备份现有配置
- [ ] 阅读 CRITICAL GUIDELINES（HelperFunctions.cmake 顶部）
- [ ] 了解 4 个关键规则

应用 v1.1 后：
- [ ] 运行 `validate_cmake_modules.sh`
- [ ] 编译所有库
- [ ] 运行测试
- [ ] 检查是否有新警告

---

**快速上手时间**: 5 分钟  
**完整迁移时间**: 30-60 分钟  
**投资回报率**: 高（减少 250 行代码，提升质量）
