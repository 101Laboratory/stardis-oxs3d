# P2 级 GPU Wavefront 数值正确性测试 — 详细设计

**生成时间**: 2026-02-06 (v1)  
**关联文档**:
- [numerical_correctness_test_checklist.md](numerical_correctness_test_checklist.md)（总清单）
- [wf_p0_test_design.md](wf_p0_test_design.md)（P0 级测试设计，本文参照其格式）
- [phase_b4_fine_grained_state_machine.md](phase_b4_fine_grained_state_machine.md)（状态机设计）
- [wf_numerical_tests/](wf_numerical_tests/)（分类物理原理）

**范围**: 5 个 P2 级测试(WF-E1, WF-E2, WF-E3, WF-G1, WF-A7)的 Wavefront 适配详细方案  
**阻塞关系**: P0、P1 全部通过是 P2 的前置条件；P2 全部通过是 P3 的前置条件

---

## 一、核心设计决策

### 1.1 P2 测试定位

P2 级测试与 P0 共享同一验证框架（`sdis_solve_wavefront_probe` 公共探针接口 + `test_sdis_wf_p0_utils.h` 工具函数），但侧重于更复杂的物理场景：

| 维度 | P0 | P2 |
|------|-----|-----|
| **时间** | 稳态/简单稳态 | **瞬态 (Green 函数时间演化)** |
| **几何** | 单位立方体 (12 三角形) | 单位立方体 + **超形状 (supershape)** + **复合几何** |
| **边界条件** | Dirichlet/Neumann/Robin | 多面异温 Dirichlet + **绝热** + **解析时空耦合** |
| **拓扑** | 单连通域 | 包含 **solid-solid 透明界面** (球嵌入体) |
| **鲁棒性** | 标准凸几何 | **非凸超形状** + 自适应 delta |

### 1.2 公共 API 与 P0 完全一致

P2 复用 P0 已建立的 `sdis_solve_wavefront_probe(scn, &args, &est)` 接口，无需新增 API。相比 P0，P2 区别在于：

1. **`args.time_range`**: P0 使用 `{DBL_MAX, DBL_MAX}`（稳态）或默认值；P2 中 WF-E1/E2/E3 使用有限时间点
2. **`solid_shader.t0`**: WF-E3 使用 `t0 = -INF`（无初始条件，完全边界驱动）
3. **几何创建**: WF-E3/G1/A7 通过 `s3dut` 库创建超形状 / 球体几何
4. **sdis_data**: WF-G1/A7 使用 `sdis_data` 承载动态参数（温度剖面切换/界面温度）

### 1.3 验证协议

与 P0 相同的**双层验证协议**：

| 层级 | 比较方式 | 决定权 |
|------|---------|--------|
| **Primary** | wavefront vs **闭式解析值** | ✅ **决定 PASS/FAIL** |
| Diagnostic | wavefront vs depth-first (可选) | ❌ 仅日志输出 |

**解析值为唯一权威基准** — 任何实现（CPU/GPU）的数值偏差相对解析值独立评判。

### 1.4 共享测试基础设施

P2 测试复用 P0 的共享头文件和工具函数:

| 头文件 | 提供 |
|--------|------|
| `test_sdis_utils.h` | `CHK`/`OK` 宏、`box_get_indices`/`box_get_position`/`box_get_interface` 回调、`DUMMY_SOLID_SHADER`/`DUMMY_FLUID_SHADER`/`DUMMY_INTERFACE_SHADER` 默认 shader、`eq_eps` 浮点比较、`dummy_medium_getter` |
| `test_sdis_wf_p0_utils.h` | `p0_compare_analytic`（Primary 比较）、`p0_diag_compare`（Diagnostic 比较，可选）、`P0_NREALISATIONS`/`P0_TOL_SIGMA`/`P0_PASS_RATE` 共享常量 |
| `<star/s3dut.h>` | `s3dut_create_super_shape`/`s3dut_create_sphere`（WF-E3/G1/A7 使用） |

---

## 二、测试配置详情

### 2.1 P2 测试列表与分组

| 测试 ID | 测试文件 | CPU 对标 | 几何 | 链接库 | CMake 组 |
|---------|---------|---------|------|--------|----------|
| WF-E1 | `test_sdis_wf_e1_unsteady.c` | `test_sdis_unsteady.c` | 单位立方体 (12 tri) | `sdis_obj` | `WF_P2_TESTS_BASIC` |
| WF-E2 | `test_sdis_wf_e2_unsteady_1d.c` | `test_sdis_unsteady_1d.c` | 单位立方体 (12 tri) | `sdis_obj` | `WF_P2_TESTS_BASIC` |
| WF-E3 | `test_sdis_wf_e3_unsteady_analytic.c` | `test_sdis_unsteady_analytic_profile.c` | 超形状 (256×128 slices) | `sdis_obj s3dut` | `WF_P2_TESTS_S3DUT` |
| WF-G1 | `test_sdis_wf_g1_robustness.c` | `test_sdis_solid_random_walk_robustness.c` | 超形状 (128×64 slices) | `sdis_obj s3dut` | `WF_P2_TESTS_S3DUT` |
| WF-A7 | `test_sdis_wf_a7_solve_probe3.c` | `test_sdis_solve_probe3.c` | 立方体 + 球体 (12+sphere tri) | `sdis_obj s3dut` | `WF_P2_TESTS_S3DUT` |

### 2.2 共享参数表

| 参数 | WF-E1 | WF-E2 | WF-E3 | WF-G1 | WF-A7 |
|------|-------|-------|-------|-------|-------|
| `nrealisations` | 10000 | 10000 | 100000 | 10000 | 10000 |
| `diff_algo` | delta_sphere | delta_sphere | delta_sphere | delta_sphere | 默认 |
| `picard_order` | 默认 | 默认 | 默认 | 默认 | 默认 |
| `time_range` | 有限时间点 ×9 | 有限时间点 ×9 | {5.0, 5.0} | {INF, INF} | {INF, INF} |
| Primary σ 容差 | **4.0** | 3.0 | 3.0 | 3.0 | 3.0 |
| 通过率阈值 | **90%** | 95% | 100% (1探针) | 2/2 子测试 | 100% (1探针) |
| TIMEOUT | 600s | 600s | 600s | 600s | 600s |

---

## 三、WF-E1：3D 瞬态导热（六面异温）

### 3.1 对标 CPU 测试

**CPU**: `test_sdis_unsteady.c` → `sdis_solve_probe(pos={0.3,0.4,0.6}, time=t)` → Green 函数级数解

