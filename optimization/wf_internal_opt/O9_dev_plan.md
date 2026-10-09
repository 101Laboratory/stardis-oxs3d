# O9: path_state 域分解 SoA — 详细开发计划

**创建日期**: 2026-03-04  
**基于**: O9_path_state_soa_comprehensive_report.md + 代码库实测审计  
**目标分支**: `stardis-cus3d-o9`  
**工作目录**: `stardis-cus3d-o9/stardis-solver/0.16.2/src/`  

---

## 0. 设计核心原则

### 0.1 不使用兼容层——硬切策略

本迁移**不保留** `struct path_state` 兼容层。原因：

1. **隐式指针转换风险**：如果同时存在 `struct path_state* p` 和 `struct path_view* pv`，开发者可能在一处使用 `p->rwalk`（指向旧 AoS 的过时拷贝）而另一处使用 `pv->core->rwalk`（指向实际数据），造成**静默数据不一致**。
2. **编译器无法捕获语义错误**：兼容层使得所有旧代码仍可通过编译，但运行时行为可能在 scatter/gather 不同步时产生随机数值偏差。
3. **双重内存占用**：兼容层需要维护 AoS + SoA 双份存储，抵消 O9 的核心收益。

取而代之：**删除 `struct path_state`，强制所有访问通过 `struct path_view`**。编译器将精确报告所有 2700+ 处 `p->` 引用的编译错误。每一处都必须手工迁移到正确的域指针。

### 0.2 魔数 + 断言防御体系

迁移完成后，存在一类编译器无法捕获的隐式错误：**指针类型正确但指向错误的域数组元素**（如将 `path_bnd*` 传递到期望 `path_cnd_ds*` 的位置，若两者大小类似则不会 segfault）。

防御措施：

```c
/* === O9 魔数防御 — 编译期 + 运行期双重验证 === */

/* 每个域结构体首字段为 magic，所有域使用不同值 */
#define PATH_CORE_MAGIC     0xC0RE0009u   /* 3233546249 */
#define PATH_RAY_IO_MAGIC   0xRA10C009u   /* 2702573577 */
#define PATH_CND_DS_MAGIC   0xCD5E0009u   /* 3445596169 */
#define PATH_BND_MAGIC      0xBD000009u   /* 3170893833 */
#define PATH_LOCALS_MAGIC   0x10CA0009u   /*  280821769 */

/* 运行期断言宏 (NDEBUG 下编译为空) */
#ifndef NDEBUG
  #define ASSERT_CORE(ptr)    ASSERT((ptr) && (ptr)->_magic == PATH_CORE_MAGIC)
  #define ASSERT_RAY_IO(ptr)  ASSERT((ptr) && (ptr)->_magic == PATH_RAY_IO_MAGIC)
  #define ASSERT_CND_DS(ptr)  ASSERT((ptr) && (ptr)->_magic == PATH_CND_DS_MAGIC)
  #define ASSERT_BND(ptr)     ASSERT((ptr) && (ptr)->_magic == PATH_BND_MAGIC)
  #define ASSERT_LOCALS(ptr)  ASSERT((ptr) && (ptr)->_magic == PATH_LOCALS_MAGIC)
#else
  #define ASSERT_CORE(ptr)    ((void)0)
  #define ASSERT_RAY_IO(ptr)  ((void)0)
  #define ASSERT_CND_DS(ptr)  ((void)0)
  #define ASSERT_BND(ptr)     ((void)0)
  #define ASSERT_LOCALS(ptr)  ((void)0)
#endif
```

**植入点**：
- `refill/init_path`：初始化路径时写入所有域的 magic
- `pv_xxx()` 内联访问器：解析后立即 `ASSERT_XXX(pv->xxx)`
- 每个 `step_*` 函数入口：断言传入的域指针 magic 正确
- `scatter_path_to_domains()`（仅用于 init_path 过渡）：写入 magic 后 scatter

**验证通过后移除**：所有 CTest 通过 + porous 逐像素验证 (1e-6) 后，删除 `_magic` 字段和所有断言宏。

### 0.3 编译错误驱动的改动清单

策略：先完成结构体定义和 pool 分配，**删除 `struct path_state`**，让编译器产生所有错误。将编译错误列表作为**精确的改动清单**（而非手工枚举），逐文件修复，每修一个文件即可编译验证该文件是否有遗漏。

---

## 1. 前置条件清单

| # | 条件 | 状态 | 说明 |
|---|------|------|------|
| 1 | `sdis_wf_domain.h` 已定义 5 个域结构体 | ✅ 已完成 | path_core/path_ray_io/path_cnd_ds/path_bnd/path_locals |
| 2 | `sdis_wf_domain_inl.h` 已实现 pv_xxx() | ✅ 已完成 | 延迟加载访问器 |
| 3 | `sdis_wf_domain_sync.h` 已实现 scatter/gather | ✅ 已完成 | 过渡期辅助函数 |
| 4 | P1 冷块已独立 (sfn_arr/enc_arr/ext_arr) | ✅ 已完成 | 在 wavefront_pool 中独立分配 |
| 5 | 基准测试 porous 320×320×32 通过 | □ 待验证 | 迁移前最后一次基准运行 |
| 6 | 所有 CTest 当前分支通过 | □ 待验证 | 确认起点无回归 |

---

## 2. 迁移阶段计划

### 阶段总览

```
Phase 0: 基准锁定 + git tag                           [0.5天]
Phase 1: 结构体增加魔数 + pool SoA 分配               [1天]
Phase 2: 删除 struct path_state + 步函数签名切换       [1天, 产生 ~2700 错误]
Phase 3: step_* 函数逐文件修复                         [5-8天]
Phase 4: 管线主循环函数适配                            [2-3天]
Phase 5: vtable init_path/accumulate 适配              [1天]
Phase 6: dispatch_soa 层废弃 + sync 消除               [1天]
Phase 7: 清理 + 验证 + 魔数移除                        [1-2天]

总工期: 11-16 天
```

---

## Phase 0: 基准锁定 (0.5天)

### 0.1 运行当前基准

```bash
cd stardis-cus3d-o9/build
cmake --build . --config Release > build_baseline.log 2>&1
ctest -C Release --output-on-failure > ctest_baseline.log 2>&1
```

验证所有测试通过。
注：命令仅参考，用户已经完成参照存档。

### 0.2 运行 porous 渲染基准

```bash
cd Stardis-Starter-Pack-0.2.0/porous
<exe> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > baseline_320x320x32.ht
```

保存输出文件作为逐像素验证的参考基准。
注：命令仅参考，用户已经完成参照存档。

### 0.3 Git snapshot

```bash
git tag o9-baseline-pre-migration
git checkout -b o9/domain-decomposition
```

直接在`/stardis-cus3d-o9/`进行开发，确保与cus3d分支头一致再开始开发。

---

## Phase 1: 结构体增强 + Pool SoA 分配 (1天)

### 1.1 在域结构体中植入魔数字段

修改 `sdis_wf_domain.h`，在每个域结构体的**首字段**添加 `_magic`：

```c
struct path_core {
  uint32_t _magic;  /* O9 debug: must be PATH_CORE_MAGIC */
  /* --- Dispatch hot fields --- */
  enum path_phase       phase;
  ...
};

struct path_ray_io {
  uint32_t _magic;  /* O9 debug: must be PATH_RAY_IO_MAGIC */
  struct path_ray_request     ray_req;
  ...
};

struct path_cnd_ds {
  uint32_t _magic;  /* O9 debug: must be PATH_CND_DS_MAGIC */
  float   ds_dir0[3], ds_dir1[3];
  ...
};

struct path_bnd {
  uint32_t _magic;  /* O9 debug: must be PATH_BND_MAGIC */
  struct s3d_hit bnd_hit0, bnd_hit1;
  ...
};

struct path_locals {
  uint32_t _magic;  /* O9 debug: must be PATH_LOCALS_MAGIC */
  union { ... } u;
};
```

### 1.2 在 pv_xxx() 访问器中添加断言

修改 `sdis_wf_domain_inl.h`：

