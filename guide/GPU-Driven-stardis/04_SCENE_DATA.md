# 04 — 场景数据 GPU 表达

## 1. 现有场景组织方式

### 1.1 CPU 场景数据层次

```
sdis_scene (求解器层)
├── interfaces[]            darray, ~200B/each    — 材料界面属性 (发射率、导热、对流系数...)
├── media[]                 darray, ~250B/each    — 介质属性 (固体/流体, 导热系数, 密度...)
├── prim_props[]            darray, 24B/each      — 每 primitive 属性 (interface*, front_enc, back_enc)
├── enclosures{}            htable, ~200B/each    — 每 enclosure 数据 (几何视图, 体积, 面积)
│   └── enclosure.s3d_view  → s3d_scene_view      — 子场景几何视图
│   └── enclosure.local2global[]                  — 本地 → 全局 prim_id 映射
├── key2prim3d{}            htable                 — primkey → s3d_primitive 映射
├── source                  sdis_source*           — 外部热源
├── radenv                  sdis_radiative_env*    — 环境辐射
├── s3d_view                s3d_scene_view*        — 主场景 3D 视图
└── dev                     sdis_device*           — 设备管理

s3d_scene_view (OptiX 层)
├── UnifiedTracer           OptiX pipeline + BVH   — 已在 GPU
├── pp_table[]              vector<pp_entry>       — geom_id → shape 查找
├── prim_cdf[]              vector<float>          — 面积加权 CDF (表面采样)
├── shape_snapshots{}       map                    — build-time filter 快照
├── query_prim_ranges[]     vector                 — closest-point 查询网格
└── query_sphere_entries[]  vector                 — closest-point 球体
```

### 1.2 数据访问模式分析

通过分析 step 函数的场景数据访问，识别热数据 vs 冷数据：

| 访问方式 | 调用频率 | 数据 | 当前存储 |
|---------|---------|------|---------|
| `scene_get_enclosure_ids(prim_id)` | **极高** (每次 boundary) | prim→enc 映射 | CPU prim_props[] |
| `scene_get_interface(prim_id)` | **极高** (每次 boundary) | prim→interface | CPU prim_props[] |
| `interface_side_get_emissivity()` | **高** (boundary+radiative) | 常数/回调 | CPU function pointer |
| `solid_get_thermal_conductivity()` | **高** (conductive) | 常数/T-dep | CPU function pointer |
| `scene_get_enclosure(enc_id)` | **中** (conductive init) | enc 属性 | CPU hash table |
| `s3d_primitive_get_attrib(prim,uv)` | **中** (hit 后插值) | 顶点数据 | CPU vector per shape |
| `s3d_scene_view_sample()` | **低** (convective only) | CDF + geometry | CPU vector |
| `source_sample()` | **低** (ext flux only) | 光源几何 | CPU struct |

## 2. GPU 场景数据设计

### 2.1 设计原则

1. **全只读**: 场景数据在求解过程中不变，上传一次
2. **扁平化**: 消除指针追踪、hash table、动态数组
3. **连续内存**: 利用 GPU coalesced access
4. **分层缓存**: 热数据 → L1/constant，温数据 → L2/global，冷数据 → global

### 2.2 DeviceSceneData 主结构

```cuda
struct DeviceSceneData {
    // ═══ Tier 0: 极热数据（每条路径每步都可能访问）═══
    DevicePrimTable      prims;        // 每 primitive 的快速查找
    DeviceInterfaceTable interfaces;   // 材料界面属性
    DeviceMediumTable    media;        // 介质属性
    
    // ═══ Tier 1: 热数据（特定阶段高频访问）═══
    DeviceEnclosureTable enclosures;   // enclosure 属性 + 映射
    DeviceGeometry       geometry;     // 顶点/索引数据（属性插值）
    
    // ═══ Tier 2: 温数据（偶尔的批量访问）═══
    DeviceEnclosureSampler* enc_samplers; // 每 enclosure 一个采样器
    DeviceSource         source;       // 外部热源
    DeviceRadiativeEnv   radenv;       // 环境辐射
    DeviceHTable         H_3d;         // H 函数反查表
    
    // ═══ OptiX traversable（光追用）═══
    OptixTraversableHandle traversable;
    
    // ═══ 全局参数 ═══
    double fp_to_meter;
    double tmin, tmax;
};
```

### 2.3 DevicePrimTable — 每 primitive 查找表

