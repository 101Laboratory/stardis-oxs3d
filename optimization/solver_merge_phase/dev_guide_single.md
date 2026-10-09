# O11 Merge-Phase 开发指南 (单池简化版)

**代号**: O11-MergePhase-Single  
**基于**: dev_guide.md v1.1 (完整版, 保留不删)  
**简化原则**: 完整版引入了 gpu_dispatch_all 统一异步发射 + 双池 pipeline + enc/cp 延迟一轮等设计,
调试难度过高且耦合严重。本文档剥离这些内容, 仅描述 **单池同步管线**, 对应已在 worktree 上实现的代码。  

---

## 与完整版的区别

| 项目 | 完整版 (dev_guide.md) | 本文档 |
|------|----------------------|--------|
| pool_run | pool_run_single + pool_run_dual | **仅 pool_run_single** |
| enc/cp GPU dispatch | gpu_dispatch_all (异步统一发射) | **post_merged_enc_cp (同步)** |
| enc/cp 延迟 | 全部延迟一轮 (与 RT 对称) | **当轮同步执行, 不延迟** |
| RT dispatch | 纳入 gpu_dispatch_all | **gpu_launch_async (独立, 与现有相同)** |
| distribute | "纯写者" 模型 | **distribute+step 一体** (`merged_pass_distribute_step`) |
| cascade | "吸收 step" 模型 | **标准 cascade** (`cascade_advance_single_path`) |
| gpu_dispatch_all | 新 API: 排队 RT+CP+enc→compute_stream | **不实现** |
| gpu_wait_d2h_all | 新 API: 等待全部 + 后处理 | **不实现** |
| 双池 ping-pong | pool_run_dual 交替 A/B | **不实现** |
| 附录 C 安全性验证 | 针对 enc/cp 延迟一轮的因果序验证 | **enc/cp 同步, 不存在此风险** |
| pinned buffer for enc/cp | 新增 cp_pinned, enc_pinned 等 | **不需要** |

**核心差异**: enc/cp 不延迟。merged_pass 的 Phase C 收集 enc/cp 请求,
`post_merged_enc_cp()` 当轮同步执行完毕后才 gpu_launch RT。
因此 cascade 永远不会遇到 "RESULT phase 但数据未写入" 的问题。

---

## 1. 前置条件

### 1.1 Worktree

```bash
cd stardis-cus3d
git worktree add ../stardis-cus3d-merge-phase opt/merge-phase
cd ../stardis-cus3d-merge-phase
```

### 1.2 基线

```bash
cd Stardis-Starter-Pack/porous
<stardis-exe-main> -M porous.txt -t 32 -V 3 \
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > baseline.ht
```

记录: wall, cascade, distribute, collect, compact, harvest+refill, total_rays, cascade_iterations, failed_paths。

### 1.3 构建

```bash
cd stardis-cus3d-merge-phase/build
cmake --build . --config Release --target sdis 2>&1 | Select-String "error C" | Select-Object -First 10
```

---

## 2. 目标架构 (单池同步管线)

### 2.1 当前 main 的循环结构 (6 次 path_state 扫描)

```
cpu_pre_gpu():           compact + collect                Scan #1,2
gpu_launch_async()
gpu_wait_download()
gpu_postprocess():       distribute(step推进+写入) + enc/cp(同步)  Scan #3 + 2×hot_arr
cpu_between():           cascade + compact + harvest       Scan #4,5,6
```

### 2.2 O11 单池目标 (1 次 path_state 扫描 + 同步 enc/cp)

```
merged_pass():           distribute+step + cascade + collect(3类) + harvest   Scan #1 (40MB)
compact_active_paths():  hot_arr 扫描 + active_indices 重建                   (160KB)
refill_pool():           串行 pre-alloc + OMP init                            (<100 slots)
post_merged_enc_cp():    同步 enc_locate + cp batch (GPU kernel + 结果分发)    (~2-4ms/轮)
gpu_launch_async():      异步 RT trace (仅 ray batch)                         
gpu_wait_download():     等待 RT kernel + D2H                                 
```

**关键**: path_state (40MB) 只扫描 **一次**, 在 merged_pass 内完成分发/推进/收集/收割。
compact 只扫描 hot_arr (160KB, L2 内)。enc/cp 在 merged_pass 之后同步执行, 与 main 完全相同。

### 2.3 pipeline 时序对比

```
main:     [compact+collect]──[GPU]──[distribute+enc/cp]──[cascade+compact+harvest]
                40MB×2         ~隐藏          40MB+hot           40MB×3

O11 单池: [merged_pass]──[compact]──[refill]──[enc/cp]──[GPU launch]──[GPU wait]
              40MB×1        160KB    ~<100      同步        异步          同步
```

