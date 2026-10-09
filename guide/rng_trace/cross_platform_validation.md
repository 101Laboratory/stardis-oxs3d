# CPU / GPU 跨平台对比验证方案

**目标**: 验证同一 `(pixel_x, pixel_y, spp_idx)` 路径在 CPU 深度优先和 GPU Persistent
Wavefront 两种执行模型下产生 **bit-exact** 相同的 RNG 序列、状态转换序列和最终温度。

---

## 1. 验证层次

```
┌───────────────────────────────────────────────────────────────────────┐
│                      验证层次金字塔                                    │
├───────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  L4: 全图统计一致性                       ← 现有 e2e 框架             │
│      32×32 @ 256 SPP, 95% 像素 ≤ 4σ                                  │
│                                                                       │
│  L3: 单路径温度 bit-exact                 ← per-path CBRNG 保证      │
│      T_cpu(px,py,spp) == T_gpu(px,py,spp)                            │
│                                                                       │
│  L2: 单路径状态转换序列 identical          ← State Trace 保证         │
│      cpu_state_trace == gpu_state_trace                               │
│                                                                       │
│  L1: 单路径 RNG 序列 bit-exact            ← CBRNG + RNG Trace 保证   │
│      cpu_rng_trace == gpu_rng_trace                                   │
│                                                                       │
└───────────────────────────────────────────────────────────────────────┘
```

**L1 → L2 → L3 是因果链**：
- L1 成立 ⇒ 两端每次随机数调用返回相同值
- L1 + 光追 bit-exact ⇒ 状态转换路径完全相同（L2）
- L2 成立 ⇒ 最终温度 bit-exact（L3）
- L3 对所有路径成立 ⇒ 全图统计一致（L4，可升级到 bit-exact 比较）

---

## 2. 验证 Pipeline

### 2.1 自动化流程

```
┌──────────────────────────────────────────────────────────────┐
│              validate_rng_trace.ps1                            │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  输入参数:                                                   │
│    -PixelX 5 -PixelY 3 -SppIdx 7                            │
│    -CpuExe  <stardis-cpu-rngtrace build path>                │
│    -GpuExe  <stardis-cus3d build path>                       │
│    -Scene   <porous.txt path>                                │
│    -ImageSize 32x32 -Spp 32                                  │
│                                                              │
│  Step 1: 运行 CPU (深度优先)                                  │
│    $env:STARDIS_TRACE_PIXEL="5,3"                            │
│    $env:STARDIS_TRACE_SPP="7"                                │
│    $env:STARDIS_RNG_TRACE="1"                                │
│    $env:STARDIS_TRACE_OUTPUT="cpu_rng_trace.log"             │
│    $env:STARDIS_STATE_TRACE_OUTPUT="cpu_state_trace.log"     │
│    $env:STARDIS_SNAPSHOT_PHASES="BND_DISPATCH,PATH_DONE"     │
│    $env:STARDIS_SNAPSHOT_OUTPUT="cpu_snapshots/"             │
│    & $CpuExe -M $Scene -t 1 -V 0 -R spp=32:img=32x32 ...  │
│                                                              │
│  Step 2: 运行 GPU (Persistent Wavefront)                     │
│    $env:STARDIS_WAVEFRONT="1"                                │
│    $env:STARDIS_TRACE_PIXEL="5,3"                            │
│    $env:STARDIS_TRACE_SPP="7"                                │
│    $env:STARDIS_RNG_TRACE="1"                                │
│    $env:STARDIS_TRACE_OUTPUT="gpu_rng_trace.log"             │
│    $env:STARDIS_STATE_TRACE_OUTPUT="gpu_state_trace.log"     │
│    $env:STARDIS_SNAPSHOT_PHASES="BND_DISPATCH,PATH_DONE"     │
│    $env:STARDIS_SNAPSHOT_OUTPUT="gpu_snapshots/"             │
│    & $GpuExe -M $Scene -t 1 -V 0 -R spp=32:img=32x32 ...  │
│                                                              │
│  Step 3: 对比                                                │
│    3a: diff cpu_rng_trace.log gpu_rng_trace.log              │
│    3b: diff cpu_state_trace.log gpu_state_trace.log          │
│    3c: foreach snapshot in cpu_snapshots/                     │
│           diff $_ gpu_snapshots/$(basename $_)               │
│    3d: 提取最终温度 T，比较 bit-exact                        │
│                                                              │
│  Step 4: 报告                                                │
│    ✅ L1 PASS: RNG 序列 bit-exact (N calls)                  │
│    ✅ L2 PASS: 状态转换 identical (M steps)                   │
│    ✅ L3 PASS: T_cpu=342.170000K == T_gpu=342.170000K         │
│                                                              │
│  或:                                                         │
│    ❌ L1 FAIL at RNG call #47:                                │
│       CPU: RNG[  47] = 0x3a7f2b9c1d8e4f06  BND_SS_DECIDE    │
│       GPU: RNG[  47] = 0x8c2e1f0a3b5d6789  BND_SS_DECIDE    │
│       → Bug: path_rng key 初始化参数不一致                    │
│                                                              │
│  或:                                                         │
│    ✅ L1 PASS                                                 │
│    ❌ L2 FAIL at step #23:                                    │
│       CPU: BND_DISPATCH → SS_REINJECT_SAMPLE                  │
│       GPU: BND_DISPATCH → SF_PROB_DISPATCH                    │
│       → Bug: 光追命中结果不同（enclosure id 分歧）            │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 diff 工具

```powershell
# validate_rng_trace.ps1 — 核心对比逻辑

