# 02 — Step 函数 GPU 迁移方案

## 1. 现有 Step 函数基础设施

### 1.1 现有调度入口

两个主分发函数基于 `path_phase` 枚举值路由到具体 step：

```c
// 无需光线结果时的分发（~30 个 case）
advance_one_step_no_ray(slot):
  switch (slot->hp.phase) {
    case PATH_INIT:                    → step_init()
    case PATH_COUPLED_BOUNDARY:        → step_boundary()
    case PATH_COUPLED_CONDUCTIVE:      → step_conductive()
    case PATH_COUPLED_CONVECTIVE:      → step_convective()
    case PATH_COUPLED_RADIATIVE:       → step_coupled_radiative_begin()
    case PATH_BND_DISPATCH:            → step_bnd_dispatch()
    case PATH_BND_SF_PROB_DISPATCH:    → step_bnd_sf_prob_dispatch()
    case PATH_BND_SF_NULLCOLL_DECIDE:  → step_bnd_sf_nullcoll_decide()
    case PATH_BND_SFN_PROB_DISPATCH:   → step_bnd_sfn_prob_dispatch()
    case PATH_BND_SFN_RAD_DONE:        → step_bnd_sfn_rad_done()
    case PATH_BND_SFN_COMPUTE_Ti:      → step_bnd_sfn_compute_Ti()
    case PATH_BND_SFN_CHECK_PMIN_PMAX: → step_bnd_sfn_check_pmin_pmax()
    case PATH_BND_SS_REINJECT_DECIDE:  → step_bnd_ss_reinject_decide()
    case PATH_CND_DS_CHECK_TEMP:       → step_cnd_ds_check_temp()
    case PATH_CND_DS_STEP_ADVANCE:     → step_cnd_ds_step_advance()
    case PATH_CND_WOS_CHECK_TEMP:      → step_cnd_wos_check_temp()
    case PATH_CND_WOS_TIME_TRAVEL:     → step_cnd_wos_time_travel()
    case PATH_CNV_INIT:                → step_cnv_init()
    case PATH_CNV_SAMPLE_LOOP:         → step_cnv_sample_loop()
    case PATH_BND_POST_ROBIN_CHECK:    → step_bnd_post_robin_check()
    case PATH_ENC_LOCATE_RESULT:       → step_enc_locate_result()
    // ... 更多
  }

// 有光线结果时的分发（~15 个 case）
advance_one_step_with_ray(slot, hit):
  switch (slot->hp.phase) {
    case PATH_RAD_TRACE_PENDING:         → step_radiative_trace()
    case PATH_BND_EXT_DIRECT_TRACE:      → step_bnd_ext_direct_result()
    case PATH_BND_EXT_DIFFUSE_TRACE:     → step_bnd_ext_diffuse_result()
    case PATH_BND_EXT_DIFFUSE_SHADOW:    → step_bnd_ext_diffuse_shadow_result()
    case PATH_BND_SF_REINJECT_SAMPLE:    → step_bnd_sf_reinject_process()
    case PATH_BND_SF_NULLCOLL_RAD_TRACE: → step_bnd_sf_nullcoll_rad_trace()
    case PATH_BND_SS_REINJECT_SAMPLE:    → step_bnd_ss_reinject_process()
    case PATH_CND_DS_STEP_TRACE:         → step_cnd_ds_step_process()
    case PATH_CND_WOS_CLOSEST_RESULT:    → step_cnd_wos_closest_result()
    case PATH_CND_WOS_DIFFUSION_CHECK:   → step_cnd_wos_diffusion_check_result()
    case PATH_CND_WOS_FALLBACK_TRACE:    → step_cnd_wos_fallback_result()
    case PATH_CNV_STARTUP_TRACE:         → step_cnv_startup_result()
    case PATH_ENC_QUERY_EMIT:            → step_enc_query_resolve()
    case PATH_ENC_QUERY_FB_EMIT:         → step_enc_query_fb_resolve()
    // ... 更多
  }
```

### 1.2 path_state 结构 (CPU 版本)

