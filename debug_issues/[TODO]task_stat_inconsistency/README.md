# Ray Statistics Inconsistency Analysis: CPU vs GPU (cus3d)

**创建日期**: 2026-03-01  
**状态**: 分析完成  
**关联分支**: `feat/cpu-ray-stats` (stardis-cpu-raystats worktree)

---

## 1. 观测到的数据

### CPU (stardis-cpu-raystats, solve_camera)
```
Ray statistics: rays=3352409287 (rad=863304355 cond_ds=2489104904(retry=28) shadow=0 enc=0 startup=0 closest_pt=0 other=0)
```

### GPU (stardis-cus3d, persistent wavefront)
```
persistent wavefront summary:
  total_steps=481618  total_rays=12913258262  avg_wavefront_width=13556.4
  refill_phase: rays=12484340710 (96.7%)  wall=461.058s
  drain_phase:  rays=428905458 (3.3%)  steps=95313  wall=38.769s
  batch_size: min=2, max=44088
  rays: radiative=866422168  cond_ds=2491154618(retry=8)  shadow=0  enclosure=9555681468  startup=0  other=0
```

### 差异汇总

| 类别 | CPU | GPU | 差值 | 差异% | 严重性 |
|------|-----|-----|------|-------|--------|
| **total** | 3,352,409,287 | 12,913,258,262 | -9,560,848,975 | -74.0% | **CRITICAL** |
| **radiative** | 863,304,355 | 866,422,168 | -3,117,813 | -0.36% | Expected |
| **cond_ds** | 2,489,104,904 | 2,491,154,618 | -2,049,714 | -0.08% | Expected |
| **cond_ds_retry** | 28 | 8 | +20 | +250% | Expected |
| **shadow** | 0 | 0 | 0 | — | OK |
| **enclosure** | **0** | **9,555,681,468** | **-9,555,681,468** | **-100%** | **CRITICAL** |
| **startup** | 0 | 0 | 0 | — | OK |
| **closest_pt** | 0 | (不追踪) | — | — | N/A |
| **other** | 0 | 0 | 0 | — | OK |

---

## 2. 逐项分析

### 2.1 CRITICAL — enclosure: 0 (CPU) vs 9,555,681,468 (GPU)

**根因**: CPU `rays_enclosure` 计数器**从未被增量**。

CPU 的 enclosure 查询实现在** scene 层**——`sdis_scene_Xd.h` 中的 `scene_get_enclosure_id_in_closed_boundaries()`。该函数直接调用 `scene_view_trace_ray()`，跳过了求解器层的 ray stats 计数基础设施。

#### CPU 的 enclosure 查询流程

```
scene_get_enclosure_id_in_closed_boundaries()
  ├── 构建 PI/4 旋转矩阵
  ├── FOR EACH idir IN [+X, -X, +Y, -Y, +Z, -Z]:
  │   ├── 旋转方向向量
  │   ├── scene_view_trace_ray()  ← 直接调用，不经过 ray_stats !!
  │   ├── 验证命中（距离 > 1e-6, |cos(N,dir)| > 1e-2）
  │   └── 命中有效 → break（early exit）
  └── 全部 6 射线失败 → scene_get_enclosure_id() 暴力遍历所有图元
```

对比 GPU 的流程：
```
step_enc_query_emit()
  ├── 构建 6 条 PI/4 旋转射线
  ├── 设置 ray_count_ext = 6, phase = PATH_ENC_QUERY_EMIT
  ├── → 进入 pool_collect_ray_requests_bucketed()
  │     └── tl_rays_enclosure += nrays (= 6)  ← 总是计入 6 条射线
  ├── → GPU batch trace 6 rays
  └── step_enc_query_resolve()
        ├── 遍历 6 命中结果
        ├── 有效 → 返回 enc_id
        └── 失败 → step_enc_query_fb_emit() → 1 fallback ray
              └── 也计入 rays_enclosure
```

#### 关键差异

| 方面 | CPU | GPU |
|------|-----|-----|
| 射线发射策略 | 逐条发射，early exit（通常 1-2 条） | 批量发射 6 条（然后逐一检查结果） |
| 计数 | **不计数** | 固定按 6（或 1 fallback）计数 |
| Fallback 策略 | 暴力遍历所有图元（大量 trace_ray） | 1-ray fallback → M10 BVH closest-point |

#### 预估 CPU 真实 enclosure 射线数

GPU 发射了 9,555,681,468 条 enclosure 射线。若每次查询发射 6 条：  
→ 约 1,592,613,578 次查询

CPU 每次查询通常在 1-2 条射线后命中（early exit），但场景复杂时可能更多。  
假设 CPU 平均 ~1.5 条/查询 → **约 2,388,920,367 条 CPU enclosure 射线**

> **注意**: 这只是粗略估计。GPU 固定 6 条/查询的设计是为了批量化 GPU trace 而优化的，CPU 的 early exit 是更高效的串行策略。两端的实际查询次数应该一致。

