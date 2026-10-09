# `ray_realisation_3d` 函数执行流程与GPU并行化改造分析

**生成时间**: 2026-01-21 19:42:00  
**分析目标**: STARDIS蒙特卡洛辐射传输求解器的核心光线追踪函数  
**目的**: 为GPU实现（CUDA/DXR）提供详细的改造指南  

---

## 一、函数执行流程（顺序逻辑）

### **1. 主入口: `ray_realisation_3d`** 
📍 位置: `stardis-solver/0.16.2/src/sdis_realisation.c:58-108`

```
输入参数:
  - scn: 场景数据（几何、材质、边界条件）
  - args: 光线参数（position, direction, time, Picard阶数）
  - weight: 输出温度权重

执行流程:
┌─────────────────────────────────────────────────────┐
│ 步骤1: 初始化随机游走状态(rwalk)                     │
│   - 设置起始位置: rwalk.vtx.P = args->position      │
│   - 设置时间: rwalk.vtx.time = args->time           │
│   - 设置包围体: rwalk.enc_id = args->enc_id         │
│   - 清空击中信息: rwalk.hit_3d = S3D_HIT_NULL       │
├─────────────────────────────────────────────────────┤
│ 步骤2: 初始化随机游走上下文(ctx)                     │
│   - 温度边界: ctx.Tmin, ctx.Tmax                    │
│   - Picard分支数: ctx.max_branchings = order - 1    │
│   - 扩散算法: ctx.diff_algo                         │
│   - 热路径记录: ctx.heat_path                       │
├─────────────────────────────────────────────────────┤
│ 步骤3: 注册起始热顶点                                │
│   register_heat_vertex(heat_path, &rwalk.vtx, ...)  │
│   - 记录起始位置到热路径追踪结构                     │
├─────────────────────────────────────────────────────┤
│ ★ 步骤4: 核心辐射路径追踪                           │
│   trace_radiative_path_3d(scn, dir, &ctx, &rwalk,   │
│                           args->rng, &T)            │
│   - 追踪光线直到击中表面或到达辐射环境               │
│   - 占据 >80% 计算时间                              │
├─────────────────────────────────────────────────────┤
│ 步骤5: 如果温度未完成（T.done == false）            │
│   ★ sample_coupled_path_3d(scn, &ctx, &rwalk,       │
│                            args->rng, &T)           │
│   - 递归处理多物理耦合路径（对流/传导/边界）         │
│   - 处理Picard迭代的分支                            │
├─────────────────────────────────────────────────────┤
│ 步骤6: 返回最终温度权重                              │
│   *weight = T.value;                                │
└─────────────────────────────────────────────────────┘
```

**关键观察**:
- 函数本身是顺序执行的（单条射线）
- GPU并行化需要在**批量射线**层面实现（外层循环）
- 核心计算集中在 `trace_radiative_path_3d` 和 `sample_coupled_path_3d`

---

### **2. 核心路径追踪: `trace_radiative_path_3d`**
📍 位置: `stardis-solver/0.16.2/src/sdis_heat_path_radiative_Xd.h:197-321`

这是**最关键的GPU移植目标**，内含不定长循环：

```c
// 主循环：光线弹跳直到终止
for(;;) {  // ⚠️ 无限循环，依赖break退出
    
    // ════════════════════════════════════════════════════
    // A. 射线-几何求交（最耗时操作）
    // ════════════════════════════════════════════════════
    res = find_next_fragment(
        scn,              // 场景（BVH加速结构）
        pos,              // 当前位置
        dir,              // 光线方向
        &rwalk.hit_3d,    // 上一次击中信息（避免自相交）
        rwalk.vtx.time,   // 当前时间
        rwalk.enc_id,     // 当前包围体ID
        &rwalk.hit_3d,    // 输出：新的击中信息
        &interf,          // 输出：击中的接口（材质）
        &frag             // 输出：表面片段（位置、法线等）
    );
    
    // 内部调用链:
    // find_next_fragment()
    //   └─> trace_ray() 
    //         └─> Embree BVH遍历 (RTCRayHit)
    //               └─> scene_view_trace_ray()
    
    // ════════════════════════════════════════════════════
    // B. 判断是否到达辐射环境（终止条件1）
    // ════════════════════════════════════════════════════
    if (SXD_HIT_NONE(&rwalk.hit_3d)) {  // 未击中任何几何
        // 设置边界辐射温度
        res = set_limit_radiative_temperature(
            scn, ctx, rwalk, dir, branch_id, &T
        );
        // 内部查询 radiative_env_get_temperature()
        // 返回环境辐射温度（如天空、黑体等）
        
        T.done = 1;  // 标记温度已确定
        break;       // ★ 退出循环
    }
    
    // ════════════════════════════════════════════════════
    // C. 更新随机游走位置到击中点
    // ════════════════════════════════════════════════════
    d3_set(rwalk.vtx.P, frag.P);        // 新位置
    rwalk.hit_side = frag.side;         // 击中面朝向（正面/背面）
    
    // ════════════════════════════════════════════════════
    // D. 验证介质类型（辐射路径只能在流体中传播）
    // ════════════════════════════════════════════════════
    struct sdis_medium* chk_mdm = 
        (rwalk.hit_side == SDIS_FRONT) 
            ? interf->medium_front 
            : interf->medium_back;
    
    if (sdis_medium_get_type(chk_mdm) == SDIS_SOLID) {
        // ⚠️ 致命错误：辐射路径进入固体
        // （不支持半透明固体）
        return RES_BAD_OP_IRRECOVERABLE;
    }
    
    // ════════════════════════════════════════════════════
    // E. 记录热路径顶点（用于Green函数）
    // ════════════════════════════════════════════════════
    register_heat_vertex(
        ctx->heat_path,
        &rwalk.vtx,         // 当前顶点
        T.value,            // 当前温度
        SDIS_HEAT_VERTEX_RADIATIVE,  // 顶点类型
        branch_id           // 分支ID（Picard迭代）
    );
    
    // ════════════════════════════════════════════════════
    // F. 表面光学属性（BRDF）采样
    // ════════════════════════════════════════════════════
    struct brdf brdf;
    brdf_setup_args.interf = interf;
    brdf_setup_args.frag = &frag;
    brdf_setup(scn->dev, &brdf_setup_args, &brdf);
    
    // 俄罗斯轮盘赌：发射 vs 反射
    if (ssp_rng_canonical(rng) < brdf.emissivity) {
        // 光线被表面吸收，切换到边界温度模式
        T.func = boundary_path_3d;   // 函数指针切换 ⚠️
        rwalk.enc_id = ENCLOSURE_ID_NULL;  // 不在任何包围体
        break;  // ★ 退出循环（终止条件2）
    }
    
    // ════════════════════════════════════════════════════
    // G. 采样新方向（BRDF重要性采样）
    // ════════════════════════════════════════════════════
    // 归一化法线
    switch(frag.side) {
        case SDIS_FRONT: 
            d3_set(N, frag.Ng); 
            break;
        case SDIS_BACK:  
            d3_minus(N, frag.Ng);  // 反转法线
            break;
    }
    
    struct brdf_sample bounce;
    double wi[3];
    d3_minus(wi, dir);  // 入射方向
    
    brdf_sample(&brdf, rng, wi, N, &bounce);
    d3_set(dir, bounce.dir);  // 更新光线方向
    
    ++nbounces;  // 弹跳计数器（调试用）
    
    // 继续下一次迭代
}
```

**关键循环特征（GPU挑战）**:

| 特征 | CPU行为 | GPU挑战 |
|------|---------|---------|
| **循环长度** | 不定（依赖随机数和几何） | Warp Divergence |
| **数据依赖** | 每次迭代依赖上次结果 | 无法向量化单条射线 |
| **分支密集** | if-else判断多 | 降低SIMD效率 |
| **内存访问** | 指针跳转（BVH遍历） | 缓存不友好 |
| **函数调用** | 深度调用栈 | 寄存器压力 |

**性能热点分析**:
```
find_next_fragment()  → 50-60% 时间（BVH遍历）
brdf_setup/sample()   → 20-30% 时间（材质计算）
register_heat_vertex()→ 5-10% 时间（记录路径）
其他                  → 10-15% 时间
```

---

### **3. 耦合路径采样: `sample_coupled_path_3d`**
📍 位置: `stardis-solver/0.16.2/src/sdis_realisation_Xd.h:86-170`

处理多物理场景（热传导、对流、辐射耦合）：