```
path_state (~2KB per path)
├── path_hot (8B) — P0_OPT: phase, active, needs_ray, ray_bucket, ray_count_ext
├── path_core (~500B)
│   ├── wf_rng (100B) — Threefry CBRNG state
│   ├── sdis_heat_path (~200B) — 物理路径状态
│   │   ├── position[3], time, normal[3]
│   │   ├── rwalk (random walk vertex)
│   │   ├── green_path (Green 函数累积)
│   │   └── nbranchings, sub_path_branch_id
│   ├── s3d_hit (40B) — 最新光追结果缓存
│   ├── ray_request[6] — 待发射光线（最多 6 条 for enc query）
│   └── locals union (~200B) — 按阶段复用的局部状态
│       ├── bnd_sf — solid-fluid boundary 本地数据
│       ├── bnd_ss — solid-solid boundary 本地数据
│       ├── bnd_ext — external flux 本地数据
│       ├── cnd_ds — delta-sphere 本地数据
│       ├── cnd_wos — Walk-on-Sphere 本地数据
│       └── cnv — convective 本地数据
│
├── path_sfn_data (SoA, 冷数据) — PicardN 递归栈
├── path_enc_data (SoA, 冷数据) — enclosure 查询 6 方向 + 命中
└── path_ext_data (SoA, 冷数据) — external flux 累积
```

## 2. GPU Step 函数设计

### 2.1 `__device__` 函数签名

```cuda
// 统一的 step 返回类型
struct StepResult {
    path_phase next_phase;      // 下一个阶段
    uint8_t    needs_ray;       // 是否需要光线
    uint8_t    ray_count;       // 需要的光线数量 (1, 2, 4, 6)
    uint8_t    ray_bucket;      // 光线类型桶
    uint8_t    done;            // 路径是否完成
};

// 设备端 step 函数签名
__device__ StepResult device_step_init(
    DevicePathState* ps,
    const DeviceSceneData* scene);

__device__ StepResult device_step_radiative_trace(
    DevicePathState* ps,
    const DeviceHitResult* hit,
    const DeviceSceneData* scene);

// ... 每个 step 函数一个 __device__ 版本
```

### 2.2 GPU path_state 设计 (SoA 布局)

CPU 版本使用 AoS（每路径一个大 struct），GPU 版本使用 **分级 SoA** 以优化内存访问模式：

```cuda
// ═══ Tier 0: 极热数据（每轮都访问, 在同一 cache line 内）═══
struct PathHotSoA {        // 8B × N paths
    uint8_t*  phase;       // path_phase 枚举
    uint8_t*  active;      // 是否活跃
    uint8_t*  needs_ray;   // 是否需要光线
    uint8_t*  ray_bucket;  // 光线类型桶
    uint8_t*  ray_count;   // 需要的光线数量
    uint8_t*  done;        // 是否完成
    uint16_t* pad;         // 对齐
};

// ═══ Tier 1: 热数据（大多数 step 都访问）═══
struct PathCoreSoA {
    // 几何状态 (每路径 72B)
    double3*  position;     // 当前位置
    double3*  normal;       // 当前法线
    double*   time;         // 当前时间

    // 光追命中缓存 (每路径 56B)
    float3*   hit_normal;   
    float2*   hit_uv;       
    float*    hit_distance;  
    uint32_t* hit_geom_id;  
    uint32_t* hit_prim_id;  
    uint32_t* hit_inst_id;

    // RNG (每路径 100B)  
    // Threefry key + counter + buffer
    uint64_t* rng_key;      // [4 × N]
    uint64_t* rng_ctr;      // [4 × N]
    uint64_t* rng_buf;      // [4 × N]
    int32_t*  rng_buf_idx;

    // 物理路径追踪 (每路径 ~100B)
    double*   weight;       // 路径权重
    uint32_t* enc_id;       // 当前 enclosure
    uint32_t* prim_id;      // 当前 primitive
    uint8_t*  side;         // 前/后面
    int32_t*  nbranchings;  // 分支次数
};

// ═══ Tier 2: 温数据（特定阶段访问）═══
struct PathLocalsSoA {
    // 边界条件局部状态 — 按阶段复用
    // bnd_sf, bnd_ss, bnd_ext 各需 ~80-120B
    double*   locals_bnd_f64;  // [16 × N] 统一双精度局部变量
    uint32_t* locals_bnd_u32;  // [8 × N]  统一整数局部变量
    float3*   locals_dirs;     // [4 × N]  方向向量
};

// ═══ Tier 3: 冷数据（偶尔访问）═══
struct PathColdSoA {
    // PicardN 递归栈 (仅 M8 使用, 每路径 ~200B if active)
    double*   sfn_T_values;    // [MAX_PICARD_DEPTH × N]
    uint32_t* sfn_depth;       // [N]
    
    // Enclosure 6-ray 查询 (仅 M1/M10 使用)
    float3*   enc_directions;  // [6 × N]
    float*    enc_dir_hits;    // [6 × N]
    
    // External flux (仅 M7 使用)
    double*   ext_flux_direct;   // [N]
    double*   ext_flux_scattered;// [N]
};

// ═══ Green 函数累积器（固定大小）═══
struct PathGreenSoA {
    // 替代 CPU 的 darray，使用固定大小
    double*   power_terms;     // [MAX_POWER_TERMS × N] (MAX ~32)
    double*   flux_terms;      // [MAX_FLUX_TERMS × N]  (MAX ~16)
    uint32_t* power_count;     // [N]
    uint32_t* flux_count;      // [N]
    double*   result_temperature; // [N] 累积温度
};
```

