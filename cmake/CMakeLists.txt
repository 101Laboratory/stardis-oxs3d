# Copyright (C) |Meso|Star> 2016-2018
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

cmake_minimum_required(VERSION 3.0)
project(stardis C)
enable_testing()

set(SDIS_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../src)
option(NO_TEST "Do not build tests" OFF)

################################################################################
# Check dependencies
################################################################################
find_package(RCMake 0.3 REQUIRED)
find_package(Star2D 0.1 REQUIRED)
find_package(Star3D 0.4 REQUIRED)
find_package(StarSP 0.7 REQUIRED)
find_package(RSys 0.6 REQUIRED)
find_package(OpenMP 1.2 REQUIRED)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${RCMAKE_SOURCE_DIR})
include(rcmake)
include(rcmake_runtime)

rcmake_append_runtime_dirs(_runtime_dirs RSys Star3D StarSP)

################################################################################
# Configure and define targets
################################################################################
set(VERSION_MAJOR 0)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

set(SDIS_FILES_SRC
  sdis_data.c
  sdis_device.c
  sdis_estimator.c
  sdis_interface.c
  sdis_medium.c
  sdis_scene.c
  sdis_solve_probe.c)

set(SDIS_FILES_INC_API
  sdis.h)

set(SDIS_FILES_INC
  sdis_device_c.h
  sdis_estimator_c.h
  sdis_interface_c.h
  sdis_medium_c.h
  sdis_scene_c.h)

set(SDIS_FILES_DOC COPYING README.md)

# Prepend each file by `SDIS_SOURCE_DIR'
rcmake_prepend_path(SDIS_FILES_SRC ${SDIS_SOURCE_DIR})
rcmake_prepend_path(SDIS_FILES_INC ${SDIS_SOURCE_DIR})
rcmake_prepend_path(SDIS_FILES_INC_API ${SDIS_SOURCE_DIR})
rcmake_prepend_path(SDIS_FILES_DOC ${PROJECT_SOURCE_DIR}/../)

add_library(sdis SHARED
  ${SDIS_FILES_SRC}
  ${SDIS_FILES_INC}
  ${SDIS_FILES_INC_API})
target_link_libraries(sdis RSys Star2D Star3D StarSP)

set_target_properties(sdis PROPERTIES
  DEFINE_SYMBOL SDIS_SHARED_BUILD
  COMPILE_FLAGS ${OpenMP_C_FLAGS}
  VERSION ${VERSION}
  SOVERSION ${VERSION_MAJOR})
rcmake_copy_runtime_libraries(sdis)

if(CMAKE_COMPILER_IS_GNUCC)
  set_target_properties(sdis PROPERTIES LINK_FLAGS "${OpenMP_C_FLAGS} -lm")
endif()

rcmake_setup_devel(sdis Stardis ${VERSION} sdis_version.h)

################################################################################
# Add tests
################################################################################
if(NOT NO_TEST)
  find_package(Star3DUT REQUIRED 0.2.0)

  function(build_test _name)
    add_executable(${_name} ${SDIS_SOURCE_DIR}/${_name}.c)
    target_link_libraries(${_name} RSys sdis)
  endfunction()

  function(register_test _name)
    add_test(${_name} ${ARGN})
    rcmake_set_test_runtime_dirs(${_name} _runtime_dirs)
  endfunction()

  function(new_test _name)
    build_test(${_name})
    register_test(${_name} ${_name})
  endfunction()

  new_test(test_sdis_data)
  new_test(test_sdis_device)
  new_test(test_sdis_interface)
  new_test(test_sdis_medium)
  new_test(test_sdis_scene)
  new_test(test_sdis_solve_probe)
  new_test(test_sdis_solve_probe2)
  new_test(test_sdis_solve_probe3)
  new_test(test_sdis_solve_probe_2d)
  new_test(test_sdis_solve_probe2_2d)
  new_test(test_sdis_solve_probe3_2d)

  target_link_libraries(test_sdis_solve_probe3 Star3DUT)
  if(CMAKE_COMPILER_IS_GNUCC)
    target_link_libraries(test_sdis_solve_probe3_2d m)
  endif()
endif()

################################################################################
# Define output & install directories
################################################################################
install(TARGETS sdis
  ARCHIVE DESTINATION bin
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(FILES ${SDIS_FILES_INC_API} DESTINATION include/)
install(FILES ${SDIS_FILES_DOC} DESTINATION share/doc/stardis)

