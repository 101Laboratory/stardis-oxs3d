# guide 知识库

技术指南与设计文档目录，按主题领域垂直组织。包含从迁移规划到算法分析的全项目知识积累。

## 子目录索引

| 目录 | 内容 |
|------|------|
| `publication_101lab/` | 毕业成果盘点与 101Lab 发布方案：代码/文档/数据配套、CPU 与 GPU 统一主仓库、论文源码范围、嵌套仓库历史完整性；入口 `publication_101lab/README.md` |
| `algorithm/` | 物理算法模块：Green函数用法与工作流、Picard迭代、表面处理、采样分析 |
| `architecture/` | 系统架构参考：CPU原始架构分析（TECHNICAL_ANALYSIS）、IR层架构、射线数据结构、GPU可行性边界 |
| `build_migration/` | 构建系统迁移记录：Makefile→CMake SOP、依赖图、接口审计（cross_project_audit）、可行性报告 |
| `cus3d/` | custar-3d架构设计、接口不兼容性审计、cuBQL迁移实施指南 |
| `dxrs3d/` | DirectX Raytracing方案设计（已废弃，OptiX替代） |
| `embree_migration/` | Embree→cuBQL/OptiX完整迁移文档：API映射表、可行性报告、后端抽象设计 |
| `GPU_WF_Validation/` | GPU波前实现的实验验证计划与结果 |
| `migration/` | 项目迁移阶段规划与里程碑（已完成历史记录）；`obsolete_dx12/` 存放废弃的DX12规划文档 |
| `oxs3d/` | oxstar-3d（OptiX后端）架构设计与接口映射 |
| `performance/` | 性能瓶颈分析、GPU优化路线图、算法级优化方案（含UE光追扩哈希方案综述） |
| `rng_trace/` | 随机数路径追溯：跨CPU/GPU平台RNG序列对比，可重复性验证 |
| `s3d/` | star-3d架构分析、线程安全性分析、API使用模式 |
| `senc3d/` | senc3d包壳库架构与接口分析 |
| `stat_consistency_test/` | GPU/CPU数值一致性验证方法论：3σ准则、5张主力曲线图方案、Python绘图脚本 |
| `upper_parallelization/` | 求解器波前并行化核心设计：调用链分析、状态机设计与转移映射、耦合路径GPU实现、随机游走并行化 |
| `GPU-Driven-stardis/` | 全GPU求解器设计：单池调度器、step函数GPU迁移、依赖库适配、场景数据GPU表达 |
