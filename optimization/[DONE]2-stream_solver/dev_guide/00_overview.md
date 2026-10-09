# 双缓冲流水线开发指南 — 总览

**创建时间**: 2026-02-24  
**配套分析**: `../analysis.md`  
**目标代码**: `stardis-cus3d/stardis-solver/0.16.2` + `stardis-cus3d/oxstar-3d/0.10`

---

## 文档索引

| 文件 | 阶段 | 内容 | 预计工作量 |
|------|------|------|-----------|
| [01_P0_async_api.md](01_P0_async_api.md) | P0 | GPU 后端异步 API | 2 天 |
| [02_P1_eliminate_shared_state.md](02_P1_eliminate_shared_state.md) | P1 | 消除共享状态竞争 | 1 天 |
| [03_P2_half_pool_structure.md](03_P2_half_pool_structure.md) | P2 | pool_view 统一池视图结构 | 1 天 |
| [04_P3_pool_functions.md](04_P3_pool_functions.md) | P3 | 11 个 pool 函数统一化 | 3 天 |
| [05_P4_pipeline_main_loop.md](05_P4_pipeline_main_loop.md) | P4 | 流水线主循环重构 | 2 天 |
| [06_P5_validation.md](06_P5_validation.md) | P5 | 验证与回归测试 | 2 天 |

**总计: ~11 天**

---

## 架构总览

### 当前架构（串行）

```
每步 3.19ms:
  CPU ──[compact]──[collect]──────────────[wait GPU]──[distribute]──[cascade]──[harvest]──
  GPU                         ──[trace]──
                              1.54ms
```

### 目标架构（双缓冲流水线）

```
每半步 1.66ms:
  CPU ──[post(A)]──[between(A)+pre(A)]──────[post(B)]──[between(B)+pre(B)]──
  GPU  ═══════[trace(B)]═══════════════  ═══════[trace(A)]═══════════════
```

### 分层改造

```
Layer 1: GPU 后端异步化         (P0+P1, oxstar-3d,   ~100 行)
         ├─ batch_trace_context 扩展 (stream, params, retrace bufs)
         ├─ _async / _wait 拆分 API
         └─ cudaDeviceSynchronize → cudaStreamSynchronize

Layer 2: Pool 统一视图化         (P2+P3, solver,      ~300 行)
         ├─ pool_view 统一池视图 + init/destroy + merge/split
         └─ 11 个 pool 函数统一接受 (pool, pv) 签名

Layer 3: 主循环流水线重构       (P4,    solver,      ~200 行)
         ├─ cpu_pre_gpu / gpu_launch / gpu_wait_and_postprocess / cpu_between
         ├─ 双调度主循环 + drain + 动态 merge/split
         └─ 单池/双池共用同一套函数，仅调度逻辑不同
```

---

## 关键源文件清单

| 文件 | 说明 | 改动类型 |
|------|------|---------|
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_internal.h` | batch_trace_context 结构 (L267-278) | 扩展 |
| `oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp` | batch_trace_impl (L1185-1430) | 拆分 async/wait |
| `oxstar-3d/0.10/s3d_wrapper/s3d.h` | 公共 API 声明 (L391-406) | 新增 |
| `oxstar-3d/0.10/src/unified_tracer.h` | m_batch_params_ptr (L363) | 参考 |
| `oxstar-3d/0.10/src/unified_tracer.cpp` | traceBatchMultiHit (L1255-1282) | 参考 |
| `oxstar-3d/0.10/src/buffer_manager.h` | CudaBuffer 模板 (全文) | 不改 |
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.h` | wavefront_pool (L78-237) | 重构 (移除重复字段, 加 views[]+num_active_views) |
| `stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c` | 主循环+pool函数 (2021行) | 大改 |

---

## 依赖图

```
P0: async API ──────┐
                     ├──→ P4: pipeline main loop ──→ P5: validation
P1: shared state ───┘         ↑
                              │
P2: pool_view struct ──→ P3: pool functions ─┘
```

P0+P1 可独立开发和测试（单池+异步 launch+cascade 重叠即有收益）。  
P2+P3 可独立开发和单元测试（pool_view 版函数不依赖异步 API）。  
P4 整合 P0-P3 产出。P5 端到端验证。

---

## 前置条件

- [x] retrace 缓冲区已从 `static` 迁移到 `sv` 成员 (ox_s3d_internal.h L219-220)
- [ ] 当前代码可编译通过 (`cmake --build . --config Release`)
- [ ] porous 场景单缓冲基准数据已保存 (用于 P5 对比)

## 环境变量约定

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `STARDIS_PIPELINE` | `1` | `0` = 单池模式 (views[0] 全范围视图)，`1` = 双池流水线 |
| `STARDIS_CASCADE_OMP` | `1` | `0` = cascade 禁用 OMP |
| `STARDIS_PIPELINE_LOG` | `0` | `1` = 输出 [A]/[B] 视图调度日志 |
