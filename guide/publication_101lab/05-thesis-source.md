# 论文 LaTeX 源工程发布范围

返回 [总目录](README.md)。用户已明确：论文公开完整 LaTeX 工程，生成结果和本地环境无需公开。必要插图属于编译输入，不能按“全部二进制”统一删除。

## 已核实的现状

- 工程入口是 `D:/thesis/MainBody.tex`，包括章节、`ReferenceBase.bib` 和 `Template/scuthesis2020.cls`；`.latexmkrc` 配置 XeLaTeX。
- 当前 HEAD 为 `72306b4be707`，相对本地 `origin/master` 领先 1 个提交；远端公开 API 返回 404，不能据此断定仓库已删除或是私有。
- 引言、求解器章节、结论、致谢、参考文献、图表和参考文献样式有未提交修改；不能仅导出 HEAD。
- `Template/` 是子模块，指向 `https://github.com/salomelly/scu_thesis_template.git`，本地 commit `7429516a52d7`。其 `gbt7714-numerical.bst` 有局部改动，必须一并保存。
- 现有 README 主要介绍原论文模板；AGENTS.md 中“摘要和结论仍是占位内容”的描述已不能代表当前正文。发布时应改成研究成果入口，并保留模板致谢和来源。

## 文件分类

| 分类 | 处理 | 例子 |
|---|---|---|
| 论文源码 | 保留 | `MainBody.tex`、`Chapters/*.tex`、`ReferenceBase.bib` |
| 排版与参考文献依赖 | 保留并记录来源 | `Template/` 实际内容、`.cls/.sty/.bst`、`License/` |
| 必要插图 | 保留，能用矢量源重建时再有验证地优化 | `Figures/FSM_*/`、`Figures/Consistency/`、`Figures/editor/`、模板 logo |
| 插图可编辑源 | 保留 | SVG、绘图源码和图设计说明；不把可编辑源误当临时文件 |
| 构建配置与说明 | 保留 | `.latexmkrc`、`Makefile`、`compile.bat`、所需宏包/字体版本的文本说明 |
| 整篇论文输出 | 排除，亦不另做论文 PDF Release | 根目录各 `MainBody*.pdf`、以姓名题目命名的毕业论文 PDF |
| 编译辅助文件 | 排除 | `.aux/.log/.toc/.lof/.lot/.fls/.fdb_latexmk/.xdv/.bbl/.blg` |
| 本机环境 | 排除 | `.venv/`、编辑器会话、已安装 TeX 树、字体二进制包、缓存 |
| 图表备份 | 从源工程排除，历史价值另入总库档案 | `Figures - bak/`、`*.bak` |
| 研究与评审资料 | 按完整公开要求放研究总库 | `OurWork/`、`References/`、`ref_need/`、`report/`、`ReviewComments/` |
| 继承的模板自动化及外来资料 | 单独核对适用性 | `.github/`、`Reference Document/` |

“不公开环境”不影响提供文字形式的安装要求与编译命令。保留 Fandol 等现有模板选择及依赖说明，不打包整套 TeX 安装。构建验收产生的 PDF 仅在验收目录使用，不提交或发布。

## 静态候选范围和体积

完整逐文件分类见 [thesis-source-scope.csv](evidence/thesis-source-scope.csv)。这是发布白名单的候选依据，尚未实际复制或删除文件。

| 分组 | 文件数 | MiB |
|---|---:|---:|
| 常规源工程候选资源 | 89 | 5.51 |
| 直接引用的必要插图 | 37 | 8.84 |
| 源工程候选小计 | **126** | **14.35** |
| 转入研究总库的笔记与评审资料 | 42 | 0.62 |
| 排除的编译中间文件、备份及本机配置 | 165 | 22.51 |
| 排除的整篇论文 PDF | 5 | 47.55 |
| 模板工作流，待核对 | 30 | 0.12 |
| 外来规范文档，待核对 | 1 | 0.45 |

上述体积不含 `.git`、`.venv`，不是 Git 历史压缩包大小。源工程候选保留了部分未直接引用的可编辑图源，进一步裁剪须以完整性优先。

对主文件、章节和模板字面量引用共检查 50 条，未发现字面量目标缺失；详情见 [thesis-references.json](evidence/thesis-references.json)。该检查没有展开 TeX 宏，也未验证字体、宏包、引用格式和版面，不能替代编译。

## 发布时的具体做法

1. 从当前工作目录导出候选集，记录 HEAD 和未提交差异；保持原工程原样。以当前 `.tex` 为源码来源，不从日期较新的 PDF 反推最终版本。
2. 将 `Template/` 的当前实际源码连同局部修改纳入新工程，记录上游地址和 commit，保留其许可。推荐 vendor 这些小文件，避免漏掉子模块未提交修改；若仍用子模块，需先让修改有可公开获取的 commit。
3. 编写研究论文 README：题目、贡献、章节到代码的关系、依赖、构建方法、引用方式、素材来源。
4. 在隔离的干净目录运行 `latexmk -xelatex MainBody.tex`，构建输出重定向到本地日志；核查引用、插图和最终版式。完成后仅提交源码，不提交验收生成物。
5. 用路径级排除规则限制整篇输出，例如根目录 `MainBody*.pdf`；不要设置全局 `*.pdf` 或 `*.png`，否则会丢失必要插图。
6. 检查新仓库的所有提交均不含旧的整篇 PDF、XDV、环境目录。原历史中已跟踪的生成物不会因 `.gitignore` 自动消失。

现有 `MainBody.tex` 中个人署名、学号、导师、日期及评审资料按用户“向 101Lab 公开所有内容”的要求纳入整理范围，不额外设为内部资料。第三方素材的来源和分发依据仍逐项登记。
