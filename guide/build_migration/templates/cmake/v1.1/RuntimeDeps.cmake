# ====================================================================
# RuntimeDeps.cmake - 运行时依赖管理
# Version: 1.1.1
# Date: 2026-01-21
# ====================================================================
# 用途：管理第三方库的运行时依赖（DLL 复制、运行时部署）
# 适用场景：Embree, CUDA, DirectX, OpenCL, Vulkan 等第三方库
# ====================================================================

# ====================================================================
# 1. 注册运行时依赖
# ====================================================================
# 用法：register_runtime_dependency(
#           NAME <name>          # 依赖名称，如 "Embree4", "CUDA"
#           ROOT <path>          # 根目录
#           TYPE <type>          # SHARED / HEADER_ONLY / STATIC
#           [DLLS <dll_list>]    # DLL 列表（Windows）
#       )
# 
# 示例：
#   register_runtime_dependency(
#       NAME Embree4
#       ROOT ${EMBREE4_ROOT}
#       TYPE SHARED
#       DLLS 
#           ${EMBREE4_ROOT}/bin/embree4.dll
#           ${EMBREE4_ROOT}/bin/tbb12.dll
#   )
# ====================================================================
function(register_runtime_dependency)
    cmake_parse_arguments(ARG "" "NAME;ROOT;TYPE" "DLLS" ${ARGN})

    if(NOT ARG_NAME OR NOT ARG_ROOT)
        message(FATAL_ERROR "register_runtime_dependency requires NAME and ROOT")
    endif()

    # 创建 INTERFACE 目标（用于传递 include 路径和 DLL 列表）
    set(TARGET_NAME ${ARG_NAME}_runtime)
    
    if(NOT TARGET ${TARGET_NAME})
        add_library(${TARGET_NAME} INTERFACE)
    endif()

    # 暴露 include 目录
    if(EXISTS ${ARG_ROOT}/include)
        target_include_directories(${TARGET_NAME}
            INTERFACE ${ARG_ROOT}/include
        )
    endif()

    # 记录运行时属性
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_DLLS "${ARG_DLLS}"
        RUNTIME_ROOT "${ARG_ROOT}"
        RUNTIME_TYPE "${ARG_TYPE}"
    )

    message(STATUS "Registered runtime dependency: ${ARG_NAME} (${ARG_TYPE})")
    message(STATUS "  Root: ${ARG_ROOT}")
    if(ARG_DLLS)
        message(STATUS "  DLLs: ${ARG_DLLS}")
    endif()
endfunction()

# ====================================================================
# 2. 部署运行时依赖
# ====================================================================
# 用法：deploy_runtime_dependencies(TARGET <exe_target>)
# 
# 说明：
# - 自动查找目标的所有传递依赖
# - 查找已注册的运行时目标（*_runtime）
# - 将 DLL 复制到可执行文件目录
# - 使用 copy_if_different 避免重复复制
# 
# 注意：
# - 必须在测试可执行文件创建后调用
# - 仅支持 Windows 平台
# ====================================================================
function(deploy_runtime_dependencies TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "deploy_runtime_dependencies: TARGET '${TARGET}' does not exist")
    endif()

    # 递归收集所有链接库（包括传递依赖）
    set(ALL_LIBS)
    set(VISITED)
    
    # 内部递归函数
    function(collect_libs lib)
        if(NOT TARGET ${lib})
            return()
        endif()
        
        # 防止循环依赖
        if("${lib}" IN_LIST VISITED)
            return()
        endif()
        list(APPEND VISITED ${lib})
        set(VISITED ${VISITED} PARENT_SCOPE)
        
        list(APPEND ALL_LIBS ${lib})
        set(ALL_LIBS ${ALL_LIBS} PARENT_SCOPE)
        
        # 获取该库的依赖
        get_target_property(DEPS ${lib} LINK_LIBRARIES)
        if(DEPS)
            foreach(dep IN LISTS DEPS)
                collect_libs(${dep})
                set(VISITED ${VISITED} PARENT_SCOPE)
                set(ALL_LIBS ${ALL_LIBS} PARENT_SCOPE)
            endforeach()
        endif()
    endfunction()
    
    # 从目标开始收集
    get_target_property(LINK_LIBS ${TARGET} LINK_LIBRARIES)
    if(LINK_LIBS)
        foreach(lib IN LISTS LINK_LIBS)
            collect_libs(${lib})
        endforeach()
    endif()
    
    # 检查全局注册的运行时目标（*_runtime）
    # 常见第三方库：Embree, CUDA, DirectX, Vulkan, OpenCL 等
    set(COMMON_RUNTIME_TARGETS 
        Embree4_runtime 
        Embree3_runtime
        CUDA_runtime
        DirectX12_runtime
        Vulkan_runtime
        OpenCL_runtime
        Random123_runtime
        MKL_runtime
        TBB_runtime
    )
    
    foreach(rt IN LISTS COMMON_RUNTIME_TARGETS)
        if(TARGET ${rt})
            list(APPEND ALL_LIBS ${rt})
        endif()
    endforeach()

    # 遍历所有库，查找并部署 DLL
    set(DLL_COUNT 0)
    foreach(lib IN LISTS ALL_LIBS)
        if(TARGET ${lib})
            get_target_property(DLLS ${lib} RUNTIME_DLLS)
            if(DLLS)
                foreach(dll IN LISTS DLLS)
                    if(EXISTS "${dll}")
                        add_custom_command(
                            TARGET ${TARGET} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                ${dll} $<TARGET_FILE_DIR:${TARGET}>
                            COMMENT "Deploying third-party DLL: ${dll}"
                        )
                        math(EXPR DLL_COUNT "${DLL_COUNT} + 1")
                    else()
                        message(WARNING "DLL not found: ${dll}")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()
    
    if(DLL_COUNT GREATER 0)
        message(VERBOSE "Deployed ${DLL_COUNT} third-party DLLs for ${TARGET}")
    endif()
endfunction()

# ====================================================================
# 3. 使用示例
# ====================================================================
# 
# # 在 ThirdPartyRegistry.cmake 中注册依赖：
# 
# set(EMBREE4_ROOT "${CMAKE_SOURCE_DIR}/third_party/embree4")
# if(EXISTS "${EMBREE4_ROOT}")
#     register_runtime_dependency(
#         NAME Embree4
#         ROOT ${EMBREE4_ROOT}
#         TYPE SHARED
#         DLLS 
#             ${EMBREE4_ROOT}/bin/embree4.dll
#             ${EMBREE4_ROOT}/bin/tbb12.dll
#             ${EMBREE4_ROOT}/bin/tbbmalloc.dll
#     )
# endif()
# 
# # 在模块的 CMakeLists.txt 中链接：
# 
# target_link_libraries(my_library PUBLIC Embree4_runtime)
# 
# # HelperFunctions.cmake 会自动调用 deploy_runtime_dependencies()
# # 无需手动操作！
# 
# ====================================================================