function Compare-TraceFiles {
  param([string]$CpuFile, [string]$GpuFile, [string]$Label)

  $cpu = Get-Content $CpuFile
  $gpu = Get-Content $GpuFile

  if($cpu.Count -ne $gpu.Count) {
    Write-Host "❌ $Label FAIL: line count differs (CPU=$($cpu.Count) GPU=$($gpu.Count))"
    # 找到第一个分歧行
    $minLines = [Math]::Min($cpu.Count, $gpu.Count)
    for($i = 0; $i -lt $minLines; $i++) {
      if($cpu[$i] -ne $gpu[$i]) {
        Write-Host "  First divergence at line $($i+1):"
        Write-Host "    CPU: $($cpu[$i])"
        Write-Host "    GPU: $($gpu[$i])"
        break
      }
    }
    return $false
  }

  for($i = 0; $i -lt $cpu.Count; $i++) {
    if($cpu[$i] -ne $gpu[$i]) {
      Write-Host "❌ $Label FAIL at line $($i+1):"
      Write-Host "    CPU: $($cpu[$i])"
      Write-Host "    GPU: $($gpu[$i])"
      return $false
    }
  }

  Write-Host "✅ $Label PASS ($($cpu.Count) lines identical)"
  return $true
}
```

---

## 3. 测试矩阵

### 3.1 场景 × 路径类型覆盖

| 场景 | 主要路径类型 | 测试像素 | 说明 |
|------|------------|---------|------|
| `probe3` | 纯辐射 | 中心像素 | 只有辐射反弹，最简单 |
| `probe_boundary_list` | S/S 重注入 | 边界附近像素 | 触发 `SS_REINJECT_*` 分支 |
| `contact_resistance_2` | S/S + 接触热阻 | 界面像素 | 触发 TCR 计算 |
| `convection_non_uniform` | 对流路径 | 流体区域像素 | 触发 `CNV_*` 分支 |
| `conducto_radiative_2d` | S/F Picard1 | 固/流界面 | 触发 `SF_*` null-collision |
| `external_flux_with_diffuse` | 外部通量 | 有源项的像素 | 触发 `EXT_*` 分支 |
| `unsteady_1d` | 瞬态 + 时间采样 | 任意 | `sample_time` 消耗 RNG |
| `volumic_power2` | 体积功率 | 有功率区域 | 触发 delta-sphere 导热 |
| `porous` (Starter Pack) | 完整耦合 | 任意 | 综合验证 |

### 3.2 选择追踪像素的策略

不是所有像素/SPP 都会触发所有路径类型。选择策略：

1. **先运行 CPU 版本，收集 state trace 统计**：哪些像素触发了哪些状态分支
2. **选择覆盖最多状态类型的像素**
3. **或者**：运行全图 bit-exact 比较（所有像素所有 SPP），找到第一个分歧像素定向 debug

### 3.3 全图 bit-exact 模式

不使用 trace（避免 I/O 开销），直接在 C 代码中对比结果：

```c
/* test_sdis_rng_bitexact.c — 全图 bit-exact 验证 */

