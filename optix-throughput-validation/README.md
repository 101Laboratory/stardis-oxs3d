# OptiX 9.1.0 Throughput Benchmark

GPU加速批量光追吞吐量测试，基于 NVIDIA OptiX 9.1.0 构建。

## 项目结构

```
optix-throughput-validation/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文件
├── include/
│   ├── optix_check.h           # CUDA + OptiX 错误检查宏
│   ├── ray_types.h             # 共享数据结构 (Ray, HitResult)
│   └── launch_params.h         # OptiX启动参数/SBT数据结构
├── src/
│   ├── device_manager.h/cpp    # 设备管理：CUDA初始化、OptiX上下文
│   ├── buffer_manager.h        # 缓冲区管理：RAII GPU内存模板
│   ├── geometry_manager.h/cpp  # 几何管理：场景生成(Cornell Box/随机三角形)
│   ├── accel_manager.h/cpp     # 加速结构管理：GAS/IAS构建与压缩
│   ├── pipeline_manager.h/cpp  # 管线管理：Module/ProgramGroup/Pipeline/SBT
│   ├── ray_tracer.h/cpp        # 光追请求：单次/批量追踪
│   └── main.cpp                # 吞吐量测试主程序
└── device/
    ├── programs.cu             # OptiX设备程序 (raygen/miss/closesthit)
    ├── kernels.h               # CUDA工具内核声明
    └── kernels.cu              # CUDA工具内核实现 (射线生成等)
```

## 模块说明

| 模块 | 功能 |
|------|------|
| **DeviceManager** | CUDA设备初始化、OptiX上下文创建与管理、设备信息查询 |
| **CudaBuffer\<T\>** | RAII GPU内存封装，支持同步/异步上传下载 |
| **GeometryManager** | 测试场景生成：单三角形、Cornell Box、随机网格、均匀网格 |
| **AccelManager** | GAS/IAS加速结构构建，支持压缩(compaction) |
| **PipelineManager** | OptiX Module编译、ProgramGroup创建、Pipeline链接、SBT构建 |
| **RayTracer** | 单次光追(`traceSingle`)、批量光追(`traceBatch`)、计时批量光追(`traceBatchTimed`) |

## 测试内容

1. **射线数量扫描** - Cornell Box场景下从1K到64M射线的吞吐量
2. **场景复杂度扫描** - 固定1M射线，场景从1个到100万个三角形
3. **实例加速(IAS)测试** - 不同实例数量下的性能
4. **多流并发测试** - 1/2/4/8个CUDA stream并发launch

## 构建

### 前提条件
- CUDA Toolkit 12.x+
- OptiX SDK 9.1.0 (安装在 `C:\ProgramData\NVIDIA Corporation\OptiX SDK 9.1.0`)
- CMake 3.18+
- Visual Studio 2022

### 构建命令

```powershell
cd optix-throughput-validation
mkdir build; cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### 运行

```powershell
# 完整测试 (默认64M最大射线数，10次迭代)
.\build\Release\optix_throughput.exe

# 快速测试
.\build\Release\optix_throughput.exe --max-rays 1048576 --iters 3

# 不运行验证
.\build\Release\optix_throughput.exe --no-validate

# 查看帮助
.\build\Release\optix_throughput.exe --help
```

### CTest

```powershell
cd build
ctest -C Release --output-on-failure
```

## 输出

### 控制台输出
格式化表格显示每个测试用例的:
- 射线数量 / 三角形数量
- 构建时间(ms)
- 追踪时间(ms)
- MRays/s (百万射线/秒)
- GRays/s (十亿射线/秒)
- 命中率(%)

### CSV输出
结果保存至 `optix_throughput_results.csv`，可导入Excel/Python分析。

## OptiX 9 关键特性使用

- **Built-in Triangle Intersection** - 硬件加速三角形求交
- **Compacted GAS** - 加速结构内存压缩
- **Instance Acceleration** - IAS/GAS两级加速结构
- **Buffer-based Raycasting** - 预生成射线缓冲区批量追踪
- **Multi-stream Launch** - 多CUDA Stream并发追踪
- **Stack Size Optimization** - 自动计算最优栈大小
- **Payload Registers** - 高效的光线-着色器数据传递
