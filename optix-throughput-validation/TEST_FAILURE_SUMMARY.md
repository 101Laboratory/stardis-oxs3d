# ox_s3d 测试失败汇总报告

**最后更新**: 2026-02-22  
**构建目录**: `optix-throughput-validation/build_s3d`  
**测试总数**: 20 | **通过**: 14 | **失败**: 6  
**退出码说明**: `0xc0000409` = CHK() 宏断言失败后调用 `exit(1)`，Windows 报告为 STATUS_STACK_BUFFER_OVERRUN

---

## 修复进度

| 轮次 | 日期 | 通过/总数 | 修复内容 |
|------|------|-----------|---------|
| 初始 | 02-06 | 5/20 | 初始状态 |
| Round 1 | 02-06 | 8/20 | Shape ID 分配、实例守护、参数校验、球体支持、AABB 变换、Batch 统计 |
| Round 2 | 02-06 | 10/20 | has_geometry 标志、NULL 检查、mesh 校验、空场景守护、CDF 球体支持、采样实例、属性校验 |
| Round 3 | 02-22 | 14/20 | 球体参数校验、空间枚举校验、采样 u/v 校验、Shape ID 持久化（不再在 detach/clear 时重置）、球体 UV/法线修复、体积计算修复（有符号 + 球体分离）、实例变换应用到 get_attrib |

---

## 一、通过的测试（14 个）

| # | 测试名 | 耗时 | 通过时间 |
|---|--------|------|---------|
| 1 | optix_throughput_validation | 8.04s | 初始 |
| 2 | test_s3d_accel_struct_conf | 0.41s | 初始 |
| 3 | test_s3d_batch_closest_point | ~30s | Round 1 |
| 4 | test_s3d_batch_trace | ~30s | Round 1 |
| 6 | test_s3d_device | 0.55s | 初始 |
| 7 | test_s3d_primitive | ~2s | Round 2 |
| 8 | test_s3d_sampler | ~2s | Round 2 |
| 9 | test_s3d_sample_sphere | ~30s | Round 2 |
| 12 | test_s3d_scene_view_aabb | 0.51s | Round 2 |
| 13 | test_s3d_seams | 0.54s | 初始 |
| 16 | test_s3d_sphere_box | 28.68s | 初始 |
| 17 | test_s3d_sphere_instance | 26.92s | Round 3 |
| 18 | test_s3d_trace_ray | 33.94s | Round 3 |
| 20 | test_s3d_trace_ray_sphere | ~30s | Round 3 |

---

## 二、当前失败测试汇总表（6 个）

| # | 测试名 | 失败位置 | 退出码 | 失败类别 | 上轮位置 |
|---|--------|---------|--------|---------|---------|
| 5 | test_s3d_closest_point | :894 | 0xc0000409 | 最近点查询正确性 | :1181 |
| 10 | test_s3d_scene | :227 | 0xc0000409 | 模式组合校验 | :145 |
| 11 | test_s3d_scene_view | :274 | 0xc0000409 | UV 一致性 | :226 |
| 14 | test_s3d_shape | (无行号) | SegFault | 内存安全/生命周期 | :72 |
| 15 | test_s3d_sphere | :104 | 0xc0000409 | 体积快照语义 | SegFault |
| 19 | test_s3d_trace_ray_instance | :140 | 0xc0000409 | 实例 prim_id / 法线 | :140 |

---

## 三、当前失败详细分析

### 类别 A：最近点查询正确性（1 个测试）

#### A1. test_s3d_closest_point — 行 894
```c
CHK(!S3D_HIT_NONE(&hit));
```
**上下文**: 在 10000 个随机查询点的循环中，对单个三角形执行 `closest_point(view, pos, INF, NULL, &hit)`，部分查询返回 HIT_NONE。  
**直接原因**: `s3d_scene_view_closest_point` 以 `radius=INF` 查询时，某些查询点位置使 OptiX NN 查询未能找到最近点。可能原因：(1) OptiX nearest-neighbor 查询的搜索半径在设备端被截断；(2) 查询坐标超出 BVH 覆盖范围时未正确回退。  
**进展**: 相较初始失败位置 L1181（参数校验缺失）已大幅前进，参数校验已修复，现为查询正确性问题。  
**涉及模块**: `ox_s3d_scene_view.cpp` — `s3d_scene_view_closest_point` / OptiX NN 查询参数

---

### 类别 B：模式组合校验（1 个测试）

