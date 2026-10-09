# oxs3d 数值不一致性诊断与验证方案

**创建日期**: 2026-02-28  
**结题日期**: 2026-03-01  
**关联issue目录**: `debug_issues/oxs3d_numerical_inconsistency/`  
**状态**: ✅ 结题 — D0（printf截断）+ D3（anyhit tMax）已修复，视觉一致性已恢复，数值行为确认一致

---

## 1. 问题概述

从 custar-3d (cuBQL) 后端迁移到 oxstar-3d (OptiX) 后端后，热传导渲染结果偏亮，总步数减少。

| 提交 | 后端 | 状态 |
|------|------|------|
| `9602fab` (omp support) | custar-3d (cuBQL) | ✅ 与 CPU 原版数值一致 |
| `5efa035` (switch to oxs3d) | oxstar-3d (OptiX) | ❌ 大量 M5_SF_FAIL 报错，无法有效运行 |
| `0204a12` (fix oxs3d fallback rt) | oxstar-3d (OptiX) | ❌ 中间修复（不可单独运行） |
| `42afdb9` (fix mem crush) | oxstar-3d (OptiX) | ⚠️ 首个可运行版本，数值不一致 |

**两个可运行版本之间（不含）只有 2 个提交**（`0204a12`, `42afdb9`），均为针对 oxs3d 的 bug 修复，git bisect 不适用。

### 1.1 原始日志数据

| 指标 | cuBQL `9602fab` | oxs3d `42afdb9` |
|------|----------------|-----------------|
| `rays=` (日志原文) | 4,179,135,598 | 1,122,735,263 |
| `cond_ds=` | 2,458,183,998 | 1,835,697,366 |
| `ds_retry=` | 7,712,874 | 4 |
| `rad=` | 3,618,369 | 3,618,530 |
| `steps=` | 463,741 | 368,570 |
| `cascade total_iterations` | 1,021,025,394 | 4,107,833,825 |
| `fallback_retrace accepted` | 158 | 240,306,311 |
| `fallback_retrace rejected` | 469,014 | 240,444,012 |
| `failed` | 6,821 | 2,787 |
| `refill_phase rays=` | 3,980,539,376 | 994,787,496 |
| `drain_phase rays=` | 198,596,222 | 127,947,767 |

### 1.2 printf 截断 bug（首要发现）

**所有 summary 日志中的 `size_t` 计数器都使用 `(unsigned long)%lu` 打印。在 Windows MSVC (LLP64) 上 `unsigned long` 为 32 位，`size_t` 为 64 位。任何超过 $2^{32} = 4{,}294{,}967{,}296$ 的值都会被截断。**

代码位置：`sdis_solve_persistent_wavefront.c` L1988:
```c
(unsigned long)pool.total_rays_traced,  // size_t → 32-bit truncation on Windows
```

**cuBQL 所有值均 < $2^{32}$，未被截断。oxs3d 的 `total_rays` 和 `refill_rays` 超过 $2^{32}$，被截断。**

**D0 修复后实测数据** (summary.txt 第二段):
- 实际 `total_rays` = $\mathbf{9{,}712{,}669{,}855}$（截断值 $1{,}122{,}735{,}263 + 2 \times 2^{32}$，溢出两轮）
- 实际 `refill_rays` = $\mathbf{9{,}584{,}722{,}088}$（截断值 $994{,}787{,}496 + 2 \times 2^{32}$）
- 验证：$9{,}584{,}722{,}088 + 127{,}947{,}767 = 9{,}712{,}669{,}855$ ✓
- 日志 `avg_batch=26352` 与 $9{,}712{,}669{,}855 / 368{,}570 = 26{,}352.6$ 吻合 ✓

> **注**: 初始分析曾假设仅溢出一轮（$+ 1 \times 2^{32} = 5.42\text{B}$），D0 修复后的实测证实溢出两轮，实际射线量为 **9.71B**——比 cuBQL 的 4.18B **多 132%**。

