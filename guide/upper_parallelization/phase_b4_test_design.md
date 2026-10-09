# Phase B-4: 细粒度显式状态机 + 射线分桶 — 测试设计文档

**生成时间**: 2026-02-12  
**关联实施计划**: [phase_b4_fine_grained_state_machine.md](phase_b4_fine_grained_state_machine.md)  
**测试框架**: 自定义 C 测试框架（`CHK/OK/BA/BO` 宏 + `main()` 返回 0/1）  
**测试基础设施**: `test_sdis_utils.h/c`（box 几何、dummy shaders、设备工厂）  
**验证基线**: CPU depth-first 求解器 (`STARDIS_WAVEFRONT=0`)

---

## 一、测试策略总述

### 1.0 关键约束：宽度-1 同步 GPU 光追尚未修复

> **⚠️ 阻塞性约束（2026-02-12 确认）**
>
> 当前 `s3d_scene_view_trace_ray`（宽度-1 同步 GPU 光追）尚未完全修复。
> 每次调用都会触发完整的 GPU kernel launch + 同步等待，导致性能极低。
> 目前 `solve_camera` 的深度优先求解器路径（`STARDIS_WAVEFRONT=0`）以及
> wavefront 路径中所有非批量射线调用都依赖此函数。
>
> **直接后果**：
> - 所有依赖 `s3d_scene_view_trace_ray` 的端到端测试在当前状态下极度缓慢（不可行）
> - 无法在每个 Milestone 完成时运行 `solve_camera` 端到端回归测试
> - **只有在全部 B-4 Milestone（M0–M10）完成后，宽度-1 同步调用被批量调用完全替代，
>   端到端 IR Render 链路的回归测试才具备可行性**
>
> **应对策略**：将测试分为"即时可执行"和"延迟到全量完成后执行"两类。
> 各 Milestone 的中间验证聚焦于编译期检查、状态机转换逻辑单元测试（使用 mock/合成数据，
> 不涉及实际 GPU 光追）、代码结构审查和构建验证。

### 1.1 核心验证原则

Phase B-4 的所有改动都在 CPU 状态机侧进行，GPU 仅提供批量射线追踪。因此验证策略基于以下不变量：

1. **功能正确性**: 细粒度状态机产出的温度结果必须与原始 depth-first 求解器统计兼容
2. **批量化等价性**: 将射线从同步调用提升到批量提交，物理结果不变（仅射线执行顺序/分组变化）
3. **回归安全**: 每个 Milestone 完成后，所有现有 32 个 CMake 测试必须继续 pass
4. **渐进可测性 [新增]**: 受宽度-1 同步光追性能限制，端到端测试延迟到所有 Milestone 全部完成后执行；中间阶段仅执行不依赖实际 GPU 光追的测试

### 1.2 验证层次

```
Layer 4: 端到端回归 (全场景 solve_camera)      ← ⛔ 延迟到 M0–M10 全部完成后
Layer 3: 路径类型专项 (限定场景激活特定状态链)  ← ⛔ 延迟（依赖 solve_camera）
Layer 2: 状态机单元测试 (step 函数 + 转换逻辑) ← ✅ 每个 Milestone 必做（mock 数据）
Layer 1: 编译期 / 结构检查 (sizeof, enum, 构建) ← ✅ 每个 Milestone 必做
```

#### 即时可执行测试（每个 Milestone）

- **编译期检查**: `sizeof(struct path_state)`、`PATH_PHASE_COUNT` 连续性、枚举值无间隙
- **构建验证**: `sdis` 和 `stardis` target 零编译错误
- **状态机转换单元测试**: 手动构造 `path_state`，调用 `advance_one_step_no_ray` / `advance_one_step_with_ray`，验证状态转换正确性（使用合成 hit 数据，无需 GPU 光追）
- **射线请求结构检查**: 验证 `ray_count_ext`、`ray_bucket` 字段的正确赋值
- **代码审查**: 新增 step 函数与原始 depth-first 代码的逻辑对等性人工审查

#### 延迟测试（全部 Milestone 完成后）

- **端到端温度回归**: `solve_camera` wavefront vs depth-first 像素级统计兼容
- **解析解对比**: 与已知解析解温度分布对比
- **批量化覆盖率**: `total_batch_rays / total_all_rays > 85%`
- **性能回归**: B-4 vs B-3 M3 的 wall-clock 时间对比
- **集成测试 T-INT**: 全路径类型混合场景

### 1.3 统计兼容性检验方法

沿用 `test_sdis_wavefront_benchmark.c` 的验证方法：

```c
/* 对每个像素 (ix, iy): */
double se_combined = sqrt(se_ref * se_ref + se_test * se_test);
int compatible = fabs(mean_ref - mean_test) <= 4.0 * se_combined;

/* 通过条件: >= 95% 像素统计兼容 */
double pass_rate = (double)n_compatible / (double)n_pixels;
int pass = (pass_rate >= 0.95);
```

参数选择:
- **spp**: 通常 64-256（平衡精度与测试时间）
- **image_def**: 32×32 到 64×64（快速测试）
- **4σ 门限**: 保证 >99.99% 的统计功效（单像素级别）
- **95% 像素通过**: 容忍少数像素因几何退化（掠射角等）导致的极端方差

### 1.4 测试命名规范

```
test_sdis_b4_m{N}_{feature}.c
  │            │     │
  │            │     └─ 测试焦点描述
  │            └─ Milestone 编号
  └─ Phase B-4 前缀
```

---

## 二、Milestone 专属测试设计

### T0: Milestone 0 — 状态枚举 + 数据结构扩展

**测试文件**: `test_sdis_b4_m0_enum_regression.c`

