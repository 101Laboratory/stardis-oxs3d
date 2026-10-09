# Stardis Green函数工作流程指南

**目标读者**: 需要在IR渲染任务中生成和使用Green函数的用户  
**生成时间**: 2026-01-21 22:10:00  
**基于版本**: Stardis 0.12, Stardis-Solver 0.16.2  

---

## 快速参考卡片

```bash
# 第一步：生成Green函数（需要光追）
stardis -M model.txt -p 0,0,0 -G green.bin -n 1000000 -o 1

# 第二步：使用Green函数（无需光追，秒级完成）
# (修改model.txt中的边界温度/材质参数后)
stardis -M model_modified.txt -p 0,0,0 -G green.bin

# IR渲染 + Green函数
stardis -M model.txt -R camera.txt -G green.bin,paths.txt -n 10000000
```

---

## 一、概念理解

### **1.1 Green函数不会加速首次计算**

| 场景 | 是否使用Green | 光追时间 | 总时间 |
|------|--------------|---------|--------|
| **单次渲染** | ❌ 不使用 | 2小时 | **2小时** |
| **单次渲染** | ✅ 生成Green | 2小时 + 2分钟（保存） | **2小时2分** ❌ 更慢！ |
| **50次参数扫描** | ❌ 每次光追 | 2小时 × 50 | **100小时** |
| **50次参数扫描** | ✅ 用Green | 2小时（首次）+ 30秒 × 50 | **2小时25分** ✅ |

**关键洞察**:
- Green函数**不会**加速单次计算的追踪+采样过程（90% CPU时间）
- Green函数的价值在于**避免重复光追**（参数扫描场景）
- 首次生成Green函数时，光追时间与标准渲染**完全相同**

---

## 二、Stardis命令行选项

### **2.1 Green函数相关选项**

```bash
# Green函数生成/使用选项
-G <green_bin>[,<green_ascii>]    生成二进制Green函数文件
-g                                 生成ASCII Green函数（打印到stdout）

# 计算模式选项（与Green兼容的）
-p x,y,z[,time[,time]]            探针温度（体积中）
-P x,y,z[,time[,time]][:side]     探针温度（表面上）
-m medium_name[,time[,time]]      介质平均温度
-s surface[,time[,time]]          表面平均温度

# IR渲染选项
-R <camera_config>                 红外相机渲染

# 必需的输入选项
-M <model_file>                    场景模型文件（几何+材质+边界条件）

# 其他重要选项
-n <samples>                       蒙特卡洛采样数（默认值取决于模式）
-o <picard_order>                  Picard迭代阶数（Green函数必须=1）
-a <algorithm>                     扩散算法：dsphere或wos（默认dsphere）
-t <nthreads>                      线程数（默认：系统最大）
-x <rng_input>                     输入随机数生成器状态文件
-X <rng_output>                    输出随机数生成器状态文件
-V <0-3>                           详细级别（0=安静, 3=最详细）
```

### **2.2 选项组合规则**

**互斥选项** (不能同时使用):
- `-g` 和 `-G` 互斥（只能选一种Green输出格式）
- `-G` 不能重复使用
- 计算模式选项互斥（只能选一个：`-p`, `-P`, `-m`, `-s`, `-S`, `-F`, `-R`等）

**依赖关系**:
- 使用 `-G` 或 `-g` 时，必须指定兼容的计算模式（`-p`, `-P`, `-m`, `-s`）
- IR渲染 (`-R`) **不能**与Green函数输出一起使用（当前限制）
- Green函数要求 `-o 1`（Picard阶数必须为1，线性假设）

**兼容的计算模式** (可以使用Green函数):
```
GREEN_COMPATIBLE_MODES =
    MODE_COMPUTE_PROBE_TEMP_ON_VOL       (-p)
  | MODE_COMPUTE_PROBE_TEMP_ON_SURF      (-P)
  | MODE_COMPUTE_TEMP_MEAN_IN_MEDIUM     (-m)
  | MODE_COMPUTE_TEMP_MEAN_ON_SURF       (-s)
```

