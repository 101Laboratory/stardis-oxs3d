# STARDIS 路径追踪顺序执行与GPU并行化策略分析

**生成时间**: 2026-01-21 22:03:00  
**分析目标**: 理解CPU顺序执行架构 → GPU并行化改造策略 → Green函数性能优化  
**基于文档**: `ray_realisation_analysis.md` + `green_func_usage.md`  
**状态**: 架构决策指南 - 准备实施阶段  

---

## 执行摘要

**核心发现**:
1. **顺序执行原因**: 多物理场耦合导致路径状态转换依赖完整的辐射追踪结果
2. **GPU并行化主要挑战**: DFS→BFS转换、函数指针消除、内存容量限制
3. **Green函数价值**: 在线性场景(Picard=1)下，对GPU有**战略性优势**，但需精心设计内存管理

**架构建议**: 混合三层策略
- **快速路径**: Picard=1 → 全GPU Wavefront + 增量Green函数流式传输
- **常规路径**: Picard=2-4 → GPU主体 + CPU多物理耦合
- **复杂路径**: Picard>4 → 完全CPU回退

---

## 一、为什么路径追踪和采样分开执行？

### **1.1 CPU代码结构回顾**

```c
// 📍 stardis-solver/0.16.2/src/sdis_realisation.c
res_T ray_realisation_3d(...) {
    // ════════════════════════════════════════════════════
    // 阶段1: 辐射路径追踪（主循环）
    // ════════════════════════════════════════════════════
    trace_radiative_path_3d(scn, dir, &ctx, &rwalk, rng, &T);
    // 内部: for(;;) { 
    //     求交 → BRDF采样 → 俄罗斯轮盘赌 → 继续或终止
    // }
    
    // ════════════════════════════════════════════════════
    // 阶段2: 耦合路径采样（如果未完成）
    // ════════════════════════════════════════════════════
    if (!T.done) {
        sample_coupled_path_3d(scn, &ctx, &rwalk, rng, &T);
        // 内部: while(!T.done) {
        //     根据 T.func 指针调用不同物理模式
        // }
    }
    
    return T.value;
}
```

### **1.2 分离执行的根本原因**

#### **原因A: 多物理场耦合的状态依赖**

**问题**: 下一个物理模式依赖当前模式的完整结果

| 辐射路径终止方式 | 触发的下一步物理模式 | 依赖信息 |
|----------------|-------------------|---------|
| **击中界面 + 吸收** | `boundary_path` → 边界条件处理 | 需知道击中点位置、法线、界面材质 |
| **边界是固-固接触** | `conductive_path` → 固体热传导随机游走 | 需要边界处理的结果（接触热阻） |
| **边界是固-流接触** | `convective_path` → 流体对流追溯 | 需要Robin边界系数 |
| **到达辐射环境** | 直接返回环境温度 | 无后续 |
| **时间超限** | 返回初始温度 | 无后续 |

**示例场景**:
```
射线从探针发射 → 穿过空气(流体) → 击中金属板
                                    ↓
                    辐射路径结束，切换到boundary_path
                                    ↓
                    检测到固-固接触 → 切换到conductive_path
                                    ↓
                    固体内随机游走 → 击中另一表面
                                    ↓
                    再次boundary_path → 最终温度确定
```

**CPU代码片段**:
```c
// 📍 sdis_heat_path_radiative_Xd.h:197
for(;;) {  // 辐射追踪主循环
    find_next_fragment(...);  // 求交
    
    if (SXD_HIT_NONE(&hit)) {
        // 到达辐射环境 → 直接完成
        set_limit_radiative_temperature(...);
        T.done = 1;
        break;
    }
    
    // BRDF采样
    brdf_sample(&brdf, rng, wi, N, &bounce);
    
    // 俄罗斯轮盘赌
    if (ssp_rng_canonical(rng) < brdf.emissivity) {
        // 被吸收 → 切换到边界路径（阶段2）
        T.func = boundary_path_3d;  // ⚠️ 函数指针切换
        break;  // 退出辐射循环，进入sample_coupled_path
    }
}
```

#### **原因B: Picard迭代的递归分支**

**Picard迭代**: 处理辐射-传导非线性耦合的数值方法

```
Picard阶数 = 1:  线性近似（Green函数适用）
             射线路径简单，无分支

Picard阶数 = 2:  考虑一次反馈
             路径可能分支1次

Picard阶数 = n:  考虑n-1次反馈
             路径可能分支n-1次
```

**CPU实现**: 递归调用
```c
// 📍 sdis_realisation_Xd.h:86
res_T sample_coupled_path_3d(...) {
    ctx->nbranchings += 1;  // 递归深度+1
    
    while (!T->done) {
        // 动态多态：T->func 可能指向任何物理模式函数
        res = T->func(scn, ctx, rwalk, rng, T);
        
        // T->func 内部可能再次调用 sample_coupled_path_3d
        // 形成递归（最大深度 = Picard阶数 - 1）
    }
    
    ctx->nbranchings -= 1;
    return res;
}
```

**为什么不能并行化递归**:
- GPU栈空间有限（~2 KB/thread）
- 递归深度不固定（依赖随机数和边界条件）
- 不同射线的递归模式完全不同（warp divergence严重）

#### **原因C: 状态回滚机制**

**失败重试**: 某些物理模式可能返回 `RES_BAD_OP`（操作失败），需要回滚并重试

```c
const struct rwalk rwalk_bkp = *rwalk;  // 备份状态
const struct temperature T_bkp = *T;
size_t nfails = 0;

do {
    res = T->func(scn, ctx, rwalk, rng, T);
    
    if (res == RES_BAD_OP) {
        // 恢复到备份状态
        *rwalk = rwalk_bkp;
        *T = T_bkp;
    }
} while (res == RES_BAD_OP && ++nfails < MAX_FAILS);
```

**GPU挑战**: 状态回滚需要复制完整的 `rwalk` 结构（~200 bytes），对GPU寄存器压力大

---

### **1.3 分离的优势（CPU视角）**

| 优势 | 说明 |
|------|------|
| **代码清晰** | 每个物理模式独立函数，易于调试 |
| **递归自然** | CPU栈空间大，递归无压力 |
| **灵活扩展** | 添加新物理模式只需新函数 + 函数指针 |
| **内存友好** | 动态数组可按需增长 |

---

## 二、顺序执行对GPU并行化的影响

### **2.1 核心冲突：DFS vs BFS**

#### **CPU (Depth-First Search)**
```
线程1: 射线A → 追踪完整路径(10次弹跳) → 完成
线程2: 射线B → 追踪完整路径(5次弹跳) → 完成
线程3: 射线C → 追踪完整路径(15次弹跳) → 完成
...
并行度: 线程级并行（OpenMP）
```

**问题**: 在GPU上，32个线程组成一个Warp，必须同步执行相同指令（SIMT）

```
Warp内32条射线:
  射线0: 10次弹跳
  射线1: 5次弹跳  ← 第6次开始idle，等待其他射线
  射线2: 15次弹跳 ← 拖慢整个Warp
  ...
  射线31: 3次弹跳 ← 第4次开始idle

实际利用率: ~40% (大量线程空转)
```