```c
static INLINE void
pv_init(struct path_view* pv, struct wavefront_pool* pool, size_t slot_idx)
{
  pv->pool     = pool;
  pv->slot_idx = slot_idx;
  pv->core     = &pool->core_arr[slot_idx];
  ASSERT_CORE(pv->core);
  pv->ray_io   = NULL;
  pv->cnd_ds   = NULL;
  pv->bnd      = NULL;
  pv->locals   = NULL;
  pv->sfn      = NULL;
  pv->enc      = NULL;
  pv->ext      = NULL;
}

static INLINE struct path_cnd_ds*
pv_cnd_ds(struct path_view* pv)
{
  if(!pv->cnd_ds) {
    pv->cnd_ds = &pv->pool->cnd_ds_arr[pv->slot_idx];
    ASSERT_CND_DS(pv->cnd_ds);
  }
  return pv->cnd_ds;
}
/* ... 其他访问器类似 */
```

### 1.3 在 wavefront_pool 中添加 5 个域 SoA 数组指针

修改 `sdis_solve_persistent_wavefront.h` 中的 `struct wavefront_pool`：

```c
struct wavefront_pool {
  /* --- O9: Domain SoA arrays (replaces slots[]) --- */
  struct path_core*     core_arr;      /* [pool_size] */
  struct path_ray_io*   ray_io_arr;    /* [pool_size] */
  struct path_cnd_ds*   cnd_ds_arr;    /* [pool_size] */
  struct path_bnd*      bnd_arr;       /* [pool_size] */
  struct path_locals*   locals_arr;    /* [pool_size] */

  /* --- P1: Cold-block SoA arrays (already externalized) --- */
  struct path_sfn_data *sfn_arr;
  struct path_enc_data *enc_arr;
  struct path_ext_data *ext_arr;

  /* --- REMOVED: struct path_state* slots; --- */
  size_t                pool_size;
  size_t                active_count;
  ...
};
```

### 1.4 修改 pool_create() 分配逻辑

修改 `sdis_solve_persistent_wavefront.c` 中的 `pool_create()`：

```c
/* 替换原始的 slots calloc */
/* -pool->slots = (struct path_state*)calloc(pool_size, sizeof(struct path_state)); */
/* +O9: 5 个域 SoA 数组 */
pool->core_arr    = (struct path_core*)   calloc(pool_size, sizeof(struct path_core));
pool->ray_io_arr  = (struct path_ray_io*) calloc(pool_size, sizeof(struct path_ray_io));
pool->cnd_ds_arr  = (struct path_cnd_ds*) calloc(pool_size, sizeof(struct path_cnd_ds));
pool->bnd_arr     = (struct path_bnd*)    calloc(pool_size, sizeof(struct path_bnd));
pool->locals_arr  = (struct path_locals*) calloc(pool_size, sizeof(struct path_locals));

if(!pool->core_arr || !pool->ray_io_arr || !pool->cnd_ds_arr
|| !pool->bnd_arr  || !pool->locals_arr) return RES_MEM_ERR;

/* O9: 初始化魔数 */
{
  size_t i;
  for(i = 0; i < pool_size; i++) {
    pool->core_arr[i]._magic    = PATH_CORE_MAGIC;
    pool->ray_io_arr[i]._magic  = PATH_RAY_IO_MAGIC;
    pool->cnd_ds_arr[i]._magic  = PATH_CND_DS_MAGIC;
    pool->bnd_arr[i]._magic     = PATH_BND_MAGIC;
    pool->locals_arr[i]._magic  = PATH_LOCALS_MAGIC;
  }
}
```

### 1.5 修改 pool_destroy() 释放逻辑

```c
free(pool->core_arr);
free(pool->ray_io_arr);
free(pool->cnd_ds_arr);
free(pool->bnd_arr);
free(pool->locals_arr);
```

### 1.6 静态断言

在 `sdis_wf_domain.h` 末尾添加编译期大小验证：

```c
/* O9 size guards — these trigger if struct layout changes unexpectedly */
#define O9_STATIC_ASSERT(expr, msg)  typedef char static_assert_##msg[(expr)?1:-1]

O9_STATIC_ASSERT(sizeof(struct path_core)    <= 640,  core_too_large);
O9_STATIC_ASSERT(sizeof(struct path_ray_io)  <= 320,  ray_io_too_large);
O9_STATIC_ASSERT(sizeof(struct path_cnd_ds)  <= 320,  cnd_ds_too_large);
O9_STATIC_ASSERT(sizeof(struct path_bnd)     <= 192,  bnd_too_large);
O9_STATIC_ASSERT(sizeof(struct path_locals)  <= 1024, locals_too_large);
```

### 1.7 Phase 1 验收

此时仍保留 `struct path_state` 和 `pool->slots`（但不再使用）。新增的域数组与旧数组并行存在。编译应当成功，CTest 应当通过（因为尚未切换任何消费者）。

```bash
git commit -m "O9/P1: add domain SoA arrays + magic + static_assert"
```

---

## Phase 2: 删除 struct path_state + 签名切换 (1天)

### 2.1 删除 struct path_state

在 `sdis_wf_state.h` 中：
- **注释掉或删除** `struct path_state { ... };` 的全部定义（约 250 行）
- **保留** `struct path_ray_request`（被 `path_ray_io` 嵌套使用）
- **保留** `struct path_bnd_sf_locals`（被 `path_locals.bnd_sf` 引用）
- **保留** `#define MAX_PICARD_DEPTH 3`（被 path_sfn_data 使用）
- **保留** P1 结构体 (path_sfn_data, path_enc_data, path_ext_data)

### 2.2 删除 pool->slots 字段

在 `sdis_solve_persistent_wavefront.h` 中：
- 删除 `struct path_state* slots;` 成员
- 保留 domain SoA 数组指针（Phase 1 中添加的）

### 2.3 切换所有 step_* 函数签名

所有 step_* 函数的第一个参数从 `struct path_state* p` 变为 `struct path_view* pv`。

**统一模式**：

| 原签名 | 新签名 |
|--------|--------|
| `step_xxx(struct path_state* p, struct sdis_scene* scn)` | `step_xxx(struct path_view* pv, struct sdis_scene* scn)` |
| `step_xxx(struct path_state* p, struct sdis_scene* scn, struct path_enc_data* enc)` | `step_xxx(struct path_view* pv, struct sdis_scene* scn)` |
| `step_xxx(struct path_state* p, struct sdis_scene* scn, struct path_ext_data* ext)` | `step_xxx(struct path_view* pv, struct sdis_scene* scn)` |
| `step_xxx(struct path_state* p, struct sdis_scene* scn, struct path_sfn_data* sfn)` | `step_xxx(struct path_view* pv, struct sdis_scene* scn)` |
| `step_xxx(struct path_state* p, struct sdis_scene* scn, const struct s3d_hit* hit)` | `step_xxx(struct path_view* pv, struct sdis_scene* scn, const struct s3d_hit* hit)` |
| `step_xxx(struct path_state* p, struct sdis_scene* scn, const struct s3d_hit* h0, const struct s3d_hit* h1, struct path_enc_data* enc)` | `step_xxx(struct path_view* pv, struct sdis_scene* scn, const struct s3d_hit* h0, const struct s3d_hit* h1)` |

**关键变化**：`enc`/`ext`/`sfn` 不再作为函数参数传递，改为通过 `pv_enc(pv)` / `pv_ext(pv)` / `pv_sfn(pv)` 从 path_view 延迟访问。这消除了调用端 `&pool->enc_arr[slot_idx]` 的散落代码。

修改文件：`sdis_wf_steps.h`（声明） + 所有 `sdis_wf_steps_*.c`（实现）。

### 2.4 切换两个 dispatch 函数签名

```c
/* 原 */
advance_one_step_no_ray(struct path_state* p, struct sdis_scene* scn,
                        int* advanced,
                        struct wavefront_pool* pool, size_t slot_idx)

advance_one_step_with_ray(struct path_state* p, struct sdis_scene* scn,
                          const struct s3d_hit* hit0,
                          const struct s3d_hit* hit1,
                          struct wavefront_pool* pool, size_t slot_idx)

/* 新 */
advance_one_step_no_ray(struct path_view* pv, struct sdis_scene* scn,
                        int* advanced)

advance_one_step_with_ray(struct path_view* pv, struct sdis_scene* scn,
                          const struct s3d_hit* hit0,
                          const struct s3d_hit* hit1)
```

`pool` 和 `slot_idx` 通过 `pv->pool` 和 `pv->slot_idx` 访问。