**内存预算** (32K paths, RTX 4090):
| Tier | 每路径大小 | 总计 |
|------|-----------|------|
| Tier 0 (hot) | 8B | 256KB |
| Tier 1 (core) | ~330B | 10.3MB |
| Tier 2 (locals) | ~200B | 6.3MB |
| Tier 3 (cold) | ~200B | 6.3MB |
| Green | ~300B | 9.4MB |
| **总计** | ~1038B | **32.4MB** |

对比 CPU 版本 ~2KB/path × 32K = 64MB，GPU SoA 版本压缩到 32MB（因为消除了 padding 和指针开销）。完全在 L2 cache (RTX 4090: 72MB) 范围内。

### 2.3 GPU Step 函数迁移策略

#### Phase 映射表

| CPU Step 函数 | 源文件 | GPU 迁移难度 | 关键障碍 |
|--------------|--------|-------------|---------|
| `step_init` | core.c | **简单** | 无 |
| `step_radiative_trace` | core.c | **简单** | BRDF 回调 → 编译期解析 |
| `step_coupled_radiative_begin` | core.c | **简单** | 半球采样 |
| `step_boundary` (router) | core.c | **简单** | 纯路由 |
| `step_conductive` (router) | core.c | **简单** | 纯路由 |
| `step_convective` (router) | core.c | **简单** | 纯路由 |
| `step_bnd_dispatch` | cnv.c | **简单** | interface 查询 |
| `step_bnd_post_robin_check` | cnv.c | **简单** | medium 温度查询 |
| `step_bnd_ext_check` | bnd_ext.c | **中等** | source 采样 + interface 查询 |
| `step_bnd_ext_direct_result` | bnd_ext.c | **简单** | 半球采样 |
| `step_bnd_ext_diffuse_result` | bnd_ext.c | **中等** | BRDF + source 多重采样 |
| `step_bnd_ext_diffuse_shadow_result` | bnd_ext.c | **简单** | 单次命中处理 |
| `step_bnd_ext_finalize` | bnd_ext.c | **中等** | Green 函数 add_flux |
| `step_bnd_sf_reinject_sample` | bnd_sf.c | **中等** | 2-ray emit + retry 逻辑 |
| `step_bnd_sf_reinject_process` | bnd_sf.c | **中等** | enclosure 验证 + retry |
| `step_bnd_sf_prob_dispatch` | bnd_sf.c | **中等** | null-collision + 多分支 |
| `step_bnd_sf_nullcoll_rad_trace` | bnd_sf.c | **中等** | BRDF + 吸收测试循环 |
| `step_bnd_sf_nullcoll_decide` | bnd_sf.c | **简单** | 概率判定 |
| `step_bnd_sfn_prob_dispatch` | bnd_sfn.c | **困难** | PicardN 栈管理 |
| `step_bnd_sfn_rad_trace` | bnd_sfn.c | **中等** | 同 sf nullcoll |
| `step_bnd_sfn_rad_done` | bnd_sfn.c | **困难** | 栈初始化 + 温度链 |
| `step_bnd_sfn_compute_Ti` | bnd_sfn.c | **非常困难** | 递归子路径 |
| `step_bnd_sfn_check_pmin_pmax` | bnd_sfn.c | **中等** | 边界检查 |
| `step_bnd_ss_reinject_sample` | bnd_ss.c | **中等** | 4-ray emit |
| `step_bnd_ss_reinject_process` | bnd_ss.c | **中等** | 4-hit 处理 + retry |
| `step_bnd_ss_reinject_decide` | bnd_ss.c | **简单** | 概率判定 |
| `step_cnd_ds_check_temp` | cnd.c | **中等** | enclosure + solid 属性 |
| `step_cnd_ds_step_advance` | cnd.c | **中等** | 体积功率 + 时间回溯 |
| `step_cnd_wos_check_temp` | cnd.c | **中等** | closest-point query |
| `step_cnd_wos_closest_result` | cnd.c | **中等** | ε-shell 判定 + 扩散 |
| `step_cnd_wos_time_travel` | cnd.c | **中等** | H 函数反查表 |
| `step_cnv_init` | cnv.c | **简单** | 流体属性查询 |
| `step_cnv_sample_loop` | cnv.c | **中等** | 表面均匀采样 |
| `step_enc_query_emit` | enc.c | **中等** | 6 方向旋转矩阵 |
| `step_enc_query_resolve` | enc.c | **简单** | 6-hit 距离排序 |
| `step_enc_locate_result` | enc.c | **简单** | prim_id → enc_id 查表 |

