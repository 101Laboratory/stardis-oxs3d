# O9: path_state 热/冷 SoA 域分解 — 综合报告

**创建日期**: 2026-03-04  
**实测更新**: 2026-03-05  
**基于**: timing_coverage_baseline.md (计时基准) + aos_soa_gpu_migration_analysis.md (AoS↔SoA 迁移分析) + cpu_phase_optimization_plan.md (O1-O9 计划) + P1/P2 域分解 dev guide  
**场景**: porous 320×320 spp=32 pool=16384  
**基准墙钟**: 5 min 22s (315.7s wall)，450K steps，avg_width=14,503.8  
**状态**: ❌ **结题搁置** — 实测净收益仅 5.6%，pool_size scaling 劣化不可接受  

---

## TL;DR

O9 的本质是通过**域分解**将 2040B 的 `path_state` AoS 拆为 5 个功能域 SoA 数组，使得 CPU 各阶段只需触碰本阶段所需的字段子集。这一改造**直接消除或大幅降低**当前 315s 墙钟中以下耗时大户：

| 受益阶段 | 当前耗时 | 预期优化后 | 节省 | 原理 |
|---------|---------|-----------|------|------|
| sync_a + sync_b | 46.0s (14.6%) | **~0s** | **46.0s** | dispatch_soa 层彻底消除 |
| cascade | 40.2s (12.7%) | ~14-20s | **20-26s** | 工作集 2040B→480B，L2/L3 命中 |
| distribute | 32.9s (10.4%) | ~20-25s | **8-13s** | 步幅 2040B→228+480B |
| collect | 19.3s (6.1%) | ~6-10s | **9-13s** | 步幅 2040B→228B |
| harvest+refill | 23.5s (7.4%) | ~18-20s | **3-5s** | refill memset 2040→480B |
| compact | 18.1s (5.7%) | ~15-17s | **1-3s** | SoA index 读只需 20B→直接用 core |

**合计预期节省: 87-106s (28%-34%)**。墙钟从 315s 降至 ~210-230s。

> **⚠️ 实测结果 (2026-03-05)**: 净收益仅 -17.6s (5.6%)。sync 消除确实兑现 -46s，但多数组 SoA 布局导致 refill/compact/collect 严重回退 +30s，pool=32K 时 cascade 出现 2.87× 超线性劣化。详见第 10 章。

---

## 1. 全阶段耗时贡献序列

### 1.1 按绝对耗时排序（含耗时原理）

下表基于 timing_coverage_baseline.md 实测数据（99.8% coverage），每项附带**耗时根因**和**O9 可影响度**。

| # | 阶段 | 耗时(s) | 占比 | O9 影响度 | 耗时根因 |
|---|------|---------|------|----------|----------|
| 1 | **gpu_launch** | 89.8 | 28.4% | ❌ 无 | H2D memcpy + kernel launch；450K步×~0.2ms；CPU 端阻塞等待 PCIe 传输 |
| 2 | **cascade** | 40.2 | 12.7% | ✅ 高 | 串行读 2040B/slot AoS 做状态推进，cache 利用率 <25%；DS 路径每次仅碰 ~700B |
| 3 | **distribute** | 32.9 | 10.4% | ✅ 中 | 散射写 2040B stride AoS + `step_*()` 物理计算；每射线有效载荷 ~56B + 状态写回 |
| 4 | **trace** | 27.4 | 8.7% | ❌ 无 | gpu=5.0s + cpu_postprocess=21.4s(multi-hit filter)；不涉及 path_state |
| 5 | **sync_b** | 25.0 | 7.9% | ✅✅ 完全消除 | cascade 后全量 AoS→SoA 同步：遍历所有 active slots 写 5 个 SoA 字段 |
| 6 | **harvest+refill** | 23.5 | 7.4% | ✅ 低 | refill: memset 2040B/slot + init；harvest: 结果累加 |
| 7 | **sync_a** | 21.0 | 6.6% | ✅✅ 完全消除 | distribute 后 targeted AoS→SoA 同步：need_ray + enc + cp 槽位写 5 个 SoA 字段 |
| 8 | **collect** | 19.3 | 6.1% | ✅ 高 | 2-pass radix bucketing：每射线从 2040B stride 读 32B 有效载荷（1.6% 利用率）|
| 9 | **compact** | 18.1 | 5.7% | ✅ 低 | 流压缩 active_indices：读 dsoa 20B/slot（已是 SoA）|
| 10 | **enc_locate** | 10.3 | 3.3% | ❌ 无 | GPU batch 查询，不涉及 path_state |
| 11 | **cp** | 5.7 | 1.8% | ❌ 无 | GPU batch 查询，不涉及 path_state |
| 12 | **housekeeping** | 1.3 | 0.4% | ❌ 无 | 诊断 + 进度输出 |
| 13 | **gpu_sync** | 0.9 | 0.3% | ❌ 无 | kernel 完成等待 |

### 1.2 按 O9 可影响度分类