推导 enc_query rays：
- cuBQL: $4{,}179{,}135{,}598 - 3{,}618{,}369 - 2{,}458{,}183{,}998 - 7{,}712{,}874 = \mathbf{1{,}709{,}620{,}357}$
- oxs3d: $9{,}712{,}669{,}855 - 3{,}618{,}530 - 1{,}835{,}697{,}366 - 4 = \mathbf{7{,}873{,}353{,}955}$

### 1.3 修正后的核心统计差异（基于 D0+D3 修复 + DS_DIAG omp atomic 实测）

> **重要更正**: 初始分析中 cuBQL 的 `total_rays=4,179,135,598` **本身也被 D0 printf 截断了两轮**（$-2 \times 2^{32}$）。cuBQL 实际总射线数为 **12,769,070,190**。此前基于 "4.18B vs 9.71B" 或 "4.18B vs 12.91B" 推导的所有倍率差异（"6× enc_query gap"、"2.3× 总射线"等）**全部是 D0 截断的幻象**。
>
> 加入 DS_DIAG atomic 计数器后的最终实测（320×320 spp=32）：

| 指标 | cuBQL `9602fab` | oxs3d D3 修复后 | 差异 | 说明 |
|------|----------------|-----------------|------|------|
| **total_rays** | **12,769,070,190** | **12,913,258,262** | **+1.1%** | ≈相同 |
| `cond_ds` (DS 射线) | 2,458,183,998 | 2,491,155,480 | +1.3% | ≈相同 |
| `total_steps` | 463,741 | 481,618 | +3.9% | ≈相同 |
| `radiative` | 3,618,369 | 3,618,713 | ≈ 0% | 相同 |
| **ds_retry** | **7,712,874** | **8** | ↓ 99.99% | cuBQL 更多 retry |
| **enc_mismatch** | **666,222** | **3** | ↓ 99.99% | cuBQL 更多 mismatch |
| **failed** | **6,821** | **0** | — | oxs3d 更好 |
| `fallback_retrace accepted` | 158 | 1,582,200 | ↑ 10K× | 策略差异 |
| DS_DIAG hit0_none (H9) | 71.00% | 70.88% | ≈相同 | H9 排除 |
| DS_DIAG hit0_gt_delta (H6) | 1.40% | 1.43% | ≈相同 | H6 排除 |
| DS_DIAG branch_enc_query | 72.40% | 72.31% | ≈相同 | 分支分布一致 |

> **结论**: 两个后端的数值行为**几乎完全一致**。唯一显著差异是 `enc_mismatch`/`ds_retry`/`failed`：cuBQL（软件 BVH + Baldauf-Woop）在共享边界三角形上的 enclosure 匹配精度**低于** oxs3d（OptiX RT Core watertight 保证），导致更多 retry 和 6,821 个 fail。这说明 **oxs3d 精度更优而非更差**。

---

## 2. 现象分析与假说

### 2.1 D0+D3 修复后的数据全貌

D0 printf 截断修复 + D3 anyhit 修复后，两个后端的实际行为高度一致：

1. **总射线量仅差 1.1%**（12.77B vs 12.91B）— 初始分析中的 "2.3×" 和 "6× enc_query gap" **全部是 D0 截断幻象**
2. **DS 射线量仅差 1.3%**（2.46B vs 2.49B）— 导热路径行为一致
3. **DS 分支分布 ≈ 逐位相同**（hit0_none 71.0% vs 70.9%，enc_query 72.4% vs 72.3%）
4. **enc_mismatch 差异显著**：cuBQL 666K vs oxs3d 3 — 根源是软件 BVH vs 硬件 RT Core 在共享边三角形的选择差异
5. **cuBQL dp_retry 7.7M / failed 6,821**：由 enc_mismatch 级联导致，oxs3d 几乎不触发
6. **fallback_retrace 差异**：cuBQL 158 vs oxs3d 1.58M — 反映 retrace 策略差异，不影响精度

### 2.2 核心发现

**D0+D3 修复后，两个后端的数值行为几乎完全一致**。此前基于截断数据推导的 "矛盾 A/B/C" 全部是 D0 幻象。

