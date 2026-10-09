# Firefly（异常高亮像素）根因分析

**日期**: 2026-02-16  
**状态**: 根因已定位，待修复  
**影响**: Porous 场景 GPU 渲染结果中部分像素异常高亮，与 CPU 版本不一致  
**症状**: 运行时输出数百条 `wavefront: conductive delta_sphere robust exceeded 100 attempts` 警告

---

## 1. 问题描述

### 1.1 现象

GPU wavefront 求解器渲染 porous 示例场景时：
- 部分像素出现异常高亮（firefly），CPU 版本无此问题
- 运行日志中出现数百条 `conductive delta_sphere robust exceeded 100 attempts` 警告
- CPU 版本在相同场景下几乎不产生 robust 警告

### 1.2 场景参数

```
# porous.txt 核心参数
FOAM:  λ=237 W/mK, δ=0.0002m, T=UNKNOWN, ε=0.9
PLATE: λ=1 W/mK, T=700K
H_BOUNDARY: h=300
TRAD: 680-800K
picard_order: 1 (默认值，未在配置中指定)
```

### 1.3 已排除的假设

| 假设 | 排除原因 |
|------|---------|
| PicardN sfn_stack 错误拦截 | picard_order=1，sfn_stack_depth 始终为 0，M8 拦截不触发 |
| 失败路径贡献温度 | `T.done=0` 时 harvest 不累加，`done_reason=-1` 时 `T.done` 未被设为 1 |
| 6-ray enclosure query 概率性不一致 | 6-ray query 是确定性的（固定 PI/4 旋转方向），CPU/GPU 一致 |
| ds_robust_attempt 作用域不同 | GPU 按 step 重置（L790），与 CPU 的 per-call `iattempt` 作用域一致 |
| 射线范围差异 | 两端均为 `[FLT_MIN, delta_solid * 1.001]`，一致 |
| 光追精度差异（float vs double） | CPU (Embree) 和 GPU (cuBQL) 均使用 `float` 做射线求交，精度同级 |

---

## 2. 根因

### 2.1 缺失的 `HIT_ON_BOUNDARY` 检查

**CPU** 在 `scene_get_enclosure_id_in_closed_boundaries`（`sdis_scene_Xd.h` L1358）中，
6-ray enclosure query 的 resolve 循环有 4 级检查：

```c
/* CPU: sdis_scene_Xd.h L1350-L1370 */
FOR_EACH(idir, 0, 2*DIM) {
    SXD(scene_view_trace_ray(..., &hit));

    if(SXD_HIT_NONE(&hit)) continue;                    // CHECK 1: 无命中
    if(HIT_ON_BOUNDARY(&hit, P, dirs[idir])) continue;  // CHECK 2: 边/顶点 ← 关键
    
    fX(normalize)(N, hit.normal);
    cos_N_dir = fX(dot)(N, dirs[idir]);
    
    if(hit.distance > 1.e-6 && absf(cos_N_dir) > 1.e-2f) {  // CHECK 3+4
        scene_get_enclosure_ids(scn, hit.prim.prim_id, enc_ids);
        enc_id = cos_N_dir < 0 ? enc_ids[0] : enc_ids[1];
        break;
    }
}
```

**GPU** 在 `step_enc_query_resolve`（`sdis_wf_steps.c` L1031-L1070）中**缺失 CHECK 2**：

```c
/* GPU: sdis_wf_steps.c L1040-L1067 */
for(idir = 0; idir < 6; idir++) {
    const struct s3d_hit* hit = &p->enc_query.dir_hits[idir];

    if(S3D_HIT_NONE(hit)) continue;                     // CHECK 1: 有
    /* ❌ 无 HIT_ON_BOUNDARY 检查 */                      // CHECK 2: 缺失！
    if(hit->distance <= 1.e-6f) continue;                // CHECK 3: 有
    
    f3_normalize(N, hit->normal);
    cos_N_dir = f3_dot(N, p->enc_query.directions[idir]);
    if(absf(cos_N_dir) <= 1.e-2f) continue;             // CHECK 4: 有
    
    scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);
    enc_id = cos_N_dir < 0 ? enc_ids[0] : enc_ids[1];
    break;
}
```

`step_enc_query_fb_resolve`（第 7 条 fallback 光线）同样缺失此检查。

### 2.2 `HIT_ON_BOUNDARY`（`hit_on_edge`）的作用

定义于 `sdis_scene_Xd.h` L278-L323：