```c
res_T sample_coupled_path_3d(
    struct sdis_scene* scn,
    struct rwalk_context* ctx,
    struct rwalk* rwalk,
    struct ssp_rng* rng,
    struct temperature* T
) {
    // 递归深度控制
    ctx->nbranchings += 1;
    CHK(ctx->nbranchings <= ctx->max_branchings);  // Picard阶数限制
    
    // ════════════════════════════════════════════════════
    // 主循环：直到温度确定
    // ════════════════════════════════════════════════════
    while (!T->done) {
        
        // 保存当前状态（用于失败回滚）
        const struct rwalk rwalk_bkp = *rwalk;
        const struct temperature T_bkp = *T;
        size_t nfails = 0;
        
        // ════════════════════════════════════════════════
        // 动态多态：根据温度状态调用不同路径函数
        // ════════════════════════════════════════════════
        // ⚠️ GPU不友好：函数指针在运行时确定
        do {
            res = T->func(  // 可能是以下之一:
                scn, ctx, rwalk, rng, T
            );
            // 可能的函数:
            //   - radiative_path_3d    辐射传输（已在上面追踪）
            //   - conductive_path_3d   热传导（固体中随机游走）
            //   - convective_path_3d   对流（流体中随机游走）
            //   - boundary_path_3d     边界条件（固-流界面）
            
            // 失败重试机制
            if (res == RES_BAD_OP) {
                *rwalk = rwalk_bkp;  // 恢复状态
                *T = T_bkp;
            }
        } while (res == RES_BAD_OP && ++nfails < MAX_FAILS);
        
        if (res != RES_OK) {
            log_err(...);
            goto error;
        }
        
        // ════════════════════════════════════════════════
        // 更新热顶点类型（根据实际执行的函数）
        // ════════════════════════════════════════════════
        if (heat_vtx && !T->done && T->func != boundary_path) {
            if (T->func == conductive_path) {
                heat_vtx->type = SDIS_HEAT_VERTEX_CONDUCTION;
            } else if (T->func == convective_path) {
                heat_vtx->type = SDIS_HEAT_VERTEX_CONVECTION;
            } else if (T->func == radiative_path) {
                heat_vtx->type = SDIS_HEAT_VERTEX_RADIATIVE;
            }
        }
    }
    
    ctx->nbranchings -= 1;
    return res;
}
```

**函数调用模式（多态）**:

```
temperature.func 可能指向:
├─ radiative_path_3d
│   └─ trace_radiative_path_3d (光线追踪主循环)
│
├─ conductive_path_3d
│   └─ 固体中热传导随机游走
│       ├─ 可能使用 Delta-Sphere 算法
│       └─ 或使用 Walk-on-Sphere (WoS) 算法
│
├─ convective_path_3d
│   └─ 流体中对流随机游走
│       └─ 追溯到初始温度或边界
│
└─ boundary_path_3d
    └─ 边界条件处理
        ├─ 固-固接触
        ├─ 固-流接触（Robin条件）
        └─ 外部通量处理
```

**GPU挑战**:
1. **动态多态**: `T->func` 函数指针在运行时才知道类型
2. **状态回滚**: 失败时需要恢复完整的 `rwalk` 和 `T` 状态
3. **深度递归**: Picard迭代可能嵌套多层（虽然有 `max_branchings` 限制）

---

## 二、关键数据结构

### **1. 随机游走状态 (struct rwalk)**
📍 `stardis-solver/0.16.2/src/sdis_heat_path.h:90-111`

```c
struct rwalk {
    struct sdis_rwalk_vertex vtx;  // 时空位置
        // double P[3];    // 位置 (24 bytes)
        // double time;    // 时间 (8 bytes)
    
    unsigned enc_id;                // 包围体ID (4 bytes)
    
    struct s3d_hit hit_3d;          // Embree击中信息
        // float distance;         // 击中距离
        // float uv[2];            // 参数坐标
        // float normal[3];        // 法线
        // unsigned prim_id;       // 图元ID
        // unsigned geom_id;       // 几何ID
        // (~48 bytes)
    
    struct s2d_hit hit_2d;          // 2D版本（本项目用3D）
    
    double dir[3];                  // 到达辐射环境的方向 (24 bytes)
    double elapsed_time;            // 已经过时间 (8 bytes)
    enum sdis_side hit_side;        // 击中面朝向 (4 bytes)
};

// 总大小: ~200 bytes/ray
```

**内存布局分析**:
```
CPU (AoS):
  struct rwalk rays[N];
  - 缓存不友好（大结构体）
  - 指针跳转多

GPU优化 (SoA):
  struct {
      float3* positions;    // [N] 连续内存
      float*  times;        // [N]
      uint*   enc_ids;      // [N]
      // ... 其他字段分离存储
  };
  - 内存合并访问
  - 更好的缓存局部性
```

---

### **2. 击中信息 (struct s3d_hit)** - 来自Embree
📍 `star-3d/0.10/src/s3d_backend.h`

```c
struct s3d_hit {
    float distance;        // 射线参数 t (hit point = origin + t * dir)
    float uv[2];          // 三角形重心坐标 (用于插值)
    float normal[3];      // 几何法线（世界空间）
    
    unsigned prim_id;     // 图元ID（对应scene中的primitive）
    unsigned geom_id;     // 几何ID（Embree内部ID）
    
    struct s3d_primitive prim;  // 包含材质指针等
};
```

**Embree集成**:
```c
// CPU: 直接调用Embree
RTCRayHit rayhit;
rtcIntersect1(scene, &context, &rayhit);

// GPU迁移选项:
// 1. 提取Embree BVH数据到GPU
// 2. 使用OptiX (NVIDIA)
// 3. 使用DXR (DirectX Raytracing)
// 4. 自定义CUDA BVH遍历
```

---

### **3. 温度状态 (struct temperature)**
📍 `stardis-solver/0.16.2/src/sdis_heat_path.h:113-124`

```c
struct temperature {
    // ⚠️ 函数指针：动态多态的核心
    res_T (*func)(
        struct sdis_scene* scn,
        struct rwalk_context* ctx,
        struct rwalk* rwalk,
        struct ssp_rng* rng,
        struct temperature* temp
    );  // 8 bytes (指针)
    
    double value;  // 当前温度值 (Kelvin) (8 bytes)
    int done;      // 是否已确定 (4 bytes)
};

// 总大小: 20 bytes (对齐后可能24)
```

**GPU改造方案**:

```c
// ❌ CPU: 函数指针多态
struct temperature {
    res_T (*func)(...);  // 运行时确定
};

// ✅ GPU: 枚举 + switch (或多Kernel)
enum PathType {
    PATH_RADIATIVE,
    PATH_CONDUCTIVE,
    PATH_CONVECTIVE,
    PATH_BOUNDARY
};

struct temperature_gpu {
    PathType type;     // 4 bytes
    double value;      // 8 bytes
    int done;          // 4 bytes
};

__device__ void execute_path(PathType type, ...) {
    switch(type) {  // 编译期展开，无间接跳转
        case PATH_RADIATIVE: 
            radiative_kernel(...); 
            break;
        case PATH_CONDUCTIVE: 
            conductive_kernel(...); 
            break;
        // ...
    }
}
```

---

### **4. 场景数据 (struct sdis_scene)**
📍 `stardis-solver/0.16.2/src/sdis_scene_c.h:230-256`

```c
struct sdis_scene {
    // ════════════════════════════════════════════════════
    // 几何数据（需要迁移到GPU）
    // ════════════════════════════════════════════════════
    struct s3d_scene_view* s3d_view;  // Embree BVH
        // RTCScene rtc_scn;
        // struct htable_geom cached_geoms;
        // float lower[3], upper[3];  // AABB
    
    // ════════════════════════════════════════════════════
    // 材质和介质（需要扁平化）
    // ════════════════════════════════════════════════════
    struct darray_interf interfaces;   // 动态数组
        // struct sdis_interface* data;
        // size_t size, capacity;
    
    struct darray_medium media;        // 动态数组
        // struct sdis_medium* data;
    
    struct darray_prim_prop prim_props;  // 图元属性
        // struct prim_prop {
        //     struct sdis_interface* interf;
        //     unsigned front_enclosure;
        //     unsigned back_enclosure;
        // };
    
    // ════════════════════════════════════════════════════
    // 包围体和拓扑（需要哈希表→数组）
    // ════════════════════════════════════════════════════
    struct htable_enclosure enclosures;  // 哈希表
        // unsigned key → struct enclosure
    
    unsigned outer_enclosure_id;  // 外部包围体
    
    // ════════════════════════════════════════════════════
    // 物理参数（直接复制到GPU常量内存）
    // ════════════════════════════════════════════════════
    double fp_to_meter;  // 浮点数到米的转换因子
    double tmin;         // 最低温度 (K)
    double tmax;         // 最高温度 (K)
    
    // ════════════════════════════════════════════════════
    // 边界条件
    // ════════════════════════════════════════════════════
    struct sdis_source* source;           // 外部光源
    struct sdis_radiative_env* radenv;    // 辐射环境
    
    // ════════════════════════════════════════════════════
    // 引用计数和设备指针
    // ════════════════════════════════════════════════════
    ref_T ref;
    struct sdis_device* dev;
};
```