#### **GPU (Breadth-First Search - Wavefront)**
```
迭代0: 所有射线并行求交 (100% warp占用率)
迭代1: 所有射线并行BRDF采样
迭代2: 所有射线并行求交
...
迭代N: 压缩活跃射线 → 继续

并行度: 海量射线级并行（百万级）
```

**优势**: 同一迭代内所有射线做相同操作，避免warp divergence

---

### **2.2 具体挑战分析**

#### **挑战1: 函数指针消除**

**CPU**: `T->func` 运行时确定
```c
res = T->func(scn, ctx, rwalk, rng, T);
// func 可能是:
//   - radiative_path_3d
//   - conductive_path_3d
//   - convective_path_3d
//   - boundary_path_3d
```

**GPU影响**:
- 间接跳转无法内联 → 性能损失 50%+
- 不同Warp内射线调用不同函数 → 序列化执行

**解决方案对比**:

| 方案 | 实现 | 性能 | 复杂度 |
|------|------|------|--------|
| **枚举+Switch** | `switch(T.type) { case RADIATIVE: ... }` | 基线的70% | 低 |
| **多Kernel分派** | 根据类型分流到不同Kernel | 基线的50% | 中 |
| **模板特化** | `template<PathType> __global__ void trace()` | 基线的45% | 高 |

**推荐**: 枚举+Switch（第一阶段），多Kernel（优化阶段）

---

#### **挑战2: 递归展开**

**CPU**: 真递归（最大深度 = Picard阶数）
```c
void sample_coupled_path(...) {
    ctx->nbranchings++;
    while (!T->done) {
        T->func(...);  // 可能再次调用 sample_coupled_path
    }
    ctx->nbranchings--;
}
```

**GPU**: 栈空间限制
```
RTX 4090:
- L1 Cache per SM: 128 KB
- 最大活跃线程: 1536/SM
- 每线程可用栈: ~85 bytes ❌ (需要 ~2 KB)
```

**解决方案**: 显式栈 + 迭代

```cuda
#define MAX_PICARD_ORDER 8

struct PathStackFrame {
    RWalk rwalk;        // 200 bytes
    Temperature temp;   // 24 bytes
    int iteration;      // 4 bytes
};  // 总计 ~228 bytes/帧

struct PathStack {
    PathStackFrame frames[MAX_PICARD_ORDER];  // 1824 bytes
    int depth;
};

__device__ void sample_coupled_iterative(PathStack* stack, ...) {
    while (stack->depth >= 0) {
        PathStackFrame* frame = &stack->frames[stack->depth];
        
        // 执行当前帧逻辑
        execute_path_type(frame->temp.type, ...);
        
        // 检查是否需要分支（模拟递归调用）
        if (needs_branch(frame) && stack->depth < MAX_PICARD_ORDER - 1) {
            stack->depth++;  // 压栈
            init_frame(&stack->frames[stack->depth], ...);
        } else if (frame->temp.done) {
            stack->depth--;  // 弹栈
        }
    }
}
```

**代价**: 每线程 1.8 KB 固定开销

---

#### **挑战3: 状态回滚**

**CPU**: 简单复制
```c
const struct rwalk rwalk_bkp = *rwalk;  // 栈上复制
*rwalk = rwalk_bkp;  // 恢复
```

**GPU**: 寄存器压力
```cuda
// 每次备份需要复制 ~200 bytes
// 如果每条射线平均失败2次 → 额外 400 bytes 寄存器占用
// 降低occupancy: 2048 registers/thread → 可能只支持 512 threads/SM
```

**优化策略**:
1. **减少失败率**: 改进采样算法，避免触发 `RES_BAD_OP`
2. **增量备份**: 只备份关键字段（位置、时间、击中信息），其他重算
3. **重试限制**: MAX_FAILS = 3（而非CPU的10）

---

#### **挑战4: 动态数据结构**

**Green函数记录**: 动态数组
```c
struct green_path {
    struct darray_power_term power_terms;   // 运行时增长
    struct darray_flux_term flux_terms;
    struct darray_extflux_terms extflux_terms;
};

// CPU: realloc() 无压力
darray_push_back(&path->power_terms, &new_term);
```

**GPU**: 不支持动态分配（Kernel内）
```cuda
// 方案: 固定容量数组 + 原子计数
#define MAX_POWER_TERMS 64
#define MAX_FLUX_TERMS 32
#define MAX_EXTFLUX_TERMS 16

struct gpu_green_path {
    PowerTerm power_terms[MAX_POWER_TERMS];
    FluxTerm flux_terms[MAX_FLUX_TERMS];
    ExtFluxTerms extflux_terms[MAX_EXTFLUX_TERMS];
    
    int power_count;    // 原子递增
    int flux_count;
    int extflux_count;
};

__device__ void add_power_term(gpu_green_path* path, const PowerTerm* term) {
    int idx = atomicAdd(&path->power_count, 1);
    if (idx < MAX_POWER_TERMS) {
        path->power_terms[idx] = *term;
    } else {
        // 溢出处理: 标记错误，或扩展到全局池
    }
}
```

**内存占用**:
```
固定容量方案:
  每路径: 64×16 + 32×20 + 16×40 + 12 = 1672 bytes
  100M 路径: 167 GB ❌ 超出 24 GB

压缩方案（共享ID池）:
  平均每路径: 8×16 + 4×20 + 2×40 = 288 bytes
  100M 路径: 28.8 GB ❌ 仍超出
  
分批方案（Tiling）:
  每批 10M 路径: 2.88 GB ✅
  批次数: 10
```

---

### **2.3 Wavefront架构改造蓝图**

#### **阶段分解**

```
┌──────────────────────────────────────────────────────┐
│ Wavefront Iteration N                                 │
├──────────────────────────────────────────────────────┤
│ Stage 1: 射线-几何求交                                │
│   intersect_kernel<<<grid, block>>>(rays, N_active)  │
│   - 并行BVH遍历                                      │
│   - 输出: hit_info[N_active]                         │
├──────────────────────────────────────────────────────┤
│ Stage 2: 材质查询与分类                               │
│   classify_rays_kernel<<<...>>>(rays, hit_info)      │
│   - 根据击中材质，标记射线类型                         │
│   - 输出: ray_type[N_active]                         │
├──────────────────────────────────────────────────────┤
│ Stage 3: 辐射路径处理                                 │
│   IF (ray_type == RADIATIVE):                        │
│     brdf_kernel<<<...>>>(rays, hits)                 │
│     - BRDF评估与重要性采样                           │
│     - 俄罗斯轮盘赌终止判断                            │
│     - 记录Green函数项（如果启用）                     │
├──────────────────────────────────────────────────────┤
│ Stage 4: 非辐射路径处理（多Kernel分派）               │
│   IF (ray_type == CONDUCTIVE):                       │
│     conductive_kernel<<<...>>>(...)                  │
│   IF (ray_type == CONVECTIVE):                       │
│     convective_kernel<<<...>>>(...)                  │
│   IF (ray_type == BOUNDARY):                         │
│     boundary_kernel<<<...>>>(...)                    │
├──────────────────────────────────────────────────────┤
│ Stage 5: Stream Compaction（压缩活跃射线）            │
│   N_active = compact_active_rays(rays, N_active)     │
│   - 移除已终止的射线                                 │
│   - 保持数据紧凑，提高后续迭代效率                     │
├──────────────────────────────────────────────────────┤
│ Stage 6: 终止检查                                     │
│   IF (N_active == 0 OR iteration > MAX_BOUNCES):     │
│     break                                            │
│   ELSE:                                              │
│     goto Wavefront Iteration N+1                     │
└──────────────────────────────────────────────────────┘
```