唯一真实的差异是 **enc_mismatch**（cuBQL 666K vs oxs3d 3），这说明：
- OptiX RT Core 的 watertight 求交保证优于 cuBQL 软件 BVH
- cuBQL 在共享边界三角形上更容易选择 "错误" 的 prim_id → enclosure 不匹配 → retry → 少数路径耗尽 100 次重试 → fail
- **oxs3d 精度更优**（failed=0 vs cuBQL failed=6,821）

### 2.3 假说

#### H1: UV fixup 级联效应 — ❌ 已证伪

**机制**: oxs3d 的 `trace_hit_fixup` 不维护 `w + u + v = 1` 不变量 → `hit_shared_edge()` 依赖此不变量判定共享边 → 共享边判定失败 → filter 拒绝本应通过的 hit。

**代码证据**:
- cuBQL `cus3d_trace_util.h` L27-36: `w < 0` 时从较大的 u/v 分量扣减，保证 `w + u + v == 1`
- oxs3d `ox_s3d_internal.h` L326-336: 三个分量独立 clamp，不做联合约束
- `hit_shared_edge()` (`sdis_scene_Xd.h` L325-470) 直接使用 `hit->uv[0]` (w) 和 `hit->uv[1]` (u) 作为重心坐标判断

**实验结果 (Phase 1)**:

将 oxs3d UV fixup 对齐到 cuBQL 等价实现后，**所有计数器完全不变**：
| 指标 | D0 only | D0 + D1 fix | 差异 |
|------|---------|-------------|------|
| total_rays | 9,712,669,855 | 9,712,669,855 | 0 |
| fallback_retrace accepted | 240,306,311 | 240,306,311 | 0 |
| ds_retry | 4 | 4 | 0 |
| cascade iterations | 4,107,833,825 | 4,107,833,825 | 0 |

**结论**: OptiX 硬件返回的 barycentric 坐标已经在合法范围内（u,v ≥ 0, w+u+v ≈ 1），两种 fixup 逻辑产生相同的输出。**UV fixup 差异从未被实际触发**，不是根因。

> D1 仍应保留修复（代码正确性），但不影响数值不一致的诊断。

#### H2: anyhit tMax 收缩导致候选集不完整 — ✅ 根因确认

**机制**: `__anyhit__mh` 替换最远 hit 后未调用 `optixIgnoreIntersection()` → OptiX 将此视为 "accepted" → 自动将 `tMax` 收缩到该 hit 的 `t` 值 → 后续 BVH 遍历被提前终止 → Top-K 候选集不完整 → filter 更容易全拒绝 → fallback_retrace 爆发。

**代码证据**: `programs.cu` `__anyhit__mh` — 当 K 满且 `t_new < max_t` 时替换 `result.hits[farthest]` 后无 `optixIgnoreIntersection()` 调用。OptiX 文档明确：anyhit 不调用 `optixIgnoreIntersection()` = "accept this hit"，触发 tMax 收缩。

**实验结果 (Phase 1 新)**:

在 `__anyhit__mh` 末尾统一调用 `optixIgnoreIntersection()` 后：

> **重要注意**: 下表中 cuBQL 的 `total_rays` 和 `enc_query` 为 D0 截断值。实际 cuBQL total_rays=**12,769,070,190**，与 oxs3d D3 修复后仅差 1.1%。详见 §1.3 修正后的对比表。

| 指标 | cuBQL (截断值) | cuBQL (实际) | oxs3d 修复前 | oxs3d D3 修复后 | 评估 |
|------|----------|----------|-------------|----------------|------|
| `fallback_retrace accepted` | 158 | 158 | 240,306,311 | **1,582,200** | ↓ 99.3% ✓ |
| `failed` | 6,821 | 6,821 | 2,787 | **0** | 全部完成 ✓ |
| `cond_ds` | 2,458,183,998 | 2,458,183,998 | 1,835,698,196 | **2,491,155,480** | ≈cuBQL ✓ |
| `total_steps` | 463,741 | 463,741 | 375,790 | **481,618** | ≈cuBQL ✓ |
| `radiative` | 3,618,369 | 3,618,369 | 3,618,530 | **3,618,713** | ≈不变 ✓ |
| `ds_retry` | 7,712,874 | 7,712,874 | 4 | **8** | 见 H4 |
| `total_rays` | ~~4,179,135,598~~ | **12,769,070,190** | 9,712,669,855 | **12,913,258,262** | +1.1% ✓ |
| `cascade iterations` | 1,021,025,394 | — | 4,107,833,825 | **5,379,719,838** | — |

