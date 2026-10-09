# Q&A: ENC 和 CP 查询的吞吐量测试覆盖情况

**日期**: 2026-03-20  
**上下文**: oxs3d 分支 OptiX 后端 (`stardis-oxs3d/oxstar-3d/0.10`)

---

## Q1: 吞吐量测试是否缺少对 ENC 和 CP 查询的性能潜力测试？

**结论**: CP 已覆盖，ENC 完全缺失。

### CP 查询 ✅ 已有

`stardis-oxs3d/oxstar-3d/0.10/src/main.cpp` 中包含：

| 函数 | 内容 |
|---|---|
| `benchmarkCPQuerySweep` | 固定场景，扫描查询数量 (1K → max) |
| `benchmarkCPTriSweep` | 固定查询数，扫描三角形数量 (1 → 1M) |

### ENC 查询 ❌ 缺失

`main.cpp` 中 16 个函数里无任何 ENC 相关 benchmark。`findEnclosureBatch` (E5) 在 `unified_tracer.cpp` 中已实现，但：

1. 没有 `Timed` 变体（对比 CP 有 `queryBatchTimed`）
2. 实现本身有多轮 D2H/H2D + CPU 中转，测出的不是 GPU 真实吞吐量

---

## Q2: ENC 为什么必须回传 CPU？是 solver 层提供了什么兜底？

### 为什么回传 CPU

**根因**：`prim_id → enc_id` 的映射表仅存在于 CPU。

GPU 层（`wait_d2h`）返回的结果里 `enc_id` 永远是 `S3D_INVALID_ID`：

```c
// ox_s3d_scene_view.cpp: batch_enc_wait_d2h_impl
results[i].enc_id = S3D_INVALID_ID;  /* Solver resolves via prim_props */
```

最终解析在 solver 层 `step_enc_locate_result`（`sdis_wf_steps_enc.c`）里做：

```c
scene_get_enclosure_ids(scn, (unsigned)enc->locate.prim_id, enc_ids);
enc_id = enc_ids[enc->locate.side];   // side: 0=front, 1=back
```

`scn->prim_props` 是 solver CPU-side 的 `darray`，从未上传至 GPU，因此这一步只能在 CPU 完成。

此外，CP 结果与 RT 结果的组合（`CPResult + HitResult → side`）也直接在 `wait_d2h` 里 CPU 完成，没有对应的 combine kernel。

**注意**：异步路径（`batch_enc_async_impl`）已经优化：CP kernel 和 RT kernel 并行发射，用 CUDA event 正确同步，问题仅在"最后一步"的 CPU 组合和 `prim_id → enc_id` 查找。

### Solver 层的兜底机制

`step_enc_locate_result` 有三层降级：

**1. degenerate 静默接受（最常见兜底）**
```c
// 点恰好在面上 (distance < 1e-8)
// 不调用 O(nprims) 同步遍历，直接返回 ENCLOSURE_ID_NULL
enc_id = ENCLOSURE_ID_NULL;
// 注释: "extremely rare, does not affect convergence"
```

**2. M1-v2 六射线路径（ENC 的备用策略）**

对同一查询点发射 6 条 π/4 旋转的轴对齐光线，第一条有效命中解析 `enc_id`。完整降级链：
```
M10 (CP+RT batch) 
  → M1-v2: 6-ray batch 
    → fallback: 1条随机方向射线 
      → 最终兜底: 同步 scene_get_enclosure_id()  [O(nprims), CPU遍历]
```

最后那个同步调用只有 6 条射线全部失败时才触发，极为罕见。

---

## 关联文件

| 文件 | 说明 |
|---|---|
| `stardis-oxs3d/oxstar-3d/0.10/src/main.cpp` | 吞吐量测试主程序（无 ENC benchmark） |
| `stardis-oxs3d/oxstar-3d/0.10/src/unified_tracer.cpp:1778` | `findEnclosureBatch` 实现（E5） |
| `stardis-oxs3d/oxstar-3d/0.10/s3d_wrapper/ox_s3d_scene_view.cpp:3686` | `batch_enc_wait_d2h_impl`：CPU combine 逻辑 |
| `stardis-oxs3d/stardis-solver/0.16.2/src/sdis_wf_steps_enc.c:81` | `step_enc_locate_result`：`prim_id+side → enc_id` |
| `stardis-oxs3d/stardis-solver/0.16.2/src/sdis_scene_c.h:338` | `scene_get_enclosure_ids`：CPU-side `prim_props` 查表 |
