# fix: test_s3d_trace_ray_instance (#19) — dirty 重建破坏快照语义

**目标测试**: test_s3d_trace_ray_instance L140  
**涉及文件**: `ox_s3d_scene.cpp`, `ox_s3d_scene_view.cpp`  
**日期**: 2026-02-22  
**依赖**: 与 `snapshot_semantics_fix.md` Step 1 完全相同（移除 dirty 标记），本文档提供独立根因推导

---

## 一、测试代码逻辑

### 完整流程 (test_s3d_trace_ray_instance.c L89–L165)

```c
// L106-108: 创建第一个场景 scn#1，attach quad，实例化
CHK(s3d_scene_create(dev, &scn) == RES_OK);           // scn = scn#1
CHK(s3d_scene_attach_shape(scn, quad) == RES_OK);
CHK(s3d_scene_instantiate(scn, &quad_inst) == RES_OK); // quad_inst.child_scene = scn#1
CHK(s3d_instance_set_transform(quad_inst, transform) == RES_OK); // R_x(π)
CHK(s3d_scene_ref_put(scn) == RES_OK);                // 释放 scn#1 引用

// L113-115: 创建第二个场景 scn#2，attach quad_inst → 创建 view[0]
CHK(s3d_scene_create(dev, &scn) == RES_OK);            // scn = scn#2
CHK(s3d_scene_attach_shape(scn, quad_inst) == RES_OK);
CHK(s3d_scene_view_create(scn, S3D_TRACE, &view[0]) == RES_OK);  // ★ view[0] 含 quad_inst

// L117-119: clear scn#2 → 重新 attach 裸 quad → 创建 view[1]
CHK(s3d_scene_clear(scn) == RES_OK);                   // ← ⚠️ 标记 view[0]->dirty
CHK(s3d_scene_attach_shape(scn, quad) == RES_OK);      // ← ⚠️ 再次标记 view[0]->dirty
CHK(s3d_scene_view_create(scn, S3D_TRACE, &view[1]) == RES_OK);  // view[1] 含裸 quad

// L128-131: 同一射线分别 trace 两个 view
CHK(s3d_scene_view_trace_ray(view[0], ray.org, ray.dir, ...));  // ★ dirty → rebuild!
CHK(s3d_scene_view_trace_ray(view[1], ray.org, ray.dir, ...));

// L140-145: 断言命中结果
CHK(hit[0].prim.prim_id == 0);                // ★ L140 FAILS — 期望实例场景命中 tri 0
CHK(hit[1].prim.prim_id == 1);                // 原始场景命中 tri 1
CHK(hit[0].prim.geom_id == quad_id);
CHK(hit[1].prim.geom_id == quad_id);
CHK(hit[0].prim.inst_id == quad_inst_id);     // 期望实例 ID
CHK(hit[1].prim.inst_id == S3D_INVALID_ID);   // 非实例
```

### Quad 几何

```
v0(-1,-1,0)   v1(-1,1,0)   v2(1,1,0)   v3(1,-1,0)
三角形 0: (v0, v1, v3) = {(-1,-1,0), (-1,1,0), (1,-1,0)}
三角形 1: (v3, v1, v2) = {(1,-1,0), (-1,1,0), (1,1,0)}
```

### 射线

```
origin = (0, 0.5, -1),  direction = (0, 0, 1)
命中 z=0 平面的交点 = (0, 0.5, 0)
```

---

## 二、几何分析

### 原始 quad（无变换）

射线交点 `(0, 0.5, 0)` 在哪个三角形内？

**三角形 0** `(v0, v1, v3)` = `{(-1,-1,0), (-1,1,0), (1,-1,0)}`：
- 这是一个直角三角形，斜边 v1→v3 的方程为 `y = -x`
- 交点 `(0, 0.5)`：`0.5 > -0 = 0` → **不在三角形 0 内**

**三角形 1** `(v3, v1, v2)` = `{(1,-1,0), (-1,1,0), (1,1,0)}`：
- 斜边 v3→v1 的方程为 `y = -x`
- 交点 `(0, 0.5)`：`0.5 > 0` → **在三角形 1 内** → `prim_id = 1` ✓

