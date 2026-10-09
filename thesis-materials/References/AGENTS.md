# References/ 参考文献目录说明

本目录按论文章节对 Zotero EOIR 集合中的参考文献进行分类管理。  
**用途**: 管理与查阅，而非直接参与 LaTeX 编译。实际编译使用根目录的 `ReferenceBase.bib`。

---

## 文件说明

| 文件 | 对应章节 | 篇数 | 来源 Zotero 子集合 |
|------|----------|------|--------------------|
| `ch1_introduction.bib` | 第一章 绪论 | ~50 | 集成系统 + 应用 + 经验半经验与第一性方法 |
| `ch2_mc_theory.bib` | 第二章 理论基础 | ~16 | 蒙卡热输运方法 + 热输运理论 |
| `ch3_wavefront_solver.bib` | 第三章 Wavefront 求解器 | ~10 | Wavefront + 质量评估 |
| `ch4_rt_backend.bib` | 第四章 光追后端 | ~8 | 光追实现 + optix + embree |
| `ch5_gui.bib` | 第五章 GUI 编辑器 | ~0 | 材质处理（暂无条目） |
| `eoir_uncategorized.bib` | — | ~11 | EOIR 根目录未归入子集合的论文 |

---

## Zotero 子集合 → 文件映射

| 子集合名称 | Collection Key | 归入文件 |
|-----------|---------------|---------|
| 集成系统 | `YDX3ET5E` | `ch1_introduction.bib` |
| 应用 | `VLXCEIT9` | `ch1_introduction.bib` |
| 经验半经验与第一性方法 | `HFGRFYUY` | `ch1_introduction.bib` |
| 蒙卡热输运方法 | `GLN8YZG3` | `ch2_mc_theory.bib` |
| 热输运理论 | `ML2FZRUN` | `ch2_mc_theory.bib` |
| Wavefront | `4DD8IT8H` | `ch3_wavefront_solver.bib` |
| 质量评估 | `W8LNS3M7` | `ch3_wavefront_solver.bib` |
| 光追实现 | `3YKPYUFJ` | `ch4_rt_backend.bib` |
| optix | `GKQIN25N` | `ch4_rt_backend.bib` |
| embree | `ZY524LFB` | `ch4_rt_backend.bib` |
| 材质处理 | `NJCNBTFL` | `ch5_gui.bib` |
| EOIR 根目录（未分类） | `MJF343F7` | `eoir_uncategorized.bib` |

---

## Citation Key 格式说明

- **英文论文**: 使用 Better BibTeX（BBT）自动生成，格式如 `kajiyaRenderingEquation1986`
- **中文论文（无 BBT key）**: 自动降级使用 Zotero 本地 API 导出，citation key 质量较差（如 `__2025`）  
  → 建议在 Zotero 中为中文论文手动设置 BBT citation key，然后重新导出

---

## 如何更新 / 重新导出

在 Zotero 运行时，从论文根目录执行：

```bash
node export_refs.js
```

该脚本会：
1. 调用 BBT `item.search('')` 构建 Zotero item key → BBT citation key 映射
2. 按集合获取论文列表（Zotero 本地 API，端口 23119）
3. 通过 BBT `item.export` 导出高质量 BibTeX（端口 23119，JSON-RPC）
4. 无 BBT key 的条目自动降级到 Zotero API 导出

> 前提：Zotero 已运行，且已安装 Better BibTeX 插件

---

## 如何将论文添加到 LaTeX 编译

这些 .bib 文件**不参与编译**，只用于管理查阅。将所需条目合并到根目录的 `ReferenceBase.bib` 中：

1. 在对应 `.bib` 文件中找到目标 `@article{...}` 或 `@book{...}` 条目
2. 复制到 `ReferenceBase.bib`（检查是否已存在）
3. 在正文 `.tex` 中使用 `\cite{citekey}` 引用

---

## 生成时间 & 版本控制

- 这些 `.bib` 文件由 `export_refs.js` 脚本生成，建议加入版本控制（`.gitignore` 中不要排除）
- 每次在 Zotero 中新增或整理文献后，重新运行脚本以同步最新状态
- 由于 Zotero 运行于本地，这些文件仅在本机有效；跨机器协作时需在目标机重新导出
