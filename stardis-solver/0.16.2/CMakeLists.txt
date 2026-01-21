# stardis-solver Library - CMake Build Configuration
# Version: 0.16.2
# Description: Monte Carlo radiation transfer solver
# Dependencies: rsys, star-2d, star-3d, star-enclosures-2d, star-enclosures-3d, star-sp, star-wf, OpenMP
# Note: MPI support disabled for Windows migration
# Rewritten: 2026-01-21 (Workspace template)

cmake_minimum_required(VERSION 3.25)
project(stardis-solver
    VERSION 0.16.2
    DESCRIPTION "stardis-solver: Monte Carlo radiation transfer solver"
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
# Source Files (MPI sources excluded)
# ============================================================================

set(SDIS_SOURCES
    src/sdis.c
    src/sdis_brdf.c
    src/sdis_camera.c
    src/sdis_data.c
    src/sdis_device.c
    src/sdis_estimator.c
    src/sdis_estimator_buffer.c
    src/sdis_green.c
    src/sdis_heat_path.c
    src/sdis_heat_path_boundary.c
    src/sdis_heat_path_conductive.c
    src/sdis_interface.c
    src/sdis_log.c
    src/sdis_medium.c
    src/sdis_misc.c
    src/sdis_primkey.c
    src/sdis_radiative_env.c
    src/sdis_realisation.c
    src/sdis_scene.c
    src/sdis_solve.c
    src/sdis_solve_camera.c
    src/sdis_source.c
    src/sdis_tile.c
)

# ============================================================================
# Library Target
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(sdis SHARED ${SDIS_SOURCES})
    target_compile_definitions(sdis PRIVATE SDIS_SHARED_BUILD)
else()
    add_library(sdis STATIC ${SDIS_SOURCES})
endif()

add_library(sdis::sdis ALIAS sdis)

set_target_properties(sdis PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "sdis"
)

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(sdis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(sdis
    PUBLIC
        rsys
        s2d
        s3d
        senc2d
        senc3d
        ssp
        OpenMP::OpenMP_C
)

# Optional: star-wf
if(TARGET swf)
    target_link_libraries(sdis PRIVATE swf)
endif()

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(sdis PRIVATE m)
endif()

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    # Test utility source (shared by all tests)
    add_library(test_sdis_utils OBJECT src/test_sdis_utils.c)
    target_link_libraries(test_sdis_utils PUBLIC sdis rsys)
    
    # Regular tests
    set(SDIS_REGULAR_TESTS
        src/test_sdis_camera.c
        src/test_sdis_conducto_radiative.c
        src/test_sdis_conducto_radiative_2d.c
        src/test_sdis_contact_resistance.c
        src/test_sdis_contact_resistance_2.c
        src/test_sdis_convection.c
        src/test_sdis_convection_non_uniform.c
        src/test_sdis_data.c
        src/test_sdis_enclosure_limit_conditions.c
        src/test_sdis_flux.c
        src/test_sdis_flux2.c
        src/test_sdis_flux_with_h.c
        src/test_sdis_interface.c
        src/test_sdis_medium.c
        src/test_sdis_picard.c
        src/test_sdis_radiative_env.c
        src/test_sdis_solve_probe.c
        src/test_sdis_solve_probe_2d.c
        src/test_sdis_solve_probe2_2d.c
        src/test_sdis_solve_probe3_2d.c
        src/test_sdis_source.c
        src/test_sdis_transcient.c
        src/test_sdis_unsteady.c
        src/test_sdis_unsteady_1d.c
        src/test_sdis_unsteady_analytic_profile_2d.c
        src/test_sdis_unsteady_atm.c
        src/test_sdis_volumic_power.c
        src/test_sdis_volumic_power4.c
    )
    
    add_module_tests(
        SOURCES ${SDIS_REGULAR_TESTS}
        LIBRARIES test_sdis_utils sdis
    )
    
    # Long-running tests (with timeout)
    set(SDIS_LONG_TESTS
        src/test_sdis_volumic_power2.c
        src/test_sdis_volumic_power2_2d.c
        src/test_sdis_volumic_power3_2d.c
    )
    
    foreach(test_src ${SDIS_LONG_TESTS})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE test_sdis_utils sdis)
        add_test(NAME ${test_name} COMMAND ${test_name})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 600)
    endforeach()
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS sdis
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)
