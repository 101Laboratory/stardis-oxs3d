# 疑似Bug: GPU ENC_ROT_MATRIX 与 CPU f33_rotation 旋转矩阵不匹配

**状态**: 已解决（2026-03-13）  
**风险等级**: 高（若确认则影响所有 M10 enclosure 查询）  
**创建日期**: 2026-02-14  
**关联**: `debug_issues/irregular_reinjection_retry_failure/` — M5_SF_ENC_RETRY 大量失败

## 结论更新（2026-03-13）

- 该项已从 TODO 转为 RESOLVED。
- 当前代码中 `ENC_ROT_MATRIX` 已与 CPU `f33_rotation(PI/4, PI/4, PI/4)` 对齐。
- 已存在一致性测试用于约束后续回归（`test_enc_rot_matrix_consistency.c`）。
- 本文其余内容保留为问题调查历史记录。

---

## 1. 问题描述

GPU kernel `cus3d_find_enclosure.cu` 中用于 6-ray side 判定的旋转矩阵 `ENC_ROT_MATRIX`，
其注释声称 "Matches CPU `f33_rotation(PI/4, PI/4, PI/4)`"，但经推导两者的矩阵构建公式不同，
可能导致 GPU 和 CPU 对同一点发射的 6 条旋转射线方向不一致。

如果 GPU 射线方向不同，在凹角/凹边几何处可能导致：
- GPU 的 6 条射线命中**不同的三角形**（返回错误的 `prim_id` → 错误的 `enc_id`）
- GPU 的 6 条射线全部命中边缘/掠射面而失败（`side=-1`），触发 degenerate fallback

**观察到的症状**: `solid_enc=1, enc_resolved=0` — GPU 返回 enc=0，实际应为 enc=1。

---

## 2. 代码定位

### 2.1 GPU 实现

**文件**: `stardis-cus3d/custar-3d/0.10/src/cus3d_find_enclosure.cu`  
**行号**: L242-L256 (矩阵定义), L306-L313 (乘法函数), L433/L720 (调用点)

```cuda
/* L242-L256: 矩阵定义 */
/* Pre-computed R = Rz(PI/4) · Ry(PI/4) · Rx(PI/4)
 * Matches CPU f33_rotation(PI/4, PI/4, PI/4).  Row-major 3×3. */
__device__ static const float ENC_ROT_MATRIX[9] = {
    /* Row 0: */ COS_PI4 * COS_PI4,
                COS_PI4 * SIN_PI4 * SIN_PI4 - SIN_PI4 * COS_PI4,
                COS_PI4 * SIN_PI4 * COS_PI4 + SIN_PI4 * SIN_PI4,
    /* Row 1: */ SIN_PI4 * COS_PI4,
                SIN_PI4 * SIN_PI4 * SIN_PI4 + COS_PI4 * COS_PI4,
                SIN_PI4 * SIN_PI4 * COS_PI4 - COS_PI4 * SIN_PI4,
    /* Row 2: */ -SIN_PI4,
                COS_PI4 * SIN_PI4,
                COS_PI4 * COS_PI4
};

/* L306-L313: 乘法 — row-major: out = M × v */
__device__ static float3
enc_rotate_dir(const float raw_dir[3])
{
    float3 r;
    r.x = ENC_ROT_MATRIX[0]*raw_dir[0] + ENC_ROT_MATRIX[1]*raw_dir[1] + ENC_ROT_MATRIX[2]*raw_dir[2];
    r.y = ENC_ROT_MATRIX[3]*raw_dir[0] + ENC_ROT_MATRIX[4]*raw_dir[1] + ENC_ROT_MATRIX[5]*raw_dir[2];
    r.z = ENC_ROT_MATRIX[6]*raw_dir[0] + ENC_ROT_MATRIX[7]*raw_dir[1] + ENC_ROT_MATRIX[8]*raw_dir[2];
    return r;
}
```

### 2.2 CPU 参考实现

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_scene_Xd.h`  
**行号**: L1318-L1382

```c
/* 矩阵构建 */
f33_rotation(frame, (float)PI/4, (float)PI/4, (float)PI/4);

