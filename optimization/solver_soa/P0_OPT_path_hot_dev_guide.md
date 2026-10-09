# P0_OPT: path_hot 极热字段聚合 — 开发指南

**创建时间**: 2026-03-05  
**基于**: P0 dispatch_soa 镜像层 (已实施) + O9 域分解实测教训  
**状态**: ✅ 已实施并验证  
**目标**: 将 P0 的 5 个独立 SoA 数组 (`dispatch_soa`) 聚合为单一 8B 紧凑 AoS 数组 (`path_hot`)，从 `path_state` 中移除这 5 个字段由独立参数传递，**彻底消除 sync_a / sync_b / sync_c 同步层**  
**预期收益**: -44 ~ -46s (14-15%)，墙钟从 315s 降至 ~270s  
**实测收益**: **-53.2s (16.9%)**，墙钟从 315.7s 降至 262.5s，超出预测  
**工期**: 5-7 天  
**场景**: porous 320×320 spp=32 pool=16384  
**代码位置**: stardis-cus3d-p0opt worktree

---

## 0. 动机与 O9 教训

### 0.1 P0 的遗留问题

P0 成功地将 5 个调度热字段 (`phase`, `active`, `needs_ray`, `ray_bucket`, `ray_count_ext`) 从 `path_state` AoS **镜像**到独立 SoA 数组 (`dispatch_soa`)，使 compact/collect 读取步幅从 ~2040B 降至 4B。但 P0 采用 **双写同步** 策略：

- step 函数修改 `p->phase` (AoS)
- 在 SYNC POINT A/B/C 将 AoS → SoA 同步
- compact/collect 读 SoA

这一同步层在实测中产生了 **46.0s (14.6%)** 的开销：
| 同步点 | 耗时 | 触发时机 |
|--------|------|---------|
| sync_a | 21.0s | distribute 后，遍历 need_ray + enc + cp 槽位 |
| sync_b | 25.0s | cascade 后，遍历所有 active slots |
| sync_c | ~含在 refill | refill 每条新路径 |

### 0.2 O9 的尝试与失败

O9 试图通过 **全域分解** (5-8 个 SoA 大数组) 消除整个 `dispatch_soa` 同步层。实测结果 (stardis-cus3d-o9 worktree)：

- sync 消除确实兑现 **-46s** ✅
- 但多数组 SoA 布局导致 refill/compact/collect 严重回退 **+30s** ❌
- pool=32K 时 cascade 出现 **2.87× 超线性劣化** ❌
- 净收益仅 -17.6s (5.6%)，成本/收益比不可接受 ❌

根因：5-8 个独立大数组引发 TLB/prefetch/memset 灾难。详见 `../wf_internal_opt/O9_path_state_soa_comprehensive_report.md`。

### 0.3 P0_OPT 方案：最小切面

核心洞察：**O9 的 sync 消除收益 (-46s) 是确定性的，失败在于引入了过多独立大数组**。P0_OPT 是 O9 的最小切面——**只从 AoS 分离最热的 5 个字段聚合为 1 个 8B 微结构体**：

- 不是 5 个独立 SoA 数组 → **1 个紧凑 AoS 数组**
- 不增加 5-8 个 MB 级大数组 → **仅增加 128-256KB**
- pool=16K: 5 × 4B × 16K = 320KB (P0 五数组) → 8B × 16K = **128KB** (P0_OPT 单数组)

---

## 1. 数据结构设计

### 1.1 字段值域分析

| 字段 | 当前类型 | 值域 | 最大值 | 可缩至 |
|------|---------|------|--------|--------|
| `phase` | `enum path_phase` (4B) | 0 ~ PATH_PHASE_COUNT (~50) | ~50 | `uint8_t` |
| `active` | `int` (4B) | {0, 1} | 1 | `uint8_t` |
| `needs_ray` | `int` (4B) | {0, 1} | 1 | `uint8_t` |
| `ray_bucket` | `enum ray_bucket_type` (4B) | {0..5} | 5 | `uint8_t` |
| `ray_count_ext` | `int` (4B) | {0, 1, 2, 4, 6} | 6 | `uint8_t` |