**可完全消除** (46.0s / 14.6%):
- `sync_a` (21.0s) + `sync_b` (25.0s): 域分解后 `dispatch_soa` 层不再需要——调度热字段（phase, active, needs_ray, ray_bucket, ray_count_ext）直接在 `path_core` 中原地读取，无需维护独立 SoA 镜像

**可大幅降低** (92.4s / 29.3%):
- `cascade` (40.2s): 工作集从 2040B→480B (`path_core`)，DS 最热路径仅 712B
- `collect` (19.3s): 读步幅从 2040B→228B (`path_ray_io`)，cache 利用率从 1.6% 提升至 14%
- `distribute` (32.9s): 写步幅从 2040B→228+480B，减少 cache line 污染

**轻度改善** (41.6s / 13.2%):
- `harvest+refill` (23.5s): memset 从 2040B→480B，init 触碰更少字段
- `compact` (18.1s): 已是 SoA 读取，改善有限

**不受影响** (135.4s / 42.9%):
- `gpu_launch` (89.8s): PCIe 传输瓶颈，与 path_state 布局无关
- `trace` (27.4s): GPU kernel + CPU filter，不涉及 path_state
- `enc_locate` + `cp` + `gpu_sync` + `housekeeping`: GPU 或辅助操作

---

## 2. 关键耗时大户原理深入

### 2.1 sync_a + sync_b = 46.0s — 为何如此昂贵？

**根因**: 当前架构维护双重状态表示——AoS (`path_state.phase/active/needs_ray/...`) + SoA (`dispatch_soa.phase[]/active[]/...`)。每次修改 AoS 后必须同步到 SoA：

```c
/* dispatch_soa_sync_from_path() 内联函数——每次写 5 个散落字段 */
soa->phase[idx]         = p->phase;          /* stride = pool_size × 4B */
soa->active[idx]        = p->active;         /* stride = pool_size × 4B */
soa->needs_ray[idx]     = p->needs_ray;      /* stride = pool_size × 4B */
soa->ray_bucket[idx]    = p->ray_bucket;     /* stride = pool_size × 4B */
soa->ray_count_ext[idx] = p->ray_count_ext;  /* stride = pool_size × 4B */
```

**量化**:
- sync_a: 每步同步 ~14,500 个 need_ray + enc + cp 槽位 → 450K 步 × 14.5K × 5 次写 = **~33 billion 次散射写入**
- sync_b: 每步同步 ~14,500 个 active slots → 同量级
- 每次写入跨 5 个独立 SoA 数组，cache line 利用率差（每个 4B 写占用 64B cache line）

**O9 消除机制**: 域分解后，`phase/active/needs_ray/ray_bucket/ray_count_ext` 这 5 个字段直接内嵌在 `struct path_core` 中。compact、collect 等代码直接从 `core_arr[i].phase` 读取，不需要独立的 `dispatch_soa` 层。sync_a + sync_b **完全消失**。

> **节省 46.0s (14.6% of wall) — 这是 O9 最大的单项收益。**

### 2.2 cascade = 40.2s — 2040B 步幅的代价

**根因**: `cascade_advance_single_path()` 对每个 active slot 执行状态机推进。每次 re-entry 必须加载 `path_state[slot_idx]`（2040B），但实际触碰的字段因路径类型而异：

| 路径类型 | 加载量 (当前) | 实际触碰量 | 浪费比 |
|---------|-------------|-----------|--------|
| CND_DS (最热) | 2040B | ~712B (core+cnd_ds) | 2.9× |
| CND_WOS | 2040B | ~816B (core+locals.wos) | 2.5× |
| CNV | 2040B | ~512B (core+locals.cnv) | 4.0× |
| BND_SS | 2040B | ~1168B (core+bnd+locals.ss) | 1.7× |
| BND_SF | 2040B | ~1896B (core+bnd+locals.sf+ext) | 1.1× |

**cascade 工作集分析** (pool_size=16384, avg_width=14,504):
- 当前: 14,504 × 2040B = **28.2 MB** — 远超 L2 (1.25MB)，逼近 L3 (36MB)
- O9 后: 14,504 × 480B = **6.6 MB** — L3 完全命中，L2 热部分可命中

**DS 路径深度分析**: DS 是导热 delta-sphere 循环，占 cascade 总迭代的 ~40-50%（2490M DS rays / 12.9B total）。每次 re-entry 仅 2 步后因 `needs_ray` 中断。O9 延迟加载使其只碰 `path_core`(480B) + `path_cnd_ds`(232B) = 712B，**工作集从 28.2MB 降至 ~10MB**。

> **预期节省 20-26s (cascade 50-65% ↓)**

### 2.3 collect = 19.3s — 1.6% 的 cache 利用率

**根因**: `collect` 从每个 `path_state[i]` 中提取 `ray_req` (64B 有效) + `filter_data_storage` 前 16B = 共 ~80B 有效载荷，但必须加载 2040B 的完整 AoS 条目。80/2040 = **3.9% cache line 利用率**（实际更低，因为目标字段散布在结构体不同偏移位置，可能横跨 2-3 个 cache line 外的位置）。