#### CPU enclosure 查询调用点（stardis-cpu-raystats 分支）

| # | 文件 | 行 | 语境 | GPU 对应 |
|---|------|----|------|----------|
| 1 | `sdis_heat_path_conductive_Xd.h` | L43 | 导热路径入口 | `PATH_CND_INIT_ENC` |
| 2 | `sdis_heat_path_conductive_wos_Xd.h` | L83 | WoS 一致性检查 | enc_query |
| 3 | `sdis_heat_path_conductive_delta_sphere_Xd.h` | L152 | DS 步进验证 | `PATH_CND_DS_STEP_ENC_VERIFY` |
| 4 | `sdis_heat_path_conductive_delta_sphere_Xd.h` | L351 | DS 路径入口 | `PATH_CND_INIT_ENC` |
| 5 | `sdis_heat_path_boundary_Xd_c.h` | L322 | 边界重注入 dir0 miss | `PATH_BND_SF_REINJECT_ENC` |
| 6 | `sdis_heat_path_boundary_Xd_c.h` | L334 | 边界重注入 dir1 miss | `PATH_BND_SF_REINJECT_ENC` |
| 7 | `sdis_heat_path_boundary_Xd_c.h` | L472 | 重注入有效性检查 | `PATH_BND_SS_REINJECT_ENC` |

---

### 2.2 Expected — radiative: 863,304,355 vs 866,422,168 (−0.36%)

**根因**: 不同执行模型下的 RNG 消耗顺序差异。

- CPU 串行执行每条路径（一条路径完整走完后开始下一条），RNG 序列严格顺序消耗
- GPU wavefront 并行执行所有路径（所有路径同时推进一步），RNG 消耗顺序完全不同
- Random123 counter-based RNG 保证同一计数器产生同一随机数，但不同的消耗顺序导致不同路径分支

差异数量 ~310 万条射线，仅占总量 0.36%。对于蒙特卡洛模拟来说，这是**完全预期的正常偏差**。最终积分结果应在统计误差范围内一致。

**结论**: 无需修复。

---

### 2.3 Expected — cond_ds: 2,489,104,904 vs 2,491,154,618 (−0.08%)

与 radiative 相同的根因。差异 ~205 万条（0.08%），在蒙特卡洛正常偏差范围内。

**结论**: 无需修复。

---

### 2.4 Expected — retry: 28 vs 8

**根因**: retry（robust fallback）是极稀有事件，由路径几何位置决定。

- CPU 的 `sample_next_step_robust` 在 `iattempt > 0` 时 → `rs->rays_cond_ds_retry += 2`（每次 retry 增 2）
- GPU 的 `pool_collect_ray_requests_bucketed` 在 `p->ds_robust_attempt > 0` 时 → `tl_rays_cond_ds_retry += nrays`

CPU retry=28 → 14 次 retry iteration  
GPU retry=8 → 4 次 retry iteration

由于 retry 只在特定几何位置触发（两条方向射线同时 miss），不同的 RNG 序列导致不同路径到达这些位置的频率不同。

**结论**: 无需修复。差异在统计噪声范围内。

---

### 2.5 OK — closest_pt: 0 (CPU) vs 未追踪 (GPU)

`rays_closest_pt` 是 CPU 独有的计数器（GPU 的 closest_point 查询通过独立的 `pool_collect_cp_requests` 管线处理，不计入 ray stats）。

CPU closest_pt=0 表示测试场景**未使用 WoS 或 Custom 导热算法**（仅使用 delta-sphere）。WoS 的 `closest_point` 查询才会触发 `rays_closest_pt` 增量。

对于 CPU 增量点的代码确认：
- `sdis_heat_path_conductive_wos_Xd.h:340` — `check_diffusion_position` 中
- `sdis_heat_path_conductive_wos_Xd.h:513` — `sample_next_position` 中
- `sdis_heat_path_conductive_custom_Xd.h:119` — `get_path_hit` 中

**结论**: 符合预期。无需修复。

---

### 2.6 OK — shadow: 0 / startup: 0 / other: 0

两端均为 0，说明：
- 场景中没有外部净通量计算（shadow=0）
- 没有对流路径（startup=0，意味着场景无流体区域）
- 没有未分类射线（other=0）

**结论**: 一致。无需修复。

---

## 3. 根因归纳

| 优先级 | 问题 | 类型 | 影响 |
|--------|------|------|------|
| **P0** | CPU enclosure 射线未计数 | CPU 计数遗漏 | total 少 74% |
| P3 | rad/cond_ds 微差 | 预期行为 | 无 |
| P3 | retry 数量不同 | 预期行为 | 无 |
| — | closest_pt=0 | 场景相关 | 无 |

**唯一需要修复的问题**: CPU 端 enclosure 射线计数遗漏。

---

## 4. 修复方案

### 方案 A: 在 scene 函数接口中传递 ray_stats（推荐）

