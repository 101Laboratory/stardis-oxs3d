# 验证实验方案

**日期**: 2026-02-17  
**前置**: [investigation.md](investigation.md), [fix_per_path_cbrng.md](fix_per_path_cbrng.md)

---

## 1. 零代码验证实验（优先执行）

### 1.1 SPP 变化 → 块大小变化

**假设**: 块大小 $= \text{TILE\_SIZE} \times \sqrt{\text{pool\_size} / (\text{TILE\_SIZE}^2 \times \text{spp})}$

| SPP | 预测块大小 (px) | 预测块 tile 数 |
|-----|----------------|---------------|
| 16 | $4 \times \sqrt{32768/(16 \times 16)} = 4 \times 11.3 \approx 45$ | 11×11 |
| 32 | $4 \times \sqrt{32768/(16 \times 32)} = 4 \times 8 = 32$ | 8×8 |
| 64 | $4 \times \sqrt{32768/(16 \times 64)} = 4 \times 5.7 \approx 23$ | 6×6 |

**实验**: 用 cube_IR 场景，分别用 spp=16, 32, 64 渲染。如果块大小按上表变化 → RNG 共享假设得到强化。

```bash
# spp=16
stardis -M cube_IR.txt -t 4 -V 3 -R spp=16:img=320x320:... > cube_IR_spp16.ht
# spp=32
stardis -M cube_IR.txt -t 4 -V 3 -R spp=32:img=320x320:... > cube_IR_spp32.ht
# spp=64
stardis -M cube_IR.txt -t 4 -V 3 -R spp=64:img=320x320:... > cube_IR_spp64.ht
```

### 1.2 nthreads 变化 → 块大小变化

**假设**: `nthreads` 改变不影响块大小（因为块大小只取决于 pool_size 和 spp），但可能改变块内噪声的统计特征。

如果有机制通过环境变量调整 pool_size（`STARDIS_POOL_SIZE`），则可验证：

| pool_size | 预测块大小 @ spp=32 |
|-----------|---------------------|
| 8192 | 16×16 |
| 32768 | 32×32 |
| 131072 | 64×64 |

```bash
# 小 pool → 小块
set STARDIS_POOL_SIZE=8192
stardis -M cube_IR.txt -t 4 -V 3 -R spp=32:img=320x320:... > cube_IR_pool8k.ht
# 大 pool → 大块
set STARDIS_POOL_SIZE=131072
stardis -M cube_IR.txt -t 4 -V 3 -R spp=32:img=320x320:... > cube_IR_pool131k.ht
```

---

## 2. 最小代码修改验证

### 2.1 快速验证: nbuckets = pool_size

**最小修改**（仅 2 处）:

1. [sdis.c L326](../../stardis-cus3d/stardis-solver/0.16.2/src/sdis.c):
```c
/* 将 proxy_args.nbuckets = dev->nthreads 改为: */
proxy_args.nbuckets = 32768;  /* 或从环境变量读取 pool_size */
```

2. [sdis.c L336](../../stardis-cus3d/stardis-solver/0.16.2/src/sdis.c):
```c
/* 增大 sequence_size 确保单 bucket 池大小合理 */
proxy_args.sequence_size = 32768 * 1000;  /* 32M per refill */
```

3. RNG 创建循环扩展到 pool_size 个:
```c
FOR_EACH(i, 0, 32768) {
    res = ssp_rng_proxy_create_rng(proxy, i, &rngs[i]);
}
```

4. round-robin 分配使用 1:1 映射:
```c
pool.slot_rngs[i] = per_thread_rng[i];  /* 每 slot 独立 bucket */
```

**⚠️ 问题**: proxy 的 `per_thread_rng` 数组需要从 `nthreads` 扩展到 `pool_size`。这需要修改 `sdis_create_rngs` 的签名和调用方。

**如果块噪点消失** → 确认 RNG 共享是根因，进入正式 per-path CBRNG 修复。  
**如果不消失** → RNG 共享不是唯一原因，需要启动 per-pixel 诊断层。