/* 方法 1：运行两次（深度优先 + wavefront），比较结果缓冲区 */
{
  double* result_df = run_depth_first(scn, ...);
  double* result_wf = run_wavefront(scn, ...);

  for(i = 0; i < npixels; i++) {
    /* bit-exact 比较（不是浮点 epsilon！） */
    uint64_t bits_df, bits_wf;
    memcpy(&bits_df, &result_df[i], sizeof(double));
    memcpy(&bits_wf, &result_wf[i], sizeof(double));

    if(bits_df != bits_wf) {
      size_t px = i % image_w;
      size_t py = i / image_w;
      printf("MISMATCH pixel=(%zu,%zu) "
             "df=%.17g (0x%016llx) wf=%.17g (0x%016llx)\n",
             px, py,
             result_df[i], (unsigned long long)bits_df,
             result_wf[i], (unsigned long long)bits_wf);
      mismatch_count++;
    }
  }

  printf("bit-exact: %zu/%zu pixels match (%.1f%%)\n",
         npixels - mismatch_count, npixels,
         100.0 * (npixels - mismatch_count) / npixels);
}
```

**bit-exact 100% 通过 = 系统验证完成**。

---

## 4. 已知限制与分歧源分析

### 4.1 可能的非 RNG 分歧源

即使 RNG bit-exact，以下因素仍可能导致 CPU/GPU 结果分歧：

| 分歧源 | 影响 | 检测方法 | 缓解 |
|--------|------|---------|------|
| **浮点乘加（FMA）差异** | MSVC `/fp:precise` vs NVCC `-fmad=true` 默认值不同 | 比较 L1 trace — 如果 RNG bit-exact 但 L2 分歧 | 统一编译标志：MSVC `/fp:precise`，NVCC `-fmad=false` |
| **光追精度** | GPU cuBQL 和 CPU Embree 的射线-三角形相交算法不同 | L2 trace 中 `BND_DISPATCH` 后的分支走向 | 本项目两端都用 cuBQL（不涉及 Embree），但CPU 后端 fallback 需确认 |
| **enclosure 查询** | GPU batch enc_locate vs CPU 串行 enc_locate | L2 trace 中 `ENC_LOCATE_RESULT` 的 `resolved_enc_id` | 两端使用相同的 BVH 和判定算法 |
| **`double` 归一化精度** | `(uint64 >> 11) * 2^-53` 在不同编译器上是否 bit-exact | 编译两端的 RNG 归一化代码，对比汇编 | 使用 `static inline` + 相同头文件保证源码级一致 |
| **未初始化内存** | `calloc` vs `memset` 差异 | valgrind / ASAN | 统一使用 `memset(p, 0, sizeof(*p))` |

### 4.2 分歧诊断决策树

```
RNG trace 是否 bit-exact？
├── ❌ 否 → path_rng_init 参数不同（key、seed 错误）
│          检查 pixel coords、spp_idx、image_width、global_seed
│
└── ✅ 是
    State trace 是否 identical？
    ├── ❌ 否 → 光追结果不同 或 浮点精度差异
    │   ├── 分歧发生在 shoot_ray 之后？ → 光追后端差异
    │   ├── 分歧发生在纯计算步？ → FMA / 浮点差异
    │   └── 检查 snapshot 中 rwalk.P、enc_id 的分歧点
    │
    └── ✅ 是
        温度是否 bit-exact？
        ├── ❌ 否 → 温度累加顺序差异（sum += w 的顺序）
        │          wavefront 中路径完成顺序≠深度优先
        │          解法：使用 Kahan 补偿求和或对结果排序后累加
        │
        └── ✅ 是
            → 🎉 验证通过