```c
#define ON_EDGE_EPSILON 1.e-4f

static INLINE int
hit_on_edge(const struct s3d_hit* hit, const float org[3], const float dir[3])
{
    /* 获取三角形三个顶点 */
    S3D(triangle_get_vertex_attrib(&hit->prim, 0, S3D_POSITION, &v0));
    S3D(triangle_get_vertex_attrib(&hit->prim, 1, S3D_POSITION, &v1));
    S3D(triangle_get_vertex_attrib(&hit->prim, 2, S3D_POSITION, &v2));

    /* 计算三角形面积 */
    tri_2area = f3_len(f3_cross(N, E0, E1));
    
    /* 计算命中点将三角形分成的 3 个子三角形面积 */
    hit_2area0 = ...; hit_2area1 = ...; hit_2area2 = ...;
    
    /* 任一子面积占比 < 1e-4 → 命中在边/顶点上 */
    if(hit_2area0/tri_2area < ON_EDGE_EPSILON
    || hit_2area1/tri_2area < ON_EDGE_EPSILON
    || hit_2area2/tri_2area < ON_EDGE_EPSILON)
        return 1;
    return 0;
}
```

当射线恰好命中两个三角形的共享边时，返回的 `prim_id` 可能是两侧三角形中的任意一个——
如果这两个三角形属于**不同 enclosure**，就会解析出错误的 enclosure ID。

### 2.3 为什么 Porous 场景特别敏感

Porous 场景的 FOAM 结构由薄壁 (δ=0.0002m) STL 网格构成，具有高密度三角形和大量共享边。
PI/4 旋转后的 6 个固定方向恰好可能与某些网格边对齐，导致 edge hit 的概率显著高于简单 box 几何。

---

## 3. 因果链

```
┌─────────────────────────────────────────────────────────────┐
│ 1. 传导路径初始化时，step_enc_query_emit 发射 6-ray query  │
│    → step_enc_query_resolve 处理结果                        │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│ 2. 某条光线命中三角形共享边（edge hit）                      │
│    ├─ CPU: HIT_ON_BOUNDARY → skip → 尝试下一方向/fallback   │
│    │       → 正确 enc_id                                    │
│    └─ GPU: 无检查 → 接受 edge hit → 可能选错 prim_id        │
│           → 解析出错误 enc_id                                │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│ 3. step_cnd_ds_check_temp (L660-L700)                       │
│    └─ ds_initialized=0 时首次执行:                           │
│       enc_id = enc_query.resolved_enc_id (错误值)            │
│       若 enc_id != rwalk.enc_id 但 medium 相同               │
│       → 静默采纳: p->rwalk.enc_id = enc_id (L694)           │
│       → p->ds_enc_id = enc_id (L700) ← baseline 被污染      │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│ 4. Delta-sphere 循环每步验证 enclosure:                      │
│    ├─ Case A (L616): 正向命中 → enc_id != ds_enc_id          │
│    │   → retry (ds_robust_attempt++)                         │
│    └─ Case B (L773): 6-ray 子查询 → enc_id != ds_enc_id     │
│        → retry (ds_robust_attempt++)                         │
│    正确的邻域查询持续返回真实 enc_id，与被污染的                │
│    ds_enc_id 不匹配 → 持续重试                                │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│ 5. 100 次重试耗尽                                            │
│    → "robust exceeded 100 attempts" 警告                     │
│    → res = RES_BAD_OP → goto error                           │
│    → done_reason = -1, active = 0, T.done 保持 0             │
│    → 路径丢弃                                                │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│ 6. harvest_completed_paths (L987):                           │
│    if(p->T.done) → 0 → 不累加温度                            │
│    → 该像素有效样本数减少 (count 偏低)                        │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│ 7. 生存者偏差 → 异常高亮 (Firefly)                           │
│                                                              │
│    正常像素: T_avg = (Σ辐射路径·T + Σ传导路径·T) / N_total    │
│    异常像素: 传导路径系统性失败                                │
│             T_avg ≈ Σ辐射路径·T / N_surviving                │
│                   ≈ 700K (PLATE 温度，偏高)                   │
│                                                              │
│    传导路径通常在 FOAM 内部发现较低温度 → 拉低均值             │
│    传导路径丢失 → 均值偏高 → 像素异常亮                       │
└─────────────────────────────────────────────────────────────┘
```

### 3.1 关键细节：harvest 分母问题

`harvest_completed_paths` 使用的是 `estimator->nrealisations`（成功次数）作为分母，
而非 `nrealisations`（总样本数）。在 GPU 的 persistent wavefront 中：