**最核心的表**: 每次命中都要查 prim→interface, prim→enclosure。

```cuda
struct DevicePrimTable {
    uint32_t n_primitives;
    
    // 每 primitive 属性，通过 global_prim_id 索引
    uint32_t* interface_id;      // [n_prims] → DeviceInterfaceTable 索引
    uint32_t* front_enclosure;   // [n_prims] → enclosure ID
    uint32_t* back_enclosure;    // [n_prims] → enclosure ID
};
```

**大小**: 12B/prim × 100K prims = 1.2MB。完全在 L2 cache 内。

**构建流程**:
```cpp
void build_device_prim_table(const sdis_scene* scn, DevicePrimTable* d_table) {
    uint32_t n = scn->prim_props.count;
    
    // Host staging buffers
    std::vector<uint32_t> h_interf_id(n), h_front_enc(n), h_back_enc(n);
    
    for (uint32_t i = 0; i < n; i++) {
        const struct prim_prop* pp = &scn->prim_props.items[i];
        h_interf_id[i]  = interface_get_index(pp->interf);  // 需要补充的索引API
        h_front_enc[i]  = pp->front_enclosure;
        h_back_enc[i]   = pp->back_enclosure;
    }
    
    // H2D
    cudaMalloc(&d_table->interface_id, n * sizeof(uint32_t));
    cudaMemcpy(d_table->interface_id, h_interf_id.data(), ...);
    // ... front_enclosure, back_enclosure 类似
}
```

**global_prim_id 映射**: OptiX closest-hit program 通过 `optixGetPrimitiveIndex()` 返回 GAS 内的 local prim index。需要加上该 geometry 的 offset 得到 global index：

```cuda
// OptiX closest-hit program
extern "C" __global__ void __closesthit__radiance() {
    const uint32_t geom_id = optixGetInstanceId();
    const uint32_t local_prim_id = optixGetPrimitiveIndex();
    
    // 查找该 geometry 的 global offset
    const uint32_t global_prim_id = 
        params.geom_prim_offset[geom_id] + local_prim_id;
    
    // 写入 hit result
    HitPayload& payload = getPayload();
    payload.global_prim_id = global_prim_id;
    payload.distance = optixGetRayTmax();
    payload.normal = /* ... */;
    payload.uv = /* ... */;
}
```

### 2.4 DeviceEnclosureTable

```cuda
struct DeviceEnclosureTable {
    uint32_t n_enclosures;
    uint32_t outer_enclosure_id;
    
    // 每 enclosure 属性
    uint32_t* medium_id;         // [n_enc]
    double*   hc_upper_bound;    // [n_enc]
    double*   S_over_V;          // [n_enc]
    double*   volume;            // [n_enc]
    
    // local → global prim_id 映射（用于 enc 内采样后转换 prim_id）
    // 扁平化: enc_local2global_offset[enc_id] 起始位置
    //         enc_local2global_count[enc_id] 数量
    //         enc_local2global[offset + local_prim_id] = global_prim_id
    uint32_t* local2global;        // [total_enc_prims] 扁平数组
    uint32_t* local2global_offset; // [n_enc] 每 enclosure 起始偏移
    uint32_t* local2global_count;  // [n_enc] 每 enclosure primitive 数量
};
```

### 2.5 DeviceGeometry — 几何数据

用于 step 函数中的 `s3d_primitive_get_attrib()` 替代。

```cuda
struct DeviceGeometry {
    // 按 geometry (shape) 组织
    uint32_t n_geometries;
    
    // 扁平化的顶点和索引数据
    float3*   all_positions;     // [total_vertices] 所有形状的顶点
    uint3*    all_indices;       // [total_triangles] 所有形状的三角形索引
    
    // 每 geometry 的偏移量
    uint32_t* vertex_offset;     // [n_geom] 
    uint32_t* index_offset;      // [n_geom]
    
    // 顶点属性 (ATTRIB_0 .. ATTRIB_3)
    // 扁平化: per-geometry per-attrib
    float*    attrib_data[4];    // [4][total_vertices * max_components]
    uint8_t*  attrib_type;       // [n_geom * 4] 每 geom 每 attr 的类型 (FLOAT/FLOAT2/...)
    
    // 几何法线（如果需要双精度法线）
    // OptiX 返回的是 float 法线；某些 step 需要 double 精度
    // 方案: 在 device 重算（从顶点位置 cross product）
};

__device__ float3 device_get_position_at(
    const DeviceGeometry* geom,
    uint32_t geom_id, uint32_t prim_id, float2 uv)
{
    uint32_t v_off = geom->vertex_offset[geom_id];
    uint32_t i_off = geom->index_offset[geom_id];
    
    uint3 tri = geom->all_indices[i_off + prim_id];
    float3 v0 = geom->all_positions[v_off + tri.x];
    float3 v1 = geom->all_positions[v_off + tri.y];
    float3 v2 = geom->all_positions[v_off + tri.z];
    
    float w = 1.0f - uv.x - uv.y;
    return make_float3(
        w * v0.x + uv.x * v1.x + uv.y * v2.x,
        w * v0.y + uv.x * v1.y + uv.y * v2.y,
        w * v0.z + uv.x * v1.z + uv.y * v2.z);
}
```