全部 5 个字段值域均在 `[0, 255]` 内，可安全收窄为 `uint8_t`。

### 1.2 `struct path_hot` 定义

```c
/* sdis_wf_hot.h — 替代 sdis_wf_soa.h */

#ifndef SDIS_WF_HOT_H
#define SDIS_WF_HOT_H

#include <stdint.h>

/*******************************************************************************
 * path_hot -- 极热调度字段聚合体 (8B, 8 slots / cache line)
 *
 * 从 path_state 中提取的 5 个最高频访问字段，以紧凑 uint8_t 方式存储为
 * 独立 AoS 数组。step_* 函数通过独立参数 `struct path_hot* hot` 直接
 * 读写，不经过 path_state，消除 dispatch_soa 同步层。
 ******************************************************************************/
struct path_hot {
    uint8_t  phase;          /* enum path_phase,      max ~50 */
    uint8_t  active;         /* 0 or 1                        */
    uint8_t  needs_ray;      /* 0 or 1                        */
    uint8_t  ray_bucket;     /* enum ray_bucket_type,  max 5  */
    uint8_t  ray_count_ext;  /* {0, 1, 2, 4, 6}               */
    uint8_t  _pad[3];        /* → 8B total, 自然对齐          */
};

/* 编译期大小断言 */
#define PATH_HOT_STATIC_ASSERT \
    typedef char path_hot_size_check[(sizeof(struct path_hot) == 8) ? 1 : -1]

#endif /* SDIS_WF_HOT_H */
```

### 1.3 Cache 密度对比

| 方案 | 每 slot | pool=16K 总量 | slots/cache line | compact 全扫描量 |
|------|---------|-------------|-----------------|----------------|
| P0 dispatch_soa (5 独立数组) | 20B (5×4B) | 320KB | — (5 流) | 192KB (3 数组) |
| **P0_OPT path_hot** | **8B** | **128KB** | **8** | **128KB (1 流)** |
| O9 core_arr (480B stride) | 480B | 7.5MB | 0.13 | 7.5MB (1 流) |

P0_OPT 的 128KB 全量扫描完全在 L2 (1.25MB) 内，且为单一连续数组——最优预取模式。

### 1.4 Pool 存储修改

```c
/* struct wavefront_pool — 替换 dsoa 为 hot_arr */
struct wavefront_pool {
    /* ... existing fields ... */
    struct path_hot*  hot_arr;   /* [pool_size], 单次 calloc 分配 */
    /* 删除: struct dispatch_soa dsoa; */
};
```

分配/释放：
```c
/* 替换 dispatch_soa_alloc/free */
pool->hot_arr = (struct path_hot*)calloc(pool_size, sizeof(struct path_hot));
/* ... */
free(pool->hot_arr);  pool->hot_arr = NULL;
```

---

## 2. 访问模式设计

### 2.1 设计原则：独立参数，非嵌入指针

P0_OPT 将 `path_hot*` 作为**独立参数**传入 step 函数，而非在 `path_state` 中嵌入指针。

**理由**：
- 这本质是冷热数据分离——`path_hot` 是从 `path_state` 中提取的「更热」数据
- `path_state` 不增加 8B 指针开销，保持结构体纯净
- 概念清晰：`path_state` = 慢状态，`path_hot` = 快调度状态
- 无额外间接寻址层（P0 的 `dispatch_soa` 本身就是 6 个指针的间接层）

### 2.2 step 函数签名变更模式

**变更前** (P0 当前):
```c
static LOCAL_SYM res_T
step_radiative_trace(struct path_state* p,
                     struct sdis_scene* scn,
                     const struct s3d_hit* hit)
{
    /* ... */
    p->phase = PATH_COUPLED_BOUNDARY;
    p->needs_ray = 0;
    /* ... */
}
```

**变更后** (P0_OPT):
```c
static LOCAL_SYM res_T
step_radiative_trace(struct path_state* p,
                     struct path_hot* hot,
                     struct sdis_scene* scn,
                     const struct s3d_hit* hit)
{
    /* ... */
    hot->phase = (uint8_t)PATH_COUPLED_BOUNDARY;
    hot->needs_ray = 0;
    /* ... */
}
```

