# GPU Wavefront 数值正确性测试清单

**生成时间**: 2026-02-14  
**关联文档**: [phase_b4_test_design.md](phase_b4_test_design.md)（架构级测试）  
**分类实验原理**: [wf_numerical_tests/](wf_numerical_tests/)  
**范围**: 基于 CPU 求解器完整测试体系，建立 GPU Wavefront 的独立数值正确性验证  
**目标**: wavefront 产出的温度/通量/功率值在物理上正确，不依赖 depth-first 作为唯一基准

---

## 一、背景：CPU 求解器的数值正确性保障体系

CPU 版共 **31 个求解器测试**（28 常规 + 3 长时间），覆盖 **13 个求解器 API 入口**。

### 1.1 三层验证防线

| 层 | 方法 | 说明 |
|---|---|---|
| **L1: 对解析解** | MC 估计值 vs 数学精确解 | 物理正确性的根本保证 |
| **L2: Green 函数一致性** | 直接求解 vs Green 重解算 | 两条独立码路径交叉验证 |
| **L3: 序列化往返** | 写文件 → 读回 → 重解算 | 数据完整性 |

### 1.2 容差策略

| 策略 | 表达式 | 适用范围 |
|---|---|---|
| **3σ MC 标准误差** | `eq_eps(T.E, ref, T.SE * 3)` | 绝大多数物理测试 |
| **1σ** | `eq_eps(T.E, ref, T.SE)` | 零方差/低方差（均匀场） |
| **固定 1e-6** | `eq_eps(x, ref, 1.e-6)` | 几何计算 |
| **精确相等** | `CHK(T.E == Text)` | 零方差析出 |
| **温度范围 1%** | `eq_eps(T.E, ref, (Tmax-Tmin)*0.01)` | 数值参考（非解析） |
| **4σ + 95% 像素** | 组合 SE + 像素通过率 | B4 架构等价性测试 |

### 1.3 参考值来源

| 类型 | CPU 测试 |
|---|---|
| 解析解（稳态导热） | flux, volumic_power, contact_resistance, solve_boundary |
| 解析解（瞬态） | convection, unsteady_1d, unsteady_analytic_profile |
| 线性化辐射解析解 | conducto_radiative |
| 硬编码数值参考 | picard, unsteady, external_flux |
| 物理退化一致性 | solve_probe（均匀场→T=Tf）, enclosure_limit |
| 第三方工业验证 | volumic_power2（EDF Syrthès） |

---

## 二、GPU Wavefront 测试的核心设计决策

### 2.1 公开 `solve_tile_wavefront` 为 SDIS 公共接口

CPU 测试通过 `sdis_solve_probe(pos)` 等逐点 API 验证正确性——这些 API 在 wavefront 架构下不存在等价物。为了可测试性和一致性，将 wavefront 求解器通过一个新的公共接口直接暴露：

```c
/* sdis.h 新增 */
SDIS_API res_T
sdis_solve_wavefront_probe
  (struct sdis_scene* scn,
   const struct sdis_solve_wavefront_probe_args* args,
   struct sdis_estimator** estimator);
```

**设计要点**：

- 接受单个探针位置，内部构建 1×1 虚拟相机，通过 `solve_tile_wavefront` 执行
- 输出标准 `sdis_estimator`（含 `.E` / `.SE`），与 CPU `sdis_solve_probe` 输出格式一致
- 测试代码可直接复用 CPU 测试的 `eq_eps(T.E, ref, T.SE * 3)` 验证模式
- 同时保留 `sdis_solve_camera` 的像素缓冲区验证路径（多探针/全图）

可选的扩展接口：

```c
/* 批量探针——内部映射到正交相机像素 */
SDIS_API res_T
sdis_solve_wavefront_probe_list
  (struct sdis_scene* scn,
   const struct sdis_solve_wavefront_probe_list_args* args,
   struct sdis_estimator_buffer** buf);

/* 边界探针——wavefront 版 */
SDIS_API res_T
sdis_solve_wavefront_boundary
  (struct sdis_scene* scn,
   const struct sdis_solve_wavefront_boundary_args* args,
   struct sdis_estimator** estimator);
```

### 2.2 双重验证协议

每个 `WF-*` 测试执行两层验证：

```
验证 A: wavefront 结果 vs 解析解  (物理正确性)
    eq_eps(wf_result.E, analytic_ref, 3 * wf_result.SE)
    通过率 ≥ 95% (多探针/多像素场景)

验证 B: wavefront vs depth-first (架构等价性)
    逐像素 4σ 统计兼容
    通过率 ≥ 95%
```

