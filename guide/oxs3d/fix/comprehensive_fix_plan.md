# ox_s3d 综合修复方案

**日期**: 2026-02-22  
**状态**: 待实施  
**范围**: test #11, #14, #15, #19, test_s3d_closest_point  
**涉及文件**: 4 个源文件 + 1 个 CUDA 文件  

---

## 一、修复总览

本方案整合以下 5 份独立分析文档，消除冗余和冲突，形成统一实施计划：

| 文档 | 目标测试 | 核心问题 |
|------|---------|---------|
| [snapshot_semantics_fix.md](snapshot_semantics_fix.md) | #11 scene_view, #15 sphere | dirty 标记 + 快照语义缺失 |
| [fix_test19_trace_ray_instance.md](fix_test19_trace_ray_instance.md) | #19 trace_ray_instance | dirty 重建破坏实例 GAS |
| [fix_test15_flip_surface_snapshot.md](fix_test15_flip_surface_snapshot.md) | #15 sphere | flip_surface 快照精细方案 |
| [fix_test14_shape_ref_put_child_scene.md](fix_test14_shape_ref_put_child_scene.md) | #14 shape | instance child_scene 引用泄漏 → SegFault |
| [fix_closest_point_search_radius.md](fix_closest_point_search_radius.md) | closest_point | 搜索半径覆盖不足 + radius exclusive 语义 |

### 修复间依赖关系

```
Fix A (dirty 标记移除)
  ├── 直接修复: #11, #19
  └── 前置于 Fix B (快照需在 dirty 移除后才有意义)

Fix B (快照语义)
  ├── 直接修复: #15
  └── 附带修复: hitresult_to_s3d_hit, primitives_count, get_primitive

Fix C (child_scene 引用释放)
  └── 独立修复: #14

Fix D (CP 搜索半径 + exclusive 语义)
  └── 独立修复: closest_point
```

---

## 二、文件修改汇总

| # | 文件 | 修改类型 | 来源 Fix |
|---|------|---------|---------|
| M1 | `s3d_wrapper/ox_s3d_scene.cpp` | 删除 3 处 dirty 标记 | A |
| M2 | `s3d_wrapper/ox_s3d_internal.h` | 增加 `shape_snapshot` 结构体和快照 map | B |
| M3 | `s3d_wrapper/ox_s3d_scene_view.cpp` | 快照辅助函数 + rebuild 时拍快照 + 清理 | B |
| M4 | `s3d_wrapper/ox_s3d_scene_view.cpp` | `compute_volume` 使用快照 | B |
| M5 | `s3d_wrapper/ox_s3d_scene_view.cpp` | `hitresult_to_s3d_hit` 使用快照 flip | B |
| M6 | `s3d_wrapper/ox_s3d_scene_view.cpp` | `primitives_count` / `get_primitive` 使用快照 | B |
| M7 | `s3d_wrapper/ox_s3d_scene_view.cpp` | `ref_put` 时释放快照 | B |
| M8 | `s3d_wrapper/ox_s3d_shape.cpp` | `ref_put` 释放 child_scene 引用 | C |
| M9 | `s3d_wrapper/ox_s3d_scene_view.cpp` | 单次 CP 搜索半径扩展 | D |
| M10 | `s3d_wrapper/ox_s3d_scene_view.cpp` | 批量 CP 搜索半径扩展 | D |
| M11 | `device/nn_programs.cu` | radius exclusive 语义 (`<=` → `<`) | D |

---

## 三、实施步骤

### Fix A — 移除 dirty 标记（修复 #11, #19）

#### M1: `ox_s3d_scene.cpp`

删除 3 处将已有 view 标记为 dirty 的代码。view 在 `create2` 时一次性构建，后续场景修改不影响已有 view。

**s3d_scene_attach_shape** (~L96):
```diff
    scn->shapes[shape->id] = shape;
    s3d_shape_ref_get(shape);

-    /* Mark all views as dirty */
-    for (auto* v : scn->views) v->dirty = true;
-
    return RES_OK;
```

**s3d_scene_detach_shape** (~L111-112):
```diff
    scn->shapes.erase(it);

-    /* Mark all views as dirty */
-    for (auto* v : scn->views) v->dirty = true;
-
    s3d_shape_ref_put(shape);
```

**s3d_scene_clear** (~L127):
```diff
    scn->shapes.clear();

-    for (auto* v : scn->views) v->dirty = true;
-
    return RES_OK;
```