**测试目标**: 确认扩展枚举和数据结构后，现有行为完全不变

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------| 
| T0.1 | `path_phase` 枚举值连续性 | 编译期检查 `PATH_PHASE_COUNT` 值 / 运行时遍历 | 所有枚举值 0..N-1 无间隙 | ✅ 即时 |
| T0.2 | `path_state` 大小合理性 | `sizeof(struct path_state)` 检查 | ≤ 2500 bytes | ✅ 即时 |
| T0.3 | 现有 wavefront benchmark 回归 | 运行 `test_sdis_wavefront_benchmark` | 结果与改动前一致 | ⛔ 延迟 |
| T0.4 | 所有 32 个 CMake 测试回归 | `ctest --output-on-failure` | 全部 pass | ⛔ 延迟 |
| T0.5 | [新增] 构建验证 | `cmake --build` sdis + stardis target | 零编译错误 | ✅ 即时 |
| T0.6 | [新增] switch 覆盖完整性 | 所有新枚举值在 advance_one_step_* 的 switch 中有对应 case | 编译无 -Wswitch 警告 | ✅ 即时 |

**验证关键点**:
- `advance_one_step_no_ray` 和 `advance_one_step_with_ray` 的 `switch` 中，所有新状态走 `default` 分支，不改变执行路径
- `struct path_state` 的新字段全部零初始化

---

### T1: Milestone 1 — Enclosure 查询子状态机

**测试文件**: `test_sdis_b4_m1_enclosure_batch.c`

**测试目标**: 验证 6 方向批量 enclosure 查询与同步逐条查询结果一致

**测试场景**: 嵌套 box 几何（2 个同心 box，内外不同 enclosure_id）

