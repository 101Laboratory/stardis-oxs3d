# P0 级 GPU Wavefront 数值正确性测试 — 详细设计

**生成时间**: 2026-02-15 (v3: 2026-02-15 — 解析值为唯一权威基准)  
**关联文档**:
- [numerical_correctness_test_checklist.md](numerical_correctness_test_checklist.md)（总清单）
- [phase_b4_test_design.md](phase_b4_test_design.md)（架构级测试）
- [wf_numerical_tests/](wf_numerical_tests/)（分类物理原理）
- [phase_b4_fine_grained_state_machine.md](phase_b4_fine_grained_state_machine.md)（状态机设计）

**范围**: 5 个 P0 级测试(WF-A1, WF-A2, WF-B2, WF-C1, WF-D1)的 Wavefront 适配详细方案  
**阻塞关系**: P0 全部通过是所有后续开发(P1/P2/P3)的前置条件

---

## 一、核心设计决策

### 1.1 测试路径选择：直接暴露 persistent wavefront solver 为公共探针接口

将 `solve_tile_wavefront` 的内部能力暴露为公共 API，使测试直接构造探针任务（位置 + 实现数 + 物理参数），完全绕过 `sdis_solve_camera` 和相机抽象，与 CPU 版 `sdis_solve_probe` 1:1 对应。

| 方案 | 可行性 | 选择 |
|------|--------|------|
| A: 暴露 `sdis_solve_wavefront_probe` 公共 API | 需新增公共接口 + 内部 `init_paths_from_probe` | ✅ **P0 采用** |
| B: 通过 `sdis_solve_camera` + 正交相机映射探针 | 引入不必要的相机间接层 | ❌ 测试不应依赖渲染管线 |
| C: 通过 `sdis_solve_camera` + 透视相机 | 像素→位置映射非线性 | ❌ 不适合 |

**方案 A 的优势**：

1. **与 CPU 测试 1:1 对应**：CPU 测试通过 `sdis_solve_probe(scn, args, &estimator)` 验证，wavefront 测试通过 `sdis_solve_wavefront_probe(scn, args, &estimator)` 验证——相同的 `args`，相同的 `estimator` 输出格式
2. **无相机/像素映射开销**：不需要处理像素坐标 → 空间坐标的映射、视野覆盖、边缘像素等问题
3. **精确控制探针位置**：测试可直接指定探针的 3D 坐标（如 `pos={0.25, 0.5, 0.5}`），与解析解精确对应
4. **可复用 CPU 测试的验证逻辑**：相同的 `eq_eps(T.E, analytic_ref, 3.0 * T.SE)` 判定方式
5. **为未来 GPU probe API 奠基**：该接口本身就是 GPU 求解器的核心公共接口之一

### 1.2 新增公共 API 设计

#### 1.2.1 相机依赖点分析

`solve_tile_wavefront` 中相机仅在 `init_all_paths()` 的一行使用：

```c
/* sdis_solve_wavefront.c:191 — 唯一的相机依赖点 */
camera_ray(cam, samp, ray_pos, ray_dir);
d3_set(p->rwalk.vtx.P, ray_pos);
/* ... */
f3_set_d3(p->rad_direction, ray_dir);
```

对于**探针模式**（导热问题），相机射线的 `ray_dir` 无物理意义（MC 随机游走方向由 delta-sphere 采样决定），只需设置 `rwalk.vtx.P = probe_position`。对于辐射问题，`rad_direction` 仅在初始辐射发射时使用，探针模式可用随机方向替代。

#### 1.2.2 API 签名

```c
/*******************************************************************************
 * sdis_solve_wavefront_probe — wavefront 版本的 sdis_solve_probe
 *
 * 将单个探针的所有实现（realisations）打包为 wavefront 路径，
 * 以批量光追替代单路径深度优先追踪。
 *
 * 接口语义与 sdis_solve_probe 完全一致：
 *   - 输入: sdis_solve_probe_args (position + nrealisations + 物理参数)
 *   - 输出: sdis_estimator (E, SE, count — 与 CPU 版相同)
 *
 * 内部实现:
 *   1. 调用 init_paths_from_probe() 设置 wavefront 路径
 *   2. 驱动 wavefront 主循环 (advance_all → collect_rays → batch_trace → distribute)
 *   3. 调用 collect_results_probe() 汇聚结果到 accum
 ******************************************************************************/
res_T
sdis_solve_wavefront_probe
  (struct sdis_scene*                   scn,
   const struct sdis_solve_probe_args*  args,
   struct sdis_estimator**              out_estimator);

/*******************************************************************************
 * sdis_solve_wavefront_probe_list — 批量版本
 *
 * 多个探针位置的高效批量求解。所有探针的 realisations 混合打包为
 * wavefront 路径，最大化 GPU 批量光追利用率。
 ******************************************************************************/
res_T
sdis_solve_wavefront_probe_list
  (struct sdis_scene*                        scn,
   const struct sdis_solve_probe_list_args*  args,
   struct sdis_estimator_buffer**            out_buf);
```

#### 1.2.3 内部改造概要

需新增/修改的内部函数（均在 `sdis_solve_wavefront.c` 中）：

| 函数 | 状态 | 说明 |
|------|------|------|
| `init_paths_from_probe()` | **新增** | 替代 `init_all_paths()` 的相机部分；直接设 `rwalk.vtx.P = args->position`，不调用 `camera_ray` |
| `collect_results_probe()` | **新增** | 替代 `collect_results()` 的 tile 像素写入；将完成路径的 `T.value` 写入 `accum` (sum/sum2/count) |
| `solve_wavefront_probe_impl()` | **新增** | wavefront 主循环，与 `solve_tile_wavefront` 共享 advance/collect/trace/distribute 步骤 |
| `init_all_paths()` | 不修改 | 仍供 `solve_tile_wavefront`（相机模式）使用 |
| `collect_results()` | 不修改 | 仍供相机模式使用 |

`init_paths_from_probe()` 伪代码：

```c
static res_T
init_paths_from_probe(
  struct wavefront_context* wf,
  struct sdis_scene*        scn,
  struct ssp_rng*           base_rng,
  const unsigned            enc_id,
  const double              position[3],
  const double              time_range[2],
  const size_t              nrealisations,
  const size_t              picard_order,
  const enum sdis_diffusion_algorithm diff_algo)
{
  size_t i;
  for(i = 0; i < nrealisations; i++) {
    struct path_state* p = &wf->paths[i];

    p->path_id = (uint32_t)i;
    p->pixel_x = 0;              /* 单探针：映射到虚拟像素 (0,0) */
    p->pixel_y = 0;
    p->realisation_idx = (uint32_t)i;
    p->rng = base_rng;

    /* 直接从 position 初始化 — 不需要 camera_ray */
    double time = sample_time(p->rng, time_range);
    p->rwalk = RWALK_NULL;
    d3_set(p->rwalk.vtx.P, position);  /* ← 核心替换点 */
    p->rwalk.vtx.time = time;
    p->rwalk.enc_id = enc_id;

    /* 随机初始辐射方向（仅辐射问题需要，导热问题忽略） */
    {
      float rd[3];
      ssp_rng_hemisphere_uniform(p->rng, rd);
      f3_copy(p->rad_direction, rd);
    }

    /* rwalk_context、T、scratch 初始化 — 与 init_all_paths 完全相同 */
    p->ctx = RWALK_CONTEXT_NULL;
    p->ctx.Tmin = scn->tmin;  /* ... 其余同 init_all_paths ... */
    p->ctx.max_branchings = picard_order - 1;
    p->ctx.diff_algo = diff_algo;
    p->T = TEMPERATURE_NULL;
    p->phase = PATH_INIT;
    p->active = 1;
    p->needs_ray = 0;
    /* ... 清零 scratch ... */
  }
  wf->active_count = nrealisations;
  return RES_OK;
}
```

`collect_results_probe()` 伪代码：