**验证**: 首次构建由 `s3d_scene_view_create2()` 直接调用 `rebuild_tracer()`，不走 `ensure_built()`。用户要看到场景变更，须销毁旧 view 并创建新 view——与 CPU Embree 参考实现行为一致。

---

### Fix B — 快照语义（修复 #15，附带修复 hitresult/primitives）

> **注**: 本方案采用 `fix_test15_flip_surface_snapshot.md` 的精细设计（含 shape 指针的 `shape_snapshot` 结构体），而非 `snapshot_semantics_fix.md` 中多 map 方案。两者意图相同，前者更精简。

#### M2: `ox_s3d_internal.h` — 增加快照类型

在 `s3d_scene_view` 结构体内，`geom_to_inst` 之后添加：

```cpp
    /* Instance tracking: tracer geom_id → instance shape (for flattened instances) */
    std::map<unsigned int, s3d_shape*>   geom_to_inst;  /* tracer geom_id → instance shape */

    /* ---- Build-time attribute snapshots ---- */
    struct shape_snapshot {
        s3d_shape* shape;       /* shape pointer (geometry data is immutable after creation) */
        bool       flip_surface;
        bool       enabled;
    };
    std::map<unsigned int, shape_snapshot> shape_snapshots;  /* shape_id → build-time state */

    /* Instance children snapshots: inst_shape_id → { child_shape_id → snapshot } */
    std::map<unsigned int, std::map<unsigned int, shape_snapshot>> inst_child_snapshots;
```

**设计决策**:
- `std::map` 与现有 `shape_to_geom` / `geom_to_shape` 风格一致
- 存储 `shape*` 指针以便 `compute_volume` / `primitives_count` 读取不可变几何数据
- 不为快照增加引用计数——shape 生命周期由 scene 管理，且当前无"销毁 shape 后查询旧 view"的场景
- 同时快照 `is_enabled`，与 cus3d 行为一致

#### M3: `ox_s3d_scene_view.cpp` — 快照写入与清理

**3a** — 在 `rebuild_tracer()` 开头的清理段，增加快照清理：

```cpp
    /* Clear previous geometry */
    sv->tracer.clearAllGeometry();
    sv->shape_to_geom.clear();
    sv->geom_to_shape.clear();
    sv->geom_to_inst.clear();
    sv->shape_snapshots.clear();          // ← 新增
    sv->inst_child_snapshots.clear();     // ← 新增
```

**3b** — 在遍历 shapes 的循环中，每个 shape 处理完映射后，追加快照写入：

对于**非 instance** shape（`sv->shape_to_geom[shape->id] = tracer_gid;` 之后）：

```cpp
        sv->shape_to_geom[shape->id] = tracer_gid;
        sv->geom_to_shape[tracer_gid] = shape->id;
        shape->tracer_geom_id = tracer_gid;

        /* Snapshot attributes at build time */
        sv->shape_snapshots[shape->id] = { shape, shape->flip_surface, shape->enabled };
```

对于 **instance** shape（instance 循环内，`cs->tracer_geom_id = gid;` 之后）：

```cpp
                    if (gid != S3D_INVALID_ID) {
                        sv->shape_to_geom[cs->id]  = gid;
                        sv->geom_to_shape[gid]      = cs->id;
                        sv->geom_to_inst[gid]        = shape; /* track the instance */
                        cs->tracer_geom_id           = gid;

                        /* Snapshot child shape attributes */
                        sv->inst_child_snapshots[shape->id][cs->id] = { cs, cs->flip_surface, cs->enabled };
                    }
```

同时在 instance 遍历外补充 instance shape 本身的快照（`continue;` 之前）：

```cpp
            /* Snapshot the instance shape itself */
            sv->shape_snapshots[shape->id] = { shape, shape->flip_surface, shape->enabled };
            continue;
```

#### M4: `ox_s3d_scene_view.cpp` — `compute_volume` 使用快照

替换整个 `s3d_scene_view_compute_volume` 函数体：