### 2.4 迁移分组策略

按功能模块分 4 组，每组内部依赖闭合：

**Group 1: Core Path（radiative bounce loop）**
```
step_init → step_radiative_trace → step_coupled_radiative_begin
依赖: BRDF, interface, medium, RNG, radiative_env
光线类型: RADIATIVE
测试: wf_a* 系列
```

**Group 2: Boundary（M3/M5/M7/M8）**
```
step_bnd_dispatch
  ├── step_bnd_ss_* (M3: 4-ray solid-solid)
  ├── step_bnd_sf_* (M5: 2-ray solid-fluid + null collision)
  ├── step_bnd_sfn_* (M8: PicardN recursive)
  └── step_bnd_ext_* (M7: external flux shadow rays)
step_bnd_post_robin_check
依赖: interface, medium, Green fn, source, RNG, enclosure
光线类型: STEP_PAIR, SHADOW, RADIATIVE
测试: wf_b*, wf_c*, wf_f* 系列
```

**Group 3: Conductive（M4/M9）**
```
step_cnd_ds_* (M4: delta-sphere)
step_cnd_wos_* (M9: Walk-on-Spheres)
依赖: solid properties, enclosure, H-function table, Green fn, RNG
光线类型: STEP_PAIR (M4), RADIATIVE (M9 fallback), CP query
测试: wf_c* 系列
```

**Group 4: Convective（M6）+ Enclosure Query（M1/M10）**
```
step_cnv_* (M6: null-collision surface sampling)
step_enc_* (M1: 6-ray + fallback, M10: BVH locate)
依赖: fluid properties, enclosure geometry, RNG, s3d sampling
光线类型: STARTUP, ENCLOSURE
测试: wf_d* 系列
```

## 3. 关键迁移挑战

### 3.1 挑战 1: 函数指针 → 编译期分发

CPU 版本大量使用函数指针回调（interface shader, medium property getter, BRDF 类型）。GPU 无法使用函数指针（性能灾难）。

**解决方案**: 编译期模板特化 + enum 分发

```cuda
// CPU 版本: 函数指针回调
double emissivity = interface_side_get_emissivity(interf, side, &frag);
// 内部: interf->shader->get_emissivity(interf->data, side, &frag)

// GPU 版本: 设备端查表
__device__ double device_get_emissivity(
    const DeviceInterfaceTable* table,
    uint32_t interf_id,
    uint8_t side, 
    const DeviceFragment* frag) 
{
    // 大多数场景: 常数发射率，直接从表中读取
    return table->emissivity[interf_id * 2 + side];
    
    // 若需支持空间变化发射率:
    // 使用预计算的纹理查找或分段线性插值
}
```

**适用范围分析**:

