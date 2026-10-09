# `\textbf{}` 加粗滥用审计报告

**审计日期**: 2026-04-08  
**审计范围**: `Chapters/*.tex` 全部章节文件  

---

## 1. 总体统计

| 类别 | 数量 | 说明 | 是否需要修改 |
|------|------|------|:---:|
| 算法伪代码关键字 | 30 | `\textbf{return}`、`\textbf{if}`、`\textbf{else}` 等 algorithmicx 环境内标准用法 | ✗ |
| 表格标题/数据加粗 | 18 | `\multicolumn` 组标题、表格内强调数据 | ✗ |
| **正文中不当加粗** | **251** | 术语首现定义、概念强调、段落标签等 | **✓** |
| **合计** | **299** | — | **251 处需改** |

---

## 2. 按章节分布

| 章节文件 | 伪代码 | 表格 | 正文加粗 | 合计 |
|----------|:------:|:----:|:--------:|:----:|
| `0_2_Symbols.tex` | 0 | 4 | 0 | 4 |
| `1_Introduction.tex` | 0 | 0 | 3 | 3 |
| `2_Research.tex` | 10 | 0 | 82 | 92 |
| `3_ParallelizedSolver.tex` | 20 | 10 | 102 | 132 |
| `4_RTBackend.tex` | 0 | 4 | 38 | 42 |
| `5_GUI.tex` | 0 | 0 | 17 | 17 |
| `6_Conclusion.tex` | 0 | 0 | 9 | 9 |
| **合计** | **30** | **18** | **251** | **299** |

> 第三章（102 处）和第二章（82 处）是重灾区，合计占正文加粗总量的 73%。

---

## 3. 正文加粗的具体类型与修改建议

### 3.1 术语首现定义（~80 处）— 建议改为 `\emph{}`

将首次出现的专业术语用斜体标记，符合学术论文通行惯例。

