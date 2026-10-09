# Persistent Wavefront Pool (PWF) 设计文档索引

**文件路径**: `d:/Stardis-GPU/guide/pwf_design/`  
**源代码**: `stardis-oxs3d-o16/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c`  
**代码行数**: ~5600 行  
**版本**: Phase B-3 M3 + O12/O13/O16 + Plan E · P0_OPT · P1 · P2  
**更新日期**: 2026-03-19

---

## 📚 文档导航

### 1. **[STRUCTURE.md](STRUCTURE.md)** - 总体架构与代码结构
**入门必读** | 13大代码模块 | 22+关键优化

内容:
- 项目发展历程 (M1→M2→M2.5→M3)
- 完整代码结构分解 (13个部分)
- 内存管理和缓冲区布局
- 动态合并/分割机制 (batch_idx sentinel化处理)
- 三种运行模式 (CAMERA/PROBE/PROBE_BATCH)
- 公开API接口
- 关键优化标记 (O1-O16, L4, Plan E, P0_OPT, P1, P2)

**何时阅读**: 需要理解整体架构、文件组织、模块划分

**关键概念**:
```
Pool View 缓冲区 (per-view):
├─ 索引数组 [capacity]: active, need_ray, done, bucket_*
├─ 光线缓冲 [max_rays=capacity×6]: ray_requests, ray_hits, etc.
├─ GPU Batch Context: pinned buffer管理
├─ Enclosure Locate: enc_locate_requests/results
└─ Closest Point: cp_requests/results
```

---

### 2. **[DATA_FLOW.md](DATA_FLOW.md)** - 数据流与执行流程
**深度理解** | 执行路径图 | 流水线设计

内容:
- 整体流程架构 (初始化→任务→执行→销毁)
- Phase详细执行流 (初始化、主循环、GPU交互、主运行循环)
- Merged Pass四相结构 (distribute→cascade→collect→harvest)
- GPU交互流程 (启动、下载、同步)
- 双缓冲重叠设计 (O11)
- 主运行循环选择 (dual vs single vs unified dispatcher)
- 关键数据流约束 (O11_SAFETY、Sentinel值、Bucket对齐)
- 路径生命周期流
- 性能监控点

**何时阅读**: 需要理解代码执行顺序、数据依赖、GPU↔CPU交互

**关键时序**:
```
merged_pass 四相:
├─ Phase A: 分发所有 GPU 结果 (enc + cp batch级 + rt per-path)
├─ Phase B: 级联处理非光线步骤 (OMP parallel)
├─ Phase C: 收集新光线/enc/cp请求 + refill
└─ Phase D: harvest 完成路径

双缓冲时序 (O13 async submit):
├─ ensure_done(view=0) → wait_d2h(A) → merged_pass(A) → signal_submit(A)
├─ ensure_done(view=1) → wait_d2h(B) → merged_pass(B) → signal_submit(B)
└─ submit线程并行执行 gpu_submit_all, coverage=129.8%
```

---

### 3. **[FUNCTIONS.md](FUNCTIONS.md)** - 函数参考手册
**快速查询** | 函数签名 | 参数说明

内容:
- 调试诊断函数 (3个)
- 内存管理函数 (2个)
- 动态合并/分割函数 (4个)
- 模式操作接口 (Wavefront Ops vtable)
  - Camera模式 (4个)
  - Probe模式 (4个)
  - Probe Batch模式 (4个)
- 光线请求处理函数 (2个)
- 批处理函数 (4个 for enc/cp)
- 级联处理函数 (1个 + 工具集)
- Merged Pass函数 (4个)
- GPU同步和启动函数 (2个)
- 主运行循环函数 (3个)
- 诊断报告函数 (2个)
- 公开API函数 (3个)
- 工具函数 (8个)
- 函数调用依赖图

**何时阅读**: 查询特定函数签名、参数、功能

**快速查询例**:
```
merged_pass(pool, pv, scn)
  → 行 2542-4527
  → 四相: distribute + cascade + collect + harvest

pool_collect_ray_requests(pool, pv)
  → 行 1391-2193
  → 将光线按类型分桶 (6种)
```

---

### 4. **[OPTIMIZATION.md](OPTIMIZATION.md)** - 性能优化详解
**优化深度** | O标记详解 | 调优指南

