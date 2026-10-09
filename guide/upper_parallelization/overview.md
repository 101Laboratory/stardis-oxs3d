# sdis_solve_camera → custar-3d 调用链分析与GPU并行化方案

**生成时间**: 2026-02-09  
**分析范围**: `stardis-cus3d/stardis-solver` → `stardis-cus3d/custar-3d`  
**目标**: 分析完整调用链的并行化潜力，设计利用custar-3d GPU光追后端的可行方案

---

## 一、调用链全景

```
sdis_solve_camera (CPU, OMP parallel for tiles)
 └─ solve_tile (Morton order遍历像素)
     └─ solve_pixel (SPP次realisation循环)
         └─ ray_realisation_3d (发射相机射线)
             ├─ trace_radiative_path_3d (辐射路径循环)
             │   └─ find_next_fragment (最多10次retry)
             │       └─ trace_ray() ──→ s3d_scene_view_trace_ray
             │                              └─ trace_ray_impl (Top-K, max depth=4)
             │                                  └─ cus3d_trace_ray_single_multi ★GPU★
             │
             └─ sample_coupled_path_3d (状态机驱动)
                 ├─ boundary_path → find_reinjection_ray → 2×trace_ray ★GPU★
                 ├─ conductive_path
                 │   ├─ delta_sphere: 每步2×trace_ray ★GPU★
                 │   └─ WoS: closest_point ★GPU★ + trace_ray ★GPU★
                 ├─ convective_path → 1×trace_ray ★GPU★ (启动时)
                 └─ radiative_path → trace_radiative_path_3d (递归)
```

### 关键文件位置

| 层级 | 文件 | 函数 |
|------|------|------|
| 入口 | `stardis-solver/0.16.2/src/sdis_solve_camera.c` | `sdis_solve_camera` |
| Tile/Pixel | 同上 | `solve_tile`, `solve_pixel` |
| Realisation | `sdis_realisation.c` | `ray_realisation_3d` |
| 辐射路径 | `sdis_heat_path_radiative_Xd.h` | `trace_radiative_path_Xd`, `find_next_fragment` |
| 射线封装 | `sdis_heat_path_radiative_Xd.h` L370-390 | `trace_ray` (static) |
| 耦合路径 | `sdis_realisation_Xd.h` | `sample_coupled_path_Xd` |
| 边界路径 | `sdis_heat_path_boundary_Xd.h` | `boundary_path` |
| 导热(δ-球) | `sdis_heat_path_conductive_delta_sphere_Xd.h` | `conductive_path_delta_sphere` |
| 导热(WoS) | `sdis_heat_path_conductive_wos_Xd.h` | `conductive_path_wos` |
| 对流 | `sdis_heat_path_convective_Xd.h` | `convective_path` |
| custar-3d API | `custar-3d/0.10/src/s3d_scene_view_trace_ray.cpp` | `s3d_scene_view_trace_ray` |
| GPU Top-K | 同上 | `trace_ray_impl` (Top-K + CPU filter) |
| CUDA kernel | `custar-3d/0.10/src/cus3d_trace.cu` | `cus3d_trace_ray_single_multi` |
| BVH遍历 | 同上 | `trace_rays_topk_kernel` (cuBQL) |

---

## 二、各层详细分析

### 2.1 sdis_solve_camera（最外层并行）

```c
// sdis_solve_camera.c L573-604
omp_set_num_threads((int)scn->dev->nthreads);
#pragma omp parallel for schedule(static, 1)
for(mcode = mcode_1st; mcode < (int64_t)ntiles_adjusted; mcode += mcode_incr) {
    // 每个OMP线程独立拥有一个RNG: per_thread_rng[ithread]
    // 创建tile → 加入共享链表(omp critical)
    // solve_tile → solve_pixel × (tile内像素数)
}
```

**当前并行粒度**: tile级（TILE_SIZE=4, 即4×4=16像素/tile），OMP线程池。  
**Morton order**: 使用Morton码遍历tile以提高缓存局部性。  
**共享状态**: `estimator_buffer`（通过`omp critical`写入tile链表），`ATOMIC`错误码。

