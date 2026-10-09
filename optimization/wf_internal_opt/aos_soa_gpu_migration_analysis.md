# AoS↔SoA 转换 CPU→GPU 迁移可行性分析

**创建日期**: 2026-03-04  
**关联文件**:  
- `stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` (collect L1494, distribute L2099)  
- `stardis-cus3d/oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` (AoS→SoA L2517, SoA→AoS L2590)  
- `stardis-cus3d/oxstar-3d/0.10/include/ray_types.h` (Ray 32B, HitResult 40B, FilterPerRayData 16B)  
- `stardis-cus3d/oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h` (hitresult_to_s3d_hit L419, resolve_shape L492)  
- `optimization/solver_soa/P2_domain_decomposition_dev_guide.md` (path_state 域分解计划)

**状态**: 分析完成，结论为 GPU 迁移不可行，推荐替代路径

---

## TL;DR

当前 persistent wavefront solver 中存在两层 AoS↔SoA 转换：**求解器层**（`path_state` ↔ packed 射线/结果数组）和 **GPU 接口层**（`s3d_ray_request` → `Ray`，`HitResult` → `s3d_hit`）。分析表明，将这些转换直接迁移到 GPU 上**不可行或收益为负**——核心瓶颈不是转换计算本身，而是 CPU 端跨 2040B 步幅 AoS 结构的稀疏内存访问。真正有效的优化路径是：(1) 消除中间格式层级（直接写 pinned `Ray[]`），(2) 推进 P2 域分解以降低 CPU 访问步幅，(3) 在 GPU kernel 内部完成 `HitResult` → 精简结果的转换以减少 D2H 传输量。

---

## 1. 当前数据流

### H2D 方向（3 步转换链）

| 步骤 | 位置 | 操作 | 数据量 (W=2000) |
|------|------|------|----------------|
| ① `collect` | `sdis_solve_persistent_wavefront.c` L1494 | `path_state[i]` (2040B stride) → `s3d_ray_request[]` (40B) | 读 ~4MB 散射, 写 80KB |
| ② CPU convert | `ox_s3d_scene_view.cpp` L2517 | `s3d_ray_request[i]` → pinned `Ray[i]`(32B) + `FilterPerRayData[i]`(16B) | 读 80KB, 写 96KB |
| ③ H2D upload | `ox_s3d_scene_view.cpp` L2540 | `cudaMemcpyAsync` pinned → device | 96KB |

**步骤① collect 细节**：OMP 3-pass bucketed scatter (zero-atomic)。Pass 1 按 bucket 类型统计每线程射线数；prefix sum 计算全局 bucket offset + 每线程写入基址；Pass 2 散射射线数据到 `ray_requests[]`。瓶颈在于每条射线需从 2040B stride 的 `path_state[i]` 中提取 8 个 float 字段（~32B 有效载荷/2040B 步幅 = 1.6% 利用率）。

**步骤② CPU convert 细节**：简单的逐元素字段赋值 + `memcpy`（filter_per_ray 布局匹配，零转换）：
```cpp
for (size_t i = 0; i < nrays; i++) {
    ctx->h_rays_pinned[i].origin    = make_float3(requests[i].origin[0], ...);
    ctx->h_rays_pinned[i].direction = make_float3(requests[i].direction[0], ...);
    ctx->h_rays_pinned[i].tmin      = requests[i].range[0];
    ctx->h_rays_pinned[i].tmax      = requests[i].range[1];
}
memcpy(ctx->h_filter_pinned, filter_per_ray, nrays * sizeof(FilterPerRayData));
```
耗时约 2-5μs（pinned memory WC 写入通道，几乎不占 cycle 预算）。

### D2H 方向（3 步转换链）

| 步骤 | 位置 | 操作 | 数据量 (W=2000) |
|------|------|------|----------------|
| ④ D2H download | `ox_s3d_scene_view.cpp` L2590 | device `HitResult[]` → pinned | 80KB |
| ⑤ CPU convert | `ox_s3d_internal.h` L419 | `HitResult[i]` → `s3d_hit[i]`（需 `resolve_shape()` 查表 + sphere UV 计算） | 80KB → 112KB |
| ⑥ `distribute` | `sdis_solve_persistent_wavefront.c` L2099 | `s3d_hit[]` + `step_*()` 物理计算 → 写回 `path_state[i]` (2040B stride) | 读 112KB, 散射写 ~4MB |