```c
static res_T
collect_results_probe(
  struct wavefront_context* wf,
  struct accum*             acc_temp,
  struct accum*             acc_time)
{
  size_t i;
  *acc_temp = ACCUM_NULL;
  *acc_time = ACCUM_NULL;

  for(i = 0; i < wf->total_paths; i++) {
    const struct path_state* p = &wf->paths[i];
    ASSERT(!p->active);

    if(p->T.done) {
      acc_temp->sum  += p->T.value;
      acc_temp->sum2 += p->T.value * p->T.value;
      acc_temp->count += 1;
    }
  }
  return RES_OK;
}
```

#### 1.2.4 enclosure_id 获取

CPU `solve_one_probe` 在循环外调用 `scene_get_enclosure_id(scn, position, &enc_id)` 获取探针所在包壳 ID。wavefront 版本同样需要在 `sdis_solve_wavefront_probe` 入口处做一次 `scene_get_enclosure_id` 调用，然后传递给 `init_paths_from_probe`。

### 1.3 验证协议

#### 设计原则

**任何数值正确性测试的基准必须是固定的解析值/理论值，而非从其他实现计算出的结果。**

理由：
- `stardis-cus3d` 内的 depth-first 求解器 (`sdis_solve_probe`) 与 wavefront 求解器共享**同一个 cuBQL GPU 光追后端**（`s3d_scene_view_trace_ray` → `cus3d_trace_ray_single_multi` → CUDA kernel），两者对比只能证明状态机拆分等价性，不能证明光追后端的正确性
- 解析解来自物理方程的闭式解（Fourier 定律、Laplace 方程、集总参数模型等），与任何实现完全无关
- 已有 CPU 测试的解析参考值可直接复用——它们本就是物理理论值，不是 CPU 实现的计算结果

#### 验证层级

每个 P0 测试对每个探针位置执行两层验证：

```
数值正确性验证（Primary — 决定 PASS/FAIL）:
    wavefront 结果 vs 硬编码解析温度
    容差: |wf_T.E - T_analytic| ≤ 3.0 * wf_T.SE
    通过率: ≥ 95% 探针
    同时验证: (a) wavefront 状态机拆分正确性
             (b) cuBQL GPU 光追后端正确性
             (c) 场景/shader 配置正确性

状态机等价性诊断（Diagnostic — 仅日志输出，不影响测试结果）:
    wavefront 结果 vs depth-first 结果
    容差: |mean_wf - mean_df| ≤ 4σ_combined
    其中 σ_combined = sqrt(SE_wf² + SE_df²)
    用途: 当 Primary 失败时，辅助定位问题是在 wavefront 拆分
          还是在共享的场景/shader 配置
```

#### 诊断矩阵

| Primary | Diagnostic | 含义 |
|---------|------------|------|
| ✅ PASS | ✅ 一致 | 一切正常 |
| ✅ PASS | ❌ 不一致 | 不应出现（两者共享同一后端），检查 RNG/精度 |
| ❌ FAIL | ✅ 一致 | wavefront 和 depth-first **一致地偏离**解析值 → 场景/shader 配置错误（非状态机问题） |
| ❌ FAIL | ❌ 不一致 | wavefront 状态机拆分引入的特有错误 |

### 1.4 共用测试基础设施扩展

在 `test_sdis_b4_e2e_utils.h` 基础上新增 `test_sdis_wf_p0_utils.h`:

```c
/* --- P0 测试配置 --- */
#define P0_NREALISATIONS  10000  /* 每探针实现数 (与 CPU 测试一致) */
#define P0_NPROBES        11     /* 沿 X 轴的探针数 (x=0.0, 0.1, ..., 1.0) */
#define P0_TOL_SIGMA      3.0   /* 解析值容差 3σ (Primary) */
#define P0_DIAG_SIGMA     4.0   /* 状态机等价诊断 4σ (Diagnostic, 不影响通过) */
#define P0_PASS_RATE      0.95  /* 95% 探针通过率 (仅 Primary) */
#define P0_ENABLE_DIAG    1     /* 1=运行 Diagnostic, 0=跳过以加速 */
```

新增工具函数:

```c
/* 运行单探针 wavefront 求解 → 返回 (E, SE) */
static res_T
p0_solve_wavefront_probe
  (struct sdis_scene* scn,
   const double position[3],
   size_t nrealisations,
   size_t picard_order,
   enum sdis_diffusion_algorithm diff_algo,
   struct sdis_estimator** out);

/* 运行单探针 depth-first 求解 (仅用于 Diagnostic) */
static res_T
p0_solve_depthfirst_probe
  (struct sdis_scene* scn,
   const double position[3],
   size_t nrealisations,
   size_t picard_order,
   enum sdis_diffusion_algorithm diff_algo,
   struct sdis_estimator** out);

/* Primary: 解析值比较 (决定 PASS/FAIL) */
typedef double (*p0_analytic_fn)(double x);
static int
p0_compare_analytic
  (struct sdis_estimator* est,
   double expected_T,
   double tol_sigma);

/* Diagnostic: 状态机等价性比较 (仅日志输出) */
static int
p0_diag_compare
  (struct sdis_estimator* est_wf,
   struct sdis_estimator* est_df,
   double tol_sigma);

/* 一键探针扫描: 沿 X 轴 N 个探针，仅 Primary 决定返回值 */
static int
p0_run_probe_sweep
  (struct sdis_scene* scn,
   p0_analytic_fn T_analytic,
   size_t nprobes,
   size_t nrealisations,
   size_t picard_order,
   enum sdis_diffusion_algorithm diff_algo,
   double y_fixed, double z_fixed);
```

---

## 二、测试配置详情

### 共用参数对照

| 参数 | CPU 测试值 | P0 Wavefront | 说明 |
|------|-----------|-------------|------|
| MC 实现数 | 10k-100k (每探针) | 10k (每探针) | 与 CPU 一致 |
| 几何 | 手工 12 三角形 | 同 (box_get_indices) | 复用 test_sdis_utils |
| 探针布局 | 单探针 | 11 探针沿 X 轴 (x=0.0...1.0) | 覆盖完整温度分布 |
| RNG | Threefry | Threefry (默认) | 一致 |
| 扩散算法 | Delta-sphere | Delta-sphere (默认) | 一致 |

---

## 三、WF-A1：固定 T + 固定通量稳态导热

### 3.1 对标 CPU 测试

**CPU**: `test_sdis_flux.c` → `sdis_solve_probe(pos={x, 0.5, 0.5})` → `eq_eps(T.E, ref, 3*T.SE)`

### 3.2 物理场景

```
     PHI=10 (通量)      λ=0.1 (导体)        T0=320K (固定温度)
  ←──────────┤ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ ├──────────→
  x=0 (-X)                                 x=1 (+X)
                 其余四面绝热
```

### 3.3 解析解

$$T(x) = T_0 + (1 - x) \cdot \frac{\Phi}{\lambda} = 320 + 100(1-x) \text{ [K]}$$

温度沿 X 轴线性分布，与 Y/Z 无关。

| 位置 $x$ | 温度 [K] |
|-----------|---------|
| 0.0 | 420 |
| 0.25 | 395 |
| 0.5 | 370 |
| 0.75 | 345 |
| 1.0 | 320 |

### 3.4 Wavefront 场景构建

#### 几何与材料

```c
/* 固体材料: unknown T (MC求解), λ=0.1, cp=2, ρ=25, δ=0.05 */
struct sdis_medium* solid = e2e_create_solid(dev,
    SDIS_TEMPERATURE_NONE,   /* temperature: unknown */
    0.1,                     /* lambda */
    2.0,                     /* cp */
    25.0,                    /* rho */
    0.05,                    /* delta */
    0.0);                    /* power: none */
```

#### 接口分配（12 三角形 → 6 面）

| 三角形索引 | 面 | 接口类型 | 参数 |
|-----------|-----|---------|------|
| 0-1 | Front (-Z) | 绝热 | hc=0, T=NaN, phi=0 |
| 2-3 | Left (-X) | 固定通量 | hc=0, T=NaN, phi=10 |
| 4-5 | Back (+Z) | 绝热 | hc=0, T=NaN, phi=0 |
| 6-7 | Right (+X) | 固定温度 | hc=0, T=320, phi=0 |
| 8-9 | Top (+Y) | 绝热 | hc=0, T=NaN, phi=0 |
| 10-11 | Bottom (-Y) | 绝热 | hc=0, T=NaN, phi=0 |