### 实例 quad（R_x(π) 旋转）

`f33_rotation_pitch(transform, π)` 绕 X 轴旋转 180°：

$$R_x(\pi) = \begin{pmatrix} 1 & 0 & 0 \\ 0 & -1 & 0 \\ 0 & 0 & -1 \end{pmatrix}$$

变换后顶点：
```
v0' = (-1,  1, 0)    v1' = (-1, -1, 0)
v2' = ( 1, -1, 0)    v3' = ( 1,  1, 0)
```

三角形 0'：`{v0', v1', v3'}` = `{(-1,1,0), (-1,-1,0), (1,1,0)}`
三角形 1'：`{v3', v1', v2'}` = `{(1,1,0), (-1,-1,0), (1,-1,0)}`

射线交点仍为 `(0, 0.5, 0)`：
- **三角形 0'** 包含 `(0, 0.5)` → `prim_id = 0` ✓
- **三角形 1'** 不包含 `(0, 0.5)`

### 期望结果

| view | 几何 | 命中三角形 | prim_id |
|------|------|-----------|---------|
| view[0] | quad_inst (R_x(π)) | tri 0' | **0** |
| view[1] | quad (原始) | tri 1 | **1** |

---

## 三、根因：dirty 标记导致 view[0] 被错误重建

### 因果链

```
s3d_scene_view_create(scn#2, ..., &view[0])
  └→ rebuild_tracer(view[0])    // 遍历 scn#2->shapes = {quad_inst} → 正确构建含实例的 GAS
  └→ view[0]->dirty = false

s3d_scene_clear(scn#2)
  └→ scn#2->shapes.clear()
  └→ for (auto* v : scn->views) v->dirty = true    // ⚠️ view[0]->dirty = true

s3d_scene_attach_shape(scn#2, quad)
  └→ scn#2->shapes = {quad}                         // scn#2 现在只含裸 quad
  └→ for (auto* v : scn->views) v->dirty = true    // ⚠️ view[0]->dirty 仍为 true

s3d_scene_view_trace_ray(view[0], ...)
  └→ ensure_built(view[0])
     └→ sv->dirty == true → rebuild_tracer(view[0])
        └→ 遍历 sv->scn->shapes = scn#2->shapes = {quad}  // ← ⚠️ 不是 {quad_inst}!
        └→ 构建的 GAS 只含裸 quad，无实例变换
  └→ trace 结果：命中 tri 1 → prim_id = 1 ≠ 0
```

**view[0] 在创建时正确包含 quad_inst（带 R_x(π) 实例变换），但 `s3d_scene_clear` 标记 dirty 后，下次 trace 时被错误重建为只含裸 quad 的场景。**

### 与 CPU 参考实现对比

| 方面 | CPU (Embree) | GPU (OptiX) — 当前 bug |
|------|-------------|----------------------|
| view 创建 | `scene_view_sync` 一次性快照 shapes 到 Embree scene | `rebuild_tracer` 从 `scn->shapes` 构建 GAS |
| 场景变化影响 | 信号回调 `on_shape_detach` 仅记录到 `detached_shapes`，**不重建 BVH** | `dirty=true` → 下次 trace 时从**已变更的**场景重建 |
| clear 后旧 view trace | Embree scene 不受影响，返回原始结果 | rebuild 后返回新场景的结果 |

CPU 实现中 `s3d_scene_clear` 通过 `SIG_BROADCAST` 通知 view，view 的回调 `on_shape_detach` 仅将 shape ID 压入 `detached_shapes` 延迟列表，**不修改 Embree BVH**。

---

## 四、修复方案

### 方案 A：移除 dirty 标记（推荐，同 snapshot_semantics_fix.md Step 1）

**文件**: `ox_s3d_scene.cpp`

删除三处将已有 view 标记为 dirty 的代码：

**s3d_scene_attach_shape (L95-96)**:
```cpp
// 删除以下两行
    /* Mark all views as dirty */
    for (auto* v : scn->views) v->dirty = true;
```

**s3d_scene_detach_shape (L111-112)**:
```cpp
// 删除以下两行
    /* Mark all views as dirty */
    for (auto* v : scn->views) v->dirty = true;
```

**s3d_scene_clear (L127)**:
```cpp
// 删除以下一行
    for (auto* v : scn->views) v->dirty = true;
```

**安全性分析**：
- 首次构建由 `s3d_scene_view_create2` 直接调用 `rebuild_tracer(sv)`，不走 `ensure_built()`
- `dirty` 初始值为 `true`，但 `create2` 在 `push_back(sv)` 之后立即 `rebuild_tracer` 将其置 `false`
- 用户要看到场景变更，须销毁旧 view 并创建新 view（与 CPU 行为一致）

### 方案 B：在快照中保存 shapes 列表（更完备）

若需保留 dirty 机制给后续扩展用，可在 `rebuild_tracer` 时拷贝 `scn->shapes` 到 `sv->snapped_shapes`，dirty rebuild 时从快照而非当前场景构建。但这增加了不必要的复杂度，且与 CPU API 契约（view 为不可变快照）不符。

**推荐方案 A**。

---

## 五、预期修复效果

修复后时序：

```
s3d_scene_view_create(scn#2, ..., &view[0])
  └→ rebuild_tracer(view[0])    // scn#2->shapes = {quad_inst} → GAS 含实例变换
  └→ view[0]->dirty = false

s3d_scene_clear(scn#2)           // 不再标记 dirty

s3d_scene_attach_shape(scn#2, quad)  // 不再标记 dirty

s3d_scene_view_trace_ray(view[0], ...)
  └→ ensure_built(view[0])
     └→ sv->dirty == false → return RES_OK    // ✓ 保留原始 GAS
  └→ trace 在含 R_x(π) 实例的 GAS 上执行
  └→ 命中 tri 0' → prim_id = 0 ✓
```

### 断言验证

| 行 | 断言 | 预期 |
|----|------|------|
| L140 | `hit[0].prim.prim_id == 0` | ✓ 实例场景 R_x(π) 命中 tri 0 |
| L141 | `hit[1].prim.prim_id == 1` | ✓ 原始场景命中 tri 1（不受修改影响） |
| L142 | `hit[0].prim.geom_id == quad_id` | ✓ 底层均是 quad 的几何 |
| L143 | `hit[1].prim.geom_id == quad_id` | ✓ |
| L144 | `hit[0].prim.inst_id == quad_inst_id` | ✓ view[0] 保留了 instance 映射 |
| L145 | `hit[1].prim.inst_id == S3D_INVALID_ID` | ✓ view[1] 直接 attach 裸 quad |
| L146 | `hit[0].normal == -hit[1].normal` | ✓ R_x(π) 翻转法线 |
| L147 | `hit[0].distance == hit[1].distance` | ✓ 两者到 z=0 平面距离均为 1 |

---

## 六、连锁影响

### 同步修复的测试

此修复（移除 dirty 标记）与 `snapshot_semantics_fix.md` 的 Step 1 相同。应同时修复：

| 测试 | 失败行 | 修复机制 |
|------|--------|---------|
| test_s3d_trace_ray_instance (#19) | L140 | view[0] 不再被 dirty 重建，保留实例 GAS |
| test_s3d_scene_view (#11) | L274 | detach 不再触发 rebuild，UV 快照保持 |

### 不影响的测试

- test_s3d_sphere (#15)：依赖 `compute_volume` 快照，需 **额外** 的 `shape_snapshots` 机制（见 `fix_test15_flip_surface_snapshot.md`）
- 14 个已通过测试：无回退风险（所有都在 view 创建后立即 trace，不涉及 clear/detach 后旧 view 复用）

---

## 七、验证步骤

```powershell
cd optix-throughput-validation/build_s3d

# 构建
cmake --build . --config Release > build.log 2>&1

# 目标测试
.\bin\Release\test_s3d_trace_ray_instance.exe

# 回归：连锁受益测试
.\bin\Release\test_s3d_scene_view.exe

# 全量回归
ctest -C Release --output-on-failure
```

预期结果：
- test_s3d_trace_ray_instance：所有断言通过（L140 `prim_id == 0`）
- test_s3d_scene_view L274：UV 一致性通过（view 快照保持）
- 14 个已通过测试无回退