```cpp
res_T s3d_scene_view_compute_volume(s3d_scene_view* sv, float* volume) {
    if (!sv || !volume) return RES_BAD_ARG;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    float mesh_vol = 0.0f;
    float sphere_vol = 0.0f;

    for (auto& kv : sv->shape_snapshots) {                  // ← 快照
        const auto& snap = kv.second;
        s3d_shape* shape = snap.shape;
        if (!snap.enabled) continue;                         // ← 快照 enabled

        if (shape->type == OX_SHAPE_MESH) {
            float shape_vol_local = 0.0f;
            for (unsigned t = 0; t < shape->ntris; t++) {
                unsigned i0 = shape->indices[t*3+0];
                unsigned i1 = shape->indices[t*3+1];
                unsigned i2 = shape->indices[t*3+2];
                float v0x = shape->positions[i0*3+0], v0y = shape->positions[i0*3+1], v0z = shape->positions[i0*3+2];
                float v1x = shape->positions[i1*3+0], v1y = shape->positions[i1*3+1], v1z = shape->positions[i1*3+2];
                float v2x = shape->positions[i2*3+0], v2y = shape->positions[i2*3+1], v2z = shape->positions[i2*3+2];
                float cx = v1y * v2z - v1z * v2y;
                float cy = v1z * v2x - v1x * v2z;
                float cz = v1x * v2y - v1y * v2x;
                shape_vol_local += v0x * cx + v0y * cy + v0z * cz;
            }
            if (snap.flip_surface)                           // ← 快照 flip
                shape_vol_local = -shape_vol_local;
            mesh_vol += shape_vol_local;
        }
        else if (shape->type == OX_SHAPE_SPHERE) {
            if (shape->sphere_radius <= 0.0f) continue;
            float r = shape->sphere_radius;
            float sv_val = (4.0f / 3.0f) * 3.14159265358979323846f * r * r * r;
            if (snap.flip_surface)                           // ← 快照 flip
                sv_val = -sv_val;
            sphere_vol += sv_val;
        }
    }

    *volume = mesh_vol / 6.0f + sphere_vol;
    return RES_OK;
}
```

#### M5: `ox_s3d_internal.h` — `hitresult_to_s3d_hit` 使用快照 flip

修改 `ox_s3d_util` 命名空间中的 `hitresult_to_s3d_hit` 签名，增加 `sv` 参数：

```cpp
static inline void hitresult_to_s3d_hit(
    const s3d_scene_view* sv,       // ← 新增
    const HitResult& hr,
    s3d_shape* shape,
    unsigned int shape_id,
    unsigned int local_prim_id,
    struct s3d_hit* out,
    s3d_shape* inst = nullptr)
```

在 sphere 法线翻转处使用快照：

```cpp
        /* For UV computation, we need the OUTWARD normal (un-flipped) */
        float ox = nx, oy = ny, oz = nz;
        bool flip = shape->flip_surface;
        if (sv) {
            auto it = sv->shape_snapshots.find(shape->id);
            if (it != sv->shape_snapshots.end()) flip = it->second.flip_surface;
        }
        if (flip) { ox = -ox; oy = -oy; oz = -oz; }
```

更新 `ox_s3d_scene_view.cpp` 中所有调用 `hitresult_to_s3d_hit` 的地方，第一个参数传入 `sv`。

#### M6: `ox_s3d_scene_view.cpp` — `primitives_count` 和 `get_primitive` 使用快照

**`s3d_scene_view_primitives_count`**:

```cpp
    size_t total = 0;
    for (auto& kv : sv->shape_snapshots) {              // ← 快照
        const auto& snap = kv.second;
        s3d_shape* shape = snap.shape;
        if (!snap.enabled) continue;                     // ← 快照 enabled
        if (shape->type == OX_SHAPE_MESH) total += shape->ntris;
        else if (shape->type == OX_SHAPE_SPHERE) total += 1;
        else if (shape->type == OX_SHAPE_INSTANCE) {
            auto iit = sv->inst_child_snapshots.find(shape->id);
            if (iit != sv->inst_child_snapshots.end()) {
                for (auto& ckv : iit->second) {
                    const auto& csnap = ckv.second;
                    if (!csnap.enabled) continue;
                    if (csnap.shape->type == OX_SHAPE_MESH) total += csnap.shape->ntris;
                    else if (csnap.shape->type == OX_SHAPE_SPHERE) total += 1;
                }
            }
        }
    }
```

**`s3d_scene_view_get_primitive`**: 同理替换数据源。

#### M7: `ox_s3d_scene_view.cpp` — view 销毁时清理快照

在 `s3d_scene_view_ref_put` 的引用归零代码路径，`delete sv` 之前：

```cpp
    if (--sv->ref == 0) {
        /* Unregister from scene */
        if (sv->scn) {
            auto& v = sv->scn->views;
            v.erase(std::remove(v.begin(), v.end(), sv), v.end());
            s3d_scene_ref_put(sv->scn);
        }
        sv->tracer.cleanup();
        sv->shape_snapshots.clear();              // ← 新增
        sv->inst_child_snapshots.clear();         // ← 新增
        delete sv;
    }
```

---

### Fix C — Instance child_scene 引用释放（修复 #14 SegFault）

