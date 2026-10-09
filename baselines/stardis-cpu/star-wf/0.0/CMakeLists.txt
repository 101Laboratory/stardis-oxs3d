# star-wf Library - CMake Build Configuration
# Version: 0.0.0
# Description: Wavefront format library
# Dependencies: rsys
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-wf
    VERSION 0.0.0
    DESCRIPTION "star-wf: Wavefront format library"
    LANGUAGES C
)

# ============================================================================
# Load Helper Functions
# ============================================================================

if(NOT COMMAND add_module_tests)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
    else()
        message(FATAL_ERROR "Cannot find HelperFunctions.cmake - must build from workspace root")
    endif()
endif()

# ============================================================================
# Options (Standalone Build Support)
# ============================================================================

if(NOT DEFINED ENABLE_TESTS)
    option(ENABLE_TESTS "Enable building tests" ON)
endif()

if(NOT DEFINED BUILD_SHARED_LIBS)
    option(BUILD_SHARED_LIBS "Build shared library" ON)
endif()

# ============================================================================
# Source Files
# ============================================================================

set(SWF_SOURCES
    src/swf_H.c
    src/swf_tabulation.c
)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(swf SHARED ${SWF_SOURCES})
    target_compile_definitions(swf PRIVATE SWF_SHARED_BUILD)
else()
    add_library(swf STATIC ${SWF_SOURCES})
endif()

add_library(swf::swf ALIAS swf)

set_target_properties(swf PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "swf"
)

# ============================================================================
# Generate Standard Include Structure (star/ namespace)
# ============================================================================

set(SWF_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public header to expose under star/ namespace
set(SWF_PUBLIC_HEADERS
    src/swf.h
)

# Create include/star/ structure in build directory
foreach(header ${SWF_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${header}
        ${SWF_GENERATED_INCLUDE_DIR}/star/${header_name}
        COPYONLY
    )
endforeach()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(swf
    PUBLIC
        # Generated structure for <star/swf.h>
        $<BUILD_INTERFACE:${SWF_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(swf PUBLIC rsys)

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(swf PRIVATE m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    set(SWF_TEST_SOURCES
        src/test_swf_H2d.c
        src/test_swf_H3d.c
    )
    
    add_module_tests(
        SOURCES ${SWF_TEST_SOURCES}
        LIBRARIES swf
    )
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS swf
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${SWF_PUBLIC_HEADERS}
    DESTINATION include/star
)
