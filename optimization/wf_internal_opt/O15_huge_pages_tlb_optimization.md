# O15: Huge Pages TLB 优化

**创建日期**: 2026-03-18  
**基于**: O14 实验失败后的根因分析、pool_size 超线性劣化的重新归因  
**场景**: porous 320×320 spp=32，merge-phase 架构 (O11+O12+O13)  
**硬件**: 24 核 / 32 线程，L2 = 32 MB (24核共享)，L3 = 36 MB (共享)  
**状态**: ❌ 结题关闭 — VTune 显示瓶颈为 L3/DRAM capacity miss 而非 TLB，转向 O16  

---

## TL;DR

pool_size 增大时 per-slot 成本超线性增长。O14 证伪了"调度策略"方向，实际瓶颈归因于 **容量缺失（capacity miss）+ TLB miss**。Huge Pages 是成本最低的干预——将 `slots[]` 等大数组从 4KB 页 → 2MB 大页分配，TLB 条目从 ~50,000 削减到 ~100，完全消除 TLB miss 惩罚。

---

## 1. 问题量化

### 1.1 当前内存布局与 TLB 压力

pool=32K 时各主要数组的内存足迹：

| 数组 | 元素大小 | 总量 (32K) | 4KB 页数 | TLB 条目 |
|------|---------|-----------|---------|---------|
| `slots[]` (path_state) | 1600B | 50.0 MB | 12,800 | 12,800 |
| `sfn_arr[]` | 3700B | 115.6 MB | 29,594 | 29,594 |
| `enc_arr[]` | 596B | 18.6 MB | 4,775 | 4,775 |
| `ext_arr[]` | 360B | 11.3 MB | 2,880 | 2,880 |
| `hot_arr[]` | 8B | 0.25 MB | 64 | 64 |
| **合计** | — | **195.8 MB** | **50,113** | **50,113** |

典型 x86-64 TLB 容量：

| 级别 | 条目数 | Miss 惩罚 |
|------|--------|----------|
| L1 dTLB | ~64 | ~7 cycles |
| L2 sTLB | ~1,536 | ~20 cycles |
| Page table walk | — | **~100-200 cycles** |

**50,113 个 4KB 页 vs 1,536 L2 sTLB 条目** → 稳态 TLB miss rate ≈ 97%。每次 slot 访问触发 ~25 个 cache line 读取，每个可能触发 page table walk（100-200 cycles ≈ 30-60 ns）。

### 1.2 TLB miss 对 per-slot 成本的贡献估算

```
per-slot 场景:
  25 cache lines × path_state 内部字段访问
  + cascade 子步中 BVH/材料/几何体的随机访问

TLB miss 频率估算（稳态，L2 sTLB thrash）:
  每个 unique 4KB page touch 触发一次 page walk
  per-slot path_state: 1600B / 4KB = 1 page (若对齐), 实际 ~1-2 pages（跨页边界）
  cascade 中场景数据: 每子步 3-10 page walks（BVH 散射访问）

保守估算 per-slot TLB walk 成本:
  path_state: 2 walks × 150 cycles = 300 cycles
  sfn/enc/ext: 3 walks × 150 cycles = 450 cycles
  场景数据: 不受 huge pages 影响（单独分析）
  
  每 slot TLB 惩罚 ≈ 750 cycles ≈ 0.21 μs (@ 3.5 GHz)
```

pool=32K per-slot 成本 1930 μs 中，0.21 μs 占 ~0.01%。但这是**极度保守**的估算——page table walk 本身还会 cache miss（4级页表，TLB miss 时产生额外 4 次 L3/DRAM 访问），真实惩罚可以是 500-1000 cycles。scale up 后：

```
上界估算:
  5 walks × 800 cycles = 4000 cycles ≈ 1.14 μs per slot
  32K slots/thread: 1024 × 1.14 μs = 1.17 ms per thread → 总计微秒级
```

**直接 TLB penalty 在 per-slot 时间中占比很小**（<1%），但 **间接影响** 可能更大：page table walk 产生的 cache line 加载会驱逐 L1/L2 中的热数据（path_state 字段、BVH 节点），产生二阶 cache miss 级联。

### 1.3 为什么仍然值得尝试