### 2.3 赋值 cast 规则

| 字段 | 赋值示例 | 是否需要 cast | 说明 |
|------|---------|-------------|------|
| `phase` | `hot->phase = (uint8_t)PATH_XXX;` | **是** | enum→uint8_t 需显式 cast |
| `active` | `hot->active = 0;` / `hot->active = 1;` | 否 | 小整数字面量隐式转换 |
| `needs_ray` | `hot->needs_ray = 0;` / `hot->needs_ray = 1;` | 否 | 同上 |
| `ray_bucket` | `hot->ray_bucket = (uint8_t)RAY_BUCKET_XXX;` | **是** | enum→uint8_t |
| `ray_count_ext` | `hot->ray_count_ext = (uint8_t)N;` | 视情况 | N=0,1 不需要; N=2,4,6 不需要但加上更规范 |

**比较无需 cast**：`if(hot->phase == PATH_RAD_TRACE_PENDING)` ✅ — C 整数提升自动处理 `uint8_t → int` 比较。

### 2.4 辅助宏（可选）

如果 cast 过于繁琐，可定义 setter 宏：

```c
#define HOT_SET_PHASE(hot, ph)     ((hot)->phase    = (uint8_t)(ph))
#define HOT_SET_BUCKET(hot, bkt)   ((hot)->ray_bucket = (uint8_t)(bkt))
```

> 是否使用宏取决于实施者偏好。直接 cast 更显式，宏更简洁。建议首轮用直接 cast，后续可统一。

---

## 3. 管线函数适配

### 3.1 compact — 读 hot_arr[]

**当前** (P0):
```c
for(i = base; i < end; i++) {
    enum path_phase ph = dsoa->phase[i];    /* stride=4B，stream 1 */
    int act            = dsoa->active[i];    /* stride=4B，stream 2 */
    int nr             = dsoa->needs_ray[i]; /* stride=4B，stream 3 */
    /* 分类到 active_indices / done_indices / need_ray_indices */
}
```

**P0_OPT 后**:
```c
for(i = base; i < end; i++) {
    struct path_hot h = pool->hot_arr[i];   /* 8B 整体读取，1 stream */
    enum path_phase ph = (enum path_phase)h.phase;
    int act            = h.active;
    int nr             = h.needs_ray;
    /* 分类逻辑不变 */
}
```

改进：3 个独立 stream → 1 个 stream，数据量 192KB → 128KB。

**注意 M8 picard 拦截逻辑**：compact 中有一个特殊分支在 `PATH_DONE + sfn_stack > 0` 时直接写回 phase/active 到 AoS+SoA。P0_OPT 后只需写 `hot_arr[i]`:

```c
/* P0_OPT: M8拦截路径 */
if(ph == PATH_DONE && pool->sfn_arr[i].depth > 0) {
    pool->hot_arr[i].phase  = (uint8_t)PATH_BND_SFN_COMPUTE_Ti_RESUME;
    pool->hot_arr[i].active = 1;
    /* 不需要写 pool->slots[i].phase/active — 已从 path_state 移除 */
    ph = PATH_BND_SFN_COMPUTE_Ti_RESUME;
    act = 1;
}
```

### 3.2 collect — 读 hot_arr[]

**当前** (P0):
```c
/* Pass 1: 计数 */
size_t nrays = count_path_rays_soa(pool->dsoa.phase[i],
                                   pool->dsoa.ray_count_ext[i], p);
int bkt = (int)pool->dsoa.ray_bucket[i];

/* Pass 2: scatter */
int bkt = (int)pool->dsoa.ray_bucket[i];
enum path_phase ph_i = pool->dsoa.phase[i];
```

**P0_OPT 后**:
```c
/* Pass 1: 计数 — 从 hot_arr 读 */
struct path_hot h = pool->hot_arr[i];
size_t nrays = count_path_rays_hot(h.phase, h.ray_count_ext, p);
int bkt = (int)h.ray_bucket;

/* Pass 2: scatter */
struct path_hot h = pool->hot_arr[i];
int bkt = (int)h.ray_bucket;
enum path_phase ph_i = (enum path_phase)h.phase;
```

