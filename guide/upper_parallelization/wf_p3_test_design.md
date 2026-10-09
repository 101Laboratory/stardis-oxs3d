# P3 级 GPU Wavefront 数值正确性测试 — 详细设计

**生成时间**: 2026-02-06 (v1)  
**关联文档**:
- [numerical_correctness_test_checklist.md](numerical_correctness_test_checklist.md)（总清单）
- [wf_p0_test_design.md](wf_p0_test_design.md)（P0 级测试设计，本文参照其格式）
- [wf_p2_test_design.md](wf_p2_test_design.md)（P2 级测试设计）
- [phase_b4_fine_grained_state_machine.md](phase_b4_fine_grained_state_machine.md)（状态机设计）
- [wf_numerical_tests/](wf_numerical_tests/)（分类物理原理）

**范围**: 6 个 P3 级测试(WF-E5, WF-C4, WF-B3, WF-I3, WF-B5, WF-I1)的 Wavefront 适配详细方案  
**阻塞关系**: P0、P1、P2 全部通过是 P3 的前置条件；P3 全部通过是最终集成验收的前置条件

---

## 一、核心设计决策

### 1.1 P3 测试定位

P3 级测试与 P0/P2 共享同一验证框架（`sdis_solve_wavefront_probe` 公共探针接口 + `test_sdis_wf_p0_utils.h` 工具函数），但侧重于工业级复杂场景的综合验证：

| 维度 | P0 | P2 | **P3** |
|------|-----|-----|--------|
| **时间** | 稳态/简单稳态 | 瞬态 (Green 函数) | **瞬态多介质 + Picard 迭代** |
| **几何** | 单位立方体 | 立方体 + 超形状 + 球体 | **多介质板 + 嵌套立方体 + 超形状** |
| **边界条件** | Dirichlet/Neumann/Robin | 多面异温 + 绝热 + 解析时空耦合 | **净通量 + 大气 + 对流+辐射 + 复合 Robin** |
| **介质** | 单固体 | 单固体 + solid-solid 透明 | **固体+流体+大气 + 双固体(嵌套) + 多流体温度** |
| **辐射** | 简单辐射 | 无 / 简单 | **辐射环境耦合 + 线性化 Hrad + Picard 辐射迭代** |
| **参考来源** | 解析闭式解 | 解析闭式解 | **解析 + 数值参考 (独立求解器 / EDF Syrthès)** |
| **体积功率** | 有 (A2) | 有 (G1) | **嵌套体积功率 + 双层固体** |

### 1.2 公共 API 与 P0/P2 完全一致

P3 复用 P0 已建立的 `sdis_solve_wavefront_probe(scn, &args, &est)` 接口，无需新增 API。相比 P0/P2，P3 区别在于：

1. **`args.picard_order`**: WF-C4 使用 `picard_order=1`（辐射 Picard 迭代）
2. **多介质场景**: WF-E5 同时包含固体、流体、大气三类介质
3. **辐射环境 (radenv)**: WF-E5/C4/B3 创建并绑定 `sdis_radiative_env`
4. **净通量边界 (flux)**: WF-C4 使用 `interface_get_flux` 回调
5. **数值参考值**: WF-E5/I1 使用外部数值参考（非解析），采用宽松固定容差
6. **sdis_data 高级用法**: WF-E5/C4/I1/I3 大量使用 `sdis_data` 承载介质/界面运行时参数

### 1.3 验证协议

P3 采用**混合验证协议**，视参考来源不同使用不同容差策略：

| 参考来源 | 测试 | 容差策略 | 决定权 |
|---------|------|---------|--------|
| **解析闭式解** | WF-C4, WF-B3, WF-B5 | $\|T_\text{MC} - T_\text{ref}\| \leq \sigma_\text{tol} \times \text{SE}$ | ✅ **PASS/FAIL** |
| **数值参考** (独立求解器) | WF-E5 | $\|T_\text{MC} - T_\text{ref}\| \leq \text{EPS}$ (固定 0.5K) | ✅ **PASS/FAIL** |
| **数值参考** (EDF Syrthès) | WF-I1 | $\|T_\text{MC} - T_\text{ref}\| \leq \max(3\sigma, 5\text{K})$ | ✅ **PASS/FAIL** |
| **范围/对称性检查** | WF-I3 | $T \in [T_\text{min}, T_\text{max}]$、对称差 $\leq 5\sigma_\text{combined}$ | ✅ **PASS/FAIL** |

### 1.4 共享测试基础设施

P3 测试复用 P0 的共享头文件和工具函数:

| 头文件 | 提供 |
|--------|------|
| `test_sdis_utils.h` | `CHK`/`OK` 宏、`box_get_indices`/`box_get_position`/`box_get_interface` 回调、`DUMMY_SOLID_SHADER`/`DUMMY_FLUID_SHADER`/`DUMMY_INTERFACE_SHADER` 默认 shader、`eq_eps` 浮点比较、`dummy_medium_getter` |
| `test_sdis_wf_p0_utils.h` | `p0_compare_analytic`（Primary 比较）、`P0_NREALISATIONS`/`P0_TOL_SIGMA`/`P0_PASS_RATE` 共享常量 |
| `<star/s3dut.h>` | `s3dut_create_super_shape`（WF-B5 使用） |

---

## 二、测试配置详情

### 2.1 P3 测试列表与分组

| 测试 ID | 测试文件 | CPU 对标 | 几何 | 链接库 | CMake 组 |
|---------|---------|---------|------|--------|----------|
| WF-E5 | `test_sdis_wf_e5_unsteady_atm.c` | `test_sdis_unsteady_atm.c` | 多介质板 (12v, 22 tri) | `sdis_obj` | `WF_P3_TESTS_BASIC` |
| WF-C4 | `test_sdis_wf_c4_flux2.c` | `test_sdis_flux2.c` | 固体板+流体域 (12v, 22 tri) | `sdis_obj` | `WF_P3_TESTS_BASIC` |
| WF-B3 | `test_sdis_wf_b3_boundary_flux.c` | `test_sdis_solve_boundary_flux.c` | 单位立方体 (8v, 12 tri) | `sdis_obj` | `WF_P3_TESTS_BASIC` |
| WF-I3 | `test_sdis_wf_i3_enclosure_limit.c` | `test_sdis_enclosure_limit_conditions.c` | 嵌套立方体 (16v, 24 tri) | `sdis_obj` | `WF_P3_TESTS_BASIC` |
| WF-I1 | `test_sdis_wf_i1_volumic_power2.c` | `test_sdis_volumic_power2.c` | 嵌套长方体+内方块 (16v, 36 tri) | `sdis_obj` | `WF_P3_TESTS_BASIC` |
| WF-B5 | `test_sdis_wf_b5_probe_list.c` | `test_sdis_solve_probe_list.c` | 超形状 (256×128 slices) | `sdis_obj s3dut` | `WF_P3_TESTS_S3DUT` |

### 2.2 共享参数表

| 参数 | WF-E5 | WF-C4 | WF-B3 | WF-I3 | WF-I1 | WF-B5 |
|------|-------|-------|-------|-------|-------|-------|
| `nrealisations` | 10000 | 10000 | 10000 | 10000 | 10000 | 10000 |
| `diff_algo` | 默认 | 默认 | 默认 | 默认 | 默认 | 默认 |
| `picard_order` | 默认 | **1** | 默认 | 默认 | 默认 | 默认 |
| `time_range` | 有限时间点 ×22 | 有限时间点 ×15 | {INF, INF} | {INF, INF} | {INF, INF} | {INF, INF} |
| 容差类型 | **固定 0.5K** | 3σ 统计 | 3σ 统计 | 范围/对称 | **max(3σ, 5K)** | 3σ 统计 |
| 通过率阈值 | **90%** | 95% | 95% | **3/3** (100%) | **90%** | 95% |
| radenv | ✅ | ✅ | ✅ | — | — | — |
| sdis_data | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| TIMEOUT | 600s | 600s | 600s | 600s | 600s | 600s |

---

## 三、WF-E5：瞬态多介质导热 + 大气辐射

### 3.1 对标 CPU 测试

**CPU**: `test_sdis_unsteady_atm.c` → 2D + 3D 场景，流体/固体/大气耦合，4 种求解模式（tfluid/tsolid/tbound1/tbound2）

**GPU**: `test_sdis_wf_e5_unsteady_atm.c` → 仅 3D 场景，仅内部探针（tfluid + tsolid），无边界探针

**差异**:
- CPU 同时运行 2D 和 3D，GPU 仅运行 3D
- CPU 包含边界探针 `sdis_solve_probe_boundary`（tbound1/tbound2），GPU 跳过（wavefront 不支持边界探针查询）
- 探针位置固定化：CPU 中 solid 探针使用随机 Y/Z，GPU 使用固定 Y=Z=XHpE×0.5 以确保可复现性
- 参考值为数值参考（非解析），使用宽松固定容差 0.5K