/* 乘法 — 注意调用的是 fXX_mulfX */
fXX_mulfX(dirs[idir], frame, dirs[idir]);
```

**f33_rotation 定义**: `stardis-cus3d/rsys/0.15/src/real33.h`, L85-L103

```c
/* pitch=c1/s1, yaw=c2/s2, roll=c3/s3 */
dst[0] = c2*c3; dst[1] = c1*s3 + c3*s1*s2; dst[2] = s1*s3 - c1*c3*s2;
dst[3] =-c2*s3; dst[4] = c1*c3 - s1*s2*s3; dst[5] = c1*s2*s3 + c3*s1;
dst[6] = s2;    dst[7] =-c2*s1;            dst[8] = c1*c2;
```

**fXX_mulfX (即 f33_mulf3) 定义**: `stardis-cus3d/rsys/0.15/src/realXY.h`, L214-L225

```c
/* mat×vec, 但 mat 是 column-major 存储 */
REALXY_REALX_FUNC__(mul)
  (REAL_TYPE__* dst, const REAL_TYPE__* mat, const REAL_TYPE__* vec)
{
  REAL_TYPE__ row[REALX_DIMENSION__];
  REAL_TYPE__ tmp[REALY_DIMENSION__];
  int y;
  FOR_EACH(y, 0, REALY_DIMENSION__) {
    REALXY_FUNC__(row)(row, mat, y);  /* 从 column-major 提取第 y 行 */
    tmp[y] = REALX_FUNC__(dot)(row, vec);
  }
  return REALY_FUNC__(set)(dst, tmp);
}
```

**row 提取函数**: `realXY.h`, L108-L115

```c
/* 关键: mat 是 column-major，row[x] = mat[x * REALY_DIMENSION + irow] */
REALXY_FUNC__(row)(REAL_TYPE__* row, const REAL_TYPE__* mat, const int irow)
{
  FOR_EACH(x, 0, REALX_DIMENSION__)
    tmp[x] = mat[x * REALY_DIMENSION__ + irow];
}
```

---

## 3. 分歧分析

### 3.1 CPU 实际计算

`f33_rotation(PI/4, PI/4, PI/4)` 产生的 9 个数存储在 `frame[9]` 中。
由于 rsys 使用 **column-major** 存储（`col_ptr(mat, icol) = mat + icol * REALY_DIM`），
`frame` 的 column-major 布局为：

```
frame[0] = c2*c3       frame[3] = -c2*s3       frame[6] = s2
frame[1] = c1*s3+...   frame[4] = c1*c3-...    frame[7] = -c2*s1
frame[2] = s1*s3-...   frame[5] = c1*s2*s3+... frame[8] = c1*c2
```

**等等** — 但 `real33.h` 直接写 `dst[0..8]`。需要判定：`f33_rotation` 的写入顺序
到底是 row-major 还是 column-major？

查看 `realXY.h` 的 `row()` 函数：`row[x] = mat[x * REALY_DIM + irow]`
- 对 3×3: `row(mat, 0) → mat[0], mat[3], mat[6]` = 第 0 行
- 这证明 **mat 按列存储**（column-major）

所以 `f33_rotation` 写出的按索引排列为：

```
[0]=c2*c3  [1]=c1*s3+c3*s1*s2  [2]=s1*s3-c1*c3*s2   ← 第 0 列
[3]=-c2*s3 [4]=c1*c3-s1*s2*s3  [5]=c1*s2*s3+c3*s1    ← 第 1 列
[6]=s2     [7]=-c2*s1           [8]=c1*c2              ← 第 2 列
```

**逻辑矩阵**（以 row 方式读取）：

| | col 0 | col 1 | col 2 |
|---|---|---|---|
| **row 0** | frame[0] = c2c3 | frame[3] = -c2s3 | frame[6] = s2 |
| **row 1** | frame[1] = c1s3+c3s1s2 | frame[4] = c1c3-s1s2s3 | frame[7] = -c2s1 |
| **row 2** | frame[2] = s1s3-c1c3s2 | frame[5] = c1s2s3+c3s1 | frame[8] = c1c2 |

`f33_mulf3(out, frame, v)` 做的是 `out[y] = row(frame, y) · v`，即：

$$\text{out} = M \cdot v$$

其中 $M$ 的第 $y$ 行由 `row(frame, y)` 给出。

代入 $c = s = \frac{\sqrt{2}}{2}$：

$$M_{\text{CPU}} = \begin{pmatrix} 0.5 & -0.5 & 0.707 \\ 0.854 & 0.146 & -0.5 \\ 0.146 & 0.854 & 0.5 \end{pmatrix}$$

### 3.2 GPU 实际计算

`enc_rotate_dir(v)` 做 `out = ENC_ROT_MATRIX(row-major) × v`：

代入 $c = s = \frac{\sqrt{2}}{2}$：

```
Row 0: 0.5,   0.5*0.5 - 0.707*0.707,   0.5*0.707 + 0.707*0.5
     = 0.5,   0.25 - 0.5,               0.354 + 0.354
     = 0.5,  -0.25,                      0.707
       ↑ 不对：0.5*0.5*0.5 - 0.707*0.707 = 0.125 - 0.5 = -0.375 ← 需重新计算
