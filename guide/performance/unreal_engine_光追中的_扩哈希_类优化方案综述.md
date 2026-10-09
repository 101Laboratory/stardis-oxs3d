# Unreal Engine 光线追踪中的“扩哈希”类优化方案（工程视角）

> 本文从**“递归并行 + 扩哈希（记忆化）”**的角度，对 Unreal Engine（UE4/UE5）中实时光线追踪相关的核心优化方案进行统一抽象与归纳，帮助理解这些 Feature 在算法层面的共同思想。

---

## 1. 背景：为什么实时光追必须“扩哈希”

实时光线追踪面对的问题本质是：

- 路径追踪是**递归算法**
- GPU 上是**大规模并行**
- 大量光线在**空间、方向、时间上高度相似**

如果不做缓存与复用：

- 同一类子路径会被指数级重复计算
- 并行度越高，浪费越严重

因此 UE 的核心策略不是“算得更快”，而是：

> **尽量让同一类递归子问题只算一次，其余全部复用**

这正是“扩哈希 / 记忆化”的工程体现。

---

## 2. 总体分层结构

UE 中与光追相关的“扩哈希”优化可以按层次理解为：

| 层级 | 抽象层 | 代表 Feature |
|---|---|---|
| L0 | 几何层 | TLAS / BLAS / BVH |
| L1 | 路径调度层 | Ray Sorting / SER / Wave Ops |
| L2 | 表面光照层 | Lumen Surface Cache |
| L3 | 时序复用层 | ReSTIR / History Buffer |
| L4 | 空间采样层 | Radiance Cache / Probe Grid |

下面逐层说明。

---

## 3. L0：BVH（TLAS / BLAS）——最底层的“隐式哈希”

### 对应 Feature

- TLAS（Top-Level Acceleration Structure）
- BLAS（Bottom-Level Acceleration Structure）

### 扩哈希视角理解

- **Key**：空间包围盒（节点）
- **Value**：潜在命中几何体集合

作用：

- 避免每条光线与所有三角形求交
- 将大量重复的几何查询折叠为一次节点访问

> BVH 本质上是一个结构化的空间哈希，而非简单的树。

---

## 4. L1：Ray Coherence 与 Shader 重排（路径状态合并）

### 对应 Feature

- Ray Sorting / Ray Batching
- Wave-level Operations（HLSL Wave Ops）
- Shader Execution Reordering（SER，UE5.3+）

### 扩哈希视角理解

- **Key**：命中 Shader + 材质状态 + Bounce 类型
- **Value**：一次合并执行的着色结果

作用：

- 将“逻辑等价”的递归路径合并执行
- 避免 warp / wave 内大量分支发散

> 这是“递归并行 → 状态聚合”的硬件级实现。

---

## 5. L2：Lumen Surface Cache（最典型的记忆化）

### 对应 Feature

- Lumen Surface Cache
- Mesh Cards / Surface Cards

### 哈希结构

- **Key**：
  - 世界空间位置（Card）
  - 法线方向
  - 材质 ID / 粗糙度
- **Value**：
  - 多方向入射辐射度（GI / AO / Radiance）

### 作用

- 次级、三级反射不再递归 trace
- 直接查表返回光照结果

> 这是标准的“递归终止 + 子问题记忆化”。

---

## 6. L3：Temporal Reuse（时间维度的扩哈希）

### 6.1 ReSTIR（DI / GI）

#### 对应 Feature

- ReSTIR Direct Illumination
- ReSTIR Global Illumination（UE5.3+）

#### 哈希视角

- **Key**：
  - 命中点位置
  - 法线
  - 光源 / 路径状态
- **Value**：
  - Reservoir（加权样本集合）

> 上一帧的高质量样本 = 当前帧的子问题解

---

### 6.2 History Buffer / TSR

- GI History
- Reflection History
- Shadow History

作用：

- 时间上的结果缓存
- 用于降噪与稳定

> 本质是“跨帧哈希表”。

---

## 7. L4：Radiance Cache / Probe Grid（空间近似哈希）

### 对应 Feature

- Lumen Radiance Cache
- Screen Probe Gathering
- World-space Probe Grid

### 哈希特征

- **Key**：
  - 世界空间 Cell
  - 方向 Cone / Bucket
- **Value**：
  - 积分辐射度

特点：

- Key 是“近似等价类”而非精确状态
- 多像素、多路径共享结果

---

## 8. 统一抽象总结

从算法角度看，UE 的实时光追可以统一描述为：

> **带多层级缓存的递归路径追踪系统**

| UE 术语 | 算法本质 |
|---|---|
| Cache | 记忆化（Memoization） |
| Reuse | 哈希命中 |
| History | 时间维度扩哈希 |
| Reservoir | 哈希桶 |
| Probe / Card | 空间 Key 压缩 |
| SER | 状态级合并执行 |

---

## 9. 对研究与工程的启示

- 实时 ≠ 更精确
- 实时 = **递归 + 哈希 + 近似等价类**

这一思想同样适用于：

- 红外路径追踪
- 热输运耦合
- 多物理场实时仿真

---

## 10. 一句话结论

> Unreal Engine 的实时光线追踪通过在几何、路径、光照与时间多个层级引入缓存与复用机制，将原本指数复杂度的递归路径追踪问题转化为可控的、近似记忆化并行计算问题。
