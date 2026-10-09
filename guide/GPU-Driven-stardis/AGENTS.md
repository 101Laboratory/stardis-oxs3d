# GPU-Driven Stardis — 全 GPU 求解器设计文档

**目标**: 将 stardis 蒙特卡洛辐射传输求解器完全迁移到 GPU，消除 CPU↔GPU 往返。  
**基线**: stardis-oxs3d-merge-phase（OptiX 光追后端 + CPU 波前调度）  
**范围**: 单设备、单池调度方案

## 文档索引

| 文档 | 内容 | 层级 |
|------|------|------|
| [00_OVERVIEW.md](00_OVERVIEW.md) | 总体方案概述、动机、约束、架构概览 | L0 概览 |
| [01_SCHEDULING.md](01_SCHEDULING.md) | 单池 GPU 调度器设计：替换 CPU merged_pass | L1 核心设计 |
| [02_STEP_MIGRATION.md](02_STEP_MIGRATION.md) | step 函数 GPU 迁移：现有基础设施分析与迁移方案 | L1 核心设计 |
| [03_LIBRARY_PORTING.md](03_LIBRARY_PORTING.md) | step 依赖的 solver/非 solver 库函数 GPU 适配方案 | L2 实施细节 |
| [04_SCENE_DATA.md](04_SCENE_DATA.md) | 场景数据 GPU 表达：布局、传输、访问模式 | L2 实施细节 |

## 交叉引用

- 现有波前设计 → [/guide/pwf_design/](../pwf_design/)
- OptiX 后端设计 → [/guide/oxs3d/](../oxs3d/)
- 性能优化路线 → [/guide/performance/](../performance/)
- 上层并行化分析 → [/guide/upper_parallelization/](../upper_parallelization/)
- debug 记录 → [/debug_issues/](../../debug_issues/)

---

*创建: 2026-03-18*