```
外部 box: [-1,-1,-1] → [1,1,1]    enclosure_id = 0 (radenv)
内部 box: [-0.3,-0.3,-0.3] → [0.3,0.3,0.3]  enclosure_id = 1 (solid)
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T1.1 | 内部点 enclosure 查询 | 点 (0,0,0)，调用 `step_enc_query_emit` + `step_enc_query_resolve` | `enc_id == 1` | ✅ 即时 (mock hit) |
| T1.2 | 外部点 enclosure 查询 | 点 (0.5,0.5,0.5)，同上 | `enc_id == 0` | ✅ 即时 (mock hit) |
| T1.3 | 边界附近点 | 点 (0.3-ε, 0, 0)，距内 box 面 ε 处 | 正确识别内侧 | ✅ 即时 (mock hit) |
| T1.4 | 批量 vs 同步一致性 | 随机生成 1000 个点，对比 `step_enc_query` 结果与 `scene_get_enclosure_id_in_closed_boundaries` 结果 | 100% 一致 | ⛔ 延迟 (需实际光追) |
| T1.5 | 射线数量验证 | 每次查询必须产生 6 条射线请求 | `ray_count_ext == 6` | ✅ 即时 |
| T1.6 | 射线桶类型验证 | 所有 6 条射线标记为 `RAY_BUCKET_ENCLOSURE` | 桶类型正确 | ✅ 即时 |
| T1.7 | 全部 miss 的退化情况 | 点在所有几何体外部 | `enc_id == ENCLOSURE_ID_NULL` 或 fallback 到遍历模式 | ✅ 即时 (mock miss) |

**验证关键点**:
- 6 方向射线结果中，仅需第一个有效交点的 enclosure_id
- 退化情况：掠射角导致所有 6 方向的 `cos(N, dir)` 过小

---

### T2: Milestone 2 — 射线分桶框架

**测试文件**: `test_sdis_b4_m2_ray_bucketing.c`

**测试目标**: 验证分桶排序后射线-路径映射的正确性，以及分桶不改变物理结果

**测试场景**: 标准 box 场景（与 wavefront benchmark 相同），forced mixed state composition

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T2.1 | 桶计数验证 | 构造已知混合状态池（手动设置 20 个 RAD + 10 个 DS + 5 个 ENC），调用 `collect_ray_requests_bucketed` | 桶计数准确: rad=20, ds=20, enc=30, shadow=0, startup=0 | ✅ 即时 |
| T2.2 | 桶偏移连续性 | `bucket_offsets[i+1] >= bucket_offsets[i]` 且 `sum == total_rays` | 偏移正确 | ✅ 即时 |
| T2.3 | ray_to_slot 映射完整性 | 分桶后每条射线的 `ray_to_slot[j]` 指向有效路径 | 无悬空映射 | ✅ 即时 |
| T2.4 | 分发后状态正确 | 分桶 collect → batch trace → bucketed distribute → 对比分桶前后路径状态 | 位级一致（相同种子） | ⛔ 延迟 (需 batch trace) |
| T2.5 | 端到端统计兼容 | 标准 box 场景，分桶 vs 不分桶 (spp=128, 32×32) | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T2.6 | 桶分布诊断输出 | 检查诊断日志中各桶射线数 | 诊断日志非空、桶数符合预期 | ✅ 即时 |

**验证关键点**:
- 分桶仅改变射线在数组中的排列顺序，不改变物理含义
- 确认 `ray_to_slot` 和 `ray_slot_sub` 在分桶后仍正确

---

### T3: Milestone 3 — Solid/Solid Reinjection

**测试文件**: `test_sdis_b4_m3_solid_solid.c`

**测试目标**: 验证 solid/solid 界面 reinjection 射线批量化后的正确性

**测试场景**: 双层固体 box（两种导热系数不同的固体材料共享界面）

```
Box A: lambda_A = 1.0 W/(m·K), T_top = 400K
Box B: lambda_B = 2.0 W/(m·K), T_bottom = 300K
界面: solid/solid, 无对流/辐射
```

此场景的解析解已知（稳态线性温度分布），可作为精确验证基准。

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T3.1 | Reinjection 射线数量 | 每次 `SS_REINJECT_SAMPLE` 产生 4 条射线 | `ray_count_ext == 4` | ✅ 即时 |
| T3.2 | 射线方向对称性 | front_dir1 = reflect(front_dir0)，back_dir1 = reflect(back_dir0) | 方向向量反射对称 | ✅ 即时 |
| T3.3 | enclosure miss → ENC 子状态 | 构造 miss 场景（开放边界），验证正确进入 `PATH_ENC_QUERY_EMIT` | 状态转换正确 | ✅ 即时 (mock) |
| T3.4 | 注入侧概率正确 | 多次运行，统计 front/back 注入比例 | 接近 `lambda_A/(lambda_A + lambda_B)` ± 3σ | ✅ 即时 (mock) |
| T3.5 | Time rewind 终止 | 非稳态限制条件下路径在 time rewind 时正确终止 | `PATH_DONE`, `done_reason == TIME_REWIND` | ✅ 即时 (mock) |
| T3.6 | 端到端温度验证 | 32×32 spp=128，对比 depth-first | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T3.7 | 解析解对比 | 同上，对比稳态线性温度分布 | 误差 < 5% (蒙特卡洛统计误差) | ⛔ 延迟 |

**验证关键点**:
- Reinjection 目标位置必须在正确的 enclosure 内
- `solid_reinjection` 的 time_rewind 逻辑与原始代码一致
- 注入后正确设置 `rwalk.enc_id` 和后续状态

---

### T4: Milestone 4 — Delta-Sphere 导热细化

**测试文件**: `test_sdis_b4_m4_delta_sphere.c`

**测试目标**: 验证 delta-sphere 导热路径细化（enclosure 验证批量化）后的正确性

**测试场景**: 单固体 box，Dirichlet 边界条件

```
Box: lambda = 1.0 W/(m·K)
6面温度: T_x+ = 400K, T_x- = 300K, 其余 = 350K
```

解析解: 线性温度分布 T(x) = 300 + 100·x (x ∈ [0,1])

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T4.1 | 初始 ENC 查询 | `PATH_CND_INIT_ENC` 进入 ENC 子状态后正确返回到 `CND_DS_CHECK_TEMP` | 状态链正确 | ✅ 即时 |
| T4.2 | 步进射线 2 条 | `CND_DS_STEP_TRACE` 产生 dir0+dir1 两条射线 | `ray_count_ext == 2`, `ray_bucket == RAY_BUCKET_STEP_PAIR` | ✅ 即时 |
| T4.3 | Enclosure 验证触发条件 | hit0.distance > delta → 进入 `CND_DS_STEP_ENC_VERIFY` | 条件判断正确 | ✅ 即时 (mock hit) |
| T4.4 | Enclosure 验证跳过条件 | hit0.distance ≤ delta → 直接进入 `CND_DS_STEP_ADVANCE` | 跳过 ENC 查询 | ✅ 即时 (mock hit) |
| T4.5 | 循环/退出判断 | HIT_NONE → 继续循环(CHECK_TEMP), !HIT_NONE → BND_DISPATCH | 状态转换正确 | ✅ 即时 (mock) |
| T4.6 | 步数统计 | 同一路径的 DS 步进次数与 depth-first 版本相当 | 步数比 ∈ [0.8, 1.2] | ⛔ 延迟 |
| T4.7 | 端到端温度验证 | 64×64 spp=64，对比 depth-first | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T4.8 | 解析解对比 | 检查中心行像素温度均值 | 与 T(x) 解析解误差 < 3% | ⛔ 延迟 |
| T4.9 | Robust retry 计数 | 监控 `ds_robust_attempt` 分布 | retry > 0 的比例 < 5% (典型场景) | ⛔ 延迟 |

**验证关键点**:
- 每次 delta-sphere 循环迭代的射线发射-处理-推进是否与原始 `step_conductive_ds_process` 等价
- `ds_green_power_term` 累积精度不受状态切分影响

---

### T5: Milestone 5 — Picard1 Null-Collision

**测试文件**: `test_sdis_b4_m5_picard1.c`

**测试目标**: 验证 picard1 (solid/fluid) 的 reinjection + null-collision 循环批量化

**测试场景**: 固-流耦合场景

```
固体 box: lambda = 1.0 W/(m·K)
流体包壳: h_conv = 10 W/(m²·K), T_fluid = 350K
界面: solid/fluid, emissivity = 0.5 (有辐射耦合)
辐射环境: T_rad = 300K
picard_order = 1 (picard1 路径)
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T5.1 | Reinjection 射线数量 | `SF_REINJECT_SAMPLE` 产生 2 条射线 (solid 侧) | `ray_count_ext == 2` | ✅ 即时 |
| T5.2 | 概率分派正确性 | 统计 conv/cond/rad 分派比例 (10000 次) | 接近 p_conv/p_cond/p_radi ± 3σ | ✅ 即时 (mock) |
| T5.3 | Null-collision 辐射射线 | `SF_NULLCOLL_RAD_TRACE` 发射 1 条辐射射线 | `ray_bucket == RAY_BUCKET_RADIATIVE` | ✅ 即时 |
| T5.4 | Accept/reject 判断 | 追踪 accept 率与理论 h_radi/h_radi_hat 一致 | accept 率 ∈ [0.3, 0.8] (场景相关) | ⛔ 延迟 |
| T5.5 | Null-collision 循环次数 | 统计平均循环次数 | 有限，均值 < 10 (h_radi_hat 选择合理) | ⛔ 延迟 |
| T5.6 | Robin 后检查 | null-collision accept 后进入 `BND_POST_ROBIN_CHECK` | 状态转换正确 | ✅ 即时 (mock) |
| T5.7 | 端到端温度验证 | 32×32 spp=128，对比 depth-first | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T5.8 | 仅导热路径场景 | emissivity=0, h_conv=0 → 只走 cond | 退化为纯导热，与 T4 结果一致 | ⛔ 延迟 |
| T5.9 | 仅对流路径场景 | lambda=∞ (高导热)，h_conv >> 0 → 几乎只走 conv | 温度趋向 T_fluid | ⛔ 延迟 |

