# 利用现有 `cus3d_trace_ray_batch` 的并行化可行性分析

**生成时间**: 2026-02-09  
**前置文档**: [overview.md](overview.md)（完整调用链分析）  
**核心问题**: 在最小结构改动下，现有batch API能提供多大程度的并行化优化？

---

## 一、现状：逐射线GPU调用的真实开销

### 1.1 `cus3d_trace_ray_single_multi` 每次调用的固定开销

```
cus3d_trace_ray_single_multi() 内部流程:
  cudaMallocAsync(&d_org)        ─┐
  cudaMallocAsync(&d_dir)         │  ~20μs
  cudaMallocAsync(&d_rng)         │
  cudaMallocAsync(&d_res)        ─┘
  cudaMemcpyAsync(d_org, H→D)   ─┐
  cudaMemcpyAsync(d_dir, H→D)    │  ~15μs
  cudaMemcpyAsync(d_rng, H→D)   ─┘
  kernel<<<1, 1>>>()              │  ~5-10μs  ← 只用了1个GPU线程
  cudaStreamSynchronize()         │  ~5μs
  cudaMemcpyAsync(result, D→H)   │  ~5μs
  cudaStreamSynchronize()         │  ~5μs
  cudaFreeAsync() × 4            ─┘  ~20μs
  ─────────────────────────────────
  总固定开销:                        ~75-100μs
  实际BVH遍历计算:                   ~1-5μs
```

**95%以上的时间花在启动/同步/内存管理上，而非计算。**

### 1.2 GPU stream竞争

所有OMP线程共享一个 `cus3d_device`，该设备只有**一条compute stream**（`cus3d_device.stream`）。这意味着来自不同OMP线程的 kernel<<<1,1>>> 调用在GPU上**串行排队**：

```
OMP Thread 0: submit kernel #1 ──┐
OMP Thread 1: submit kernel #2 ──┤── 同一 CUDA stream
OMP Thread 0: submit kernel #3 ──┤   → GPU串行执行
OMP Thread 1: submit kernel #4 ──┘
```

当前架构下，增加OMP线程数**不会提高GPU利用率**，反而增加stream竞争的同步开销。

### 1.3 已有的Batch API

`cus3d_trace_ray_batch` 已完备但**从未被上层使用**：

```c
// cus3d_trace.h
struct cus3d_ray_batch {
    struct gpu_buffer_float3   d_origins;      // 预分配GPU缓冲区
    struct gpu_buffer_float3   d_directions;
    struct gpu_buffer_float2   d_ranges;
    struct gpu_buffer_uint32   d_ray_data_offsets;
    size_t                     count;
};

res_T cus3d_ray_batch_create(struct cus3d_ray_batch* batch, size_t max_rays);
void  cus3d_ray_batch_destroy(struct cus3d_ray_batch* batch);

res_T cus3d_trace_ray_batch(
    const struct cus3d_bvh* bvh,
    const struct cus3d_geom_store* store,
    struct cus3d_device* dev,
    const struct cus3d_ray_batch* rays,
    struct cus3d_hit_result* h_results);
```

Batch API使用 `<<<grid_size, 256>>>` 启动kernel，能正确利用GPU并行度。

### 1.4 `s3d_scene_view_trace_rays`（复数）是假batch

```cpp
// s3d_scene_view_trace_ray.cpp L235-270
res_T s3d_scene_view_trace_rays(scnview, nrays, mask, origins, ..., hits) {
    FOR_EACH(iray, 0, nrays) {
        // 逐条调用单射线版本 ← 完全没有使用batch API！
        res = s3d_scene_view_trace_ray(scnview, origins+iorg, ...);
    }
}
```

### 1.5 `s3d_scene_view_closest_point` — 纯CPU暴力搜索

WoS算法依赖的 `closest_point` 完全在CPU上执行，逐三角形暴力距离计算，无BVH加速，无GPU。这是WoS路径的额外性能问题，但不在本文讨论范围内。