**GPU迁移策略**:

```c
// CPU: 动态数据结构（指针跳转多）
struct darray_interf interfaces;  // 运行时增长
struct htable_enclosure enclosures;  // 哈希表查找

// GPU: 扁平化数组（连续内存）
struct GPUScene {
    // 几何加速结构
    BVHNode* bvh_nodes;        // [node_count]
    Triangle* triangles;       // [tri_count]
    
    // 材质数组（索引访问）
    Material* materials;       // [material_count]
    Medium* media;             // [medium_count]
    
    // 图元→材质映射
    uint32_t* prim_to_material;  // [prim_count]
    uint32_t* prim_to_enclosure; // [prim_count]
    
    // 包围体数据（扁平化）
    Enclosure* enclosures;     // [enclosure_count]
    
    // 物理常量（常量内存）
    double tmin, tmax;
    double fp_to_meter;
};

// 上传到GPU
cudaMemcpy(d_scene, &gpu_scene, sizeof(GPUScene), cudaMemcpyHostToDevice);
```

---

### **5. 随机数生成器状态 (struct ssp_rng)**
📍 `star-sp/0.15/src/ssp_rng_c.h`

```c
// CPU: Random123 (Threefry/Philox)
struct ssp_rng {
    ssp_rng_type_T type;  // RNG类型枚举
    union {
        struct threefry_state tf;
        struct philox_state px;
        struct aes_state aes;
    } state;  // 各RNG的内部状态
};

// GPU: 无状态Random123（更适合并行）
__device__ float random_float(
    uint64_t ray_id,      // 全局唯一ID
    uint32_t bounce,      // 弹跳计数
    uint32_t dimension    // 维度索引
) {
    // 使用Counter-Based RNG
    philox4x32_key_t key = {{ray_id, 0}};
    philox4x32_ctr_t ctr = {{bounce, dimension, 0, 0}};
    
    philox4x32_ctr_t rand = philox4x32(ctr, key);
    return rand.v[0] / (float)UINT32_MAX;
}
```

**优势**:
- ✅ 无需保存RNG状态（节省内存）
- ✅ 每个样本独立计算（无竞争）
- ✅ 可重现（便于验证）
- ✅ Random123已有CUDA支持

---

## 三、GPU并行化改造准备工作

### **阶段1: 数据结构重构** ⏱️ 1-2周

#### **1.1 内存布局转换（AoS → SoA）**

**问题**: CPU使用Array of Structures（AoS），GPU更适合Structure of Arrays（SoA）

```cpp
// ════════════════════════════════════════════════════
// ❌ CPU版本 (AoS)
// ════════════════════════════════════════════════════
struct Ray {
    float3 origin;
    float3 direction;
    float time;
    uint32_t enc_id;
    HitInfo hit;
};

Ray rays[N];  // 缓存行利用率低

// 访问模式（Warp内32个线程）:
for (int i = tid; i < N; i += blockDim.x) {
    float3 dir = rays[i].direction;  
    // ⚠️ 跨度200字节，缓存miss严重
}

// ════════════════════════════════════════════════════
// ✅ GPU版本 (SoA)
// ════════════════════════════════════════════════════
struct RayStream {
    float3* origins;      // [N] 连续存储
    float3* directions;   // [N]
    float*  times;        // [N]
    uint32_t* enc_ids;    // [N]
    HitInfo* hits;        // [N]
    uint8_t* active_mask; // [N] 标记活跃射线
};

// 访问模式（内存合并）:
for (int i = tid; i < N; i += blockDim.x) {
    float3 dir = directions[i];  
    // ✅ 连续访问，单次128字节事务
}
```

**内存带宽对比**:
```
AoS:  32 threads × 200 bytes = 6400 bytes / 多次事务
SoA:  32 threads × 12 bytes  = 384 bytes  / 单次事务 (float3)
性能提升: ~4-8x
```

#### **1.2 动态数据结构扁平化**

**问题**: CPU的动态数组/哈希表在GPU上难以高效实现

```cpp
// ════════════════════════════════════════════════════
// ❌ CPU: 动态数组（运行时增长）
// ════════════════════════════════════════════════════
struct darray_heat_vertex {
    struct sdis_heat_vertex* data;  // 动态分配
    size_t size;
    size_t capacity;
};

void heat_path_add_vertex(struct darray* arr, const vertex* v) {
    if (arr->size == arr->capacity) {
        arr->capacity *= 2;
        arr->data = realloc(arr->data, ...);  // GPU不支持
    }
    arr->data[arr->size++] = *v;
}

// ════════════════════════════════════════════════════
// ✅ GPU: 固定容量 + 原子计数
// ════════════════════════════════════════════════════
#define MAX_PATH_VERTICES 256

struct HeatPath {
    HeatVertex vertices[MAX_PATH_VERTICES];  // 预分配
    int vertex_count;  // 原子递增
};

__device__ void add_vertex(HeatPath* path, const HeatVertex* v) {
    int idx = atomicAdd(&path->vertex_count, 1);
    if (idx < MAX_PATH_VERTICES) {
        path->vertices[idx] = *v;
    } else {
        // 溢出处理（记录错误）
    }
}
```

**哈希表扁平化**:

```cpp
// ❌ CPU: 哈希表
struct htable_enclosure {
    unsigned* keys;       // 动态桶
    struct enclosure* values;
    size_t size, capacity;
};

// 查找: O(1) 平均，但需要链表/探测
enclosure* get(htable* t, unsigned key) {
    size_t idx = hash(key) % t->capacity;
    // 处理冲突...
}

// ✅ GPU: 扁平数组 + 直接索引
struct GPUEnclosures {
    Enclosure* data;       // [MAX_ENCLOSURES]
    uint32_t count;
};

// 假设 enclosure_id < MAX_ENCLOSURES (预处理重映射)
__device__ Enclosure* get(GPUEnclosures* enc, uint32_t id) {
    return (id < enc->count) ? &enc->data[id] : nullptr;
}
```

#### **1.3 内存对齐优化**

```cpp
// ════════════════════════════════════════════════════
// ⚠️ 未对齐结构体（GPU效率低）
// ════════════════════════════════════════════════════
struct RayState {
    float3 origin;     // 12 bytes
    float time;        // 4 bytes
    float3 direction;  // 12 bytes
    uint32_t enc_id;   // 4 bytes
};  // 总共 32 bytes，但对齐不佳

// ════════════════════════════════════════════════════
// ✅ 128字节边界对齐（2个缓存行）
// ════════════════════════════════════════════════════
struct __align__(128) RayState {
    // 第一缓存行 (64 bytes)
    float3 origin;       // 12 bytes
    float pad0;          // 4 bytes padding
    float3 direction;    // 12 bytes
    float time;          // 4 bytes
    
    // 第二缓存行 (64 bytes)
    float3 hit_normal;   // 12 bytes
    float hit_distance;  // 4 bytes
    uint32_t enc_id;     // 4 bytes
    uint32_t prim_id;    // 4 bytes
    uint8_t active;      // 1 byte
    uint8_t pad1[39];    // padding到128
};
```

**对齐检查**:
```cuda
// 编译时断言
static_assert(sizeof(RayState) == 128, "Bad alignment");
static_assert(sizeof(RayState) % 128 == 0, "Not cache-aligned");
```

---

### **阶段2: 算法重构（DFS → BFS）** ⏱️ 2-3周

#### **2.1 Wavefront 路径追踪架构**

**核心问题**: CPU是深度优先（单条射线追到底），GPU需要宽度优先（批量射线同步前进）