**验证关键点**:
- null-collision 循环的统计无偏性（accept 率 × 路径数 ≈ 理论概率 × 路径数）
- h_radi_hat 上界是否足够大（过小会导致 accept 率 > 1 的数值错误）
- 外部通量子过程（如有）与 M7 的集成

---

### T6: Milestone 6 — 对流路径 + 边界分派

**测试文件**: `test_sdis_b4_m6_convective.c`

**测试目标**: 验证对流路径的启动射线批量化 + 边界分派重构

**测试场景**:

场景 A（对流主导）:
```
流体包壳: h_conv = 100 W/(m²·K), T_fluid = 400K
固体 box: T_boundary = 300K (Dirichlet)
picard_order = 1
```

场景 B（纯 Dirichlet）:
```
固体 box: 6面已知温度
相机观察界面
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T6.1 | 对流启动射线 | 从流体内部启动时产生 1 条 +Z 射线 | `ray_bucket == RAY_BUCKET_STARTUP` | ✅ 即时 |
| T6.2 | 从界面启动 | 路径从界面打到流体侧时直接进 SAMPLE_LOOP | 无启动射线 | ✅ 即时 (mock) |
| T6.3 | Null-collision 接受率 | 统计 h_c/h_c_upper 比值分布 | 在 (0, 1] 范围内 | ⛔ 延迟 |
| T6.4 | BND_DISPATCH Dirichlet 终止 | 场景 B，所有路径到达 Dirichlet 面即终止 | 100% 路径 done_reason == TEMP_KNOWN | ✅ 即时 (mock) |
| T6.5 | BND_DISPATCH 三路分派 | 手动构造 3 种界面类型，验证分派目标 | ss→SS_REINJECT, sf(max_br)→SF_REINJECT, sf(not)→SFN_PROB | ✅ 即时 (mock) |
| T6.6 | Robin 后检查 | 从对流/导热路径进入 boundary → robin check → done/continue | DONE 或继续到下一路径 | ✅ 即时 (mock) |
| T6.7 | 端到端 (场景 A) | 32×32 spp=128，对比 depth-first | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T6.8 | 端到端 (场景 B) | 32×32 spp=64，对比解析解 | 温度均值误差 < 1% (Dirichlet 场景精确) | ⛔ 延迟 |

**验证关键点**:
- 对流 null-collision 循环是纯计算，不需射线——不影响 batch 但必须正确推进
- `BND_DISPATCH` 的三路分派必须与原始 `step_boundary` 的行为完全一致

---

### T7: Milestone 7 — 外部净通量

**测试文件**: `test_sdis_b4_m7_external_flux.c`

**测试目标**: 验证 shadow ray 和漫射弹跳射线的批量化

**测试场景**:
```
固体 box
外部点光源: 位置 (0, 0, 2), 功率 = 100 W
界面: emissivity = 0.8
picard_order = 1, 外部通量启用
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T7.1 | Shadow ray 发射 | cos_theta > 0 时发射 1 条 shadow ray | `ray_bucket == RAY_BUCKET_SHADOW`, range 限制为源距离 | ✅ 即时 |
| T7.2 | Shadow ray 遮挡 | 在光源与界面间放置遮挡物 | 直接贡献 = 0 | ⛔ 延迟 |
| T7.3 | Shadow ray 未遮挡 | 无遮挡 | 直接贡献 > 0 | ⛔ 延迟 |
| T7.4 | 漫射弹跳射线 | 反射界面产生漫射弹跳 | `ray_bucket == RAY_BUCKET_RADIATIVE` | ✅ 即时 |
| T7.5 | 弹跳处 shadow ray | 弹跳位置的 shadow ray | `ray_bucket == RAY_BUCKET_SHADOW` | ✅ 即时 |
| T7.6 | 多次弹跳循环 | emissivity=0.2 (高反射率) → 多次弹跳 | nbounces > 1, 逐弹跳能量守恒 | ⛔ 延迟 |
| T7.7 | 通量方向正确 | cos_theta ≤ 0 → 跳过直接贡献 | 无 shadow ray 发射 | ✅ 即时 (mock) |
| T7.8 | Return state 正确 | 通量完成后返回 picard1 主循环 | `phase == ext_flux.return_state` | ✅ 即时 (mock) |
| T7.9 | 端到端验证 | 32×32 spp=128，含外部源场景，对比 depth-first | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T7.10 | 无外部源 bypass | 场景无外部源 → 直接跳过整个 EXT 子流程 | 不发射任何 EXT 射线 | ✅ 即时 (mock) |

**验证关键点**:
- `ext_flux` 结构独立于 `locals` union，因为它与 picard 同时活跃
- 漫射弹跳循环的终止条件：miss（散射到无穷）或 absorbed
- 能量守恒：直接 + 漫射反射 + 散射 ≤ 入射通量

---

### T8: Milestone 8 — PicardN 递归栈

**测试文件**: `test_sdis_b4_m8_picardN.c`

**测试目标**: 验证 picardN 的 COMPUTE_TEMPERATURE 递归子路径栈机制