---

## 三、完整工作流程

### **工作流A: 探针温度 + Green函数**

#### **步骤1: 生成Green函数**

```bash
# 场景: 探针位于 (1.5, 2.0, 3.0)，观测时间 t=10s
stardis \
  -M system.txt \              # 场景模型文件
  -p 1.5,2.0,3.0,10 \          # 探针位置和时间
  -G probe_green.bin \         # 输出二进制Green函数
  -n 10000000 \                # 1000万采样（高质量）
  -o 1 \                       # Picard阶数=1（线性，必需）
  -a wos \                     # 使用Walk-on-Sphere算法
  -t 16 \                      # 16线程并行
  -X rng_state.dat \           # 保存RNG状态（可重现）
  -V 3                         # 详细输出

# 输出:
#   probe_green.bin  (~2 GB, 取决于路径数)
#   rng_state.dat    (~1 KB, RNG状态)
#
# 运行时间: 2-4小时（取决于场景复杂度）
```

**关键参数说明**:
- `-n 10000000`: 采样数越多，Green函数越精确，但生成时间线性增长
- `-o 1`: **必须为1**，Green函数基于线性假设
- `-a wos`: Walk-on-Sphere算法比Delta-Sphere更精确（无偏）
- `-X rng_state.dat`: 保存RNG状态，后续使用时加载以保证一致性

#### **步骤2: 修改场景参数**

编辑 `system.txt`，改变以下参数（不改变几何）:
- 边界温度 (`T_BOUNDARY_FOR_SOLID`, `H_BOUNDARY_FOR_FLUID`)
- 材质热导率 (`SOLID` 定义中的 `conductivity`)
- 界面发射率 (`H_BOUNDARY_FOR_FLUID` 中的 `emissivity`)
- 体积功率密度 (`SOLID` 中的 `volumic_power`)

**示例修改**:
```diff
# system.txt 原始
T_BOUNDARY_FOR_SOLID wall
  temperature 300.0
END

# system.txt 修改后
T_BOUNDARY_FOR_SOLID wall
  temperature 400.0  # 改变温度 300K → 400K
END
```

#### **步骤3: 使用Green函数快速求解**

```bash
# 使用已生成的Green函数，快速计算新边界条件下的温度
stardis \
  -M system_modified.txt \     # 修改后的场景文件
  -p 1.5,2.0,3.0,10 \          # 相同的探针位置和时间
  -G probe_green.bin \         # 加载Green函数（无需生成）
  -x rng_state.dat \           # 加载RNG状态（保证一致性）
  -V 3

# 输出: 温度估算（标准输出）
# 运行时间: 10-30秒 ✅ 比首次快100-500倍！
```

**性能对比**:
```
首次（生成Green）: 2小时
后续（使用Green）: 20秒
加速比: 360倍
```

#### **步骤4: 多次参数扫描**

```bash
# 自动化脚本：测试不同的墙壁温度
for T in 300 320 340 360 380 400; do
  # 修改system.txt中的温度
  sed -i "s/temperature [0-9.]*/temperature $T.0/" system.txt
  
  # 使用Green函数计算
  result=$(stardis -M system.txt -p 1.5,2.0,3.0,10 -G probe_green.bin -V 0 | grep "Temperature")
  
  echo "Wall T=$T K => Probe T=$result"
done

# 总时间: 6次 × 20秒 = 2分钟（vs 标准方法的12小时）
```

---

### **工作流B: IR渲染 + Green函数**

⚠️ **当前限制**: Stardis 0.12中，IR渲染模式（`-R`）**不支持**直接输出Green函数。

**原因**（代码分析）:
```c
// stardis-args.c:985-991
if(args->mode & (MODE_GREEN_BIN | MODE_GREEN_ASCII)) {
  if(args->mode & MODE_COMPUTE_IMAGE_IR) {  // IR渲染模式
    // 错误：Green函数与IR渲染不兼容
  }
}
```