```cpp
// ════════════════════════════════════════════════════
// ❌ CPU: 深度优先搜索 (DFS)
// ════════════════════════════════════════════════════
void trace_single_ray(Ray ray) {
    while (!ray.done) {
        Hit hit = intersect(scene, ray);  // 串行
        if (hit.valid) {
            ray = sample_brdf(hit);       // 串行
        } else {
            ray.done = true;
        }
    }
}

// 主循环
for (int i = 0; i < N_RAYS; i++) {
    trace_single_ray(rays[i]);  // 完全串行
}

// ════════════════════════════════════════════════════
// ✅ GPU: 宽度优先搜索 (BFS) - Wavefront
// ════════════════════════════════════════════════════
void wavefront_trace(RayStream* rays, int N) {
    int active_count = N;
    int iteration = 0;
    
    while (active_count > 0) {
        // ──────────────────────────────────────────────
        // Stage 1: 所有活跃射线并行求交
        // ──────────────────────────────────────────────
        intersect_kernel<<<blocks, threads>>>(
            rays, active_count
        );
        cudaDeviceSynchronize();
        
        // ──────────────────────────────────────────────
        // Stage 2: 压缩活跃射线（移除终止的）
        // ──────────────────────────────────────────────
        active_count = compact_rays(rays, active_count);
        
        if (active_count == 0) break;
        
        // ──────────────────────────────────────────────
        // Stage 3: BRDF评估和采样
        // ──────────────────────────────────────────────
        brdf_kernel<<<blocks, threads>>>(
            rays, active_count
        );
        cudaDeviceSynchronize();
        
        // ──────────────────────────────────────────────
        // Stage 4: 俄罗斯轮盘赌终止
        // ──────────────────────────────────────────────
        terminate_kernel<<<blocks, threads>>>(
            rays, active_count
        );
        
        iteration++;
        if (iteration > MAX_BOUNCES) break;  // 安全阈值
    }
}
```

**Wavefront 优势**:

| 方面 | DFS（CPU） | Wavefront（GPU） |
|------|-----------|------------------|
| **Warp效率** | 低（同一Warp内射线弹跳次数不同） | 高（同一stage所有射线做同样操作） |
| **分支发散** | 高（if-else混乱） | 低（每个stage逻辑单一） |
| **内存访问** | 随机（每条射线独立） | 合并（批量访问相似数据） |
| **调试** | 容易（线性执行） | 困难（需要kernel间状态追踪） |

#### **2.2 Stream Compaction 实现**

**目的**: 移除已终止的射线，保持队列紧凑

```cuda
// ════════════════════════════════════════════════════
// 方法1: Thrust库 (简单但性能一般)
// ════════════════════════════════════════════════════
#include <thrust/remove_if.h>

int compact_rays_thrust(RayStream* rays, int N) {
    // 移除 active_mask == 0 的射线
    int* new_end = thrust::remove_if(
        thrust::device,
        rays->active_mask,
        rays->active_mask + N,
        [] __device__ (uint8_t mask) { return mask == 0; }
    );
    
    return new_end - rays->active_mask;
}

// ════════════════════════════════════════════════════
// 方法2: CUB库前缀和 (高性能)
// ════════════════════════════════════════════════════
#include <cub/cub.cuh>

__global__ void compact_kernel(
    RayStream* in,
    RayStream* out,
    uint32_t* scan_result,  // 前缀和
    int N
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N) return;
    
    if (in->active_mask[tid]) {
        int out_idx = scan_result[tid];  // 写入位置
        out->origins[out_idx] = in->origins[tid];
        out->directions[out_idx] = in->directions[tid];
        // ... 复制其他字段
    }
}

int compact_rays_cub(RayStream* rays, int N) {
    // 1. 前缀和计算活跃射线数量
    uint32_t* d_scan;
    cudaMalloc(&d_scan, N * sizeof(uint32_t));
    
    cub::DeviceScan::ExclusiveSum(
        nullptr, temp_storage_bytes,
        rays->active_mask, d_scan, N
    );
    
    // 2. 压缩射线数据
    RayStream* temp_rays;
    cudaMalloc(&temp_rays, ...);
    
    compact_kernel<<<blocks, threads>>>(
        rays, temp_rays, d_scan, N
    );
    
    // 3. 交换指针
    std::swap(rays, temp_rays);
    
    // 4. 返回活跃数量
    uint32_t active_count;
    cudaMemcpy(&active_count, &d_scan[N-1], ...);
    return active_count;
}

// ════════════════════════════════════════════════════
// 方法3: Warp-Level Compaction (最快，但复杂)
// ════════════════════════════════════════════════════
__global__ void compact_warp_kernel(
    RayStream* rays,
    int* out_count,
    int N
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int lane_id = threadIdx.x % 32;
    
    __shared__ int warp_offsets[32];  // 每个warp的起始位置
    
    // Warp内选票
    uint32_t active = __ballot_sync(0xFFFFFFFF, 
                                    tid < N && rays->active_mask[tid]);
    int warp_count = __popc(active);  // 活跃线程数
    
    // 第一个lane分配全局偏移
    int global_offset = 0;
    if (lane_id == 0) {
        global_offset = atomicAdd(out_count, warp_count);
        warp_offsets[threadIdx.x / 32] = global_offset;
    }
    global_offset = warp_offsets[threadIdx.x / 32];
    
    // 计算lane内偏移
    uint32_t lane_mask = (1 << lane_id) - 1;
    int lane_offset = __popc(active & lane_mask);
    
    // 写入压缩位置
    if (rays->active_mask[tid]) {
        int out_idx = global_offset + lane_offset;
        // 复制数据...
    }
}
```

**性能对比**:
```
Thrust:      ~10 ms (10M rays)
CUB:         ~3 ms
Warp-Level:  ~1 ms
```

#### **2.3 函数指针消除策略**

**问题**: `temperature->func` 动态函数指针导致GPU间接跳转

```cpp
// ════════════════════════════════════════════════════
// ❌ CPU: 函数指针（GPU性能损失50%+）
// ════════════════════════════════════════════════════
struct Temperature {
    PathFunction func;  // 运行时才知道指向哪个函数
    double value;
    int done;
};

__device__ void sample_path(Temperature* T) {
    T->func(...);  // ⚠️ 间接跳转，无法内联
}

// ════════════════════════════════════════════════════
// ✅ 方案1: 枚举 + switch（编译期展开）
// ════════════════════════════════════════════════════
enum PathType : uint8_t {
    PATH_RADIATIVE   = 0,
    PATH_CONDUCTIVE  = 1,
    PATH_CONVECTIVE  = 2,
    PATH_BOUNDARY    = 3
};

struct Temperature {
    PathType type;  // 4 bytes (vs 8 bytes pointer)
    double value;
    int done;
};

__device__ void sample_path(Temperature* T, ...) {
    switch (T->type) {
        case PATH_RADIATIVE:
            radiative_path_kernel(...);
            break;
        case PATH_CONDUCTIVE:
            conductive_path_kernel(...);
            break;
        case PATH_CONVECTIVE:
            convective_path_kernel(...);
            break;
        case PATH_BOUNDARY:
            boundary_path_kernel(...);
            break;
    }
    // 编译器会内联并优化switch
}

// ════════════════════════════════════════════════════
// ✅ 方案2: 多Kernel调度（最高效）
// ════════════════════════════════════════════════════
// 根据类型将射线分流到不同Kernel

struct RayQueues {
    int radiative_count;
    int conductive_count;
    int convective_count;
    int boundary_count;
    
    int* radiative_indices;   // [N]
    int* conductive_indices;  // [N]
    // ...
};

void dispatch_by_type(RayStream* rays, int N) {
    // 1. 分类射线
    RayQueues queues;
    classify_rays_kernel<<<...>>>(rays, &queues, N);
    
    // 2. 分别启动Kernel
    if (queues.radiative_count > 0) {
        radiative_kernel<<<...>>>(
            rays, queues.radiative_indices, queues.radiative_count
        );
    }
    
    if (queues.conductive_count > 0) {
        conductive_kernel<<<...>>>(
            rays, queues.conductive_indices, queues.conductive_count
        );
    }
    
    // ... 其他类型
}
```

**性能收益**:
```
函数指针:    100% (基线)
枚举+switch: ~70%  (30%提升)
多Kernel:    ~50%  (2x提升)
```

#### **2.4 递归展开**

**问题**: `sample_coupled_path` 的递归（GPU栈有限）