内容:
- 优化概览表 (8项优化)
- **O2**: Pre-bucketed光线索引 (避免排序)
- **O7**: Software Prefetch (L1预热) 
- **O8**: Full Memset 缓存预热
- **O11**: 双缓冲GPU↔CPU重叠
  - 实现原理
  - 性能收益 (30-50% 总体提升)
  - 动态merge/split 触发条件
- **O11_SAFETY**: Write-before-read同步
- **O12**: Stream Auto-Ordering (H2D→K→D2H 流水)
- **O13**: Async Submit Thread (submit线程隐藏)
- **L4**: GPU内联滤波器优化
- 诊断和分析工具
- Cascade Profiling (SDIS_CASCADE_PROFILE)
- 关键性能指标 (GPU利用率、波前宽度、时间覆盖率)
- 性能调优指南 (4步)
- 未来优化机会 (O12-O16)

**何时阅读**: 优化性能、诊断瓶颈、理解设计权衡

**关键指标**:
```
GPU利用率 = kernel_time / (kernel_time + post_time + retrace_time)
            目标 > 90%

波前宽度 = diag_total_active / total_steps
           目标 > 70% pool_size

时间覆盖率 = (sum of timed phases) / wall_time
             目标 > 90%
```

---

### 5. **[TIMING_GANTT.md](TIMING_GANTT.md)** - 时序甘特图与性能分析
**可视化** | CPU-GPU重叠 | 瓶颈识别

内容:
- Mermaid gantt 图展示 O13 异步 submit 流水线
- O13/O12/pre-O12 三代时序对比
- GPU执行时间 (3.4s kernel, 3802 Mrays/s)
- CPU Merged Pass 四相详解
  - Cascade: 78.5s (65.7% of wall, **主瓶颈**)
  - Compact+Refill: 8.9s
- Submit: 29.8s (完全隐藏)
- Coverage: 129.8%
- 瓶颈识别 + 优化机会

**何时阅读**: 理解实际运行时序、识别性能瓶颈、规划优化方向

**关键洞察**:
```
实测时序 (O13, 119.5s wall):
├─ Submit:       29.8s  [24.9%] ← 异步线程，完全隐藏
├─ Wait D2H:    27.2s  [22.7%]
├─ Merged Pass: 78.5s  [65.7%] ← 主瓶颈
├─ Compact:      8.9s  [ 7.4%]
├─ Housekeeping: 0.6s  [ 0.5%]
└─ Coverage: 129.8% (>100% = submit 被隐藏)
```

---

### 6. **[TIMELINE_USAGE.md](TIMELINE_USAGE.md)** - Timeline 日志增强功能
**工具指南** | 时间戳输出 | 解析脚本

内容:
- 新增 `[TIMELINE_TS]` 时间戳输出格式
- 11 个时间戳定义 (t0-t10)
- 启用 Timeline 日志（环境变量）
- 解析脚本使用方法 (`parse_timeline.py`)
- 统计分析功能（平均/标准差/吞吐量）
- Gantt 图生成（平均周期 + 绝对时间戳）
- 时间戳应用场景（精确重建时序、异步重叠分析）
- 性能影响分析（< 0.1% 开销）

**何时阅读**: 需要详细分析流水线时序、对比多个周期、生成性能报告

**快速上手**:
```powershell
# 1. 运行程序（自动启用 timeline）
.\stardis.exe -M input.txt > timeline.txt 2>&1

# 2. 解析并生成报告
python scripts/parse_timeline.py timeline.txt

# 3. 查看 timeline_analysis.md
```

---

### 7. **[FSM_STATES.md](FSM_STATES.md)** - 路径状态机完整设计文档
**核心参考** | 59状态全记录 | 转移图 | 源码映射

