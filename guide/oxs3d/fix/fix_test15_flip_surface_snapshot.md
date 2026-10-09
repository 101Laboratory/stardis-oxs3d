# fix: test_s3d_sphere (#15) — flip_surface 快照语义修复

**目标测试**: test_s3d_sphere L104  
**涉及文件**: `ox_s3d_internal.h`, `ox_s3d_scene_view.cpp`  
**日期**: 2026-02-22  
**前置分析**: 对比 stardis-cus3d 三层快照架构，提取适用于 ox_s3d 的轻量方案

---

## 一、cus3d vs ox_s3d 快照架构对比

### cus3d 三层快照链路

```
shape->flip_surface  ──build──►  geometry->flip_surface  (Host htable 快照)
                                       │
                                       ▼
                                 geom_entry->flip_surface  (Host geom_store 扁平数组)
                                       │
                                       ▼ cudaMemcpyAsync
                                 geom_gpu_entry.flip_surface  (GPU Device 端)
```

| 层级 | 结构体 | 文件 | 用途 |
|------|--------|------|------|
| L1 Host 缓存 | `geometry.flip_surface` | `s3d_geometry.h:35` | `compute_volume` 读取 |
| L2 Host 扁平 | `geom_entry.flip_surface` | `cus3d_geom_store.h:55` | GPU 上传前暂存 |
| L3 GPU | `geom_gpu_entry.flip_surface` | `cus3d_types.h:73` | GPU 内核读取 |

**快照写入点**: `scene_view_sync` → `scene_view_register_*_cu`

```c
// s3d_scene_view.cpp:379 (mesh)
geom->flip_surface = shape->flip_surface;

// s3d_scene_view.cpp:444 (sphere)
geom->flip_surface = shape->flip_surface;
```

**快照读取点**: `scene_view_compute_volume`

```c
// s3d_scene_view.cpp:700-730
const char flip = geom->flip_surface ^ flip_surface;  // ← geometry 快照值
```

### ox_s3d 当前状态（BUG）

```
shape->flip_surface  ──build──►  tracer 内部 (GPU 光追法线用)
         ↑
         └─── compute_volume 直接实时读取 ← BUG
```

- `rebuild_tracer` 传 `shape->flip_surface` 给 `addGeometryMesh/Sphere`（GPU 快照 ✓）
- `compute_volume` 直接读 `shape->flip_surface` 实时值（Host 无快照 ✗）
- `hitresult_to_s3d_hit` 法线翻转也直接读实时值（Host 无快照 ✗）

### 设计决策

cus3d 需要三层是因为它自建 BVH + 自管 GPU buffer。ox_s3d 的 GPU 数据由 OptiX tracer 内部管理，已有 GPU 快照。**仅需补齐 Host 端查询路径的一层快照**，用 `std::map` 即可，无需复制完整 `geometry` 结构。

同时快照 `is_enabled`，与 cus3d 行为一致（cus3d 的 `geometry` 和 `geom_entry` 均快照 `is_enabled`）。

---

## 二、失败根因

### 测试代码 (test_s3d_sphere.c L96-112)

```c
// L97-98: 正常体积 — PASS
CHK(s3d_scene_view_compute_volume(view, &volume) == RES_OK);
CHK(eq_epsf(volume, (float)(4.0/3.0*PI*radius*radius*radius), 1.e-6f));

// L100: flip surface — 在已有 view 上修改 shape
CHK(s3d_shape_flip_surface(sphere0) == RES_OK);

// L104: ★ FAILS ★ — view 未 rebuild，期望体积仍为正值
CHK(s3d_scene_view_compute_volume(view, &volume) == RES_OK);
CHK(eq_epsf(volume, (float)(4.0/3.0*PI*radius*radius*radius), 1.e-6f));

// L107-109: rebuild view，flip 生效，体积为负
CHK(s3d_scene_view_ref_put(view) == RES_OK);
CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
CHK(s3d_scene_view_compute_volume(view, &volume) == RES_OK);
CHK(eq_epsf(volume, (float)(-4.0/3.0*PI*radius*radius*radius), 1.e-6f));
```

### 因果链

1. `s3d_shape_flip_surface(sphere0)` → `shape->flip_surface = true`
2. `compute_volume` → 遍历 `sv->scn->shapes` → 读 `shape->flip_surface`（已为 true）
3. `sv_val = -sv_val` → volume 变负 → L104 断言失败

---

## 三、实现方案

### Step 1 — 数据结构定义

**文件**: `ox_s3d_internal.h`  
**位置**: `s3d_scene_view` 结构体内，`geom_to_inst` 之后

