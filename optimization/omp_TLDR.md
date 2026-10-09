# Wavefront Solver OMP 多线程加速可行性 — TL;DR

**日期**: 2026-02-19  
**结论**: 当前阶段不需要 OMP，流水线化优先

---

## 加速潜力

| 方案 | 效果 | 改造量 |
|------|------|--------|
| **仅流水线** | 40min→**20min** (2.0×) | ~500行 |
| **仅 OMP** | 40min→**23min** (1.7×) | ~330行 |
| **OMP + 流水线** | 40min→**20min** (2.0×) | ~830行 |

OMP 与流水线叠加后**几乎无额外收益** — pool=4096 时 CPU≈GPU 已平衡，流水线化后 $T \approx \max(T_{cpu}, T_{gpu}) = T_{gpu}$，CPU 降至 183s 不能突破 GPU 的 1187.5s 下限。

## 各阶段并行化难度

| 阶段 | 占 CPU% | 难度 | 关键问题 |
|------|--------:|------|---------|
| **cascade** | 56.4% | ✅ Trivial | profiling 计数器需 per-thread 数组 |
| **distribute** | 21.7% | ✅ Trivial | 仅 `paths_failed` 需归约 |
| **collect** | 12.7% | 🟡 Moderate | 重写为 3-pass radix scatter |
| **compact** | 4.2% | 🟡 Moderate | parallel prefix sum |
| **harvest+refill** | 5.0% | 🔴 Hard | estimator 同像素写竞争 |

**cascade + distribute** 合占 78%，仅需 ~70 行，即可获 **67% 的 CPU 加速收益**。

## 注意事项

- MSVC OpenMP 仅支持 2.0 — 无 `#pragma omp atomic` for `double`
- i9-13900K 有效并行度 ~8（P-cores）
- CPU 版 stardis 用任务级并行（per-tile），GPU 版需数据级并行（per-slot）
- `advance_one_step_no_ray` 和所有 `step_*` 函数已验证纯函数/线程安全

## 建议

先实施流水线 → 观察 `pipeline_stalls` → 若 CPU 频繁 stall 才引入 OMP。  
届时仅做 cascade + distribute（~70 行）即为最优 ROI。
