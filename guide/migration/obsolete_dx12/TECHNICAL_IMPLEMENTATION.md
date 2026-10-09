# Stardis GPU迁移项目 - 详细技术实现方案

## 1. 整体架构设计

### 1.1 三层架构实现

```
┌─────────────────────────────────────────────────────┐
│                 应用程序层 (Application)             │
│  • C++17接口，兼容现有C API                         │
│  • 算法逻辑控制器                                   │
│  • 数据流管理和调度                                 │
├─────────────────────────────────────────────────────┤
│                 抽象渲染层 (Render Abstraction)      │
│  • 统一渲染接口 (IRenderDevice, IRenderContext)     │
│  • 资源抽象管理 (IBuffer, ITexture, IShader)        │
│  • 平台无关的数据结构和算法                         │
├─────────────────────────────────────────────────────┤
│                 DirectX 12实现层 (DX12 Implementation)│
│  • DX12设备封装 (DX12Device, DX12CommandContext)    │
│  • 资源管理封装 (DX12Buffer, DX12Texture)           │
│  • 计算着色器管理和编译                             │
└─────────────────────────────────────────────────────┘
```

### 1.2 核心类设计

```cpp
// 抽象层接口设计
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual IBuffer* CreateBuffer(const BufferDesc& desc) = 0;
    virtual IShader* CreateShader(const ShaderDesc& desc) = 0;
    virtual IRenderContext* CreateContext() = 0;
    // ... 其他接口
};

// DX12实现
class DX12Device : public IRenderDevice {
private:
    ComPtr<ID3D12Device8> m_device;
    ComPtr<ID3D12CommandQueue> m_computeQueue;
    ComPtr<ID3D12CommandQueue> m_copyQueue;
    // 资源管理
public:
    // 实现抽象接口
};

// 应用程序层
class StardisRenderer {
private:
    std::unique_ptr<IRenderDevice> m_device;
    std::unique_ptr<IRenderContext> m_context;
    // 算法状态和数据
public:
    Result SolveProbe(const SolveProbeArgs& args);
    Result SolveBoundary(const SolveBoundaryArgs& args);
    // ... 其他算法接口
};
```

## 2. DirectX 12封装层详细设计

### 2.1 设备初始化和管理

```cpp
class DX12DeviceInitializer {
public:
    struct DeviceCreationDesc {
        bool enable_debug_layer = true;
        bool enable_gpu_validation = false;
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_12_2;
        uint32_t compute_queue_priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    };
    
    static std::unique_ptr<DX12Device> CreateDevice(const DeviceCreationDesc& desc);
};

// 双精度支持检测
bool DX12Device::CheckDoublePrecisionSupport() {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
        return options.DoublePrecisionFloatShaderOps != 0;
    }
    return false;
}
```

### 2.2 资源管理封装

```cpp
class DX12Buffer : public IBuffer {
public:
    struct BufferDesc {
        size_t size;
        BufferUsage usage; // Upload, Default, Readback
        BufferAccess access; // CPU_Read, CPU_Write, GPU_Read, GPU_Write
        std::optional<std::string> debug_name;
    };
    
    DX12Buffer(DX12Device* device, const BufferDesc& desc);
    ~DX12Buffer();
    
    // 内存映射接口
    void* Map();
    void Unmap();
    
    // 更新数据
    void UpdateData(const void* data, size_t size, size_t offset = 0);
    
private:
    ComPtr<ID3D12Resource> m_resource;
    D3D12_RESOURCE_STATES m_current_state;
    D3D12_HEAP_TYPE m_heap_type;
    // 内存分配器引用
};
```

### 2.3 计算着色器管理

```cpp
class DX12ComputeShader : public IShader {
public:
    struct ComputeShaderDesc {
        std::string source_file; // HLSL文件路径
        std::string entry_point = "CSMain";
        std::vector<std::string> defines; // 预处理宏定义
        std::vector<ShaderResourceDesc> resources; // 绑定资源描述
    };
    
    DX12ComputeShader(DX12Device* device, const ComputeShaderDesc& desc);
    
    // 设置计算参数
    void SetConstantBuffer(uint32_t slot, IBuffer* buffer);
    void SetShaderResource(uint32_t slot, IBuffer* buffer);
    void SetUnorderedAccess(uint32_t slot, IBuffer* buffer);
    
private:
    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pso;
    // 反射信息
    struct ShaderReflection {
        uint32_t thread_group_x;
        uint32_t thread_group_y;
        uint32_t thread_group_z;
        // 资源绑定信息
    };
};
```

