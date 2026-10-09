# 修复：test_s3d_closest_point 搜索半径覆盖不足

**日期**: 2026-02-22  
**测试**: test_s3d_closest_point — 行 894  
**状态**: 待实施  

---

## 一、问题根因

### 1.1 失败现象

```c
// test_s3d_closest_point.c:888-894
FOR_EACH(i, 0, 10000) {
    pos[i] = mid[i] + (rand_canonic() * 2 - 1) * (extent[i]) * 5.f;  // 10× AABB
    CHK(s3d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit) == RES_OK);
    CHK(!S3D_HIT_NONE(&hit));  // ← 失败：部分查询返回 HIT_NONE
}
```

对单个三角形用 `radius=INF` 查询 10000 个随机点（分布在 10× AABB 范围内），部分查询返回 miss。

### 1.2 语义确认

OptiX 实现的数学**是正确的表面投影语义**（非点云 NN）：

- `nn_programs.cu` 中 `closestPointOnTriangle()` 实现了标准 Voronoi region 最近面投影算法
- `__intersection__nn` 使用 `optixGetWorldRayOrigin()` 作为查询点，对三角形表面求最近点
- 返回重心坐标 `(u, v)` 和距离平方

### 1.3 根因：AABB 覆盖范围不足

OptiX 最近点查询使用"零长度射线 + 膨胀 AABB"机制：

```
查询点 → 零长度射线(tmax=1e-16) → BVH 遍历 → 仅检查包含射线 origin 的 AABB
```

AABB 膨胀量 = `search_radius`，在 `ensure_built` 中设为 `diag * 2`：

```cpp
// ox_s3d_scene_view.cpp:191
sv->search_radius = diag * 2.0f;
```

**当查询点距离三角形超过 `search_radius` 时，不在任何扩展 AABB 内，intersection 程序不会被调用 → miss。**

#### 数值验证

| 量 | 值 |
|---|---|
| 测试三角形 | `(-0.5,-0.3,0.1), (-0.4,0.2,0.3), (0.7,0.01,-0.5)` |
| AABB 尺寸 | `(1.2, 0.5, 0.8)` |
| 场景 diag | `≈ 1.53` |
| `search_radius = diag × 2` | `≈ 3.06` |
| 查询点最远距离 (10× AABB) | `≈ 7.8` |
| **覆盖缺口** | `7.8 > 3.06` → **~50%+ 查询点超出覆盖** |

#### 与 Embree CPU 的关键差异

| 特性 | Embree (CPU) | OptiX (当前) |
|------|-------------|-------------|
| 搜索启动 | `rtcPointQuery(scene, &query, ...)` | 零长度射线 `optixTrace(origin, dir, 0, 1e-16)` |
| 搜索范围 | **动态**：初始 radius 参数可为 INF | **静态**：构建时固定为 `diag × 2` |
| 半径缩小 | 回调中 `args->query->radius = new_dist` → BVH 自动剪枝 | intersection 中更新 payload，**不影响 BVH 遍历范围** |
| INF 半径支持 | ✅ 原生支持 | ❌ 受限于 AABB 扩展量 |

### 1.4 附带 Bug：radius exclusive 语义

测试行 913-921 期望 `radius` 是 **exclusive upper bound**：

```c
radius = hit.distance;
CHK(s3d_scene_view_closest_point(view, pos, radius, NULL, &hit) == RES_OK);
CHK(S3D_HIT_NONE(&hit));            // distance == radius → miss (exclusive)
radius = nextafterf(radius, FLT_MAX);
CHK(s3d_scene_view_closest_point(view, pos, radius, NULL, &hit) == RES_OK);
CHK(!S3D_HIT_NONE(&hit));           // distance < radius → hit
```

但 `nn_programs.cu:239` 用的是 `dist <= q.radius`（inclusive）。

---

## 二、修复方案

### 策略：按需扩大 search_radius + 重建 Query GAS

`UnifiedTracer` 已有完整工具链，无需修改底层：

| API | 作用 | 已有 |
|-----|------|------|
| `setSearchRadius(float)` | 修改 `m_search_radius`（仅设值） | ✅ |
| `rebuildQueryGAS(bool)` | 用新 radius 重新生成 AABB + 重建 GAS | ✅ |
| `getSearchRadius()` | 查询当前值 | ✅ |

**只需修改 `ox_s3d_scene_view.cpp` 和 `nn_programs.cu`。**

---

## 三、修改清单（3 处）

### 修改 1：单次 CP 搜索半径扩展

**文件**: `s3d_wrapper/ox_s3d_scene_view.cpp`  
**位置**: `s3d_scene_view_closest_point` 函数，`if (!sv->query_mesh_set)` 之后、`CPQuery q;` 之前

**原代码**:
```cpp
    if (!sv->query_mesh_set) return RES_OK;

    CPQuery q;
    q.position = make_float3(pos[0], pos[1], pos[2]);
    q.radius   = radius;
```