### 2.6 DeviceEnclosureSampler — 表面均匀采样

用于 convective step 中的 `s3d_scene_view_sample()` 替代。

```cuda
struct DeviceEnclosureSampler {
    // CDF 表（面积加权累积分布）
    float*    cdf;               // [n_prims] cumulative area distribution
    float     total_area;
    uint32_t  n_prims;
    
    // 底层几何引用（通过 DeviceGeometry 访问顶点）
    uint32_t  geom_id;           // 该 enclosure 对应的 geometry
    uint32_t  prim_offset;       // 在 all_indices 中的起始
    
    // local → global 映射
    uint32_t* local2global;      // 指向 DeviceEnclosureTable 的映射
};

__device__ void device_sample_enclosure_surface(
    const DeviceEnclosureSampler* sampler,
    const DeviceGeometry* geom,
    float r1, float r2, float r3,   // 3 uniform randoms
    float3* out_pos,
    float3* out_normal,
    uint32_t* out_global_prim_id)
{
    // Step 1: 二分查找 CDF 选择三角形
    uint32_t local_prim = upper_bound(sampler->cdf, sampler->n_prims, r1);
    
    // Step 2: 三角形内均匀采样 (Turk's method)
    float sqrt_r2 = sqrtf(r2);
    float u = 1.0f - sqrt_r2;
    float v = r3 * sqrt_r2;
    
    // Step 3: 查询顶点位置
    uint32_t idx_offset = sampler->prim_offset + local_prim;
    uint3 tri_idx = geom->all_indices[idx_offset];
    uint32_t v_off = geom->vertex_offset[sampler->geom_id];
    
    float3 v0 = geom->all_positions[v_off + tri_idx.x];
    float3 v1 = geom->all_positions[v_off + tri_idx.y];
    float3 v2 = geom->all_positions[v_off + tri_idx.z];
    
    float w = 1.0f - u - v;
    *out_pos = make_float3(
        w*v0.x + u*v1.x + v*v2.x,
        w*v0.y + u*v1.y + v*v2.y,
        w*v0.z + u*v1.z + v*v2.z);
    
    // Step 4: 几何法线
    float3 e1 = v1 - v0;
    float3 e2 = v2 - v0;
    *out_normal = normalize(cross(e1, e2));
    
    // Step 5: 全局 prim_id
    *out_global_prim_id = sampler->local2global[local_prim];
}
```

## 3. 场景组织方式的变更

### 3.1 需要的变更

| 现有结构 | 变更内容 | 原因 |
|---------|---------|------|
| `prim_props[]` (AoS, 含指针) | → SoA 分离数组 + 整数 ID 替代指针 | GPU 无法追踪 host 指针 |
| `enclosures{}` (hash table) | → 连续数组 (enc_id 作为索引) | GPU hash table 性能差 |
| `key2prim3d{}` (hash table) | → 不迁移（仅构建时使用） | 运行时不需要 |
| `s3d_hit.shape__` (void*) | → 不需要（GPU 用 geom_id 查表） | 消除指针 |
| `s3d_hit.inst__` (void*) | → instance_id 整数 | 消除指针 |
| `interface->shader` (func ptr) | → 预计算常数表 | GPU 无法调用 host func ptr |
| `medium->getter` (func ptr) | → 预计算常数表 | 同上 |
| `enclosure.s3d_view` (scene view) | → DeviceEnclosureSampler (CDF) | GPU 不需要完整 view |
| `enclosure.local2global[]` (darray) | → 扁平连续数组 | 消除 darray |