**GPU**: `test_sdis_wf_e1_unsteady.c` → `sdis_solve_wavefront_probe(scn, &args, &est)` → 9 个时间点逐探针求解

**差异**:
- CPU 使用 `sdis_solve_probe`（depth-first），GPU 使用 `sdis_solve_wavefront_probe`（persistent wavefront）
- CPU 测试 3D 几何，GPU 测试完全复刻 3D 几何（单位立方体，`fp_to_meter=0.1`）
- 参考数据完全相同（Green 函数级数展开硬编码值）

### 3.2 物理场景

```
          T3=310K (+Y)
         . . . .
  T0=310K ├──────────┤ T1=320K
  (-X)    │          │  (+X)
          │  固体    │
          │  T₀=280K │
          │  (t≤0)   │
  T5=300K ├──────────┤ T4=320K
  (-Z)    T2=330K    (+Z)
          (-Y)

  单位立方体 (0,0,0)-(1,1,1), fp_to_meter=0.1
  λ=0.5, ρ=2500, Cp=2000, δ=1/60
```

6 面各有不同固定温度，固体 $t \leq 0$ 时返回初温 $T_0 = 280$K，$t > 0$ 时返回 `SDIS_TEMPERATURE_NONE`。

### 3.3 解析解

3D 热传导方程的 Green 函数级数解（无闭式公式，数值求和收敛值）。

$$T(\mathbf{r}, t) = T_\infty + \sum_{l,m,n=1}^{\infty} A_{lmn} \sin\!\left(\frac{l\pi x}{L}\right) \sin\!\left(\frac{m\pi y}{L}\right) \sin\!\left(\frac{n\pi z}{L}\right) e^{-\alpha(l^2+m^2+n^2)\pi^2 t / L^2}$$

其中热扩散率 $\alpha = \lambda / (\rho c_p) = 0.5/(2500 \times 2000) = 10^{-7}$ m²/s，$L = 0.1$m。

参考值（探针位置 $(0.3, 0.4, 0.6)$ fp 单位）：

| 时间 [s] | 温度 [K] |
|----------|---------|
| 1000 | 281.33455593977152 |
| 2000 | 286.90151817350699 |
| 3000 | 292.84330866161531 |
| 4000 | 297.81444160746452 |
| 5000 | 301.70787295764546 |
| 10000 | 310.78920179442139 |
| 20000 | 313.37629443163121 |
| 30000 | 313.51064004438581 |
| 1000000 | 313.51797642855502 |

### 3.4 Wavefront 场景构建

#### 几何与材料

```c
/* 固体: unknown T (t>0), Tinit=280K (t<=0) */
/* λ=0.5, ρ=2500, Cp=2000, δ=1/60 */
solid_shader.calorific_capacity = e1_solid_get_calorific_capacity; /* 2000 */
solid_shader.thermal_conductivity = e1_solid_get_thermal_conductivity; /* 0.5 */
solid_shader.volumic_mass = e1_solid_get_volumic_mass; /* 2500 */
solid_shader.delta = e1_solid_get_delta; /* 1/60 */
solid_shader.temperature = e1_solid_get_temperature; /* t<=0:280, else:NONE */

/* 场景: box geometry, fp_to_meter = 0.1 */
scn_args.fp_to_meter = 0.1;
```

#### 接口分配

使用单一 interface，通过 shader 回调中的法线方向判断面温度：

| 法线 | 对应面 | 温度 [K] |
|------|--------|---------|
| `Ng[0] == +1` | -X 面 | T0 = 310 |
| `Ng[0] == -1` | +X 面 | T1 = 320 |
| `Ng[1] == +1` | -Y 面 | T2 = 330 |
| `Ng[1] == -1` | +Y 面 | T3 = 310 |
| `Ng[2] == +1` | -Z 面 | T4 = 320 |
| `Ng[2] == -1` | +Z 面 | T5 = 300 |

```c
static double
e1_interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  (void)data; CHK(frag != NULL);
       if(frag->Ng[0] ==  1) return 310.0;  /* -X */
  else if(frag->Ng[0] == -1) return 320.0;  /* +X */
  else if(frag->Ng[1] ==  1) return 330.0;  /* -Y */
  else if(frag->Ng[1] == -1) return 310.0;  /* +Y */
  else if(frag->Ng[2] ==  1) return 320.0;  /* -Z */
  else if(frag->Ng[2] == -1) return 300.0;  /* +Z */
  else { CHK(0 && "Unreachable"); return 0; }
}
```

**所有 12 个三角形共享同一 interface 实例**（仅 front/back temperature 回调不同于绝热面——此处全部面都有固定温度，无绝热面）。

### 3.5 探针布局与求解

```c
/* 单一探针位置, 9 个时间点 */
static const double e1_probe_pos[3] = {0.3, 0.4, 0.6};

for(i = 0; i < 9; i++) {
    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    args.nrealisations = 10000;
    args.position[0] = 0.3; args.position[1] = 0.4; args.position[2] = 0.6;
    args.time_range[0] = e1_refs[i].time;
    args.time_range[1] = e1_refs[i].time;
    args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

    OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

    /* Primary: wavefront vs Green 函数解析值 (4σ, 宽松) */
    pass = p0_compare_analytic(est_wf, e1_refs[i].temp, 4.0 /* E1_TOL_SIGMA */);
    n_pass += pass;

    OK(sdis_estimator_ref_put(est_wf));
}

/* 通过率: >= 90% */
CHK((double)n_pass / 9.0 >= 0.90);
```