### 2.2 solve_pixel（realisation循环）

```c
// sdis_solve_camera.c L87-140
FOR_EACH(irealisation, 0, nrealisations) {
    // 1. RNG采样像素内位置 (jittered sampling)
    samp[0] = ((double)ipix[0] + ssp_rng_canonical(rng)) * pix_sz[0];
    samp[1] = ((double)ipix[1] + ssp_rng_canonical(rng)) * pix_sz[1];
    
    // 2. 生成相机射线
    camera_ray(cam, samp, ray_pos, ray_dir);
    
    // 3. 执行一次完整realisation
    ray_realisation_3d(scn, &realis_args, &w);
    
    // 4. 累加温度和时间
    pixel->acc_temp.sum += w;
    pixel->acc_temp.sum2 += w*w;
    pixel->acc_temp.count += 1;
}
```

**关键观察**: 每次realisation完全独立（不同RNG序列），是最大的并行化单元。

### 2.3 ray_realisation_3d（路径入口）

```c
// sdis_realisation.c L62-110
res_T ray_realisation_3d(struct sdis_scene* scn, struct ray_realisation_args* args, double* weight) {
    // 初始化随机游走状态
    d3_set(rwalk.vtx.P, args->position);
    rwalk.vtx.time = args->time;
    rwalk.enc_id = args->enc_id;
    
    // Phase 1: 沿相机射线追踪辐射路径
    res = trace_radiative_path_3d(scn, dir, &ctx, &rwalk, args->rng, &T);
    
    // Phase 2: 如果未结束，进入耦合路径（边界/导热/对流循环）
    if(!T.done) {
        res = sample_coupled_path_3d(scn, &ctx, &rwalk, args->rng, &T);
    }
    
    *weight = T.value;  // 最终温度贡献值
}
```

### 2.4 trace_radiative_path_3d（辐射路径 - 射线密集）

```c
// sdis_heat_path_radiative_Xd.h L197-310
// 无限循环直到: 1)到达辐射环境 2)被表面吸收(emissivity采样)
for(;;) {
    // 查找下一个界面交点
    res = find_next_fragment(scn, pos, dir, &rwalk->hit, ...);
    
    if(HIT_NONE) {
        // 到达辐射环境 → 读取环境温度 → T.done=1
        set_limit_radiative_temperature(...);
        break;
    }
    
    // 到达界面 → BRDF采样
    if(rng < brdf.emissivity) {
        // 被吸收 → 转入 boundary_path
        T->func = boundary_path;
        break;
    }
    
    // 反射 → BRDF采样新方向
    brdf_sample(&brdf, rng, wi, N, &bounce);
    d3_set(dir, bounce.dir);
    ++nbounces;
}
```

**每个辐射bounce = 1次find_next_fragment = 1+次trace_ray**

### 2.5 find_next_fragment（射线追踪 + retry）

```c
// sdis_heat_path_radiative_Xd.h L400-490
do {
    // 调用trace_ray（封装s3d_scene_view_trace_ray）
    trace_ray(scn, rt_pos, in_dir, INF, enc_id, in_hit, &hit);
    
    if(HIT_NONE) break;
    
    // 获取interface, 检查valid
    interf = scene_get_interface(scn, hit.prim.prim_id);
    res = check_interface(interf, &frag, ...);
    
    // 如果interface无效（数值问题），微调位置重试
    if(res != RES_OK) {
        move_away_primitive_boundaries(in_hit, delta, rt_pos);
    }
} while(res != RES_OK);  // 最多10次
```

### 2.6 trace_ray（射线封装层）

```c
// sdis_heat_path_radiative_Xd.h L370-392
void trace_ray(scn, pos, dir, distance, enc_id, hit_from, hit) {
    struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
    filter_data.hit_3d = *hit_from;     // 自交避免: 排除起始面
    filter_data.epsilon = 1.e-6;        // 距离阈值
    filter_data.scn = scn;              // enclosure过滤
    filter_data.enc_id = enc_id;
    
    s3d_scene_view_trace_ray(scn->s3d_view, ray_org, ray_dir, ray_range, &filter_data, hit);
}
```