---

## 二、射线调用点分类

### 2.1 按filter需求分类

**无filter调用（`ray_data=NULL`）— 可直接使用batch API：**

| 调用点 | 文件:行 | 每次条数 | 频率 |
|--------|---------|---------|------|
| delta_sphere 正反探测 | `sdis_heat_path_conductive_delta_sphere_Xd.h:67-68` | **2条** | 每导热步1次 |
| convective startup | `sdis_heat_path_convective_Xd.h:126` | 1条 | 每对流路径1次 |
| WoS fallback | `sdis_heat_path_conductive_wos_Xd.h:566` | 1条 | 偶发 |
| enclosure探测 | `sdis_scene_Xd.h:1270,1352` | 1-6条 | 场景初始化 |
| solve_medium | `sdis_solve_medium_Xd.h:260` | 1条 | 介质求解 |

**有filter调用（传入`filter_data`）— 不能直接使用batch API：**

| 调用点 | 文件 | 每次条数 | 频率 | filter内容 |
|--------|------|---------|------|-----------|
| find_next_fragment (辐射路径) | `sdis_heat_path_radiative_Xd.h:385-388` | 1条 | 每辐射bounce 1次 | 自交避免 + enclosure |
| find_reinjection_ray (边界) | `sdis_heat_path_boundary_Xd_c.h:307-309` | **2条** | 每boundary步 | 自交避免 + epsilon |
| WoS closest-point相关 | `sdis_heat_path_conductive_wos_Xd.h:311` | 1条 | 每WoS步 | 自交避免 |
| custom solid path | `sdis_heat_path_conductive_custom_Xd.h:101` | 1条 | 自定义 | 自交避免 |

### 2.2 Filter逻辑分析

`hit_filter_function`（定义在 `sdis_scene_Xd.h:477-540`）的完整逻辑：

```c
static int hit_filter_function(hit, org, dir, range, query_data, global_data) {
    filter_data = query_data;
    if(!filter_data) return 0;              // 无filter → 接受

    // 1. 自交避免: 同一primitive → 拒绝
    if(PRIMITIVE_EQ(&hit_from->prim, &hit->prim)) return 1;

    // 2. 零距离 → 拒绝
    if(hit->distance <= 0) return 1;

    // 3. 极近距离: 检查是否在共享边上
    if(eq_epsf(hit->distance, 0, filter_data->epsilon)) {
        reject = hit_shared_edge(...);
        if(reject) return 1;
    }

    // 4. Enclosure过滤: 命中点的enclosure必须匹配
    if(filter_data->scn && HIT_ON_BOUNDARY(hit, org, dir)) {
        scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);
        chk_enc_id = dot(dir, hit->normal) < 0 ? enc_ids[FRONT] : enc_ids[BACK];
        if(chk_enc_id != filter_data->enc_id) return 1;
    }
    return 0;  // 接受
}
```

filter逻辑的特点：
- **步骤1-3（自交避免）** — 仅依赖 `from_hit.prim_id` 和距离，可轻易上移GPU
- **步骤4（enclosure过滤）** — 需要 `prim_prop[]` 表查询，可上传为GPU constant/global memory

---

## 三、三个批量化层级

### 层级A: 调用点微批量（2-ray batch）— 改动最小

**原理**: 仅在同一函数内连续发出的2条射线合并为1次batch调用。

**可改造位置** — 仅限 `ray_data=NULL` 的调用点：

```c
// 当前 delta_sphere: 2次独立GPU kernel启动
SXD(scene_view_trace_ray(view, pos, dirs[0], range, NULL, &hits[0]));  // kernel<<<1,1>>>
SXD(scene_view_trace_ray(view, pos, dirs[1], range, NULL, &hits[1]));  // kernel<<<1,1>>>

// 改为: 1次batch调用
s3d_scene_view_trace_ray_pair(view, pos, dirs[0], dirs[1], range, &hits[0], &hits[1]);
// 内部: 1次cudaMalloc + 1次kernel<<<1, 256>>> + 1次sync
```