验证 A 是根本保证；验证 B 是回归安全网。如果 depth-first 有 bug，只有验证 A 能检测到。

### 2.3 不可直接映射到 wavefront 的场景

| 场景 | 原因 | 策略 |
|---|---|---|
| 介质平均温度 (`solve_medium`) | 需要体积积分 | 新增 `sdis_solve_wavefront_medium` 或保留 CPU 测试 |
| 体积功率积分 (`compute_power`) | 纯积分无光追 | 保留 CPU 测试（与光追后端无关） |
| Green 函数验证 | Wavefront 无 Green 路径 | 保留 CPU depth-first 测试 |
| 面通量分量 (CF/RF/TF) | 相机只输出温度 | 扩展通道或保留 CPU 测试 |
| 自定义路径采样回调 | 涉及函数指针复杂性 | 后期扩展 |

---

## 三、CPU 测试全景图（31 个测试按物理分类）

### 类别 A：纯导热稳态（7 个测试）

详细原理：[wf_numerical_tests/cat_A_steady_conduction.md](wf_numerical_tests/cat_A_steady_conduction.md)

| # | CPU 测试 | 物理场景 | 解析参考 | N | 维度 |
|---|---|---|---|---|---|
| A1 | `test_sdis_flux` | 固定T + 固定通量 | $T = T_0 + (1-x)\Phi/\lambda$ | 10k | 2D+3D |
| A2 | `test_sdis_volumic_power` | 两面固定T + 体积功率 | $T = \frac{P}{2\lambda}(\frac{1}{4}-x^2)+T_0$ | 10k | 2D+3D |
| A3 | `test_sdis_contact_resistance` | 双材料板 + 接触热阻R | 含R耦合方程 | 10k | 2D+3D |
| A4 | `test_sdis_contact_resistance_2` | 同A3 + boundary验证 | 同A3 | 10k | 2D+3D |
| A5 | `test_sdis_volumic_power3_2d` | 三层三明治 + 对流BC | 各层线性/二次分布 | 10k | 2D |
| A6 | `test_sdis_volumic_power4` | 体功率 + 对流BC | 二次分布 | 100k | 2D+3D |
| A7 | `test_sdis_solve_probe3` / `_2d` | 含内部球体同材料 | 线性插值 325K | 10k | 2D+3D |

### 类别 B：探针/边界/介质求解（6 个测试）

详细原理：[wf_numerical_tests/cat_B_probe_boundary_medium.md](wf_numerical_tests/cat_B_probe_boundary_medium.md)

| # | CPU 测试 | 物理场景 | 解析参考 | N |
|---|---|---|---|---|
| B1 | `test_sdis_solve_probe` | 均匀流体场 | T = T_fluid（零方差） | 1k |
| B2 | `test_sdis_solve_boundary` | Dirichlet + 对流 | $T = \frac{HT_f + \lambda T_b/A}{H + \lambda/A}$ | 10k |
| B3 | `test_sdis_solve_boundary_flux` | 多通量分量 | 解析CF/RF/TF | 100k |
| B4 | `test_sdis_solve_medium` / `_2d` | 单/双介质平均 | $T = \sum T_i V_i / \sum V_i$ | 10k |
| B5 | `test_sdis_solve_probe_list` | 三线性场 + 超形状 | $T = 333x + 432y + 579z$ | 10k |
| B6 | `test_sdis_solve_probe_boundary_list` | 三线性场 + 边界探针 | 同 B5 | 10k |

### 类别 C：导热-辐射耦合 Picard（4 个测试）

详细原理：[wf_numerical_tests/cat_C_picard_radiation.md](wf_numerical_tests/cat_C_picard_radiation.md)

| # | CPU 测试 | 物理场景 | 解析参考 | N |
|---|---|---|---|---|
| C1 | `test_sdis_conducto_radiative` | 固体+两流体，ε=1，线性化辐射 | $h_r = 4\sigma T_{ref}^3\varepsilon$ | 10k |
| C2 | `test_sdis_conducto_radiative_2d` | C1 的 2D 版 | 同C1 | 10k |
| C3 | `test_sdis_picard` | 薄板辐射，picard_order=1/2/3 | 硬编码精确参考 | 10k |
| C4 | `test_sdis_flux2` | 通量+对流+辐射复合瞬态 | 硬编码（15×5矩阵） | 10k |

### 类别 D：对流（2 个测试）

详细原理：[wf_numerical_tests/cat_D_convection.md](wf_numerical_tests/cat_D_convection.md)