**O9 改善**: `ray_req` 和 `filter_data_storage` 移入 `path_ray_io`(228B)。collect 只需遍历 `ray_io_arr[]`，步幅从 2040B→228B，cache 利用率从 3.9% 提升至 **35%**（80/228）——**9× 改善**。

> **预期节省 9-13s (collect 50-65% ↓)**

### 2.4 distribute = 32.9s — 散射写 + 物理计算

**根因**: distribute 分 3 阶段处理射线结果，每条射线执行完整的物理步进函数（`step_radiative_trace`、`step_conductive_ds_process` 等），涉及读写 `path_state[i]` 的多个不连续字段。此外 distribute 还包含了 O7 prefetch 优化。

distribute 的时间构成估计：
- ~40%: `step_*()` 物理计算 (ALU + 分支) — 不受 O9 影响
- ~35%: path_state 散射读写 + cache miss — **O9 直接受益**
- ~25%: prefetch + 循环开销 + SoA sync — O9 消除 sync 部分

**O9 改善**: distribute 的散射写从跨 2040B AoS 变为跨 `ray_io_arr[i]`(228B) + `core_arr[i]`(480B)——两个连续数组，cache 行为大幅改善。

> **预期节省 8-13s (distribute 25-40% ↓)**

### 2.5 gpu_launch = 89.8s — O9 不能帮，但不影响相对收益

**根因**: 89.8s 是 H2D memcpy + kernel launch 的 CPU 阻塞等待时间。450K 步 × 2 Phase × 每次 ~100μs。这是 PCIe 带宽和 CUDA driver 开销，与 `path_state` 内存布局无关。

**优化方向**: pinned memory 直写（方案 E）、减小 `ray_request` 尺寸、合并批次。**这些是独立于 O9 的，属于 gpu_launch 专项优化（P0 优先级）**。

---

## 3. GPU 迁移路径评估（排除项）

AoS↔SoA 转换迁移到 GPU 的 4 种方案均已评估为**不可行或收益为负**：

| 方案 | 结论 | 原因 |
|------|------|------|
| A: path_state 上传 GPU → GPU extract | ❌ | CPU 指针无效；传输量从 96KB 暴增到 ~8MB |
| B: s3d_ray_request GPU 转换 | ❌ | 步骤②本身仅 2-5μs，kernel launch 开销更大 |
| C: HitResult GPU 转 s3d_hit | ❌ | 需 `resolve_shape()` 查 CPU unordered_map + CPU 指针 |
| D: GPU 直接 scatter 到 path_state | ❌ | `step_*()` 完整物理计算不可能 GPU 端执行 |

**核心结论**: 当前转换计算量微不足道（2-5μs），**真正的瓶颈是 CPU 端跨 2040B 步幅的稀疏内存访问**。O9 域分解才是对症下药。

---

## 4. O9 域分解方案概述

### 4.1 拆分方案

将 `path_state`(~2040B) 拆为 5 个功能域 SoA 数组 + 已有 3 个 P1 冷块数组：

| 域 | 结构体 | 大小/slot | 访问者 | 频率 |
|----|--------|-----------|--------|------|
| **A: `path_core`** | identity + lifecycle + rwalk + ctx + T + rng + diag + dispatch | 480B | **所有阶段** | 每步 |
| **B: `path_ray_io`** | ray_req + filter_data + rad_direction/bounce/retry | 228B | collect, distribute | 每步 |
| **C: `path_cnd_ds`** | ds_* 全部字段 | 232B | CND_DS 步骤 | DS 路径 |
| **D: `path_bnd`** | bnd_hit0/hit1 + bnd_reinject_distance + scratch | 128B | BND_* 步骤 | 边界路径 |
| **E: `path_locals`** | union { bnd_ss, bnd_sf, cnd_wos, cnv } | 928B | 域特定步骤 | 按路径类型 |
| (P1) `sfn_arr` | PicardN 递归栈 | 3700B | BND_SFN | 稀少 |
| (P1) `enc_arr` | 包壳查询 | 596B | ENC 步骤 | 中频 |
| (P1) `ext_arr` | 外部通量 | 360B | BND_EXT | 中频 |

### 4.2 `struct path_view` 延迟加载机制

```c
struct path_view {
    struct wavefront_pool *pool;
    size_t slot_idx;
    struct path_core    *core;       /* 立即加载 (每步必需) */
    struct path_ray_io  *ray_io;     /* 延迟加载 (NULL → pv_ray_io() 时解析) */
    struct path_cnd_ds  *cnd_ds;     /* 延迟加载 */
    struct path_bnd     *bnd;        /* 延迟加载 */
    struct path_locals  *locals;     /* 延迟加载 */
    struct path_sfn_data *sfn;       /* 延迟加载 */
    struct path_enc_data *enc;       /* 延迟加载 */
    struct path_ext_data *ext;       /* 延迟加载 */
};
```

cascade 入口只初始化 `core` 指针 (1 次解引用)，其余 7 个域按需通过 `pv_xxx()` 内联函数延迟解析。DS 热路径每次 re-entry 仅触碰 2 个域指针。

### 4.3 内存预算

pool_size=32768 时：