### 3.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    ├── [t <= 0: 初温已知] → PATH_DONE (T = 280K — 拒绝, 因为 time > 0)
    └── [t > 0: NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    └── [Dirichlet 面] → PATH_DONE (T = face temperature)
```

**验证重点**:
- **瞬态时间积分**: random walk 在有限时间 $t$ 内的 delta-sphere 步进与初温回退
- **初温条件**: `solid_get_temperature(vtx)` 在 $t \leq 0$ 时返回 280K，$t > 0$ 时返回 NONE
- **多面异温 Dirichlet**: 6 面各有不同温度，random walk 达到不同面贡献不同温度

### 3.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥90% 探针（≥8/9） `|T.E - T_ref|` ≤ `4 × T.SE` | **PASS/FAIL** |

**σ 容差放宽至 4.0 的原因**: 瞬态问题的 MC 方差比稳态更高，早期时间点（$t=1000$s）温度变化敏感，统计波动显著。通过率阈值从 95% 放宽至 90%。

### 3.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 全部探针 T=280K | 瞬态时间积分未启动，始终返回初温 |
| 全部探针 T=稳态极限值 | `time_range` 未正确传递，等效于 $t=\infty$ |
| 早期时间点 FAIL，晚期 PASS | delta-sphere 在短时间步内精度不足，δ 偏大 |
| 接近稳态但偏移 | 面温度分配错误（法线→温度映射不一致） |
| SE 异常大 | `nrealisations` 不足或 delta-sphere 参数不当 |

---

## 四、WF-E2：1D 瞬态导热（两面异温 + 四面绝热）

### 4.1 对标 CPU 测试

**CPU**: `test_sdis_unsteady_1d.c` → 2D 正方形，两面固定温度 + 两面绝热，单点多时间  
**GPU**: `test_sdis_wf_e2_unsteady_1d.c` → 3D 单位立方体，两 X 面固定 + 四面绝热，中心点多时间

**差异**:
- CPU 使用 2D 几何（正方形 4 边），GPU 升级为 3D 几何（立方体 12 三角形）
- 物理等价：4 面绝热使得温度仅沿 X 方向变化，等效 1D 问题
- 参考值相同（1D Green 函数级数展开在 $x=0.5$ 处的值）

### 4.2 物理场景

```
  T0=310K ├──────────────── ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ ─────────────────┤ T1=320K
  (-X)    │   固体: Tinit=280K (t≤0)                           │  (+X)
  x=0     │   λ=0.5, ρ=2500, Cp=2000, δ=1/80                  │  x=1
          │              ← 绝热 (4面: ±Y, ±Z) →                │
          └─────────────────────────────────────────────────────┘

  单位立方体 (0,0,0)-(1,1,1), fp_to_meter=0.1
  稳态极限: T(0.5) = (310+320)/2 = 315K
```

### 4.3 解析解

1D 热传导方程 Green 函数级数解：

$$T(x,t) = T_0 + (T_1 - T_0)x + \sum_{n=1}^{\infty} \left[A_n \sin(n\pi x) \right] e^{-\alpha n^2 \pi^2 t / L^2}$$

其中 $A_n$ 由初始条件 $T(x,0) = T_\mathrm{init} = 280$K 和边界条件 $T(0)=T_0=310$K，$T(1)=T_1=320$K 确定。

$\alpha = \lambda / (\rho c_p) = 0.5 / (2500 \times 2000) = 10^{-7}$ m²/s，$L=0.1$m。

参考值（探针位置 $x=0.5$，即 $(0.5, 0.5, 0.5)$ fp 单位）：

| 时间 [s] | 温度 [K] |
|----------|---------|
| 1000 | 280.02848664122115 |
| 2000 | 280.86935314560424 |
| 3000 | 282.88587826961236 |
| 4000 | 285.39698306113996 |
| 5000 | 287.96909375994932 |
| 10000 | 298.39293888670881 |
| 20000 | 308.80965010883347 |
| 30000 | 312.69280796373141 |
| 1000000 | 315.00000000000000 |

**稳态极限**: $T_\infty = (T_0 + T_1) / 2 = 315$K（中点处对称）

### 4.4 Wavefront 场景构建

#### 几何与材料

```c
/* 固体: unknown T (t>0), Tinit=280K (t<=0) */
/* λ=0.5, ρ=2500, Cp=2000, δ=1/80 (比 E1 更细) */
#define E2_DELTA  (1.0 / 80.0)

solid_shader.temperature = e2_solid_get_temperature;
/* t<=0 → 280K, t>0 → SDIS_TEMPERATURE_NONE */
```

#### 接口分配

使用单一 interface，通过法线方向 X 分量区分 Dirichlet / 绝热：

| 法线 | 对应面 | 接口类型 | 温度 |
|------|--------|---------|------|
| `|Ng[0]| > 0.5`, `Ng[0] > 0` | +X (x=1) | Dirichlet | T1 = 320 |
| `|Ng[0]| > 0.5`, `Ng[0] < 0` | -X (x=0) | Dirichlet | T0 = 310 |
| 其他 | ±Y, ±Z | 绝热 | `SDIS_TEMPERATURE_NONE` |

```c
static double
e2_interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  (void)data; CHK(frag != NULL);
  if(fabs(frag->Ng[0]) > 0.5) {
    if(frag->Ng[0] > 0) return 320.0;  /* +X (x=1) => T1 */
    else                 return 310.0;  /* -X (x=0) => T0 */
  }
  return SDIS_TEMPERATURE_NONE;  /* adiabatic */
}
```

### 4.5 探针布局与求解

```c
/* 立方体中心 (0.5, 0.5, 0.5), 9 个时间点 */
static const double e2_probe_pos[3] = {0.5, 0.5, 0.5};

for(i = 0; i < 9; i++) {
    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    args.nrealisations = 10000;
    d3_set(args.position, e2_probe_pos);
    args.time_range[0] = e2_refs[i].time;
    args.time_range[1] = e2_refs[i].time;
    args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

    OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

    /* Primary: wavefront vs Green 函数解析值 (3σ) */
    pass = p0_compare_analytic(est_wf, e2_refs[i].temp, 3.0);
    n_pass += pass;

    OK(sdis_estimator_ref_put(est_wf));
}

/* 通过率: >= 95% (≥9/9 = 100% 或允许 1 个失败 → 89%... 实际上 9 个中需 ≥9*0.95≈9 个) */
CHK((double)n_pass / 9.0 >= 0.95);
```

### 4.6 覆盖的状态机路径

与 WF-E1 相同的瞬态导热路径，额外验证:

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [t > 0: NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    ├── [Dirichlet ±X 面] → PATH_DONE (T = T0 或 T1)
    └── [绝热 ±Y/±Z 面] → PATH_DONE (反射回固体)
```

**验证重点**:
- **绝热面反射**: random walk 到达 ±Y/±Z 面时反射回固体继续步进
- **1D 等效性**: 4 面绝热 → 温度沿 X 方向按 1D Green 函数演化

### 4.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥95% 探针（≥9/9） `|T.E - T_ref|` ≤ `3 × T.SE` | **PASS/FAIL** |

**δ=1/80 比 E1 的 1/60 更细的原因**: 1D 投影问题中仅 X 方向有效扩散，需要更细的步长确保路径不跨越 X 方向边界。

### 4.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 全部 T=280K | 初温时间判断错误，始终返回初温 |
| T=315K（稳态）所有时间点 | `time_range` 未传递到 wavefront solver |
| 非 X 方向温度梯度 | 绝热面配置错误，Y/Z 面也分配了温度 |
| 早期时间 FAIL | δ 偏大导致短时间步精度不足 |
| `SDIS_TEMPERATURE_NONE` 面返回 0 而非反射 | 绝热面的 interface temperature callback 返回值错误 |

