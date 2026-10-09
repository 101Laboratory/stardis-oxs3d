# STARDIS COMPUTE_IR模式GPU实现指南

**版本**: 1.0  
**状态**: 实施准备阶段  
**目标**: 分阶段完成CPU到GPU的完整迁移  
**核心原则**: 渐进式迁移，每步可验证，保持API兼容性  

---

## 执行摘要

### 当前状态分析
- ✅ **已完成**: CPU代码复制，架构分析完成
- ✅ **已完成**: 28个内部库的Windows适配
- ❌ **未开始**: DX12实现，构建系统迁移  

### 实施哲学
**"先走通，再优化"**  
1. 最小可行原型 → 2. 功能完整 → 3. 性能优化 → 4. 生产就绪  

### 成功标准
- GPU结果与CPU逐像素对比，容差1e-6
- 512x512分辨率下，256spp时，性能提升10-100倍
- 保持现有输入文件格式和命令行接口
- 支持RTX 4090双精度计算

---

## 第一阶段：基础设施准备 (✅ 已完成)

### 目标
建立可编译的Windows开发环境，处理依赖库问题。

### 任务分解

#### 任务1.1：依赖库Windows存根 (优先级：最高)
**问题**: 28个内部库在Windows不可用  
**解决方案**: 创建最小实现存根

```c
// 示例：rsys库存根 (rsys/0.15/src/ 的基本实现)
// 文件: include/stardis/port/rsys_stub.h
#pragma once

// 基本内存分配器存根
typedef struct mem_allocator {
    void* user_data;
} mem_allocator;

#define MEM_ALLOCATED_SIZE(allocator) (0)
#define MEM_DUMP(allocator, buf, size) ((void)0)

// 动态数组存根
#define DARRAY_NAME(name) /* 忽略 */
#define DARRAY_DATA(type) /* 忽略 */

// 日志系统存根
typedef struct logger {
    int level;
} logger;

#define LOG_ERROR 1
#define LOG_WARNING 2
#define LOG_OUTPUT 3

static inline void logger_init(mem_allocator* alloc, logger* log) { log->level = 3; }
static inline void logger_release(logger* log) { (void)log; }
static inline void logger_print(logger* log, int level, const char* fmt, ...) {
    if (level <= log->level) { /* 简单实现 */ }
}
```

**需要创建的存根库** (按依赖顺序):
1. `rsys` - 基础工具，内存管理 √
2. `star-3d` - 3D几何 (只实现必要接口)
3. `star-sp` - 采样和随机数 (重点，需要完整移植)
4. `s2d/s3d` - 2D/3D几何基础
5. `senc2d/senc3d` - 包壳库 (简化)

#### 任务1.2：构建系统迁移 (优先级：高)
**当前**: Makefile (Unix)  
**目标**: CMake (跨平台)

```cmake
# 根目录 CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(Stardis-GPU LANGUAGES C CXX CUDA)

# 选项配置
option(USE_DX12 "启用DirectX 12后端" ON)
option(USE_DOUBLE_PRECISION "启用双精度浮点" ON)
option(USE_VALIDATION "启用GPU验证层" ON)
option(BUILD_VALIDATOR "构建验证工具" ON)

# 子目录
add_subdirectory(stardis-cpu)     # CPU代码，仅编译必要部分
add_subdirectory(include)         # 头文件
add_subdirectory(src)             # GPU实现
add_subdirectory(tests)           # 测试
add_subdirectory(tools/validator) # 验证工具

# GPU架构检测
if(USE_DX12)
    find_package(DirectX REQUIRED)
    add_definitions(-DSTARDIS_ENABLE_DX12)
    
    # RTX 4090双精度支持检测
    if(USE_DOUBLE_PRECISION)
        add_definitions(-DSTARDIS_DOUBLE_PRECISION)
        message(STATUS "启用双精度浮点支持")
    endif()
endif()

# 编译器设置
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /std:c11")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()
```

