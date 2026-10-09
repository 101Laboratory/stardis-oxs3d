# CMake 模板索引

**项目**: STARDIS-GPU CMake 构建系统  
**维护者**: Sisyphus Agent  
**最后更新**: 2026-01-21  

---

## 📦 可用版本

### v1.1 (最新) ✅

**发布日期**: 2026-01-21  
**状态**: 推荐用于生产环境  
**评分**: 9.0/10  

**亮点**:
- ✅ 减少 330 行重复代码
- ✅ 5/5 关键错误预防
- ✅ 自动化验证脚本
- ✅ 数组+循环 Workspace 配置
- ✅ 统一命名空间生成

**文档**:
- [README.md](v1.1/README.md) - 完整使用指南
- [CHANGELOG.md](v1.1/CHANGELOG.md) - 版本变更日志
- [HelperFunctions.cmake](v1.1/HelperFunctions.cmake) - 核心辅助函数
- [CMakeLists_workspace.txt](v1.1/CMakeLists_workspace.txt) - Workspace 模板
- [CMakeLists_module.txt](v1.1/CMakeLists_module.txt) - Module 模板
- [validate_cmake_modules.sh](v1.1/validate_cmake_modules.sh) - 验证脚本

**快速开始**:
```bash
cd templates/cmake/v1.1
cat README.md  # 阅读完整文档
bash validate_cmake_modules.sh  # 验证现有项目
```

---

### v1.0 (已淘汰) ⚠️

**发布日期**: 2026-01-20  
**状态**: 已被 v1.1 替代  
**评分**: 7.2/10  

**问题**:
- ❌ 未阻止 5 个关键错误
- ❌ 330 行重复代码
- ❌ 缺少反模式警告
- ❌ 无自动化验证

**仅用于**:
- 历史参考
- 对比研究

**不推荐用于新项目**

---

## 🎯 选择指南

| 场景 | 推荐版本 | 原因 |
|------|----------|------|
| 新项目 | v1.1 | 最佳实践，完整功能 |
| 现有项目迁移 | v1.1 | 减少技术债，提升质量 |
| 学习 CMake | v1.1 | 包含详细文档和示例 |
| 维护 v1.0 项目 | v1.1 | 兼容升级，风险低 |
| 历史研究 | v1.0 | 了解演进过程 |

---

## 📚 相关文档

### 核心文档
- [cmake_template_audit.md](/principles/cmake_template_audit.md) - v1.1 设计依据

### 旧版模板（参考）
位于 `/templates/` 根目录：
- `cmake_工程级模板与工具集（workspace_sop）.md`
- `template_cmakelists_workspace_level.txt`
- `template_cmakelists_module_level.txt`
- `template_cmake_module_*.cmake`

**注意**: 这些文档基于 v1.0 设计，已被 v1.1 替代。

---

## 🔄 迁移路径

### 从 v1.0 迁移到 v1.1

**难度**: ⭐⭐☆☆☆ (简单)  
**耗时**: 30-60 分钟（取决于模块数量）  
**风险**: 低（向后兼容）  

**步骤**:
1. 备份现有配置
2. 复制新的 HelperFunctions.cmake
3. 运行验证脚本
4. 根据报告逐步改进

详见: [v1.1/README.md#迁移指南](v1.1/README.md)

---

## 🆕 更新通知

### 订阅方式
监控此文件的变更：
```bash
git log --follow templates/cmake/INDEX.md
```

### 预计下次更新
**v1.2** - 预计 2026-Q2
- 自动化迁移脚本
- 模块生成器
- C++ 增强支持

---

## 🤝 反馈与贡献

### 报告问题
1. 运行 `validate_cmake_modules.sh`
2. 检查 `cmake_template_audit.md` 了解设计原理
3. 提交详细的问题描述

### 建议改进
欢迎提交改进建议，特别是：
- 新的反模式检测
- 更好的错误提示
- 文档改进

---

**版本历史**:
- 2026-01-21: v1.1 发布，重大改进
- 2026-01-20: v1.0 初始版本

**维护者**: Sisyphus Agent  
**项目**: STARDIS-GPU