**预期收益**:
- 每次调用节省 ~75-100μs（省掉1次完整kernel启动周期）
- 但 delta_sphere 每路径仅5-50步，且不影响辐射路径（射线最密集的阶段）

**有filter的调用点无法使用**: `find_next_fragment` 和 `find_reinjection_ray` 使用了Top-K + CPU filter机制，`cus3d_trace_ray_batch` 仅返回nearest hit，无法替代。

**端到端加速估计: ~1.2-1.5×**

**结论**: **收益太小，不值得单独做。** Batch size=2，GPU利用率仍然极低（<1%）。

---

### 层级B: 跨路径同步批量（Tile Wavefront）— 中等改动 ★推荐★

**原理**: 将同一tile内所有活跃路径在同一"步"中需要的射线收集起来，一次batch dispatch到GPU。

#### 可获得的Batch Size

| 参数 | 值 | 说明 |
|------|-----|------|
| Tile内像素 | 16 | TILE_SIZE=4, 即4×4 |
| 每像素SPP | 64-256 | 典型配置 |
| **初始并发路径** | **1024-4096** | 每步产生1-2条射线 |
| 路径存活衰减 | 指数衰减 | 平均~10步路径 |
| **平均batch size** | **300-1500条射线** | 中后期活跃路径减少 |

实际上可以超越单个tile：将多个tile的射线收集到同一batch中。

#### 加速效果量化对比

| Batch大小 | kernel启动模式 | 固定开销分摊/ray | 对比单射线 | GPU利用率 |
|-----------|---------------|-----------------|-----------|----------|
| 1（当前） | <<<1, 1>>> | 85μs | 1× | <0.1% |
| 2（层级A） | <<<1, 1>>> | 42μs | 2× | <0.1% |
| 256 | <<<1, 256>>> | 0.33μs | **~250×** | ~3% |
| 1024 | <<<4, 256>>> | 0.08μs | **~1000×** | ~12% |
| 4096 | <<<16, 256>>> | 0.02μs | **~4000×** | ~50% |
| 16384 | <<<64, 256>>> | <0.01μs | **~10000×** | ~95% |

#### 需要的架构改造

当前 `solve_tile` 是 "run each pixel to completion" 模式：

```c
// 当前架构
FOR_EACH(pixel_mcode, 0, npixels) {
    FOR_EACH(irealisation, 0, spp) {
        ray_realisation_3d(scn, &args, &w);  // 从头到尾串行跑完一条路径
        // 路径内部: trace_ray → kernel<<<1,1>>> → sync → process → trace_ray → ...
    }
}
```

改为 **lockstep wavefront** 模式：

```c
// 改造后架构
struct path_state paths[TILE_PIXELS * SPP];  // 所有路径的外化状态
init_all_paths(paths, cam, pix_sz, ...);

while(any_active(paths)) {
    // Step 1: 收集所有活跃路径的射线请求
    n = collect_ray_requests(paths, ray_batch);

    // Step 2: 一次batch trace
    s3d_scene_view_trace_rays_batch(view, ray_batch, n, hits);  // kernel<<<n/256, 256>>>

    // Step 3: 每条路径处理hit结果，推进一步
    for_each_active_path(paths) {
        advance_path_one_step(path, hit);   // 状态机: radiative/boundary/conductive/convective
        // 如果需要新射线 → 标记为active, 下一轮collect
        // 如果路径结束 → 标记为done, 累加结果
    }

    // Step 4: 压缩(可选)
    compact_active_paths(paths);
}
```

#### 核心改造难点：路径状态外化

当前路径采样是通过嵌套函数调用实现的，栈上保存中间状态：