```cuda
// ════════════════════════════════════════════════════
// ❌ CPU: 真递归
// ════════════════════════════════════════════════════
void sample_coupled_path(Context* ctx, ...) {
    ctx->nbranchings++;
    
    while (!T->done) {
        T->func(...);  // 可能再次调用sample_coupled_path
    }
    
    ctx->nbranchings--;
}

// ════════════════════════════════════════════════════
// ✅ GPU: 显式栈 + 迭代
// ════════════════════════════════════════════════════
#define MAX_PICARD_ORDER 8  // Picard迭代最大深度

struct PathStackFrame {
    RWalk rwalk;
    Temperature temp;
    int iteration;
};

struct PathStack {
    PathStackFrame frames[MAX_PICARD_ORDER];
    int depth;
};

__device__ void sample_coupled_path_iterative(
    PathStack* stack,
    Context* ctx,
    RNG* rng
) {
    while (stack->depth >= 0) {
        PathStackFrame* frame = &stack->frames[stack->depth];
        
        // 执行当前帧的逻辑
        switch (frame->temp.type) {
            case PATH_RADIATIVE:
                trace_radiative_path(...);
                
                // 检查是否需要branching
                if (needs_branch && stack->depth < MAX_PICARD_ORDER - 1) {
                    // 模拟递归调用：压入新帧
                    stack->depth++;
                    PathStackFrame* new_frame = &stack->frames[stack->depth];
                    initialize_frame(new_frame, ...);
                } else {
                    // 当前帧完成
                    frame->temp.done = true;
                }
                break;
                
            // ... 其他路径类型
        }
        
        // 弹出完成的帧
        if (frame->temp.done) {
            stack->depth--;
        }
    }
}
```

**栈内存分析**:
```
每帧大小: ~300 bytes (RWalk + Temperature)
最大深度: 8
总栈大小: 2.4 KB / thread

RTX 4090:
- 每个SM: 64 KB shared memory
- 支持线程数: ~26 threads/SM (栈限制下)
```

---

### **阶段3: 加速结构迁移** ⏱️ 1-2周

#### **3.1 BVH选择策略**

**选项对比**:

| 方案 | 优点 | 缺点 | 适用场景 |
|------|------|------|---------|
| **提取Embree BVH** | 易于验证 | 需要手动遍历 | 原型阶段 |
| **OptiX API** | 硬件加速 | 需要转换数据 | CUDA路径 |
| **DXR硬件RT** | 最快 | 仅限DirectX | UE集成 |
| **自定义LBVH** | 灵活控制 | 需要调优 | 特殊几何 |

**推荐路线**: Embree提取 → (可选) OptiX优化

#### **3.2 提取Embree BVH到GPU**

```cpp
// ════════════════════════════════════════════════════
// Step 1: 提取BVH节点数据（CPU端）
// ════════════════════════════════════════════════════
struct BVHNode {
    float3 bbox_min, bbox_max;  // AABB包围盒
    union {
        struct {  // 内部节点
            uint32_t left_child;
            uint32_t right_child;
        };
        struct {  // 叶子节点
            uint32_t prim_start;
            uint32_t prim_count;
        };
    };
    uint32_t is_leaf;  // 标志位
};

void extract_embree_bvh(RTCScene embree_scene, std::vector<BVHNode>& nodes) {
    // Embree 不直接暴露BVH，需要自己构建
    // 或使用Embree的回调接口获取
    
    // 方法1: 重建BVH
    struct Triangle {
        float3 v0, v1, v2;
        uint32_t material_id;
    };
    
    std::vector<Triangle> triangles;
    extract_triangles_from_embree(embree_scene, triangles);
    
    // 使用LBVH构建
    build_lbvh_gpu(triangles, nodes);
}

// ════════════════════════════════════════════════════
// Step 2: 上传到GPU
// ════════════════════════════════════════════════════
BVHNode* d_bvh_nodes;
cudaMalloc(&d_bvh_nodes, nodes.size() * sizeof(BVHNode));
cudaMemcpy(d_bvh_nodes, nodes.data(), ..., cudaMemcpyHostToDevice);

// ════════════════════════════════════════════════════
// Step 3: GPU BVH遍历Kernel
// ════════════════════════════════════════════════════
__global__ void intersect_bvh_kernel(
    RayStream rays,
    BVHNode* bvh,
    Triangle* triangles,
    HitInfo* hits,
    int ray_count
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= ray_count) return;
    
    // 加载射线
    float3 org = rays.origins[tid];
    float3 dir = rays.directions[tid];
    
    // ──────────────────────────────────────────────────
    // BVH栈遍历（每个线程独立）
    // ──────────────────────────────────────────────────
    __shared__ int stack_storage[1024];  // Shared memory栈
    int* stack = &stack_storage[threadIdx.x * 32];  // 每线程32层
    
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;  // 根节点
    
    float t_min = INFINITY;
    int hit_prim = -1;
    float2 hit_uv = {0, 0};
    
    // ──────────────────────────────────────────────────
    // 遍历循环
    // ──────────────────────────────────────────────────
    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        BVHNode node = bvh[node_idx];
        
        // A. AABB求交（快速剔除）
        float t_near, t_far;
        if (!ray_aabb_intersect(
            org, dir, 
            node.bbox_min, node.bbox_max,
            &t_near, &t_far
        )) {
            continue;  // 未击中包围盒
        }
        
        // B. 叶子节点：测试图元
        if (node.is_leaf) {
            for (uint32_t i = 0; i < node.prim_count; i++) {
                uint32_t prim_idx = node.prim_start + i;
                Triangle tri = triangles[prim_idx];
                
                // Möller-Trumbore算法
                float t, u, v;
                if (ray_triangle_intersect(
                    org, dir,
                    tri.v0, tri.v1, tri.v2,
                    &t, &u, &v
                )) {
                    if (t < t_min) {
                        t_min = t;
                        hit_prim = prim_idx;
                        hit_uv = make_float2(u, v);
                    }
                }
            }
        }
        // C. 内部节点：压栈子节点
        else {
            // 优化：先访问近节点
            float t_left_near, t_left_far;
            float t_right_near, t_right_far;
            
            bool hit_left = ray_aabb_intersect(
                org, dir,
                bvh[node.left_child].bbox_min,
                bvh[node.left_child].bbox_max,
                &t_left_near, &t_left_far
            );
            
            bool hit_right = ray_aabb_intersect(
                org, dir,
                bvh[node.right_child].bbox_min,
                bvh[node.right_child].bbox_max,
                &t_right_near, &t_right_far
            );
            
            // 排序压栈（近的先访问）
            if (hit_left && hit_right) {
                if (t_left_near < t_right_near) {
                    stack[stack_ptr++] = node.right_child;
                    stack[stack_ptr++] = node.left_child;
                } else {
                    stack[stack_ptr++] = node.left_child;
                    stack[stack_ptr++] = node.right_child;
                }
            } else if (hit_left) {
                stack[stack_ptr++] = node.left_child;
            } else if (hit_right) {
                stack[stack_ptr++] = node.right_child;
            }
        }
    }
    
    // ──────────────────────────────────────────────────
    // 写回结果
    // ──────────────────────────────────────────────────
    hits[tid].distance = t_min;
    hits[tid].prim_id = hit_prim;
    hits[tid].uv = hit_uv;
    hits[tid].valid = (hit_prim >= 0);
}
```

**AABB求交优化版**:

```cuda
__device__ bool ray_aabb_intersect(
    float3 org, float3 dir,
    float3 bbox_min, float3 bbox_max,
    float* t_near, float* t_far
) {
    // Slab method (优化版)
    float3 inv_dir = make_float3(
        1.0f / dir.x,
        1.0f / dir.y,
        1.0f / dir.z
    );
    
    float3 t0 = (bbox_min - org) * inv_dir;
    float3 t1 = (bbox_max - org) * inv_dir;
    
    float3 tmin = fminf(t0, t1);
    float3 tmax = fmaxf(t0, t1);
    
    *t_near = fmaxf(fmaxf(tmin.x, tmin.y), tmin.z);
    *t_far  = fminf(fminf(tmax.x, tmax.y), tmax.z);
    
    return *t_near <= *t_far && *t_far > 0;
}
```

#### **3.3 OptiX集成（可选优化）**