```

### 4.3 温度累加顺序问题

**重要**：即使单条路径 bit-exact，**像素级温度均值**仍可能不同：

```
CPU 深度优先: pixel_acc = w0 + w1 + w2 + ... + w(S-1)
GPU wavefront: pixel_acc = w3 + w0 + w7 + ... + w2   (完成顺序不同)
```

浮点加法不满足结合律 → `(a + b) + c ≠ a + (b + c)`。

**解决方案**：
1. **bit-exact 比较应在 per-path 级别**，不在 per-pixel 级别
2. per-pixel 比较使用 epsilon 容差（`|a - b| < 1e-12`）
3. 或：在两端强制按 `(spp_idx)` 顺序累加（排序后求和）

---

## 5. 测试代码设计

### 5.1 `test_sdis_rng_trace.c`（GPU 端）

```c
/* 测试 1: per-path CBRNG 确定性 */
static void test_cbrng_determinism(void)
{
  struct path_rng rng1, rng2;
  int i;
  path_rng_init(&rng1, 5, 3, 32, 7, 0);
  path_rng_init(&rng2, 5, 3, 32, 7, 0);

  for(i = 0; i < 10000; i++) {
    CHK(path_rng_next_u64(&rng1) == path_rng_next_u64(&rng2));
  }
}

/* 测试 2: 不同路径产生不同序列 */
static void test_cbrng_independence(void)
{
  struct path_rng rng_a, rng_b;
  int differ = 0, i;
  path_rng_init(&rng_a, 5, 3, 32, 7, 0);
  path_rng_init(&rng_b, 5, 3, 32, 8, 0);  /* 不同 spp */

  for(i = 0; i < 100; i++) {
    if(path_rng_next_u64(&rng_a) != path_rng_next_u64(&rng_b))
      differ++;
  }
  CHK(differ > 90);  /* 几乎所有值不同 */
}

/* 测试 3: canonical 范围 */
static void test_cbrng_canonical_range(void)
{
  struct path_rng rng;
  int i;
  path_rng_init(&rng, 10, 20, 64, 0, 42);

  for(i = 0; i < 100000; i++) {
    double r = path_rng_canonical(&rng);
    CHK(r >= 0.0 && r < 1.0);
  }
}

/* 测试 4: execution-order independence — 关键测试 */
static void test_cbrng_order_independence(void)
{
  /* 模拟两种执行顺序处理 4 条路径，每条消耗 100 个随机数 */
  struct path_rng paths_df[4], paths_wf[4];
  double results_df[4], results_wf[4];
  int i, j;

  /* 深度优先：路径 0 全部 → 路径 1 全部 → ... */
  for(i = 0; i < 4; i++) {
    path_rng_init(&paths_df[i], (uint32_t)i, 0, 4, 0, 0);
    results_df[i] = 0;
    for(j = 0; j < 100; j++)
      results_df[i] += path_rng_canonical(&paths_df[i]);
  }

  /* 广度优先：路径 0 step0 → 路径 1 step0 → ... → 路径 0 step1 → ... */
  for(i = 0; i < 4; i++)
    path_rng_init(&paths_wf[i], (uint32_t)i, 0, 4, 0, 0);

  for(i = 0; i < 4; i++)
    results_wf[i] = 0;

  for(j = 0; j < 100; j++) {
    for(i = 0; i < 4; i++)
      results_wf[i] += path_rng_canonical(&paths_wf[i]);
  }

  /* 结果必须 bit-exact */
  for(i = 0; i < 4; i++) {
    uint64_t bits_df, bits_wf;
    memcpy(&bits_df, &results_df[i], sizeof(double));
    memcpy(&bits_wf, &results_wf[i], sizeof(double));
    CHK(bits_df == bits_wf);
  }
}

/* 测试 5: Threefry4x64 跨平台一致性（hardcoded 参考值） */
static void test_cbrng_reference_vectors(void)
{
  /* 使用 Random123 文档中的已知测试向量验证 Threefry 正确性 */
  threefry4x64_ctr_t ctr = {{0, 0, 0, 0}};
  threefry4x64_key_t key = {{0, 0, 0, 0}};
  threefry4x64_ctr_t out = threefry4x64(ctr, key);

  /* 参考值需从 Random123 源码或测试套件获取 */
  /* CHK(out.v[0] == EXPECTED_0); */
  /* CHK(out.v[1] == EXPECTED_1); */
  /* ... */
  (void)out;  /* placeholder — fill in reference values during P0 */
}