```c
/* sdis_solve_persistent_wavefront.c L987-L993 */
if(p->T.done) {
    estimator->temperature.sum  += p->T.value;
    estimator->temperature.sum2 += p->T.value * p->T.value;
    estimator->temperature.count += 1;
    estimator->realisation_time.count += 1;
    estimator->nrealisations += 1;  /* ← 只有成功路径计入 */
}
```

失败路径 (`T.done=0`) 既不贡献温度，也不增加 `nrealisations`，符合 CPU 行为。
但 CPU 几乎没有这类失败，而 GPU 的大量失败导致特定像素的有效样本数大幅降低，
统计估计量偏向于幸存的辐射路径。

---

## 4. 修复方案

### 4.1 方案 A：在 `step_enc_query_resolve` 中添加基于 UV 的 edge 检查（推荐）

GPU 的 `s3d_hit` 在 `trace_hit_fixup` 之后已有重心坐标：`uv[0]=w, uv[1]=u`。
可以利用这些坐标做等效的 edge 检查，无需重新获取顶点和计算三角形面积。

#### 4.1.1 新增辅助函数

在 `sdis_wf_steps.c` 顶部添加：

```c
/* Edge detection using post-fixup barycentric coordinates.
 * After trace_hit_fixup: uv[0]=w (v0 weight), uv[1]=u (v1 weight).
 * v2 weight = 1 - w - u.
 * If any barycentric coordinate < ON_EDGE_EPSILON, the hit lies on
 * or very near a triangle edge — same semantic as CPU's hit_on_edge(). */
#define ON_EDGE_EPSILON_UV 1.e-4f
static INLINE int
hit_on_edge_uv(const struct s3d_hit* hit)
{
    float w, u, v;
    if(hit->prim.type != S3D_TRIANGLE) return 0;
    w = hit->uv[0];
    u = hit->uv[1];
    v = 1.0f - w - u;
    if(v < 0.0f) v = 0.0f;
    return (w < ON_EDGE_EPSILON_UV
         || u < ON_EDGE_EPSILON_UV
         || v < ON_EDGE_EPSILON_UV);
}
```

#### 4.1.2 修改 `step_enc_query_resolve`

```c
/* sdis_wf_steps.c L1040+ */
for(idir = 0; idir < 6; idir++) {
    const struct s3d_hit* hit = &p->enc_query.dir_hits[idir];
    float N[3], cos_N_dir;

    if(S3D_HIT_NONE(hit)) continue;
    if(hit_on_edge_uv(hit)) continue;          /* ← 新增 */
    if(hit->distance <= 1.e-6f) continue;
    /* ... 其余不变 ... */
}
```

#### 4.1.3 修改 `step_enc_query_fb_resolve`

```c
/* sdis_wf_steps.c, step_enc_query_fb_resolve 中 */
if(!S3D_HIT_NONE(hit) && !hit_on_edge_uv(hit) && hit->distance > 1.e-6f) {
    /* ... 原有逻辑 ... */
}
```

#### 4.1.4 等效性论证

CPU 的 `hit_on_edge` 通过子三角形面积比判断：

```
area_ratio_i = sub_triangle_area_i / total_triangle_area
```

等效于重心坐标 `λ_i`（`area_ratio_i = λ_i`），阈值 `1e-4` 完全一致。
GPU 的 `trace_hit_fixup` 已将 Möller-Trumbore UV 转换为重心坐标
（`w = 1-mt_u-mt_v`, `u = mt_u`），并做了 `CLAMP` + 非负修正。
因此 `hit_on_edge_uv` 与 `hit_on_edge` 在数学上等价，且更高效
（无需获取顶点、计算叉积）。

### 4.2 方案 B：在 GPU batch trace 中增加 geometry-level filter（备选）

在 `s3d_scene_view_batch_trace.cpp` 的 CPU 后处理中，
对 enclosure query 桶的光线额外执行 `HIT_ON_BOUNDARY` 检查。
— 不推荐，因为 enclosure query 的 `filter_data=NULL`，
batch trace 层无法区分是否需要 edge 检查。

### 4.3 方案 C：在 `step_conductive_ds_process` 的直接命中路径也加 edge 检查

`step_conductive_ds_process`（L610-L632）中有直接从 hit primitive 获取 enc_id 的路径，
当 delta_sphere 2-ray 正向命中在边上时同样可能返回错误 enc_id：

```c
/* L610-L616 */
scene_get_enclosure_ids(scn, p->ds_hit0.prim.prim_id, enc_ids);
enc_id = f3_dot(p->ds_dir0, p->ds_hit0.normal) < 0
       ? enc_ids[0] : enc_ids[1];
if(enc_id != p->ds_enc_id) {   /* ← 若 hit0 在边上 → enc_id 可能错 */
    p->ds_robust_attempt++;     /* → 不必要的 retry */
```

