# GPU 光追 / OptiX / Embree / BVH / CUDA 文献

## 对应章节位置

- [Chapters/1_Introduction.tex](../Chapters/1_Introduction.tex)，第125–129行：
  - `[引用需求] 需补充：CUDA/GPGPU 综述文献`
  - `[引用需求] 需补充：Embree 论文（Wald 等）`
  - `[引用需求] 需补充：OptiX 论文（Parker 等 2010）`
  - `[引用需求] 需补充：Laine 等 Megakernels Considered Harmful (2013, HPG)`
  - `[引用需求] 需补充：RT Core 架构论文或 NVIDIA 技术白皮书`
- [Chapters/2_Research.tex](../Chapters/2_Research.tex)，第87–113行：BVH、Embree、OptiX、Wavefront 模型
- [Chapters/4_RTBackend.tex](../Chapters/4_RTBackend.tex)，第54–71行：OptiX 执行模型

---

## 1. Laine, Karras, Aila 2013 — Megakernels Considered Harmful ✅

> S. Laine, T. Karras, T. Aila, "Megakernels Considered Harmful: Wavefront Path Tracing on GPUs," in *Proc. High-Performance Graphics (HPG)*, pp. 137–143, 2013.

**DOI**：`10.1145/2492045.2492060`  
**发表**：ACM High-Performance Graphics 2013  
**引用数**：71  
**摘要**：核心论文，提出将路径追踪光线的不同状态分成 Wavefront（波前）批量处理，以解决 GPU Megakernel 的分歧（divergence）和占用率（occupancy）问题。  
**建议 citation key**：`laineMegakernelsConsideredHarmful2013`  
**BibTeX**：
```bibtex
@inproceedings{laineMegakernelsConsideredHarmful2013,
  author    = {Laine, Samuli and Karras, Tero and Aila, Timo},
  title     = {Megakernels Considered Harmful: Wavefront Path Tracing on GPUs},
  booktitle = {Proceedings of High-Performance Graphics},
  pages     = {137--143},
  year      = {2013},
  month     = {jul},
  publisher = {ACM},
  doi       = {10.1145/2492045.2492060},
}
```

---

## 2. Parker et al. 2010 — OptiX GPU Ray Tracing Engine ✅

> S. G. Parker, J. Bigler, A. Dietrich, H. Friedrich, et al., "OptiX: A General Purpose Ray Tracing Engine," in *ACM SIGGRAPH 2010 Papers*, pp. 1–13, 2010.

**DOI**：`10.1145/1833349.1778803`  
**发表**：ACM SIGGRAPH 2010  
**引用数**：75  
**摘要**：介绍 NVIDIA OptiX 框架：可编程 GPU 光线追踪引擎，通过简单程序模型（ray generation, closest hit, any hit, miss shader）支持任意复杂场景的高性能光追。  
**建议 citation key**：`parkerOptiXGeneralPurpose2010`  
**完整作者（CrossRef 确认）**：Parker, Bigler, Dietrich, Friedrich + NVIDIA 团队  
**BibTeX**：
```bibtex
@inproceedings{parkerOptiXGeneralPurpose2010,
  author    = {Parker, Steven G. and Bigler, James and Dietrich, Andreas and Friedrich, Heiko and Hoberock, Jared and Luebke, David and McAllister, David and McGuire, Morgan and Morley, Keith and Robison, Austin and Stich, Martin},
  title     = {{OptiX}: A General Purpose Ray Tracing Engine},
  booktitle = {ACM SIGGRAPH 2010 Papers},
  pages     = {1--13},
  year      = {2010},
  month     = {jul},
  publisher = {ACM},
  doi       = {10.1145/1833349.1778803},
}
```

---

## 3. Áfra & Wald 2016 — Embree Ray Tracing Kernels ✅

> A. T. Áfra and I. Wald, "Embree Ray Tracing Kernels," in *Proc. ACM SIGGRAPH 2016 Talks*, pp. 1–2, 2016.  
> （Intel Corporation, ACM SIGGRAPH 2016 Talk）

**DOI**：`10.1145/2897839.2927450`  
**发表**：ACM SIGGRAPH 2016 Talks  
**引用数**：6  
**备注**：此为 SIGGRAPH 2016 Talk（2页摘要），是 Embree 的官方会议引文。  
  更完整的 Embree 参考文献可用：Wald, Woop, Benthin et al. "Embree: A Kernel Framework for Efficient CPU Ray Tracing," *TOG* 33(4), 2014 — CrossRef 搜索未直接返回该 TOG 版本，但 DOI 为 `10.1145/2601097.2601199`（需验证）。  