---

## 五、WF-E3：超形状中的非稳态解析温度剖面

### 5.1 对标 CPU 测试

**CPU**: `test_sdis_unsteady_analytic_profile.c` → 超形状 + 解析温度公式，`sdis_solve_probe(pos, time=5)` × 100000 实现

**GPU**: `test_sdis_wf_e3_unsteady_analytic.c` → 相同超形状 + 相同解析公式，`sdis_solve_wavefront_probe` × 100000 实现

**差异**: 仅求解器引擎替换（depth-first → wavefront），所有物理参数完全相同。

### 5.2 物理场景

```
      T(z)             /\ <-- T(x,y,z,t) 解析温度场
       |  T(y)     ___/  \___
       |/          \  . T=? /     超形状: 非凸边界
       o--- T(x)   /_  __  _\    MC 仅知边界 T
                    \/  \/

  超形状参数:
    f0: A=1.5, B=1, M=11, N0=1, N1=1, N2=2
    f1: A=1,   B=2, M=3.6, N0=1, N1=2, N2=0.7
    radius=1, nslices=256, nstacks=128

  固体: λ=0.1, ρ=25, Cp=2, δ=1/20
  solid.t0 = -INF (无初始条件, 边界完全驱动)
```

### 5.3 解析解

温度场由调和函数 + 指数衰减模式组成：

$$T(\mathbf{r}, t) = \frac{1}{\lambda}\left[B_1(x^3 z - 3x y^2 z) + B_2 \sin(k_x x)\sin(k_y y)\sin(k_z z)\,e^{-\alpha(k_x^2+k_y^2+k_z^2)t}\right]$$

| 参数 | 值 | 说明 |
|------|------|------|
| $B_1$ | 10 | 调和分量系数 |
| $B_2$ | 1000 | 衰减分量系数 |
| $k_x = k_y = k_z$ | $\pi/4$ | 空间频率 |
| $\alpha$ | $\lambda/(\rho c_p) = 0.1/50 = 0.002$ | 热扩散率 [m²/s] |
| $\lambda$ | 0.1 | 热导率 |

**探针位置**: $(0.2, 0.3, 0.4)$，$t = 5$s

参考温度:

$$T_\mathrm{ref} = \frac{1}{0.1}\left[10(0.2^3 \times 0.4 - 3 \times 0.2 \times 0.3^2 \times 0.4) + 1000 \sin\!\left(\frac{0.2\pi}{4}\right)\sin\!\left(\frac{0.3\pi}{4}\right)\sin\!\left(\frac{0.4\pi}{4}\right)e^{-0.002 \times 3(\pi/4)^2 \times 5}\right]$$

（具体数值由代码中 `e3_temperature(pos, time)` 函数计算）

### 5.4 Wavefront 场景构建

#### 超形状几何

```c
static struct s3dut_mesh*
e3_create_super_shape(void)
{
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;

  f0.A = 1.5; f0.B = 1; f0.M = 11.0; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2.0;
  f1.A = 1.0; f1.B = 2; f1.M =  3.6; f1.N0 = 1; f1.N1 = 2; f1.N2 = 0.7;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, 1, 256, 128, &mesh));
  return mesh;
}
```

**翻转绕序**: 三角形索引 `ids[1] ↔ ids[2]` 以使法线指向超形状内部（同 CPU 测试）。

#### 材料

```c
/* 固体: T = SDIS_TEMPERATURE_NONE (始终未知) */
solid_shader.t0 = -INF;  /* 无初始条件: MC 不会回退到 t<=t0 */

/* 边界: T = 解析公式 e3_temperature(frag->P, frag->time) */
interf_shader.front.temperature = e3_interf_get_temperature;
interf_shader.back.temperature  = e3_interf_get_temperature;
```

**`t0 = -INF` 的含义**: solid 没有初始时间条件，MC 路径的时间积分无下限。路径在有限时间 delta-sphere 步进后到达边界时终止，边界处温度由解析公式在当前路径时间给出。

### 5.5 探针布局与求解

```c
const double pos[3] = {0.2, 0.3, 0.4};
const double time = 5.0;

struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
args.nrealisations = 100000;   /* 与 CPU 相同 */
d3_set(args.position, pos);
args.time_range[0] = time;
args.time_range[1] = time;
args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

/* Primary: wavefront vs 解析解 (3σ) */
double ref = e3_temperature(pos, time);
CHK(p0_compare_analytic(est_wf, ref, 3.0));
```

**100000 实现数的原因**: 超形状的非凸边界导致 random walk 路径更长、方差更高，需要高实现数降低统计误差。

### 5.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [NONE, t0=-INF 无初温终止] → PATH_CND_DS_STEP_TRACE
        → PATH_CND_DS_STEP_PROCESS → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    └── [Dirichlet 面: T=analytic(P,time)] → PATH_DONE
```

**验证重点**:
- **`t0 = -INF` 无初始条件模式**: MC 路径无时间下限，只能通过达到边界终止
- **非凸几何上的 delta-sphere**: 步进球可能与超形状表面自相交，考验光追鲁棒性
- **时空耦合边界温度**: 边界温度同时依赖位置和时间

### 5.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | `|T.E - T_ref|` ≤ `3 × T.SE` | **PASS/FAIL** |

单探针测试，Primary 必须通过。

### 5.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| T = 0 或 NaN | 超形状几何未正确创建或 mesh 数据为空 |
| T 远离参考值 | 法线翻转方向错误（`ids[1]↔ids[2]` 未执行） |
| SE 非常大 | 超形状网格分辨率不足 (nslices < 256) |
| 路径超时 | `t0 = -INF` 下路径无限回退（不应发生，因为边界终止） |
| 边界温度未使用 frag->time | `e3_interf_get_temperature` 忽略了时间参数 |

---

## 六、WF-G1：非凸超形状鲁棒性测试

### 6.1 对标 CPU 测试

**CPU**: `test_sdis_solid_random_walk_robustness.c` →
- Sub-test 1: 三线性温度剖面 + delta-sphere
- Sub-test 2: 体积功率 + delta-sphere

CPU 测试包含更多子测试（4 种 profile × 2 种 algorithm = 8 种组合），GPU 版精简为 2 种最核心组合。

**GPU**: `test_sdis_wf_g1_robustness.c` → 2 个子测试（trilinear / volumetric_power），仅 delta-sphere

### 6.2 物理场景

```
       ___/  \___      超形状: 严重非凸
      /  .  T=?  \     f0: A=1, B=1, M=20, N0=1, N1=1, N2=5
     /_   __   __\     f1: A=1, B=1, M=7,  N0=1, N1=2, N2=5
      \/  \/  \/       radius=1, 128×64 slices
                       包围盒 ~[-1,1]^3

  子测试 1 (trilinear): 边界 T(P) = 333x' + 432y' + 579z'
                         x' = (x+10)/20, y' = (y+10)/20, z' = (z+10)/20
                         λ=10, 无体积功率

  子测试 2 (volumetric_power): Pw=10000, 边界 T 由体积功率解析解给出
                                T(P) = β(Px²-upper_x²) + ..., β = -Pw/(6λ)
