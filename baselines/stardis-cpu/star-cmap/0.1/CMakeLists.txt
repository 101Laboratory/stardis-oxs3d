# ====================================================================
# star-cmap Library - CMake Build Configuration (Template v1.1)
# ====================================================================
# Version: 0.1.0
# Description: Color mapping library (47 palettes)
# Dependencies: rsys
# Template: cmake v1.1.0 (2026-01-21)
# ====================================================================

cmake_minimum_required(VERSION 3.25)
project(star-cmap
    VERSION 0.1.0
    DESCRIPTION "star-cmap: Color mapping library with scientific colormaps"
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
    option(ENABLE_TESTS "Enable building tests" ON)
endif()

if(NOT DEFINED BUILD_SHARED_LIBS)
    option(BUILD_SHARED_LIBS "Build shared library" ON)
endif()

# ====================================================================
# Dependencies
# ====================================================================

# rsys dependency (if not already available)
if(NOT TARGET rsys)
    find_package(rsys 0.15 QUIET)
    if(NOT rsys_FOUND)
        # Try to find as sibling directory
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../rsys/0.15/CMakeLists.txt")
            add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../rsys/0.15 
                             ${CMAKE_BINARY_DIR}/rsys EXCLUDE_FROM_ALL)
            message(STATUS "star-cmap: Found rsys as sibling directory")
        else()
            message(FATAL_ERROR "star-cmap: rsys not found. Set rsys_ROOT or place in sibling directory")
        endif()
    endif()
endif()

# ====================================================================
# Source Files
# ====================================================================

# Explicit list (RECOMMENDED per v1.1 RULE 1)
set(SCMAP_SOURCES
    src/scmap.c
    src/scmap_palettes.c
)

# CRITICAL: Validate no test files mixed in
validate_library_sources(SCMAP_SOURCES)

# ====================================================================
# Library Target
# ====================================================================

if(BUILD_SHARED_LIBS)
    add_library(scmap SHARED ${SCMAP_SOURCES})
    target_compile_definitions(scmap PRIVATE SCMAP_SHARED_BUILD)
    set_target_properties(scmap PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    add_library(scmap STATIC ${SCMAP_SOURCES})
endif()

add_library(star::scmap ALIAS scmap)

set_target_properties(scmap PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "scmap"
)

# ====================================================================
# Generate Namespace Headers (star/)
# ====================================================================

# Define public headers to expose
set(SCMAP_PUBLIC_HEADERS
    src/scmap.h
)

# Generate namespace structure: include/star/scmap.h
generate_namespace_headers(
    TARGET scmap
    NAMESPACE star
    HEADERS ${SCMAP_PUBLIC_HEADERS}
)

# ====================================================================
# Include Directories
# ====================================================================

target_include_directories(scmap
    PUBLIC
        # Generated namespace headers (for #include <star/scmap.h>)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        # Source directory (for internal use and #include <scmap.h>)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        # Legacy compatibility: allow #include <star/scmap.h> from project root
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# ====================================================================
# Dependencies Linking
# ====================================================================

# Link rsys - PUBLIC because scmap.h includes <rsys/rsys.h>
# Users of scmap need rsys headers available
target_link_libraries(scmap PUBLIC rsys)

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(scmap PRIVATE m)
endif()

# ====================================================================
# Tests
# ====================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    # Discover test sources
    file(GLOB SCMAP_TEST_SOURCES src/test_*.c)
    
    if(SCMAP_TEST_SOURCES)
        # Test 1 & 2: Basic tests
        add_module_tests(
            SOURCES ${SCMAP_TEST_SOURCES}
            LIBRARIES scmap
        )
        
        # Test 3: Palette tests (special handling for 47 palettes)
        # Find the palette test executable
        set(PALETTES
            accent blues brbg bugn bupu chromajs dark2 gnbu gnpu greens greys
            inferno jet magma moreland oranges orrd paired parula pastel1 pastel2
            piyg plasma prgn pubu pubugn puor purd purples rdbu rdgy rdpu rdylbu
            rdylgn reds sand set1 set2 set3 spectral viridis whgnbu whylrd ylgn
            ylgnbu ylorbr ylorrd ylrd
        )
        
        # Create individual test for each palette
        foreach(palette ${PALETTES})
            add_test(NAME test_scmap_palette_${palette}
                COMMAND test_scmap_palettes ${palette}
            )
        endforeach()
        
        message(STATUS "star-cmap: Configured ${CMAKE_CURRENT_LIST_LINE} palette tests")
    endif()
endif()

# ====================================================================
# Installation
# ====================================================================

# Install library targets
install(TARGETS scmap
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${SCMAP_PUBLIC_HEADERS}
    DESTINATION include/star
)

# CRITICAL: Exclude test headers from installation (v1.1 RULE 3)
install(DIRECTORY src/
    DESTINATION include/star
    FILES_MATCHING PATTERN "*.h"
    PATTERN "test_*.h" EXCLUDE
)

# ====================================================================
# Configuration Summary
# ====================================================================

message(STATUS "========================================== ")
message(STATUS "star-cmap Configuration Summary")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Library Type: ${BUILD_SHARED_LIBS}")
message(STATUS "  Dependencies:")
message(STATUS "    - rsys: Found")
message(STATUS "  Tests: ${ENABLE_TESTS}")
message(STATUS "==========================================")
