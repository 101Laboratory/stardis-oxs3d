# stardis Application - CMake Build Configuration
# Version: 0.12.0
# Description: Monte Carlo radiation transfer application (main executable)
# Dependencies: rsys, star-3d, stardis-solver, star-enclosures-2d, star-enclosures-3d, star-sp, star-geometry-3d, star-stl
# Note: MPI support disabled for Windows migration
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(stardis
    VERSION 0.12.0
    DESCRIPTION "stardis: Monte Carlo radiation transfer application"
    LANGUAGES C
)

# ============================================================================
# Load Helper Functions (Not needed for application, but for consistency)
# ============================================================================

if(NOT COMMAND add_module_tests)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HelperFunctions.cmake")
    endif()
endif()

# ============================================================================
# Source Files
# ============================================================================

set(STARDIS_SOURCES
    src/stardis-app.c
    src/stardis-args.c
    src/stardis-compute.c
    src/stardis-compute-probe-boundary.c
    src/stardis-description.c
    src/stardis-extern-source.c
    src/stardis-fluid.c
    src/stardis-fluid-prog.c
    src/stardis-fbound.c
    src/stardis-fbound-prog.c
    src/stardis-hbound.c
    src/stardis-hbound-prog.c
    src/stardis-hfbound.c
    src/stardis-hfbound-prog.c
    src/stardis-intface.c
    src/stardis-main.c
    src/stardis-output.c
    src/stardis-parsing.c
    src/stardis-program.c
    src/stardis-radiative-env.c
    src/stardis-sfconnect.c
    src/stardis-sfconnect-prog.c
    src/stardis-ssconnect.c
    src/stardis-ssconnect-prog.c
    src/stardis-solid.c
    src/stardis-solid-prog.c
    src/stardis-tbound.c
    src/stardis-tbound-prog.c
    src/getopt.c
    src/wordexp.c
)

# ============================================================================
# Generate Configuration Headers
# ============================================================================

# Configuration values from config.mk
set(STARDIS_ARGS_DEFAULT_TRAD "300")
set(STARDIS_ARGS_DEFAULT_TRAD_REFERENCE "300")
set(STARDIS_ARGS_DEFAULT_COMPUTE_TIME "INF")
set(STARDIS_ARGS_DEFAULT_PICARD_ORDER "1")
set(STARDIS_ARGS_DEFAULT_RENDERING_FOV "70")
set(STARDIS_ARGS_DEFAULT_RENDERING_IMG_HEIGHT "480")
set(STARDIS_ARGS_DEFAULT_RENDERING_IMG_WIDTH "640")
set(STARDIS_ARGS_DEFAULT_RENDERING_OUTPUT_FILE_FMT "HT")
set(STARDIS_ARGS_DEFAULT_RENDERING_POS "1,1,1")
set(STARDIS_ARGS_DEFAULT_RENDERING_SPP "4")
set(STARDIS_ARGS_DEFAULT_RENDERING_TGT "0,0,0")
set(STARDIS_ARGS_DEFAULT_RENDERING_TIME "INF,INF")
set(STARDIS_ARGS_DEFAULT_RENDERING_UP "0,0,1")
set(STARDIS_ARGS_DEFAULT_SAMPLES_COUNT "10000")
set(STARDIS_ARGS_DEFAULT_SCALE_FACTOR "1")
set(STARDIS_ARGS_DEFAULT_VERBOSE_LEVEL "1")
set(STARDIS_MAX_NAME_LENGTH "64")
set(STARDIS_GREEN_TYPES_VERSION "4")
set(STARDIS_PROG_PROPERTIES_VERSION "2")

# Version information
set(STARDIS_APP_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(STARDIS_APP_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(STARDIS_APP_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# Generate headers from .in templates
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-default.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-default.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-args.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-args.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-version.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-version.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-green-types.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-green-types.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-prog-properties.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stardis-prog-properties.h"
    @ONLY
)

# ============================================================================
# Executable Target
# ============================================================================

add_executable(stardis ${STARDIS_SOURCES})

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(stardis
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(stardis
    PRIVATE
        rsys
        s3d
        sdis
        senc2d
        senc3d
        ssp
)

# Optional dependencies
if(TARGET sg3d)
    target_link_libraries(stardis PRIVATE sg3d)
endif()

if(TARGET sstl)
    target_link_libraries(stardis PRIVATE sstl)
endif()

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(stardis PRIVATE m)
endif()

# ============================================================================
# Windows Runtime Dependency Deployment
# ============================================================================

if(WIN32 AND COMMAND deploy_runtime_dependencies)
    # 部署第三方运行时依赖（Embree4, TBB 等）
    deploy_runtime_dependencies(stardis)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS stardis
    RUNTIME DESTINATION bin
)

# Install public headers
install(FILES
    src/stardis-green-types.h
    src/stardis-prog-properties.h
    DESTINATION include/stardis
)
