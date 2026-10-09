# Stardis OXS3D 研究归档

原始 Stardis 为法国 Meso-Star 团队的工作。此仓库保存 Windows/CMake 移植、OptiX s3d 后端、混合 Wavefront 求解器及研究资料。

- GPU 工程位于根目录；CPU 基线位于 baselines/stardis-cpu/。
- 构建要求与已验证范围见 README.md、PUBLICATION_VALIDATION.md、KNOWN_LIMITATIONS.md。
- 文档导航见 guide/publication_101lab/README.md；实验数据及哈希见 manifests/。
- 构建输出使用 > build.log 2>&1；运行 stardis 使用 > result.ht 2> runtime.log，结果与运行日志分开。
- 使用双精度、避免热路径动态分配、针对性测试；不要在未验证前把历史问题标为已解决。
- CHANGE_LOG 是忽略的本地日志；修改后按本地日志规则记录，不修改归档的历史快照。