#### 任务1.3：验证框架设计 (优先级：中)
```cpp
// tools/validator/src/gpu_validator.cpp
class GPUValidator {
public:
    struct ValidationConfig {
        double tolerance = 1e-6;       // 容差
        size_t test_pixels = 100;      // 测试像素数
        size_t test_samples = 1000;    // 测试采样数
        bool per_pixel_stats = true;   // 逐像素统计
    };
    
    ValidationResult validate(
        const std::string& cpu_result,
        const std::string& gpu_result,
        ValidationConfig config
    ) {
        // 1. 逐像素对比
        // 2. 统计差异分析
        // 3. 性能对比
        // 4. 生成报告
    }
};
```

### 第一阶段交付物
- [ ] Windows可编译的存根库
- [ ] 跨平台CMake构建系统
- [ ] 基础验证框架
- [ ] 开发环境配置文档

---

## 第二阶段：核心算法移植 (3-4周)

### 目标
将蒙特卡洛热传输核心算法移植到GPU，保持算法正确性。

### 迁移策略：三层渐进式

#### 第2.1层：最小原型 - 单像素验证 (1周)
**目标**: 验证基础算法正确性

```cpp
// src/gpu/minimal_prototype.cu
__global__ void single_pixel_monte_carlo_kernel(
    const GpuScene* scene,
    const CameraParams* camera,
    const uint2 pixel_coord,
    Estimator* result
) {
    // 第1步：相机光线生成 (直接移植camera_ray)
    double3 ray_org, ray_dir;
    camera_ray_gpu(camera, pixel_coord, &ray_org, &ray_dir);
    
    // 第2步：场景相交 (简化版)
    HitInfo hit = simple_scene_intersect(scene, ray_org, ray_dir);
    if (!hit.hit) return;
    
    // 第3步：简单辐射传输 (忽略传导/对流)
    double temperature = simple_radiative_transfer(scene, hit);
    
    // 第4步：结果累积
    atomicAdd(&result->sum, temperature);
    atomicAdd(&result->count, 1);
}

// 调用方式：单线程，单像素
cudaMalloc(&d_result, sizeof(Estimator));
single_pixel_monte_carlo_kernel<<<1, 1>>>(
    d_scene, d_camera, make_uint2(256, 256), d_result
);
cudaDeviceSynchronize();
```

#### 第2.2层：完整辐射传输 (1周)
**目标**: 实现完整的辐射传输算法

```cpp
// src/gpu/radiative_transfer.cu
__device__ double radiative_boundary_transfer(
    const GpuScene* scene,
    const HitInfo* hit,
    const RNGState* rng
) {
    // 实现sdis_heat_path_radiative_Xd.h的核心逻辑
    // 关键功能：
    // 1. 发射率计算
    // 2. 视角因子
    // 3. 辐射环境查询
    // 4. T^4处理 (Picard迭代基础)
    
    double T_surface = scene->materials[hit->material_id].temperature;
    double emissivity = scene->materials[hit->material_id].emissivity;
    
    // Stefan-Boltzmann定律: L = ε·σ·T^4
    const double sigma = 5.670374419e-8; // W·m⁻²·K⁻⁴
    double radiance = emissivity * sigma * pow(T_surface, 4.0);
    
    return radiance_to_temperature(radiance);
}
```

#### 第2.3层：传导传输算法 (1-2周)
**目标**: 实现Delta Sphere和Walk on Sphere算法

```cpp
// src/gpu/diffusion_solver.cu
enum DiffusionAlgorithm {
    DELTA_SPHERE,
    WALK_ON_SPHERE
};

__device__ double delta_sphere_step(
    const GpuScene* scene,
    double3 position,
    double delta,
    RNGState* rng
) {
    // Delta Sphere算法实现
    // 1. 生成δ球面上的随机方向
    // 2. 检查是否击中边界
    // 3. 递归直到到达温度边界
    // 参考: sdis_heat_path_conductive_delta_sphere_Xd.h
    
    double3 direction = uniform_sphere_sample(rng);
    double3 new_position = position + direction * delta;
    
    // 边界检查
    if (is_boundary_hit(scene, position, new_position)) {
        return sample_boundary_temperature(scene, new_position);
    }
    
    // 递归
    return delta_sphere_step(scene, new_position, delta, rng);
}
```

