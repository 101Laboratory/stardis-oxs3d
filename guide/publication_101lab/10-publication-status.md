# 101Lab 发布执行状态

2026-10-09，用户已授权公开发布。Git 署名为 `ericpu <eric_pu@foxmail.com>`；使用已授权的 EricSolshkov 账号（101Laboratory 管理员）。

## 已完成的本地准备

- 独立发布副本位于 `D:/Stardis-Publication-101Lab/repos`，保留原工作区和个人远端。
- 保留 GPU 各实验分支及未提交研究快照、两个 stash；导入现存 CPU 父历史到 baselines/stardis-cpu。
- 已确认 stardis-win 与 thesis 是可访问的个人私有仓库。公开 API 的旧 404 不代表仓库不存在。
- 765 份研究源文件/小型资料直接入 Git；2,981 份大型原始资料已分卷归档，逐个解压流和 SHA-256 校验通过。
- 论文完整源码编译成功，生成输出不发布；编辑器相关 86 项测试通过。
- 求解器配置未通过：当前 VS2022 缺可用 MSVC C++ 工具链，未运行求解器。此限制随归档说明公开。
- 常见凭据模式检查覆盖 3,538 个当前文本类文件、5,203 个历史 blob，未发现匹配；历史无超过 100 MiB 的 blob。不是穷尽式秘密扫描。

## 目标仓库

- https://github.com/101Laboratory/stardis-research
- https://github.com/101Laboratory/stardis-oxs3d
- https://github.com/101Laboratory/stardis-editor
- https://github.com/101Laboratory/stardis-thesis

此文件保存发布准备阶段快照；实时完成情况以 [总仓库发布状态](https://github.com/101Laboratory/stardis-research/blob/main/RELEASE_STATUS.md) 为准。早期嵌套历史的 27 个缺失 SHA 仍作为已知档案缺口保留。