**步骤⑤ hitresult_to_s3d_hit 细节**：
- 调用 `resolve_shape(sv, hr.geom_id, shape_id)`——查询 `std::unordered_map<uint, s3d_shape*>`
- 填充 `s3d_hit.prim` 结构体（含 CPU 端 `shape__` 和 `inst__` 指针）
- 球体类型需 `shape->type` 判断 + `acos/atan2` UV 计算
- mesh 类型调用 `trace_hit_fixup`（CCW→CW 法线翻转）
- OMP 并行，`schedule(static)`，nrays < 256 时退化为串行

**步骤⑥ distribute 细节**：3 阶段分桶分发（Radiative/Conductive/Other），每个射线结果执行完整的物理状态推进（`step_radiative_trace`, `step_conductive_ds_process`, `advance_one_step_with_ray`），包含 O7 prefetch 优化（预取 4 iter ahead）。

---

## 2. 数据结构尺寸参考

| 结构 | 大小 | 存储布局 | 角色 |
|------|------|----------|------|
| `path_state` | ~2040B | AoS: `slots[pool_size]` | 路径完整状态 |
| `s3d_ray_request` | 40B | packed AoS 中间缓冲 | CPU collect 输出，GPU convert 输入 |
| `Ray` (GPU) | 32B | pinned + device | GPU 射线输入 |
| `FilterPerRayData` | 16B | pinned + device | GPU 内联过滤器参数 |
| `HitResult` (GPU) | 40B | pinned + device | GPU 单 hit 输出 |
| `s3d_hit` | ~56B | AoS 中间缓冲 | CPU convert 输出，distribute 输入 |
| `dispatch_soa` per slot | 20B | 5 个独立 SoA 数组 | 调度热字段 |

---

## 3. 性能基准数据

### 小规模场景（256² spp4, pool=4096, OMP ON）

| 阶段 | 耗时(s) | 占比 | 每步均摊(μs) |
|------|---------|------|-------------|
| trace (GPU) | 89.6 | 58.7% | 724 |
| distribute | 16.8 | 11.0% | 136 |
| collect | 14.3 | 9.4% | 116 |
| cascade | 11.0 | 7.2% | 89 |
| harvest+refill | 8.6 | 5.6% | 70 |
| compact ×2 | 5.5 | 3.6% | 45 |

### 大规模场景（320² spp32, pool=8192, OMP ON）

| 阶段 | 耗时(s) | 占比 | 每步均摊(μs) |
|------|---------|------|-------------|
| trace (GPU) | 1007.7 | 63.0% | — |
| distribute | 209.8 | 13.1% | 530 |
| collect | 160.0 | 10.0% | 404 |
| cascade | 79.8 | 5.0% | 202 |
| harvest+refill | 51.6 | 3.2% | — |
| compact | 38.7 | 2.4% | — |

**CPU 管理总成本跨场景恒定占 ~29%**。

---

## 4. GPU 迁移方案逐项评估

### 方案 A：上传 `path_state[]` 到 GPU，GPU 端 gather + convert

**思路**：跳过 CPU collect，直接将 `path_state` AoS 数组 + `need_ray_indices` 上传到 device，用 CUDA kernel 提取射线字段。

**结论：❌ 不可行**

| 维度 | 评估 |
|------|------|
| 指针问题 | `path_state` 含 CPU 指针（`filter_data_storage.hit_3d.prim.shape__`、`scn` 指针等），GPU 端无效 |
| 传输量 | 从 96KB 暴增到 ~8MB（pool_size=4096 × 2040B），即使只传 `need_ray` 子集也需 ~4MB |
| 内存访问 | `collect` 瓶颈是 2040B 步幅随机读→cache miss，迁移到 GPU 后变成 DRAM latency + warp divergence，更差 |
| 固定开销 | GPU kernel launch + sync 约 5μs，可能超过转换计算本身 |

### 方案 B：上传 `s3d_ray_request[]` 到 GPU，GPU 端转换为 `Ray[]`

**思路**：省去 CPU 端 `s3d_ray_request` → `Ray` 逐元素拷贝，直接将 `s3d_ray_request` 上传到 device，GPU kernel 做字段重排。

**结论：❌ 收益微不足道**

| 维度 | 评估 |
|------|------|
| CPU 耗时 | 步骤②仅 2-5μs（2000 × 8 float 赋值，pinned WC 写入近零延迟） |
| 指针字段 | `s3d_ray_request` 含 `void* filter_data` 指针，GPU 无法解引用 |
| 额外开销 | 新增 kernel launch (~5μs) + device 端额外 `s3d_ray_request` buffer 分配 |
| 净效果 | 负：增加 kernel launch latency，无法隐藏在任何 overlap 中 |

### 方案 C：GPU 端完成 `HitResult` → `s3d_hit` 转换

**思路**：在 GPU kernel 输出阶段直接生成 `s3d_hit` 格式，或在 D2H 前用 CUDA kernel 转换。