## 3. GPU算法迁移策略

### 3.1 蒙特卡洛射线跟踪GPU化

#### 原始CPU算法分析 (sdis_realisation.c)
```c
// CPU实现核心
res_T ray_realisation_3d(
    struct sdis_scene* scn,
    struct ray_realisation_args* args,
    double* weight)
{
    // 初始化随机游走上下文
    struct rwalk_context ctx = RWALK_CONTEXT_NULL;
    struct rwalk rwalk = RWALK_NULL;
    
    // 设置起始位置和方向
    d3_set(rwalk.vtx.P, args->position);
    f3_set_d3(dir, args->direction);
    
    // 跟踪辐射路径
    res = trace_radiative_path_3d(scn, dir, &ctx, &rwalk, args->rng, &T);
    
    // 采样耦合路径
    if(!T.done) {
        res = sample_coupled_path_3d(scn, &ctx, &rwalk, args->rng, &T);
    }
    
    *weight = T.value;
    return res;
}
```

#### GPU并行化设计
```cpp
// GPU计算着色器设计
[numthreads(64, 1, 1)]
void CSMonteCarlo(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint ray_index = dispatch_id.x;
    
    // 每个线程处理一条射线
    Ray ray = g_ray_buffer[ray_index];
    RandomState rng = InitializeRNG(ray_index, g_frame_seed);
    
    // GPU射线跟踪循环
    RayResult result = TraceRadiationPath(ray, rng);
    
    // 如果未完成，采样耦合路径
    if (!result.done) {
        result = SampleCoupledPath(ray, rng, result);
    }
    
    // 原子累加结果
    InterlockedAddFloat(g_result_buffer[ray.pixel_index], result.weight);
}

// 数据结构GPU优化
struct GPU_Ray {
    float3 origin;
    float3 direction;
    float energy;
    uint pixel_index;
    // 紧凑存储，128字节对齐
};

struct GPU_SceneData {
    // SoA布局优化内存访问
    StructuredBuffer<float3> vertices;
    StructuredBuffer<uint> indices;
    StructuredBuffer<float3> normals;
    StructuredBuffer<Material> materials;
    
    // 加速结构
    RaytracingAccelerationStructure tlas;
    
    // 热物理属性
    StructuredBuffer<ThermalProperty> thermal_properties;
};
```

### 3.2 热路径计算GPU化

#### 数据布局转换 (AoS → SoA)
```cpp
// CPU数据结构 (AoS)
struct HeatPathVertex {
    double position[3];
    double time;
    double temperature;
    uint32_t type;
    // ... 其他字段
};

// GPU数据结构 (SoA)
struct GPU_HeatPathData {
    // 位置数据 (连续存储)
    RWStructuredBuffer<float3> positions;
    RWStructuredBuffer<float> times;
    RWStructuredBuffer<float> temperatures;
    RWStructuredBuffer<uint> types;
    
    // 连接关系
    RWStructuredBuffer<uint> vertex_counts;
    RWStructuredBuffer<uint> vertex_offsets;
};

// 热路径计算着色器
[numthreads(256, 1, 1)]
void CSHeatPathCalculation(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint path_index = dispatch_id.x;
    
    // 并行处理每个热路径
    uint vertex_count = g_heat_path_data.vertex_counts[path_index];
    uint vertex_offset = g_heat_path_data.vertex_offsets[path_index];
    
    // 计算热传导
    for (uint i = 0; i < vertex_count - 1; i++) {
        uint v0_idx = vertex_offset + i;
        uint v1_idx = vertex_offset + i + 1;
        
        float3 p0 = g_heat_path_data.positions[v0_idx];
        float3 p1 = g_heat_path_data.positions[v1_idx];
        float t0 = g_heat_path_data.temperatures[v0_idx];
        
        // 热传导计算
        float heat_flux = CalculateHeatConduction(p0, p1, t0);
        
        // 更新温度
        g_heat_path_data.temperatures[v1_idx] = 
            UpdateTemperature(t0, heat_flux, g_delta_time);
    }
}
```

### 3.3 双精度计算实现

