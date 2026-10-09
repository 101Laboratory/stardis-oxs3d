# ====================================================================
# HelperFunctions.cmake - CMake 辅助函数库
# Version: 1.1.0
# Date: 2026-01-21
# ====================================================================
# 用途：简化模块级 CMakeLists.txt，提供标准化的构建逻辑
# 规范：基于 cmake_template_audit.md 改进建议
# ====================================================================

# ====================================================================
# ⚠️  CRITICAL GUIDELINES - 模块开发者必读！
# ====================================================================
# [RULE 1] 库源文件 MUST 使用显式列表，禁止 file(GLOB src/*.c)
#          ❌ 错误：file(GLOB LIB_SOURCES src/*.c)
#          ✅ 正确：set(LIB_SOURCES src/foo.c src/bar.c)
#          原因：file(GLOB) 会意外包含 test_*.c，导致符号重定义
#
# [RULE 2] 头文件命名空间 MUST 包含所有公开 API 头文件
#          包括：主头文件、模板头文件、*_undefs.h、*X2d.h 等
#          使用 generate_namespace_headers() 自动处理
#
# [RULE 3] 测试头文件 MUST 排除在公共 include 之外
#          使用 filter_test_headers() 过滤
#          install() 规则添加 PATTERN "test_*.h" EXCLUDE
#
# [RULE 4] MSVC 模板参数 MUST 是编译时常量
#          ❌ 错误：template<int N = Type::min()>  // 函数调用
#          ✅ 正确：template<int N = 0>            // 常量
#          Windows MSVC 比 GCC/Clang 严格，不接受运行时函数调用
# ====================================================================

message(STATUS "Loading HelperFunctions.cmake v1.1.0")

# ====================================================================
# 1. 源文件管理函数
# ====================================================================