| 回调类型 | 典型实现 | GPU 替代方案 |
|---------|---------|-------------|
| `interface_side_get_emissivity()` | 常数 | 查表 O(1) |
| `interface_side_get_temperature()` | 常数或线性 | 查表 + 插值 |
| `interface_get_convection_coef()` | 常数 | 查表 O(1) |
| `interface_get_thermal_contact_resistance()` | 常数 | 查表 O(1) |
| `solid_get_thermal_conductivity()` | 常数或 T-dependent | 查表或多项式求值 |
| `solid_get_delta()` | 常数 | 查表 O(1) |
| `fluid_get_temperature()` | 常数 | 查表 O(1) |
| `fluid_get_properties()` | 常数 | 查表 O(1) |
| `brdf_sample()` | Lambert (diffuse) | 内置 cosine 半球采样 |
| `source_sample()` | point/sphere 光源 | 内置几何采样 |

> **关键洞察**: stardis 的材料系统以**常数属性**为主（工程热分析场景），少数场景有温度依赖。编译期查表覆盖 >95% 的用例。

### 3.2 挑战 2: 动态数组 → 固定大小缓冲区

CPU Green 函数使用 `darray`（可扩展数组）。GPU 需预分配固定大小。

**分析**: 通过 profiling 现有测试用例，统计 power_terms 和 flux_terms 的最大使用量：
- power_terms: 典型 2–8 项，极端 ~20 项
- flux_terms: 典型 1–4 项，极端 ~10 项
- 设 MAX_POWER_TERMS = 32, MAX_FLUX_TERMS = 16 足够覆盖所有场景

```cuda
struct DeviceGreenPath {
    double power_terms[MAX_POWER_TERMS];  // 固定大小
    double flux_terms[MAX_FLUX_TERMS];
    uint16_t power_count;
    uint16_t flux_count;
    // 溢出处理: 如果 count > MAX, 设置 overflow 标记
    // 溢出路径在下一轮 host 同步时转移到 CPU 处理（极罕见）
    uint8_t overflow;
};
```

### 3.3 挑战 3: PicardN 递归

M8 (PicardN) 的 `step_bnd_sfn_compute_Ti` 会递归进入 `PATH_COUPLED_BOUNDARY` ——即一条路径在计算温度时需要启动一条"子路径"。

**CPU 实现**: 使用 `sfn_stack[MAX_PICARD_DEPTH]` 保存/恢复上下文，depth 典型 ≤ 3。

**GPU 方案**: 保持相同的显式栈（已是迭代化的），直接迁移：
```cuda
struct DeviceSfnStack {
    // 每层保存的上下文
    struct {
        double T_values[6];
        double rwalk_saved[8];
        uint32_t return_state;
        uint32_t depth;
    } frames[MAX_PICARD_DEPTH];  // MAX_PICARD_DEPTH = 4
    uint32_t current_depth;
};
```

**风险**: 栈溢出时 CPU 版本 fallback 到同步计算。GPU 版本需要：
- 方案 A: 标记溢出路径，延迟到 host 处理（影响极小，因为溢出极罕见）
- 方案 B: 增大 MAX_PICARD_DEPTH 到 6（增加 ~100B/path 冷数据）

### 3.4 挑战 4: 表面均匀采样 (`s3d_scene_view_sample`)

M6 convective 的 `step_cnv_sample_loop` 调用 `s3d_scene_view_sample()` 在 enclosure 表面均匀采样一个点。当前这是 host-side 函数（使用预计算的 CDF 表 + 三角形面积加权采样）。

**GPU 方案**: 将 CDF 表和三角形数据上传到 device：
```cuda
struct DeviceEnclosureSampler {
    float*    prim_cdf;       // [n_prims] 累积分布函数
    float     total_area;     
    uint32_t  n_prims;
    
    // 三角形顶点（用于面内均匀采样）
    float3*   vertices;       // [3 * n_prims]（或引用 GAS 内共享数据）
};

__device__ void device_sample_enclosure_surface(
    const DeviceEnclosureSampler* sampler,
    float r1, float r2, float r3,   // 3 个 RNG uniform
    float3* out_pos, float3* out_normal, uint32_t* out_prim_id);
```

### 3.5 挑战 5: `s3d_primitive_get_attrib` → device 几何查询

多个 step 调用 `s3d_primitive_get_attrib()` 用 barycentric 坐标插值顶点属性。当前是 host-side 函数。

