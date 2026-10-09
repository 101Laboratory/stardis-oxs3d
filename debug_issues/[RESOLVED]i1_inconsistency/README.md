# I1 不一致性诊断

**创建日期**: 2026-02-06  
**结案日期**: 2026-03-03  
**状态**: ✅ 根因已确认 — 测试参考值选用错误（2D/3D列混淆），求解器本身无 bug  
**测试**: `test_sdis_wf_i1_volumic_power2`

## 问题描述

I1 测试（嵌套体积功率场景）中 8 个探针有 2 个失败：

| 探针位置 y | 参考值 (°C) | GPU结果偏差 | Sigma偏差 |
|-----------|------------|-----------|---------|
| 0.65      | 247.09     | T偏低      | ~6σ     |
| 0.45      | 308.42     | T偏低      | ~12σ    |

y=0.65 位于 solid1 中、紧邻内部 solid2 立方体上方（内部块 y∈[0.4,0.6]）。  
y=0.45 位于 solid2 内部。两个探针的 T 系统性偏低（相当于热量从 solid2 泄漏）。

## 场景

```
外部长方体: [-0.5,0.5] × [-1,1] × [-0.5,0.5]  solid1 (λ=1, 无体积功率)
内部立方体: [-0.1,0.1] × [0.4,0.6] × [-0.5,0.5]  solid2 (λ=10, Pw=10000)
上方边界: solid1/fluid1 对流 h=5, T_fluid=373.15K
下方边界: solid1/fluid2 对流 h=10, T_fluid=273.15K
侧面: 绝热
内部4个侧面: solid1/solid2 透明界面
内部前/后面: solid2/fluid1 绝热
```

## 假说

### H1（主要）: SS 重注入概率/映射错误
`step_bnd_ss_reinject_decide` 中 FRONT/BACK enclosure ID 映射可能翻转，  
或概率公式 `proba = λ_frt / (λ_frt + λ_bck)` 中 λ 值取错了方向。

**诊断指标**: `ss_avg_lambda_frt`, `ss_avg_lambda_bck`, `ss_chose_frt / ss_hits`
- 如果 `ss_avg_λf` ≠ 期望的 10.0（进入 solid2 方向），则 H1b 确认
- 如果 λ 正确但 proba ≠ 期望比例（如 10/11≈0.909），则 H1a 确认

### H2（次要）: DS 包壳验证在界面附近过度重试
探针靠近 solid-solid 界面，DS 步进时包壳查询可能频繁失配导致重试。

**诊断指标**: `ds_mismatch_rate`（= `ds_enc_mismatch / ds_enc_query`）
- 如果 mismatch_rate >> 5%，则 H2 有可能

### H3（较低）: 体积功率公式缺陷
内联的 `handle_volumic_power` 中 3D 修正项可能有误。

**诊断指标**: `vp_ds_accum / vp_ds_pw_steps`（平均每步功率贡献）
- 对比理论值 `Pw * δ² / (2λ)`

## 诊断框架

### 编译启用
```bash
cmake -DWF_I1_DIAG=ON ...
cmake --build . --config Release --target test_sdis_wf_i1_volumic_power2
```

### 运行
```bash
cd build/stardis-solver/0.16.2
ctest -C Release -R wf_i1 --output-on-failure
```

### 输出
运行后在工作目录生成 `I1_diag.csv`，包含以下列：

| 列名 | 含义 |
|------|------|
| `probe_y` | 探针 Y 坐标 |
| `ss_hits` | SS 界面命中总次数 |
| `ss_chose_frt / ss_chose_bck` | 选择 FRONT/BACK 的次数 |
| `ss_retry` | SS 重试次数 |
| `ss_avg_proba` | 平均选择 FRONT 的概率 |
| `ss_avg_lambda_frt / _bck` | 平均 λ_front / λ_back |
| `ds_steps` | DS 步进总次数 |
| `ds_enc_mismatch` | DS 包壳失配次数 |
| `ds_enc_query` | DS 包壳查询总次数 |
| `ds_mismatch_rate` | 失配率 |
| `vp_ds_accum` | DS 步进累计体积功率贡献 |
| `vp_ds_pw_steps` | 有体积功率的 DS 步数 |
| `vp_reinject_accum` | SS 重注入造成的 T 变化累计 |
| `vp_reinject_count` | SS 重注入次数 |
| `boundary_hits` | 边界命中总次数 |
| `paths_total / paths_done` | 总路径数/完成路径数 |

