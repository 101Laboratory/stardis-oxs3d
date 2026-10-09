# Block-Pattern High-Temperature Noise Investigation

**日期**: 2026-02-17  
**状态**: 根因定位完成，修复方案已设计  
**严重度**: 高 — 影响所有 wavefront 渲染图像的温度分布正确性

---

## 1. 现象描述

### 1.1 cube_IR 场景（极简验证场景）

- **场景**: 单个方块，两侧面 T0=750K, T1=850K，仅几十个三角形
- **现象**: 高温噪点以**方块状伪影**分布，方块与图像像素网格对齐
- **方块大小**: 约 **32×32 像素**
- **分布**: 仅出现在直接命中实体的区域；未命中区域 GPU/CPU 基本一致
- **角度依赖**: 多角度渲染时方块边界不同，但始终清晰、主要出现在画面右下角

### 1.2 porous 场景（生产场景）

- **场景**: ~13000 三角形的多孔泡沫体，FOAM(λ=237, ρ=2700, cp=890)
- **直射多孔体**: 看不出明显的 tile 状噪声分布
  - **解释**: 三角形间物理温度差异远大于 RNG 关联噪声，淹没了空间模式
- **反射像命中多孔体**: 有明显的块状噪声
  - **解释**: 经过 LAT 边界镜面反射（specular_fraction=0.5）后，物理温度空间变化被平滑化，RNG 关联噪声得以浮现（详见 §4.3）

### 1.3 关键特征

| 特征 | 指向 |
|------|------|
| 方块状、图像对齐 | wavefront pool 的像素分组/调度机制 |
| 32×32 像素 ≈ pool_size/spp/16 | pool 初始 fill 的 tile 批次边界 |
| 随角度变化边界 | 天空/实体分布改变 → refill 模式改变 |
| 仅命中实体区域 | 实体路径消耗大量 RNG → 交织效应更强 |

---

## 2. 根因分析

### 2.1 RNG 共享机制（核心问题）

**代码位置**: [sdis_solve_persistent_wavefront.c L1269-L1275](../../stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c)

```c
/* ====== 2. Assign per-slot RNGs (round-robin from per_thread_rng) ====== */
for(i = 0; i < pool.pool_size; i++) {
    pool.slot_rngs[i] = per_thread_rng[i % nthreads];  /* 指针共享，非拷贝！ */
    SSP(rng_ref_get(pool.slot_rngs[i]));                /* 仅增加引用计数 */
}
```

**问题**: 仅 `nthreads`（通常 = 4）个 `ssp_rng` 实例通过 `i % nthreads` 被 `pool_size`（= 32768）个 slot **指针共享**。每个 RNG 实例被 `32768/4 = 8192` 条路径交替消耗。

**根本原因**: CPU 版本中 `nthreads` 既是 RNG bucket 数量也是同时执行的 OMP 线程数——每个 bucket 被一个线程**独占**使用。GPU wavefront 版本打破了 1:1 对应关系。

### 2.2 关键参数

| 参数 | 值 | 来源 |
|------|-----|------|
| `nthreads` | 4（典型 `-t 4`）| 命令行 |
| `pool_size` | 32768（RTX 4090: 128 SM × 8 warps × 32）| [L1195-L1199](../../stardis-cus3d/stardis-solver/0.16.2/src/sdis_solve_persistent_wavefront.c) |
| `TILE_SIZE` | 4 | [sdis_tile.h L27](../../stardis-cus3d/stardis-solver/0.16.2/src/sdis_tile.h) |
| 每 tile tasks | 16 pix × 32 spp = 512 | task queue 生成 |
| fill 覆盖 tiles | 32768 / 512 = 64 = 8×8 | 初始 fill |
| **fill 覆盖像素** | **8×4 = 32 × 32** | **= 观测到的块大小** |

### 2.3 32×32 块大小推导

$$\text{block\_size} = \text{TILE\_SIZE} \times \sqrt{\frac{\text{pool\_size}}{\text{TILE\_SIZE}^2 \times \text{spp}}} = 4 \times \sqrt{\frac{32768}{16 \times 32}} = 4 \times 8 = 32$$

改变 spp 应改变块大小：spp=16 → ~45 像素，spp=64 → ~23 像素。

### 2.4 RNG 实例创建路径