### 2.2 正式修复: Per-Path CBRNG

见 [fix_per_path_cbrng.md](fix_per_path_cbrng.md) 的完整实现方案。

---

## 3. 修复后回归测试

### 3.1 现有 E2E 测试

```bash
cd stardis-cus3d/build
cmake --build . --config Release
ctest -C Release -R "b4_e2e" --output-on-failure
```

预期: 12 个 e2e 测试全部通过（wavefront vs depth-first 逐像素 4σ 兼容性 ≥95%）。

### 3.2 Cube_IR 视觉验证

- 渲染 cube_IR 场景（GPU wavefront）
- 渲染 cube_IR 场景（CPU depth-first，作为参考）
- 逐像素差异图：`|T_gpu - T_cpu|` 应无 32×32 块结构
- 统计兼容性检查

### 3.3 Porous 验证

```bash
cd Stardis-Starter-Pack/porous
stardis_gpu -M porous.txt -t 4 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > IR_gpu.ht
stardis_cpu -M porous.txt -t 4 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > IR_cpu.ht
```

对比:
- 反射像区域的块状噪点是否消失
- 直射区域的整体噪声水平是否降低
- 全图温度分布的统计特征是否更接近 CPU

### 3.4 统计验证

用现有 `e2e_compare_images` 框架对比 GPU vs CPU 图像:
- 逐像素 $|T_{gpu} - T_{cpu}| \leq 4\sigma_{combined}$ 的通过率应 ≥ 95%
- Picard 系统性正偏（~1.7K）是否改善（如果部分来自 RNG 关联）

---

## 4. 单元测试清单

### 4.1 `test_wf_rng.c` — wf_rng 正确性

| 测试 | 验证内容 |
|------|---------|
| `test_wf_rng_deterministic` | 同 seed 两次 init → 相同序列 |
| `test_wf_rng_independence` | 不同 (px,py) → 不同序列（前 1000 个数无交集） |
| `test_wf_rng_canonical_range` | 10⁶ 次 canonical 全部 ∈ [0, 1) |
| `test_wf_rng_canonical_uniform` | Chi-square 均匀性检验 (100 bin, p>0.01) |
| `test_wf_rng_vs_ssp_threefry` | 同 key/counter → 与 ssp_rng(Threefry) 产出相同 `uint64_t` |
| `test_wf_rng_buffer_exhaustion` | 连续 ≥10⁶ 次 get() 无崩溃 |
| `test_wf_rng_counter_overflow` | 连续 ~2³² 次 get() 后 ctr.v[0] 回绕，ctr.v[1]++ |

### 4.2 `test_wf_rng_adapter.c` — ssp_rng wrapper 兼容性

| 测试 | 验证内容 |
|------|---------|
| `test_adapter_canonical` | `ssp_rng_canonical(thin_rng)` == `wf_rng_canonical(&wf_rng)` |
| `test_adapter_sphere_uniform` | `ssp_ran_sphere_uniform_float(thin_rng, ...)` 与直接调用等价 |
| `test_adapter_hemisphere_cos` | `ssp_ran_hemisphere_cos(thin_rng, ...)` 与直接调用等价 |
| `test_adapter_exp` | `ssp_ran_exp(thin_rng, mu)` 与直接调用等价 |

---

## 5. 实验记录模板

```
实验 #___
日期: ____-__-__
场景: □ cube_IR  □ porous  □ e2e_T__
参数变更: ________________
SPP: __  分辨率: ___×___  nthreads: __  pool_size: _____

结果:
  □ 块噪点消失
  □ 块噪点减弱（尺度变化: ___×___ → ___×___）
  □ 块噪点不变
  □ 新问题出现: ________________

GPU vs CPU 差异:
  通过率: ___% (4σ)
  最大差异: ___K @ pixel (___,___)
  平均差异: ___K

备注: ________________
```

---

*文档生成: 2026-02-17*
