# dxrstar-3d 实施路线图

**版本**: 0.1  
**日期**: 2026-02-20  
**前置文档**: [architecture.md](architecture.md), [module-design.md](module-design.md), [api-mapping.md](api-mapping.md)

---

## 总览

基于 custar-3d 的成功经验，dxrstar-3d 采用**自底向上的分阶段实施策略**，每个阶段都有明确的验证里程碑。

```
Phase 0 ─ 基础设施 (D3D12 Device + Memory + Descriptor)
    │
Phase 1 ─ 几何数据存储 + DXR 加速结构
    │
Phase 2 ─ 射线追踪 (Inline RT Compute Shader)
    │
Phase 3 ─ 最近点查询 + 包壳定位
    │
Phase 4 ─ 场景视图集成 + s3d.h 公共 API 打通
    │
Phase 5 ─ 批量操作 + 上下文复用
    │
Phase 6 ─ GPU/CPU 结果一致性验证
    │
Phase 7 ─ 性能优化 + AS Compaction
```

---

## Phase 0: 基础设施 (预计 2 周)

### 目标
建立 D3D12 设备管理、GPU 资源管理、描述符堆管理、PSO 管理的基础层。

### 任务清单

| # | 任务 | 文件 | 验证标准 |
|---|------|------|---------|
| 0.1 | CMakeLists.txt 项目骨架 | `CMakeLists.txt` | 编译通过 |
| 0.2 | D3D12 设备创建/销毁 | `dxrs3d_device.h/.cpp` | `test_dxrs3d_device.c` 通过 |
| 0.3 | DXR 能力检测 | `dxrs3d_device.cpp` | 正确报告 DXR tier |
| 0.4 | GPU 缓冲创建/销毁/上传/下载 | `dxrs3d_mem.h/.cpp` | 往返测试: upload → download == 原始数据 |
| 0.5 | Descriptor Heap 管理 | `dxrs3d_descriptor.h/.cpp` | 分配/释放 1000 个描述符 |
| 0.6 | Shader 编译框架 (DXC) | `dxrs3d_shader_mgr.h/.cpp` | 编译一个空 Compute Shader |
| 0.7 | 命令执行封装 (begin/submit/wait) | `dxrs3d_device.cpp` | 提交 → 等待 → 无错误 |

### 关键决策

- **Agility SDK**: 是否嵌入 D3D12 Agility SDK (确保最新 DXR 功能)?
  - 推荐: 是，版本 >= 1.614.0，确保 DXR 1.1 + Inline RT
- **D3D12MA**: 是否使用 D3D12 Memory Allocator?
  - 推荐: 初期不需要，手动管理；后期可引入
- **HLSL 编译模式**: 运行时编译 vs 离线编译?
  - 推荐: **离线编译** (DXIL .cso)，运行时加载，避免分发 dxcompiler.dll
  - 开发期可使用运行时编译方便迭代

### 里程碑 M0
- ✅ D3D12 Device 正确创建，报告 DXR Tier
- ✅ GPU 缓冲 upload → download 数据一致
- ✅ Compute Shader 编译并创建 PSO 成功

---

## Phase 1: 几何数据 + 加速结构 (预计 2 周)

### 目标
实现几何数据平坦化和 DXR BLAS/TLAS 构建。

### 任务清单

| # | 任务 | 文件 | 验证标准 |
|---|------|------|---------|
| 1.1 | 类型定义 | `dxrs3d_types.h` | 编译通过 |
| 1.2 | geom_store 框架 | `dxrs3d_geom_store.h/.cpp` | 创建/销毁无泄漏 |
| 1.3 | geom_store_sync (三角形) | `dxrs3d_geom_store.cpp` | 顶点/索引正确上传 |
| 1.4 | geom_store_sync (球体) | `dxrs3d_geom_store.cpp` | 球体数据 + AABB 上传 |
| 1.5 | BLAS 构建 (三角形) | `dxrs3d_accel.cpp` | 构建成功，无 D3D12 错误 |
| 1.6 | BLAS 构建 (球体/AABB) | `dxrs3d_accel.cpp` | AABB 几何 BLAS 构建成功 |
| 1.7 | TLAS 构建 (单层,无实例) | `dxrs3d_accel.cpp` | TLAS 构建成功 |
| 1.8 | TLAS 构建 (实例化场景) | `dxrs3d_accel.cpp` | 多 BLAS + 实例变换 |
| 1.9 | 伪实例机制 | `dxrs3d_accel.cpp` | 混合场景 TLAS 构建 |
| 1.10 | AABB 查询 | `dxrs3d_accel.cpp` | `get_bounds()` 返回正确值 |

### 关键实现点

