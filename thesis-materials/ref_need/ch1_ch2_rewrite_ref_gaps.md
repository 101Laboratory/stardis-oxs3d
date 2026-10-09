# Ch1 & Ch2 重写引用缺口分析

> 生成日期：2026-03-30  
> 最后更新：2026-03-30（完整 Zotero 库盘点后修订）  
> 背景：Ch1/Ch2 计划全部重写；目标总引用量70-80条。

---

## Zotero 库现有文献盘点（2026-03-30 真实状态）

> EOIR 核心集合共 **13个子集合，约90条去重文献**。

| Zotero 集合 | Key | 条数 | 主题 |
|-------------|-----|------|------|
| 红外特征建模 | HFGRFYUY | 28 | 大气传输(MODTRAN/LOWTRAN)、签名预测工具(MuSES/DIRSIG/CAMEO-SIM)、早期 IR 场景生成方法 |
| 集成系统 | YDX3ET5E | 19 | 国内外 GPU 实时 IR 仿真框架，含 OptiX IR 应用（徐非凡 2022） |
| 蒙卡热输运方法 | GLN8YZG3 | 10 | MC 辐射传输经典(Howell 1964/1969)、null-collision(Galtier 2013)、MC 全局光照(Szirmay-Kalos 2000) |
| WoS 子集合 | NSHDVQP4 | 3 | Walk-on-Spheres(Muller 1956)、Grid-free MC PDE 综述(Sawhney 2025)、逆辐射传输(Liu 2025) |
| 耦合热输运 | NRNA26NB | 6 | **核心框架**：Bati 2023、Tregan 2023、Caliot 2024、Fournier 2016、Villefranque 2019/2022 |
| Wavefront | 4DD8IT8H | 9 | Laine 2013 Wavefront 原论文、GPU 调度算法(Kerbl 2017, Troendle 2019)、Work-stealing(Blumofe 1999) |
| 光追实现 | 3YKPYUFJ | 6 | Möller-Trumbore 1997、Woop 2013 watertight、Zhu 2022 RTNN、Evangelou 2021 BVH 半径搜索 |
| 应用 | VLXCEIT9 | 4 | 坦克/装甲目标与地面背景 IR 耦合仿真 |
| 质量评估 | W8LNS3M7 | 5 | IR 仿真图像保真度评价、MC 渲染误差分析 |
| 渲染基础 | GRWSSFB3 | 1 | **Kajiya 1986 渲染方程** |
| GPU | AR82FDKB | 2 | **Aila & Laine 2009 GPU BVH 遍历**、NVIDIA Ada GPU 架构白皮书 |
| optix | GKQIN25N | 2 | **OptiX Parker 2010**、CUDA Kirk 2007 |
| embree | ZY524LFB | 2 | **Embree Wald 2014**、Embree SIGGRAPH 2016 talks |
| 蒙卡基础 | PUVQAIDK | 1 | **Kac 1947 Feynman-Kac 公式** |
| **合计（去重）** | — | **≈ 90** | — |

---

## 已确认可用文献（关键文献状态速查）

> 以 Zotero Key 标注，可直接通过 BBT 导出 BibTeX。

