# 成果归属 演进路线与嵌套仓库完整性

返回 [总目录](README.md)。主仓库拟采用 **101Laboratory/stardis-oxs3d**。`stardis-optix-s3d` 是用户认可的备选名；`stardis-cuda` 仅为现有远端历史名称，不作为最终成果名称。

## 研究贡献的正确边界

| 阶段 | 所属与贡献 | 本地及远端关系 | 在发布中的地位 |
|---|---|---|---|
| 原始 Stardis | 法国 Meso-Star 团队的蒙特卡洛热输运基座 | 上游 `gitlab.com/meso-star/stardis`；个人 `EricSolshkov/stardis` 是镜像/派生入口 | 明确致谢、引用与许可；不归为本研究原创实现 |
| Windows 与构建系统迁移 | 用户将 Linux 工程迁到 Windows，并调整构建系统 | 本地 `stardis-cpu/` 的 origin 为 `EricSolshkov/stardis-win` | 基础工程贡献及 CPU 对照组，纳入同一研究主库 |
| cus3d / custar-3d 尝试 | 用户尝试 CUDA/cuBQL 路线重写关键 s3d 能力，因性能不佳放弃 | 历史提交、`guide/cus3d/`、`guide/embree_migration/`、优化记录；公开 `custar-3d` 当前无分支 | 保留失败路线、代码历史及性能证据，不作为默认实现 |
| oxs3d 与混合 Wavefront | 用户以 OptiX 重写关键 s3d 后端，并完成配套求解器并行化及优化 | 本地 `stardis-oxs3d/` 与 8 个 GPU worktree；当前个人 remote 名为 `stardis-cuda` | 完整研究的核心代码库，发布名 `stardis-oxs3d` |
| 场景编辑器 | 用户的配套 GUI、场景与任务工作流 | 本地及远端 `stardis-editor` 接近，保留当前小范围差异整理 | 整理后发布到 101Lab，避免扩展为无关重构 |
| 毕业论文 | 方法、实验、研究总结和交接材料 | `D:/thesis` | 完整 LaTeX 源工程；与主库实现和实验数据配套 |

README 建议使用“基于 Meso-Star Stardis 的 Windows 移植、OptiX s3d 后端与 Wavefront 混合求解器”，不要使用暗示原始 Stardis 为个人原创的描述。依赖库原作者、上游仓库和版本在 `THIRD_PARTY_NOTICES.md` 分模块记录。

## 当前结构检查结果

检查依据包含磁盘 `.git`、各工作树当前索引的 `160000` gitlink、已跟踪 `.gitmodules`、历史 raw diff 及对象存在性。详见 [当前嵌套检查](evidence/nested-repositories.json)、[历史检查](evidence/nested-history.json)、[CPU 历史对象](evidence/cpu-nested-objects.json)。

- 当前 CPU、GPU 主树及 8 个附加 worktree、编辑器的索引中没有 gitlink，也没有已跟踪 `.gitmodules`。当前磁盘扫描没有发现这些工程内部额外的 `.git`。
- 论文当前有 `Template` gitlink，精确 commit 为 `7429516a52d7a086d3842831f5af7943c406a168`。模板内部还存在未提交样式修改。
- CPU 历史存在 `3df1a7c chore: absorb nested git repositories` 和 `5a5252f absorb nested repos`。历史 raw diff 中检测到 60 条 gitlink 变化，归并为 **30 个路径/提交引用**。
- 对这些 30 个路径/提交引用执行 `git cat-file -e <sha>^{commit}`，**29 条引用的提交对象不在 CPU 父仓库对象库中，对应 27 个不同 SHA**；30 条引用共涉及 28 个不同 SHA。这是对象缺口，不等于当前源码有 29 个模块缺失，也不等于这些上游提交已在所有地方丢失。
- 30 个历史路径中，29 个在当前 HEAD 下有已跟踪文件。唯一不存在的旧路径是 `aw/rsys/0.15`，它与根级 `rsys/0.15` 引用了同一个旧提交；根级路径当前有 129 个已跟踪文件。这支持“旧重复嵌套路径已被展开”的解释，但仍需核对迁移差异，不能仅凭目录存在证明逐文件完整。
- 公开 `EricSolshkov/stardis` 当前默认提交的递归树未截断且无 gitlink，见 [镜像树快照](evidence/upstream-mirror-tree.json)。这只覆盖该提交，不能推出它的所有历史及上游仓库都没有嵌套结构。

## 发布时保证什么

**当前版本源码完整**：检查每个模块实际源文件已被 Git 跟踪，版本头和生成规则齐全；对于仍使用子模块的工程，固定 URL/commit 并验证全新递归 clone 可获取；不提交一个 gitlink 却漏掉对应仓库或访问权限。

**现存历史可追溯**：保留 CPU 父仓库历史、GPU 所有实验 ref、未提交快照、stash 对应研究成果。subtree 导入使用现存父历史，不声称自动把旧嵌套仓库的独立历史一起带入。

**缺失历史可定位**：对 `cpu-nested-objects.json` 每个缺失项登记原模块目录、引用 commit、可能上游和原本机嵌套仓库备份。先查旧工作区/备份及已知个人 fork，再查模块上游；用户并未要求扫描整个个人磁盘，本次没有扩大扫描范围。获取到原 repo 后可保留其对象与归档 refs 到同一主库，或者形成单独的历史档案附件并在主库索引。无需为每个库新建 GitHub 项目。

**实验能够回到源码**：历史基准绑定当时的父仓库 commit、嵌套模块 commit 或可验证源码快照、必要工作区补丁。无法恢复到原 commit 的情况写明“有源码快照，原嵌套历史未恢复”，而非“完整历史已恢复”。

## 尚需执行的结构验收

1. 在已完成的历史路径与当前跟踪文件计数映射上，继续逐文件核对吸收操作保留了哪些源码、许可和修改。
2. 对 27 个不同的缺失提交对象寻找可访问的上游或备份；未能找回的形成显式缺口表。
3. 保留历史 cus3d 代码所在 commit 和对应负结果，确认死代码清理之前的对象在归档 refs 中可达。
4. 对 CPU/GPU 当前版本与拟发布的关键实验分支各做一次独立导出/clone 检查，确认没有外部工作树路径依赖。
5. 对论文 Template 选择 vendor 或固定可访问子模块，保留修改与来源；源工程最终从公开入口能独立获取。

以上检查与本次方案共同构成结构完整性门槛；当前还不能标记为全部完成。