**典型示例**：
| 文件 | 行 | 当前写法 | 修改为 |
|------|-----|---------|--------|
| `2_Research.tex` | 43 | `\textbf{傅里叶定律}` | `\emph{傅里叶定律}` |
| `2_Research.tex` | 50 | `\textbf{热传导方程}` | `\emph{热传导方程}` |
| `2_Research.tex` | 76 | `\textbf{Stefan-Boltzmann 定律}` | `\emph{Stefan-Boltzmann 定律}` |
| `2_Research.tex` | 80 | `\textbf{Kirchhoff 定律}` | `\emph{Kirchhoff 定律}` |
| `2_Research.tex` | 82 | `\textbf{辐射传输方程}` | `\emph{辐射传输方程}` |
| `2_Research.tex` | 92 | `\textbf{漫射灰体}` | `\emph{漫射灰体}` |
| `2_Research.tex` | 92 | `\textbf{参与性介质}` | `\emph{参与性介质}` |
| `2_Research.tex` | 94 | `\textbf{双向反射分布函数}` | `\emph{双向反射分布函数}` |
| `2_Research.tex` | 94 | `\textbf{Lambert（漫射）反射}` | `\emph{Lambert（漫射）反射}` |
| `2_Research.tex` | 107 | `\textbf{牛顿冷却定律}` | `\emph{牛顿冷却定律}` |
| `2_Research.tex` | 134 | `\textbf{联合能量平衡边界条件}` | `\emph{联合能量平衡边界条件}` |
| `2_Research.tex` | 145 | `\textbf{体积耦合}` | `\emph{体积耦合}` |
| `2_Research.tex` | 153 | `\textbf{蒙特卡洛路径追踪}` | `\emph{蒙特卡洛路径追踪}` |
| `2_Research.tex` | 181 | `\textbf{蒙特卡洛估计量}` | `\emph{蒙特卡洛估计量}` |
| `2_Research.tex` | 186 | `\textbf{维数灾难}` | `\emph{维数灾难}` |
| `2_Research.tex` | 193 | `\textbf{重要性采样}` | `\emph{重要性采样}` |
| `2_Research.tex` | 204 | `\textbf{正向蒙特卡洛追踪}` | `\emph{正向蒙特卡洛追踪}` |
| `2_Research.tex` | 204 | `\textbf{能量包}` | `\emph{能量包}` |
| `2_Research.tex` | 206 | `\textbf{辐射角系数}` | `\emph{辐射角系数}` |
| `2_Research.tex` | 215 | `\textbf{反向路径追踪}` | `\emph{反向路径追踪}` |
| `2_Research.tex` | 229 | `\textbf{渲染方程}` | `\emph{渲染方程}` |
| `2_Research.tex` | 238 | `\textbf{反向路径追踪}` | `\emph{反向路径追踪}` |
| `2_Research.tex` | 238 | `\textbf{俄罗斯轮盘赌}` | `\emph{俄罗斯轮盘赌}` |
| `2_Research.tex` | 240 | `\textbf{Null-collision 技术}` | `\emph{Null-collision 技术}` |
| `2_Research.tex` | 267 | `\textbf{统一到单一路径空间}` | `\emph{统一到单一路径空间}` |
| `2_Research.tex` | 267 | `\textbf{传热模态}` | `\emph{传热模态}` |
| `2_Research.tex` | 267 | `\textbf{模态转换}` | `\emph{模态转换}` |
| `2_Research.tex` | 283 | `\textbf{辐射段（RAD）}` | `\emph{辐射段}（RAD）` |
| `2_Research.tex` | 284 | `\textbf{传导段（CND）}` | `\emph{传导段}（CND）` |
| `2_Research.tex` | 289 | `\textbf{线性性}` | `\emph{线性性}` |
| `2_Research.tex` | 289 | `\textbf{时间回退}` | `\emph{时间回退}` |
| `2_Research.tex` | 290 | `\textbf{对流段（CNV）}` | `\emph{对流段}（CNV）` |
| `2_Research.tex` | 293 | `\textbf{模态转换}` | `\emph{模态转换}` |
| `2_Research.tex` | 318 | `\textbf{线性化}` | `\emph{线性化}` |
| `2_Research.tex` | 541 | `\textbf{Möller-Trumbore（MT）算法}` | `\emph{Möller-Trumbore（MT）算法}` |
| `2_Research.tex` | 552 | `\textbf{不满足水密性}` | `\emph{不满足水密性}` |
| `2_Research.tex` | 560 | `\textbf{射线对齐坐标系}` | `\emph{射线对齐坐标系}` |
| `2_Research.tex` | 564 | `\textbf{棱函数}` | `\emph{棱函数}` |
| `2_Research.tex` | 589 | `\textbf{层次包围体加速结构}` | `\emph{层次包围体加速结构}` |
| `2_Research.tex` | 591 | `\textbf{轴对齐包围盒}` | `\emph{轴对齐包围盒}` |
| `2_Research.tex` | 593 | `\textbf{面积启发式}` | `\emph{面积启发式}` |
| `2_Research.tex` | 598 | `\textbf{binning 近似}` | `\emph{binning 近似}` |
| `2_Research.tex` | 600 | `\textbf{栈式深度优先}` | `\emph{栈式深度优先}` |
| `2_Research.tex` | 621 | `\textbf{射线包（ray packet）}` | `\emph{射线包}（ray packet）` |
| `2_Research.tex` | 623 | `\textbf{多叉 BVH（MBVH）}` | `\emph{多叉 BVH}（MBVH）` |
| `2_Research.tex` | 643 | `\textbf{RT Core}` | `\emph{RT Core}` |
| `2_Research.tex` | 659 | `\textbf{管线（Pipeline）}` | `\emph{管线}（Pipeline）` |
| `2_Research.tex` | 659 | `\textbf{着色器绑定表（Shader Binding Table，SBT）}` | `\emph{着色器绑定表}（SBT）` |
| `3_ParallelizedSolver.tex` | 34 | `\textbf{耦合输运状态机 Wavefront 求解器}` | `\emph{耦合输运状态机 Wavefront 求解器}` |
| `3_ParallelizedSolver.tex` | 47 | `\textbf{挂起点}` | `\emph{挂起点}` |
| `3_ParallelizedSolver.tex` | 47 | `\textbf{序列化}` | `\emph{序列化}` |
| `3_ParallelizedSolver.tex` | 161 | `\textbf{Wavefront 执行范式}` | `\emph{Wavefront 执行范式}` |
| `3_ParallelizedSolver.tex` | 161 | `\textbf{路径池}` | `\emph{路径池}` |
| `3_ParallelizedSolver.tex` | 388 | `\textbf{路径池（Path Pool）}` | `\emph{路径池}（Path Pool）` |
| `3_ParallelizedSolver.tex` | 596 | `\textbf{双池 Ping-Pong 调度}` | `\emph{双池 Ping-Pong 调度}` |
| `4_RTBackend.tex` | 8 | `\textbf{射线-网格求交查询}` | `\emph{射线-网格求交查询}` |
| `4_RTBackend.tex` | 8 | `\textbf{最近面投影查询}` | `\emph{最近面投影查询}` |
| `4_RTBackend.tex` | 8 | `\textbf{封闭腔体成员查询}` | `\emph{封闭腔体成员查询}` |
| `4_RTBackend.tex` | 12 | `\textbf{自定义图元求交程序}` | `\emph{自定义图元求交程序}` |
| `4_RTBackend.tex` | 31 | `\textbf{程序类型（Program Groups）}` | `\emph{程序类型}（Program Groups）` |
| `4_RTBackend.tex` | 33 | `\textbf{加速结构（AS）}` | `\emph{加速结构}（AS）` |
| `4_RTBackend.tex` | 35 | `\textbf{Shader Binding Table（SBT）}` | `\emph{Shader Binding Table}（SBT）` |
| `4_RTBackend.tex` | 37 | `\textbf{Pipeline}` | `\emph{Pipeline}` |
| `5_GUI.tex` | 23 | `\textbf{StardisEditor}` | `\emph{StardisEditor}` |
| `5_GUI.tex` | 117 | `\textbf{Model-View 架构}` | `\emph{Model-View 架构}` |
| `5_GUI.tex` | 117 | `\textbf{PyQt5 信号/槽机制}` | `\emph{PyQt5 信号/槽机制}` |
| `5_GUI.tex` | 172 | `\textbf{边界归属的语义分析}` | `\emph{边界归属的语义分析}` |

