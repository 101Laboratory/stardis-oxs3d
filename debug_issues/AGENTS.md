# debug_issues 知识库

程序问题与解决方案记录目录。每个子目录对应一个独立的程序 issue，包含复现步骤、根因分析和修复方案。

## Debug 流程

收到问题报告后，首先在本文档下查询是否已有相关记录。若无，创建新的问题记录目录，在其中创建 `AGENTS.md` 文件，记录问题描述，并进行分析。
若问题不能直接定位，必须提出可行假说，并设计验证方案，鼓励通过收集数据的方法辅助验证。假说、验证方案、数据插桩都必须记录在 `AGENTS.md` 中，保留完整的分析链条。
若假说全部被排除，则必须提出新的假说，直到找到根因。

### ❗❗ Data-First 原则

**禁止在没有运行时数据的情况下对复杂问题进行多轮静态代码分析。**

教训来源：`merge_phase_M5_SF_FAIL_on_merge` 问题在多个会话中仅基于代码阅读推断“enc/cp 残留是根因”，设计了方案 F1/F4，全部指向错误方向。插桩收集一次运行时数据后，`enc=0, cp=0` 一行立即否定原假说，15 分钟内定位到真实根因（batch_idx 跨缓冲区失效）。

**强制流程**：
1. 提出假说后，**立即设计插桩/日志收集运行时数据**，而不是继续静态推理
2. 用数据验证/否定假说，而不是用更多代码阅读“确认”假说
3. 一轮静态分析未能定位后，禁止进行第二轮静态分析——必须先收集数据

找到根因后，须在目录名前插入 `[RESOLVED]` 标签，并在 `AGENTS.md` 中记录解决方案和预防措施。

对暂不解决的问题，须在目录名前插入 `[TODO]` 标签。
对已经不存在于当前版本中的问题，须在目录名前插入 `[OBSOLETE]` 标签。

## issue 子目录索引

### 活跃问题（ACTIVE）

| 目录 | 问题描述 | 严重度 |
|------|---------|--------|
| `[TODO]pcie_full_duplex_serialization/` | **GPU-side事件计时证明PCIe全双工未工作**：双流架构设计预期，但254次测量全为overlap=0（1.6µs串行化间隙），潜在46%性能提升未实现 | 高 |
| `merge_phase_test_framework_incompatibility/` | B4 测试因架构变更不再需要；WF 测试改动大但数据已收集，当前仅低优先级跟踪 | 低 |

### 待处理（TODO）

| 目录 | 问题描述 | 优先级 |
|------|---------|--------|
| `[TODO]instanced_prim_id_scope/` | 静态证据显示仍存在：instanced ENC 仍传递 BLAS-local prim_id（待运行时复核） | 高 |
| `[TODO]task_stat_inconsistency/` | 静态证据显示仍存在：CPU ENC 查询统计链路缺失，WF/GPU 计数仍不一致 | 低 |
| `[TODO]wf_c3_picard_systematic_bias/` | 历史上存在系统性正偏；受 merge-phase WF 测试框架不兼容阻塞，暂无法复测 | 中 |
| `[TODO]irregular_reinjection_retry_failure/` | 仅确认重试分支/日志点存在，缺少可复现实测数据（继续跟踪） | 低 |
| `[TODO]d1_uv_fixup/` | 静态证据显示仍存在：cuBQL/oxs3d UV fixup 逻辑不等价（当前场景未触发） | 低 |
| `[TODO]d2_normal_transform/` | 静态证据显示仍存在：非均匀缩放路径法线变换实现差异（当前场景未触发） | 低 |
| `[TODO]scn_prim_id_issue/` | 静态证据显示仍存在：oxs3d scene_prim_id 非全局唯一（multi-GAS 风险） | 低 |

### 已解决（RESOLVED）

| 目录 | 问题描述 | 解决日期 |
|------|---------|---------|
| `[RESOLVED]wavefront_dead_code_cleanup/` | 清理 ~20,000 行死代码（F1-F10）：per-tile wavefront solver + custar-3d 后端 | 2026-03-13 |
| `[RESOLVED]block_firefly_noise/` | harvest 重复累加已完成路径温度 → PATH_HARVESTED 修复 | 2026-02 |
| `[RESOLVED]i1_inconsistency/` | 测试参考值 2D/3D 列混淆，非 solver bug | 2026-03-03 |
| `[RESOLVED]M5_SF_FAIL/` | serial fallback 缺失 fill_filter_per_ray() | 2026-03-05 |
| `[RESOLVED]oxs3d_numerical_inconsistency/` | D0（printf size_t 截断）+ D3（anyhit tMax bug） | 2026-03-01 |
| `[RESOLVED]silent_exit1_merge_phase/` | merged_pass PhaseC TL_RAY_BUF_MAX 溢出 → mid-loop flush 修复 | 2026-03-11 |
| `[RESOLVED]merge_phase_M5_SF_FAIL_on_merge/` | 双池合并后 batch_idx 跨 per-view ray_hits 缓冲区失效 → sentinel 化修复 | 2026-03-13 |
| `[RESOLVED]d2h_enc_cp_distribute_misalignment/` | D2H 越权执行 enc/cp distribute → 移至 merged_pass Phase A 对齐 | 2026-03-13 |
| `[RESOLVED]merge_phase_numerical_inconsistency/` | merged_pass 架构数值不一致修复（见设计文档） | 2026-03-13 |
| `[RESOLVED]oxs3d_retrace/` | oxstar-3d retrace 循环深度限制修复 | 2026-03-13 |
| `[RESOLVED]rsys_proxy_allocator_debug/` | rsys Debug 模式 proxy allocator assert 修复 | 2026-03-13 |
| `[RESOLVED]merge_phase_enc_residual/` | merge pool 后 enc 查询垃圾值修复（根因见 merge_phase_M5_SF_FAIL_on_merge） | 2026-03-13 |
| `[RESOLVED]1-width_rt_retrace/` | solver 重构完成（已过时，已归档） | 2026-03-13 |
| `[RESOLVED]bandwidth_pressure/` | cuBQL 方案带宽瓶颈解决（oxstar-3d 替代方案落地） | 2026-03-13 |
| `[RESOLVED]enc_rot_matrix_mismatch/` | ENC_ROT_MATRIX 与 CPU f33_rotation 对齐，并补充一致性测试 | 2026-03-13 |
| `[RESOLVED]solver_loop_duplication/` | 统一 run_pool 执行层：camera/probe/probe_batch 入口仅初始化，求解循环下沉到统一封装 | 2026-03-15 |