#### 第2.4层：边界条件处理 (1周)
**目标**: 实现三种边界类型的GPU版本

```cpp
// src/gpu/boundary_handlers.cuh
// 文件结构映射CPU代码
// sdis_heat_path_boundary_Xd_solid_solid.h → boundary_solid_solid.cu
// sdis_heat_path_boundary_Xd_solid_fluid_picard1.h → boundary_solid_fluid.cu
// sdis_heat_path_boundary_Xd_handle_external_net_flux.h → boundary_external.cu

__device__ BoundaryHandler get_boundary_handler(GpuScene* scene, uint32_t boundary_type) {
    switch (boundary_type) {
        case BOUNDARY_SOLID_SOLID:
            return boundary_solid_solid_handler;
        case BOUNDARY_SOLID_FLUID:
            return (scene->picard_order == 1) 
                ? boundary_solid_fluid_picard1_handler
                : boundary_solid_fluid_picardN_handler;
        case BOUNDARY_EXTERNAL:
            return boundary_external_handler;
        default:
            return boundary_default_handler;
    }
}
```

### 第二阶段验证策略

#### 验证级别1：单元测试
```cpp
// tests/gpu_unit_tests.cu
TEST_F(GPUTest, CameraRayGeneration) {
    // 对比CPU和GPU的camera_ray输出
    double3 cpu_org, cpu_dir;
    camera_ray_cpu(&cpu_cam, sample, &cpu_org, &cpu_dir);
    
    double3 gpu_org, gpu_dir;
    camera_ray_gpu<<<1, 1>>>(&gpu_cam, sample, &gpu_org, &gpu_dir);
    
    ASSERT_VEC3_NEAR(cpu_org, gpu_org, 1e-10);
    ASSERT_VEC3_NEAR(cpu_dir, gpu_dir, 1e-10);
}
```

#### 验证级别2：算法正确性
```cpp
// tests/algorithm_correctness.cpp
TEST_F(AlgorithmTest, RadiativeTransferCompare) {
    // 准备相同输入场景
    Scene* cpu_scene = create_test_scene();
    GpuScene* gpu_scene = upload_to_gpu(cpu_scene);
    
    // 运行1000次采样对比
    for (int i = 0; i < 1000; i++) {
        double cpu_temp = cpu_radiative_transfer(cpu_scene, hit);
        double gpu_temp = gpu_radiative_transfer(gpu_scene, hit);
        
        double diff = abs(cpu_temp - gpu_temp);
        ASSERT_LT(diff, 1e-6) << "采样 " << i << " 差异过大";
    }
}
```

#### 验证级别3：统计收敛性
```cpp
// tests/statistical_convergence.cpp
TEST_F(StatisticalTest, MonteCarloConvergence) {
    // 测试GPU蒙特卡洛的统计性质
    // 1. 均值收敛性
    // 2. 方差正确性
    // 3. 大数定律验证
    
    vector<double> gpu_samples = run_gpu_monte_carlo(10000);
    vector<double> cpu_samples = run_cpu_monte_carlo(10000);
    
    double gpu_mean = mean(gpu_samples);
    double cpu_mean = mean(cpu_samples);
    double gpu_var = variance(gpu_samples);
    double cpu_var = variance(cpu_samples);
    
    // 均值差异 < 0.1%
    ASSERT_RELATIVE_NEAR(gpu_mean, cpu_mean, 0.001);
    // 方差差异 < 1%
    ASSERT_RELATIVE_NEAR(gpu_var, cpu_var, 0.01);
}
```