**关键差异**: CPU 测试使用自定义 `interface_get_phi()` 回调设置通量。E2E utils 的 `e2e_interf_params` 不包含 `phi` 字段。

**解决方案**: 在测试文件内部定义专用的 interface shader，与 CPU `test_sdis_flux.c` 模式完全一致。三种接口：

```c
/* 绝热接口 */
static struct a1_interf a1_adiabatic = { SDIS_TEMPERATURE_NONE, 0.0 };
/* T0 接口 */
static struct a1_interf a1_t0        = { 320.0, 0.0 };
/* PHI 接口 */
static struct a1_interf a1_phi       = { SDIS_TEMPERATURE_NONE, 10.0 };
```

### 3.5 探针布局与求解

沿 X 轴等间距放置 11 个探针，Y=0.5，Z=0.5：

```c
/* 解析温度函数 */
static double wf_a1_analytic(double x)
{
    return 320.0 + (1.0 - x) * 10.0 / 0.1;  /* T0 + (1-x)*PHI/LAMBDA */
}

/* 探针扫描 */
#define A1_T0      320.0
#define A1_PHI     10.0
#define A1_LAMBDA  0.1
#define A1_NPROBES 11

static int test_wf_a1_steady_3d(void)
{
    struct sdis_scene* scn = /* ... 构建场景 ... */;
    size_t i;
    int n_pass_primary = 0, n_pass_diag = 0;

    for(i = 0; i < A1_NPROBES; i++) {
        double x = (double)i / (double)(A1_NPROBES - 1);
        double pos[3] = { x, 0.5, 0.5 };
        double T_ref = wf_a1_analytic(x);

        struct sdis_estimator* est_wf = NULL;

        /* Wavefront 求解 — 直接构造探针任务 */
        struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
        args.nrealisations = P0_NREALISATIONS;
        args.position[0] = pos[0];
        args.position[1] = pos[1];
        args.position[2] = pos[2];
        args.picard_order = 1;
        args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

        OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

        /* Primary: wavefront vs 解析值 */
        {
            struct sdis_mc mc;
            OK(sdis_estimator_get_temperature(est_wf, &mc));
            if(fabs(mc.E - T_ref) <= P0_TOL_SIGMA * mc.SE)
                n_pass_primary++;
            else
                fprintf(stderr,
                    "  probe x=%.2f: wf=%.4f ref=%.4f SE=%.2e "
                    "diff=%.1f sigma\n",
                    x, mc.E, T_ref, mc.SE,
                    mc.SE > 0 ? fabs(mc.E - T_ref) / mc.SE : 999.0);
        }

        /* Diagnostic: wavefront vs depth-first (仅日志) */
        if(P0_ENABLE_DIAG) {
            struct sdis_estimator* est_df = NULL;
            OK(sdis_solve_probe(scn, &args, NULL, &est_df));
            n_pass_diag += p0_diag_compare(est_wf, est_df, P0_DIAG_SIGMA);
            OK(sdis_estimator_ref_put(est_df));
        }

        OK(sdis_estimator_ref_put(est_wf));
    }

    fprintf(stdout, "  Primary:    %d/%d probes pass (%.0f%%)\n",
            n_pass_primary, A1_NPROBES,
            100.0 * n_pass_primary / A1_NPROBES);
    if(P0_ENABLE_DIAG)
        fprintf(stdout, "  Diagnostic: %d/%d probes consistent (%.0f%%)\n",
                n_pass_diag, A1_NPROBES,
                100.0 * n_pass_diag / A1_NPROBES);

    /* 仅 Primary 决定测试通过 */
    CHK((double)n_pass_primary / A1_NPROBES >= P0_PASS_RATE);
    return 1;
}
```

### 3.6 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_CHECK_TEMP → PATH_CND_DS_STEP_TRACE → PATH_CND_DS_STEP_PROCESS
→ ... (重复 DS 步进) ...
→ PATH_BND_DISPATCH → [绝热: PATH_DONE] / [固定T: PATH_DONE] / [固定通量: PATH_BND_POST_ROBIN_CHECK → PATH_DONE]
```

**验证重点**: DS 导热步进 + 绝热/Dirichlet/Neumann 三种边界条件

### 3.7 通过标准

| 维度 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥95% 探针的 `\|wf_T.E - T_analytic\|` ≤ `3 × wf_T.SE` | **PASS/FAIL 决定性** |
| Diagnostic | wavefront ≈ depth-first (4σ) | 仅日志，不影响结果 |
| 失败率 | 每探针 `failure_count / realisation_count` ≤ 0.1% | 辅助检查 |
| 温度范围 | 所有探针 `T.E ∈ [320, 420]` | 辅助检查 |

### 3.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 全部探针温度 = T0 | 通量 BC 未生效，PHI 不传递 |
| 温度梯度方向反转 | 三角形面法线方向错误，-X/+X 接口交换 |
| Primary FAIL + Diag 一致 | wavefront 和 depth-first 一致地偏离解析值 → 场景/shader 配置错误 |
| Primary FAIL + Diag 不一致 | wavefront 状态机拆分引入的特有错误 |
| SE 异常大 | DS 步长 δ 过大或 enclosure 查询失败 |
| 边界探针系统性偏差 | 探针过于接近边界面，MC 收敛差（在 x∈[0.05,0.95] 范围验证） |

---

## 四、WF-A2：体积功率稳态导热

### 4.1 对标 CPU 测试

**CPU**: `test_sdis_volumic_power.c` → `sdis_solve_probe(pos={x, 0.5, 0.5})` → 体积功率二次分布

### 4.2 物理场景

```
  T0=320K ├──── ▓▓▓▓  P=10 W/m³  ▓▓▓▓ ────┤ T0=320K
  x=0 (-X)       x=0.5 (中心)          x=1 (+X)
                   其余四面绝热
```

### 4.3 解析解

$$T(x) = \frac{P}{2\lambda}\left(\frac{1}{4} - (x - 0.5)^2\right) + T_0$$

其中 $x$ 是归一化坐标 $[0, 1]$，重写为:

$$T(x) = \frac{10}{0.2} \left(0.25 - (x-0.5)^2\right) + 320 = 50 \left(0.25 - (x-0.5)^2\right) + 320$$

| 位置 $x$ | 温度 [K] |
|-----------|---------|
| 0.0 | 320.0 |
| 0.25 | 329.375 |
| 0.5 | 332.5 |
| 0.75 | 329.375 |
| 1.0 | 320.0 |

### 4.4 Wavefront 场景构建

#### 几何与材料

```c
/* 固体: unknown T, λ=0.1, P=10 W/m³ */
struct sdis_medium* solid = e2e_create_solid(dev,
    SDIS_TEMPERATURE_NONE, 0.1, 2.0, 25.0,
    1.0/60.0,   /* delta */
    10.0);       /* volumic_power = P0 */
```

#### 接口分配

| 三角形 | 面 | 接口类型 |
|--------|-----|---------|
| 0-1 | Front (-Z) | 绝热 (T=NaN, hc=0) |
| 2-3 | Left (-X) | **固定温度 T0=320** |
| 4-5 | Back (+Z) | 绝热 |
| 6-7 | Right (+X) | **固定温度 T0=320** |
| 8-9 | Top (+Y) | 绝热 |
| 10-11 | Bottom (-Y) | 绝热 |

**实施方案**: 沿用 CPU 测试的自定义 shader + `interfaces[12]` 逐三角形分配模式:

```c
struct sdis_interface* ifaces[12];
ifaces[0] = ifaces[1] = iface_adiabatic;    /* Front -Z */
ifaces[2] = ifaces[3] = iface_t0;           /* Left -X: T=320 */
ifaces[4] = ifaces[5] = iface_adiabatic;    /* Back +Z */
ifaces[6] = ifaces[7] = iface_t0;           /* Right +X: T=320 */
ifaces[8] = ifaces[9] = iface_adiabatic;    /* Top +Y */
ifaces[10] = ifaces[11] = iface_adiabatic;  /* Bottom -Y */
```

### 4.5 探针布局与求解

```c
static double wf_a2_analytic(double x)
{
    double dx = x - 0.5;
    return 10.0 / (2.0 * 0.1) * (0.25 - dx * dx) + 320.0;
}

