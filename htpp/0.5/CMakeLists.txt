# ====================================================================
# htpp Executable - CMake Build Configuration (Template v1.1 Executable)
# ====================================================================
# Version: 0.5.0
# Description: Heat Transfer Post-Processor tool
# Dependencies: rsys, scmap, OpenMP
# Template: cmake v1.1.0 (2026-01-21) - Executable variant
# ====================================================================

cmake_minimum_required(VERSION 3.25)
project(htpp
    VERSION 0.5.0
    DESCRIPTION "htpp: Heat Transfer Post-Processor (IR rendering tool)"
    LANGUAGES C
)

# ====================================================================
# Load Helper Functions
# ====================================================================

if(NOT COMMAND add_module_tests)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
    else()
        message(FATAL_ERROR "Cannot find HelperFunctions.cmake - must build from workspace root")
    endif()
endif()

# ====================================================================
# Options (Standalone Build Support)
# ====================================================================

if(NOT DEFINED ENABLE_TESTS)
    option(ENABLE_TESTS "Enable building tests" OFF)  # htpp is an exe, typically no tests
endif()

# ====================================================================
# Dependencies
# ====================================================================

# rsys dependency
if(NOT TARGET rsys)
    find_package(rsys 0.15 QUIET)
    if(NOT rsys_FOUND)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../rsys/0.15/CMakeLists.txt")
            add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../rsys/0.15 
                             ${CMAKE_BINARY_DIR}/rsys EXCLUDE_FROM_ALL)
            message(STATUS "htpp: Found rsys as sibling directory")
        else()
            message(FATAL_ERROR "htpp: rsys not found. Set rsys_ROOT or place in sibling directory")
        endif()
    endif()
endif()

# scmap dependency
if(NOT TARGET scmap)
    find_package(scmap 0.1 QUIET)
    if(NOT scmap_FOUND)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../star-cmap/0.1/CMakeLists.txt")
            add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../star-cmap/0.1 
                             ${CMAKE_BINARY_DIR}/scmap EXCLUDE_FROM_ALL)
            message(STATUS "htpp: Found scmap as sibling directory")
        else()
            message(FATAL_ERROR "htpp: scmap not found. Set scmap_ROOT or place in sibling directory")
        endif()
    endif()
endif()

# OpenMP dependency (required for parallel heat transfer computation)
find_package(OpenMP REQUIRED COMPONENTS C)
if(NOT OpenMP_C_FOUND)
    message(FATAL_ERROR "OpenMP is required for htpp but was not found")
endif()

# ====================================================================
# Version Header Generation
# ====================================================================

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/htpp_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/htpp_version.h"
    @ONLY
)

# ====================================================================
# Source Files
# ====================================================================

# Explicit list for executable (no need to filter tests for exe)
set(HTPP_SOURCES
    src/htpp.c
)

# Windows POSIX compatibility: getopt implementation
if(WIN32)
    list(APPEND HTPP_SOURCES src/getopt.c)
endif()

# ====================================================================
# Executable Target
# ====================================================================

add_executable(htpp ${HTPP_SOURCES})

# Define Windows POSIX compatibility macros
if(WIN32)
    target_compile_definitions(htpp PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )
endif()

# ====================================================================
# Include Directories
# ====================================================================

# PRIVATE include paths (executable doesn't expose headers)
target_include_directories(htpp PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}     # Generated version header
    ${CMAKE_CURRENT_SOURCE_DIR}/src # getopt.h on Windows
)

# ====================================================================
# Dependencies Linking
# ====================================================================

# Link dependencies (all PRIVATE for executable)
# NOTE: No need to manually add include paths - they propagate through targets
target_link_libraries(htpp PRIVATE
    rsys
    scmap
    OpenMP::OpenMP_C
)

# Math library (Unix only)
if(NOT MSVC)
    target_link_libraries(htpp PRIVATE m)
endif()

# ====================================================================
# Output Properties
# ====================================================================

set_target_properties(htpp PROPERTIES
    OUTPUT_NAME "htpp"
    VERSION ${PROJECT_VERSION}
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ====================================================================
# Windows DLL Deployment
# ====================================================================

if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    # CMake 3.21+: Automatic runtime DLL collection
    add_custom_command(TARGET htpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:htpp>
            $<TARGET_FILE_DIR:htpp>
        COMMAND_EXPAND_LISTS
        COMMENT "Deploying runtime DLLs for htpp"
    )
elseif(WIN32)
    # Fallback: Manual DLL copy for direct dependencies
    foreach(dep rsys scmap)
        if(TARGET ${dep})
            get_target_property(dep_type ${dep} TYPE)
            if(NOT dep_type STREQUAL "STATIC_LIBRARY")
                add_custom_command(TARGET htpp POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${dep}>
                        $<TARGET_FILE_DIR:htpp>
                    COMMENT "Copying ${dep} DLL to htpp executable directory"
                )
            endif()
        endif()
    endforeach()
    
    message(STATUS "Windows DLL deployment configured for htpp executable")
endif()

# ====================================================================
# Installation
# ====================================================================

install(TARGETS htpp
    RUNTIME DESTINATION bin
)

# Install documentation
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
    install(FILES COPYING
        DESTINATION share/doc/htpp
        OPTIONAL
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    install(FILES README.md
        DESTINATION share/doc/htpp
        OPTIONAL
    )
endif()

# Install man page (Unix-like systems only)
if(UNIX AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/htpp.1")
    install(FILES htpp.1
        DESTINATION share/man/man1
        OPTIONAL
    )
endif()

# ====================================================================
# Configuration Summary
# ====================================================================

message(STATUS "========================================== ")
message(STATUS "htpp Configuration Summary")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Dependencies:")
message(STATUS "    - rsys: Found")
message(STATUS "    - scmap: Found")
message(STATUS "    - OpenMP: ${OpenMP_C_VERSION}")
message(STATUS "  Output: Executable (heat transfer post-processor)")
message(STATUS "  Target Directory: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "==========================================")
