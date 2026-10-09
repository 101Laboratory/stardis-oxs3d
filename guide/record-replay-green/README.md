# Record-Replay Green 函数审计

Green 函数 **录制-回放** (record-replay) 机制的全链路代码审计。
覆盖 CLI → solver 库 → MC 路径核心 → oxs3d 波前复刻对比。

| 文档 | 内容 |
|------|------|
| [01_CLI与solver入口](01_cli_solver_entry.md) | stardis CLI 参数、模式枚举、solver 库 API |
| [02_数据流与消费点](02_dataflow_consumption.md) | `green_path` 数据流、CPU 参考实现消费清单 |
| [03_oxs3d波前复刻对比](03_oxs3d_comparison.md) | oxs3d WF 步骤文件逐模块对比、缺口分析 |
| [04_测试计划](04_test_plan.md) | 验证计划（待填充） |

**前置阅读**: `guide/algorithm/green_func_usage.md`（Green 函数数据结构与概念）

**源码版本**: stardis-solver 0.16.2