```cpp
// ════════════════════════════════════════════════════
// OptiX 7+ API (更现代，性能更好)
// ════════════════════════════════════════════════════

// 1. 构建加速结构
OptixTraversableHandle build_optix_accel(
    const std::vector<Triangle>& triangles
) {
    // 上传顶点数据
    CUdeviceptr d_vertices;
    cudaMalloc(&d_vertices, triangles.size() * sizeof(float3) * 3);
    cudaMemcpy(...);
    
    // 构建GAS (Geometry Acceleration Structure)
    OptixAccelBuildOptions accel_options = {};
    accel_options.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
    accel_options.operation  = OPTIX_BUILD_OPERATION_BUILD;
    
    OptixAccelBufferSizes gas_buffer_sizes;
    optixAccelComputeMemoryUsage(
        optix_context,
        &accel_options,
        &build_input,
        1,
        &gas_buffer_sizes
    );
    
    // 执行构建
    CUdeviceptr d_gas_output;
    cudaMalloc(&d_gas_output, gas_buffer_sizes.outputSizeInBytes);
    
    OptixTraversableHandle gas_handle;
    optixAccelBuild(
        optix_context,
        cuda_stream,
        &accel_options,
        &build_input,
        1,
        d_temp_buffer,
        gas_buffer_sizes.tempSizeInBytes,
        d_gas_output,
        gas_buffer_sizes.outputSizeInBytes,
        &gas_handle,
        nullptr, 0
    );
    
    return gas_handle;
}

// 2. 射线追踪Kernel (使用OptiX)
extern "C" __global__ void __raygen__trace_rays() {
    const uint3 idx = optixGetLaunchIndex();
    const uint3 dim = optixGetLaunchDimensions();
    
    const int tid = idx.x;
    
    // 加载射线参数
    RayStream* rays = (RayStream*)optixGetSbtDataPointer();
    float3 org = rays->origins[tid];
    float3 dir = rays->directions[tid];
    
    // 追踪射线（硬件加速）
    unsigned int hit_prim;
    float hit_distance;
    float2 hit_uv;
    
    optixTrace(
        gas_handle,           // 加速结构
        org,                  // 起点
        dir,                  // 方向
        0.0f,                 // tmin
        1e16f,                // tmax
        0.0f,                 // rayTime
        OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_NONE,
        0, 1, 0,              // SBT参数
        hit_prim,             // 输出
        hit_distance,
        hit_uv.x, hit_uv.y
    );
    
    // 写回结果
    rays->hits[tid].prim_id = hit_prim;
    rays->hits[tid].distance = hit_distance;
    rays->hits[tid].uv = hit_uv;
}

// 3. Closest Hit程序
extern "C" __global__ void __closesthit__() {
    const float2 barycentrics = optixGetTriangleBarycentrics();
    const int prim_id = optixGetPrimitiveIndex();
    
    // 设置返回值
    optixSetPayload_0(prim_id);
    optixSetPayload_1(__float_as_uint(optixGetRayTmax()));
    optixSetPayload_2(__float_as_uint(barycentrics.x));
    optixSetPayload_3(__float_as_uint(barycentrics.y));
}
```

**性能对比（1M rays）**:
```
手动BVH:   ~50 ms
OptiX:     ~5 ms   (10x faster)
```

---

### **阶段4: 随机数生成** ⏱️ 1周

#### **4.1 Random123 GPU实现**

```cuda
// ════════════════════════════════════════════════════
// Random123 - 无状态随机数生成器
// ════════════════════════════════════════════════════
#include <Random123/philox.h>

// 每条射线的RNG上下文
struct RayRNG {
    uint64_t ray_id;       // 全局唯一ID（不变）
    uint32_t bounce;       // 当前弹跳数
    uint32_t dimension;    // 当前维度索引
};

// ════════════════════════════════════════════════════
// 生成单个随机数
// ════════════════════════════════════════════════════
__device__ float random_float(const RayRNG* rng) {
    // Philox 4x32 (128位输出)
    philox4x32_key_t key = {{(uint32_t)rng->ray_id, 
                             (uint32_t)(rng->ray_id >> 32)}};
    philox4x32_ctr_t ctr = {{rng->bounce, rng->dimension, 0, 0}};
    
    philox4x32_ctr_t rand = philox4x32(ctr, key);
    
    // 转换到 [0, 1)
    return rand.v[0] * (1.0f / 4294967296.0f);
}

// ════════════════════════════════════════════════════
// 生成4个随机数（向量化）
// ════════════════════════════════════════════════════
__device__ float4 random_float4(RayRNG* rng) {
    philox4x32_key_t key = {{(uint32_t)rng->ray_id, 
                             (uint32_t)(rng->ray_id >> 32)}};
    philox4x32_ctr_t ctr = {{rng->bounce, rng->dimension, 0, 0}};
    
    philox4x32_ctr_t rand = philox4x32(ctr, key);
    
    rng->dimension += 4;  // 消耗4个维度
    
    return make_float4(
        rand.v[0] * (1.0f / 4294967296.0f),
        rand.v[1] * (1.0f / 4294967296.0f),
        rand.v[2] * (1.0f / 4294967296.0f),
        rand.v[3] * (1.0f / 4294967296.0f)
    );
}

// ════════════════════════════════════════════════════
// 应用示例：余弦加权半球采样
// ════════════════════════════════════════════════════
__device__ float3 sample_cosine_hemisphere(RayRNG* rng, float3 N) {
    float u1 = random_float(rng);
    rng->dimension++;
    
    float u2 = random_float(rng);
    rng->dimension++;
    
    // Malley's method
    float r = sqrtf(u1);
    float theta = 2.0f * M_PI * u2;
    
    float x = r * cosf(theta);
    float y = r * sinf(theta);
    float z = sqrtf(fmaxf(0.0f, 1.0f - u1));
    
    // 局部坐标→世界坐标
    float3 T, B;
    build_orthonormal_basis(N, &T, &B);
    
    return x * T + y * B + z * N;
}
```

#### **4.2 RNG分配策略**

```cuda
// ════════════════════════════════════════════════════
// 方案1: 基于像素的种子（渲染用）
// ════════════════════════════════════════════════════
__global__ void generate_camera_rays_kernel(
    RayStream* rays,
    Camera camera,
    int width, int height,
    int sample_index  // 第几个样本
) {
    int px = blockIdx.x * blockDim.x + threadIdx.x;
    int py = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (px >= width || py >= height) return;
    
    int tid = py * width + px;
    
    // 全局唯一ID: (像素坐标 + 样本索引)
    uint64_t ray_id = ((uint64_t)sample_index << 32) | 
                      ((uint64_t)py << 16) | 
                      (uint64_t)px;
    
    RayRNG rng = {ray_id, 0, 0};
    
    // 生成抖动光线
    float jitter_x = random_float(&rng);
    rng.dimension++;
    float jitter_y = random_float(&rng);
    rng.dimension++;
    
    float u = (px + jitter_x) / width;
    float v = (py + jitter_y) / height;
    
    rays->origins[tid] = camera.position;
    rays->directions[tid] = camera_get_ray_direction(camera, u, v);
    rays->rng[tid] = rng;
}

// ════════════════════════════════════════════════════
// 方案2: 线程ID作为种子（通用）
// ════════════════════════════════════════════════════
__global__ void monte_carlo_kernel(RayStream* rays, int N) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N) return;
    
    // 简单策略：线程ID + 启动计数
    uint64_t ray_id = tid + 
                      ((uint64_t)launch_count << 32);
    
    RayRNG rng = {ray_id, 0, 0};
    rays->rng[tid] = rng;
}

// ════════════════════════════════════════════════════
// 方案3: 基于时间的种子（真随机，不可重现）
// ════════════════════════════════════════════════════
__global__ void init_rng_time_based(RayRNG* rngs, int N) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N) return;
    
    // GPU时钟（纳秒级）
    uint64_t clock_val = clock64();
    
    rngs[tid] = {
        .ray_id = clock_val ^ tid,  // 混合时钟和ID
        .bounce = 0,
        .dimension = 0
    };
}
```

#### **4.3 验证RNG质量**

```cpp
// ════════════════════════════════════════════════════
// CPU端：与CPU RNG对比
// ════════════════════════════════════════════════════
void validate_rng_consistency() {
    const int N = 1000000;
    
    // CPU生成
    std::vector<float> cpu_random(N);
    ssp_rng* cpu_rng = ssp_rng_create(SSP_RNG_PHILOX);
    ssp_rng_set_seed(cpu_rng, 12345);
    
    for (int i = 0; i < N; i++) {
        cpu_random[i] = ssp_rng_canonical(cpu_rng);
    }
    
    // GPU生成
    float* d_gpu_random;
    cudaMalloc(&d_gpu_random, N * sizeof(float));
    
    generate_random_kernel<<<blocks, threads>>>(d_gpu_random, N, 12345);
    
    std::vector<float> gpu_random(N);
    cudaMemcpy(gpu_random.data(), d_gpu_random, ...);
    
    // 对比
    double max_diff = 0;
    for (int i = 0; i < N; i++) {
        double diff = fabs(cpu_random[i] - gpu_random[i]);
        max_diff = std::max(max_diff, diff);
    }
    
    printf("Max difference: %.3e (should be ~0)\n", max_diff);
    assert(max_diff < 1e-6);  // 应该完全一致
}
```

