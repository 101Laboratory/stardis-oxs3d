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