### 3.2 不需要变更的部分

| 现有结构 | 原因 |
|---------|------|
| `sdis_scene` 整体框架 | Host 侧仍需用于初始化 |
| `senc3d_scene` | 拓扑计算在构建时完成 |
| `s3d_scene/shape` | OptiX 资源管理保持 host 侧 |
| `sdis_device` | 设备管理保持 host 侧 |
| `sdis_source` | 少量属性直接上传即可 |

### 3.3 enc_id 映射策略

**问题**: 现有 `enc_id` 是用户分配的任意 unsigned（不保证从 0 连续）。GPU 需要连续索引以支持数组查找。

**解决方案**: 构建时建立 `user_enc_id → compact_enc_idx` 映射

```cpp
struct EnclosureMapping {
    std::unordered_map<uint32_t, uint32_t> user2compact; // CPU-side
    std::vector<uint32_t> compact2user;                   // 反向映射
    
    // GPU-side: 如果 user_enc_id 已知是小整数范围，直接用数组
    // 否则使用 hash table 或排序+二分
    uint32_t* d_user2compact; // [max_user_id + 1] 直接查找（如果范围小）
};
```

大多数 stardis 场景中 enc_id 从 0 开始连续分配。对于非连续情况，使用简单的重映射数组。

## 4. 内存预算

### 4.1 典型场景（porous.txt）

| 数据 | 大小 | 说明 |
|------|------|------|
| 几何顶点 | 600KB | ~50K vertices × 12B |
| 几何索引 | 1.2MB | ~100K triangles × 12B | 
| prim_props (SoA) | 1.2MB | 100K × 12B |
| interface 表 | 2KB | ~20 interfaces × ~100B |
| medium 表 | 1KB | ~10 media × ~100B |
| enclosure 表 | 5KB | ~10 enclosures × ~500B |
| local2global | 400KB | ~100K entries × 4B |
| CDF 表 | 400KB | ~100K × 4B |
| H-function 表 | 2KB | ~200 entries × 8B |
| source/radenv | <1KB | 少量常数 |
| **场景总计** | **~3.8MB** | |
| OptiX BVH | ~50MB | 已在 GPU |
| 路径状态 (32K) | ~32MB | 见 02 文档 |
| **GPU 总计** | **~86MB** | RTX 3080 (10GB) 完全无压力 |

### 4.2 大型场景估算

假设 1M triangles, 500K vertices, 100 enclosures:

| 数据 | 大小 |
|------|------|
| 几何 | ~18MB |
| prim_props | 12MB |
| local2global | 4MB |
| CDF | 4MB |
| OptiX BVH | ~200MB |
| 路径状态 | ~32MB |
| **总计** | **~270MB** |

RTX 4090 (24GB) 下仍有充裕余量。

## 5. 数据上传策略

### 5.1 一次性上传（求解前）

```cpp
DeviceSceneData* upload_scene_to_gpu(const sdis_scene* scn) {
    DeviceSceneData* d_scene;
    cudaMalloc(&d_scene, sizeof(DeviceSceneData));
    
    // 1. 构建扁平化表（CPU 侧）
    build_device_prim_table(scn, &h_scene.prims);
    build_device_interface_table(scn, &h_scene.interfaces);
    build_device_medium_table(scn, &h_scene.media);
    build_device_enclosure_table(scn, &h_scene.enclosures);
    build_device_geometry(scn, &h_scene.geometry);
    build_device_enc_samplers(scn, &h_scene.enc_samplers);
    build_device_source(scn->source, &h_scene.source);
    build_device_radenv(scn->radenv, &h_scene.radenv);
    build_device_H_table(scn->dev->H_3d, &h_scene.H_3d);
    
    // 2. H2D 上传
    upload_prim_table(&h_scene.prims);
    upload_interface_table(&h_scene.interfaces);
    // ... 全部上传
    
    // 3. 合并到 DeviceSceneData
    cudaMemcpy(d_scene, &h_scene, sizeof(DeviceSceneData), cudaMemcpyHostToDevice);
    
    return d_scene;
}
```

### 5.2 动态场景支持（未来）

当前设计假定场景在求解期间不变。如果未来需要支持动态场景（如非定常温度边界）：
- 使用 **double buffering**: 前台表供 kernel 读取，后台表在 host 更新
- 通过 CUDA events 同步切换
- 增量更新（只更新变化的属性字段）