## 决策树

```
1. ss_avg_lambda_frt ≈ 10.0 且 ss_avg_lambda_bck ≈ 1.0？
   ├─ 否 → H1b: FRONT/BACK λ 映射翻转
   └─ 是 → 2. ss_avg_proba ≈ 0.909 (= 10/11)?
            ├─ 否 → H1a: 概率公式错误
            └─ 是 → 3. ds_mismatch_rate > 5%?
                     ├─ 是 → H2: 包壳验证问题
                     └─ 否 → 4. vp_ds_accum / vp_ds_pw_steps 合理?
                              ├─ 否 → H3: 体积功率公式缺陷
                              └─ 是 → 新假说需要（可能是 Picard 收敛）
```

## 修改的文件

| 文件 | 修改内容 |
|------|---------|
| `sdis_wf_state.h` | `path_state` 增加 16 个诊断计数器（`#ifdef WF_I1_DIAG`）|
| `sdis_solve_persistent_wavefront.h` | 新增 `probe_diag` 结构体 + `probe_batch_mode_ctx.diags` |
| `sdis_solve_persistent_wavefront.c` | 全局 `g_wf_i1_diags` + 聚合逻辑 |
| `sdis_wf_steps_bnd_ss.c` | SS 命中/选择/重试/重注入 T 变化追踪 |
| `sdis_wf_steps_cnd.c` | DS 步进/失配/体积功率追踪 |
| `sdis_wf_steps_core.c` | 包壳查询/边界命中追踪 |
| `test_sdis_wf_i1_volumic_power2.c` | 缩减到 2 探针 + 诊断 CSV 输出 |
| `CMakeLists.txt` | `WF_I1_DIAG` 选项 |

## 清理

诊断完成后，需要在本文件所在目录下更新问题分析历史和修复方法。用户将会手动执行git reset，这将会删除所有修改，包括对问题的修复（如果已经实现）：
1. 移除 `CMakeLists.txt` 中的 `WF_I1_DIAG` 选项和 compile definition
2. 移除所有 `#ifdef WF_I1_DIAG` 块
3. 恢复测试文件中的 8 探针配置
4. 删除全局 `g_wf_i1_diags` / `g_wf_i1_diags_count`

---

# 诊断实验记录与结论

## 实验 1：诊断框架运行 (I1_diag.csv)

### 运行参数
- `WF_I1_DIAG=ON`, N=10000, 2 探针 (y=0.65, y=0.45)
- POOL_SIZE=8192, δ=0.01

### 实验数据

| 指标 | y=0.65 | y=0.45 |
|------|--------|--------|
| T (K) | 533.34 | 557.78 |
| T (°C) | 260.19 | 284.63 |
| SE (K) | 2.21 | 2.18 |
| ss_hits | 3,266,893 | 1,093,662 |
| ss_chose_frt | 2,980,722 | 999,204 |
| ss_avg_proba | 0.9124 | 0.9136 |
| ss_avg_lambda_frt | 10.0 | 10.0 |
| ss_avg_lambda_bck | 1.0 | 1.0 |
| ds_steps | 376,148,223 | 388,810,774 |
| ds_enc_mismatch | 3 | 0 |
| ds_mismatch_rate | ≈0 | 0 |
| vp_ds_accum | 1,854,730 | 2,140,553 |
| vp_ds_pw_steps | 119,556,234 | 134,834,478 |
| vp_reinject_accum | 1,357 | 6,293 |
| vp_reinject_count | 32,206 | 56,120 |
| boundary_hits | 8,697,155 | 8,558,447 |