/* 测试 6: depth-first vs wavefront single-path bit-exact */
static void test_single_path_bitexact(void)
{
  /* 使用一个最小测试场景（如 probe3），运行单个像素的：
   *   (a) CPU 深度优先 solve_pixel with path_rng
   *   (b) wavefront solve with path_rng
   * 比较温度值 bit-exact */

  /* 需要完整场景加载 — 实现为 deferred integration test */
}
```

### 5.2 `test_sdis_rng_trace.c`（CPU 端，stardis-cpu-rngtrace）

CPU 端测试包含测试 1-5（与 GPU 完全相同的代码），确保 `path_rng` 实现跨项目一致。

---

## 6. 输出文件格式规范

### 6.1 RNG Trace 格式（L1）

```
# STARDIS RNG TRACE — pixel=(5,3) spp=7 seed=0
# Format: RNG[call_idx] = 0xHEX_VALUE  phase=PHASE_NAME  function:line
RNG[     0] = 0x3a7f2b9c1d8e4f06  phase=DEPTH_FIRST               sample_time_prng:45
RNG[     1] = 0x8c2e1f0a3b5d6789  phase=DEPTH_FIRST               solve_pixel:131
RNG[     2] = 0x1a2b3c4d5e6f7890  phase=DEPTH_FIRST               solve_pixel:132
RNG[     3] = 0x9876543210abcdef  phase=DEPTH_FIRST               path_ran_sphere_uniform_float:12
RNG[     4] = 0xfedcba0987654321  phase=DEPTH_FIRST               path_ran_sphere_uniform_float:13
...
# END TRACE — 247 calls total
```

GPU 版本格式完全相同，仅 `phase` 字段替换为状态机名：

```
# STARDIS RNG TRACE — pixel=(5,3) spp=7 seed=0
RNG[     0] = 0x3a7f2b9c1d8e4f06  phase=PATH_INIT                 sample_time_prng:45
RNG[     1] = 0x8c2e1f0a3b5d6789  phase=PATH_INIT                 init_single_path:291
RNG[     2] = 0x1a2b3c4d5e6f7890  phase=PATH_INIT                 init_single_path:293
RNG[     3] = 0x9876543210abcdef  phase=PATH_INIT                 step_init:159
RNG[     4] = 0xfedcba0987654321  phase=PATH_INIT                 step_init:159
...
```

**关键**：`RNG[call_idx]` 和 `0xHEX_VALUE` 两列在两端必须逐行相同。`phase` 和
`function:line` 列允许不同（因为代码结构不同），但用于定位分歧原因。

### 6.2 State Trace 格式（L2）

```
=== PATH pixel=(5,3) spp=7 path_id=487 ===
[   0] PATH_INIT                    → PATH_RAD_TRACE_PENDING     ray=1 bucket=RADIATIVE     P=(0.050000,0.010000,0.000000) enc=3  rng_calls=5
[   1] PATH_RAD_TRACE_PENDING       → BND_DISPATCH               ray=0 bucket=-              P=(0.023146,0.010568,0.034568) enc=5  rng_calls=8
[   2] BND_DISPATCH                 → SS_REINJECT_SAMPLE          ray=0 bucket=-              P=(0.023146,0.010568,0.034568) enc=5  rng_calls=8
[   3] SS_REINJECT_SAMPLE           → SS_REINJECT_ENC             ray=4 bucket=STEP_PAIR      P=(0.023146,0.010568,0.034568) enc=5  rng_calls=16
...
[ 147] PATH_RAD_TRACE_PENDING       → PATH_DONE                  T=342.1700000000 done_reason=1  steps=147 rng_calls=247
```

CPU 版本生成等价格式，但 state 名统一用 wavefront 术语映射：

```
=== PATH pixel=(5,3) spp=7 path_id=0 ===
[   0] PATH_INIT                    → PATH_RAD_TRACE_PENDING     rng_calls=5   P=(0.050000,0.010000,0.000000) enc=3
[   1] PATH_RAD_TRACE_PENDING       → BND_DISPATCH               rng_calls=8   P=(0.023146,0.010568,0.034568) enc=5
...
```

### 6.3 Snapshot 格式（L3）

见 [gpu_implementation.md 第 4.2 节](gpu_implementation.md#42-path_state_dump-输出格式)。

---

## 7. 分歧定位工作流（实战示例）

### 场景：全图对比发现 pixel(12,7) spp=3 温度不匹配

**Step 1**: 启用 trace 重新运行

```powershell
$env:STARDIS_TRACE_PIXEL="12,7"
$env:STARDIS_TRACE_SPP="3"