**filter_data结构**（定义在`sdis_scene_c.h`）:
```c
struct hit_filter_data {
    struct s3d_hit hit_3d;   // 上一次hit（用于自交避免）
    double epsilon;          // 距离阈值
    struct sdis_scene* scn;  // 场景（用于enclosure查询）
    unsigned enc_id;         // 当前enclosure ID
    s3d_hit_filter_function_T custom_filter_3d;
    void* custom_filter_data;
};
```

### 2.7 s3d_scene_view_trace_ray（custar-3d API层）

```cpp
// custar-3d/0.10/src/s3d_scene_view_trace_ray.cpp L100-190
static res_T trace_ray_impl(scnview, org, dir, range, ray_data, hit, depth) {
    // 一次GPU调用: 获取Top-K个最近候选
    cus3d_trace_ray_single_multi(bvh, store, dev, org, dir, range, TOPK_COUNT=4, &multi);
    
    // CPU侧遍历候选, 应用filter
    for(i = 0; i < multi.count; i++) {
        cus3d_hit_to_s3d_hit(store, bvh, &multi.hits[i], hit);
        trace_hit_fixup(hit, ge);  // UV/法线约定转换
        
        if(ge->filter_func) {
            int rejected = ge->filter_func(hit, org, dir, range, ray_data, ge->filter_data);
            if(rejected) continue;
        }
        return RES_OK;  // Hit accepted
    }
    
    // 所有K个候选都被reject → 递归fallback(推进range)
    if(multi.count == TOPK_COUNT) {
        fallback_range[0] = multi.hits[K-1].distance + 1e-6f;
        return trace_ray_impl(scnview, org, dir, fallback_range, ray_data, hit, depth+1);
    }
    *hit = S3D_HIT_NULL;
}
```

### 2.8 cus3d_trace_ray_single_multi（GPU kernel启动 - 性能瓶颈）

```cuda
// cus3d_trace.cu L1052-1228
res_T cus3d_trace_ray_single_multi(bvh, store, dev, origin, direction, range, max_hits, result) {
    // ❌ 每次调用: 4次cudaMallocAsync + 3次cudaMemcpyAsync
    cudaMallocAsync(&d_org, sizeof(float3), s);
    cudaMallocAsync(&d_dir, sizeof(float3), s);
    cudaMallocAsync(&d_rng, sizeof(float2), s);
    cudaMallocAsync(&d_res, sizeof(cus3d_multi_hit_result), s);
    
    cudaMemcpyAsync(d_org, &h_org, ...);
    cudaMemcpyAsync(d_dir, &h_dir, ...);
    cudaMemcpyAsync(d_rng, &h_rng, ...);
    
    // ❌ kernel<<<1, 1>>>  — 只用了1个GPU线程!
    trace_rays_topk_kernel<<<1, 1, 0, s>>>(...);
    
    cudaStreamSynchronize(s);     // ❌ 阻塞等待
    
    cudaMemcpyAsync(result, d_res, ...);  // 下载结果
    cudaStreamSynchronize(s);
    
    // ❌ 4次cudaFreeAsync
    cudaFreeAsync(d_org/d_dir/d_rng/d_res, s);
}
```

**单次调用开销估算**: ~50-200μs (kernel启动+同步+内存管理), 而实际BVH遍历仅需 ~1-5μs。

### 2.9 已有的Batch API

```c
// cus3d_trace.h
res_T cus3d_trace_ray_batch(
    const struct cus3d_bvh* bvh,
    const struct cus3d_geom_store* store,
    struct cus3d_device* dev,
    const struct cus3d_ray_batch* rays,    // 预分配的GPU缓冲区
    struct cus3d_hit_result* h_results);   // Batch mode, block_size=256
```

`cus3d_trace_ray_batch` 已经实现了批量化 — kernel<<<grid_size, 256>>>，但**stardis-solver从未使用它**，因为路径是逐射线串行推进的。

---

## 三、射线调用点汇总

以下是stardis-solver中所有调用`s3d_scene_view_trace_ray`的位置：