#### B1. test_s3d_scene — 行 227
```c
CHK(s3d_scene_view_create(scn3, S3D_SAMPLE|S3D_TRACE, &scnview3) == RES_BAD_ARG);
```
**直接原因**: 创建 scene_view 时，`S3D_SAMPLE|S3D_TRACE` 组合模式应返回 `RES_BAD_ARG`（互斥模式），但当前实现未校验模式标志的互斥性。  
**进展**: 相较初始失败位置 L145（实例守护校验）已大幅前进，实例守护已修复。  
**涉及模块**: `ox_s3d_scene_view.cpp` — `s3d_scene_view_create` / 模式标志校验

---

### 类别 C：UV 一致性问题（1 个测试）

#### C1. test_s3d_scene_view — 行 274
```c
CHK(f2_eq(hit.uv, hit2.uv) == 1);
```
**上下文**: scn 和 scn2 各附加 plane+cube（顺序不同），分别创建 scnview 和 scnview2。两个视图追踪同一射线命中 plane，UV 应相同。先 detach plane from scn2，再在旧 scnview2 上追踪（视图已 build，应为快照），UV 应与 scnview 的命中结果一致。  
**直接原因**: 可能原因：(1) UV fixup (`trace_hit_fixup`) 的行为依赖每个 scene_view 中 geom 的顺序索引，不同场景中相同几何体的 SBT 偏移不同导致 UV 映射差异；(2) 场景中 shape 附加顺序影响 OptiX IAS/GAS 构建，进而影响 hit 结果中的 `primitiveIndex` 映射。  
**进展**: 相较初始失败位置 L226（geom_id 映射）已前进，geom_id 映射已修复，现为 UV 一致性残留问题。  
**涉及模块**: `ox_s3d_scene_view.cpp` / `ox_s3d_internal.h` — `hitresult_to_s3d_hit` / UV fixup 逻辑

---

### 类别 D：内存安全 / 生命周期（1 个测试）

#### D1. test_s3d_shape — SegFault（无行号）
```
***Exception: SegFault  1.85 sec
```
**上下文**: 测试通过所有 CHK 断言后（无 `error:` 输出），在某函数调用中崩溃。OptiX 日志仅显示设备初始化（无 UnifiedTracer 创建），表明崩溃发生在 CPU 端操作中。  
**直接原因**: 可能原因：(1) `s3d_mesh_copy` 实现中访问未初始化或越界内存；(2) `s3d_shape_ref_put` 清理链中，shape 被销毁后 instance 仍持有悬空引用；(3) 在多次 `mesh_setup_indexed_vertices` 调用后内部状态不一致导致后续操作访问无效内存。  
**进展**: 相较初始失败位置 L72（shape ID 无效）已大幅前进，Shape ID 分配已修复，但后续操作中存在内存安全问题。  
**涉及模块**: `ox_s3d_shape.cpp` — `s3d_mesh_copy` / `s3d_shape_ref_put` / 内部状态管理

---

### 类别 E：体积计算快照语义（1 个测试）

#### E1. test_s3d_sphere — 行 104
```c
CHK(eq_epsf(volume, (float)(4.0/3.0*PI*radius*radius*radius), 1.e-6f));
```
**上下文**: 
1. 创建球体并附加到场景，创建 view
2. 计算 volume → 期望 +4/3·π·r³ ✓
3. 调用 `s3d_shape_flip_surface(sphere0)` — **在已有 view 上**
4. 计算 volume → **仍期望 +4/3·π·r³**（view 是快照，flip 尚未生效）← L104 失败
5. 重建 view
6. 计算 volume → 期望 -4/3·π·r³（flip 生效）

**直接原因**: `compute_volume` 在查询时直接读取 `shape->flip_surface` 当前值，而非使用 view 构建时的快照状态。flip_surface 修改应在 view rebuild 后才生效，但当前实现在运行时直接访问 shape 的 mutable 状态。  
**修复方向**: 在 view build/rebuild 时将每个 shape 的 `flip_surface` 状态快照到 scene_view 内部数据结构中，`compute_volume` 使用快照值而非实时值。  
**进展**: 相较初始状态 SEGFAULT 已大幅改善，球体体积计算基本正确，仅快照语义有偏差。  
**涉及模块**: `ox_s3d_scene_view.cpp` — `s3d_scene_view_compute_volume` / rebuild 快照逻辑

---

### 类别 F：实例追踪结果（1 个测试）

