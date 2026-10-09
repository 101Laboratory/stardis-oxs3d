# [TODO] Stack-Free PicardN: 显式栈外化为 PENDING Pool

**状态**: 设计阶段（未实施）
**前置**: B-4 M8（显式栈状态机，sfn_arr SoA 数组）已完成
**目标**: 把 picardN 的显式祖先栈（`pool->sfn_arr[]`，3.7 KB / slot）完全外化为独立 path slot，**消除 sfn_arr 整列**，解除 `picard_order ≤ 3` 算法上限（实践受 pool 容量软约束）

---

## 一、Picard 机制本质回顾

### 1. 问题：固/流界面的非线性辐射

固/流耦合换热边界的能量守恒含 $\varepsilon\sigma T^4$ 非线性项。MC 路径追踪要求把它写为按概率分支的线性形式：

$$
p_{cv}=\tfrac{h_{cv}}{\hat h},\ p_{cd}=\tfrac{h_{cd}}{\hat h},\ p_{rd}=\tfrac{h_{rd}}{\hat h},\ \hat h=h_{cv}+h_{cd}+\hat h_{rd}
$$

辐射系数 $h_{rd}=\varepsilon\sigma(T_s^2+T_{env}^2)(T_s+T_{env})$ 依赖未知温度 → 需要 Picard 迭代破除循环。

### 2. Picard order 的语义

- **`picard_order = 1`**：用预设 $\hat T$ 给 $\hat h_{rd}=4\sigma\hat T^3\varepsilon$（一阶展开），单次分支选择 + $T_{ref}$ 线性化偏置项。偏差 $O((T-T_{ref})^2)$。
- **`picard_order > 1`（picardN）**：null-collision + 嵌套采样 6 个温度 $T_0..T_5$，用三元乘积构造 $T^3$ 的无偏估计：
  $$h_{rd}\propto T_3T_4T_5 + T_0T_3T_4 + T_0T_1T_3 + T_0T_1T_2$$

### 3. 树形展开 vs 分支数 vs 深度

| 概念 | 值 | 由谁决定 |
|---|---|---|
| 分支数 (fan-out) | **6**（固定） | $T^3$ 展开式的项数 |
| 树深 | **`picard_order`** | 用户参数 |
| 叶节点行为 | picard1 线性化 + $T_{ref}$ | 边界 shader 提供 |
| 偏差量级 | $O((T-T_{ref})^{2P})$ | 每加一层翻倍幂次 |

理论树规模上界 $N \le (6^P-1)/5$。实际由于：
- **CHECK_PMIN_PMAX 早接受/早拒绝**：拿到部分 $T_i$ 即可判定，跳过剩余采样
- **null-collision 拒绝**：整组 $T_0..T_5$ 可能被丢弃重抽
- **非辐射分支提前终止**：概率 $p_{cv}+p_{cd}$ 直接进入对流/传导

→ 平均工作量远小于 $6^P$。

### 4. 在路径追踪流程中的位置

```
摄像机/探针起点
  └ sample_realisation → 初始 rwalk
     └ sample_coupled_path
        ├ boundary_path ← 命中表面
        │  ├ solid/solid    : 透射继续
        │  ├ solid/fluid p1 : 单次分支（线性）
        │  └ solid/fluid pN : ★PICARD★
        │     └ COMPUTE_TEMPERATURE → 递归回 sample_coupled_path
        ├ conductive_path : delta-sphere / WoS
        ├ convective_path : 流体内对流
        └ radiative_path  : 视线辐射段
     ↓ 直到命中已知温度（Dirichlet / 已知介质 / 环境黑体）
```

**只在固/流耦合边界启用**。每个子路径本身也是完整路径追踪，形成最多 `picard_order` 层 × 每层最多 6 子调用的递归树。

### 5. Green function 不兼容 picardN

Green 假设系统线性，Picard 高阶展开破坏线性。代码层面通过 `test_invalidity_picardN_green` 强制 `picard_order == 1`。

---

## 二、为什么不能尾递归优化