#### HLSL双精度支持
```hlsl
// 双精度计算着色器示例
struct DoublePrecisionConstants {
    double min_temperature;
    double max_temperature;
    double thermal_conductivity;
    double time_step;
};

ConstantBuffer<DoublePrecisionConstants> g_constants : register(b0);

// 双精度热传导计算
double CalculateHeatConductionDP(double3 pos1, double3 pos2, double temp1, double temp2)
{
    double distance = length(pos2 - pos1);
    if (distance < 1e-12) return 0.0;
    
    double temperature_gradient = (temp2 - temp1) / distance;
    double heat_flux = g_constants.thermal_conductivity * temperature_gradient;
    
    return heat_flux;
}

// 混合精度策略
[numthreads(64, 1, 1)]
void CSMixedPrecisionHeatSolver(uint3 dispatch_id : SV_DispatchThreadID)
{
    // 使用float进行大部分计算
    float3 pos1_f = asfloat(g_positions_double[dispatch_id.x * 2]);
    float3 pos2_f = asfloat(g_positions_double[dispatch_id.x * 2 + 1]);
    
    // 关键部分使用double
    double critical_value = CalculateCriticalValueDP(
        asdouble(pos1_f), 
        asdouble(pos2_f)
    );
    
    // 结果合并
    g_results[dispatch_id.x] = float(critical_value);
}
```

## 4. 构建系统设计

### 4.1 CMake构建配置

```cmake
# CMakeLists.txt - 根目录
cmake_minimum_required(VERSION 3.25)
project(Stardis-GPU LANGUAGES CXX C)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows平台配置
if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 子目录
add_subdirectory(src)
add_subdirectory(shaders)
add_subdirectory(tests)
add_subdirectory(tools)

# 安装配置
install(TARGETS stardis-gpu
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
```

### 4.2 着色器编译集成

```cmake
# shaders/CMakeLists.txt
find_program(DXC_EXECUTABLE dxc PATHS "$ENV{VULKAN_SDK}/Bin" "$ENV{DXSDK_DIR}/Bin")

# 着色器编译函数
function(compile_hlsl_shader SHADER_FILE ENTRY_POINT SHADER_MODEL OUTPUT_FILE)
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${DXC_EXECUTABLE}
            -T cs_6_6
            -E ${ENTRY_POINT}
            -Fo ${OUTPUT_FILE}
            ${SHADER_FILE}
            -D DOUBLE_PRECISION=1
            -D GPU_PLATFORM=1
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling HLSL shader ${SHADER_FILE}"
    )
endfunction()

# 编译计算着色器
compile_hlsl_shader(
    monte_carlo.hlsl
    CSMonteCarlo
    cs_6_6
    ${CMAKE_CURRENT_BINARY_DIR}/monte_carlo.cso
)

compile_hlsl_shader(
    heat_path.hlsl
    CSHeatPathCalculation
    cs_6_6
    ${CMAKE_CURRENT_BINARY_DIR}/heat_path.cso
)

# 创建着色器目标
add_custom_target(shaders ALL DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/monte_carlo.cso
    ${CMAKE_CURRENT_BINARY_DIR}/heat_path.cso
)
```

## 5. 验证测试框架

### 5.1 GPU/CPU结果对比

```cpp
class ResultValidator {
public:
    struct ValidationConfig {
        double absolute_tolerance = 1e-12;
        double relative_tolerance = 1e-9;
        uint32_t sample_count = 1000;
        bool enable_statistical_test = true;
    };
    
    ValidationResult ValidateGPUvsCPU(
        const GPURenderer& gpu_renderer,
        const CPURenderer& cpu_renderer,
        const TestScene& scene,
        const ValidationConfig& config);
    
private:
    // 逐像素对比
    bool ComparePixelByPixel(
        const ImageBuffer& gpu_image,
        const ImageBuffer& cpu_image,
        const ValidationConfig& config);
    
    // 统计测试
    StatisticalTestResult PerformStatisticalTest(
        const std::vector<double>& gpu_samples,
        const std::vector<double>& cpu_samples);
};

// 验证测试用例
TEST_F(GPUValidationTest, MonteCarloProbeSolver) {
    // 加载测试场景
    TestScene scene = LoadTestScene("probe_test.json");
    
    // CPU参考计算
    CPURenderer cpu_renderer;
    ImageBuffer cpu_result = cpu_renderer.SolveProbe(scene);
    
    // GPU计算
    GPURenderer gpu_renderer;
    ImageBuffer gpu_result = gpu_renderer.SolveProbe(scene);
    
    // 验证
    ValidationConfig config;
    config.absolute_tolerance = 1e-12;
    config.relative_tolerance = 1e-9;
    
    ResultValidator validator;
    ValidationResult result = validator.ValidateGPUvsCPU(
        gpu_renderer, cpu_renderer, scene, config);
    
    EXPECT_TRUE(result.passed) << "GPU/CPU结果不一致: " << result.message;
    EXPECT_GT(result.gpu_speedup, 10.0) << "GPU加速不足: " << result.gpu_speedup;
}
```

