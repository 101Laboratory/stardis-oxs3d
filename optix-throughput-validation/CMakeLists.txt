#
# CMakeLists.txt - OptiX 9.1.0 Throughput Benchmark
#
# Standalone project: no OptiX SDK sutil dependency.
# Builds three components:
#   1. Ray tracing device programs (programs.cu → programs.ptx via nvcc --ptx)
#   2. Nearest neighbor device programs (nn_programs.cu → nn_programs.ptx)
#   3. Main executable (host C++ + regular CUDA kernels)
#

cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
project(optix_throughput_validation LANGUAGES C CXX CUDA)

# ---- C++ / CUDA standards ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# ---- Output directories: all binaries → build/bin/<Config> ----
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---- OptiX SDK ----
set(OptiX_INSTALL_DIR "C:/ProgramData/NVIDIA Corporation/OptiX SDK 9.1.0"
    CACHE PATH "Path to OptiX SDK installation")
set(OptiX_INCLUDE_DIR "${OptiX_INSTALL_DIR}/include")

if(NOT EXISTS "${OptiX_INCLUDE_DIR}/optix.h")
    message(FATAL_ERROR
        "OptiX SDK not found at: ${OptiX_INSTALL_DIR}\n"
        "Set -DOptiX_INSTALL_DIR=<path> to the OptiX SDK root directory.")
endif()
message(STATUS "OptiX SDK found: ${OptiX_INSTALL_DIR}")

# ---- CUDA Toolkit ----
find_package(CUDAToolkit REQUIRED)
message(STATUS "CUDA Toolkit: ${CUDAToolkit_VERSION}")
message(STATUS "NVCC: ${CUDAToolkit_NVCC_EXECUTABLE}")

# ---- GPU Architecture ----
# Default: sm_86 (Ampere). Override with -DCUDA_ARCH=89 for Ada Lovelace etc.
set(CUDA_ARCH "86" CACHE STRING "CUDA architecture for regular CUDA kernels")