static int test_wf_a2_steady_3d(void)
{
    struct sdis_scene* scn = /* ... */;
    size_t i;
    int n_pass_primary = 0, n_pass_diag = 0;

    for(i = 0; i < P0_NPROBES; i++) {
        double x = (double)i / (double)(P0_NPROBES - 1);
        double pos[3] = { x, 0.5, 0.5 };
        double T_ref = wf_a2_analytic(x);

        struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
        args.nrealisations = P0_NREALISATIONS;
        d3_set(args.position, pos);
        args.picard_order = 1;
        args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

        struct sdis_estimator *est_wf = NULL;
        OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

        /* Primary: wavefront vs 解析值 */
        n_pass_primary += p0_compare_analytic(est_wf, T_ref, P0_TOL_SIGMA);

        /* Diagnostic: wavefront vs depth-first (仅日志) */
        if(P0_ENABLE_DIAG) {
            struct sdis_estimator *est_df = NULL;
            OK(sdis_solve_probe(scn, &args, NULL, &est_df));
            n_pass_diag += p0_diag_compare(est_wf, est_df, P0_DIAG_SIGMA);
            OK(sdis_estimator_ref_put(est_df));
        }

        OK(sdis_estimator_ref_put(est_wf));
    }

    /* 仅 Primary 决定测试通过 */
    CHK((double)n_pass_primary / P0_NPROBES >= P0_PASS_RATE);
    return 1;
}
```

### 4.6 覆盖的状态机路径

与 WF-A1 相同的 DS 导热路径，额外验证:
- `solid_get_volumic_power` 回调在 DS 步进中正确累积
- 对称 Dirichlet BC 产生对称温度分布

### 4.7 通过标准

与 WF-A1 相同（Primary ≥ 95% 3σ，Diagnostic 仅日志），额外检查:
- 温度分布对称性: `T(x)` ≈ `T(1-x)` 对对称探针对

### 4.8 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 全部探针 T=320K | 体积功率未传递到 MC 路径 |
| 抛物线偏移 | 体积功率累积系数错误（未除以 2λ） |
| 不对称分布 | -X/+X 面接口配置不一致 |
| 中心温度过高 | δ 过大导致步进跨越边界 |

---

## 五、WF-B2：Dirichlet + 对流边界稳态

### 5.1 对标 CPU 测试

**CPU**: `test_sdis_solve_boundary.c` → `sdis_solve_probe_boundary(iprim=6, uv, side=FRONT)` + `sdis_solve_boundary(prims={6,7})`

### 5.2 物理场景

```
  Tb=300K (固定温度)    λ=0.1 (导体)     H=0.5, Tf=310K (对流)
  ├──────────────── ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ ─────────────────┤
  x=0 (-X)                                          x=1 (+X)
                     ← 绝热 (4面) →
```

### 5.3 解析解

一维稳态 $-\lambda T'' = 0$ → $T(x) = c_1 x + c_2$

- $T(0) = T_b = 300$ → $c_2 = 300$
- $-\lambda c_1 = H(T(1) - T_f)$ → $-0.1 c_1 = 0.5(c_1 + 300 - 310)$
- $-0.1 c_1 = 0.5 c_1 - 5$ → $c_1 = 5/0.6 = 8.333...$

$$T(x) = 300 + 8.333x \text{ [K]}$$

| 位置 $x$ | 温度 [K] |
|-----------|---------|
| 0.0 | 300.000 |
| 0.25 | 302.083 |
| 0.5 | 304.167 |
| 0.75 | 306.250 |
| 1.0 | 308.333 |

### 5.4 测试策略

CPU 测试使用 `sdis_solve_probe_boundary` 和 `sdis_solve_boundary`——这两个 API 是**逐面求解**，直接测量边界面温度。wavefront 探针模式求解**内部体积温度** $T(x)$，不直接求边界温度（$x=1$ 处的 $T_{boundary} = 308.333$K）。

**方案 B2**: 使用 `sdis_solve_wavefront_probe` 验证内部线性温度分布 $T(x) = 300 + 8.333x$。验证完整的线性温度分布同时也间接验证了对流边界条件是否正确——如果 Robin BC 不正确，内部温度梯度将偏离解析解。

### 5.5 场景构建

```c
/* 固体材料 */
struct sdis_medium* solid = e2e_create_solid(dev,
    SDIS_TEMPERATURE_NONE, 0.1, 2.0, 25.0, 0.05, 0.0);

/* 流体材料 (对流面外侧) — 仅提供流体温度 */
struct sdis_medium* fluid = e2e_create_fluid(dev, 310.0, 2.0, 25.0);
```

#### 接口分配

| 三角形 | 面 | 接口 | 参数 |
|--------|-----|------|------|
| 0-1 | Front (-Z) | 绝热 | hc=0, T=NaN |
| 2-3 | Left (-X) | **Dirichlet** | hc=0, T=300 |
| 4-5 | Back (+Z) | 绝热 | hc=0, T=NaN |
| 6-7 | Right (+X) | **Robin (对流)** | hc=0.5, T=NaN, front=solid, back=fluid |
| 8-9 | Top (+Y) | 绝热 | hc=0, T=NaN |
| 10-11 | Bottom (-Y) | 绝热 | hc=0, T=NaN |

**对流接口关键配置**: Right 面的 interface 必须连接 solid(front) 和 fluid(back)，`convection_coef = H = 0.5`，流体温度 `Tf = 310`。boundary 求解路径需要走 `PATH_BND_SF_*` 状态链。

### 5.6 探针布局与求解

```c
static double wf_b2_analytic(double x)
{
    double Tb = 300.0, Tf = 310.0, H_conv = 0.5, lam = 0.1;
    double c1 = H_conv * (Tf - Tb) / (H_conv + lam);  /* 8.333... */
    return Tb + c1 * x;
}

static int test_wf_b2_steady_3d(void)
{
    struct sdis_scene* scn = /* ... */;
    size_t i;
    int n_pass_primary = 0, n_pass_diag = 0;

    for(i = 0; i < P0_NPROBES; i++) {
        double x = (double)i / (double)(P0_NPROBES - 1);
        double pos[3] = { x, 0.5, 0.5 };
        double T_ref = wf_b2_analytic(x);

        struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
        args.nrealisations = P0_NREALISATIONS;
        d3_set(args.position, pos);
        args.picard_order = 1;
        args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

        struct sdis_estimator *est_wf = NULL;
        OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

        n_pass_primary += p0_compare_analytic(est_wf, T_ref, P0_TOL_SIGMA);

        if(P0_ENABLE_DIAG) {
            struct sdis_estimator *est_df = NULL;
            OK(sdis_solve_probe(scn, &args, NULL, &est_df));
            n_pass_diag += p0_diag_compare(est_wf, est_df, P0_DIAG_SIGMA);
            OK(sdis_estimator_ref_put(est_df));
        }

        OK(sdis_estimator_ref_put(est_wf));
    }

    CHK((double)n_pass_primary / P0_NPROBES >= P0_PASS_RATE);
    return 1;
}
```

### 5.7 覆盖的状态机路径

```
PATH_INIT → PATH_CND_INIT_ENC → PATH_ENC_QUERY_EMIT
→ PATH_CND_DS_* (导热步进)
→ PATH_BND_DISPATCH
    ├── [绝热面] → PATH_DONE (反射回固体)
    ├── [Dirichlet -X] → PATH_DONE (T = Tb = 300K)
    └── [Robin +X: solid/fluid] → PATH_BND_SF_REINJECT_SAMPLE
        → PATH_BND_SF_PROB_DISPATCH
            ├── [对流吸收] → PATH_CNV_INIT → PATH_CNV_STARTUP_TRACE → ... → PATH_DONE
            └── [导热反射] → PATH_CND_* (继续随机游走)