### 5.2 性能基准测试

```cpp
class PerformanceBenchmark {
public:
    struct BenchmarkResult {
        double cpu_time_ms;
        double gpu_time_ms;
        double speedup;
        double memory_usage_mb;
        double bandwidth_gbps;
        std::map<std::string, double> stage_times;
    };
    
    BenchmarkResult RunBenchmark(
        const std::string& benchmark_name,
        const BenchmarkScene& scene,
        uint32_t iteration_count = 10);
    
private:
    // 时间测量
    struct TimingScope {
        std::chrono::high_resolution_clock::time_point start;
        std::string stage_name;
        
        TimingScope(const std::string& name) : stage_name(name) {
            start = std::chrono::high_resolution_clock::now();
        }
        
        ~TimingScope() {
            auto end = std::chrono::high_resolution_clock::now();
            double duration = std::chrono::duration<double, std::milli>(end - start).count();
            // 记录到性能计数器
        }
    };
    
    // GPU查询
    class GPUQueryPool {
        ComPtr<ID3D12QueryHeap> m_query_heap;
        ComPtr<ID3D12Resource> m_query_buffer;
        
    public:
        void BeginQuery(ID3D12GraphicsCommandList* cmd_list, uint32_t query_index);
        void EndQuery(ID3D12GraphicsCommandList* cmd_list, uint32_t query_index);
        std::vector<uint64_t> ResolveQueries();
    };
};
```

## 6. 开发工作流和工具

### 6.1 开发环境设置脚本

```powershell
# setup-dev-env.ps1
Write-Host "设置Stardis-GPU开发环境..." -ForegroundColor Green

# 检查必要工具
$required_tools = @(
    @{Name="Visual Studio 2022"; Check={Test-Path "C:\Program Files\Microsoft Visual Studio\2022\Community"}},
    @{Name="Windows SDK"; Check={Test-Path "C:\Program Files (x86)\Windows Kits\10\Include"}},
    @{Name="DirectX Agility SDK"; Check={Test-Path "$env:USERPROFILE\.nuget\packages\microsoft.direct3d.d3d12.agility.sdk"}},
    @{Name="CMake"; Check={Get-Command cmake -ErrorAction SilentlyContinue}},
    @{Name="vcpkg"; Check={Test-Path "$env:VCPKG_ROOT"}}
)

# 安装缺失的依赖
if (-not (Test-Path "$env:VCPKG_ROOT")) {
    Write-Host "安装vcpkg..." -ForegroundColor Yellow
    git clone https://github.com/Microsoft/vcpkg.git "$env:USERPROFILE\vcpkg"
    & "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
}

# 安装项目依赖
Write-Host "安装项目依赖..." -ForegroundColor Yellow
& "$env:VCPKG_ROOT\vcpkg.exe" install `
    catch2 `
    fmt `
    spdlog `
    nlohmann-json `
    directxmath `
    directx-headers

# 生成开发配置
Write-Host "生成开发配置..." -ForegroundColor Green
$dev_config = @{
    build_type = "Debug"
    enable_tests = $true
    enable_shader_debug = $true
    double_precision = $true
} | ConvertTo-Json

$dev_config | Out-File "dev-config.json" -Encoding UTF8
```

### 6.2 调试和性能分析