```

### 6.3 解析解

#### 子测试 1: 三线性温度场

$$T(\mathbf{r}) = 333 \cdot \frac{x+10}{20} + 432 \cdot \frac{y+10}{20} + 579 \cdot \frac{z+10}{20}$$

三线性函数是调和函数（$\nabla^2 T = 0$），满足 Laplace 方程。因此 MC 求解的内部温度必须精确等于边界 profile（对于线性/三线性场，MC 求解是精确的）。

探针位置 $(0, 0, 0)$:

$$T_\mathrm{ref} = 333 \times 0.5 + 432 \times 0.5 + 579 \times 0.5 = 672.0$$

#### 子测试 2: 体积功率解析温度

稳态 Poisson 方程 $-\lambda \nabla^2 T = P_w$ 在球形（近似）域内的解：

$$T(\mathbf{r}) = \beta(x^2 - x_{\max}^2) + \beta(y^2 - y_{\max}^2) + \beta(z^2 - z_{\max}^2)$$

$$\beta = -\frac{1}{3} \cdot \frac{P_w}{2\lambda} = -\frac{10000}{60} \approx -166.67$$

边界温度由公式在 `upper = AABB 上界` 处取 0 确定。

探针位置 $(0, 0, 0)$:

$$T_\mathrm{ref} = -166.67(-x_{\max}^2 - y_{\max}^2 - z_{\max}^2) = 166.67(x_{\max}^2 + y_{\max}^2 + z_{\max}^2)$$

（具体值取决于超形状 AABB 上界 `upper`，由 `sdis_scene_get_aabb` 运行时获取）

### 6.4 Wavefront 场景构建

#### 超形状几何

```c
f0.A = 1; f0.B = 1; f0.M = 20; f0.N0 = 1; f0.N1 = 1; f0.N2 = 5;
f1.A = 1; f1.B = 1; f1.M = 7;  f1.N0 = 1; f1.N1 = 2; f1.N2 = 5;
OK(s3dut_create_super_shape(NULL, &f0, &f1, 1, 128, 64, &msh));
```

**翻转绕序**: 同 E3，`ids[1] ↔ ids[2]`。

#### 材料与数据

使用 `sdis_data` 动态绑定固体/界面参数，允许同一场景在两个子测试间切换 profile：

```c
/* solid_data: 通过 sdis_data 承载 δ, λ, power 等 */
struct solid_data { double delta, cp, lambda, rho, temperature, power; };

/* interf_data: 承载 profile 类型 + AABB 上界 */
struct interf_data { enum profile profile; double upper[3]; double h; };
```

#### 自适应 delta

```c
OK(sdis_scene_get_medium_spread(scn, solid, &spread));
sp->delta = 0.4 / spread;
```

`medium_spread` 返回固体介质的几何特征尺度，delta 按 `0.4/spread` 自适应设置。

### 6.5 探针布局与求解

```c
/* === 子测试 1: trilinear === */
ip->profile = PROFILE_TRILINEAR;
sp->power = SDIS_VOLUMIC_POWER_NONE;

args.nrealisations = 10000;
args.position = {0.0, 0.0, 0.0};
args.time_range = {INF, INF};  /* 稳态 */
args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

OK(sdis_solve_wavefront_probe(scn, &args, &est));
Tref = trilinear_temperature(pos);  /* 672.0 */

/* 验证: failure_count <= 0.05% AND |T.E - Tref| <= 3*T.SE */
ok = (nfails <= G1_NREALS * 0.0005) && eq_eps(T.E, Tref, T.SE * 3.0);

/* === 子测试 2: volumetric power === */
ip->profile = PROFILE_VOLUMETRIC_POWER;
sp->power = 10000.0;

/* 相同探针位置和参数 */
Tref = volumetric_temperature(pos, upper);  /* 取决于 AABB */

ok = (nfails <= G1_NREALS * 0.0005) && eq_eps(T.E, Tref, T.SE * 3.0);
```

### 6.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    └── [Dirichlet: T=profile(P)] → PATH_DONE
```

**子测试 2 额外覆盖**:
- `solid_get_volumic_power` 回调在 DS 步进中正确累积 (P=10000)
- 非凸几何上的体积功率积分（步进球可能跨越凹面）

**验证重点**:
- **非凸几何鲁棒性**: M=20 的超形状具有 20 瓣花生形状，delta-sphere 步进可能与凹面产生多次相交
- **failure_count 监控**: 鲁棒性测试额外检查失败率 ≤ 0.05%
- **自适应 delta**: 步长由几何 spread 自适应确定

### 6.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary (sub1)** | `failure_count ≤ 0.05% × N` **AND** `eq_eps(T.E, 672.0, 3*T.SE)` | **PASS/FAIL** |
| **Primary (sub2)** | `failure_count ≤ 0.05% × N` **AND** `eq_eps(T.E, Tref_vol, 3*T.SE)` | **PASS/FAIL** |
| **总体** | 两个子测试均 PASS | **PASS/FAIL** |

### 6.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| `failure_count` 高 (>5) | 非凸几何上 delta-sphere 步进自相交或 BVH 遍历错误 |
| sub1 T≠672 但接近 | 三线性公式坐标变换错误（偏移/缩放） |
| sub2 T≠Tref_vol 但方向正确 | `volumic_power` 回调系数错误（β 计算） |
| sub2 T=0 | `sp->power` 未从 `NONE` 切换为 10000 |
| 两者均失败 | 场景构建错误（绕序翻转、mesh 数据为空） |
| delta 异常大 | `sdis_scene_get_medium_spread` 返回异常小的值 |

---

## 七、WF-A7：嵌入球体同材料稳态导热

### 7.1 对标 CPU 测试

**CPU**: `test_sdis_solve_probe3.c` → 立方体 + 中心球体（solid-solid 透明界面），前/后面 Dirichlet

**GPU**: `test_sdis_wf_a7_solve_probe3.c` → 相同几何 + 界面配置，`sdis_solve_wavefront_probe`

