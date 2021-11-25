# Copyright (C) 2016-2021 |Meso|Star> (contact@meso-star.com)
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.1)
project(stardis C)
enable_testing()

include(CMakeDependentOption)

set(SDIS_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../src)
option(NO_TEST "Do not build tests" OFF)
option(USE_MPI
  "Enable the support of distributed parallelism \
using the Message Passing Interface specification." ON)

CMAKE_DEPENDENT_OPTION(ALL_TESTS
  "Perform basic and advanced tests" OFF "NOT NO_TEST" OFF)

###############################################################################
# Check dependencies
###############################################################################
find_package(RCMake 0.4 REQUIRED)
find_package(Star2D 0.5 REQUIRED)
find_package(Star3D 0.8 REQUIRED)
find_package(StarSP 0.12 REQUIRED)
find_package(StarEnc2D 0.5 REQUIRED)
find_package(StarEnc3D 0.5 REQUIRED)
find_package(RSys 0.12 REQUIRED)
find_package(OpenMP 2.0 REQUIRED)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${RCMAKE_SOURCE_DIR})
include(rcmake)
include(rcmake_runtime)

include_directories(
  ${Star2D_INCLUDE_DIR}
  ${Star3D_INCLUDE_DIR}
  ${StarSP_INCLUDE_DIR}
  ${StarEnc2D_INCLUDE_DIR}
  ${StarEnc3D_INCLUDE_DIR}
  ${RSys_INCLUDE_DIR})

if(USE_MPI)
  find_package(MPI 2 REQUIRED)
  set(CMAKE_C_COMPILER ${MPI_C_COMPILER})
  include_directories(${MPI_INCLUDE_PATH})
endif()

rcmake_append_runtime_dirs(_runtime_dirs
  RSys Star2D Star3D StarSP StarEnc2D StarEnc3D)

###############################################################################
# Configure and define targets
###############################################################################
set(VERSION_MAJOR 0)
set(VERSION_MINOR 12)
set(VERSION_PATCH 3)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

set(SDIS_FILES_SRC
  sdis.c
  sdis_camera.c
  sdis_data.c
  sdis_device.c
  sdis_estimator.c
  sdis_estimator_buffer.c
  sdis_green.c
  sdis_heat_path.c
  sdis_heat_path_boundary.c
  sdis_interface.c
  sdis_log.c
  sdis_medium.c
  sdis_misc.c
  sdis_realisation.c
  sdis_scene.c
  sdis_solve.c)

set(SDIS_FILES_INC_API
  sdis.h)

set(SDIS_FILES_INC
  sdis_camera.h
  sdis_device_c.h
  sdis_estimator_c.h
  sdis_green.h
  sdis_heat_path.h
  sdis_heat_path_boundary_c.h
  sdis_heat_path_boundary_Xd.h
  sdis_heat_path_boundary_Xd_fixed_flux.h
  sdis_heat_path_boundary_Xd_solid_fluid_picard1.h
  sdis_heat_path_boundary_Xd_solid_solid.h
  sdis_heat_path_conductive_Xd.h
  sdis_heat_path_convective_Xd.h
  sdis_heat_path_radiative_Xd.h
  sdis_interface_c.h
  sdis_log.h
  sdis_misc.h
  sdis_misc_Xd.h
  sdis_medium_c.h
  sdis_realisation.h
  sdis_realisation_Xd.h
  sdis_scene_c.h
  sdis_scene_Xd.h
  sdis_solve_boundary_Xd.h
  sdis_solve_medium_Xd.h
  sdis_solve_probe_Xd.h
  sdis_solve_probe_boundary_Xd.h
  sdis_Xd_begin.h
  sdis_Xd_end.h)

set(SDIS_FILES_DOC COPYING README.md)

# Prepend each file by `SDIS_SOURCE_DIR'
rcmake_prepend_path(SDIS_FILES_SRC ${SDIS_SOURCE_DIR})
rcmake_prepend_path(SDIS_FILES_INC ${SDIS_SOURCE_DIR})
rcmake_prepend_path(SDIS_FILES_INC_API ${SDIS_SOURCE_DIR})
rcmake_prepend_path(SDIS_FILES_DOC ${PROJECT_SOURCE_DIR}/../)

if(CMAKE_COMPILER_IS_GNUCC)
  set(MATH_LIB m)
endif()

if(MSVC)
  ### disable verbose warnings:
  # warning C4127: conditional expression is constant
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4127")
  # warning C4204:
  #   nonstandard extension used : non-constant aggregate initializer
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4204")
  # warning C4938: Floating point reduction variable may cause inconsistent
  # results under /fp:strict or #pragma fenv_access
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4938")
endif()

add_library(sdis SHARED
  ${SDIS_FILES_SRC}
  ${SDIS_FILES_INC}
  ${SDIS_FILES_INC_API})