`COMPUTE_TEMPERATURE` 返回的 $T_i$ 立即用于：
1. 计算 $h_{radi,\min/\max}$（依赖前面所有 $T_0..T_{i-1}$）
2. CHECK_PMIN_PMAX 早终止判定
3. 决定是否继续采样 $T_{i+1}$ 或 break/continue

**递归调用不在尾位置**（每次调用后都有累积运算），且为**树形递归**（fan-out ≤ 6），不是线性递归链。状态包括 `rwalk_snapshot`, `T_snapshot`, `hvtx_saved`, `ihvtx_radi_begin/end`, `r`, `T0..T5`，跨子调用必须保留。

**结论**：TCO ✗，显式栈/CPS ✓。

---

## 三、当前 GPU 端实现（M8 显式栈）

[sdis_wf_steps_bnd_sfn.c](../../stardis-oxs3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c) 状态机：

```
SFN_PROB_DISPATCH → conv / cond / rad
  └ rad: SFN_RAD_TRACE → ... → SFN_RAD_DONE
     └ SFN_COMPUTE_Ti → T.done? 直接用 : push → BND_DISPATCH
        └ SFN_COMPUTE_Ti_RESUME → pop → SFN_CHECK_PMIN_PMAX
           └ accept / reject / 下一个 Ti
```

### 当前内存布局（基于 [sdis_wf_state.h](../../stardis-oxs3d/stardis-solver/0.16.2/src/sdis_wf_state.h)）

- **path_state**（hot data）：~2.2 KB / slot —— rwalk、T、ctx、ray_req、hot、filter_data、locals union
- **pool->sfn_arr[]**（cold SoA，单独数组）：**~3700 B / slot**，含 `stack[MAX_PICARD_DEPTH=3]` 三个祖先帧快照 + `depth`
- 其他 SoA 冷块：`enc_arr`、`ext_arr`、`scn_arr` 等

**关键事实**：`sfn_stack` 已经不在 path_state 内（O9 系列 P1 优化已迁出），是独立 SoA 数组。因此 PENDING 方案对 path_state 大小**无直接影响**；真正的目标是**消除 sfn_arr 这 3.7 KB / slot 的冷区**。

### 心智模型修正

`path_state` + `sfn_arr[slot]` 协同工作时：
- `path_state` 承载**当前活跃帧**（无论父还是子，时分复用同一 slot）
- `sfn_arr[slot].stack[0..depth-1]` 承载**冻结的祖先帧快照**（rwalk_saved, T_saved, bnd_sf_backup, T_values, …）
- 子路径 push 时：当前 path_state 被序列化到 `stack[depth++]`；子路径占用同一 slot 继续走主状态机
- 子路径 PATH_DONE 时：dispatcher 在 sfn_arr[slot].depth > 0 时拦截，phase 切回 `SFN_COMPUTE_Ti_RESUME`，从 `stack[--depth]` 反序列化恢复祖先帧

- 栈溢出（depth+1 >= MAX）时 fallback 到同步 `solid_fluid_boundary_picardN_path_3d`

---

## 四、提议方案：PENDING Pool 外化

### 核心思路

每个 picardN 边界点生成的子路径**作为带标记的普通 path 写入新 slot**。父 path 留在原 slot 不动（数据不复制、不序列化），通过状态位标记为 `PICARD_PENDING`：

- 父 path → 进入 `PICARD_PENDING` 等待子完成；其 path_state **保持原状**，无需 backup（因为子在别的 slot 跑，不会覆写父的 union）
- 子完成时 dispatcher 拦截 `PATH_DONE`，把 `T.value` 写回父 `T_values[T_count++]`；子 slot 回收
- 父继续：phase = `SFN_CHECK_PMIN_PMAX`，按 CHECK 结果决定 early accept/reject 或 emit 下一个子

**本质**：把 inline 栈帧（`sfn_arr[slot].stack[]`）的祖先冻结快照**完全消除**——既然子不再覆写父的 slot，就根本不需要 backup。

### 状态搬迁（极简）