| 调用位置 | 文件 | 每路径步频率 | 说明 |
|---------|------|------------|------|
| `trace_ray()` in radiative path | `sdis_heat_path_radiative_Xd.h:385-388` | 每bounce 1次 | 通过`find_next_fragment`封装 |
| `sample_next_step_robust` (delta_sphere) | `sdis_heat_path_conductive_delta_sphere_Xd.h:67-68` | 每步 **2次** | 正反方向估算delta |
| `find_reinjection_ray` (boundary) | `sdis_heat_path_boundary_Xd_c.h:307-309` | 每boundary步 **2次** | 正反向查找reinjection |
| `handle_convective_path_startup` | `sdis_heat_path_convective_Xd.h:126` | 每convective path **1次** | 初始化hit |
| `conductive_path_wos` fallback | `sdis_heat_path_conductive_wos_Xd.h:566` | 偶尔 1次 | WoS数值修正 |
| `scene_get_enclosure_id` | `sdis_scene_Xd.h:1270,1352` | 场景初始化 | enclosure归属检测 |
| `solve_medium` | `sdis_solve_medium_Xd.h:260` | 介质求解 | 单独求解器入口 |

另有 `s3d_scene_view_closest_point` 调用:

| 调用位置 | 文件 | 频率 |
|---------|------|------|
| WoS每步 | `sdis_heat_path_conductive_wos_Xd.h:339,510` | 每WoS步 1次 |
| Custom solid path | `sdis_heat_path_conductive_custom_Xd.h:118` | 自定义路径 |
| enclosure_id检测 | `sdis_scene_Xd.h:1168,1176` | 场景初始化 |

---

## 四、关键性能瓶颈

| 瓶颈 | 原因 | 严重程度 |
|------|------|---------|
| **逐射线GPU kernel启动** | `cus3d_trace_ray_single_multi` 每次调用: cudaMalloc×4 + cudaMemcpy×3 + kernel<<<1,1>>> + cudaSync + cudaFree×4 | **致命** |
| **深度数据依赖** | 每步射线结果决定下一步方向（BRDF反射、随机游走snap） | **结构性** |
| **路径长度不确定** | 蒙特卡洛路径在 radiative↔boundary↔conductive/convective 间状态转换，步数不可预知 | 高 |
| **CPU filter回调** | `trace_ray_impl` 中 `ge->filter_func` 在CPU侧执行，决定是否接受hit | 中 |

---

## 五、并行化潜力分析

### 5.1 像素/Realisation级并行（最高潜力 ★★★★★）

**当前**: OMP线程间并行（~8-16线程），每线程串行执行一条完整路径。

**潜力**: `image_width × image_height × SPP` 条独立路径可以**全部并行**。  
- 典型场景: 512×512×64 = **1600万条独立路径**  
- 每条路径内的射线虽然有数据依赖，但**不同路径之间完全独立**  
- GPU RTX 4090有16384 CUDA cores，理想并发度远超CPU

### 5.2 路径内射线批量化（中等潜力 ★★★）

- `delta_sphere`: 每步采样2个方向（dir0, dir1）→ 可合并为1次batch trace
- `find_reinjection_ray`: 2个方向 → 同上
- `find_next_fragment` 的retry循环：串行依赖，无法批量化

### 5.3 Filter上移GPU（中等潜力 ★★★）

当前CPU端filter (`hit_filter_data`) 的逻辑：
1. **自交避免**: 检查 `hit.prim_id == from_hit.prim_id` 且距离 < epsilon → 跳过
2. **Enclosure过滤**: 检查hit的primitive是否属于当前enclosure

这两个逻辑都可以编码为GPU kernel中的条件判断：
- 将 `prim_prop[]`（front/back enclosure mapping）上传为GPU constant memory
- 在 BVH traversal lambda 中直接reject不符合条件的hit

这将消除 Top-K + CPU-side filter 的整个架构，回归到 single-nearest-hit 高效模式。

---

## 六、可行方案设计

### 方案: Persistent Megakernel + Wavefront Path Queue

这是唯一能同时解决"逐射线kernel启动开销"和"路径间并行"两大问题的方案。

