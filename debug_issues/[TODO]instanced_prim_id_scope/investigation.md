# 疑似Bug: Instanced 场景 prim_id 作用域不匹配

**状态**: 待验证（当前测试场景不触发）  
**风险等级**: 高（若使用真实 instancing 则必然出错）  
**创建日期**: 2026-02-14  
**关联**: `debug_issues/irregular_reinjection_retry_failure/` — M5_SF_ENC_RETRY 大量失败

---

## 1. 问题描述

`find_enclosure_instanced_kernel` 返回的 `prim_id` 是 **子 geom_store（BLAS）的 0-based 本地索引**，
但 solver 层 `step_enc_locate_result()` 直接用该值作为**全局索引**去查询 `scn->prim_props[]` 表。

对于 **pseudo-instance**（child_store == parent_store）或 **非 instanced 场景**，
两者碰巧使用同一个索引空间，不会出错。
但对于**真实 instanced 场景**（child_store 有独立的 0-based 索引），
BLAS-local prim_id 会索引到 `prim_props` 中错误的条目，返回错误的 `enc_id`。

**当前测试场景（porous）不使用 instancing，因此当前观察到的 M5_SF_ENC_RETRY 失败
不太可能由此 bug 引起。但此 bug 对未来使用 instancing 的场景是定时炸弹。**

---

## 2. 代码定位

### 2.1 GPU 内核: BLAS-local prim_id 的产生

**文件**: `stardis-cus3d/custar-3d/0.10/src/cus3d_find_enclosure.cu`

#### 最近点阶段 (L598-L645)

```cuda
/* find_enclosure_instanced_kernel — child_candidate lambda */
auto child_candidate = [&](uint32_t primID) -> float {
    // ↑ primID 来自 cuBQL 对 child_bvh 的遍历 → BLAS-local 索引

    uint32_t geom_idx = cur_ptg[primID];     // 子 store 的 prim_to_geom
    struct geom_gpu_entry ge = cur_ge[geom_idx]; // 子 store 的 geom_entries
    ...
    hit.prim_id  = (int32_t)primID;   // ← 写入 BLAS-local
    hit.geom_idx = (int32_t)geom_idx; // ← 子 store 的 geom 索引
    hit.inst_id  = (int32_t)instID;
};
```

#### 6-ray side 判定阶段 (L759-L878)

```cuda
/* local_intersect lambda — 同样在 child_bvh 中遍历 */
auto local_intersect = [&](uint32_t primID) -> float {
    uint32_t gidx = cp[primID];          // 子 store
    ...
    local_hit_prim = (int)primID;         // BLAS-local
};
...
resolved_prim = ray_best_prim;           // BLAS-local
...
hit.prim_id = resolved_prim;             // ← 最终写入也是 BLAS-local
```

### 2.2 Host 中间层: 直接透传

**文件**: `stardis-cus3d/custar-3d/0.10/src/s3d_scene_view_find_enclosure.cpp`, L198-L210

```cpp
/* Step 3: CPU post-processing */
results[i].prim_id  = gpu_enc->prim_id;   // 直接透传, 不做任何偏移转换
results[i].side     = gpu_enc->side;
results[i].enc_id   = (unsigned)-1;        // "needs upper-level resolution"
```

注释声称 "The prim_id from the GPU is a global primID in the geom_store"——
**对非 instanced 场景成立，对 instanced 场景不成立。**

### 2.3 Solver 层: 用 prim_id 索引 prim_props

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_wf_steps.c`, L917-L920

```c
/* step_enc_locate_result */
if(p->enc_locate.prim_id >= 0 && p->enc_locate.side >= 0) {
    unsigned enc_ids[2];
    scene_get_enclosure_ids(scn, (unsigned)p->enc_locate.prim_id, enc_ids);
    //                          ↑↑↑ 需要全局 prim_props 索引
    //                               实际收到 BLAS-local 索引
    enc_id = enc_ids[p->enc_locate.side];
}
```

**文件**: `stardis-cus3d/stardis-solver/0.16.2/src/sdis_scene_c.h`, L336

```c
static INLINE void
scene_get_enclosure_ids(const struct sdis_scene* scn, const unsigned iprim, unsigned encs[2])
{
    encs[0] = darray_prim_prop_cdata_get(&scn->prim_props)[iprim].front_enclosure;
    encs[1] = darray_prim_prop_cdata_get(&scn->prim_props)[iprim].back_enclosure;
}
```

### 2.4 对比: Trace 命中的 prim_id 处理路径（正确实现）

**文件**: `stardis-cus3d/custar-3d/0.10/src/cus3d_prim.cpp`, L47-L62

```cpp
/* Trace 命中路径: 正确处理 instanced prim_id */
const struct cus3d_geom_store* resolved_store = store;
if (gpu_hit->inst_id >= 0) {
    const struct cus3d_geom_store* cs =
        cus3d_bvh_get_instance_store(bvh, (unsigned)gpu_hit->inst_id);
    if (cs) resolved_store = cs;
}
const struct geom_entry* ge = &resolved_store->entries[gpu_hit->geom_idx];

