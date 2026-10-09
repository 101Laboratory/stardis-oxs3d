# wf_c3_picard_systematic_bias — Wavefront Picard 系统性正偏

**状态**: 🔍 跟踪中 — 历史正偏已确认；当前基于既有数据持续跟踪，WF 测试链路不作为阻塞项  
**创建日期**: 2026-02-16  
**严重度**: 中 — 测试 `test_sdis_wf_c3_picard_multi` 6/6 组均正偏，4 组超 3σ

## 追踪状态更新（2026-03-13）

- 已与 `merge_phase_test_framework_incompatibility` 的最新决策同步：B4 不再需要测试，WF 测试改动大但已有数据。
- WF 测试链路当前不作为阻塞条件；本问题继续按 TODO 跟踪并以既有数据维护风险结论。
- 后续若恢复 WF 全量回归链路，再以同配置补充复测数据并更新最终结论。

---

## 问题描述

Wavefront Picard solver 结果相对 CPU depth-first 参考值存在系统性正偏：

| Config | 场景 | 偏差 (K) | σ 倍数 | 通过? |
|--------|------|----------|--------|-------|
| [0] | Picard1 const-Tref | +1.66 | 4.1σ | ❌ |
| [1] | Picard1 T4-ref | +1.43 | 3.7σ | ❌ |
| [2] | Picard2 const-Tref | +2.04 | 4.4σ | ❌ |
| [3] | Picard3 large-dT | +2.19 | 1.6σ | ✅（SE 较大） |
| [4] | Picard1+P const-Tref | +1.63 | 3.9σ | ❌ |
| [5] | Picard1+P T4-ref | +1.14 | 2.8σ | ✅（SE 较大） |

全部 6 组均正偏（+1.1~+2.2K，~0.5% 相对误差），[3][5] 仅因 SE 较大而"通过"，偏差量级与其他组一致。

**附加异常**：[2] Picard2 出现越界错误 `invalid sub-path temperature 560K (range [280,350])`，约 96/10000 realisations 失败。

---

## 排除项

- ❌ 测试参数错误 → 已逐项验证与 CPU 测试完全一致
- ❌ 参考值错误 → 来自 CPU depth-first solver 高精度计算
- ❌ 统计波动 → 10000 realisations，SE ≈ 0.4K，偏差 ≈ 4σ，< 0.01%

---

## 可能根因（待验证）

1. Picard 辐射权重累加方式 — wavefront 架构下 picard 子路径的辐射贡献可能存在累加偏差
2. sub-path 温度截断 — [2] 的 560K 越界暗示截断处理可能引入正偏
3. T⁴ 线性化展开误差 — wavefront 中 $T_{ref}$ 的选取时机可能与 depth-first 不完全等价
4. 辐射路径终止条件 — wavefront batching 中辐射路径的终止判断略有不同

---

## 建议后续步骤

1. 将 C3 标记为 known-fail，作为 Picard 路径修复的回归靶标
2. 优先排查 M8（picard radiation）step 中的权重计算逻辑，对比 depth-first 实现
3. 开启 `P0_ENABLE_DIAG=1` 对比同场景下 wavefront vs depth-first 的逐 realisation 统计

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `wf_c3_picard_systematic_bias.md` | 完整分析报告（测试输出、偏差特征、参数验证、根因假说） |