### 3.2 物理场景

```
                  TG=310K (ground, -X)
                     ↓
  ┌─────────────────────────────────────────────────┐
  │ Fluid           │ Solid                         │→ Atmosphere
  │ ρ=1.3, cp=1005  │ λ=0.6, ρ=2400, cp=800        │   TA=290K
  │ T₀=300K         │ T₀=300K, δ=XE/40             │   HA=400
  │                  │                               │   ε=1
  │   ► probe_fluid  │   ► probe_solid               │
  │                  │                               │
  └──────────┬───────┴───────────────────────────────┘
   x=0       x=XH=3   x=XHpE=3.2
             contact: HC=400, ε=1

  Ground: HG=400, ε=1, TG=310K
  Radiative env: TR=260K, Tref=297.975K
  Adiabatic faces: ±Y, ±Z (fluid+solid)
```

12 顶点、22 三角形。流体板占据 $[0, X_H=3]$，固体板占据 $[X_H, X_{HpE}=3.2]$。5 种界面类型：绝热-流体、绝热-固体、地面、接触面、大气。

### 3.3 参考数据

数值参考来自独立求解器，**非解析**。容差：$\text{EPS} = (T_\max - T_\min) \times 0.01 = (310 - 260) \times 0.01 = 0.5$K。

#### 流体探针 $(1.5, 1.5, 1.5)$

| 时间 [s] | 温度 [K] |
|----------|---------|
| 0 | 300.000 |
| 1000 | 309.539 |
| 2000 | 309.673 |
| 3000 | 309.732 |
| 4000 | 309.768 |
| 5000 | 309.792 |
| 6000 | 309.809 |
| 7000 | 309.821 |
| 8000 | 309.831 |
| 9000 | 309.837 |
| 10000 | 309.842 |

#### 固体探针 $(3.04, 1.6, 1.6)$

| 时间 [s] | 温度 [K] |
|----------|---------|
| 0 | 300.000 |
| 1000 | 300.874 |
| 2000 | 302.258 |
| 3000 | 303.222 |
| 4000 | 303.900 |
| 5000 | 304.390 |
| 6000 | 304.750 |
| 7000 | 305.016 |
| 8000 | 305.212 |
| 9000 | 305.356 |
| 10000 | 305.463 |

### 3.4 Wavefront 场景构建

#### 几何与材料

```c
/* 12 顶点定义多介质板:
 * 左面 (0), 流体/固体分界 (XH=3), 右面 (XHpE=3.2) */

/* 固体 */
solid_props->lambda = 0.6;  solid_props->cp = 800.0;
solid_props->rho = 2400.0;  solid_props->delta = XE / 40.0;
solid_props->t0 = 0;        solid_props->temperature = 300.0; /* T₀ */

/* 内部流体 */
fluid_props->rho = 1.3;   fluid_props->cp = 1005.0;
fluid_props->t0 = 0;      fluid_props->temperature = 300.0; /* T₀ */

/* 大气流体 (已知温度, t0=INF → 始终已知) */
fluid_A_props->temperature = 290.0;  fluid_A_props->t0 = INF;

/* Dummy solid (exterior, trivial → 不参与热传导) */
dummy_props->lambda = 0;  dummy_props->t0 = INF;
dummy_props->temperature = SDIS_TEMPERATURE_NONE;
```

#### 接口分配

| 三角形 | 面 | 接口类型 | 参数 |
|--------|-----|---------|------|
| 0-1 | -Z (fluid part) | adiabatic\_fluid | h=0, ε=0 |
| 2-3 | -Z (solid part) | adiabatic\_solid | h=0, ε=0 |
| 4-5 | -X (ground) | ground | T=310K, h=400, ε=1, Tref=TG |
| 6-9 | +Z front | adiabatic (fluid+solid) | h=0, ε=0 |
| 10-11 | +X (atmosphere) | atmosphere | T=NONE, h=400, ε=1, Tref=297.975K |
| 12-15 | +Y | adiabatic (fluid+solid) | h=0, ε=0 |
| 16-19 | -Y | adiabatic (fluid+solid) | h=0, ε=0 |
| 20-21 | 内部 (contact) | contact | T=NONE, h=400, ε=1, Tref=297.975K |

**辐射环境**:
```c
radenv_shader.temperature = e5_radenv_get_temperature;       /* → TR=260K */
radenv_shader.reference_temperature = e5_radenv_get_reference_temperature; /* → TR=260K */
OK(sdis_radiative_env_create(dev, &radenv_shader, NULL, &radenv));
scn_args.radenv = radenv;
scn_args.t_range[0] = TMIN;  /* 260 */
scn_args.t_range[1] = TMAX;  /* 310 */
```

### 3.5 探针布局与求解

```c
/* === 流体探针: 11 个时间点 === */
for(i = 0; i < 11; i++) {
    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    args.nrealisations = 10000;
    args.position[0] = XH * 0.5;    /* 1.5 */
    args.position[1] = XH * 0.5;    /* 1.5 */
    args.position[2] = XH * 0.5;    /* 1.5 */
    args.time_range[0] = e5_tfluid_times[i];
    args.time_range[1] = e5_tfluid_times[i];

    OK(sdis_solve_wavefront_probe(box_scn, &args, &est_wf));
    pass = fabs(mc.E - e5_tfluid_refs[i]) <= EPS;
}

/* === 固体探针: 11 个时间点 === */
for(i = 0; i < 11; i++) {
    args.position[0] = X_PROBE;     /* XH + 0.2*XE = 3.04 */
    args.position[1] = XHpE * 0.5;  /* 1.6 */
    args.position[2] = XHpE * 0.5;  /* 1.6 */
    args.time_range[0] = e5_tsolid_times[i];
    args.time_range[1] = e5_tsolid_times[i];

    OK(sdis_solve_wavefront_probe(box_scn, &args, &est_wf));
    pass = fabs(mc.E - e5_tsolid_refs[i]) <= EPS;
}

/* 总计 22 探针, 通过率 >= 90% */
CHK((double)n_pass / (double)n_total >= 0.90);
```

