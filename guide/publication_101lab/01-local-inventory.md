# 本地成果清单

返回 [总目录](README.md)。盘点覆盖三个研究工作区；统计包含构建产物和重复 worktree，不是发布包大小。文件枚举清单见 [local-files.csv](evidence/local-files.csv)。

## 工作区和仓库结构

`D:/Stardis-GPU` 顶层不是 Git 仓库，包含 Windows 迁移基线、OptiX 核心主库及其 8 个附加工作树。编辑器、论文分别有独立仓库，论文模板是子模块。合计 13 个 Git 位置，不代表 13 个项目；最终 CPU/GPU 及研究资料归到一个 `stardis-oxs3d` 主库。

原始 Stardis 属于法国 Meso-Star 团队；本地模块目录中的上游代码和依赖不应计为个人原创。贡献演进见 [归属与结构](09-lineage-and-nested-repos.md)。

## Stardis-GPU

48,490 个枚举文件，23,894.25 MiB。

| 目录 | 文件数 | MiB | 内容及发布落点 |
|---|---:|---:|---|
| `(root files)` | 2 | 0.01 | 保留来源和用途，按文件级清单整理 |
| `.opencode` | 15 | 0.14 | 本机开发辅助材料，区分有价值的说明与本机状态 |
| `.vscode` | 2 | 0.00 | 编辑器设置，发布前检查用途与路径 |
| `GPU_WF_Validation` | 116 | 9.16 | 全 GPU 探索验证的代码和结果 |
| `Stardis-Starter-Pack` | 312 | 421.63 | 示例、场景、模型、实验输出和脚本；需拆清输入/结果 |
| `cuda-duplex-validation` | 26 | 81.85 | CUDA 双工诊断源码、trace、Nsight 记录 |
| `debug_issues` | 82 | 3,821.21 | 问题证据与结论；超大 trace 放同版本附件 |
| `guide` | 223 | 10.10 | 架构、迁移、物理、并行化和未来路线；随主代码发布 |
| `optimization` | 128 | 56.04 | 优化与负结果；保留关联实验分支 |
| `optix-throughput-validation` | 1,540 | 926.34 | 后端基准项目，含需要排除的构建树 |
| `perf_diag` | 15 | 39.78 | 吞吐/性能 CSV 与诊断采样 |
| `physical_consistency_stats` | 47 | 4.98 | CPU/GPU 一致性 CSV、绘图程序和图 |
| `profiling` | 2,766 | 13,225.22 | 大型 VTune 原始档案；同仓库数据附件 |
| `scripts` | 3 | 0.02 | 实验运行、计时分析或论文工具脚本 |
| `stardis-cpu` | 1,953 | 252.51 | Windows 构建迁移成果，CPU 对照组；导入统一主库 |
| `stardis-oxs3d` | 2,669 | 1,005.27 | 核心 GPU 主库与实验分支共同 Git 对象库 |
| `stardis-oxs3d-cpu-wf` | 6,366 | 811.10 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |
| `stardis-oxs3d-merge-phase` | 2,179 | 303.15 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |
| `stardis-oxs3d-o14` | 5,873 | 551.76 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |
| `stardis-oxs3d-o16` | 4,794 | 500.96 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |
| `stardis-oxs3d-stream-order` | 4,169 | 421.95 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |
| `stardis-oxs3d-test-validation` | 6,907 | 607.33 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |
| `stardis-oxs3d-wos-fix` | 5,872 | 551.88 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |
| `stardis-oxs3d-wos-test` | 2,431 | 291.87 | 同一 GPU 仓库的实验 worktree；以分支和配套实验记录发布 |

## stardis-editor

109 个枚举文件，1.09 MiB。

| 目录 | 文件数 | MiB | 内容及发布落点 |
|---|---:|---:|---|
| `(root files)` | 13 | 0.09 | 保留来源和用途，按文件级清单整理 |
| `.vscode` | 3 | 0.00 | 编辑器设置，发布前检查用途与路径 |
| `config_examples` | 2 | 0.01 | 示例参数 |
| `config_library` | 2 | 0.00 | 配置库，检查可移植路径 |
| `design` | 27 | 0.33 | 编辑器设计文档 |
| `scripts` | 4 | 0.02 | 实验运行、计时分析或论文工具脚本 |
| `src` | 32 | 0.53 | 编辑器源码 |
| `tests` | 26 | 0.11 | 编辑器测试 |