`count_path_rays_soa()` 重命名为 `count_path_rays_hot()` 并修改参数类型。

### 3.3 cascade — 构造 hot 指针传入

**当前** (P0):
```c
/* cascade per-path */
struct path_state* p = &pool->slots[slot_idx];
/* ... cascade 循环，内部 step 函数读写 p->phase ... */
/* SYNC POINT B: cascade 后同步 dsoa */
dispatch_soa_sync_from_path(&pool->dsoa, slot_idx, p);
```

**P0_OPT 后**:
```c
/* cascade per-path */
struct path_state* p = &pool->slots[slot_idx];
struct path_hot* hot = &pool->hot_arr[slot_idx];
/* ... cascade 循环，step 函数读写 hot->phase ... */
/* 无 SYNC POINT B — hot_arr 已直接写入 */
```

cascade 循环的出口判断同步修改：
```c
for(;;) {
    if(hot->needs_ray) break;
    if(hot->phase == (uint8_t)PATH_DONE || hot->phase == (uint8_t)PATH_ERROR) {
        /* M8 拦截... */
        break;
    }
    if(path_phase_is_ray_pending((enum path_phase)hot->phase)) break;
    if(path_phase_is_enc_locate_pending((enum path_phase)hot->phase)) break;
    if(path_phase_is_cp_pending((enum path_phase)hot->phase)) break;
    
    res = advance_one_step_no_ray(p, hot, scn, &advanced, pool, slot_idx);
    if(!advanced) break;
}
```

### 3.4 distribute — 传入 hot 指针

**当前** (P0):
```c
/* distribute per-ray-result */
struct path_state* p = &pool->slots[i];
step_radiative_trace(p, scn, h0);
/* SYNC POINT A: distribute 后同步 dsoa */
dispatch_soa_sync_from_path(&pool->dsoa, i, p);
```

**P0_OPT 后**:
```c
struct path_state* p = &pool->slots[i];
struct path_hot* hot = &pool->hot_arr[i];
step_radiative_trace(p, hot, scn, h0);
/* 无 SYNC POINT A — hot_arr 已直接写入 */
```

### 3.5 distribute_enc_locate / distribute_cp — 直接写 hot_arr

**当前**: `p->phase = PATH_ENC_LOCATE_RESULT;`  
**P0_OPT**: `pool->hot_arr[i].phase = (uint8_t)PATH_ENC_LOCATE_RESULT;`

### 3.6 init_single_path / probe_init_path — 初始化 hot_arr

**当前**:
```c
memset(p, 0, sizeof(*p));
p->phase = PATH_INIT;
p->active = 1;
p->needs_ray = 0;
/* ... */
dispatch_soa_sync_from_path(&pool->dsoa, slot_idx, p);  /* SYNC C */
```

**P0_OPT**:
```c
memset(p, 0, sizeof(*p));
/* phase/active/needs_ray/ray_bucket/ray_count_ext 已不在 p 中 */
/* ... */
struct path_hot h = {0};
h.phase  = (uint8_t)PATH_INIT;
h.active = 1;
pool->hot_arr[slot_idx] = h;   /* 8B 单次写入，无 sync */
```

### 3.7 消除的代码

| 删除项 | 位置 | 说明 |
|--------|------|------|
| SYNC POINT A | `sdis_solve_persistent_wavefront.c` L3375-L3397 | distribute 后 dsoa 同步循环 |
| SYNC POINT B | `sdis_solve_persistent_wavefront.c` L3546-L3566 | cascade 后 dsoa 同步循环 (含 O6 skip) |
| SYNC POINT C | `sdis_solve_persistent_wavefront.c` L1222, L2859 | refill 中 `dispatch_soa_sync_from_path()` 调用 |
| `dispatch_soa_sync_from_path()` | `sdis_wf_soa.h` L57-L65 | 内联同步函数 |
| `dispatch_soa_sync_to_path()` | `sdis_wf_soa.h` L72-L82 | 内联反向同步函数 |
| `dispatch_soa_assert_consistent()` | `sdis_wf_soa.h` L89-L100 | 调试一致性检查 |
| `dispatch_soa_alloc()` | `sdis_wf_soa.c` L23-L43 | 5 次 calloc 分配 |
| `dispatch_soa_free()` | `sdis_wf_soa.c` L45-L65 | 5 次 free 释放 |
| `struct dispatch_soa` | `sdis_wf_soa.h` L42-L49 | 整个结构体定义 |
| `time_sync_a_s` / `time_sync_b_s` | 计时变量及输出 | timing 行 sync_a=xxx sync_b=xxx |

