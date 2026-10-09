# Wavefront 求解器硬件利用率测试-实验3结果

**创建日期**: 2026-02-18  
**状态**: per-phase 插桩数据已收集  
**目的**: 确认 cascade（CPU 时间 56%）花在哪些具体 step 函数  
**CPU**: Intel i9-13900K (24 cores, P-core 5.8GHz)  
**测试场景**: Stardis-Starter-Pack porous

## 1. 测试方法

### 1.1 宏观 profile（已完成，320×320 spp=32）

三种 pool_size 配置下的完整求解运行：

```powershell
cd Stardis-Starter-Pack\porous

# pool=4096
$env:STARDIS_POOL_SIZE="4096"
..\..\stardis-cus3d\build\bin\Release\stardis.exe `
  -M porous.txt -t 4 -V 3 `
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "IR_rendering_320x320x32.ht"

# pool=10240, pool=32768 同上，修改 STARDIS_POOL_SIZE
```

### 1.2 per-phase 插桩（已完成，256×256 spp=4）

使用插桩版本运行更小的测试（缩短运行时间同时保持统计代表性）：

```powershell
cd Stardis-Starter-Pack\porous
$env:STARDIS_POOL_SIZE="4096"
..\..\stardis-cus3d\build\bin\Release\stardis.exe `
  -M porous.txt -t 4 -V 3 `
  -R spp=4:img=256x256:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 `
  > "IR_rendering_256x256x4.ht"