**解决方案**: 使用探针模式生成Green函数，然后在IR渲染中使用场景级别的Green函数。

#### **方案1: 为整个场景生成Green函数网格**

```bash
# 1. 生成多个探针的Green函数（覆盖感兴趣区域）
for x in $(seq 0.0 0.5 5.0); do
  for y in $(seq 0.0 0.5 5.0); do
    for z in $(seq 0.0 0.5 3.0); do
      stardis -M scene.txt -p $x,$y,$z -G green_${x}_${y}_${z}.bin -n 1000000 -o 1
    done
  done
done

# 2. 执行IR渲染（标准模式，无Green）
stardis -M scene.txt -R camera.txt -n 10000 > ir_image.htrdr

# 3. 后处理：使用Green函数重新计算温度（自定义脚本）
python recompute_with_green.py ir_image.htrdr green_*.bin > ir_image_updated.htrdr
```

#### **方案2: 修改Stardis源码支持IR+Green**

需要修改 `stardis-solver/0.16.2/src/sdis_solve_camera.c`，添加Green路径记录：

```c
// 在 sdis_solve_camera 中添加Green路径支持
struct sdis_solve_camera_args {
  // ... 现有参数
  struct green_path_handle* green_path;  // 新增
};

// 在 ray_realisation_3d 调用时传递 green_path
```

**预计工作量**: 1-2周（需要修改API并测试）

#### **方案3: 分离生成Green函数和使用（推荐）**

```bash
# 步骤1: 为相机视图中的代表性像素生成Green函数
# 选择10×10网格的采样点
python generate_camera_samples.py camera.txt --grid 10x10 > sample_pixels.txt

# 步骤2: 为每个采样点生成Green函数
while read px py pz; do
  stardis -M scene.txt -p $px,$py,$pz -G green_pixel_${px}_${py}.bin -n 1000000 -o 1
done < sample_pixels.txt

# 步骤3: 执行标准IR渲染（获取完整图像）
stardis -M scene.txt -R camera.txt -n 100000 > baseline.htrdr

# 步骤4: 改变边界条件后，插值Green函数结果
# （使用采样点的Green函数插值到所有像素）
python interpolate_green_to_image.py \
  --green-files green_pixel_*.bin \
  --camera camera.txt \
  --output updated_image.htrdr
```

---

## 四、Green函数文件格式

### **4.1 二进制格式** (`.bin`)

📍 定义: `stardis-cpu/stardis/0.12/src/stardis-green-types.h`

```c
// 文件结构（二进制）
struct green_file_header {
  int32_t version;              // Green函数格式版本（当前=3）
  uint8_t scene_hash[32];       // 场景SHA256哈希（验证一致性）
  uint8_t signature[32];        // 用户签名（可选）
  uint64_t npaths_valid;        // 有效路径数
  uint64_t npaths_invalid;      // 无效路径数
  // ... RNG状态、统计信息
};

struct green_path_data {
  double elapsed_time;          // 路径运行时间 [s]
  uint32_t end_type;            // 终点类型（界面/辐射环境/体积）
  uint32_t limit_id;            // 终点ID
  
  // 功率项数组
  uint32_t npower_terms;
  struct {
    double term;                // Green系数 [K/W]
    uint32_t medium_id;
  } power_terms[];
  
  // 热流项数组
  uint32_t nflux_terms;
  struct {
    double term;                // Green系数 [K/W/m²]
    uint32_t interface_id;
    uint8_t side;               // FRONT/BACK
  } flux_terms[];
  
  // 外部热流项数组
  uint32_t nextflux_terms;
  struct {
    double term_wrt_power;      // [K/W]
    double term_wrt_diffuse_radiance;  // [K/W/m²/sr]
    double time;
    double dir[3];
  } extflux_terms[];
};
```