### 第二阶段交付物
- [ ] 完整辐射传输GPU实现
- [ ] Delta Sphere传导算法GPU实现
- [ ] 三种边界条件GPU实现
- [ ] 单元测试套件
- [ ] 算法正确性验证报告

---

## 第三阶段：性能优化与集成 (2-3周)

### 目标
优化GPU性能，集成到现有应用框架。

### 3.1 性能优化策略

#### 优化1：内存访问模式
```cpp
// src/gpu/optimizations/memory_layout.h
// 优化数据结构，提高GPU缓存效率
struct __align__(16) GpuMaterial {
    float conductivity;      // 导热系数
    float density;          // 密度
    float specific_heat;    // 比热容
    float emissivity;       // 发射率
    float temperature;      // 温度
    uint32_t boundary_type; // 边界类型
    uint32_t flags;         // 标志位
}; // 32字节，对齐到缓存行

struct __align__(64) GpuSceneData {
    uint32_t num_triangles;
    uint32_t num_materials;
    uint32_t num_boundaries;
    uint32_t num_sources;
    GpuTriangle* triangles;    // 单独缓冲区
    GpuMaterial* materials;    // 单独缓冲区
    GpuBoundary* boundaries;   // 单独缓冲区
    float scene_aabb[6];       // 场景包围盒
}; // 64字节，一个缓存行
```

#### 优化2：随机数生成优化
```cpp
// src/gpu/rng/threefry_gpu.cu
// Threefry RNG的GPU优化实现
__device__ __forceinline__ uint64_t threefry_next(RNGState* state) {
    // GPU优化的Threefry实现
    // 使用warp级别共享状态减少内存访问
    uint64_t result;
    
    // 关键：每个warp共享一个随机数流
    uint32_t lane_id = threadIdx.x & 0x1F; // warp内线程ID
    uint32_t warp_id = threadIdx.x >> 5;   // warp索引
    
    if (lane_id == 0) {
        // 只有warp的第一个线程更新共享状态
        state->shared_state[warp_id] = threefry_advance(state->shared_state[warp_id]);
    }
    __syncthreads();
    
    // 所有线程从共享状态生成独立随机数
    result = threefry_derive(state->shared_state[warp_id], lane_id);
    
    return result;
}
```

#### 优化3：射线追踪优化
```cpp
// src/gpu/raytracing/bvh_gpu.cu
// GPU友好的BVH结构
struct GPUBVHNode {
    float aabb_min[3];
    float aabb_max[3];
    
    union {
        uint32_t left_child;    // 内部节点：左子节点索引
        uint32_t first_prim;    // 叶子节点：第一个三角形索引
    };
    
    uint32_t prim_count;        // 叶子节点：三角形数量
    uint32_t flags;             // 节点标志 (0=内部节点, 1=叶子节点)
};

// BVH遍历核函数
__device__ bool bvh_intersect(
    const GPUBVHNode* bvh_nodes,
    const GpuTriangle* triangles,
    const Ray* ray,
    HitInfo* hit
) {
    // 栈式遍历，避免递归
    uint32_t stack[64];
    uint32_t stack_ptr = 0;
    stack[stack_ptr++] = 0; // 根节点
    
    while (stack_ptr > 0) {
        uint32_t node_idx = stack[--stack_ptr];
        GPUBVHNode node = bvh_nodes[node_idx];
        
        if (!ray_aabb_intersect(ray, node.aabb_min, node.aabb_max)) {
            continue;
        }
        
        if (node.flags == 1) { // 叶子节点
            for (uint32_t i = 0; i < node.prim_count; i++) {
                GpuTriangle tri = triangles[node.first_prim + i];
                if (ray_triangle_intersect(ray, &tri, hit)) {
                    hit->material_id = tri.material_id;
                }
            }
        } else { // 内部节点
            stack[stack_ptr++] = node.left_child;
            stack[stack_ptr++] = node.left_child + 1;
        }
    }
    
    return hit->t < ray->t_max;
}
```