### 3.2 概念/逻辑强调（~70 处）— 建议改为 `\emph{}` 或去除标记

正文中对概念的行内强调不应使用加粗，改为斜体或去除格式标记。

**典型示例**：
| 文件 | 行 | 当前写法 | 修改建议 |
|------|-----|---------|----------|
| `2_Research.tex` | 304 | `\textbf{固-流界面（S-F）耦合}` | `\emph{固-流界面（S-F）耦合}` |
| `2_Research.tex` | 327 | `\textbf{固-固界面（S-S）耦合}` | `\emph{固-固界面（S-S）耦合}` |
| `2_Research.tex` | 345 | `\textbf{界面分发}`、`\textbf{辐射子路径}` 等 | `\emph{}` |
| `2_Research.tex` | 437 | `\textbf{路径状态与执行控制流的隐式耦合}` | `\emph{}` |
| `2_Research.tex` | 457 | `\textbf{路径状态}`、`\textbf{状态推进}`、`\textbf{几何查询执行}` | `\emph{}` |
| `2_Research.tex` | 485 | `\textbf{持久化路径状态}`、`\textbf{挂起队列}`、`\textbf{CPU 推进阶段}`、`\textbf{后端执行阶段}` | `\emph{}` |
| `2_Research.tex` | 487 | `\textbf{将状态推进与查询执行分离，使批量查询自然涌现}` | `\emph{}` |
| `2_Research.tex` | 489 | `\textbf{RT 查询}`、`\textbf{ENC 查询}`、`\textbf{CP 查询}` | `\emph{}` |
| `2_Research.tex` | 503 | `\textbf{独有性能瓶颈}` | `\emph{}` |
| `2_Research.tex` | 571 | `\textbf{恰好一个}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 34 | `\textbf{状态推进}` `\textbf{几何查询执行}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 45 | `\textbf{正确性}` `\textbf{并行化设计}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 94 | `\textbf{递归调用栈中每一个...}` | `\emph{}`（长短语） |
| `3_ParallelizedSolver.tex` | 140 | `\textbf{物理必要性}` `\textbf{物理计算需求}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 144 | `\textbf{几何与传播状态}` `\textbf{物理路径参数}` 等5类字段 | `\emph{}` |
| `3_ParallelizedSolver.tex` | 150 | `\textbf{完全由算法的物理计算需求决定}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 374 | `\textbf{同构查询}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 504 | `\textbf{路径优先遍历优化}` 等 | `\emph{}` |
| `3_ParallelizedSolver.tex` | 626 | `\textbf{隐藏}` `\textbf{延迟隐藏}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 864 | `\textbf{Wavefront 架构改造不引入物理偏差}` | `\emph{}` |
| `3_ParallelizedSolver.tex` | 906 | `\textbf{固有代价}` `\textbf{解耦}` | `\emph{}` |
| `4_RTBackend.tex` | 10 | `\textbf{交点}` `\textbf{投影点}` | `\emph{}` |
| `4_RTBackend.tex` | 10 | `\textbf{重构为等价的光追问题}` `\textbf{正确性依赖几何拓扑论证}` | `\emph{}` |
| `4_RTBackend.tex` | 57 | `\textbf{查询映射}`、`\textbf{语义等价}`、`\textbf{批量接口}`、`\textbf{上下文兼容}` | `\emph{}` |
| `4_RTBackend.tex` | 59 | `\textbf{映射平凡}`、`\textbf{映射非平凡}`、`\textbf{映射需要正确性论证}` | `\emph{}` |
| `4_RTBackend.tex` | 66 | `\textbf{非过滤多命中路径}` `\textbf{GPU 内联过滤路径}` | `\emph{}` |
| `4_RTBackend.tex` | 68 | `\textbf{批次规模}` | `\emph{}` |
| `4_RTBackend.tex` | 79 | `\textbf{交点}` `\textbf{投影点}` | `\emph{}` |
| `6_Conclusion.tex` | 11 | `\textbf{交点}` `\textbf{投影点}` | `\emph{}` |

### 3.3 段落/列表项标签（~50 处）— 建议改为 `\emph{}` 或去除标记

段落起始的编号标签或列表项标题不应使用 `\textbf{}`。

**典型示例**：
| 文件 | 行 | 当前写法 | 修改建议 |
|------|-----|---------|----------|
| `2_Research.tex` | 59 | `\textbf{第一类（Dirichlet）边界}` | 去除加粗或用 `\emph{}` |
| `2_Research.tex` | 60 | `\textbf{第二类（Neumann）边界}` | 同上 |
| `2_Research.tex` | 61 | `\textbf{第三类（Robin）边界}` | 同上 |
| `2_Research.tex` | 651-654 | `\textbf{Closest-Hit Shader}` 等4个 Shader 类型 | `\emph{}` |
| `2_Research.tex` | 711 | `\textbf{第一类：最近命中光追...}` | 去除加粗或用 `\emph{}` |
| `2_Research.tex` | 713 | `\textbf{第二类：最近面投影...}` | 同上 |
| `2_Research.tex` | 715 | `\textbf{第三类：封闭腔体归属查询...}` | 同上 |
| `4_RTBackend.tex` | 87 | `\textbf{代理几何构建}` | 去除加粗 |
| `4_RTBackend.tex` | 89 | `\textbf{零长度查询射线}` | 去除加粗 |
| `4_RTBackend.tex` | 91 | `\textbf{自定义 Intersection Program}` | 去除加粗 |
| `4_RTBackend.tex` | 95 | `\textbf{完备性}` `\textbf{正确性}` `\textbf{有限搜索半径}` | 去除加粗 |
| `4_RTBackend.tex` | 120 | `\textbf{第一步——CP 批量查询}` `\textbf{第二步——RT 侧判定查询}` | 去除加粗 |
| `4_RTBackend.tex` | 130 | `\textbf{第二步的并发构造}` | 去除加粗 |
| `5_GUI.tex` | 13-21 | `\textbf{（a）位置参数无标签}` 等5个痛点标签 | 去除加粗或用 `\emph{}` |
| `5_GUI.tex` | 91-94 | `\textbf{包壳唯一性}` 等4个约束标签 | 去除加粗或用 `\emph{}` |
| `5_GUI.tex` | 209-212 | `\textbf{画刷}` `\textbf{洪泛填充}` 等4个工具标签 | 去除加粗或用 `\emph{}` |
| `6_Conclusion.tex` | 9 | `\textbf{（一）基于 Wavefront 架构的...}` | 去除加粗 |
| `6_Conclusion.tex` | 11 | `\textbf{（二）基于 OptiX 的...}` | 同上 |
| `6_Conclusion.tex` | 13 | `\textbf{（三）热输运场景仿真任务...}` | 同上 |
| `6_Conclusion.tex` | 21-27 | `\textbf{（一）全 GPU 求解器架构。}` 等4条展望 | 同上 |
| `1_Introduction.tex` | 161-165 | `（1）\textbf{基于有限状态机的...}` 等3条贡献 | 去除加粗 |

### 3.4 Wavefront/FSM 模块查询统计标注（~50 处）— 建议直接去除加粗

第三章中大量对查询数量的强调标注：

| 文件 | 行 | 当前写法 | 修改建议 |
|------|-----|---------|----------|
| `3_ParallelizedSolver.tex` | 219 | `\textbf{1 个 RT 查询}` | `1 个 RT 查询` |
| `3_ParallelizedSolver.tex` | 254 | `\textbf{1 个 RT 查询}` `\textbf{1 个 ENC 查询}` | 去除加粗 |
| `3_ParallelizedSolver.tex` | 256 | `\textbf{CP 查询}` `\textbf{RT 查询}` 等 | 去除加粗 |
| `3_ParallelizedSolver.tex` | 296 | `\textbf{1 个 RT 查询}` | 去除加粗 |
| `3_ParallelizedSolver.tex` | 319 | `\textbf{3 个 ENC 查询}` | 去除加粗 |
| `3_ParallelizedSolver.tex` | 321 | `\textbf{2 个 RT 查询}` | 去除加粗 |
| `3_ParallelizedSolver.tex` | 325 | `\textbf{4 条 RT}` `\textbf{1 个 ENC 查询}` | 去除加粗 |
| `3_ParallelizedSolver.tex` | 327 | `\textbf{3 个 RT 查询}` | 去除加粗 |
| `3_ParallelizedSolver.tex` | 345 | `\textbf{59 个细粒度状态}` | 去除加粗 |

### 3.5 阶段/Phase 标签（~6 处）— 建议改为 `\emph{}` 或 `\textsc{}`

| 文件 | 行 | 当前写法 |
|------|-----|---------|
| `3_ParallelizedSolver.tex` | 407 | `\textbf{最小调度循环}` |
| `3_ParallelizedSolver.tex` | 409 | `\textbf{Phase 1: \textsc{DistributeResults}` |
| `3_ParallelizedSolver.tex` | 424 | `\textbf{Phase 2: \textsc{AdvancePaths}` |
| `3_ParallelizedSolver.tex` | 446 | `\textbf{Phase 3: \textsc{HarvestDone}` |
| `3_ParallelizedSolver.tex` | 461 | `\textbf{Phase 4: \textsc{RefillSlots}` |
| `3_ParallelizedSolver.tex` | 478 | `\textbf{Phase 5: \textsc{CollectQueries}` |

### 3.6 验证/结论段落标签（~10 处）— 建议去除加粗

| 文件 | 行 | 当前写法 |
|------|-----|---------|
| `3_ParallelizedSolver.tex` | 761 | `\textbf{第一级——单模态稳态（纯传导）}` 等4级 |
| `3_ParallelizedSolver.tex` | 801 | `\textbf{逐像素双样本 $Z$ 检验}` |
| `3_ParallelizedSolver.tex` | 818 | `\textbf{Bonferroni 校正}` |
| `3_ParallelizedSolver.tex` | 820 | `\textbf{Benjamini-Hochberg (BH) 假发现率（FDR）校正}` |
| `3_ParallelizedSolver.tex` | 844 | `\textbf{PASS (strong)}` |
| `4_RTBackend.tex` | 247-253 | `\textbf{RT 查询}` `\textbf{CP 查询}` `\textbf{ENC 查询}` `\textbf{统一架构}` |

### 3.7 注释标号标签（3 处）— 建议去除加粗

| 文件 | 行 | 当前写法 |
|------|-----|---------|
| `3_ParallelizedSolver.tex` | 63 | `\textbf{[*]}` |
| `3_ParallelizedSolver.tex` | 90 | `\textbf{[A]}` |
| `3_ParallelizedSolver.tex` | 92 | `\textbf{[B]}` |

---

## 4. 修改策略总结

| 修改方法 | 适用场景 | 预计涉及处数 |
|----------|----------|:----------:|
| `\textbf{}` → `\emph{}` | 术语首现定义、概念强调 | ~150 |
| `\textbf{}` → 去除标记 | 查询数量统计、段落编号标签、注释标号 | ~80 |
| `\textbf{}` → `\emph{}` 或其他语义命令 | Phase 标签、验证方法标签 | ~20 |
| 保留不改 | 算法伪代码关键字、表格标题 | 48 |

---

## 5. 全量清单

以下为 251 处正文 `\textbf{}` 的完整逐行清单（按文件-行号排序）：

### `1_Introduction.tex`（3 处）
- L161: `\textbf{基于有限状态机的 Wavefront 耦合热输运求解算法。}`
- L163: `\textbf{三类异构几何查询的 GPU 等价映射方案。}`
- L165: `\textbf{基于 \texttt{stardis-input} DSL 的图形化场景编辑器 StardisEditor。}`

### `2_Research.tex`（82 处）
- L43: `\textbf{傅里叶定律}`
- L50: `\textbf{热传导方程}`
- L59: `\textbf{第一类（Dirichlet）边界}`
- L60: `\textbf{第二类（Neumann）边界}`
- L61: `\textbf{第三类（Robin）边界}`
- L76: `\textbf{Stefan-Boltzmann 定律}`
- L80: `\textbf{Kirchhoff 定律}`
- L82: `\textbf{辐射传输方程}`
- L92: `\textbf{漫射灰体}` / `\textbf{参与性介质}`
- L94: `\textbf{双向反射分布函数}` / `\textbf{Lambert（漫射）反射}`
- L107: `\textbf{牛顿冷却定律}`
- L134: `\textbf{联合能量平衡边界条件}`
- L145: `\textbf{体积耦合}`
- L153: `\textbf{蒙特卡洛路径追踪}`
- L181: `\textbf{蒙特卡洛估计量}`
- L186: `\textbf{维数灾难}`
- L193: `\textbf{重要性采样}`
- L204: `\textbf{正向蒙特卡洛追踪}` / `\textbf{能量包}`
- L206: `\textbf{辐射角系数}`
- L215: `\textbf{反向路径追踪}`
- L229: `\textbf{渲染方程}`
- L238: `\textbf{反向路径追踪}` / `\textbf{俄罗斯轮盘赌}`
- L240: `\textbf{Null-collision 技术}`
- L267: `\textbf{统一到单一路径空间}` / `\textbf{传热模态}` / `\textbf{模态转换}`
- L283: `\textbf{辐射段（RAD）}`
- L284: `\textbf{传导段（CND）}`
- L289: `\textbf{线性性}` / `\textbf{时间回退}`
- L290: `\textbf{对流段（CNV）}`
- L293: `\textbf{模态转换}`
- L304: `\textbf{固-流界面（S-F）耦合}`
- L318: `\textbf{线性化}`
- L327: `\textbf{固-固界面（S-S）耦合}`
- L345: `\textbf{界面分发}` / `\textbf{辐射子路径}` / `\textbf{传导子路径}` / `\textbf{对流子路径}`
- L437: `\textbf{路径状态与执行控制流的隐式耦合}`
- L457: `\textbf{路径状态}` / `\textbf{状态推进}` / `\textbf{几何查询执行}`
- L485: `\textbf{持久化路径状态}` / `\textbf{挂起队列}` ×2 / `\textbf{CPU 推进阶段}` / `\textbf{后端执行阶段}`
- L487: `\textbf{将状态推进与查询执行分离，使批量查询自然涌现}`
- L489: `\textbf{RT 查询}` / `\textbf{ENC 查询}` / `\textbf{CP 查询}`
- L503: `\textbf{独有性能瓶颈}`
- L541: `\textbf{Möller-Trumbore（MT）算法}`
- L552: `\textbf{不满足水密性}`
- L557: `\textbf{穿透失效}` / `\textbf{双重命中失效}` / `\textbf{低仰角红外成像}`
- L560: `\textbf{射线对齐坐标系}`
- L564: `\textbf{棱函数}`
- L571: `\textbf{恰好一个}`
- L589: `\textbf{层次包围体加速结构}`
- L591: `\textbf{轴对齐包围盒}`
- L593: `\textbf{面积启发式}`
- L598: `\textbf{binning 近似}`
- L600: `\textbf{栈式深度优先}`
- L621: `\textbf{射线包（ray packet）}`
- L623: `\textbf{多叉 BVH（MBVH）}`
- L643: `\textbf{RT Core}`
- L651-654: `\textbf{Closest-Hit Shader}` / `\textbf{Miss Shader}` / `\textbf{Any-Hit Shader}` / `\textbf{Intersection Shader}`
- L659: `\textbf{管线（Pipeline）}` / `\textbf{着色器绑定表（Shader Binding Table，SBT）}`
- L711: `\textbf{第一类：最近命中光追（Closest-Hit Ray Tracing，RT 查询）。}`
- L713: `\textbf{第二类：最近面投影（Closest Point Projection，CP 查询）。}`
- L715: `\textbf{第三类：封闭腔体归属查询（Point-in-Enclosure，ENC 查询）。}`

### `3_ParallelizedSolver.tex`（102 处）
- L34: `\textbf{耦合输运状态机 Wavefront 求解器}` / `\textbf{状态推进}` / `\textbf{几何查询执行}`
- L45: `\textbf{正确性}` / `\textbf{并行化设计}`
- L47: `\textbf{挂起点}` / `\textbf{序列化}`
- L63: `\textbf{[*]}`
- L90: `\textbf{[A]}`
- L92: `\textbf{[B]}`
- L94: `\textbf{递归调用栈中每一个几何查询调用点...}`
- L98: `\textbf{19 个}`
- L140: `\textbf{物理必要性}` / `\textbf{RT 查询}` / `\textbf{ENC 查询}` / `\textbf{CP 查询}` / `\textbf{物理计算需求}`
- L144: 5类字段各1处（几何与传播状态、物理路径参数、随机数状态、过程局部变量联合体、相位标识）+ `\textbf{完全等价}`
- L146: `\textbf{几何与传播状态}` / `\textbf{过程局部变量联合体}`
- L150: `\textbf{完全由算法的物理计算需求决定}`
- L157-158: `\textbf{前提 A（可挂起性）}` / `\textbf{前提 B（可聚合性）}`
- L161: `\textbf{Wavefront 执行范式}` / `\textbf{路径池}` / `\textbf{推进→收集→批量执行→回填}`
- L163: `\textbf{批量请求/批量响应接口边界}`
- L168: 4个模块名（辐射传播段、界面耦合事件、传导游走段、对流换热段）
- L219-345: 大量查询数量标注（约30处）
- L374-596: Wavefront 机制概念标注（约20处）
- L761-926: 验证和性能分析标签（约10处）

### `4_RTBackend.tex`（38 处）
- L8-12: 三类查询定义及核心论点（约10处）
- L31-37: OptiX 核心概念定义（4处）
- L57-59: 映射框架定义（约7处）
- L66-99: RT/CP 映射具体设计标注（约12处）
- L120-130: ENC 映射步骤标注（约5处）
- L247-253: 小结段落标签（4处）

### `5_GUI.tex`（17 处）
- L13-21: 5个痛点标签
- L23: StardisEditor 名称
- L91-94: 4个 DSL 约束标签
- L117: 2个架构概念
- L172: 边界归属的语义分析
- L209-212: 4个画笔工具标签

### `6_Conclusion.tex`（9 处）
- L9-13: 3条主要贡献标签
- L11: 2处概念强调（交点/投影点）
- L21-27: 4条展望标签

---

## 6. 执行建议

1. **全局替换优先级**：先处理 `2_Research.tex`（82处）和 `3_ParallelizedSolver.tex`（102处），覆盖 73% 的问题。
2. **批量替换策略**：可使用正则替换 `\\textbf\{([^}]+)\}` → `\\emph{$1}`，但需逐文件人工排除算法环境和表格内的合法用法。
3. **编译验证**：每章修改后执行 `latexmk` 编译确认无破坏。
4. **特殊处理**：
   - 嵌套 `\texttt{}` 的情况（如 L165 `\textbf{基于 \texttt{stardis-input}...}`）需手动处理。
   - 段落/列表标签中含句号的情况（如 `\textbf{（一）...。}`），去除加粗后确认排版无异。
   - Phase 标签含 `\textsc{}` 嵌套（如 L409 `\textbf{Phase 1: \textsc{DistributeResults}}`）需确认格式。