### 2.5 切换 cascade_advance_single_path 签名

```c
/* 原 */
cascade_advance_single_path(
  struct path_state* p,
  struct sdis_scene* scn,
  struct wavefront_pool* pool, size_t slot_idx, ...)

/* 新 */
cascade_advance_single_path(
  struct path_view* pv,
  struct sdis_scene* scn, ...)
```

### 2.6 切换 vtable 签名

```c
struct wavefront_ops {
  res_T (*generate_tasks)(struct wavefront_pool* pool, const void* mode_ctx);

  /* O9: init_path 现在写入 domain arrays 而非 path_state */
  res_T (*init_path)(struct path_view* pv,
                     struct ssp_rng* rng,
                     const struct pixel_task* task,
                     struct sdis_scene* scn,
                     unsigned enc_id,
                     const void* mode_ctx,
                     const double* time_range,
                     size_t picard_order,
                     enum sdis_diffusion_algorithm diff_algo,
                     uint32_t path_id,
                     uint64_t global_seed);

  /* O9: accumulate_result 从 path_core 读取 */
  void (*accumulate_result)(const struct path_core* core,
                            void* result_ctx);
};
```

### 2.7 产生编译错误清单

```bash
cmake --build . --config Release > o9_error_list.log 2>&1
```

**预期**: ~2700+ 编译错误，分布在：

| 文件 | 预期错误数 | 原因 |
|------|-----------|------|
| sdis_wf_steps_core.c | ~220+ | `p->xxx` 全部未定义 |
| sdis_wf_steps_cnd.c | ~270+ | 同上 |
| sdis_wf_steps_bnd_sf.c | ~440+ | 同上 |
| sdis_wf_steps_bnd_sfn.c | ~320+ | 同上 |
| sdis_wf_steps_bnd_ss.c | ~300+ | 同上 |
| sdis_wf_steps_bnd_ext.c | ~170+ | 同上 |
| sdis_wf_steps_cnv.c | ~165+ | 同上 |
| sdis_wf_steps_enc.c | ~41+ | 同上 |
| sdis_solve_persistent_wavefront.c | ~470+ | `pool->slots`, `p->xxx` |
| test_sdis_*.c (多个) | ~200+ | 测试代码中的 path_state 使用 |

```bash
git commit -m "O9/P2: delete path_state, switch all signatures [BROKEN BUILD]"
```

> **此 commit 是有意的 broken build**。编译错误列表是接下来 Phase 3-6 的精确改动清单。

---

## Phase 3: step_* 函数逐文件修复 (5-8天)

### 3.0 通用替换规则

每个 step_* 函数体内，`p->xxx` 需要根据字段所属域替换为对应的访问路径：

#### 域 A (path_core) 字段 — `pv->core->xxx`

```
p->phase              → pv->core->phase
p->active             → pv->core->active
p->needs_ray          → pv->core->needs_ray
p->ray_bucket         → pv->core->ray_bucket
p->ray_count_ext      → pv->core->ray_count_ext
p->path_id            → pv->core->path_id
p->pixel_x            → pv->core->pixel_x
p->pixel_y            → pv->core->pixel_y
p->realisation_idx    → pv->core->realisation_idx
p->rwalk              → pv->core->rwalk
p->rwalk.xxx          → pv->core->rwalk.xxx
p->ctx                → pv->core->ctx
p->ctx.xxx            → pv->core->ctx.xxx
p->T                  → pv->core->T
p->T.xxx              → pv->core->T.xxx
p->coupled_nbranchings → pv->core->coupled_nbranchings
p->steps_taken        → pv->core->steps_taken
p->done_reason        → pv->core->done_reason
p->rng                → pv->core->rng
p->rng_state          → pv->core->rng_state
p->ipix_image[x]      → pv->core->ipix_image[x]
```

#### 域 B (path_ray_io) 字段 — `pv_ray_io(pv)->xxx`

```
p->ray_req            → pv_ray_io(pv)->ray_req
p->ray_req.xxx        → pv_ray_io(pv)->ray_req.xxx
p->filter_data_storage → pv_ray_io(pv)->filter_data_storage
p->rad_direction[x]   → pv_ray_io(pv)->rad_direction[x]
p->rad_bounce_count   → pv_ray_io(pv)->rad_bounce_count
p->rad_retry_count    → pv_ray_io(pv)->rad_retry_count
```

#### 域 C (path_cnd_ds) 字段 — `pv_cnd_ds(pv)->xxx`

```
p->ds_dir0[x]          → pv_cnd_ds(pv)->ds_dir0[x]
p->ds_dir1[x]          → pv_cnd_ds(pv)->ds_dir1[x]
p->ds_hit0             → pv_cnd_ds(pv)->ds_hit0
p->ds_hit1             → pv_cnd_ds(pv)->ds_hit1
p->ds_delta_solid      → pv_cnd_ds(pv)->ds_delta_solid
p->ds_initialized      → pv_cnd_ds(pv)->ds_initialized
p->ds_enc_id           → pv_cnd_ds(pv)->ds_enc_id
p->ds_medium           → pv_cnd_ds(pv)->ds_medium
p->ds_props_ref        → pv_cnd_ds(pv)->ds_props_ref
p->ds_green_power_term → pv_cnd_ds(pv)->ds_green_power_term
p->ds_position_start[x] → pv_cnd_ds(pv)->ds_position_start[x]
p->ds_robust_attempt   → pv_cnd_ds(pv)->ds_robust_attempt
p->ds_delta            → pv_cnd_ds(pv)->ds_delta
p->ds_delta_solid_param → pv_cnd_ds(pv)->ds_delta_solid_param
```

#### 域 D (path_bnd) 字段 — `pv_bnd(pv)->xxx`

```
p->bnd_hit0              → pv_bnd(pv)->bnd_hit0
p->bnd_hit1              → pv_bnd(pv)->bnd_hit1
p->bnd_reinject_distance → pv_bnd(pv)->bnd_reinject_distance
p->bnd_solid_enc_id      → pv_bnd(pv)->bnd_solid_enc_id
p->bnd_retry_count       → pv_bnd(pv)->bnd_retry_count
```

#### 域 E (path_locals) 字段 — `pv_locals(pv)->u.xxx`

```
p->locals.bnd_ss.xxx     → pv_locals(pv)->u.bnd_ss.xxx
p->locals.bnd_sf.xxx     → pv_locals(pv)->u.bnd_sf.xxx
p->locals.cnd_wos.xxx    → pv_locals(pv)->u.cnd_wos.xxx
p->locals.cnv.xxx        → pv_locals(pv)->u.cnv.xxx
```

#### P1 冷块 — `pv_enc(pv)->xxx` / `pv_ext(pv)->xxx` / `pv_sfn(pv)->xxx`

这些已在 P1 中独立化，但原来通过函数参数 `enc`/`ext`/`sfn` 传入。现在改为通过 path_view 访问：

```
enc->xxx  → pv_enc(pv)->xxx
ext->xxx  → pv_ext(pv)->xxx
sfn->xxx  → pv_sfn(pv)->xxx
```

#### 地址取出 `&p->xxx` 的处理

这是**最危险的改动类别**。需要确保语义一致：

```
&p->rwalk          → &pv->core->rwalk
&p->ctx            → &pv->core->ctx
&p->T              → &pv->core->T
&p->rng_state      → &pv->core->rng_state
&p->ds_hit0        → &pv_cnd_ds(pv)->ds_hit0
&p->ds_hit1        → &pv_cnd_ds(pv)->ds_hit1
&p->ds_props_ref   → &pv_cnd_ds(pv)->ds_props_ref
&p->filter_data_storage → &pv_ray_io(pv)->filter_data_storage
&p->rwalk.vtx      → &pv->core->rwalk.vtx
&p->rwalk.hit_3d   → &pv->core->rwalk.hit_3d
&p->locals.bnd_sf  → &pv_locals(pv)->u.bnd_sf
```

### 3.1 修复顺序和每文件详细指南

按 `p->` 引用数**从少到多**排列，先修最简单的文件，建立模式信心。

#### 3.1.1 sdis_wf_steps_enc.c (~41 处) — 最简单的入手点