**三角形 BLAS**:
```cpp
D3D12_RAYTRACING_GEOMETRY_DESC geom_desc = {};
geom_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
geom_desc.Triangles.VertexBuffer.StartAddress = d_vertices.gpu_va;
geom_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
geom_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
geom_desc.Triangles.VertexCount = n_vertices;
geom_desc.Triangles.IndexBuffer = d_indices.gpu_va;
geom_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
geom_desc.Triangles.IndexCount = n_triangles * 3;
geom_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;  /* non-opaque for Top-K */
```

**球体 BLAS**:
```cpp
D3D12_RAYTRACING_GEOMETRY_DESC geom_desc = {};
geom_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
geom_desc.AABBs.AABBs.StartAddress = d_sphere_aabbs.gpu_va;
geom_desc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);  /* 24 bytes */
geom_desc.AABBs.AABBCount = n_spheres;
geom_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
```

### 里程碑 M1
- ✅ 三角形网格 BLAS 构建成功 (Cornell Box 级别复杂度)
- ✅ 球体 AABB BLAS 构建成功
- ✅ TLAS 构建成功 (含实例变换 + 伪实例)
- ✅ `get_bounds()` 返回正确 AABB

---

## Phase 2: 射线追踪 (预计 3 周)

### 目标
实现 Inline RT 射线追踪 Compute Shader，支持单次命中和 Top-K 多命中。

### 任务清单

| # | 任务 | 文件 | 验证标准 |
|---|------|------|---------|
| 2.1 | HLSL 公共定义 | `common.hlsli`, `math_utils.hlsli` | 编译通过 |
| 2.2 | 球体交叉测试 shader | `sphere_intersection.hlsli` | 单元测试 |
| 2.3 | 单次最近命中 shader | `trace_rays.hlsl` | 编译 + PSO |
| 2.4 | Top-K 多命中 shader | `trace_rays_topk.hlsl` | 编译 + PSO |
| 2.5 | Host 端批处理框架 | `dxrs3d_trace.h/.cpp` | API 签名就绪 |
| 2.6 | 批量射线: 上传→Dispatch→下载 | `dxrs3d_trace.cpp` | 三角形场景命中 |
| 2.7 | 单射线 API | `dxrs3d_trace.cpp` | 单射线命中正确 |
| 2.8 | 球体命中支持 | `trace_rays*.hlsl` | 球体交叉 + 命中结果 |
| 2.9 | Top-K 多命中验证 | `dxrs3d_trace.cpp` | K=2 返回正确候选 |
| 2.10 | 命中结果转换 | `dxrs3d_prim.cpp`, `dxrs3d_trace_util.h` | UV/法线与 custar-3d 一致 |

### UV/法线约定验证矩阵

| 测试场景 | 验证内容 | 容差 |
|---------|---------|------|
| 轴对齐三角形 | u, v 重心坐标 | 1e-6 |
| 倾斜三角形 | 法线方向 | 1e-6 |
| 球体中心射线 | distance = radius | 1e-6 |
| 球体切线射线 | distance, uv | 1e-4 |
| 法线翻转 | 背面命中法线方向 | 精确匹配 |

### 里程碑 M2
- ✅ Cornell Box 场景: 批量射线追踪返回正确命中
- ✅ 混合场景 (三角形+球体): 命中正确
- ✅ Top-K: K=2 返回正确排序的候选命中
- ✅ UV/法线: 与 custar-3d 结果一致 (容差 1e-6)

---

## Phase 3: 最近点 + 包壳定位 (预计 2 周)

### 目标
实现最近点查询和包壳定位的 Compute Shader。

### 任务清单

| # | 任务 | 文件 | 验证标准 |
|---|------|------|---------|
| 3.1 | 最近点 shader (多射线近似) | `closest_point.hlsl` | 编译 + PSO |
| 3.2 | 最近点 Host API | `dxrs3d_closest_point.h/.cpp` | API 签名就绪 |
| 3.3 | 最近点批处理 | `dxrs3d_closest_point.cpp` | 简单场景通过 |
| 3.4 | 包壳定位 shader (6-ray) | `find_enclosure.hlsl` | 编译 + PSO |
| 3.5 | 包壳定位 Host API | `dxrs3d_find_enclosure.h/.cpp` | API 签名就绪 |
| 3.6 | 包壳定位批处理 | `dxrs3d_find_enclosure.cpp` | 封闭体内点检测 |

### 里程碑 M3
- ✅ 最近点查询: 返回最近的三角形及距离
- ✅ 包壳定位: 正确判断点在哪个封闭体内

---

## Phase 4: 场景视图集成 (预计 2 周)

### 目标
将 dxrs3d_* 模块集成到 s3d_scene_view 层，打通完整的 s3d.h 公共 API。

### 任务清单

