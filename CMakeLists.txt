# ====================================================================
# STARDIS-CPU 工程级 CMakeLists.txt (Workspace Level)
# ====================================================================
# 项目: STARDIS CUDA版本（多库/多版本 Monte Carlo 辐射传输求解器）
# 模板: CMake v1.1.0 (数组+循环模式)
# 版本: 1.0.0
# 创建日期: 2026-02-10
# ====================================================================
# 变更说明：
# 暂无
# ====================================================================

cmake_minimum_required(VERSION 3.25)

# ====================================================================
# 1. 工程定义（Workspace Orchestrator）
# ====================================================================
project(STARDIS_CUDA_Workspace
    VERSION 0.16.2
    DESCRIPTION "STARDIS Monte Carlo Radiative Transfer Solver - CUDA Edition (13 Modules with htpp)"
    LANGUAGES C CXX
)

# ====================================================================
# 2. 工程级 CMake 模块路径
# ====================================================================
list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_SOURCE_DIR}/cmake
)

# ====================================================================
# 3. 加载工程基础设施（必须按顺序）
# ====================================================================
include(ProjectOptions)       # 编译选项、输出目录、全局开关
include(CompilerPolicy)       # 编译器检查、生成器验证
include(RuntimeDeps)          # 运行时依赖管理（DLL 复制等）
include(ThirdPartyRegistry)   # 第三方库注册（Random123, Embree, MPI CUDA）
include(HelperFunctions)      # 辅助函数（简化项目级配置）

# ====================================================================
# 3.5 启用 CTest 测试基础设施（Workspace 级）
# ====================================================================
# 必须在根目录调用 enable_testing() 才能使 ctest 命令在 build/ 根目录工作
# 子模块的 enable_testing() 只在子目录生效
if(ENABLE_TESTS)
    enable_testing()
    message(STATUS "CTest infrastructure enabled at workspace level")
endif()

# Check if any module has B4/WF tests enabled
get_property(ENABLE_B4_WF_TESTS CACHE ENABLE_B4_WF_TESTS PROPERTY VALUE)
if(ENABLE_B4_WF_TESTS AND NOT ENABLE_TESTS)
    enable_testing()
    message(STATUS "CTest infrastructure enabled for B4/WF tests only")
endif()

# ====================================================================
# 4. 第三方依赖注册（工程级唯一位置）
# ====================================================================
# 注意：ThirdPartyRegistry.cmake 已处理 Embree、cuBQL、Random、MPI
# 如需添加其他第三方库，在此注册

# 示例：Embree（如果已解压到 third_party/embree）
# if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/embree)
#     register_runtime_dependency(
#         NAME Embree
#         ROOT ${CMAKE_SOURCE_DIR}/third_party/embree
#         TYPE SHARED
#         DLLS ${CMAKE_SOURCE_DIR}/third_party/embree/bin/embree4.dll
#     )
# endif()

# ====================================================================
# 5. 内部项目模块加载（数组+循环模式 - v1.2.0）
# ====================================================================
# 说明：加载13个模块（11个核心 + star-cmap + htpp）
# 依赖顺序：按照dependency_graph.md中的依赖关系排列
# 可执行文件：stardis (主求解器) + htpp (热传输后处理器)
# ====================================================================

# --------------------------------------------------------------------
# 5.1 模块加载辅助宏（使用宏而非函数以避免作用域问题）
# --------------------------------------------------------------------
macro(load_module path display requirement)
    set(_cmake_file "${CMAKE_SOURCE_DIR}/${path}/CMakeLists.txt")
    
    if(EXISTS ${_cmake_file})
        add_subdirectory(${path})
        message(STATUS "  [✓] ${display}")
    else()
        if("${requirement}" STREQUAL "REQUIRED")
            message(FATAL_ERROR "  [✗] ${display} not found but marked REQUIRED!")
        else()
            message(STATUS "  [ ] ${display} (optional, not found)")
        endif()
    endif()
endmacro()

# --------------------------------------------------------------------
# 5.2 按依赖层次顺序加载所有模块
# --------------------------------------------------------------------
# 依赖层次（从底向上）：
#   Layer 1: rsys (基础工具库)
#   Layer 2: star-2d, custar-3d, star-sp (几何和采样)
#   Layer 3: star-enclosures-2d, star-enclosures-3d (包壳计算)
#   Layer 4: star-wf, star-geometry-3d, star-stl, star-cmap (工具库)
#   Layer 5: stardis-solver (核心求解器)
#   Layer 6: stardis (主应用程序)
#   Layer 7: htpp (热传输后处理器可执行文件)
# --------------------------------------------------------------------