### 2.4 保留不变的函数

| 函数 | 说明 |
|------|------|
| `gpu_launch_async()` | 读 pinned buffer, 发射 RT kernel, 不变 |
| `gpu_wait_download()` | 等 RT D2H, 不变 |
| `compact_active_paths()` | 重建 active_indices, 不变 |
| `refill_pool()` | 分配新任务, 不变 |
| `pool_distribute_enc_locate_results()` | 写 enc_arr + phase→RESULT, 不变 |
| `pool_distribute_cp_results()` | 写 cnd_wos.cached_hit + phase→RESULT, 不变 |
| `cascade_advance_single_path()` | 标准 cascade loop, **不修改** |

---

## 3. 已实现的代码结构

> 以下描述 worktree `stardis-cus3d-merge-phase` 中已实现的代码。

### 3.1 pool_run_single() (L4632)

```c
while(pool->active_count > 0 || pool->task_next < pool->task_count) {
  pool->total_steps++;

  merged_pass(pool, pv, scn);             /* Step 1: distribute+cascade+collect+harvest */
  compact_active_paths(pool, pv);          /* Step 2: 160KB hot_arr 扫描 */
  refill_pool(pool, pv, &refill_count);   /* Step 3: 串行 pre-alloc + OMP init */
  post_merged_enc_cp(pool, pv, scn);      /* Step 4: 同步 enc/cp batch */
  gpu_launch_async(pool, pv, sv);          /* Step 5: 异步 RT trace */
  gpu_wait_download(pool, pv, sv);         /* Step 6: 等待 RT + D2H */

  pool_update_active_count(pool);
}
```

### 3.2 merged_pass() 内部 per-path 逻辑 (L4041)

每个 active slot 的处理顺序:

```
Phase A: merged_pass_distribute_step()  ← 分发上轮 ray_hits + 调用 step 推进
Phase B: cascade_advance_single_path()  ← no-ray 循环直到产生新请求
Phase C: collect ray / enc / cp 请求    ← 写入 pv->ray_requests / enc / cp 缓冲
Phase D: harvest (如果 PATH_DONE)       ← 累积结果 + 标记为 HARVESTED
```

### 3.3 post_merged_enc_cp() (L4534)

```c
if(pv->enc_locate_count > 0):
    s3d_scene_view_find_enclosure_batch_ctx()  → pool_distribute_enc_locate_results()
if(pv->cp_count > 0):
    s3d_scene_view_closest_point_batch_ctx()   → pool_distribute_cp_results()
```

enc/cp 结果在本轮同步写入 `enc_arr[slot]` 和 `cnd_wos.cached_hit`,
phase 转为 `*_RESULT`。下一轮 merged_pass 的 cascade 消费这些 RESULT。

### 3.4 数据流时序

```
Round N:
  merged_pass:
    Phase A: 分发 Round N-1 的 ray_hits, 调用 step_radiative_trace 等推进
    Phase B: cascade 消费 Round N-1 enc/cp 的 RESULT (post_merged_enc_cp 已写入)
             → 可能产生新的 enc/cp PENDING 或 needs_ray
    Phase C: 收集 ray + enc + cp 请求
    Phase D: harvest 完成的 path

  post_merged_enc_cp:   ← 同步执行 Round N 的 enc/cp 请求
    enc_locate batch → distribute → phase = *_RESULT (供 Round N+1 cascade 消费)
    cp batch → distribute → phase = *_RESULT

  gpu_launch_async:     ← 异步发射 Round N 的 RT trace

Round N+1:
  gpu_wait_download:    ← 等待 Round N 的 RT 完成, D2H
  merged_pass:
    Phase A: 分发 Round N 的 ray_hits
    Phase B: cascade 消费 Round N 的 enc/cp RESULT
    ...
```

**关键保证**: Phase B cascade 遇到 `*_RESULT` phase 时, 对应数据 **已在上轮
post_merged_enc_cp 中同步写入**。不存在 "RESULT phase 但数据未写入" 的问题。

---

## 4. 当前状态与待修复问题

### 4.1 已完成

- [x] `merged_pass()` 框架 (serial + OMP parallel)
- [x] `merged_pass_distribute_step()` — Phase A
- [x] `cascade_advance_single_path()` — Phase B (复用, 未修改)
- [x] Phase C: ray/enc/cp collect
- [x] Phase D: harvest
- [x] `post_merged_enc_cp()` — 同步 enc/cp
- [x] `pool_run_single()` — 单池循环重组
- [x] Debug 断言: `assert_result_phases_backed`, `assert_no_pending_result_before_dispatch`
- [x] `pool_run_dual()` 框架 (暂不调试, 后续再处理)