**测试场景**:
```
固-流耦合
picard_order = 2 (max_branchings = 1, 允许 1 层递归)
高辐射耦合: emissivity = 0.9
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T8.1 | 栈深度管理 | 压栈时 `sfn_stack_depth++`，弹栈时 `--` | 始终 ∈ [0, MAX_PICARD_DEPTH] | ✅ 即时 |
| T8.2 | 栈溢出保护 | picard_order > MAX_PICARD_DEPTH+1 → fallback | 打印警告，降级到同步路径 | ✅ 即时 |
| T8.3 | COMPUTE_Ti 子路径启动 | 压栈保存当前 rwalk/T → 进入 BND_DISPATCH | rwalk_saved 与原始一致 | ✅ 即时 (mock) |
| T8.4 | 子路径完成弹栈 | 子路径 PATH_DONE → 检查 sfn_stack_depth > 0 → 弹栈 | resume state 正确 | ✅ 即时 (mock) |
| T8.5 | CHECK_PMIN_PMAX 提前接受 | h_radi 范围窄 → 无需更多 Ti → accept | 减少子路径采样数 | ✅ 即时 (mock) |
| T8.6 | CHECK_PMIN_PMAX 提前拒绝 | h_radi 范围明显高于阈值 → reject → null-collision | 回到 SFN_PROB_DISPATCH | ✅ 即时 (mock) |
| T8.7 | 多层递归 | picard_order=3, 构造深递归场景 | 每层子路径独立、栈帧不互相污染 | ✅ 即时 (mock) |
| T8.8 | 端到端验证 | 32×32 spp=256 (高 spp 因方差大), 对比 depth-first | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T8.9 | PicardN vs Picard1 退化 | picard_order=1 时 SFN 路径不应被触达 | 0 个路径进入 SFN_* 状态 | ✅ 即时 (mock) |

**验证关键点**:
- 栈帧保存/恢复的完整性：rwalk 的位置、时间、enc_id、hit 必须精确恢复
- T_values[] 数组的累积不被子路径污染
- 递归终止条件：所有 6 个 Ti 采样完成或 CHECK_PMIN_PMAX 提前判定

---

### T9: Milestone 9 — WoS 导热路径

**测试文件**: `test_sdis_b4_m9_wos.c`

**测试目标**: 验证 Walk on Spheres 导热路径的细粒度状态

**前置条件**: custar-3d 层实现 `closest_point` 批量 API

**测试场景**:
```
固体 box: lambda = 1.0 W/(m·K)
diff_algo = WOS (选择 WoS 而非 delta-sphere)
Dirichlet: T_x+ = 400K, T_x- = 300K
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T9.1 | Closest-point 查询 | `CND_WOS_CLOSEST` 发射 closest_point 查询 | 返回最近表面点和距离 | ✅ 即时 (mock) |
| T9.2 | ε-shell snap | distance ≤ ε → snap to boundary → BND_DISPATCH | 正确转到边界处理 | ✅ 即时 (mock) |
| T9.3 | 扩散位置有效 | distance > ε → 在球面上采样新位置 → 移动 | 新位置在球面上 | ✅ 即时 (mock) |
| T9.4 | Fallback trace 触发 | 扩散位置无效（check 失败）→ `CND_WOS_FALLBACK_TRACE` | 发射 1 条 trace_ray | ✅ 即时 |
| T9.5 | 循环计数 | 统计 WoS 循环次数 | 有限，与 delta-sphere 量级对比 | ⛔ 延迟 |
| T9.6 | 时间退回终止 | time_travel 到达极限 → DONE | 正确终止 | ✅ 即时 (mock) |
| T9.7 | 端到端温度验证 | 32×32 spp=128, diff_algo=WOS，对比 delta-sphere 结果 | 均值差异 < 5% (两种算法的统计兼容) | ⛔ 延迟 |
| T9.8 | 解析解对比 | 与 T(x) = 300 + 100·x 对比 | 误差 < 3% | ⛔ 延迟 |

**验证关键点**:
- `closest_point` 是不同于 `trace_ray` 的几何查询类型——需要单独的批量 API
- WoS 的 ε-shell 参数选择影响收敛速度和偏差

---

### T10: Milestone 10 — Point-in-Enclosure GPU Kernel

**测试文件**: `test_sdis_b4_m10_enc_locate.c`

**测试目标**: 验证 BVH closest-primitive 的 point-in-enclosure kernel 替代 M1 的6 射线 + 暴力 fallback 方案后，enclosure 查询结果正确且不触发任何宽度=1 同步光追

**背景**: M1 的 `step_enc_query_resolve` 在 6 方向射线全部失败时会 fallback 到 `scene_get_enclosure_id()`（暴力遍历所有图元，每个图元最多 3 次 `trace_ray`）。该 fallback 在 `pool_cascade_non_ray_steps_compact` 内执行，完全破坏 wavefront 批量化设计。M10 用 BVH nearest-neighbor GPU kernel 替代整个 enclosure 查询路径，消除了射线追踪对 enclosure 查询的依赖。

**测试场景**:

场景 A（基础嵌套 box）——与 T1 相同，作为对比基线：
```
外部 box: [-1,-1,-1] → [1,1,1]    enclosure_id = 0 (radenv)
内部 box: [-0.3,-0.3,-0.3] → [0.3,0.3,0.3]  enclosure_id = 1 (solid)
```

场景 B（边缘退化）—— 专门触发原 M1 fallback 的几何：
```
极薄固体夹层: 厚度 = 0.001，查询点在夹层内部
查询点恰在边/顶附近（使 6 方向射线全部 miss 或 cos 过小）
```