**涉及函数** (5 个):
1. `step_enc_locate_submit(pv, enc, query_pos, return_state)` → `step_enc_locate_submit(pv, query_pos, return_state)`
2. `step_enc_locate_result(pv, scn, enc)` → `step_enc_locate_result(pv, scn)`
3. `step_enc_query_emit(pv, enc, query_pos, return_state)` → `step_enc_query_emit(pv, query_pos, return_state)`
4. `step_enc_query_resolve(pv, scn, enc)` → `step_enc_query_resolve(pv, scn)`
5. `step_enc_query_fb_resolve(pv, scn, enc)` → `step_enc_query_fb_resolve(pv, scn)`

**域访问模式**:
- `p->phase` → `pv->core->phase` (core)
- `p->needs_ray` → `pv->core->needs_ray` (core)
- `p->ray_bucket` → `pv->core->ray_bucket` (core)
- `p->ray_count_ext` → `pv->core->ray_count_ext` (core)
- `enc->xxx` → `pv_enc(pv)->xxx` (P1 cold)
- `p->ray_req.xxx` → `pv_ray_io(pv)->ray_req.xxx` (ray_io)
- `p->filter_data_storage` → `pv_ray_io(pv)->filter_data_storage` (ray_io)

**入口断言**:
```c
static res_T
step_enc_locate_submit(struct path_view* pv,
                       const double query_pos[3],
                       enum path_phase return_state)
{
  struct path_enc_data* enc = pv_enc(pv);    /* 延迟加载 */
  struct path_core* c = pv->core;
  ASSERT_CORE(c);
  /* ... 替换所有 p->xxx ... */
}
```

**验证**: 编译 sdis_wf_steps_enc.c 通过（单文件）。  
**Commit**: `O9/P3.1: migrate sdis_wf_steps_enc.c (41 refs)`

#### 3.1.2 sdis_wf_steps_cnv.c (~164 处)

**涉及函数** (5 个):
1. `step_cnv_init(pv, scn)`
2. `step_cnv_startup_result(pv, scn, hit)`
3. `step_cnv_sample_loop(pv, scn)`
4. `step_bnd_dispatch(pv, scn)`
5. `step_bnd_post_robin_check(pv, scn)`

**域访问模式**:
- core: phase, active, needs_ray, ray_bucket, rwalk, ctx, T, done_reason, rng, coupled_nbranchings
- ray_io: ray_req, rad_direction, rad_bounce_count, filter_data_storage
- locals: `pv_locals(pv)->u.cnv.xxx`
- bnd: bnd_hit0/hit1, bnd_reinject_distance

**特殊注意**: `step_cnv_sample_loop` 包含 null-collision 循环，频繁访问 `p->rwalk` 和 `p->T`。为避免每次循环都通过 `pv->core->` 间接寻址，在函数入口**缓存本地指针**：

```c
static res_T
step_cnv_sample_loop(struct path_view* pv, struct sdis_scene* scn)
{
  struct path_core* c = pv->core;
  struct rwalk* rw = &c->rwalk;           /* 缓存热路径指针 */
  struct temperature* T = &c->T;
  ASSERT_CORE(c);
  /* ... 函数体中使用 rw->xx, T->xx 代替 p->rwalk.xx, p->T.xx ... */
}
```

**Commit**: `O9/P3.2: migrate sdis_wf_steps_cnv.c (164 refs)`

#### 3.1.3 sdis_wf_steps_bnd_ext.c (~167 处)

**涉及函数** (5 个):
1. `step_bnd_ext_check(pv, scn)` — 原 `(pv, scn, ext)` → ext 通过 pv_ext 获取
2. `step_bnd_ext_direct_result(pv, scn, hit)` — 原 `(pv, scn, hit, ext)`
3. `step_bnd_ext_diffuse_result(pv, scn, hit)` — 原 `(pv, scn, hit, ext)`
4. `step_bnd_ext_diffuse_shadow_result(pv, scn, hit)` — 原 `(pv, scn, hit, ext)`
5. `step_bnd_ext_finalize(pv, scn)` — 原 `(pv, scn, ext)`

**域访问模式**:
- core: phase, active, needs_ray, rwalk, ctx, T, ray_bucket
- ray_io: ray_req, rad_direction, filter_data_storage
- ext: pv_ext(pv)->xxx (全部外部通量字段)
- `&p->rwalk.vtx` → `&pv->core->rwalk.vtx` (地址取出，传给 heat_path 接口)
- `&p->rwalk.hit_3d` → `&pv->core->rwalk.hit_3d`

**Commit**: `O9/P3.3: migrate sdis_wf_steps_bnd_ext.c (167 refs)`

#### 3.1.4 sdis_wf_steps_core.c (~219 处)

**涉及函数** (7 个 step + 2 个 dispatch):
1. `step_init(pv, scn)`
2. `step_boundary(pv, scn)`
3. `step_conductive(pv, scn)` — 原 `(pv, scn, enc)` → enc 通过 pv_enc 获取
4. `step_convective(pv, scn)`
5. `step_coupled_radiative_begin(pv, scn)`
6. `step_radiative_trace(pv, scn, hit)`
7. `step_conductive_ds_process(pv, scn, h0, h1)` — 原 `(pv, scn, h0, h1, enc)`
8. `advance_one_step_no_ray(pv, scn, advanced)` — dispatch switch
9. `advance_one_step_with_ray(pv, scn, h0, h1)` — dispatch switch

**关键变化 — dispatch switch 中的 enc/ext/sfn 传递**:

原来 dispatch 通过 `&pool->enc_arr[slot_idx]` 传参：
```c
/* 原 advance_one_step_no_ray */
case PATH_CND_DS_CHECK_TEMP:
  res = step_cnd_ds_check_temp(p, scn, &pool->enc_arr[slot_idx]);
  break;
```

新的 dispatch 不再传这些参数（step 函数从 pv 获取）：
```c
/* 新 advance_one_step_no_ray */
case PATH_CND_DS_CHECK_TEMP:
  res = step_cnd_ds_check_temp(pv, scn);
  break;
```

**核心 `step_radiative_trace` 域访问**:
- core: phase, active, done_reason, rwalk, ctx, T, rng, needs_ray, ray_bucket, coupled_nbranchings
- ray_io: rad_direction, rad_bounce_count, rad_retry_count, ray_req, filter_data_storage
- `&p->ds_hit0` → `&pv_cnd_ds(pv)->ds_hit0` (传给 `step_conductive_ds_process`)

**Commit**: `O9/P3.4: migrate sdis_wf_steps_core.c (219 refs)`

#### 3.1.5 sdis_wf_steps_cnd.c (~269 处)

**涉及函数** (12 个):
- DS 系列: `step_cnd_ds_check_temp`, `step_cnd_ds_step_enc_verify`, `step_cnd_ds_step_advance`
- WoS 系列: `step_cnd_wos_check_temp`, `step_cnd_wos_closest`, `step_cnd_wos_closest_result`, `step_cnd_wos_diffusion_check`, `step_cnd_wos_diffusion_check_result`, `step_cnd_wos_fallback_trace`, `step_cnd_wos_fallback_result`, `step_cnd_wos_time_travel`

**域访问模式**:
- core: 大量 rwalk, ctx, T, phase, active, done_reason, rng
- cnd_ds: 全部 ds_* 字段（DS 热路径核心）
- locals: `pv_locals(pv)->u.cnd_wos.*` (WoS 路径)
- enc: `pv_enc(pv)->xxx` (enclosure 验证)
- ray_io: ray_req, filter_data_storage (WoS fallback trace)
- bnd: 无

**最危险的地址取出**:
```c
/* 原 */
solid_path_conductive_3d(&p->ctx, &p->rwalk, p->rng, &p->T, KMAX, ...);
solid_path_delta_sphere_init(&p->rwalk, p->rng, &p->ds_props_ref, ...);

/* 新 */
solid_path_conductive_3d(&pv->core->ctx, &pv->core->rwalk, pv->core->rng, &pv->core->T, KMAX, ...);
solid_path_delta_sphere_init(&pv->core->rwalk, pv->core->rng, &pv_cnd_ds(pv)->ds_props_ref, ...);
```

**性能技巧**: DS 步函数极热，每次 re-entry 仅 2 步。在函数入口缓存域指针：