#### F1. test_s3d_trace_ray_instance — 行 140
```c
CHK(hit[0].prim.prim_id == 0);
```
**上下文**: 
1. 创建 quad（2 个三角形），设置 hit filter
2. 将 quad 绕 pitch 旋转 π 创建实例 `quad_inst`
3. 场景1: 仅含 `quad_inst`；场景2: 仅含 `quad`（原始）
4. 同一射线 `(0, 0.5, -1)→(0, 0, 1)` 追踪两个场景
5. 期望 `hit[0].prim_id == 0`（实例场景命中第一个三角形），`hit[1].prim_id == 1`（原始场景命中第二个三角形）

**直接原因**: π 旋转使 quad 翻转，射线应先命中不同的三角形。可能原因：(1) hit filter 函数 `filter` 中的逻辑影响命中选择；(2) OptiX 实例变换后 `primitiveIndex` 的映射不正确；(3) 旋转后 CW/CCW 面朝向翻转影响命中的三角形选择。  
**进展**: 失败位置不变（L140），表示此问题在 Round 3 中未被修复。  
**涉及模块**: `ox_s3d_scene_view.cpp` — 实例变换 + hit filter + prim_id 映射

---

## 四、已修复的历史问题

以下问题在 Round 1-3 中已成功修复：

| 原类别 | 原测试 | 原失败位置 | 修复内容 |
|--------|--------|-----------|---------|
| 几何 ID | test_s3d_shape :72 | shape 创建时 ID 为 INVALID | 创建时分配唯一 ID (next_shape_id++) |
| 几何 ID | test_s3d_scene_view :226 | geom_id 映射错误 | geom_to_inst 映射表 + hitresult 解码修正 |
| 参数校验 | test_s3d_trace_ray :191 | 方向非归一化未校验 | 添加 dir 归一化检查 |
| 参数校验 | test_s3d_sampler :74 | u/v/w 范围未校验 | 添加 \[0,1) 范围检查 |
| 参数校验 | test_s3d_closest_point :1181 | radius<=0 未校验 | 添加 radius 合法性检查 |
| 球体支持 | test_s3d_sphere SEGFAULT | 未初始化球体崩溃 | 球体初始化守护检查 |
| 球体支持 | test_s3d_sample_sphere :61 | 球体 CDF 构建缺失 | CDF 支持球体面积 |
| 球体支持 | test_s3d_sphere_instance :69 | 球体实例采样失败 | 实例球体面积 + 采样 |
| 球体支持 | test_s3d_trace_ray_sphere :97 | 球体法线查询失败 | 球体 get_attrib 实现 |
| Batch 统计 | test_s3d_batch_closest_point :284 | batch_accepted 不正确 | 统计计数器修复 |
| Batch 统计 | test_s3d_batch_trace :229 | batch_accepted 不正确 | 统计计数器修复 |
| AABB | test_s3d_scene_view_aabb :156 | 实例变换未应用到 AABB | 实例变换矩阵传播 |
| 属性校验 | test_s3d_primitive :148 | 属性可用性未检查 | 添加属性可用性校验 |
| 实例守护 | test_s3d_scene :145 | 实例化 shape 可直接 attach | 添加实例守护检查 |
| 空场景 | test_s3d_scene :200+ | 空场景各种操作 | 空场景守护 + 空 AABB |

---

## 五、修复优先级建议

| 优先级 | 类别 | 测试 | 修复复杂度 | 说明 |
|--------|------|------|-----------|------|
| P0 | E 体积快照 | test_s3d_sphere | 中 | 需要在 view build 时快照 flip_surface 状态 |
| P0 | B 模式校验 | test_s3d_scene | 低 | 在 scene_view_create 中添加模式互斥检查 |
| P1 | D 内存安全 | test_s3d_shape | 高 | 需定位 SegFault，可能涉及 mesh_copy 或 ref 生命周期 |
| P1 | F 实例追踪 | test_s3d_trace_ray_instance | 中 | 需分析 hit filter + 实例变换对 prim_id 的影响 |
| P2 | C UV 一致性 | test_s3d_scene_view | 中 | UV fixup 可能依赖场景内部顺序 |
| P2 | A 最近点 | test_s3d_closest_point | 中-高 | OptiX NN 查询参数或 BVH 覆盖范围问题 |

---

## 六、统计摘要

- **已修复**: 9 个测试（Round 1-3），从 5/20 通过提升到 14/20 通过
- **剩余**: 6 个测试，涵盖 6 个不同类别
- **低复杂度**: 1 个（模式互斥校验）
- **中复杂度**: 3 个（体积快照、UV 一致性、实例追踪）
- **高复杂度**: 2 个（内存安全 SegFault、最近点查询正确性）