场景 C（批量规模）：
```
同场景 A几何，批量查询 10000 个随机点（包含内部/外部/边界附近）
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T10.1 | 状态转换基础 | 手工构造 `path_state`，设置 `phase = PATH_ENC_LOCATE_PENDING`，验证 `path_phase_is_ray_pending` 返回 0（不是射线请求）且 `path_phase_is_locate_pending` 返回 1 | 状态分类正确 | ✅ 即时 |
| T10.2 | Locate submit 字段赋值 | 调用 `step_enc_locate_submit(p)`，验证 `p->enc_locate.query_pos` 已填充、`p->phase == PATH_ENC_LOCATE_PENDING`、`p->needs_locate == 1` | 字段正确 | ✅ 即时 |
| T10.3 | Locate result 状态推进 | 手工填充 `enc_locate.resolved_enc_id = 1`，调用 `step_enc_locate_result(p)`，验证 `p->phase == p->enc_locate.return_state` | return_state 跳转正确 | ✅ 即时 |
| T10.4 | 内部点查询（场景 A） | 点 (0,0,0)，通过完整的 collect→kernel→distribute 流程 | `enc_id == 1` | ⛔ 延迟 (GPU kernel) |
| T10.5 | 外部点查询（场景 A） | 点 (0.5,0.5,0.5) | `enc_id == 0` | ⛔ 延迟 (GPU kernel) |
| T10.6 | 边界附近点（场景 A） | 点 (0.3-ε, 0, 0)，距内 box 面 ε 处 | 正确识别内侧 `enc_id == 1` | ⛔ 延迟 (GPU kernel) |
| T10.7 | 边缘退化场景（场景 B） | M1 的 6 射线全部 miss 的点，M10 仍能正确查询 | closest-primitive 始终找到最近面，`enc_id` 正确 | ⛔ 延迟 (GPU kernel) |
| T10.8 | M1 vs M10 一致性（场景 C） | 10000 个随机点，对比 M10 `find_enclosure_batch` 与 CPU `scene_get_enclosure_id_in_closed_boundaries` 结果 | ≥ 99.9% 一致（允许极边缘 case 差异） | ⛔ 延迟 (GPU kernel) |
| T10.9 | 零宽度-1 射线计数 | 在 `s3d_scene_view_trace_ray`（单射线版）内置计数器，运行包含 enclosure 查询的场景 | enclosure 路径的 `trace_ray` 调用数 = 0 | ⛔ 延迟 |
| T10.10 | Collect/Distribute 流程验证 | 手工构造 10 条路径，5 条 `ENC_LOCATE_PENDING` + 5 条 `RAD_TRACE_PENDING`，运行 `pool_collect_enc_locate_requests` | `enc_locate_count == 5`，且与 `ray_count` 分离 | ✅ 即时 |
| T10.11 | 射线桶不占用 | M10 启用后，`RAY_BUCKET_ENCLOSURE` 桶射线数 = 0 | enclosure 查询完全从射线桶中移除 | ⛔ 延迟 |
| T10.12 | 多磁场景混合 | 在包含 SS/SF/DS enclosure 查询的场景中，所有查询通过 M10 执行 | 端到端温度 ≥ 95% 像素统计兼容 | ⛔ 延迟 |
| T10.13 | path_state 缩小验证 | M10 的 `enc_locate` 字段 vs M1 的 `enc_query` 字段 | `sizeof(enc_locate)` < `sizeof(enc_query)` (约 20B vs 约 200B) | ✅ 即时 |
| T10.14 | cascade 安全性 | 在 cascade 内遍历所有状态转换，断言无 `scene_get_enclosure_id` 调用 | cascade 内零光追 | ✅ 即时 (mock) |
| T10.15 | M10 与 M9 共享 kernel | 确认 `bvh_closest_primitive_batch` 同时服务于 `find_enclosure_batch` 和 `closest_point_batch` | 两个 API 共享同一底层 kernel | ✅ 即时 (代码审查) |

**验证关键点**:
- **正确性等价**: M10 的 `find_enclosure_batch` 必须产出与 CPU 原始 `scene_get_enclosure_id_in_closed_boundaries` + `scene_get_enclosure_id`（两级合并）相同的 `enc_id`
- **边缘 case 增强**: M10 的 closest-primitive 方案在原始 6 射线全 miss 的场景下应能正确工作（T10.7），这是 M1 需要触发暴力 fallback 的 case
- **分类隔离**: `PATH_ENC_LOCATE_PENDING` 不应出现在 `advance_one_step_no_ray` 或 `advance_one_step_with_ray` 中——它走独立的 `collect_enc_locate` / `distribute_enc_locate` 通道
- **cascade 安全**: M10 完成后，`step_enc_query_resolve` 及其 fallback 调用 `scene_get_enclosure_id` 被完全移除，cascade 内不应再有任何光追调用
- **法线方向判断**: closest-primitive 返回的最近点 Q 和法线 N，`dot(P - Q, N)` 的符号决定 front/back 侧。当 P 恰在面上（`distance ≈ 0`）时，符号可能不确定——需要与 CPU 原始行为对齐（返回 `ENCLOSURE_ID_NULL`）

---

## 三、集成测试

### T-INT: 全路径类型混合场景

**测试文件**: `test_sdis_b4_integration.c`

**测试目标**: 在单个场景中同时激活所有路径类型，验证状态机完整性

**测试场景**: 完整的热模拟场景

```
几何:
  - 外层固体 shell (lambda_outer = 2.0)
  - 中间固体 core (lambda_inner = 1.0)
  - 流体填充 (h_conv = 20, T_fluid = 400K)
边界条件:
  - 外表面 Dirichlet: T = 300K
  - 内界面: solid/solid (激活 SS_REINJECT)
  - 中界面: solid/fluid (激活 SF/SFN, conv, ext_flux)
辐射:
  - emissivity = 0.6 (激活辐射路径)
外部源:
  - 点光源 (激活 EXT_* 状态)
参数:
  - picard_order = 2 (激活 picardN)
  - diff_algo = DELTA_SPHERE