内容:
- FSM 总体结构概述（BND 中心调度枢纽）
- 状态分类标记说明 ([R]/[C]/[CP]/[ENC])
- **路径生命周期**: PATH_INIT → PATH_DONE → PATH_HARVESTED
- **RAD**: 辐射传播射线追踪 → 吸收/反射
- **Legacy入口** (6): COUPLED_BOUNDARY/CONDUCTIVE/CONVECTIVE/RADIATIVE
- **BND-SS** (M3): 固固界面4射线再射入
- **BND-SF** (M5): 固流界面Picard1 + null-collision
- **BND-SFN** (M8): PicardN + Picard递归栈 (MAX_PICARD_DEPTH=3)
- **BND-EXT** (M7): 外部净热流直接+漫反射+阴影链
- **CND-DS** (M4): Delta-sphere 对向射线游走
- **CND-WoS** (M9): Walk-on-Spheres CP+RT混合
- **CNV** (M6): 对流null-collision采样循环
- **ENC** (M1-v2/M10): 6射线腔体归属 + BVH最近基元
- 射线桶分类 (5种 ray_bucket_type)
- 4种异构查询类型汇总 (RT/CP/ENC/ENC-RT)
- 完整59状态枚举表 (含步进函数映射)
- 全局状态转移图
- path_state 内存布局概要

**何时阅读**: 理解路径状态机设计、查询具体状态转移、实现新步进函数

**关键数据**:
```
59 states (PATH_PHASE_COUNT = 60 含哨兵)
├─ [R]  射线等待: ~17 states
├─ [C]  纯计算:   ~36 states
├─ [CP] 最近点:    2 states (WoS)
├─ [ENC] 腔体:     1 state  (M10)
└─ [Future] 预留:  ~5 states
```

---

## 🗂️ 文档结构权衡

| 文档 | 用途 | 深度 | 交叉引用 |
|------|------|------|--------|
| STRUCTURE | 总览 | 广 | ← DATA_FLOW, FUNCTIONS, FSM_STATES |
| DATA_FLOW | 执行 | 深 | → STRUCTURE, FUNCTIONS |
| FUNCTIONS | 查询 | 精 | → STRUCTURE, OPTIMIZATION, FSM_STATES |
| OPTIMIZATION | 性能 | 深 | → STRUCTURE, DATA_FLOW |
| TIMING_GANTT | 可视化 | 深 | → OPTIMIZATION, DATA_FLOW |
| TIMELINE_USAGE | 工具 | 精 | → TIMING_GANTT, OPTIMIZATION |
| FSM_STATES | 状态机 | 深 | → STRUCTURE, FUNCTIONS, DATA_FLOW |

### 阅读路径

**场景A: 我是新人，想快速了解代码**
```
1. STRUCTURE.md (概览)
2. FSM_STATES.md §1-§2 (状态机总体结构与标记说明)
3. DATA_FLOW.md 中的 "整体流程架构" 和 "Phase详细执行流"
4. FUNCTIONS.md 中的函数调用依赖图
```

**场景B: 我要修复一个bug, 需要理解执行流**
```
1. FUNCTIONS.md (定位函数)
2. FSM_STATES.md (查找对应状态转移)
3. DATA_FLOW.md (理解上下文)
4. STRUCTURE.md (查看相关模块)
```

**场景C: 我要优化性能，遇到瓶颈**
```
1. TIMING_GANTT.md (理解实际时序和瓶颈)
2. OPTIMIZATION.md (理解已有优化)
3. OPTIMIZATION.md 中的 "性能调优指南"
4. DATA_FLOW.md (理解flow优化)
5. STRUCTURE.md (确认架构)
```

**场景D: 我要添加新feature**
```
1. STRUCTURE.md (整体设计)
2. FSM_STATES.md (确认状态影响范围)
3. FUNCTIONS.md (相关函数)
4. DATA_FLOW.md (数据依赖)
5. OPTIMIZATION.md (性能影响)
```

**场景E: 我要理解状态机或实现新步进函数**
```
1. FSM_STATES.md (完整状态机参考)
2. FSM_STATES.md 附录A (源文件映射)
3. FUNCTIONS.md (现有步进函数签名)
4. DATA_FLOW.md (Merged Pass如何调度)
```

---

## 🔍 关键概念速查

### Pool 和 View
```
Pool (整个系统)
├─ pool_size = 512 + 64 * num_sm
├─ num_active_views = 1 or 2 (动态)
└─ Views[] (1或2个buffer)
   ├─ base: slot起始位置
   ├─ view_size: 涵盖slot数
   ├─ capacity: 最大活跃数
   └─ 所有缓冲区
```