父 PENDING slot 需要新增的字段（写入原本未使用的 sfn 替代区域，或挪到 path_state union 末尾）：
- `child_slots[6]`（uint32 × 6 = 24 B）：子 slot 索引，用于完成时路由 T.value 回写
- `T_values[6]` + `T_count`（已在 bnd_sf locals 中）
- `r, p_conv, p_cond, h_hat, epsilon`（CHECK_PMIN_PMAX 复用，已在 bnd_sf locals 中）
- `rwalk_s, T_s, hvtx_s`（fluid 侧辐射子路径起点，已在 bnd_sf locals 中）
- `rwalk_snapshot, T_snapshot, hvtx_saved`（null-collision 回滚，已在 bnd_sf locals 中）

**关键观察**：上述除 `child_slots[6]` 外的所有字段，**已经在 `path_state.locals.bnd_sf` 内**（M8 实现时为支持 picardN 已经备好）。新方案下父 PENDING 留在原 slot，这些字段原地保留即可。

**真正新增的存储**：仅 `child_slots[6]`（24 B / slot）。**比 sfn_arr 3700 B / slot 小 150 倍**。

### 子 path 的初始化

子 emit 时，从父 path_state 继承所需起点写入子 slot 的 path_state：
- `rwalk` ← 父的 `rwalk_s`（i<3）或 `rwalk`（i≥3）
- `T` ← TEMPERATURE_NULL（func = boundary_path_3d）
- `ctx` ← 父的 ctx，但 `nbranchings++`
- `rng` ← 从父 RNG split 出独立 substream（详见 §六.1）
- `parent_slot, slot_in_parent`（uint32 × 2 = 8 B）：完成时回写路由

子路径独立走完整状态机（boundary / cond / conv / rad / WoS / picardN 嵌套均可），最终 PATH_DONE 时由 dispatcher 检查 `slot_in_parent != INVALID` 路由回写。

### 嵌套 picard

子路径自身可能再次触发 picardN → 生成孙路径 → 又占一个新 slot。子也进入 `PICARD_PENDING` 等待孙。形成 PENDING slot 的 forest（按主路径分组）。

**算法层面**栈深硬约束解除；**实现层面**仍受 slot pool 容量限制（见 §六.3）—— picard_order 越深，pool 占用 = 嵌套链上每层 1 slot，无法做到真正"无限阶"。这也意味着实践中 `picard_order ≥ 4` 极少使用，**大部分 path 是 picard1**（占用 1 slot，全程不进入 PENDING）。

---

## 五、收益与代价矩阵

### 内存账（基于实测数据）

| 项 | M8 现状 | PENDING 方案 | 净变化 |
|---|---|---|---|
| `path_state` per slot | ~2.2 KB | ~2.2 KB + 24 B (`child_slots[6]`) | ≈ 持平 |
| `sfn_arr` per slot | **3700 B** | **0**（完全消除） | **-3700 B / slot** |
| 单条 picard1 路径占用 | 2.2 + 3.7 = 5.9 KB | 2.2 KB | **-63%** |
| 单条 picardN（depth=d）占用 | 5.9 KB（1 slot 时分复用） | 2.2 KB × (1 + d) | depth=1: 4.4 KB ↓；depth=2: 6.6 KB ↑（轻微）|

**关键**：实践中 picard_order ≥ 4 极少使用（容量爆炸），且大多数路径不会撞上固/流耦合边界 → **大部分 path 全程是 picard1，享受 -63% 收益**。少数 picardN 路径在 depth=1（最常见）也是净收益；depth=2 持平；depth=3 才略增。

### 全局指标

| 维度 | 收益 / 代价 |
|---|---|
| pool 总显存 | ↓ 显著（sfn_arr 整列消除，节省 `3.7 KB × pool_size`） |
| L1/L2 命中率 | ↑（cold sfn_arr 不再产生 cache 污染；path_state hot 区不变） |
| pool 有效并发数 | 取决于场景：picard1 多 → ↑（slot 瘦身后可扩 pool）；picardN 多 → 持平或微 ↓ |
| 栈深算法上限 | 解除（picard_order > 3 算法可行；实践受 pool 容量约束） |
| 实现复杂度 | ↑↑（pool 中 parent/child 关联、RNG split、heat_path 跨界） |
| 调度复杂度 | ↑（child→parent 唤醒 + slot 回收） |
| bit-exact 可重复性 | ✗（子路径调度顺序不定，仅统计一致） |

