# NEEDCITE-FEM / NEEDCITE-DOM / NEEDCITE-COUPLED-FEM

## 对应章节位置

- [Chapters/1_Introduction.tex](../Chapters/1_Introduction.tex)，第1章 §1.2.1「热输运仿真方法」→「基于数值求解的物理仿真方法」段落
  - 第49行：`[NEEDCITE-FEM]` — 有限元法/有限体积法在传热仿真中的经典文献
  - 第52行：`[NEEDCITE-DOM]` — 离散坐标法（DOM/S_N 方法）文献
  - 第54行：`[NEEDCITE-COUPLED-FEM]` — FEM/FVM 中辐射-传导-对流耦合的外迭代方法文献

---

## 1. NEEDCITE-FEM — 有限元/有限体积法经典传热教材

### 1a. Patankar 1980 (书籍，CrossRef 无 DOI) ⚠️

> S. V. Patankar, *Numerical Heat Transfer and Fluid Flow*, Hemisphere Publishing, 1980.

**状态**：CrossRef 未收录（出版于 1980 年，Hemisphere 出版社）  
**替代方案**：可通过 Google Scholar / OpenLibrary 引用为 `@book`，无 DOI。  
**建议 citation key**：`patankarNumericalHeatTransfer1980`  
**BibTeX 建议**：
```bibtex
@book{patankarNumericalHeatTransfer1980,
  author    = {Patankar, Suhas V.},
  title     = {Numerical Heat Transfer and Fluid Flow},
  publisher = {Hemisphere Publishing Corporation},
  year      = {1980},
  isbn      = {9780891165224},
}
```

---

### 1b. Incropera et al. — Fundamentals of Heat and Mass Transfer (书籍) ⚠️

> F. P. Incropera, D. P. DeWitt, T. L. Bergman, A. S. Lavine,  
> *Fundamentals of Heat and Mass Transfer*, 7th ed., Wiley, 2011.

**状态**：CrossRef 无整本书 DOI（教材类）  
**建议 citation key**：`incroperaFundamentalsHeatMass2011`  
**BibTeX 建议**：
```bibtex
@book{incroperaFundamentalsHeatMass2011,
  author    = {Incropera, Frank P. and DeWitt, David P. and Bergman, Theodore L. and Lavine, Adrienne S.},
  title     = {Fundamentals of Heat and Mass Transfer},
  edition   = {7},
  publisher = {Wiley},
  year      = {2011},
  isbn      = {9780470501979},
}
```

---

## 2. NEEDCITE-DOM — 离散坐标法文献

### 2a. Fiveland 1984 ✅（CrossRef 已确认）

> W. A. Fiveland, "Discrete-Ordinates Solutions of the Radiative Transport Equation for Rectangular Enclosures," *J. Heat Transfer*, vol. 106, no. 4, pp. 699–706, 1984.

**DOI**：`10.1115/1.3246741`  
**发表**：ASME International, Journal of Heat Transfer  
**引用数**：508  
**建议 citation key**：`fivelandDiscreteOrdinatesSolutions1984`  
**BibTeX（CrossRef 数据）**：
```bibtex
@article{fivelandDiscreteOrdinatesSolutions1984,
  author    = {Fiveland, W. A.},
  title     = {Discrete-Ordinates Solutions of the Radiative Transport Equation for Rectangular Enclosures},
  journal   = {Journal of Heat Transfer},
  volume    = {106},
  number    = {4},
  pages     = {699--706},
  year      = {1984},
  month     = {nov},
  publisher = {ASME International},
  doi       = {10.1115/1.3246741},
}
```

---

### 2b. Modest — Radiative Heat Transfer (教材，CrossRef 有书章节 DOI) ⚠️

> M. F. Modest, *Radiative Heat Transfer*, 3rd ed., Academic Press/Elsevier, 2013.  
> CrossRef 收录了章节 DOI（如反问题章节 `10.1016/b978-0-12-386944-9.50023-6`），但整本书无独立 DOI。

**状态**：书籍本身无整本 DOI；`ISBN 9780123869449`（Elsevier 3rd ed.）  
**建议 citation key**：`modestRadiativeHeatTransfer2013`  
**BibTeX 建议**：
```bibtex
@book{modestRadiativeHeatTransfer2013,
  author    = {Modest, Michael F.},
  title     = {Radiative Heat Transfer},
  edition   = {3},
  publisher = {Academic Press / Elsevier},
  year      = {2013},
  isbn      = {9780123869449},
}
```

---

## 3. NEEDCITE-COUPLED-FEM — 辐射-传导-对流耦合外迭代文献

### 3a. Howell et al. — Thermal Radiation Heat Transfer (教材) ⚠️

> J. R. Howell, M. P. Mengüç, K. Daun, R. Siegel,  
> *Thermal Radiation Heat Transfer*, 7th ed., CRC Press, 2020.

**状态**：CrossRef 有书章节 DOI（如 Monte Carlo 章节 `10.1201/9780429327308-14`），整本书无独立 DOI。  
**建议 citation key**：`howellThermalRadiationHeat2021`  
**BibTeX 建议**：
```bibtex
@book{howellThermalRadiationHeat2021,
  author    = {Howell, John R. and Meng{\"u}{\c{c}}, M. Pinar and Daun, Kyle and Siegel, Robert},
  title     = {Thermal Radiation Heat Transfer},
  edition   = {7},
  publisher = {CRC Press},
  year      = {2021},
  isbn      = {9780429327308},
}
```

---

## 补充说明

- **Patankar 1980** 和 **Incropera** 是传热数值方法的"标准教科书"引用，论文中通常以 `@book` 格式引用，无需 DOI。
- **Fiveland 1984** 是 DOI 已确认的期刊文章，可直接通过 Zotero Add by DOI 导入。
- 若只需证明"有限元/有限体积法用于热传导仿真"，可选用 `modestRadiativeHeatTransfer2013` + `fivelandDiscreteOrdinatesSolutions1984` 组合，覆盖传导/辐射两侧。
