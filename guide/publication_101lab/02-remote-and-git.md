# 远端成果与本地版本对应

返回 [总目录](README.md)。远端取自 2026-10-09 公开 GitHub API；完整响应见 [remote-github.json](evidence/remote-github.json)。现有 `stardis-cuda` 仅是历史远端名，目标成果名称是 `stardis-oxs3d`。

## 与毕业研究直接相关的公开仓库

| 当前远端 | 已核实情况 | 发布关系 |
|---|---|---|
| [EricSolshkov/stardis](https://github.com/EricSolshkov/stardis) | `master`，`84c43566c01c`，GPL-3.0 元数据 | 法国团队原始 Stardis 的镜像/派生入口，不是个人原创求解器 |
| [EricSolshkov/stardis-cuda](https://github.com/EricSolshkov/stardis-cuda) | `main`、`opt/merge-phase` 均为 `d89963a3f09d`；无公开 tag/release | 本地 `stardis-oxs3d` 的 remote；整合后更名发布为组织 `stardis-oxs3d` |
| [EricSolshkov/stardis-editor](https://github.com/EricSolshkov/stardis-editor) | `master=89b66b1d7f9a`，与本地 HEAD 一致；无公开 tag/release | 本地有少量功能/测试修改，整理后发布到组织 |
| [EricSolshkov/custar-3d](https://github.com/EricSolshkov/custar-3d) | 仓库存在，分支列表为空 | 不能依赖它恢复放弃的 CUDA 路线；查核心 Git 历史与旧归档 |
| [EricSolshkov/cuBQL](https://github.com/EricSolshkov/cuBQL)、[EricSolshkov/embree](https://github.com/EricSolshkov/embree) | 依赖 fork，公开元数据为 Apache-2.0 | 记录版本与来源，具体个人修改范围待核实 |

本地 CPU 的 remote 指向 `EricSolshkov/stardis-win`；论文指向 `EricSolshkov/thesis`。两者公开 API 返回 404，表示本次未能验证远端，不能据此断言不存在、未上传或是私有。CPU 本地工程及 origin 的关系已核实。

## 本地 checkout 与当前状态

| 位置 | 分支 | HEAD | 工作区状态 |
|---|---|---|---|
| `D:/Stardis-GPU/stardis-cpu` | `master` | `e5b56a9f821c` | 5 条修改/未跟踪/子模块状态 |
| `D:/Stardis-GPU/stardis-oxs3d` | `main` | `407766cdb455` | 干净；相对本地 upstream 缓存领先 1 |
| `D:/Stardis-GPU/stardis-oxs3d-cpu-wf` | `exp/cpu-wavefront-embree` | `407766cdb455` | 4 条修改/未跟踪/子模块状态 |
| `D:/Stardis-GPU/stardis-oxs3d-merge-phase` | `opt/merge-phase` | `d89963a3f09d` | 1 条修改/未跟踪/子模块状态 |
| `D:/Stardis-GPU/stardis-oxs3d-o14` | `opt/o14-partition` | `635d66153581` | 干净 |
| `D:/Stardis-GPU/stardis-oxs3d-o16` | `opt/o16-mem-access` | `f6d421f7ed42` | 干净 |
| `D:/Stardis-GPU/stardis-oxs3d-stream-order` | `opt/stream-auto-order` | `1489531a3271` | 干净 |
| `D:/Stardis-GPU/stardis-oxs3d-test-validation` | `test-b4-wf-validation` | `1044d05510a7` | 4 条修改/未跟踪/子模块状态 |
| `D:/Stardis-GPU/stardis-oxs3d-wos-fix` | `fix/wos-cp-radius` | `d89963a3f09d` | 3 条修改/未跟踪/子模块状态 |
| `D:/Stardis-GPU/stardis-oxs3d-wos-test` | `test/wos-validation` | `d89963a3f09d` | 3 条修改/未跟踪/子模块状态 |
| `D:/stardis-editor` | `master` | `89b66b1d7f9a` | 9 条修改/未跟踪/子模块状态 |
| `D:/thesis` | `master` | `72306b4be707` | 29 条修改/未跟踪/子模块状态；相对本地 upstream 缓存领先 1 |
| `D:/thesis/Template` | `master` | `7429516a52d7` | 1 条修改/未跟踪/子模块状态 |

公开 API 已独立确认 GPU 当前 main 比本地少 `407766c`（属性冲突诊断提交），编辑器远端 HEAD 与本地一致。论文的领先状态仅对本地缓存成立；远端尚未可读。

CPU 与 GPU 树里没有当前 gitlink，但 CPU 早期历史有 30 个嵌套模块引用，其中 29 条引用缺少提交对象（27 个不同 SHA）；详见 [结构检查](09-lineage-and-nested-repos.md)。

## 发布前不能漏掉的历史

- GPU 主树有 10 个本地分支、9 个 checkout；公开 remote 目前只有 2 个分支。
- `opt/o14-partition` 未合入 `main`。O16 和 stream-auto-order 的提交已在当前主线祖先中，保留名字和负结果/实验说明仍有价值。
- `exp/cpu-wavefront-embree`、WoS 相关工作树和验证工作树有未提交研究代码，不能只推分支引用。
- 两个 stash 分别涉及 GPU_PP 与 PCIe overlap 插桩，原始 SHA 在 supplemental-git.json。
- `.worktree-state` 是本机管理文件，不替代 Git 分支关系或实验版本说明。

## 两个账号的公开仓库总清单

组织已有仓库均不承担本研究的主项目职责。个人其他仓库列出以说明远端盘点范围，未纳入毕业成果发布批次；GitHub fork 标志不等于原创归属（镜像可能显示 fork=false）。

### 101Laboratory

公开列表 4 个，分页读取完成：True。

| 仓库 | fork | 默认分支 | 最近 push UTC | 与本次关系 |
|---|---|---|---|---|
| [awesome-simulation](https://github.com/101Laboratory/awesome-simulation) | 是 | `main` | 2024-07-29T07:23:07Z | 现有组织成果，保持独立 |
| [CopyTranslator](https://github.com/101Laboratory/CopyTranslator) | 是 | `master` | 2024-12-26T08:50:50Z | 现有组织成果，保持独立 |
| [GDCVaultCrawler](https://github.com/101Laboratory/GDCVaultCrawler) | 是 | `master` | 2024-12-19T17:15:22Z | 现有组织成果，保持独立 |
| [PaperDemos](https://github.com/101Laboratory/PaperDemos) | 否 | `master` | 2024-10-08T10:42:52Z | 现有组织成果，保持独立 |

### EricSolshkov

公开列表 38 个，分页读取完成：True。

| 仓库 | fork | 默认分支 | 最近 push UTC | 与本次关系 |
|---|---|---|---|---|
| [ArkPlanner](https://github.com/EricSolshkov/ArkPlanner) | 是 | `master` | 2020-11-01T12:08:59Z | 未纳入本研究批次 |
| [ChordNova](https://github.com/EricSolshkov/ChordNova) | 是 | `master` | 2021-03-25T05:40:49Z | 未纳入本研究批次 |
| [CopyTranslator](https://github.com/EricSolshkov/CopyTranslator) | 否 | `master` | 2024-12-27T09:01:47Z | 未纳入本研究批次 |
| [cuBQL](https://github.com/EricSolshkov/cuBQL) | 是 | `main` | 2026-01-18T21:46:36Z | 研究核心或依赖 |
| [custar-3d](https://github.com/EricSolshkov/custar-3d) | 否 | `main` | 2026-02-08T07:52:45Z | 研究核心或依赖 |
| [DataRecovery](https://github.com/EricSolshkov/DataRecovery) | 是 | `master` | 2017-01-07T11:18:53Z | 未纳入本研究批次 |
| [dsp-calc](https://github.com/EricSolshkov/dsp-calc) | 是 | `main` | 2025-11-29T19:04:47Z | 未纳入本研究批次 |
| [DSPCalculator](https://github.com/EricSolshkov/DSPCalculator) | 是 | `master` | 2025-10-25T13:44:54Z | 未纳入本研究批次 |
| [dsp_blueprint_editor](https://github.com/EricSolshkov/dsp_blueprint_editor) | 是 | `master` | 2025-11-30T13:04:37Z | 未纳入本研究批次 |
| [EasyVtuber](https://github.com/EricSolshkov/EasyVtuber) | 是 | `main` | 2022-04-07T14:40:23Z | 未纳入本研究批次 |
| [embree](https://github.com/EricSolshkov/embree) | 是 | `master` | 2026-02-03T14:41:41Z | 研究核心或依赖 |
| [GDCVaultCrawler](https://github.com/EricSolshkov/GDCVaultCrawler) | 否 | `master` | 2024-12-19T17:15:00Z | 未纳入本研究批次 |
| [GenshinAutoMusic](https://github.com/EricSolshkov/GenshinAutoMusic) | 否 | `master` | 2021-10-25T11:58:26Z | 未纳入本研究批次 |
| [guitarpro](https://github.com/EricSolshkov/guitarpro) | 是 | `master` | 2026-08-19T16:01:08Z | 未纳入本研究批次 |
| [Harmonifold](https://github.com/EricSolshkov/Harmonifold) | 否 | `main` | 2026-09-23T17:42:01Z | 未纳入本研究批次 |
| [Hello-World](https://github.com/EricSolshkov/Hello-World) | 否 | `master` | 2018-07-14T11:07:29Z | 未纳入本研究批次 |
| [Ideal-Piano](https://github.com/EricSolshkov/Ideal-Piano) | 是 | `master` | 2024-05-07T11:42:09Z | 未纳入本研究批次 |
| [LearnOpenGL](https://github.com/EricSolshkov/LearnOpenGL) | 是 | `master` | 2021-11-23T13:02:01Z | 未纳入本研究批次 |
| [MeoAssistance-Arknights](https://github.com/EricSolshkov/MeoAssistance-Arknights) | 是 | `master` | 2025-02-09T05:37:18Z | 未纳入本研究批次 |
| [MeoAssistance-NeuralCloud](https://github.com/EricSolshkov/MeoAssistance-NeuralCloud) | 是 | `master` | 2021-11-03T13:19:14Z | 未纳入本研究批次 |
| [momentum](https://github.com/EricSolshkov/momentum) | 是 | `main` | 2025-07-27T15:30:29Z | 未纳入本研究批次 |
| [Notes](https://github.com/EricSolshkov/Notes) | 否 | `master` | 2026-06-25T10:39:16Z | 未纳入本研究批次 |
| [NotRealElimination](https://github.com/EricSolshkov/NotRealElimination) | 否 | `master` | 2022-03-23T08:19:26Z | 未纳入本研究批次 |
| [Physics2D](https://github.com/EricSolshkov/Physics2D) | 是 | `master` | 2021-12-16T12:58:08Z | 未纳入本研究批次 |
| [Physics2D-TestBed-Qt](https://github.com/EricSolshkov/Physics2D-TestBed-Qt) | 是 | `master` | 2021-12-11T05:10:10Z | 未纳入本研究批次 |
| [Pilot](https://github.com/EricSolshkov/Pilot) | 是 | `main` | 2022-05-09T11:43:56Z | 未纳入本研究批次 |
| [PyQt2048](https://github.com/EricSolshkov/PyQt2048) | 否 | `master` | 2024-07-20T17:04:20Z | 未纳入本研究批次 |
| [renderdoc-for-vscode](https://github.com/EricSolshkov/renderdoc-for-vscode) | 是 | `main` | 2026-05-22T11:37:46Z | 未纳入本研究批次 |
| [RenderDoc-Hack](https://github.com/EricSolshkov/RenderDoc-Hack) | 否 | `main` | 2026-07-07T15:14:35Z | 未纳入本研究批次 |
| [SceneViewExtTest](https://github.com/EricSolshkov/SceneViewExtTest) | 是 | `main` | 2024-06-12T10:20:26Z | 未纳入本研究批次 |
| [sfa-mt-bridge](https://github.com/EricSolshkov/sfa-mt-bridge) | 否 | `main` | 2026-10-08T07:51:21Z | 未纳入本研究批次 |
| [stardis](https://github.com/EricSolshkov/stardis) | 否 | `master` | 2025-12-08T07:10:53Z | 研究核心或依赖 |
| [stardis-cuda](https://github.com/EricSolshkov/stardis-cuda) | 否 | `main` | 2026-03-20T08:41:39Z | 研究核心或依赖 |
| [stardis-editor](https://github.com/EricSolshkov/stardis-editor) | 否 | `master` | 2026-03-31T09:33:10Z | 研究核心或依赖 |
| [Tanks](https://github.com/EricSolshkov/Tanks) | 否 | `main` | 2023-03-02T09:10:01Z | 未纳入本研究批次 |
| [VisionConeVisualizer](https://github.com/EricSolshkov/VisionConeVisualizer) | 否 | `master` | 2024-05-06T14:03:59Z | 未纳入本研究批次 |
| [yas](https://github.com/EricSolshkov/yas) | 是 | `main` | 2021-10-08T12:56:16Z | 未纳入本研究批次 |
| [YTBSpider](https://github.com/EricSolshkov/YTBSpider) | 否 | `master` | 2023-01-28T10:36:33Z | 未纳入本研究批次 |

## 远端范围限制

已核实的是两个指定账号当前公开内容，未证明私有仓库不存在，也未搜索所有组织成员的个人仓库。上述时间为 GitHub pushed_at UTC 原值；盘点日期采用用户的 Asia/Shanghai 日期。
