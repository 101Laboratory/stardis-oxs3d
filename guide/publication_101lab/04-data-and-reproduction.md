# 实验数据与复现索引

返回 [总目录](README.md)。实验材料与 CPU/GPU 代码同属一个 STARDIS 主仓库；体积大的原始文件通过该仓库的版本化附件保存。下表是已定位的证据入口，尚未逐个重新运行实验。

## 论文到实现与数据的映射

| 研究内容 | 代码或方法 | 数据与脚本 | 论文位置及待补信息 |
|---|---|---|---|
| 物理一致性曲线 | `physical_consistency_stats/numerical_consistency_analysis.md`；`guide/stat_consistency_test/` | `physical_consistency_stats/cpu/`、`gpu/` 各 13 份 CSV；`plot_comparison*.py`；`img/` | 第三章 `tab:consistency-suite` 及 `Figures/Consistency/fig1...fig6`；报告含旧 cuBQL 描述，需确认每批数据对应的真实后端和 commit |
| 端到端红外结果对比 | CPU baseline 与 GPU Wavefront 的渲染输出 | `Stardis-Starter-Pack/porous/` 及其历史结果；`physical_consistency_stats/img/` | 第三章差异图、Z 诊断；图文件存在不等于原始 512×512×128 输入已完整绑定，需逐一匹配 |
| 线程数及端到端性能 | `stardis-cpu/`、`stardis-oxs3d-merge-phase/` | Starter Pack 的 `porous/`、`testporous/` 日志、`.ht` 与分析脚本 | 第三章 `tab:performance-comparison`；记录线程数、pool、dsphere/WoS 与采样量，不能混用不同配置 |
| pool 容量扫描 | 主仓库 Wavefront 状态调度 | `D:/thesis/OurWork/PoolSizeScan/pool_size_scan.md`；`testporous/IR_stardis-ox_320x320x32_32T_*_dsphere.txt` | 第三章 `tab:pool-scan`；同名参数日志需关联实现版本 |
| 单池和双池 | `opt/merge-phase` 及具体运行配置 | `OurWork/SingleDualPoolComparison/SingleDualPoolComparision.md`；`testporous/*_single.txt` 与配对双池日志 | 第三章 `tab:single-dual`；比较表记录 32T、pool 8192，复现时保留完整参数 |
| CPU/GPU 时间分解 | `scripts/parse_timeline.py` 与实验目录分析脚本 | `OurWork/TimeBreakdown/time_breakdown.md` 指向 `testporous/` 日志、`refill_cycle_stats.py`、`ta_tb_breakdown.py` | 第三章 `tab:time-breakdown`；区分 host、device 计时及重叠，不能直接相加成墙钟时间 |
| WF + Embree 消融 | `stardis-oxs3d-cpu-wf/` 的未提交修改和未跟踪 `star-3d/` | 论文第三章消融表、实验说明及对应运行日志待完整绑定 | `tab:wf-embree-ablation`；必须先固化源码，否则公开 Git 无法恢复该实现 |
| RT、CP、ENC 查询吞吐 | `optix-throughput-validation/`、`oxstar-3d/`；论文 `OurWork/backend/` | `perf_diag/optix_throughput_results.csv`、`s3d_embree_throughput_results.csv`、其他 kernel CSV | 第四章 `tab:rt_throughput`、`tab:rt_gpu_cpu`；基准项目 `rsys` 路径仍指向不存在的旧目录 |
| 全 GPU 路线可行性 | `GPU_WF_Validation/src/` 与 `guide/GPU_WF_Validation/`、`guide/GPU-Driven-stardis/` | `GPU_WF_Validation/results/kernel_{a,b,c,d}*.csv`、`projection.csv` | 后续研究/历史探索；不能与已实现的 CPU 调度混合架构混称 |
| PCIe 重叠诊断 | `cuda-duplex-validation/*.cu`、`scripts/parse_pcie_overlap.py` | CUDA 验证目录中的 timeline/trace CSV、8 个 `.nsys-rep`；对应 debug issue | 后续瓶颈研究；保留诊断与失败假说，不宣称已解决 |
| O9/O14/O15/O16 优化 | `optimization/wf_internal_opt/`、GPU 实验分支 | `profiling/stardis-oxs3d-aos/`、相关性能表和报告 | 负结果有交接价值；区分 O14 未合入主线与 O16 已在主线祖先中的事实 |
| GUI 工作流 | `D:/stardis-editor/src/`、`design/`、`tests/` | 场景工程 JSON、论文 `Figures/editor/` 截图 | 第五章；工作区功能修改尚未提交，配套版本需锁定 |