#### **内存布局 (SoA)**

```cuda
struct RayStream {
    // 几何数据
    float3* origins;         // [N_max]
    float3* directions;      // [N_max]
    float*  times;           // [N_max]
    
    // 击中信息
    float*  hit_distances;   // [N_max]
    float3* hit_normals;     // [N_max]
    uint32_t* hit_prim_ids;  // [N_max]
    float2* hit_uvs;         // [N_max]
    
    // 路径状态
    double* weights;         // [N_max] 温度权重
    uint8_t* path_types;     // [N_max] 当前路径类型
    uint8_t* active_mask;    // [N_max] 1=活跃, 0=终止
    
    // Green函数数据（可选）
    GreenPath* green_paths;  // [N_max]
    
    int N_active;            // 当前活跃数量
    int iteration;           // 当前迭代次数
};
```

**对齐优化**:
```cuda
// 确保128字节对齐（2个缓存行）
static_assert(sizeof(RayStream) % 128 == 0);

// 使用__align__确保GPU内存合并访问
__align__(16) float3* origins;  // 16字节对齐
```

---

## 三、Green函数对GPU并行化的战略价值

### **3.1 Green函数核心优势**

#### **优势A: 解耦几何与边界条件**

**传统Monte Carlo**:
```
改变边界温度 → 重新追踪100M条射线 → 10分钟
改变材质热导率 → 重新追踪100M条射线 → 10分钟
参数扫描100个配置 → 1000分钟 = 16.7小时
```

**Green函数方法**:
```
生成Green函数（一次性）:
  追踪100M条射线 → 记录Green系数 → 保存到文件
  时间: 10分钟
  输出文件: ~20 GB

求解不同边界条件:
  加载Green函数 → 矩阵-向量乘法 → 完成
  时间: 每个配置 <1秒
  参数扫描100个配置 → 100秒 = 1.67分钟

总时间: 10 + 1.67 = 11.67分钟 (vs 1000分钟, 85x加速)
```

#### **优势B: GPU内存压力缓解（分阶段计算）**

**问题**: 100M Green路径 × 2 KB = 200 GB >> 24 GB GPU内存

**Green函数策略**: 增量生成 + 流式传输

```
┌─────────────────────────────────────────────────┐
│ 阶段1: GPU生成Green路径（批次1）                │
│   10M 射线 → Wavefront追踪 → 记录Green系数       │
│   GPU内存占用: 2 GB (路径) + 1 GB (BVH) = 3 GB  │
├─────────────────────────────────────────────────┤
│ 阶段2: 传输到CPU并清空GPU                       │
│   cudaMemcpy(host, device, 2 GB)                │
│   写入文件: green_batch_001.bin                  │
│   cudaMemset(device, 0, 2 GB)                   │
├─────────────────────────────────────────────────┤
│ 阶段3: 重复批次2-10                             │
│   ...                                           │
├─────────────────────────────────────────────────┤
│ 阶段4: CPU聚合所有批次                          │
│   合并 green_batch_*.bin → green_complete.bin   │
│   总大小: 20 GB                                 │
├─────────────────────────────────────────────────┤
│ 阶段5: Green函数求解（CPU）                     │
│   for config in configs:                        │
│       load_green_function(...)                  │
│       solve_temperature(config)                 │
└─────────────────────────────────────────────────┘
```

**关键**: GPU只需保持一个批次的内存，避免OOM

---

### **3.2 Green函数在GPU上的性能分析**

#### **场景1: 单次求解（不使用Green函数）**

```
直接Monte Carlo GPU:
  100M 射线 × Wavefront追踪 → 温度估计
  时间: ~5秒 (假设GPU性能)
  内存: 3 GB (活跃射线 + BVH)
```

#### **场景2: 参数扫描（使用Green函数）**

```
配置数量: N
每个配置的边界条件不同（温度、热流）

方案A: 直接MC（无Green函数）
  总时间: N × 5秒
  N=1:   5秒
  N=10:  50秒
  N=100: 500秒

方案B: Green函数（CPU生成 + CPU求解）
  生成时间: 600秒 (CPU慢)
  求解时间: N × 1秒
  总时间: 600 + N
  N=1:   601秒 ❌ 不如直接MC
  N=10:  610秒 ❌
  N=100: 700秒 ✅ 开始有优势

方案C: Green函数（GPU生成 + CPU求解）★
  生成时间: 50秒 (GPU快 12x)
  求解时间: N × 1秒
  总时间: 50 + N
  N=1:   51秒 ❌ 仍不如直接MC
  N=10:  60秒 ✅ 开始有优势
  N=100: 150秒 ✅ 3.3x加速

方案D: Green函数（GPU生成 + GPU求解）★★
  生成时间: 50秒
  求解时间: N × 0.1秒 (GPU矩阵乘法快)
  总时间: 50 + N×0.1
  N=1:   50.1秒 ❌
  N=10:  51秒 ✅
  N=100: 60秒 ✅ 8.3x加速
```

**结论**: Green函数在 **参数扫描场景** 下才有GPU优势，且需要 **N ≥ 10** 才回本

---

### **3.3 Green函数GPU实现策略**

#### **策略1: 增量流式传输（推荐用于大规模）**

```cuda
// 伪代码
void generate_green_function_gpu_streaming(
    Scene* scene,
    int total_rays,
    const char* output_file
) {
    const int batch_size = 10'000'000;  // 10M rays/batch
    const int num_batches = total_rays / batch_size;
    
    FILE* fp = fopen(output_file, "wb");
    GreenPath* h_green_paths = malloc(batch_size * sizeof(GreenPath));
    GreenPath* d_green_paths;
    cudaMalloc(&d_green_paths, batch_size * sizeof(GreenPath));
    
    for (int batch = 0; batch < num_batches; batch++) {
        // A. GPU生成一批Green路径
        wavefront_trace_green<<<grid, block>>>(
            scene, d_green_paths, batch_size
        );
        cudaDeviceSynchronize();
        
        // B. 传输到CPU
        cudaMemcpy(h_green_paths, d_green_paths, 
                   batch_size * sizeof(GreenPath), 
                   cudaMemcpyDeviceToHost);
        
        // C. 写入文件
        fwrite(h_green_paths, sizeof(GreenPath), batch_size, fp);
        
        // D. 清空GPU内存（复用）
        cudaMemset(d_green_paths, 0, batch_size * sizeof(GreenPath));
        
        printf("Batch %d/%d complete\n", batch+1, num_batches);
    }
    
    fclose(fp);
    cudaFree(d_green_paths);
    free(h_green_paths);
}
```

