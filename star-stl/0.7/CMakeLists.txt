# star-stl Library - CMake Build Configuration
# Version: 0.7.0
# Description: STL file format library
# Dependencies: rsys
# Special: Optional sstl_util executable
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(star-stl
    VERSION 0.7.0
    DESCRIPTION "star-stl: STL file format library"
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

option(SSTL_BUILD_UTIL "Build sstl utility executable" OFF)

# ============================================================================
# Source Files
# ============================================================================

set(SSTL_SOURCES
    src/sstl.c
    src/sstl_ascii.c
    src/sstl_binary.c
    src/sstl_writer.c
)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(sstl SHARED ${SSTL_SOURCES})
    target_compile_definitions(sstl PRIVATE SSTL_SHARED_BUILD)
else()
    add_library(sstl STATIC ${SSTL_SOURCES})
endif()

add_library(sstl::sstl ALIAS sstl)

set_target_properties(sstl PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "sstl"
)

# ============================================================================
# Generate Standard Include Structure (star/ namespace)
# ============================================================================

set(SSTL_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

# Public header to expose under star/ namespace
set(SSTL_PUBLIC_HEADERS
    src/sstl.h
)

# Create include/star/ structure in build directory
foreach(header ${SSTL_PUBLIC_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${header}
        ${SSTL_GENERATED_INCLUDE_DIR}/star/${header_name}
        COPYONLY
    )
endforeach()

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(sstl
    PUBLIC
        # Generated structure for <star/sstl.h>
        $<BUILD_INTERFACE:${SSTL_GENERATED_INCLUDE_DIR}>
        # Source directory (internal use only)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(sstl PUBLIC rsys)

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(sstl PRIVATE m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    set(SSTL_TEST_SOURCES
        src/test_sstl.c
        src/test_sstl_desc.c
        src/test_sstl_load_ascii.c
        src/test_sstl_load_binary.c
        src/test_sstl_writer.c
    )
    
    add_module_tests(
        SOURCES ${SSTL_TEST_SOURCES}
        LIBRARIES sstl
    )
endif()

# ============================================================================
# Utility Executable (Optional)
# ============================================================================

if(SSTL_BUILD_UTIL)
    add_executable(sstl_util src/sstl_main.c)
    set_target_properties(sstl_util PROPERTIES OUTPUT_NAME "sstl")
    target_link_libraries(sstl_util PRIVATE sstl rsys)
    
    install(TARGETS sstl_util
        RUNTIME DESTINATION bin
    )
endif()

# ============================================================================
# Installation
# ============================================================================

# Install library targets
install(TARGETS sstl
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install public headers to include/star/
install(FILES ${SSTL_PUBLIC_HEADERS}
    DESTINATION include/star
)