### 3.2 应用层集成

#### 集成模式：运行时切换
```cpp
// src/core/stardis_gpu_backend.cpp
class StardisGPUDevice : public StardisDevice {
public:
    enum ExecutionMode {
        MODE_CPU_ONLY,      // 仅CPU
        MODE_GPU_ONLY,      // 仅GPU
        MODE_HYBRID,        // 混合模式
        MODE_VALIDATION     // 验证模式（双路运行）
    };
    
    StardisGPUDevice(ExecutionMode mode = MODE_GPU_ONLY) 
        : mode_(mode), dx12_device_(nullptr) {
        
        if (mode_ != MODE_CPU_ONLY) {
            init_dx12_device();
            init_gpu_resources();
        }
    }
    
    virtual res_T solve_camera(const sdis_solve_camera_args* args,
                              sdis_estimator_buffer** buf) override {
        
        switch (mode_) {
            case MODE_CPU_ONLY:
                return cpu_solve_camera(args, buf);
            case MODE_GPU_ONLY:
                return gpu_solve_camera(args, buf);
            case MODE_HYBRID:
                return hybrid_solve_camera(args, buf);
            case MODE_VALIDATION:
                return validation_solve_camera(args, buf);
        }
        return RES_ERROR;
    }
};
```

#### 命令行接口扩展
```bash
# 使用示例
./stardis-gpu -R -gpu                     # GPU模式
./stardis-gpu -R -cpu                     # CPU模式  
./stardis-gpu -R -validate                # 验证模式（对比CPU/GPU）
./stardis-gpu -R -gpu -double             # GPU双精度模式
./stardis-gpu -R -gpu -diffusion wos      # 指定扩散算法
./stardis-gpu -R -gpu -picard 3           # 指定Picard阶数
```

### 第三阶段交付物
- [ ] 性能优化的GPU内核
- [ ] 应用层集成代码
- [ ] 命令行接口扩展
- [ ] 性能基准测试报告
- [ ] 生产就绪的二进制发布

---

## 第四阶段：验证与调优 (1-2周)

### 目标
全面验证正确性，优化性能达到目标。

### 4.1 验证矩阵

| 测试维度 | 测试内容 | 验收标准 |
|---------|---------|---------|
| **算法正确性** | 逐像素对比，100个测试场景 | 绝对误差 < 1e-6 |
| **统计性质** | 均值、方差、分布对比 | 相对误差 < 0.1% |
| **边界情况** | 极端温度、复杂几何、边界条件 | 无崩溃，合理结果 |
| **数值稳定性** | 双精度vs单精度，病态条件 | 结果稳定，无NaN |
| **性能基准** | 不同场景规模性能测试 | 达到加速比目标 |

### 4.2 性能调优检查表

```cpp
// tools/profiler/gpu_profiler.cpp
class GPUProfiler {
public:
    struct PerformanceMetrics {
        double kernel_time_ms;      // 内核执行时间
        double memory_throughput;   // 内存吞吐量 GB/s
        double occupancy;           // 占用率 %
        double warp_efficiency;     // warp效率 %
        double instruction_throughput; // 指令吞吐量
        double divergence_penalty;  // 分支发散惩罚
    };
    
    void profile_kernel(const std::string& kernel_name) {
        // 使用NVIDIA Nsight或类似工具
        // 分析：
        // 1. 内存访问模式
        // 2. 分支效率
        // 3. 寄存器使用
        // 4. 共享内存使用
        // 5. 指令混合
    }
};
```

### 4.3 验证测试套件