#### M8: `ox_s3d_shape.cpp` — `s3d_shape_ref_put`

```diff
 res_T s3d_shape_ref_put(s3d_shape* shape) {
     if (!shape) return RES_BAD_ARG;
     if (shape->ref == 0) return RES_BAD_ARG;
     if (--shape->ref == 0) {
+        /* Release child_scene reference for instance shapes */
+        if (shape->type == OX_SHAPE_INSTANCE && shape->child_scene) {
+            s3d_scene_ref_put(shape->child_scene);
+            shape->child_scene = nullptr;
+        }
         delete shape;
     }
     return RES_OK;
 }
```

**引用链修复效果**:
- 修复前: `ref_put(inst)` → `delete inst`，child_scene 引用泄漏 → scene 不销毁 → device 不销毁 → OptiX atexit SegFault
- 修复后: `ref_put(inst)` → `scene_ref_put(child_scene)` → scene 引用正确递减 → 最终正常销毁

**头文件依赖**: `ox_s3d_shape.cpp` 已 `#include "ox_s3d_internal.h"`，`s3d_scene_ref_put` 在 `s3d.h` 中声明为 `extern "C"`，无需新增 include。

---

### Fix D — CP 搜索半径扩展 + exclusive 语义（修复 closest_point）

#### M9: `ox_s3d_scene_view.cpp` — 单次 CP 搜索半径扩展

在 `s3d_scene_view_closest_point` 函数中，`if (!sv->query_mesh_set) return RES_OK;` 之后、`CPQuery q;` 之前：

```cpp
    if (!sv->query_mesh_set) return RES_OK;

    /* Expand search radius if the requested radius exceeds current coverage.
     * This ensures queries with radius=INF (or very large) can reach all
     * primitives regardless of their distance from the query point. */
    {
        float eff = radius;
        if (!std::isfinite(eff) || eff > 1e30f)
            eff = 1e30f;
        if (eff > sv->search_radius) {
            sv->search_radius = eff;
            sv->tracer.setSearchRadius(sv->search_radius);
            sv->tracer.rebuildQueryGAS(/*compact=*/true);
        }
    }

    CPQuery q;
```

#### M10: `ox_s3d_scene_view.cpp` — 批量 CP 搜索半径扩展

在 `batch_cp_impl` 函数中，query buffer 构建循环内追踪最大 radius，循环后按需重建：

```cpp
    /* Build query buffer and find maximum requested radius */
    std::vector<CPQuery> queries(nqueries);
    float max_radius = 0.0f;
    for (size_t i = 0; i < nqueries; i++) {
        queries[i].position = make_float3(requests[i].pos[0],
                                           requests[i].pos[1],
                                           requests[i].pos[2]);
        queries[i].radius   = requests[i].radius;
        float r = requests[i].radius;
        if (!std::isfinite(r) || r > 1e30f) r = 1e30f;
        if (r > max_radius) max_radius = r;
    }

    /* Expand search radius if any query exceeds current coverage */
    if (max_radius > sv->search_radius) {
        sv->search_radius = max_radius;
        sv->tracer.setSearchRadius(sv->search_radius);
        sv->tracer.rebuildQueryGAS(/*compact=*/true);
    }

    std::vector<CPResult> results = sv->tracer.closestPointBatch(queries);
```

#### M11: `device/nn_programs.cu` — radius exclusive 语义

```diff
-        if (dist <= q.radius) {
+        if (dist < q.radius) {
```

CPU 参考实现中 `radius` 是 exclusive upper bound：`distance == radius` 时不算命中。

---

## 四、设计决策汇总

| 决策 | 选择 | 理由 |
|------|------|------|
| dirty 标记 | **移除** | CPU 参考实现不使用 dirty flag，view 为不可变快照 |
| 快照数据结构 | `struct shape_snapshot` + `std::map` | 聚合 `shape*` + `flip` + `enabled`，精简且可扩展 |
| 快照引用计数 | **不增加** | shape 生命周期由 scene 管理，无循环引用风险 |
| instance 子场景快照 | 独立 map `inst_child_snapshots` | 与快照写入路径匹配，避免交叉引用 |
| INF 映射值 (CP) | `1e30f` | `FLT_MAX` AABB 扩展会在 CUDA 中溢出，`1e30` 足够 |
| search_radius 策略 | 单调递增 | 首次大 radius 触发重建，后续复用 |
| radius 语义 | exclusive (`<`) | 与 CPU Embree 行为一致 |
| hitresult 参数扩展 | 增加 `sv` 参数 | 所有调用点在 `ox_s3d_scene_view.cpp` 中，改动局部 |