| 数组 | 单元大小 | 总量 |
|------|---------|------|
| `core_arr` | 480B | 15.0 MB |
| `ray_io_arr` | 228B | 7.1 MB |
| `cnd_ds_arr` | 232B | 7.2 MB |
| `bnd_arr` | 128B | 4.0 MB |
| `locals_arr` | 928B | 29.0 MB |
| `sfn_arr` | 3700B | 115.7 MB |
| `enc_arr` | 596B | 18.6 MB |
| `ext_arr` | 360B | 11.3 MB |
| **合计** | | **207.9 MB** |

vs 原始 AoS `slots[]` (6688B × 32768 = 209 MB)。**内存中性 (−0.5%)**。

---

## 5. 量化收益模型

### 5.1 各阶段收益推导

#### sync_a + sync_b: 46.0s → 0s

**机制**: `dispatch_soa` 层被废弃。5 个调度热字段 (phase, active, needs_ray, ray_bucket, ray_count_ext) 内嵌于 `path_core`：

```c
struct path_core {
    enum path_phase  phase;       // 直接读
    int              active;      // 直接读
    int              needs_ray;   // 直接读
    enum ray_bucket_type ray_bucket;
    int              ray_count_ext;
    // ... 其他核心字段 ...
};
```

compact/collect/distribute 直接从 `core_arr[i].phase` 读取，无需独立 SoA 镜像和同步。

**节省**: **46.0s (100%)**  
**置信度**: ★★★★★ (确定性消除)

#### cascade: 40.2s → 14-20s

**机制**: cascade 工作集压缩——

| 指标 | 当前 | O9 后 |
|------|------|-------|
| slot 步幅 | 2040B | 480B (core) |
| DS 路径加载量 | 2040B | 712B (core+cnd_ds) |
| cascade 工作集 (14.5K active) | 28.2 MB | 6.6 MB (core-only) |
| L3 命中率 | ~40-60% | ~90-95% |

辅助因素：
- `path_view` 延迟加载避免预取无关域
- DS 路径 2 步爆发长度意味着 core + cnd_ds 被反复重用，L1/L2 热度极高
- CNV null-collision 自循环（1步×N）只碰 core + locals(32B 分支前缀)

**节省**: **20-26s (50-65%)**  
**置信度**: ★★★★☆ (取决于 L3 命中率实际提升幅度)

#### collect: 19.3s → 6-10s

**机制**: 读步幅压缩——

| 指标 | 当前 | O9 后 |
|------|------|-------|
| 读步幅 | 2040B (path_state) | 228B (path_ray_io) |
| 有效载荷/cache line | ~80B / 2040B = 3.9% | ~80B / 228B = 35% |
| 改善倍数 | — | **9×** |

OMP 2-pass bucketed scatter 的 cache 行为直接受步幅影响。步幅缩小 9× 意味着相同 cache miss 数对应 9× 更多有效数据读取。

**节省**: **9-13s (50-65%)**  
**置信度**: ★★★★☆

#### distribute: 32.9s → 20-25s

**机制**: distribute 的内存访问改善——
- 射线结果写入从 `path_state[i]` (2040B stride) 变为 `ray_io_arr[i]` (228B stride) + 部分 `core_arr[i]` (480B stride)
- `step_*()` 物理计算中的字段访问通过 `path_view` 路由到对应域数组
- sync_a 内嵌于 distribute 的部分**被消除**（21.0s 中属于 distribute 触发的同步）

**注意**: distribute 的 ~40% 时间是纯 ALU 物理计算（不受 O9 影响），因此优化上限约 60%。

**节省**: **8-13s (25-40%)**  
**置信度**: ★★★☆☆ (物理计算占比不确定)

#### harvest+refill: 23.5s → 18-20s

**机制**:
- `refill_pool()` 的 `memset` 从 2040B→480B (path_core)。其他域按需初始化关键字段（不全量 memset）
- harvest 读 `core_arr[i].done_reason / T` 等结果字段
- `advance_path_to_first_ray()` 初始化路径仅碰 core + 首个 step 所需域

**节省**: **3-5s (13-21%)**  
**置信度**: ★★★☆☆

#### compact: 18.1s → 15-17s

**机制**: compact 当前已读 dispatch_soa (20B/slot) 构建索引列表。O9 后直接读 `core_arr[i].active` (480B stride，但字段在前 16B 内)。CPU prefetch 效果可能略差（步幅变大），但消除 SoA 层的间接访问。整体改善有限。

**节省**: **1-3s (5-17%)**  
**置信度**: ★★☆☆☆

### 5.2 收益汇总

| 阶段 | 当前(s) | 预期(s) | 节省(s) | 节省% | 置信度 |
|------|---------|---------|---------|-------|--------|
| sync_a + sync_b | 46.0 | 0 | **46.0** | 100% | ★★★★★ |
| cascade | 40.2 | 14-20 | **20-26** | 50-65% | ★★★★☆ |
| collect | 19.3 | 6-10 | **9-13** | 50-65% | ★★★★☆ |
| distribute | 32.9 | 20-25 | **8-13** | 25-40% | ★★★☆☆ |
| harvest+refill | 23.5 | 18-20 | **3-5** | 13-21% | ★★★☆☆ |
| compact | 18.1 | 15-17 | **1-3** | 5-17% | ★★☆☆☆ |
| **合计** | **180.0** | **73-92** | **87-106** | **48-59%** | |

