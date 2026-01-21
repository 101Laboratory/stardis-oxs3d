# star-3d Library - CMake Build Configuration
# Version: 0.10.0
# Description: 3D ray tracing and geometric operations
# Dependencies: rsys, embree4
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-3d
    VERSION 0.10.0
    DESCRIPTION "star-3d: 3D ray tracing and geometric operations"
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

# Library sources (production code only - NO test_*.c files)
set(S3D_SOURCES
    src/s3d_device.c
    src/s3d_geometry.c
    src/s3d_instance.c
    src/s3d_mesh.c
    src/s3d_primitive.c
    src/s3d_scene.c
    src/s3d_scene_view.c
    src/s3d_scene_view_closest_point.c
    src/s3d_scene_view_trace_ray.c
    src/s3d_shape.c
    src/s3d_sphere.c
)

# v1.1: Validate no test files mixed in
validate_library_sources(S3D_SOURCES)

file(GLOB S3D_HEADERS CONFIGURE_DEPENDS src/*.h)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(s3d SHARED ${S3D_SOURCES})
    target_compile_definitions(s3d PRIVATE S3D_SHARED_BUILD)
else()
    add_library(s3d STATIC ${S3D_SOURCES})
endif()

add_library(star-3d::s3d ALIAS s3d)

set_target_properties(s3d PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "s3d"
)

# ============================================================================
# Generate Namespace Headers (star/) - v1.1
# ============================================================================

# Public headers to expose
set(S3D_PUBLIC_HEADERS
    src/s3d.h
)

# v1.1: Use helper function to generate namespace structure
generate_namespace_headers(
    TARGET s3d
    NAMESPACE star
    HEADERS ${S3D_PUBLIC_HEADERS}
)

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(s3d
    PUBLIC
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)
# Note: Namespace headers auto-configured by generate_namespace_headers()

# ============================================================================
# Dependencies
# ============================================================================

# rsys (internal dependency)
target_link_libraries(s3d PUBLIC rsys)

# Embree4 (external dependency - managed by workspace)
if(TARGET embree)
    target_link_libraries(s3d PRIVATE embree)
elseif(EMBREE4_LIBRARY)
    target_link_libraries(s3d PRIVATE ${EMBREE4_LIBRARY} ${TBB12_LIBRARY})
    target_include_directories(s3d PRIVATE ${EMBREE4_INCLUDE_DIR})
endif()

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(s3d PRIVATE m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    # Test sources discovered from src/test_*.c pattern
    file(GLOB S3D_TEST_SOURCES src/test_*.c)
    
    if(S3D_TEST_SOURCES)
        add_module_tests(
            SOURCES ${S3D_TEST_SOURCES}
            LIBRARIES s3d
        )
    endif()
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS s3d
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${S3D_PUBLIC_HEADERS}
    DESTINATION include/star
)