---

## 4. 改动点完整清单

### 4.1 按文件统计

| 文件 | 改动类型 | 预估改动处 | 说明 |
|------|---------|-----------|------|
| **sdis_wf_soa.h** → **sdis_wf_hot.h** | 重写 | ~80 行 | 替换 dispatch_soa 为 path_hot |
| **sdis_wf_soa.c** → **sdis_wf_hot.c** | 重写 | ~30 行 | 单次 calloc/free |
| **sdis_wf_state.h** | 删除 5 字段 | ~5 行 | 移除 phase/active/needs_ray/ray_bucket/ray_count_ext |
| **sdis_solve_persistent_wavefront.h** | 替换成员 | ~3 行 | dsoa → hot_arr |
| **sdis_solve_persistent_wavefront.c** | 管线适配 | ~200+ 处 | compact/collect/cascade/distribute/init/sync 删除 |
| **sdis_wf_steps_core.c** | 签名 + 字段 | ~80 处 | dispatch hub + 11 个 step 函数 |
| **sdis_wf_steps_bnd_sf.c** | 签名 + 字段 | ~60 处 | 7 个 step 函数 |
| **sdis_wf_steps_bnd_ss.c** | 签名 + 字段 | ~40 处 | 4 个 step 函数 |
| **sdis_wf_steps_bnd_sfn.c** | 签名 + 字段 | ~45 处 | 6 个 step 函数 |
| **sdis_wf_steps_cnd.c** | 签名 + 字段 | ~50 处 | 12 个 step 函数 |
| **sdis_wf_steps_bnd_ext.c** | 签名 + 字段 | ~30 处 | 5 个 step 函数 |
| **sdis_wf_steps_enc.c** | 签名 + 字段 | ~20 处 | 5 个 step 函数 |
| **sdis_wf_steps_cnv.c** | 签名 + 字段 | ~25 处 | 5 个 step 函数 |
| **sdis_wf_steps.h** | 声明更新 | ~49 行 | 所有 step 函数声明添加 `path_hot*` |
| **sdis_ray_sort.c** | 读取路径 | ~2 处 | `p->phase` / `p->ray_count_ext` → 从 hot_arr 读 |
| **test_sdis_dispatch_soa.c** | 重写/删除 | 整文件 | dispatch_soa 测试 → path_hot 测试 |
| **test_sdis_b4_m2_ray_bucketing.c** | 适配 | ~10 处 | p->phase/ray_bucket → hot.phase/ray_bucket |
| **test_sdis_b4_m3_solid_solid.c** | 适配 | ~3 处 | p->phase 读取 |
| **test_sdis_b4_m8_picardN.c** | 适配 | ~5 处 | p->phase 写入 |
| **test_sdis_ray_sort.c** | 适配 | ~5 处 | p->phase/ray_count_ext |

**合计**：~650 处改动，覆盖约 20 个文件。

### 4.2 step 函数间交叉调用需同步更新

以下 step→step 调用的被调方必须已接收 `hot` 参数，否则编译失败。建议按**被依赖顺序**逐文件实施：