| # | 任务 | 文件 | 验证标准 |
|---|------|------|---------|
| 4.1 | s3d_device_c.h 修改 | `s3d_device_c.h` | `gpu` 指向 dxrs3d_device |
| 4.2 | s3d_scene_view_c.h 修改 | `s3d_scene_view_c.h` | 含 dxrs3d_* 指针 |
| 4.3 | s3d_device.cpp 改写 | `s3d_device.cpp` | 调用 dxrs3d_device_* |
| 4.4 | scene_view_setup_dxr() | `s3d_scene_view.cpp` | 第 8 章同步流水线 |
| 4.5 | trace_ray 改写 | `s3d_scene_view_trace_ray.cpp` | Top-K + Filter |
| 4.6 | closest_point 改写 | `s3d_scene_view_closest_point.cpp` | 调用 dxrs3d_cp |
| 4.7 | find_enclosure 改写 | `s3d_scene_view_find_enclosure.cpp` | 调用 dxrs3d_enc |
| 4.8 | 复用主机端模块 | `s3d_scene.cpp` 等 | 不修改，直接复用 |

### 复用策略

从 custar-3d 复制以下文件并**零修改**使用:
```
s3d.h, s3d_buffer.h, s3d_c.h, s3d_shape_c.h
s3d_geometry.h/.cpp, s3d_mesh.h/.cpp, s3d_sphere.h/.cpp
s3d_instance.h/.cpp, s3d_primitive.cpp, s3d_scene.cpp, s3d_shape.cpp
```

需要改写的文件 (替换 cus3d_* → dxrs3d_*):
```
s3d_device_c.h, s3d_device.cpp
s3d_scene_view_c.h, s3d_scene_view.cpp
s3d_scene_view_trace_ray.cpp
s3d_scene_view_closest_point.cpp
s3d_scene_view_batch_trace.cpp
s3d_scene_view_batch_closest_point.cpp
s3d_scene_view_find_enclosure.cpp
```

### 里程碑 M4
- ✅ `s3d_device_create()` → 正确初始化 DXR 设备
- ✅ `s3d_scene_view_create()` → 构建 BLAS/TLAS 成功
- ✅ `s3d_scene_view_trace_ray()` → 单射线 + Filter 正确
- ✅ 所有 custar-3d 单元测试在 dxrstar-3d 上通过

---

## Phase 5: 批量操作 (预计 1 周)

### 目标
实现批量操作 API 及上下文复用机制。

### 任务清单

| # | 任务 | 验证标准 |
|---|------|---------|
| 5.1 | `s3d_batch_trace_context_create/destroy` | 预分配缓冲成功 |
| 5.2 | `s3d_scene_view_trace_rays_batch()` | 批量射线结果正确 |
| 5.3 | `s3d_scene_view_trace_rays_batch_ctx()` | 上下文复用模式正确 |
| 5.4 | `s3d_batch_cp_context_create/destroy` | 最近点批量 |
| 5.5 | `s3d_scene_view_closest_point_batch()` | 结果正确 |
| 5.6 | `s3d_batch_enc_context_create/destroy` | 包壳批量 |
| 5.7 | `s3d_scene_view_find_enclosure_batch()` | 结果正确 |
| 5.8 | 统计信息填充 | `stats` 字段正确 |

### 里程碑 M5
- ✅ 批量射线追踪 (1000+ 射线) 结果正确
- ✅ 上下文复用模式正常工作
- ✅ 统计信息准确

---

## Phase 6: GPU/CPU 一致性验证 (预计 2 周)

### 目标
使用现有 custar-3d 测试套件验证 dxrstar-3d 的数值正确性。

### 任务清单

| # | 任务 | 验证标准 |
|---|------|---------|
| 6.1 | 移植 custar-3d 所有 test_s3d_*.c 测试 | 编译通过 |
| 6.2 | 单射线追踪测试 | `test_s3d_trace_ray.c` 全部通过 |
| 6.3 | 批量射线追踪测试 | `test_s3d_batch_trace.c` 全部通过 |
| 6.4 | 最近点测试 | `test_s3d_closest_point.c` 全部通过 |
| 6.5 | 包壳定位测试 | 包壳测试全部通过 |
| 6.6 | 实例化场景测试 | `test_s3d_trace_ray_instance.c` 通过 |
| 6.7 | 球体场景测试 | `test_s3d_trace_ray_sphere.c` 通过 |
| 6.8 | AABB 测试 | `test_s3d_scene_view_aabb.c` 通过 |
| 6.9 | 采样测试 | `test_s3d_sampler.c` 通过 |
| 6.10 | Cornell Box 逐像素对比 | 容差 1e-6 |

### custar-3d vs dxrstar-3d 数值对比工具