### Merged Pass 四相
```
Phase A (Distribute):  GPU结果 → path_state
Phase B (Cascade):     path_state 非光线步骤 (OMP parallel)
Phase C (Collect):     path_state → GPU请求 + refill
Phase D (Harvest):     收集完成路径
```

### Bucket 类型
```
RAY_BUCKET_RADIATIVE      (随机方向长程射线)
RAY_BUCKET_STEP_PAIR      (对向短程射线: DS步/SS&SF再射入)
RAY_BUCKET_SHADOW         (固定距离阴影射线)
RAY_BUCKET_STARTUP        (单方向探针射线)
RAY_BUCKET_ENCLOSURE      (6射线轴对齐腔体查询)
→ 详见 FSM_STATES.md §10
```

### 运行模式
```
CAMERA:       W × H × spp 像素任务 (Morton顺序)
PROBE:        nrealisations 线性任务 (单点)
PROBE_BATCH:  nprobes × nrealisations 交错任务 (多点)
```

---

## 📊 代码统计

```
总行数:             ~5600
注释率:             ~15%
函数数量:           ~45+
bucket类型:         6
pool view最多:      2
path_phase数:       59 (定义在sdis_wf_types.h, PATH_PHASE_COUNT=60含哨兵)
优化标记:           O1-O16 + L4 + Plan E + P0_OPT + P1 + P2
```

---

## 🔗 相关文件

| 文件 | 用途 |
|-----|------|
| `sdis_wf_types.h` | Path阶段定义、path_state结构 |
| `sdis_wf_rng.h` | CBRNG (Cryptographic Block RNG) |
| `sdis_solve_wavefront.c` | 非持久化wavefront参考实现 |
| `guide/upper-parallelization/phase_b3_persistent_wavefront.md` | 原始设计文档 |
| `perf_diag/` | 性能数据、Nsight报告、吞吐量统计 |

---

## 🚀 快速命令

### 构建 (非持久化参考)
```bash
cd stardis-oxs3d-o16 && mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DS3D_BACKEND=optix ..
cmake --build . --config Release
```

### 运行示例
```bash
cd Stardis-Starter-Pack/porous
<stardis-exe> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320 > IR.ht 2>&1
```

### 启用调试
```bash
export STARDIS_PIXEL_TRACE=trace.csv
export STARDIS_PATH_TRACE=5000
export STARDIS_CASCADE_PROFILE=1  # 需要CMake重新编译
./stardis ...
```

---

## 📝 维护说明

本文档由 PWF 代码梳理自动生成。

**更新策略**:
1. 代码重大重构 → 更新 STRUCTURE.md
2. 算法改动 → 更新 DATA_FLOW.md
3. 函数签名/参数变化 → 更新 FUNCTIONS.md
4. 性能优化 → 更新 OPTIMIZATION.md

**版本锁定**: 代码版本 0.16.2 (Phase B-3 M3 + O12/O13/O16 + Plan E · P0_OPT · P1 · P2)

---

## ❓ 常见问题

**Q: 为什么要用双缓冲？**  
A: 允许GPU追踪和CPU级联并行执行，提升30-50%性能。

**Q: Bucket有什么作用？**  
A: 避免光线重新排序，访问模式更cache-friendly。

**Q: 什么时候会trigger merge？**  
A: 当任一view活跃路径数 < 12.5% × view_size时。

**Q: Merge时如何处理batch_idx？**  
A: V1路径的batch_idx指向views[1]缓冲区，merge后需避免跨缓冲区读取。merge_to_single_pool()将V1所有待处理路径的batch_idx设为sentinel (uint32_t)-1，使其在下轮merged_pass中重新收集到views[0]缓冲区。

**Q: 如何监控性能？**  
A: 查看log_drain_phase_report()输出，或启用STARDIS_*环境变量。

**Q: 支持哪些优化？**  
A: O2, O7, O8, O11 (dual buffer), O11_MERGE (动态), L4 (GPU filter), O16 (NT store pinned writes), Plan E (pinned直接写入), P0_OPT/P1/P2 (SoA分层+Pool View).

---

## 📞 联系

设计参考: `guide/upper-parallelization/phase_b3_persistent_wavefront.md`  
问题记录: `debug_issues/AGENTS.md`  
性能数据: `perf_diag/`, `physical_consistency_stats/`