**结论：❌ 不可行**（基于当前架构）

| 维度 | 评估 |
|------|------|
| 依赖关系 | `hitresult_to_s3d_hit()` 需调用 `resolve_shape()`——查询 `std::unordered_map<uint, s3d_shape*>` |
| CPU 指针 | `s3d_hit.prim.shape__` 和 `s3d_hit.prim.inst__` 是 CPU 端对象指针，GPU 端不可生成 |
| 场景数据 | sphere 类型需 `shape->type`、`shape->flip_surface`、`sv->find_snapshot()` 等 CPU-only 数据 |
| 改动范围 | 完全迁移需将整个场景映射表+shape 元数据扁平化到 device，改动巨大且持续维护成本高 |

### 方案 D：GPU 端直接 scatter 结果回 `path_state`

**思路**：使用 UVA 或 host-mapped memory，让 GPU kernel 直接写回 `path_state` 字段。

**结论：❌ 架构不兼容**

| 维度 | 评估 |
|------|------|
| 结构复杂性 | `path_state` 是 2040B 复杂 C 结构体，含大量 CPU 指针和 union |
| 物理计算 | `distribute` 执行 `step_radiative_trace()` 等完整物理步进函数，不可能在 GPU 完成 |
| 带宽限制 | Host-mapped memory 写入带宽仅 ~6 GB/s (PCIe)，远低于 CPU DDR |

---

## 5. 可行且有实际收益的替代优化方案

### 方案 E：消除 `s3d_ray_request` 中间层——collect 直接写 pinned `Ray[]`

**思路**：将步骤①②合并。`collect` 函数直接将 `path_state[i]` 射线字段写入 pinned `Ray[]` + `FilterPerRayData[]`，消除 `s3d_ray_request` 中间缓冲区。

| 维度 | 评估 |
|------|------|
| 可行性 | **高**。`collect` 已逐字段写 `s3d_ray_request`——只需改写目标为 pinned `Ray*` + `FilterPerRayData*` |
| 接口改动 | 求解器层需获取 `s3d_batch_trace_context` 的 pinned 指针（通过 `s3d_scene_view` 接口暴露） |
| 内存节省 | 消除 `pv->ray_requests[]` 分配（W×40B = 80KB） |
| 预期收益 | ~5μs/step（步骤②全部耗时）+ L2/L3 cache 污染减少 |
| 风险 | 中——跨 `sdis_solver` ↔ `oxstar-3d` 层暴露 pinned 指针，打破 API 封装 |

**实现要点**：
```c
/* collect 直接写 pinned Ray (跳过 s3d_ray_request 中间层) */
Ray* pinned_rays = pool->pinned_ray_ptr;  /* 从 s3d_batch_trace_context 获取 */
FilterPerRayData* pinned_filter = pool->pinned_filter_ptr;

/* OMP Pass 2: scatter 直接到 pinned staging */
{
  size_t ray_idx = my_cursor[bkt]++;
  Ray* rr = &pinned_rays[ray_idx];
  rr->origin    = make_float3(p->ray_req.origin[0], p->ray_req.origin[1], p->ray_req.origin[2]);
  rr->direction = make_float3(p->ray_req.direction[0], p->ray_req.direction[1], p->ray_req.direction[2]);
  rr->tmin      = p->ray_req.range[0];
  rr->tmax      = p->ray_req.range[1];
  // fill_filter_per_ray 直接写 pinned_filter[ray_idx]
}
```

### 方案 F：消除 `s3d_hit` 中间层——distribute 直接读 `HitResult`

**思路**：将步骤⑤⑥合并。`distribute` 直接读 pinned `HitResult[]`，在 `step_*` 函数中内联必要的转换。

| 维度 | 评估 |
|------|------|
| 可行性 | **中**。`step_radiative_trace(p, scn, h0)` 需改为接受 `const HitResult*` 或 `hit_view` 适配器 |
| 查表优化 | `resolve_shape()` 可按 `geom_id` 批量执行（相同 geom_id 的射线结果共享查找结果） |
| 数据量 | `s3d_hit`(56B) 比 `HitResult`(40B) 大 40%——读取量直接减少 |
| 预期收益 | ~20-30μs/step（步骤⑤耗时 + distribute cache 行为改善），约 19% distribute 改善 |
| 风险 | 高——`s3d_hit` 被 30+ 个求解器函数使用，接口改动面大 |

### 方案 G：GPU kernel 输出精简格式，减少 D2H 量

**思路**：在 GPU inline filter kernel 中做 geom→shape 映射（device-side lookup table），输出预解析的 `shape_id`，去掉 `geom_id`+`inst_id`。

