# Zotero EOIR 文献库清单报告

> 生成时间：2025-01  
> 覆盖集合：9个子集合，共约 70+ 条目  
> 核心集合 EOIR Key: `MJF343F7`

---

## 目录

- [集合1：经验/半经验/第一性方法 (HFGRFYUY)](#集合1经验半经验第一性方法-hfgrfyuy)
- [集合2：集成系统 (YDX3ET5E)](#集合2集成系统-ydx3et5e)
- [集合3：蒙卡热输运方法 (GLN8YZG3)](#集合3蒙卡热输运方法-gln8yzg3)
- [集合4：WoS子集合 (NSHDVQP4)](#集合4wos子集合-nshdvqp4)
- [集合5：耦合热输运子集合 (NRNA26NB)](#集合5耦合热输运子集合-nrna26nb)
- [集合6：Wavefront (4DD8IT8H)](#集合6wavefront-4dd8it8h)
- [集合7：光追实现 (3YKPYUFJ)](#集合7光追实现-3ykpyufj)
- [集合8：应用 (VLXCEIT9)](#集合8应用-vlxceit9)
- [集合9：质量评估 (W8LNS3M7)](#集合9质量评估-w8lns3m7)
- [关键文献缺失核查](#关键文献缺失核查)

---

## 集合1：经验/半经验/第一性方法 (HFGRFYUY)

> 28 篇，涵盖大气传输模型(MODTRAN/LOWTRAN)、签名预测工具(MuSES/PRISM/DIRSIG/CAMEO-SIM)、材质建模(BRDF/微面元)、早期红外场景生成历史文献。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `VAUTIKVU` | 刘梦章 2023 | 基于物理材质渲染和全链路红外成像仿真的岸滩目标与环境特性研究 | CNKI 博士论文 | 同时出现于集成系统集合，覆盖全链路仿真流程 |
| `KW8MLWTR` | Nandhakumar 1988 | Integrated analysis of thermal and visual images for scene interpretation | IEEE TPAMI 10(4) | 从物理原理推导热-可见光图像关系，用于场景解读 |
| `MUAQJHTP` | Altshuler 1961 | Infrared transmission and background radiation by clear atmospheres | DTIC 技术报告 | 清洁大气红外传输与背景辐射的早期奠基性报告 |
| `TKL6PZHZ` | McClatchey 1973 | AFCRL atmospheric absorption line parameters compilation | AFCRL 技术报告 | 大气吸收谱线参数汇编，MODTRAN 数据基础 |
| `GTT36C7Z` | — 1987 | MODTRAN: a moderate resolution model for LOWTRAN | CTIT Tech Reports | 中等分辨率大气辐射传输模型，LOWTRAN 的改进版本 |
| `9JKK6KXN` | Kneizys 1978 | Atmospheric transmittance and radiance: the lowtran code | SPIE Proc. | LOWTRAN 大气传输编码，早期标准大气模型 |
| `KDY778ZS` | Berk 2014 | MODTRAN6: a major upgrade of the MODTRAN radiative transfer code | SPIE 9088 | MODTRAN 第六版，高光谱分辨率大气辐射传输 |
| `VUNT2SIB` | Johnson 1998 | MuSES: a new heat and signature management design tool for virtual prototyping | 会议/技术报告 | MuSES 工具：虚拟样机的热特征管理，取代旧版 PRISM |
| `QM83MFE5` | Upadhyay 2025 | A comprehensive survey on synthetic infrared image synthesis | Infrared Phys. & Tech. 147 | 合成红外图像生成的综合综述，覆盖数学建模与深度学习方法 |
| `8DNCX6MH` | Kottler 2019 | Physically-based thermal simulation of large scenes for infrared imaging | VISIGRAPP 2019 pp.53-64 | 大规模场景物理热仿真：材质分类→网格→FVM 求解，含辐射/对流/传导 |
| `MFV4EMSM` | Gonda 1989 | A comprehensive methodology for thermal signature simulation of targets and backgrounds | SPIE 1098 pp.23-27 | TACOM 热特征仿真综合方法，含目标/背景温度预测和 3D 图形 |
| `5CYHGGYC` | Naraniya 2021 | Scene simulation and modeling of InfraRed search and track sensor for air-borne long range point targets | ICORT 2021 | 空载长距离点目标IRST 系统的红外场景仿真模型 |
| `XTZTIWWW` | Klaassen 2020 | MIRISim: a simulator for the mid-infrared instrument on JWST | MNRAS (arXiv 2010.15710) | JWST MIRI 中红外仪器模拟器，含探测器/畸变/噪声完整链路 |
| `MK9TVCQT` | Coiro 2013 | Global illumination technique for aircraft infrared signature calculations | J. Aircraft 50(1):103-113 | 用全局光照技术计算飞机红外特征，引入计算机图形学方法 |
| `ZI66GZ3K` | Kwan 2008 | A simulation for hyperspectral thermal IR imaging sensors | SPIE 6966:466-476 | 超光谱热红外传感器仿真系统 IRHSS，使用 MuSES 计算超光谱辐亮度 |
| `EI5J2XZE` | Gonda 2003 | An explanation of vehicle-terrain interaction in IR synthetic scenes | SPIE Proc. 2003 p.9 | 将 MuSES 目标集成到 IR 合成场景程序，研究地面-车辆热交互 |
| `YYUX72C3` | Haynes 2003 | Accurate scene modeling using synthetic imagery (CAMEO-SIM) | SPIE 5075:85-96 | CAMEO-SIM 场景建模工具：物理精确 IR/可见光合成图像，含 FIRE 保真度工具集 |
| `2BA3DEVC` | Priest 2002 | Polarimetric microfacet scattering theory with applications to absorptive and reflective surfaces | Opt. Eng. 41(5):988-993 | 极化微面元散射理论，适用于吸收性和反射性表面 |
| `MFHBSMPB` | Schott 1995 | Incorporation of texture in multispectral synthetic image generation tools | SPIE 1995:189-196 | 在多光谱合成图像生成工具中引入纹理 |
| `JA65GKS8` | Meyers 2002 | Modeling polarimetric imaging using DIRSIG | RIT Theses 2002 | 使用 DIRSIG 建模极化成像，DIRSIG 系统的重要参考文献 |
| `PSW4AVCG` | Sheffer & Cathcart 1988 | Computer generated IR imagery: a first principles modeling approach | SPIE 0933:199-206 | 热特征第一性原理预测的 IR 计算机合成图像方法，Georgia Tech |
| `VYF3NDA8` | Balfour 1997 | Semi-empirical model-based approach for IR scene simulation | SPIE 3061:616-623 | 半经验热系数方法：将场景分割为热元素，用气象参数预测温度变化 |
| `WM6EK3XL` | Curtis 1990 | Diurnal and seasonal variation of structural element thermal signatures | SPIE 1311:136-145 | 建筑材料温度的昼夜/季节变化统计分析，第一性热建模的数据支撑 |
| `4DDAGXHE` | Biesel 1987 | Real-time simulated forward looking infrared (FLIR) imagery for training | SPIE 0781:71-80 | 实时 FLIR 仿真训练系统，包括 IR 场景数据库与后处理器 |
| `BWFQVXTF` | Kornfeld 1985 | Computer generation of infrared imagery | Applied Optics 24(24):4534 | 工程模型预测热辐射分布，实现中型计算机上的数字热像仿真 |
| `4C2GI8LE` | Ben-Yosef 1983 | Simulation of IR images of natural backgrounds | Appl. Optics 22(1):190 | 保持温度统计与功率谱的自然背景 IR 图像两种仿真方案 |
| `YNWKDBG2` | Hinderer 1982 | Model for generating synthetic three-dimensional (3D) images of small vehicles | SPIE 1982:8-13 | 小型车辆三维合成像生成模型，早期第一性 IR 造像 |
| `MZ4DP7I9` | Jacobs 1980 | Simulation of the thermal behaviour of an object and its nearby surroundings | 技术报告 1980 | 物体及其近邻环境热行为仿真，最早的热场景互作研究之一 |

---

## 集合2：集成系统 (YDX3ET5E)

> 19 条目（含 2 条与其他集合共享），涵盖国内外 IR 场景实时仿真软件/框架，GPU 加速渲染，大规模地形 IR 渲染。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `VAUTIKVU` | 刘梦章 2023 | 基于物理材质渲染和全链路红外成像仿真的岸滩目标与环境特性研究 | CNKI 博士论文 | *（共享，见集合1）* |
| `XRTPJSDJ` | 张越恒 2024 | 红外目标背景融合与快速图像仿真技术研究 | CNKI 博士论文 | 目标-背景融合与快速 IR 图像仿真技术 |
| `2FHJV9HN` | 吕琪 2024 | 红外场景仿真方法及发展现状研究综述 | 防护工程 46(5) | 国内较新的 IR 场景仿真方法综述，覆盖发展现状 |
| `UQ6GEU8H` | 杨博 2017 | 基于OSG的红外视景仿真研究 | CNKI 博士论文 | 基于 OpenSceneGraph 的红外视景仿真系统研究 |
| `A2FMJT3K` | 张鑫 2012 | 基于GPU编程的红外反射特性建模与仿真 | CNKI 硕士论文 | GPU 并行加速的红外反射特性建模与渲染 |
| `5NIBP2CJ` | 张健 2005 | 基于测量数据的红外场景生成方法及实现 | 系统仿真学报 (10) | 基于实测数据驱动的 IR 场景生成方法与系统实现 |
| `GQ8DV3WH` | 潘雪萍 2007 | 基于Creator和Vega的红外飞行视景仿真 | 计算机仿真 (9) | 利用 Creator/Vega 工具链构建红外飞行视景仿真系统 |
| `6IVV5LW2` | 周帆 2016 | 基于VEGA的地面目标与背景红外场景动态仿真研究 | CNKI 硕士论文 | 基于 VEGA 的地面目标/背景红外场景动态仿真 |
| `UKYNJB8A` | 李晨阳 2020 | 基于三维场景的红外成像仿真框架及实现 | 计算机仿真 37(7) | 三维 IR 成像仿真框架：目标-背景-大气-传感器全链路 |
| `U3SRE5Y9` | Huang 2014 | GPU-based high-precision real-time radiometric rendering for IR scene generation | Infrared Phys. & Tech. 65 | GPU 高精度实时辐射度渲染，用于 IR 场景生成 |
| `KSK7PJV3` | Li 2018 | Real-time and multi-resolution rendering of the infrared characteristics for large-scale terrains | Infrared Phys. & Tech. 94:173-183 | 大规模地形红外特性实时多分辨率渲染，Out-of-Core 技术 |
| `QHFZNUIK` | 李蔚清 2022 | 面向数字孪生战场的红外场景实时仿真方法 | 指挥信息系统与技术 13(4) | 数字孪生战场体系：有限自动机状态机 + 并行红外图像实时生成 |
| `VR9HA7WV` | 秦均玲 2019 | 大规模红外场景实时渲染技术研究 | CNKI 硕士论文 | 神经网络背景温度场 + 纹理映射辐射场 + 改进 PCSS 红外阴影 |
| `2HXEUMTY` | 张繁 2018 | 基于Unity3D的改进实时红外仿真系统 | 计算机辅助设计与图形学学报 30(7) | Unity3D 红外仿真系统，GPU 并行计算 30fps 实时，多材质处理 |
| `3Z8SLNV4` | Wu 2015 | Real-time mid-wavelength infrared scene rendering with a feasible BRDF model | Infrared Phys. & Tech. 68:124-133 | 实时中波红外大场景渲染，提出满足赫姆霍兹互易性的 BRDF 模型 |
| `8L235WKV` | 徐振道 2023 | 坦克目标与复杂地面背景耦合热红外特征研究 | CNKI 博士论文 | *（共享，见集合8）* |
| `GQG8H5C5` | 王霞 2015 | 红外场景仿真技术发展综述 | 红外技术 37(7):537-543 | 国内视角的 IR 场景仿真三阶段综述：数据准备/辐射计算/后处理 |
| `CSWTTDAH` | 黄曦 2014 | 高真实感红外场景实时仿真技术研究 | CNKI 博士论文 | 系统化物理基础 IR 仿真软件，含神经网络温度场/纹理生成/GPU 加速 |
| `DJN2I558` | 徐非凡 2022 | 基于OptiX并行光线追踪计算的红外成像仿真软件研制 | CNKI 硕士论文 | 用 OptiX 光线追踪 + CUDA 实现 IR 成像仿真，含 Monte Carlo 重要性采样/BRDF/降噪 |

---

## 集合3：蒙卡热输运方法 (GLN8YZG3)

> 10 篇，涵盖 Monte Carlo 辐射传输经典理论、null-collision 算法、国内红外 MC 应用。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `TYHCAZUY` | 喻良瑜 2020 | 基于路径追踪的三维红外场景仿真算法及实现 | CNKI 硕士论文 | 国内较早将路径追踪方法用于三维红外场景仿真的探索性工作 |
| `WSPC6XIU` | 王婷 2018 | 基于Monte Carlo方法的雾红外传输仿真及分析 | 光子学报 47(12) | MC 方法在雾介质中的红外传输仿真与分析 |
| `6JR9WI68` | Fan 2025 | Monte-carlo-based simulations of radiative transfer in photovoltaic solar farms | JQSRT 338 | 光伏农场辐射传输 MC 仿真，复杂多界面场景 |
| `EIQVECDK` | Mourtaday 2024 | Monte carlo simulation of atmospheric radiative forcings using a path-integral formulation | JQSRT 327 | 路径积分方法的大气辐射强迫 MC 仿真 |
| `E85R5R5L` | Galtier 2013 | Integral formulation of null-collision monte carlo algorithms | JQSRT 125 | ★ **核心文献** Null-collision MC 算法积分公式化，grid-free 辐射传输算法奠基 |
| `APQMMSGH` | Ertürk 2017 | Monte carlo methods for radiative transfer | Handbook of Thermal Science and Engineering | MC 辐射传输方法综合章节，含基础理论到应用 |
| `UYGGVAGP` | Howell 1964 | Monte carlo solution of thermal transfer through radiant media between gray walls | J. Heat Transfer 86(1) | MC 求解灰体壁面辐射介质热传输，早期热传输 MC 方法 |
| `KBXKXAQP` | Howell 1969 | Application of monte carlo to heat transfer problems | Advances in Heat Transfer 5 | MC 方法在热传输问题中的应用综述，经典奠基章节 |
| `U3TG7DPE` | 于洋 2010 | 基于蒙特卡洛法的红外坦克仿真 | 半导体光电 31(2):325-327 | BVH 加速的 MC 法红外坦克仿真，能束追踪与层次包围盒 |
| `TBEFPJPM` | Szirmay-Kalos 2000 | Monte-carlo methods in global illumination | 专著（Semantic Scholar） | MC 方法用于全局光照渲染：重要性采样/Metropolis 等方差缩减技术综述 |

---

## 集合4：WoS 子集合 (NSHDVQP4)

> 3 篇，聚焦 Walk-on-Spheres 方法与 Grid-free MC PDE 求解前沿。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `HL7D8LS3` | Muller 1956 | Some continuous monte carlo methods for the dirichlet problem | Ann. Math. Stat. 27(3) | ★ **核心文献** Walk-on-Spheres (WoS) 方法原始论文，Dirichlet 边值问题的 MC 求解 |
| `C656SZNW` | Sawhney 2025 | State of the art in grid-free monte carlo methods for partial differential equations | ACM SIGGRAPH 2025 课程 | Grid-free MC PDE 求解最新综述，含 WoS 及其扩展方法 |
| `KIK6D2QT` | Liu 2025 | Inverse radiative transport for infrared scenes with gaussian primitives | ACM SIGGRAPH Asia 2025 | 用高斯体元做红外场景逆辐射传输，前沿工作 |

---

## 集合5：耦合热输运子集合 (NRNA26NB)

> 6 篇，核心理论基础集，覆盖辐射-传导-对流单路径 MC 耦合求解的完整理论链。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `XQ9K5R37` | Villefranque 2022 | The "teapot in a city": a paradigm shift in urban climate modeling | Science Advances 8(27) | 城市气候建模范式转换：MC 路径追踪结合复杂几何，茶壶-城市案例 |
| `UGPFJ8GZ` | Villefranque 2019 | A path-tracing monte carlo library for 3-D radiative transfer in highly resolved cloudy atmospheres | JAMES 11(8) | 高分辨率云大气三维辐射传输路径追踪 MC 库 |
| `NRCB53QN` | Bati 2023 | Coupling conduction, convection and radiative transfer in a single path-space: application to infrared rendering | ACM ToG 42(4) SIGGRAPH | ★ **核心文献（本论文直接基础）** 单路径空间耦合热输运，红外渲染应用 |
| `QGI93KSN` | Tregan 2023 | Coupling radiative, conductive and convective heat-transfers in a single monte carlo algorithm: a general theoretical framework | PLOS One 18(4):e0283681 | 耦合热输运 MC 算法的完整理论基础：传播子/Green函数/Feynman-Kac 理论 |
| `JMWMEL8H` | Caliot 2024 | Coupled heat transfers resolution by monte carlo in urban geometry including direct and diffuse solar irradiations | Int. J. Heat Mass Transfer 222:125139 | 城市复杂几何中含太阳直射/漫射的耦合热传输 MC 求解，双重随机化技术 |
| `P4LTQ7TI` | Fournier 2016 | Radiative, conductive and convective heat-transfers in a single monte carlo algorithm | J. Phys.: Conf. Ser. 676(1):012007 | 辐射-传导-对流单 MC 算法的早期概念论文，第二类 Fredholm 方程框架 |

---

## 集合6：Wavefront (4DD8IT8H)

> 9 篇，涵盖 Wavefront 路径追踪原始论文、GPU 调度算法、并行不规则工作负载处理。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `CLAYGS7B` | Laine 2013 | Megakernels considered harmful: wavefront path tracing on GPUs | HPG 2013 (ACM) | ★ **核心文献** Wavefront 路径追踪原始论文，解决 GPU Megakernel 的 warp divergence 和寄存器压力 |
| `9HX4TUHR` | Frolov 2024 | CrossRT: a cross platform programming technology for hardware-accelerated ray tracing | arXiv 2409.12617 | 跨平台硬件加速光线追踪编程技术 |
| `42DH2IL6` | Kerbl 2017 | Hierarchical Bucket Queuing for Fine-Grained Priority Scheduling on the GPU | CGF 36(8) | GPU 细粒度优先级调度的层次桶队列，适用于不规则 Wavefront |
| `PE8TVULL` | Troendle 2019 | A specialized concurrent queue for scheduling irregular workloads on GPUs | ICS 2019 | 针对 GPU 不规则负载调度的专用并发队列 |
| `WH2DLRQ2` | Blumofe 1999 | Scheduling multithreaded computations by work stealing | J. ACM 46(5) | Work-stealing 调度多线程计算，并行调度理论基础 |
| `4ZPQFSNU` | Börger 2021 | A Behavioural Theory of Recursive Algorithms | Sci. Comp. Prog. 210 | 递归算法的行为理论，状态机形式化基础 |
| `8YB3VIQD` | Teodoro 2012 | Efficient irregular wavefront propagation algorithms on hybrid CPU-GPU machines | arXiv | 混合 CPU-GPU 机器上的不规则 Wavefront 传播算法 |
| `JH3TTFNM` | Ancourt 1991 | Scanning polyhedra with DO loops | SIGPLAN Not. 26(7) | DO 循环遍历多面体，循环并行化基础理论 |
| `8H9XSVYP` | Arantes 2026 | Impact of data-oriented and object-oriented design on performance and cache utilization | arXiv 2512.07841 | 面向数据 vs 面向对象设计对 GPU 性能和缓存利用率的影响分析 |

---

## 集合7：光追实现 (3YKPYUFJ)

> 6 篇，聚焦 GPU 光线与三角形求交、BVH 遍历精度、近邻搜索硬件加速。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `T3ZV4XIQ` | Vaidyanathan 2016 | Watertight ray traversal with reduced precision | HPG 2016 | 降精度无缝 BVH 射线遍历算法 |
| `59Z67DYX` | Woop 2013 | Watertight ray/triangle intersection | JCGT 2(1) | 无缝射线/三角形求交算法，消除缝隙伪影 |
| `HHCGCZDI` | Kuang (无年份) | A practical GPU based KNN algorithm | 会议论文 | GPU 上的实用 K 近邻算法 |
| `ZE97UVDF` | Evangelou 2021 | Fast radius search using bounding volume hierarchies | JCGT 10(1) | 利用 BVH 的快速半径搜索算法 |
| `93MCD22G` | Möller 1997 | Fast, minimum storage ray-triangle intersection | Journal of Graphics Tools 2(1) | ★ Möller-Trumbore 算法，最小存储射线-三角形求交经典 |
| `I9KXW5NS` | Zhu 2022 | RTNN: accelerating neighbor search using hardware ray tracing | PPoPP 2022 | 利用 GPU 硬件 RT Core 加速近邻搜索（RT Core 用于非光追任务） |

---

## 集合8：应用 (VLXCEIT9)

> 4 篇独立条目（1 条共享），专注坦克/装甲目标与地面背景的 IR 耦合仿真应用。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `US4775BJ` | 山清 2020 | 基于变跟踪点的红外场景仿真原理及关键技术研究 | 激光与红外 50(7) | 变跟踪点方法的 IR 场景仿真原理与关键技术 |
| `D7ELUXC7` | 韩玉阁 2013 | 装甲车辆与地面背景的热交互作用及红外仿真 | 红外与激光工程 42(1) | 装甲车辆与地面背景热交互及 IR 特征仿真分析 |
| `YSVE8WJH` | 肖甫 2005 | 地面坦克目标红外热成像物理模型研究 | 系统仿真学报 (11) | 地面坦克 IR 热成像物理模型：传热方程与辐射计算 |
| `8L235WKV` | 徐振道 2023 | 坦克目标与复杂地面背景耦合热红外特征研究 | CNKI 博士论文 | 冷/热静态及动态坦克与典型地面背景的耦合热 IR 特征研究，含轨迹/扬尘建模 |
| `VAUTIKVU` | 刘梦章 2023 | — | — | *（共享，见集合1）* |

---

## 集合9：质量评估 (W8LNS3M7)

> 5 篇，涵盖 IR 仿真图像保真度评价与 MC 渲染误差分析。

| Key | 作者/年份 | 标题 | 来源 | 摘要节选 |
|-----|----------|------|------|---------|
| `ZVXGGYLJ` | 杨梦迪 2023 | 红外仿真图像质量评价方法研究 | CNKI 硕士论文 | 红外仿真图像质量评价指标与方法研究 |
| `LVSNPBLH` | Wu 2017 | Adaptive grid-based confidence assessment for synthetic optoelectronic images by PRISSE | Infrared Phys. & Tech. 85 | 基于自适应网格的 PRISSE 合成光电图像置信度评估方法 |
| `WN3YG6K3` | Sakai 2019 | A method for estimating the errors in many-light rendering with supersampling | Comp. Visual Media 5(2) | 超采样多光源渲染的误差估计方法 |
| `VRUZYXW2` | Celarek 2019 | Quantifying the error of light transport algorithms | CGF 38(4) EGSR | 量化光传输算法误差的系统方法 |
| `JBYYDS2Z` | Whittle 2017 | Analysis of reported error in monte carlo rendered images | The Visual Computer 33(6) | MC 渲染图像中报告误差的分析 |

---

## 关键文献缺失核查

> 以下为论文各章节所需的核心文献，检查其是否已在 Zotero 库中。

| 编号 | 主题/文献 | 状态 | 所在集合/Key | 备注 |
|------|----------|------|-------------|------|
| 1 | Walk on Spheres (Muller 1956) | ✅ **已有** | NSHDVQP4 / `HL7D8LS3` | WoS 方法原论文 |
| 2 | Null-collision MC (Galtier 2013) | ✅ **已有** | GLN8YZG3 / `E85R5R5L` | null-collision 积分公式化 |
| 3 | Wavefront 路径追踪 (Laine 2013) | ✅ **已有** | 4DD8IT8H / `CLAYGS7B` | Megakernels 原论文 |
| 4 | 耦合热输运 (Bati 2023) | ✅ **已有** | NRNA26NB / `NRCB53QN` | 本论文直接基础 |
| 5 | 耦合热输运理论基础 (Tregan 2023) | ✅ **已有** | NRNA26NB / `QGI93KSN` | Feynman-Kac 理论框架 |
| 6 | 城市 MC 耦合热输运 (Caliot 2024) | ✅ **已有** | NRNA26NB / `JMWMEL8H` | 城市复杂几何，双重随机化 |
| 7 | Grid-free MC PDE 综述 (Sawhney 2025) | ✅ **已有** | NSHDVQP4 / `C656SZNW` | 最新综述 |
| 8 | MuSES 签名工具 (Johnson 1998) | ✅ **已有** | HFGRFYUY / `VUNT2SIB` | 取代旧版 PRISM |
| 9 | DIRSIG 系统参考 (Meyers 2002) | ✅ **已有** | HFGRFYUY / `JA65GKS8` | DIRSIG 极化成像建模 |
| 10 | CAMEO-SIM (Haynes 2003) | ✅ **已有** | HFGRFYUY / `YYUX72C3` | 物理精确合成图像 |
| 11 | 大气模型 MODTRAN (Berk 2014) | ✅ **已有** | HFGRFYUY / `KDY778ZS` | MODTRAN6 |
| 12 | Möller-Trumbore 求交 (Möller 1997) | ✅ **已有** | 3YKPYUFJ / `93MCD22G` | 经典射线-三角求交 |
| 13 | 硬件 RT 近邻搜索 (Zhu 2022) | ✅ **已有** | 3YKPYUFJ / `I9KXW5NS` | RT Core 非光追应用 |
| 14 | **渲染方程 (Kajiya 1986)** | ❌ **缺失** | — | 第二章理论基础必需 |
| 15 | **重要性采样/MIS (Veach 1997)** | ❌ **缺失** | — | 第二章 MC 方法必需 |
| 16 | **MC 方法奠基 (Metropolis 1953)** | ❌ **缺失** | — | 第二章 MC 历史背景 |
| 17 | **GPU SIMT/CUDA (Nickolls 2010)** | ❌ **缺失** | — | 第三章 GPU 架构背景 |
| 18 | **BVH GPU 遍历 (Aila & Laine 2009)** | ❌ **缺失** | — | 第四章 BVH 加速结构 |
| 19 | **OptiX SDK (Parker 2010)** | ❌ **缺失** | — | 第四章 OptiX 后端必需 |
| 20 | **Embree (Wald 2014)** | ❌ **缺失** | — | 第三/四章 CPU Baseline |
| 21 | **RT Core 架构白皮书 (NVIDIA Turing/Ada)** | ❌ **缺失** | — | 第四章硬件原理 |
| 22 | **Feynman-Kac 原始论文 (Kac 1947)** | ❌ **缺失** | — | 第二章理论基础（可选）|

---

## 统计汇总

| 集合 | Key | 条目数（本次核查） | 主题 |
|------|-----|------------------|------|
| 经验/半经验/第一性方法 | HFGRFYUY | **28** | 大气传输、签名预测工具、早期 IR 场景生成 |
| 集成系统 | YDX3ET5E | **19** | GPU 实时 IR 仿真框架 |
| 蒙卡热输运方法 | GLN8YZG3 | **10** | MC 辐射传输理论与应用 |
| WoS 子集合 | NSHDVQP4 | **3** | Grid-free MC PDE |
| 耦合热输运子集合 | NRNA26NB | **6** | 辐射-传导-对流耦合 MC |
| Wavefront | 4DD8IT8H | **9** | GPU 并行调度与 Wavefront |
| 光追实现 | 3YKPYUFJ | **6** | BVH/RT Core 实现细节 |
| 应用 | VLXCEIT9 | **4+1共享** | 装甲目标 IR 仿真 |
| 质量评估 | W8LNS3M7 | **5** | 仿真图像误差评价 |
| **合计（去重）** | — | **≈ 85** | — |

### 优先补充文献建议

按章节紧迫程度排序：

1. **第四章** — OptiX (Parker 2010), NVIDIA Turing/Ada 白皮书, Aila & Laine 2009 BVH
2. **第三章** — Embree (Wald 2014), CUDA/Nickolls 2010, 或等价的 GPU 架构参考
3. **第二章** — Kajiya 1986 渲染方程, Veach 1997 MIS, Metropolis 1953 MC

可通过 BBT JSON-RPC 的 `item.export` 导出上述已有文献的 BibTeX，缺失文献建议直接在 Zotero 中通过 DOI 导入。