| 章节用途 | 文献 | Zotero 集合 | Key | 状态 |
|---------|------|------------|-----|------|
| §2.2.1 MC方法基础 | Kac 1947 — Random walk & Brownian motion | PUVQAIDK | `XGFPD8UD` | ✅ 已有 |
| §2.2.1 MC辐射传输经典 | Howell 1964/1969 — MC heat transfer | GLN8YZG3 | `UYGGVAGP`/`KBXKXAQP` | ✅ 已有 |
| §2.2.1 MC全局光照 | Szirmay-Kalos 2000 — MC methods in global illumination | GLN8YZG3 | `TBEFPJPM` | ✅ 已有（含重要性采样） |
| §2.2.2 WoS传导随机游走 | Muller 1956 — Walk on Spheres | NSHDVQP4 | `HL7D8LS3` | ✅ 已有 |
| §2.2.2 WoS前沿综述 | Sawhney 2025 — Grid-free MC for PDEs | NSHDVQP4 | `C656SZNW` | ✅ 已有 |
| §2.2.3 渲染方程 | Kajiya 1986 — The Rendering Equation | GRWSSFB3 | `KF8YG8XD` | ✅ 已有 |
| §2.2.3 Null-collision | Galtier 2013 — Integral null-collision MC | GLN8YZG3 | `E85R5R5L` | ✅ 已有 |
| §2.2.4 耦合热输运核心 | Bati 2023 — Single path-space coupling | NRNA26NB | `NRCB53QN` | ✅ 已有 |
| §2.2.4 耦合理论形式化 | Tregan 2023 — Feynman-Kac框架 | NRNA26NB | `QGI93KSN` | ✅ 已有 |
| §2.2.4 城市耦合应用 | Caliot 2024 — 城市几何 MC 耦合 | NRNA26NB | `JMWMEL8H` | ✅ 已有 |
| §2.3 Wavefront | Laine 2013 — Megakernels considered harmful | 4DD8IT8H | `CLAYGS7B` | ✅ 已有 |
| §2.4 OptiX | Parker 2010 — OptiX ray tracing engine | GKQIN25N | `7J2L2NTF` | ✅ 已有 |
| §2.4 CUDA | Kirk 2007 — NVIDIA CUDA software & GPU parallel arch. | GKQIN25N | `9FJDGIQL` | ✅ 已有 |
| §2.4 Embree | Wald 2014 — Embree CPU ray tracing | ZY524LFB | `9EX3ET7E` | ✅ 已有 |
| §2.4 BVH GPU遍历 | Aila & Laine 2009 — Ray traversal on GPUs | AR82FDKB | `6ZNDGF9H` | ✅ 已有 |
| §2.4 RT Core架构 | NVIDIA Ada GPU arch. whitepaper | AR82FDKB | `2HRHZ9H2` | ✅ 已有（无PDF，仅URL） |
| §2.4 射线-三角求交 | Möller 1997 — Fast ray-triangle intersection | 3YKPYUFJ | `93MCD22G` | ✅ 已有 |
| §2.4 RT Core近邻搜索 | Zhu 2022 — RTNN hardware RT for neighbor search | 3YKPYUFJ | `I9KXW5NS` | ✅ 已有 |

---

## 剩余真实缺口

### 缺口一：MC 方法奠基文献（§2.2.1，缺1-2篇，P1）

**问题**：Kac 1947 已有，但 §2.2.1 需要交代 MC 方法本身的统计学奠基背景（"统计抽样估计积分"的方法论根基）。

| 文献 | 用途 | 缺失原因 |
|------|------|---------|
| **Metropolis & Ulam 1949**（"The Monte Carlo Method", JASA）| MC 方法命名与奠基，支撑"基于统计采样的积分估计" | ❌ 库中无 |
| Hammersley & Handscomb 1964（《Monte Carlo Methods》书） | MC 理论系统化，支撑"维度无关收敛"（可选） | ❌ 库中无 |

> **补充策略**：在 Zotero 中通过 DOI `10.1080/01621459.1949.10483310` 导入 Metropolis & Ulam 1949（JASA 44:335-341）。

---

### 缺口二：MIS / 重要性采样（§2.2.3，缺1篇，P2）

**问题**：Szirmay-Kalos 2000 覆盖 MC 全局光照含重要性采样综述，但无精确的 MIS 原始论文引用。论文若声明"路径追踪使用多重重要性采样"则需要 Veach 1997。

| 文献 | 用途 | 缺失原因 |
|------|------|---------|
| **Veach 1997**（博士论文/SIGGRAPH 1995 MIS）| MIS 方法原始论文 | ❌ 库中无 |

> **操作建议**：若论文仅需宽泛引用重要性采样，可以 Szirmay-Kalos 2000 代替。若需精确 MIS 引用，从 Zotero 通过 DOI 导入 Veach 1995（`10.1145/218380.218468`）。

---

### 缺口三：Null-collision 原始来源（§2.2.3，缺1篇，P2）

**问题**：Galtier 2013 已有（null-collision 积分公式化），但 Woodcock 1965（delta tracking 的原始工程文献）是方法溯源链的起点，学术规范要求引用。

| 文献 | 用途 | 缺失原因 |
|------|------|---------|
| **Woodcock 1965**（AEEW-M 1108 技术报告）| Delta tracking / null-collision 方法原始来源 | ❌ 库中无（灰色文献） |
| Novak 2018（"Monte Carlo Methods for Volumetric Light Transport Simul.", CGF）| Null-collision 综述，可替代 Woodcock 的"工程来源" | ❌ 库中无 |

> **操作建议**：优先导入 Novak 2018（`10.1111/cgf.13383`，可访问 CGF），作为 null-collision 综述引用；Woodcock 1965 为灰色文献，可从 OECD NEA 数据库获取扫描版。

---

### 缺口四：GPU SIMT 架构背景（§1.2.5/§2.3，缺1篇，P2）

**问题**：Kirk 2007（2 页会议短论文）过于简短，作为 SIMT 机制的主要引用依据不足。需要一篇更系统的 GPU 并行架构参考。

| 文献 | 用途 | 缺失原因 |
|------|------|---------|
| **Nickolls et al. 2008**（"Scalable Parallel Programming with CUDA", IEEE Micro）| SIMT 执行模型、warp 调度、线程发散系统介绍 | ❌ 库中无 |

