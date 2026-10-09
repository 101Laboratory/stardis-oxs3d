# 解决方案

## 执行日期
2026-03-13

## 解决方法

### Phase A（提交 a5dc4bb）
**F2 修复**：从死代码块中提取 probe wrapper 函数
- 从 `sdis_solve_wavefront.c` 的 `#else !SDIS_P0_OPT` 死块中提取：
  - `sdis_solve_persistent_wavefront_probe()` (88 行)
  - `sdis_solve_persistent_wavefront_probe_batch()` (105 行)
- 移至 `sdis_solve_persistent_wavefront.c` 末尾
- 避免 20+ 测试链接失败
- 构建验证：零错误（仅 LNK4217 警告）

### Phase B（提交 bfcebb4）
**文件删除（F1, F3, F4, F5, F6, F10）**：
- **F1**: `sdis_solve_wavefront.c` (1,725 行) - 旧 per-tile wavefront solver
- **F3**: `sdis_solve_wavefront.h` (165 行) - wavefront_context 结构和声明
- **F4**: `sdis_wf_soa.{h,c}`, `test_sdis_dispatch_soa.c` (183 行) - dispatch_soa SoA 布局
- **F5**: `sdis_ray_sort.{h,c}`, `test_sdis_ray_sort.c` (277 行) - 从未编译的射线排序
- **F6**: `sdis_wf_steps.c.bak` (3,000 行) - 拆分前的统一步骤实现备份
- **F10**: `custar-3d/` (70 文件, 14,400 行) - 废弃的 CUDA BVH 后端

**代码清理（F7, F8, F9）**：
- **F7**: `sdis_solve_camera.c` L667-790 (125 行) - 删除 goto 后不可达的 Phase B-2 tile 循环
- **F8**: `count_path_rays()` (5 行) - 删除死 #else 分支（访问已移动的 path_state 字段）
- **F9**: `sdis.h` (5 行) - 删除 `sdis_solve_wavefront_probe()` 声明（无定义/调用者）

### Phase C（包含在 bfcebb4）
**构建系统清理**：
- 删除 `target_compile_definitions(sdis_obj PUBLIC SDIS_P0_OPT)` - 宏已废弃
- 删除 `S3D_BACKEND` CMake 选项 - custar-3d 删除后仅保留 OptiX
- 从 `SDIS_SOURCES` 中删除 `sdis_solve_wavefront.c`, `sdis_wf_soa.c`
- 从 CMakeLists.txt 中删除 `test_sdis_dispatch_soa` 测试注册
- 修复 21 个文件中的 `#include "sdis_solve_wavefront.h"`：
  - `sdis_solve_persistent_wavefront.h`: 替换为 `sdis_wf_types.h` + `sdis_wf_state.h` + `sdis_wf_hot.h`
  - `sdis_solve_camera.c`: 直接删除（已包含 persistent_wavefront.h）
  - 9 个 `sdis_wf_steps_*.c`: 删除（未使用 wavefront_context）

## 验证

### 构建验证
```bash
cd stardis-oxs3d-dead-code-cleanup/build
cmake -G "Visual Studio 17 2022" -A x64 -DENABLE_TESTS=OFF ..
cmake --build . --config Release -j8
```
- **结果**：零错误，stardis.exe + sdis.dll 成功生成
- **注意**：不再需要 `-DS3D_BACKEND=optix` 参数

### Git 提交
- **a5dc4bb**: `fix(F2): extract probe wrappers from dead code block`
- **bfcebb4**: `refactor: remove ~20,015 lines of dead code (F1-F10)`
- **合并至**: `opt/merge-phase` 分支（快进合并）
- **推送至**: `origin/opt/merge-phase` (6dada61..bfcebb4)

### 代码统计
```
95 files changed, 233 insertions(+), 30,907 deletions(-)
```
- **删除**：~20,015 行死代码（F1-F10）
- **添加**：193 行 probe wrappers（F2 修复）+ 构建系统清理

## 预防措施

1. **及时清理 #ifdef 死分支**：SDIS_P0_OPT 宏定义后应立即清理所有 `#else !SDIS_P0_OPT` 分支，而非等到后续重构
2. **公共 API 位置检查**：在删除模块前，使用 `grep -r` 检查导出符号是否被外部依赖（如 F2 的 probe 函数）
3. **死后端及时删除**：custar-3d 后端在 OptiX 方案验证后应立即删除，避免维护负担
4. **构建选项清理**：条件编译宏和 CMake 选项在相关代码路径删除后应同步移除

## 影响范围

- **风险等级**：低（仅删除死代码，活跃路径未修改）
- **测试覆盖**：wf 测试因架构不兼容无法运行（已知问题 merge_phase_test_framework_incompatibility）
- **构建影响**：简化构建流程（移除 S3D_BACKEND 选项）
- **代码维护**：减少 ~20k 行无效代码，降低维护复杂度

## 相关文档

- **设计文档**: `/guide/pwf_design/` - 持久化 wavefront 架构设计
- **已知问题**: `merge_phase_test_framework_incompatibility/` - B4/WF 测试框架不兼容
- **相关优化**: `/optimization/wavefront_pipeline_optimization_plan.md`
