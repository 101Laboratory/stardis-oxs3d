# ====================================================================
# ProjectOptions.cmake
# 工程级编译选项与全局变量配置
# ====================================================================

# C/C++ 标准配置（MSVC 需要 C99 最低，其他平台使用 C89）
if(MSVC)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    set(CMAKE_C_STANDARD 89)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)
endif()

# C++ 标准（用于 star-sp 等需要 C++ 的模块）
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 位置无关代码（PIC/PIE）
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 全局输出目录（统一到 build/ 下）
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 测试输出目录
set(CMAKE_TEST_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Tests)

# 选项：启用测试
option(ENABLE_TESTS "Enable building tests" ON)

# 选项：启用 MPI（对应原 config.mk 的 DISTRIB_PARALLELISM）
option(ENABLE_MPI "Enable MPI distributed parallelism" OFF)

# 选项：s3d 后端选择（cubql = cuBQL/custar-3d, optix = OptiX/oxstar-3d）
set(S3D_BACKEND "optix" CACHE STRING "s3d ray-tracing backend: cubql or optix")
set_property(CACHE S3D_BACKEND PROPERTY STRINGS cubql optix)
if(NOT S3D_BACKEND MATCHES "^(cubql|optix)$")
    message(FATAL_ERROR "Invalid S3D_BACKEND='${S3D_BACKEND}'. Must be 'cubql' or 'optix'.")
endif()
message(STATUS "s3d backend: ${S3D_BACKEND}")

# 选项：构建类型（对应原 config.mk 的 BUILD_TYPE）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()

# 平台特定编译选项
if(MSVC)
    # MSVC 警告和安全标志
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:/W4>              # 警告级别 4
        $<$<COMPILE_LANGUAGE:C,CXX>:/GS>              # 缓冲区安全检查
        $<$<COMPILE_LANGUAGE:C,CXX>:/guard:cf>        # 控制流防护
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4200>          # 禁用：零长数组
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4204>          # 禁用：非常量聚合初始化器
        $<$<COMPILE_LANGUAGE:C,CXX>:/wd4221>          # 禁用：可变参数
    )
else()
    # GCC/Clang 警告标志（对应原 WFLAGS）
    add_compile_options(
        -Wall
        -Wextra
        -Wshadow
        -Wcast-align
        -Wconversion
        -Wmissing-declarations
        -Wmissing-prototypes
        -pedantic
    )
    
    # GCC/Clang 安全强化标志（对应原 CFLAGS_HARDENED）
    add_compile_options(
        -D_FORTIFY_SOURCE=2
        -fcf-protection=full
        -fstack-clash-protection
        -fstack-protector-strong
        -fvisibility=hidden
        -fstrict-aliasing
    )
endif()

# 构建类型特定选项
# 注意：对于MSVC，使用CMAKE_C_FLAGS_<CONFIG>而不是add_compile_options()
# 因为add_compile_options()是全局的，会应用到所有配置导致冲突
if(MSVC)
    # Release模式 - 带调试符号（/Zi 生成PDB，方便Release调试）
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /DNDEBUG /Zi" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG /Zi" CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF" CACHE STRING "" FORCE)
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
    
    # CUDA Release模式 - 添加 -lineinfo（行号调试信息，性能影响极小）+ 主机端 /Zi
    set(CMAKE_CUDA_FLAGS_RELEASE "${CMAKE_CUDA_FLAGS_RELEASE} -lineinfo -Xcompiler=/Zi" CACHE STRING "" FORCE)
    
    # Debug模式 - MSVC默认已包含/Od /RTC1，只需添加/Zi
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Zi" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi" CACHE STRING "" FORCE)
else()
    # GCC/Clang - 可以安全使用add_compile_options
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
        add_link_options(-s -Wl,-z,relro,-z,now)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g)
        add_link_options(-Wl,-z,relro,-z,now)
    endif()
endif()

# Windows 平台特殊处理
if(WIN32)
    # 定义 OS_WINDOWS 宏（用于条件编译）
    add_compile_definitions(OS_WINDOWS)
endif()

# OpenMP 支持（对应原 config.mk 的 -fopenmp）
find_package(OpenMP)
if(OpenMP_C_FOUND)
    # OpenMP 将通过 target_link_libraries 在项目级添加
    message(STATUS "OpenMP found: ${OpenMP_C_VERSION}")
else()
    message(WARNING "OpenMP not found. Some modules may fail to build.")
endif()

# 消息输出
message(STATUS "=== Project Options ===")
message(STATUS "C Standard:       C${CMAKE_C_STANDARD}")
message(STATUS "CXX Standard:     C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "Enable Tests:     ${ENABLE_TESTS}")
message(STATUS "Enable MPI:       ${ENABLE_MPI}")
message(STATUS "Output Dirs:")
message(STATUS "  Libraries:      ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Executables:    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  Tests:          ${CMAKE_TEST_OUTPUT_DIRECTORY}")