```
sdis.c L326: proxy_args.nbuckets = dev->nthreads  (= 4)
  → ssp_rng_proxy_create_rng(proxy, ibucket, &rngs[ibucket])  ×4
    → 创建 4 个 rng_bucket，每个包装了 proxy 主 RNG 的子序列
      → 底层: r123::Engine<Threefry4x64>，RNG_SEQUENCE_SIZE = 100000
```

每个 bucket RNG 是 proxy 主 RNG 的**序列分区视图**（非独立流），通过 `rng_bucket_get()` 从池中读取随机数，池耗尽时通过 mutex 获取新池。

---

## 3. 因果链详解

### 3.1 初始 Fill 的确定性 RNG 映射

**task queue** 按两层 Morton Z-curve 排序:
```
外层: Tile Morton 序 (ntiles_x × ntiles_y)
内层: Tile 内 Pixel Morton 序 (TILE_SIZE²)
最内层: SPP (0..spp-1)
```

**fill_pool** 按 task_queue 顺序将前 pool_size 个 task 装入 slot 0..pool_size-1:
```
Slot 0  → RNG[0]  → tile0_pix0_spp0    (消耗 2~3 RNG: time+sample_x+sample_y)
Slot 1  → RNG[1]  → tile0_pix0_spp1
Slot 2  → RNG[2]  → tile0_pix0_spp2
Slot 3  → RNG[3]  → tile0_pix0_spp3
Slot 4  → RNG[0]  → tile0_pix0_spp4    ← 与 slot 0 共享 RNG[0]
Slot 5  → RNG[1]  → tile0_pix0_spp5
...
Slot 511 → RNG[3] → tile0_pix15_spp31   (tile 0 结束)
Slot 512 → RNG[0] → tile1_pix0_spp0     ← 512 % 4 == 0，RNG 对齐锁步！
...
```

**关键**: `512 % 4 == 0`，所以**每个 tile 的 RNG 分配模式完全相同**。所有 tile 的 pixel→slot→RNG 映射是锁步的。

### 3.2 运行时 RNG 消耗的不对称性

每条路径在不同状态消耗不同数量的 RNG:

| 操作 | RNG draws |
|------|-----------|
| init (稳态) | 2 |
| step_radiative_trace: miss | 0 |
| step_radiative_trace: hit→absorb | 1 |
| step_radiative_trace: hit→specular | 2 |
| step_radiative_trace: hit→diffuse | 4 |
| **delta-sphere 每逻辑步** | **3** (2 方向 + 1 time_rewind) |
| delta-sphere 重试/步 | +2 |
| boundary prob dispatch | 1 |
| SF reinject (漫反射) | 2 |

**传导路径**：$N$ = 10,000 ~ 100,000 逻辑步 → 每路径消耗 **30,000 ~ 300,000** RNG draws。

8192 条路径从同一 RNG 实例交替消耗，每条路径的消耗量不同 → **slot A 的第 k 个随机数取决于与它共享 RNG 的其他 8191 条路径到第 k 步时各自消耗了多少** → 确定性但路径依赖的交织模式。

### 3.3 Refill 的非确定性放大

- `compact_active_paths()` 线性扫描 slot 0..pool_size-1，`done_indices` 按 slot 索引升序排列
- done_indices 的**组成**取决于哪些路径先完成——天空像素（0 步）远快于实体像素（10 万步）
- 天空像素的 slot 先释放 → refill 用后续 tile 的像素填充
- **跨 32×32 区域边界时**，RNG 已被前面路径的传导游走消耗了大量状态 → 新区域从完全不同的 RNG 偏移量开始 → **32×32 边界处 RNG 状态不连续** → 温度估计的统计特征不连续 → 可见的方块边界

---

## 4. 为什么不同区域表现不同

### 4.1 未命中区域（天空）

天空像素的路径几乎立即完成（radiative miss → 环境温度 → done），RNG 消耗极少（init 的 2~3 draws）。共享 RNG 的交织效应微弱，结果接近独立采样。**GPU ≈ CPU**。

### 4.2 直射实体区域（cube_IR / porous 直射）

实体路径消耗大量 RNG（传导游走 3N draws，N >> 1），**共享 RNG 的路径间产生空间关联**。同一 32×32 区域内的路径从 RNG 的相近位置消耗 → 它们的 BRDF 吸收/反射决策、delta-sphere 方向采样有关联 → 一个区域的温度估计整体偏向某类路径，相邻区域偏向另一类。

在 cube_IR（几何简单、物理温度变化平缓）中 → **32×32 块噪声清晰可见**。