| 维度 | 评估 |
|------|------|
| 可行性 | **中**。场景通常 <1000 个 geometry，映射表 <4KB，放 constant memory |
| 输出格式 | `{t, bary_u, bary_v, prim_idx, shape_id, normal[3]}` = 36B（节省 10%） |
| D2H 节省 | 80KB → 72KB，绝对省 ~5μs（PCIe 传输时间本就 <50μs） |
| 核心价值 | **为方案 F 扫除障碍**——shape_id 已在 GPU 解析，distribute 无需 `resolve_shape()` |
| 风险 | 中——需在 pipeline 初始化时构建并上传 geom→shape 映射表，场景更新时同步 |

### 方案 H（核心方案）：P2 域分解降低 collect/distribute 访问步幅

这是 `optimization/solver_soa/P2_domain_decomposition_dev_guide.md` 的核心内容。

| 维度 | 评估 |
|------|------|
| 机制 | 将 `path_state`(2040B) 拆为 `path_core`(480B) + `path_ray_io`(228B) + 其他子结构体 |
| collect 影响 | 只需读 `path_ray_io[]`(228B stride) → **cache 利用率提升 9×** |
| distribute 影响 | 只需写 `path_ray_io[]` + 部分 `path_core[]` 字段 |
| 预期收益 | collect 116→30-40μs，distribute 136→40-50μs，**最大单项优化 ~150-200μs/step** |
| 风险 | 高——pool 核心数据结构重构，需全面回归测试 |

---

## 6. 定量对比总结

| 方案 | 收益 (μs/step) | 实现难度 | 对架构的影响 | 推荐度 |
|------|----------------|---------|-------------|--------|
| A: path_state 上传 GPU | **-50~-100** (负) | 极高 | 重写求解器 | ❌ |
| B: s3d_ray_request GPU 转换 | **-3~+2** | 中 | 新增 kernel | ❌ |
| C: HitResult GPU 转 s3d_hit | N/A (不可行) | 极高 | 场景元数据上传 | ❌ |
| D: GPU scatter 到 path_state | N/A (不兼容) | 极高 | UVA + 重写 | ❌ |
| **E: 消除 s3d_ray_request** | **+5** | 中 | 跨层接口 | ✅ |
| **F: 消除 s3d_hit 中间层** | **+20~30** | 高 | 求解器接口改动 | ✅ |
| **G: GPU 预解析 shape_id** | **+5~10** | 中 | device lookup table | ✅ |
| **H: P2 域分解 (path_ray_io SoA)** | **+150~200** | 高 | pool 核心重构 | ✅✅✅ |

---

## 7. 推荐实施顺序

```
1. H — P2 域分解（最高优先级，单项收益最大 ~150-200μs/step）
   └─ 拆出 path_ray_io SoA 数组，collect/distribute 读写步幅 2040B → 228B
   └─ 这是根本性改善，其他方案在此之后才有意义

2. E — 消除 s3d_ray_request 中间层（P2 之后，趁重构接口时一起做）
   └─ collect 直接写 pinned Ray[] + FilterPerRayData[]
   └─ 与 P2 的 path_ray_io 拆分协同

3. G — GPU geom→shape 预解析（独立可做）
   └─ 构建 device-side uint32_t geom_to_shape[max_geom_id] lookup table
   └─ kernel 输出附带 shape_id，D2H 后 distribute 直接使用

4. F — 消除 s3d_hit 中间层（依赖 G + P2 完成后接口稳定时）
   └─ distribute 直接读 HitResult，内联 normal fixup + UV 计算
   └─ 最复杂的改动，P2+G 完成后自然水到渠成
```

## 8. 验证要求

- **性能验证**：每步均摊 timing 对比（现有 `pool->timing_*` 计时设施）
- **正确性验证**：逐像素 GPU/CPU 结果对比（tolerance 1e-6），porous 320×320×32 场景
- **回归测试**：`ctest -C Release --output-on-failure`

## 9. 结论

| 决策项 | 结论 |
|--------|------|
| AoS↔SoA 转换直接迁移 GPU | **否**——CPU 端转换计算量微不足道，瓶颈在 `path_state` 2040B AoS 步幅导致的 cache miss，GPU 迁移无法改善且引入更多 overhead |
| 根本方案 | **P2 域分解**——通过降低 CPU 数据步幅解决根因，比改变转换执行位置有效 30-40× |
| 辅助方案 E/F/G | **消除中间格式层**——减少不必要的内存拷贝和格式转换环节，P2 完成后实施 |

---

*文档创建: 2026-03-04 | 分析范围: persistent wavefront solver 的 H2D/D2H AoS↔SoA 转换*
