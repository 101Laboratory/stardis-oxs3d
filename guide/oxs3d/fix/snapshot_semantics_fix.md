# scene_view 快照语义修复方案

**目标测试**: test_s3d_scene_view (#11, L274), test_s3d_sphere (#15, L104)  
**涉及文件**: `ox_s3d_scene.cpp`, `ox_s3d_scene_view.cpp`, `ox_s3d_internal.h`  
**日期**: 2026-02-22

---

## 根因

`scene_view` 缺少快照语义。场景修改操作（`attach`/`detach`/`clear`）将所有已有 view 标记为 `dirty=true`，导致下次查询时 `ensure_built()` → `rebuild_tracer()` 用**场景当前状态**重建 tracer，已有 view 的内容被篡改。

此外 `compute_volume`/`primitives_count`/`get_primitive` 直接从 `sv->scn->shapes` 实时读取数据，同样违反快照语义（`flip_surface` 修改立即可见，而非等 view 重建）。

### test #11 失败链

1. `s3d_scene_detach_shape(scn2, plane)` → 标记 `scnview2->dirty = true`
2. `s3d_scene_view_trace_ray(scnview2, ...)` → `ensure_built()` 检测 dirty → `rebuild_tracer()` 从 `scn2->shapes`（plane 已移除）重建
3. 射线原本命中 plane(z=0.5)，重建后命中 cube 背面(z=1.0) → UV 不同 → L274 失败

### test #15 失败链

1. `s3d_shape_flip_surface(sphere0)` 修改 `shape->flip_surface`
2. `s3d_scene_view_compute_volume(view, ...)` 直接读取 `shape->flip_surface` 当前值
3. volume 已变为负值，但测试期望 view 快照中仍为正值 → L104 失败

### CPU 参考实现对比

CPU 版 (`stardis-cpu/star-3d/0.10/src/s3d_scene_view.c`) 不使用 dirty flag，通过信号回调 + 延迟删除实现快照：
- detach 时若 view 正在使用（`mask != 0`），仅将 shape ID 存入 `detached_shapes` 数组
- view 引用归零释放时才处理 `detached_shapes`
- 几何缓存由 Embree 管理，detach 不影响已构建的 BVH

---

## 修复步骤

### Step 1 — 移除场景修改时的 dirty 标记

**文件**: `ox_s3d_scene.cpp`

删除三处 `dirty = true` 设置：

```cpp
// === s3d_scene_attach_shape (L96) — 删除 ===
for (auto* v : scn->views) v->dirty = true;

// === s3d_scene_detach_shape (L112) — 删除 ===
for (auto* v : scn->views) v->dirty = true;

// === s3d_scene_clear (L127) — 删除 ===
for (auto* v : scn->views) v->dirty = true;
```

**安全性**: 首次构建由 `s3d_scene_view_create2()` 直接调用 `rebuild_tracer()`，不经过 `ensure_built()`，不依赖 dirty 初始值。用户要看到场景变更，须销毁旧 view 并创建新 view。

---

### Step 2 — 在 `s3d_scene_view` 结构体中增加快照字段

**文件**: `ox_s3d_internal.h`，`s3d_scene_view` 结构体

在 `geom_to_inst` 之后添加：

```cpp
/* ---- Snapshot (filled at build time) ---- */
std::map<unsigned int, s3d_shape*> snapped_shapes;   /* build 时 shapes 快照 (持有引用) */
std::map<unsigned int, bool>       snapped_flip;     /* build 时 flip_surface 快照 */

/* Instance children snapshots: inst_shape_id → {child_shape_id → child_shape*} */
std::map<unsigned int, std::map<unsigned int, s3d_shape*>> snapped_inst_children;
std::map<unsigned int, std::map<unsigned int, bool>>       snapped_inst_flip;
```

**设计决策**: 只保存 `shape*` 引用（增引用计数防止释放）+ `flip_surface` 值。mesh 数据（positions/indices）创建后不可变，无需深拷贝。

---

### Step 3 — 快照辅助函数

**文件**: `ox_s3d_scene_view.cpp`

在 `rebuild_tracer()` 前添加辅助函数：

```cpp
/* 释放快照中持有的所有 shape 引用 */
static void release_snapshot(s3d_scene_view* sv) {
    for (auto& kv : sv->snapped_shapes)
        s3d_shape_ref_put(kv.second);
    sv->snapped_shapes.clear();
    sv->snapped_flip.clear();

    for (auto& ikv : sv->snapped_inst_children) {
        for (auto& ckv : ikv.second)
            s3d_shape_ref_put(ckv.second);
    }
    sv->snapped_inst_children.clear();
    sv->snapped_inst_flip.clear();
}

/* 在 rebuild 时拍摄场景快照 */
static void take_snapshot(s3d_scene_view* sv) {
    release_snapshot(sv);

    for (auto& kv : sv->scn->shapes) {
        s3d_shape* shape = kv.second;
        if (!shape->enabled) continue;

        s3d_shape_ref_get(shape);
        sv->snapped_shapes[shape->id] = shape;
        sv->snapped_flip[shape->id]   = shape->flip_surface;

        if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
            auto& child_map = sv->snapped_inst_children[shape->id];
            auto& flip_map  = sv->snapped_inst_flip[shape->id];
            for (auto& ckv : shape->child_scene->shapes) {
                s3d_shape* cs = ckv.second;
                if (!cs->enabled) continue;
                s3d_shape_ref_get(cs);
                child_map[cs->id] = cs;
                flip_map[cs->id]  = cs->flip_surface;
            }
        }
    }
}
```

---

### Step 4 — 在 `rebuild_tracer()` 中调用快照

**文件**: `ox_s3d_scene_view.cpp`，`rebuild_tracer()` 函数

在 `sv->tracer.clearAllGeometry();` 之前插入：

```cpp
take_snapshot(sv);
```

---

### Step 5 — `compute_volume` 使用快照数据

**文件**: `ox_s3d_scene_view.cpp`，`s3d_scene_view_compute_volume`

替换所有 `sv->scn->shapes` → `sv->snapped_shapes`，`shape->flip_surface` → `sv->snapped_flip[shape->id]`：

```cpp
res_T s3d_scene_view_compute_volume(s3d_scene_view* sv, float* volume) {
    if (!sv || !volume) return RES_BAD_ARG;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    float mesh_vol = 0.0f;
    for (auto& kv : sv->snapped_shapes) {          // ← 快照
        s3d_shape* shape = kv.second;
        if (!shape->enabled) continue;

        if (shape->type == OX_SHAPE_MESH) {
            float shape_vol = 0.0f;
            for (unsigned t = 0; t < shape->ntris; t++) {
                /* ... 计算代码不变 ... */
            }
            if (sv->snapped_flip[shape->id])        // ← 快照 flip
                shape_vol = -shape_vol;
            mesh_vol += shape_vol;
        }
    }

    float sphere_vol = 0.0f;
    for (auto& kv : sv->snapped_shapes) {           // ← 快照
        s3d_shape* shape = kv.second;
        if (!shape->enabled || shape->type != OX_SHAPE_SPHERE) continue;
        if (shape->sphere_radius <= 0.0f) continue;
        float r = shape->sphere_radius;
        float sv_val = (4.0f / 3.0f) * 3.14159265358979323846f * r * r * r;
        if (sv->snapped_flip[shape->id])             // ← 快照 flip
            sv_val = -sv_val;
        sphere_vol += sv_val;
    }

    *volume = mesh_vol / 6.0f + sphere_vol;
    return RES_OK;
}
```

**效果**: 直接修复 test #15 — `flip_surface` 修改后已有 view 使用 build 时 flip 值，volume 不变。

---

### Step 6 — `primitives_count` 和 `get_primitive` 使用快照数据

**文件**: `ox_s3d_scene_view.cpp`

将两个函数中所有 `sv->scn->shapes` 替换为 `sv->snapped_shapes`。instance 子场景改为从 `sv->snapped_inst_children[shape->id]` 读取。

```cpp
// primitives_count
for (auto& kv : sv->snapped_shapes) {               // ← 快照
    s3d_shape* shape = kv.second;
    // ...
    if (shape->type == OX_SHAPE_INSTANCE) {
        auto it = sv->snapped_inst_children.find(shape->id);
        if (it != sv->snapped_inst_children.end()) {
            for (auto& ckv : it->second) {           // ← 快照子场景
                // ...
            }
        }
    }
}

// get_primitive — 同理
```

---

### Step 7 — view 销毁时释放快照

**文件**: `ox_s3d_scene_view.cpp`，view 的 release/析构函数

在 `s3d_scene_view_ref_put` 的引用归零代码路径中添加：

```cpp
release_snapshot(sv);
```

确保在 `delete sv` 之前调用。

---

### Step 8（可选）— `build_cdf` 使用快照

`compute_area` 通过 `build_cdf()` 计算面积。如需完全快照一致性，`build_cdf` 内部的 `sv->scn->shapes` 也应改为 `sv->snapped_shapes`。但 area 不受 `flip_surface` 影响且当前无测试失败，可作后续改进。

---

## 影响分析

### 直接修复的测试

| 测试 | 失败行 | 修复机制 |
|------|--------|---------|
| test_s3d_scene_view (#11) | L274 | Step 1: detach 不再触发 rebuild，view 保持旧 tracer |
| test_s3d_sphere (#15) | L104 | Step 5: compute_volume 使用快照 flip_surface |

### 回归风险

| 场景 | 风险 | 说明 |
|------|------|------|
| 首次构建 | 无 | `create2()` 直接调 `rebuild_tracer()`，不走 `ensure_built()` |
| attach 后查询 | 无 | 所有测试在 attach 后要么创建新 view，要么期望旧 view 不变 |
| clear 后查询 | 无 | 测试期望 view 保持旧状态（快照语义） |
| enable/disable | 无 | 当前已不设 dirty，行为不变 |

### 不受影响的测试

- `get_aabb`: 已在 `rebuild_tracer()` 中缓存到 `sv->lower/upper`，不从 scene 实时读取 ✓
- `compute_area`: `build_cdf` 在首次调用时缓存到 `sv->total_area`，后续直接返回 ✓
- 所有 14 个已通过测试：无回退风险

---

## 验证

```bash
cd optix-throughput-validation/build_s3d
cmake --build . --config Release > build.log 2>&1

# 目标测试
ctest -C Release -R "test_s3d_scene_view|test_s3d_sphere" --output-on-failure

# 全量回归
ctest -C Release --output-on-failure
```

预期：
- test_s3d_scene_view L274 通过
- test_s3d_sphere L104 通过
- 14 个已通过测试不回退