| # | CPU 测试 | 物理场景 | 解析参考 | N |
|---|---|---|---|---|
| D1 | `test_sdis_convection` | 6面不同T/H | $T(t) = T_0 e^{-\nu t} + T_\infty(1-e^{-\nu t})$ | 100k |
| D2 | `test_sdis_convection_non_uniform` | 非均匀对流系数 | 加权牛顿冷却 | 100k |

### 类别 E：瞬态/非稳态（6 个测试）

详细原理：[wf_numerical_tests/cat_E_transient.md](wf_numerical_tests/cat_E_transient.md)

| # | CPU 测试 | 物理场景 | 解析参考 | N |
|---|---|---|---|---|
| E1 | `test_sdis_unsteady` | 固体6面固定T | 硬编码Green函数值 | 10k |
| E2 | `test_sdis_unsteady_1d` | 2D薄板1D瞬态 | Green函数解析 | 10k |
| E3 | `test_sdis_unsteady_analytic_profile` | 正弦衰减 | $T \propto \sin(k_x x)\cdots e^{-\alpha k^2 t}$ | 100k |
| E4 | `test_sdis_unsteady_analytic_profile_2d` | E3 的 2D 版 | 同E3 | 100k |
| E5 | `test_sdis_unsteady_atm` | 流体+固体+大气 | 数值参考（1%容差） | 10k |
| E6 | `test_sdis_transcient` | 瞬态热传导 | 参考值 | 10k |

### 类别 F：外部通量（3 个测试）

详细原理：[wf_numerical_tests/cat_F_external_flux.md](wf_numerical_tests/cat_F_external_flux.md)

| # | CPU 测试 | 物理场景 | 解析参考 | N |
|---|---|---|---|---|
| F1 | `test_sdis_external_flux` | 球形太阳源 + 漫射/镜面地板 | 硬编码（375.88K/417.77K） | 10k/100k |
| F2 | `test_sdis_external_flux_with_diffuse_radiance` | 远距源 + 漫射辐照 | Stefan-Boltzmann | 10k |
| F3 | `test_sdis_draw_external_flux` | 相机渲染外部通量 | 无数值断言（管道验证） | 64spp |

### 类别 G：鲁棒性 + 特殊路径（3 个测试）

详细原理：[wf_numerical_tests/cat_G_robustness.md](wf_numerical_tests/cat_G_robustness.md)

| # | CPU 测试 | 物理场景 | 解析参考 | N |
|---|---|---|---|---|
| G1 | `test_sdis_solid_random_walk_robustness` | 非凸超形状，DS + WoS | 三线性解析 | 10k |
| G2 | `test_sdis_custom_solid_path_sampling` | 自定义WoS回调 | 三线性解析 | 10k |
| G3 | `test_sdis_custom_solid_path_sampling_2d` | G2 的 2D 版 | 双线性解析 | 10k |

### 类别 H：数据结构 + API 正确性（8 个非 MC 测试，与光追后端无关，不需 GPU 移植）

| # | CPU 测试 | 验证内容 |
|---|---|---|
| H1 | `test_sdis` | 库信息/MPI状态 |
| H2 | `test_sdis_data` | 数据容器 |
| H3 | `test_sdis_device` | 设备抽象 |
| H4 | `test_sdis_camera` | 相机API |
| H5 | `test_sdis_accum_buffer` | 累积缓冲区 |
| H6 | `test_sdis_scene` | 几何AABB/UV |
| H7 | `test_sdis_primkey` / `_2d` | PrimKey映射 |
| H8 | `test_sdis_source/medium/interface/radiative_env` | 组件API |

---

## 四、GPU Wavefront 数值正确性测试清单

### 总览

| 优先级 | 测试数 | 覆盖 | 阻塞关系 |
|---|---|---|---|
| **P0** | 5 | 基础物理（导热/对流/辐射） | 阻塞所有后续开发 |
| **P1** | 6 | 复合物理（Picard多阶/接触阻/外部源） | 阻塞应用验收 |
| **P2** | 5 | 瞬态 + 鲁棒性 | 阻塞瞬态场景使用 |
| **P3** | 6 | 长尾场景 + 完整覆盖 | 可选增强 |

### P0：最高优先级（基础物理正确性）

| ID | 对标CPU | 场景 | 解析参考 | SPP | 通过标准 |
|---|---|---|---|---|---|
| **WF-A1** | A1 `flux` | 固定T + 固定通量 稳态导热 | $T(x) = T_0 + (1-x)\Phi/\lambda$ | 256 | ≥95% 探针在 3σ 内 |
| **WF-A2** | A2 `volumic_power` | 体积功率 稳态导热 | $T(x) = \frac{P}{2\lambda}(\frac{1}{4}-x^2)+T_0$ | 256 | 同上 |
| **WF-B2** | B2 `solve_boundary` | Dirichlet + 对流 | $T = \frac{HT_f + \lambda T_b/A}{H + \lambda/A}$ | 256 | 3σ 内 |
| **WF-C1** | C1 `conducto_radiative` | 导热-辐射耦合 Picard1 | $h_r = 4\sigma T_{ref}^3\varepsilon$ | 512 | ≥95% |
| **WF-D1** | D1 `convection` | 均匀对流 稳态 | $T_\infty = \sum H_i T_i / \sum H_i$ | 512 | 3σ |

