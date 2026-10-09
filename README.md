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