**性能估算**:
```
GPU生成: 10M rays × 5 ms/M = 50 ms
CPU传输: 2 GB × (12 GB/s PCIe) = 167 ms
文件写入: 2 GB × (500 MB/s SSD) = 4000 ms
总计/批: 4217 ms ≈ 4.2秒

10批 × 4.2秒 = 42秒 (vs CPU的 600秒, 14x加速)
```

**优化**: 异步传输 + 双缓冲
```cuda
// 批次N在GPU生成的同时，批次N-1在CPU写入
cudaStream_t stream_compute, stream_transfer;
cudaStreamCreate(&stream_compute);
cudaStreamCreate(&stream_transfer);

GreenPath* d_paths[2];  // 双缓冲
cudaMalloc(&d_paths[0], ...);
cudaMalloc(&d_paths[1], ...);

for (int batch = 0; batch < num_batches; batch++) {
    int curr = batch % 2;
    int prev = 1 - curr;
    
    // GPU生成（Stream 0）
    wavefront_trace<<<..., stream_compute>>>(d_paths[curr], ...);
    
    // CPU传输（Stream 1，如果不是第一批）
    if (batch > 0) {
        cudaMemcpyAsync(h_paths, d_paths[prev], ..., 
                        cudaMemcpyDeviceToHost, stream_transfer);
        cudaStreamSynchronize(stream_transfer);
        fwrite(h_paths, ...);  // CPU线程写入
    }
}

// 性能提升: 传输与计算重叠 → 42秒 → ~30秒
```

---

#### **策略2: 压缩存储（减少内存占用）**

**问题**: 固定容量数组浪费内存
```
分配: 64 power_terms
实际使用: 平均 8 个
浪费率: 87.5%
```

**方案**: 全局池 + 索引

```cuda
struct GreenFunctionPool {
    // 全局贡献项池（所有路径共享）
    PowerTerm* power_pool;        // [100M 总预算]
    FluxTerm* flux_pool;          // [50M 总预算]
    ExtFluxTerms* extflux_pool;   // [20M 总预算]
    
    int power_pool_offset;   // 原子计数器
    int flux_pool_offset;
    int extflux_pool_offset;
};

struct CompactGreenPath {
    // 索引到全局池
    int power_start;      // 起始索引
    int power_count;      // 数量
    int flux_start;
    int flux_count;
    int extflux_start;
    int extflux_count;
    
    // 路径元数据
    int end_type;
    uint32_t limit_id;
    double elapsed_time;
};  // 总计: 44 bytes (vs 1672 bytes, 38x减少)

__device__ void add_power_term_pooled(
    GreenFunctionPool* pool,
    CompactGreenPath* path,
    const PowerTerm* term
) {
    // 第一次添加：分配起始索引
    if (path->power_count == 0) {
        path->power_start = atomicAdd(&pool->power_pool_offset, 1);
    } else {
        // 检查是否连续（如果是，直接追加）
        int expected_next = path->power_start + path->power_count;
        int actual_next = atomicAdd(&pool->power_pool_offset, 1);
        
        if (actual_next != expected_next) {
            // 非连续，需要重新分配（边缘情况）
            // 简化处理：放弃优化，使用全局锁
        }
    }
    
    int idx = path->power_start + path->power_count;
    pool->power_pool[idx] = *term;
    path->power_count++;
}
```

**内存占用对比**:
```
固定容量方案:
  100M paths × 1672 bytes = 167 GB

压缩方案:
  路径元数据: 100M × 44 bytes = 4.4 GB
  Power池: 800M terms × 16 bytes = 12.8 GB (假设平均8/路径)
  Flux池: 400M terms × 20 bytes = 8 GB
  ExtFlux池: 200M terms × 40 bytes = 8 GB
  总计: 33.2 GB ✅ 勉强可行（分2批）

极端压缩（仅记录路径，不记录贡献项）:
  100M paths × 20 bytes = 2 GB ✅ 单批完成
  （求解时动态重算贡献项）
```

---

#### **策略3: GPU Green函数求解Kernel**

**问题**: Green函数求解本质是累加操作
```
T = ∑[路径i] (
    ∑[功率项j] G_power[i,j] × P[j] +
    ∑[热流项k] G_flux[i,k] × q[k] +
    T_boundary[i]
)
```

**GPU加速**: 并行reduce
```cuda
__global__ void green_solve_kernel(
    CompactGreenPath* paths,
    GreenFunctionPool* pool,
    double* medium_powers,        // [N_media]
    double* interface_fluxes,     // [N_interfaces]
    double* boundary_temps,       // [N_paths]
    double* output_temps,         // [N_paths]
    int N_paths
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N_paths) return;
    
    CompactGreenPath path = paths[tid];
    double T = 0;
    
    // A. 累积功率贡献
    for (int i = 0; i < path.power_count; i++) {
        PowerTerm term = pool->power_pool[path.power_start + i];
        T += term.coeff * medium_powers[term.medium_id];
    }
    
    // B. 累积热流贡献
    for (int i = 0; i < path.flux_count; i++) {
        FluxTerm term = pool->flux_pool[path.flux_start + i];
        T += term.coeff * interface_fluxes[term.interface_id];
    }
    
    // C. 加上边界温度
    T += boundary_temps[tid];
    
    output_temps[tid] = T;
}

// 调用
dim3 grid((N_paths + 255) / 256);
dim3 block(256);
green_solve_kernel<<<grid, block>>>(paths, pool, ..., N_paths);

// 性能: 100M路径 × 10 terms/path = 1B operations
// GPU吞吐: ~1 TFLOPS (FP64) → ~1 ms ✅
```

**优化**: Shared Memory缓存
```cuda
__global__ void green_solve_optimized(
    ...
    double* medium_powers,  // [N_media] 小数组，可缓存
    ...
) {
    __shared__ double s_powers[256];  // 假设介质数 < 256
    
    // 协作加载
    if (threadIdx.x < N_media) {
        s_powers[threadIdx.x] = medium_powers[threadIdx.x];
    }
    __syncthreads();
    
    // 使用缓存版本
    for (int i = 0; i < path.power_count; i++) {
        PowerTerm term = pool->power_pool[path.power_start + i];
        T += term.coeff * s_powers[term.medium_id];  // 从shared memory读取
    }
    
    // 性能提升: ~2x (减少全局内存访问)
}
```

---

### **3.4 Green函数决策树**