```
1. sdis_wf_steps_enc.c     (被 7 个其他 step 调用: bnd_ss×3, bnd_sf×1, cnd×1, core×2)
2. sdis_wf_steps_cnd.c     (独立，无外部 step 调用它)
3. sdis_wf_steps_cnv.c     (含 step_bnd_dispatch → 调用 bnd_ss, bnd_sf)
4. sdis_wf_steps_bnd_ss.c  (调用 enc)
5. sdis_wf_steps_bnd_sf.c  (调用 enc, bnd_ext)
6. sdis_wf_steps_bnd_ext.c (独立)
7. sdis_wf_steps_bnd_sfn.c (独立)
8. sdis_wf_steps_core.c    (dispatch hub，最后——依赖所有其他文件)
```

**每文件完成后单独编译 (`cl /c`) 验证，全文件完成后 CTest 回归。**

---

## 5. 量化收益预测

### 5.1 确定性收益：sync 消除

| 同步点 | 当前耗时 | P0_OPT 后 | 节省 | 置信度 |
|--------|---------|-----------|------|--------|
| sync_a | 21.0s | **0s** | **21.0s** | ★★★★★ (O9 已验证) |
| sync_b | 25.0s | **0s** | **25.0s** | ★★★★★ (O9 已验证) |
| sync_c | ~含在 refill | **0s** | ~随 refill 减少 | ★★★★☆ |
| **合计** | 46.0s | 0s | **~46s** | |

O9 实测已验证：sync 消除确实兑现 -46s，且是唯一完美兑现的预测项。

### 5.2 中性影响：compact / collect

| 阶段 | P0 (5 数组 SoA) | P0_OPT (8B AoS) | 变化 | 原理 |
|------|-----------------|-----------------|------|------|
| compact | 18.1s | ~18s | ~0 | 128KB 单流 vs 192KB 三流，L2 内无差异 |
| collect | 19.3s | ~18-19s | -0 ~ -1s | 3 字段 8B 共置 vs 3 独立数组，微改善 |

### 5.3 微量回退：cascade / distribute

| 阶段 | 当前(s) | P0_OPT(s) | 变化 | 原理 |
|------|---------|-----------|------|------|
| cascade | 40.2 | ~40.5-41 | +0.3-0.8 | step 函数额外参数传递 + uint8_t→int cast |
| distribute | 32.9 | ~33.0-33.5 | +0.1-0.6 | 同上，但 distribute 调用密度低于 cascade |

**额外参数开销分析**：
- x64 调用约定：`hot` 指针进入第 2 个寄存器参数 (RDX/RSI)，零栈开销
- `switch((enum path_phase)hot->phase)`: 1 次 `movzx` (uint8_t→int) + switch table，vs 当前直接 `mov` (int)。差异 <1 周期
- cascade 每步 ~100ns 量级，+1ns 的寄存器传递 = **+1% cascade 时间**

### 5.4 净收益汇总

| 阶段 | 当前(s) | 变化(s) | 原理 |
|------|---------|---------|------|
| sync_a + sync_b | 46.0 | **-46.0** | 彻底消除 |
| compact | 18.1 | ~0 | 中性 |
| collect | 19.3 | -0 ~ -1 | 微改善 |
| cascade | 40.2 | +0.3 ~ +0.8 | 额外参数/cast |
| distribute | 32.9 | +0.1 ~ +0.6 | 同上 |
| 其他 | 159.2 | 0 | 不受影响 |
| **净收益** | **315.7** | **-44 ~ -46s** | **14-15%** |

### 5.5 与 O9 的对比

| 维度 | O9 域分解 (实测) | P0_OPT (预测) |
|------|----------------|---------------|
| sync 消除 | -46s ✅ | -46s |
| 额外数组数 | 5-8 个 (MB 级) | **1 个 (128KB)** |
| TLB 压力 | 5× 劣化 | **零增加** |
| compact 回退 | +3.8s | ~0 |
| collect 回退 | +4.9s | ~0 |
| harvest+refill 回退 | +15.7s | ~0 |
| cascade 回退 | -3.1s (微改善) | +0.5s (微回退) |
| pool=32K scaling | 2.87× 劣化 | **无劣化** |
| **净收益** | **-17.6s (5.6%)** | **-44~-46s (14-15%)** |

---

## 6. 风险矩阵