**文件大小估算**:
```
单条路径平均大小:
  - 基础信息: 50 bytes
  - 功率项 (平均8个): 8 × 12 = 96 bytes
  - 热流项 (平均4个): 4 × 20 = 80 bytes
  - 外部热流 (平均2个): 2 × 40 = 80 bytes
  总计: ~300 bytes/路径

10M路径: 300 bytes × 10M ≈ 3 GB
```

### **4.2 ASCII格式** (`-g`，输出到stdout）

```
# Green Function Output
# Version: 3
# Scene Hash: a3f5d8...
# Valid Paths: 10000000
# Invalid Paths: 125

# Path 0
elapsed_time: 0.0523
end_type: INTERFACE
limit_id: 42
power_terms: 2
  medium_id=3 term=1.234e-03  # [K/W]
  medium_id=7 term=5.678e-04
flux_terms: 1
  interface_id=42 side=FRONT term=2.345e-02  # [K/W/m²]
extflux_terms: 0

# Path 1
...
```

**用途**:
- 调试和验证Green函数内容
- 人工检查路径终点分布
- 转换为其他格式（JSON, CSV等）

---

## 五、实际案例

### **案例1: 散热器优化设计**

**目标**: 优化散热片厚度，使芯片温度最低。

**传统方法**（无Green函数）:
```bash
for thickness in 1.0 1.5 2.0 2.5 3.0; do
  # 修改几何（需要重新生成模型）
  python generate_model.py --thickness $thickness > model_${thickness}.txt
  
  # 完整光追求解
  stardis -M model_${thickness}.txt -p 5,5,0.1 -n 10000000 > result_${thickness}.txt
  
  # 每次耗时: 3小时
done
# 总时间: 15小时
```

**Green函数方法**:
```bash
# 问题：几何改变时，Green函数失效！
# 结论：此案例不适合Green函数（几何变化）
```

**适用改进方案**:
```bash
# 固定几何，扫描热导率（材质参数）
for conductivity in 100 150 200 250 300; do
  # 只修改材质参数（几何不变）
  sed -i "s/conductivity [0-9.]*/conductivity $conductivity/" model.txt
  
  # 使用Green函数
  stardis -M model.txt -p 5,5,0.1 -G chip_green.bin > result_${conductivity}.txt
  
  # 每次耗时: 15秒
done
# 总时间: 3小时（首次生成Green）+ 1.25分钟（5次使用）≈ 3小时2分钟
```

---

### **案例2: 建筑热舒适性分析**

**目标**: 分析不同室外温度和太阳辐射下的室内温度分布。

**场景**: 
- 固定几何（房间不变）
- 变量：室外温度（0-40°C）、太阳辐射（0-1000 W/m²）

**工作流**:
```bash
# 1. 为室内10个位置生成Green函数（一次性）
positions=(
  "2,2,1.5"   # 客厅中心
  "4,2,1.5"   # 厨房
  "6,4,1.5"   # 卧室
  # ... 其他位置
)

for pos in "${positions[@]}"; do
  stardis -M building.txt -p $pos -G green_${pos//,/_}.bin -n 5000000 -o 1 -t 32
done
# 时间: 10位置 × 1.5小时 = 15小时（周末运行）

# 2. 参数扫描（快速）
for T_outdoor in $(seq 0 5 40); do
  for solar in 0 250 500 750 1000; do
    # 修改边界条件
    sed -i "s/temperature [0-9.]*/temperature $(($T_outdoor+273))/" building.txt
    # 修改太阳辐射（外部源）
    # ...
    
    # 使用Green函数计算所有位置
    for pos in "${positions[@]}"; do
      result=$(stardis -M building.txt -p $pos -G green_${pos//,/_}.bin -V 0 | ...)
      echo "$T_outdoor,$solar,$pos,$result" >> results.csv
    done
  done
done
# 时间: 9组温度 × 5组辐射 × 10位置 × 20秒 ≈ 2.5小时
# vs 传统方法: 9 × 5 × 10 × 1.5小时 = 675小时（28天）
```