```
┌─────────────────────────────────────────────────────┐
│ 是否使用Green函数？                                  │
├─────────────────────────────────────────────────────┤
│ Q1: Picard阶数是多少？                              │
│   ├─ = 1 (线性) → 继续Q2                            │
│   └─ > 1 (非线性) → ❌ 不能用Green函数              │
├─────────────────────────────────────────────────────┤
│ Q2: 是否需要参数扫描？                              │
│   ├─ 单次求解 → ❌ 直接MC更快                       │
│   └─ 多次求解 (N ≥ 10) → 继续Q3                    │
├─────────────────────────────────────────────────────┤
│ Q3: 总射线数量？                                    │
│   ├─ < 10M → ✅ GPU单批Green生成                   │
│   ├─ 10M - 100M → ✅ GPU分批Green生成（推荐）      │
│   ├─ 100M - 1B → ✅ GPU压缩存储 + 流式传输         │
│   └─ > 1B → ⚠️ 考虑多GPU或CPU-GPU混合             │
├─────────────────────────────────────────────────────┤
│ Q4: 求解性能要求？                                  │
│   ├─ 离线分析 → CPU求解足够                        │
│   ├─ 交互式（<1秒） → ✅ GPU Green求解             │
│   └─ 实时（<10ms） → ✅ GPU Green求解 + 预计算     │
└─────────────────────────────────────────────────────┘
```

**推荐配置**:

| 场景 | 生成策略 | 求解策略 | 预期性能 |
|------|---------|---------|---------|
| **快速原型** | CPU MC | 直接输出 | 5-10 min |
| **参数扫描(N<10)** | GPU MC | 直接输出 | N × 5 sec |
| **参数扫描(N≥10)** | GPU Green(分批) | CPU求解 | 50 sec + N × 1 sec |
| **交互式设计** | GPU Green(预计算) | GPU求解 | 50 sec + N × 0.1 sec |
| **逆问题优化** | GPU Green(预计算) | GPU求解 + 梯度 | 50 sec + iter × 0.2 sec |

---

## 四、GPU实现架构建议

### **4.1 三层混合策略（推荐）**

```
┌──────────────────────────────────────────────────────────┐
│ 层级1: 快速路径 (Picard=1, 线性场景)                     │
│ ────────────────────────────────────────────────────────│
│ 执行位置: 100% GPU                                       │
│ 策略: Wavefront路径追踪 + Green函数记录                  │
│ 内存管理: 分批 + 流式传输                                │
│ 预期占比: 60-80% 实际应用                                │
│ 性能目标: 50-100x CPU加速                                │
├──────────────────────────────────────────────────────────┤
│ 层级2: 常规路径 (Picard=2-4, 中等耦合)                   │
│ ────────────────────────────────────────────────────────│
│ 执行位置: GPU主体 + CPU耦合处理                          │
│ 策略:                                                    │
│   - GPU: 辐射路径追踪（占95%时间）                       │
│   - CPU: 热传导/对流/边界（占5%时间）                    │
│ 实现: GPU遇到非辐射路径时，回传到CPU队列                 │
│ 预期占比: 15-30% 实际应用                                │
│ 性能目标: 20-50x CPU加速                                 │
├──────────────────────────────────────────────────────────┤
│ 层级3: 复杂路径 (Picard>4, 强耦合)                       │
│ ────────────────────────────────────────────────────────│
│ 执行位置: 100% CPU (回退模式)                            │
│ 策略: 完全使用现有CPU代码                                │
│ 触发条件:                                                │
│   - Picard阶数 > 4                                       │
│   - GPU栈溢出                                           │
│   - 用户显式指定                                         │
│ 预期占比: <5% 实际应用                                   │
│ 性能目标: 与CPU持平（无性能损失）                         │
└──────────────────────────────────────────────────────────┘
```

**分流逻辑**:
```cpp
enum ExecutionPath {
    GPU_FAST_PATH,      // Picard=1, Green函数
    GPU_HYBRID_PATH,    // Picard=2-4, GPU主导
    CPU_FALLBACK_PATH   // Picard>4, CPU回退
};

ExecutionPath select_path(const SolveArgs* args) {
    if (args->picard_order == 1) {
        return GPU_FAST_PATH;
    } else if (args->picard_order <= 4) {
        return GPU_HYBRID_PATH;
    } else {
        return CPU_FALLBACK_PATH;
    }
}
```

---

### **4.2 Wavefront核心Kernel设计**

#### **Kernel 1: 辐射路径追踪（主力）**

```cuda
__global__ void radiative_trace_kernel(
    RayStream* rays,
    Scene* scene,
    GreenFunctionPool* green_pool,  // nullable
    int N_active
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N_active || !rays->active_mask[tid]) return;
    
    // ════════════════════════════════════════════════════
    // A. 加载射线状态
    // ════════════════════════════════════════════════════
    float3 org = rays->origins[tid];
    float3 dir = rays->directions[tid];
    float time = rays->times[tid];
    
    // ════════════════════════════════════════════════════
    // B. BVH求交
    // ════════════════════════════════════════════════════
    HitInfo hit;
    bool hit_valid = intersect_scene_bvh(scene, org, dir, &hit);
    
    if (!hit_valid) {
        // 到达辐射环境 → 终止
        rays->weights[tid] = get_radiative_env_temperature(scene, dir);
        rays->active_mask[tid] = 0;
        
        // Green函数: 记录终点类型
        if (green_pool) {
            green_pool->paths[tid].end_type = GREEN_END_RADIATIVE_ENV;
        }
        return;
    }
    
    // ════════════════════════════════════════════════════
    // C. 更新位置
    // ════════════════════════════════════════════════════
    rays->origins[tid] = org + dir * hit.distance;
    rays->hit_normals[tid] = hit.normal;
    rays->hit_prim_ids[tid] = hit.prim_id;
    
    // ════════════════════════════════════════════════════
    // D. BRDF评估
    // ════════════════════════════════════════════════════
    Material mat = scene->materials[scene->prim_to_material[hit.prim_id]];
    
    // 俄罗斯轮盘赌
    float rng_val = rng_canonical(tid, rays->iteration, 0);
    
    if (rng_val < mat.emissivity) {
        // 被吸收 → 切换到边界路径
        rays->path_types[tid] = PATH_BOUNDARY;
        // 不标记inactive，留给boundary_kernel处理
        return;
    }
    
    // ════════════════════════════════════════════════════
    // E. 采样新方向
    // ════════════════════════════════════════════════════
    float3 wi = -dir;
    float3 wo = sample_brdf_direction(mat, wi, hit.normal, 
                                      tid, rays->iteration);
    rays->directions[tid] = wo;
    
    // ════════════════════════════════════════════════════
    // F. Green函数: 记录功率项（如果击中固体）
    // ════════════════════════════════════════════════════
    if (green_pool && mat.type == MATERIAL_SOLID) {
        add_power_term_atomic(green_pool, tid, mat.medium_id, 
                              compute_green_coeff(...));
    }
}
```

---

#### **Kernel 2: 边界路径处理**

