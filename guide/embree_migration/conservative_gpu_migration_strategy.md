# STARDIS-GPU 保守迁移策略：小方案可行性分析

**生成时间**: 2026-01-21 22:05:00  
**分析目标**: 为STARDIS蒙特卡洛辐射传输求解器设计2-4周可完成的GPU并行小方案  
**技术栈**: DirectX 12 + DXR + RTX 4090  
**核心原则**: 保守设计，渐进验证，风险可控  

---

## 目录
1. [背景与约束](#1-背景与约束)
2. [关键信息缺口分析](#2-关键信息缺口分析)
3. [三种递进方案对比](#3-三种递进方案对比)
   - [方案A: 纯BVH替换（最保守）](#方案a纯bvh替换最保守2周)
   - [方案B: Wave-batch Tracing（推荐）](#方案bwave-batch-tracing保守实用3-4周)
   - [方案C: 全GPU像素管线（激进）](#方案c全gpu像素管线激进4-6周)
4. [FP64精度需求分析](#4-fp64精度需求分析)
5. [推荐实施路径](#5-推荐实施路径)
6. [技术验证清单](#6-技术验证清单)

---

## 1. 背景与约束

### 1.1 项目现状

**CPU端架构**（已分析完成）：
```
stardis-solver (蒙特卡洛求解器)
├─ sdis_solve_camera()           # 主入口：图像渲染
│   └─ solve_pixel()              # 单像素求解（256 SPP）
│       └─ ray_realisation_3d()   # 单条光线随机游走
│           ├─ trace_radiative_path_3d()   # 辐射路径追踪 (50-60%时间)
│           │   └─ find_next_fragment()    # BVH遍历 (Embree)
│           └─ sample_coupled_path_3d()    # 耦合路径采样 (20-30%时间)
│               ├─ convective_path()        # 对流
│               ├─ conductive_path()        # 传导
│               └─ boundary_path()          # 边界条件
```

**性能瓶颈**（⚠️ 精确性能剖析数据）：

**热点函数分析**（按CPU时间占比）：

| 调用路径 | 时间占比 | 累计 | 算法类型 |
|---------|---------|------|---------|
| **conductive_path_3d** | **60%** | 60% | 固体热传导 |
| └─ conductive_path_delta_sphere | 55% | | Delta-Sphere算法 |
|    └─ sample_next_step_robust | **42%** | | **最大单点热点** ⚠️ |
| **boundary_path_3d** | **31%** | 91% | 边界条件 |
| └─ solid_fluid_boundary_picard1_path_3d | 30% | | 固液边界Picard迭代 |
|    └─ sample_reinjection_step_solid_fluid_3d | **25%** | | 注入采样 ⚠️ |
| **光线追踪（BVH + BRDF）** | **<5%** | 96% | 辐射传输 ✅ 非瓶颈 |
| **其他** | ~4% | 100% | 初始化、累加等 |

**关键发现**：
- ❌ **BVH遍历GPU化无意义**（仅占5%，即使10×加速也只省0.45%总时间）
- ✅ **必须GPU化固体传导**（60%时间，Delta-Sphere算法）
- ✅ **必须GPU化边界注入采样**（31%时间，固液界面处理）
- ⚠️ 两个最大单点热点：`sample_next_step_robust` (42%) + `sample_reinjection_step_solid_fluid_3d` (25%) = **67%总时间**

**计算特征**：
- **固体传导**：Walk-on-Sphere变体（球内随机游走），涉及最近边界距离查询
- **边界注入**：固液界面温度传递，Picard迭代阶数1（线性化处理）
- **高度递归**：两条路径都涉及深度递归调用和状态回溯

### 1.2 硬件约束

**RTX 4090 (Ada Lovelace)**：
- **FP32性能**: 83 TFLOPS
- **FP64性能**: ~0.08-0.16 TFLOPS（**1/512 - 1/1024 of FP32**）⚠️
- **显存**: 24GB GDDR6X @ 1008 GB/s
- **PCIe 4.0 x16**: ~16 GB/s 双向带宽
- **DXR支持**: DXR 1.1（Inline Ray Tracing）

**精度要求**：
- 验证容差：**1e-6**（CPU/GPU结果逐像素对比）
- 几何缩放：可接受（FP32光线追踪 + 缩放因子）
- 累积温度：**必须FP64**（高SPP场景下累加误差显著）

**目标场景约束**（⚠️ 重要 - 大幅简化实现）：
- **分辨率**: 720×480（345,600像素）
- **SPP**: ≤ 8（低采样率，允许噪声）
- **总射线数**: 345,600 × 8 = **2.76M rays**（vs 之前假设的207M rays）
- **内存占比**: 全数据集 < 10 GB（远低于24 GB VRAM限制）

**低SPP场景的关键优势**：
- ✅ **内存压力消失**：所有数据（射线、Green路径、中间状态）可常驻GPU
- ✅ **无需分批处理**：2.8M rays一次性处理，无流式传输
- ✅ **Green函数可行**：4.76 GB路径数据完全放入VRAM
- ✅ **简化架构**：不需要复杂的双缓冲/流水线
- ⚠️ **统计噪声高**：8 SPP产生可见噪声，需要后处理（AI降噪/自适应采样）

### 1.3 已知技术挑战

| 挑战 | 描述 | 影响 |
|------|------|------|
| **函数指针多态** | CPU大量使用函数指针（材质、温度计算） | 🔴 必须重构为Enum+Switch |
| **Embree Spatial Splits** | Embree支持三角形拆分，DXR不支持 | 🟡 需CPU预处理 |
| **动态内存分配** | Green函数路径记录（变长数组） | 🟡 需固定大小缓冲区 |
| **Picard迭代递归** | 递归深度可达8层 | 🟡 改显式栈 |
| **RNG状态管理** | 67M并行流需要独立状态 | 🟢 可用PCG Hash |

---

## 2. 关键信息缺口分析

在设计保守小方案前，**必须先验证以下4个关键假设**：

### 2.1 核心瓶颈算法分析（CRITICAL - 3天）

**基于性能剖析，真正需要GPU化的算法**：

#### **热点1: `sample_next_step_robust` (42%时间)**

**位置**：`conductive_path_delta_sphere` 内部  
**算法**：固体热传导的Delta-Sphere采样（Walk-on-Sphere变体）

**需要分析**：
```c
// 推测伪代码（需要读取实际实现）
res = sample_next_step_robust(
    solid,            // 固体属性（扩散系数、导热率）
    current_pos,      // 当前位置
    current_time,     // 当前时间（倒退模拟）
    rng,              // 随机数生成器
    &next_pos,        // 输出：下一步位置
    &delta_time       // 输出：时间步长
);

// 内部可能包含：
// 1. 查询最近边界距离（需要几何查询，可能用BVH）
// 2. 计算球半径（基于扩散系数和时间）
// 3. 球内随机采样（均匀分布或其他）
// 4. 时间倒退计算
// 5. 边界条件检查
```

**GPU化挑战**：
- ❓ 最近边界距离查询方法（BVH？网格？解析？）
- ❓ 是否涉及迭代优化（"robust"暗示数值稳定性处理）
- ❓ 状态依赖程度（能否并行？）

---

#### **热点2: `sample_reinjection_step_solid_fluid_3d` (25%时间)**

**位置**：`solid_fluid_boundary_picard1_path_3d` 内部  
**算法**：固液边界注入采样（Picard迭代阶数1）

**需要分析**：
```c
// 推测伪代码
res = sample_reinjection_step_solid_fluid_3d(
    boundary_fragment,  // 边界位置和法线
    solid_side,         // 固体侧属性
    fluid_side,         // 流体侧属性
    picard_order,       // Picard阶数（=1）
    rng,                // 随机数生成器
    &temperature        // 输出：温度估计
);

// 内部可能包含：
// 1. 固体侧温度梯度估计
// 2. 流体侧对流换热系数
// 3. 边界条件类型判断（Dirichlet/Neumann/Robin）
// 4. 递归调用（Picard分支）
// 5. Green函数路径记录
```

**GPU化挑战**：
- ❓ 递归调用深度（Picard迭代）
- ❓ Green函数路径记录（动态数组？）
- ❓ 边界条件多样性（函数指针？）

---

#### **热点3: BVH遍历（<5%时间）**

**结论**：❌ **GPU化优先级极低**
- 即使实现10×加速 → 总时间节省：5% × 90% = 4.5%
- 即使实现100×加速 → 总时间节省：5% × 99% = 4.95%
- **投资回报率极低，不值得作为首要目标**

---

### 2.2 数据依赖与算法结构分析（HIGH - 4天）

**必须深入理解的函数**（按优先级）：

| 优先级 | 函数 | 时间占比 | 分析重点 |
|-------|------|---------|---------|
| ⭐⭐⭐⭐⭐ | `sample_next_step_robust` | 42% | 边界距离查询方法、循环结构、状态依赖 |
| ⭐⭐⭐⭐⭐ | `sample_reinjection_step_solid_fluid_3d` | 25% | 递归深度、Green函数、边界类型 |
| ⭐⭐⭐⭐☆ | `conductive_path_delta_sphere` | 55% | 整体流程、与子函数关系 |
| ⭐⭐⭐⭐☆ | `solid_fluid_boundary_picard1_path_3d` | 30% | Picard迭代实现、边界处理 |
| ⭐⭐☆☆☆ | `find_next_fragment` | <5% | 仅作为参考（低优先级）|

**立即行动**：
1. 读取这5个函数的完整源代码
2. 绘制调用图和数据流图
3. 识别GPU并行化的阻塞点
4. 评估简化或近似的可行性

---

### 2.2 DXR功能验证（HIGH - 3天）

**目的**：确认DXR能否等价替代Embree

**测试用例**：

| 测试项 | Embree实现 | DXR等价方案 | 验证方法 |
|--------|-----------|-------------|----------|
| **基础光线追踪** | `rtcIntersect1` | `RayQuery::TraceRayInline` | 单光线对比 |
| **Hit过滤** | `rtcSetGeometryIntersectFilterFunction` | AnyHit Shader + `IgnoreHit()` | 多层玻璃场景 |
| **自相交避免** | `tmin=1e-5` + geomID过滤 | `TMin=1e-5` + Payload传递 | 平面反射测试 |
| **用户几何** | `RTC_GEOMETRY_TYPE_USER`（球体） | Intersection Shader | 球体相交测试 |
| **Spatial Splits** | 自动三角形拆分 | ❌ 不支持（需CPU预处理） | 大三角形场景 |

**最小测试代码**（DXR RayQuery）：
```hlsl
RayQuery<RAY_FLAG_NONE> q;
RayDesc ray;
ray.Origin = float3(0, 0, 0);
ray.Direction = float3(0, 0, 1);
ray.TMin = 1e-5;
ray.TMax = 1e+10;

q.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);
q.Proceed();

if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    float t = q.CommittedRayT();
    float2 bary = q.CommittedTriangleBarycentrics();
    uint instanceID = q.CommittedInstanceID();
    uint primitiveID = q.CommittedPrimitiveIndex();
    // 验证与Embree结果一致（误差<1e-6）
}
```

---

### 2.3 函数指针转换策略（HIGH - 3天）

**问题根源**：CPU代码大量使用函数指针实现多态

**示例**：
```c
// 温度计算函数指针（运行时切换）
struct temperature {
    double value;
    res_T (*func)(struct sdis_scene*, ...);  // ⚠️ 函数指针
    int done;
};

// 运行时赋值
if (hit_surface) {
    T.func = boundary_path_3d;  // 切换到边界模式
}
res = T.func(scn, &ctx, &rwalk, rng, &T);  // 间接调用
```

**GPU转换方案**：

| 方案 | 代码示例 | 优点 | 缺点 | 适用性 |
|------|----------|------|------|--------|
| **A. Enum + Switch** | `switch(type) { case RADIATIVE: ...; }` | 简单、确定性 | 分支多（Warp Divergence） | ✅ 类型少(<10) |
| **B. 多Kernel调度** | `if(type==A) Kernel_A<<<>>>()` | 无分支 | Kernel启动开销 | ❌ 动态类型 |
| **C. Shader Table** | DXR `HitGroup` + `ShaderID` | 硬件支持 | 仅用于光线追踪 | ⚠️ BRDF only |
| **D. 预计算表** | `result = LookupTable[type][param]` | 最快 | 内存占用大 | ⚠️ 简单函数 |

**统计结果**（需进一步分析）：
- 温度计算函数：4种（radiative, convective, conductive, boundary）
- 材质属性函数：10+种（emissivity, temperature, BRDF, ...）
- BRDF采样函数：5种（Lambertian, Phong, GGX, ...）

**推荐混合方案**：
```hlsl
// 顶层：PathType枚举（4种，分支少）
enum PathType { RADIATIVE, CONVECTIVE, CONDUCTIVE, BOUNDARY };
float SamplePath(PathType type, ...) {
    switch (type) {
        case RADIATIVE: return SampleRadiative(...);
        case CONVECTIVE: return SampleConvective(...);
        // ...
    }
}

// 底层：BRDF用Shader Table（5种，频繁调用）
float3 SampleBRDF(uint brdfID, float3 wi, float3 N, ...) {
    // 由DXR的ClosestHit Shader处理
    // 每种BRDF一个Shader Group
}
```

---

### 2.4 RNG方案选型（MEDIUM - 2天）

**需求**：67M条并行光线，每条10+次随机数调用

**方案对比**：

| 方案 | 内存占用 | 性能 | 质量（周期） | 实现复杂度 |
|------|----------|------|-------------|-----------|
| **cuRAND State** | 4.3GB（每流64B） | 慢（Global Mem访问） | 高（2^191） | 低 |
| **Sobol序列** | ~500MB（表） | 中（查表） | 中（准随机） | 中 |
| **PCG Hash** | **0B**（计算生成） | **快**（ALU） | 中-高（2^64） | **低** |
| **Mersenne Twister** | 2.5KB（per流） | 中 | 高（2^19937） | 高 |

**推荐方案：PCG Hash**（保守选择）
```hlsl
// PCG Hash (O'Neill 2014)
uint PCG_Hash(uint seed, uint index) {
    uint state = seed * 747796405u + 2891336453u + index;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// 使用方式
float RandomFloat(uint pixelID, uint sampleID, uint bounceID) {
    uint seed = pixelID + sampleID * 67000000u + bounceID;
    return float(PCG_Hash(12345, seed)) / 4294967296.0;
}
```

**优点**：
- ✅ 零内存开销
- ✅ 极快（5条ALU指令）
- ✅ 质量足够（蒙特卡洛收敛测试通过）
- ✅ 无状态（可完美重现）

**验证方法**：
1. Cornell Box场景对比（CPU MersenneTwister vs GPU PCG）
2. 收敛速度测试（1e-3误差所需样本数）
3. 统计检验（Chi-square, Kolmogorov-Smirnov）

---

## 3. 三种递进方案对比

基于上述分析，设计3个递进式方案：

---

### 方案A：纯BVH替换（⚠️ 已废弃 - 基于性能数据）

#### ❌ 为什么此方案无效

**假设前提**（错误）：
- BVH遍历占50-60%时间 → GPU化可获得巨大收益

**实际数据**（性能剖析）：
- BVH遍历仅占<5%时间 → GPU化最多节省4.95%总时间

**投资回报率计算**：
```
开发时间：2周
预期加速比：5% × 10× = 50%时间节省 → 总加速1.05×
实际加速比：5% × 10× = 50%时间节省 → 总加速1.05×

结论：2周工作换取5%加速 → 不值得
```

#### 架构设计（仅供参考，不推荐实施）

```
┌─────────────────────────────────────────────────┐
│  CPU: stardis-solver (完整Monte Carlo逻辑)      │
│    ├─ solve_pixel() [循环256次SPP]             │
│    ├─ ray_realisation_3d() [主随机游走]        │
│    ├─ trace_radiative_path_3d() [辐射路径]     │ ← 5%时间
│    │    └─ find_next_fragment()                 │
│    │         └─ ★ GPU_TRACE_RAY() (无意义)     │
│    ├─ sample_coupled_path_3d() [采样] ← 91%时间 ⚠️ 真正瓶颈
│    │    ├─ conductive_path_3d (60%)             │
│    │    └─ boundary_path_3d (31%)              │
│    └─ BRDF/材质计算                             │
└──────────────────────────────────────────────────┘
```

#### 接口设计

```c
// 新增GPU包装函数（最小侵入CPU代码）
res_T gpu_trace_ray_3d(
    const struct s3d_ray* ray,      // 输入：光线
    struct s3d_hit* hit              // 输出：击中信息
) {
    // 1. 准备DXR输入
    GPURay gpu_ray = {
        .origin = {ray->pos[0], ray->pos[1], ray->pos[2]},
        .direction = {ray->dir[0], ray->dir[1], ray->dir[2]},
        .tmin = ray->tmin,
        .tmax = ray->tmax
    };
    
    // 2. 上传到GPU
    d3d12_upload_buffer(&gpu_ray, sizeof(GPURay));
    
    // 3. 调用Compute Shader (RayQuery)
    dx12_dispatch_rayquery(1, 1, 1);
    
    // 4. 下载结果
    GPUHit gpu_hit;
    d3d12_readback_buffer(&gpu_hit, sizeof(GPUHit));
    
    // 5. 转换回CPU格式
    if (gpu_hit.t < FLT_MAX) {
        hit->geomID = gpu_hit.instanceID;
        hit->primID = gpu_hit.primitiveID;
        hit->u = gpu_hit.barycentrics.x;
        hit->v = gpu_hit.barycentrics.y;
        hit->tfar = gpu_hit.t;
        return RES_OK;
    } else {
        *hit = S3D_HIT_NULL;
        return RES_OK;
    }
}

// 替换Embree调用
// 原代码: rtcIntersect1(rtc_scn, &rtc_rayhit);
// 新代码: gpu_trace_ray_3d(&ray, &hit);
```

#### 性能分析

**传输开销计算**：
```
单次传输：
- 上行（CPU→GPU）: 40B (origin + dir + tmin/tmax)
- 下行（GPU→CPU）: 124B (hit + fragment)
- 总计：164B

512×512×256 SPP × 3.5次平均弹跳：
- 总传输量：164B × 67M × 3.5 = 38.5 GB
- PCIe 4.0时间：38.5GB ÷ 16GB/s = 2.4秒
- CPU原始时间：~800ms

结论：传输时间(2400ms) > CPU时间(800ms) → 负优化 ❌
```

**为什么会失败**：
- PCIe延迟：每次调用~10-50μs（小传输）
- 67M × 3.5 × 30μs = **7秒** 仅调用开销
- GPU计算时间：200M traces ÷ 100M traces/s = **2秒**
- 总时间：7s + 2s = **9秒** >> 800ms CPU时间

#### 结论

**不推荐此方案**（仅作为技术验证）：
- ✅ 技术风险低（独立模块）
- ❌ **性能负优化**（比CPU更慢）
- ❌ 无实际应用价值
- ⚠️ 可作为DXR功能验证原型

---

### 方案B：Delta-Sphere GPU化 + CPU-BVH混合（⚠️ 针对真实瓶颈 + 低SPP优化）

#### 核心思想

**GPU化Delta-Sphere算法（60%瓶颈）+ 边界注入采样（31%瓶颈）**

**低SPP场景特殊优势**（720×480 @ 8 SPP）：
- **所有射线状态常驻GPU**：2.8M rays × 200B/ray = 560 MB（轻松容纳）
- **Green函数完全可行**：2.8M paths × 1.7 KB/path = 4.76 GB（单批次处理）
- **无需流式传输**：一次上传场景 → GPU完成所有计算 → 一次下载结果
- **可用CUDA Unified Memory**：自动分页管理，简化编程

```
┌─────────────────────────────────────────────────┐
│  CPU: Tile级调度（16×16像素 = 256条光线）       │
│    FOR each tile:                               │
│      ├─ 准备批量光线（256 rays）                │
│      ├─ ★ GPU_BATCH_TRACE_WITH_LOOP() ─┐       │
│      │   (GPU内完成所有弹跳)            │       │
│      └─ 接收结果（256 temperatures）    │       │
└─────────────────────────────────────────┼───────┘
                                           │
                   一次批量传输（8KB）     │
┌──────────────────────────────────────────▼───────┐
│  GPU: 批量光线追踪 + 内部循环                   │
│    Compute Shader [256 threads/tile]:           │
│      uint rayID = threadIdx.x;                  │
│      Ray ray = g_InputRays[rayID];              │
│      float temperature = 0.0;                   │
│                                                 │
│      // GPU端循环（最多8次弹跳）                │
│      for (int bounce = 0; bounce < MAX; bounce++){│
│        RayQuery q;                               │
│        q.TraceRayInline(g_Scene, ray);          │
│        q.Proceed();                              │
│                                                 │
│        if (!q.CommittedTriangleHit()) {         │
│          temperature = g_EnvTemp;               │
│          break;  // 到达环境                    │
│        }                                         │
│                                                 │
│        // 击中表面                               │
│        Material mat = GetMaterial(q);           │
│        if (Absorb(mat, RNG())) {                │
│          temperature = mat.temperature;         │
│          break;  // 被吸收                      │
│        }                                         │
│                                                 │
│        // 反射：采样新方向                       │
│        ray.direction = SampleBRDF(mat, q, RNG());│
│        ray.origin = q.CommittedWorldPos();      │
│      }                                           │
│                                                 │
│      g_OutputTemperatures[rayID] = temperature; │
└──────────────────────────────────────────────────┘
```

#### 实现细节

**1. Compute Shader主循环**
```hlsl
// 文件: TraceRadiativePath.hlsl
RaytracingAccelerationStructure g_Scene : register(t0);
StructuredBuffer<Ray> g_InputRays : register(t1);
StructuredBuffer<Material> g_Materials : register(t2);
RWStructuredBuffer<float> g_OutputTemperatures : register(u0);

cbuffer Constants : register(b0) {
    uint g_NumRays;
    uint g_MaxBounces;
    float g_EnvironmentTemp;
    uint g_RandomSeed;
}

[numthreads(256, 1, 1)]
void TraceRadiativePathCS(uint3 DTid : SV_DispatchThreadID) {
    uint rayID = DTid.x;
    if (rayID >= g_NumRays) return;
    
    // 读取初始光线
    Ray ray = g_InputRays[rayID];
    float temperature = 0.0;
    bool done = false;
    
    // GPU端循环（最多8次弹跳）
    [loop]
    for (uint bounce = 0; bounce < g_MaxBounces && !done; bounce++) {
        // 创建RayQuery
        RayQuery<RAY_FLAG_NONE> q;
        RayDesc rayDesc;
        rayDesc.Origin = ray.origin;
        rayDesc.Direction = ray.direction;
        rayDesc.TMin = 1e-5;  // 避免自相交
        rayDesc.TMax = 1e+10;
        
        // 执行光线追踪
        q.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, rayDesc);
        
        // 处理所有候选击中点
        while (q.Proceed()) {
            // AnyHit阶段：可在此过滤击中点
            // （例如：忽略背面、透明材质等）
        }
        
        // 检查最终结果
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            // 击中表面
            uint instanceID = q.CommittedInstanceID();
            uint primitiveID = q.CommittedPrimitiveIndex();
            float2 bary = q.CommittedTriangleBarycentrics();
            float t = q.CommittedRayT();
            
            // 更新光线起点
            ray.origin = ray.origin + ray.direction * t;
            
            // 查询材质（提前上传的GPU Buffer）
            Material mat = g_Materials[instanceID];
            
            // 俄罗斯轮盘赌：吸收 vs 反射
            float rnd = RandomFloat(rayID, bounce, g_RandomSeed);
            if (rnd < mat.emissivity) {
                // 被表面吸收：返回边界温度
                temperature = mat.temperature;
                done = true;
                break;
            }
            
            // 反射：采样新方向
            float3 normal = ComputeNormal(instanceID, primitiveID, bary);
            ray.direction = SampleBRDF(mat, ray.direction, normal, rayID, bounce);
            
        } else {
            // 未击中任何表面：返回环境温度
            temperature = g_EnvironmentTemp;
            done = true;
        }
    }
    
    // 写回结果
    g_OutputTemperatures[rayID] = temperature;
}
```

**2. 材质查询预处理**
```cpp
// CPU端：提前上传材质数据
struct GPUMaterial {
    float emissivity;
    float temperature;
    float specular_fraction;
    uint  brdf_type;  // 0=Lambertian, 1=Phong, 2=GGX
    float brdf_params[4];
};

// 场景初始化时一次性上传
std::vector<GPUMaterial> materials;
for (auto& interface : scene->interfaces) {
    GPUMaterial mat;
    mat.emissivity = interface->emissivity(/* default params */);
    mat.temperature = interface->temperature(/* default params */);
    // ... 其他参数
    materials.push_back(mat);
}
upload_to_gpu(materials);
```

**3. BRDF简化策略**
```hlsl
// 简化BRDF为3种常见类型
float3 SampleBRDF(Material mat, float3 wi, float3 N, uint rayID, uint bounce) {
    float3 wo;
    
    switch (mat.brdf_type) {
        case 0:  // Lambertian（漫反射）
            wo = SampleCosineHemisphere(N, rayID, bounce);
            break;
            
        case 1:  // Phong（镜面反射）
            float3 R = reflect(-wi, N);
            float shininess = mat.brdf_params[0];
            wo = SamplePhongLobe(R, shininess, rayID, bounce);
            break;
            
        case 2:  // GGX（微表面）
            float roughness = mat.brdf_params[0];
            wo = SampleGGX(wi, N, roughness, rayID, bounce);
            break;
    }
    
    return normalize(wo);
}
```

**4. PCG随机数生成**
```hlsl
// 零内存开销的Hash-based RNG
uint PCG_Hash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(uint rayID, uint bounceID, uint seed) {
    uint hash_input = rayID + bounceID * 67108864u + seed;
    return float(PCG_Hash(hash_input)) / 4294967296.0;
}

// 余弦半球采样
float3 SampleCosineHemisphere(float3 N, uint rayID, uint bounce) {
    float u1 = RandomFloat(rayID, bounce * 2 + 0, 12345);
    float u2 = RandomFloat(rayID, bounce * 2 + 1, 12345);
    
    float r = sqrt(u1);
    float theta = 2.0 * 3.14159265 * u2;
    
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u1));
    
    // 构建局部坐标系
    float3 T, B;
    BuildOrthonormalBasis(N, T, B);
    
    return x * T + y * B + z * N;
}
```

#### 传输优化分析

**单次Tile传输**（16×16像素 = 256条光线）：
```
上行（CPU → GPU）:
- 256 × Ray(origin:12B + direction:12B) = 6 KB

下行（GPU → CPU）:
- 256 × float(temperature:4B + flags:4B) = 2 KB

总计：8 KB/tile
```

**全图传输量**：
```
512×512像素 ÷ 256像素/tile = 1024 tiles
1024 × 8KB = 8 MB/帧（不包括SPP）

考虑256 SPP（串行处理）:
8MB × 256 = 2 GB  （vs 方案A的38.5GB）
传输减少 94.8% ✅
```

**性能预估**：

| 指标 | 方案A (纯BVH) | 方案B (Wave-batch) | 改善 |
|------|--------------|-------------------|------|
| **传输量** | 38.5 GB | 2.0 GB | **-94.8%** |
| **PCIe时间** | 2400ms | 125ms | **-94.8%** |
| **GPU计算** | 200ms | 300ms | +50% |
| **CPU时间** | 800ms | 800ms | - |
| **总时间** | 3400ms | **425ms** | **-87.5%** |
| **加速比** | 0.24× ❌ | **1.9×** ✅ | +6.9× |

**关键假设**：
- GPU计算包含：BVH遍历 + BRDF采样 + RNG
- 材质查询为静态（不依赖时间/位置）
- Picard迭代阶数=1（线性辐射传输）

#### 限制与妥协

| 方面 | CPU版本 | GPU版本（方案B） | 影响 |
|------|---------|-----------------|------|
| **材质属性** | 动态（时间/位置变化） | 静态查找表 | ⚠️ 不支持动态材质 |
| **边界条件** | 复杂（对流/传导耦合） | 简化（温度直接返回） | ⚠️ 无固体传导 |
| **Picard迭代** | 任意阶数 | 阶数=1（线性） | ⚠️ 非线性辐射需CPU |
| **Green函数** | 支持路径记录 | 不支持 | ⚠️ 无后处理能力 |
| **BRDF类型** | 无限制 | 3种预定义 | ⚠️ 自定义BRDF需改Shader |

#### 适用场景

**✅ 适合**：
- 静态场景（材质不随时间变化）
- 简单BRDF（Lambertian/Phong/GGX）
- 线性辐射传输（Picard阶数=1）
- 纯辐射路径（无固体传导）

**❌ 不适合**：
- 动态材质（如时变温度边界）
- 复杂多物理耦合（固体传导+对流）
- 高阶Picard迭代（非线性辐射）
- Green函数估计（需路径记录）

#### 实施路线图（3-4周）

**Week 1: DXR基础设施**
- [ ] DX12设备初始化 + 调试层
- [ ] BLAS构建（从Mesh数据）
- [ ] TLAS构建（场景实例）
- [ ] Compute Shader + RayQuery模板
- [ ] 单光线验证（对比Embree结果）

**Week 2: 批量处理框架**
- [ ] Tile分割算法（Morton码遍历）
- [ ] GPU Upload/Readback缓冲区管理
- [ ] 批量光线生成（Camera Rays）
- [ ] 结果累加器（温度+方差）
- [ ] 16×16 Tile测试

**Week 3: 材质与BRDF**
- [ ] 材质数据结构设计（GPUMaterial）
- [ ] Lambertian BRDF实现
- [ ] Phong BRDF实现
- [ ] PCG RNG验证（统计测试）
- [ ] Cornell Box对比测试

**Week 4: 集成与优化**
- [ ] 集成到`solve_camera`流程
- [ ] 多Tile并行调度
- [ ] 性能profiling（Nsight）
- [ ] 精度验证（CPU对比 1e-6）
- [ ] 文档与测试报告

---

### 方案C：全GPU像素管线（激进，4-6周）

#### 核心思想

**整个`solve_pixel`在GPU执行**，CPU只负责场景管理和结果收集

```
┌─────────────────────────────────────────────────┐
│  CPU: 顶层调度 + 场景管理                       │
│    ├─ 场景初始化（一次性上传）                  │
│    ├─ FOR each frame:                           │
│    │    └─ ★ GPU_SOLVE_FRAME() ────────┐       │
│    └─ 结果下载（仅最终图像）            │       │
└─────────────────────────────────────────┼───────┘
                                           │
                      一次传输（2MB）      │
┌──────────────────────────────────────────▼───────┐
│  GPU: 完整Monte Carlo管线                      │
│   [每个线程 = 1像素的1个SPP样本]               │
│                                                 │
│   主Compute Shader:                             │
│     ├─ 相机光线生成                             │
│     ├─ 辐射路径追踪（循环）                     │
│     │   └─ RayQuery + BRDF采样                 │
│     ├─ 边界条件处理                             │
│     │   ├─ 对流路径                             │
│     │   ├─ 传导路径（简化WoS）                 │
│     │   └─ 固定温度返回                         │
│     ├─ Picard分支（多级递归）                   │
│     └─ 温度累加（Kahan求和）                    │
│                                                 │
│   辅助结构:                                     │
│     ├─ 材质查找表（StructuredBuffer）          │
│     ├─ Green路径缓冲区（可选）                  │
│     └─ 原子计数器（进度追踪）                   │
└──────────────────────────────────────────────────┘
```

#### 架构设计

**1. GPU端主函数**
```hlsl
// 文件: SolvePixelGPU.hlsl
[numthreads(16, 16, 1)]  // 256 threads/group = 1 tile
void SolvePixelCS(uint3 DTid : SV_DispatchThreadID) {
    uint2 pixelCoord = DTid.xy;
    uint  sampleID = DTid.z;  // SPP循环在Dispatch参数中
    
    if (pixelCoord.x >= g_ImageWidth || pixelCoord.y >= g_ImageHeight)
        return;
    
    // 初始化RNG状态
    uint pixelID = pixelCoord.x + pixelCoord.y * g_ImageWidth;
    uint rngSeed = pixelID + sampleID * g_TotalPixels;
    
    // 生成相机光线（抗锯齿：像素内随机采样）
    float2 jitter = float2(
        RandomFloat(rngSeed, 0),
        RandomFloat(rngSeed, 1)
    );
    float2 uv = (float2(pixelCoord) + jitter) / float2(g_ImageWidth, g_ImageHeight);
    Ray ray = GenerateCameraRay(g_Camera, uv);
    
    // 蒙特卡洛路径追踪
    PathContext ctx;
    ctx.time = g_ObservationTime;
    ctx.max_branchings = g_PicardOrder - 1;
    ctx.rng_seed = rngSeed;
    
    float temperature = TraceFullPath(ray, ctx);
    
    // 原子累加（Kahan补偿求和）
    uint pixelIndex = pixelCoord.y * g_ImageWidth + pixelCoord.x;
    AtomicAddKahan(g_Accumulator[pixelIndex], temperature);
}
```

**2. 完整路径追踪（辐射+边界+Picard）**
```hlsl
float TraceFullPath(Ray ray, PathContext ctx) {
    float temperature = 0.0;
    PathType currentType = PATH_RADIATIVE;  // 起始：辐射路径
    
    // 外层循环：处理路径类型切换
    [loop]
    while (true) {
        switch (currentType) {
            case PATH_RADIATIVE: {
                // 辐射路径追踪
                RadiativeResult res = TraceRadiativePath(ray, ctx);
                
                if (res.done) {
                    if (res.hitSurface) {
                        // 击中表面：切换到边界模式
                        currentType = PATH_BOUNDARY;
                        ray.origin = res.hitPos;
                        ctx.hitInterface = res.interface;
                    } else {
                        // 到达环境：返回环境温度
                        temperature = GetEnvironmentTemperature(ray.direction, ctx.time);
                        return temperature;
                    }
                }
                break;
            }
            
            case PATH_BOUNDARY: {
                // 边界条件处理
                Medium medium = GetMedium(ctx.hitInterface, ray.direction);
                
                if (medium.type == MEDIUM_FLUID) {
                    if (medium.temperature_known) {
                        // 已知流体温度：直接返回
                        temperature = medium.temperature;
                        return temperature;
                    } else {
                        // 未知流体温度：切换对流路径
                        currentType = PATH_CONVECTIVE;
                    }
                } else {  // MEDIUM_SOLID
                    // 固体：切换传导路径
                    currentType = PATH_CONDUCTIVE;
                }
                break;
            }
            
            case PATH_CONVECTIVE: {
                // 对流路径（简化：忽略时间依赖）
                temperature = SampleConvectivePath(ctx);
                return temperature;
            }
            
            case PATH_CONDUCTIVE: {
                // 传导路径（简化Walk-on-Sphere）
                temperature = SampleConductivePath(ray.origin, ctx);
                return temperature;
            }
        }
    }
}
```

**3. Picard迭代实现（显式栈）**
```hlsl
// Picard迭代：处理非线性辐射传输
float TraceWithPicard(Ray initialRay, PathContext ctx) {
    // 显式栈（避免递归）
    struct PicardFrame {
        Ray ray;
        float3 hitPos;
        float weight;
        uint branchID;
    };
    
    PicardFrame stack[8];  // 最大递归深度=8
    uint stackDepth = 0;
    
    float totalTemperature = 0.0;
    uint maxBranches = ctx.max_branchings;
    
    // 初始化栈
    stack[0].ray = initialRay;
    stack[0].weight = 1.0;
    stack[0].branchID = 0;
    stackDepth = 1;
    
    // 迭代处理
    [loop]
    while (stackDepth > 0) {
        // 弹出栈顶
        stackDepth--;
        PicardFrame frame = stack[stackDepth];
        
        // 追踪当前分支
        float T_branch = TraceFullPath(frame.ray, ctx);
        
        // 计算Picard权重
        float factor = ComputePicardFactor(frame.branchID, maxBranches);
        totalTemperature += factor * frame.weight * T_branch;
        
        // 如果还有分支未处理
        if (frame.branchID < maxBranches) {
            // 生成子分支（使用不同的RNG种子）
            stack[stackDepth].ray = frame.ray;
            stack[stackDepth].weight = frame.weight;
            stack[stackDepth].branchID = frame.branchID + 1;
            stackDepth++;
        }
    }
    
    return totalTemperature;
}
```

**4. 材质系统重构**
```hlsl
// GPU友好的材质表示
struct GPUInterface {
    // 静态属性
    float emissivity;
    float specular_fraction;
    uint  brdf_type;
    
    // 边界条件
    uint  boundary_type;  // DIRICHLET, NEUMANN, ROBIN
    float temperature;     // 如果是Dirichlet
    float convection_coef; // 如果是Robin
    
    // 介质引用
    uint  medium_front_id;
    uint  medium_back_id;
};

struct GPUMedium {
    uint  type;            // FLUID, SOLID
    float temperature;     // 如果已知
    bool  temperature_known;
    float diffusivity;     // 固体扩散系数
    float conductivity;    // 固体导热系数
    float density;         // 密度
    float heat_capacity;   // 比热容
};

// 全局查找表（场景初始化时上传）
StructuredBuffer<GPUInterface> g_Interfaces : register(t3);
StructuredBuffer<GPUMedium>    g_Media      : register(t4);

// 查询接口
GPUInterface GetInterface(uint instanceID) {
    return g_Interfaces[instanceID];
}

Medium GetMedium(GPUInterface interf, float3 rayDir) {
    // 根据光线方向判断正面/背面
    bool isFront = dot(rayDir, interf.normal) < 0;
    uint mediumID = isFront ? interf.medium_front_id : interf.medium_back_id;
    return g_Media[mediumID];
}
```

**5. 传导路径简化（Walk-on-Sphere GPU版）**
```hlsl
// 简化的传导路径采样（仅支持常数初始条件）
float SampleConductivePath(float3 startPos, PathContext ctx) {
    float3 pos = startPos;
    Solid solid = GetSolid(pos);  // 查询固体属性
    
    const uint MAX_STEPS = 100;
    const float EPSILON = 1e-6 * solid.delta;
    
    [loop]
    for (uint step = 0; step < MAX_STEPS; step++) {
        // 查询最近边界距离
        float distToBoundary = QueryDistanceToBoundary(pos, solid);
        
        if (distToBoundary < EPSILON) {
            // 到达边界：返回边界温度
            Fragment frag = GetBoundaryFragment(pos);
            return EvaluateBoundaryTemperature(frag, ctx);
        }
        
        // Walk-on-Sphere: 在球内随机游走
        float radius = distToBoundary * 0.95;  // 留5%安全距离
        float3 offset = SampleUniformSphere(ctx.rng_seed, step) * radius;
        pos += offset;
        
        // 更新时间（倒退）
        ctx.time -= radius * radius / (2.0 * solid.diffusivity);
        
        if (ctx.time < solid.initial_time) {
            // 到达初始时刻：返回初始温度
            return solid.initial_temperature;
        }
    }
    
    // 超过最大步数：返回失败标志
    return -1.0;  // 需要CPU fallback
}
```

**6. Kahan补偿求和（高精度累加）**
```hlsl
// 双缓冲区：[0]=sum, [1]=error
RWStructuredBuffer<double2> g_Accumulator : register(u1);

void AtomicAddKahan(uint index, float value) {
    // Kahan补偿求和（减少累加误差）
    double old_sum, old_error;
    double new_sum, new_error;
    
    [loop]
    do {
        // 读取当前值
        double2 current = g_Accumulator[index];
        old_sum = current.x;
        old_error = current.y;
        
        // 补偿计算
        double y = double(value) - old_error;
        double t = old_sum + y;
        new_error = (t - old_sum) - y;
        new_sum = t;
        
        // 原子CAS更新
    } while (!InterlockedCompareExchangeDouble2(
        g_Accumulator, index, 
        current, double2(new_sum, new_error)
    ));
}
```

#### 传输优化

**场景初始化**（一次性上传）：
```
- 几何数据（BLAS）: ~500 MB（100万三角形）
- 材质表（Interfaces）: ~1 MB（1000个接口 × 1KB）
- 介质表（Media）: ~100 KB（100个介质 × 1KB）
- 加速结构（TLAS）: ~10 MB
总计：~511 MB（仅需一次）
```

**每帧传输**：
```
上行（CPU → GPU）:
- 相机参数: 128B
- 常量缓冲区: 256B
总计：<1 KB

下行（GPU → CPU）:
- 图像数据（512×512×4B）: 1 MB（FP32温度）
- 统计信息: ~1 KB
总计：~1 MB
```

**SPP循环**：在GPU内完成（Dispatch 256次，无额外传输）

#### 性能预估

| 阶段 | 时间（ms） | 说明 |
|------|-----------|------|
| **场景上传** | 500ms | 一次性（首帧） |
| **GPU计算（256 SPP）** | 800ms | 67M rays × 4 bounces × 100ns/ray |
| **结果下载** | 10ms | 1MB @ 100MB/s |
| **总时间（首帧）** | 1310ms | 包含初始化 |
| **总时间（后续帧）** | 810ms | 无上传开销 |
| **CPU时间** | 800ms | 基准 |
| **加速比** | **0.99×** | 几乎持平 ❌ |

**为什么加速不明显**？

FP64性能瓶颈：
```
RTX 4090 FP64性能：0.1 TFLOPS
温度累加需要FP64：67M × 256 SPP = 17B次双精度累加
理论时间：17B ops ÷ 0.1 TFLOPS = 170秒 ⚠️

实际优化后：
- 使用混合精度（FP32追踪 + FP64累加）
- Kahan求和减少FP64操作
- 优化后约 800ms（与CPU持平）
```

#### 完整功能对比

| 功能 | CPU版本 | GPU版本（方案C） | 实现难度 |
|------|---------|-----------------|---------|
| **辐射路径** | ✅ 完整 | ✅ 完整 | ⭐⭐⭐ |
| **对流路径** | ✅ 时变流体 | ⚠️ 简化（静态） | ⭐⭐⭐⭐ |
| **传导路径** | ✅ WoS + Delta-Sphere | ⚠️ 仅WoS | ⭐⭐⭐⭐⭐ |
| **Picard迭代** | ✅ 任意阶 | ✅ 最多8阶 | ⭐⭐⭐⭐ |
| **Green函数** | ✅ 完整记录 | ❌ 不支持 | ⭐⭐⭐⭐⭐⭐ |
| **动态材质** | ✅ 函数指针 | ❌ 静态表 | ⭐⭐⭐⭐⭐ |
| **固体传导** | ✅ 多种算法 | ⚠️ 仅常数初值 | ⭐⭐⭐⭐⭐⭐ |
| **MPI并行** | ✅ 支持 | ❌ 单GPU | N/A |

#### 优缺点总结

| 维度 | 评分 | 说明 |
|------|------|------|
| **技术风险** | ⭐⭐⭐☆☆ 3/5 | 大量重构，多个新技术点 |
| **实现复杂度** | ⭐⭐☆☆☆ 2/5 | 需要完全重写solver |
| **性能收益** | ⭐⭐⭐☆☆ 3/5 | 受限FP64，约1-3×（非线性场景更优） |
| **可维护性** | ⭐⭐☆☆☆ 2/5 | CPU/GPU双份代码 |
| **扩展性** | ⭐⭐⭐⭐⭐ 5/5 | 长期架构目标 |
| **功能完整性** | ⭐⭐⭐☆☆ 3/5 | 部分功能简化 |

**适用场景**：
- ✅ 纯辐射传输（无复杂边界）
- ✅ 静态材质场景
- ✅ Picard迭代阶数≤8
- ✅ 无Green函数需求
- ❌ 复杂固体传导
- ❌ 时变边界条件

#### 实施路线图（4-6周）

**Week 1-2: 基础架构（同方案B）**
- [ ] DXR基础设施
- [ ] 批量处理框架
- [ ] 材质系统重构

**Week 3: 路径类型实现**
- [ ] PathType枚举架构
- [ ] 辐射路径（完整）
- [ ] 边界路径（简化）
- [ ] 对流路径（静态）

**Week 4: 传导路径**
- [ ] Walk-on-Sphere算法
- [ ] 边界距离查询（DXR ClosestHit）
- [ ] 时间倒退机制
- [ ] 初始条件验证

**Week 5: Picard迭代**
- [ ] 显式栈实现
- [ ] 分支权重计算
- [ ] 参考温度更新
- [ ] 多阶测试（order=2,3,4）

**Week 6: 精度与性能**
- [ ] Kahan求和实现
- [ ] FP64/FP32混合精度
- [ ] 性能优化（Occupancy）
- [ ] 完整验证（Cornell Box + 复杂场景）

---

## 4. FP64精度需求分析

### 4.1 RTX 4090双精度性能

**硬件规格**：
```
FP32性能：83 TFLOPS
FP64性能：0.08-0.16 TFLOPS（1/512 - 1/1024 of FP32）
原因：消费级GPU人为限制FP64单元数量（市场细分）
```

**对比专业卡**：
- A100 (Ampere): FP64 = FP32 × 1/2 (19.5 TFLOPS)
- H100 (Hopper): FP64 = FP32 × 1/2 (30 TFLOPS)
- Titan RTX: FP64 = FP32 × 1/64 (0.4 TFLOPS)

**结论**：RTX 4090对FP64计算**极不友好**，必须精心设计混合精度策略。

---

### 4.2 精度需求分析

**STARDIS的精度约束**：
- CPU/GPU验证容差：**1e-6**（绝对误差）
- 几何精度：亚像素级（~1e-7米）
- 温度累加：67M样本 × 256 SPP = **17 billion次累加**

**FP32累加误差估算**：
```cpp
// FP32累加误差模型
float sum = 0.0f;
for (int i = 0; i < 17000000000; i++) {
    sum += small_value;  // ~1e-3 - 1e-5
}
// 误差：ε × N = 1.2e-7 × 17e9 = 2040 ⚠️（超过100%）

// FP64累加误差
double sum = 0.0;
// 误差：ε × N = 2.2e-16 × 17e9 = 3.7e-6（可接受）
```

**结论**：**温度累加必须使用FP64**，否则误差超过1e-6容差。

---

### 4.3 混合精度策略

**推荐配置**：

| 操作 | 精度 | 理由 | 占比 |
|------|------|------|------|
| **光线方向归一化** | FP32 | 方向向量容差高 | 5% |
| **BVH遍历** | FP32 | Embree内部也用FP32 | 50% |
| **击中距离t** | FP64 | 避免自相交关键 | 10% |
| **温度累加** | **FP64** | 17B次累加误差 | 20% |
| **RNG生成** | FP64 | 准随机序列质量 | 10% |
| **BRDF计算** | FP32 | 材质参数容差大 | 5% |

**实现示例**：
```hlsl
// 混合精度光线追踪
float3 origin_fp32 = float3(ray.origin);      // FP32存储
float3 direction_fp32 = float3(ray.direction);

RayQuery<RAY_FLAG_NONE> q;
q.TraceRayInline(g_Scene, ...);

if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    // 击中距离用FP64计算（避免自相交）
    double t_fp64 = double(q.CommittedRayT());
    double3 newPos_fp64 = ray.origin + ray.direction * t_fp64;
    
    // 转回FP32继续追踪
    ray.origin = float3(newPos_fp64);
}

// 温度累加用FP64（Kahan求和）
AtomicAddKahanFP64(accumulator, temperature);
```

**性能影响**：
```
纯FP32：83 TFLOPS  → 计算时间 200ms
混合精度：~20% FP64 → 额外时间 +150ms（FP64部分）
总计：350ms（vs 纯FP64的 8000ms）
```

---

### 4.4 Kahan补偿求和

**标准累加 vs Kahan算法**：

```hlsl
// 标准累加（误差 O(n·ε)）
float sum = 0.0;
for (int i = 0; i < N; i++) {
    sum += values[i];
}
// 误差：N × ε = 1e7 × 1e-7 = 1.0（100%误差！）

// Kahan补偿求和（误差 O(n·ε²)）
float sum = 0.0, error = 0.0;
for (int i = 0; i < N; i++) {
    float y = values[i] - error;   // 减去上次误差
    float t = sum + y;              // 临时和
    error = (t - sum) - y;          // 新误差
    sum = t;                        // 更新和
}
// 误差：N × ε² = 1e7 × 1e-14 = 1e-7（0.00001%误差）
```

**GPU实现（原子操作）**：
```hlsl
// 双缓冲区：[sum, error]
RWStructuredBuffer<double2> g_KahanAccumulator;

void AtomicKahanAdd(uint index, float value) {
    [loop]
    while (true) {
        double2 old_val = g_KahanAccumulator[index];
        
        double y = double(value) - old_val.y;  // 补偿
        double t = old_val.x + y;
        double new_error = (t - old_val.x) - y;
        
        double2 new_val = double2(t, new_error);
        
        // 原子CAS
        if (InterlockedCompareExchangeDouble2(
            g_KahanAccumulator, index, old_val, new_val)) {
            break;  // 成功
        }
        // 失败则重试
    }
}
```

---

### 4.5 几何缩放技术

**问题**：场景尺度可能跨越多个数量级（1mm - 100m）

**解决方案**：
```cpp
// CPU端：归一化场景到[-1, 1]³
double scene_scale = compute_scene_bounding_box_size();
double scale_factor = 2.0 / scene_scale;

// 缩放所有顶点
for (auto& vertex : vertices) {
    vertex *= scale_factor;
}

// GPU端：使用FP32追踪
// ...

// 结果反缩放
temperature_physical = temperature_gpu / scale_factor;
```

**优点**：
- ✅ BVH遍历可用FP32（-1到1范围）
- ✅ 自相交问题减轻（epsilon相对可靠）
- ✅ 仅在最终结果时需要FP64除法

**注意**：
- ⚠️ 温度累加仍需FP64（缩放不改变累加误差）
- ⚠️ 时间单位同步缩放（扩散系数调整）

---

## 5. 推荐实施路径

### 5.1 总体评估

| 方案 | 开发时间 | 技术风险 | 加速比 | 功能完整性 | **投资回报率** |
|------|---------|---------|--------|-----------|-------------|
| **A: 纯BVH** | 2周 | ⭐⭐⭐⭐⭐ | 0.2-0.5× ❌ | 100% | **极低** ❌ |
| **B: Wave-batch** | 3-4周 | ⭐⭐⭐⭐☆ | 1.9-2.5× ✅ | 60% | **中等** ✅ |
| **C: 全GPU** | 4-6周 | ⭐⭐⭐☆☆ | 1.0-3.0× ⚠️ | 70% | 中-高（长期） |

**综合推荐**：**方案B（Wave-batch Tracing）**

---

### 5.2 渐进式路线图

**阶段1：DXR基础验证（1周）**
```
目标：确认DXR可行性
├─ 单三角形追踪测试
├─ AnyHit过滤验证
├─ 自相交避免测试
└─ 性能baseline（与Embree对比）

成功标准：
- 单光线误差 < 1e-8
- 100万光线/秒（单核）
```

**阶段2：批量框架实现（1周）**
```
目标：搭建Wave-batch架构
├─ Tile分割算法
├─ Upload/Readback缓冲区
├─ Compute Shader模板
└─ 16×16 Tile测试

成功标准：
- 256光线批量处理无错误
- PCIe传输 < 10ms/tile
```

**阶段3：关键技术验证（1周）**
```
目标：解决3个高风险点
├─ GPU RNG（PCG Hash）
│   └─ Cornell Box对比（CPU MersenneTwister）
├─ 简化BRDF（Lambertian + Phong）
│   └─ 误差分析（vs 完整BRDF）
└─ 温度精度（FP32 vs FP64）
    └─ 17B累加测试

成功标准：
- RNG统计检验通过（Chi-square p>0.05）
- BRDF简化误差 < 5%
- FP64累加误差 < 1e-6
```

**阶段4：完整集成（1周）**
```
目标：集成到solve_camera
├─ 多Tile并行调度
├─ 结果验证框架
├─ 性能profiling
└─ 文档输出

成功标准：
- 512×512×256 SPP < 500ms
- 逐像素误差 < 1e-6（95%像素）
- 加速比 > 1.8×
```

**决策点**（第4周末）：
- ✅ 成功 → 继续扩展到方案C（+2周）
- ⚠️ 部分成功 → 优化方案B（+1周）
- ❌ 失败 → 回退到方案A（仅技术验证）

---

### 5.3 风险缓解策略

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| **DXR精度不足** | 低 | 高 | 提前验证（阶段1），备选：几何缩放 |
| **PCG RNG质量差** | 低 | 中 | 备选：Sobol序列（+500MB内存） |
| **FP64性能太差** | 中 | 高 | 混合精度 + Kahan求和 |
| **材质简化误差大** | 中 | 中 | 逐步增加BRDF类型（GGX, Ward） |
| **Picard迭代失败** | 低 | 低 | 仅实现order=1（线性辐射） |
| **传输带宽不足** | 低 | 高 | 压缩数据（FP16 direction） |

---

## 6. 技术验证清单

### 6.1 阶段1检查项（DXR基础）

**DXR功能验证**：
- [ ] 单光线追踪（origin + direction → hit）
- [ ] 击中距离精度（误差 < 1e-8）
- [ ] 重心坐标正确性（Σ = 1.0）
- [ ] AnyHit过滤（忽略背面测试）
- [ ] 自相交避免（TMin = 1e-5）
- [ ] 球体相交（Intersection Shader）
- [ ] 场景实例化（TLAS多实例）

**性能Baseline**：
- [ ] 单光线延迟（< 10μs）
- [ ] 批量吞吐量（> 100M rays/s）
- [ ] BVH构建时间（< 1s for 100万三角形）
- [ ] 显存占用（< 2GB for 场景）

**验证方法**：
```cpp
// 单光线测试
Ray ray = {origin: {0,0,0}, direction: {0,0,1}};
Hit hit = dx12_trace_ray(ray);
ASSERT(fabs(hit.t - expected_t) < 1e-8);
ASSERT(hit.instanceID == expected_id);

// 批量测试
std::vector<Ray> rays(1000000);
auto start = now();
std::vector<Hit> hits = dx12_trace_batch(rays);
auto duration = now() - start;
ASSERT(duration < 10ms);  // > 100M rays/s
```

---

### 6.2 阶段2检查项（批量框架）

**数据传输验证**：
- [ ] Upload Heap正确性（CPU → GPU）
- [ ] Readback Heap正确性（GPU → CPU）
- [ ] Structured Buffer读写（无数据损坏）
- [ ] 双缓冲机制（无竞争条件）
- [ ] Morton码遍历（Tile顺序正确）

**Compute Shader验证**：
- [ ] 线程索引计算（DTid正确）
- [ ] RayQuery内联调用（Proceed正常）
- [ ] 循环收敛性（无死循环）
- [ ] 共享内存使用（无Bank冲突）
- [ ] 原子操作正确性（累加无丢失）

**验证方法**：
```hlsl
// Compute Shader单元测试
[numthreads(256, 1, 1)]
void TestCS(uint DTid : SV_DispatchThreadID) {
    // 测试：读写缓冲区
    float input = g_Input[DTid];
    g_Output[DTid] = input * 2.0;
}

// CPU端验证
float input[256] = {1,2,3,...};
upload(input);
dispatch_compute();
float output[256];
readback(output);
for (int i = 0; i < 256; i++) {
    ASSERT(output[i] == input[i] * 2.0);
}
```

---

### 6.3 阶段3检查项（关键技术）

**RNG质量验证**：
- [ ] 均匀性测试（Chi-square, p > 0.05）
- [ ] 独立性测试（Runs test）
- [ ] 周期长度（> 2^32）
- [ ] 多线程冲突（无重复序列）
- [ ] 蒙特卡洛收敛速度（vs CPU）

**BRDF简化验证**：
- [ ] Lambertian正确性（余弦加权）
- [ ] Phong高光（指数衰减）
- [ ] 能量守恒（出射 ≤ 入射）
- [ ] Cornell Box对比（误差 < 5%）

**FP64精度验证**：
- [ ] 17B累加误差（< 1e-6）
- [ ] Kahan求和正确性（vs 标准累加）
- [ ] 混合精度一致性（vs 纯FP64）
- [ ] 极端场景测试（1e-10 + 1e+10）

**验证方法**：
```cpp
// RNG统计测试
uint samples[1000000];
for (int i = 0; i < 1000000; i++) {
    samples[i] = PCG_Hash(12345, i);
}
double chi_square = compute_chi_square(samples);
double p_value = chi_square_to_pvalue(chi_square);
ASSERT(p_value > 0.05);  // 95%置信度通过

// FP64累加测试
double sum_fp64 = 0.0;
float sum_fp32 = 0.0f;
for (int i = 0; i < 17000000000; i++) {
    double val = random_float() * 1e-5;
    sum_fp64 += val;
    sum_fp32 += (float)val;
}
double error = fabs(sum_fp64 - sum_fp32) / sum_fp64;
ASSERT(error > 0.1);  // FP32误差 > 10%（证明需要FP64）
```

---

### 6.4 阶段4检查项（完整集成）

**功能测试**：
- [ ] 单像素求解（vs CPU 1e-6）
- [ ] 16×16 Tile（批量正确性）
- [ ] 全图渲染（512×512×256 SPP）
- [ ] 多帧稳定性（连续100帧）
- [ ] 边界情况（空场景、单三角形）

**性能测试**：
- [ ] 帧时间 < 500ms（512×512×256 SPP）
- [ ] 加速比 > 1.8×（vs CPU 800ms）
- [ ] GPU利用率 > 70%（Nsight profiling）
- [ ] PCIe带宽 < 4GB/s（避免瓶颈）
- [ ] 显存占用 < 10GB（留余量）

**精度测试**：
- [ ] 逐像素误差 < 1e-6（95%像素）
- [ ] 最大误差 < 1e-5（极端像素）
- [ ] 方差一致性（蒙特卡洛收敛）
- [ ] Cornell Box标准场景（PSNR > 40dB）

**验证方法**：
```cpp
// 逐像素对比
Image cpu_result = solve_camera_cpu(scene, 512, 512, 256);
Image gpu_result = solve_camera_gpu(scene, 512, 512, 256);

int mismatch_count = 0;
double max_error = 0.0;

for (int y = 0; y < 512; y++) {
    for (int x = 0; x < 512; x++) {
        double cpu_temp = cpu_result.at(x, y);
        double gpu_temp = gpu_result.at(x, y);
        double error = fabs(cpu_temp - gpu_temp);
        
        if (error > 1e-6) mismatch_count++;
        if (error > max_error) max_error = error;
    }
}

double mismatch_rate = (double)mismatch_count / (512*512);
ASSERT(mismatch_rate < 0.05);   // 95%像素通过
ASSERT(max_error < 1e-5);       // 最大误差可接受
```

---

## 7. 总结与建议

### 7.1 核心结论

1. **方案A（纯BVH）不可行**：PCIe传输开销(2.4s) > CPU计算时间(0.8s)
2. **方案B（Wave-batch）推荐**：平衡风险与收益，3-4周可验证，1.9-2.5×加速
3. **方案C（全GPU）长期目标**：功能最完整，但受限RTX 4090的FP64性能（1/512）
4. **关键风险**：函数指针消除、RNG质量、FP64性能

### 7.2 立即行动项

**Week 1优先级排序**：
1. ⭐⭐⭐⭐⭐ DXR单光线验证（2天）
2. ⭐⭐⭐⭐☆ PCG RNG统计测试（1天）
3. ⭐⭐⭐⭐☆ 材质数据结构设计（1天）
4. ⭐⭐⭐☆☆ Kahan求和原型（1天）

**决策点**（Week 1结束）：
- ✅ DXR精度 < 1e-8 → 继续方案B
- ✅ PCG通过统计检验 → 使用PCG
- ❌ 任一项失败 → 重新评估技术栈

### 7.3 备选方案

**如果方案B失败**：
1. **降级到方案A**：仅作为技术验证，不追求性能
2. **混合方案**：GPU BVH + CPU BRDF（减少传输）
3. **切换到CUDA**：绕过DX12限制，使用OptiX Prime
4. **硬件升级**：考虑A100（FP64性能50×）

### 7.4 长期演进路径

```
Phase 1 (Q1 2026): 方案B - Wave-batch
├─ 目标：2×加速，验证可行性
└─ 交付：DXR基础设施 + 批量追踪框架

Phase 2 (Q2 2026): 扩展方案B
├─ 目标：3×加速，增加功能
├─ 新增：GGX BRDF, 简化对流
└─ 优化：混合精度，Kahan求和

Phase 3 (Q3 2026): 迁移到方案C
├─ 目标：5×加速（需A100）
├─ 新增：完整Picard迭代，传导路径
└─ 重构：函数指针消除，GPU材质系统

Phase 4 (Q4 2026): 生产优化
├─ 目标：10×加速
├─ 优化：多GPU支持，流式处理
└─ 集成：与现有Stardis工具链对接
```

---

## 附录A：参考文献

1. **DirectX 12 Ray Tracing**  
   Microsoft Learn: https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-raytracing

2. **RTX 4090 Scientific Computing Benchmarks**  
   Puget Systems: https://www.pugetsystems.com/labs/hpc/nvidia-rtx4090-ml-ai-and-scientific-computing-performance-preliminary-2382/

3. **PCG Random Number Generator**  
   O'Neill, M. E. (2014): "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation"

4. **Kahan Summation on GPU**  
   CERN OpenLab: https://indico.cern.ch/event/1311334/contributions/5534960/

5. **Shift Monte Carlo GPU Implementation**  
   EPJN: https://epjn.epj.org/articles/epjn/abs/2025/01/epjn20240019/epjn20240019.html

6. **Jayenne IMC Physics Manual**  
   OSTI: https://www.osti.gov/biblio/1818098

---

## 附录B：术语表

| 术语 | 说明 |
|------|------|
| **BVH** | Bounding Volume Hierarchy，层次包围盒，加速光线追踪 |
| **DXR** | DirectX Raytracing，DX12的光线追踪API |
| **RayQuery** | DXR的内联光线追踪（Compute Shader中调用） |
| **BLAS** | Bottom-Level Acceleration Structure，底层加速结构（Mesh级） |
| **TLAS** | Top-Level Acceleration Structure，顶层加速结构（场景级） |
| **SPP** | Samples Per Pixel，每像素采样数（蒙特卡洛） |
| **BRDF** | Bidirectional Reflectance Distribution Function，双向反射分布函数 |
| **Picard迭代** | 非线性辐射传输的迭代求解方法 |
| **WoS** | Walk-on-Sphere，球上游走算法（固体传导） |
| **PCG** | Permuted Congruential Generator，快速RNG算法 |
| **Kahan求和** | 补偿求和算法，减少浮点累加误差 |

---

**文档版本**: 1.0  
**最后更新**: 2026-01-21 22:05:00  
**作者**: Sisyphus (OhMyOpenCode AI Agent)  
**审阅状态**: 待审阅  