**改为**:
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
    q.position = make_float3(pos[0], pos[1], pos[2]);
    q.radius   = radius;
```

### 修改 2：批量 CP 搜索半径扩展

**文件**: `s3d_wrapper/ox_s3d_scene_view.cpp`  
**位置**: `batch_cp_impl` 函数，build query buffer 段

**原代码**:
```cpp
    double t0 = now_ms();

    /* Build query buffer */
    std::vector<CPQuery> queries(nqueries);
    for (size_t i = 0; i < nqueries; i++) {
        queries[i].position = make_float3(requests[i].pos[0],
                                           requests[i].pos[1],
                                           requests[i].pos[2]);
        queries[i].radius   = requests[i].radius;
    }

    std::vector<CPResult> results = sv->tracer.closestPointBatch(queries);
```

**改为**:
```cpp
    double t0 = now_ms();

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

### 修改 3：radius exclusive 语义

**文件**: `device/nn_programs.cu`  
**位置**: `__raygen__cp` 函数，行 239

**原代码**:
```cuda
        if (dist <= q.radius) {
```

**改为**:
```cuda
        if (dist < q.radius) {
```

**理由**: CPU 参考实现中 radius 是 exclusive upper bound — `distance == radius` 时不算命中。

---

## 四、设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| INF 映射值 | `1e30f` | `FLT_MAX ≈ 3.4e38`，AABB 扩展 `±FLT_MAX` 会在 CUDA float 运算中溢出；`1e30` 足够覆盖任何实际场景 |
| 缓存策略 | 取 max，只增不减 | 第一次大 radius 查询触发重建，后续不再需要；`sv->search_radius` 单调递增 |
| `rebuildQueryGAS(true)` | compact=true | 重建后 compaction 减少显存占用 |
| 是否改 `ensure_built` | **否** | `diag × 2` 是好的初始默认值（覆盖大多数常规查询），仅在用户请求更大 radius 时才扩展 |
| `std::isfinite` vs `std::isinf` | `std::isfinite` | 同时处理 INF 和 NaN |

---

## 五、性能影响

| 场景 | 影响 |
|------|------|
| `radius < diag × 2` | **零影响**，不触发重建 |
| `radius = INF`（首次） | **一次性重建** Query GAS（~1-10 ms），AABB 膨胀到 1e30 |
| `radius = INF`（后续） | 无重建；BVH 退化为 O(N) 遍历（所有 AABB 完全重叠）|
| 正常 batch 查询 | 取 max radius 一次重建，后续 batch 复用 |

> **注**: 当 `search_radius = 1e30` 时所有 AABB 完全重叠，OptiX BVH 无法有效分区，等价于线性扫描。但 Embree 在 `radius=INF` 时同样无法有效裁剪（初始半径无限大时第一次遍历也是全局的）。大场景中 `radius=INF` 本身就不是性能友好的调用方式。

---

## 六、验证检查点

修改后应通过的测试段：

| 行号 | 检查内容 | 修复前 | 修复后 |
|------|---------|--------|--------|
| 894 | `!S3D_HIT_NONE` — 10×AABB 范围 + radius=INF | ❌ 部分 miss | ✅ 1e30 覆盖全部 |
| 896 | `f3_eq_eps(closest_pos, attr.value, 1e-4)` — 结果精度 | N/A (未到达) | ✅ 数学正确 |
| 915 | `S3D_HIT_NONE` — `radius = hit.distance` → miss | ❌ inclusive | ✅ exclusive |
| 919 | `!S3D_HIT_NONE` — `radius = nextafterf(distance)` → hit | ✅ | ✅ |
| 1013 | 16 组 amplitude 精度测试 (每组新建 view) | ❌ 大 amplitude miss | ✅ 每组重建 |
| 1159 | 实例化三角形 CP 一致性 | 视 radius 覆盖 | ✅ |

### 构建 & 运行

```powershell
cd D:\Works\Projects\Stardis-GPU\optix-throughput-validation\build_s3d
cmake --build . --config Release --target test_s3d_closest_point > build_cp.log 2>&1
Select-String "error" build_cp.log
& .\bin\Release\test_s3d_closest_point.exe
```

### 回归测试

修改涉及 CP 路径，需确认已通过的 CP 相关测试不受影响：

```powershell
& .\bin\Release\test_s3d_batch_closest_point.exe
```

---

## 七、后续考虑

如果产品化部署中 `radius=INF` 的大场景性能不可接受，可考虑：

1. **双 GAS 策略**：保留 `diag × 2` 的 GAS 用于常规查询；仅在 miss 时用大 radius GAS 重试
2. **分桶策略**：batch 中按 radius 分组，小 radius 用紧凑 GAS，大 radius 用膨胀 GAS
3. **cuBQL 回退**：`stardis-cus3d/custar-3d` 中的 cuBQL 实现天然支持动态搜索半径裁剪，可作为大 radius 查询的后端

当前阶段以正确性优先，性能优化留待后续。
