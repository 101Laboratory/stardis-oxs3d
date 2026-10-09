# P5: 验证与回归测试

**工作量**: 2 天  
**前置**: P0-P4 全部完成  
**目标**: 确认正确性、性能、无回归

---

## 1. 功能正确性验证

### 1.1 单缓冲回退 (bit-exact)

```bash
# 设置 STARDIS_PIPELINE=0 回退到原版串行路径
set STARDIS_PIPELINE=0
cd Stardis-Starter-Pack/porous
<stardis-exe> -M porous.txt -t 4 -V 3 \
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > IR_single_buffer.ht
```

**验证**: 输出 `IR_single_buffer.ht` 必须与修改前的基准文件 **bit-exact 一致**。

```bash
# Windows: fc 逐字节比较
fc /B IR_single_buffer.ht IR_baseline.ht
```

如果不一致，说明 P2/P3 改造引入了回归（即使在单池模式下），需排查。

### 1.2 双缓冲结果对比

```bash
set STARDIS_PIPELINE=1
<stardis-exe> -M porous.txt -t 4 -V 3 \
  -R spp=32:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > IR_dual_buffer.ht
```

**验证**: 与 `IR_single_buffer.ht` 比较，容差 1e-6。

差异来源（已知且可接受）：
- 浮点累加顺序不同（同一像素的不同 spp 分布在两半，harvest 顺序不同）
- 影响量级：ULP 级 (~1e-15 per addition)，不超过 1e-10

### 1.3 Pixel Trace 逐路径比较

利用已有的 pixel trace 机制逐路径比较两种模式的随机游走轨迹：

```bash
set STARDIS_PIXTRACE=1
# 分别运行单缓冲和双缓冲，输出 pixel trace CSV
# 比较同一 path_id 的轨迹
```

验证要点：
- 同一 `path_id` 的 `T.value` 应完全一致（相同 RNG + 相同步骤）
- `steps_taken` 应完全一致
- `done_reason` 应完全一致

**如果不一致**: 说明半池化改变了路径的执行逻辑，需要排查 `_half` 函数实现。

---

## 2. 性能验证

### 2.1 Wall-clock 对比

| 场景 | 配置 | 单缓冲 | 双缓冲 | 加速比 |
|------|------|--------|--------|--------|
| porous | 320×320 spp=32 pool=8192 | 22min 44s (基准) | 目标 ≤12min | ≥1.9× |
| porous | 320×320 spp=1 pool=8192 | TBD | TBD | TBD |
| porous | 320×320 spp=256 pool=8192 | TBD | TBD | TBD |

### 2.2 分阶段计时

从双缓冲日志中提取关键计时指标：

```
预期:
  每半步 GPU trace:  ~1.54ms
  每半步 CPU total:  ~1.65ms
  GPU 空闲:          ~0.12ms/半步
  CPU 空闲:          ~0ms (CPU 是瓶颈)
```

如果 GPU 空闲时间远超 0.12ms，说明 CPU 阶段有未预期的开销。
如果 每半步总时间远超 1.66ms，检查：
- AoS→SoA 转换是否有额外开销
- half_pool 索引数组的 cache 局部性
- OMP cascade 在小 wavefront 时的开销

### 2.3 Nsight Systems 分析

```bash
nsys profile -o dual_buffer_trace <stardis-exe> ...
```

在 Nsight Systems GUI 中验证：
1. 两个 CUDA stream 存在明确的**并行执行区间**
2. `optixLaunch` 在 stream A/B 上交替出现
3. `cudaStreamSynchronize` 只出现在对应 stream 上（无 `cudaDeviceSynchronize`）
4. GPU kernel 占用率接近 90%+

### 2.4 GPU 吞吐量

```
GPU 吞吐量 = total_rays_traced / total_gpu_time
应保持:  ~17 Mrays/s (不因分半而降低)
```

如果吞吐量下降，检查：
- 半池 batch size 是否太小（batch < 4K 时 kernel launch overhead 显著）
- `uploadAsync` 是否正确异步（不应在 upload 时阻塞）

---

## 3. 压力测试

### 3.1 极短路径 (spp=1)

```bash
set STARDIS_PIPELINE=1
<stardis-exe> -M porous.txt -t 4 -V 3 \
  -R spp=1:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > IR_spp1.ht
```

验证要点：
- drain phase 极短，流水线排空逻辑正确
- 一半快速排空时不崩溃
- refill 不会遗漏任务

### 3.2 长时间 refill (spp=256)