```c
static res_T
step_cnd_ds_step_advance(struct path_view* pv, struct sdis_scene* scn)
{
  struct path_core* c = pv->core;
  struct path_cnd_ds* ds = pv_cnd_ds(pv);
  ASSERT_CORE(c);
  ASSERT_CND_DS(ds);
  /* ... 函数体中使用 c->rwalk, ds->ds_delta, etc. ... */
}
```

**Commit**: `O9/P3.5: migrate sdis_wf_steps_cnd.c (269 refs)`

#### 3.1.6 sdis_wf_steps_bnd_ss.c (~296 处)

**涉及函数** (3 个主 + 内部辅助):
1. `step_bnd_ss_reinject_sample(pv, scn)`
2. `step_bnd_ss_reinject_enc_result(pv, scn)` — 原 `(pv, scn, enc)`
3. `step_bnd_ss_reinject_decide(pv, scn)`

**域访问模式**:
- core: rwalk, ctx, T, phase, active, done_reason, rng, needs_ray, ray_bucket, ray_count_ext
- bnd: bnd_hit0/hit1, bnd_reinject_distance, bnd_solid_enc_id
- locals: `pv_locals(pv)->u.bnd_ss.*` (大量使用——dir_frt, dir_bck, ray_frt, ray_bck, enc_ids, lambda, delta, reinject_*, batch_idx_*)
- ray_io: ray_req, filter_data_storage
- enc: pv_enc(pv)->xxx

**特殊注意**: SS 4-ray 模式使用 `p->locals.bnd_ss.batch_idx_frt0` 等批次索引，这些在 collect 中设置、distribute 中读取。需确保 collect 和 distribute 也通过 `pv_locals(pv)->u.bnd_ss.batch_idx_xxx` 访问。

**Commit**: `O9/P3.6: migrate sdis_wf_steps_bnd_ss.c (296 refs)`

#### 3.1.7 sdis_wf_steps_bnd_sfn.c (~319 处)

**涉及函数** (6 个):
1. `step_bnd_sfn_prob_dispatch(pv, scn)` — 原 `(pv, scn, sfn)`
2. `step_bnd_sfn_rad_trace(pv, scn, hit)`
3. `step_bnd_sfn_rad_done(pv, scn)` — 原 `(pv, scn, sfn)`
4. `step_bnd_sfn_compute_Ti(pv, scn)` — 原 `(pv, scn, sfn)`
5. `step_bnd_sfn_compute_Ti_resume(pv, scn)` — 原 `(pv, scn, sfn)`
6. `step_bnd_sfn_check_pmin_pmax(pv, scn)` — 原 `(pv, scn, sfn)`

**域访问模式**:
- core: rwalk, ctx, T, phase, active, done_reason, rng, coupled_nbranchings
- ray_io: ray_req, rad_direction, rad_bounce_count, rad_retry_count, filter_data_storage
- locals: `pv_locals(pv)->u.bnd_sf.*` (PicardN 复用 bnd_sf_locals)
- sfn: `pv_sfn(pv)->xxx` (递归栈)
- enc: pv_enc(pv)->xxx (enclosure 查询)

**最危险的地址取出** (sfn_stack 相关):
```c
/* 原: push 到递归栈 */
sfn->stack[sfn->depth].rwalk_saved = p->rwalk;
sfn->stack[sfn->depth].T_saved     = p->T;

/* 新 */
struct path_sfn_data* sfn = pv_sfn(pv);
sfn->stack[sfn->depth].rwalk_saved = pv->core->rwalk;
sfn->stack[sfn->depth].T_saved     = pv->core->T;
```

```c
/* 原: pop 从递归栈恢复 */
p->rwalk = sfn->stack[sfn->depth].rwalk_saved;
p->T     = sfn->stack[sfn->depth].T_saved;

/* 新 */
pv->core->rwalk = pv_sfn(pv)->stack[pv_sfn(pv)->depth].rwalk_saved;
pv->core->T     = pv_sfn(pv)->stack[pv_sfn(pv)->depth].T_saved;
```

**建议**: sfn 热度不高（picardN 是稀少路径），但恢复逻辑涉及大结构体赋值，确保域内存储的是拷贝而非指针。

**Commit**: `O9/P3.7: migrate sdis_wf_steps_bnd_sfn.c (319 refs)`

#### 3.1.8 sdis_wf_steps_bnd_sf.c (~438 处) — 最大的单文件

**涉及函数** (4 个主 + 内部辅助):
1. `step_bnd_sf_reinject_sample(pv, scn)`
2. `step_bnd_sf_reinject_enc_result(pv, scn)` — 原 `(pv, scn, enc)`
3. `step_bnd_sf_prob_dispatch(pv, scn)` — 原 `(pv, scn, ext)`
4. `step_bnd_sf_nullcoll_decide(pv, scn)`

**域访问模式**:
- core: rwalk, ctx, T, phase, active, done_reason, rng, needs_ray, ray_bucket, coupled_nbranchings
- ray_io: ray_req, rad_direction, rad_bounce_count, rad_retry_count, filter_data_storage
- bnd: bnd_hit0/hit1, bnd_reinject_distance, bnd_solid_enc_id, bnd_retry_count
- locals: `pv_locals(pv)->u.bnd_sf.*` (最大的 locals 变体)
- ext: `pv_ext(pv)->xxx` (external flux 启动)
- enc: `pv_enc(pv)->xxx`

**特殊复杂度**: `step_bnd_sf_prob_dispatch` 是整个求解器中最复杂的函数之一（~400 行），包含 picard1/N 概率分发、温度采样、多种边界条件处理。建议分段修改：
1. 先替换所有 core 字段 (phase, rwalk, T, ...)
2. 再替换 locals 字段 (bnd_sf.xxx)
3. 再替换 bnd 字段
4. 再替换 ray_io 字段
5. 最后替换 enc/ext 字段

每段替换后尝试编译验证无 typo。

**Commit**: `O9/P3.8: migrate sdis_wf_steps_bnd_sf.c (438 refs)`

### 3.2 Phase 3 验收标准

- 所有 8 个 `sdis_wf_steps_*.c` 文件编译通过（standalone compile check）
- `sdis_wf_steps_core.c` 中的 2 个 dispatch switch 调用新签名
- `sdis_wf_steps.h` 中所有声明已更新
- 无 `struct path_state` 的引用残留

---

## Phase 4: 管线主循环函数适配 (2-3天)

这是改动量最大的单文件：`sdis_solve_persistent_wavefront.c` (~470 处 `p->` 引用 + ~40 处 `pool->slots`)。

### 4.1 collect_ray_requests 适配

**原**：遍历 `&pool->slots[i]` 获取 `p->ray_req`, `p->filter_data_storage`, `p->locals.bnd_ss.batch_idx_*`

**新**：构建 `path_view`（或直接用 `pool->ray_io_arr[i]`，因为 collect 只需 ray_io + core）

```c
/* collect 热循环 — 只需 ray_io + core */
for(k = 0; k < pv->need_ray_count; k++) {
  uint32_t i = pv->need_ray_indices[k];
  struct path_core*   c  = &pool->core_arr[i];
  struct path_ray_io* ri = &pool->ray_io_arr[i];

  ASSERT_CORE(c);
  ASSERT_RAY_IO(ri);

  /* 读 ray_req */
  rr->origin[0]    = ri->ray_req.origin[0];
  ...
  /* 读 filter_data */
  if(!S3D_HIT_NONE(&ri->filter_data_storage.hit_3d)) {
    rr->filter_data = &ri->filter_data_storage;
  }
}
```

**注意**: collect 中的 `p->locals.bnd_ss.batch_idx_frt0` 等需要通过 `pool->locals_arr[i].u.bnd_ss.batch_idx_frt0` 访问。这些是 SS 4-ray 模式专用，可通过 phase 判断有条件加载。

**注意**: collect 中的 enc_query 批次索引通过 `pool->enc_arr[i].batch_indices[j]` 访问（P1 已独立，无变化）。

### 4.2 distribute_ray_results 适配

**原**: 遍历 `&pool->slots[i]` 写回 hit 结果，调用 `step_xxx(p, scn, hit, ...)`

**新**: 构建 `path_view`，调用新签名 step：