**GPU 方案**: 上传顶点属性到 device texture/buffer，提供 device 查询：
```cuda
__device__ float3 device_get_attrib_position(
    const DeviceGeometry* geom,
    uint32_t geom_id, uint32_t prim_id, float2 uv)
{
    uint3 idx = geom->indices[geom_id][prim_id];
    float3 v0 = geom->positions[idx.x];
    float3 v1 = geom->positions[idx.y];
    float3 v2 = geom->positions[idx.z];
    float w = 1.0f - uv.x - uv.y;
    return w * v0 + uv.x * v1 + uv.y * v2;
}
```

## 4. advance_one_step → device_advance 迁移模式

### 4.1 通用模式

每个 CPU step 函数迁移为 `__device__` 函数，遵循统一的输入/输出协议：

```cuda
// 输入: 路径状态 + 场景数据（只读）+ 光追结果（可选）
// 输出: 修改后的路径状态 + StepResult{next_phase, needs_ray, done}

__device__ StepResult device_step_XXX(
    // 读写: 路径状态指针（指向 SoA 中当前路径的各字段）
    DevicePathAccessor* path,
    // 只读: 光追结果（当 has_pending_hit 时有效）
    const DeviceHitResult* hit,    // nullable
    // 只读: 场景数据表
    const DeviceSceneData* scene,
    // 读写: RNG 状态 (嵌入 path 中但单独传递以便内联)
    DeviceRNG* rng)
{
    StepResult result = {0};
    
    // ... step 逻辑 ...
    
    result.next_phase = PATH_XXX;
    result.needs_ray = 1;
    result.ray_count = 2;
    result.ray_bucket = RAY_BUCKET_STEP_PAIR;
    return result;
}
```

### 4.2 cascade loop 的 GPU 实现

```cuda
__device__ void device_cascade_advance(
    DevicePathAccessor* path,
    const DeviceSceneData* scene,
    DeviceRNG* rng,
    int max_iterations)
{
    for (int i = 0; i < max_iterations; i++) {
        if (path->needs_ray || path->done) break;
        
        StepResult r;
        switch (path->phase) {
            case PATH_INIT:
                r = device_step_init(path, NULL, scene, rng);
                break;
            case PATH_COUPLED_BOUNDARY:
                r = device_step_boundary(path, NULL, scene, rng);
                break;
            // ... 30+ cases ...
            default:
                r.needs_ray = 0;
                r.done = 0;
                break;
        }
        
        path->phase = r.next_phase;
        path->needs_ray = r.needs_ray;
        path->ray_count = r.ray_count;
        path->done = r.done;
    }
}
```

**Warp divergence 分析**: 典型场景中，同一 warp 的 32 条路径在 cascade 阶段可能处于不同 phase。最坏情况：32 个不同 phase → 串行执行 32 次。

**缓解**: 在 compact 阶段按 `phase` 排序，使同一 warp 的路径尽量处于相同 phase（参见 [01_SCHEDULING.md](01_SCHEDULING.md) 第 4.2 节 bucketed dispatch）。

## 5. 验证策略

### 5.1 逐步替换验证

每迁移一个 Group 就执行 3σ 一致性验证：

```
1. CPU 基线: merge-phase 运行指定测试用例 → baseline_results[]
2. GPU 替换: 用 device step 替换对应 CPU step → gpu_results[]
3. 统计检验: |mean_gpu - mean_cpu| < 3σ/√N
```

### 5.2 单线程 CPU 模拟

将 `__device__` 函数编译为普通 CPU 函数（通过 `#ifdef __CUDA_ARCH__` 条件编译），在 CPU 单线程模式下运行，与原始 CPU 实现逐 step 对比 path_state。

```cpp
#ifdef __CUDA_ARCH__
  #define DEVICE_FUNC __device__
#else
  #define DEVICE_FUNC /* CPU fallback */
#endif

DEVICE_FUNC StepResult device_step_init(...) {
    // 共享实现
}
```

### 5.3 RNG 确定性

GPU 和 CPU 使用完全相同的 Threefry CBRNG（相同 key, counter），确保 RNG 序列一致。只需验证浮点计算路径因 FMA 等差异导致的偏差在统计容忍范围内。

---

*下一步*: → [03_LIBRARY_PORTING.md](03_LIBRARY_PORTING.md) (依赖库的 GPU 适配方案)