---

## 六、关键风险与必须解决的问题

### 1. RNG split 后端支持（**正确性**，一票否决）

子路径独立运行需 RNG fork 出独立 substream。
- **Counter-based RNG（Philox / Threefry）**：原生支持 split
- **KISS**：**不支持 split**

SSP 后端审计：当前使用何种 RNG？若仅 KISS 可用，需先扩展 SSP 添加可分流后端。否则子流相关性破坏无偏性。

### 2. Warp 利用率塌陷（性能瓶颈真凶）

PENDING path 不参与本轮 dispatch → 同 warp 32 lane 可能大部分挂起 → **SIMT divergence**。
- **缓解**：stream compaction，分离 PENDING 与 active 到独立 buffer
- **副作用**：compaction 开销 + path_id 失稳（child 持有的 parent_id 失效）→ 需**稳定 ID + 间接索引表**

### 3. PENDING Pool 容量与死锁

保留 CHECK_PMIN_PMAX 顺序语义（**强烈建议**：父一次只 emit 1 子，子完成回写后才决定是否 emit 下一个）。此时单条主路径最大同时占用 = **1 + picard_order** slot（嵌套链上每层 1 子）。

- 不再是"6 子翻倍"的爆炸场景
- pool 容量储备压力：`pool_size ≥ N_main_paths × (1 + max_picard_order_in_flight)`
- **算法可表达无穷阶 picard，但 pool 容量必然有限** → 实践中仍需 `picard_order` 软上限（如 ≤ 8）。超过时反压 fallback 到同步 M8 实现（保留 sfn_arr 作为 safety net 数组，按需分配）

**死锁防护**：emit 失败（pool 满）→ 父继续按同步路径执行该 $T_i$（synchronous fallback），不进入 PENDING。这要求**保留**同步 picardN 实现路径作为后备。

### 4. 子路径 emit 的吞吐放大

**采用顺序语义后**（父一次 emit 1 子），每条主路径在 picardN 内的额外 slot 占用是 `picard_order` 量级，不是 `6^P`。实际 emit 次数受 CHECK_PMIN_PMAX 早终止压制 → 平均 emit 数 << 6。

- emit 队列：每轮单 slot 写入即可，无需 burst
- kernel 启动配置：按 active path 数动态调整（pool 中 active 与 PENDING 比例可用 atomic counter 跟踪）

### 5. heat_path 跨 path 一致性

CPU 同步版里 heat_path 是 ctx 共享 buffer，子路径直接 append 按 branch_id 区分。并行子路径 append → 顺序由调度决定 → **顶点顺序非确定**。

- GPU 端若已是每 path 独立 buffer + 末尾 merge：问题转化为 merge 阶段排序成本
- 否则需重构 heat_path 注册路径

### 6. NULL_COLLISION 跨 path 回滚

整组 $T_0..T_5$ 拒绝时需：
- `heat_path_restart(ctx->heat_path, &hvtx)`
- `heat_path_increment_sub_path_branch_id(...)`
- reset rwalk → 决定是重发 6 个新子还是父保持 PENDING 复用

跨 path 撤销比 inline 撤销显著复杂。

### 7. 公共前缀（辐射子路径）的归属

`radiative_path` 在 $T_0..T_5$ 采样前完成（决定 `rwalk_s` 和 `hvtx_s`），是 picardN 的 prelude，**不是 6 个 $T_i$ 之一**。GPU 端是 `SFN_RAD_TRACE` 多步状态。新方案下仍属"父 PENDING 之前的预处理"，不应作为独立子 path（除非另设公共前缀完成事件）。

### 8. 测试基础设施重写

[test_sdis_b4_m8_picardN.c](../../stardis-oxs3d/stardis-solver/0.16.2/src/test_sdis_b4_m8_picardN.c) 的栈结构测试不再适用：
- 改为 PENDING pool 测试（容量、parent-child 关联、唤醒顺序）
- 新增**乱序唤醒测试**（6 子任意顺序完成结果都正确）
- bit-exact 测试退化为统计 3σ（沿用 physical_consistency_stats 框架）

---