---

## 五、实施顺序

```
阶段 1: Fix A (dirty 标记移除)     — 1 个文件，3 处删除
   ↓     验证: test #11, #19
阶段 2: Fix C (child_scene 释放)   — 1 个文件，1 处修改
   ↓     验证: test #14
阶段 3: Fix B (快照语义)           — 2 个文件，6 处修改
   ↓     验证: test #15，全量回归
阶段 4: Fix D (CP 半径 + exclusive) — 2 个文件，3 处修改
         验证: test_s3d_closest_point, test_s3d_batch_closest_point
```

> Fix A 和 Fix C 互相独立，可并行实施。Fix B 依赖 Fix A（dirty 移除后快照才有意义）。Fix D 完全独立。

---

## 六、预期修复效果

| 测试 | 失败行 | 根因 | 修复阶段 |
|------|--------|------|---------|
| #11 test_s3d_scene_view | L274 | dirty rebuild 使 detach 后旧 view 被重建 | A |
| #14 test_s3d_shape | SegFault | instance 析构未释放 child_scene 引用 | C |
| #15 test_s3d_sphere | L104 | `compute_volume` 实时读 `flip_surface` | B |
| #19 test_s3d_trace_ray_instance | L140 | dirty rebuild 将实例 GAS 替换为裸 quad | A |
| test_s3d_closest_point | L894 | 搜索半径不足（`diag×2 < 10×AABB`） | D |
| test_s3d_closest_point | L915 | radius inclusive 语义错误 | D |
| test_s3d_closest_point | L1013 | 大 amplitude miss | D |

---

## 七、性能影响

| 修改 | 影响 |
|------|------|
| dirty 标记移除 | 无性能影响，减少不必要的重建反而提升性能 |
| 快照写入 (rebuild 时) | 可忽略——仅在 rebuild 时多几次 map insert |
| 快照读取 (query 时) | `std::map` 查找 O(log N)，N 为 shape 数，通常 <100 |
| CP search_radius 扩展 | 首次 `radius=INF` 触发一次 GAS 重建 (~1-10 ms)；后续无重建 |
| CP 大 radius BVH 退化 | `search_radius=1e30` 时 AABB 完全重叠，等价于线性扫描（CPU Embree 在 INF 时同样如此） |

---

## 八、回归测试

### 基础验证

```powershell
cd D:\Works\Projects\Stardis-GPU\optix-throughput-validation\build_s3d

# 构建全部
cmake --build . --config Release > build.log 2>&1
Select-String "error" build.log

# 全量回归
ctest -C Release --output-on-failure
```

### 分阶段验证

```powershell
# 阶段 1: Fix A
.\bin\Release\test_s3d_scene_view.exe    # #11 L274
.\bin\Release\test_s3d_trace_ray_instance.exe  # #19 L140

# 阶段 2: Fix C
.\bin\Release\test_s3d_shape.exe         # #14 无 SegFault

# 阶段 3: Fix B
.\bin\Release\test_s3d_sphere.exe        # #15 L104

# 阶段 4: Fix D
.\bin\Release\test_s3d_closest_point.exe         # L894, L915, L1013
.\bin\Release\test_s3d_batch_closest_point.exe   # 回归
```

### 已通过测试无回退确认

14 个已通过测试均在 view 创建后立即查询，不涉及 clear/detach 后旧 view 复用，无回退风险。重点关注：

- test_s3d_sphere_instance (#17) — 涉及 instance 生命周期
- test_s3d_scene (#10) — 涉及 scene 创建/销毁

---

## 九、后续考虑

1. **快照引用计数强化**: 当前 `shape_snapshots` 不持有引用计数。若后续出现"销毁 shape 后查询旧 view"的场景，需在 `take_snapshot` 时 `ref_get`，在 `release_snapshot` 时 `ref_put`。
2. **`build_cdf` 快照**: `compute_area` 通过 `build_cdf()` 计算面积。area 不受 `flip_surface` 影响且当前无测试失败，可作后续改进。
3. **CP 大场景性能**: 若 `radius=INF` 的大场景性能不可接受，可考虑双 GAS 策略或 cuBQL 回退（详见 `fix_closest_point_search_radius.md` 第七节）。
4. **`resolve_shape` 快照**: `ox_s3d_util::resolve_shape` 当前从 `sv->scn->shapes` 查找 shape 指针，应改为从 `sv->shape_snapshots` 查找以完全一致。当前测试不触发此路径的问题，列为低优先级。