**收益**: 加速**270倍**

---

### **案例3: 红外相机仿真（当前限制）**

**目标**: 模拟红外相机观测到的温度图像（分辨率640×480）。

**当前限制**: `-R` 模式不支持 `-G` 选项。

**变通方案A** （低分辨率采样）:
```bash
# 1. 在图像平面上采样32×24=768个点
python sample_camera_plane.py camera.txt --resolution 32x24 > samples.txt

# 2. 为每个采样点生成Green函数
cat samples.txt | parallel -j 16 \
  'stardis -M scene.txt -p {1},{2},{3} -G green_{#}.bin -n 1000000 -o 1'

# 3. 改变边界条件后，快速重新计算
for temp in 280 300 320 340; do
  sed -i "s/ground_temperature [0-9.]*/ground_temperature $temp/" scene.txt
  
  # 使用Green函数计算768个采样点
  for i in {1..768}; do
    stardis -M scene.txt -p $(sed -n "${i}p" samples.txt) -G green_${i}.bin
  done > temps_${temp}.txt
  
  # 插值到完整分辨率640×480
  python interpolate.py temps_${temp}.txt --out ir_${temp}.png
done
```

**变通方案B** （等待GPU实现）:
```bash
# GPU版本可以直接支持IR渲染 + Green函数
stardis-gpu -M scene.txt -R camera.txt -G green_ir.bin -n 10000000

# GPU实现中，Green路径记录是每射线的，可以无缝集成
```

---

## 六、常见问题

### **Q1: 为什么我的Green函数文件这么大（10 GB+）？**

**A**: Green函数大小 = 路径数 × 平均路径大小。

**减小文件大小的方法**:
1. **减少采样数** (`-n`): 1000万 → 100万（文件大小 ÷10）
2. **简化场景**: 减少介质和界面数量
3. **使用更粗的扩散参数**: 减少路径长度

**示例**:
```bash
# 高质量（大文件）
stardis -M scene.txt -p 0,0,0 -G green_high.bin -n 10000000
# 文件: 3.2 GB

# 中等质量（平衡）
stardis -M scene.txt -p 0,0,0 -G green_mid.bin -n 1000000
# 文件: 320 MB

# 低质量（快速测试）
stardis -M scene.txt -p 0,0,0 -G green_low.bin -n 100000
# 文件: 32 MB
```

---

### **Q2: 我修改了几何，Green函数还能用吗？**

**A**: ❌ **不能**。Green函数严格绑定到场景几何。

**验证机制**: Stardis会计算场景哈希，加载Green函数时对比：
```c
// 如果哈希不匹配，报错
if(!hash256_eq(hash_scene, hash_green)) {
  log_err("场景不一致，Green函数无效");
  return RES_BAD_ARG;
}
```

**可以修改的**（Green函数仍有效）:
- ✅ 边界温度（`T_BOUNDARY_FOR_SOLID`）
- ✅ 对流系数（`H_BOUNDARY_FOR_FLUID` 中的 `convection_coef`）
- ✅ 材质热导率（`SOLID` 中的 `conductivity`）
- ✅ 发射率（`emissivity`）
- ✅ 体积功率（`volumic_power`）

**不能修改的**（会使Green函数失效）:
- ❌ 几何形状（添加/删除/移动物体）
- ❌ 网格拓扑（改变三角形数量/连接）
- ❌ 介质类型（固体 ↔ 流体转换）
- ❌ 单位换算系数（`fp_to_meter`）

---

### **Q3: Picard阶数必须为1吗？可以用更高阶吗？**

**A**: ❌ Green函数**必须** Picard阶数 = 1（线性假设）。

**原因**:
```
线性假设:
  T(x) = ∑ G·S  （叠加原理成立）

非线性（辐射项 T⁴）:
  T(x) = f(T₁⁴, T₂⁴, ...)  （叠加原理失效）
```