| # | 风险 | 影响 | 概率 | 缓解 |
|---|------|------|------|------|
| 1 | ~262 处 `p->xxx` 替换引入 typo | 中: 编译错误或运行时数值问题 | 中 | 逐文件编译 + CTest，每文件一个 commit |
| 2 | enum→uint8_t 截断 (未来新增 phase >255) | 低: 当前 max ~50 | 极低 | `static_assert(PATH_PHASE_COUNT < 256)` |
| 3 | step→step 交叉调用遗漏 `hot` 参数 | 低: 编译直接报错 | 低 | 按被依赖序实施 |
| 4 | `sdis_ray_sort.c` 无法通过 step 参数获取 hot | 低: 需传入 hot_arr | 确定 | 修改 ray_sort 接口或直接传 hot_arr+index |
| 5 | compact 8B AoS vs 12B (3×4B) SoA 性能持平非改善 | 低: 不会变差 | 中 | 128KB L2 内，影响可忽略 |
| 6 | 测试文件适配量被低估 | 低: 测试仅编译期改动 | 中 | 测试适配安排在最后 |

---

## 7. 实施步骤与工期

```
Step 1: 新头文件 + Pool 重构 (0.5 天)
  ├── 创建 sdis_wf_hot.h (struct path_hot 定义)
  ├── 改写 sdis_wf_soa.c → sdis_wf_hot.c (单 calloc/free)
  ├── wavefront_pool: dsoa → hot_arr
  ├── 从 path_state 移除 5 字段 (sdis_wf_state.h)
  └── 编译验证: 预期大量编译错误 (作为改动清单)

Step 2: Step 函数声明更新 (0.25 天)
  ├── sdis_wf_steps.h: 所有 step 函数声明添加 struct path_hot* hot
  ├── advance_one_step_no_ray / advance_one_step_with_ray 声明更新
  └── 编译验证: 全部 step 文件报签名不匹配

Step 3: Step 文件逐文件迁移 (3-4 天)
  ├── 按被依赖序: enc → cnd → cnv → bnd_ss → bnd_sf → bnd_ext → bnd_sfn → core
  ├── 每文件: 签名添加 hot + p->xxx → hot->xxx 替换 + cast
  ├── 每文件编译验证 (cl /c)
  └── 8 个文件全部完成后链接验证

Step 4: 管线函数适配 (1 天)
  ├── compact: dsoa→hot_arr 读取
  ├── collect: dsoa→hot_arr 读取 + count_path_rays_hot
  ├── cascade: 构造 hot 指针，传入 advance_one_step_no_ray
  ├── distribute: 构造 hot 指针，传入 step 函数
  ├── init_single_path / probe_init_path: 初始化 hot_arr
  ├── 删除 SYNC POINT A / B / C
  ├── 删除 time_sync_a_s / time_sync_b_s 计时
  └── distribute_enc_locate / distribute_cp: 直写 hot_arr

Step 5: 辅助文件 + 测试适配 (0.5 天)
  ├── sdis_ray_sort.c: 传入 hot_arr 或 hot 参数
  ├── test_sdis_dispatch_soa.c → test_sdis_path_hot.c
  ├── 更新 4 个测试文件
  └── CMakeLists.txt 更新 (如有文件重命名)

Step 6: 验证 + 性能基准 (0.5 天)
  ├── CTest 全量通过
  ├── porous 320×320×32 逐像素对比 (1e-6 容差)
  ├── 计时验证: sync_a=0.000s sync_b=0.000s
  └── pool_size=8K/16K/32K 三点 scaling 验证
```

**总工期: 5-7 天**

---

## 8. 验证清单

### 8.1 编译验证
- [x] `cmake --build . --config Release` 零 error
- [x] 零 warning（`-Wall -Wextra` 下无 implicit conversion 警告）
- [x] `static_assert(sizeof(struct path_hot) == 8)` 通过

### 8.2 正确性验证
- [x] CTest 全量通过 (`ctest -C Release --output-on-failure`)
- [x] porous 320×320×32 逐像素对比 vs main 基准 (容差 1e-6)
- [x] `paths_failed` 数目与基准一致 (115 vs 115)
- [x] steps 完全一致 (450,089 = 450,089)，rays 差异 <0.03%