**差异**: 求解器引擎替换。CPU 验证球体不影响温度分布（同材料透明界面），GPU 验证 wavefront 路径在 solid-solid 界面上的正确穿越。

### 7.2 物理场景

```
                      (1,1,1)
       +----------------+
      /'     #  #      /|
     +----*--------*--+ |
     | ' #          # | |T1=350K
     | ' #    球    # | | (+Z)
 T0  | '  #  r=0.25#  | |
 300K| +.....#..#.....|.+
     |/               |/
     +----------------+
   (0,0,0)

  单位立方体 (0,0,0)-(1,1,1)
  球心 (0.5, 0.5, 0.5), 半径 0.25, 64×32 segments

  接口: solid-solid 透明 (球面) + solid-fluid (box 面)
  固体: Cp=2, λ=50, ρ=25, δ=1/20, T=NONE

  Front (-Z): T=300K,  Back (+Z): T=350K,  others: 绝热
```

### 7.3 解析解

球体与立方体使用**完全相同的材料属性**，solid-solid 界面为透明（null shader）。因此球体对温度分布无任何影响，等效于纯立方体 1D 导热：

$$T(z) = T_\mathrm{front} \cdot (1 - z) + T_\mathrm{back} \cdot z = 300(1-z) + 350z$$

探针位置 $(0.5, 0.5, 0.5)$:

$$T_\mathrm{ref} = 300 \times 0.5 + 350 \times 0.5 = 325.0 \text{ K}$$

### 7.4 Wavefront 场景构建

#### 复合几何

使用 `stretchy_array` 合并 box 顶点/三角形 + sphere 顶点/三角形：

```c
/* Box: 8 vertices, 12 triangles (from test_sdis_utils.h) */
/* Sphere: s3dut_create_sphere(r=0.25, 64, 32) → ~4032 triangles */

/* 合并顶点 (sphere 偏移到中心) */
FOR_EACH(i, 0, msh_data.nvertices) {
    sa_push(ctx.positions, msh_data.positions[i*3+0] + 0.5);  /* 球心偏移 */
    sa_push(ctx.positions, msh_data.positions[i*3+1] + 0.5);
    sa_push(ctx.positions, msh_data.positions[i*3+2] + 0.5);
}

/* 合并三角形索引 (sphere 索引偏移 box_nvertices) */
FOR_EACH(i, 0, msh_data.nprimitives) {
    sa_push(ctx.indices, msh_data.indices[i*3+0] + box_nvertices);
    /* ... */
}
```

#### 接口分配（4 种接口）

| 三角形范围 | 面 | 接口类型 | 参数 |
|-----------|-----|---------|------|
| 0-1 | Box Front (-Z) | solid/fluid Dirichlet | T = 300K |
| 4-5 | Box Back (+Z) | solid/fluid Dirichlet | T = 350K |
| 2-3, 6-11 | Box 其余面 | solid/fluid 绝热 | T = NONE, hc = 0 |
| ≥ 12 | 球面 | **solid/solid** 透明 | null shader |

```c
static void
a7_get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct a7_context* ctx = context;
  if(itri == 0 || itri == 1)       *bound = ctx->solid_fluid_T300;   /* front */
  else if(itri == 4 || itri == 5)  *bound = ctx->solid_fluid_T350;   /* back */
  else if(itri < box_ntriangles)   *bound = ctx->solid_fluid_Tnone;  /* rest */
  else                             *bound = ctx->solid_solid;         /* sphere */
}
```

**solid-solid 界面创建**:

```c
/* solid-solid: 两侧都是同一个 solid，null shader → 透明 */
interf_shader = SDIS_INTERFACE_SHADER_NULL;
OK(sdis_interface_create(dev, solid, solid, &interf_shader, NULL, &solid_solid));
```

### 7.5 探针布局与求解

```c
/* 单探针: 立方体中心 = 球心 */
solve_args.nrealisations = 10000;
solve_args.position[0] = 0.5;
solve_args.position[1] = 0.5;
solve_args.position[2] = 0.5;
solve_args.time_range[0] = INF;  /* 稳态 */
solve_args.time_range[1] = INF;

OK(sdis_solve_wavefront_probe(scn, &solve_args, &est_wf));

/* 解析参考: T(z=0.5) = 325K */
ref = 350.0 * 0.5 + (1.0 - 0.5) * 300.0;  /* 325.0 */

pass = (nfails < A7_NREALS / 1000)                    /* failure_count < 0.1% */
    && p0_compare_analytic(est_wf, ref, A7_TOL_SIGMA); /* |T.E - 325| <= 3*SE */

CHK(pass);
```

### 7.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    ├── [solid/fluid Dirichlet: T=300 或 T=350] → PATH_DONE
    ├── [solid/fluid 绝热] → PATH_DONE (反射回固体)
    └── [solid/solid 透明] → PATH_BND_SS_REINJECT_*
        → PATH_CND_DS_CHECK_TEMP (继续固体导热步进)
```

**验证重点**:
- **solid-solid 透明界面穿越**: random walk 到达球面时应无缝穿越（同材料无温度跳变），继续在固体中步进
- **failure_count**: 球面额外三角形增加光追复杂度，监控失败率 < 0.1%
- **温度分布不受球体影响**: 球面虽增加界面但不改变温度场

### 7.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | `nfails < N/1000` **AND** `|T.E - 325|` ≤ `3 × T.SE` | **PASS/FAIL** |

### 7.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| T = 300K 或 350K（极端值） | 球面界面阻止了 random walk 穿越 |
| `failure_count` 高 | 球面三角形翻转或 BVH 构建问题 |
| T ≠ 325 但接近 | solid-solid 界面温度不连续（界面 shader 返回了非零温度） |
| T = NaN | 球面网格数据为空或 stretchy_array 越界 |
| `nfails + nreals ≠ N` | estimator 计数逻辑错误 |

---

## 八、测试文件结构与 CMake 注册

### 8.1 文件命名

```
stardis-cus3d/stardis-solver/0.16.2/src/
├── test_sdis_wf_p0_utils.h              ← P0/P2 共用工具
├── test_sdis_wf_e1_unsteady.c           ← WF-E1 (P2)
├── test_sdis_wf_e2_unsteady_1d.c        ← WF-E2 (P2)
├── test_sdis_wf_e3_unsteady_analytic.c  ← WF-E3 (P2, s3dut)
├── test_sdis_wf_g1_robustness.c         ← WF-G1 (P2, s3dut)
└── test_sdis_wf_a7_solve_probe3.c       ← WF-A7 (P2, s3dut)
```

### 8.2 CMake 注册

```cmake
# ---- P2 wavefront numerical correctness tests ----
# Basic tests: link sdis_obj only (box geometry from test_sdis_utils.h)
set(WF_P2_TESTS_BASIC
    test_sdis_wf_e1_unsteady
    test_sdis_wf_e2_unsteady_1d)

