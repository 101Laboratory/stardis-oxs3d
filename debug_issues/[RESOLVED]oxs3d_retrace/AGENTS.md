# oxs3d_retrace — retrace 循环无深度限制

**状态**: 🔍 活跃 — 根因已定位，修复方案已设计，待实施  
**创建日期**: 2026-02-23  
**严重度**: 中（性能）— profiler 显示 `ntdll.dll` self-time 占 80%

---

## 问题描述

oxstar-3d batch trace 接口实现 filter + retrace fallback 后，Visual Studio Profiler 显示 stardis 主程序首个高耗时调用为 `ntdll.dll`（Windows NT 内核用户态入口），self-time 占 80%。此问题在实现 filter retrace 之前不存在。

---

## 根因

`s3d_scene_view_trace_ray()` 内部的 retrace 循环（`while (retry_tmin < tmax)`）**无深度限制**。相比之下，cuBQL 后端有 `MAX_FALLBACK_DEPTH=4` 约束。

每次循环迭代均触发完整的 GPU roundtrip：

```
while (retry_tmin < tmax)           ← 无深度限制
  └─ traceSingle()
       ├─ cudaMalloc(d_params)      ← 每次分配
       ├─ cudaMemcpy H→D
       ├─ optixLaunch(1,1,1)        ← 单条射线 launch
       ├─ cudaDeviceSynchronize()
       └─ cudaFree(d_params)        ← 每次释放
```

大量频繁的 `cudaMalloc`/`cudaFree` 和单射线 kernel launch 通过 `ntdll.dll` 系统调用完成，导致 80% 的 self-time 集中于此。

---

## 修复方案

1. 添加 `MAX_FALLBACK_DEPTH` 深度上限（参考 cuBQL 的 4 层限制）
2. 将 retrace 列表改为批量化，避免单射线逐次 launch
3. 预分配 `d_params` 缓冲区，消除热路径中的 `cudaMalloc`/`cudaFree`

（详见 `fix_plan.md`）

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `analysis.md` | 根因定位分析（调用链、profiler 数据） |
| `fix_plan.md` | 详细修复方案 |