### P1：高优先级（复合物理场景）

| ID | 对标CPU | 场景 | SPP | 通过标准 |
|---|---|---|---|---|
| **WF-A3** | A3 `contact_resistance` | 双材料 + 接触热阻 | 256 | ≥95% 3σ |
| **WF-A6** | A6 `volumic_power4` | 体功率 + 对流BC | 512 | ≥95% 3σ |
| **WF-C3** | C3 `picard` | Picard 多阶 (1/2/3) | 512 | ≥95% 3σ |
| **WF-D2** | D2 `convection_non_uniform` | 非均匀对流系数 | 512 | 3σ |
| **WF-F1** | F1 `external_flux` | 外部球形源 + 漫射地板 | 512 | ≥95% 3σ |
| **WF-F2** | F2 `ext_flux_diffuse_radiance` | 外部源 + 漫射辐照 | 256 | 3σ |

### P2：中优先级（瞬态 + 鲁棒性）

| ID | 对标CPU | 场景 | SPP | 通过标准 |
|---|---|---|---|---|
| **WF-E1** | E1 `unsteady` | 3D 瞬态导热 | 256 | ≥90% 4σ（瞬态方差大） |
| **WF-E2** | E2 `unsteady_1d` | 1D 瞬态导热 | 256 | ≥95% 3σ |
| **WF-E3** | E3 `unsteady_analytic_profile` | 正弦温度场瞬态 | 512 | ≥95% 3σ |
| **WF-G1** | G1 `random_walk_robustness` | 非凸超形状 | 256 | 失败率 ≤ 0.05% |
| **WF-A7** | A7 `solve_probe3` | 内嵌球体同材料 | 128 | ≥95% 3σ |

### P3：完整覆盖（长尾场景）

| ID | 对标CPU | 场景 | SPP | 说明 |
|---|---|---|---|---|
| **WF-E5** | E5 `unsteady_atm` | 流体+固体+大气瞬态 | 512 | 容差 1%（沿用CPU） |
| **WF-C4** | C4 `flux2` | 复合瞬态+Picard+通量 | 512 | 15×5 参考矩阵 |
| **WF-B3** | B3 `boundary_flux` | 面通量分量(CF/RF/TF) | 1024 | 需通道扩展 |
| **WF-I3** | I3 `enclosure_limit` | 内嵌空腔极限条件 | 128 | 零方差精确 |
| **WF-B5** | B5 `solve_probe_list` | 三线性场批量探针 | 256 | 超形状几何 |
| **WF-I1** | I1 `volumic_power2` | 嵌套体功率 vs Syrthès | 256 | 可选交叉验证 |

---

## 五、与现有 B4 架构测试的关系

```
┌─────────────────────────────────────────────────┐
│  现有 B4 测试（架构级，11 + 1 benchmark）         │
│  ├── M1–M8, M10：状态机转换 (mock, 无光追)       │
│  ├── E2E：wavefront vs depth-first 像素兼容      │
│  └── 回答: "wavefront 和 depth-first 一致吗?"    │
│                                                 │
│  WF-* 测试（数值正确性，本清单，22 个）            │
│  ├── WF-A1~G1：wavefront 结果 vs 解析解          │
│  ├── 附带: wavefront vs depth-first 交叉验证      │
│  └── 回答: "wavefront 的结果物理上正确吗?"        │
└─────────────────────────────────────────────────┘
```

两者互补，缺一不可。

---

## 六、实施路线

1. **API 层**: 在 `sdis.h` 新增 `sdis_solve_wavefront_probe` 公共接口
2. **P0 测试**: 实现 WF-A1（建立模板），然后 WF-A2/B2/C1/D1
3. **P1 测试**: 实现 WF-A3/A6/C3/D2/F1/F2
4. **P2 测试**: 实现 WF-E1/E2/E3/G1/A7
5. **P3 测试**: 按需实现

**测试文件命名**: `test_sdis_wf_<scenario>.c`  
**CMake 注册**: 通过 `add_module_tests` 或手动 `add_test`

---

*文档更新: 2026-02-14 | 分类实验详情见 [wf_numerical_tests/](wf_numerical_tests/)*
