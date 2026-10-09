# 2026-10-09 发布验证

| 项目 | 结果 |
|---|---|
| 原研究树保护 | 发布在独立副本中进行；未重置原分支或删除文件 |
| CPU/GPU 历史 | 导入现存 CPU 父历史；保存 GPU 各分支、本地修改及两个 stash |
| CPU 5 个未跟踪头文件 | 已确认均由 `stardis/0.12/CMakeLists.txt` 的 configure_file 从 .in 生成，保留生成规则 |
| CPU/GPU 新构建 | 配置未通过：发布机器的 VS2022 未提供可用 MSVC C++ 工具链，尚未进行编译/运行验证 |
| 编辑器相关测试 | 86 个 task_runner_tests 通过；不代表全部 GUI 测试 |
| 论文源码 | 隔离工程 latexmk + XeLaTeX 编译成功；存在原有重复 PDF page destination 警告；结果 PDF 不发布 |
| 原始数据 | 校验信息见 manifests；上传及递归获取状态由总仓库 RELEASE_STATUS.md 记录 |

CPU/GPU 配置失败是当前发布环境限制，尚不足以判断源码在完整依赖环境中的构建结果。历史性能结果保持原出处和参数，不重写为本次验证结果。