```bash
# 自动化测试脚本
#!/bin/bash
# run_validation_suite.sh

echo "=== STARDIS-GPU 验证测试套件 ==="
echo "开始时间: $(date)"

# 阶段1：基础功能测试
echo "阶段1: 基础功能测试"
./run_test.sh basic_camera_ray
./run_test.sh simple_intersection
./run_test.sh radiative_basic

# 阶段2：算法正确性测试
echo "阶段2: 算法正确性测试"
./run_test.sh radiative_comprehensive -samples 10000
./run_test.sh diffusion_delta_sphere -tolerance 1e-8
./run_test.sh boundary_conditions -scenes 10

# 阶段3：性能基准测试
echo "阶段3: 性能基准测试"
./run_benchmark.sh simple_scene -cpu -gpu -compare
./run_benchmark.sh medium_scene -cpu -gpu -compare
./run_benchmark.sh complex_scene -cpu -gpu -compare

# 阶段4：集成测试
echo "阶段4: 集成测试"
./run_integration_test.sh full_pipeline
./run_integration_test.sh command_line_interface
./run_integration_test.sh file_formats

echo "测试完成时间: $(date)"
echo "生成报告: validation_report_$(date +%Y%m%d_%H%M%S).html"
```

### 第四阶段交付物
- [ ] 完整验证测试报告
- [ ] 性能调优完成
- [ ] 最终性能基准
- [ ] 发布准备检查清单

---

## 实施路线图总览

### 时间线 (总耗时: 7-11周)
```
第1-2周：基础设施准备
├─ 依赖库存根实现
├─ CMake构建系统
├─ 基础验证框架
└─ 开发环境配置

第3-6周：核心算法移植
├─ 第1步：单像素原型 (1周)
├─ 第2步：辐射传输完整 (1周)
├─ 第3步：传导传输算法 (1-2周)
├─ 第4步：边界条件处理 (1周)
└─ 单元测试套件

第7-9周：性能优化与集成
├─ 内存访问优化
├─ 随机数生成优化
├─ 射线追踪优化
├─ 应用层集成
└─ 命令行接口扩展

第10-11周：验证与调优
├─ 全面验证测试
├─ 性能调优
├─ 生产就绪检查
└─ 文档与发布
```

### 风险评估与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| 依赖库移植复杂 | 高 | 中 | 阶段1重点处理，保持最小接口 |
| GPU算法正确性 | 中 | 高 | 渐进验证，每步对比CPU |
| 性能不达标 | 中 | 中 | 早期性能分析，预留优化时间 |
| Windows兼容性 | 低 | 高 | 持续集成测试，多平台验证 |
| 双精度支持问题 | 低 | 高 | RTX 4090验证，备选方案 |

### 成功指标

1. **功能正确性** (必须达到)
   - 100%测试场景通过验证
   - 逐像素误差 < 1e-6
   - 无回归错误

2. **性能指标** (目标)
   - 简单场景: 100x加速
   - 中等场景: 50x加速  
   - 复杂场景: 20x加速

3. **代码质量**
   - GPU代码覆盖率 > 80%
   - 无编译器警告
   - 文档完整度 > 90%

4. **易用性**
   - 兼容现有命令行接口
   - 自动模式检测
   - 清晰的错误信息

---

## 附录：详细技术规范

### A. DX12实现规范

```cpp
// include/stardis/dx12/device.h 扩展
class DX12StardisDevice : public StardisDevice {
private:
    // DX12资源
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<ID3D12CommandAllocator> command_allocator_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;
    
    // 计算管道
    ComPtr<ID3D12PipelineState> compute_pso_;
    ComPtr<ID3D12RootSignature> root_signature_;
    
    // 资源堆
    ComPtr<ID3D12DescriptorHeap> cbv_srv_uav_heap_;
    ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    
    // 双精度支持检测
    bool check_double_precision_support() {
        D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
        if (SUCCEEDED(device_->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
            return options.DoublePrecisionFloatShaderOps;
        }
        return false;
    }
};
```

### B. 数据结构转换规范

