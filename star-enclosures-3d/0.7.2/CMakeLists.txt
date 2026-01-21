# star-enclosures-3d Library - CMake Build Configuration
# Version: 0.7.2
# Description: 3D geometric enclosure calculations
# Dependencies: rsys, star-3d, OpenMP
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-enclosures-3d
    VERSION 0.7.2
    DESCRIPTION "star-enclosures-3d: 3D geometric enclosure calculations"
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

set(SENC3D_SOURCES
    src/senc3d_descriptor.c
    src/senc3d_device.c
    src/senc3d_enclosure.c
    src/senc3d_scene.c
    src/senc3d_scene_analyze.c
)

# v1.1: Validate no test files mixed in
validate_library_sources(SENC3D_SOURCES)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(senc3d SHARED ${SENC3D_SOURCES})
    target_compile_definitions(senc3d PRIVATE SENC3D_SHARED_BUILD)
else()
    add_library(senc3d STATIC ${SENC3D_SOURCES})
endif()

add_library(senc3d::senc3d ALIAS senc3d)

set_target_properties(senc3d PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "senc3d"
)

# ============================================================================
# Generate Standard Include Structure (star/ namespace)
# ============================================================================

set(SENC3D_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public headers to expose under star/ namespace
set(SENC3D_PUBLIC_HEADERS
    src/senc3d.h
    src/sencX3d.h
    src/sencX3d_undefs.h
)

# Create include/star/ structure in build directory
foreach(header ${SENC3D_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${header}
        ${SENC3D_GENERATED_INCLUDE_DIR}/star/${header_name}
        COPYONLY
    )
endforeach()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(senc3d
    PUBLIC
        # Generated structure for <star/senc3d.h>
        $<BUILD_INTERFACE:${SENC3D_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(senc3d
    PUBLIC
        rsys
        s3d
        OpenMP::OpenMP_C
)

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(senc3d PRIVATE m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    set(SENC3D_TEST_SOURCES
        src/test_senc3d_bad_grouping.c
        src/test_senc3d_bad_grouping2.c
        src/test_senc3d_bad_grouping3.c
        src/test_senc3d_cube_behind_cube.c
        src/test_senc3d_cube_in_cube.c
        src/test_senc3d_cube_on_cube.c
        src/test_senc3d_device.c
        src/test_senc3d_enclosure.c
        src/test_senc3d_glazing.c
        src/test_senc3d_inconsistant_cube.c
        src/test_senc3d_invalid_scenes.c
        src/test_senc3d_multi_media.c
        src/test_senc3d_scene.c
        src/test_senc3d_some_enclosures.c
        src/test_senc3d_some_triangles.c
        src/test_senc3d_unspecified_medium.c
        src/test_senc3d_zero_distance.c
    )
    
    add_module_tests(
        SOURCES ${SENC3D_TEST_SOURCES}
        LIBRARIES senc3d
    )
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS senc3d
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${SENC3D_PUBLIC_HEADERS}
    DESTINATION include/star
)