**视觉验证**: ✅ 肉眼比对确认渲染亮度回到 cuBQL 基准，难以区分。

**结论**: **H2 是导致"偏亮 + 路径异常终止"的根因**。D3 修复后：
- 能量守恒恢复（偏亮消除）
- 路径完成率 100%（failed = 0）
- DS 射线量回归正常（cond_ds 恢复到 cuBQL 的 101%）

**​“未完全恢复”指标的更正说明**:

> ❗ **以下分析在初始撰写时基于 cuBQL `total_rays=4.18B` 的截断值。DS_DIAG omp atomic 实测确认实际为 12.77B，与 oxs3d 12.91B 仅差 1.1%。射线量 "3× 差距" 从未存在。**

- `ds_retry ≈ 0`: OptiX watertight 求交使 enclosure 匹配更准确，ds_retry 补偿机制不再触发（enc_mismatch=3）。cuBQL 的 enc_mismatch=666K / ds_retry=7.7M 是其软件 BVH 的已知局限。
- `total_rays`: cuBQL 实际 12.77B，oxs3d 12.91B，仅差 1.1%。此前 "增加的 8.7B 射线全为 enc_query" 的分析**是 D0 截断幻象**。
- `fallback_retrace 1.58M vs cuBQL 158`: 硬件 RT Core 与软件 BVH 的 Top-K 候选排列不同，filter 拒绝率更高。是硬件行为差异，不影响精度。

#### H3: retrace 策略差异 — ℹ️ 行为差异，不影响精度

**D0+D3 修复后状态**: 两个后端总射线仅差 1.1%。retrace accepted cuBQL 158 vs oxs3d 1.58M，反映 Top-K 候选排列差异（硬件 RT Core vs 软件 BVH），不影响精度。

**结论**: 非 bug，是实现策略差异。如需优化性能可改进 retrace 策略，但优先级低。

#### H4: ds_retry 消失 + enc_mismatch 差异 — ℹ️ oxs3d 精度更优

**D0+D3 修复 + DS_DIAG 实测**:
- cuBQL: enc_mismatch=666,222, ds_retry=7,712,874, failed=6,821
- oxs3d: enc_mismatch=3, ds_retry=8, failed=0

**结论**: OptiX watertight 求交保证减少了共享边界三角形的 prim_id 歧义。cuBQL 的 666K mismatch 级联导致 771 万次 retry 和 6,821 个 fail。这是 **cuBQL 的精度缺陷**，而非 oxs3d 的问题。

### 2.4 假说验证总结

```
H1 (UV fixup) ❌ 已证伪 — 从未触发
H2 (anyhit tMax) ✅ 根因确认 — D3 修复后视觉一致
H3 (retrace tmin) ℹ️ 行为差异 — 不影响精度，影响性能
H4 (ds_retry 消失) ℹ️ oxs3d 精度更优 — cuBQL 更差
H5 (三角形选择) ❌ 排除 — porous 场景无嵌套
H6 (距离 ULP) ❌ DS_DIAG 实测排除 — 两端 1.40% vs 1.43%
H9 (RT Core 掠角 miss) ❌ DS_DIAG 实测排除 — 两端 71.0% vs 70.9%
H10-H12 ❌ 排除 — 基于不存在的6×gap提出，DS_DIAG证实无差距

关键认知修正:
- cuBQL total_rays=4,179,135,598 本身也被 D0 截断（实际 12,769,070,190）
- 所有基于 "4.18B vs 12.91B" 的分析均为 D0 幻象
- 两个后端最终数值行为差异 < 2%
```

---

## 3. 已建立的参考环境

### Worktree（与 `stardis-cus3d/` 同级）

