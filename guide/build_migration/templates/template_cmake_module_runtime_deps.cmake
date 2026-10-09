function(register_runtime_dependency)
    cmake_parse_arguments(ARG "" "NAME;ROOT;TYPE" "DLLS" ${ARGN})

    if(NOT ARG_NAME OR NOT ARG_ROOT)
        message(FATAL_ERROR "register_runtime_dependency requires NAME and ROOT")
    endif()

    add_library(${ARG_NAME}_runtime INTERFACE)

    target_include_directories(${ARG_NAME}_runtime
        INTERFACE ${ARG_ROOT}/include
    )

    set_target_properties(${ARG_NAME}_runtime PROPERTIES
        RUNTIME_DLLS "${ARG_DLLS}"
        RUNTIME_ROOT "${ARG_ROOT}"
        RUNTIME_TYPE "${ARG_TYPE}"
    )
endfunction()

function(deploy_runtime_dependencies TARGET)
    get_target_property(LINK_LIBS ${TARGET} LINK_LIBRARIES)
    foreach(lib IN LISTS LINK_LIBS)
        if(TARGET ${lib})
            get_target_property(DLLS ${lib} RUNTIME_DLLS)
            if(DLLS)
                foreach(dll IN LISTS DLLS)
                    add_custom_command(
                        TARGET ${TARGET} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${dll} $<TARGET_FILE_DIR:${TARGET}>
                    )
                endforeach()
            endif()
        endif()
    endforeach()
endfunction()