### 决策树结果
1. ✅ `ss_avg_lambda_frt=10.0`, `ss_avg_lambda_bck=1.0` → λ 映射正确 → **H1b 排除**
2. ✅ `ss_avg_proba≈0.913` ≈ 理论值 10/11=0.909 → 概率公式正确 → **H1a 排除**
3. ✅ `ds_mismatch_rate≈0` → 包壳验证正常 → **H2 排除**
4. ✅ `vp_ds_accum/vp_ds_pw_steps≈0.0155K` 量级合理 → **H3 不太可能**
5. ⚠️ 新发现：`vp_reinject_accum` > 0 → VP 在 SS 重注入时有额外贡献 → **提出 H4**

### H4: VP 是温度偏差的唯一来源
SS 重注入调用 `solid_reinjection_3d` → `handle_volumic_power`（边界版本，简化公式 d²/(6λ)）→ `time_rewind`。
怀疑 VP 处理存在问题（double-counting、time_rewind VP undo、错误的 λ）。

## 实验 2：No-VP 控制实验 (Check 0)

### 设计
在测试文件中增加 Check 0：运行前将 solid2 的 `P` 临时设为 `SDIS_VOLUMIC_POWER_NONE`，
令 Pw=0，纯传导+对流基线。运行后恢复 Pw=10000 再运行 Check 1。

### 实验数据

| 探针 y | Check 0 T (K) | Check 0 T (°C) | SE (K) | 1D 解析解 T (K) |
|--------|---------------|----------------|--------|----------------|
| 0.65 | 347.37 | 74.22 | 0.437 | ~347.2 |
| 0.45 | 342.54 | 69.39 | 0.461 | ~344.1 |

| 探针 y | Check 1 T (K) | Check 1 T (°C) | SE (K) | 判定 |
|--------|---------------|----------------|--------|------|
| 0.65 | 533.34 | 260.19 | 2.21 | (待定) |
| 0.45 | 557.78 | 284.63 | 2.18 | (待定) |

### 分析
- **基线 (Pw=0) 精确匹配 1D 解析解** → 无 VP 时随机游走、SS 重注入、传导、对流全部正确
- **H4 确认**：温度偏差的唯一来源是 VP

### VP 贡献分离

| 探针 y | VP 贡献 = Check1 - Check0 | "当前参考值"期望的 VP 贡献 |
|--------|--------------------------|--------------------------|
| 0.65 | 533.34 - 347.37 = **185.97K** | (247.09+273.15) - 347.37 = **172.87K** → +13K 多 |
| 0.45 | 557.78 - 342.54 = **215.24K** | (308.42+273.15) - 342.54 = **239.03K** → -24K 少 |

表面看 VP 有 ±13~24K 的系统性偏差。但这是基于 `temperature_3d` 列参考值的判断。

## 关键发现：CPU 参考测试的 2D/3D 双列

### CPU 测试源码分析

**文件**: `stardis-cpu/stardis-solver/0.16.2/src/test_sdis_volumic_power2.c`

CPU 测试 `struct reference` 存储了 **两列** Syrthès 参考温度：

```c
struct reference {
  double pos[3];
  double temperature_2d; /* In celcius */
  double temperature_3d; /* In celcius */
};

const struct reference refs1[] = {
  {{0, 0.85, 0}, 190.29, 189.13},
  {{0, 0.65, 0}, 259.95, 247.09},   /* 2D=259.95,  3D=247.09  — 差 12.86°C */
  {{0, 0.45, 0}, 286.33, 308.42},   /* 2D=286.33,  3D=308.42  — 差 22.09°C */
  {{0, 0.25, 0}, 235.44, 233.55},
  {{0, 0.05, 0}, 192.33, 192.30},
  {{0,-0.15, 0}, 156.82, 156.98},
  {{0,-0.35, 0}, 123.26, 123.43},
  {{0,-0.55, 0}, 90.250, 90.040}
};
```

CPU 的 `check()` 函数打印并对比的是 **`refs[i].temperature_2d`**。
CPU MC 结果 (N=100000) 也匹配 2D 列：y=0.65→259.73°C, y=0.45→285.29°C。

### GPU 测试选用了 3D 列