修改 `scene_get_enclosure_id_in_closed_boundaries()` 签名，增加 `struct sdis_ray_stats* rs` 参数并在每次 `scene_view_trace_ray()` 调用前 `SDIS_RAY_STAT_INC(rs, rays_enclosure)`。

**优点**:
- 精确计数（CPU 的 early exit 只计实际发射的射线数）
- 也能覆盖 fallback (`scene_get_enclosure_id`) 中的射线

**缺点**:
- 需修改 scene 层接口（`sdis_scene_c.h`, `sdis_scene.c`, `sdis_scene_Xd.h`）
- 所有 7 个调用点需传递 `rs` 或 `ctx->ray_stats`

### 方案 B: 让 scene 函数返回射线计数

修改 `scene_get_enclosure_id_in_closed_boundaries()` 增加 `unsigned* out_nrays` 输出参数，返回实际发射的射线数。调用者在求解器层合计到 `rays_enclosure`。

**优点**:
- 不引入 ray_stats 对 scene 层的依赖
- 精确计数

**缺点**:
- 所有调用点需要处理额外输出参数
- Fallback 函数也需要同样改造

### 方案 C: 在调用点固定增量（近似）

在每个调用点估算射线数（例如固定 +6 或查询后增加实际数）。

**优点**: 最小改动  
**缺点**: 不精确，与 GPU 对比仍会有差异

### 推荐: 方案 A

理由：与现有 `SDIS_RAY_STAT_INC` 模式一致；精确计数；scene 函数已有大量参数，增加一个不影响可读性。

---

## 5. 修复涉及文件清单

### 接口修改
| 文件 | 修改内容 |
|------|----------|
| `sdis_scene_c.h` | `scene_get_enclosure_id_in_closed_boundaries` 签名加 `rs` |
| `sdis_scene.c` | 分发函数加 `rs` 透传 |
| `sdis_scene_Xd.h` | 实现中在 `scene_view_trace_ray` 前加 `SDIS_RAY_STAT_INC(rs, rays_enclosure)` |

### 调用点修改（共 7 处）
| 文件 | 行 | 传递 |
|------|----|------|
| `sdis_heat_path_conductive_Xd.h` | L43 | `ctx->ray_stats` |
| `sdis_heat_path_conductive_wos_Xd.h` | L83 | `rs`（已有参数） |
| `sdis_heat_path_conductive_delta_sphere_Xd.h` | L152 | `rs`（已有参数） |
| `sdis_heat_path_conductive_delta_sphere_Xd.h` | L351 | `rs`（已有参数） |
| `sdis_heat_path_boundary_Xd_c.h` | L322 | `args->ray_stats` |
| `sdis_heat_path_boundary_Xd_c.h` | L334 | `args->ray_stats` |
| `sdis_heat_path_boundary_Xd_c.h` | L472 | `args->ray_stats` |

### Fallback 函数（如果需要精确覆盖）
| 文件 | 函数 | 说明 |
|------|------|------|
| `sdis_scene_Xd.h` | `scene_get_enclosure_id()` | 暴力遍历，每个图元可能 1-3 次 trace_ray |

---

## 6. 关于 GPU 和 CPU 总射线数一致性的说明

即使修复了 enclosure 计数，CPU 和 GPU 的 total rays **仍然不会完全相等**：

1. **enclosure 射线数/查询不同**: GPU 固定 6 射线/查询，CPU 为 1-6（early exit）
2. **radiative/cond_ds 微差**: 不同 RNG 消耗顺序（预期行为）
3. **Fallback 策略不同**: CPU 暴力遍历 vs GPU M10 BVH closest-point

预期修复后的对比：
- `enc` 数量级应从 0 跃升至数十亿量级（略低于 GPU 的 9.5B，因 CPU early exit）
- `rad` / `cond_ds` 保持 <1% 差异
- `total` 差距大幅缩小，但不会完全相等

---

## 7. 参考文件路径

### GPU (stardis-cus3d)
- 统计计数: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` L870-900, L1133-1165, L1293-1323
- 统计累加: 同文件 L2436-2444
- 统计日志: 同文件 L2188-2230
- Enclosure 查询: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c` L118-260
- Enclosure 调用点: `sdis_wf_steps_core.c` L476,L514; `sdis_wf_steps_cnd.c` L158; `sdis_wf_steps_bnd_sf.c` L367; `sdis_wf_steps_bnd_ss.c` L504,L516,L562
- 状态枚举: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_types.h` L46-150

### CPU (stardis-cpu-raystats)
- 统计结构: `stardis-solver/0.16.2/src/sdis_heat_path.h` L34-77
- Enclosure 实现: `stardis-solver/0.16.2/src/sdis_scene_Xd.h` L1210-1394
- Enclosure 接口: `stardis-solver/0.16.2/src/sdis_scene_c.h` L270,L287
- 统计聚合: `stardis-solver/0.16.2/src/sdis_solve_camera.c` L698-720
