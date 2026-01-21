# ====================================================================
# ThirdPartyRegistry.cmake
# 第三方依赖注册入口（当前仅作占位，未来可扩展）
# ====================================================================

# 当前占位：未来可扩展 CUDA / DX12 / MKL / Embree 等平台检测逻辑

message(STATUS "=== Third Party Registry ===")

# ----------------------------------------------------------------
# Random123（Header-Only RNG 库）
# ----------------------------------------------------------------
set(RANDOM123_ROOT "${CMAKE_SOURCE_DIR}/random123/v1.14.0" CACHE PATH "Random123 root directory")

if(EXISTS "${RANDOM123_ROOT}/include")
    # 设置全局变量供子模块使用
    set(RANDOM123_INCLUDE_DIR "${RANDOM123_ROOT}/include" CACHE PATH "Random123 include directory")
    
    # 注册运行时依赖（header-only，无DLL）
    register_runtime_dependency(
        NAME Random123
        ROOT ${RANDOM123_ROOT}
        TYPE HEADER_ONLY
    )
    
    message(STATUS "Registered runtime dependency: Random123 (HEADER_ONLY)")
    message(STATUS "  Root: ${RANDOM123_ROOT}")
    message(STATUS "  Include: ${RANDOM123_INCLUDE_DIR}")
else()
    message(WARNING "Random123 not found at ${RANDOM123_ROOT}. star-sp will fail to build.")
endif()

# ----------------------------------------------------------------
# Embree4（Ray Tracing Kernel Library）
# ----------------------------------------------------------------
set(EMBREE4_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/embree4" CACHE PATH "Embree4 root directory")

if(EXISTS "${EMBREE4_ROOT}/include/embree4")
    # 设置全局变量供子模块使用
    set(EMBREE4_INCLUDE_DIR "${EMBREE4_ROOT}/include" CACHE PATH "Embree4 include directory")
    set(EMBREE4_LIBRARY "${EMBREE4_ROOT}/lib/embree4.lib" CACHE FILEPATH "Embree4 library")
    set(TBB12_LIBRARY "${EMBREE4_ROOT}/lib/tbb12.lib" CACHE FILEPATH "TBB12 library")
    
    # 创建 IMPORTED 目标（使 target_link_libraries 可用）
    add_library(embree SHARED IMPORTED)
    set_target_properties(embree PROPERTIES
        IMPORTED_LOCATION "${EMBREE4_ROOT}/bin/embree4.dll"
        IMPORTED_IMPLIB "${EMBREE4_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${EMBREE4_INCLUDE_DIR}"
    )
    
    # 创建 TBB IMPORTED 目标
    add_library(tbb12 SHARED IMPORTED)
    set_target_properties(tbb12 PROPERTIES
        IMPORTED_LOCATION "${EMBREE4_ROOT}/bin/tbb12.dll"
        IMPORTED_IMPLIB "${TBB12_LIBRARY}"
    )
    
    # embree 依赖 tbb12
    target_link_libraries(embree INTERFACE tbb12)
    
    # 注册运行时依赖（DLL复制）
    register_runtime_dependency(
        NAME Embree4
        ROOT ${EMBREE4_ROOT}
        TYPE SHARED
        DLLS 
            ${EMBREE4_ROOT}/bin/embree4.dll
            ${EMBREE4_ROOT}/bin/tbb12.dll
            ${EMBREE4_ROOT}/bin/tbbmalloc.dll
    )
    
    message(STATUS "Registered runtime dependency: Embree4 (SHARED)")
    message(STATUS "  Root: ${EMBREE4_ROOT}")
    message(STATUS "  Include: ${EMBREE4_INCLUDE_DIR}")
    message(STATUS "  Library: ${EMBREE4_LIBRARY}")
    message(STATUS "  IMPORTED target 'embree' created")
else()
    message(WARNING "Embree4 not found at ${EMBREE4_ROOT}. star-2d and star-3d will fail to build.")
endif()

# ----------------------------------------------------------------
# MPI（对应原 config.mk 的 DISTRIB_PARALLELISM）
# ----------------------------------------------------------------
if(ENABLE_MPI)
    find_package(MPI REQUIRED COMPONENTS C)
    if(MPI_C_FOUND)
        message(STATUS "MPI found: ${MPI_C_VERSION}")
        # MPI 将通过 target_link_libraries 在项目级添加
    else()
        message(FATAL_ERROR "MPI enabled but not found. Disable with -DENABLE_MPI=OFF")
    endif()
endif()

# ----------------------------------------------------------------
# 未来扩展示例
# ----------------------------------------------------------------
# CUDA / DX12 / OpenCL / 其他加速库检测可在此添加
