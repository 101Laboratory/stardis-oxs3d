# Merge Phase 测试框架不兼容调查报告

**调查时间**: 2026-03-12  
**调查环境**: stardis-oxs3d-merge-phase + stardis-oxs3d-test-validation  
**调查方法**: 编译审计 + 源码分析  

## 调查过程

### 1. 环境准备

创建了专用测试工作树以避免污染主开发环境：

```bash
cd D:\Stardis-GPU
git -C stardis-oxs3d-merge-phase worktree add -b test-b4-wf-validation ../stardis-oxs3d-test-validation
cd stardis-oxs3d-test-validation/build_test
```

### 2. CMake 配置增强

在 `stardis-solver/0.16.2/CMakeLists.txt` 中添加了专用控制变量：

```cmake
option(ENABLE_B4_WF_TESTS "Enable building B4 and WF series tests only" OFF)
if(ENABLE_TESTS OR ENABLE_B4_WF_TESTS)
    # 测试构建逻辑
endif()
```

### 3. 编译测试验证

#### 3.1 H系列测试（API/数据结构）- ✅ 成功

```bash
cmake --build . --config Release --target test_sdis_camera
# 结果：编译成功，无错误
```

#### 3.2 B4系列测试 - ❌ 数据结构不兼容

```bash
cmake --build . --config Release --target test_sdis_b4_m2_ray_bucketing
# 错误示例：
# error C2039: "dsoa": 不是 "wavefront_pool" 的成员
# error C2039: "active": 不是 "path_state" 的成员
```

#### 3.3 WF系列测试 - ❌ 函数未实现

```bash
cmake --build . --config Release --target test_sdis_wf_a1_flux  
# 错误示例：
# error LNK2019: 无法解析的外部符号 __imp_sdis_solve_persistent_wavefront_probe_batch
```

## 详细错误分析

### B4 测试数据结构问题

#### 缺失的 `wavefront_pool` 成员
```c
// 测试代码期望：
wf_pool->dsoa  // ❌ 不存在

// 需要的函数：
dispatch_soa_alloc(&wf_pool->dsoa, N_PATHS);     // ❌ 未定义
dispatch_soa_free(&wf_pool->dsoa);              // ❌ 未定义  
dispatch_soa_sync_from_path(&wf_pool->dsoa, &path); // ❌ 未定义
```

#### 缺失的 `path_state` 成员
```c
// 测试代码期望：
path_state ps;
ps.active = 1;        // ❌ 成员不存在
ps.needs_ray = 1;     // ❌ 成员不存在
ps.phase = PHASE_CND; // ❌ 成员不存在
ps.ray_bucket = 0;    // ❌ 成员不存在
ps.ray_count_ext = 0; // ❌ 成员不存在
```

### WF 测试函数缺失问题

#### API 声明存在但实现缺失

在 `sdis.h` 中找到完整的函数声明：

```c
// ✅ 声明存在
SDIS_API res_T
sdis_solve_persistent_wavefront_probe_batch
  (struct sdis_scene* scn,
   const size_t nprobes, 
   const struct sdis_solve_probe_args* args_array,
   struct sdis_estimator** out_estimators);

// ❌ 但链接时找不到实现
```

#### 相关函数状态
```c
// 这些函数的实现状态未知：
sdis_solve_wavefront_probe                    // 声明存在
sdis_solve_persistent_wavefront_probe         // 声明存在 
sdis_solve_persistent_wavefront_probe_batch   // 声明存在，WF测试依赖
```

## 根本原因分析

### B4系列测试不兼容 - ✅ 原因明确

**历史背景**: oxs3d项目主分支和merge-phase分支快速迭代引入了太多架构变更，为减少干扰临时禁用了测试，导致测试没有跟上代码变更。

**架构演进**: 
- **wavefront**: 已被抛弃，不再使用
- **persistent wavefront**: 长期测试已证明稳定性，是当前唯一状态机

**具体技术变更**:
- `path_state`发生了结构性变化  
- `dsoa`字段在O系列优化O10中已被移除
- 现在使用单独的`hot_arr`（不完全等价，包含其他字段）
- `ds`相关字段被移到了`locals`中以进一步缩小`ps`大小

**结论**: B4系列测试已过时，不再需要。如需测试persistent wavefront状态机，需重新设计测试架构。

### WF系列测试链接问题 - ✅ 配置正确，需深入分析

**功能定位**:
- 迁移自原始CPU系列测试，用于数值正确性验证
- 曾负责收集测试数据供外部使用（数据已收集完成，存储在`/physical_consistency_stats/`）

**技术验证结果**:
- ✅ **函数实现存在**: `sdis_solve_persistent_wavefront_probe`、`sdis_solve_persistent_wavefront_probe_batch` 在 `sdis_solve_wavefront.c` 中有完整实现
- ✅ **API声明正确**: 在 `sdis.h` 中有正确的 `SDIS_API` 声明
- ✅ **CMake链接配置正确**: 所有WF测试都配置为 `target_link_libraries(${test} PRIVATE sdis_obj)`
- ✅ **P0_OPT优化已启用**: `target_compile_definitions(sdis_obj PUBLIC SDIS_P0_OPT)`

**链接失败深层原因**:
链接配置看起来完全正确，但测试仍然失败。可能的原因：
- 条件编译导致函数被排除
- 构建顺序问题
- DLL导出宏在特定配置下的行为差异
- VS2022特定的链接器行为

## 下一步调查计划

### 1. WF函数链接问题解决 (优先级1)
- [ ] 验证是否需要与`sdis_obj`链接
- [ ] 检查CMakeLists.txt中的链接库配置
- [ ] 验证DLL导出符号表是否包含WF函数
- [ ] 确认函数实现的条件编译设置

### 2. 架构决策确认 (优先级2)  
- [ ] 确认B4系列测试的废弃决策
- [ ] 评估是否需要为persistent wavefront重新设计测试
- [ ] 确认WF数值正确性测试的保留价值

### 3. 测试框架重构规划 (优先级3)
- [ ] 如果WF测试仍需保留，制定重构计划
- [ ] 考虑基于persistent wavefront的新测试架构
- [ ] 评估和现有`/physical_consistency_stats/`数据的一致性需求

---

*调查报告: 2026-03-12*