**全局影响**: 墙钟从 315s → **~210-230s** (优化 27-33%)

> 注意: gpu_launch (89.8s) + trace (27.4s) + enc_locate+cp (16.0s) + housekeeping (1.3s) + gpu_sync (0.9s) = 135.4s 不受 O9 影响。这 135.4s 是 O9 之后的理论下界。

---

## 6. 迁移优先级与实施路径

### 6.1 五步实施计划

```
Step 1: 定义结构体 + path_view (1-2 天)
 ├── 定义 path_core/path_ray_io/path_cnd_ds/path_bnd/path_locals
 ├── 定义 struct path_view + pv_xxx() 延迟加载内联函数
 └── 编译验证 (预期大量编译错误——作为改动清单)

Step 2: Pool 分配重构 (1-2 天)
 ├── wavefront_pool: slots[] → 5 个域 SoA 数组
 ├── pool_create() / pool_destroy() 分配/释放逻辑
 ├── compact_active_paths(): swap/copy 所有域数组
 └── dispatch_soa 层标记 deprecated (但暂保留，逐步移除)

Step 3: 管线函数适配 (2-3 天)
 ├── cascade_advance_single_path(): 构建 path_view
 ├── collect_ray_requests_bucketed(): 读 ray_io_arr
 ├── distribute_ray_results(): 写 ray_io_arr + core_arr
 ├── harvest/refill: 读写 core_arr
 ├── SYNC POINT A/B → 删除 (dispatch_soa 不再需要)
 └── compact: 直接读 core_arr[i].active

Step 4: step 函数批量迁移 (5-8 天)
 ├── 修改 51 个 step_* 函数签名: path_state *p → path_view *pv
 ├── ~1600 处 p->xxx 替换为 pv->core->xxx / pv_xxx(pv)->yyy
 ├── 按文件顺序: enc(41处) → cnv(164) → bnd_ext(167) → bnd_ss(280)
 │                → cnd(250) → bnd_sf(400) → bnd_sfn(300) → core(300)
 └── 每文件编译 + CTest 回归

Step 5: 清理 + 验证 (2-3 天)
 ├── 删除 struct path_state (不再存在)
 ├── 删除 dispatch_soa 层及所有 sync 代码
 ├── sizeof 静态断言 (path_core ≤ 512B, etc.)
 ├── porous 320×320×32 逐像素对比 (1e-6 容差)
 └── 性能基准 (pool_size=8K/16K/32K 对比)
```

**总工期估计**: 11-18 天 (2-3 周)

### 6.2 按文件的改动量估计

| 文件 | 函数数 | p->替换 | 加载域 | 工期 |
|------|--------|---------|--------|------|
| sdis_wf_steps_enc.c | 5 | ~41 | core + enc | 0.5 天 |
| sdis_wf_steps_cnv.c | 3 | ~164 | core + locals(cnv) | 0.5 天 |
| sdis_wf_steps_bnd_ext.c | 5 | ~167 | core + ext + ray_io | 1 天 |
| sdis_wf_steps_bnd_ss.c | 4 | ~280 | core + bnd + locals | 1 天 |
| sdis_wf_steps_cnd.c | 7 | ~250 | core + cnd_ds / locals(wos) | 1 天 |
| sdis_wf_steps_bnd_sf.c | 5 | ~400 | core + bnd + locals + ext | 1.5 天 |
| sdis_wf_steps_bnd_sfn.c | 5 | ~300 | core + locals + sfn | 1 天 |
| sdis_wf_steps_core.c | 6+分发表 | ~300 | core + 按 phase 选择 | 1 天 |
| sdis_solve_persistent_wavefront.c | ~15 | ~200+ | 全部 | 2 天 |
| sdis_wf_state.h + wavefront.h | — | 结构体定义 | — | 1 天 |
| sdis_wf_soa.h | — | 废弃文件 | — | 含在 Step 5 |

### 6.3 风险矩阵

| # | 风险 | 影响 | 缓解措施 |
|---|------|------|---------|
| 1 | **1600+ 处手工替换引入 typo** | 高: 运行时 segfault/数值错误 | 逐文件编译+CTest，每文件一个 commit |
| 2 | **指针穿透 `&p->rwalk` 语义变化** | 中: `&pv->core->rwalk` 等效，但需确认全部 30+ 处 | grep `&p->rwalk` 全量检查 |
| 3 | **compact swap 多数组一致性** | 中: 8 个数组必须同步 swap | compact 函数单元测试 |
| 4 | **path_view memset 开销** | 低: 64B memset + 1 指针初始化 <1ns vs cascade 步进数百 ns | 基准比较 |
| 5 | **union locals 对齐/偏移变化** | 低: 独立数组，对齐由编译器保证 | `static_assert(offsetof(...))` |
| 6 | **调试困难: 2 周内代码不可运行** | 中: step 函数逐文件迁移期间无法全量编译 | 保留 `path_state` 兼容层渐进过渡 |

