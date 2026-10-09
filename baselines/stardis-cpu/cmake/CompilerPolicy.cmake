# ====================================================================
# CompilerPolicy.cmake
# 编译器检查与强制策略
# ====================================================================

# 强制生成器检查：仅允许 Visual Studio 2022（对应规范要求）
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio 17 2022")
    message(WARNING 
        "=================================================================\n"
        "  WARNING: Generator is '${CMAKE_GENERATOR}'\n"
        "  According to cmake_config_principle_v_1.1.md:\n"
        "  Only 'Visual Studio 17 2022' is MANDATORY for this project.\n"
        "  \n"
        "  However, for development convenience, we allow other generators.\n"
        "  If you encounter issues, please use:\n"
        "    cmake -G \"Visual Studio 17 2022\" -A x64 ..\n"
        "=================================================================")
endif()

# 平台检查：强制 x64
if(CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR 
        "Platform must be x64. Use: cmake -G \"Visual Studio 17 2022\" -A x64 ..")
endif()

# 编译器 ID 检查
if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    message(STATUS "Compiler: MSVC (Visual Studio)")
    # MSVC 不支持 C89，调整为 C11
    set(CMAKE_C_STANDARD 11 CACHE STRING "C Standard for MSVC" FORCE)
    # 禁用 MSVC 特定的安全警告（_CRT_SECURE_NO_WARNINGS）
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
    message(STATUS "Compiler: GCC")
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Compiler: Clang")
else()
    message(WARNING "Unknown compiler: ${CMAKE_C_COMPILER_ID}")
endif()

# 输出编译器信息
message(STATUS "=== Compiler Policy ===")
message(STATUS "Generator:        ${CMAKE_GENERATOR}")
message(STATUS "Platform:         ${CMAKE_GENERATOR_PLATFORM}")
message(STATUS "C Compiler:       ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "CXX Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
