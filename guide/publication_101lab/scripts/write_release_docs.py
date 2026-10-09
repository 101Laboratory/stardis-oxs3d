"""Write release-facing documentation into the isolated publication copy."""
import json
from pathlib import Path
import shutil

BASE=Path('D:/Stardis-Publication-101Lab')
R=BASE/'repos'
CORE=R/'stardis-oxs3d'
HUB=R/'stardis-research'
THESIS=R/'stardis-thesis'
EDITOR=R/'stardis-editor'
LOCAL=Path('D:/Stardis-GPU/guide/publication_101lab')

def write(p,text):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text.strip()+'\n',encoding='utf8')

def main():
    shutil.copy2(CORE/'README.md',CORE/'UPSTREAM_README.md')
    write(CORE/'README.md',r'''
# Stardis OXS3D

基于法国 **Meso-Star 团队 Stardis** 的 Windows 构建迁移、OptiX s3d 查询后端与 CPU–GPU Wavefront 混合求解器研究归档。原始热输运方法、Stardis 及上游库归原作者；本项目的迁移、后端重写、并行化及实验工作由蒲昱岐（EricPu / EricSolshkov）完成。

[完整成果工作区](https://github.com/101Laboratory/stardis-research) · [编辑器](https://github.com/101Laboratory/stardis-editor) · [论文源工程](https://github.com/101Laboratory/stardis-thesis)

## 研究路线

1. 原始 Linux Stardis：上游基座，见 [保留的原始介绍](UPSTREAM_README.md) 和 [Meso-Star 上游](https://gitlab.com/meso-star/stardis)。
2. `baselines/stardis-cpu/`：Windows 与 CMake 构建迁移，对应原 `stardis-win`，保留 CPU 父仓库历史。
3. cus3d / cuBQL：早期 CUDA 路线，性能不佳后放弃；代码存在历史提交，文档见 `guide/cus3d/`，不作为默认实现。
4. 当前主线：`oxstar-3d/` 用 OptiX 重写关键 s3d 查询；`stardis-solver/` 实现混合 Wavefront 调度与优化。

CPU/GPU 的实验 worktree 作为同一仓库的分支保留，未为每个实验创建项目。`archive/cpu-windows` 保留 CPU 原始 HEAD；`archive/stash-0`、`archive/stash-1` 保留开发暂存历史。分支是研究快照，不保证每一分支均可构建。

## 文件入口

| 路径 | 内容 |
|---|---|
| `CMakeLists.txt`、`stardis/`、`stardis-solver/`、`oxstar-3d/` | 当前 GPU 主线及其他上游库模块 |
| `baselines/stardis-cpu/` | CPU 基线独立构建树 |
| `guide/`、`debug_issues/`、`optimization/` | 架构、问题追踪、成功与失败优化记录 |
| `Stardis-Starter-Pack/` | 场景、STL、脚本及小型实验资料 |
| `physical_consistency_stats/`、`perf_diag/` | 一致性和性能汇总 |
| `GPU_WF_Validation/`、`optix-throughput-validation/`、`cuda-duplex-validation/` | 独立实验源码 |
| `thesis-materials/` | 研究笔记、文献整理、评审/答辩材料 |
| `manifests/` | 来源、分支快照、研究文件哈希、大型数据附件清单 |

[整理与实验导航](guide/publication_101lab/README.md) · [已知限制](KNOWN_LIMITATIONS.md) · [发布验证](PUBLICATION_VALIDATION.md)

## 构建

需要 Windows x64、CMake、Visual Studio 2022 的 C++ 桌面开发工具链；GPU 版本另需 CUDA Toolkit、兼容驱动及 OptiX 9.1 头文件。参考平台参数见历史实验记录，不能将查询吞吐提升等同于完整求解器加速比。

```powershell
git clone --branch v9.1.0 --depth 1 https://github.com/NVIDIA/optix-dev.git ../optix-dev
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DS3D_BACKEND=optix -DOptiX_INSTALL_DIR="../optix-dev" > configure.log 2>&1
cmake --build build --config Release > build.log 2>&1
```

OptiX 头文件也可来自官方 SDK；`OptiX_INSTALL_DIR` 应指向含 `include/optix.h` 的目录。CPU 基线在自身根目录独立运行相同 CMake 构建流程，不传 GPU 后端参数。

运行 stardis 时，`stdout` 是计算结果，`stderr` 是运行日志：使用 `> result.ht 2> runtime.log`。验证优先选择小场景和相关测试，不默认重跑全量历史测试。

## 大型实验数据

原始 trace、VTune/Nsight session、`.ht` 与大图通过同仓库 `research-archive-2026-10-09` Release 保存；每个压缩分卷不超过 512 MiB。Git 保留逐文件 SHA-256 与分卷 SHA-256。

```powershell
python scripts/fetch_research_data.py --list
python scripts/fetch_research_data.py --dataset Stardis-Starter-Pack
```

下载后按原相对路径恢复。`--all` 下载所有档案，体积较大。旧文档中的绝对路径与状态原样保留作为研究历史；当前使用入口以本 README 和发布验证为准。

## 来源与许可

保留 [COPYING](COPYING) 以及各子目录原有许可。外部模块、模型和图片遵循各自原有条件；并不因统一归档就变为个人原创或同一种许可。见 [来源说明](THIRD_PARTY_NOTICES.md)。
''')
    write(CORE/'KNOWN_LIMITATIONS.md',r'''
# 已知限制与历史完整性

这是毕业研究归档，包含成功、失败和未完成实验。

- CPU 父仓库的历史曾吸收嵌套仓库；30 条历史路径/提交引用涉及 28 个不同 SHA，其中 27 个 SHA 不在现存父仓库对象库中。当前 29 个路径有已跟踪文件，旧 `aw/rsys/0.15` 与根级 rsys 指向相同历史提交。保存现存历史不等于恢复全部原子仓库历史。
- 当前主线保留上游和 Windows 迁移代码，不宣称原始 Stardis 为个人原创。早期 cus3d CUDA 路线已放弃。
- `debug_issues/AGENTS.md` 保留实例 prim_id、Picard 偏差复测阻塞、PCIe 重叠、UV、法线及统计等历史问题，未在本次归档中重新证明已解决。
- 旧一致性报告包含 cuBQL 阶段的描述；旧数据与后续 OptiX commit 尚未全部一一绑定。未知 provenance 明确保持未知。
- 历史笔记、脚本可能引用原机盘符与目录；入口构建说明已经独立列出。原始日志及图表不代表本次重新测量。
- 本次未重新运行 GPU/CPU 求解器：当前发布机器未安装可用的 MSVC C++ 工具链，CMake 的 VS2022 配置检查失败。勿将归档标签解释为通过全量测试的稳定软件版本。
''')
    write(CORE/'THIRD_PARTY_NOTICES.md',r'''
# 上游与素材来源

原始 Stardis 和相关 Meso-Star 库来自法国 Meso-Star 团队，保留每个源文件的版权头、原始 README 与 COPYING。原始入口：https://gitlab.com/meso-star/stardis 。Windows/CMake 迁移、cus3d 探索、OptiX s3d 后端与混合 Wavefront 工作由蒲昱岐 / EricPu / EricSolshkov 完成。

CPU/GPU 树中保留了各模块原有版本目录。Embree/TBB、Random123 等依赖保留自身许可，不能视为研究者原创。OptiX/CUDA 工具链由使用者按 NVIDIA 官方渠道获取，不在本次新数据档案中打包安装环境。现存 Git 历史中的原有第三方文件保持来源。

Stardis-Starter-Pack 保留自己的 COPYING。论文相关第三方插图及参考资料的引用以论文和原文件说明为准；本归档不授予超出原权利人的权限。论文模板来源由论文仓库单独列明。

编辑器、论文正文、研究笔记未新加统一开放许可；公开可读不等于授予未声明的再许可。各部分现有许可与作者归属继续适用。
''')
    write(CORE/'PUBLICATION_VALIDATION.md',r'''
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
''')
    write(THESIS/'README.md',r'''
# 基于路径追踪的混合架构耦合热输运红外仿真算法

蒲昱岐，四川大学计算机学院，2026 年硕士毕业论文的完整 LaTeX 源工程。

[完整成果入口](https://github.com/101Laboratory/stardis-research) · [OptiX 后端与求解器](https://github.com/101Laboratory/stardis-oxs3d) · [场景编辑器](https://github.com/101Laboratory/stardis-editor)

原始 Stardis 是法国 Meso-Star 团队的基座工作；论文研究涉及 Windows 构建迁移、OptiX s3d 后端、混合 Wavefront 求解器及场景编辑器。

## 获取与编译

使用带中文支持的 TeX Live、XeLaTeX、BibTeX 和 latexmk：

```powershell
latexmk -xelatex -interaction=nonstopmode MainBody.tex
```

`.latexmkrc`、模板、参考文献样式、章节、BibTeX、必要图片和图源均随工程提供。2026-10-09 在隔离目录编译成功；存在原有重复 page destination 警告，未在本次改写论文排版。

本仓库不包含整篇论文生成 PDF、XDV、编译日志/辅助文件或虚拟环境。必要插图中的 PDF/PNG 是输入资源。研究笔记、评审、答辩和实验材料在核心仓库的 `thesis-materials/`。

## 源码来源

源自当前本地论文工作目录：HEAD `72306b4be707ecfd5e92c26b1a51b57524e7f68a` 加本地最终修改。新建源码发布历史以避免继承整篇 PDF/XDV；原论文历史仍在原仓库保留。

`Template/` 从 https://github.com/salomelly/scu_thesis_template.git 的 `7429516a52d7a086d3842831f5af7943c406a168` 导出，包含本地参考文献样式修改，作为普通源码 vendor 保存。保留原模板作者、版权和 License；模板源自四川大学论文模板项目，许可不自动覆盖论文正文和外来图片。

章节对应：第三章求解器与实验；第四章 OptiX 查询后端；第五章编辑器。论文版本与代码版本由总入口仓库的子模块提交共同固定。
''')
    # Prevent any generated thesis outputs or inherited environment from entering the new history.
    with (THESIS/'.gitignore').open('a',encoding='utf8') as f:f.write('\n/MainBody*.pdf\n*.xdv\n*.aux\n*.bbl\n*.blg\n*.fdb_latexmk\n*.fls\n*.log\n*.toc\n*.lof\n*.lot\n.venv/\n__pycache__/\nCHANGE_LOG\n')
    old=(EDITOR/'README.md').read_text(encoding='utf8')
    write(EDITOR/'README.md','''# Stardis Editor

为 Meso-Star Stardis 及本研究的 Windows/OptiX 实现提供场景编辑、VTK 可视化、边界条件画笔与任务队列。

[完整成果入口](https://github.com/101Laboratory/stardis-research) · [求解器及实验](https://github.com/101Laboratory/stardis-oxs3d) · [论文源码](https://github.com/101Laboratory/stardis-thesis)

```powershell
python -m pip install -r requirements.txt
python run_scene_editor.py
```

运行时需在编辑器中选择实际 stardis/htpp 可执行文件。默认不预置原机路径。2026-10-09 相关 task runner 测试 86 项通过；本发布保留现有历史和本地功能修改。

## 原项目说明

'''+old)
    write(EDITOR/'requirements.txt','PyQt5>=5.15,<6\nvtk>=9,<10')
    write(EDITOR/'requirements-dev.txt','-r requirements.txt\npytest>=8,<10')
    # Preserve original settings in earlier commits; current checkout starts portable and empty.
    write(EDITOR/'editor_settings.json',json.dumps({'search_dirs':[],'recent_exes':[],'recent_work_dirs':[],'recent_projects':[],'last_project_path':'','startup_behavior':'none','exe_tags':{}},indent=2))
    write(EDITOR/'user_settings.json','{}')
    for repo in [CORE,EDITOR,THESIS,HUB]:
        write(repo/'CHANGE_LOG','# CHANGE_LOG — 本地发布整理日志\n# 本文件忽略，不作为正式发布日志。\n\n- [DOCS] 2026-10-09：建立 101Lab 发布副本、入口和来源说明，保留用户原工作区。')
    write(HUB/'.gitignore','CHANGE_LOG\n__pycache__/\n*.pyc')
    write(HUB/'README.md',r'''
# Stardis 毕业研究成果工作区

蒲昱岐（EricPu / EricSolshkov）的毕业研究归档，发布于 **101Laboratory**。原始 Stardis 是法国 **Meso-Star 团队**的工作；本成果在此基础上完成 Windows/CMake 迁移、早期 cus3d 探索、OptiX s3d 后端、混合 Wavefront 求解器及 GUI 编辑器。

## 一次获取完整工作区

```bash
git clone --recurse-submodules https://github.com/101Laboratory/stardis-research.git
```

```text
stardis-research/
├── stardis-oxs3d/    # CPU/GPU 实现、研究分支、文档、场景、实验与数据索引
├── stardis-editor/   # 场景编辑器及测试
└── thesis/           # 完整 LaTeX 源工程及必要插图
```

普通 clone 不会填充子模块；已克隆时执行 `git submodule update --init --recursive`。本仓库通过三个固定提交锁定互相配套的版本，不自动追踪各子项目最新分支。更新时先 pull 总仓库，再执行同一子模块命令。

## 阅读顺序

1. [发布方案与归属](PUBLICATION_PLAN.md)：项目边界、历史与数据策略。
2. [论文](thesis/README.md)：方法、实验和总结。
3. [CPU/OptiX 求解器](stardis-oxs3d/README.md)：构建入口、研究文档与数据。
4. [场景编辑器](stardis-editor/README.md)：交互式场景与任务工作流。
5. [发布状态](RELEASE_STATUS.md)：已经验证的内容与尚存限制。

大型原始数据位于 stardis-oxs3d 的版本化 Release，不随初次 clone 下载；其下载脚本按哈希验证并恢复原路径。论文只发布源工程，不发布整篇 PDF、编译中间文件或本地环境。

## 历史和版本

CPU Windows 迁移历史与全部 GPU 研究分支统一在 stardis-oxs3d 内；worktree 不另建项目。编辑器保留已有历史，论文使用干净源码发布历史，模板源码连同局部修改直接提供。各组件的原作者、上游许可和素材来源保持不变。

本标签是研究档案，不代表所有实验分支和历史结论都已重新验证。特别是 CPU 早期嵌套仓库的部分历史对象仍缺失，求解器当前机器构建也有环境限制，详见发布状态。
''')
    write(HUB/'PUBLICATION_PLAN.md',r'''
# 101Lab 毕业研究发布方案

## 结构

总入口 `stardis-research` 以三个 Git submodule 固定 `stardis-oxs3d`、`stardis-editor` 和 `stardis-thesis`，本地目录分别为 `stardis-oxs3d/`、`stardis-editor/`、`thesis/`。三个项目可独立维护，用户通过一次递归 clone 获取完整工作区。

`stardis-oxs3d` 使用现有 GPU 历史作为主线，导入 CPU Windows 移植父仓库历史及源码到 `baselines/stardis-cpu/`，保留实验分支、未提交研究快照和两个 stash 的归档分支。配套文档、场景、脚本、小数据、评审及答辩材料同库保存。

## 归属

原始 Linux Stardis 和热输运基座属于法国 Meso-Star 团队。研究者的工作依次是 Windows/CMake 迁移（stardis-cpu / stardis-win）、性能不佳后放弃的 cus3d 路线、OptiX s3d 重写与混合 Wavefront 求解器、编辑器。最终核心发布名为 stardis-oxs3d，stardis-cuda 仅为旧远端名称。

## 数据与文档

小型文本/CSV/图源进入 Git。大型 trace、剖析 session、计算输出与历史大图进入同仓库版本化 Release；原始路径、逐文件和分卷 SHA-256 进入 manifest，提供下载/校验/恢复脚本。全量原始记录供后续研究复查，构建对象和缓存不作为研究数据。

论文公开完整 LaTeX 源码、模板、参考文献和必要插图，排除整篇输出 PDF、XDV、环境与辅助文件。模板的本地修改一并保留。评审、答辩、研究笔记在核心库 thesis-materials 中与源码对应。

## 可追溯性

父仓库导入不能补回原嵌套仓库缺失对象。CPU 的 30 条旧引用涉及 28 个不同提交，其中 27 个提交对象未在当前父仓库找到；如后续取得原仓库或备份，再补历史。当前资料如缺种子/代码版本则保持 unknown，不用现行版本回填旧实验。

总仓库 tag 锁定三个子仓库提交；子仓库 manifest 锁定数据附件。后续变更须先发布子仓库提交与数据，再更新总仓库指针。发布保持原个人远端和本地工作区，以组织仓库作为实验室交接入口。

## 后续工作

在安装完整 MSVC/CUDA/OptiX 环境后运行 CPU/GPU 最小构建与场景验证；逐项补齐论文实验的代码、参数、种子和日志关系；追回旧嵌套对象；根据未来维护安排完善许可与贡献规范。
''')
    print('Wrote release documentation')

if __name__=='__main__':main()