GPU 测试 `test_sdis_wf_i1_volumic_power2.c` 的参考值：
```c
static const struct i1_probe_ref i1_refs1[] = {
  {{0, 0.65, 0}, 247.09},   /* ← temperature_3d */
  {{0, 0.45, 0}, 308.42},   /* ← temperature_3d */
};
```

### 四路结果对比

| y | Syrthès 2D | Syrthès 3D | CPU MC (3D几何) | GPU WF (3D几何) | 匹配列 |
|---|-----------|-----------|----------------|----------------|--------|
| 0.85 | 190.29 | 189.13 | 190.20 | — | 2D ✓ |
| **0.65** | **259.95** | **247.09** | **259.73** | **260.19** | **2D ✓ , 3D ✗** |
| **0.45** | **286.33** | **308.42** | **285.29** | **284.63** | **2D ✓ , 3D ✗** |
| 0.25 | 235.44 | 233.55 | 235.67 | — | ≈两者 |
| 0.05 | 192.33 | 192.30 | 192.46 | — | ≈两者 |
| -0.15 | 156.82 | 156.98 | 157.53 | — | ≈两者 |
| -0.35 | 123.26 | 123.43 | 124.23 | — | ≈两者 |
| -0.55 | 90.250 | 90.040 | 91.03 | — | ≈两者 |

CPU MC（3D 几何，`sdis_scene_create` 调用 `solve_probe_3d`）和 GPU WF 给出**完全一致**的结果，
且都匹配 **2D 参考值**，不匹配 3D 参考值。

## 根因：几何 z-不变性 → 2D/3D 参考值分别对应不同的物理问题

### 场景的 z-不变性

```
  z=-0.5                    z=+0.5
    │ ┌─────────────────────┐ │       俯视图 (沿 y 轴看)
    │ │  outer (solid1)     │ │
    │ │   ┌───────────────┐ │ │       内盒子 z∈[-0.5,0.5]
    │ │   │ inner(solid2) │ │ │       = 外盒子 z∈[-0.5,0.5]
    │ │   └───────────────┘ │ │       → z 方向完全贯穿
    │ │                     │ │
    │ └─────────────────────┘ │
  adiab.                   adiab.     ← z 面全部绝热
```

**关键几何事实**：
- 外盒子 z∈[-0.5, 0.5]，内盒子 z∈[-0.5, 0.5] — **内盒子在 z 方向贯穿整个外盒子**
- z=±0.5 面（前后面）：outer 和 inner 都是**绝热** (h=0)
- 几何、材料、边界条件全部 z-无关

### 物理推导

稳态 Fourier 方程：

    -λ (∂²T/∂x² + ∂²T/∂y² + ∂²T/∂z²) = Pw

由于：
1. **几何 z-不变**：任意 z=const 截面的材料分布和边界完全相同
2. **z 面绝热**：∂T/∂z|_{z=±0.5} = 0
3. **源项与 z 无关**

设 T 有 z 依赖项 f(z)，则 f''(z)=0 且 f'(±0.5)=0 → f=常数 → **∂T/∂z ≡ 0**

∂²T/∂z² 项消失后，3D 方程退化为：

    -λ (∂²T/∂x² + ∂²T/∂y²) = Pw

这就是 **2D 热传导方程**。因此无论用 2D 还是 3D 求解器，精确解都相同。

### MC 层面的等价性

- **2D DS** (DIM=2)：圆盘随机游走，每步 VP = Pw·d²/(4λ)
- **3D DS** (DIM=3)：球体随机游走，每步 VP = Pw·d²/(6λ)

3D 模式每步 VP 更小（6 vs 4），但 z 方向需更多步数在绝热面间来回弹，两效应精确抵消。期望值相同。
这已被 CPU 3D MC 结果验证（匹配 2D 参考值）。

### `temperature_3d` 列的含义

`temperature_3d` 列来自 EDF/Syrthès 在**另一个真正 3D 几何**上的计算 —— 大概率是内盒子 z 范围有限
（例如 z∈[-0.1, 0.1]，形成悬浮的小立方体），使得 z 方向有非零热流，构成一个不同的物理问题。

**当前测试的几何不是那个。** 当前几何内盒子 z 贯穿外盒子。