1. **实施成本极低**：仅改 `calloc` → `VirtualAlloc(..., MEM_LARGE_PAGES)`，代码改动 <50 行
2. **TLB 条目从 50,113 → ~100**：完全消除 page table walk 及其二阶缓存驱逐
3. **所有 pool_size 均受益**：8K/16K/32K/64K 都有 TLB 压力
4. **可验证**：有明确预期，容易通过 perf/VTune 对比 dTLB-load-misses 确认

---

## 2. 技术方案

### 2.1 Windows 大页分配

Windows 使用 `VirtualAlloc` 配合 `MEM_LARGE_PAGES` 标志分配 2MB 大页：

```c
#include <windows.h>

static void* huge_page_alloc(size_t size)
{
  /* Round up to large page boundary */
  SIZE_T lp_size = GetLargePageMinimum();
  if(lp_size == 0) return NULL;  /* Large pages not supported */
  
  size = (size + lp_size - 1) & ~(lp_size - 1);
  
  void* p = VirtualAlloc(NULL, size,
    MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
    PAGE_READWRITE);
  return p;  /* NULL on failure */
}

static void huge_page_free(void* p, size_t size)
{
  if(p) VirtualFree(p, 0, MEM_RELEASE);
  (void)size;
}
```

### 2.2 前置条件：SeLockMemoryPrivilege

`MEM_LARGE_PAGES` 需要进程拥有 `SeLockMemoryPrivilege` 权限。配置方式：

1. `secpol.msc` → Local Policies → User Rights Assignment → "Lock pages in memory"
2. 添加当前用户
3. 注销/重登录（组策略刷新）

非管理员环境中 VirtualAlloc 会返回 NULL。方案必须包含 **graceful fallback**：

```c
static void* pool_alloc_zeroed(size_t count, size_t elem_size, int* used_huge)
{
  size_t total = count * elem_size;
  void* p = NULL;
  
  *used_huge = 0;
  
  /* Attempt huge page allocation */
  if(total >= HUGE_PAGE_THRESHOLD) {
    p = huge_page_alloc(total);
    if(p) {
      memset(p, 0, total);  /* VirtualAlloc MEM_COMMIT 已清零，但保险起见 */
      *used_huge = 1;
      return p;
    }
    /* Fallback: large page allocation failed */
  }
  
  return calloc(count, elem_size);
}
```

`HUGE_PAGE_THRESHOLD` 建议设为 2MB（一个大页），小于此的分配不值得走大页路径。

### 2.3 需替换的分配点

`pool_create` 中以下 5 个分配改为大页：

| 分配 | 当前调用 | 32K 大小 | 4KB 页 → 2MB 页 |
|------|---------|---------|----------------|
| `pool->slots` | `calloc(N, 1600)` | 50 MB | 12,800 → 25 |
| `pool->sfn_arr` | `calloc(N, 3700)` | 116 MB | 29,594 → 58 |
| `pool->enc_arr` | `calloc(N, 596)` | 19 MB | 4,775 → 10 |
| `pool->ext_arr` | `calloc(N, 360)` | 11 MB | 2,880 → 6 |
| `pool->hot_arr` | `path_hot_arr_alloc` | 0.25 MB | 64 → 1 |
| **合计** | | **196 MB** | **50,113 → 100** |

`pool_destroy` 中对应 5 个释放点改为 `huge_page_free` / `free` 条件分发。

### 2.4 对齐保证

`VirtualAlloc` 返回的指针至少按 large page size (2MB) 对齐，满足所有 SIMD 和 cache line 对齐需求。`calloc` fallback 路径不变。

---

## 3. 预期收益模型

### 3.1 TLB miss 消除

| | 4KB 页 | 2MB 大页 | 改善 |
|--|--------|---------|------|
| TLB 条目 | 50,113 | 100 | **500×** |
| L2 sTLB 命中率 | ~3% | **~100%** | Page walk 完全消除 |
| Page walk/slot | 2-5 次 | 0 次 | — |

### 3.2 per-slot 成本预期变化

直接 TLB penalty 占比小（<1%），但 page table walk 的**二阶效应**（L1/L2 pollution）难以精确量化。

