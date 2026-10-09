# star-2d Library - CMake Build Configuration
# Version: 0.7.0
# Description: 2D ray tracing and geometric operations using Embree acceleration
# Dependencies: rsys, embree4
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-2d
    VERSION 0.7.0
    DESCRIPTION "star-2d: 2D ray tracing and geometric operations"
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

set(S2D_SOURCES
    src/s2d_device.c
    src/s2d_geometry.c
    src/s2d_line_segments.c
    src/s2d_primitive.c
    src/s2d_scene.c
    src/s2d_scene_view.c
    src/s2d_scene_view_closest_point.c
    src/s2d_shape.c
)

# v1.1: Validate no test files mixed in
validate_library_sources(S2D_SOURCES)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(s2d SHARED ${S2D_SOURCES})
    target_compile_definitions(s2d PRIVATE S2D_SHARED_BUILD)
else()
    add_library(s2d STATIC ${S2D_SOURCES})
endif()

add_library(star-2d::s2d ALIAS s2d)

set_target_properties(s2d PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "s2d"
)

# ============================================================================
# Generate Standard Include Structure (star/ namespace)
# ============================================================================

set(S2D_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public header to expose under star/ namespace
set(S2D_PUBLIC_HEADERS
    src/s2d.h
)

# Create include/star/ structure in build directory
foreach(header ${S2D_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${header}
        ${S2D_GENERATED_INCLUDE_DIR}/star/${header_name}
        COPYONLY
    )
endforeach()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(s2d
    PUBLIC
        # Generated structure for <star/s2d.h>
        $<BUILD_INTERFACE:${S2D_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

# rsys (internal dependency)
target_link_libraries(s2d PUBLIC rsys)

# Embree4 (external dependency - managed by workspace)
if(TARGET embree)
    target_link_libraries(s2d PRIVATE embree)
elseif(EMBREE4_LIBRARY)
    target_link_libraries(s2d PRIVATE ${EMBREE4_LIBRARY} ${TBB12_LIBRARY})
    target_include_directories(s2d PRIVATE ${EMBREE4_INCLUDE_DIR})
endif()

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(s2d PRIVATE m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    set(S2D_TEST_SOURCES
        src/test_s2d_closest_point.c
        src/test_s2d_device.c
        src/test_s2d_primitive.c
        src/test_s2d_raytrace.c
        src/test_s2d_sample.c
        src/test_s2d_shape.c
        src/test_s2d_scene.c
        src/test_s2d_scene_view.c
        src/test_s2d_scene_view2.c
        src/test_s2d_trace_ray.c
        src/test_s2d_trace_ray_3d.c
    )
    
    add_module_tests(
        SOURCES ${S2D_TEST_SOURCES}
        LIBRARIES s2d
    )
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS s2d
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${S2D_PUBLIC_HEADERS}
    DESTINATION include/star
)
