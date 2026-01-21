# ====================================================================
# RuntimeDeps.cmake
# 运行时依赖管理（DLL 复制、第三方运行时部署）
# ====================================================================

# 注册运行时依赖（第三方库）
# 用法：register_runtime_dependency(NAME <name> ROOT <path> TYPE <type> [DLLS <dll_list>])
function(register_runtime_dependency)
    cmake_parse_arguments(ARG "" "NAME;ROOT;TYPE" "DLLS" ${ARGN})

    if(NOT ARG_NAME OR NOT ARG_ROOT)
        message(FATAL_ERROR "register_runtime_dependency requires NAME and ROOT")
    endif()

    # 创建 INTERFACE 目标（仅用于传递 include 路径）
    add_library(${ARG_NAME}_runtime INTERFACE)

    # 暴露 include 目录
    if(EXISTS ${ARG_ROOT}/include)
        target_include_directories(${ARG_NAME}_runtime
            INTERFACE ${ARG_ROOT}/include
        )
    endif()

    # 记录运行时属性
    set_target_properties(${ARG_NAME}_runtime PROPERTIES
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

# 部署运行时依赖（将 DLL 复制到 exe 输出目录）
# 用法：deploy_runtime_dependencies(TARGET <exe_target>)
function(deploy_runtime_dependencies TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "deploy_runtime_dependencies: TARGET '${TARGET}' does not exist")
    endif()

    # 递归收集所有链接库（包括传递依赖）
    set(ALL_LIBS)
    set(VISITED)
    
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
    
    # 检查全局注册的运行时目标（Embree4_runtime, Random123_runtime 等）
    set(RUNTIME_TARGETS Embree4_runtime Random123_runtime)
    foreach(rt IN LISTS RUNTIME_TARGETS)
        if(TARGET ${rt})
            list(APPEND ALL_LIBS ${rt})
        endif()
    endforeach()

    # 遍历所有库，查找并部署 DLL
    foreach(lib IN LISTS ALL_LIBS)
        if(TARGET ${lib})
            # 方式1：从 RUNTIME_DLLS 属性获取（自定义注册）
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
                    else()
                        message(WARNING "DLL not found: ${dll}")
                    endif()
                endforeach()
            endif()
            
            # 方式2：从 IMPORTED_LOCATION 属性获取（IMPORTED SHARED 库）
            get_target_property(LIB_TYPE ${lib} TYPE)
            if(LIB_TYPE STREQUAL "SHARED_LIBRARY")
                get_target_property(IS_IMPORTED ${lib} IMPORTED)
                if(IS_IMPORTED)
                    get_target_property(DLL_LOCATION ${lib} IMPORTED_LOCATION)
                    if(DLL_LOCATION AND EXISTS "${DLL_LOCATION}")
                        add_custom_command(
                            TARGET ${TARGET} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                ${DLL_LOCATION} $<TARGET_FILE_DIR:${TARGET}>
                            COMMENT "Deploying IMPORTED DLL: ${DLL_LOCATION}"
                        )
                    endif()
                endif()
            endif()
        endif()
    endforeach()
endfunction()
