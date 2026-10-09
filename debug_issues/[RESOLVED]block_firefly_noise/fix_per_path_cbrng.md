# Per-Path CBRNG 修复方案

**日期**: 2026-02-17  
**前置**: [investigation.md](investigation.md) — 根因分析  
**目标**: 消除 wavefront pool 中的 RNG 共享问题，实现 per-path 独立随机数流

---

## 1. 需求量分析

### 1.1 渲染规模参数

| 参数 | 典型值 | 最大值 |
|------|--------|--------|
| 图像尺寸 | 300 × 300 | 320 × 320 |
| SPP | 32 | 64 |
| 总路径数 | 2,880,000 | 6,553,600 |
| 每路径逻辑步数 | $10^4$ ~ $10^5$ | $10^6$ |
| 每步 RNG draws | 1 ~ 3（传导典型 3） | ~100（复杂边界路径） |
| **单路径最大 RNG draws** | $3 \times 10^5$ | $10^8$ |
| **全渲染最大 RNG draws** | $8.6 \times 10^{11}$ | $6.6 \times 10^{14}$ |

### 1.2 Threefry4x64 容量

| 维度 | 本方案使用量 | Threefry4x64 容量 | 裕度 |
|------|------------|-------------------|------|
| 独立流数（key 空间） | $\leq 6.6 \times 10^6$ 条路径 | $2^{256} \approx 10^{77}$ | $10^{70}$× |
| 单流长度（counter 空间） | $\leq 10^8$ draws | $4 \times 2^{256} \approx 10^{77}$ | $10^{69}$× |
| 实际使用 counter 范围 | $\leq 2.5 \times 10^7$ 次 `threefry4x64()` 调用 | $2^{128}$（仅用 v[0]+v[1]） | $10^{31}$× |

**结论**: Threefry4x64 的 key 和 counter 空间远超所有可能的渲染需求。

---

## 2. 数据结构设计

### 2.1 `struct wf_rng` — 内联 CBRNG 状态

```c
/* sdis_wf_rng.h */
#include <Random123/threefry.h>

struct wf_rng {
    threefry4x64_key_t key;   /* 32B: 由 (px, py, spp_idx, seed) 确定 */
    threefry4x64_ctr_t ctr;   /* 32B: 每 4 次 draw 递增一次 */
    uint64_t buf[4];           /* 32B: 当前 threefry4x64() 输出缓冲 */
    int buf_idx;               /* 4B:  缓冲消耗位置 0..3, >=4 表示需刷新 */
};                             /* 总计: 100 字节 */
```

### 2.2 内存开销

| 项 | 大小 |
|----|------|
| per path_state 增量 | +100B（新增 wf_rng）- 8B（移除 rng 指针）= +92B |
| path_state 新总量 | ~2.3 KB（原 ~2.2 KB，+4.2%） |
| pool 总增量 | 92B × 32768 = **3.0 MB** |
| 移除 slot_rngs 数组 | -32768 × 8B = -256 KB |
| **净增量** | **~2.7 MB** |

### 2.3 Key 编码方案

```c
key.v[0] = ((uint64_t)pixel_y << 32) | (uint64_t)pixel_x;  /* 像素坐标 */
key.v[1] = (uint64_t)spp_idx;                                /* 样本索引 */
key.v[2] = global_seed;                                       /* 用户种子 */
key.v[3] = 0;                                                 /* 保留扩展 */
```

| 字段 | bit 宽度 | 最大值 | 说明 |
|------|---------|--------|------|
| pixel_x | 32 | 4×10⁹ | 远超任何可能分辨率 |
| pixel_y | 32 | 4×10⁹ | |
| spp_idx | 64 | 1.8×10¹⁹ | 支持极高 SPP |
| global_seed | 64 | 用户控制 | 命令行 `-s` 参数 |

---

## 3. API 设计

### 3.1 核心操作（全部 `static INLINE`，零开销）