prim->prim_id       = gpu_hit->prim_id - (int)ge->prim_offset; // shape-local
prim->scene_prim_id = (unsigned)gpu_hit->prim_id;               // geom_store 内全局
prim->shape__       = ge->shape;
```

Trace 路径通过 `inst_id → child_store → geom_entry → prim_offset` 正确解析了
shape-local 和 scene_prim_id。Enclosure 路径**缺少这一步解析**。

---

## 3. 原始 CPU 实现

CPU 实现中 enclosure 查询通过 `scene_get_enclosure_id_in_closed_boundaries()` 在
**CPU Embree 后端**上执行（见 `sdis_scene_Xd.h` L1318-L1382）。

```c
/* CPU 路径: trace ray → get hit → hit.prim.prim_id → scene_get_enclosure_ids */
SXD(scene_view_trace_ray(scn->sXd(view), P, dirs[idir], range, NULL, &hit));
scene_get_enclosure_ids(scn, hit.prim.prim_id, enc_ids);
```

CPU 的 Embree 后端返回的 `hit.prim.prim_id` 是**全局的 scene primitive ID**（与
`prim_props` 索引空间一致），因为 Embree 使用 flat geometry，不存在 instance-local
vs global 的歧义。

GPU 迁移到 cuBQL 后引入了 TLAS/BLAS 两级结构，产生了索引空间不匹配的问题。

---

## 4. 修复方案

### 方案 A: 在 host 后处理中转换 prim_id（推荐）

在 `s3d_scene_view_find_enclosure.cpp` 的 `find_enclosure_batch_impl` 中，
对 instanced 结果做与 trace 相同的 `inst_id → child_store → global prim_id` 映射。

```cpp
/* Step 3: CPU post-processing — after getting GPU results */
if(gpu_enc->inst_id >= 0) {
    /* Instance hit: need to convert BLAS-local prim_id to parent/scene prim_id */
    const struct cus3d_geom_store* cs =
        cus3d_bvh_get_instance_store(view->bvh, (unsigned)gpu_enc->inst_id);
    if(cs) {
        const struct geom_entry* ge = &cs->entries[gpu_enc->geom_idx];
        /* Map to scene-level prim_id.
         * 需要确认: scene prim_id = child_store_offset + gpu_enc->prim_id ? 
         * 或者需要通过 shape → scene primitive mapping 表? */
        results[i].prim_id = gpu_enc->prim_id; // TODO: add proper mapping
    }
}
```

**风险**: 中等。需要仔细理解 solver `prim_props` 的索引构建方式是否与
child_store prim_id 可映射。当前 `child_store_offset` 始终为 0（硬编码），
可能指示 child_store prim_id == parent_store prim_id（pseudo-instance 模式）。

### 方案 B: 在 kernel 中输出全局 prim_id

在 instanced kernel 的 `child_candidate` lambda 中加入全局偏移：

```cuda
hit.prim_id = (int32_t)(primID + inst.geom_store_offset);
```

**风险**: 低（如果 `geom_store_offset` 正确）。但当前 `geom_store_offset` 始终为 0。

### 方案 C: 让 solver 使用 inst_id + prim_id 联合查找

扩展 `s3d_enc_locate_result` 增加 `inst_id` 字段，让 solver 的
`step_enc_locate_result` 根据 `inst_id` 选择正确的 prim_props 子表。

**风险**: 高。需要修改 solver 层数据结构和查找逻辑。

---

## 5. 现有测试覆盖分析

### 5.1 Enclosure 查询测试

| 测试 | 使用 instancing? | 覆盖此 bug? |
|------|-----------------|------------|
| `test_sdis_b4_m1_enclosure_batch.c` | ❌ mock 场景，无 instancing | ❌ |
| `test_sdis_b4_m4_delta_sphere.c` | ❌ flat 几何 | ❌ |
| porous 渲染场景 | ❌ **确认不使用 instancing** | ❌ |
| T10.8 (设计文档计划) | ⛔ 尚未实现 | ❌ |

### 5.2 Instanced Trace 测试

| 测试 | 覆盖 enclosure 查询? | 覆盖此 bug? |
|------|---------------------|------------|
| `test_s3d_trace_ray_instance.c` | ❌ 仅测 trace hit 的 prim_id/inst_id/geom_id | ❌ |
| `test_s3d_batch_closest_point.c` Test 6 | ❌ closest point, 不涉及 enclosure | ❌ |
| `test_s3d_sphere_instance.c` | ❌ 采样和 trace | ❌ |

### 5.3 覆盖结论

**zero coverage** — 没有任何测试在 instanced 场景上执行 enclosure 查询。
所有现有 enclosure 测试使用 flat（非 instanced）场景。
Instanced 测试仅覆盖 trace 和 closest-point。

### 5.4 当前 porous 场景不触发

确认 `Stardis-Starter-Pack/porous/porous.txt` 不包含 instance 关键字。
因此当前的 M5_SF_ENC_RETRY 失败**不是此 bug 导致的**。
Porous 使用 `find_enclosure_kernel`（非 instanced），其 prim_id 是全局索引。

---

## 6. 测试方案

### T1: Instanced 场景 enclosure 查询正确性（集成测试）

**目的**: 验证 instanced 场景中 `find_enclosure_instanced_kernel` 返回的
prim_id 能正确映射到 `prim_props` 的 enc_id。

**方法**:
1. 构造 cube-in-cube 场景（外部 ambient enclosure + 内部 solid enclosure）
2. 将内部 cube 注册为 instance（非 pseudo-instance，独立 child_store）
3. 在内部 cube 内外各放置 N 个查询点
4. GPU `find_enclosure_instanced_kernel` → `prim_id` → `scene_get_enclosure_ids` → `enc_id`
5. CPU `scene_get_enclosure_id_in_closed_boundaries` → `enc_id_ref`
6. 逐点对比 `enc_id` vs `enc_id_ref`

**预期结果**: 如果 prim_id 作用域 bug 存在，instanced 几何内部的查询点
将返回错误的 `enc_id`（索引到错误的 `prim_props` 条目）。

**位置**: `stardis-cus3d/custar-3d/0.10/src/test_s3d_enc_instanced.c`

### T2: prim_id 范围验证（单元测试）

**目的**: 验证 instanced kernel 返回的 prim_id 不会越界。

**方法**:
1. 构造场景：parent_store 有 100 个 prim，child_store 有 20 个 prim
2. 执行 instanced enclosure 查询
3. 验证返回的 prim_id 在 `[0, prim_props_size)` 范围内
4. 如果 prim_id 是 BLAS-local（0-based），预期 `prim_id < 20` 
   但 `prim_props_size == 100`，不会越界但索引错误

**预期结果**: prim_id < 20 将索引到 parent_store 的前 20 个条目（不是 instance 对应的条目）。

### T3: Pseudo-instance vs 真实 instance 对比（回归测试）

**目的**: 确认 pseudo-instance（identity transform，共享 parent store）不受影响。

**方法**:
1. 同一场景，分别用 pseudo-instance 和真实 instance 设置
2. 对相同查询点执行 enclosure 查询
3. 对比结果

**预期结果**: pseudo-instance 正确（prim_id 空间一致），真实 instance 可能出错。

### T4: child_store_offset 检验（代码审查）

**目的**: 确认 `child_store_offset` 始终为 0 的原因和影响。

**方法**:
1. 检查 `cus3d_bvh.cu` 中所有 `child_store_offset = 0` 的设置点
2. 分析是否有未来计划使用非零 offset
3. 如果 offset 应该用于索引转换但始终为 0，则是未完成的实现

---

## 7. 与当前 M5_SF_ENC_RETRY 问题的关系

**当前 porous 场景不使用 instancing**，因此此 bug **不是当前观察到的
`solid_enc=1, enc_resolved=0` 问题的根因**。

当前 retry 问题更可能与以下因素相关：
- Bug #1（旋转矩阵不匹配）在特定凹角几何处导致 6-ray 命中不同面
- 凹角/凹边处的数值精度问题
- float 精度截断（查询点从 double 转为 float 上传到 GPU）

此 bug 应归类为**预防性修复**——在引入真实 instanced 场景之前必须解决。