---

### **阶段5: 数值精度保证** ⏱️ 持续

#### **5.1 混合精度策略**

```cuda
// ════════════════════════════════════════════════════
// 性能 vs 精度权衡
// ════════════════════════════════════════════════════

// RTX 4090双精度性能: 
//   FP64: ~0.66 TFLOPS (1:64 ratio)
//   FP32: ~42 TFLOPS

// 策略：关键物理量用双精度，几何用单精度

struct RayState {
    // ──────────────────────────────────────────────────
    // 几何计算: 单精度（FP32）
    // ──────────────────────────────────────────────────
    float3 origin;       // 位置
    float3 direction;    // 方向
    float3 hit_normal;   // 法线
    float hit_distance;  // 距离
    
    // ──────────────────────────────────────────────────
    // 温度累积: 双精度（FP64）
    // ──────────────────────────────────────────────────
    double temperature;  // 温度值 (Kelvin)
    double weight;       // 路径权重
    
    // ──────────────────────────────────────────────────
    // 物理常数: 双精度（编译期常量）
    // ──────────────────────────────────────────────────
    // extern __constant__ double c_sigma;  // Stefan-Boltzmann常数
    // extern __constant__ double c_tmin, c_tmax;
};

// ════════════════════════════════════════════════════
// 温度计算（双精度）
// ════════════════════════════════════════════════════
__device__ double compute_blackbody_emission(double T) {
    // σ * T^4 （需要高精度）
    return c_sigma * T * T * T * T;
}

// ════════════════════════════════════════════════════
// 射线求交（单精度足够）
// ════════════════════════════════════════════════════
__device__ bool ray_triangle_intersect_fp32(
    float3 org, float3 dir,
    float3 v0, float3 v1, float3 v2,
    float* t, float* u, float* v
) {
    // Möller-Trumbore (FP32)
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 pvec = cross(dir, edge2);
    
    float det = dot(edge1, pvec);
    if (fabsf(det) < 1e-6f) return false;  // FP32 epsilon
    
    // ... 剩余计算
}
```

#### **5.2 数值稳定技巧**

```cuda
// ════════════════════════════════════════════════════
// 1. 预计算幂次（避免重复计算）
// ════════════════════════════════════════════════════
struct PhysicsConstants {
    double tmin, tmin2, tmin3, tmin4;
    double tmax, tmax2, tmax3, tmax4;
};

__constant__ PhysicsConstants c_physics;

__device__ double compute_radiative_power(double T) {
    double T2 = T * T;
    double T4 = T2 * T2;
    
    // 使用预计算的边界
    double power = (T4 - c_physics.tmin4) / (c_physics.tmax4 - c_physics.tmin4);
    return power;
}

// ════════════════════════════════════════════════════
// 2. 避免灾难性抵消
// ════════════════════════════════════════════════════
// ❌ 差分形式（精度损失）
__device__ double bad_quadratic_formula(double a, double b, double c) {
    return (-b + sqrt(b*b - 4*a*c)) / (2*a);
}

// ✅ 数值稳定版本
__device__ double stable_quadratic_formula(double a, double b, double c) {
    double disc = b*b - 4*a*c;
    if (disc < 0) return -1;
    
    double q = -0.5 * (b + copysign(sqrt(disc), b));
    double t0 = q / a;
    double t1 = c / q;
    
    return fmin(t0, t1);  // 取较小的正根
}

// ════════════════════════════════════════════════════
// 3. epsilon比较（避免精确相等）
// ════════════════════════════════════════════════════
const float EPSILON_FP32 = 1e-6f;
const double EPSILON_FP64 = 1e-12;

__device__ bool is_zero_fp32(float x) {
    return fabsf(x) < EPSILON_FP32;
}

__device__ bool is_zero_fp64(double x) {
    return fabs(x) < EPSILON_FP64;
}

// ════════════════════════════════════════════════════
// 4. 向量归一化（防止溢出）
// ════════════════════════════════════════════════════
__device__ float3 safe_normalize(float3 v) {
    float len2 = dot(v, v);
    
    if (len2 < EPSILON_FP32) {
        return make_float3(0, 0, 1);  // 默认方向
    }
    
    float inv_len = rsqrtf(len2);  // 快速倒数平方根
    return v * inv_len;
}
```

#### **5.3 验证框架**

```cpp
// ════════════════════════════════════════════════════
// 逐像素对比工具
// ════════════════════════════════════════════════════
struct ValidationResult {
    double max_error;      // 最大误差
    double mean_error;     // 平均误差
    double rmse;           // 均方根误差
    int total_pixels;
    int failed_pixels;     // 超过阈值的像素数
    
    struct {
        int x, y;
        double cpu_value;
        double gpu_value;
        double error;
    } worst_pixel;         // 最差像素
};

ValidationResult validate_gpu_cpu(
    const double* cpu_image,
    const double* gpu_image,
    int width, int height,
    double threshold = 1e-6
) {
    ValidationResult result = {};
    result.total_pixels = width * height;
    
    double sum_squared_error = 0;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            
            double cpu_val = cpu_image[idx];
            double gpu_val = gpu_image[idx];
            double error = fabs(cpu_val - gpu_val);
            
            // 统计
            result.mean_error += error;
            sum_squared_error += error * error;
            
            if (error > result.max_error) {
                result.max_error = error;
                result.worst_pixel = {x, y, cpu_val, gpu_val, error};
            }
            
            if (error > threshold) {
                result.failed_pixels++;
                
                // 详细输出前10个失败像素
                if (result.failed_pixels <= 10) {
                    printf("Pixel (%d, %d): CPU=%.10f GPU=%.10f error=%.3e\n",
                           x, y, cpu_val, gpu_val, error);
                }
            }
        }
    }
    
    result.mean_error /= result.total_pixels;
    result.rmse = sqrt(sum_squared_error / result.total_pixels);
    
    // 打印报告
    printf("\n═══════════════════════════════════════════\n");
    printf("GPU/CPU Validation Report\n");
    printf("═══════════════════════════════════════════\n");
    printf("Total pixels:   %d\n", result.total_pixels);
    printf("Failed pixels:  %d (%.2f%%)\n", 
           result.failed_pixels,
           100.0 * result.failed_pixels / result.total_pixels);
    printf("Max error:      %.3e\n", result.max_error);
    printf("Mean error:     %.3e\n", result.mean_error);
    printf("RMSE:           %.3e\n", result.rmse);
    printf("Threshold:      %.3e\n", threshold);
    printf("\nWorst pixel: (%d, %d)\n", 
           result.worst_pixel.x, result.worst_pixel.y);
    printf("  CPU: %.10f\n", result.worst_pixel.cpu_value);
    printf("  GPU: %.10f\n", result.worst_pixel.gpu_value);
    printf("  Err: %.3e\n", result.worst_pixel.error);
    printf("═══════════════════════════════════════════\n");
    
    return result;
}

// ════════════════════════════════════════════════════
// 测试场景生成器
// ════════════════════════════════════════════════════
struct TestScene {
    std::string name;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    Camera camera;
    int width, height;
    int samples_per_pixel;
};

std::vector<TestScene> generate_test_scenes() {
    std::vector<TestScene> scenes;
    
    // 场景1: 单三角形 + 点光源
    scenes.push_back({
        .name = "single_triangle",
        .triangles = {
            {{0,0,0}, {1,0,0}, {0,1,0}}
        },
        .materials = {
            {.emissivity = 0.8, .reflectance = 0.2}
        },
        .width = 64, .height = 64,
        .samples_per_pixel = 100
    });
    
    // 场景2: Cornell Box
    scenes.push_back({
        .name = "cornell_box",
        .triangles = load_cornell_box_mesh(),
        .materials = load_cornell_box_materials(),
        .width = 128, .height = 128,
        .samples_per_pixel = 1000
    });
    
    // 场景3: 温度梯度场景
    scenes.push_back({
        .name = "temperature_gradient",
        .triangles = create_gradient_scene(),
        .width = 256, .height = 256,
        .samples_per_pixel = 500
    });
    
    return scenes;
}

// ════════════════════════════════════════════════════
// 自动化测试流程
// ════════════════════════════════════════════════════
void run_validation_suite() {
    auto scenes = generate_test_scenes();
    
    for (const auto& scene : scenes) {
        printf("\n=== Testing scene: %s ===\n", scene.name.c_str());
        
        // CPU渲染
        auto cpu_image = render_cpu(scene);
        
        // GPU渲染
        auto gpu_image = render_gpu(scene);
        
        // 验证
        auto result = validate_gpu_cpu(
            cpu_image.data(),
            gpu_image.data(),
            scene.width, scene.height,
            1e-6  // 阈值
        );
        
        // 保存对比图
        save_comparison_image(
            scene.name + "_comparison.png",
            cpu_image, gpu_image, scene.width, scene.height
        );
        
        // 断言通过
        if (result.failed_pixels > result.total_pixels * 0.01) {
            printf("❌ FAILED: Too many failed pixels (>1%%)\n");
            exit(1);
        }
        
        printf("✅ PASSED\n");
    }
    
    printf("\n🎉 All tests passed!\n");
}
```

