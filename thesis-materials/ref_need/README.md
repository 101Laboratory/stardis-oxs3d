# ref_need — 待补充文献归档目录

通过 CrossRef MCP 搜索收集，按章节 NEEDCITE 标记分类。  
生成时间：2026-03-11  
来源：扫描 `Chapters/` 下所有 `[NEEDCITE-*]` 和 `[引用需求]` 注释标记

## 文件列表

| 文件 | 内容 |
|------|------|
| `ch1_fem_dom.md` | 第一章 NEEDCITE-FEM, NEEDCITE-DOM, NEEDCITE-COUPLED-FEM |
| `ch1_ch2_rendering.md` | 渲染方程、路径追踪经典文献（Kajiya/Howell/Laine 等） |
| `ch2_ch4_gpu_rt.md` | GPU 光追 / OptiX / Embree / BVH / CUDA 相关 |

## 使用说明

每条记录标注：
- **确认状态**：CrossRef 已确认 DOI ✅ / 书籍无 DOI ⚠️ / 未找到 ❌
- **DOI**：可直接传给 BBT `item.export` 或 Zotero 导入
- **建议 citation key**：参考现有 `ReferenceBase.bib` 命名风格
- **对应 NEEDCITE 标记**：定位到具体章节位置
