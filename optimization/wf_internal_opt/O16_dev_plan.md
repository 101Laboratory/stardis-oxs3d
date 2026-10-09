# O16: 访存瓶颈优化 — 开发计划

**创建日期**: 2026-03-19  
**基于**: O16_memory_access_bottleneck_analysis.md  
**目标分支**: `stardis-oxs3d-merge-phase`  
**状态**: 📋 待实施  

---

## 热点 1：L3541 — `merged_pass_flush_tl_rays` pinned buffer 写入

### 问题

每条 ray 写 4 个 SoA 数组（`ray_pinned` 32B, `filter_pinned` 16B, `ray_to_slot` 4B, `ray_slot_sub` 4B）。`ray_pinned`/`filter_pinned` 采用 `cudaHostAllocDefault`（WB 可缓存），写入触发 write-allocate fetch；写完后 CPU 永远不再读取，但数据留在 L3 挤占其它热数据。P=8192 下唯一的 L3 miss 压力点。

### 方案 A（推荐）：Non-temporal store

对 `ray_pinned`（32B）和 `filter_pinned`（16B）使用 `_mm_stream_si128` NT 写入，绕过 cache 直接写入 write-combine buffer → DRAM。`ray_to_slot`/`ray_slot_sub` 保持普通写（被 `fixup_batch_idx` 顺序读取，留 cache 有益）。

**改动范围**: 仅 `merged_pass_flush_tl_rays()` 函数体

**实现步骤**:

1. 添加 SSE2 头文件和对齐断言
   ```c
   #include <emmintrin.h>   /* _mm_stream_si128, _mm_loadu_si128 */
   ```

2. 替换 `ray_pinned` 逐字段写为 NT 块写
   ```c
   /* ray_pinned: 32B = 2 × __m128i, NT 写入绕过 cache */
   {
     __m128i lo, hi;
     /* 从 tl_ray_entry（double → float 下转）pack 为连续 32B */
     float tmp[8];
     tmp[0] = (float)e->origin[0];
     tmp[1] = (float)e->origin[1];
     tmp[2] = (float)e->origin[2];
     tmp[3] = (float)e->tmin;
     tmp[4] = (float)e->direction[0];
     tmp[5] = (float)e->direction[1];
     tmp[6] = (float)e->direction[2];
     tmp[7] = (float)e->tmax;
     lo = _mm_loadu_si128((const __m128i*)&tmp[0]);
     hi = _mm_loadu_si128((const __m128i*)&tmp[4]);
     _mm_stream_si128((__m128i*)rp, lo);
     _mm_stream_si128((__m128i*)((char*)rp + 16), hi);
   }
   ```

3. 替换 `filter_pinned` 写为 NT 块写
   ```c
   /* filter_pinned: 16B = 1 × __m128i, NT 写入 */
   _mm_stream_si128((__m128i*)&pv->filter_pinned[ray_idx],
                    _mm_loadu_si128((const __m128i*)&e->filter));
   ```

4. 在 flush 完成后插入 `_mm_sfence()` 保证 NT store 全局可见
   ```c
   /* merged_pass_flush_tl_rays 调用点之后 */
   _mm_sfence();
   ```

**前提**: `ray_pinned` 和 `filter_pinned` 需 16B 对齐。`cudaHostAllocDefault` 返回地址通常 ≥ 256B 对齐，满足。添加编译期或运行时 assert 确认。

**预期收益**:
- 消除 ~2.25 MB write-allocate 读取流量
- 释放 L3 空间给 distribute 热数据
- P=8192 下 L3 Bound 从 10.1% 预期降至 ~5%

### 方案 B（备选）：`cudaHostAllocWriteCombined`

分配 pinned buffer 时改用 WC flag：
```c
cudaHostAlloc(&h_rays_pinned, max * sizeof(Ray), cudaHostAllocWriteCombined);
```

效果等同于方案 A 但无需修改写入代码。**风险**：任何 CPU 读取（含 debug 打印）会导致性能雪崩（WC 读延迟 ~500 ns/cacheline）。需审计所有访问路径确保只写不读。侵入性大于方案 A。

### 方案 C（附带优化）：缩减 `tl_ray_entry` 为 float

当前 `tl_ray_entry` 用 double 存 origin/direction/tmin/tmax（64B），源与目标都是 float。改 float 后每元素从 ~104B 降到 ~56B，改善线程本地缓冲区的 L1/L2 命中率。对 L3 写端压力帮助有限。

---

## 热点 2：L3291 + L3307 — `merged_pass_distribute_step` hit 分发

### 问题