```

**验证重点**: solid/fluid 边界处的对流概率分派 + 对流路径温度贡献

### 5.8 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥95% 探针 `\|T.E - (300 + 8.333*x)\|` ≤ `3 × T.SE` | **PASS/FAIL** |
| Diagnostic | wavefront ≈ depth-first (4σ) | 仅日志 |

### 5.9 失败诊断

| 症状 | 可能原因 |
|------|---------|
| +X 面温度 = Tf (310K) | 对流 BC 实现为 Dirichlet 而非 Robin |
| 均匀温度 = 300K | 对流未生效，所有路径走 Dirichlet |
| Primary FAIL，分布不线性 | solid/fluid 接口的 `convection_coef` 未传递 |

---

## 六、WF-C1：导热-辐射耦合 Picard1

### 6.1 对标 CPU 测试

**CPU**: `test_sdis_conducto_radiative.c` → 复杂几何（主固体 + 两侧流体包壳），picard_order=1

### 6.2 物理场景

```
  T0=300K ├── 流体包壳 ──┤── 固体(λ=0.1, ε=1) ──┤── 流体包壳 ──┤ T1=310K
  x=-1.5               x=-1                  x=+1               x=+1.5
                         ↕ 辐射 (ε=1, specular=1)
```

### 6.3 解析解

线性化辐射换热系数:

$$h_r = 4 \sigma T_{\text{ref}}^3 \varepsilon = 4 \times 5.6696 \times 10^{-8} \times 300^3 \times 1 = 6.12317 \times 10^{-3} \text{ W/(m²·K)}$$

$$\Delta T = \frac{\lambda}{2\lambda + d \cdot h_r} (T_1 - T_0) = \frac{0.1}{0.2 + 2 \times 6.12317 \times 10^{-3}} \times 10$$

$$T_{s0} = T_0 + \Delta T, \quad T_{s1} = T_1 - \Delta T$$

$$T(x) = T_{s0} + \frac{x - (-1)}{2} (T_{s1} - T_{s0}) = T_{s0} (1-u) + T_{s1} u, \quad u = \frac{x+1}{2}$$

### 6.4 场景构建

CPU 测试使用**非标准几何**（16 顶点，32 三角形 — 主固体 + 两个流体包壳），需直接在测试中构建完整几何（与 CPU 测试的 `get_indices`/`get_position`/`get_interface` 回调一致），通过 `sdis_scene_create` 手工创建场景。

#### 场景构建概要

```c
/* 几何: 自定义 (16 顶点, 32 三角形) */
/* 固体 [-1,-1,-1] → [+1,+1,+1]
 * 左包壳 [-1.5,-1,-1] → [-1,+1,+1]
 * 右包壳 [+1,-1,-1] → [+1.5,+1,+1]
 */

/* 5种接口:
 * [0] solid/solid 绝热: emissivity=-1, convection=-1
 * [1] solid/fluid 辐射面: emissivity=1, specular=1, convection=0
 * [2] fluid/solid 完美反射: emissivity=0, specular=1
 * [3] T0 Dirichlet + 辐射: T=300, emissivity=1
 * [4] T1 Dirichlet + 辐射: T=310, emissivity=1
 */
```

### 6.5 探针布局与求解

探针仅放置在**固体区域** $x \in [-1, 1]$：

```c
static double wf_c1_analytic(double x)
{
    /* 只验证固体区域 x ∈ [-1, 1] */
    double lambda = 0.1, d = 2.0, eps = 1.0, Tref = 300.0;
    double T0 = 300.0, T1 = 310.0;
    double hr = 4.0 * 5.6696e-8 * Tref * Tref * Tref * eps;
    double dT = lambda / (2.0 * lambda + d * hr) * (T1 - T0);
    double Ts0 = T0 + dT, Ts1 = T1 - dT;
    double u = (x + 1.0) / d;
    return Ts0 * (1.0 - u) + Ts1 * u;
}

#define C1_NPROBES 9   /* x = -1.0, -0.75, ..., 0, ..., 0.75, 1.0 */

static int test_wf_c1_steady_3d(void)
{
    struct sdis_scene* scn = /* ... 手工构建 32-tri 场景 ... */;
    size_t i;
    int n_pass_primary = 0, n_pass_diag = 0;

    for(i = 0; i < C1_NPROBES; i++) {
        double x = -1.0 + 2.0 * (double)i / (double)(C1_NPROBES - 1);
        double pos[3] = { x, 0.0, 0.0 };  /* 固体中心线 */
        double T_ref = wf_c1_analytic(x);

        struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
        args.nrealisations = 20000;  /* 辐射路径方差大, 增加实现数 */
        d3_set(args.position, pos);
        args.picard_order = 1;       /* picard1 线性辐射 */
        args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;

        struct sdis_estimator *est_wf = NULL;
        OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

        n_pass_primary += p0_compare_analytic(est_wf, T_ref, P0_TOL_SIGMA);

        if(P0_ENABLE_DIAG) {
            struct sdis_estimator *est_df = NULL;
            OK(sdis_solve_probe(scn, &args, NULL, &est_df));
            n_pass_diag += p0_diag_compare(est_wf, est_df, P0_DIAG_SIGMA);
            OK(sdis_estimator_ref_put(est_df));
        }

        OK(sdis_estimator_ref_put(est_wf));
    }

    CHK((double)n_pass_primary / C1_NPROBES >= P0_PASS_RATE);
    return 1;
}
```

### 6.6 Picard 阶数配置

```c
/* picard_order = 1: 使用常数 Tref=300K 线性化辐射 */
args.picard_order = 1;
```

### 6.7 覆盖的状态机路径

```
PATH_INIT → PATH_RAD_TRACE_PENDING → PATH_RAD_PROCESS_HIT
→ PATH_BND_DISPATCH
    └── [solid/fluid 辐射面] → PATH_BND_SF_REINJECT_SAMPLE
        → PATH_BND_SF_REINJECT_ENC → PATH_BND_SF_PROB_DISPATCH
            ├── [辐射吸收] → PATH_RAD_TRACE_PENDING (辐射路径继续)
            ├── [导热反射] → PATH_CND_* (导热步进)
            └── [null-collision] → PATH_BND_SF_NULLCOLL_RAD_TRACE → PATH_BND_SF_NULLCOLL_DECIDE
```

**验证重点**: 辐射路径 + solid/fluid picard1 边界 + null-collision 辐射子射线

### 6.8 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** | ≥95% **固体区**探针在 3σ 内 | **PASS/FAIL** |
| Diagnostic | wavefront ≈ depth-first (4σ) | 仅日志 |

### 6.9 失败诊断

| 症状 | 可能原因 |
|------|---------|
| 温度全部 = (T0+T1)/2 = 305K | 辐射未生效，退化为纯导热 |
| ΔT 偏差 > 10% | $h_r$ 计算错误 (Boltzmann 常数、Tref 幂次) |
| 包壳区温度异常 | 流体包壳边界配置错误 |
| picard 收敛震荡 | picard_order 与 Tref 不匹配 |

---

## 七、WF-D1：均匀对流六面冷却

### 7.1 对标 CPU 测试

**CPU**: `test_sdis_convection.c` → `sdis_solve_probe(pos={0.25, 0.25, 0.25}, time=t)` → 指数衰减

### 7.2 物理场景

```
          T4=340K (+Z)
             ↓
  T0=300K ← ▓▓ → T1=310K    单位立方体流体
  (-X)    ▓▓  ▓▓    (+X)     H=10 W/(m²·K)
          T2=320K  T3=330K   初温 Tf_0=280K
          (-Y)     (+Y)
             ↑
          T5=350K (-Z)