### 为什么恰好 2/8 FAIL

| y | | 2D-3D 差 (°C) | MC SE (°C) | 3σ |
|---|---|-------------|-----------|-----|
| 0.85 | | 1.16 | 0.57 | 1.71 | → 差 < 3σ → PASS |
| **0.65** | | **12.86** | 0.68 | 2.04 | → **差 >> 3σ → FAIL (6σ)** |
| **0.45** | | **22.09** | 0.69 | 2.07 | → **差 >> 3σ → FAIL (12σ)** |
| 0.25 | | 1.89 | 0.71 | 2.13 | → 差 < 3σ → PASS（边缘） |
| 0.05~-0.55 | | < 0.5 | ~0.6 | ~1.8 | → 差 << 3σ → PASS |

只有 y=0.65 和 y=0.45 的 2D/3D 参考值差异远超 MC 噪声，其余 6 点差异在噪声范围内自动 PASS。

## 假说最终状态

| 假说 | 状态 | 排除/确认依据 |
|------|------|-------------|
| H1a: SS 概率公式错误 | ❌ 排除 | ss_avg_proba≈0.913 ≈ 10/11 |
| H1b: FRONT/BACK λ 映射翻转 | ❌ 排除 | λ_frt=10.0, λ_bck=1.0 精确 |
| H2: DS 包壳验证问题 | ❌ 排除 | mismatch_rate≈0 |
| H3: VP 公式缺陷 | ❌ 排除 | 每步 VP 量级合理 |
| H4: VP 处理 bug | ❌ 排除 | no-VP 基线正确 + GPU≈CPU MC 结果 |
| **H5: 参考值选用错误** | **✅ 确认** | GPU选用 temperature_3d 列，该列对应不同几何 |

**根因**：GPU 测试 `i1_refs1[]` 选用了 `temperature_3d` 列的参考值。该列对应的 Syrthès 模拟
使用了不同几何（内盒子 z 范围有限），而当前测试的几何是 z-贯穿+绝热的，物理解等价于 2D，
正确参考值是 `temperature_2d` 列。**wavefront 求解器本身无 bug。**

## 修复方案

### 修改文件
`stardis-cus3d/stardis-solver/0.16.2/src/test_sdis_wf_i1_volumic_power2.c`

### 修改内容
将 `i1_refs1[]` 的参考温度从 Syrthès 3D 列替换为 Syrthès 2D 列：

```c
/* 修改前（temperature_3d 列 — 对应不同几何） */
static const struct i1_probe_ref i1_refs1[I1_NREFS1] = {
  {{0, 0.85, 0}, 189.13},
  {{0, 0.65, 0}, 247.09},
  {{0, 0.45, 0}, 308.42},
  {{0, 0.25, 0}, 233.55},
  {{0, 0.05, 0}, 192.30},
  {{0,-0.15, 0}, 156.98},
  {{0,-0.35, 0}, 123.43},
  {{0,-0.55, 0},  90.040}
};

/* 修改后（temperature_2d 列 — 与当前 z-贯穿几何一致） */
static const struct i1_probe_ref i1_refs1[I1_NREFS1] = {
  {{0, 0.85, 0}, 190.29},
  {{0, 0.65, 0}, 259.95},
  {{0, 0.45, 0}, 286.33},
  {{0, 0.25, 0}, 235.44},
  {{0, 0.05, 0}, 192.33},
  {{0,-0.15, 0}, 156.82},
  {{0,-0.35, 0}, 123.26},
  {{0,-0.55, 0},  90.250}
};
```

同时更新结构体注释：`/* Syrthes 2D reference (z-invariant geometry) */`

### 验证步骤
```bash
cmake --build . --config Release
ctest -C Release -R i1_volumic_power2 --output-on-failure
# 预期：8/8 PASS
```

### 其他清理
1. 移除 `WF_I1_DIAG` 编译选项及所有 `#ifdef WF_I1_DIAG` 诊断代码
2. 恢复测试文件中的 8 探针完整配置
3. 删除全局 `g_wf_i1_diags` / `g_wf_i1_diags_count`