```cpp
// ---- 现有代码 ----
std::map<unsigned int, s3d_shape*>   geom_to_inst;

// ---- 新增: Build 时属性快照 ----
struct shape_snapshot {
    bool flip_surface;
    bool enabled;
};
std::map<unsigned int, shape_snapshot> shape_snapshots;  // shape_id → build 时状态
```

**选型理由**：
- `std::map` 而非 `std::unordered_map`：与现有 `shape_to_geom` / `geom_to_shape` 风格一致
- 结构体而非独立 map：便于后续扩展更多快照属性（如 `filter_func`）
- 不持有 `shape*` 引用：`compute_volume` 仍需读 shape 的顶点数据（创建后不可变），通过 `snapped_shapes` 映射获取指针比增减引用计数更简单安全

### Step 2 — 快照写入

**文件**: `ox_s3d_scene_view.cpp`  
**位置**: `rebuild_tracer()` 函数，遍历 `sv->scn->shapes` 的循环内

在现有的 `sv->shape_to_geom[shape->id] = tracer_gid;` 之后添加：

```cpp
// ---- 现有代码 ----
sv->shape_to_geom[shape->id] = tracer_gid;
sv->geom_to_shape[tracer_gid] = shape->id;

// ---- 新增: 快照属性 ----
sv->shape_snapshots[shape->id] = { shape->flip_surface, shape->enabled };
```

### Step 3 — 快照清理

**文件**: `ox_s3d_scene_view.cpp`  
**位置 A**: `rebuild_tracer()` 开头的清理段

```cpp
// ---- 现有代码 ----
sv->shape_to_geom.clear();
sv->geom_to_shape.clear();
sv->geom_to_inst.clear();

// ---- 新增 ----
sv->shape_snapshots.clear();
```

**位置 B**: `s3d_scene_view_ref_put()` 的引用归零销毁路径，`delete sv` 之前

```cpp
// ---- 新增 ----
sv->shape_snapshots.clear();
// ---- 现有代码 ----
delete sv;
```

### Step 4 — `compute_volume` 使用快照

**文件**: `ox_s3d_scene_view.cpp`  
**函数**: `s3d_scene_view_compute_volume`

**改动 4a** — 遍历源从 `sv->scn->shapes` 改为 `sv->snapped_shapes`：

不。`shape_snapshots` 只存 flip/enabled，不存 shape 指针。应保持遍历 `sv->scn->shapes`（顶点数据需要从 shape 读取），但将 `enabled` 和 `flip_surface` 的判断改为从快照读取。

但有一个问题：如果 shape 已从 scene detach，`sv->scn->shapes` 中就找不到了。需要在 `shape_snapshots` 中同时保存 shape 指针。

**修订：Step 1 修改为**

```cpp
struct shape_snapshot {
    s3d_shape* shape;     // build 时 shape 指针（几何数据不可变，生命周期由 scene 管理）
    bool flip_surface;
    bool enabled;
};
std::map<unsigned int, shape_snapshot> shape_snapshots;
```

**改动 4b** — `compute_volume` 完整替换：

```cpp
res_T s3d_scene_view_compute_volume(s3d_scene_view* sv, float* volume) {
    if (!sv || !volume) return RES_BAD_ARG;
    res_T rc = ensure_built(sv);
    if (rc != RES_OK) return rc;

    float mesh_vol = 0.0f;
    float sphere_vol = 0.0f;

    for (auto& kv : sv->shape_snapshots) {
        const auto& snap = kv.second;
        s3d_shape* shape = snap.shape;
        if (!snap.enabled) continue;                    // ← 快照 enabled

        if (shape->type == OX_SHAPE_MESH) {
            float shape_vol_local = 0.0f;
            for (unsigned t = 0; t < shape->ntris; t++) {
                unsigned i0 = shape->indices[t * 3 + 0];
                unsigned i1 = shape->indices[t * 3 + 1];
                unsigned i2 = shape->indices[t * 3 + 2];
                float x0 = shape->positions[i0 * 3 + 0];
                float y0 = shape->positions[i0 * 3 + 1];
                float z0 = shape->positions[i0 * 3 + 2];
                float x1 = shape->positions[i1 * 3 + 0];
                float y1 = shape->positions[i1 * 3 + 1];
                float z1 = shape->positions[i1 * 3 + 2];
                float x2 = shape->positions[i2 * 3 + 0];
                float y2 = shape->positions[i2 * 3 + 1];
                float z2 = shape->positions[i2 * 3 + 2];
                shape_vol_local +=
                    x0 * (y1 * z2 - y2 * z1)
                  + x1 * (y2 * z0 - y0 * z2)
                  + x2 * (y0 * z1 - y1 * z0);
            }
            if (snap.flip_surface)                      // ← 快照 flip
                shape_vol_local = -shape_vol_local;
            mesh_vol += shape_vol_local;
        }
        else if (shape->type == OX_SHAPE_SPHERE) {
            if (shape->sphere_radius <= 0.0f) continue;
            float r = shape->sphere_radius;
            float sv_val = (4.0f / 3.0f) * 3.14159265358979323846f * r * r * r;
            if (snap.flip_surface)                      // ← 快照 flip
                sv_val = -sv_val;
            sphere_vol += sv_val;
        }
    }

    *volume = mesh_vol / 6.0f + sphere_vol;
    return RES_OK;
}
```