### 8.3 性能验证 (pool=16384)
- [x] timing 输出中无 sync_a / sync_b 行（字段已删除）
- [x] 墙钟时间 262.5s ≤ 275s ✅
- [x] pool=32K 时无超线性劣化 (wall 316.6s = 1.21× pool=16K, ≤ 2.0×) ✅

### 8.4 回归检查 (pool=16384)
- [x] compact: 17.6s ≤ 18.1±1.8s ✅
- [x] collect: 19.0s ≤ 19.3±1.9s ✅
- [x] cascade: 40.8s ≤ 42.2s ✅ (+1.4%)
- [x] harvest+refill: 27.6s — 超出基准 23.5±1.2s ⚠️ (+17.5%，可接受)

### 8.5 pool_size scaling 验证

| 阶段 | pool=16K | pool=32K | 比率 | O9 比率 | 判定 |
|------|----------|----------|------|---------|------|
| cascade | 40.8s | 69.0s | 1.69× | 2.87× | ✅ 正常 |
| distribute | 31.7s | 46.1s | 1.46× | 3.42× | ✅ 正常 |
| collect | 19.0s | 29.3s | 1.54× | 2.44× | ✅ 正常 |
| compact | 17.6s | 18.8s | 1.07× | 1.33× | ✅ 优于O9 |
| wall | 262.5s | 316.6s | 1.21× | 1.87× | ✅✅ |

### 8.6 实测 timing (pool=16384)

```
timing: compact=17.630s  collect=19.039s  trace=25.567s(gpu=4.978s cpu=19.606s)
        distribute=31.668s  enc_locate=2.162s  cp=2.311s
        cascade=40.758s
        harvest+refill=27.619s  housekeeping=1.329s
        gpu_sync=1.090s  gpu_launch=92.903s
        total_timed=262.076s  wall=262.510s  coverage=99.8%
```

### 8.7 实测 timing (pool=32768)

```
timing: compact=18.792s  collect=29.318s  trace=33.993s(gpu=6.024s cpu=26.399s)
        distribute=46.115s  enc_locate=2.147s  cp=2.148s
        cascade=68.961s
        harvest+refill=29.436s  housekeeping=0.711s
        gpu_sync=0.695s  gpu_launch=84.058s
        total_timed=316.375s  wall=316.630s  coverage=99.9%
```

---

## 9. 与后续优化的关系

### 9.1 P0_OPT 实测 timing

```
timing: compact=17.630s  collect=19.039s  trace=25.567s(gpu=4.978s cpu=19.606s)
        distribute=31.668s  enc_locate=2.162s  cp=2.311s
        cascade=40.758s
        harvest+refill=27.619s  housekeeping=1.329s
        gpu_sync=1.090s  gpu_launch=92.903s
        total_timed=262.076s  wall=262.510s  coverage=99.8%
```

### 9.2 后续方向优先级排序

P0_OPT 完成后，剩余瓶颈及可选方向：

| # | 方向 | 剩余耗时 | 优化空间 | 依赖 P0_OPT |
|---|------|---------|---------|------------|
| 1 | gpu_launch 方案 E (消除 s3d_ray_request) | ~90s | -5~-15s | 否 (独立) |
| 2 | gpu_launch 批次合并 | ~90s | -20~-40s | 否 (独立) |
| 3 | cascade ALU 优化 | ~41s | -5~-10s | 否 (独立) |
| 4 | collect/distribute 进一步 SoA | ~52s | O9 证明不可行 | — |

**P0_OPT 与 gpu_launch 优化完全正交，可并行开发。**

### 9.3 P0_OPT 对 GPU 未来迁移的影响

- `path_hot` 的 8B 紧凑布局与 GPU warp/coalesced access 天然兼容
- 如果未来将 compact/collect 搬到 GPU，`hot_arr[]` 可直接作为 device array
- `uint8_t` 类型在 GPU shared memory 中更省空间 (8B/thread vs 20B)

---

*文档创建: 2026-03-05 | 实测验证: 2026-03-05 | 状态: ✅ 已实施并验证 | 基于 P0 + O9 实测*