# ---- rsys dependency (hardcoded path) ----
set(RSYS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../rsys/0.15" CACHE PATH "Path to rsys 0.15 in the publication workspace")
if(NOT EXISTS "${RSYS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "rsys not found at: ${RSYS_DIR}")
endif()
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
add_subdirectory("${RSYS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rsys")

# ---- Compile OptiX programs to PTX ----
# OptiX device programs must be compiled to PTX (or OptiX IR).
# We use compute_70 PTX for broad compatibility; OptiX JIT-compiles for actual GPU.
set(PTX_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/device/programs.cu")
set(PTX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/programs.ptx")

# Find nvcc executable
if(CUDAToolkit_NVCC_EXECUTABLE)
    set(NVCC_CMD "${CUDAToolkit_NVCC_EXECUTABLE}")
else()
    find_program(NVCC_CMD nvcc HINTS "${CUDAToolkit_BIN_DIR}")
endif()

if(NOT NVCC_CMD)
    message(FATAL_ERROR "nvcc not found. Make sure CUDA Toolkit is installed.")
endif()

add_custom_command(
    OUTPUT "${PTX_OUTPUT}"
    COMMAND "${NVCC_CMD}"
        --ptx
        -I"${OptiX_INCLUDE_DIR}"
        -I"${CMAKE_CURRENT_SOURCE_DIR}/include"
        --machine=64
        -arch=compute_70
        --use_fast_math
        -std=c++17
        -D__CUDA_INCLUDE_COMPILER_INTERNAL_HEADERS__
        "${PTX_SOURCE}"
        -o "${PTX_OUTPUT}"
    DEPENDS
        "${PTX_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/unified_params.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ray_types.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/nn_types.h"
    COMMENT "Compiling OptiX device programs to PTX (compute_70)"
    VERBATIM
)

# ---- Compile NN OptiX programs to PTX ----
set(NN_PTX_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/device/nn_programs.cu")
set(NN_PTX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/nn_programs.ptx")

add_custom_command(
    OUTPUT "${NN_PTX_OUTPUT}"
    COMMAND "${NVCC_CMD}"
        --ptx
        -I"${OptiX_INCLUDE_DIR}"
        -I"${CMAKE_CURRENT_SOURCE_DIR}/include"
        --machine=64
        -arch=compute_70
        --use_fast_math
        -std=c++17
        -D__CUDA_INCLUDE_COMPILER_INTERNAL_HEADERS__
        "${NN_PTX_SOURCE}"
        -o "${NN_PTX_OUTPUT}"
    DEPENDS
        "${NN_PTX_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/unified_params.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ray_types.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/nn_types.h"
    COMMENT "Compiling NN device programs to PTX (compute_70)"
    VERBATIM
)

add_custom_target(ptx_programs ALL DEPENDS "${PTX_OUTPUT}" "${NN_PTX_OUTPUT}")

# ---- Main executable ----
add_executable(optix_throughput
    src/main.cpp
    src/device_manager.cpp
    src/geometry_manager.cpp
    src/unified_tracer.cpp
    device/kernels.cu
    device/nn_kernels.cu
)

target_include_directories(optix_throughput PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/device"
    "${OptiX_INCLUDE_DIR}"
)

target_link_libraries(optix_throughput PRIVATE
    CUDA::cudart
)

# CUDA architecture for regular kernels
set_target_properties(optix_throughput PROPERTIES
    CUDA_ARCHITECTURES "${CUDA_ARCH}"
)

# Pass PTX file paths as compile definitions
string(REPLACE "\\" "/" PTX_PATH_FWD "${PTX_OUTPUT}")
string(REPLACE "\\" "/" NN_PTX_PATH_FWD "${NN_PTX_OUTPUT}")
target_compile_definitions(optix_throughput PRIVATE
    OPTIX_PTX_FILE_PATH="${PTX_PATH_FWD}"
    OPTIX_NN_PTX_FILE_PATH="${NN_PTX_PATH_FWD}"
)

# Ensure PTX is built before the executable
add_dependencies(optix_throughput ptx_programs)

# Copy PTX files to executable output directory
add_custom_command(TARGET optix_throughput POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PTX_OUTPUT}"
        "$<TARGET_FILE_DIR:optix_throughput>/programs.ptx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NN_PTX_OUTPUT}"
        "$<TARGET_FILE_DIR:optix_throughput>/nn_programs.ptx"
    COMMENT "Copying PTX files to output directory"
)

# ---- s3d wrapper library (OptiX-backed, shared/DLL) ----
add_library(s3d SHARED
    s3d_wrapper/ox_s3d_device.cpp
    s3d_wrapper/ox_s3d_shape.cpp
    s3d_wrapper/ox_s3d_scene.cpp
    s3d_wrapper/ox_s3d_scene_view.cpp
    s3d_wrapper/ox_s3d_primitive.cpp
    src/device_manager.cpp
    src/geometry_manager.cpp
    src/unified_tracer.cpp
    device/kernels.cu
    device/nn_kernels.cu
)

target_include_directories(s3d PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/s3d_wrapper"
)

target_include_directories(s3d PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/device"
    "${OptiX_INCLUDE_DIR}"
)

target_link_libraries(s3d PUBLIC
    CUDA::cudart
    rsys
)

target_compile_definitions(s3d PRIVATE
    S3D_SHARED_BUILD
    OPTIX_PTX_FILE_PATH="${PTX_PATH_FWD}"
    OPTIX_NN_PTX_FILE_PATH="${NN_PTX_PATH_FWD}"
)

set_target_properties(s3d PROPERTIES
    CUDA_ARCHITECTURES "${CUDA_ARCH}"
)

add_dependencies(s3d ptx_programs)

# ---- CTest support ----
enable_testing()
add_test(
    NAME optix_throughput_validation
    COMMAND optix_throughput --iters 3 --max-rays 1048576
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:optix_throughput>"
)

# ---- s3d test executables (from tests/ directory) ----
set(S3D_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

set(S3D_TEST_SOURCES
    test_s3d_accel_struct_conf.c
    test_s3d_batch_closest_point.c
    test_s3d_batch_trace.c
    test_s3d_closest_point.c
    test_s3d_device.c
    test_s3d_primitive.c
    test_s3d_sampler.c
    test_s3d_sample_sphere.c
    test_s3d_scene.c
    test_s3d_scene_view.c
    test_s3d_scene_view_aabb.c
    test_s3d_seams.c
    test_s3d_shape.c
    test_s3d_sphere.c
    test_s3d_sphere_box.c
    test_s3d_sphere_instance.c
    test_s3d_trace_ray.c
    test_s3d_trace_ray_instance.c
    test_s3d_trace_ray_sphere.c
)

foreach(test_src ${S3D_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${S3D_TEST_DIR}/${test_src})
    target_include_directories(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/s3d_wrapper"
        "${S3D_TEST_DIR}"
    )
    target_link_libraries(${test_name} PRIVATE s3d)
    add_dependencies(${test_name} ptx_programs)
    # Deploy runtime DLLs (s3d.dll, rsys.dll, etc.) to test exe directory
    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
        add_custom_command(TARGET ${test_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${test_name}>
                $<TARGET_FILE_DIR:${test_name}>
            COMMAND_EXPAND_LISTS
            COMMENT "Deploying runtime DLLs for ${test_name}"
        )
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name}
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${test_name}>")
endforeach()

# ---- Print configuration summary ----
message(STATUS "")
message(STATUS "=== OptiX Throughput Benchmark Configuration ===")
message(STATUS "  OptiX:          ${OptiX_INSTALL_DIR}")
message(STATUS "  CUDA Toolkit:   ${CUDAToolkit_VERSION}")
message(STATUS "  CUDA Arch:      sm_${CUDA_ARCH}")
message(STATUS "  PTX Arch:       compute_70 (JIT for actual GPU)")
message(STATUS "  PTX Targets:    programs.ptx, nn_programs.ptx")
message(STATUS "  s3d:            s3d wrapper library (shared/DLL)")
message(STATUS "  rsys:           stardis-cus3d/rsys/0.15")
message(STATUS "  Tests:          ${CMAKE_CURRENT_SOURCE_DIR}/tests/")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "================================================")