## 6. OptiX 集成点

### 6.1 BVH 与场景数据的关系

OptiX BVH (`OptixTraversableHandle`) 已在 GPU。关键是确保 solver kernel 和 OptiX kernel 共享同一份几何数据：

```
OptiX closet-hit program → 输出 (geom_id, local_prim_id, normal, uv, distance)
solver_advance kernel    → 通过 geom_prim_offset[geom_id] + local_prim_id 得到 global_prim_id
                         → 通过 global_prim_id 查 DevicePrimTable 得到 interface_id, enc_id
                         → 通过 interface_id 查 DeviceInterfaceTable 得到材料属性
```

### 6.2 geom_prim_offset 表

在 BVH 构建时，记录每个 geometry 在全局 primitive 空间中的偏移：

```cuda
// 上传到 device，OptiX SBT hit record 或 launch params 中引用
struct GeomPrimOffset {
    uint32_t* offsets;  // [n_geometries]
    // offsets[geom_id] = 该 geometry 之前所有 geometry 的 triangle count 之和
};
```

在 `UnifiedTracer::buildAccel()` 时可计算此表。

### 6.3 Instance 变换

当前实现在 BVH 构建时 flatten instances。GPU-driven 方案保持相同策略：
- 每个 instance child 作为独立 geometry 添加到 GAS/IAS
- 通过 `geom_to_inst` 映射表在 device 侧也可重建 instance 关系（如果需要）
- 大多数 step 函数只关心 `global_prim_id` → interface/enclosure，不关心 instance 关系

### 6.4 Filter 集成

现有 CPU post-process 使用 build-time snapshot 的 filter 函数。GPU-driven 方案：

**方案 A: OptiX any-hit inline filter（推荐）**
```cuda
extern "C" __global__ void __anyhit__filter() {
    const uint32_t geom_id = optixGetInstanceId();
    const uint32_t prim_id = optixGetPrimitiveIndex();
    const uint32_t global_prim_id = params.geom_prim_offset[geom_id] + prim_id;
    
    // 读取该 primitive 的 enclosure ID
    uint32_t front_enc = params.prim_front_enc[global_prim_id];
    uint32_t back_enc  = params.prim_back_enc[global_prim_id];
    
    // 滤波逻辑: 如果不是目标 enclosure，忽略该命中
    // （具体逻辑依赖 step 类型，可通过 ray payload 传递 filter 参数）
    uint32_t target_enc = getPayload<FilterPayload>().target_enc;
    if (front_enc != target_enc && back_enc != target_enc) {
        optixIgnoreIntersection();
    }
}
```

**方案 B: 保持 multi-hit + GPU filter kernel**
- OptiX 返回 K=2 候选
- GPU filter kernel 选择第一个有效命中
- 无效全部 → 用 retrace 缓冲区重做

方案 A 更简洁，但 any-hit 可能降低 OptiX 光追吞吐量（打断 BVH traversal pipeline）。需要 profile 比较。当前 L4 inline filter (Mode A) 已证明 any-hit 性能可接受。

## 7. 总结：场景迁移清单

| 任务 | 优先级 | 工作量 | 依赖 |
|------|-------|--------|------|
| DevicePrimTable 构建+上传 | P0 | 200 LOC | scene 加载完成 |
| DeviceInterfaceTable 构建+上传 | P0 | 200 LOC | scene 加载完成 |
| DeviceMediumTable 构建+上传 | P0 | 150 LOC | scene 加载完成 |
| DeviceEnclosureTable 构建+上传 | P0 | 300 LOC | senc3d 完成后 |
| geom_prim_offset 表 | P0 | 50 LOC | BVH 构建时 |
| DeviceGeometry (顶点/索引) | P1 | 200 LOC | hit 后属性插值 |
| DeviceEnclosureSampler (CDF) | P2 | 200 LOC | convective step |
| DeviceSource + DeviceRadiativeEnv | P1 | 50 LOC | radiative step |
| DeviceHTable (H-function) | P1 | 50 LOC | conductive step |
| Voxel grid enclosure locate | P2 | 400 LOC | M10 性能优化 |
| OptiX any-hit filter 集成 | P1 | 150 LOC | L4 集成 |

**总估算**: ~1950 LOC 新代码用于场景数据 GPU 化。

---

*返回索引*: → [AGENTS.md](AGENTS.md)