**建议 citation key**：`afraEmbreeRayTracing2016`（Talk版本）或 `waldEmbreeKernelFramework2014`（TOG版本）  
**BibTeX（Talk 版本）**：
```bibtex
@inproceedings{afraEmbreeRayTracing2016,
  author    = {{\'{A}}fra, Attila T. and Wald, Ingo},
  title     = {Embree Ray Tracing Kernels},
  booktitle = {ACM SIGGRAPH 2016 Talks},
  pages     = {1--2},
  year      = {2016},
  month     = {jul},
  publisher = {ACM},
  doi       = {10.1145/2897839.2927450},
}
```

---

## 4. Aila & Laine 2009 — Understanding the Efficiency of Ray Traversal on GPUs ✅

> T. Aila and S. Laine, "Understanding the Efficiency of Ray Traversal on GPUs," in *Proc. High-Performance Graphics (HPG) 2009*, pp. 145–149, Aug. 2009.

**DOI**：`10.1145/1572769.1572792`  
**发表**：ACM High-Performance Graphics 2009  
**引用数**：290  
**摘要**：系统分析 GPU BVH 遍历的各种算法（SIMT 线程效率），为后续 Embree/OptiX 实现提供理论基础。  
**建议 citation key**：`ailaUnderstandingEfficiencyRay2009`  
**BibTeX**：
```bibtex
@inproceedings{ailaUnderstandingEfficiencyRay2009,
  author    = {Aila, Timo and Laine, Samuli},
  title     = {Understanding the Efficiency of Ray Traversal on GPUs},
  booktitle = {Proceedings of High-Performance Graphics},
  pages     = {145--149},
  year      = {2009},
  month     = {aug},
  publisher = {ACM},
  doi       = {10.1145/1572769.1572792},
}
```

---

## 5. Kirk 2007 / Nickolls 2007 — NVIDIA CUDA

### 5a. Kirk 2007 — NVIDIA CUDA Software and GPU Parallel Computing Architecture ✅

> D. Kirk, "NVIDIA CUDA Software and GPU Parallel Computing Architecture," in *Proc. ISMM 2007*, pp. 103–104, 2007.

**DOI**：`10.1145/1296907.1296909`  
**引用数**：240  

### 5b. Nickolls 2007 — GPU Parallel Computing Architecture and CUDA Programming Model ✅

> J. Nickolls, "GPU Parallel Computing Architecture and CUDA Programming Model," *Proc. IEEE Hot Chips 19*, pp. 1–12, 2007.

**DOI**：`10.1109/hotchips.2007.7482491`  
**引用数**：24  

**建议**：优先使用更广泛引用的 `Kirk 2007`；或直接引用 NVIDIA 官方 CUDA 编程手册（`@manual`，无 DOI）。  
**替代建议**（更常用）：  
```bibtex
@manual{nvidiacudaprogrammingguide2024,
  author       = {{NVIDIA Corporation}},
  title        = {{CUDA C++ Programming Guide}},
  year         = {2024},
  note         = {Version 12.x},
  url          = {https://docs.nvidia.com/cuda/cuda-c-programming-guide/},
}
```

---

## 6. RT Core 架构 — NVIDIA 技术白皮书 ⚠️（无 DOI）

**状态**：NVIDIA Turing/Ampere/Ada 架构白皮书为官方技术文档，CrossRef 未收录。  
**引用方式**：`@techreport` 或 `@misc`。  
**建议**：
```bibtex
@misc{nvidiaturingarchitecture2018,
  author       = {{NVIDIA Corporation}},
  title        = {{NVIDIA Turing GPU Architecture: Graphics Reinvented}},
  year         = {2018},
  note         = {Whitepaper, version 1.0},
  url          = {https://images.nvidia.com/aem-dam/en-zz/Solutions/design-visualization/technologies/turing-architecture/NVIDIA-Turing-Architecture-Whitepaper.pdf},
}
```

---

## 补充说明

- **Laine 2013 Megakernels** 是第二章「Wavefront 执行模型」和第三章「并行化改造」的核心理论支撑。
- **Parker 2010 OptiX** 是第四章「OptiX 光追后端」必引文献。
- **Aila 2009 GPU ray traversal** 是 BVH 遍历效率分析的经典参考，适用于第二章和第四章。
- **Embree (Áfra & Wald 2016)** 是第三章 Baseline 系统描述的引文。
- RT Core 白皮书以 `@misc` 方式引用，注明 URL 和版本号即可。