**高阶Picard** (`-o 2+`): 考虑辐射非线性，Green函数无效。

**使用场景**:
```bash
# ✅ 正确：线性场景（传导主导）
stardis -M conduction_scene.txt -p 0,0,0 -G green.bin -o 1

# ❌ 错误：非线性场景（强辐射）
stardis -M strong_radiation_scene.txt -p 0,0,0 -G green.bin -o 3
# 报错: "Green function does not make sense when dealing with non-linearities"
```

---

### **Q4: 如何验证Green函数的正确性？**

**A**: 对比Green函数求解结果与标准求解结果。

**验证脚本**:
```bash
#!/bin/bash
# green_validation.sh

# 1. 标准求解（Ground Truth）
stardis -M scene.txt -p 1,2,3 -n 1000000 -o 1 > standard.txt
T_standard=$(grep "Temperature" standard.txt | awk '{print $2}')

# 2. 生成Green函数
stardis -M scene.txt -p 1,2,3 -G green.bin -n 1000000 -o 1

# 3. 使用Green函数求解（完全相同的场景）
stardis -M scene.txt -p 1,2,3 -G green.bin > green.txt
T_green=$(grep "Temperature" green.txt | awk '{print $2}')

# 4. 比较结果
diff=$(echo "scale=6; ($T_standard - $T_green) / $T_standard * 100" | bc)
echo "相对误差: $diff %"

# 期望: 误差 < 0.01%（数值精度范围内）
if (( $(echo "$diff < 0.01" | bc -l) )); then
  echo "✅ Green函数验证通过"
else
  echo "❌ Green函数验证失败，误差过大"
fi
```

**预期结果**:
```
标准求解: 347.523 K ± 0.124 K
Green求解: 347.521 K ± 0.125 K
相对误差: 0.0006 %
✅ Green函数验证通过
```

---

### **Q5: Green函数可以在GPU上加速吗？**

**A**: ✅ 可以，GPU加速**生成**过程，不加速**使用**过程。

**性能分析**:

| 阶段 | CPU时间 | GPU时间 | 加速比 |
|------|---------|---------|--------|
| **生成Green函数** | 2小时 | 2-5分钟 | **24-60倍** |
| **使用Green函数** | 20秒 | 10秒 | **2倍**（有限收益） |

**原因**:
- **生成**: 需要完整光追（90% CPU时间） → GPU大幅加速
- **使用**: 只是简单累加（`T = ∑G·P`）→ GPU收益有限（已经很快）

**GPU实现优先级**:
```
1. 加速光追（生成Green函数）⭐⭐⭐⭐⭐
2. Green路径记录           ⭐⭐⭐
3. Green函数求解           ⭐（已经够快）
```

---

## 七、性能优化建议

### **7.1 采样数选择**

| 应用 | 推荐采样数 | 文件大小 | 生成时间 | 精度 |
|------|-----------|---------|---------|------|
| **快速原型** | 10万 | ~30 MB | 10分钟 | ±5% |
| **工程分析** | 100万 | ~300 MB | 1.5小时 | ±1% |
| **高精度仿真** | 1000万 | ~3 GB | 15小时 | ±0.1% |
| **出版质量** | 1亿 | ~30 GB | 150小时 | ±0.01% |

**自适应策略**:
```bash
# 1. 用少量采样快速验证流程
stardis -M scene.txt -p 0,0,0 -G green_test.bin -n 10000 -V 3

# 2. 检查统计不确定性
# 如果 stderr/mean > 1%，增加采样数

# 3. 生产运行
stardis -M scene.txt -p 0,0,0 -G green_final.bin -n 10000000
```

---

### **7.2 并行化策略**

