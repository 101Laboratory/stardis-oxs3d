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
    LANGUAGES C CXX
)

# C++ standard required for sdis_wf_rng_adapter.cpp (uses ssp_rng_c.h which
# includes <random>, Random123/conventional/Engine.hpp)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    src/sdis_wf_steps_core.c
    src/sdis_wf_steps_enc.c
    src/sdis_wf_steps_cnd.c
    src/sdis_wf_steps_bnd_ss.c
    src/sdis_wf_steps_bnd_sf.c
    src/sdis_wf_steps_bnd_sfn.c
    src/sdis_wf_steps_bnd_ext.c
    src/sdis_wf_steps_cnv.c
    src/sdis_solve_persistent_wavefront.c
    src/sdis_wf_hot.c
    src/sdis_wf_rng_adapter.cpp
    src/sdis_source.c
    src/sdis_tile.c
)

# ============================================================================
# Library Target
# ============================================================================

# OBJECT library: compile sources once, link into both DLL and test targets.
# Tests link sdis_obj directly (all symbols visible, no DLL export needed).
add_library(sdis_obj OBJECT ${SDIS_SOURCES})
target_include_directories(sdis_obj
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
        ${RANDOM123_INCLUDE_DIR}
)

# ============================================================================
# Dependencies (on the OBJECT lib so both sdis and tests inherit them)
# ============================================================================

find_package(OpenMP)

target_link_libraries(sdis_obj
    PUBLIC
        rsys
        s2d
        s3d
        senc2d
        senc3d
        ssp
)

# OpenMP: link directly (no generator expression) to ensure MSVC Visual Studio
# generator correctly sets <OpenMPSupport>true</OpenMPSupport> for OBJECT libs.
# Using $<$<BOOL:...>:OpenMP::OpenMP_C> silently fails to propagate the MSBuild
# property, causing all #pragma omp directives to be ignored.
# For OBJECT libraries, we also need target_compile_options to force the flag,
# because the imported target's INTERFACE_COMPILE_OPTIONS may not map to the
# MSBuild <OpenMPSupport> property for OBJECT targets.
if(OpenMP_C_FOUND)
    target_link_libraries(sdis_obj PUBLIC OpenMP::OpenMP_C)
    target_compile_options(sdis_obj PRIVATE ${OpenMP_C_FLAGS})
endif()

# Optional: star-wf
if(TARGET swf)
    target_link_libraries(sdis_obj PRIVATE swf)
endif()

# Math library (Unix only)
if(UNIX AND NOT APPLE)
    target_link_libraries(sdis_obj PRIVATE m)
endif()

# ============================================================================
# Shared / Static library (links the OBJECT lib)
# ============================================================================

# When building a DLL, EXPORT_SYM must expand to __declspec(dllexport)
# during compilation — so the define goes on sdis_obj, not sdis.
# For test EXEs the dllexport attribute is harmlessly ignored by the linker.
#
# When building STATIC, SDIS_STATIC_BUILD is PUBLIC so that consumers
# (stardis, tests) also see it and get  SDIS_API = extern LOCAL_SYM
# instead of extern __declspec(dllimport).
if(BUILD_SHARED_LIBS)
    target_compile_definitions(sdis_obj PRIVATE SDIS_SHARED_BUILD)
    add_library(sdis SHARED)
else()
    target_compile_definitions(sdis_obj PUBLIC  SDIS_STATIC_BUILD)
    add_library(sdis STATIC)
endif()

target_link_libraries(sdis PUBLIC sdis_obj)

add_library(sdis::sdis ALIAS sdis)

set_target_properties(sdis PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME "sdis"
)

# ============================================================================
# Tests
# ============================================================================

if(ENABLE_TESTS)
    enable_testing()
    
    # Test utility source (shared by all tests)
    add_library(test_sdis_utils OBJECT src/test_sdis_utils.c)
    target_link_libraries(test_sdis_utils PUBLIC sdis rsys)

    # =========================================================================
    # WoS-related tests only (test/wos-validation branch)
    #
    # This worktree builds only the subset of B4 and WF tests that exercise
    # the Walk-on-Spheres (WoS) conductive path algorithm.
    #
    # B4-M9:  FSM unit tests (T9.1-T9.7b) + E2E probes (T9.8 steady, T9.9
    #         transient).  Pure conduction scenes (unit box, Dirichlet BC).
    # WF-C3:  Picard multi-order conducto-radiative test.  4/6 configs use
    #         SDIS_DIFFUSION_WOS; tests WoS within a coupled solver context.
    # =========================================================================

    # When linking tests against sdis_obj (OBJECT lib) while SDIS_SHARED_BUILD
    # is PRIVATE on sdis_obj, tests see SDIS_API = __declspec(dllimport) by
    # default.  Force SDIS_SHARED_BUILD so the symbol decoration matches the
    # object files (avoids LNK2019 on __imp_* symbols).
    if(BUILD_SHARED_LIBS)
        set(WOS_TEST_DEFS SDIS_SHARED_BUILD)
    endif()

    # ---- B4-M9: WoS state machine unit tests (T9.1-T9.9) ----
    # Pure conduction: unit box with Dirichlet BC, steady + transient.
    # T9.1-T9.7b test individual FSM states (LOCAL_SYM step functions).
    # T9.8-T9.9 test end-to-end probe (SDIS_API solver function).
    add_executable(test_sdis_b4_m9_wos
        src/test_sdis_b4_m9_wos.c)
    target_link_libraries(test_sdis_b4_m9_wos PRIVATE sdis_obj)
    target_compile_definitions(test_sdis_b4_m9_wos PRIVATE ${WOS_TEST_DEFS})
    add_test(NAME test_sdis_b4_m9_wos COMMAND test_sdis_b4_m9_wos)
    set_tests_properties(test_sdis_b4_m9_wos PROPERTIES
        TIMEOUT 600
        LABELS "b4;wos;conduction")

    # ---- WF-C3: Picard multi-order conducto-radiative test ----
    # 4 of 6 configs use SDIS_DIFFUSION_WOS; tests WoS in coupled context.
    add_executable(test_sdis_wf_c3_picard_multi
        src/test_sdis_wf_c3_picard_multi.c)
    target_link_libraries(test_sdis_wf_c3_picard_multi PRIVATE sdis_obj)
    target_compile_definitions(test_sdis_wf_c3_picard_multi PRIVATE ${WOS_TEST_DEFS})
    add_test(NAME test_sdis_wf_c3_picard_multi COMMAND test_sdis_wf_c3_picard_multi)
    set_tests_properties(test_sdis_wf_c3_picard_multi PROPERTIES
        TIMEOUT 600
        LABELS "wf;p1;wos;numerical")

    # Deploy runtime DLLs (Embree4, TBB, etc.)
    if(WIN32 AND COMMAND deploy_runtime_dependencies)
        deploy_runtime_dependencies(test_sdis_b4_m9_wos)
        deploy_runtime_dependencies(test_sdis_wf_c3_picard_multi)
    endif()

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