```bash
set STARDIS_PIPELINE=1
<stardis-exe> -M porous.txt -t 4 -V 3 \
  -R spp=256:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0 \
  > IR_spp256.ht
```

验证要点：
- 稳态阶段持续时间长，流水线吞吐量稳定
- `task_next` 正确递增，两半不会重复分配同一任务
- 总 paths_completed = 320 × 320 × 256

### 3.3 场景类型覆盖

如有多种场景类型可用：

| 场景类型 | 特点 | 重点验证 |
|---------|------|---------|
| 纯辐射 | 只有 radiative bucket | distribute radiative |
| 纯导热 | conductive DS 路径 | distribute conductive + 2-ray |
| 耦合 | 混合 radiative + conductive | bucket 混合分发 |
| 有边界 | SFN stack resume | compact 中的 PATH_DONE + sfn_stack_depth |
| 有 enc_locate | 需要包围体查询 | enc_locate collect/distribute half |
| 有 WoS | 需要最近点查询 | cp collect/distribute half |

---

## 4. 内存安全

### 4.1 CUDA 内存检查

```bash
compute-sanitizer --tool memcheck <stardis-exe> ...
```

确认：
- 无越界 GPU 内存访问
- 无未初始化 GPU 内存读取
- 无 race condition (--tool racecheck)

### 4.2 CPU 内存检查 (Windows)

在 Debug 构建中启用 CRT 内存泄漏检测：

```c
#ifdef _DEBUG
_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
```

或使用 AddressSanitizer (MSVC `/fsanitize=address`)。

确认：
- half_pool 初始化的所有 malloc 都有对应 free
- batch_ctx 的 CUDA stream 和 params buffer 正确释放
- 无 double free

---

## 5. 回归检测自动化

### 5.1 CTest 集成

在 `CMakeLists.txt` 中添加双缓冲测试：

```cmake
# 单缓冲回退 (bit-exact)
add_test(NAME pipeline_fallback_porous
    COMMAND ${CMAKE_COMMAND} -E env STARDIS_PIPELINE=0
    $<TARGET_FILE:stardis> -M ${STARTER_PACK}/porous/porous.txt
    -t 4 -V 3 -R spp=4:img=64x64:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0)

# 双缓冲 (正确性)
add_test(NAME pipeline_dual_porous
    COMMAND ${CMAKE_COMMAND} -E env STARDIS_PIPELINE=1
    $<TARGET_FILE:stardis> -M ${STARTER_PACK}/porous/porous.txt
    -t 4 -V 3 -R spp=4:img=64x64:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0)
```

### 5.2 结果比较脚本

```python
#!/usr/bin/env python3
"""compare_ht.py — 比较两个 .ht 文件的温度场"""
import sys, struct, math

def read_ht(path):
    with open(path, 'rb') as f:
        data = f.read()
    # ... 解析 .ht 格式 ...
    return temperatures  # list of float

def compare(a, b, tol=1e-6):
    assert len(a) == len(b), f"size mismatch: {len(a)} vs {len(b)}"
    max_diff = 0.0
    for i, (va, vb) in enumerate(zip(a, b)):
        diff = abs(va - vb)
        if diff > max_diff:
            max_diff = diff
        if diff > tol:
            print(f"FAIL pixel {i}: {va} vs {vb}, diff={diff}")
    print(f"max_diff = {max_diff}")
    return max_diff <= tol

if __name__ == "__main__":
    ok = compare(read_ht(sys.argv[1]), read_ht(sys.argv[2]))
    sys.exit(0 if ok else 1)
```

---

## 6. 已知可接受差异

| 差异类型 | 量级 | 原因 | 处置 |
|---------|------|------|------|
| 温度累加顺序 | ~1e-15 per pixel | 双缓冲改变 harvest 顺序 | 容差 1e-6 内忽略 |
| 路径执行顺序 | 无影响 | 路径独立，结果不变 | — |
| drain 阶段步数 | ±1 | 两半排空速度不同 | 检查 total_steps 接近 |

---

## 验证通过标准

| 测试 | 通过条件 |
|------|---------|
| 单缓冲回退 | bit-exact 与基准一致 |
| 双缓冲温度 | 最大差异 < 1e-6 |
| Pixel trace | 同 path_id 的 T.value 完全一致 |
| 性能 | 加速比 ≥ 1.8× |
| GPU 利用率 | ≥ 85% |
| 内存安全 | compute-sanitizer 零错误 |
| spp=1 压力 | 无崩溃，结果正确 |
| spp=256 压力 | paths_completed = 期望值 |