```

让我精确计算（令 $c = s = \frac{\sqrt{2}}{2} \approx 0.70711$）：

```
GPU[0] = c*c = 0.5
GPU[1] = c*s*s - s*c = c*s² - sc = sc(s-1) ... 
```

精确代入注释声称的公式 $R_z(π/4) \cdot R_y(π/4) \cdot R_x(π/4)$：

$$R_z = \begin{pmatrix} c & -s & 0 \\ s & c & 0 \\ 0 & 0 & 1 \end{pmatrix}, \quad
R_y = \begin{pmatrix} c & 0 & s \\ 0 & 1 & 0 \\ -s & 0 & c \end{pmatrix}, \quad
R_x = \begin{pmatrix} 1 & 0 & 0 \\ 0 & c & -s \\ 0 & s & c \end{pmatrix}$$

$R_z R_y R_x$:

| | col 0 | col 1 | col 2 |
|---|---|---|---|
| row 0 | $c^2$ | $cs^2 - sc$ | $cs c + s^2$ |
| row 1 | $sc$ | $s^2 s + c^2$ | $s^2 c - cs$ |
| row 2 | $-s$ | $cs$ | $c^2$ |

代入 $c = s$:

| | col 0 | col 1 | col 2 |
|---|---|---|---|
| row 0 | 0.5 | $c^3 - c^2 = 0.354 - 0.5 = -0.146$ | $c^2 \cdot c + c^2 = c^3 + c^2 = 0.354 + 0.5 = 0.854$ |
| row 1 | 0.5 | $c^3 + c^2 = 0.854$ | $c^3 - c^2 = -0.146$ |
| row 2 | -0.707 | 0.5 | 0.5 |

$$M_{\text{GPU}} = \begin{pmatrix} 0.5 & -0.146 & 0.854 \\ 0.5 & 0.854 & -0.146 \\ -0.707 & 0.5 & 0.5 \end{pmatrix}$$

### 3.3 对比

$$M_{\text{CPU}} = \begin{pmatrix} 0.5 & -0.5 & 0.707 \\ 0.854 & 0.146 & -0.5 \\ 0.146 & 0.854 & 0.5 \end{pmatrix} \qquad M_{\text{GPU}} = \begin{pmatrix} 0.5 & -0.146 & 0.854 \\ 0.5 & 0.854 & -0.146 \\ -0.707 & 0.5 & 0.5 \end{pmatrix}$$

**对 [1,0,0] 的旋转**:
- CPU: $(0.5, 0.854, 0.146)$
- GPU: $(0.5, 0.5, -0.707)$

**结论**: $M_{\text{CPU}} \neq M_{\text{GPU}}$。两者是不同的旋转矩阵。

### 3.4 但是……

**两者都是正交旋转矩阵**，仅旋转顺序不同（$R_x R_y R_z$ vs $R_z R_y R_x$）。
6-ray 的目的是避免轴对齐射线击中几何离散化边界，只要旋转后的 6 个方向
**不与任何几何轴对齐**，算法在大多数情况下都能工作。

**以下情况可能导致差异**：
- 某些特定的凹角几何，CPU 的 6 射线恰好绕过边缘，而 GPU 的命中边缘（或反之）
- 两者命中不同的三角形，但该三角形恰好位于两个 enclosure 的交界处

**这不一定是 bug，但确实是 CPU/GPU 不一致性的来源。**

---

## 4. 修复方案

### 方案 A: 令 GPU 矩阵与 CPU 完全一致（安全修复）

将 `ENC_ROT_MATRIX` 替换为 CPU `f33_rotation` 的等价数值，按 row-major 填入
$M_{\text{CPU}}$ 的值。

```cuda
/* 修正: 与 CPU f33_rotation(PI/4, PI/4, PI/4) 完全一致, row-major */
__device__ static const float ENC_ROT_MATRIX[9] = {
    /* Row 0: */  0.5f,                -0.5f,                0.70710678118f,
    /* Row 1: */  0.85355339059f,       0.14644660941f,     -0.5f,
    /* Row 2: */  0.14644660941f,       0.85355339059f,      0.5f
};
```

**风险**: 低。仅改变 GPU 侧射线方向使之与 CPU 一致，不改变算法逻辑。
**前提**: 需要先验证 $M_{\text{CPU}}$ 的数值正确性（见测试方案）。

### 方案 B: 保持现状，仅修复注释（如验证后无功能影响）

如果经验证两个矩阵在实际场景中产生相同的 enc_id（因为两者都能避免轴对齐），
则仅修正注释：

```cuda
/* R = Rz(PI/4) · Ry(PI/4) · Rx(PI/4), row-major.
 * NOTE: This is NOT the same as CPU f33_rotation(PI/4, PI/4, PI/4)
 * which produces Rx·Ry·Rz in column-major. Both are valid PI/4
 * rotation matrices that avoid axis alignment. */