**保守预期**：per-slot 成本降低 **1-5%**（仅 TLB 直接 + 部分二阶）  
**乐观预期**：per-slot 成本降低 **5-15%**（如果 page table walk 的 L2 pollution 是 cascade 场景数据 miss 的重要贡献者）

### 3.3 验证指标

| 指标 | 采集方式 | 预期变化 |
|------|---------|---------|
| `dTLB-load-misses` | VTune / perf stat | **>90% 下降** |
| `dTLB-store-misses` | VTune / perf stat | **>90% 下降** |
| `DTLB_LOAD_MISSES.WALK_COMPLETED` | perf stat | **→0** |
| cpuA/cpuB p50 | stderr 日志 | **1-15% 下降** |
| cycle p50 | stderr 日志 | **1-10% 下降** |
| 32K/8K cpuA ratio | 交叉对比 | **下降**（TLB 压力与 pool_size 正相关） |

---

## 4. 实施计划

### 4.1 Worktree

基于 `stardis-oxs3d-merge-phase`（baseline，不包含 O14）创建新 worktree：

```bash
cd d:\Stardis-GPU\stardis-oxs3d-merge-phase
git worktree add ../stardis-oxs3d-o15 -b opt/o15-huge-pages
```

### 4.2 代码变更清单

| 步骤 | 文件 | 变更 |
|------|------|------|
| 1 | `sdis_solve_persistent_wavefront.h` | `wavefront_pool` 新增 `int huge_page_flags` 位掩码（记录哪些分配用了大页） |
| 2 | `sdis_solve_persistent_wavefront.c` | 新增 `huge_page_alloc` / `huge_page_free` / `pool_alloc_zeroed` 静态函数 |
| 3 | `sdis_solve_persistent_wavefront.c` | `pool_create`：5 个分配点改用 `pool_alloc_zeroed`，记录 flags |
| 4 | `sdis_solve_persistent_wavefront.c` | `pool_destroy`：根据 flags 调用 `huge_page_free` 或 `free` |
| 5 | `path_hot_arr_alloc` | 适配大页分配（或在 pool_create 中覆盖其结果） |
| 6 | 编译验证 | `cmake --build` |
| 7 | 功能验证 | 无大页权限时 fallback 到 calloc，输出无差异 |
| 8 | 性能验证 | 有大页权限时 8K/16K/32K 对比 baseline |

### 4.3 Fallback 策略

```
启动时:
  尝试 VirtualAlloc MEM_LARGE_PAGES 分配 slots[]
    成功 → 继续大页路径，stderr 输出 "[pool] huge pages: enabled (2MB)"
    失败 → calloc fallback，stderr 输出 "[pool] huge pages: unavailable, using standard pages"
```

无需命令行开关——尝试即可，失败静默降级。零行为变化保证。

---

## 5. 风险评估

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 收益低于噪声 (<1%) | **中** | 低（无回退成本） | 先用 VTune 确认 TLB miss 是否为显著贡献者 |
| SeLockMemoryPrivilege 不可用 | 低 | 无（graceful fallback） | 自动降级 |
| 2MB 对齐导致内存碎片 | 极低 | 无实际影响（pool 长期存活） | 仅大数组用大页 |
| VirtualAlloc 不清零 | 无 | — | MEM_COMMIT 保证清零；额外 memset 双保险 |

---

## 6. 与其他优化方向的关系

O14 失败后识别了三个方向。Huge Pages (O15) 是第一优先级（成本最低），结果会指导后续方向选择：

| 方向 | 编号 | 依赖关系 | 优先级 |
|------|------|---------|--------|
| **Huge Pages (TLB 消除)** | **O15** | 独立 | **1 — 最简单，先验证** |
| 空间相干排序 (Morton code) | O16 | 独立 | 2 — 中等难度，可能收益最大 |
| path_state hot core SoA 拆分 | O17 | 可叠加 O15 | 3 — 大重构，间接收益 |

O15 的 VTune 结果（TLB miss 占比）可以直接回答：TLB 是否是 super-linear scaling 的主要贡献者。如果消除 TLB miss 后 per-slot 成本依然随 pool_size 超线性增长，则确认瓶颈在 **L3 capacity** 而非 TLB，需转向 O16/O17。

---

*报告创建: 2026-03-18 | 状态: 📋 分析完成，待实施*
