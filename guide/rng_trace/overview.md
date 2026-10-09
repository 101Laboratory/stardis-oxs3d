# Per-Path CBRNG + RNG Trace 系统

**创建日期**: 2026-02-14  
**状态**: 设计完成，准备实现  
**工作目录**: CPU → `stardis-cpu-rngtrace/`，GPU → `stardis-cus3d/`

---

## 1. 动机

### 1.1 当前 RNG 架构的问题

当前系统使用 `ssp_rng_proxy` 层管理 per-thread RNG（基于 Random123 Threefry4x64 的
`r123::Engine` 有状态包装）。所有路径共享同一个 per-thread RNG 指针：

```c
/* sdis_solve_wavefront.c L178 / sdis_solve_persistent_wavefront.c L284 */
p->rng = base_rng;  /* 或 slot_rngs[slot_idx] */
```

| 执行模型 | RNG 消耗序列 | 结果 |
|----------|-------------|------|
| **CPU 深度优先** | Path0 全部 → Path1 全部 → ... | R0..Rk → R(k+1)..R(2k) → ... |
| **GPU Wavefront 广度优先** | Path0 init → Path1 init → ... → Path0 step1 → Path1 step1 → ... | Path0 拿到 R0, R(N), R(2N)... — 序列完全不同 |

**后果**：

- 同一 `(pixel, spp)` 路径在 CPU 和 GPU 上拿到完全不同的随机数序列
- 不可能做 bit-exact 逐值对比，只能用统计容差（4σ）验证
- 难以定位跨平台数值差异的根因

### 1.2 目标

1. **执行顺序无关性** — 同一 `(pixel, spp)` 路径无论在深度优先还是广度优先调度下，产生完全相同的随机数序列
2. **bit-exact 可复现** — CPU 和 GPU 对同一路径产生完全相同的随机数
3. **精确 debug** — 可以追踪任意指定 `(pixel_x, pixel_y, spp_idx)` 的完整 RNG 调用序列、状态转换序列、中间值快照，并在两个平台之间 diff
4. **零性能回退** — CBRNG 无需 mutex/atomic，天然支持 GPU SIMT 并行

---

## 2. 核心设计：Per-Path Counter-Based RNG

### 2.1 原理

利用 Random123 Threefry4x64 的 **CBRNG（Counter-Based RNG）** 特性：

```
result[4] = Threefry4x64(counter, key)
```

- **key** = 路径身份标识（pixel 线性坐标 + spp 索引 + 全局种子）
- **counter** = 路径内调用计数器，从 0 递增
- 每次调用产生 4 个独立 `uint64_t`，用内部缓冲区逐个消费

**关键保证**：Threefry4x64 是纯整数运算（加法、XOR、rotation），
`(key, counter) → uint64[4]` 的映射在所有平台/编译器上 **bit-exact**。

### 2.2 数据结构

```c
struct path_rng {
    uint64_t key[2];      /* key = {pixel_linear_id, spp_idx ^ global_seed} */
    uint64_t counter;     /* 路径内 Threefry 调用计数器，从 0 递增 */
    uint64_t buf[4];      /* Threefry4x64 一次输出 4 个数的缓冲区 */
    int      buf_idx;     /* 当前消耗位置 (0..3)，4 = 需要 refill */
};
```

**存储开销**：~48 bytes/path（vs `path_state` ~2.2KB，增幅 ~2%）。

### 2.3 接口

```c
/* 初始化：CPU 和 GPU 调用完全相同的函数 */
void path_rng_init(struct path_rng* prng,
                   uint32_t pixel_x, uint32_t pixel_y,
                   uint32_t image_width, uint32_t spp_idx,
                   uint64_t global_seed);

/* 产生 [0, 1) 的 double（替换 ssp_rng_canonical） */
double path_rng_canonical(struct path_rng* prng);

/* 产生 [0, 1) 的 float（替换 ssp_rng_canonical_float） */
float  path_rng_canonical_float(struct path_rng* prng);

/* 产生原始 uint64（供高级采样函数内部使用） */
uint64_t path_rng_next_u64(struct path_rng* prng);
```

### 2.4 集成策略

两种方案：

| 方案 | 改动量 | 说明 |
|------|-------|------|
| **A: ssp_rng 后端适配** | 小 | 新增 `rng_desc` 后端，包装 `path_rng`，`p->rng` 接口不变 |
| **B: 直接替换调用点** | 中 | 在 `path_state` 中嵌入 `path_rng prng`，替换 ~40 处 `ssp_rng_xxx(p->rng)` |

**选择方案 B**：直接替换，原因：
1. `ssp_rng` 涉及虚函数表 + allocator + 引用计数，增加不必要的间接层
2. `path_rng` 是值类型，嵌入 `path_state` 更易于管理生命周期
3. 未来迁移到 CUDA device code 时，值类型可直接 memcpy 到 GPU
4. 改动点虽多（~40处），但机械性强，可通过 sed/replace 批量完成

---

## 3. RNG Trace 系统：4 层 Debug 工具

### 3.1 层级总览

| 层级 | 名称 | 信息粒度 | 开关 |
|:----:|------|---------|------|
| L1 | **RNG Sequence Trace** | 每次 RNG 调用的值 + 调用点 + 状态 | 编译时 `SDIS_RNG_TRACE` + 运行时环境变量 |
| L2 | **State Transition Trace** | 每步状态转换 + 位置 + 温度 + enc_id | 同上 |
| L3 | **Snapshot Checkpoint** | 指定阶段的完整 `path_state` dump | `STARDIS_SNAPSHOT_PHASES` 环境变量 |
| L4 | **Cross-Platform Validation** | 自动化 CPU/GPU diff + 判定 | 测试脚本驱动 |