```

### 7.3 解析解

$$\nu = \frac{6 \times H}{\rho c_p} = \frac{60}{50} = 1.2 \text{ s}^{-1}$$

$$T_\infty = \frac{300+310+320+330+340+350}{6} = 325.0 \text{ K}$$

$$T(t) = 280 e^{-1.2t} + 325 (1-e^{-1.2t})$$

| 时间 | 温度 [K] |
|------|---------|
| $t = \infty$ (稳态) | 325.000 |
| $t = 1/\nu$ | 308.447 |
| $t = 2/\nu$ | 318.912 |

### 7.4 测试策略

对流问题是**流体**内温度求解，MC 路径不涉及固体导热步进（无 DS/WoS），仅在流体体积内经历时间积分 + 对流边界吸收。

**方案 D1（稳态先行）**:

1. 先测稳态 ($t = \infty$): 所有探针应收敛到 $T_\infty = 325$ K，与空间位置无关（集总参数模型）
2. 稳态成功后再测瞬态时间点

稳态测试简化:
- `time_range = {DBL_MAX, DBL_MAX}`（稳态）
- 解析参考 = 常数 325 K

### 7.5 场景构建

```c
/* 流体材料 (稳态模式: 温度 = NONE → MC 求解) */
/* 瞬态模式: t ≤ 0 时返回 Tf_0=280K, 否则 NONE */
```

**关键**: 稳态模式下流体温度回调必须返回 `SDIS_TEMPERATURE_NONE`，瞬态模式下对 $t \leq 0$ 返回初温 $T_{f,0}$。需自定义 fluid shader（参照 CPU `test_sdis_convection.c` 的 `is_stationary` 模式）。

#### 接口分配（6 面各有不同温度）

| 三角形 | 面 | 接口温度 | 对流系数 |
|--------|-----|---------|---------|
| 0-1 | Front (-Z) | T5=350 | H=10 |
| 2-3 | Left (-X) | T0=300 | H=10 |
| 4-5 | Back (+Z) | T4=340 | H=10 |
| 6-7 | Right (+X) | T1=310 | H=10 |
| 8-9 | Top (+Y) | T3=330 | H=10 |
| 10-11 | Bottom (-Y) | T2=320 | H=10 |

**6 个不同的 interface 实例**:

```c
struct sdis_interface* iface[6];
double temps[6] = {350, 300, 340, 310, 330, 320};
for(int i = 0; i < 6; i++) {
    iface[i] = e2e_create_interface(dev,
        solid_wall,  /* 外侧固体 (温度=temps[i]) */
        fluid,       /* 内侧流体 */
        H,           /* hc=10 */
        0.0,         /* epsilon=0 (纯对流) */
        0.0,         /* specular=0 */
        300.0);      /* ref_temp (unused for eps=0) */
}
```

**注意**: CPU 测试中接口是 `fluid/solid` 类型（fluid 在前），interface 的 front/back medium 顺序取决于三角形法线方向。

### 7.6 探针布局与求解（稳态）

集总参数模型下稳态温度与空间位置无关，放置若干探针验证:

```c
static double wf_d1_steady_analytic(double x)
{
    (void)x;
    return (300.0 + 310.0 + 320.0 + 330.0 + 340.0 + 350.0) / 6.0;  /* 325K */
}

#define D1_NPROBES_STEADY 5

static int test_wf_d1_steady_3d(void)
{
    struct sdis_scene* scn = /* ... */;
    /* 探针分布在 box 内部不同位置 */
    double probe_positions[5][3] = {
        {0.25, 0.25, 0.25},
        {0.75, 0.25, 0.25},
        {0.50, 0.50, 0.50},
        {0.25, 0.75, 0.75},
        {0.75, 0.75, 0.75}
    };
    size_t i;
    int n_pass_primary = 0, n_pass_diag = 0;

    for(i = 0; i < D1_NPROBES_STEADY; i++) {
        double T_ref = 325.0;

        struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
        args.nrealisations = 100000;  /* 对流路径需高实现数 */
        d3_set(args.position, probe_positions[i]);
        args.picard_order = 1;
        args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;
        /* 稳态: time_range 使用默认 {DBL_MAX, DBL_MAX} */

        struct sdis_estimator *est_wf = NULL;
        OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

        n_pass_primary += p0_compare_analytic(est_wf, T_ref, P0_TOL_SIGMA);

        if(P0_ENABLE_DIAG) {
            struct sdis_estimator *est_df = NULL;
            OK(sdis_solve_probe(scn, &args, NULL, &est_df));
            n_pass_diag += p0_diag_compare(est_wf, est_df, P0_DIAG_SIGMA);
            OK(sdis_estimator_ref_put(est_df));
        }

        OK(sdis_estimator_ref_put(est_wf));
    }

    CHK((double)n_pass_primary / D1_NPROBES_STEADY >= P0_PASS_RATE);
    return 1;
}
```

### 7.7 瞬态验证（可选扩展）

稳态 P0 通过后，扩展为瞬态测试:

```c
double times[] = { 1.0/1.2, 2.0/1.2, 3.0/1.2, 4.0/1.2 };

for(int i = 0; i < 4; i++) {
    args.time_range[0] = times[i];
    args.time_range[1] = times[i];
    /* 切换为非稳态模式 (fluid shader 对 t<=0 返回 Tf_0=280) */
    double ref = 280.0 * exp(-1.2 * times[i])
               + 325.0 * (1.0 - exp(-1.2 * times[i]));
    /* 验证 */
}
```

### 7.8 覆盖的状态机路径

```
PATH_INIT → PATH_CNV_INIT → PATH_CNV_STARTUP_TRACE → PATH_CNV_STARTUP_RESULT
→ PATH_CNV_SAMPLE_LOOP (时间积分循环)
→ PATH_DONE (边界吸收, 温度 = 对流面温度)
```

**验证重点**: 纯对流路径（无导热），6 面不同温度的加权

### 7.9 通过标准

| 验证 | 条件 | 角色 |
|------|------|------|
| **Primary** (稳态) | ≥95% 探针 `|T.E - 325|` ≤ `3 × T.SE` | **PASS/FAIL** |
| Diagnostic | wavefront ≈ depth-first (4σ) | 仅日志 |

### 7.10 失败诊断

| 症状 | 可能原因 |
|------|---------|
| T = 280K (初温) | 稳态模式下流体温度回调未返回 SDIS_TEMPERATURE_NONE |
| T = Tf_i (某面温度) | 对流路径立即终止，未与多面交互 |
| T ≠ 325 但接近 | 面积/温度加权计算错误（非均匀 H_i 但这里 H 都相同） |
| SE 非常大 | 对流路径步长或终止条件有问题 |

---

## 八、测试文件结构与 CMake 注册

### 8.1 文件命名

```
stardis-cus3d/stardis-solver/0.16.2/src/
├── test_sdis_wf_p0_utils.h        ← P0 共用工具 (探针求解, 解析比较)
├── test_sdis_wf_a1_flux.c         ← WF-A1
├── test_sdis_wf_a2_volumic.c      ← WF-A2
├── test_sdis_wf_b2_boundary.c     ← WF-B2
├── test_sdis_wf_c1_condrad.c      ← WF-C1
└── test_sdis_wf_d1_convection.c   ← WF-D1
```

### 8.2 CMake 注册

```cmake
# P0 wavefront numerical correctness tests
set(WF_P0_TESTS
    test_sdis_wf_a1_flux
    test_sdis_wf_a2_volumic
    test_sdis_wf_b2_boundary
    test_sdis_wf_c1_condrad
    test_sdis_wf_d1_convection)

foreach(test ${WF_P0_TESTS})
    add_executable(${test} ${test}.c)
    target_link_libraries(${test} PRIVATE sdis_obj rsys ssp s3d)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES
        TIMEOUT 300
        LABELS "wf;p0;numerical")
endforeach()
```

### 8.3 每个测试的 main() 模板

```c
#include "sdis.h"
#include "test_sdis_utils.h"
#include "test_sdis_wf_p0_utils.h"

/* ... shader 定义 (自定义 interface 回调) ... */
/* ... 解析温度函数 T(x) ... */

static int test_wf_XX_steady_3d(void) { /* ... */ }