```c
/* distribute — radiative bucket */
for(k = 0; k < pv->bucket_radiative_n; k++) {
  uint32_t i = pv->bucket_radiative[k];
  struct path_view view;
  pv_init(&view, pool, i);

  const struct s3d_hit* h0 = &pv_ray_hits[view.core->ray_req... /* 不对——batch_idx 在 ray_io */];
  /* 修正: batch_idx 在 ray_io 域 */
  struct path_ray_io* ri = pv_ray_io(&view);
  const struct s3d_hit* h0 = &pv_ray_hits[ri->ray_req.batch_idx];

  view.core->needs_ray = 0;
  res = step_radiative_trace(&view, scn, h0);
}
```

**关键修改列表**:
1. `pool->slots[i]` → 构建 `path_view` 或直接访问域数组
2. `p->needs_ray = 0` → `pool->core_arr[i].needs_ray = 0`
3. `p->ray_req.batch_idx` → `pool->ray_io_arr[i].ray_req.batch_idx`
4. step 函数调用传 `path_view*` 替代 `path_state*`
5. `p->locals.bnd_ss.ray_frt[j] = *hit` → `pool->locals_arr[i].u.bnd_ss.ray_frt[j] = *hit`
6. `p->locals.cnd_wos.cached_hit = *hit` → `pool->locals_arr[i].u.cnd_wos.cached_hit = *hit`

### 4.3 cascade_advance_single_path 适配

```c
static int
cascade_advance_single_path(
  struct path_view* pv,
  struct sdis_scene* scn,
  size_t* local_iterations,
  size_t* local_advances,
  size_t* local_paths_failed,
  size_t* local_enc_degenerate_null
#ifdef SDIS_CASCADE_PROFILE
  ,size_t  local_phase_count[]
  ,double  local_phase_time[]
#endif
)
{
  /* 原: p->needs_ray → pv->core->needs_ray */
  /* 原: p->phase → pv->core->phase */
  /* poolsfn_arr[slot_idx].depth → pv_sfn(pv)->depth */
  for(;;) {
    if(pv->core->needs_ray) break;
    if(pv->core->phase == PATH_DONE || ...) {
      /* M8 拦截 */
      if(pv->core->phase == PATH_DONE && pv_sfn(pv)->depth > 0) {
        pv->core->phase  = PATH_BND_SFN_COMPUTE_Ti_RESUME;
        pv->core->active = 1;
      }
      ...
    }
    res = advance_one_step_no_ray(pv, scn, &advanced);
    ...
  }
  return 0;
}
```

### 4.4 cascade 主循环适配

```c
/* 原: cascade OMP 循环 */
for(k = 0; k < pv->active_compact; k++) {
  uint32_t i = pv->active_indices[k];
  struct path_state* p = &pool->slots[i];
  cascade_advance_single_path(p, scn, pool, i, ...);
  dispatch_soa_sync_from_path(&pool->dsoa, i, p);  /* SYNC_B */
}

/* 新 */
for(k = 0; k < active_view->active_compact; k++) {
  uint32_t i = active_view->active_indices[k];
  struct path_view view;
  pv_init(&view, pool, i);
  cascade_advance_single_path(&view, scn, ...);
  /* SYNC_B 不再需要——dispatch fields 已在 core 中 */
}
```

### 4.5 compact_active_paths 适配

**原**: 从 `dsoa->phase[i]`, `dsoa->active[i]`, `dsoa->needs_ray[i]` 读取

**新**: 直接从 `pool->core_arr[i].phase`, `.active`, `.needs_ray` 读取

```c
static void
compact_active_paths(struct wavefront_pool* pool, struct pool_view* pv)
{
  size_t base = pv->base;
  size_t end  = base + pv->view_size;
  size_t i;

  /* O9: 不再需要 dispatch_soa */
  pv->active_compact      = 0;
  pv->need_ray_count      = 0;
  pv->done_count          = 0;

  for(i = base; i < end; i++) {
    struct path_core* c = &pool->core_arr[i];
    enum path_phase ph  = c->phase;
    int act             = c->active;
    int nr              = c->needs_ray;

    if(ph == PATH_DONE || ph == PATH_ERROR || ph == PATH_HARVESTED) {
      /* M8 sfn check */
      if(ph == PATH_DONE && pool->sfn_arr[i].depth > 0) {
        c->phase  = PATH_BND_SFN_COMPUTE_Ti_RESUME;
        c->active = 1;
        ph = PATH_BND_SFN_COMPUTE_Ti_RESUME;
        act = 1;
      } else {
        pv->done_indices[pv->done_count++] = (uint32_t)i;
        continue;
      }
    }
    if(!act) continue;

    pv->active_indices[pv->active_compact++] = (uint32_t)i;

    if(nr) {
      struct path_ray_io* ri = &pool->ray_io_arr[i];
      if(ri->ray_req.ray_count > 0) {
        pv->need_ray_indices[pv->need_ray_count++] = (uint32_t)i;
        /* bucket 分类 */
        ...
      }
    }
  }
}
```

### 4.6 harvest_completed_paths 适配

```c
/* 原: 从 path_state 读结果 */
struct path_state* p = &pool->slots[i];
if(p->phase != PATH_DONE && p->phase != PATH_ERROR) continue;
pool->ops->accumulate_result(p, pool->result_ctx);
p->phase = PATH_HARVESTED;
p->active = 0;
pool->dsoa.phase[i] = PATH_HARVESTED;
pool->dsoa.active[i] = 0;

/* 新: 从 core_arr 读 */
struct path_core* c = &pool->core_arr[i];
if(c->phase != PATH_DONE && c->phase != PATH_ERROR) continue;
pool->ops->accumulate_result(c, pool->result_ctx);
c->phase = PATH_HARVESTED;
c->active = 0;
/* dispatch_soa 同步不再需要 */
```

### 4.7 refill_pool 适配

```c
/* 原 */
pool->ops->init_path(&pool->slots[i], pool->slot_rngs[i], task, ...);
res = advance_path_to_first_ray(&pool->slots[i], pool->scn, pool, i);
dispatch_soa_sync_from_path(&pool->dsoa, i, &pool->slots[i]);

/* 新 */
struct path_view view;
pv_init(&view, pool, i);
pool->ops->init_path(&view, pool->slot_rngs[i], task, ...);
res = advance_path_to_first_ray(&view, pool->scn);
/* dispatch_soa 同步不再需要 */
```

### 4.8 sync 点消除

- **删除** `wait_and_postprocess` 中的 `dispatch_soa_sync_from_path` 循环 (3 处: need_ray_indices, enc_locate_to_slot, cp_to_slot)
- **删除** `pool_run_single` 中的等效同步循环
- 验证无其他 `dispatch_soa_sync_from_path` 调用残留

### 4.9 pool_run_single / pool_run_dual 中的 SYNC POINT 标注注释

搜索 `SYNC POINT A`, `SYNC POINT B`, `sync_a`, `sync_b` 等注释/变量名，删除或更新。

### 4.10 distribute_enc_locate_results / distribute_cp_results 适配

```c
/* 原 */
struct path_state* p = &pool->slots[slot_id];
p->phase = PATH_ENC_LOCATE_RESULT;
/* enc 数据写到 P1 冷块—不变 */

/* 新 */
struct path_core* c = &pool->core_arr[slot_id];
c->phase = PATH_ENC_LOCATE_RESULT;
```

### 4.11 sdis_ray_sort.c 适配

`sdis_ray_sort.c` L210 有一处 `&pool->slots[slot_id]`，用于 prefetch：

```c
/* 原 */
struct path_state* p = &pool->slots[slot_id];
PREFETCH_T0(p);

/* 新: prefetch core + ray_io */
PREFETCH_T0(&pool->core_arr[slot_id]);
PREFETCH_T0(&pool->ray_io_arr[slot_id]);
```

### 4.12 Phase 4 验收

- `sdis_solve_persistent_wavefront.c` 编译通过
- `sdis_ray_sort.c` 编译通过
- 所有 `pool->slots` 引用已清除（grep 验证）
- 所有 `dispatch_soa_sync_from_path` 调用已删除

**Commit**: `O9/P4: migrate pipeline functions (470+ refs)`

---

## Phase 5: vtable init_path / accumulate 适配 (1天)

### 5.1 camera_init_path 适配

**原**: `memset(p, 0, sizeof(*p))` + 逐字段写入 ~40 个 `p->xxx`

**新**: 零初始化所有域 + 写入对应域字段 + 植入 magic

