# 渲染方程 / 路径追踪 / 蒙特卡洛热辐射经典文献

## 对应章节位置

- [Chapters/1_Introduction.tex](../Chapters/1_Introduction.tex)，第108行：`[引用需求] 经典渲染领域路径追踪文献（Kajiya 1986 渲染方程）`
- [Chapters/2_Research.tex](../Chapters/2_Research.tex)，第60行：`[引用需求] Kajiya 1986 渲染方程`
- 第一章 §1.2.2「基于路径追踪蒙特卡洛热输运仿真方法」（写作思路注释）中提到 Howell (1964, 1969)

---

## 1. Kajiya 1986 — 渲染方程 ✅（CrossRef 已确认）

> J. T. Kajiya, "The Rendering Equation," *ACM SIGGRAPH Computer Graphics*, vol. 20, no. 4, pp. 143–150, 1986.

**DOI**：`10.1145/15886.15902`  
**发表**：ACM SIGGRAPH 1986  
**引用数**：1123  
**摘要**：提出积分方程统一化各种渲染算法，并引入 Hierarchical Sampling 方差缩减，奠定路径追踪理论基础。  
**建议 citation key**：`kajiyaRenderingEquation1986`  
**BibTeX**：
```bibtex
@article{kajiyaRenderingEquation1986,
  author    = {Kajiya, James T.},
  title     = {The Rendering Equation},
  journal   = {ACM SIGGRAPH Computer Graphics},
  volume    = {20},
  number    = {4},
  pages     = {143--150},
  year      = {1986},
  month     = {aug},
  publisher = {Association for Computing Machinery (ACM)},
  doi       = {10.1145/15886.15902},
}
```

---

## 2. Howell & Perlmutter 1964 — 蒙特卡洛热辐射开创性工作 ✅（CrossRef 已确认）

> J. R. Howell and M. Perlmutter, "Monte Carlo Solution of Thermal Transfer Through Radiant Media Between Gray Walls," *Journal of Heat Transfer*, vol. 86, no. 1, pp. 116–122, 1964.

**DOI**：`10.1115/1.3687044`  
**发表**：ASME International, Journal of Heat Transfer  
**引用数**：178  
**摘要**：最早将蒙特卡洛方法应用于辐射传热求解（灰体气体+灰壁），与精确解和 Hottel 区域法对比。  
**建议 citation key**：`howellMonteCarloSolution1964`  
**BibTeX**：
```bibtex
@article{howellMonteCarloSolution1964,
  author    = {Howell, J. R. and Perlmutter, M.},
  title     = {Monte Carlo Solution of Thermal Transfer Through Radiant Media Between Gray Walls},
  journal   = {Journal of Heat Transfer},
  volume    = {86},
  number    = {1},
  pages     = {116--122},
  year      = {1964},
  month     = {feb},
  publisher = {ASME International},
  doi       = {10.1115/1.3687044},
}
```

---

## 3. Howell & Perlmutter 1964 (AIChE) — 非灰体蒙卡辐射 ✅（CrossRef 已确认）

> J. R. Howell and M. Perlmutter, "Monte Carlo solution of radiant heat transfer in a nongrey nonisothermal gas with temperature dependent properties," *AIChE Journal*, vol. 10, no. 4, pp. 562–567, 1964.

**DOI**：`10.1002/aic.690100429`  
**建议 citation key**：`howellMonteCarloRadiant1964`  
**备注**：若已有 `howellMonteCarloSolution1964`，此篇可按需补充（非必须，章节文字仅提到 1964 Howell 即可）。

---

## 补充说明

- **Kajiya 1986** 是必引经典，在第二章「渲染方程理论基础」段落引用。
- **Howell 1964** 是「蒙特卡洛方法在热辐射中的早期应用」段落的开创性引文。
- 写作思路中还提到 **Veach 1997**（Multiple Importance Sampling）— CrossRef 未搜索，如需可后续补充。