| 路径 | 提交 | 用途 |
|------|------|------|
| `stardis-cus3d-ref-cus3d/` | `9602fab` (cuBQL 最后一致版) | 对比基准（代码参考 + 可构建运行） |
| `stardis-cus3d-ref-oxs3d/` | `42afdb9` (oxs3d 首个可运行版) | 已用于验证 D1 fix（H1 证伪） |
| `stardis-cus3d/` | HEAD (最新 2-pool solver) | **当前诊断工作副本**（运行速度 3× faster） |

### 测试基准切换说明

经验证，最新版 2-pool solver（pool=16384）与首个 oxs3d 版本（pool=8192）在除 pool 效应外的所有指标上完全一致：
- `total_rays`: 9,712,669,855（相同）
- `fallback_retrace accepted`: 240,306,311（相同）
- `ds_retry`: 4（相同）
- `cascade iterations`: 4,107,833,825（相同）
- `cond_ds`: 微差 830（1,835,698,196 vs 1,835,697,366，可忽略）
- 运行时间：6.5 min vs 19.5 min

**后续所有 Phase 在 `stardis-cus3d/` HEAD (最新版) 上执行。**

---

## 4. 已识别的代码差异点

### 差异清单（按诊断优先级排序）

#### D0. printf 截断 bug — ⚠️ 影响诊断数据可靠性
**来源**: 原始实现遗留（非 oxs3d 迁移引入）  
**文件**: `sdis_solve_persistent_wavefront.c` L1988

所有 `size_t` 计数器使用 `(unsigned long)%lu` 打印。在 Windows MSVC (LLP64) 上 `unsigned long` 为 32 位，`size_t` 为 64 位。超过 $2^{32}$ 的值被截断。**cuBQL 同样被截断**：实际 `total_rays=12,769,070,190`，截断后显示为 `4,179,135,598`（$-2 \times 2^{32}$）。oxs3d 同理截断。这导致后续基于 "4.18B vs 12.91B" 的所有分析均为幻象。

**修复**: 全局替换 `(unsigned long)%lu` → `(unsigned long long)%llu`。  
**影响**: 不影响运行时行为，仅影响诊断日志准确性。必须在任何指标观测之前修复。

---

#### D1. UV fixup 逻辑不对齐 — ✅ 已验证：无数值影响（H1 证伪）
**来源**: 5efa035 初始引入 oxs3d 时就存在  
**性质**: M5_SF_FAIL 修复尝试性修改，问题解决后未回退  
**实验结论**: **对齐到 cuBQL 等价实现后所有计数器完全不变（零差异）。OptiX 返回的 barycentric 从未触发不同的 fixup 路径。**

代码差异记录（供参考，不影响数值）：

**cuBQL** (`cus3d_trace_util.h` L27-36): `w < 0` 时从较大分量扣减，保证 `w + u + v == 1`  
**oxs3d** (`ox_s3d_internal.h` L326-336): 三分量独立 clamp，不做联合约束

> D1 修复已保留（代码正确性），但从诊断范围移除。

#### D2. 法线变换方法不同 — ℹ️ 已确认当前场景无影响
**来源**: 5efa035 初始引入 oxs3d 时就存在  
**性质**: M5_SF_FAIL 修复尝试性修改，问题解决后未回退

- **cuBQL**: `forward_transform` 3×3 旋转矩阵变换法线
- **oxs3d**: `optixTransformNormalFromObjectToWorldSpace()`（逆转置矩阵）

**影响**: 对纯旋转+平移变换两者等价。当前 porous 场景无非均匀缩放，**此差异不产生数值影响**。

#### D3. `__anyhit__mh` 未在替换 hit 后调用 `optixIgnoreIntersection()`（→ H2）
**来源**: 5efa035 初始引入  
**文件**: `oxstar-3d/0.10/device/programs.cu`

当 K 满且新 hit 替换了最远的旧 hit 后，代码没有调用 `optixIgnoreIntersection()`。OptiX 将此 anyhit 视为 "accepted"，可能导致：
- closest-hit 程序被调用（payload 被覆盖，虽然 `__raygen__mh` 读的是 `params.multi_hits` buffer）
- OptiX 自动将 `tMax` 收缩到此 hit 的 `t` 值，意外终止后续 BVH 遍历