### 3.2 环境变量接口

| 环境变量 | 格式 | 说明 |
|----------|------|------|
| `STARDIS_TRACE_PIXEL` | `x,y` | 追踪目标像素坐标 |
| `STARDIS_TRACE_SPP` | `n` | 追踪目标 SPP 序号 |
| `STARDIS_TRACE_OUTPUT` | 文件路径 | trace 输出文件（默认 `stderr`） |
| `STARDIS_SNAPSHOT_PHASES` | `PHASE1,PHASE2,...` | 在哪些状态转换时输出快照 |
| `STARDIS_RNG_TRACE` | `1` / `0` | 运行时启用/禁用 RNG 值追踪 |

### 3.3 开销控制

- 全部 trace 仅追踪单条路径，对其他路径零开销
- 匹配检查是 3 次 integer compare（`pixel_x`, `pixel_y`, `spp_idx`）
- Release 构建可通过 `#if SDIS_RNG_TRACE` 完全编译排除

---

## 4. 文件结构

```
guide/rng_trace/
├── overview.md                 ← 本文件（总览）
├── cpu_implementation.md       ← CPU 平台实现方案
├── gpu_implementation.md       ← GPU 平台实现方案
└── cross_platform_validation.md ← CPU/GPU 对比验证方案
```

**代码位置**：

| 组件 | CPU 位置 (`stardis-cpu-rngtrace/`) | GPU 位置 (`stardis-cus3d/`) |
|------|-----------------------------------|----------------------------|
| `path_rng` 定义 | `star-sp/0.15/src/ssp_path_rng.h` | `star-sp/0.15/src/ssp_path_rng.h` |
| `path_rng` 实现 | `star-sp/0.15/src/ssp_path_rng.c` | `star-sp/0.15/src/ssp_path_rng.c` |
| RNG Trace 工具 | `star-sp/0.15/src/ssp_path_rng_trace.h` | `star-sp/0.15/src/ssp_path_rng_trace.h` |
| 求解器集成 | `stardis-solver/0.16.2/src/sdis_solve_camera.c` | `stardis-solver/0.16.2/src/sdis_wf_steps.c` |
| 状态追踪集成 | `stardis-solver/0.16.2/src/sdis_path_trace.h` | `stardis-solver/0.16.2/src/sdis_path_trace.h` |
| 对比测试 | `stardis-solver/0.16.2/src/test_sdis_rng_trace.c` | `stardis-solver/0.16.2/src/test_sdis_rng_trace.c` |
| 验证脚本 | — | `scripts/validate_rng_trace.ps1` |

---

## 5. 实现阶段

| 阶段 | 任务 | 工作目录 | 工时 | 依赖 |
|:----:|------|---------|:----:|:----:|
| **P0** | 实现 `ssp_path_rng.h/.c` | 两端同步 | 1天 | 无 |
| **P1-CPU** | CPU 求解器集成 per-path RNG | `stardis-cpu-rngtrace/` | 1天 | P0 |
| **P1-GPU** | GPU wavefront 集成 per-path RNG | `stardis-cus3d/` | 1天 | P0 |
| **P2** | 实现 RNG Trace + State Trace (L1+L2) | 两端同步 | 1天 | P1 |
| **P3** | 实现 Snapshot Checkpoint (L3) | 两端同步 | 0.5天 | P2 |
| **P4** | 实现 Cross-Platform Validation (L4) | `stardis-cus3d/` | 1天 | P2+P3 |
| **P5** | E2E 测试切换到 bit-exact 模式 | 两端 | 0.5天 | P4 |

**总计 ~6 天**

---

## 6. 成功指标

| 指标 | 门限 |
|------|------|
| 同一 `(pixel, spp)` 的 RNG 序列在 CPU/GPU 间 bit-exact | 100% |
| 同一路径的状态转换序列在 CPU/GPU 间完全一致 | 100% |
| Trace 开启时单路径运行时间增加 | < 5% |
| Trace 关闭时性能开销 | 0（编译排除） |
| path_state 存储增加 | < 3% |

---

## 7. 已知风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| 浮点运算顺序差异（MSVC vs NVCC 优化） | 光追命中判定微小差异 → 分支走向不同 → RNG 序列分叉 | 阶段 P1 先不涉及 GPU kernel，仅在 CPU host code 上验证；使用 `/fp:precise` 和 `-fmad=false` |
| `sample_time` 对 steady-state 不消耗 RNG | key 一致但 counter 偏移不一致 | 显式分析并文档化 `sample_time` 的 RNG 消耗行为，必要时强制消耗 |
| `ssp_ran_sphere_uniform` 等高级采样函数内部消耗多个 RNG | 需保证内部调用次数一致 | 所有高级采样函数内部只调用 `ssp_rng_canonical`，替换底层即可 |
| retry 循环中的动态 RNG 消耗 | 如果 retry 次数不同，后续 RNG 分叉 | retry 条件依赖光追结果，光追 bit-exact 则 retry 相同 |
| Threefry 在 MSVC / NVCC 上的实现一致性 | CBRNG 纯整数运算，跨编译器 bit-exact | Random123 自带正确性测试；在 P0 阶段增加交叉验证 |

---

*下一步：阅读 [cpu_implementation.md](cpu_implementation.md) 了解 CPU 平台详细实现方案*