### 4.2 待验证

编译后运行验证:

```bash
# 最小场景 (快速排查 crash)
<stardis-exe> -M porous.txt -t 1 -V 3 \
  -R spp=1:img=16x16:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > test_min.ht

# 中等场景 (对比正确性)
<stardis-exe> -M porous.txt -t 4 -V 3 \
  -R spp=4:img=64x64:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > test_mid.ht

# 完整场景 (性能测量)
<stardis-exe> -M porous.txt -t 32 -V 3 \
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > test_full.ht
```

### 4.3 验证标准

enc/cp 当轮同步执行, 不延迟, 因此结果应 **bit-exact** (与 main 一致):

```bash
# 应完全相同
fc /b baseline.ht merged.ht

# 统计指标必须一致
findstr "total_rays completed_paths failed_paths" baseline.log merged.log
```

---

## 5. 已知陷阱

### 5.1 enc_query 6-ray 和 bnd_ss 4-ray 预传递

`merged_pass_distribute_step()` 必须在调用 `advance_one_step_with_ray()` 之前
完成 multi-ray hit 的预拷贝 (enc_query 的 6 条, bnd_ss 的 4 条)。
当前实现在 distribute step 的 `RAY_BUCKET_OTHER` 分支中处理。

### 5.2 OMP schedule

merged_pass 使用 `schedule(dynamic, 64)`, 因为 cascade 的 for(;;) 工作量不均匀。

### 5.3 TL ray buffer → pinned memory flush

OMP 线程使用栈上 `tl_rays[TL_RAY_BUF_MAX]` 收集 ray requests,
满后 flush 到 pinned buffer (`merged_pass_flush_tl_rays`)。
flush 使用 `omp atomic` 获取全局偏移, 避免竞争。

### 5.4 refill 的 advance_path_to_first_ray

新 path 初始化后调用 `advance_path_to_first_ray()`, 推进状态机到第一条 ray。
在 refill 的 OMP 阶段执行, 与 merged_pass 无冲突 (操作不同 slot)。

### 5.5 batch_idx 修正

merged_pass 中 ray requests 的 batch_idx 按线程局部偏移写入,
`merged_pass_fixup_batch_idx()` 在 flush 后修正为全局偏移。

---

## 6. 性能预期

### 6.1 CPU 侧改善

| 组件 | main (秒) | O11 单池 (预期) | 说明 |
|------|----------|----------------|------|
| distribute | 36.2 | 0 (合并) | 不再独立扫描 |
| cascade | 36.9 | 0 (合并) | 不再独立扫描 |
| collect | ~3 | 0 (合并) | 不再独立扫描 |
| compact | ~2×3 | ~2 (1次) | hot_arr only |
| harvest | ~2 | 0 (合并) | 不再独立扫描 |
| **merged_pass** | — | ~42 | 单次 40MB 扫描 |
| post_merged_enc_cp | ~3 | ~3 | 不变 |
| gpu_launch | 27.5 | 27.5 | 不变 |
| **总 CPU** | ~108 | ~75 | -30% |

### 6.2 GPU 侧

不变。RT kernel 仍通过 `gpu_launch_async()` 异步发射。
enc/cp 仍同步。GPU throughput 远超需求 (瓶颈在 CPU)。

### 6.3 后续优化空间 (不在本文档范围)

- **双池 pipeline**: pool_run_dual, GPU 与 CPU 重叠
- **gpu_dispatch_all**: enc/cp 异步化, 与 RT 统一发射
- **enc/cp 延迟一轮**: 消除同步等待 (需附录 C 安全性验证)
- **compact_indices 轻量化**: 仅扫描 hot_arr, 不重建 bucket

---

## 7. 从单池过渡到双池 (未来)

双池管线 (pool_run_dual) 的核心变更:
1. 两个 pool_view A/B 交替处理
2. GPU(A) 异步发射后, CPU 处理 B, 实现 GPU-CPU 重叠
3. enc/cp 仍可同步 (post_merged_enc_cp), 或延迟一轮 (需完整版安全验证)

当前 pool_run_dual 代码已存在但未调试。
**仅在单池验证通过后** 再启动双池调试。

---

*文档版本: v1.0 | O11-MergePhase 单池简化版 | 基于 dev_guide.md v1.1*