Phase A distribute 按 slot 顺序遍历，但 `ray_hits[batch_idx]` 的 batch_idx 指向 Phase C 按 bucket 类型排列的 ray index → 全局随机读。叠加 path_state 2KB AoS 步幅读 batch_idx 的利用率极低，以及 enc 6-ray 路径的双重间接寻址。P=32768 下 DRAM bound 49.3% 的主要来源。

### 方案 A（推荐）：distribute 热字段提取 + 软件 prefetch

将 distribute 所需的最小字段从大 AoS 结构中提取到紧凑的辅助数组，消除步幅浪费。

**新增辅助数组**（在 `wavefront_pool` 中）:
```c
/* O16: distribute 热字段 — 紧凑 SoA，每 slot 12B */
uint32_t* dist_batch_idx;     /* [pool_size] — p->ray_req.batch_idx 镜像 */
uint32_t* dist_batch_idx2;    /* [pool_size] — p->ray_req.batch_idx2 镜像 */
uint32_t* dist_ray_count;     /* [pool_size] — p->ray_req.ray_count 镜像 (packed u8 即可) */
```

**写入时机**: `merged_pass_fixup_batch_idx()` 已按 ray 遍历并写回 `p->ray_req.batch_idx`，同步写入辅助数组。

**读取时机**: `merged_pass_distribute_step()` 入口处读辅助数组替代 `p->ray_req.batch_idx`：
```c
uint32_t bidx = pool->dist_batch_idx[slot];   /* 4B 步幅，连续读 */
if(bidx == (uint32_t)-1) return 0;
const struct s3d_hit* h0 = &pv->ray_hits[bidx];
```

**配合软件 prefetch**：在 OMP 循环中提前 N 个 slot prefetch ray_hits 地址：
```c
if(ph + 4 < (int)n) {
  uint32_t future_slot = pv->active_indices[ph + 4];
  uint32_t future_bidx = pool->dist_batch_idx[future_slot];
  if(future_bidx != (uint32_t)-1)
    PREFETCH_T0(&pv->ray_hits[future_bidx]);
}
```

**预期收益**:
- batch_idx 读取：步幅从 2048B 降至 4B → cache line 利用率 ×16
- ray_hits 随机读：prefetch 隐藏部分 DRAM 延迟（~20-30% 改善，非根治）
- 辅助数组内存开销：P×12B = 393KB (P=32768)，可忽略

### 方案 B：ray_hits 按 slot 重排

在 GPU trace 返回后、distribute 前，将 ray_hits 从 bucket 序重排为 slot 序：

```c
/* 利用已有的 ray_to_slot 映射，构建 slot→ray 紧凑映射 */
for(r = 0; r < total_rays; r++) {
  uint32_t slot = pv->ray_to_slot[r];
  reordered_hits[slot_hit_offset[slot]++] = pv->ray_hits[r];
}
```

distribute 遍历 slot 时读 `reordered_hits[slot]` → 顺序访问。

**预期收益**:
- ray_hits 读取从全局随机变为近似顺序 → 消除 capacity miss 根因
- 适用于所有 pool_size

**风险**:
- 重排本身的 scatter-read + gather-write 也有开销（一次性 O(total_rays)）
- 需要额外的 `reordered_hits[pool_size]` 缓冲区（P=32768: 1.75 MB）
- enc 6-ray 路径需要特殊处理：重排时保持 6 hit 连续

### 方案 C：enc 路径 gather 预取

仅针对 enc 6-ray 路径：在 distribute 前做一轮批量 gather，将分散的 ray_hits 收集到 `enc_arr[slot].dir_hits[]` 中（独立循环，可 prefetch）：

```c
/* 预 gather pass：专门处理 enc 6-ray 路径的 ray_hits */
#pragma omp parallel for schedule(static)
for(i = 0; i < enc_count; i++) {
  uint32_t slot = enc_slots[i];
  for(j = 0; j < 6; j++)
    pool->enc_arr[slot].dir_hits[j] = pv->ray_hits[pool->enc_arr[slot].batch_indices[j]];
}
```

将 6-ray 的双重间接寻址从 distribute 热路径中分离出来。

**预期收益**: 减少 distribute 中最恶劣的 12-miss enc 路径开销。不解决 1-2 ray 路径的随机读。

---

## 热点 3：L177 — `sdis_wf_steps_cnd.c` 导热路径 enc 检查

### 问题

`enc->resolved_enc_id` (enc_arr, 600B 步幅) vs `p->locals.cnd_ds.enc_id` (path_state, 2048B 步幅)。两次访问各加载一条 cache line，使用 4B，利用率 6.25%。导热路径高频调用。

