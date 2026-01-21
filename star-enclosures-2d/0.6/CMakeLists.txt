# star-enclosures-2d Library - CMake Build Configuration
# Version: 0.6.0
# Description: 2D enclosure and view factor calculation
# Dependencies: rsys, star-2d, OpenMP
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-enclosures-2d
    VERSION 0.6.0
    DESCRIPTION "star-enclosures-2d: 2D enclosure and view factor calculation"
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

set(SENC2D_SOURCES
    src/senc2d_descriptor.c
    src/senc2d_device.c
    src/senc2d_enclosure.c
    src/senc2d_scene.c
    src/senc2d_scene_analyze.c
)

# v1.1: Validate no test files mixed in
validate_library_sources(SENC2D_SOURCES)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(senc2d SHARED ${SENC2D_SOURCES})
    target_compile_definitions(senc2d PRIVATE SENC2D_SHARED_BUILD)
else()
    add_library(senc2d STATIC ${SENC2D_SOURCES})
endif()

add_library(star::senc2d ALIAS senc2d)

set_target_properties(senc2d PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "senc2d"
)

# ============================================================================
# Generate Standard Include Structure (star/ namespace)
# ============================================================================

set(SENC2D_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public headers to expose under star/ namespace
set(SENC2D_PUBLIC_HEADERS
    src/senc2d.h
    src/sencX2d.h
    src/sencX2d_undefs.h
)

# Create include/star/ structure in build directory
foreach(header ${SENC2D_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${header}
        ${SENC2D_GENERATED_INCLUDE_DIR}/star/${header_name}
        COPYONLY
    )
endforeach()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(senc2d
    PUBLIC
        # Generated structure for <star/senc2d.h>
        $<BUILD_INTERFACE:${SENC2D_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(senc2d
    PUBLIC
        rsys
        s2d
        OpenMP::OpenMP_C
)

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(senc2d PUBLIC m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    set(SENC2D_TEST_SOURCES
        src/test_senc2d_square_behind_square.c
        src/test_senc2d_square_in_square.c
        src/test_senc2d_square_on_square.c
        src/test_senc2d_device.c
        src/test_senc2d_enclosure.c
        src/test_senc2d_inconsistant_square.c
        src/test_senc2d_invalid_scenes.c
        src/test_senc2d_many_enclosures.c
        src/test_senc2d_many_segments.c
        src/test_senc2d_multi_media.c
        src/test_senc2d_scene.c
        src/test_senc2d_some_enclosures.c
        src/test_senc2d_some_segments.c
        src/test_senc2d_unspecified_medium.c
    )
    
    add_module_tests(
        SOURCES ${SENC2D_TEST_SOURCES}
        LIBRARIES senc2d
    )
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS senc2d
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${SENC2D_PUBLIC_HEADERS}
    DESTINATION include/star
)