```c
/* ---- 初始化 ---- */
static INLINE void
wf_rng_seed(struct wf_rng* r,
            uint32_t px, uint32_t py, uint32_t spp_idx,
            uint64_t global_seed);

/* ---- 底层 64-bit 随机数 ---- */
static INLINE uint64_t
wf_rng_get(struct wf_rng* r);

/* ---- 分布采样 ---- */
static INLINE double  wf_rng_canonical(struct wf_rng* r);       /* [0, 1) double */
static INLINE float   wf_rng_canonical_float(struct wf_rng* r); /* [0, 1) float  */
static INLINE double  wf_rng_uniform_double(struct wf_rng* r, double lo, double hi);
static INLINE float   wf_rng_uniform_float(struct wf_rng* r, float lo, float hi);

/* ---- 状态管理 ---- */
static INLINE void    wf_rng_discard(struct wf_rng* r, uint64_t n); /* O(1) 跳跃 */
```

### 3.2 实现关键点

```c
static INLINE uint64_t wf_rng_get(struct wf_rng* r)
{
    if(r->buf_idx >= 4) {
        threefry4x64_ctr_t out = threefry4x64(r->ctr, r->key);
        r->buf[0] = out.v[0];
        r->buf[1] = out.v[1];
        r->buf[2] = out.v[2];
        r->buf[3] = out.v[3];
        r->buf_idx = 0;
        /* 128-bit increment: v[0]++, 溢出时 v[1]++ */
        if(++r->ctr.v[0] == 0) r->ctr.v[1]++;
    }
    return r->buf[r->buf_idx++];
}

static INLINE double wf_rng_canonical(struct wf_rng* r)
{
    /* 与 ssp_rng_canonical 等价: (uint64_t) / 2^64 → [0, 1) */
    return (double)wf_rng_get(r) * 0x1p-64;
}

static INLINE float wf_rng_canonical_float(struct wf_rng* r)
{
    /* 取高 24 bit 转 float，保证 [0, 1) 且均匀 */
    return (float)(wf_rng_get(r) >> 40) * 0x1p-24f;
}
```

---

## 4. 集成方案

### 4.1 方案 A（推荐）: Thin `ssp_rng` Wrapper — 零调用点修改

**原理**: 创建一个自定义的 `ssp_rng` 实例，其 `desc.get` 函数指针指向 `wf_rng_get`。所有现有的 `ssp_rng_canonical(p->rng)` 调用通过虚分发自动转到 `wf_rng_get`。

**优势**: 不修改任何 RNG 调用点（~30+ 处），风险最低  
**劣势**: 保留虚分发开销（每次 RNG 调用 1 次间接函数调用）

**实现步骤**:

#### Step A1: 在 `path_state` 中添加 wf_rng

```c
/* sdis_wf_state.h */
struct path_state {
    /* ... 现有字段 ... */
    struct ssp_rng* rng;           /* 保留，指向 per-slot thin wrapper */
    struct wf_rng   rng_state;     /* 新增: 100B 内联 CBRNG 状态 */
    /* ... */
};
```

#### Step A2: 创建 per-slot thin ssp_rng

```c
/* sdis_solve_persistent_wavefront.c — pool 初始化 */
static uint64_t wf_rng_adapter_get(void* state) {
    return wf_rng_get((struct wf_rng*)state);
}

static struct rng_desc wf_rng_desc = {
    .init = NULL,     /* 不需要 */
    .release = NULL,  /* 不需要 */
    .set = NULL,      /* 通过 wf_rng_seed 代替 */
    .get = wf_rng_adapter_get,
    .discard = NULL,  /* 可选实现 */
    .min = 0,
    .max = UINT64_MAX,
    .sizeof_state = sizeof(struct wf_rng),
    .alignof_state = 8
};
```

在 pool 初始化中为每个 slot 创建一个轻量 `ssp_rng`，其 `state` 直接指向 `pool->slots[i].rng_state`:

```c
for(i = 0; i < pool.pool_size; i++) {
    pool.slot_rngs[i] = create_thin_ssp_rng(&wf_rng_desc, &pool.slots[i].rng_state);
    /* create_thin_ssp_rng: 堆分配 ssp_rng header，state 指向外部 wf_rng */
}
```

#### Step A3: 在 init_single_path 中播种

```c
/* sdis_solve_persistent_wavefront.c — init_single_path */
wf_rng_seed(&p->rng_state,
            task->ipix_image[0],
            task->ipix_image[1],
            task->spp_idx,
            pool->global_seed);
/* p->rng 已在 pool 初始化时设好，指向包含 p->rng_state 的 thin ssp_rng */
```

#### Step A4: 移除 round-robin 分配