```

**缩小测试的合理性**: 256×256×4 = 262,144 条路径（vs 320×320×32 = 3,276,800），约 ×12.5 缩减。同一场景、同一 pool_size、702M 次 cascade 迭代——对 phase 百分比分布而言统计量完全充足。绝对耗时按比例缩小，但百分比分布具有高代表性。

## 2. 结果

### 2.1 数据文件

| pool_size | 文件 |
|-----------|------|
| 4096 | [4096profile.txt](../profile%20reports/4096profile.txt) |
| 10240 | [10240profile.txt](../profile%20reports/10240profile.txt) |
| 32768 | [32768profile.txt](../profile%20reports/32768profile.txt) |

### 2.2 总览

| pool_size | 总耗时 | steps | avg_width | 总射线 |
|----------:|-------:|------:|----------:|-------:|
| 4096 | **40m24s** | 912,610 | 3,326.1 | 3,383,092,008 |
| 10240 | **45m40s** | 494,152 | 6,142.8 | 3,383,092,008 |
| 32768 | **58m38s** | 302,419 | 10,037.2 | 3,383,092,008 |

### 2.3 各阶段绝对耗时

| pool_size | compact | collect | trace (GPU) | distribute | cascade | harvest+refill | 总耗时 |
|----------:|--------:|--------:|------------:|-----------:|--------:|---------------:|-------:|
| 4096 | 48.8s | 149.5s | **1187.5s** | 254.2s | **661.7s** | 58.5s | 2424.2s |
| 10240 | 60.4s | 178.5s | **1136.8s** | 319.4s | **886.5s** | 83.8s | 2740.1s |
| 32768 | 108.6s | 319.8s | **1059.5s** | 548.1s | **1170.8s** | 191.7s | 3518.2s |

### 2.4 Cascade 在 CPU 时间中的占比

$T_{cpu} = \text{compact} + \text{collect} + \text{distribute} + \text{cascade} + \text{harvest+refill}$

| pool_size | T_cpu | cascade | **cascade 占 CPU%** | 增量 vs pool=4096 |
|----------:|------:|--------:|-------------------:|-----------------:|
| 4096 | 1172.7s | **661.7s** | **56.4%** | baseline |
| 10240 | 1528.5s | **886.5s** | **58.0%** | +224.8s (+34%) |
| 32768 | 2339.0s | **1170.8s** | **50.1%** | +509.1s (+77%) |

### 2.5 Cascade 缩放行为

| 缩放指标 | pool 4096→10240 (×2.5) | pool 4096→32768 (×8) |
|---------|----------------------:|--------------------:|
| cascade 耗时缩放 | ×1.34 | **×1.77** |
| steps 缩放 | ×0.54 | ×0.33 |
| avg_width 缩放 | ×1.85 | ×3.02 |
| cascade 每步耗时 | 0.725ms | **3.873ms** |

pool=4096 时 cascade 每步 0.725ms，pool=32768 时膨胀到 3.873ms（**×5.3**），远超 pool_size 线性增长预期——证实了 **cache 效应导致的超线性恶化**。

### 2.6 射线组成与路径统计

所有 pool_size 下完全一致（确认总工作量恒定）：

| 指标 | 值 |
|------|-----|
| 总射线 | 3,383,092,008 |
| radiative 射线 | 3,338,953 (0.1%) |
| step_pair (导热) 射线 | **2,304,915,948 (68.1%)** |
| ds_retry 射线 | 7,776,752 (0.2%) |
| shadow / startup | 0 |
| 完成路径 | 3,276,800 |
| 失败路径 | 6,388 |
| 最大深度 | **467,682** |

### 2.7 Refill/Drain 阶段分析

| pool_size | refill 射线占比 | drain 射线占比 | drain steps | drain 耗时 | drain 占总% |
|----------:|---------------:|---------------:|------------:|-----------:|------------:|
| 4096 | 99.4% | 0.6% | 175,633 | 71.0s | **2.9%** |
| 10240 | 98.3% | 1.7% | 202,848 | 163.1s | **6.0%** |
| 32768 | 93.3% | **6.7%** | 216,023 | **527.7s** | **15.0%** |

### 2.8 Batch Size 范围

| pool_size | min batch | max batch | 理论 max (pool×6) |
|----------:|----------:|----------:|------------------:|
| 4096 | 1 | 18,162 | 24,576 |
| 10240 | 1 | 50,497 | 61,440 |
| 32768 | 1 | 150,759 | 196,608 |

### 2.9 Per-Phase Cascade 分解（256×256 spp=4 @ pool=4096）

```
total_iterations=702,508,784  total_advances=702,508,404
```

差值仅 380 → 99.99995% 的迭代均产生有效 advance（几乎无空转）。

**Top-10 热点（按累计耗时排序）**:

| 排名 | Phase | 名称 | Count | Time | Avg(μs) | 占 cascade% |
|:----:|------:|------|------:|-----:|--------:|----------:|
| 1 | **34** | `PATH_CND_DS_CHECK_TEMP` | 163,245,854 | **14.955s** | 0.092 | **20.2%** |
| 2 | **38** | `PATH_CND_DS_STEP_ADVANCE` | 163,166,340 | **14.114s** | 0.087 | **19.1%** |
| 3 | **3** | `PATH_COUPLED_BOUNDARY` | 56,042,603 | **8.439s** | 0.151 | **11.4%** |
| 4 | **16** | `PATH_BND_SF_PROB_DISPATCH` | 111,921,171 | **8.337s** | 0.074 | **11.3%** |
| 5 | **37** | `PATH_CND_DS_STEP_ENC_VERIFY` | 118,307,065 | **6.693s** | 0.057 | **9.0%** |
| 6 | **5** | `PATH_COUPLED_CONDUCTIVE` | 44,859,794 | 2.199s | 0.049 | 3.0% |
| 7 | **15** | `PATH_BND_SF_REINJECT_ENC` | 44,959,252 | 2.131s | 0.047 | 2.9% |
| 8 | 18 | `PATH_BND_SF_NULLCOLL_DECIDE` | 5,168 | 0.001s | 0.240 | 0.0% |
| 9 | 10 | `PATH_BND_POST_ROBIN_CHECK` | 967 | 0.000s | — | 0.0% |
| 10 | 51 | `PATH_ENC_LOCATE_RESULT` | 445 | 0.000s | — | 0.0% |

**按功能分组**:

| 功能组 | Phases | 合计耗时 | 占 cascade% |
|--------|--------|--------:|----------:|
| **Delta-sphere 导热循环** | 34 + 37 + 38 | **35.76s** | **48.3%** |
| **Solid/Fluid 边界** | 3 + 16 + 15 | **18.91s** | **25.6%** |
| **导热入口** | 5 | 2.20s | 3.0% |
| **其他** (18+10+51) | — | 0.001s | 0.0% |
| **Top-10 合计** | — | **56.87s** | **76.9%** |

**附加观察**:
- `enc_escalation: query_fb→m10=445, m10_degenerate_null=445` — ENC fallback 极少（445 次/702M 迭代），enclosure 查询本身不是瓶颈

---

## 3. 分析

### 3.1 核心结论：Delta-Sphere 导热循环占 Cascade 近半

实测数据确认了预期：**delta-sphere 导热循环**（phase 34→37→38）占 cascade 时间的 **48.3%**，是 cascade 内部的绝对主导热点。

```
Cascade 时间分布:

DS 导热循环 (34+37+38) ████████████████████████  48.3%
SF 边界     (3+16+15)  ████████████             25.6%
导热入口    (5)        ██                        3.0%
其余 Top-10            │                         0.0%
未统计 phases                                   23.1%
```

### 3.2 Delta-Sphere 循环的内部结构

Phase 34 和 38 的 count 几乎相同（163.2M vs 163.2M），确认它们构成紧密循环：

```
DS 循环: 34(CHECK_TEMP) → [需要ray? → 35(TRACE)[R] → 回来] → 37(ENC_VERIFY) → 38(ADVANCE) → 34
```

| 指标 | 值 | 含义 |
|------|-----|------|
| phase 34 count | 163.2M | DS 循环总迭代次数 |
| phase 38 count | 163.2M | 与 34 一致 — 每次进入必走完 |
| phase 37 count | **118.3M** | 仅 **72.4%** 的 DS 步进需要 ENC verify |
| 跳过 ENC verify | ~45M (27.6%) | 部分步进已有有效 enclosure 信息 |

Phase 37（ENC_VERIFY）的 count 低于 34/38，说明 ~28% 的 delta-sphere 步进跳过了 enclosure 验证（可能因位置仍在同一 enclosure 内），直接进入位置更新。

### 3.3 Solid/Fluid 边界：意外的第二热点

| Phase | 名称 | Count | 特征 |
|------:|------|------:|------|
| 3 | `COUPLED_BOUNDARY` | 56.0M | 边界入口 |
| 16 | `BND_SF_PROB_DISPATCH` | **111.9M** | 入口的 **2倍** |
| 15 | `BND_SF_REINJECT_ENC` | 45.0M | ≈入口的 0.8× |

Phase 16 的 count 是 phase 3 的 **2 倍**，说明 SF 边界的 picard 概率判定平均循环 ~2 次后才决定方向。这是一个意外发现——**边界概率循环的迭代次数显著**。

Phase 3（COUPLED_BOUNDARY）的 avg=0.151μs 是所有热点中**最高的**——边界判断涉及多路分派和材料表查找，比 DS 循环内的简单数值步进更重。

### 3.4 每次调用极快，瓶颈在调用次数

| Phase | Avg (μs) | 特征 |
|------:|------:|------|
| 34 (CHECK_TEMP) | 0.092 | ~92ns — 一次 cache line 读取 |
| 38 (ADVANCE) | 0.087 | ~87ns — 简单算术 |
| 37 (ENC_VERIFY) | 0.057 | ~57ns — 条件检查 |
| 3 (BOUNDARY) | **0.151** | ~151ns — 最重的单步 |
| 16 (SF_PROB) | 0.074 | ~74ns — 概率计算 |

所有 advance 的单次耗时在 50–150ns 范围——已经接近单次函数调用 + cache line 访问的硬件极限。**优化个体 advance 函数没有空间**。瓶颈完全在于 **702M 次调用的总量**。

### 3.5 Cascade 超线性缩放的机制验证

结合宏观 profile 数据的 cascade 每步耗时：

| pool_size | cascade/step | 相对 4096 | path_state 内存 | L3 适配？ |
|----------:|------------:|----------:|--------------:|:---------:|
| 4096 | 0.725ms | 1.00× | 9MB | ✅ |
| 10240 | 1.794ms | 2.47× | 22MB | ⚠️ |
| 32768 | 3.873ms | 5.34× | 72MB | ❌ |

per-phase 数据确认每次 advance 仅 50–150ns，cascade 的"每步"时间远大于此（0.725ms = 725μs），因为**每步包含遍历全部活跃 slot 的多次 advance**。pool 越大 → 活跃 slot 越多 → 每步的 advance 总次数更多 → cache 更挤 → 超线性恶化。

### 3.6 与实验 4 (GPU Kernel) 的交叉分析

| 瓶颈 | 位置 | 占比 | 性质 | 优化手段 |
|------|------|------|------|---------|
| **GPU kernel** | L2 Crossbar | ~78% SOL | bandwidth-bound | BVH 布局/射线排序 |
| **GPU 非 kernel** | 传输+同步 | ~50% of T_gpu | idle time | **流水线化** |
| **CPU cascade** | DS 循环 (34/37/38) | 48% of cascade, 27% of T_cpu | volume-bound | 减少迭代次数 |
| **CPU cascade** | SF 边界 (3/16/15) | 26% of cascade, 14% of T_cpu | iteration-bound | 减少 picard 迭代 |

两个瓶颈互补：GPU 难再提速（L2 crossbar 已饱和），CPU cascade 是流水线化后的下一瓶颈。

### 3.7 优化方向（按实际数据修订）

#### P0: pool=4096 + 流水线化（不变）

理论加速 2×（40min→20min）。per-phase 数据不影响此方案。

#### P1: 减少 DS 循环迭代次数（新增）

Phase 34+38 各 163M 次 → DS 循环是 cascade 的 **48%**。如果能减少 DS 步进次数：

| 手段 | 预期效果 |
|------|---------|
| **自适应步长**: 离边界远时增大 delta-sphere 半径 | 减少 30-50% 迭代 |
| **早退策略**: 设置最大步数后 fallback | 减少极端深度路径 |
| **温度外推**: 在导热梯度平缓区域跳跃而非步进 | 大幅减少迭代 |

这些改动涉及物理算法，需谨慎验证。

#### P2: SF 边界 picard 迭代优化（新增）

Phase 16 count = 2× phase 3 count → picard 循环平均 ~2 次。如能提升 picard 收敛速度或减少不必要的重入，可削减 26% cascade 时间。

#### P3: SoA 数据布局（保留但降级）

per-phase 数据显示每次 advance 仅 50-150ns（已接近硬件极限），SoA 化的收益主要体现在大 pool_size 下。pool=4096 时 9MB 已在 L3 内，SoA 收益有限。仅在需要增大 pool 时再考虑。

### 3.8 汇总

| 维度 | 发现 | 严重程度 | 可操作性 |
|------|------|---------|---------|
| **DS 循环主导** | 48.3% cascade，163M 次迭代 | 高 | 中（需改物理算法） |
| **SF 边界第二** | 25.6% cascade，picard ×2 迭代 | 中 | 中（算法调优） |
| **单步极快** | 50-150ns/advance | — | 无优化空间 |
| **调用量巨大** | 702M advances | 高 | 需从算法层面减少 |
| **前序预测验证** | 34/37/38 确认为热点 | ✅ | 预测准确 |