# CPU
& $CpuExe ... > cpu_result.ht 2>cpu_rng_trace.log
# GPU
& $GpuExe ... > gpu_result.ht 2>gpu_rng_trace.log
```

**Step 2**: 对比 RNG trace

```powershell
# 快速检查
$cpu = Get-Content cpu_rng_trace.log | Where-Object { $_ -match "^RNG" }
$gpu = Get-Content gpu_rng_trace.log | Where-Object { $_ -match "^RNG" }

# 找到第一个分歧
for($i = 0; $i -lt [Math]::Min($cpu.Count, $gpu.Count); $i++) {
  $c = ($cpu[$i] -split "\s+")[2]  # hex value
  $g = ($gpu[$i] -split "\s+")[2]
  if($c -ne $g) {
    Write-Host "First RNG divergence at call #$i"
    Write-Host "  CPU: $($cpu[$i])"
    Write-Host "  GPU: $($gpu[$i])"
    break
  }
}
```

**Step 3a**: 如果 RNG 分歧 → 检查 `path_rng_init` 参数

```
常见原因：
- image_width 不一致（CPU 用 tile 局部坐标 vs GPU 用全局坐标）
- global_seed 不同
- spp_idx 偏移不同
```

**Step 3b**: 如果 RNG 一致但状态分歧 → 检查光追

```
RNG[  47] 两端都是 0x3a7f...
但 state trace 在 step 23 分歧：
  CPU: BND_DISPATCH → SS_REINJECT_SAMPLE
  GPU: BND_DISPATCH → SF_PROB_DISPATCH

→ 光追命中的 enc_id 不同
→ 检查 snapshot 在 step 22 的 rwalk.P 和射线方向
→ 可能是 BVH 构建差异或射线-三角形相交精度
```

**Step 3c**: 如果 RNG + 状态一致但温度不同 → 检查累加顺序

```
→ 使用 per-path 温度 dump 而非 per-pixel 聚合值
→ 确认 w_cpu == w_gpu for this specific (pixel, spp)
→ 如果 per-path w 都一致，问题在累加顺序（浮点结合律）
```

---

## 8. CTest 集成

### 8.1 新增测试目标

```cmake
# stardis-cus3d/CMakeLists.txt

# 单元测试（无需场景）
add_executable(test_sdis_rng_trace
  stardis-solver/0.16.2/src/test_sdis_rng_trace.c)
target_link_libraries(test_sdis_rng_trace sdis s3d ssp rsys)
add_test(NAME sdis_rng_trace_unit COMMAND test_sdis_rng_trace)

# bit-exact 集成测试（需场景数据 — deferred）
# add_test(NAME sdis_rng_bitexact_probe3 ...)
```

### 8.2 运行命令

```powershell
# 单元测试
cd stardis-cus3d/build-rngtrace
ctest -C Release -R "rng_trace" --output-on-failure

# 单路径 trace 对比（手动）
.\validate_rng_trace.ps1 -PixelX 5 -PixelY 3 -SppIdx 7 `
  -CpuExe "..\stardis-cpu-rngtrace\build\Release\stardis.exe" `
  -GpuExe ".\build-rngtrace\Release\stardis.exe" `
  -Scene "..\Stardis-Starter-Pack\porous\porous.txt"
```

---

## 9. 时间线

| 天 | CPU (`stardis-cpu-rngtrace/`) | GPU (`stardis-cus3d/`) |
|:--:|------|------|
| 1 | P0: `ssp_path_rng.h` + 单元测试 | P0: 同步复制 `ssp_path_rng.h` |
| 2 | P1-CPU: `solve_pixel` + `ray_realisation_3d` 改造 | — |
| 3 | — | P1-GPU: `path_state` + `init_single_path` + `sdis_wf_steps.c` 改造 |
| 4 | P2: RNG Trace 实现 | P2: RNG Trace + State Trace 实现 |
| 5 | P3: Snapshot 实现 | P3: Snapshot 实现 |
| 6 | — | P4: `validate_rng_trace.ps1` + bit-exact 测试 |

---

*返回总览：[overview.md](overview.md)*