> **注**：Kirk 2007 可继续作为 CUDA 技术引用；Nickolls 2008 用于支撑 SIMT/warp divergence 分析。DOI: `10.1109/MM.2008.22`。

---

### 缺口五：NVIDIA RT Core 架构白皮书（§2.4/Ch4，补充，P2）

**问题**：库中有 NVIDIA Ada GPU 架构白皮书（GeForce RTX 4000）但无 Turing 架构（GeForce RTX 2000/RTX 2080 Ti）白皮书——RT Core 硬件是在 Turing 中首次引入。Ada 能补充但 Turing 才是首发参考。

| 文献 | 用途 | 缺失原因 |
|------|------|---------|
| **NVIDIA Turing GPU Architecture Whitepaper**（2018）| RT Core 首次引入、BVH 硬件遍历原理 | ❌ 库中无（URL 已有的是 Ada） |

> URL: `https://images.nvidia.com/aem-dam/en-zz/Solutions/design-visualization/technologies/turing-architecture/NVIDIA-Turing-Architecture-Whitepaper.pdf`。在 Zotero 中以"Report"类型导入。

---

### 缺口六：应用领域文献（§1.1，缺4-6篇，P3）

§1.1 需要在军事目标探测以外补充其他领域（工业 NDT、医疗、建筑节能），以展示红外仿真的多元应用价值。库中目前所有应用文献都聚焦于军事目标/背景。

| 场景 | 需要文献类型 | 建议搜索词 |
|------|------------|----------|
| 工业无损检测（NDT） | 主动热成像、锁相热成像（pulsed thermography） | "active thermography NDT" Maldague 2001 |
| 建筑热桥/能效 | 被动红外建筑检测 | "infrared thermography building energy" |
| 医疗热成像 | 红外医学成像综述 | "infrared thermography medical" Ring & Ammer 2012 |

> **优先级**：若 §1.1 只写军事+工业，可以仅补充 1-2 篇 NDT 应用；医疗和建筑可视论文写作取舍省略。

---

### 缺口七：BVH 构造算法（§2.4，缺1篇，P3）

**问题**：库中已有 BVH 遍历（Aila & Laine 2009）、使用（Embree Wald 2014），但 BVH 构造算法（SAH 表面积启发式）文献缺失，无法支撑 §2.4 对 BVH 建树原理的阐述。

| 文献 | 用途 | 缺失原因 |
|------|------|---------|
| Goldsmith & Salmon 1987 或 **Wald et al. 2007**（SAH BVH 综述）| BVH SAH 构造理论 | ❌ 库中无 |

---

## 引用分配目标（更新后）

### Ch1 绪论（目标 ~40条引用）

| 节 | 主题 | 可用已有条数 | 仍需补充 |
|----|------|------------|---------|
| §1.1 | 应用场景（军事+工业+其他） | 军事 5-6 篇（红外特征建模集合） | 工业/医疗/建筑 2-4 篇 |
| §1.2.1 | 经验/半经验方法历史 | 12 篇（HFGRFYUY 集合） | 基本充足 |
| §1.2.2 | FEM/DOM/耦合数值方法 | 1 篇（Kottler 2019） | 1-2 篇 FEM 辐射-传导 |
| §1.2.3 | 全局光照与 GPU 红外仿真 | 3-4 篇（集成系统集合含 GPU 仿真、Coiro 2013） | 基本充足 |
| §1.2.4 | 路径追踪 MC 热输运（含 Bati 框架） | 8 篇（耦合热输运+WoS+GLN集合） | 基本充足 |
| §1.2.5 | GPU 并行/Wavefront 架构 | 3-4 篇（Laine 2013, Kirk 2007, Aila 2009） | Nickolls 2008 × 1 |
| §1.3 | 本文工作描述 | — | 自写，少量引用 |

### Ch2 理论基础（目标 ~35条引用）

| 节 | 主题 | 可用已有条数 | 仍需补充 |
|----|------|------------|---------|
| §2.1 | 热输运三机制理论 | 少量（Tregan 2023 含部分） | 传热学教材引用 1-2 篇 |
| §2.2.1 | MC 方法基础 | Kac 1947, Howell 1964/1969, Szirmay-Kalos 2000 | Metropolis 1949 × 1 |
| §2.2.2 | WoS 随机游走 | Muller 1956, Sawhney 2025, Kac 1947 | 充足 |
| §2.2.3 | 路径追踪+null-collision | Kajiya 1986, Galtier 2013, Szirmay-Kalos 2000 | Woodcock 1965 或 Novak 2018 × 1（可选） |
| §2.2.4 | 耦合热输运 MC 框架 | Bati 2023, Tregan 2023, Caliot 2024, Fournier 2016 | 充足 |
| §2.3 | Wavefront 执行模型 | Laine 2013, Kirk 2007, Nickolls 2008（待入库）| 充足（补入后） |
| §2.4 | 光追加速（BVH/OptiX/Embree） | Parker 2010, Wald 2014, Aila 2009, Möller 1997, Zhu 2022 | NVIDIA Turing 白皮书、BVH SAH × 1 |