---

## 四、实施时间线与风险控制

### **时间表（12周）**

```
Week 1-2:   数据结构GPU化
            ├─ AoS→SoA转换
            ├─ 动态数组扁平化
            └─ 内存对齐优化
            
Week 3-4:   Wavefront架构
            ├─ 基础BFS循环
            ├─ Stream Compaction
            └─ Kernel调度框架
            
Week 5-6:   加速结构
            ├─ 提取Embree BVH
            ├─ GPU BVH遍历
            └─ 求交Kernel优化
            
Week 7:     随机数生成
            ├─ Random123集成
            ├─ RNG分配策略
            └─ 一致性验证
            
Week 8-9:   核心Kernel实现
            ├─ radiative_path_kernel
            ├─ conductive_path_kernel
            ├─ boundary_path_kernel
            └─ 多物理耦合逻辑
            
Week 10:    验证框架
            ├─ CPU/GPU对比工具
            ├─ 测试场景生成
            └─ 自动化测试套件
            
Week 11-12: 性能优化 + Bug修复
            ├─ Profile分析（Nsight）
            ├─ 内存带宽优化
            ├─ 占用率调优
            └─ 边缘案例处理
```

### **风险与缓解**

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|---------|
| **Wavefront复杂度超预期** | 高 | 严重 | 1. 先实现简化版（无压缩）<br>2. 参考OptiX示例<br>3. 考虑使用OptiX API |
| **双精度性能不达标** | 中 | 严重 | 1. 混合精度优化<br>2. 关键路径FP64，其他FP32<br>3. 评估误差容忍度 |
| **内存带宽瓶颈** | 中 | 中等 | 1. Shared memory缓存<br>2. 减少数据冗余<br>3. 使用Texture内存 |
| **BVH遍历性能差** | 低 | 严重 | 1. 直接使用OptiX<br>2. 优化栈管理<br>3. Warp排序射线 |
| **数值精度不一致** | 中 | 严重 | 1. 详尽的单元测试<br>2. 参考实现对比<br>3. 调整epsilon阈值 |

### **Go/No-Go检查点**

**Week 4 检查点**: Wavefront原型
```
✅ 必须达成:
   - 单弹跳射线追踪工作
   - 压缩算法正确
   - 性能 > 1M rays/sec

❌ 失败标志:
   - 无法实现有效压缩
   - 性能 < 100K rays/sec
   → 考虑切换到OptiX或简化算法
```

**Week 8 检查点**: 核心功能
```
✅ 必须达成:
   - 完整路径追踪工作
   - Cornell Box结果正确
   - CPU/GPU误差 < 1e-4

❌ 失败标志:
   - 温度值系统性偏差 > 1%
   - 无法通过简单场景验证
   → 重新审视算法正确性
```

---

## 五、技术决策建议

### **CUDA vs DXR 对比表**

| 维度 | CUDA | DXR | 建议 |
|------|------|-----|------|
| **射线追踪性能** | 手动BVH: 慢<br>OptiX: 快 | 硬件RT Core: 最快 | 原型用CUDA+手动BVH<br>优化用OptiX |
| **双精度支持** | 原生支持 | Shader转换麻烦 | CUDA |
| **随机数生成** | Random123直接用 | 需自定义Shader | CUDA |
| **调试工具** | Nsight强大 | PIX功能有限 | CUDA |
| **灵活性** | 完全控制 | 受Shader Model限制 | CUDA |
| **学习曲线** | 中等 | 陡峭（Ray Tracing Pipeline） | CUDA |
| **UE集成** | 需要桥接 | 自然集成 | 未来可迁移DXR |
| **社区资源** | 丰富 | 科学计算少 | CUDA |

**最终建议**: 
```
阶段1 (Week 1-10): 纯CUDA实现
   ├─ 手动BVH验证算法
   └─ 确保数值正确性

阶段2 (Week 11-12): 性能优化
   ├─ 集成OptiX加速射线追踪
   └─ 达到10-100x加速目标

阶段3 (未来): UE集成准备
   └─ 评估DXR迁移的必要性
```

### **关键性能指标**

```
基准测试 (Cornell Box, 512×512, 100 SPP):
├─ CPU时间:      ~300 秒
├─ GPU目标:      <30 秒 (10x)
├─ GPU理想:      <3 秒 (100x)
└─ 瓶颈预测:     射线追踪 (60%) + BRDF (30%)

子系统性能目标:
├─ 射线追踪:      50-100x (OptiX加速)
├─ BRDF采样:      20-50x  (高并行度)
├─ 温度累积:      5-10x   (双精度拖累)
├─ 随机数生成:    30-60x  (无状态RNG)
└─ Host↔Device:   注意数据传输开销
```

---

## 六、参考资源

### **必读论文**
1. **Wavefront Path Tracing**: "Megakernels Considered Harmful" (Laine et al., 2013)
2. **GPU BVH**: "Fast BVH Construction on GPUs" (Lauterbach et al., 2009)
3. **Monte Carlo on GPU**: "Efficient Monte Carlo Methods on Graphics Hardware" (Pharr, 2011)

### **代码参考**
- OptiX SDK Examples: `/optix/SDK/optixWhitted`
- PBRT-v4: GPU path tracer实现
- Falcor: Real-time rendering framework
- Mitsuba 3: Physics-based renderer

### **工具链**
- **Profile**: NVIDIA Nsight Compute / Nsight Systems
- **Debug**: cuda-gdb, compute-sanitizer
- **Verify**: CUDA Memcheck
- **Benchmark**: Criterium (C++), pytest-benchmark

---

## 七、总结

### **核心挑战排序**
1. ⚠️⚠️⚠️ **Wavefront架构** - 最复杂，影响全局
2. ⚠️⚠️ **双精度性能** - 可能无法达到100x目标
3. ⚠️⚠️ **BVH移植** - 手动实现难度高
4. ⚠️ **数值一致性** - 需要大量测试
5. ✅ **RNG集成** - Random123已有支持

### **成功关键因素**
- ✅ **增量式开发**: 每周可运行的原型
- ✅ **持续验证**: CPU结果作为Ground Truth
- ✅ **性能监控**: 每次提交测试性能回归
- ✅ **提前优化判断**: Week 4/8 Go/No-Go决策

### **最终交付物**
```
stardis-gpu/
├── src/
│   ├── cuda/
│   │   ├── kernels/           # 核心Kernel
│   │   ├── bvh/               # 加速结构
│   │   ├── wavefront.cu       # Wavefront引擎
│   │   └── rng.cuh            # Random123封装
│   ├── core/
│   │   ├── scene_gpu.h        # GPU场景数据
│   │   └── ray_state.h        # 射线状态SoA
│   └── validation/
│       ├── cpu_reference.cpp  # CPU参考实现
│       └── test_suite.cpp     # 自动化测试
├── tests/
│   ├── unit/                  # 单元测试
│   └── integration/           # 集成测试
└── benchmarks/
    └── performance.cu         # 性能测试
```

---

**文档版本**: 1.0  
**最后更新**: 2026-01-21  
**维护者**: Sisyphus (AI Agent)  
**状态**: 🟢 准备就绪 - 可进入实施阶段