CPU 对应路径（`sample_next_step_robust` L148-L152）同样没有 edge 检查，
但 CPU 的 `scene_view_trace_ray` 内部 hit filter 已包含 `HIT_ON_BOUNDARY`
（`sdis_scene_Xd.h` L527），而 GPU delta_sphere 射线未设置 filter
（`setup_delta_sphere_rays` 中 `filter_data = HIT_FILTER_DATA_NULL`）。

**但 CPU 也没有在这里做 edge 检查**——CPU 的 robust loop 也会因此 retry。
差异在于 CPU 的 enclosure 初始化是正确的，所以 retry 通常 1-2 次就成功。
GPU 因 baseline 被污染，retry 永远无法成功。

**因此方案 C 是次要优化，不是修 firefly 的必要条件。** 修复方案 A 已足够。

---

## 5. 测试方案

### 5.1 单元测试：`test_sdis_b4_m1_enclosure_edge_hit.c`

**模块**: `stardis-solver` (B-4 M1)  
**目标**: 验证 `step_enc_query_resolve` 在命中三角形边时正确跳过并尝试下一方向

#### 测试用例设计

```
几何体:
  两个共享边的三角形，分属不同 enclosure (enc_A, enc_B)

      v2
     / | \
    /  |  \
   / A | B \
  /    |    \
v0 ----+---- v1   ← 共享边 v0-v1
        v3

查询点: P 位于 enc_A 内部
6 条 PI/4 旋转方向中:
  - dirs[0] 被构造为恰好命中共享边 v0-v1
  - dirs[1] 命中三角形 A 的内部（远离边的位置）
```

#### 预期行为

| 检查 | 修复前 | 修复后 |
|------|--------|--------|
| dirs[0] 命中边 | 接受 → 可能返回 enc_B | skip → 尝试 dirs[1] |
| dirs[1] 命中 A 内部 | 不到达 | 接受 → 返回 enc_A |
| 最终 resolved_enc_id | enc_A **或** enc_B（不确定） | enc_A（确定） |

#### 实现要点

```c
/* test_sdis_b4_m1_enclosure_edge_hit.c */

/* 1. 构造两个共享边三角形，分属 enc_A 和 enc_B */
/* 2. 设置路径状态 p，模拟 enc_query.dir_hits[0] 在边上
 *    (uv[0] ≈ 0 或 uv[1] ≈ 0 或 1-uv[0]-uv[1] ≈ 0) */
/* 3. 设置 dir_hits[1] 在三角形内部 (所有重心坐标 > 0.1) */
/* 4. 调用 step_enc_query_resolve(p, scn) */
/* 5. CHK(p->enc_query.resolved_enc_id == enc_A) */

/* 附加: 验证 edge 阈值边界 */
/* Case: uv 恰好在 ON_EDGE_EPSILON 处 → 应被跳过 */
/* Case: uv 在 ON_EDGE_EPSILON * 2 处 → 应被接受 */
```

### 5.2 单元测试：`test_sdis_b4_m4_delta_sphere_enc_mismatch.c`

**模块**: `stardis-solver` (B-4 M4)  
**目标**: 验证 `step_cnd_ds_check_temp` 在 enc_query 返回错误 enc_id 时的行为

#### 测试用例

1. **Same-medium mismatch**: enc_query 返回 enc_B (相同 medium)，rwalk.enc_id=enc_A
   - 修复前: 静默采纳 enc_B → ds_enc_id=enc_B → 后续 retry 循环失败
   - 修复后: enc_query 不再返回错误 enc_id（被 edge 检查过滤）

2. **Different-medium mismatch**: enc_query 返回 enc_C (不同 medium)
   - 预期: `RES_BAD_OP_IRRECOVERABLE`（已有保护，无需修改）

### 5.3 集成测试：`test_sdis_wf_g2_thinwall_robust.c`

**模块**: `stardis-solver` (wf 集成)  
**目标**: 端到端验证薄壁几何下 delta_sphere 传导路径的 robust 重试不会系统性失败

#### 几何体

简化的 porous-like 场景：2 个同材料薄壁 enclosure，共享一个面

```
壁厚 δ = 0.001 (比 porous 的 0.0002 放大 5x，保证测试稳定性)
enclosure_A: 立方体 [0, 0.5]^3
enclosure_B: 立方体 [0.5, 1.0] × [0, 0.5]^2
共享面: x = 0.5
material: λ=237, ρ=2700, cp=900, T=UNKNOWN
边界温度: T_left=300K, T_right=700K
```

