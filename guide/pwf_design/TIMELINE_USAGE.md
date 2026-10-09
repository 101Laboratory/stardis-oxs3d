# PWF Timeline Enhanced Logging

**更新日期**: 2026-03-19  
**版本**: stardis-oxs3d-o16 (O16 NT Store + O13 async submit)

---

## 概述

在 camera-mode verbose 路径和 probe `pool_run_dual` 中，每个 A+B 双半周期记录 6 个时间戳，用于精确分析 CPU-GPU 流水线时序。

### 输出格式

```
[TIMELINE] step=2000 |waitA=0.12|cpuA=0.35|waitB=0.11|cpuB=0.28|submit=0.04(async)|cycle=0.91ms raysA=37914 raysB=38235
```

### 时间戳定义 (O13, 6 点)

| 时间戳 | 阶段 | 描述 |
|--------|------|------|
| `t_cy[0]` | 周期开始 | ensure_done 返回后 |
| `t_cy[1]` | 完成 wait_d2h(A) | A 数据下载完成 |
| `t_cy[2]` | 完成 CPU(A) | merged_pass + compact + refill A |
| `t_cy[3]` | 完成 wait_d2h(B) | B 数据下载完成 |
| `t_cy[4]` | 完成 CPU(B) | merged_pass + compact + refill B |
| `t_cy[5]` | 周期结束 | housekeeping 完成 |

**与旧 11 点方案对比**:
```
旧 (O11): syncKA → startDA → launchB → waitDA → cpuA → syncKB → startDB → launchA → waitDB → cpuB
新 (O13): waitA → cpuA → waitB → cpuB  (submit 在后台线程，不占时间戳)
```

---

## 使用方法

### 1. 启用 Timeline 日志

当前代码中硬编码为 `STARDIS_PIPELINE_LOG="2"`，无需手动设置。
输出频率：每 1000 步一行。

### 2. 运行程序

```powershell
cd Stardis-Starter-Pack/porous
.\stardis.exe -M porous.txt -t 32 -V 3 -R spp=32:img=320x320 2> timeline.txt
```

### 3. 解读数据

```
waitA  = 主线程等待 A 的 D2H 完成（含 GPU stall + PCIe 传输）
cpuA   = merged_pass + compact + refill on A
waitB  = 主线程等待 B 的 D2H 完成
cpuB   = merged_pass + compact + refill on B
submit = 最近一次 submit 线程的 wall-clock（参考值，异步）
cycle  = 整个 A+B 双半周期的墙钟
```

**关键指标**: `cycle ≈ waitA + cpuA + waitB + cpuB`（submit 被隐藏）

---

## 性能影响

### 额外开销

- **时间戳采集**: 6 次 `time_current()` 调用/周期
- **日志输出**: 1 行 `[TIMELINE]`（每 1000 步输出一次）
- **累积计算**: 6 次 `time_elapsed_sec()` 调用

**估算**: < 1 µs/周期（可忽略，< 0.1%）

---

## 已知限制

1. **时间戳精度**: 受系统时钟精度限制（Windows QueryPerformanceCounter ~100 ns）
2. **Submit 时间**: 报告的是 submit 线程的 wall-clock，不是主线程阻塞时间
3. **输出频率**: 固定每 1000 步

---

## 参考

- **设计文档**: `guide/pwf_design/TIMING_GANTT.md`
- **解析脚本**: `scripts/parse_timeline.py`
- **示例数据**: `Stardis-Starter-Pack/porous/10000_timeline_analysis.md`

---

*Last updated: 2026-03-13*