```
custar-3d 输出 (参考值) ←→ dxrstar-3d 输出 (待验证)
                          │
                  逐射线/逐像素对比
                  ├─ distance: |Δ| < 1e-6
                  ├─ uv: |Δ| < 1e-6
                  ├─ normal: |Δ| < 1e-5
                  ├─ primID: 精确匹配
                  └─ instanceID: 精确匹配
```

### 里程碑 M6
- ✅ 全部 custar-3d 测试在 dxrstar-3d 上通过
- ✅ Cornell Box 渲染结果逐像素一致
- ✅ IR 渲染示例输出一致

---

## Phase 7: 性能优化 (持续)

### 优化方向

| 优化 | 预期收益 | 优先级 |
|------|---------|-------|
| AS Compaction | 减少 30-50% AS 内存 | P1 |
| AS Refit | 动态场景更新加速 10x | P2 |
| 预分配 Upload/Readback Heap | 减少 per-batch 分配开销 | P1 |
| Wave Intrinsics | 归约操作加速 2-4x | P3 |
| 多 Command Queue 流水线 | Build/Trace 重叠执行 | P2 |
| Descriptor Indexing (Bindless) | 减少 root table 切换 | P3 |
| Shader 离线编译 | 消除运行时编译开销 | P1 |

---

## 依赖与先决条件

### 硬件要求
- GPU: 支持 DXR 1.1 (NVIDIA Turing+, AMD RDNA2+, Intel Arc)
- OS: Windows 10 v1903+ 或 Windows 11
- 驱动: 支持 D3D12 Feature Level 12.1+

### 软件依赖
- Windows SDK 10.0.20348.0+ (含 D3D12 + DXR headers)
- DirectX Shader Compiler (DXC) — 用于 HLSL SM 6.5+ 编译
- PIX (可选，调试/分析)

### CMake 配置要点
```cmake
# dxrstar-3d CMakeLists.txt 骨架
project(dxrstar-3d LANGUAGES CXX)

# D3D12 依赖
find_package(directx-headers CONFIG)  # 或手动指定 SDK 路径
target_link_libraries(dxrstar-3d PRIVATE
    d3d12.lib dxgi.lib d3dcompiler.lib dxcompiler.lib)

# HLSL 编译规则
function(compile_hlsl shader_file entry_point profile output_file)
    add_custom_command(
        OUTPUT ${output_file}
        COMMAND dxc -T ${profile} -E ${entry_point}
                -Fo ${output_file} ${shader_file}
                -enable-16bit-types
        DEPENDS ${shader_file}
    )
endfunction()

compile_hlsl(trace_rays.hlsl TraceRaysCS cs_6_5 trace_rays.cso)
compile_hlsl(trace_rays_topk.hlsl TraceRaysTopKCS cs_6_5 trace_rays_topk.cso)
compile_hlsl(closest_point.hlsl ClosestPointCS cs_6_5 closest_point.cso)
compile_hlsl(find_enclosure.hlsl FindEnclosureCS cs_6_5 find_enclosure.cso)
compile_hlsl(compute_bounds.hlsl ComputeBoundsCS cs_6_0 compute_bounds.cso)
```

---

## 时间线总览

```
Week 1-2:   Phase 0 — 基础设施 (D3D12 Device + Mem + Descriptor + PSO)
Week 3-4:   Phase 1 — 几何数据 + BLAS/TLAS
Week 5-7:   Phase 2 — 射线追踪 (Inline RT)
Week 8-9:   Phase 3 — 最近点 + 包壳定位
Week 10-11: Phase 4 — 场景视图集成
Week 12:    Phase 5 — 批量操作
Week 13-14: Phase 6 — 一致性验证
Week 15+:   Phase 7 — 性能优化 (持续)
```

**预计总工期: 14-16 周** (1 人)

---

## 与 custar-3d 并行开发策略

```
共享层(不变):
  s3d.h, s3d_scene.cpp, s3d_shape.cpp, s3d_mesh.cpp,
  s3d_sphere.cpp, s3d_instance.cpp, s3d_primitive.cpp
  → 抽离到独立库 `s3d-core`，两个后端共享

后端层(各自独立):
  custar-3d/0.10/src/cus3d_*   (已完成)
  dxrstar-3d/0.10/src/dxrs3d_* (待实现)
  → 通过 CMake option 选择编译哪个后端

桥接层(需条件编译):
  s3d_device_c.h    → #ifdef USE_DXR / USE_CUBQL
  s3d_scene_view_c.h → #ifdef USE_DXR / USE_CUBQL
  s3d_device.cpp     → 后端选择
  s3d_scene_view*.cpp → 后端选择
```

**推荐**: 使用 CMake `option(USE_DXR "Use DXR backend" OFF)` 控制后端选择，避免运行时开销。

---

*本文档为 dxrstar-3d 项目的完整实施路线图。具体实现时请结合 custar-3d 源代码和 DXR API 文档。*