---

## 7. O9 与其他优化的关系

### 7.1 O9 vs gpu_launch (P0) — 独立正交

| 维度 | O9 | gpu_launch 优化 |
|------|-----|----------------|
| 靶点 | 纯 CPU 阶段内存布局 | PCIe 传输 + CUDA driver |
| 耗时占比 | 57.1% (可影响的 180s) | 28.4% (不受 O9 影响的 89.8s) |
| 优化手段 | path_state SoA 拆分 | pinned memory 直写 / 批次合并 |
| 依赖关系 | 无依赖 | 无依赖 |

**结论**: 两者完全独立，可并行开发。gpu_launch 优化（方案 E: 消除 s3d_ray_request 中间层）可在 O9 之前或之后实施。

### 7.2 O9 vs 方案 E/F/G — 协同增强

O9 完成后，以下方案的实施成本和收益都会改善：

| 方案 | O9 前收益 | O9 后收益 | 原因 |
|------|----------|----------|------|
| E: 消除 s3d_ray_request | +5μs/step | +5μs/step + 接口更干净 | `ray_io_arr` 已独立，直写 pinned Ray 更自然 |
| F: 消除 s3d_hit 中间层 | +20-30μs/step | +20-30μs/step | distribute 接口重构已在 O9 中完成 |
| G: GPU 预解析 shape_id | +5-10μs/step | +5-10μs/step | 独立于 O9 |

### 7.3 O9 对 pool_size 甜蜜点的影响

O9 后各阶段工作集大幅缩小，pool_size 可进一步上推：

| pool_size | cascade 工作集 (core_arr) | 当前 L3 命中 | O9 后 L3 命中 |
|-----------|--------------------------|-------------|--------------|
| 8192 | 3.7 MB | ✅ | ✅✅ (L2 边缘) |
| 16384 | 7.5 MB | ✅ | ✅ |
| 32768 | 15.0 MB | ⚠️ | ✅ |
| 65536 | 30.0 MB | ❌ | ✅ (L3 边缘) |

> O9 使 pool_size=32768 时 cascade 工作集仍在 L3 舒适区内，拓宽了 GPU 利用率的调参空间。

---

## 8. 推荐实施顺序（综合所有优化）

```
阶段 1: O9 域分解 (2-3 周)                     [预期 -87~-106s / -28~-34%]
  └→ 最高 ROI 单项优化，消除 sync 层 + 压缩工作集
  
阶段 2: gpu_launch 优化 — 方案 E (3-5 天)       [预期 -5~-15s]
  └→ 消除 s3d_ray_request 中间层，collect 直写 pinned Ray[]
  └→ 与 O9 后的 ray_io_arr 独立布局天然协同

阶段 3: gpu_launch 优化 — 批次合并 (1-2 周)     [预期 -20~-40s]
  └→ 减少 450K 次 kernel launch 至更大批次
  └→ 依赖 pool_size 上推至 32K+ (O9 解锁)

阶段 4: 方案 G + F — GPU 预解析 + 消除 s3d_hit (1-2 周) [预期 -10~-20s]
  └→ device-side geom→shape lookup, distribute 直读 HitResult

累计预期: 315s → ~155-185s (41-51% 提升)
```

---

## 9. 结论（预测阶段，2026-03-04）

| 决策项 | 结论 |
|--------|------|
| O9 是否值得投入 2-3 周？ | **是**。87-106s 节省 (28-34%) 是当前架构下最高 ROI 的单项优化 |
| GPU 迁移 AoS↔SoA 转换？ | **否**。CPU 端转换计算量微不足道 (2-5μs)，瓶颈在 2040B 步幅 cache miss |
| sync_a + sync_b 是否需要单独优化？ | **否**。O9 的 dispatch_soa 消除将其**彻底**清零 |
| gpu_launch 应在 O9 之前还是之后？ | **之后**。O9 的接口重构为方案 E/F 扫清障碍 |
| O9 后是否还有显著优化空间？ | **是**。gpu_launch (89.8s) 仍是最大单项，但需要不同技术路线 (PCIe/driver 优化) |

---

## 10. 实测结果与分析（2026-03-05）

### 10.1 实测环境

- **代码**: stardis-cus3d-o9 worktree，完整域分解实现
- **场景**: porous 320×320 spp=32
- **OMP**: 32 线程
- **测试 pool_size**: 16384, 32768

### 10.2 pool=16384 实测数据

```
persistent_wavefront DONE: 320x320 spp=32 pool=16384
  elapsed=4 mins 58 secs
  steps=481836  rays=12,912,150,630  avg_width=13,550.1
  paths: completed=3,276,800  failed=11  max_depth=278,340

timing: compact=21.833s  collect=24.181s  trace=33.860s(gpu=8.349s cpu=22.275s)
        distribute=31.761s  enc_locate=9.675s  cp=2.600s
        sync_a=0.000s  cascade=37.086s  sync_b=0.000s
        harvest+refill=39.218s  housekeeping=1.346s
        gpu_sync=1.069s  gpu_launch=95.017s
        total_timed=297.647s  wall=298.105s  coverage=99.8%
```