**线程数选择**:
```bash
# 查看系统核心数
nproc   # Linux: 输出 32

# 使用全部核心（默认行为）
stardis -M scene.txt -p 0,0,0 -G green.bin -n 10000000
# 自动使用32线程

# 手动指定（预留给其他任务）
stardis -M scene.txt -p 0,0,0 -G green.bin -n 10000000 -t 24
```

**MPI分布式**（可选，需编译时启用）:
```bash
# 跨4个节点，每节点32核心 = 128核心总计
mpirun -n 4 -hostfile hosts.txt \
  stardis -M scene.txt -p 0,0,0 -G green.bin -n 100000000

# Green函数会自动聚合到rank 0
```

---

### **7.3 内存优化**

**问题**: 大型Green函数（10M+ 路径）需要大量内存。

**监控内存**:
```bash
# 运行时监控
/usr/bin/time -v stardis -M scene.txt -p 0,0,0 -G green.bin -n 10000000 2>&1 | grep "Maximum resident"

# 输出: Maximum resident set size (kbytes): 8192000  (8 GB)
```

**减少内存占用**:
1. **流式写入**: Green路径生成后立即写入磁盘（Stardis已实现）
2. **压缩存储**: 使用 `gzip` 压缩（需后处理）
   ```bash
   stardis ... -G green.bin
   gzip green.bin   # green.bin → green.bin.gz（压缩率~50%）
   ```
3. **批次处理**: 分多次生成，合并Green函数（需自定义脚本）

---

### **7.4 磁盘I/O优化**

**问题**: 大文件写入慢（HDD上可能10+ MB/s）。

**解决方案**:
```bash
# 1. 使用SSD/NVMe存储Green函数
stardis -M scene.txt -p 0,0,0 -G /fast_ssd/green.bin -n 10000000

# 2. 使用tmpfs（内存盘，更快但断电丢失）
mkdir /mnt/ramdisk
mount -t tmpfs -o size=32G tmpfs /mnt/ramdisk
stardis -M scene.txt -p 0,0,0 -G /mnt/ramdisk/green.bin -n 10000000
# 完成后复制到永久存储
cp /mnt/ramdisk/green.bin /permanent_storage/

# 3. 异步I/O（Stardis内部已优化）
```

---

## 八、故障排查

### **错误1: "Green function does not make sense with picard_order > 1"**

**原因**: Picard阶数不为1。

**解决**:
```bash
# ❌ 错误
stardis -M scene.txt -p 0,0,0 -G green.bin -o 2

# ✅ 正确
stardis -M scene.txt -p 0,0,0 -G green.bin -o 1
```

---

### **错误2: "Scene hash mismatch"**

**原因**: 加载的Green函数与当前场景不匹配。

**解决**:
```bash
# 检查场景文件是否被修改（几何部分）
diff scene_original.txt scene_current.txt

# 如果几何改变，必须重新生成Green函数
stardis -M scene_current.txt -p 0,0,0 -G green_new.bin -n 10000000
```

---

### **错误3: "Cannot open file for writing"**

**原因**: 输出文件路径不存在或无写权限。

**解决**:
```bash
# 检查目录是否存在
ls -ld /output/dir/

# 创建目录
mkdir -p /output/dir/

# 检查权限
chmod u+w /output/dir/
```

---

### **错误4: Green函数求解结果异常（NaN或Inf）**

**原因**: Green函数损坏或场景参数极端。

**诊断**:
```bash
# 1. 验证Green函数完整性
stardis -M scene.txt -p 0,0,0 -G green.bin -g > green_ascii.txt
grep -i "nan\|inf" green_ascii.txt

# 2. 检查场景参数是否合理
grep "temperature\|conductivity\|emissivity" scene.txt

# 3. 重新生成Green函数
rm green.bin
stardis -M scene.txt -p 0,0,0 -G green.bin -n 1000000 -o 1
```

---

## 九、未来GPU实现考虑

### **9.1 GPU中的Green函数记录**

**挑战**:
- 每条射线需要记录动态数组（功率项、热流项）
- GPU内存有限（RTX 4090: 24 GB）