```
ray_realisation_3d()
  └─ trace_radiative_path_3d()     ← 循环，栈上保存 dir, N, brdf
       └─ find_next_fragment()      ← 最多10次retry
  └─ sample_coupled_path_3d()       ← 状态机循环
       └─ T->func() 调用:
           ├─ boundary_path()        ← 栈上保存 interf, frag
           ├─ conductive_path()      ← 栈上保存 props, delta, dir
           └─ convective_path()      ← 栈上保存 enc, props
```

需要将所有这些栈上状态提取到一个 `struct path_state` 中：

```c
struct path_state {
    // 随机游走核心状态
    double   position[3];
    double   time;
    struct s3d_hit  hit_3d;
    unsigned enc_id;
    int      hit_side;

    // 温度累加
    double   T_value;
    int      T_done;

    // 路径类型状态机
    enum path_phase {
        PHASE_RADIATIVE_TRACE,      // 辐射路径: 等待trace结果
        PHASE_RADIATIVE_BRDF,       // 辐射路径: 已获hit, 待BRDF采样
        PHASE_BOUNDARY,             // 边界路径: 查温度/决定下一步
        PHASE_CONDUCTIVE_SAMPLE,    // 导热: 等待delta_sphere的2条射线结果
        PHASE_CONDUCTIVE_REWIND,    // 导热: 时间回退
        PHASE_CONVECTIVE,           // 对流: 采样enclosure
        PHASE_DONE                  // 完成
    } phase;

    // 射线请求输出
    float    ray_org[3];
    float    ray_dir[3];
    float    ray_range[2];
    void*    ray_filter_data;       // NULL 或 filter_data
    int      needs_ray;             // 本步是否需要射线追踪
    int      ray_count;             // 本步需要的射线数（1或2）
    float    ray_dir2[3];           // 第二条射线方向（delta_sphere用）

    // 蒙特卡洛上下文
    int      nbranchings;
    int      max_branchings;
    struct ssp_rng* rng;

    // 输出定位
    uint32_t pixel_x, pixel_y;
};
```

#### 对filter问题的解决

**策略: CPU后处理 + 选择性re-trace**

```c
// batch trace N rays → 得到 N 个 nearest hit (cus3d_hit_result)
cus3d_trace_ray_batch(bvh, store, dev, &ray_batch, hit_results);

// CPU逐条后处理
for(i = 0; i < N; i++) {
    // 1. UV/法线fixup
    trace_hit_fixup(&s3d_hits[i], ge);

    // 2. 检查filter
    if(paths[i].ray_filter_data) {
        int rejected = hit_filter_function(&s3d_hits[i], ..., paths[i].ray_filter_data, ...);
        if(rejected) {
            // 被reject → 回退到单射线Top-K re-trace
            s3d_scene_view_trace_ray(view, org, dir, range, filter_data, &s3d_hits[i]);
            retrace_count++;
        }
    }
}
```

**预期re-trace比例**: 根据filter逻辑分析，大多数射线（>90%）不会触发filter reject（只有从表面出发的射线才需要自交避免）。因此fallback的性能影响有限。

随着后续优化（filter上移GPU），retrace可以完全消除。

---

### 层级C: GPU全流水线 — 大改动

路径状态完全在GPU上（CUDA global memory），CPU仅调度wavefront循环。

与层级B的区别：
- 步骤3的"advance one step"也在GPU kernel中执行
- 需要将 scene data（interface表、medium属性、BRDF参数）全部上传GPU
- 路径推进kernel + trace kernel 交替执行，无CPU-GPU数据往返

**batch size: 10k-1M**，GPU利用率接近100%。

---

## 四、`cus3d_trace_ray_batch` 直接使用的Gap分析

