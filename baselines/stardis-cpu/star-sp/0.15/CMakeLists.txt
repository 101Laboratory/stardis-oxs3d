# star-sp Library - CMake Build Configuration
# Version: 0.15.0
# Description: Sampling library and RNG abstraction
# Dependencies: rsys, random123 (header-only)
# Special: Requires C++ compilation for AES RNG support
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-sp
    VERSION 0.15.0
    DESCRIPTION "star-sp: Sampling library and RNG abstraction"
    LANGUAGES C CXX
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

option(SSP_ENABLE_AES "Enable AES random number generator" ON)

# ============================================================================
# C++ Standard (required for random123)
# ============================================================================

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Source Files
# ============================================================================

set(SSP_SOURCES
    src/ssp_ran.c
    src/ssp_ranst_discrete.c
    src/ssp_ranst_gaussian.c
    src/ssp_ranst_piecewise_linear.c
    src/ssp_rng.c
    src/ssp_rng_proxy.c
)

# v1.1: Validate no test files mixed in
validate_library_sources(SSP_SOURCES)

# Note: Random123 requires C++ compilation
# IMPORTANT: Compile .c files with C++ compiler (as per original Makefile)
# This is required because random123 uses C++ features and ssp_rng.c uses <sstream>
set_source_files_properties(${SSP_SOURCES} PROPERTIES LANGUAGE CXX)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(ssp SHARED ${SSP_SOURCES})
    target_compile_definitions(ssp PRIVATE SSP_SHARED_BUILD NOMINMAX)
else()
    add_library(ssp STATIC ${SSP_SOURCES})
    target_compile_definitions(ssp PRIVATE NOMINMAX)
endif()

add_library(star-sp::ssp ALIAS ssp)

set_target_properties(ssp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "star-sp"
)

# ============================================================================
# Generate Standard Include Structure (star/ namespace)
# ============================================================================

set(SSP_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public header to expose under star/ namespace
set(SSP_PUBLIC_HEADERS
    src/ssp.h
)

# Create include/star/ structure in build directory
foreach(header ${SSP_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${header}
        ${SSP_GENERATED_INCLUDE_DIR}/star/${header_name}
        COPYONLY
    )
endforeach()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(ssp
    PUBLIC
        # Generated structure for <star/ssp.h>
        $<BUILD_INTERFACE:${SSP_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${RANDOM123_INCLUDE_DIR}
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(ssp PUBLIC rsys)

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(ssp PUBLIC m)
endif()

# ============================================================================
# AES Support
# ============================================================================

if(SSP_ENABLE_AES)
    if(MSVC)
        # MSVC has AES intrinsics by default
        target_compile_definitions(ssp PRIVATE WITH_R123_AES)
    else()
        # GCC/Clang need -maes flag
        target_compile_options(ssp PRIVATE -maes)
        target_compile_definitions(ssp PRIVATE WITH_R123_AES)
    endif()
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    # Simple tests (one test per source file)
    set(SSP_SIMPLE_TESTS
        src/test_ssp_ran_circle.c
        src/test_ssp_ran_discrete.c
        src/test_ssp_ran_gaussian.c
        src/test_ssp_ran_hemisphere.c
        src/test_ssp_ran_hg.c
        src/test_ssp_ran_piecewise_linear.c
        src/test_ssp_ran_sphere.c
        src/test_ssp_ran_sphere_cap.c
        src/test_ssp_ran_spherical_zone.c
        src/test_ssp_ran_tetrahedron.c
        src/test_ssp_ran_triangle.c
        src/test_ssp_ran_uniform_disk.c
        src/test_ssp_rng_proxy.c
    )
    
    add_module_tests(
        SOURCES ${SSP_SIMPLE_TESTS}
        LIBRARIES ssp
    )
    
    # Special test: test_ssp_rng with multiple RNG variants
    add_executable(test_ssp_rng src/test_ssp_rng.c)
    target_link_libraries(test_ssp_rng PRIVATE ssp rsys)
    
    # Add tests for each RNG variant
    if(SSP_ENABLE_AES)
        add_test(NAME test_ssp_rng_aes COMMAND test_ssp_rng aes)
    endif()
    add_test(NAME test_ssp_rng_kiss COMMAND test_ssp_rng kiss)
    add_test(NAME test_ssp_rng_mt19937_64 COMMAND test_ssp_rng mt19937_64)
    add_test(NAME test_ssp_rng_ranlux48 COMMAND test_ssp_rng ranlux48)
    add_test(NAME test_ssp_rng_random_device COMMAND test_ssp_rng random_device)
    add_test(NAME test_ssp_rng_threefry COMMAND test_ssp_rng threefry)
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS ssp
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${SSP_PUBLIC_HEADERS}
    DESTINATION include/star
)