target_link_libraries(sdis
  RSys Star2D Star3D StarSP StarEnc2D StarEnc3D ${MATH_LIB})

set_target_properties(sdis PROPERTIES
  DEFINE_SYMBOL SDIS_SHARED_BUILD
  COMPILE_FLAGS ${OpenMP_C_FLAGS}
  VERSION ${VERSION}
  SOVERSION ${VERSION_MAJOR})
rcmake_copy_runtime_libraries(sdis)

if(CMAKE_COMPILER_IS_GNUCC)
  set_target_properties(sdis PROPERTIES LINK_FLAGS "${OpenMP_C_FLAGS}")
endif()

if(USE_MPI)
  set_target_properties(sdis PROPERTIES COMPILE_DEFINITIONS "SDIS_USE_MPI")
endif()

rcmake_setup_devel(sdis Stardis ${VERSION} sdis_version.h)

###############################################################################
# Add tests
###############################################################################
if(NOT NO_TEST)
  find_package(Star3DUT REQUIRED 0.2.0)

  add_library(sdis-test-utils STATIC
    ${SDIS_SOURCE_DIR}/test_sdis_utils.h
    ${SDIS_SOURCE_DIR}/test_sdis_utils.c)

  function(build_test _name)
    add_executable(${_name} ${SDIS_SOURCE_DIR}/${_name}.c)
    target_link_libraries(${_name} sdis-test-utils RSys sdis)
  endfunction()

  function(register_test _name)
    add_test(${_name} ${ARGN})
    rcmake_set_test_runtime_dirs(${_name} _runtime_dirs)
  endfunction()

  function(new_test _name)
    build_test(${_name})
    register_test(${_name} ${_name})
  endfunction()

  new_test(test_sdis)
  new_test(test_sdis_camera)
  new_test(test_sdis_compute_power)
  new_test(test_sdis_conducto_radiative)
  new_test(test_sdis_conducto_radiative_2d)
  new_test(test_sdis_contact_resistance)
  new_test(test_sdis_contact_resistance_2)
  new_test(test_sdis_convection)
  new_test(test_sdis_convection_non_uniform)
  new_test(test_sdis_data)
  new_test(test_sdis_device)
  new_test(test_sdis_flux)
  new_test(test_sdis_interface)
  new_test(test_sdis_medium)
  new_test(test_sdis_picard)
  new_test(test_sdis_scene)
  new_test(test_sdis_solid_random_walk_robustness)
  new_test(test_sdis_solve_camera)
  new_test(test_sdis_solve_probe)
  new_test(test_sdis_solve_probe2)
  new_test(test_sdis_solve_probe3)
  new_test(test_sdis_solve_probe_2d)
  new_test(test_sdis_solve_probe2_2d)
  new_test(test_sdis_solve_probe3_2d)
  new_test(test_sdis_solve_boundary)
  new_test(test_sdis_solve_boundary_flux)
  new_test(test_sdis_solve_medium)
  new_test(test_sdis_solve_medium_2d)
  new_test(test_sdis_transcient)
  new_test(test_sdis_unstationary_atm)
  new_test(test_sdis_volumic_power)
  new_test(test_sdis_volumic_power4)

  # Additionnal tests
  build_test(test_sdis_volumic_power2)
  build_test(test_sdis_volumic_power2_2d)
  build_test(test_sdis_volumic_power3_2d)

  if(ALL_TESTS)
    add_test(test_sdis_volumic_power2    test_sdis_volumic_power2)
    add_test(test_sdis_volumic_power2_2d test_sdis_volumic_power2_2d)
    add_test(test_sdis_volumic_power3_2d test_sdis_volumic_power3_2d)
  endif()

  target_link_libraries(test_sdis_compute_power Star3DUT)
  target_link_libraries(test_sdis_solid_random_walk_robustness Star3DUT)
  target_link_libraries(test_sdis_solve_medium Star3DUT)
  target_link_libraries(test_sdis_solve_probe3 Star3DUT)
  target_link_libraries(test_sdis_solve_probe3_2d ${MATH_LIB})
  target_link_libraries(test_sdis_solve_camera Star3DUT)

  if(USE_MPI)
    set_target_properties(test_sdis PROPERTIES
      COMPILE_DEFINITIONS "SDIS_USE_MPI")
  endif()

  rcmake_copy_runtime_libraries(test_sdis_solid_random_walk_robustness)

endif()

###############################################################################
# Define output & install directories
###############################################################################
install(TARGETS sdis
  ARCHIVE DESTINATION bin
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(FILES ${SDIS_FILES_INC_API} DESTINATION include/)
install(FILES ${SDIS_FILES_DOC} DESTINATION share/doc/stardis-solver)
