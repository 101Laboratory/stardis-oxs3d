# ====================================================================
# rsys - 基础系统库
# ====================================================================
# 版本: 2.0.0 (基于 Workspace 统一管理模板)
# 创建日期: 2026-01-21
# 备份位置: cmake_backup/20260120_pre_template_migration/
# ====================================================================

cmake_minimum_required(VERSION 3.25)

project(rsys
    VERSION 0.15.0
    DESCRIPTION "Rsys: 基础系统库 - 内存管理、数据结构、线程、日志等"
    LANGUAGES C
)

# ====================================================================
# 辅助函数加载（支持单独构建和 Workspace 构建）
# ====================================================================
if(NOT COMMAND add_module_tests)
    # 尝试从父目录找到 cmake 模块
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
    else()
        message(FATAL_ERROR "Cannot find HelperFunctions.cmake")
    endif()
endif()

# ====================================================================
# 测试选项（单独构建时定义默认值）
# ====================================================================
if(NOT DEFINED ENABLE_TESTS)
    option(ENABLE_TESTS "Enable building tests" ON)
endif()

# ====================================================================
# 源码文件列表
# ====================================================================
# 注意：平台特定源码（Windows 线程 vs pthread）

if(WIN32)
    set(RSYS_SOURCES
        src/clock_time.c
        src/cstr.c
        src/hash.c
        src/image.c
        src/library.c
        src/logger.c
        src/mem_allocator.c
        src/mem_lifo_allocator.c
        src/mem_proxy_allocator.c
        src/windows/windows_condition.c
        src/windows/windows_mutex.c
        src/quaternion.c
        src/str.c
        src/text_reader.c
    )
else()
    set(RSYS_SOURCES
        src/clock_time.c
        src/cstr.c
        src/hash.c
        src/image.c
        src/library.c
        src/logger.c
        src/mem_allocator.c
        src/mem_lifo_allocator.c
        src/mem_proxy_allocator.c
        src/pthread/pthread_condition.c
        src/pthread/pthread_mutex.c
        src/quaternion.c
        src/str.c
        src/text_reader.c
    )
endif()

# 头文件列表（用于 IDE 显示）
file(GLOB RSYS_HEADERS
    src/*.h
    src/windows/*.h
    src/pthread/*.h
)

# ====================================================================
# 库目标定义
# ====================================================================
# 库类型由 Workspace 级或命令行参数控制（BUILD_SHARED_LIBS）
if(NOT DEFINED BUILD_SHARED_LIBS)
    option(BUILD_SHARED_LIBS "Build shared library instead of static" ON)
endif()

if(BUILD_SHARED_LIBS)
    add_library(rsys SHARED ${RSYS_SOURCES})
    # 定义共享库构建宏（用于符号导出）
    target_compile_definitions(rsys PRIVATE RSYS_SHARED_BUILD)
else()
    add_library(rsys STATIC ${RSYS_SOURCES})
endif()

# 设置库属性
set_target_properties(rsys PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# 别名目标（保持兼容性）
add_library(rsys::rsys ALIAS rsys)

# ====================================================================
# Generate Standard Include Structure (rsys/ namespace)
# ====================================================================

set(RSYS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public headers to expose under rsys/ namespace
file(GLOB RSYS_ALL_HEADERS src/*.h)

# Filter out test headers and internal headers
set(RSYS_PUBLIC_HEADERS_LIST)
foreach(header ${RSYS_ALL_HEADERS})
    get_filename_component(header_name ${header} NAME)
    # Exclude test headers
    if(NOT header_name MATCHES "^test_")
        list(APPEND RSYS_PUBLIC_HEADERS_LIST ${header})
    endif()
endforeach()

# Create include/rsys/ structure in build directory
foreach(header ${RSYS_PUBLIC_HEADERS_LIST})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${header}
        ${RSYS_GENERATED_INCLUDE_DIR}/rsys/${header_name}
        COPYONLY
    )
endforeach()

# ====================================================================
# Include 路径配置
# ====================================================================
target_include_directories(rsys
    PUBLIC
        # Generated structure for <rsys/rsys.h>
        $<BUILD_INTERFACE:${RSYS_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ====================================================================
# 平台特定依赖
# ====================================================================
if(WIN32)
    # Windows 无需额外库（使用原生 API）
else()
    # Unix/Linux 需要 pthread 和数学库
    find_package(Threads REQUIRED)
    target_link_libraries(rsys PUBLIC Threads::Threads m)
endif()

# ====================================================================
# 测试配置（由 Workspace 级 ENABLE_TESTS 控制）
# ====================================================================
if(ENABLE_TESTS)
    enable_testing()
    
    # 测试源码列表（从原 Makefile 的 TEST_SRC 变量提取）
    set(RSYS_TEST_SOURCES
        src/test_algorithm.c
        src/test_atomic.c
        src/test_binary_heap.c
        src/test_condition.c
        src/test_cstr.c
        src/test_double22.c
        src/test_double2.c
        src/test_double33.c
        src/test_double3.c
        src/test_double44.c
        src/test_double4.c
        src/test_dynamic_array.c
        src/test_endianness.c
        src/test_float22.c
        src/test_float2.c
        src/test_float33.c
        src/test_float3.c
        src/test_float44.c
        src/test_float4.c
        src/test_free_list.c
        src/test_func_name.c
        src/test_hash_table.c
        src/test_hash_sha256.c
        src/test_image.c
        src/test_library.c
        src/test_list.c
        src/test_logger.c
        src/test_math.c
        src/test_mem_allocator.c
        src/test_misc.c
        src/test_morton.c
        src/test_mutex.c
        src/test_quaternion.c
        src/test_ref.c
        src/test_signal.c
        src/test_str.c
        src/test_stretchy_array.c
        src/test_text_reader.c
        src/test_time.c
        src/test_vmacros.c
    )
    
    # 批量添加测试（使用修复后的辅助函数）
    add_module_tests(
        SOURCES ${RSYS_TEST_SOURCES}
        LIBRARIES rsys
    )
    
    # ----------------------------------------------------------------
    # 特殊处理：test_library 需要预编译的 test_lib.dll
    # ----------------------------------------------------------------
    if(TARGET test_library AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_lib.dll")
        # 复制预编译的 test_lib.dll 到测试可执行文件目录
        add_custom_command(TARGET test_library POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/test_lib.dll"
                $<TARGET_FILE_DIR:test_library>
            COMMENT "Deploying pre-built test_lib.dll for test_library"
        )
    endif()
endif()

# ====================================================================
# 安装配置
# ====================================================================
include(GNUInstallDirs)

# 安装库文件
install(TARGETS rsys
    EXPORT rsysTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装头文件到 include/rsys/
install(DIRECTORY src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rsys
    FILES_MATCHING PATTERN "*.h"
    PATTERN "test_*.h" EXCLUDE
    PATTERN "windows" EXCLUDE
    PATTERN "pthread" EXCLUDE
)

# 生成并安装 CMake 包配置文件
install(EXPORT rsysTargets
    FILE rsysTargets.cmake
    NAMESPACE rsys::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rsys
)

# 生成版本文件
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rsysConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# 安装配置文件
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/rsysConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rsys
)