```c
/* 删除或 #ifdef 排除 */
// for(i = 0; i < pool.pool_size; i++) {
//     pool.slot_rngs[i] = per_thread_rng[i % nthreads];
//     SSP(rng_ref_get(pool.slot_rngs[i]));
// }
```

### 4.2 方案 B（可选后续优化）: 直接替换所有 RNG 调用

将所有 `ssp_rng_canonical(p->rng)` 替换为 `wf_rng_canonical(&p->rng_state)`。消除虚函数分发。

**影响范围** (~30 处):

| 文件 | 函数 | 调用数 |
|------|------|--------|
| `sdis_wf_steps.c` | delta-sphere 方向/time_rewind/BRDF/absorption/dispatch | ~20 |
| `sdis_brdf.c` | `brdf_sample` | 3 |
| `sdis_misc.c` | `time_rewind` → `ssp_ran_exp` | 1 |
| `sdis_solve_persistent_wavefront.c` | `init_single_path` (sample_time, sample_x, sample_y) | 3 |
| `ssp_ran.c` (间接) | `sphere_uniform`, `hemisphere_cos`, `exp`, `circle_uniform` | ~6 |

**star-sp 采样函数的适配**: 需要为 `ssp_ran_sphere_uniform_float` 等创建接受 `wf_rng*` 的版本，或将它们改为模板/宏。

---

## 5. 关键性质保证

### 5.1 确定性

给定相同的 `(pixel_x, pixel_y, spp_idx, global_seed)`:
- `wf_rng_seed` 产生相同的 key
- Counter 从 0 递增
- `threefry4x64(ctr, key)` 是纯函数
- → **完全相同的随机数序列** → 渲染结果确定可重现

### 5.2 独立性

不同像素/样本的 key 不同:
- `key.v[0]` = `(py<<32)|px` — 像素唯一
- `key.v[1]` = `spp_idx` — 样本唯一
- Threefry4x64 的 20 轮混合提供加密级别的 key-counter 独立性
- → **不同路径的随机序列统计独立**

### 5.3 与 CPU 的等价性（统计意义）

CPU depth-first 中每个 OMP 线程的 RNG 独占使用 → 每个像素的 RNG 序列是独立的。  
Per-path CBRNG → 每条路径的 RNG 序列是独立的。  
两者在**统计独立性**上等价。序列值不同（key/seed 编码不同），但蒙特卡洛估计的期望值和方差应一致。

### 5.4 不影响 Threefry 的统计质量

Threefry4x64-20 已通过 TestU01 BigCrush（所有 160 项测试）和 PractRand（1 TB）。Counter-based 设计**天然支持多流**——每个 key 是一个统计独立的流，不存在 proxy bucket 的"序列分区"问题。

---

## 6. 与其他 RNG 使用场景的兼容性

| 场景 | 当前 RNG 使用 | 影响 |
|------|-------------|------|
| Wavefront 路径（主场景） | `p->rng` → `pool.slot_rngs[i % nthreads]` | **替换为 per-path wf_rng** |
| Depth-first 探针测试 | `per_thread_rng[ithread]` | **不受影响** — 仍通过 ssp_rng_proxy |
| CPU depth-first 相机 | `per_thread_rng[omp_get_thread_num()]` | **不受影响** — 仅 CPU 代码路径 |
| Picard 子路径 | `p->rng`（同一路径的 RNG） | **自动受益** — wf_rng 在子路径中继续使用 |
| 对流采样 | `p->rng` | **自动受益** |

---

## 7. 预期 `ssp_rng_canonical` → `wf_rng_canonical` 数值等价性

| 属性 | `ssp_rng_canonical` (Threefry via Engine) | `wf_rng_canonical` (直接 Threefry) |
|------|----------------------------------------|-----------------------------------|
| 公式 | `(get() - 0) / 2^64` | `(double)get() * 2^-64` |
| 范围 | [0, 1) | [0, 1) |
| 精度 | 53 bit mantissa（`dbl_k=1`） | 53 bit mantissa |
| 底层算法 | Threefry4x64-20 | Threefry4x64-20 |

**数学等价**: `(double)(x - 0) / 18446744073709551616.0L` vs `(double)x * 5.421010862427522e-20`。两者通过 IEEE 754 double 精度运算结果应完全一致（`0x1p-64 = 1.0 / (1ULL << 64)`）。

---

*文档生成: 2026-02-17 | 状态: 待实施 → 验证*