```cpp
// GPU调试辅助工具
class GPUDebugHelper {
public:
    // 启用调试层
    static void EnableDebugLayer() {
        ComPtr<ID3D12Debug> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
            debug_controller->EnableDebugLayer();
        }
        
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings)))) {
            dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }
    
    // 设备移除数据诊断
    static void AnalyzeDeviceRemovedData(ID3D12Device* device) {
        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT auto_breadcrumbs_output = {};
            D3D12_DRED_PAGE_FAULT_OUTPUT page_fault_output = {};
            
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&auto_breadcrumbs_output))) {
                // 分析自动面包屑数据
                AnalyzeBreadcrumbs(auto_breadcrumbs_output);
            }
            
            if (SUCCEEDED(dred->GetPageFaultOutput(&page_fault_output))) {
                // 分析页错误数据
                AnalyzePageFault(page_fault_output);
            }
        }
    }
    
    // GPU标记工具
    class GPUMarker {
        ID3D12GraphicsCommandList* m_cmd_list;
        
    public:
        GPUMarker(ID3D12GraphicsCommandList* cmd_list, const char* name)
            : m_cmd_list(cmd_list) {
            PIXBeginEvent(m_cmd_list, 0, name);
        }
        
        ~GPUMarker() {
            PIXEndEvent(m_cmd_list);
        }
    };
};
```

## 7. 实施路线图详细任务

### Phase 1: 基础架构 (2周)

#### 第1周
1. **环境搭建** (3天)
   - Windows开发环境配置
   - Visual Studio 2022项目模板
   - vcpkg依赖管理配置
   - CMake构建系统基础

2. **DX12封装层** (4天)
   - 设备初始化和管理
   - 基础资源管理类
   - 命令列表和队列封装
   - 内存分配器集成

#### 第2周
1. **数据通路** (3天)
   - 输入文件解析器 (兼容现有格式)
   - 数据验证工具
   - 基础IO测试框架

2. **基础验证** (2天)
   - GPU/CPU基础数学运算验证
   - 内存传输正确性测试
   - 构建系统完整测试

### Phase 2: 核心算法迁移 (3周)

#### 第3周
1. **算法分析重构** (4天)
   - 蒙特卡洛算法GPU并行化设计
   - 数据结构AoS到SoA转换
   - 随机数生成GPU实现

2. **计算着色器开发** (3天)
   - 基础射线跟踪着色器
   - 热路径计算着色器
   - 双精度计算支持

#### 第4周
1. **算法集成** (5天)
   - GPU算法完整实现
   - 资源绑定和调度
   - 异步计算管道

2. **验证测试** (2天)
   - 功能正确性验证
   - 精度测试框架
   - 基础性能测试

#### 第5周
1. **性能优化** (3天)
   - 内存访问优化
   - 计算着色器优化
   - 管道状态优化

2. **完整测试** (4天)
   - 完整测试套件
   - 边界情况测试
   - 性能基准测试

### Phase 3: 优化和完善 (2周)

#### 第6周
1. **高级优化** (5天)
   - 异步计算优化
   - 内存层次优化
   - 指令级优化

2. **功能完整** (2天)
   - 所有输入格式支持
   - 错误处理和恢复
   - 日志和监控

#### 第7周
1. **文档和部署** (3天)
   - API文档生成
   - 用户指南编写
   - 示例代码编写

2. **最终验证** (2天)
   - 端到端测试
   - 性能达标验证
   - 发布准备

## 8. 质量保证措施

### 8.1 代码质量门禁
- 所有代码必须通过Clang-Tidy静态分析
- 代码覆盖率必须 > 80%
- 零编译器警告策略
- 定期代码审查

### 8.2 测试自动化
- CI/CD流水线集成测试
- nightly构建和测试
- 性能回归监测
- 内存泄漏检测

### 8.3 文档完整性
- API文档自动生成
- 架构决策记录 (ADR)
- 用户指南和教程
- 故障排除指南

## 9. 风险和应急计划

### 高风险应对
1. **双精度性能不足**
   - 应急: 实现混合精度回退方案
   - 监控: 持续性能监测和优化

2. **算法收敛问题**
   - 应急: 保留CPU参考实现
   - 监控: 收敛性测试和验证

3. **内存管理复杂性**
   - 应急: 简化内存管理策略
   - 监控: 内存使用分析和优化

### 进度风险应对
1. **技术难点延期**
   - 应急: 调整优先级，先完成核心功能
   - 监控: 每周进度评估和调整

2. **范围蔓延**
   - 应急: 严格需求冻结和变更控制
   - 监控: 定期范围验证和确认

---
*技术方案版本: 1.0.0*  
*创建日期: 2026-01-16*  
*预计实施开始: 2026-01-20*  
*预计完成: 2026-03-10*