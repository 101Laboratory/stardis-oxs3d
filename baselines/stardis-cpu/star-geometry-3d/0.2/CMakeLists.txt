# star-geometry-3d Library - CMake Build Configuration
# Version: 0.2.0
# Description: 3D geometry helper library
# Dependencies: rsys, star-3d
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-geometry-3d
    VERSION 0.2.0
    DESCRIPTION "star-geometry-3d: 3D geometry helper library"
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

set(SG3D_SOURCES
    src/sg3d_device.c
    src/sg3d_geometry.c
)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(sg3d SHARED ${SG3D_SOURCES})
    target_compile_definitions(sg3d PRIVATE SG3D_SHARED_BUILD)
else()
    add_library(sg3d STATIC ${SG3D_SOURCES})
endif()

add_library(sg3d::sg3d ALIAS sg3d)

set_target_properties(sg3d PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "sg3d"
)

# ============================================================================
# Generate Standard Include Structure (star/ namespace)
# ============================================================================

set(SG3D_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public header to expose under star/ namespace
set(SG3D_PUBLIC_HEADERS
    src/sg3d.h
    src/sg3d_sencXd_helper.h
    src/sg3d_sdisXd_helper.h
    src/sg3d_sXd_helper.h
)

# Create include/star/ structure in build directory
foreach(header ${SG3D_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${header}
        ${SG3D_GENERATED_INCLUDE_DIR}/star/${header_name}
        COPYONLY
    )
endforeach()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(sg3d
    PUBLIC
        # Generated structure for <star/sg3d.h>
        $<BUILD_INTERFACE:${SG3D_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(sg3d PUBLIC rsys s3d)

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(sg3d PRIVATE m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    set(SG3D_TEST_SOURCES
        src/test_sg3d_cube_behind_cube.c
        src/test_sg3d_cube_in_cube.c
        src/test_sg3d_cube_on_cube.c
        src/test_sg3d_device.c
        src/test_sg3d_geometry_2.c
        src/test_sg3d_geometry.c
        src/test_sg3d_invalid_models.c
        src/test_sg3d_multi_media.c
        src/test_sg3d_unspecified_properties.c
    )
    
    add_module_tests(
        SOURCES ${SG3D_TEST_SOURCES}
        LIBRARIES sg3d
    )
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS sg3d
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${SG3D_PUBLIC_HEADERS}
    DESTINATION include/star
)
