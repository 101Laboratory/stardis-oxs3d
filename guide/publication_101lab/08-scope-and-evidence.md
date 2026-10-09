# 范围 依据与更新方法

返回 [总目录](README.md)。本目录是本地盘点及发布方案，不能替代最终发布物或构建验证报告。

## 用户已确定的原则

1. 整理毕业论文及相关代码工作区，为 101Laboratory 留下可理解、可复现、可继续开发的成果。
2. 面向 101Lab 公开完整研究材料，包括开发过程、数据、论文相关文档、评审和答辩资料。
3. 论文只公开完整 LaTeX 源工程及必要输入，不发布整篇论文生成结果和环境，以缩减二进制体积。
4. STARDIS 的 CPU 迁移版本与多个 GPU worktree 归为同一项目、同一主仓库；各条研究历史用分支保留，代码、文档、数据必须配套。

据此最终方案为一个 STARDIS 研究主仓库及编辑器、论文两个配套仓库。主仓库采用 `stardis-oxs3d`，用户明确不接受 `stardis-cuda` 作为成果发布名；后者仅记录为现有远端历史地址。原始 Stardis 属于法国团队，Windows 迁移、放弃的 cus3d 路线和最终 OptiX 核心须明确区分。嵌套仓库完整性是发布验收条件。

## 盘点边界

- 完整枚举 `D:/Stardis-GPU`、`D:/stardis-editor`、`D:/thesis` 三个研究工作区。未扫描其他磁盘、个人目录、Zotero 数据库或浏览器存储；附带的可视化工作区不含在研究清单中。
- 排除 `.git` 对象、`.venv/venv`、`node_modules`、`__pycache__`、`.pytest_cache`、符号链接和本发布规划目录；明确记录排除路径。构建文件纳入枚举，后续发布时分类排除。
- 文件类别是后缀/路径启发式，包含 `review` 提示；不是自动删除或发布授权。`.txt`、`.obj`、`.pdf` 等必须结合实际用途判定。
- 本地 Git 使用只读命令，每次命令指定当前路径 `safe.directory`，不写全局 Git 配置、不执行 fetch/checkout/reset。`origin/*` 是本地缓存，只有与公开 API 的当前分支 SHA 对照后才可说远端一致。
- 本次看到 13 个 Git 位置：CPU、GPU、编辑器、论文 4 个独立仓库，GPU 的 8 个附加工作树，以及论文模板子模块。独立仓库数不等于最终项目数。
- 公开 GitHub REST API 对两个账号分页读取完成，得到组织 4 个、个人 38 个公开仓库。连接器按 owner 列表返回空，不据此判断账号没有仓库；公开 API 成为此次完整公开列表的依据。
- `stardis-win` 与 `thesis` 的公开 API 返回 404；连接器对前者返回 404、对后者读取失败。可能是权限、私有或地址变化，当前只能标记“未验证远端”，不能宣称未曾发布。
- 没有运行构建、求解器、性能实验、测试套件或论文编译；没有进行全面秘密/权利审核。已做文件枚举、Git 状态、远端 SHA、当前/历史 gitlink 与对象存在性、字面量 LaTeX 依赖和文档链接检查。

## 证据文件

| 文件 | 内容 |
|---|---|
| [local-files.csv](evidence/local-files.csv) | 每个枚举文件的工作区、相对路径、字节数、启发式类别、UTC 修改时间 |
| [local-summary.json](evidence/local-summary.json) | 一级目录统计、大文件、排除路径、扫描错误；本次扫描错误为 0 |
| [local-git.json](evidence/local-git.json) | 13 个 Git 位置的 HEAD、分支、状态、remote、worktree、作者与差异摘要 |
| [supplemental-git.json](evidence/supplemental-git.json) | 两个 stash、未推送提交、未合入主线分支、论文已跟踪输出及模板差异 |
| [remote-github.json](evidence/remote-github.json) | GitHub 公开 API 返回的仓库元数据、分支、tag、release 与顶层目录 |
| [thesis-source-scope.csv](evidence/thesis-source-scope.csv) | 论文逐文件候选发布范围与排除理由 |
| [thesis-references.json](evidence/thesis-references.json) | LaTeX 字面量引用的静态存在性检查 |
| [selected-sha256.json](evidence/selected-sha256.json) | 论文正文/参考文献和一致性 CSV 的内容哈希；不是全部 23 GiB 档案的哈希 |
| [nested-repositories.json](evidence/nested-repositories.json)、[nested-history.json](evidence/nested-history.json) | 当前和历史嵌套仓库/gitlink 结构 |
| [cpu-nested-objects.json](evidence/cpu-nested-objects.json) | CPU 历史的 30 个模块提交引用及对象存在性 |
| [upstream-mirror-tree.json](evidence/upstream-mirror-tree.json) | 原始 Stardis 个人镜像当前默认提交的递归树 |

远端原始响应保留 `url` 与获取时间；用户可直接据此复查。只记录公开元数据，不读取凭据。部分 Windows Git 输出中的中文路径存在编码替换，准确文件名以 UTF-8 CSV 文件清单为准。

## 重新生成

在 `D:/Stardis-GPU` 执行，Python 3 标准库即可。脚本只写 `--output` 目录的报告；不推送 Git 或改写源码。

```powershell
python guide/publication_101lab/scripts/inventory.py --output guide/publication_101lab/evidence --remote
python guide/publication_101lab/scripts/catalog.py
python guide/publication_101lab/scripts/render_inventory.py
```

第一个脚本支持 `--roots`；省略 `--remote` 只更新本地三份主证据，已有远端文件会保留旧时间，须留意时效。第二个脚本使用默认论文路径 `D:/thesis`，读取本目录 evidence 并生成文档目录、论文候选范围和部分 SHA-256。第三个脚本重画本地和远端清单表，但其中的人工说明仍须对照新证据复核。`supplemental-git.json`、嵌套结构证据及人工分析文档需另行复核更新，不能因为重新跑过 inventory 就视为同步更新。

库清单中的 GitHub 容量是 API 的仓库元数据，不等同于本地 checkout 大小；本地体积以 MiB/GiB 二进制单位计，包含重复 checkout，不做内容去重。所有规划和状态均以 2026-10-09 的本次盘点为界。