---

## 优先级排序（修订后）

| 优先级 | 类别 | 文献 | DOI/获取方式 |
|--------|------|------|-------------|
| **P1** | MC 方法奠基 | Metropolis & Ulam 1949 | `10.1080/01621459.1949.10483310` |
| **P2** | SIMT/GPU 架构 | Nickolls et al. 2008 | `10.1109/MM.2008.22` |
| **P2** | Null-collision 综述 | Novak et al. 2018 (CGF) | `10.1111/cgf.13383` |
| **P2** | RT Core 首发白皮书 | NVIDIA Turing Arch. Whitepaper 2018 | URL 导入 |
| **P3** | MIS 精确引用 | Veach 1995 (SIGGRAPH) | `10.1145/218380.218468` |
| **P3** | 工业/医疗 IR 应用 | Maldague NDT / Ring & Ammer 医疗 | Google Scholar 搜索 |
| **P3** | BVH SAH 构造 | Wald 2007 或 Goldsmith 1987 | `10.1109/RT.2007.4342588` |
| **P4** | Delta tracking 溯源 | Woodcock 1965 (灰色文献) | OECD NEA 获取 |

---

## 引用分配目标（重写后）

### Ch1 绪论（目标 ~40条引用）

| 节 | 主题 | 目标引用数 |
|----|------|-----------|
| §1.1 | 应用场景（5领域各2-3篇） | 12-15 |
| §1.2.1 | 经验/半经验方法历史 | 10-12 |
| §1.2.2 | FEM/DOM/耦合数值方法 | 6-8 |
| §1.2.3 | 全局光照与GPU红外 | 5-6 |
| §1.2.4 | 路径追踪MC热输运（含Bati框架） | 8-10 |
| §1.2.5 | GPU并行/Wavefront架构 | 6-8 |
| §1.3 | 本文工作 | 2-3 |

### Ch2 理论基础（目标 ~35条引用）

| 节 | 主题 | 目标引用数 |
|----|------|-----------|
| §2.1 | 热输运三机制 | 5-6 |
| §2.2.1 | MC方法基础（Metropolis, Hammersley） | 4-5 |
| §2.2.2 | WoS随机游走解PDE（Kac, Muller, Sawhney） | 4-5 |
| §2.2.3 | 路径追踪框架（Kajiya, null-collision） | 5-6 |
| §2.2.4 | 耦合热输运MC框架（Bati, Tregan等） | 5-6 |
| §2.3 | Wavefront执行模型 | 5-6 |
| §2.4 | 光追加速技术（BVH, OptiX, Embree） | 6-8 |

---

## 已废弃的旧优先级排序

> 以下旧排序在2026-03-30盘点前生成，已被上方修订内容替代，保留供对照。

| 优先级 | 类别 | 需新增条数 | 摘要对应主张 |
|--------|------|-----------|-------------|
| P1（最高） | MC方法通论基础（Metropolis/Hammersley） | 2-3 | "积分形式的统计估计" |
| P1 | Walk on Spheres（Muller/Sawhney） | ~~2-3~~ 0 | **已有** Muller 1956 + Sawhney 2025 |
| P2 | GPU SIMT体系（Nickolls/Lindholm） | 1 | "递归结构阻断批量化"，Kirk 2007已有但不足 |
| P2 | null-collision体系（Woodcock/Novak） | 1 | Galtier 2013已有，补Novak 2018综述 |
| P3 | 应用领域文献（医疗/工业） | 2-4 | §1.1多领域应用价值 |
| P3 | RT Core硬件架构白皮书 | 1 | Turing白皮书缺（Ada已有） |
| P4 | Jordan-Brouwer定理数学引用 | 0 | **可省略**，Evangelou 2021 BVH已作数学支撑 |
| P4 | Veach 1997 重要性采样 | 1 | 路径追踪效率脉络（Szirmay-Kalos 2000可部分替代） |

---

*此文档在完整 Zotero 库盘点（2026-03-30）后修订。约90篇文献已在库，剩余缺口约6-8篇，补充后即可开始章节正文重写。*

*Zotero 库完整清单见同目录 [zotero-collections-inventory.md](./zotero-collections-inventory.md)*