```

**测试用例**:

| 编号 | 用例 | 方法 | 预期 |
|:---:|------|------|------|
| T-INT.1 | 状态覆盖率 | 统计运行中所有 `path_phase` 的出现次数 | ≥80% 的状态至少出现 1 次 | ⛔ 延迟 |
| T-INT.2 | 射线桶覆盖率 | 统计每个 `ray_bucket_type` 的射线数 | 4 种射线桶至少有射线（M10 后 `RAY_BUCKET_ENCLOSURE` = 0） | ⛔ 延迟 |
| T-INT.2a | Point-location 覆盖率 | 统计 `enc_locate_count` | enc_locate 请求数 > 0 | ⛔ 延迟 |
| T-INT.3 | 端到端对比 | 64×64 spp=256，persistent wavefront vs depth-first | ≥95% 像素统计兼容 | ⛔ 延迟 |
| T-INT.4 | 批量化覆盖率 | `total_batch_rays / total_all_rays` | >85%（目标 >90%） | ⛔ 延迟 |
| T-INT.5 | 性能不回退 | 比较 B-4 vs B-3 M3 的 wall-clock 时间 | B-4 ≤ 1.1×B-3 (允许 10% 开销增长, 桶排序和更多的射线带来的收益应覆盖) | ⛔ 延迟 |
| T-INT.6 | 内存泄漏检查 | `CHK(mem_allocated_size() == 0)` 在析构后 | 零泄漏 | ⛔ 延迟 |

---

## 四、回归测试矩阵

> **注意**: 受宽度-1 同步 GPU 光追未修复的约束，矩阵分为两部分：
> - **即时测试**（✅）：每个 Milestone 完成后必须通过，不依赖实际 GPU 光追
> - **延迟测试**（⛔）：仅在 M0–M10 全部完成后执行，依赖 `solve_camera` 端到端链路

### 4.1 即时测试矩阵（每个 Milestone 必做）

| 测试 | M0 | M1 | M2 | M3 | M4 | M5 | M6 | M7 | M8 | M9 | M10 |
|------|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:---:|
| 构建验证 (sdis+stardis 0错误) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| T0 (enum 回归: T0.1,T0.2,T0.5,T0.6) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| T1 (即时项: T1.1–T1.3,T1.5–T1.7) | — | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| T2 (即时项: T2.1–T2.3,T2.6) | — | — | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| T3 (即时项: T3.1–T3.5) | — | — | — | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| T4 (即时项: T4.1–T4.5) | — | — | — | — | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| T5 (即时项: T5.1–T5.3,T5.6) | — | — | — | — | — | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| T6 (即时项: T6.1,T6.2,T6.4–T6.6) | — | — | — | — | — | — | ✅ | ✅ | ✅ | ✅ | ✅ |
| T7 (即时项: T7.1,T7.4,T7.5,T7.7,T7.8,T7.10) | — | — | — | — | — | — | — | ✅ | ✅ | ✅ | ✅ |
| T8 (即时项: T8.1–T8.7,T8.9) | — | — | — | — | — | — | — | — | ✅ | ✅ | ✅ |
| T9 (即时项: T9.1–T9.4,T9.6) | — | — | — | — | — | — | — | — | — | ✅ | ✅ |
| T10 (即时项: T10.1–T10.3,T10.10,T10.13–T10.15) | — | — | — | — | — | — | — | — | — | — | ✅ |

**注意**: M10 完成后 T1 的 6 射线子状态被替代，T1 即时测试不再适用（列标记 "—"）。

### 4.2 延迟测试矩阵（M10 完成后统一执行）

| 测试 | 内容 | 前置条件 |
|------|------|----------|
| 现有 32 个 CTest (T0.4) | 全量回归 | M0–M10 全部完成，宽度-1 同步调用已被替代 |
| wavefront_benchmark (T0.3) | 基准回归 | 同上 |
| T1.4 批量 vs 同步 | 1000 点一致性 | M1+ |
| T2.4–T2.5 分桶端到端 | 分桶结果一致性 | M2+ |
| T3.6–T3.7 SS 端到端 | 温度 + 解析解 | M3+ |
| T4.6–T4.9 DS 端到端 | 温度 + 解析解 + 统计 | M4+ |
| T5.4–T5.5, T5.7–T5.9 Picard1 端到端 | 全场景统计 | M5+ |
| T6.3, T6.7–T6.8 对流端到端 | 全场景统计 | M6+ |
| T7.2–T7.3, T7.6, T7.9 EXT 端到端 | Shadow ray + 全场景 | M7+ |
| T8.8 PicardN 端到端 | 全场景统计 | M8+ |
| T9.5, T9.7–T9.8 WoS 端到端 | 全场景统计 | M9+ |
| T10.4–T10.9, T10.11–T10.12 Enc-Locate 端到端 | GPU kernel + 一致性 + 零宽度-1 | M10+ |
| T-INT (全部 7 项) | 集成测试 | M10 完成 |

### 4.3 延迟验证阶段（Post-M10 Gate）

> **触发条件**: M0–M10 全部完成，所有 `s3d_scene_view_trace_ray`（宽度-1 同步调用）
> 已被 `s3d_scene_view_trace_rays_batch_ctx`（批量射线）和 `s3d_scene_view_find_enclosure_batch`（批量点定位）完全替代。
>
> **预期效果**: `solve_camera` 端到端执行不再触发任何宽度-1 同步 GPU kernel，
> enclosure 查询走专用 closest-primitive kernel 而非射线追踪，
> 端到端测试恢复可行性能（与 B-3 M3 的 wavefront 路径相当或更快）。

**执行步骤**:

1. **烟雾测试**: 运行 `solve_camera` 单像素（1×1 spp=1），确认不 crash，
   验证 `STARDIS_WAVEFRONT=1` 路径无任何 `trace_ray` 调用（通过日志审查或 assert 检查）
2. **wavefront_benchmark 回归**: 运行完整基准测试（32×32 spp=64），确认统计兼容性
3. **现有 CTest 全量回归**: `ctest --output-on-failure`，所有 32 个已有测试 pass
4. **按 §4.2 矩阵逐项执行所有延迟测试**: 从 T1.4 开始，按 Milestone 顺序运行
5. **T-INT 集成测试**: 最后执行全路径类型混合场景，确认 ≥80% 状态覆盖 + ≥95% 像素兼容
6. **性能基准**: 与 B-3 M3 对比 wall-clock 时间，确认 B-4 ≤ 1.1×B-3

**通过标准**: §4.2 中所有延迟测试全部 pass → Phase B-4 验收完成

---

## 五、诊断与调试支持

### 5.1 每轮诊断日志格式

每个 wavefront 主循环迭代应输出（M3 已有框架，B-4 扩展）：

```
[WF step %zu] active=%zu rays=%zu (rad=%zu ds=%zu shd=%zu start=%zu) enc_locate=%zu batch_time=%.3fms
  buckets: RAD=%zu STEP_PAIR=%zu SHADOW=%zu STARTUP=%zu  locate=%zu
  cascade: steps=%zu max_chain=%zu
  states:  RAD_TRACE=%zu BND_DISPATCH=%zu SS_REINJECT=%zu SF_*=%zu CND_DS=%zu CNV=%zu ENC=%zu EXT=%zu