## thesis

369 个枚举文件，85.60 MiB。

| 目录 | 文件数 | MiB | 内容及发布落点 |
|---|---:|---:|---|
| `(root files)` | 84 | 52.59 | 保留来源和用途，按文件级清单整理 |
| `.github` | 30 | 0.12 | 继承自动化，需核实适用性 |
| `.vscode` | 3 | 0.00 | 编辑器设置，发布前检查用途与路径 |
| `Chapters` | 28 | 0.54 | 论文正文源文件 |
| `Figures` | 87 | 17.38 | 论文图源及必要编译输入 |
| `Figures - bak` | 71 | 12.54 | 重复/旧图备份，源工程排除，研究档案按内容去重 |
| `License` | 2 | 0.06 | 保留来源和用途，按文件级清单整理 |
| `OurWork` | 9 | 0.07 | 论文实验与实现笔记，配套主库 |
| `Reference Document` | 1 | 0.45 | 保留来源和用途，按文件级清单整理 |
| `References` | 7 | 0.20 | 文献整理 |
| `ReviewComments` | 5 | 0.06 | 答辩/评审和响应资料，纳入公开文档 |
| `Template` | 13 | 0.82 | 论文模板子模块，带本地修改 |
| `assets` | 2 | 0.45 | 保留来源和用途，按文件级清单整理 |
| `config` | 1 | 0.00 | 保留来源和用途，按文件级清单整理 |
| `ref_need` | 8 | 0.10 | 引用需求与文献笔记 |
| `report` | 13 | 0.19 | 审查/模拟评审/修改报告，保留性质标签 |
| `scripts` | 5 | 0.03 | 实验运行、计时分析或论文工具脚本 |

## 容易漏掉的材料

- `OurWork/` 的性能分解、pool scan、单双池对比和后端解释，需要与主库实验一起整理。
- Starter Pack 的场景目录混有 `.ht`、stderr 日志、渲染图、Python/PowerShell 脚本和编辑器工程，不宜整体当作纯示例文件夹。
- `GPU_WF_Validation/` 与 `guide/GPU_WF_Validation/` 分别有代码/数据和设计说明；两者都要纳入。
- 失败优化、未解决问题和旧 cus3d 代码是研究过程的一部分，用明确状态和版本保留。
- 论文模板局部修改、实验 worktree 未提交源码、stash 不会自动随主分支发布。

## 大文件示例

| 工作区 | 文件 | MiB |
|---|---|---:|
| Stardis-GPU | `debug_issues/[RESOLVED]block_firefly_noise/gpu_trace.csv` | 3,806.54 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r009macc/data.0/sep4d24.20260318T230153.339263.tb7` | 936.21 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r014macc/data.0/sep6f1c.20260319T122633.191496.tb7` | 810.07 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r013macc/data.0/sep14cc.20260319T121614.580007.tb7` | 805.74 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r011macc/data.0/sep1080.20260319T112248.701336.tb7` | 792.56 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r010macc/data.0/sep9644.20260318T231709.506560.tb7` | 779.14 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r008hpc/data.0/sep6e4c.20260318T225622.325532.tb7` | 730.16 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r012macc/data.0/sep8414.20260319T113618.066630.tb7` | 524.39 |
| Stardis-GPU | `optix-throughput-validation/build/.vs/optix_throughput_validation/v17/ipch/AutoPCH/d138345ea6498774/OX_S3D_SCENE_VIEW.ipch` | 119.06 |
| Stardis-GPU | `profiling/stardis-oxs3d-aos/r005ue/data.0/sep7ff4.20260227T195012.807749.tb7` | 118.75 |

其中 IDE IPCH 是构建缓存，研究 trace 和剖析 session 是原始实验资料；体积相近不代表相同归档价值。完整超过 100 MiB 的列表在 [local-summary.json](evidence/local-summary.json)。

## 扫描口径

合计 **48,968 文件，23.419 GiB**，扫描错误 0。排除 Git 对象、虚拟环境、部分缓存和本发布规划目录；不做内容去重。当前文件清单覆盖旧版本与生成物，发布范围以方案、逐文件审查和用户约束为准。