### 10.3 pool=32768 实测数据

```
persistent_wavefront DONE: 320x320 spp=32 pool=32768
  elapsed=9 mins 16 secs
  steps=290719  rays=12,912,150,630  avg_width=22,397.2

timing: compact=29.012s  collect=59.141s  trace=59.696s(gpu=14.890s cpu=39.451s)
        distribute=108.756s  enc_locate=10.032s  cp=3.485s
        sync_a=0.000s  cascade=106.579s  sync_b=0.000s
        harvest+refill=50.393s  housekeeping=1.180s
        gpu_sync=1.485s  gpu_launch=126.680s
        total_timed=556.439s  wall=556.781s  coverage=99.9%
```

### 10.4 逐阶段预测 vs 实测对比（pool=16384）

| 阶段 | 基准(s) | 预测(s) | 实测(s) | 预测偏差 | 判定 |
|------|---------|---------|---------|---------|------|
| **sync_a + sync_b** | 46.0 | 0 | **0.0** | 完美 | ✅ 兑现 |
| **cascade** | 40.2 | 14-20 | **37.1** | +85~+165% | ❌ 严重不足 |
| **distribute** | 32.9 | 20-25 | **31.8** | +27~+59% | ❌ 不足 |
| **collect** | 19.3 | 6-10 | **24.2** | +142~+303% | ❌❌ 逆向回退 |
| **compact** | 18.1 | 15-17 | **21.8** | +28~+45% | ❌❌ 逆向回退 |
| **harvest+refill** | 23.5 | 18-20 | **39.2** | +96~+118% | ❌❌❌ 最大回退 |
| **wall** | **315.7** | **210-230** | **298.1** | +30~+42% 偏差 | **仅兑现 17%** |

**净收益**: -17.6s (5.6%)，远低于预测的 -87~-106s (28-34%)

### 10.5 pool=32768 时 scaling 劣化

| 阶段 | pool=16K O9 | pool=32K O9 | 比值 | 理想比值 |
|------|------------|------------|------|----------|
| **cascade** | 37.1s | **106.6s** | **2.87×** | ≤1.0× |
| **distribute** | 31.8s | **108.8s** | **3.42×** | ≤1.0× |
| **collect** | 24.2s | 59.1s | 2.44× | ≤1.0× |
| compact | 21.8s | 29.0s | 1.33× | ~0.6× |
| **wall** | **298s** | **557s** | **1.87×** | ≤1.0× |

cascade total_iterations 在两个 pool_size 下完全一致（5,379,253,197），证明是纯 **per-iteration 成本恶化**：
- pool=16K: 6.9 ns/iter
- pool=32K: **19.8 ns/iter** (2.87×)

### 10.6 回退根因分析

#### 根因 1: `init_single_path` 8× 散射 memset — harvest+refill +15.7s

**最大单项回退**。O9 实现中 `init_single_path` 对 8 个独立域数组分别 memset：

```c
memset(c, 0, sizeof(*c));                           // 480B  → core_arr[i]
memset(ri, 0, sizeof(*ri));                          // 228B  → ray_io_arr[i]
memset(ds, 0, sizeof(*ds));                          // 232B  → cnd_ds_arr[i]
memset(bnd, 0, sizeof(*bnd));                        // 128B  → bnd_arr[i]
memset(pv_locals(pv), 0, sizeof(struct path_locals)); // 928B → locals_arr[i]
memset(pv_sfn(pv), 0, sizeof(struct path_sfn_data)); // 3700B → sfn_arr[i]
memset(pv_enc(pv), 0, sizeof(struct path_enc_data)); // 596B  → enc_arr[i]
memset(pv_ext(pv), 0, sizeof(struct path_ext_data)); // 360B  → ext_arr[i]
```

- 8 次 memset 到 **8 个不同基址**的非连续内存区域，总量 6652B（反而比基准的 2040B 大 3.3×）
- 每次 refill 的 slot index `i` 来自 `done_indices`（不连续），8 个独立 TLB 条目 + 8 组独立 cache line
- 3.26M 次 refill × (8 TLB miss + 散射 cache 污染) = 巨量开销
- 基准只做 1 次 `memset(slots[i], 0, 2040B)` — 连续内存、单次 cache line 批量写入

#### 根因 2: compact 从 4B-stride SoA 退化到 480B-stride AoS — compact +3.8s

- 基准 `dispatch_soa.phase[]` 和 `dsoa.active[]` 是独立 dense `int[]` 数组（stride=4B），compact 顺序流式读取，cache line 100% 利用
- O9 改为读 `core_arr[i].phase`（stride=480B），每读 4B 占用 64B cache line — **利用率 0.83%**
- view_size=8192: 基准 32KB 顺序读 → O9 3.75MB 跳读

#### 根因 3: collect 双数组随机访问 — collect +4.9s

