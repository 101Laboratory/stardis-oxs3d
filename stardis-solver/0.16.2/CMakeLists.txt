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
    # Regular tests -- H-category (data structure / API, no ray tracing)
    # These do NOT call sdis_solve_probe or any depth-first MC solver, so they
    # remain valid regardless of the ray-tracing backend.
    # =========================================================================
    set(SDIS_REGULAR_TESTS
        src/test_sdis_camera.c          # H4: camera API
        src/test_sdis_data.c            # H2: data container
        src/test_sdis_interface.c       # H8: interface component API
        src/test_sdis_medium.c          # H8: medium component API
        src/test_sdis_radiative_env.c   # H8: radiative env component API
        src/test_sdis_source.c          # H8: source component API
    )

    add_module_tests(
        SOURCES ${SDIS_REGULAR_TESTS}
        LIBRARIES test_sdis_utils sdis
    )

    # =========================================================================
    # DISABLED: depth-first solver tests (use sdis_solve_probe / synchronous
    # single-width ray queries).  The ray-tracing backend has been fully
    # migrated to GPU (cuBQL), so the depth-first code path can no longer
    # execute ray queries.  Corresponding wavefront (WF-*) replacements are
    # listed next to each test where available; tests without a WF
    # replacement are 2D-only or cover features not yet ported.
    #
    # Re-enable these ONLY if a CPU depth-first fallback is restored.
    # =========================================================================
    # set(SDIS_DISABLED_DEPTH_FIRST_TESTS
    #     src/test_sdis_conducto_radiative.c        # → WF-C1
    #     src/test_sdis_conducto_radiative_2d.c      # 2D only, no WF replacement
    #     src/test_sdis_contact_resistance.c         # → WF-A3
    #     src/test_sdis_contact_resistance_2.c       # A4, no WF replacement
    #     src/test_sdis_convection.c                 # → WF-D1
    #     src/test_sdis_convection_non_uniform.c     # → WF-D2
    #     src/test_sdis_enclosure_limit_conditions.c # → WF-I3
    #     src/test_sdis_flux.c                       # → WF-A1
    #     src/test_sdis_flux2.c                      # → WF-C4
    #     src/test_sdis_flux_with_h.c                # no WF replacement
    #     src/test_sdis_picard.c                     # → WF-C3
    #     src/test_sdis_solve_probe.c                # B1, no WF replacement
    #     src/test_sdis_solve_probe_2d.c             # 2D only, no WF replacement
    #     src/test_sdis_solve_probe2_2d.c            # 2D only, no WF replacement
    #     src/test_sdis_solve_probe3_2d.c            # 2D only, no WF replacement
    #     src/test_sdis_transcient.c                 # E6, no WF replacement
    #     src/test_sdis_unsteady.c                   # → WF-E1
    #     src/test_sdis_unsteady_1d.c                # → WF-E2
    #     src/test_sdis_unsteady_analytic_profile_2d.c # 2D only, no WF replacement
    #     src/test_sdis_unsteady_atm.c               # → WF-E5
    #     src/test_sdis_volumic_power.c              # → WF-A2
    #     src/test_sdis_volumic_power4.c             # → WF-A6
    # )
    
    # =========================================================================
    # DISABLED: long-running depth-first solver tests (same reason as above).
    # =========================================================================
    # set(SDIS_LONG_TESTS
    #     src/test_sdis_volumic_power2.c      # → WF-I1
    #     src/test_sdis_volumic_power2_2d.c    # 2D only, no WF replacement
    #     src/test_sdis_volumic_power3_2d.c    # A5, 2D only, no WF replacement
    # )
    #
    # foreach(test_src ${SDIS_LONG_TESTS})
    #     get_filename_component(test_name ${test_src} NAME_WE)
    #     add_executable(${test_name} ${test_src})
    #     target_link_libraries(${test_name} PRIVATE test_sdis_utils sdis)
    #     add_test(NAME ${test_name} COMMAND ${test_name})
    #     set_tests_properties(${test_name} PROPERTIES TIMEOUT 600)
    # endforeach()

    # ---- B-4 tests link the OBJECT library directly ----
    # sdis_obj gives direct access to all symbols (LOCAL_SYM included)
    # without needing WINDOWS_EXPORT_ALL_SYMBOLS on the DLL.

    # M1 enclosure batch test (disabled: M1 6-ray ENC replaced by M10 BVH locate)
    # add_executable(test_sdis_b4_m1_enclosure_batch
    #     src/test_sdis_b4_m1_enclosure_batch.c)
    # target_link_libraries(test_sdis_b4_m1_enclosure_batch PRIVATE sdis_obj)
    # add_test(NAME test_sdis_b4_m1_enclosure_batch
    #          COMMAND test_sdis_b4_m1_enclosure_batch)

    add_executable(test_sdis_b4_m2_ray_bucketing
        src/test_sdis_b4_m2_ray_bucketing.c)
    target_link_libraries(test_sdis_b4_m2_ray_bucketing PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m2_ray_bucketing
             COMMAND test_sdis_b4_m2_ray_bucketing)

    add_executable(test_sdis_b4_m3_solid_solid
        src/test_sdis_b4_m3_solid_solid.c)
    target_link_libraries(test_sdis_b4_m3_solid_solid PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m3_solid_solid
             COMMAND test_sdis_b4_m3_solid_solid)

    add_executable(test_sdis_b4_m4_delta_sphere
        src/test_sdis_b4_m4_delta_sphere.c)
    target_link_libraries(test_sdis_b4_m4_delta_sphere PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m4_delta_sphere
             COMMAND test_sdis_b4_m4_delta_sphere)

    add_executable(test_sdis_b4_m6_convective
        src/test_sdis_b4_m6_convective.c)
    target_link_libraries(test_sdis_b4_m6_convective PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m6_convective
             COMMAND test_sdis_b4_m6_convective)

    add_executable(test_sdis_b4_m5_picard1
        src/test_sdis_b4_m5_picard1.c)
    target_link_libraries(test_sdis_b4_m5_picard1 PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m5_picard1
             COMMAND test_sdis_b4_m5_picard1)

    add_executable(test_sdis_b4_m7_external_flux
        src/test_sdis_b4_m7_external_flux.c)
    target_link_libraries(test_sdis_b4_m7_external_flux PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m7_external_flux
             COMMAND test_sdis_b4_m7_external_flux)

    add_executable(test_sdis_b4_m8_picardN
        src/test_sdis_b4_m8_picardN.c)
    target_link_libraries(test_sdis_b4_m8_picardN PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m8_picardN
             COMMAND test_sdis_b4_m8_picardN)

    # M9 WoS state machine tests
    add_executable(test_sdis_b4_m9_wos
        src/test_sdis_b4_m9_wos.c)
    target_link_libraries(test_sdis_b4_m9_wos PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m9_wos COMMAND test_sdis_b4_m9_wos)
    set_tests_properties(test_sdis_b4_m9_wos PROPERTIES TIMEOUT 600)

    # M10 enc_locate tests
    add_executable(test_sdis_b4_m10_enc_locate
        src/test_sdis_b4_m10_enc_locate.c)
    target_link_libraries(test_sdis_b4_m10_enc_locate PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_m10_enc_locate
             COMMAND test_sdis_b4_m10_enc_locate)

    # E2E delayed tests (wavefront vs depth-first for M2-M8)
    add_executable(test_sdis_b4_e2e
        src/test_sdis_b4_e2e.c)
    target_link_libraries(test_sdis_b4_e2e PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_e2e COMMAND test_sdis_b4_e2e)
    set_tests_properties(test_sdis_b4_e2e PROPERTIES TIMEOUT 1200)

    # Full integration test (T-INT)
    add_executable(test_sdis_b4_integration
        src/test_sdis_b4_integration.c)
    target_link_libraries(test_sdis_b4_integration PRIVATE sdis_obj)
    add_test(NAME test_sdis_b4_integration COMMAND test_sdis_b4_integration)
    set_tests_properties(test_sdis_b4_integration PROPERTIES TIMEOUT 1200)

    # Wavefront benchmark (re-enabled: API fix applied, links sdis_obj for
    # access to LOCAL_SYM symbols)
    add_executable(test_sdis_wavefront_benchmark
        src/test_sdis_wavefront_benchmark.c)
    target_link_libraries(test_sdis_wavefront_benchmark PRIVATE sdis_obj)
    add_test(NAME test_sdis_wavefront_benchmark
             COMMAND test_sdis_wavefront_benchmark)
    set_tests_properties(test_sdis_wavefront_benchmark PROPERTIES TIMEOUT 1200)

    # ---- P0 wavefront numerical correctness tests ----
    # Links sdis_obj (OBJECT lib) for LOCAL_SYM access, same as B-4 tests.
    set(WF_P0_TESTS
        test_sdis_wf_a1_flux
        test_sdis_wf_a2_volumic
        test_sdis_wf_b2_boundary
        test_sdis_wf_c1_condrad
        test_sdis_wf_d1_convection)

    foreach(p0test ${WF_P0_TESTS})
        add_executable(${p0test} src/${p0test}.c)
        target_link_libraries(${p0test} PRIVATE sdis_obj)
        add_test(NAME ${p0test} COMMAND ${p0test})
        set_tests_properties(${p0test} PROPERTIES
            TIMEOUT 300
            LABELS "wf;p0;numerical")
    endforeach()

    # ---- P1 wavefront numerical correctness tests ----
    # Same pattern as P0: links sdis_obj (OBJECT lib) for LOCAL_SYM access.
    set(WF_P1_TESTS
        test_sdis_wf_a3_contact_resistance
        test_sdis_wf_a6_volumic_power4
        test_sdis_wf_c3_picard_multi
        test_sdis_wf_d2_convection_nonuniform
        test_sdis_wf_f1_external_flux
        test_sdis_wf_f2_diffuse_radiance)

    foreach(p1test ${WF_P1_TESTS})
        add_executable(${p1test} src/${p1test}.c)
        target_link_libraries(${p1test} PRIVATE sdis_obj)
        add_test(NAME ${p1test} COMMAND ${p1test})
        set_tests_properties(${p1test} PROPERTIES
            TIMEOUT 600
            LABELS "wf;p1;numerical")
    endforeach()

    # ---- P2 wavefront numerical correctness tests ----
    # Basic tests: link sdis_obj only (box geometry from test_sdis_utils.h)
    set(WF_P2_TESTS_BASIC
        test_sdis_wf_e1_unsteady
        test_sdis_wf_e2_unsteady_1d)

    foreach(p2test ${WF_P2_TESTS_BASIC})
        add_executable(${p2test} src/${p2test}.c)
        target_link_libraries(${p2test} PRIVATE sdis_obj)
        add_test(NAME ${p2test} COMMAND ${p2test})
        set_tests_properties(${p2test} PROPERTIES
            TIMEOUT 600
            LABELS "wf;p2;numerical")
    endforeach()

    # S3DUT tests: link sdis_obj + s3dut (supershape / sphere geometry)
    set(WF_P2_TESTS_S3DUT
        test_sdis_wf_e3_unsteady_analytic
        test_sdis_wf_g1_robustness
        test_sdis_wf_a7_solve_probe3)

    foreach(p2test ${WF_P2_TESTS_S3DUT})
        add_executable(${p2test} src/${p2test}.c)
        target_link_libraries(${p2test} PRIVATE sdis_obj s3dut)
        add_test(NAME ${p2test} COMMAND ${p2test})
        set_tests_properties(${p2test} PROPERTIES
            TIMEOUT 600
            LABELS "wf;p2;numerical")
    endforeach()

    # ---- P3 wavefront numerical correctness tests ----
    # Basic tests (box geometry, no s3dut): link sdis_obj only
    set(WF_P3_TESTS_BASIC
        test_sdis_wf_e5_unsteady_atm
        test_sdis_wf_c4_flux2
        test_sdis_wf_b3_boundary_flux
        test_sdis_wf_i3_enclosure_limit
        test_sdis_wf_i1_volumic_power2)

    foreach(p3test ${WF_P3_TESTS_BASIC})
        add_executable(${p3test} src/${p3test}.c)
        target_link_libraries(${p3test} PRIVATE sdis_obj)
        add_test(NAME ${p3test} COMMAND ${p3test})
        set_tests_properties(${p3test} PROPERTIES
            TIMEOUT 600
            LABELS "wf;p3;numerical")
    endforeach()

    # I1 batches 8 probes x 10K = 80K paths in a single pool pass — needs
    # more wall-clock time than the default 600 s.
    set_tests_properties(test_sdis_wf_i1_volumic_power2 PROPERTIES TIMEOUT 1200)
    set(WF_P3_TESTS_S3DUT
        test_sdis_wf_b5_probe_list)

    foreach(p3test ${WF_P3_TESTS_S3DUT})
        add_executable(${p3test} src/${p3test}.c)
        target_link_libraries(${p3test} PRIVATE sdis_obj s3dut)
        add_test(NAME ${p3test} COMMAND ${p3test})
        set_tests_properties(${p3test} PROPERTIES
            TIMEOUT 600
            LABELS "wf;p3;numerical")
    endforeach()

    # Deploy runtime DLLs (Embree4, TBB, etc.) for all B4 test executables
    if(WIN32 AND COMMAND deploy_runtime_dependencies)
        # deploy_runtime_dependencies(test_sdis_b4_m1_enclosure_batch)
        deploy_runtime_dependencies(test_sdis_b4_m2_ray_bucketing)
        deploy_runtime_dependencies(test_sdis_b4_m3_solid_solid)
        deploy_runtime_dependencies(test_sdis_b4_m4_delta_sphere)
        deploy_runtime_dependencies(test_sdis_b4_m6_convective)
        deploy_runtime_dependencies(test_sdis_b4_m5_picard1)
        deploy_runtime_dependencies(test_sdis_b4_m7_external_flux)
        deploy_runtime_dependencies(test_sdis_b4_m8_picardN)
        deploy_runtime_dependencies(test_sdis_b4_m10_enc_locate)
        deploy_runtime_dependencies(test_sdis_b4_e2e)
        deploy_runtime_dependencies(test_sdis_b4_integration)
        deploy_runtime_dependencies(test_sdis_wavefront_benchmark)
        # O14: test_sdis_dispatch_soa removed (dispatch_soa layer eliminated in merge-phase)
        foreach(p0test ${WF_P0_TESTS})
            deploy_runtime_dependencies(${p0test})
        endforeach()
        foreach(p1test ${WF_P1_TESTS})
            deploy_runtime_dependencies(${p1test})
        endforeach()
        foreach(p2test ${WF_P2_TESTS_BASIC})
            deploy_runtime_dependencies(${p2test})
        endforeach()
        foreach(p2test ${WF_P2_TESTS_S3DUT})
            deploy_runtime_dependencies(${p2test})
        endforeach()
        foreach(p3test ${WF_P3_TESTS_BASIC})
            deploy_runtime_dependencies(${p3test})
        endforeach()
        foreach(p3test ${WF_P3_TESTS_S3DUT})
            deploy_runtime_dependencies(${p3test})
        endforeach()
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