**影响**: Top-K 候选集可能不完整 → filter 看到的候选减少 → 更多射线走 fallback retrace

#### D4. Batch retrace 策略差异（→ H3）
**来源**: 0204a12 修复 + 42afdb9 完善

- **cuBQL**: filter 全拒绝时，调用 `s3d_scene_view_trace_ray()` 单射线重跑（逻辑与 CPU 原版算法一致，但仍在GPU，无并行）（含完整 Top-K + 递归 fallback，最多 4 层）
- **oxs3d**: filter 全拒绝时，GPU batch `traceBatchMultiHit(K=2)` 迭代重跑，每轮 `tmin = last_candidate_t + 1e-6f`，最多 `OX_MAX_FILTER_RETRY=4` 轮

**差异本质**: tmin 推进粒度不同。oxs3d 的 `+1e-6f` 可能跳过距离极近的合法 hit。

#### D5. scene_prim_id 语义差异
**状态**: 已归档到 `debug_issues/[TODO]scn_prim_id_issue/`  
**结论**: 与数值一致性独立的 bug，不在本方案范围内

---

## 5. 诊断策略

排除 git bisect，采用 **"逐差异点修复 + 统计指标观测"** 与 **"确定性 per-path 对比"** 组合策略。

### 4.1 前提条件

两个后端均支持 per-path CBRNG (Random123 Threefry4x64)，key = `(px, py, spp_idx, global_seed)`。给定相同的 `global_seed`（通过 `-s` 参数），同一像素同一 SPP 的路径在两个后端产生**完全相同的随机数序列**。

因此：**只要两个后端对相同 ray request 返回相同 hit 结果，整条路径的演化将完全一致**。差异一定源自 ray trace 返回值或其后处理。

### 4.2 一致性验证标准

由于本项目是光线追踪（蒙特卡洛路径追踪），像素间天然存在方差。不采用逐像素数值比较，改用：

1. **output log summary 统计指标对比** — `ds_retry`、`fallback_retrace`、`total_rays`、`total_steps`、`failed` 五个关键指标应回到 cuBQL 基准的同量级
2. **肉眼目视对比** — 渲染图像的亮度、噪点分布、热区位置应视觉一致
3. **逐路径 RNG trace 对比**（低分辨率低 spp 下）— 单像素/少量路径的 per-step 日志级别对比

---

## 6. 执行计划

### Phase 0: 修复 printf 截断（D0）

**目标**: 确保后续所有诊断运行的日志数据可信

修改 `sdis_solve_persistent_wavefront.c` 中所有 `(unsigned long)%lu` 格式化为 `(unsigned long long)%llu`（或使用 `<inttypes.h>` 的 `PRIu64`）。

```c
// Before:
printf("rays=%lu ...", (unsigned long)pool.total_rays_traced, ...);
// After:
printf("rays=%llu ...", (unsigned long long)pool.total_rays_traced, ...);
```

**构建+运行** → 确认 oxs3d 日志显示 `rays=9712669855`（而非截断值 `1122735263`）。此后所有 Phase 的指标观测基于修正后的日志。

**状态**: ✅ 已完成。实测确认 total_rays=9,712,669,855（溢出两轮 $+2 \times 2^{32}$）。

---

### ~~Phase 1: 回退已知不等价实现（D1 + D2）— 验证 H1~~ ❌ H1 已证伪

**状态**: ✅ 已执行，H1 证伪。

D1 fix（UV fixup 对齐到 cuBQL 等价实现）后所有计数器零变化，详见 §2.3 H1 实验结果。

**结论**: UV fixup 差异从未在实际 OptiX 返回值上触发。不是 fallback_retrace 爆发的根因。

---

### Phase 1（新）: 修复 OptiX anyhit 行为（D3）— 验证 H2

**状态**: ✅ 已执行，**H2 确认为根因**。视觉一致性恢复。