int main(void)
{
    int pass = 1;
    pass &= test_wf_XX_steady_3d();
    return pass ? 0 : 1;
}
```

---

## 九、实施顺序与依赖

```
┌──────────────────────────────────────────────────────────┐
│ Step 0: 基础设施                                          │
│   ① 实现 sdis_solve_wavefront_probe 公共 API              │
│      - init_paths_from_probe()                            │
│      - collect_results_probe()                            │
│      - solve_wavefront_probe_impl() (wavefront 主循环)    │
│   ② 实现 test_sdis_wf_p0_utils.h                         │
│      - p0_compare_analytic()                              │
│      - p0_diag_compare() (Diagnostic, 可选)               │
│   ③ 验证空场景探针求解正确 (smoke test)                    │
│   产出: sdis_solve_wavefront.c 新增函数 + utils header    │
└──────────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│ Step 1: WF-A1 (最简场景)                                  │
│   纯导热 + 3种边界 (绝热/Dirichlet/Neumann)               │
│   验证: DS步进 + 基本边界处理                               │
│   阻塞: WF-A2, WF-B2 (共享DS导热路径)                     │
└──────────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│ Step 2: WF-A2 (体积功率)                                  │
│   DS步进 + 体积功率累积                                    │
│   验证: solid_get_volumic_power 回调 + 二次分布             │
│   独立于 B2/C1/D1                                         │
└──────────────────────────────────────────────────────────┘
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
┌───────────────────────┐  ┌──────────────────────┐
│ Step 3a: WF-B2         │  │ Step 3b: WF-D1       │
│ (对流边界)              │  │ (纯对流路径)          │
│ 验证: SF boundary      │  │ 验证: CNV 路径        │
│ + Robin BC             │  │ + 多面温度加权        │
│ 可与D1并行开发          │  │ 可与B2并行开发        │
└───────────────────────┘  └──────────────────────┘
              │                       │
              └───────────┬───────────┘
                          ▼
┌──────────────────────────────────────────────────────────┐
│ Step 4: WF-C1 (所有P0中最复杂)                            │
│   辐射 + 导热 + Picard1 + 非标准几何                       │
│   前置: WF-A1 (DS导热) + WF-B2 (SF边界) 均已通过          │
│   验证: 辐射路径 + null-collision                          │
└──────────────────────────────────────────────────────────┘
```

---

## 十、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| `sdis_solve_wavefront_probe` API 实现复杂度 | 阻塞所有 P0 测试 | 复用现有 `solve_tile_wavefront` 的 advance/collect/trace/distribute；仅新增 `init_paths_from_probe` 和 `collect_results_probe`（各 ~50 行） |
| enclosure_id 初始化失败 | 探针位于 box 外部或面上 | 探针位置严格在 box 内部 (避免 x=0.0 / x=1.0 精确边界) |
| wavefront/depth-first RNG 不同序 | Diagnostic 不严格一致 | Diagnostic 使用 4σ 统计容差，不影响测试通过 |
| 非标准几何 (WF-C1) 构建复杂 | 开发时间长 | 直接从 CPU `test_sdis_conducto_radiative.c` 复制 shader/geometry 回调 |
| 自定义 shader 与 E2E 工厂不兼容 | 代码重复 | WF-A1/B2/D1 需自定义 shader；仅 WF-A2 可完全用 E2E 工厂 |
| 对流测试高实现数导致运行时间长 | CI 超时 | WF-D1 使用 100k realisations，超时设 300s |

---

## 附录 A：`test_sdis_wf_p0_utils.h` 接口草案

```c
#ifndef TEST_SDIS_WF_P0_UTILS_H
#define TEST_SDIS_WF_P0_UTILS_H

#include "sdis.h"
#include "test_sdis_utils.h"
#include <math.h>
#include <stdio.h>
#include <float.h>

/* ========== 配置 ========== */
#define P0_NREALISATIONS  10000   /* 每探针实现数 (与 CPU 测试一致) */
#define P0_NPROBES        11      /* 标准 X 轴扫描探针数 */
#define P0_TOL_SIGMA      3.0    /* Primary: 解析值容差 3σ */
#define P0_DIAG_SIGMA     4.0    /* Diagnostic: 状态机等价 4σ (不影响通过) */
#define P0_PASS_RATE      0.95   /* 95% 探针通过率 (仅 Primary) */
#define P0_ENABLE_DIAG    1      /* 1=运行 Diagnostic, 0=跳过以加速 */

/* ========== Primary: 解析值比较 (决定 PASS/FAIL) ========== */
/* 返回 1 = pass, 0 = fail */
static int
p0_compare_analytic
  (struct sdis_estimator* est,
   double expected_T,
   double tol_sigma)
{
  struct sdis_mc mc;
  if(sdis_estimator_get_temperature(est, &mc) != RES_OK) return 0;
  if(mc.SE < 1e-15) return fabs(mc.E - expected_T) < 1e-10;
  return fabs(mc.E - expected_T) <= tol_sigma * mc.SE;
}

/* ========== Diagnostic: 状态机等价性 (仅日志输出) ========== */
/* 返回 1 = consistent, 0 = inconsistent */
static int
p0_diag_compare
  (struct sdis_estimator* est_wf,
   struct sdis_estimator* est_df,
   double tol_sigma)
{
  struct sdis_mc mc_wf, mc_df;
  double sigma_comb;

  if(sdis_estimator_get_temperature(est_wf, &mc_wf) != RES_OK) return 0;
  if(sdis_estimator_get_temperature(est_df, &mc_df) != RES_OK) return 0;

  sigma_comb = sqrt(mc_wf.SE * mc_wf.SE + mc_df.SE * mc_df.SE);
  if(sigma_comb < 1e-15) return fabs(mc_wf.E - mc_df.E) < 1e-10;
  return fabs(mc_wf.E - mc_df.E) <= tol_sigma * sigma_comb;
}

/* ========== 诊断打印 ========== */
static void
p0_print_probe_result
  (double x,
   struct sdis_estimator* est_wf,
   struct sdis_estimator* est_df,   /* 可为 NULL (P0_ENABLE_DIAG=0) */
   double T_ref)
{
  struct sdis_mc mc_wf = {0}, mc_df = {0};
  (void)sdis_estimator_get_temperature(est_wf, &mc_wf);
  if(est_df)
    (void)sdis_estimator_get_temperature(est_df, &mc_df);

  fprintf(stdout,
    "  x=%.3f  wf=%.4f (SE=%.2e)  ref=%.4f  primary=%.1f sigma",
    x, mc_wf.E, mc_wf.SE, T_ref,
    mc_wf.SE > 0 ? fabs(mc_wf.E - T_ref) / mc_wf.SE : 0.0);

  if(est_df) {
    double sc = sqrt(mc_wf.SE*mc_wf.SE + mc_df.SE*mc_df.SE);
    fprintf(stdout, "  df=%.4f  diag=%.1f sigma",
      mc_df.E, sc > 0 ? fabs(mc_wf.E - mc_df.E) / sc : 0.0);
  }
  fprintf(stdout, "\n");
}

/* ========== 一键探针扫描: 仅 Primary 决定返回值 ========== */
typedef double (*p0_analytic_fn)(double x);

static int
p0_run_probe_sweep
  (struct sdis_scene*    scn,
   p0_analytic_fn        T_analytic,
   size_t                nprobes,
   size_t                nrealisations,
   size_t                picard_order,
   enum sdis_diffusion_algorithm diff_algo,
   double                y_fixed,
   double                z_fixed)
{
  size_t i;
  int n_pass_primary = 0, n_pass_diag = 0;

  fprintf(stdout, "  Running %lu probes, %lu realisations each ...\n",
    (unsigned long)nprobes, (unsigned long)nrealisations);

  for(i = 0; i < nprobes; i++) {
    double x = (double)i / (double)(nprobes - 1);
    double pos[3];
    double T_ref;

    struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    struct sdis_estimator *est_wf = NULL, *est_df = NULL;

    pos[0] = x; pos[1] = y_fixed; pos[2] = z_fixed;
    T_ref = T_analytic(x);

    args.nrealisations = nrealisations;
    args.position[0] = pos[0];
    args.position[1] = pos[1];
    args.position[2] = pos[2];
    args.picard_order = picard_order;
    args.diff_algo = diff_algo;

    /* Wavefront 求解 */
    OK(sdis_solve_wavefront_probe(scn, &args, &est_wf));

    /* Primary: wavefront vs 解析值 */
    n_pass_primary += p0_compare_analytic(est_wf, T_ref, P0_TOL_SIGMA);

    /* Diagnostic: wavefront vs depth-first (可选, 仅日志) */
    if(P0_ENABLE_DIAG) {
      OK(sdis_solve_probe(scn, &args, NULL, &est_df));
      n_pass_diag += p0_diag_compare(est_wf, est_df, P0_DIAG_SIGMA);
    }

    /* 诊断输出 */
    p0_print_probe_result(x, est_wf, est_df, T_ref);

    OK(sdis_estimator_ref_put(est_wf));
    if(est_df)
      OK(sdis_estimator_ref_put(est_df));
  }

  fprintf(stdout, "  Primary:    %d/%lu probes pass (%.1f%%)\n",
    n_pass_primary, (unsigned long)nprobes,
    100.0 * (double)n_pass_primary / (double)nprobes);

  if(P0_ENABLE_DIAG)
    fprintf(stdout, "  Diagnostic: %d/%lu probes consistent (%.1f%%)\n",
      n_pass_diag, (unsigned long)nprobes,
      100.0 * (double)n_pass_diag / (double)nprobes);

  /* 仅 Primary 决定测试通过 */
  return (double)n_pass_primary / (double)nprobes >= P0_PASS_RATE;
}