### 方案 A（推荐）：enc_id 提取到 hot_arr 或独立紧凑数组

在 `path_hot`（当前 8B，有 3B padding）中嵌入 `enc_id` 字段：
```c
struct path_hot {
  uint8_t  phase;
  uint8_t  active;
  uint8_t  needs_ray;
  uint8_t  ray_bucket;
  uint8_t  ray_count_ext;
  uint8_t  _pad[1];
  uint16_t enc_id_cache;   /* O16: enc_id 低 16 位缓存 */
};  /* 仍为 8B */
```

或者新增独立数组（如果 enc_id 需要完整 32 位）：
```c
uint32_t* slot_enc_id;   /* [pool_size], O16: 导热路径 enc_id 缓存 */
```

**写入时机**: 在 enc_locate result 回写时同步更新。
**读取时机**: `sdis_wf_steps_cnd.c:L177` 改为读 `hot->enc_id_cache` 或 `pool->slot_enc_id[slot]`。

**预期收益**:
- 消除对 600B 步幅 enc_arr 的频繁访问
- 如果嵌入 hot_arr：与 phase/active 在同一 cache line，零额外内存

### 方案 B：cascade 入口 prefetch enc_arr

在 `cascade_advance_single_path` 入口处 prefetch enc_arr：
```c
PREFETCH_T0(&pool->enc_arr[slot]);
```

不改数据布局，仅隐藏延迟。对连续 slot 有效，对跳跃 slot 帮助有限。

---

## 热点 4：L2773/L2780 — `ox_s3d_scene_view.cpp` GPU hit 后处理

### 问题

`h_hits[]` (HitResult ~40B) → `hits[]` (s3d_hit ~56B) 转换循环。顺序访问局部性良好，但 P=32768 时 input+output ~18 MB 挤占 L3 预算。`resolve_shape`/`resolve_instance` 指针跳转引入额外 cache miss。

### 方案 A（推荐）：prefetch + 分块

在转换循环中加入 prefetch：
```c
for (int ii = 0; ii < (int)count; ii++) {
  if (ii + 8 < (int)count)
    PREFETCH_T0(&h_hits[ii + 8]);  /* 8 元素提前量 ≈ 320B */
  const HitResult& hr = h_hits[ii];
  ...
}
```

对于 `resolve_shape` 表查找，可 prefetch shape 指针表：
```c
if (ii + 4 < (int)count && h_hits[ii + 4].t >= 0.0f)
  PREFETCH_T0(&pp[h_hits[ii + 4].geom_id]);
```

**预期收益**: 中低。顺序访问已有硬件 prefetcher，手动 prefetch 主要帮助 resolve_shape 指针跳转。

### 方案 B：减小 s3d_hit 大小

`s3d_hit` 中的 `s3d_primitive` 内嵌 2 个 `void*`（`shape__`, `inst__`）= 16B 指针，可延迟到真正需要时再查找，不在 hit 结构中存储。需要修改 `s3d_hit` 定义，影响面较大，作为长期方案。

---

## 实施优先级

| 优先级 | 热点 | 方案 | 改动范围 | 预期收益 | 风险 |
|--------|------|------|---------|---------|------|
| **P0** | #1 L3541 | NT store | 1 个函数 | L3 Bound ↓50% | 极低 |
| **P1** | #2 L3291/L3307 | 热字段提取 + prefetch | fixup + distribute + pool 结构 | DRAM bound ↓15-25% | 低 |
| **P2** | #2 L3307 | enc pre-gather | distribute 前新增 pass | enc 路径 miss ↓80% | 低 |
| **P3** | #3 L177 | enc_id 提取到 hot | hot 结构 + cnd step | 导热 miss ↓50% | 低 |
| **P4** | #2 L3291 | ray_hits slot 重排 | fixup 后新增 pass | 随机读→顺序读 | 中（额外 buffer + enc 特例） |
| **P5** | #4 L2773 | prefetch | scene_view 循环 | 中低 | 极低 |
| **defer** | #1 附带 | tl_ray_entry float 缩减 | tl_ray_entry + collect | L1/L2 改善 | 低 |
| **defer** | #4 长期 | s3d_hit 瘦身 | s3d.h 全局 | L3 预算释放 | 高（全局影响） |

---

## 验证方法

1. **VTune Memory Access** — 对比 P=8192 和 P=32768 下各指标（L3 Bound, DRAM Bound, LLC Miss Count）
2. **端到端计时** — porous 320×320 spp=32，与 baseline 对比 elapsed time
3. **数值一致性** — 3σ 物理一致性测试不退化

---

*创建: 2026-03-19 | 状态: 📋 待实施*