以上路径以各工作区根为基准；论文路径未写盘符时相对 `D:/thesis`。完整文件列表可在 [local-files.csv](evidence/local-files.csv) 搜索。

## 数据分层与容量

| 层次 | Git 中的内容 | 大型附件 |
|---|---|---|
| 理解结果 | 方法、参数、限制、图表解释 | 无 |
| 重画结果 | CSV、分析代码、小型参考图、`experiments.yaml` | 必要的较大原始计算输出 |
| 重跑实验 | 场景、几何、运行配置、构建/运行脚本、版本关系 | 大场景或大量 `.ht`，通过清单获取 |
| 复查过程 | Debug/优化报告、剖析摘要、查询示例 | VTune、Nsight、Visual Studio 原始采样及完整 trace |

体积事实：`profiling/` 13,225.22 MiB；`debug_issues/` 3,821.21 MiB，其中 `gpu_trace.csv` 3,806.5 MiB；Starter Pack 421.63 MiB。均是当前目录扫描值，未去重，包含其中的生成文件，不能直接用作最终附件大小承诺。

GitHub 普通 Git 单文件超过 100 MiB 会被拒绝，超过 50 MiB 会警告，见 [大文件说明](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)。Release 文档当前给出的单附件上限为小于 2 GiB；建议把本项目原始档案分为 **每包不超过 1 GiB**，给压缩差异和工具限制留余量，见 [Release 存储规则](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)。这里 1 GiB 是本方案选择，不是平台硬上限。

对 3.72 GiB CSV，应优先按记录边界分片并记录总行数、列名规则、分片顺序、每片及重组原文件 SHA-256；也可用可验证的多卷压缩包。VTune 采样通常由多个相关文件组成，按一次 session 整体打包，不能只留下最大的 `.tb7`。发布前做下载、解包和校验演练。

完整保存有研究价值的原始记录；可重建的编译对象、IDE 缓存和环境不作为研究数据发布。未知二进制不能仅凭后缀删除。大文件保存在主仓库 Release 时，目录中的 `data/README.md` 和 dataset manifest 必须提供稳定下载入口。

## 每个实验应补齐的最小记录

```yaml
experiment_id: porous-dual-pool-32t-8192
status: historical-needs-provenance-validation
code:
  cpu_original_commit: unknown
  gpu_original_commit: unknown
  working_tree_patch_sha256: unknown
inputs:
  scene_path: scenes/testporous/porous.txt
  scene_sha256: unknown
parameters:
  threads: 32
  pool_size_per_pool: 8192
  image: [320, 320]
  spp: 32
  conduction_mode: dsphere
  seed: unknown
hardware: unknown
command: unknown
outputs:
  stdout_result: unknown
  stderr_log: unknown
  dataset_version: unknown
analysis_script: unknown
thesis_reference: Chapters/3_ParallelizedSolver.tex, tab:single-dual
```

此例是待填写模板，不是已验证实验记录；场景最终发布路径也待映射。历史缺失项保持 `unknown` 并附依据，不补造种子、commit 或硬件。结果复核优先选择论文正文使用的实验，而非遍历重跑所有历史测试。

构建日志可使用 `> build.log 2>&1`。运行 stardis 必须拆开：`> result.ht 2> runtime.log`；时间数据从 stderr 获取。统计一致性需要标准误和样本数支持，不能以像素逐点 `1e-6` 误差替代蒙特卡洛 3σ 检验，也不能把旧报告的“全部通过”作为新版本的自动保证。