### Step 5 — `hitresult_to_s3d_hit` 使用快照（附带修复）

**文件**: `ox_s3d_internal.h`  
**函数**: `hitresult_to_s3d_hit` 约 L330

当前代码：
```cpp
if (shape->flip_surface) { ox = -ox; oy = -oy; oz = -oz; }
```

需改为从 `scene_view->shape_snapshots` 查找。此函数当前签名可能不含 `scene_view` 参数 — 检查签名：

- 若已有 `sv` 参数：直接查 `sv->shape_snapshots[shape->id].flip_surface`
- 若无 `sv` 参数：需增加参数，或将 flip 信息编码到 OptiX hit record 中由 tracer 返回

**实现选择**：增加 `sv` 参数最简单，因为所有调用点都在 `ox_s3d_scene_view.cpp` 中，上下文已有 `sv`。

```cpp
// 修改签名
static void hitresult_to_s3d_hit(
    const s3d_scene_view* sv,   // ← 新增
    const HitResult& hr,
    s3d_hit* out)
{
    // ...
    auto snap_it = sv->shape_snapshots.find(shape->id);
    bool flip = (snap_it != sv->shape_snapshots.end()) ? snap_it->second.flip_surface : shape->flip_surface;
    if (flip) { ox = -ox; oy = -oy; oz = -oz; }
    // ...
}
```

更新所有调用点（`trace_ray`, `closest_point`, `find_enclosure` 等）传入 `sv`。

---

## 四、与 snapshot_semantics_fix.md Step 1 的关系

`snapshot_semantics_fix.md` 的 Step 1 移除了 `dirty` 标记（修复 test #11）。本文档的 Step 2-5 与其 Step 2-7 **互补但不冲突**：

| snapshot_semantics_fix.md | 本文档 | 关系 |
|--------------------------|--------|------|
| Step 1: 移除 dirty 标记 | 不涉及 | 修复 test #11，本方案不依赖但兼容 |
| Step 2: 增加快照字段 | Step 1 | **相同意图**，本方案更精确（含 shape 指针） |
| Step 3: 辅助函数 | Step 2-3 | 本方案不需要引用计数管理（简化版） |
| Step 4: build 时调用 | Step 2 | 同 |
| Step 5: compute_volume | Step 4 | **相同**，本方案提供完整代码 |
| Step 7: 销毁释放 | Step 3 | 同 |
| 无 | Step 5 hitresult | 本方案额外覆盖 |

合并时以本文档为准，同时保留 Step 1 的 dirty 标记移除。

---

## 五、安全性分析

### shape 指针生命周期

`shape_snapshots` 存储的 `shape*` 指针在以下条件下安全：

1. **shape 从 scene detach 后**：shape 对象不被销毁（仅从 `scn->shapes` 移除），用户仍持有引用。快照中的指针仍有效。
2. **shape 被销毁（ref_put 归零）后**：快照中的悬空指针。但根据 API 约定，用户在销毁 shape 前应先 detach。且 view 在 rebuild 前不会访问被销毁的 shape。

**风险缓解**：可选方案是在快照时增加 `s3d_shape_ref_get(shape)` 引用计数，在 `release_snapshot` 时 `ref_put`。但当前 ox_s3d 的引用计数实现较简单（无循环引用风险），且测试中无 "销毁 shape 后查询旧 view" 的场景，暂不实施。

### 几何数据不可变性

`shape->positions`, `shape->indices`, `shape->sphere_radius` 在 `mesh_setup_*` / sphere 创建后不可变（仅通过创建新 shape 替换）。快照只需记录 flip/enabled，不需深拷贝几何数据。

### 线程安全

当前 ox_s3d 为单线程设计（测试也是单线程），无并发访问风险。

---

## 六、验证步骤

```powershell
cd optix-throughput-validation/build_s3d

# 构建
cmake --build . --config Release > build.log 2>&1

# 目标测试
.\bin\Release\test_s3d_sphere.exe

# 全量回归
ctest -C Release --output-on-failure
```

预期结果：
- L104 断言通过（flip 后、rebuild 前，volume = +4/3πr³）
- L109 断言通过（rebuild 后，volume = -4/3πr³）
- 14 个已通过测试无回退