```cuda
__global__ void boundary_path_kernel(
    RayStream* rays,
    Scene* scene,
    GreenFunctionPool* green_pool,
    int N_active
) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= N_active || rays->path_types[tid] != PATH_BOUNDARY) return;
    
    // 获取击中的界面信息
    uint32_t prim_id = rays->hit_prim_ids[tid];
    Interface interf = scene->interfaces[scene->prim_to_interface[prim_id]];
    
    // 判断边界类型
    if (interf.type == INTERFACE_SOLID_SOLID) {
        // 固-固接触 → 切换到传导路径
        rays->path_types[tid] = PATH_CONDUCTIVE;
        
        // Green函数: 记录热流项
        if (green_pool) {
            add_flux_term_atomic(green_pool, tid, interf.id, 
                                 compute_flux_coeff(...));
        }
        
    } else if (interf.type == INTERFACE_SOLID_FLUID) {
        // 固-流接触 → Robin边界条件
        // 简化处理: 直接返回界面温度（忽略对流）
        rays->weights[tid] = interf.temperature;
        rays->active_mask[tid] = 0;
        
        if (green_pool) {
            green_pool->paths[tid].end_type = GREEN_END_AT_INTERFACE;
            green_pool->paths[tid].limit_id = interf.id;
        }
        
    } else {
        // 其他类型 → 错误，终止
        rays->active_mask[tid] = 0;
    }
}
```

---

#### **Kernel 3: Stream Compaction（使用CUB）**

```cuda
#include <cub/cub.cuh>

int compact_active_rays(RayStream* rays, int N_current) {
    // ════════════════════════════════════════════════════
    // Step 1: 前缀和计算新索引
    // ════════════════════════════════════════════════════
    uint32_t* d_new_indices;
    cudaMalloc(&d_new_indices, N_current * sizeof(uint32_t));
    
    void* d_temp_storage = nullptr;
    size_t temp_storage_bytes = 0;
    
    cub::DeviceScan::ExclusiveSum(
        d_temp_storage, temp_storage_bytes,
        rays->active_mask, d_new_indices, N_current
    );
    
    cudaMalloc(&d_temp_storage, temp_storage_bytes);
    
    cub::DeviceScan::ExclusiveSum(
        d_temp_storage, temp_storage_bytes,
        rays->active_mask, d_new_indices, N_current
    );
    
    // ════════════════════════════════════════════════════
    // Step 2: 压缩数据
    // ════════════════════════════════════════════════════
    RayStream* temp_rays;
    cudaMalloc(&temp_rays, sizeof(RayStream));
    allocate_ray_stream(temp_rays, N_current);
    
    compact_kernel<<<(N_current+255)/256, 256>>>(
        rays, temp_rays, d_new_indices, N_current
    );
    
    // ════════════════════════════════════════════════════
    // Step 3: 计算新的活跃数量
    // ════════════════════════════════════════════════════
    uint32_t N_new;
    cudaMemcpy(&N_new, &d_new_indices[N_current-1], sizeof(uint32_t), 
               cudaMemcpyDeviceToHost);
    
    // 交换指针
    swap_ray_streams(rays, temp_rays);
    
    cudaFree(d_new_indices);
    cudaFree(d_temp_storage);
    free_ray_stream(temp_rays);
    
    return N_new;
}
```

---

### **4.3 CPU-GPU混合调度器**

```cpp
class HybridPathTracer {
public:
    void trace(const Scene* scene, const SolveArgs* args, 
               std::vector<double>& results) {
        
        ExecutionPath path = select_path(args);
        
        switch (path) {
        case GPU_FAST_PATH:
            trace_gpu_fast(scene, args, results);
            break;
            
        case GPU_HYBRID_PATH:
            trace_gpu_hybrid(scene, args, results);
            break;
            
        case CPU_FALLBACK_PATH:
            trace_cpu_fallback(scene, args, results);
            break;
        }
    }
    
private:
    // ════════════════════════════════════════════════════
    // 快速路径: 全GPU + Green函数
    // ════════════════════════════════════════════════════
    void trace_gpu_fast(const Scene* scene, const SolveArgs* args,
                        std::vector<double>& results) {
        
        // 1. 初始化GPU射线流
        RayStream* d_rays = init_ray_stream_gpu(args->N_rays);
        
        // 2. 初始化Green函数池
        GreenFunctionPool* d_green_pool = nullptr;
        if (args->generate_green) {
            d_green_pool = init_green_pool_gpu(args->N_rays);
        }
        
        // 3. Wavefront循环
        int N_active = args->N_rays;
        int iteration = 0;
        
        while (N_active > 0 && iteration < MAX_BOUNCES) {
            // 辐射追踪
            radiative_trace_kernel<<<grid, block>>>(
                d_rays, d_scene, d_green_pool, N_active
            );
            
            // 边界处理
            boundary_path_kernel<<<grid, block>>>(
                d_rays, d_scene, d_green_pool, N_active
            );
            
            // 压缩
            N_active = compact_active_rays(d_rays, N_active);
            
            iteration++;
        }
        
        // 4. 如果生成了Green函数，求解温度
        if (d_green_pool) {
            // 传输Green路径到CPU
            auto green_func = transfer_green_function_to_cpu(d_green_pool);
            
            // CPU求解
            solve_temperature_cpu(green_func, args, results);
            
        } else {
            // 直接从GPU获取温度
            download_temperatures(d_rays, results);
        }
        
        // 5. 清理
        free_ray_stream_gpu(d_rays);
        if (d_green_pool) free_green_pool_gpu(d_green_pool);
    }
    
    // ════════════════════════════════════════════════════
    // 混合路径: GPU辐射 + CPU耦合
    // ════════════════════════════════════════════════════
    void trace_gpu_hybrid(const Scene* scene, const SolveArgs* args,
                           std::vector<double>& results) {
        
        // GPU处理辐射路径（主循环）
        RayStream* d_rays = init_ray_stream_gpu(args->N_rays);
        
        // CPU队列：存储需要CPU处理的射线
        std::vector<RayState> cpu_queue;
        
        int N_active = args->N_rays;
        int iteration = 0;
        
        while (N_active > 0 && iteration < MAX_BOUNCES) {
            // GPU: 辐射追踪
            radiative_trace_kernel<<<...>>>(d_rays, d_scene, nullptr, N_active);
            
            // 检测需要CPU处理的射线
            extract_non_radiative_rays(d_rays, N_active, cpu_queue);
            
            // 如果CPU队列非空，启动CPU线程池处理
            if (!cpu_queue.empty()) {
                process_cpu_rays_async(cpu_queue, scene, args);
            }
            
            // GPU: 压缩活跃射线
            N_active = compact_active_rays(d_rays, N_active);
            
            iteration++;
        }
        
        // 等待CPU处理完成
        wait_cpu_processing();
        
        // 合并GPU和CPU结果
        merge_results(d_rays, cpu_queue, results);
        
        free_ray_stream_gpu(d_rays);
    }
    
    // ════════════════════════════════════════════════════
    // 回退路径: 完全CPU
    // ════════════════════════════════════════════════════
    void trace_cpu_fallback(const Scene* scene, const SolveArgs* args,
                            std::vector<double>& results) {
        // 直接调用现有CPU代码
        for (int i = 0; i < args->N_rays; i++) {
            double weight;
            ray_realisation_3d(scene, args, &weight);
            results.push_back(weight);
        }
    }
};
```

