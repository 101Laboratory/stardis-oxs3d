# [TODO] irregular_reinjection_retry_failure — 重注入重试失败日志

**状态**: 📋 待分析（仅有日志，无问题文档）  
**优先级**: 低 — 疑与 `enc_rot_matrix_mismatch` 或 `instanced_prim_id_scope` 相关  
**创建日期**: 未知（日志收集时间待查）

---

## 问题描述

目录目前仅含日志文件（`logs.txt`、`logs_sorted.txt`）与排序脚本（`sort_logs.py`），无分析文档。

日志中包含大量 `M5_SF_ENC_RETRY` 失败记录，为非规则（irregular）重注入重试失败的运行日志。

与 `enc_rot_matrix_mismatch` 和 `instanced_prim_id_scope` 两个活跃 issue 的文档中均有交叉引用，推测本问题可能是这两个 issue 的症状之一。

---

## 待分析事项

1. 确认日志的产生场景和配置（stardis 版本、场景名、命令行参数）
2. 分析 `logs_sorted.txt` 中的失败模式分布
3. 判断是否为独立问题，或可归入 `enc_rot_matrix_mismatch` / `instanced_prim_id_scope`
4. 确认 `[RESOLVED]M5_SF_FAIL` 修复后这类失败是否仍会出现

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `logs.txt` | 原始运行日志 |
| `logs_sorted.txt` | 按类别排序后的日志 |
| `sort_logs.py` | 日志排序脚本 |

## 相关 Issue

- `enc_rot_matrix_mismatch/` — 包壳旋转矩阵不匹配（交叉引用）
- `instanced_prim_id_scope/` — 实例化图元 ID 作用域问题（交叉引用）
- `[RESOLVED]M5_SF_FAIL/` — serial fallback 缺失 fill_filter_per_ray（已修复）