message(STATUS "")
message(STATUS "====================================================")
message(STATUS "Loading 13 Modules (11 Core + star-cmap + htpp)")
message(STATUS "====================================================")

# --------------------------------------------------------------------
# Layer 1: Base Runtime (1/11)
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "--- Layer 1: Base Runtime (1/11) ---")
load_module("rsys/0.15" "rsys v0.15 - Base runtime system" "REQUIRED")

# --------------------------------------------------------------------
# Layer 2: Geometry & Sampling (4/11)
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "--- Layer 2: Geometry & Sampling (4/11) ---")
load_module("star-2d/0.7" "star-2d v0.7 (s2d) - 2D geometry" "REQUIRED")
load_module("star-3dut/0.4" "star-3dut v0.4 (s3dut) - 3D utility meshes" "REQUIRED")
# s3d backend: cuBQL (custar-3d) or OptiX (oxstar-3d), selected by -DS3D_BACKEND=cubql|optix
if(S3D_BACKEND STREQUAL "optix")
    load_module("oxstar-3d/0.10" "oxstar-3d v0.10 (s3d) - OptiX GPU backend (replaces star-3d)" "REQUIRED")
else()
    load_module("custar-3d/0.10" "custar-3d v0.10 (s3d) - cuBQL GPU backend (replaces star-3d)" "REQUIRED")
endif()
load_module("star-sp/0.15" "star-sp v0.15 (ssp) - Sampling + Random123" "REQUIRED")

# --------------------------------------------------------------------
# Layer 3: Enclosures (2/11)
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "--- Layer 3: Enclosures (2/11) ---")
load_module("star-enclosures-2d/0.6" "star-enclosures-2d v0.6 (senc2d)" "REQUIRED")
load_module("star-enclosures-3d/0.7.2" "star-enclosures-3d v0.7.2 (senc3d)" "REQUIRED")

# --------------------------------------------------------------------
# Layer 4: Utilities (4/13)
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "--- Layer 4: Utilities (4/13) ---")
load_module("star-wf/0.0" "star-wf v0.0 (swf) - Wavefront format" "REQUIRED")
load_module("star-geometry-3d/0.2" "star-geometry-3d v0.2 (sg3d)" "REQUIRED")
load_module("star-stl/0.7" "star-stl v0.7 (sstl) - STL file I/O" "REQUIRED")
load_module("star-cmap/0.1" "star-cmap v0.1 (scmap) - Color mapping library" "REQUIRED")

# --------------------------------------------------------------------
# Layer 5: Core Solver (1/13)
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "--- Layer 5: Core Solver (1/13) ---")
load_module("stardis-solver/0.16.2" "stardis-solver v0.16.2 (sdis) - CORE SOLVER" "REQUIRED")

# --------------------------------------------------------------------
# Layer 6: Main Application (1/13)
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "--- Layer 6: Main Application (1/13) ---")
load_module("stardis/0.12" "stardis v0.12 - MAIN APPLICATION" "REQUIRED")

# --------------------------------------------------------------------
# Layer 7: Post-Processing Tools (1/13)
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "--- Layer 7: Post-Processing Tools (1/13) ---")
load_module("htpp/0.5" "htpp v0.5 - Heat Transfer Post-Processor (requires OpenMP)" "REQUIRED")

# ====================================================================
# 6. 构建后处理
# ====================================================================
message(STATUS "")
message(STATUS "====================================================")
message(STATUS "All 13 Modules Loaded Successfully")
message(STATUS "====================================================")
message(STATUS "")
message(STATUS "=== CTest Configuration ===")
if(ENABLE_TESTS)
    message(STATUS "Testing:          ENABLED")
    message(STATUS "Run tests with:   ctest --build-config ${CMAKE_BUILD_TYPE}")
    message(STATUS "Quick test:       ctest -C ${CMAKE_BUILD_TYPE} -R \"^test_rsys\"")
else()
    message(STATUS "Testing:          DISABLED")
    message(STATUS "Enable with:      -DENABLE_TESTS=ON")
endif()
message(STATUS "")