---

### **4.4 验证策略**

#### **阶段1: 单射线验证**

```cpp
void validate_single_ray() {
    // CPU版本
    double cpu_result;
    ray_realisation_3d(scene, args, &cpu_result);
    
    // GPU版本（单射线模式，禁用优化）
    double gpu_result;
    trace_single_ray_gpu(scene, args, &gpu_result);
    
    // 比较
    double error = fabs(cpu_result - gpu_result);
    double rel_error = error / fabs(cpu_result);
    
    assert(rel_error < 1e-6);  // 双精度容差
    printf("Single ray validation: PASS (error = %.2e)\n", rel_error);
}
```

#### **阶段2: 批量统计验证**

```cpp
void validate_batch_statistics() {
    const int N = 100000;
    
    std::vector<double> cpu_results(N);
    std::vector<double> gpu_results(N);
    
    // CPU
    #pragma omp parallel for
    for (int i = 0; i < N; i++) {
        ray_realisation_3d(scene, &args, &cpu_results[i]);
    }
    
    // GPU
    trace_gpu_fast(scene, &args, gpu_results);
    
    // 统计比较
    double cpu_mean, cpu_std;
    compute_statistics(cpu_results, &cpu_mean, &cpu_std);
    
    double gpu_mean, gpu_std;
    compute_statistics(gpu_results, &gpu_mean, &gpu_std);
    
    double mean_error = fabs(cpu_mean - gpu_mean) / cpu_mean;
    double std_error = fabs(cpu_std - gpu_std) / cpu_std;
    
    assert(mean_error < 1e-4);  // 统计容差
    assert(std_error < 1e-3);
    
    printf("Batch statistics validation: PASS\n");
    printf("  Mean error: %.2e\n", mean_error);
    printf("  Std error: %.2e\n", std_error);
}
```

#### **阶段3: Green函数等价性验证**

```cpp
void validate_green_function() {
    // CPU生成Green函数
    GreenFunction* cpu_green = generate_green_cpu(scene, &args);
    
    // GPU生成Green函数
    GreenFunction* gpu_green = generate_green_gpu(scene, &args);
    
    // 验证路径数量
    assert(cpu_green->npaths == gpu_green->npaths);
    
    // 逐路径比较
    int mismatches = 0;
    for (int i = 0; i < cpu_green->npaths; i++) {
        const GreenPath* cpu_path = &cpu_green->paths[i];
        const GreenPath* gpu_path = &gpu_green->paths[i];
        
        // 比较终点类型
        if (cpu_path->end_type != gpu_path->end_type) {
            mismatches++;
            continue;
        }
        
        // 比较功率项数量
        if (cpu_path->power_count != gpu_path->power_count) {
            mismatches++;
            continue;
        }
        
        // 比较功率项系数
        for (int j = 0; j < cpu_path->power_count; j++) {
            double error = fabs(cpu_path->power_terms[j].coeff - 
                               gpu_path->power_terms[j].coeff);
            if (error > 1e-6) {
                mismatches++;
                break;
            }
        }
    }
    
    double mismatch_rate = (double)mismatches / cpu_green->npaths;
    assert(mismatch_rate < 0.01);  // 容忍1%数值误差
    
    printf("Green function validation: PASS\n");
    printf("  Mismatch rate: %.2f%%\n", mismatch_rate * 100);
}
```

---

## 五、实施路线图

### **阶段0: 准备工作** (1周)

- [x] 完成CPU代码分析文档
- [x] 完成GPU架构设计文档
- [ ] 建立基准测试数据集
- [ ] 搭建GPU开发环境（CUDA Toolkit, DX12 SDK）

---

### **阶段1: 数据结构GPU化** (2周)

**任务**:
- [ ] 实现 `RayStream` (SoA布局)
- [ ] 实现 `Scene` GPU扁平化版本
- [ ] 实现 `GreenFunctionPool` 和 `CompactGreenPath`
- [ ] 编写CPU-GPU数据转换工具

**验证**:
- [ ] 单元测试：数据完整性（CPU→GPU→CPU）
- [ ] 内存占用分析（目标: <4 GB for 10M rays）

---

### **阶段2: BVH求交Kernel** (2周)

**任务**:
- [ ] 提取Embree BVH数据到GPU
- [ ] 实现GPU BVH遍历Kernel
- [ ] 实现Möller-Trumbore三角形求交
- [ ] 优化: Shared memory栈、Early exit

**验证**:
- [ ] 单射线验证（与CPU Embree比较）
- [ ] 批量验证（100K rays）
- [ ] 性能测试（目标: >100M rays/sec）

---

### **阶段3: Wavefront核心循环** (3周)

**任务**:
- [ ] 实现 `radiative_trace_kernel`
- [ ] 实现 BRDF采样（Lambert, Phong, Cook-Torrance）
- [ ] 实现俄罗斯轮盘赌终止
- [ ] 实现 Stream Compaction (CUB库)
- [ ] 实现主Wavefront循环调度器

**验证**:
- [ ] 单射线验证（vs CPU `trace_radiative_path_3d`）
- [ ] 统计验证（10K rays, 容差<1e-4）
- [ ] Warp利用率分析（目标: >80%）

---

### **阶段4: Green函数记录** (2周)

**任务**:
- [ ] 实现 `add_power_term_atomic`
- [ ] 实现 `add_flux_term_atomic`
- [ ] 实现 Green路径终点记录
- [ ] 实现增量流式传输（GPU→CPU）
- [ ] 实现Green函数序列化（二进制格式）

**验证**:
- [ ] Green路径完整性验证
- [ ] 系数数值精度验证（vs CPU, 容差<1e-6）
- [ ] 内存压力测试（100M rays, 分批）

---

### **阶段5: 边界与耦合处理** (3周)

**任务**:
- [ ] 实现 `boundary_path_kernel`
- [ ] 实现简化的热传导路径（GPU版）
- [ ] 实现CPU-GPU混合调度器
- [ ] 实现CPU队列异步处理

**验证**:
- [ ] 多物理场景验证（固-固、固-流边界）
- [ ] Picard=2-4 场景验证
- [ ] 性能分析（CPU队列占比，目标<10%）

---

### **阶段6: Green函数求解** (1周)

**任务**:
- [ ] 实现 `green_solve_kernel` (GPU)
- [ ] 实现Shared Memory优化版本
- [ ] 实现多配置批量求解

**验证**:
- [ ] 单配置求解验证（vs CPU Green求解）
- [ ] 参数扫描性能测试（N=100，目标<10秒）

---

### **阶段7: 系统集成与优化** (2周)

**任务**:
- [ ] 集成DX12抽象层
- [ ] 实现UE5兼容接口
- [ ] 性能profiling (NSight, RenderDoc)
- [ ] 内存优化（减少传输、双缓冲）

**验证**:
- [ ] 端到端性能测试（vs CPU baseline）
- [ ] 内存占用profiling
- [ ] 多场景回归测试