```c
static res_T
camera_init_path(struct path_view* pv,
                 struct ssp_rng* rng,
                 const struct pixel_task* task,
                 struct sdis_scene* scn,
                 unsigned enc_id,
                 const void* mode_ctx,
                 const double* time_range,
                 size_t picard_order,
                 enum sdis_diffusion_algorithm diff_algo,
                 uint32_t path_id,
                 uint64_t global_seed)
{
  struct path_core* c = pv->core;

  /* O9: zero-init all 5 domains for this slot */
  memset(c, 0, sizeof(*c));
  memset(pv_ray_io(pv), 0, sizeof(struct path_ray_io));
  memset(pv_cnd_ds(pv), 0, sizeof(struct path_cnd_ds));
  memset(pv_bnd(pv), 0, sizeof(struct path_bnd));
  memset(pv_locals(pv), 0, sizeof(struct path_locals));
  /* P1 cold blocks */
  memset(pv_sfn(pv), 0, sizeof(struct path_sfn_data));
  memset(pv_enc(pv), 0, sizeof(struct path_enc_data));
  memset(pv_ext(pv), 0, sizeof(struct path_ext_data));

  /* O9: 植入魔数 (memset 后) */
  c->_magic                         = PATH_CORE_MAGIC;
  pv_ray_io(pv)->_magic             = PATH_RAY_IO_MAGIC;
  pv_cnd_ds(pv)->_magic             = PATH_CND_DS_MAGIC;
  pv_bnd(pv)->_magic                = PATH_BND_MAGIC;
  pv_locals(pv)->_magic             = PATH_LOCALS_MAGIC;

  /* 写入核心字段 */
  c->path_id         = path_id;
  c->pixel_x         = task->pixel_x;
  c->pixel_y         = task->pixel_y;
  c->realisation_idx = task->realisation;
  c->phase           = PATH_INIT;
  c->active          = 1;
  c->rng             = rng;
  /* ... 其余字段初始化 ... */
}
```

### 5.2 camera_accumulate_result 适配

**原**: `const struct path_state* p` → 读 `p->ipix_image[]`, `p->T.done`, `p->T.value`

**新**: `const struct path_core* core` → 读 `core->ipix_image[]`, `core->T.done`, `core->T.value`

```c
static void
camera_accumulate_result(const struct path_core* core, void* result_ctx)
{
  ASSERT_CORE(core);
  size_t ix = core->ipix_image[0];
  size_t iy = core->ipix_image[1];
  if(core->T.done) {
    /* accumulate core->T.value to image buffer */
  }
}
```

### 5.3 probe_init_path / probe_accumulate_result — 同理

### 5.4 probe_batch_init_path / probe_batch_accumulate_result — 同理

### 5.5 Phase 5 验收

- 所有 3 个 vtable 实现（camera/probe/probe_batch）编译通过
- `pool->ops->init_path(...)` 调用点参数匹配
- `pool->ops->accumulate_result(...)` 调用点参数匹配

**Commit**: `O9/P5: migrate vtable init_path/accumulate_result`

---

## Phase 6: dispatch_soa 层废弃 (1天)

### 6.1 删除 dispatch_soa 字段

在 `struct wavefront_pool` 中删除：
```c
struct dispatch_soa dsoa;  /* REMOVED */
```

### 6.2 删除 dispatch_soa_alloc / free 调用

在 `pool_create()` 和 `pool_destroy()` 中删除对 `dispatch_soa_alloc` / `dispatch_soa_free` 的调用。

### 6.3 删除 sdis_wf_soa.h / sdis_wf_soa.c

或标记为 deprecated（如果还有测试引用）。

### 6.4 删除 sdis_wf_domain_sync.h 中的过渡函数

- `scatter_path_to_domains()` — 如果 Phase 5 中 init_path 已直接写域数组，这个函数不再需要
- `gather_domains_to_path()` — 同理
- `sync_core_to_dsoa()` / `sync_core_from_dsoa()` — dsoa 已删除

### 6.5 删除 dispatch_soa_sync_from_path 调用

grep 确认无残留：
```bash
grep -rn "dispatch_soa" stardis-cus3d-o9/stardis-solver/ --include="*.c" --include="*.h"
```

### 6.6 Phase 6 验收

- `dispatch_soa` 不出现在任何编译单元中
- 所有 sync_a / sync_b 相关代码已清除
- 编译通过

**Commit**: `O9/P6: remove dispatch_soa layer + sync_a/sync_b elimination`

---

## Phase 7: 清理 + 验证 + 魔数移除 (1-2天)

### 7.1 删除 struct path_state 遗留

确认 `sdis_wf_state.h` 中：
- `struct path_state` 定义已删除
- 保留: `struct path_ray_request`, `struct path_bnd_sf_locals`, `MAX_PICARD_DEPTH`, P1 结构体
- 或者将保留部分移动到 `sdis_wf_domain.h` 中

### 7.2 sdis_solve_wavefront.c 处理

这是旧版非持久求解器，有 329 处 `p->` 引用。

**两个选项**:
- **选项 A**: 同样迁移（+1-2天工期，但保持代码库一致）
- **选项 B**: 标记为不编译 (`#if 0 ... #endif`) 或删除（如果不再使用）

**建议**: 确认 `sdis_solve_wavefront.c` 是否被 CMakeLists.txt 编译。如果是验证/参考代码且不参与生产构建，选择 B。

### 7.3 测试文件适配

多个 `test_sdis_*.c` 文件使用 `struct path_state`：
- `test_sdis_b4_m2_ray_bucketing.c`
- `test_sdis_b4_e2e.c`
- `test_sdis_b4_integration.c`
- `test_sdis_dispatch_soa.c`
- `test_sdis_ray_sort.c`
- `test_sdis_wavefront_benchmark.c`
- 以及 ~30 个 `test_sdis_wf_*.c` 端到端测试

**处理策略**:
- 端到端测试 (`test_sdis_wf_*.c`): 通过 persistent wavefront 求解器间接使用 path_state——如果求解器已迁移，这些测试无需修改
- 单元测试 (`test_sdis_b4_*.c`, `test_sdis_dispatch_soa.c`): 直接构造 path_state——必须迁移到使用 domain structs
- `test_sdis_dispatch_soa.c`: 测试 dispatch_soa 层——**删除**（该层已废弃）

### 7.4 全量 CTest 运行

```bash
cd stardis-cus3d-o9/build
cmake --build . --config Release > build_o9.log 2>&1
ctest -C Release --output-on-failure > ctest_o9.log 2>&1
```

所有测试必须通过。

### 7.5 porous 逐像素验证

```bash
cd Stardis-Starter-Pack-0.2.0/porous
<exe> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 > o9_320x320x32.ht

# 比较
diff baseline_320x320x32.ht o9_320x320x32.ht  # 应完全一致（相同 RNG 种子）
```

**注意**: 由于域迁移不改变任何算法逻辑，仅改变内存布局，在相同 RNG 种子下输出应当**逐位一致**（不是 1e-6 容差，是 bitwise identical）。如果不一致，说明迁移引入了错误。

### 7.6 性能基准对比

```bash
# 运行多次取稳定值
<exe> -M porous.txt -t 4 -V 3 -R spp=32:img=320x320 ... > /dev/null
# 预期: ~210-230s (原 315s)
```

### 7.7 魔数移除

所有 CTest + porous 验证通过后，执行最终清理：

1. 删除 `sdis_wf_domain.h` 中的 `_magic` 字段定义
2. 删除 `ASSERT_CORE` / `ASSERT_RAY_IO` 等宏定义
3. 删除 `pool_create()` 中的 magic 初始化循环
4. 删除 `pv_xxx()` 内联函数中的 `ASSERT_XXX` 调用
5. 更新静态断言中的大小上限（减去 4B magic 字段）

```bash
git commit -m "O9/P7: remove debug magic, final cleanup"
git tag o9-domain-decomposition-complete
```

---

## 3. 风险矩阵 + 缓解