- 基准 collect 访问 `path_state[need_ray_indices[k]]` — 一个数组的随机访问，一次 cache miss 同时获取 `phase + ray_req`（同一 AoS 结构体内）
- O9 需访问**两个不同数组**: `core_arr[idx].phase`(480B stride) + `ray_io_arr[idx].ray_req`(228B stride)
- TLB 和 cache miss 翻倍
- AoS 在随机下标访问中有天然空间局部性优势；SoA 仅在顺序遍历中有优势

#### 根因 4: cascade path_view 间接开销 + path_core 仍太大

- `path_core` 480B = 7.5 cache lines，并非预想的"轻量"
- `path_view` 每次 `pv_init()` memset 8 个 NULL + 1 次 core 解引用
- `pv_cnd_ds(pv)` 等访问器：NULL 检查 → 分支 → 指针运算（双层解引用 vs 基准单层 `p->phase`）

#### 连锁效应: refill 变慢 → wavefront width 下降 → 步数上升

| 指标 | 基准 | O9 | 变化 |
|------|------|-----|------|
| avg_width | 14,504 | 13,550 | -6.6% |
| steps | 450,089 | 481,836 | +7.1% |
| gpu_throughput | 3196 Mrays/s | 1822 Mrays/s | -43% |

refill 变慢 → 空闲 slot 更多 → width 降低 → 需更多步 → 更多 gpu_launch (+5.3s) → GPU batch 更小 → throughput 下降。这是负反馈链，refill 回退是根因。

> **注**: 子代理逐行对比 19 个核心函数确认 **O9 无逻辑 bug**。宽度/步数差异来自 struct 布局变化导致的浮点 ≤1 ULP 分歧被 MC 模拟放大（DS retry 144→8, paths_failed 115→11），属于蒙特卡洛正常统计波动。

### 10.7 pool_size scaling 超线性劣化机制

pool=32K 时 cascade 2.87× 超线性劣化的根因：

1. **5× TLB 条目消耗**: cascade 访问 core/cnd_ds/bnd/locals/sfn 5 个不同基址，pool 翻倍 → TLB 覆盖率减半 → TLB miss 暴增
2. **L3 associativity 冲突**: 5 个数组的相同 slot_idx 映射到完全不同的 L3 set，32 OMP 线程同时访问 → set 冲突驱逐
3. **硬件 prefetcher 失效**: baseline AoS `slots[]` 是单一连续数组（stride 固定 2040B），prefetcher 可跟踪 1 个 stream。O9 的 5 个数组各自 stride 不同（480/232/128/928/3700B），大多数 CPU 仅支持 2-4 个 stream prefetch

pool=16K 时上述开销被 L3 余量隐藏（cascade 数据 ~7.5MB < L3 36MB），pool=32K 时数据翻倍至 ~15MB，L3 有效容量被 5 倍 TLB/associativity 压力大幅削减。

---

## 11. 最终结论（实测后更新，2026-03-05）

| 决策项 | 预测结论 (§9) | 实测结论 | 判定 |
|--------|-------------|---------|------|
| O9 净收益 | -87~-106s (28-34%) | **-17.6s (5.6%)** | ❌ 严重不达 |
| sync_a + sync_b 消除 | -46s | **-46s** | ✅ 完美兑现 |
| cascade 改善 | -20~-26s | **-3.1s** | ❌ 仅 12% |
| 其他阶段改善 | -21~-34s | **+28.3s (总计回退)** | ❌❌ 逆向 |
| pool_size 扩展性 | 改善 | **严重劣化 (2.87× 超线性)** | ❌❌❌ |
| O9 是否值得 2-3 周？ | 是 | **否** — 成本/收益比不可接受 | 推翻 |

### 预测模型失败原因

1. **低估 AoS 空间局部性**: 模型假设降低 stride 即改善 cache 行为，但忽略了 AoS 在**随机下标访问**中同一 slot 多字段共享 cache line 的天然优势
2. **未计入 TLB/prefetch 多数组代价**: 模型假设工作集缩小 = cache 效率提升，但 5-8 个独立数组的 TLB 条目和 prefetch stream 消耗完全抵消了 stride 缩小的收益
3. **忽略 refill 全域 memset**: 模型假设 "memset 2040B→480B = 节省"，但实际实现对 8 个域全量 memset（总 6652B > 2040B），且散射到 8 个非连续地址
4. **compact 回退未预见**: 模型认为 compact "已是 SoA 读取，改善有限"，实际 dispatch_soa 的 4B-stride 密集数组在全量扫描中远优于 480B-stride core_arr

### 决策

**O9 结题搁置**。多数组 SoA 域分解方向在当前 CPU 架构（x86-64, 36MB L3, 32 OMP threads）下成本/收益比不可接受。核心矛盾是：**消除 sync 层节省的 46s 被多数组随机访问的 cache/TLB 劣化吃掉大半，且 pool_size scaling 严重恶化使调参空间变窄**。

代码保留在 `stardis-cus3d-o9` worktree 供参考，不合入 main。

---

*报告创建: 2026-03-04 | 实测更新: 2026-03-05 | 状态: ❌ 结题搁置*