```
Phase 0: 初始化
  - 在GPU上预分配所有路径状态 (path_state[N])
  - 初始化cuRAND per-path RNG states
  - 将场景数据(interface表、medium属性、BRDF参数等)全部上传GPU

Phase 1: Generate Camera Rays (GPU kernel)
  - 1个thread = 1条path
  - 生成camera ray, 写入path_state[tid]

Phase 2: Wavefront Loop (CPU调度, GPU执行)
  while(active_paths > 0):
    
    2a. Trace Rays (GPU batch)
       - 从所有active path中收集射线请求
       - 一次cus3d_trace_ray_batch() 发射所有射线
       - block_size=256, 充分利用GPU SM
    
    2b. Process Hits + State Transition (GPU kernel)
       - 每个thread处理一条path的hit result
       - 应用UV fixup + filter (上移到GPU)
       - 执行状态转换逻辑:
         * radiative_path: BRDF采样→reflect/emit→产生新ray / done
         * boundary_path: 查温度→known=结算 / unknown=进入solid/fluid
         * conductive_path: delta_sphere=sample 2 dirs→产生2条ray
         * convective_path: sample enclosure→产生1条ray
       - 输出: new_ray_request[] 或 path_done[]
    
    2c. Compact (压缩活跃路径)
       - 使用GPU prefix-sum移除已完成的路径
       - 维持wavefront宽度 > SM×warp以保持GPU占用率

Phase 3: Reduce (GPU kernel)
  - 累加每个像素的 acc_temp, acc_time → estimator_buffer
```

### 具体GPU数据结构

```c
struct gpu_path_state {
    // 路径状态
    double   position[3];
    double   direction[3];
    double   time;
    float    ray_dir[3];        // 当前待trace方向
    
    // 随机游走状态  
    struct s3d_hit  hit;
    unsigned enc_id;
    int      hit_side;
    
    // 温度累加
    double   T_value;
    int      T_done;
    
    // 路径类型状态机
    enum {
        STATE_RADIATIVE,
        STATE_BOUNDARY,
        STATE_CONDUCTIVE,
        STATE_CONVECTIVE,
        STATE_DONE
    } state;
    
    // 蒙特卡洛上下文
    int      nbranchings;
    int      max_branchings;
    double   Tmin, That;
    
    // 输出目标
    uint32_t pixel_x, pixel_y;
    uint32_t realisation_id;
    
    // RNG
    curandState rng;
};
```

### 场景数据GPU映射

```c
struct gpu_scene_data {
    // 几何 (已在custar-3d中)
    struct cus3d_bvh*        bvh;
    struct cus3d_geom_store* geom_store;
    
    // 物理属性 (需新增上传)
    struct gpu_interface*    interfaces;      // prim_id → interface属性
    struct gpu_medium*       media;           // medium_id → 热物性
    struct gpu_prim_prop*    prim_props;      // prim_id → front/back enclosure
    struct gpu_enclosure*    enclosures;      // enclosure_id → medium/属性
    struct gpu_brdf*         brdfs;           // interface → BRDF参数
    
    // 辐射环境
    struct gpu_radiative_env* radenv;
    
    // 全局参数
    double   fp_to_meter;
    double   tmin, tmax;
};
```

---

## 七、分阶段迁移路线

| 阶段 | 范围 | 加速效果 | 工作量 | 验证方式 |
|------|------|---------|--------|---------|
| **阶段1**: Trace批量化 | 保持CPU主循环，将 `s3d_scene_view_trace_ray` 改为异步提交 + batch执行 | 10-50× (消除kernel启动开销) | 中 | 逐像素对比CPU结果 |
| **阶段2**: Radiative path GPU化 | `trace_radiative_path_3d` 整体迁移为GPU wavefront kernel | 100×+ (路径间并行) | 大 | 统计温度分布对比 |
| **阶段3**: Coupled path GPU化 | boundary/conductive/convective 全部上GPU | 200×+ (完全GPU pipeline) | 非常大 | 端到端结果对比 |

### 阶段1: Trace批量化（推荐首先实施）

**核心思路**: 改造 `s3d_scene_view_trace_ray` 为**延迟提交 + 批量执行**模式。