# ====================================================================
# 7. Workspace 配置摘要
# ====================================================================
message(STATUS "=======================================================")
message(STATUS "STARDIS-CPU Workspace Configuration Summary")
message(STATUS "=======================================================")
message(STATUS "Project:          ${PROJECT_NAME}")
message(STATUS "Version:          ${PROJECT_VERSION}")
message(STATUS "Description:      13 Modules (stardis + htpp executables, ${S3D_BACKEND} s3d backend)")
message(STATUS "-------------------------------------------------------")
message(STATUS "Build Type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "Generator:        ${CMAKE_GENERATOR}")
message(STATUS "C Compiler:       ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "CXX Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C Standard:       C${CMAKE_C_STANDARD}")
message(STATUS "CXX Standard:     C++${CMAKE_CXX_STANDARD}")
message(STATUS "-------------------------------------------------------")
message(STATUS "Options:")
message(STATUS "  Enable Tests:   ${ENABLE_TESTS}")
message(STATUS "  Enable MPI:     ${ENABLE_MPI}")
message(STATUS "  Shared Libs:    ${BUILD_SHARED_LIBS}")
message(STATUS "-------------------------------------------------------")
message(STATUS "Output Directories:")
message(STATUS "  Libraries:      ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Executables:    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  Tests:          ${CMAKE_TEST_OUTPUT_DIRECTORY}")
message(STATUS "=======================================================")
message(STATUS "")

# ====================================================================
# 8. 模块清单（已加载）
# ====================================================================
message(STATUS "=== Loaded Modules (13/13) ===")
message(STATUS "  1. rsys             v0.15    - Base runtime system (40 tests)")
message(STATUS "  2. star-2d          v0.7     - 2D geometry library")
if(S3D_BACKEND STREQUAL "optix")
message(STATUS "  3. oxstar-3d        v0.10    - OptiX GPU backend (s3d target, replaces star-3d)")
else()
message(STATUS "  3. custar-3d        v0.10    - cuBQL GPU backend (s3d target, replaces star-3d)")
endif()
message(STATUS "  4. star-sp          v0.15    - Sampling library (Random123)")
message(STATUS "  6. senc2d           v0.6     - 2D enclosures (14 tests)")
message(STATUS "  7. senc3d           v0.7.2   - 3D enclosures (17 tests)")
message(STATUS "  8. star-wf          v0.0     - Wavefront format (2 tests)")
message(STATUS "  9. star-geometry-3d v0.2     - 3D geometry tools (9 tests)")
message(STATUS " 10. star-stl         v0.7     - STL file I/O (5 tests)")
message(STATUS " 11. star-cmap        v0.1     - Color mapping (47 palette tests)")
message(STATUS " 12. stardis-solver   v0.16.2  - Core solver (28 tests, 3 failures known)")
message(STATUS " 13. stardis          v0.12    - Main application (EXE)")
message(STATUS " 14. htpp             v0.5     - Heat transfer post-processor (EXE)")
message(STATUS "===============================")
message(STATUS "")

# ====================================================================
# 9. 规范合规性声明
# ====================================================================
# 本文件遵循以下规范：
# - cmake_config_principle_v_1.1.md（源码级依赖配置规范）
# - dependency_graph.md（依赖关系文档）
#
# 关键合规点：
# [√] 2.1  Out-of-source build（build/ 目录）
# [√] 3.1  模块名来自 project(<MODULE_NAME>)
# [√] 4.1  源码位于 src/ 目录
# [√] 5.1  头文件通过 target_include_directories() 暴露
# [√] 6.1  依赖通过 <MODULE>_ROOT 变量发现
# [√] 7.1  依赖通过 add_subdirectory() 接入
# [√] 8.1  Target 定义在项目级 CMakeLists.txt
# [√] 9    输出到 build/bin 和 build/lib
# [√] 10.1 测试通过 ENABLE_TESTS 控制
# [√] 12   GPU 扩展预留（ThirdPartyRegistry.cmake）
#
# 迁移状态：
# - 已完成：13个模块（rsys, s2d, s3d, senc2d, ssp, senc3d, swf, sg3d, sstl, scmap, sdis, stardis, htpp）
# - 未迁移：15个工具模块（polygon, aw等，不在本workspace范围）
# - 测试状态：180+ tests (新增47个color palette tests)
# - 可执行文件：stardis.exe (主求解器) + htpp.exe (热传输后处理器)
#
# 注意事项：
# - 生成器检查在 CompilerPolicy.cmake 中（警告模式，而非强制失败）
# - 依赖顺序严格按照 dependency_graph.md 排列
# - stardis-solver 3个volumic power测试失败（已知问题，暂不修复）
# ====================================================================