```

### 5.2 路径跟踪模式

环境变量 `STARDIS_TRACE_PATH=<path_id>` 启用单路径详细跟踪：

```
[PATH 1234] step 0: INIT → RAD_TRACE_PENDING (ray: origin=[0.5,0.5,0.5] dir=[0.3,-0.7,0.6])
[PATH 1234] step 1: RAD_PROCESS_HIT (hit: prim=42 dist=1.23 side=FRONT) → BND_DISPATCH
[PATH 1234] step 2: BND_DISPATCH → SF_REINJECT_SAMPLE (solid/fluid picard1)
[PATH 1234] step 3: SF_REINJECT_SAMPLE → ENC_LOCATE_PENDING (miss on ray 1)
[PATH 1234] step 4: ENC_LOCATE_RESULT → SF_REINJECT_SAMPLE (enc_id=3)
...
```

### 5.3 状态覆盖率统计

运行结束时输出每个 `path_phase` 的命中次数：

```
[State Coverage]
  PATH_RAD_TRACE_PENDING:      142,857  (32.1%)
  PATH_CND_DS_STEP_TRACE:       89,231  (20.1%)
  PATH_ENC_LOCATE_PENDING:      67,444  (15.2%)
  PATH_BND_SS_REINJECT_SAMPLE:  34,122   (7.7%)
  PATH_BND_SF_NULLCOLL_RAD:     23,891   (5.4%)
  ...
  PATH_CND_CUSTOM:                   0   (0.0%)  ← 未覆盖
```

---

## 六、性能基准

### 6.1 基准场景定义

| 场景 | 几何复杂度 | 物理特征 | 预期热路径 |
|------|-----------|---------|-----------|
| **A: 纯辐射** | 单 box + radenv | 仅辐射传输 | RAD_TRACE 主导 |
| **B: 纯导热** | 单 box, Dirichlet | 仅 delta-sphere | CND_DS 主导 |
| **C: 耦合** | 双层 box + 流体 | 辐射+导热+对流 | 混合状态 |
| **D: 高反射** | 单 box, ε=0.01 | 大量辐射弹跳 | RAD_TRACE 长链 |
| **E: 深递归** | 耦合, picard=3 | picardN 递归 | SFN 栈深 |
| **F: 边缘退化** | 极薄夹层 + 边角点 | closest-primitive 边缘 case | ENC_LOCATE 鲁棒性 |

### 6.2 度量指标

| 指标 | 单位 | 如何获取 |
|------|------|---------|
| 射线批量化覆盖率 | % | `batch_rays / all_rays` |
| 平均 batch size | rays/step | `total_batch_rays / total_steps` |
| Batch 规整度 | % | 最大桶射线数 / batch size |
| 每步 GPU 时间 | ms | `time_trace_s / total_steps × 1000` |
| 每步 CPU 开销 | ms | `(time_collect + time_distribute + time_cascade) / total_steps × 1000` |
| CPU/GPU 时间比 | ratio | CPU_time / GPU_time (目标 < 0.5) |
| 总吞吐量 | rays/sec | `total_batch_rays / wall_clock_s` |
| Point-location 吞吐量 | queries/sec | `total_enc_locate / wall_clock_s` |
| 宽度=1 trace_ray 残留数 | count/step | 目标 = 0（M10 后 enclosure 路径归零） |

---

## 七、CMake 集成

所有 B-4 测试文件应通过 `add_module_tests` 注册到 CMake：

```cmake
# Phase B-4 细粒度状态机测试
set(B4_TESTS
    test_sdis_b4_m0_enum_regression
    test_sdis_b4_m1_enclosure_batch
    test_sdis_b4_m2_ray_bucketing
    test_sdis_b4_m3_solid_solid
    test_sdis_b4_m4_delta_sphere
    test_sdis_b4_m5_picard1
    test_sdis_b4_m6_convective
    test_sdis_b4_m7_external_flux
    test_sdis_b4_m8_picardN
    test_sdis_b4_m9_wos
    test_sdis_b4_m10_enc_locate
    test_sdis_b4_integration
)

foreach(test_name IN LISTS B4_TESTS)
    add_executable(${test_name} ${test_name}.c test_sdis_utils.c)
    target_link_libraries(${test_name} PRIVATE sdis)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 600)
endforeach()
```

---

*文档更新: 2026-02-14 — Phase B-4 测试设计 (含 T10 point-in-enclosure kernel)*