| 需求 | `cus3d_trace_ray_batch` 现状 | 缺口 | 解决方案 |
|------|---------------------------|------|---------|
| UV/法线约定转换 | ❌ 返回原始M-T坐标 | 需逐条fixup | CPU后处理（简单） |
| 自交避免filter | ❌ 无filter支持 | nearest hit可能被reject | CPU后处理 + 选择性re-trace |
| Enclosure过滤 | ❌ 无enclosure概念 | 同上 | 同上，或上传prim_prop到GPU |
| Top-K多命中 | ❌ 仅nearest hit | 被reject时需re-trace | 用现有single_multi fallback |
| `s3d_hit`结构输出 | ❌ 输出`cus3d_hit_result` | 需要类型转换 | 简单映射（已有`cus3d_hit_to_s3d_hit`） |
| 预分配GPU缓冲区 | ✅ `cus3d_ray_batch_create` | — | 直接使用 |
| 批量kernel启动 | ✅ `<<<grid, 256>>>` | — | 直接使用 |
| 混合有filter/无filter射线 | ❌ 全batch统一处理 | 需区分处理 | batch内射线标记哪些需post-filter |

---

## 五、量化结论

| 层级 | Batch Size | 射线阶段加速 | 端到端加速 | 结构改动量 | 新代码量 |
|------|-----------|------------|-----------|----------|---------|
| **当前** | 1 | 1× | 1× | — | — |
| **A: 微批量** | 2 | ~2× (仅delta/boundary) | **~1.3×** | 极小 | ~200行 |
| **B: Tile wavefront** | 300-1500 | ~300-1500× | **~10-50×** | 中等 | ~1500行 |
| **C: GPU全流水线** | 10k-1M | ~10000×+ | **~100-500×** | 大 | ~5000行 |

**层级A**收益太小，不值得单独做。

**层级B**是投入产出比最高的方案 — 利用已有的 `cus3d_trace_ray_batch` 基础设施，通过CPU侧的wavefront调度获得数量级加速。

**层级C**是终极方案但需要将整个求解器的物理逻辑移植到GPU。

---

## 六、推荐实施策略

### Phase B-1: custar-3d层 — 新增batch trace封装（~500行）

在custar-3d中新增一个真正的batch trace API，包裹底层 `cus3d_trace_ray_batch` 并补上缺失的后处理：

```c
// 新增 s3d_scene_view_trace_ray_batch.cpp

struct s3d_ray_request {
    float    org[3];
    float    dir[3];
    float    range[2];
    void*    filter_data;    // NULL = 无filter
    uint32_t user_id;        // 调用者标识
};

res_T s3d_scene_view_trace_rays_batch(
    struct s3d_scene_view* view,
    const struct s3d_ray_request* requests,
    size_t nrays,
    struct s3d_hit* out_hits);   // 输出: 经过fixup和filter的s3d_hit
```

内部实现：
1. 将 `s3d_ray_request[]` 填入预分配的 `cus3d_ray_batch`
2. 调用 `cus3d_trace_ray_batch` → `cus3d_hit_result[]`
3. 逐条: `cus3d_hit_to_s3d_hit` + `trace_hit_fixup`
4. 逐条检查filter: 通过→输出; reject→用 `cus3d_trace_ray_single_multi` 单独re-trace

**可独立测试**: 用现有 `test_s3d_trace_ray.c` 验证batch结果与逐条结果一致。

### Phase B-2: stardis-solver层 — solve_tile wavefront化（~1000行）

1. 定义 `struct path_state` — 外化 `ray_realisation_3d` 的全部中间状态
2. 将 `solve_tile` 从串行pixel循环改为wavefront步进
3. 各path函数改为"执行一步并返回射线请求"模式
4. 使用 Phase B-1 的batch API发射射线

**验证**: 对比wavefront模式与原始串行模式的像素温度（相同RNG序列，结果应bit-exact）。

### Phase B-1与B-2的解耦

Phase B-1 完全在custar-3d内部，不影响stardis-solver。可以先完成B-1并独立验证，再进行B-2。

如果B-2的wavefront改造工作量过大，B-1本身也可以被层级A使用（delta_sphere的2-ray batch），获得小幅但确定的提升。

---

*文档更新: 2026-02-09 | 基于 stardis-cus3d 代码实证分析*