foreach(p2test ${WF_P2_TESTS_BASIC})
    add_executable(${p2test} src/${p2test}.c)
    target_link_libraries(${p2test} PRIVATE sdis_obj)
    add_test(NAME ${p2test} COMMAND ${p2test})
    set_tests_properties(${p2test} PROPERTIES
        TIMEOUT 600
        LABELS "wf;p2;numerical")
endforeach()

# S3DUT tests: link sdis_obj + s3dut (supershape / sphere geometry)
set(WF_P2_TESTS_S3DUT
    test_sdis_wf_e3_unsteady_analytic
    test_sdis_wf_g1_robustness
    test_sdis_wf_a7_solve_probe3)

foreach(p2test ${WF_P2_TESTS_S3DUT})
    add_executable(${p2test} src/${p2test}.c)
    target_link_libraries(${p2test} PRIVATE sdis_obj s3dut)
    add_test(NAME ${p2test} COMMAND ${p2test})
    set_tests_properties(${p2test} PROPERTIES
        TIMEOUT 600
        LABELS "wf;p2;numerical")
endforeach()
```

### 8.3 运行时依赖部署

```cmake
if(WIN32)
    foreach(p2test ${WF_P2_TESTS_BASIC})
        deploy_runtime_dependencies(${p2test})
    endforeach()
    foreach(p2test ${WF_P2_TESTS_S3DUT})
        deploy_runtime_dependencies(${p2test})
    endforeach()
endif()
```

---

## 九、实施顺序与依赖

```
┌──────────────────────────────────────────────────────────┐
│ 前置: P0 全部通过 (WF-A1, WF-A2, WF-B2, WF-C1, WF-D1)  │
│ 前置: P1 全部通过 (状态机调度正确性)                       │
│ 产出: wavefront probe API + 基础导热/辐射/对流路径        │
└──────────────────────────────────────────────────────────┘
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
┌───────────────────────┐  ┌──────────────────────┐
│ Step 1a: WF-E2 (最简)  │  │ Step 1b: WF-A7       │
│ 1D 瞬态 box + 绝热     │  │ box + sphere 稳态     │
│ 验证: 瞬态时间积分      │  │ 验证: solid-solid 界面 │
│ 仅依赖 P0 DS 路径       │  │ 仅依赖 P0 DS 路径    │
│ 可与 A7 并行开发        │  │ 可与 E2 并行开发      │
└───────────────────────┘  └──────────────────────┘
              │                       │
              └───────────┬───────────┘
                          ▼
