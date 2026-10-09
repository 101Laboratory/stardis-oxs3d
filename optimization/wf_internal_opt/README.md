# Wavefront 内部优化分析

**创建日期**: 2026-02-19  
**状态**: 分析完成，待实施  
**前置工作**: Cascade OMP 并行化（已完成，1.89x 加速）

## 概述

本目录包含 Wavefront 持久化求解器（`sdis_solve_persistent_wavefront.c`）在 Cascade OMP 加速成功后，
针对 CPU 管理成本和 pool 架构的进一步优化分析。

## 文档索引

| 文件 | 内容 |
|------|------|
| [benchmark_analysis.md](benchmark_analysis.md) | Benchmark 数据的深度分析（pool_size 权衡、wavefront width 物理含义） |
| [cpu_phase_analysis.md](cpu_phase_analysis.md) | 主循环各 CPU 阶段的复杂度分析与优化方案（P0-P3 优先级） |
| [cpu_phase_optimization_plan.md](cpu_phase_optimization_plan.md) | O1-O9 完整优化计划（早期 stardis-cus3d 架构，部分已完成） |
| [O9_path_state_soa_comprehensive_report.md](O9_path_state_soa_comprehensive_report.md) | O9: path_state 热/冷 SoA 域分解综合报告（❌ 结题搁置，实测净收益仅 5.6%） |
| [O9_dev_plan.md](O9_dev_plan.md) | O9 详细开发计划（历史存档） |
| [O14_per_thread_partition_comprehensive_report.md](O14_per_thread_partition_comprehensive_report.md) | O14: per-thread 固定分区综合分析报告（❌ 结题关闭，v1+v2 均全面劣化） |
| [O14_dev_plan.md](O14_dev_plan.md) | O14 详细开发计划（❌ 结题关闭） |
| [O15_huge_pages_tlb_optimization.md](O15_huge_pages_tlb_optimization.md) | O15: Huge Pages TLB 优化（❌ 结题关闭，VTune 确认瓶颈非 TLB） |
| [O16_memory_access_bottleneck_analysis.md](O16_memory_access_bottleneck_analysis.md) | O16: 访存瓶颈定位与优化（🔄 进行中，VTune 高开销行汇总） |