# --------------------------------------------------------------------
# 函数：过滤库源文件（排除测试文件）
# 用法：glob_library_sources(OUTPUT_VAR)
# 说明：从 src/*.c 中自动排除 test_*.c 文件
# --------------------------------------------------------------------
macro(glob_library_sources out_var)
    file(GLOB _all_sources CONFIGURE_DEPENDS src/*.c)
    set(${out_var})
    foreach(src ${_all_sources})
        get_filename_component(name ${src} NAME)
        if(NOT name MATCHES "^test_")
            list(APPEND ${out_var} ${src})
        endif()
    endforeach()
    
    # 健全性检查：确保结果非空
    if(NOT ${out_var})
        message(WARNING "glob_library_sources: No source files found in src/")
    endif()
endmacro()

# --------------------------------------------------------------------
# 函数：验证库源文件列表（检测测试文件混入）
# 用法：validate_library_sources(LIB_SOURCES)
# 说明：防止 test_*.c 被错误添加到库源文件
# --------------------------------------------------------------------
function(validate_library_sources sources_var)
    foreach(src ${${sources_var}})
        get_filename_component(name ${src} NAME)
        if(name MATCHES "^test_.*\\.(c|cpp|cxx|cc)$")
            message(FATAL_ERROR 
                "❌ Library sources contain test file: ${src}\n"
                "   Test files must be added via add_module_tests(), not library sources.\n"
                "   If you used file(GLOB), replace it with explicit source list or glob_library_sources()."
            )
        endif()
    endforeach()
endfunction()

# ====================================================================
# 2. 头文件管理函数
# ====================================================================

# --------------------------------------------------------------------
# 函数：过滤测试头文件
# 用法：filter_test_headers(OUTPUT_VAR header1.h header2.h ...)
# 说明：从头文件列表中排除 test_*.h
# --------------------------------------------------------------------
macro(filter_test_headers out_var)
    set(${out_var})
    foreach(h ${ARGN})
        get_filename_component(name ${h} NAME)
        if(NOT name MATCHES "^test_")
            list(APPEND ${out_var} ${h})
        endif()
    endforeach()
endmacro()

# --------------------------------------------------------------------
# 函数：生成命名空间头文件结构
# 用法：generate_namespace_headers(
#           TARGET <target_name>
#           NAMESPACE <namespace>  # 如 "star", "rsys"
#           HEADERS <header1> <header2> ...
#       )
# 说明：在 build/<module>/include/<namespace>/ 下生成头文件副本
#       自动配置 target_include_directories
# --------------------------------------------------------------------
function(generate_namespace_headers)
    cmake_parse_arguments(NS "" "TARGET;NAMESPACE" "HEADERS" ${ARGN})
    
    if(NOT NS_TARGET)
        message(FATAL_ERROR "generate_namespace_headers: TARGET required")
    endif()
    
    if(NOT NS_NAMESPACE)
        message(FATAL_ERROR "generate_namespace_headers: NAMESPACE required (e.g., 'star', 'rsys')")
    endif()
    
    if(NOT NS_HEADERS)
        message(WARNING "generate_namespace_headers: No HEADERS provided for ${NS_TARGET}")
        return()
    endif()
    
    # 创建命名空间目录
    set(NS_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
    
    # 复制每个头文件到命名空间下
    foreach(header ${NS_HEADERS})
        # 支持相对路径（如 src/foo.h）和绝对路径
        if(NOT IS_ABSOLUTE ${header})
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
        endif()
        
        # 检查头文件是否存在
        if(NOT EXISTS ${header})
            message(WARNING "generate_namespace_headers: Header not found: ${header}")
            continue()
        endif()
        
        get_filename_component(name ${header} NAME)
        configure_file(
            ${header}
            ${NS_DIR}/${NS_NAMESPACE}/${name}
            COPYONLY
        )
    endforeach()
    
    # 自动配置 include 路径
    target_include_directories(${NS_TARGET} PUBLIC 
        $<BUILD_INTERFACE:${NS_DIR}>
    )
    
    message(VERBOSE "Generated namespace headers for ${NS_TARGET} in ${NS_NAMESPACE}/")
endfunction()

# ====================================================================
# 3. 测试管理函数
# ====================================================================

# --------------------------------------------------------------------
# 函数：添加单个测试
# 用法：add_module_test(
#           TEST_NAME test_foo
#           SOURCES test_foo.c
#           LIBRARIES module_name [other_libs...]
#       )
# 说明：创建测试可执行文件，自动部署 DLL（Windows）
# --------------------------------------------------------------------
function(add_module_test)
    cmake_parse_arguments(TEST "" "TEST_NAME;SOURCES" "LIBRARIES" ${ARGN})
    
    if(NOT TEST_SOURCES)
        message(FATAL_ERROR "add_module_test: SOURCES required")
    endif()
    
    if(NOT TEST_TEST_NAME)
        message(FATAL_ERROR "add_module_test: TEST_NAME required")
    endif()
    
    # 创建测试可执行文件
    add_executable(${TEST_TEST_NAME} ${TEST_SOURCES})
    
    # 链接库
    if(TEST_LIBRARIES)
        target_link_libraries(${TEST_TEST_NAME} PRIVATE ${TEST_LIBRARIES})
    endif()
    
    # 设置输出目录
    if(DEFINED CMAKE_TEST_OUTPUT_DIRECTORY)
        set_target_properties(${TEST_TEST_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_TEST_OUTPUT_DIRECTORY}
        )
    endif()
    
    # 注册到 CTest
    add_test(NAME ${TEST_TEST_NAME} COMMAND ${TEST_TEST_NAME})
    
    # Windows: 部署运行时 DLL
    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
        # 使用 CMake 3.21+ 的自动运行时 DLL 收集
        add_custom_command(TARGET ${TEST_TEST_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${TEST_TEST_NAME}>
                $<TARGET_FILE_DIR:${TEST_TEST_NAME}>
            COMMAND_EXPAND_LISTS
            COMMENT "Deploying runtime DLLs for ${TEST_TEST_NAME}"
        )
    elseif(WIN32 AND TEST_LIBRARIES)
        # 降级方案：手动复制直接依赖 DLL（传递依赖靠 CMake 自动展开）
        foreach(lib IN LISTS TEST_LIBRARIES)
            if(TARGET ${lib})
                get_target_property(lib_type ${lib} TYPE)
                
                # 只复制共享库和模块库
                if(NOT lib_type STREQUAL "OBJECT_LIBRARY" AND 
                   NOT lib_type STREQUAL "INTERFACE_LIBRARY" AND
                   NOT lib_type STREQUAL "STATIC_LIBRARY")
                    add_custom_command(TARGET ${TEST_TEST_NAME} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${lib}>
                            $<TARGET_FILE_DIR:${TEST_TEST_NAME}>
                        COMMENT "Deploying ${lib} DLL for ${TEST_TEST_NAME}"
                    )
                endif()
            endif()
        endforeach()
    endif()
    
    # 部署第三方运行时依赖（Embree4, Random123 等）
    if(WIN32 AND COMMAND deploy_runtime_dependencies)
        deploy_runtime_dependencies(${TEST_TEST_NAME})
    endif()
endfunction()

# --------------------------------------------------------------------
# 函数：批量添加测试
# 用法：add_module_tests(
#           SOURCES test1.c test2.c ...
#           LIBRARIES module_name [other_libs...]
#       )
# 说明：从源文件列表自动生成测试，文件名作为测试名
# --------------------------------------------------------------------
function(add_module_tests)
    cmake_parse_arguments(TESTS "" "" "SOURCES;LIBRARIES" ${ARGN})
    
    if(NOT TESTS_SOURCES)
        message(WARNING "add_module_tests: No SOURCES provided")
        return()
    endif()
    
    foreach(test_source IN LISTS TESTS_SOURCES)
        # 从文件名推导测试名（去掉扩展名）
        get_filename_component(test_name ${test_source} NAME_WE)
        
        # 调用单测试函数
        add_module_test(
            TEST_NAME ${test_name}
            SOURCES ${test_source}
            LIBRARIES ${TESTS_LIBRARIES}
        )
    endforeach()
endfunction()

# ====================================================================
# 4. 调试辅助函数
# ====================================================================

# --------------------------------------------------------------------
# 函数：打印模块配置摘要
# 用法：print_module_summary(
#           NAME <module_name>
#           VERSION <version>
#           SOURCES <count>
#           TESTS <count>
#       )
# --------------------------------------------------------------------
function(print_module_summary)
    cmake_parse_arguments(SUM "" "NAME;VERSION;SOURCES;TESTS;DEPENDENCIES" "" ${ARGN})
    
    message(STATUS "==========================================")
    message(STATUS "  Module: ${SUM_NAME} v${SUM_VERSION}")
    if(SUM_SOURCES)
        message(STATUS "  Sources: ${SUM_SOURCES} files")
    endif()
    if(SUM_TESTS)
        message(STATUS "  Tests: ${SUM_TESTS} files")
    endif()
    if(SUM_DEPENDENCIES)
        message(STATUS "  Dependencies: ${SUM_DEPENDENCIES}")
    endif()
    message(STATUS "==========================================")
endfunction()

# ====================================================================
# 加载完成标记
# ====================================================================
message(STATUS "✅ HelperFunctions.cmake v1.1.0 loaded")
message(STATUS "   Features: source validation, namespace headers, DLL deployment")