```cpp
// src/data_converter.cpp
class DataConverter {
public:
    // CPU数据结构 → GPU数据结构
    GpuScene* convert_scene(const sdis_scene* cpu_scene) {
        GpuScene* gpu_scene = new GpuScene();
        
        // 1. 扁平化指针结构
        gpu_scene->num_triangles = cpu_scene->geometry->num_triangles;
        gpu_scene->triangles = flatten_triangles(cpu_scene->geometry);
        
        // 2. 转换材料数据
        gpu_scene->num_materials = cpu_scene->num_materials;
        gpu_scene->materials = convert_materials(cpu_scene->materials);
        
        // 3. 构建BVH加速结构
        gpu_scene->bvh = build_bvh_gpu(gpu_scene->triangles);
        
        // 4. 上传到GPU内存
        upload_to_gpu(gpu_scene);
        
        return gpu_scene;
    }
};
```

### C. 构建系统详细配置

```cmake
# 详细的CMake配置示例
# src/gpu/CMakeLists.txt

# CUDA配置
find_package(CUDA REQUIRED)
set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -arch=sm_89")  # RTX 4090
set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} --use_fast_math")
set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -Xptxas -v")

# DX12配置
if(USE_DX12)
    find_package(DirectX REQUIRED)
    
    add_library(stardis_dx12 SHARED
        dx12_device.cpp
        dx12_resources.cpp
        dx12_kernels.cpp
    )
    
    target_include_directories(stardis_dx12 PRIVATE
        ${DirectX_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_link_libraries(stardis_dx12
        ${DirectX_LIBRARIES}
        d3d12.lib
        dxgi.lib
        d3dcompiler.lib
    )
endif()

# 验证工具
if(BUILD_VALIDATOR)
    add_executable(stardis_validator
        validator/main.cpp
        validator/compare.cpp
        validator/report.cpp
    )
    
    target_link_libraries(stardis_validator
        stardis_cpu
        stardis_gpu
        ${CMAKE_THREAD_LIBS_INIT}
    )
endif()
```

### D. 测试数据生成规范

```python
# tools/test_data/generate_test_scenes.py
class TestSceneGenerator:
    def generate_validation_scenes(self):
        """生成验证测试场景"""
        scenes = []
        
        # 1. 简单几何场景
        scenes.append({
            'name': 'single_sphere',
            'geometry': self.create_sphere(radius=1.0),
            'materials': [{'temperature': 300.0, 'emissivity': 0.9}],
            'expected_result': self.analytic_sphere_solution()
        })
        
        # 2. 复杂边界场景
        scenes.append({
            'name': 'multi_layer_slab',
            'geometry': self.create_layered_slab([0.1, 0.2, 0.1]),
            'materials': [
                {'temperature': 400.0, 'conductivity': 1.0},
                {'temperature': 300.0, 'conductivity': 0.5},
                {'temperature': 200.0, 'conductivity': 2.0}
            ],
            'expected_result': self.analytic_slab_solution()
        })
        
        # 3. 极端条件场景
        scenes.append({
            'name': 'high_temperature_gradient',
            'geometry': self.create_cube(),
            'materials': [{
                'temperature': 1000.0,  # 高温侧
                'boundary_temperature': 100.0  # 低温侧
            }],
            'expected_result': None  # 仅验证稳定性
        })
        
        return scenes
```

---

## 总结

这份实施指南提供了**可执行的、分阶段的**GPU迁移方案。关键特点是：

1. **渐进式实施** - 每阶段都有明确交付物和验证
2. **风险控制** - 早期识别和处理关键风险
3. **验证驱动** - 每个步骤都有对应的验证机制
4. **实用导向** - 提供具体代码示例和技术规范
5. **质量保证** - 从算法正确性到性能优化的全面覆盖

**下一步行动**:
1. 确认技术选型 (DX12 vs CUDA)
2. 开始第一阶段：依赖库存根实现
3. 建立持续集成环境
4. 定义更详细的API兼容性规范

**联系方式**: [项目维护者信息]

*文档版本: 1.0 | 最后更新: 2026-01-20 | 状态: 实施准备*