#### 验证条件

```c
/* 1. 运行 N=1000 次蒙特卡洛路径 */
/* 2. robust_exceeded 警告次数 < N * 0.01 (< 1%) */
/* 3. paths_failed / paths_completed < 0.02 (< 2%) */
/* 4. 温度估计在解析解 ±5% 范围内 */
```

### 5.4 精度测试：`test_s3d_batch_trace_edge.c`

**模块**: `custar-3d`  
**目标**: 验证 GPU batch trace 对共享边三角形的 prim_id 和 UV 返回值

#### 测试用例

```c
/* 构造两个共享边三角形 */
/* 射线分别命中: 三角形 A 内部、共享边、三角形 B 内部 */
/* 对比 batch trace 和 single trace 的结果:
 *   - prim_id 一致性
 *   - UV 坐标差异 < 1e-5
 *   - 对 edge hit: 验证至少一个 UV 分量 < ON_EDGE_EPSILON */
```

---

## 6. 修复验证检查清单

### 6.1 正确性

- [ ] `step_enc_query_resolve` 添加 `hit_on_edge_uv` 检查
- [ ] `step_enc_query_fb_resolve` 添加 `hit_on_edge_uv` 检查
- [ ] `test_sdis_b4_m1_enclosure_edge_hit` 通过
- [ ] `test_sdis_b4_m4_delta_sphere_enc_mismatch` 通过
- [ ] 所有现有 P0-P3 测试仍然通过（回归验证）

### 6.2 端到端

- [ ] `test_sdis_wf_g2_thinwall_robust` 通过
- [ ] Porous 场景渲染: robust 警告数量从数百条降至 < 10 条
- [ ] Porous 场景渲染: 无异常高亮像素（目视对比 CPU 结果）
- [ ] Porous 场景渲染: 逐像素温度差异 < 1%（排除噪声后）

### 6.3 性能

- [ ] `hit_on_edge_uv` 仅执行 3 次浮点比较，对热路径无性能影响
- [ ] Enclosure query 命中率仍然 > 95%（edge 检查只 skip 极少数 hit）

---

## 7. 风险评估

| 风险 | 等级 | 缓解 |
|------|------|------|
| UV 精度不足导致误判 | 低 | `trace_hit_fixup` 已做 CLAMP + 非负修正；阈值 1e-4 提供充足裕度 |
| 新检查导致更多 fallback | 低 | Porous 场景中 edge hit 占比 < 1%；fallback 有 brute-force 兜底 |
| 球体几何无 UV → `hit_on_edge_uv` 误判 | 无 | 函数检查 `prim.type != S3D_TRIANGLE` 时直接返回 0 |
| 修复后 enclosure query 全部 6 条都被 skip | 极低 | 7 条方向（6+1 fallback）+ brute-force 三级兜底 |

---

## 8. 附录：相关代码位置

| 文件 | 行号 | 说明 |
|------|------|------|
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` | L1031-L1070 | `step_enc_query_resolve` — 需修改 |
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` | L1110-L1140 | `step_enc_query_fb_resolve` — 需修改 |
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` | L967-L1028 | `step_enc_query_emit` — 无需修改 |
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` | L650-L745 | `step_cnd_ds_check_temp` — 无需修改（上游修复后不再触发） |
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` | L533-L640 | `step_conductive_ds_process` — 方案 C 可选优化 |
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c` | L757-L900 | `step_cnd_ds_step_advance` — 无需修改 |
| `stardis-cpu/stardis-solver/0.16.2/src/sdis_scene_Xd.h` | L1318-L1390 | CPU `scene_get_enclosure_id_in_closed_boundaries` — 参考实现 |
| `stardis-cpu/stardis-solver/0.16.2/src/sdis_scene_Xd.h` | L278-L323 | CPU `hit_on_edge` — 参考实现 |
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_scene_Xd.h` | L476-L537 | GPU `hit_filter_function` — 含 `HIT_ON_BOUNDARY`（filter 路径有，enc query 路径没有） |
| `stardis-cus3d/custar-3d/0.10/src/cus3d_trace_util.h` | L1-L47 | `trace_hit_fixup` — UV 转换逻辑 |
| `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_batch_trace.cpp` | L1-L500 | GPU batch trace — Top-K + CPU 后处理 |
| `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | L967-L1010 | `harvest_completed_paths` — T.done 守卫 |