## 七、实施路线建议（渐进式）

1. **审计 RNG 后端**（一票否决项）：确认 SSP 当前默认 RNG 是否支持 split / fork。若不支持，先扩展 SSP 添加 Philox/Threefry 后端。
2. **引入 `child_slots[6]` + `parent_slot, slot_in_parent`**：在 path_state 末尾或 bnd_sf union 末尾加这些字段（< 50 B），不动 sfn_arr。
3. **改造 COMPUTE_Ti**：把 `push sfn_arr.stack + 同 slot 重入 BND_DISPATCH` 改为 `分配新 slot + 初始化子 + 父进入 PICARD_PENDING`。保留**顺序语义**（一次 emit 1 子）。
4. **改造 PATH_DONE 拦截**：从 `sfn_arr[i].depth > 0 → SFN_COMPUTE_Ti_RESUME` 改为 `slot_in_parent != INVALID → 回写到 parent.T_values[slot_in_parent] + 唤醒父 + 回收子 slot`。
5. **删除 sfn_arr 数组**：当所有路径都走 PENDING 方案后，pool 中 `sfn_arr[]` 整列可移除。`MAX_PICARD_DEPTH` 改为 pool 容量软约束。
6. **保留同步 fallback**：emit 失败时调用同步 `solid_fluid_boundary_picardN_path_3d`。此时**只需要保留一份临时 sfn 栈**（线程局部或单 slot 复用），不再是 SoA 数组。
7. **可选**：推测并行（一次 emit 多子），按场景开关启用；stream compaction 分离 active / PENDING（若 warp divergence 实测严重）。

---

## 八、决策门：实施前必须 profile 的数据

ROI 取决于以下指标，**实施前在目标场景（porous IR + 高 picard_order 算例）测量**：

| 指标 | 来源 | 决策依据 |
|---|---|---|
| picard1 主导率（占总 path 比例） | wavefront 统计计数器 | 高（典型 > 80%）→ sfn_arr 整列消除收益巨大 |
| picardN 触发率（占 boundary 命中比例） | 同上 | 决定子 slot 占用峰值 |
| `sfn_arr.depth` 平均/峰值 | 同上 | 接近 3 → 解除算法上限有实际收益 |
| 当前 sfn_arr 内存占用 | `sizeof(path_sfn_data) × pool_size` | 已知 ~3.7 KB × pool_size，量化基线 |
| 当前 warp 利用率 | Nsight Compute | baseline；新方案需对比 |
| L1/L2 命中率 | Nsight Compute | sfn_arr 消除后 cold 区减少，预期 ↑ |
| SSP RNG 后端 | 源码审计 | KISS 不支持 split → 一票否决 |

---

## 九、相关引用

- CPU 参考实现：[sdis_heat_path_boundary_Xd_solid_fluid_picardN.h](../../stardis-cpu/stardis-solver/0.16.2/src/sdis_heat_path_boundary_Xd_solid_fluid_picardN.h)
- CPU picard1：[sdis_heat_path_boundary_Xd_solid_fluid_picard1.h](../../stardis-cpu/stardis-solver/0.16.2/src/sdis_heat_path_boundary_Xd_solid_fluid_picard1.h)
- GPU M8 状态机：[sdis_wf_steps_bnd_sfn.c](../../stardis-oxs3d/stardis-solver/0.16.2/src/sdis_wf_steps_bnd_sfn.c)
- M8 单元测试：[test_sdis_b4_m8_picardN.c](../../stardis-oxs3d/stardis-solver/0.16.2/src/test_sdis_b4_m8_picardN.c)
- 边界 dispatch：[sdis_heat_path_boundary_Xd.h](../../stardis-cpu/stardis-solver/0.16.2/src/sdis_heat_path_boundary_Xd.h)
- core 重入计数：[sdis_wf_steps_core.c](../../stardis-oxs3d/stardis-solver/0.16.2/src/sdis_wf_steps_core.c)

---

*归档: 2026-05-14（v2 修正：sfn 栈已在独立 SoA 数组而非 path_state 内；PENDING 方案核心收益是消除 sfn_arr 整列；保留同步 fallback 应对 pool 容量约束）*