| # | 风险 | 影响 | 概率 | 缓解措施 |
|---|------|------|------|---------|
| R1 | **1600+ 手工替换 typo** | 高: 运行时 segfault/数值错误 | 高 | 每文件 commit + CTest；magic + ASSERT 在 debug build 中拦截 |
| R2 | **`&p->rwalk` 语义变化** | 中: 地址指向错误域 | 中 | 编译器报错（p 不再存在）+ grep `&pv->` 审查 30+ 处 |
| R3 | **init_path memset 后忘记写 magic** | 中: 后续 ASSERT 全部 fire | 低 | init_path 模板代码统一，3 处实现 copy-paste 验证 |
| R4 | **collect/distribute 读写域不匹配** | 高: 批次索引错位导致 hit 分配错误 | 中 | collect 写 batch_idx 到 ray_io，distribute 从 ray_io 读——域一致性天然保证 |
| R5 | **SS 4-ray batch_idx 通过 locals 跨阶段传递** | 高: batch_idx 在 collect 中设置 (locals.bnd_ss.batch_idx_frt0) 在 distribute 中读取 | 低 | 两处都通过 `pool->locals_arr[i].u.bnd_ss.batch_idx_xxx` 访问——natural |
| R6 | **非持久求解器 (sdis_solve_wavefront.c) 编译失败** | 低: 如果不参与生产构建 | 中 | 检查 CMakeLists.txt，确认是否需要迁移 |
| R7 | **OMP 竞争条件** | 中: cascade OMP 并行中 path_view 在栈上构建，不共享 | 低 | path_view 是栈变量，每线程独立 |
| R8 | **sfn_stack pop 恢复写错域** | 高: rwalk/T 恢复到错误位置 | 低 | 编译器报错 + ASSERT_CORE 在恢复后验证 |
| R9 | **调试困难: Phase 2-4 期间代码不可运行** | 中: 无法增量验证 | 高 | Phase 3 每文件 commit，但完整验证需 Phase 4 完成后；可用 `#if 0` 注释临时绕过管线函数确认 step 文件编译 |

---

## 4. 关键决策点

### D1: sdis_solve_wavefront.c 是否迁移？

**判断方法**: 检查 CMakeLists.txt 是否编译此文件。如果是测试用途，可暂缓。

### D2: Phase 2-4 是否合为一个大 commit？

**建议**: 不合。保持 Phase 2（broken build intentional）+ Phase 3（逐文件修复）+ Phase 4（管线修复）独立。这样 git bisect 可以精确定位回归。

### D3: 魔数是否保留到 Release build？

**建议**: 不保留。ASSERT 宏在 NDEBUG 下编译为空，magic 字段在 Phase 7 中删除以恢复最优内存布局。如果需要持久防御，可保留 magic 字段但不断言（仅供 Nsight 调试时人工检查）。

### D4: prefetch 策略是否需要调整？

当前 distribute 中有 `PREFETCH_T0(&pool->slots[pv->bucket_radiative[kk + 4]])`。SoA 迁移后需要 prefetch 两个域：

```c
PREFETCH_T0(&pool->core_arr[pv->bucket_radiative[kk + 4]]);
PREFETCH_T0(&pool->ray_io_arr[pv->bucket_radiative[kk + 4]]);
```

这是性能细节，Phase 4 时实现，Phase 7 时可调优。

---

## 5. 逐文件改动量估计 (排序)

| 步骤 | 文件 | p→ 替换 | 新域访问 | 预计耗时 |
|------|------|---------|----------|---------|
| P3.1 | sdis_wf_steps_enc.c | 41 | core + enc + ray_io | 2-3h |
| P3.2 | sdis_wf_steps_cnv.c | 164 | core + ray_io + locals(cnv) + bnd | 4-5h |
| P3.3 | sdis_wf_steps_bnd_ext.c | 167 | core + ray_io + ext | 4-5h |
| P3.4 | sdis_wf_steps_core.c | 219 | core + ray_io + cnd_ds + dispatchtable | 6-8h |
| P3.5 | sdis_wf_steps_cnd.c | 269 | core + cnd_ds + locals(wos) + enc + ray_io | 6-8h |
| P3.6 | sdis_wf_steps_bnd_ss.c | 296 | core + bnd + locals(ss) + enc + ray_io | 6-8h |
| P3.7 | sdis_wf_steps_bnd_sfn.c | 319 | core + ray_io + locals(sf) + sfn + enc | 6-8h |
| P3.8 | sdis_wf_steps_bnd_sf.c | 438 | core + ray_io + bnd + locals(sf) + ext + enc | 8-12h |
| P4 | sdis_solve_persistent_wavefront.c | 470+ | 全部域 + pool 管理 | 12-16h |
| P5 | vtable impls (同文件) | ~120 | core + 全域 init | 4-5h |
| P6 | dispatch_soa cleanup | ~50 | 删除 | 2-3h |
| P7 | tests + cleanup + magic remove | ~200 | 适配 | 6-8h |
| **总计** | | **~2700+** | | **~75-100h** |

---

## 6. 验证矩阵

| 阶段 | 验证方法 | 通过标准 |
|------|---------|---------|
| Phase 1 | 编译通过 + CTest 全通过 | 0 regression |
| Phase 2 | 编译错误列表完整性 | 所有 p-> 引用产生错误 |
| Phase 3 | 每文件单独编译通过 | 0 error per file |
| Phase 4 | 全项目编译通过 | 0 error, 0 warning |
| Phase 5 | CTest 全通过 | 0 regression |
| Phase 6 | grep dispatch_soa = 0 结果 | 完全清除 |
| Phase 7.4 | CTest 全通过 | 0 regression |
| Phase 7.5 | porous 逐像素对比 | bitwise identical |
| Phase 7.6 | porous 性能对比 | wall < 250s (目标 ~210-230s) |

---

## 7. 工具辅助策略

### 7.1 编译错误驱动

```bash
# 每修完一个文件，单独编译该文件验证
cl /c /Fo:NUL sdis_wf_steps_enc.c <flags>
# 或通过 CMake 构建，从 build.log 筛选该文件的错误:
cmake --build . --config Release 2>&1 | Select-String "sdis_wf_steps_enc"
```

### 7.2 grep 审计

```bash
# 查找遗漏的 p-> 引用（Phase 3 末期）
grep -rn "p->" stardis-cus3d-o9/stardis-solver/0.16.2/src/sdis_wf_steps_*.c

# 查找遗漏的 pool->slots 引用
grep -rn "pool->slots" stardis-cus3d-o9/stardis-solver/0.16.2/src/

# 查找遗漏的 dispatch_soa 引用
grep -rn "dispatch_soa\|dsoa" stardis-cus3d-o9/stardis-solver/0.16.2/src/

# 查找遗漏的 path_state（除了注释和 .bak）
grep -rn "struct path_state" stardis-cus3d-o9/stardis-solver/0.16.2/src/ --include="*.c" --include="*.h" | grep -v ".bak" | grep -v "REMOVED"
```

### 7.3 IDE 辅助

如果 clangd/LSP 可用（Windows MSVC 环境有限），可利用 "Find All References" 验证每个域字段的消费者是否完整迁移。

---

## 8. Commit 规范

```
O9/P0:     snapshot baseline, tag o9-baseline-pre-migration
O9/P1:     add domain SoA arrays + magic + static_assert
O9/P2:     delete path_state, switch all signatures [BROKEN BUILD]
O9/P3.1:   migrate sdis_wf_steps_enc.c (41 refs)
O9/P3.2:   migrate sdis_wf_steps_cnv.c (164 refs)
O9/P3.3:   migrate sdis_wf_steps_bnd_ext.c (167 refs)
O9/P3.4:   migrate sdis_wf_steps_core.c (219 refs)
O9/P3.5:   migrate sdis_wf_steps_cnd.c (269 refs)
O9/P3.6:   migrate sdis_wf_steps_bnd_ss.c (296 refs)
O9/P3.7:   migrate sdis_wf_steps_bnd_sfn.c (319 refs)
O9/P3.8:   migrate sdis_wf_steps_bnd_sf.c (438 refs)
O9/P4:     migrate pipeline functions (470+ refs)
O9/P5:     migrate vtable init_path/accumulate_result
O9/P6:     remove dispatch_soa layer + sync elimination
O9/P7.1:   adapt tests + fix regressions
O9/P7.2:   porous pixel-exact verification PASSED
O9/P7.3:   remove debug magic, final cleanup, tag complete
```

---

*计划创建: 2026-03-04 | 预计工期: 11-16 天 | 改动规模: ~2700+ 引用替换, 10+ 源文件*
