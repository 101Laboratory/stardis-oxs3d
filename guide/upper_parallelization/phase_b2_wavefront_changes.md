# Phase B-2: Wavefront 实现变更摘要

生成时间: 2026-02-10

概述
- 目标: 在 `stardis-cus3d` 中实现 Phase B-2（wavefront 路径步进），将辐射轨迹调用批量化（Phase B-1 API），并以最小侵入方式集成到现有 `solve_tile` 流程。

新增文件
- `src/sdis_solve_wavefront.h` — wavefront 类型与 `solve_tile_wavefront` 声明。
- `src/sdis_solve_wavefront.c` — wavefront 调度器实现（初始化、步骤函数、收集/分发 ray 请求、主循环）。

修改文件
- `src/sdis_solve_camera.c` — 添加 `#include "sdis_solve_wavefront.h"`，使用环境变量 `STARDIS_WAVEFRONT=1` 切换到 `solve_tile_wavefront()`。
- `CMakeLists.txt` — 将 `src/sdis_solve_wavefront.c` 添加到 `SDIS_SOURCES`。

关键实现要点
- 批量追踪: 收集所有活跃路径的 ray 请求，使用 `s3d_scene_view_trace_rays_batch_ctx()` 一次性提交并获取命中结果，然后将结果分发回各路径并推进状态机。
- 路径状态外部化: 每条路径使用 `struct path_state` 保存状态（rwalk, ctx, 温度累加器, radiative scratch, 需要的 ray 请求等），便于 lockstep 前进。
- RNG 处理: 由于 `ssp_rng` 为不透明类型，改为保存非拥有指针 `ssp_rng *rng` 并将线程的 base RNG 共享给每条 path（`p->rng = base_rng`），避免嵌入不可拷贝的 RNG 结构。
- BRDF/类型修正: 修复 BRDF 抽样中 float/double 不一致的问题 — 使用 double 精度给 `brdf_sample`，并在需要时将方向转换成 float 存储到 path 状态。
- check_interface: 原始实现为静态（header 内），拷贝出可用的 `wf_check_interface` 以供 wavefront 模块使用，确保接口校验一致性。

设计/限制（当前里程碑 M1）
- 保守实现（M1 skeleton）: 目前仅把辐射 trace_ray 批量化；导热、对流、边界 reinjection 等仍调用原来的同步实现（委托到 `conductive_path_3d` / `convective_path_3d` / `boundary_path_3d`）。后续里程碑将把这些内部的 trace_ray 拆成显式 wavefront 步骤。
- 生命周期与所有权: wavefront 不追踪 heat_path（`register_paths` 参数被忽略），估计器缓冲仍由调用方写回。
- 随机序列: 因为路径并行推进，单像素内的 RNG 调用顺序会与串行略有不同，但蒙特卡洛期望值保持一致。

错误处理与安全
- 增加步长上限保护：`wf.total_steps` 超过阈值时报错并退出，避免死循环。
- 对 Picard 递归深度、接口一致性等异常进行日志与失败处理（标记 path 为 done 而非进程崩溃）。

构建与运行
- 已把实现加入 `CMakeLists.txt`，可以在构建后通过设置环境变量 `STARDIS_WAVEFRONT=1` 在运行时启用 wavefront 路径（否则使用原 `solve_tile()`）。

后续建议与待办
- 添加单元/集成测试: `test_sdis_wavefront.c`（统计等价性验证，最小场景运行以检测崩溃）。
- 将 conductive/delta-sphere/边界 reinject 拆成显式 wavefront 步骤以完成 Phase B-2 的后续里程碑。
- 考虑对每线程 RNG 的独立性增强（如果后续需要严格复现），可为每 path 创建独立 RNG 代理并保证种子/跳跃策略。

参考
- 本次实现基于项目已有的 Phase B-1 批量追踪 API 及 `solve_tile` 的像素遍历顺序（Morton + spp）。
- 相关文件位置（仓库相对路径）:
  - `src/sdis_solve_wavefront.c`
  - `src/sdis_solve_wavefront.h`
  - `src/sdis_solve_camera.c`
  - `CMakeLists.txt`

作者: 自动生成（由代码协助工具在实现 Phase B-2 后生成）