在 porous 直射（物理温度三角形间变化大）中 → 物理信号 >> RNG 关联噪声 → **块模式被淹没**，仅体现为整体噪声较大。

### 4.3 反射像命中实体（porous 反射像）

反射像经过 LAT 表面（emissivity=0.9, specular_fraction=0.5）的一次反弹：
1. **镜面反射**（50% 概率）: 反射方向确定，不消耗方向 RNG → **RNG 交织模式保持对齐**
2. **物理温度变化被平滑**: 反射后同一 32×32 区域的像素观察到多孔体较大区域的"模糊映射"
3. **吸收/反射决策从共享 RNG 消耗**: 同区域路径的 `canonical < emissivity` 判断是关联的 → 要么一起吸收（得到局部温度），要么一起反射（继续弹跳 → 不同温度）
4. 物理信号平滑后 → **RNG 关联的块模式浮现**

---

## 5. 排除的其他嫌疑

| 嫌疑 | 排除原因 |
|------|---------|
| GPU hit 位置精度（`pos+dir*dist` vs `frag.P`） | CPU/GPU 辐射路径均使用 `pos+dir*dist`，结构等价 |
| trace_hit_fixup UV 约定 | Embree u/v 映射和 WBW u_out/v_out 经 fixup 后一致 |
| filter_data 自交过滤 | CPU/GPU 共享完全相同的 `hit_filter_function` 源码 |
| hit_side 判定 | 相同的 `dot(dir, N) < 0` 算法 |
| check_interface / wf_check_interface | 逻辑完全相同 |
| Enclosure edge-hit (firefly_analysis.md) | `hit_on_edge_uv` 修复后噪点未改善 |
| Retry 语义差异 (GPU用hit位置, CPU用起始位置) | 仅边角情况触发，无法解释全局 32×32 块模式 |
| Picard 系统性正偏 | 偏差均匀（~1.7K），无空间结构 |
| ENC 旋转矩阵不匹配 | 影响 M10 closest-primitive，不影响 M1 6-ray |

---

## 6. 与其他已知 Issue 的关系

| Issue | 关系 |
|-------|------|
| [firefly_analysis.md](../firefly_analysis.md) | Firefly 根因是 enc edge-hit，独立于 RNG 问题。但 RNG 关联可能放大 firefly 的空间聚集效应 |
| [wf_c3_picard_systematic_bias.md](../wf_c3_picard_systematic_bias.md) | Picard +1.7K 正偏可能部分来自 RNG 关联。修复 RNG 后应重新验证 |
| [irregular_reinjection_retry_failure/](../irregular_reinjection_retry_failure/) | M5_SF_ENC_RETRY 4501次失败是几何问题（薄壁角落），与 RNG 机制无关 |
| [enc_rot_matrix_mismatch/](../enc_rot_matrix_mismatch/) | 旋转矩阵不匹配影响 M10 内部，与 RNG 无关 |

---

## 7. 参考代码位置

| 组件 | 文件 | 行号 |
|------|------|------|
| RNG round-robin 分配 | `sdis_solve_persistent_wavefront.c` | L1269-L1275 |
| Task queue Morton 生成 | `sdis_solve_persistent_wavefront.c` | L170-L243 |
| fill_pool | `sdis_solve_persistent_wavefront.c` | L396-L430 |
| refill_pool | `sdis_solve_persistent_wavefront.c` | L1025-L1069 |
| compact_active_paths | `sdis_solve_persistent_wavefront.c` | L441-L487 |
| init_single_path (RNG 消耗) | `sdis_solve_persistent_wavefront.c` | L254-L355 |
| setup_delta_sphere_rays (RNG) | `sdis_wf_steps.c` | L173-L218 |
| step_cnd_ds_step_advance (RNG) | `sdis_wf_steps.c` | L783-L917 |
| step_radiative_trace (RNG) | `sdis_wf_steps.c` | L271-L432 |
| TILE_SIZE | `sdis_tile.h` | L27 |
| pool_size 计算 | `sdis_solve_persistent_wavefront.c` | L1176-L1200 |
| RNG proxy nbuckets | `sdis.c` | L326 |
| RNG_SEQUENCE_SIZE | `sdis.c` | L40 |
| ssp_rng_proxy bucket | `ssp_rng_proxy.c` | L488-L524 |
| path_state.rng (指针) | `sdis_wf_state.h` | L166 |

---

*文档生成: 2026-02-17 | 状态: 待修复验证*