---

### **阶段8: 最终验证与文档** (1周)

**任务**:
- [ ] 大规模场景测试（1B rays）
- [ ] 数值精度报告（逐像素误差分析）
- [ ] 性能报告（speedup曲线）
- [ ] 用户文档 + API文档

**交付物**:
- [ ] GPU求解器库（DLL/静态库）
- [ ] 验证报告（CPU-GPU等价性证明）
- [ ] 性能benchmark报告
- [ ] 集成指南（UE5插件）

---

**总时间**: 17周 ≈ 4个月

---

## 六、风险分析与缓解策略

### **风险1: 数值精度不匹配**

**风险描述**: GPU双精度计算与CPU结果存在不可接受的误差

**概率**: 中等  
**影响**: 高（无法通过验证）

**缓解策略**:
1. **编译器标志**: 禁用fast-math优化
   ```bash
   nvcc --fmad=false -prec-div=true -prec-sqrt=true
   ```
2. **Kahan求和**: 用于累加大量项
   ```cuda
   __device__ double kahan_sum(double* values, int N) {
       double sum = 0, c = 0;
       for (int i = 0; i < N; i++) {
           double y = values[i] - c;
           double t = sum + y;
           c = (t - sum) - y;
           sum = t;
       }
       return sum;
   }
   ```
3. **分段测试**: 逐模块验证（求交、BRDF、累加）

---

### **风险2: 内存容量爆炸**

**风险描述**: Green函数路径超出GPU内存

**概率**: 高（确定会发生）  
**影响**: 中等（已有缓解方案）

**缓解策略**:
1. **分批处理**: 已设计（10M rays/batch）
2. **压缩存储**: 全局池 + 索引（38x减少）
3. **增量写回**: 流式传输到CPU/磁盘
4. **动态容量**: 监控GPU内存，动态调整批次大小
   ```cpp
   size_t free_mem, total_mem;
   cudaMemGetInfo(&free_mem, &total_mem);
   int batch_size = (free_mem * 0.8) / sizeof(GreenPath);
   ```

---

### **风险3: Warp Divergence严重**

**风险描述**: 射线弹跳次数差异大，导致GPU利用率低

**概率**: 中等  
**影响**: 中等（性能低于预期）

**缓解策略**:
1. **Stream Compaction**: 每N次迭代压缩一次
2. **自适应批处理**: 短路径先完成，长路径另开批次
3. **分层调度**: 按弹跳次数分组
   ```cuda
   if (iteration % 5 == 0) {
       // 分组: [0-5跳], [6-10跳], [11+跳]
       split_by_bounce_count(rays, &short_rays, &medium_rays, &long_rays);
   }
   ```

---

### **风险4: CPU-GPU传输瓶颈**

**风险描述**: PCIe带宽限制拖慢整体性能

**概率**: 低（仅限Green函数生成）  
**影响**: 低（可异步处理）

**缓解策略**:
1. **异步传输**: 使用CUDA Stream重叠计算与传输
2. **压缩传输**: 只传输必要数据（不传BVH）
3. **Pinned Memory**: 使用 `cudaMallocHost` 加速
   ```cpp
   cudaMallocHost(&h_green_paths, size);  // vs malloc: 2x faster
   ```

---

### **风险5: 多物理耦合复杂度**

**风险描述**: CPU-GPU混合调度器过于复杂，难以调试

**概率**: 中等  
**影响**: 高（开发进度延误）

**缓解策略**:
1. **分阶段实施**: 先纯GPU（Picard=1），后混合（Picard>1）
2. **回退机制**: 遇到复杂场景自动切换到CPU
3. **详细日志**: 记录每条射线的执行路径
   ```cpp
   if (debug_mode) {
       log_ray_history(tid, "GPU->CPU fallback at iteration %d", iter);
   }
   ```

---

## 七、关键技术决策总结

| 决策点 | 选择 | 理由 |
|--------|------|------|
| **并行架构** | Wavefront (BFS) | GPU SIMT架构必需 |
| **函数指针** | 枚举+Switch | 平衡性能与开发复杂度 |
| **递归处理** | 显式栈+迭代 | GPU栈限制 |
| **Green函数策略** | 分批生成+流式传输 | 内存容量限制 |
| **Green函数求解** | GPU Kernel | 交互式应用需要 |
| **BVH加速** | 提取Embree BVH | 验证友好，后续可升级OptiX |
| **RNG** | Counter-based (Philox) | 无状态，并行友好 |
| **内存布局** | SoA | GPU合并访问 |
| **验证策略** | 逐层单元测试 + 端到端统计 | 确保数值等价 |
| **执行路径** | 三层混合（Fast/Hybrid/Fallback） | 覆盖所有场景，最大化GPU利用 |

---

## 八、参考资料

### **学术论文**
- [Wavefront Path Tracing](https://research.nvidia.com/publication/2013-07_megakernels-considered-harmful-wavefront-path-tracing-gpus) (NVIDIA 2013)
- [Green's Function Monte Carlo](https://doi.org/10.1016/j.jcp.2015.02.042) (JCP 2015)
- [GPU Ray Tracing](https://developer.nvidia.com/rtx/ray-tracing) (NVIDIA RTX)

### **技术博客**
- [GPU Memory Optimization](https://developer.nvidia.com/blog/how-optimize-data-transfers-cuda-cc/)
- [CUB Library Guide](https://nvlabs.github.io/cub/)
- [Stream Compaction Techniques](https://developer.nvidia.com/gpugems/gpugems3/part-vi-gpu-computing/chapter-39-parallel-prefix-sum-scan-cuda)

### **内部文档**
- `guide/ray_realisation_analysis.md` — CPU执行流程
- `guide/green_func_usage.md` — Green函数详解
- `guide/IR_arch_gpu_impl_guide.md` — GPU架构指南

---

**文档版本**: 1.0  
**最后更新**: 2026-01-21 22:03:00  
**作者**: Sisyphus (AI Agent)  
**下一步**: 开始阶段1实施（数据结构GPU化）  

---

## 附录A: 术语表

| 术语 | 中文 | 解释 |
|------|------|------|
| **Wavefront** | 波前 | GPU并行路径追踪架构，所有射线同步推进 |
| **Stream Compaction** | 流压缩 | 移除非活跃元素，保持数据紧凑 |
| **SoA** | 结构数组 | Structure of Arrays，GPU友好的内存布局 |
| **AoS** | 数组结构 | Array of Structures，CPU传统布局 |
| **Warp Divergence** | Warp分歧 | 同一Warp内线程执行不同分支，降低效率 |
| **Green函数** | 格林函数 | 热传输的脉冲响应，用于线性系统分析 |
| **Picard迭代** | Picard迭代 | 处理非线性耦合的数值方法 |
| **BRDF** | 双向反射分布函数 | 描述表面光学属性 |
| **BVH** | 层次包围盒 | 加速射线-几何求交的数据结构 |
| **Monte Carlo** | 蒙特卡洛 | 随机采样统计方法 |

---

**END OF DOCUMENT**