```

---

## 5. 现有测试覆盖分析

| 测试文件 | 覆盖的方面 | 此问题覆盖? |
|----------|-----------|------------|
| `test_sdis_b4_m1_enclosure_batch.c` | M1 状态机的 6 射线 emit + resolve 逻辑；使用 `f33_rotation` 构建参考 frame 验证方向非轴对齐 | ⚠️ **间接覆盖** — 验证了 CPU 侧 `f33_rotation` 的正确性，但**完全不涉及 GPU `ENC_ROT_MATRIX`** |
| `test_sdis_b4_m4_delta_sphere.c` | M4 导热路径的 enc_locate 状态链 | ❌ 不涉及旋转 |
| `test_sdis_b4_m3_solid_solid.c` | M3 SS 重注入 4 射线 | ❌ |
| GPU 层 `test_s3d_*` | cuBQL 基本 trace/closest-point；`test_s3d_closest_point.c` 使用 `f33_rotation(45°,0,0)` 但不测 enc | ❌ |
| **GPU ↔ CPU enc_id 对比测试** | **不存在** — 设计文档 `phase_b4_test_design.md` 中的 T10.8 标记为 ⛔延迟 | ❌ **关键缺口** |

**结论**: `ENC_ROT_MATRIX` 的数值正确性**零覆盖**。没有任何测试将 GPU kernel 的旋转射线方向与 CPU 的进行直接比较。

---

## 6. 测试方案

### T1: 旋转矩阵数值验证（纯 CPU 单元测试）

**目的**: 验证 `ENC_ROT_MATRIX` 计算出的 6 个旋转方向与 `f33_rotation(PI/4,PI/4,PI/4)` + `f33_mulf3` 一致。

**方法**:
1. 在 CPU 侧调用 `f33_rotation(PI/4, PI/4, PI/4)` 得到 `frame[9]`
2. 对 6 个轴方向分别调用 `f33_mulf3(out, frame, dir)`，得到 6 个 CPU 旋转方向
3. 手动用 `ENC_ROT_MATRIX` row-major 乘法计算 6 个 GPU 旋转方向
4. 逐分量比较，容差 `1e-6`

**预期结果**: 当前实现应该 FAIL（矩阵不同）。

**位置**: `stardis-cus3d/custar-3d/0.10/src/test_enc_rot_matrix_consistency.c`

### T2: 6-ray 旋转方向轴对齐验证（纯 CPU 单元测试）

**目的**: 验证**两个**旋转矩阵产生的 6 个方向都不与坐标轴对齐。

**方法**:
1. 对 GPU 和 CPU 矩阵分别旋转 6 个轴方向
2. 验证旋转后每个方向的每个分量绝对值 < 0.99（即不与轴对齐）
3. 验证旋转后每个方向长度 = 1.0 ± 1e-6

**预期结果**: 两者都 PASS（两者都是合法的非轴对齐旋转）。

### T3: GPU vs CPU enclosure 查询结果对比（集成测试）

**目的**: 在标准测试场景上对随机空间点进行 GPU/CPU enclosure 查询对比。

**方法**:
1. 加载 porous 或 cube-in-cube 测试场景
2. 生成 10000 个随机点（覆盖各 enclosure + 边界/凹角附近）
3. CPU 调用 `scene_get_enclosure_id_in_closed_boundaries()`
4. GPU 调用 `s3d_scene_view_find_enclosure_batch()`
5. `step_enc_locate_result()` 解析 GPU 结果为 `enc_id`
6. 逐点对比 `enc_id_cpu` vs `enc_id_gpu`

**预期结果**: 如果旋转矩阵差异导致问题，应能看到不一致。

**位置**: `stardis-cus3d/stardis-solver/0.16.2/src/test_sdis_b4_m10_enc_locate.c`
（即设计文档 T10.8 中计划的测试）

### T4: 凹角专项测试

**目的**: 构造已知会在凹角处失败的几何，验证 side 判定。

**方法**:
1. 构造 L 型或 V 型凹角几何（2 个三角形共享一条边，夹角 < 90°）
2. 在凹角内侧放置查询点
3. 分别用 CPU 和 GPU 判定 side
4. 验证 `enc_id` 一致

**预期结果**: 可能揭示旋转矩阵差异在极端几何下的影响。