### 3.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    ├── [t <= t0=0: 初温已知] → PATH_DONE (T=300K — 仅 t=0 探针)
    └── [t > 0: NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    ├── [adiabatic face: T=NONE, h=0] → PATH_DONE (反射)
    ├── [ground: T=310K, h=400] → PATH_BND_SF_* (固体-流体对流)
    ├── [contact: T=NONE, h=400, ε=1] → PATH_BND_SF_* → PATH_RAD_* (辐射追踪)
    └── [atmosphere: h=400, ε=1, fluid T=290K] → PATH_BND_SF_* → PATH_RAD_*
→ PATH_RAD_TRACE → PATH_RAD_PROCESS → PATH_RAD_ENV (辐射环境终止, T=260K)
→ PATH_CNV_* (流体中对流步进)
→ PATH_DONE
```

**验证重点**:
- **多介质交互**: 流体→固体→大气的完整热传导+对流+辐射链路
- **辐射环境**: radenv 发射/吸收正确终止于 TR=260K
- **瞬态初温**: 流体和固体在 $t \leq 0$ 时返回已知初温 300K
- **对流边界**: ground (h=400) 和 atmosphere (h=400) 的对流系数传递

### 3.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥90% 探针（≥20/22） `|T.E - T_ref|` ≤ 0.5K | **PASS/FAIL** |

**使用固定容差而非 σ 容差的原因**: 参考值来自独立数值求解器，本身有数值误差（非精确解析值）。固定 1% 温度范围容差（0.5K）同时覆盖参考误差和 MC 统计误差。通过率放宽至 90% 以容忍早期时间点的高方差。

### 3.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 全部探针 T=300K | 瞬态时间积分未启动，始终返回初温 |
| 流体探针正确、固体偏移 | 接触面 contact 界面配置错误或对流系数 HC 传递失败 |
| 接近但系统偏低 | 辐射环境 TR=260K 的贡献路径错误（拉低温度） |
| t=0 探针 FAIL | 初温条件 `t <= t0` 判断逻辑错误 |
| SE 为 0 但 T ≠ ref | 退化路径（零方差意味着所有路径终止于同一条件） |
| 大气侧探针异常 | 大气流体 `t0=INF` 导致流体温度始终返回 290K 而非 NONE |

---

## 四、WF-C4：瞬态通量 + 对流 + 辐射 Picard 迭代

### 4.1 对标 CPU 测试

**CPU**: `test_sdis_flux2.c` → 固体板 + 流体域，左面净通量 + 右面对流+辐射，Picard 迭代

**GPU**: `test_sdis_wf_c4_flux2.c` → 相同几何 + 物理参数，`sdis_solve_wavefront_probe` + `picard_order=1`

**差异**: 仅求解器引擎替换（depth-first → wavefront）。所有物理参数、初始条件、参考值完全相同。

### 4.2 物理场景

```
  φ=10000 W/m²                            h=8, T_f=290K
  h=2, T_f=330K                            ε=1, Tref=300K
  ε=1, Tref=300K
       ↓                                        ↓
  ┌──────────┐────────────────────────────────────────┐
  │  固体    │         流体 (right)                    │
  │ [0, 0.1] │         [0.1, 1.1]                     │
  │          │                                          │
  │ λ=1.15   │         T_fluid = 290K                  │
  │ ρ=1700   │                                          │
  │ cp=800   │                                          │
  │ δ=0.005  │                                          │
  │ T₀=线性   │                                          │
  └──────────┘────────────────────────────────────────┘
  x=0                x=0.1                         x=1.1

  初始温度: T(x, 0) = u*(T2-T1)+T1,  u=x/0.1
    T1=306.334 (x=0), T2=294.941 (x=0.1)

  Radiative env: Trad=320K, Tref=300K
  picard_order = 1
```

12 顶点、22 三角形。固体板 $[0, 0.1] \times [-1, 1]^2$，右侧流体域 $[0.1, 1.1]$。

### 4.3 参考数据

解析计算参考值（与 CPU 测试完全相同）。容差：$3\sigma$ 统计容差。

| x | t [s] | 温度 [K] |
|-----|-------|---------|
| 0.01 | 1000 | 481.720 |
| 0.05 | 1000 | 335.195 |
| 0.09 | 1000 | 299.944 |
| 0.01 | 2000 | 563.218 |
| 0.05 | 2000 | 392.798 |
| 0.09 | 2000 | 324.897 |
| 0.01 | 3000 | 620.252 |
| 0.05 | 3000 | 444.734 |
| 0.09 | 3000 | 359.440 |
| 0.01 | 4000 | 665.659 |
| 0.05 | 4000 | 490.325 |
| 0.09 | 4000 | 393.899 |
| 0.01 | 10000 | 830.444 |
| 0.05 | 10000 | 664.828 |
| 0.09 | 10000 | 533.924 |

### 4.4 Wavefront 场景构建

#### 几何与材料

```c
/* 固体 */
sp->lambda = 1.15;  sp->rho = 1700.0;  sp->cp = 800.0;
solid_shader.delta = c4_solid_get_delta;  /* → 0.005 */

/* 初始温度: 空间线性分布 */
static double
c4_solid_get_temperature(const struct sdis_rwalk_vertex* vtx, ...)
{
  if(vtx->time > 0) return SDIS_TEMPERATURE_NONE;
  else {
    double u = vtx->P[0] / 0.1;
    return u * (294.941 - 306.334) + 306.334;
  }
}

/* 流体 1 (左侧): T=330K, 流体 2 (右侧): T=290K */
fp->temperature = 330.0;  /* fluid1 */
fp->temperature = 290.0;  /* fluid2 */

/* Dummy solid (外部): lambda=0, 无热传导 */
```

#### 接口分配

| 三角形 | 面 | 接口类型 | 参数 |
|--------|-----|---------|------|
| 0-1 | -Z | C4_ADIABATIC | h=0, ε=0 |
| 2-3 | -X (左面, 通量面) | C4_SOLID_FLUID_WITH_FLUX | h=2, ε=1, φ=10000, Tref=300K |
| 4-5 | +Z | C4_ADIABATIC | h=0, ε=0 |
| 6-7 | +X (右面, 固体-流体) | C4_SOLID_FLUID | h=8, ε=1, Tref=300K |
| 8-11 | ±Y | C4_ADIABATIC | h=0, ε=0 |
| 12-21 | 流体域外壳 | C4_FIXED_TEMPERATURE | h=1, ε=1, T=280K, Tref=300K |

**辐射环境**:
```c
radenv_shader.temperature = c4_radenv_get_temperature;  /* → 320K */
radenv_shader.reference_temperature = c4_radenv_get_Tref;  /* → 300K */
scn_args.radenv = radenv;
scn_args.t_range[0] = 300;  scn_args.t_range[1] = 300;
```

### 4.5 探针布局与求解

```c
for(i = 0; i < 15; i++) {
    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    args.nrealisations = 10000;
    args.picard_order = 1;   /* 辐射 Picard 迭代 */
    args.position[0] = c4_probes[i].x;
    args.position[1] = 0;
    args.position[2] = 0;
    args.time_range[0] = c4_probes[i].time;
    args.time_range[1] = c4_probes[i].time;

    OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

    /* Primary: wavefront vs 解析值 (3σ) */
    pass = p0_compare_analytic(est_wf, c4_probes[i].ref, 3.0);
    n_pass += pass;
}

/* 通过率: >= 95% (≥15/15 或允许 1 个失败) */
CHK((double)n_pass / 15.0 >= 0.95);
```

### 4.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    ├── [t <= 0: 线性初温] → PATH_DONE (T = linear profile)
    └── [t > 0: NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    ├── [adiabatic: T=NONE, h=0] → PATH_DONE (反射)
    ├── [flux face: φ=10000, h=2, ε=1] → PATH_BND_SF_* → PATH_RAD_*
    │   └── + flux 贡献累加
    ├── [solid-fluid: h=8, ε=1] → PATH_BND_SF_* → PATH_RAD_*
    └── [fixed T=280K: fluid boundary] → PATH_DONE
→ PATH_RAD_TRACE → PATH_RAD_PROCESS → PATH_RAD_ENV (Trad=320K)
→ PATH_CNV_* (流体中对流步进)
→ PATH_PICARD_REINIT (picard_order=1: 重启辐射迭代)
→ PATH_DONE
```

**验证重点**:
- **Picard 迭代 (`picard_order=1`)**: wavefront 下的 Picard 辐射重启正确性
- **净通量边界 (`flux`)**: `interface_get_flux` 回调返回 φ=10000 的正确累加
- **空间初温分布**: `solid_get_temperature(vtx)` 在 $t \leq 0$ 时返回位置相关的线性温度
- **辐射环境**: Trad=320K, Tref=300K 的辐射传递链路

### 4.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥95% 探针（≥15/15 或 ≥14/15） `|T.E - T_ref|` ≤ `3 × T.SE` | **PASS/FAIL** |

### 4.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 全部探针 T≈线性初温 | 瞬态时间积分未启动 |
| x=0.01 处 T 偏低 | 通量贡献 φ=10000 未正确累加或 `interface_get_flux` 回调未配置 |
| x=0.09 处 T 偏高 | 右面对流系数 h=8 传递错误 |
| 系统性偏移 | Picard 迭代实际未执行（`picard_order` 未传递到 wavefront） |
| 早期时间 FAIL，晚期 PASS | δ=0.005 过大导致短时间步精度不足 |
| 辐射路径贡献缺失 | radenv Trad=320K 或 emissivity ε=1 回调错误 |

---

## 五、WF-B3：稳态边界通量 — 内部温度剖面

### 5.1 对标 CPU 测试

**CPU**: `test_sdis_solve_boundary_flux.c` → 2D+3D，验证边界处温度/对流通量/辐射通量/总通量 4 通道

**GPU**: `test_sdis_wf_b3_boundary_flux.c` → 仅 3D，仅验证内部温度剖面（wavefront 不支持边界通量探针）

**差异**:
- CPU 使用 `sdis_solve_probe_boundary_flux`（边界通量探针）和 `sdis_solve_boundary_flux`（面平均），GPU 跳过这两个 API
- GPU 替代方案：验证稳态内部温度线性剖面的正确性，间接验证边界条件配置
- CPU 使用 100000 实现，GPU 使用 10000（线性剖面方差低，足够）

### 5.2 物理场景

```
  Tb=0K               T(+X)=analytic
  ┌──────────────────────────────────┐
  │ -X face:             +X face:   │
  │  H=0.5              H=0.5      │
  │  T=300K             T=NONE     │
  │  ε=1                ε=1        │
  │  Tref=0             Tref=300   │
  │                                  │
  │  固体: λ=0.1                    │
  │        ρ=25, cp=2, δ=1/40       │
  │                                  │
  │  四面绝热 (±Y, ±Z)              │
  └──────────────────────────────────┘
  x=0                             x=1

  Radiative env: Trad=300K, Tref=300K
  单位立方体 [0,1]^3 (box_get_* from test_sdis_utils.h)
```

### 5.3 解析解

稳态 1D 导热 + Robin 边界条件的解析解：

$$H_\mathrm{rad} = 4\sigma T_\mathrm{ref}^3 \varepsilon = 4 \times 5.670 \times 10^{-8} \times 300^3 \times 1 \approx 6.124$$

$$T_{+X} = \frac{H \cdot T_f + H_\mathrm{rad} \cdot T_\mathrm{rad} + \lambda \cdot T_b}{H + H_\mathrm{rad} + \lambda}$$

$$= \frac{0.5 \times 300 + 6.124 \times 300 + 0.1 \times 0}{0.5 + 6.124 + 0.1} \approx 295.56 \text{K}$$

$$T(x) = T_b + (T_{+X} - T_b) \cdot x = 295.56 \cdot x$$

| 探针 $x$ | 参考温度 $T(x)$ [K] |
|---------|---------------------|
| 0.1 | $\approx$ 29.556 |
| 0.3 | $\approx$ 88.668 |
| 0.5 | $\approx$ 147.780 |
| 0.7 | $\approx$ 206.892 |
| 0.9 | $\approx$ 266.004 |

（具体数值由代码中 `B3_T(x)` 宏计算）

### 5.4 Wavefront 场景构建

#### 几何与材料

```c
/* 单位立方体: box_get_indices / box_get_position from test_sdis_utils.h */
/* 固体: 直接返回常量，无 sdis_data */
solid_shader.calorific_capacity = b3_solid_get_cp;      /* → 2.0 */
solid_shader.thermal_conductivity = b3_solid_get_lambda; /* → 0.1 */
solid_shader.volumic_mass = b3_solid_get_rho;            /* → 25.0 */
solid_shader.delta = b3_solid_get_delta;                 /* → 1/40 */
solid_shader.temperature = b3_solid_get_temperature;     /* → NONE (稳态) */
```

#### 接口分配

使用 3 种 interface：

| 三角形 | 面 | 接口类型 | 参数 |
|--------|-----|---------|------|
| 0-1 | Front -Z | interf\_adiabatic | h=0, ε=0 |
| 2-3 | Left -X | interf\_Tb | h=0.5, T=0K, ε=1, Tref=0 |
| 4-5 | Back +Z | interf\_adiabatic | h=0, ε=0 |
| 6-7 | Right +X | interf\_H | h=0.5, T=NONE, ε=1, Tref=300K |
| 8-9 | Top +Y | interf\_adiabatic | h=0, ε=0 |
| 10-11 | Bottom -Y | interf\_adiabatic | h=0, ε=0 |

```c
/* 辐射环境 */
radenv: Trad=300K, Tref=300K
scn_args.radenv = radenv;
scn_args.t_range[0] = 0;  scn_args.t_range[1] = 300;

/* 两个流体 medium 共享同一 sdis_data (双流体包围同一固体空腔) */
fluid1 = create(T=300K);  fluid2 = create(T=300K);  /* 共享数据 */
```

### 5.5 探针布局与求解

```c
static const double probe_x[] = {0.1, 0.3, 0.5, 0.7, 0.9};

for(i = 0; i < 5; i++) {
    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    args.nrealisations = 10000;
    args.position[0] = probe_x[i];
    args.position[1] = 0.5;
    args.position[2] = 0.5;
    args.time_range[0] = INF;  /* 稳态 */
    args.time_range[1] = INF;

    OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

    ref = B3_T(probe_x[i]);  /* = Tb + (T_RIGHT - Tb) * x */
    pass = p0_compare_analytic(est_wf, ref, 3.0);
    n_pass += pass;
}

/* 通过率: >= 95% (≥5/5) */
CHK((double)n_pass / 5.0 >= 0.95);
```

### 5.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [NONE (稳态)] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    ├── [adiabatic: ±Z, ±Y] → PATH_DONE (反射)
    ├── [-X: Tb=0, h=0.5, ε=1, Tref=0] → PATH_BND_SF_* → PATH_RAD_*
    │   └── 辐射线性化参考温度 Tref=0K → Hrad ≈ 0
    └── [+X: T=NONE, h=0.5, ε=1, Tref=300K] → PATH_BND_SF_* → PATH_RAD_*
        └── 辐射线性化参考温度 Tref=300K → Hrad ≈ 6.124
→ PATH_RAD_TRACE → PATH_RAD_PROCESS → PATH_RAD_ENV (Trad=300K)
→ PATH_DONE
```

**验证重点**:
- **辐射线性化**: $H_\mathrm{rad} = 4\sigma T_\mathrm{ref}^3 \varepsilon$，不同面有不同 Tref（0/300K），Hrad 贡献差异巨大
- **Robin 边界与辐射耦合**: -X 面 (Tb=0, Tref=0) vs +X 面 (NONE, Tref=300K) 形成不对称稳态剖面
- **双流体共享 sdis_data**: fluid1 和 fluid2 共用同一温度数据块

### 5.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥95% 探针（≥5/5） `|T.E - T_ref|` ≤ `3 × T.SE` | **PASS/FAIL** |

### 5.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| T(x) ≈ 常数 | 绝热面配置错误（全部面返回 NONE 而非 Dirichlet/Robin） |
| T(x) = 150*x（线性但斜率偏差大） | Hrad 计算错误，辐射环境温度或 Tref 传递失败 |
| 左端 T(0.1) 偏高 | -X 面 Tb=0K 未正确设置（Tref 或 emissivity 错误） |
| 右端 T(0.9) 偏低 | +X 面 Robin 条件中辐射贡献缺失 |
| 全部 T=0 | 固体初始温度回调返回了固定值而非 NONE |

---

## 六、WF-I3：嵌套立方体多材料空腔极限条件

### 6.1 对标 CPU 测试

**CPU**: `test_sdis_enclosure_limit_conditions.c` → 2D 多材料空腔，验证退化条件（空腔内探针拒绝 + 零方差精确解）

**GPU**: `test_sdis_wf_i3_enclosure_limit.c` → **3D** 嵌套立方体，验证温度范围和对称性

**差异**:
- CPU 为 2D（4 面），GPU 为 3D（6 面对）
- CPU 验证退化行为（`RES_BAD_OP`、零方差），GPU 验证物理一致性（温度范围、对称性）
- GPU 不测试空腔内探针拒绝逻辑，仅测试固体域内探针

### 6.2 物理场景

```
         ╔══════════════════════════════╗
         ║ Outer cube [0,1]^3           ║ → ext_fluid T=360K
         ║   (solid: λ=1, ρ=1, cp=1)    ║    h=10
         ║                               ║
         ║   ┌───────────────┐           ║
         ║   │ Inner 0.25-0.75│          ║
         ║   │ -Z/+Z: T=280K  │          ║
         ║   │ -X/+X: T=300K  │          ║
         ║   │ -Y/+Y: T=340K  │          ║
         ║   └───────────────┘           ║
         ╚══════════════════════════════╝

  16 顶点 (内 8 + 外 8), 24 三角形 (内 12 + 外 12)
  Solid: λ=1, ρ=1, cp=1, δ=0.00625
  所有对流: h=10, 无辐射 (ε=0)
```

### 6.3 检查策略

无解析闭式解，使用物理一致性检查替代精确值验证：

| 检查 | 探针位置 | 条件 | 原理 |
|------|---------|------|------|
| Check 1 | (0.1, 0.1, 0.1) 近外壁 | $T \in [280, 360]$, SE > 0 | 靠近外壁应接近 Text=360K |
| Check 2 | (0.3, 0.5, 0.5) 近内壁 | $T \in [280, 360]$, SE > 0 | 靠近内壁受多温度流体混合影响 |
| Check 3 | (0.5, 0.1, 0.5) vs (0.5, 0.9, 0.5) | $|T_1 - T_2| \leq 5\sigma_\mathrm{combined}$ | Y 方向对称性（两位置关于 Y=0.5 对称） |

### 6.4 Wavefront 场景构建

#### 介质

```c
/* 固体: 常数属性 */
solid: λ=1, ρ=1, cp=1, δ=0.00625, T=SDIS_TEMPERATURE_NONE

/* 4 种流体 */
fluid_280: T=280K (内立方体 -Z/+Z 面)
fluid_300: T=300K (内立方体 -X/+X 面)
fluid_340: T=340K (内立方体 -Y/+Y 面)
fluid_ext: T=360K (外立方体全部面)
```

#### 接口分配

| 三角形 | 面 | 界面 | 流体温度 |
|--------|-----|------|---------|
| 0-1 | Inner -Z | fluid(280)/solid | 280K |
| 2-3 | Inner +Z | fluid(280)/solid | 280K |
| 4-5 | Inner -X | fluid(300)/solid | 300K |
| 6-7 | Inner +X | fluid(300)/solid | 300K |
| 8-9 | Inner -Y | fluid(340)/solid | 340K |
| 10-11 | Inner +Y | fluid(340)/solid | 340K |
| 12-23 | Outer 全部 | solid/fluid(360) | 360K |

```c
/* 所有界面共享: h=10, 无辐射 */
shader.convection_coef = i3_interf_get_h;  /* → 10.0 */
shader.convection_coef_upper_bound = 10.0;
/* 无 radenv */
```

### 6.5 探针布局与求解

```c
/* Check 1: 近外壁 */
args.position = {0.1, 0.1, 0.1};
args.time_range = {INF, INF};  args.nrealisations = 10000;
OK(sdis_solve_wavefront_probe(scn, &args, &est));
CHK(mc.E >= 280 && mc.E <= 360 && mc.SE > 0);

/* Check 2: 近内壁 */
args.position = {0.3, 0.5, 0.5};
CHK(mc.E >= 280 && mc.E <= 360 && mc.SE > 0);

/* Check 3: Y 方向对称性 */
args.position = {0.5, 0.1, 0.5};  → mc1
args.position = {0.5, 0.9, 0.5};  → mc2
diff = |mc1.E - mc2.E|;  combined_SE = sqrt(mc1.SE² + mc2.SE²);
CHK(diff <= 5.0 * combined_SE);

/* 全部 3 检查需通过 */
CHK(n_checks_ok == n_checks);
```

### 6.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    ├── [inner face: fluid/solid convection, h=10] → PATH_BND_SF_*
    │   └── [T_fluid = 280/300/340K 取决于面] → PATH_CNV_*
    │       └── [流体已知温度] → PATH_DONE
    └── [outer face: solid/fluid convection, h=10] → PATH_BND_SF_*
        └── [T_ext = 360K] → PATH_CNV_* → PATH_DONE
```

**验证重点**:
- **多流体温度**: 6 对面分 4 种温度的流体，random walk 到达不同面获取不同温度
- **嵌套几何**: 内外两层立方体构成的固体域，BVH 需正确处理两层三角形
- **对称性**: Y 方向对称的两个探针应得到统计一致的温度
- **温度范围**: 所有探针温度必须在 $[T_\min=280, T_\max=360]$ 范围内

### 6.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Check 1** | $T \in [280, 360]$ **AND** SE > 0 | **PASS/FAIL** |
| **Check 2** | $T \in [280, 360]$ **AND** SE > 0 | **PASS/FAIL** |
| **Check 3** | $|T_1 - T_2| \leq 5\sigma_\mathrm{combined}$ | **PASS/FAIL** |
| **总体** | 3/3 检查通过 | **PASS/FAIL** |

### 6.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| T ∉ [280, 360] | 多流体温度分配错误（某流体 T=0 或溢出） |
| SE = 0 | 所有路径终止于同一条件（退化场景或 nrealisations=0） |
| Check 3 对称性失败（diff > 5σ） | 内立方体面三角形绕序不一致（±Y 面法线方向错误） |
| T ≈ 360（所有探针） | 内立方体面的界面未正确绑定（全部指向 ext=360K） |
| T ≈ 280（全部） | 外立方体面的界面未正确绑定（全部指向 280K 流体） |

---

## 七、WF-I1：嵌套体积功率（Syrthès 参考）

### 7.1 对标 CPU 测试

**CPU**: `test_sdis_volumic_power2.c` → 同几何 + 两组参数检查（λ₁=1 和 λ₁=0.1），2D+3D 参考值

**GPU**: `test_sdis_wf_i1_volumic_power2.c` → 仅 Check 1 (λ₁=1, λ₂=10, Pw=10000)，仅 3D Syrthès 参考值

**差异**:
- CPU 包含 2 组参数（Check 1 + Check 2），GPU 仅保留 Check 1
- CPU 的 tolerance 注释掉了`CHK`（仅打印），GPU 使用 `max(3σ, 5K)` 硬断言
- 参考值使用 Syrthès 3D 值（非 2D），单位为摄氏度 → 转换为开尔文

### 7.2 物理场景

```
    fluid1          fluid2
  T=373.15K       T=273.15K
  (100°C)          (0°C)
      ↑ h=5            ↓ h=10
  ┌───┬──────────────────┬───┐
  │ a │                  │ a │
  │ d │   solid1         │ d │  ← 外长方体
  │ i │   λ=1, Pw=0      │ i │    [-0.5,0.5]x[-1,1]x[-0.5,0.5]
  │ a │  ┌──────────┐   │ a │
  │ b │  │ solid2   │   │ b │  ← 内方块
  │ a │  │ λ=10     │   │ a │    [-0.1,0.1]x[0.4,0.6]x[-0.5,0.5]
  │ t │  │ Pw=10000 │   │ t │
  │ i │  └──────────┘   │ i │
  │ c │                  │ c │
  └───┴──────────────────┴───┘
       top:h=5,fluid1    bottom:h=10,fluid2

  16 顶点, 36 三角形 (18 矩形 × 2)
```

### 7.3 参考数据

EDF Syrthès 3D 工业求解器参考值（摄氏度）。容差：$\max(3\sigma, 5\text{K})$。

| y 位置 | Syrthès 3D [°C] | 开尔文 [K] |
|--------|-----------------|-----------|
| +0.85 | 189.13 | 462.28 |
| +0.65 | 247.09 | 520.24 |
| +0.45 | 308.42 | 581.57 |
| +0.25 | 233.55 | 506.70 |
| +0.05 | 192.30 | 465.45 |
| -0.15 | 156.98 | 430.13 |
| -0.35 | 123.43 | 396.58 |
| -0.55 | 90.040 | 363.19 |

所有探针均在 $x=0, z=0$（solid1 内部，不在 solid2 中）。

### 7.4 Wavefront 场景构建

#### 几何与材料

```c
/* solid1 (外): λ=1, cp=500000, ρ=1000, δ=0.01, Pw=NONE */
sp->lambda = 1.0;  sp->cp = 500000.0;  sp->rho = 1000.0;
sp->delta = 0.01;  sp->P = SDIS_VOLUMIC_POWER_NONE;

/* solid2 (内): λ=10, cp=500000, ρ=1000, δ=0.01, Pw=10000 */
sp->lambda = 10.0;  sp->P = 10000.0;

/* fluid1: T=373.15K (100°C) — top 面 */
/* fluid2: T=273.15K (0°C) — bottom 面 */
```

#### 接口分配

| rect | 三角形 | 位置 | 界面 |
|------|--------|------|------|
| 0 | 0-1 | Cuboid left | solid1/fluid1 adiabatic (h=0) |
| 1 | 2-3 | Cuboid top | solid1/fluid1 convective (h=5) |
| 2 | 4-5 | Cuboid right | solid1/fluid1 adiabatic (h=0) |
| 3 | 6-7 | Cuboid bottom | solid1/fluid2 convective (h=10) |
| 4-11 | 8-23 | Cuboid front/back rings | solid1/fluid1 adiabatic (h=0) |
| 12-15 | 24-31 | Cube side faces | solid1/solid2 (透明) |
| 16-17 | 32-35 | Cube front/back | solid2/fluid1 adiabatic (h=0) |

```c
/* solid1-solid2 界面: 空 shader → 透明穿越 */
OK(sdis_interface_create(dev, solid2, solid1,
    &SDIS_INTERFACE_SHADER_NULL, NULL, &interf_solid1_solid2));

/* 对流界面: 带 h 和温度 */
interf_shader.convection_coef = i1_interf_h;
interf_shader.front.temperature = i1_interf_temperature;
```

### 7.5 探针布局与求解

```c
for(i = 0; i < 8; i++) {
    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    args.nrealisations = 10000;
    args.position[0] = 0;
    args.position[1] = i1_refs1[i].pos[1];  /* y: 0.85 → -0.55 */
    args.position[2] = 0;
    args.time_range[0] = INF;
    args.time_range[1] = INF;

    OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

    ref_K = i1_refs1[i].T_celsius + 273.15;
    tol = max(3.0 * mc.SE, 5.0);  /* max(3σ, 5K) */
    pass = fabs(mc.E - ref_K) <= tol;
    n_pass += pass;
}

/* 通过率: >= 90% (≥7/8 或 ≥8/8) */
CHK((double)n_pass / 8.0 >= 0.90);
```

### 7.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环, 累积体积功率)
→ PATH_BND_DISPATCH
    ├── [solid/solid 透明: cube side faces] → PATH_BND_SS_REINJECT_*
    │   └── 穿越 solid1↔solid2 界面 → 继续 DS 步进 (λ改变: 1→10 或 10→1)
    ├── [solid/fluid adiabatic: h=0] → PATH_DONE (反射)
    ├── [solid/fluid convective: h=5/10] → PATH_BND_SF_*
    │   └── [T_fluid = 373.15K 或 273.15K] → PATH_CNV_* → PATH_DONE
    └── [solid2/fluid adiabatic: h=0] → PATH_DONE (反射)
```

**验证重点**:
- **体积功率**: solid2 区域 Pw=10000 在 DS 步进中正确累积
- **solid-solid 界面穿越**: 穿越时材料属性（λ: 1←→10）切换正确
- **双固体嵌套几何**: 36 三角形的复杂场景中 BVH 构建与遍历的正确性
- **外部参考容差**: Syrthès 参考有自身数值误差，max(3σ, 5K) 容差确保鲁棒性

### 7.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥90% 探针（≥7/8） $|T_\text{wf} - T_\text{ref}| \leq \max(3\sigma, 5\text{K})$ | **PASS/FAIL** |

**使用 max(3σ, 5K) 容差的原因**: Syrthès 参考值本身有 FEM 数值误差（非精确解析值），5K 固定容差覆盖参考误差；3σ 统计容差覆盖 MC 随机性。取两者最大值确保鲁棒。通过率 90% 允许 1 个探针因参考不准而失败。

### 7.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 全部 T≈373.15K 或 273.15K | solid-solid 界面阻塞了路径穿越，仅终止于最近的流体面 |
| y=0.45 处 T 偏低（应最高） | solid2 体积功率 Pw=10000 未正确设置或 `volumic_power` 回调未启用 |
| 对称偏移 | 接口映射中某 rect 指向了错误的界面 |
| 全部 T ≈ 323K (=average) | 所有界面均为 adiabatic 配置（h=0），忽略了 top/bottom 对流 |
| 单位错误 (off by 273) | 参考值从摄氏度到开尔文的转换遗漏 |

---

## 八、WF-B5：三线性温度场 + 超形状（探针列表）

### 8.1 对标 CPU 测试

**CPU**: `test_sdis_solve_probe_list.c` → 超形状 + 三线性温度，`sdis_solve_probe_list`（批量 API）+ 多种验证

**GPU**: `test_sdis_wf_b5_probe_list.c` → 相同超形状 + 三线性温度，10 个确定性探针位置，逐个 `sdis_solve_wavefront_probe`

**差异**:
- CPU 使用批量 `sdis_solve_probe_list` API + 随机探针位置，GPU 使用逐个 `sdis_solve_wavefront_probe` + 确定性位置
- CPU 验证 RNG 独立性/状态序列化/estimator buffer 聚合，GPU 仅验证温度正确性
- 探针位置从随机改为确定性（10 个预定义位置），确保可复现性

### 8.2 物理场景

```
       ___/  \___      超形状: 非凸边界
      /  .  T=?  \     f0: A=1.5, B=1.0, M=11, N0=1, N1=1, N2=2
     /_   __   __\     f1: A=1.0, B=2.0, M=3.6, N0=1, N1=2, N2=0.7
      \/  \/  \/       radius=1, 256×128 slices

  三线性温度场:
    T(x,y,z) = 333·x' + 432·y' + 579·z'
    q' = (q - lower)/(upper - lower),  lower=-3, upper=+3

  固体: λ=25, cp=500, ρ=7500, δ=自适应 (0.4/spread)
  Dummy 流体: T=350K (surrounding environment)

  边界温度 = 解析三线性剖面
  内部探针应精确恢复同一剖面 (三线性 → Laplace 方程精确解)
```

### 8.3 解析解

三线性函数满足 Laplace 方程 $\nabla^2 T = 0$（调和函数），因此 MC 求解的内部温度精确等于边界 profile 值。

$$T(\mathbf{r}) = 333 \cdot \frac{x + 3}{6} + 432 \cdot \frac{y + 3}{6} + 579 \cdot \frac{z + 3}{6}$$

10 个确定性探针位置及参考值:

| 探针 | 位置 $(x, y, z)$ | $T_\mathrm{ref}$ [K] |
|------|-----------------|----------------------|
| 0 | (0, 0, 0) | 672.0 |
| 1 | (0.10, 0.05, -0.05) | 672.0 + offset |
| 2 | (-0.10, 0.10, 0) | 672.0 + offset |
| 3 | (0.05, -0.10, 0.08) | 672.0 + offset |
| 4 | (-0.08, 0.06, 0.04) | 672.0 + offset |
| 5 | (0, -0.12, 0) | 672.0 + offset |
| 6 | (0.07, 0.07, 0.07) | 672.0 + offset |
| 7 | (-0.05, -0.05, -0.05) | 672.0 + offset |
| 8 | (0.12, 0, 0) | 672.0 + offset |
| 9 | (0, 0, -0.10) | 672.0 + offset |

（具体参考值由 `trilinear_profile(pos)` 函数运行时计算）

所有 10 个探针均在超形状内部，距原点 ≤ 0.15，远离边界以确保 MC 精度。

### 8.4 Wavefront 场景构建

#### 超形状几何

```c
f0.A = 1.5; f0.B = 1; f0.M = 11.0; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2.0;
f1.A = 1.0; f1.B = 2; f1.M =  3.6; f1.N0 = 1; f1.N1 = 2; f1.N2 = 0.7;
OK(s3dut_create_super_shape(NULL, &f0, &f1, 1, 256, 128, &msh));
```

**翻转绕序**: `ids[1] ↔ ids[2]`（法线指向超形状内部，即固体域内）。

#### 材料与自适应 delta

```c
/* 固体 */
solid: λ=25, cp=500, ρ=7500
solid.delta = 0.4 / spread;  /* 自适应: spread 来自 sdis_scene_get_medium_spread */
solid.temperature = SDIS_TEMPERATURE_NONE;  /* 稳态 */

/* 界面: 边界温度 = 三线性剖面 */
interf_shader.front.temperature = b5_interface_get_temperature;
interf_shader.back.temperature  = b5_interface_get_temperature;
/* b5_interface_get_temperature(frag) → trilinear_profile(frag->P) */

/* Dummy 流体 (外部环境, T=350K) */
fluid: T = 350K
```

### 8.5 探针布局与求解

```c
for(i = 0; i < 10; i++) {
    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    args.nrealisations = 10000;
    args.position[0] = b5_positions[i][0];
    args.position[1] = b5_positions[i][1];
    args.position[2] = b5_positions[i][2];
    args.time_range[0] = INF;
    args.time_range[1] = INF;

    OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

    ref = trilinear_profile(b5_positions[i]);
    pass = p0_compare_analytic(est_wf, ref, 3.0);
    n_pass += pass;
}

/* 通过率: >= 95% (≥10/10 或 ≥9/10) */
CHK((double)n_pass / 10.0 >= 0.95);
```

### 8.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP
    └── [NONE] → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
        → PATH_CND_DS_STEP_ADVANCE → (循环)
→ PATH_BND_DISPATCH
    └── [Dirichlet: T=trilinear(P)] → PATH_DONE
```

**验证重点**:
- **非凸超形状几何上的 delta-sphere**: 256×128 高分辨率网格 + 非凸表面
- **自适应 delta**: `0.4 / spread` 确保步长适应几何尺度
- **三线性场精确恢复**: 调和函数 → MC 应精确恢复（零偏差，仅统计方差）
- **高三角形数几何**: 超形状可产生数万三角形，考验 BVH 构建/遍历性能

### 8.7 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥95% 探针（≥9/10 或 10/10） `|T.E - T_ref|` ≤ `3 × T.SE` | **PASS/FAIL** |

### 8.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| T = 350K（全部） | 所有路径终止于 dummy 流体而非超形状边界（绕序翻转未执行） |
| T = 0 或 NaN | 超形状 mesh 数据为空或 `s3dut_mesh_get_data` 失败 |
| SE 非常大 | delta 自适应计算异常（spread ≈ 0 → delta → ∞） |
| 偏离 672 但方向正确 | 三线性公式中的坐标范围（lower/upper）与 CPU 不一致 |
| probe(0, 0, 0) ≠ 672 | 归一化坐标公式错误 |

---

## 九、测试文件结构与 CMake 注册

### 9.1 文件命名

```
stardis-cus3d/stardis-solver/0.16.2/src/
├── test_sdis_wf_p0_utils.h                  ← P0/P2/P3 共用工具
├── test_sdis_wf_e5_unsteady_atm.c           ← WF-E5 (P3)
├── test_sdis_wf_c4_flux2.c                  ← WF-C4 (P3)
├── test_sdis_wf_b3_boundary_flux.c          ← WF-B3 (P3)
├── test_sdis_wf_i3_enclosure_limit.c        ← WF-I3 (P3)
├── test_sdis_wf_i1_volumic_power2.c         ← WF-I1 (P3)
└── test_sdis_wf_b5_probe_list.c             ← WF-B5 (P3, s3dut)
```

### 9.2 CMake 注册

```cmake
# ---- P3 wavefront numerical correctness tests ----
# Basic tests (box geometry / custom geometry, no s3dut): link sdis_obj only
set(WF_P3_TESTS_BASIC
    test_sdis_wf_e5_unsteady_atm
    test_sdis_wf_c4_flux2
    test_sdis_wf_b3_boundary_flux
    test_sdis_wf_i3_enclosure_limit
    test_sdis_wf_i1_volumic_power2)

foreach(p3test ${WF_P3_TESTS_BASIC})
    add_executable(${p3test} src/${p3test}.c)
    target_link_libraries(${p3test} PRIVATE sdis_obj)
    add_test(NAME ${p3test} COMMAND ${p3test})
    set_tests_properties(${p3test} PROPERTIES
        TIMEOUT 600
        LABELS "wf;p3;numerical")
endforeach()

# S3DUT tests (supershape geometry): link sdis_obj + s3dut
set(WF_P3_TESTS_S3DUT
    test_sdis_wf_b5_probe_list)

foreach(p3test ${WF_P3_TESTS_S3DUT})
    add_executable(${p3test} src/${p3test}.c)
    target_link_libraries(${p3test} PRIVATE sdis_obj s3dut)
    add_test(NAME ${p3test} COMMAND ${p3test})
    set_tests_properties(${p3test} PROPERTIES
        TIMEOUT 600
        LABELS "wf;p3;numerical")
endforeach()
```

### 9.3 运行时依赖部署

```cmake
if(WIN32 AND COMMAND deploy_runtime_dependencies)
    foreach(p3test ${WF_P3_TESTS_BASIC})
        deploy_runtime_dependencies(${p3test})
    endforeach()
    foreach(p3test ${WF_P3_TESTS_S3DUT})
        deploy_runtime_dependencies(${p3test})
    endforeach()
endif()
```

---

## 十、实施顺序与依赖

```
┌──────────────────────────────────────────────────────────┐
│ 前置: P0 全部通过 (WF-A1, WF-A2, WF-B2, WF-C1, WF-D1)  │
│ 前置: P1 全部通过 (状态机调度正确性)                       │
│ 前置: P2 全部通过 (瞬态/非凸/solid-solid 路径)            │
│ 产出: P3 覆盖工业级综合场景                               │
└──────────────────────────────────────────────────────────┘
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
┌───────────────────────┐  ┌──────────────────────┐
│ Step 1a: WF-B3 (最简)  │  │ Step 1b: WF-I3       │
│ 稳态 Robin + 辐射线性化 │  │ 嵌套多流体稳态       │
│ 验证: Hrad 公式正确性   │  │ 验证: 多流体对流路径  │
│ 仅依赖 P0 辐射路径      │  │ 仅依赖 P0 对流路径   │
│ 可与 I3 并行开发        │  │ 可与 B3 并行开发     │
└───────────────────────┘  └──────────────────────┘
              │                       │
              └───────────┬───────────┘
                          ▼
┌──────────────────────────────────────────────────────────┐
│ Step 2: WF-B5 (超形状 + 三线性)                           │
│   非凸几何 + 自适应 delta + 高三角形数                     │
│   前置: B3 (稳态 Dirichlet 路径已验证)                    │
│   前置: P2-G1 (超形状鲁棒性基础)                          │
└──────────────────────────────────────────────────────────┘
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
┌───────────────────────┐  ┌──────────────────────┐
│ Step 3a: WF-I1         │  │ Step 3b: WF-C4       │
│ 嵌套体积功率+外参考    │  │ 通量+Picard+辐射     │
│ 需 solid-solid 穿越    │  │ 需 flux 回调 + Picard│
│ 前置: I3 (多流体稳态)  │  │ 前置: B3 (辐射环境)  │
└───────────────────────┘  └──────────────────────┘
              │                       │
              └───────────┬───────────┘
                          ▼
┌──────────────────────────────────────────────────────────┐
│ Step 4: WF-E5 (瞬态多介质 + 大气)                         │
│   物理最复杂: 固体+流体+大气+辐射+瞬态                     │
│   前置: C4 (Picard+辐射), I3 (多流体), P2-E1/E2 (瞬态)   │
│   数值参考, 宽松容差                                       │
└──────────────────────────────────────────────────────────┘
```

---

## 十一、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 数值参考不精确 (E5, I1) | 正确实现可能因参考偏差而 FAIL | E5: 使用 0.5K 宽容差 + 90% 通过率；I1: max(3σ, 5K) + 90% |
| Picard 并行正确性 (C4) | Picard 迭代在 wavefront 批量调度下收敛性不同 | picard_order=1（最简 Picard），验证收敛后逐步提高 |
| 多介质场景构建复杂 (E5) | 5 种界面类型 + 3 种介质 + radenv | 严格复刻 CPU 测试的三角形→界面映射表 |
| 超形状 BVH 性能 (B5) | 数万三角形可能导致超时 | TIMEOUT=600s，256×128 网格已在 P2-G1 中验证 |
| solid-solid 穿越 + volumic power (I1) | 穿越界面时 λ 跳变 + 体积功率累积 | 已在 P2-A7 初步验证 solid-solid 穿越路径 |
| 对称性检查统计性 (I3) | 5σ 容差下仍有 ~0.006% 假阳性 | 组合 SE 考虑了两个独立探针的各自方差 |
| 大气流体 t0=INF (E5) | 大气温度始终已知可能导致路径退化 | 测试覆盖 t=0 到 t=10000 全范围，确认非退化 |

---

## 附录 A：P3 vs P0/P2 测试特性对比矩阵

| 特性 | P0-A1 | P0-A2 | P0-B2 | P0-C1 | P0-D1 | P2-E1 | P2-E2 | P2-E3 | P2-G1 | P2-A7 | **P3-E5** | **P3-C4** | **P3-B3** | **P3-I3** | **P3-I1** | **P3-B5** |
|:-----|:-----:|:-----:|:-----:|:-----:|:-----:|:-----:|:-----:|:-----:|:-----:|:-----:|:---------:|:---------:|:---------:|:---------:|:---------:|:---------:|
| 稳态 | ● | ● | ● | ● | ● | | | | ● | ● | | | ● | ● | ● | ● |
| 瞬态 | | | | | △ | ● | ● | ● | | | ● | ● | | | | |
| 初温条件 | | | | | △ | ● | ● | | | | ● | ● | | | | |
| 空间初温分布 | | | | | | | | | | | | ● | | | | |
| box 几何 | ● | ● | ● | | ● | ● | ● | | | ● | | | ● | | | |
| 超形状几何 | | | | | | | | ● | ● | | | | | | | ● |
| 多介质板几何 | | | | | | | | | | | ● | ● | | | | |
| 嵌套几何 | | | | | | | | | | | | | | ● | ● | |
| solid-solid 界面 | | | | | | | | | | ● | | | | | ● | |
| 绝热面 | ● | ● | ● | ● | | | ● | | | ● | ● | ● | ● | | ● | |
| 多面异温 | | | | | ● | ● | | | | | | | | | | |
| 多流体温度 | | | | | | | | | | | ● | ● | | ● | ● | |
| 体积功率 | | ● | | | | | | | ● | | | | | | ● | |
| Robin BC | | | ● | | | | | | | | | | ● | | | |
| 辐射环境 (radenv) | | | | ● | | | | | | | ● | ● | ● | | | |
| 辐射线性化 Hrad | | | | | | | | | | | | | ● | | | |
| 对流路径 | | | ● | | ● | | | | | | ● | ● | ● | ● | ● | |
| 净通量边界 (flux) | | | | | | | | | | | | ● | | | | |
| Picard 迭代 | | | | | | | | | | | | ● | | | | |
| sdis_data | | | | | | | | | ● | ● | ● | ● | ● | ● | ● | |
| s3dut 依赖 | | | | | | | | ● | ● | ● | | | | | | ● |
| 自适应 delta | | | | | | | | | ● | | | | | | | ● |
| 数值参考 (非解析) | | | | | | | | | | | ● | | | | ● | |
| 解析参考 | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | | ● | ● | | | ● |
| 范围/对称性检查 | | | | | | | | | | | | | | ● | | |

---

## 附录 B：P3 测试状态机路径覆盖矩阵

| `path_phase` | WF-E5 | WF-C4 | WF-B3 | WF-I3 | WF-I1 | WF-B5 |
|:-------------|:-----:|:-----:|:-----:|:-----:|:-----:|:-----:|
| `PATH_INIT` | ● | ● | ● | ● | ● | ● |
| `PATH_CND_INIT_ENC` | ● | ● | ● | ● | ● | ● |
| `PATH_ENC_QUERY_EMIT` | ● | ● | ● | ● | ● | ● |
| `PATH_CND_DS_CHECK_TEMP` | ● | ● | ● | ● | ● | ● |
| `PATH_CND_DS_STEP_TRACE` | ● | ● | ● | ● | ● | ● |
| `PATH_CND_DS_STEP_PROCESS` | ● | ● | ● | ● | ● | ● |
| `PATH_CND_DS_STEP_ADVANCE` | ● | ● | ● | ● | ● | ● |
| `PATH_BND_DISPATCH` | ● | ● | ● | ● | ● | ● |
| `PATH_BND_SF_*`（固体-流体） | ● | ● | ● | ● | ● | |
| `PATH_BND_SS_REINJECT_*` | | | | | ● | |
| `PATH_CNV_*`（流体对流） | ● | ● | | ● | ● | |
| `PATH_RAD_TRACE` | ● | ● | ● | | | |
| `PATH_RAD_PROCESS` | ● | ● | ● | | | |
| `PATH_RAD_ENV` | ● | ● | ● | | | |
| `PATH_PICARD_REINIT` | | ● | | | | |
| `PATH_DONE` | ● | ● | ● | ● | ● | ● |

**覆盖**: 6 个 P3 测试共覆盖 ~16 个 `path_phase` 状态，**新增覆盖的关键路径**:
- `PATH_PICARD_REINIT`（Picard 辐射迭代重启，仅 WF-C4）
- `PATH_RAD_*` + `PATH_BND_SF_*` 组合（辐射+对流耦合，WF-E5/C4/B3）
- `PATH_BND_SS_REINJECT_*` + 体积功率累积（solid-solid 穿越 + Pw，WF-I1）

**与 P0/P2 的互补**:
- P0 建立了基础路径（DS + Dirichlet + Robin + 辐射 + 对流）
- P2 扩展了瞬态、非凸几何、solid-solid 透明界面
- P3 完成了**工业级综合**：多介质 + 大气 + Picard + 净通量 + 嵌套体积功率 + 数值参考验证

---

## 附录 C：P3 参考值完整表

### C.1 WF-E5：瞬态多介质导热 + 大气（数值参考）

| PDE | 边界条件 | 初始条件 | 参考方法 |
|-----|----------|---------|---------|
| $\rho c_p \frac{\partial T}{\partial t} = \nabla \cdot (\lambda \nabla T)$（固体）；$\rho c_p \frac{\partial T}{\partial t} = 0$（流体，已知温度或对流） | Ground/Contact/Atmosphere | $T_0=300$K | 独立数值求解器 |

| 参数 | 值 | 单位 |
|------|------|------|
| $X_H$ | 3.0 | m |
| $X_E$ | 0.2 | m |
| $T_0$ | 300 | K |
| $T_G$ | 310 | K |
| $T_A$ | 290 | K |
| $T_R$ | 260 | K |
| $T_\mathrm{ref}$ | 297.975 | K |
| $H_G = H_C = H_A$ | 400 | W/(m²·K) |
| Solid: $\lambda$ | 0.6 | W/(m·K) |
| Solid: $\rho$ | 2400 | kg/m³ |
| Solid: $c_p$ | 800 | J/(kg·K) |
| Solid: $\delta$ | $X_E/40 = 0.005$ | — |
| Fluid: $\rho$ | 1.3 | kg/m³ |
| Fluid: $c_p$ | 1005 | J/(kg·K) |

**参考值**: 见 §3.3 表格（流体 11 点 + 固体 11 点 = 22 探针）

### C.2 WF-C4：瞬态通量 + 对流 + 辐射 Picard（解析参考）

| PDE | 边界条件 | 初始条件 | 参考方法 |
|-----|----------|---------|---------|
| $\rho c_p \frac{\partial T}{\partial t} = \lambda \frac{\partial^2 T}{\partial x^2}$ | 左:通量+对流+辐射, 右:对流+辐射 | $T(x,0) = (T_2-T_1)\frac{x}{0.1} + T_1$ | 解析计算 |

| 参数 | 值 | 单位 |
|------|------|------|
| $\lambda$ | 1.15 | W/(m·K) |
| $\rho$ | 1700 | kg/m³ |
| $c_p$ | 800 | J/(kg·K) |
| $\delta$ | 0.005 | — |
| $\phi$ (flux) | 10000 | W/m² |
| $T_1$ (initial left) | 306.334 | K |
| $T_2$ (initial right) | 294.941 | K |
| picard\_order | 1 | — |
| $T_\mathrm{rad}$ | 320 | K |
| $T_\mathrm{ref}$ | 300 | K |

**参考值**: 见 §4.3 表格（15 探针: 3x × 5t）

### C.3 WF-B3：稳态边界通量内部剖面（解析参考）

| PDE | 边界条件 | 闭式解 |
|-----|----------|--------|
| $-\lambda T'' = 0$ | -X: Robin ($H=0.5$, $T_b=0$, $\varepsilon=1$, $T_\mathrm{ref}=0$); +X: Robin ($H=0.5$, $\varepsilon=1$, $T_\mathrm{ref}=300$); 其余绝热 | $T(x) = T_b + (T_{+X} - T_b) \cdot x$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $\lambda$ | 0.1 | W/(m·K) |
| $H$ | 0.5 | W/(m²·K) |
| $T_b$ | 0 | K |
| $T_f$ | 300 | K |
| $T_\mathrm{rad}$ | 300 | K |
| $\varepsilon$ | 1 | — |
| $H_\mathrm{rad}$ | $4\sigma T_\mathrm{ref}^3 \varepsilon \approx 6.124$ | W/(m²·K) |
| $T_{+X}$ | $\frac{H T_f + H_\mathrm{rad} T_\mathrm{rad} + \lambda T_b}{H + H_\mathrm{rad} + \lambda} \approx 295.56$ | K |

**参考值**: $T(x) = 295.56 \cdot x$（5 个 $x$ 位置: 0.1, 0.3, 0.5, 0.7, 0.9）

### C.4 WF-I3：嵌套立方体空腔（范围/对称性检查）

| 场景 | 验证方法 |
|------|---------|
| 嵌套立方体, 4 种流体 (280/300/340/360K) | 温度范围 + 对称性 |

| 参数 | 值 |
|------|------|
| Inner cube | $[0.25, 0.75]^3$ |
| Outer cube | $[0, 1]^3$ |
| $\lambda$ | 1 |
| $h$ | 10 |
| $\delta$ | 0.00625 |
| 温度范围 | $[280, 360]$ K |

**无精确参考值** — 使用物理一致性检查

### C.5 WF-I1：嵌套体积功率（Syrthès 3D 参考）

| PDE | 参考方法 |
|-----|---------|
| $-\nabla \cdot (\lambda \nabla T) = P_w$ (solid2), $-\nabla \cdot (\lambda \nabla T) = 0$ (solid1) | EDF Syrthès 3D FEM |

| 参数 | 值 | 单位 |
|------|------|------|
| solid1: $\lambda$ | 1 | W/(m·K) |
| solid2: $\lambda$ | 10 | W/(m·K) |
| $P_w$ | 10000 | W/m³ |
| $c_p$ | 500000 | J/(kg·K) |
| $\rho$ | 1000 | kg/m³ |
| $\delta$ | 0.01 | — |
| fluid1: $T$ | 373.15 (100°C) | K |
| fluid2: $T$ | 273.15 (0°C) | K |
| $h_\mathrm{top}$ | 5 | W/(m²·K) |
| $h_\mathrm{bottom}$ | 10 | W/(m²·K) |

**参考值**: 见 §7.3 表格（8 探针, Syrthès 3D 摄氏度→开尔文）

### C.6 WF-B5：三线性温度场 + 超形状（解析参考）

| PDE | 闭式解 |
|-----|--------|
| $\nabla^2 T = 0$ (Laplace) | $T(\mathbf{r}) = 333 \cdot \frac{x+3}{6} + 432 \cdot \frac{y+3}{6} + 579 \cdot \frac{z+3}{6}$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $\lambda$ | 25 | W/(m·K) |
| $c_p$ | 500 | J/(kg·K) |
| $\rho$ | 7500 | kg/m³ |
| lower / upper | -3 / +3 | m |
| 超形状: f0 | A=1.5, B=1, M=11, N0=1, N1=1, N2=2 | — |
| 超形状: f1 | A=1, B=2, M=3.6, N0=1, N1=2, N2=0.7 | — |
| radius | 1 | m |
| slices / stacks | 256 / 128 | — |
| delta | $0.4 / \text{spread}$（自适应） | — |

**参考值**: 由 `trilinear_profile(pos)` 运行时计算（10 探针）
**典型值**: $T(0,0,0) = 672.0$K

---

*文档更新: 2026-02-06 | v1: 初始版本，混合参考验证 (解析 + 数值) | 作者: GPU Wavefront 测试设计*