┌──────────────────────────────────────────────────────────┐
│ Step 2: WF-E1 (3D 瞬态)                                  │
│   全 3D 六面异温瞬态，比 E2 更高方差                       │
│   前置: WF-E2 (瞬态时间积分 + 绝热基础验证)               │
└──────────────────────────────────────────────────────────┘
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
┌───────────────────────┐  ┌──────────────────────┐
│ Step 3a: WF-E3         │  │ Step 3b: WF-G1       │
│ 超形状瞬态 + t0=-INF   │  │ 非凸超形状鲁棒性     │
│ 需 s3dut 集成          │  │ 需 s3dut + sdis_data │
│ 前置: E1 (瞬态路径)    │  │ 可与 E3 并行开发     │
└───────────────────────┘  └──────────────────────┘
```

---

## 十、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 瞬态 MC 高方差 (E1/E2) | Primary 容差内仍频繁 FAIL | E1 使用 4σ + 90% 通过率；E2 使用 3σ + 95%；增加 nrealisations 可降低 SE |
| 超形状非凸自相交 (E3/G1) | `failure_count` 高，路径中止 | 监控 failure_count ≤ 0.05%；自适应 delta (0.4/spread) |
| sphere 三角形数量大 (A7) | BVH 构建/遍历开销 | 使用适中分辨率 (64×32)；TIMEOUT 设 600s |
| `t0 = -INF` 边界情况 (E3) | MC 路径无限回退 | 路径通过边界终止而非时间终止；delta-sphere 保证有限步数到达边界 |
| solid-solid 界面穿越 (A7) | 温度跳变或路径卡死 | null shader 配置确保透明；同材料两侧属性一致 |
| `sdis_data` 动态参数 (G1) | 运行时 profile 切换遗留状态 | 每个子测试前显式重设 `sp->power` 和 `ip->profile` |
| Green 函数参考值精度 | 级数截断导致参考值不精确 | 参考值使用高阶收敛（10⁶ 项），t=10⁶s 已验证趋于稳态极限 |

---

## 附录 A：P2 vs P0 测试特性对比矩阵

| 特性 | P0-A1 | P0-A2 | P0-B2 | P0-C1 | P0-D1 | **P2-E1** | **P2-E2** | **P2-E3** | **P2-G1** | **P2-A7** |
|:-----|:-----:|:-----:|:-----:|:-----:|:-----:|:---------:|:---------:|:---------:|:---------:|:---------:|
| 稳态 | ● | ● | ● | ● | ● | | | | ● | ● |
| 瞬态 | | | | | △ | ● | ● | ● | | |
| 初温条件 | | | | | △ | ● | ● | | | |
| `t0 = -INF` | | | | | | | | ● | | |
| box 几何 | ● | ● | ● | | ● | ● | ● | | | ● |
| 超形状几何 | | | | | | | | ● | ● | |
| 球体几何 | | | | | | | | | | ● |
| 非标准几何 | | | | ● | | | | | | |
| solid-solid 界面 | | | | | | | | | | ● |
| 绝热面 | ● | ● | ● | ● | | | ● | | | ● |
| 多面异温 | | | | | ● | ● | | | | |
| 体积功率 | | ● | | | | | | | ● | |
| Robin BC | | | ● | | | | | | | |
| 辐射耦合 | | | | ● | | | | | | |
| 对流路径 | | | ● | | ● | | | | | |
| sdis_data | | | | | | | | | ● | ● |
| failure_count | | | | | | | | | ● | ● |
| s3dut 依赖 | | | | | | | | ● | ● | ● |

---

## 附录 B：P2 测试状态机路径覆盖矩阵

| `path_phase` | WF-E1 | WF-E2 | WF-E3 | WF-G1 | WF-A7 |
|:-------------|:-----:|:-----:|:-----:|:-----:|:-----:|
| `PATH_INIT` | ● | ● | ● | ● | ● |
| `PATH_CND_INIT_ENC` | ● | ● | ● | ● | ● |
| `PATH_ENC_QUERY_EMIT` | ● | ● | ● | ● | ● |
| `PATH_CND_DS_CHECK_TEMP` | ● | ● | ● | ● | ● |
| `PATH_CND_DS_STEP_TRACE` | ● | ● | ● | ● | ● |
| `PATH_CND_DS_STEP_PROCESS` | ● | ● | ● | ● | ● |
| `PATH_CND_DS_STEP_ADVANCE` | ● | ● | ● | ● | ● |
| `PATH_BND_DISPATCH` | ● | ● | ● | ● | ● |
| `PATH_BND_SS_REINJECT_*` | | | | | ● |
| `PATH_DONE` | ● | ● | ● | ● | ● |

**覆盖**: 5 个 P2 测试共覆盖 ~10 个 `path_phase` 状态，集中于**导热 DS 路径** + **Dirichlet 边界终止**。新增覆盖的关键路径: `PATH_BND_SS_REINJECT_*`（solid-solid 界面穿越，仅 WF-A7）。

**与 P0 的互补**:
- P0 覆盖了对流 (`PATH_CNV_*`)、辐射 (`PATH_RAD_*`)、Robin BC (`PATH_BND_SF_*`)
- P2 覆盖了**瞬态时间积分**、**非凸几何鲁棒性**、**solid-solid 界面穿越**、**解析时空耦合边界**

---

## 附录 C：P2 解析参考值完整表

以下解析值来自物理方程的闭式解或高精度级数展开，与任何实现（CPU/GPU）无关。

### C.1 WF-E1：3D 瞬态导热 (Green 函数级数)

| PDE | 边界条件 | 初始条件 | 参考方法 |
|-----|----------|---------|---------|
| $\rho c_p \frac{\partial T}{\partial t} = \lambda \nabla^2 T$ | 六面 Dirichlet | $T(t \leq 0) = 280$K | Green 函数级数展开 (高阶收敛) |

| 参数 | 值 | 单位 |
|------|------|------|
| $T_\mathrm{init}$ | 280 | K |
| $T_0 \ldots T_5$ | 310, 320, 330, 310, 320, 300 | K |
| $\lambda$ | 0.5 | W/(m·K) |
| $\rho$ | 2500 | kg/m³ |
| $c_p$ | 2000 | J/(kg·K) |
| $\delta$ | 1/60 | — |
| fp\_to\_meter | 0.1 | m |

**参考值** (探针 (0.3, 0.4, 0.6)): 见 §3.3 表格

### C.2 WF-E2：1D 瞬态导热 (Green 函数级数)

| PDE | 边界条件 | 初始条件 | 参考方法 |
|-----|----------|---------|---------|
| $\rho c_p \frac{\partial T}{\partial t} = \lambda \frac{\partial^2 T}{\partial x^2}$ | $T(0)=310$, $T(1)=320$, 其余绝热 | $T(t \leq 0) = 280$K | 1D Green 函数级数 |

| 参数 | 值 | 单位 |
|------|------|------|
| $T_\mathrm{init}$ | 280 | K |
| $T_0$ | 310 | K |
| $T_1$ | 320 | K |
| $\lambda$ | 0.5 | W/(m·K) |
| $\rho$ | 2500 | kg/m³ |
| $c_p$ | 2000 | J/(kg·K) |
| $\delta$ | 1/80 | — |
| fp\_to\_meter | 0.1 | m |

**参考值** (探针 x=0.5): 见 §4.3 表格
**稳态极限**: $T_\infty = (310+320)/2 = 315.0$K

### C.3 WF-E3：非稳态解析温度场 (调和函数 + 指数衰减)

| PDE | 边界条件 | 初始条件 | 参考方法 |
|-----|----------|---------|---------|
| $\rho c_p \frac{\partial T}{\partial t} = \lambda \nabla^2 T$ | $T = $ 解析公式 | `t0 = -INF` (无初始条件) | 解析闭式解 |

| 参数 | 值 | 单位 |
|------|------|------|
| $\lambda$ | 0.1 | W/(m·K) |
| $\rho$ | 25 | kg/m³ |
| $c_p$ | 2 | J/(kg·K) |
| $B_1$ | 10 | — |
| $B_2$ | 1000 | — |
| $k_x = k_y = k_z$ | $\pi / 4$ | rad/m |

**闭式解**: $T(\mathbf{r}, t) = \frac{1}{\lambda}\left[B_1(x^3z - 3xy^2z) + B_2 \sin(k_x x)\sin(k_y y)\sin(k_z z)\,e^{-\alpha(k_x^2+k_y^2+k_z^2)t}\right]$

### C.4 WF-G1：稳态调和/Poisson 解 (超形状)

| PDE (sub1) | PDE (sub2) |
|------------|------------|
| $\nabla^2 T = 0$ (Laplace) | $-\lambda \nabla^2 T = P_w$ (Poisson) |

| 参数 | 值 | 单位 |
|------|------|------|
| $\lambda$ | 10 | W/(m·K) |
| $P_w$ (sub2) | 10000 | W/m³ |
| 三线性系数 | (333, 432, 579) | K (归一化) |
| 坐标映射 | $q' = (q+10)/20$ | — |

**Sub1 参考**: $T(0,0,0) = 333 \times 0.5 + 432 \times 0.5 + 579 \times 0.5 = 672.0$K

**Sub2 参考**: $T(0,0,0) = -\frac{P_w}{6\lambda}(0 - x_{\max}^2 + 0 - y_{\max}^2 + 0 - z_{\max}^2) = \frac{P_w}{6\lambda}(x_{\max}^2 + y_{\max}^2 + z_{\max}^2)$

### C.5 WF-A7：稳态线性导热 (嵌入球不影响)

| PDE | 边界条件 | 闭式解 |
|-----|----------|--------|
| $-\lambda T'' = 0$ (沿 Z) | $T(z=0) = 300$, $T(z=1) = 350$, 其余绝热 | $T(z) = 300 + 50z$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $T_\mathrm{front}$ | 300 | K |
| $T_\mathrm{back}$ | 350 | K |
| $\lambda$ | 50 | W/(m·K) |
| 球半径 | 0.25 | fp |
| 球心 | (0.5, 0.5, 0.5) | fp |

**参考值**: $T(0.5, 0.5, 0.5) = 300 \times 0.5 + 350 \times 0.5 = 325.0$K

---

*文档更新: 2026-02-06 | v1: 初始版本，解析值为唯一权威基准 | 作者: GPU Wavefront 测试设计*