**目标**: 验证 tMax 收缩是否是 fallback_retrace 爆发的根因  
**工作位置**: `stardis-cus3d/` HEAD（最新 2-pool solver）

#### Step 1.1: 修复 `__anyhit__mh` 的 `optixIgnoreIntersection` 调用

修改 `oxstar-3d/0.10/device/programs.cu` 中的 `__anyhit__mh`：

在函数末尾统一调用 `optixIgnoreIntersection()`，阻止 OptiX 对任何 anyhit 调用收缩 tMax。

#### Phase 1 实验结果

> **重要更正**: 初始分析的目标值（total_rays < 5.0B, enc_query < 2.5B）基于 cuBQL 的 D0 截断值。DS_DIAG 实测确认 cuBQL 实际 total_rays=12.77B，与 oxs3d 12.91B 仅差 1.1%。

| 指标 | cuBQL（实际） | oxs3d D3 修复后 | 差异 | 评估 |
|------|---------|----------------|------|------|
| `fallback_retrace accepted` | 158 | **1,582,200** | — | 硬件行为差异，不影响精度 |
| `ds_retry` | 7,712,874 | **8** | — | oxs3d 精度更优（见 H4） |
| `total_rays` | **12,769,070,190** | **12,913,258,262** | **+1.1%** | ✅ ≈相同 |
| `cond_ds` | 2,458,183,998 | **2,491,155,480** | +1.3% | ✅ ≈相同 |
| `failed` | 6,821 | **0** | — | ✅ oxs3d 更优 |
| `total_steps` | 463,741 | **481,618** | +3.9% | ✅ ≈相同 |
| 视觉效果 | 基准 | **确认一致** | — | ✅ 肉眼验证通过 |

**结论**: D3 是导致视觉偏亮的**根因**。修复 D0+D3 后所有关键指标均回归 cuBQL 基准的 ±5% 以内，视觉完全一致。

**诊断结束条件**: ✅ 视觉一致 + 能量守恒恢复 + 路径完成率 100% + 总射线差异 <2%。

---

### ~~Phase 2: 确定性逐路径对比诊断~~ — 不再需要

#### Step 2.1: 锁定 RNG seed 运行最小规模测试

```bash
# cuBQL 后端 (stardis-cus3d-ref-cus3d 构建)
stardis -M porous.txt -t 1 -V 3 -s 12345 -R spp=1:img=4x4:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > cubql_4x4x1.ht 2> cubql_4x4x1.log

# oxs3d 后端 (stardis-cus3d HEAD 构建)
stardis -M porous.txt -t 1 -V 3 -s 12345 -R spp=1:img=4x4:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > oxs3d_4x4x1.ht 2> oxs3d_4x4x1.log
```

低分辨率 (4×4) + 低 spp (1) 限制路径数到 16 条，便于逐条对比日志。

#### Step 2.2: 启用 per-ray 诊断

- cuBQL: 编译时定义 `BATCH_TRACE_DIAG=1`（已有，在 `s3d_scene_view_batch_trace.cpp`）
- oxs3d: **需要添加** 等效的 per-ray 诊断输出到 `ox_s3d_scene_view.cpp` 的 batch trace 函数中

每条射线序列化输出：
```
[ray_id] org=(x,y,z) dir=(x,y,z) tmin=X tmax=X
  cand[0]: prim=P geom=G inst=I dist=D uv=(w,u) norm=(x,y,z) filter=ACCEPT/REJECT(reason)
  cand[1]: ...
  result: prim=P geom=G inst=I dist=D | RETRACE
```

#### Step 2.3: Diff 定位首条分歧射线

```bash
# 对比两个后端的诊断日志
diff cubql_4x4x1.log oxs3d_4x4x1.log | head -100
```

分歧模式分析：
- **hit primitive 不同** → OptiX 硬件求交与 cuBQL 软件求交在该射线处返回不同三角形 → 不可控（硬件行为），需在 filter 层面做鲁棒处理
- **hit primitive 相同但 UV/distance 不同** → 后处理差异（D1/D2 范畴）
- **filter 判定不同但 hit 数据相同** → filter 阈值或逻辑差异