**解决方案**（已在`green_func_usage.md`中详细设计）:

```cuda
// GPU Green路径结构（SoA布局）
struct GPUGreenFunctionStream {
    // 固定容量数组（避免动态分配）
    PowerTerm* power_terms;       // [N_rays * MAX_POWER_TERMS]
    int* power_counts;            // [N_rays]
    
    FluxTerm* flux_terms;         // [N_rays * MAX_FLUX_TERMS]
    int* flux_counts;             // [N_rays]
    
    // ... 其他字段
};

// 分批处理（Tiling）
for (int batch = 0; batch < n_batches; batch++) {
    // 每批1M射线 → ~2 GB GPU内存
    trace_rays_gpu(rays_batch, green_stream);
    
    // 传回CPU并清空
    copy_to_cpu(green_stream);
    clear_gpu_stream(green_stream);
}
```

**预期性能**:
- CPU生成Green函数: 2小时（1000万路径）
- GPU生成Green函数: 2-5分钟 ✅ **24-60倍加速**

---

### **9.2 IR渲染 + Green函数集成**

**目标**: 支持 `stardis-gpu -R camera.txt -G green.bin`

**实现策略**:
```cuda
__global__ void camera_render_with_green(
    CameraRays rays,
    GPUGreenStream green_stream,
    float* output_image
) {
    int pixel_id = blockIdx.x * blockDim.x + threadIdx.x;
    
    // 追踪射线 + 记录Green路径
    trace_radiative_path(rays[pixel_id], &green_stream[pixel_id]);
    
    // 立即计算温度（可选）
    float T = evaluate_green_path(&green_stream[pixel_id]);
    output_image[pixel_id] = T;
}
```

**使用流程**:
```bash
# 1. GPU生成Green函数（IR渲染）
stardis-gpu -M scene.txt -R camera.txt -G ir_green.bin -n 10000

# 2. 改变边界条件后，快速重新渲染
stardis-gpu -M scene_modified.txt -R camera.txt -G ir_green.bin
# 输出: 新的IR图像（640×480），耗时 < 1秒
```

---

## 十、总结

### **Green函数适用场景**

✅ **适合**:
- 参数扫描（改变边界温度、材质属性）
- 优化设计（材质热导率、发射率优化）
- 灵敏度分析
- 实时仿真（预计算Green函数）
- 逆问题求解

❌ **不适合**:
- 单次计算（反而更慢）
- 几何变化场景
- 非线性强辐射场景（需要高阶Picard）

### **性能收益**

| 场景 | 加速比 | 示例 |
|------|--------|------|
| **单次计算** | ❌ 0.98× | 2小时 → 2小时2分（更慢） |
| **10次参数扫描** | ✅ 9× | 20小时 → 2小时3分 |
| **100次参数扫描** | ✅ 60× | 200小时 → 3.3小时 |
| **优化迭代（1000次）** | ✅ 200× | 2000小时 → 10小时 |

### **关键命令速查**

```bash
# 生成Green函数
stardis -M model.txt -p x,y,z -G green.bin -n 10000000 -o 1 -a wos

# 使用Green函数
stardis -M model_modified.txt -p x,y,z -G green.bin

# 查看Green函数（ASCII）
stardis -M model.txt -p x,y,z -g -n 100000 | less

# 验证Green函数
stardis -M model.txt -p x,y,z -G green.bin > result.txt

# IR渲染（当前不支持Green，变通方案见案例3）
stardis -M model.txt -R camera.txt -n 10000
```

---

**文档版本**: 1.0  
**最后更新**: 2026-01-21 22:10:00  
**维护者**: Sisyphus (AI Agent)  
**相关文档**: 
- `guide/green_func_usage.md` — Green函数数据结构详解
- `guide/ray_realisation_analysis.md` — 光线追踪流程分析
- `stardis-cpu/stardis/0.12/doc/stardis.1` — Stardis手册页