**新增接口**:
```c
struct s3d_ray_queue;

// 创建射线队列
res_T s3d_ray_queue_create(struct s3d_ray_queue** q, size_t capacity);

// 提交一条射线（不立即执行）
res_T s3d_ray_queue_submit(struct s3d_ray_queue* q,
    const float org[3], const float dir[3], const float range[2],
    void* ray_data, uint32_t path_id);

// 批量执行所有已提交的射线
res_T s3d_ray_queue_flush(struct s3d_ray_queue* q,
    struct s3d_scene_view* view,
    struct s3d_hit* hits);  // hits[n] for n submitted rays

void s3d_ray_queue_destroy(struct s3d_ray_queue* q);
```

**改造点**:
- 在 `solve_tile` 层面收集一个tile内所有像素的射线请求
- TILE_SIZE=4 → 16像素 × SPP × 每path平均~5条射线 ≈ 数千条射线/batch
- 已有 `cus3d_trace_ray_batch` 支持 block_size=256 的批量kernel

**关键约束**: 由于路径内步间有数据依赖，阶段1只能在同一步骤内批量化不同路径的射线，无法prefetch。但仅消除per-ray的 cudaMalloc/cudaFree/cudaSync 就能有巨大提升。

### 阶段1补充: Filter上移GPU

将CPU端Filter逻辑编码进GPU kernel，消除Top-K架构：

```cuda
// 修改trace_rays_kernel中的intersect lambda
auto intersect_prim = [&](uint32_t primID) -> float {
    uint32_t geom_idx = prim_to_geom[primID];
    
    // 新增: enclosure过滤
    uint32_t front_enc = d_prim_props[primID].front_enclosure;
    uint32_t back_enc  = d_prim_props[primID].back_enclosure;
    if(filter_enc_id != ENCLOSURE_ID_NULL
    && front_enc != filter_enc_id
    && back_enc  != filter_enc_id) {
        return ray.tMax;  // Skip this primitive
    }
    
    // 新增: 自交避免
    if(primID == filter_from_prim_id && t < filter_epsilon) {
        return ray.tMax;  // Skip self-intersection
    }
    
    // ... 原有三角形/球体求交 ...
};
```

---

## 八、风险与约束

1. **RNG一致性**: GPU cuRAND 与 CPU `ssp_rng` 序列不同。阶段1（批量化）不影响RNG，可逐像素验证。阶段2-3需要建立新的统计基准而非逐像素对比。

2. **Scene数据上传**: `sdis_scene` 中的 interface/medium/BRDF 属性表需要完整映射到GPU constant/global memory。interface数量通常为O(千)，数据量可控。

3. **动态分支**: coupled path状态机的深度递归（`sample_coupled_path` → `boundary_path` → `conductive_path` → ...）在GPU上需要转换为迭代式状态机。最大递归深度=picard_order（通常1-3），可用展开循环。

4. **双精度**: 路径位置/温度使用double，射线追踪使用float。当前混合精度模式需要保持。RTX 4090 FP64吞吐量为FP32的1/64，温度累加在GPU上用double仍可接受。

5. **内存占用**: 每路径状态约~200 bytes，100万并发路径需200MB GPU内存。RTX 4090有24GB，充足。

6. **Coupled path移植复杂度**: boundary/conductive/convective path涉及大量查表（interface属性、medium热物性、BRDF参数），移植到GPU kernel需要完整映射这些数据结构。

---

## 九、现有cus3d_trace_ray_batch使用建议

当前 `cus3d_trace_ray_batch` 的batch API已经完备：
- 支持预分配的GPU缓冲区（`cus3d_ray_batch_create`）
- 使用 `block_size=256` 的proper GPU kernel
- 支持instanced和non-instanced场景

**未被使用的原因**: stardis-solver的路径追踪是逐射线串行推进的，没有"同时需要追踪多条射线"的场景点。

**阶段1的关键改动**: 在OMP层收集多条路径的当前步射线请求，统一提交batch。这需要将路径推进改为"lockstep wavefront"模式——所有活跃路径同步推进一步，而非每条路径独立跑完。

---

*文档更新: 2026-02-09 | 基于 stardis-cus3d 代码分析*