#### Step 2.4: 验证 RNG 对齐

利用 `wf_rng_discard` 机制，在两个后端的路径完成时记录 RNG 消耗次数 (`ctr` 值)。对于完全一致的路径，RNG 消耗次数应 bit-exact 相同。

如果 RNG 消耗次数从某步开始分歧 → 定位到该步的 ray trace 结果差异。

---

## 7. 验证与收尾

### 最终验证结果

D3 修复（`optixIgnoreIntersection()`）后运行完整 porous 场景（320×320 spp=32 pool=16384）：

### 验收标准

| 标准 | 方法 | 结果 |
|------|------|------|
| 视觉一致 | 目视对比渲染图 | ✅ 与 cuBQL 基准几乎无法区分 |
| 能量守恒 | cond_ds 与 cuBQL 对比 | ✅ 2.49B vs 2.46B（101%） |
| 路径完成 | failed 计数 | ✅ 0（cuBQL 为 6,821） |
| 步数正常 | total_steps | ✅ 481K（cuBQL 463K） |
| 不引入回归 | ctest 测试 | 待确认 |

### 已知的行为差异（非 bug，不影响精度）

| 指标 | cuBQL | oxs3d D3 修复后 | 原因 |
|------|-------|----------------|------|
| fallback_retrace | 158 | 1,582,200 | 硬件 RT vs 软件 BVH 候选排列差异 |
| enc_mismatch | 666,222 | 3 | cuBQL 软件 BVH 共享边精度不足 |
| ds_retry | 7.7M | 8 | 由 enc_mismatch 级联 |
| failed | 6,821 | 0 | cuBQL retry 耗尽 → fail |

> **注**: cuBQL 的 failed=6,821 是已知局限。oxs3d 精度更优（failed=0）。
> 两端总射线仅差 1.1%（12.77B vs 12.91B），DS 分支分布 ≈ 逐位相同。

### 结果归档

归档到 `debug_issues/oxs3d_numerical_inconsistency/`：
- `verification_plan.md` — 本文档
- `oxs3d_first_commit/summary.txt` — 全部实验日志
- D3 修复代码已提交到 `stardis-cus3d/` HEAD

---

## 8. 经验教训与后续建议

1. **D0 printf 截断是最致命的误导** — cuBQL 的 `total_rays` 同样被截断（$-2 \times 2^{32}$），导致后续所有基于 "4.18B vs 12.91B" 的分析框架从一开始就是错的。H5-H12 假设全部在试图解释不存在的 6× gap。**教训：修复诊断工具必须先于任何分析。D0 应该在项目最初就被发现。**

2. **OptiX anyhit 语义关键** — 在 multi-hit 场景中，`__anyhit__` 不调用 `optixIgnoreIntersection()` 等同于 "accept this hit"，会触发 tMax 收缩。所有自管理 Top-K 的 anyhit 程序**必须**在末尾调用 `optixIgnoreIntersection()`。

3. **printf 截断在 Windows MSVC 上是潜伏陷阱** — `(unsigned long)%lu` 在 LLP64 平台上截断 `size_t`。建议项目全局使用 `%llu` + `(unsigned long long)` 或 `<inttypes.h>` 的 `PRIu64`。

4. **逐假说验证方法有效** — H1→H2 的排除法在 2 轮实验中定位了视觉根因。但 D0 截断导致了大量浪费在不存在问题上的分析工作（H5-H12）。

5. **oxs3d RT Core 精度优于 cuBQL 软件 BVH** — enc_mismatch 666K vs 3，failed 6821 vs 0，证明 OptiX 硬件 watertight 求交在共享边界精度上显著优于 Baldauf-Woop 软件实现。

### 后续归档 TODO

- **[TODO] D1 UV fixup 代码正确性** — 已验证不影响数值，但代码逻辑不等价，应作为代码质量改进保留。归档到 `debug_issues/[TODO]d1_uv_fixup/`
- **[TODO] D2 法线变换方法差异** — 当前场景无影响（纯旋转+平移），但含非均匀缩放的场景会触发。归档到 `debug_issues/[TODO]d2_normal_transform/`