#endif /* TEST_SDIS_WF_P0_UTILS_H */
```

---

## 附录 B：P0 测试状态机路径覆盖矩阵

| `path_phase` | WF-A1 | WF-A2 | WF-B2 | WF-C1 | WF-D1 |
|:-------------|:-----:|:-----:|:-----:|:-----:|:-----:|
| `PATH_INIT` | ● | ● | ● | ● | ● |
| `PATH_CND_INIT_ENC` | ● | ● | ● | ● | |
| `PATH_ENC_QUERY_EMIT` | ● | ● | ● | ● | |
| `PATH_CND_DS_CHECK_TEMP` | ● | ● | ● | ● | |
| `PATH_CND_DS_STEP_TRACE` | ● | ● | ● | ● | |
| `PATH_CND_DS_STEP_PROCESS` | ● | ● | ● | ● | |
| `PATH_CND_DS_STEP_ADVANCE` | ● | ● | ● | ● | |
| `PATH_BND_DISPATCH` | ● | ● | ● | ● | |
| `PATH_BND_POST_ROBIN_CHECK` | ● | | ● | | |
| `PATH_BND_SS_REINJECT_*` | | | | ● | |
| `PATH_BND_SF_REINJECT_SAMPLE` | | | ● | ● | |
| `PATH_BND_SF_REINJECT_ENC` | | | ● | ● | |
| `PATH_BND_SF_PROB_DISPATCH` | | | ● | ● | |
| `PATH_BND_SF_NULLCOLL_RAD_TRACE` | | | | ● | |
| `PATH_BND_SF_NULLCOLL_DECIDE` | | | | ● | |
| `PATH_RAD_TRACE_PENDING` | | | | ● | |
| `PATH_RAD_PROCESS_HIT` | | | | ● | |
| `PATH_CNV_INIT` | | | ● | | ● |
| `PATH_CNV_STARTUP_TRACE` | | | ● | | ● |
| `PATH_CNV_STARTUP_RESULT` | | | ● | | ● |
| `PATH_CNV_SAMPLE_LOOP` | | | ● | | ● |
| `PATH_DONE` | ● | ● | ● | ● | ● |

**覆盖**: 5 个 P0 测试共覆盖 ~22/45 个 `path_phase` 状态（~49%），涵盖所有主要路径类型（导热/辐射/对流/边界分派）。

---

## 附录 C：P0 解析参考值完整表

以下解析值来自物理方程的闭式解，与任何实现（CPU/GPU）无关。

### C.1 WF-A1：一维稳态导热 + 通量 BC (Fourier 定律)

| PDE | 边界条件 | 闭式解 |
|-----|----------|--------|
| $-\lambda T'' = 0$ | $-\lambda T'(0)=\Phi$, $T(1)=T_0$ | $T(x) = T_0 + (1-x)\frac{\Phi}{\lambda}$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $T_0$ | 320 | K |
| $\Phi$ | 10 | W/m² |
| $\lambda$ | 0.1 | W/(m·K) |

**代入**: $T(x) = 320 + 100(1-x)$

| $x$ | 0.0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1.0 |
|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
| $T$ [K] | 420 | 410 | 400 | 390 | 380 | 370 | 360 | 350 | 340 | 330 | 320 |

### C.2 WF-A2：含源项稳态导热 (Poisson 方程)

| PDE | 边界条件 | 闭式解 |
|-----|----------|--------|
| $-\lambda T'' = P$ | $T(0)=T_0$, $T(1)=T_0$ | $T(x) = \frac{P}{2\lambda}\left(\frac{1}{4}-(x-0.5)^2\right)+T_0$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $T_0$ | 320 | K |
| $P$ | 10 | W/m³ |
| $\lambda$ | 0.1 | W/(m·K) |

**代入**: $T(x) = 50(0.25-(x-0.5)^2)+320$

| $x$ | 0.0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1.0 |
|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
| $T$ [K] | 320.0 | 324.0 | 327.0 | 329.0 | 330.5 | 332.5 | 330.5 | 329.0 | 327.0 | 324.0 | 320.0 |

### C.3 WF-B2：Robin 边界条件 (Dirichlet + 对流)

| PDE | 边界条件 | 闭式解 |
|-----|----------|--------|
| $-\lambda T'' = 0$ | $T(0)=T_b$, $-\lambda T'(1)=H(T(1)-T_f)$ | $T(x) = T_b + \frac{H(T_f-T_b)}{H+\lambda}x$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $T_b$ | 300 | K |
| $T_f$ | 310 | K |
| $H$ | 0.5 | W/(m²·K) |
| $\lambda$ | 0.1 | W/(m·K) |

**代入**: $c_1 = 0.5 \times 10 / 0.6 = 8.\overline{3}$, $T(x)=300+8.\overline{3}x$

| $x$ | 0.0 | 0.25 | 0.5 | 0.75 | 1.0 |
|-----|-----|------|-----|------|-----|
| $T$ [K] | 300.000 | 302.083 | 304.167 | 306.250 | 308.333 |

### C.4 WF-C1：线性化辐射-导热耦合 (Picard-1)

| PDE | 边界条件 | 线性化换热系数 |
|-----|----------|---------------|
| $-\lambda T'' = 0$ (x∈[-1,1]) | 固体-流体辐射边界 | $h_r = 4\sigma T_{ref}^3 \varepsilon$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $\lambda$ | 0.1 | W/(m·K) |
| $\varepsilon$ | 1.0 | — |
| $T_{ref}$ | 300 | K |
| $T_0$ | 300 | K |
| $T_1$ | 310 | K |
| $d$ (thickness) | 2.0 | m |
| $\sigma$ (Stefan-Boltzmann) | 5.6696×10⁻⁸ | W/(m²·K⁴) |

**代入**:
1. $h_r = 4 \times 5.6696 \times 10^{-8} \times 300^3 \times 1 = 6.12317 \times 10^{-3}$
2. $\Delta T = \frac{0.1}{0.2 + 2 \times 6.12317 \times 10^{-3}} \times 10 = \frac{0.1}{0.21225} \times 10 = 4.7114$
3. $T_{s0} = 304.711$, $T_{s1} = 305.289$
4. $T(x) = T_{s0}(1-u) + T_{s1}u$, $u = (x+1)/2$

### C.5 WF-D1：集总参数对流冷却 (Lumped Capacitance)

| ODE | 稳态解 | 瞬态解 |
|-----|----------|--------|
| $\rho c_p \frac{dT}{dt} = \sum H_i(T_i - T)$ | $T_\infty = \frac{\sum T_i}{N}$ | $T(t) = T_{f,0}e^{-\nu t}+T_\infty(1-e^{-\nu t})$ |

| 参数 | 值 | 单位 |
|------|------|------|
| $T_0 \ldots T_5$ | 300,310,320,330,340,350 | K |
| $H$ (uniform) | 10 | W/(m²·K) |
| $\rho$ | 25 | kg/m³ |
| $c_p$ | 2 | J/(kg·K) |
| $T_{f,0}$ | 280 | K |

**代入**: $T_\infty = 1950/6 = \mathbf{325.0}$ K，$\nu = 60/50 = 1.2$ s⁻¹

稳态解与位置无关 — 任意探针位置均应收敛到 325.0 K。

---

*文档更新: 2026-02-15 | v3: 解析值为唯一权威基准，Diagnostic 降级 | 作者: GPU Wavefront 测试设计*
