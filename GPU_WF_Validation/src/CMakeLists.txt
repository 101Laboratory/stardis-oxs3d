cmake_minimum_required(VERSION 3.25)

# ─── Architecture must be set before project() ───
set(CMAKE_CUDA_ARCHITECTURES "89" CACHE STRING "CUDA architectures (RTX 4090 = sm_89)")

project(gpu_wf_bench LANGUAGES C CXX CUDA)

# ─── Standards ───
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)

# ─── Verify CUDA ───
include(CheckLanguage)
check_language(CUDA)
if(NOT CMAKE_CUDA_COMPILER)
    message(FATAL_ERROR "CUDA compiler not found!")
endif()

find_package(CUDAToolkit REQUIRED)
message(STATUS "CUDA Toolkit version : ${CUDAToolkit_VERSION}")
message(STATUS "CUDA architectures   : ${CMAKE_CUDA_ARCHITECTURES}")

# ─── Executable ───
add_executable(gpu_wf_bench
    main.cu
    gpu_wf_bench.cu
)

set_target_properties(gpu_wf_bench PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE  ON
)

# CUB is bundled with CUDA Toolkit 11+, no extra find needed.
target_link_libraries(gpu_wf_bench PRIVATE CUDA::cudart)

# MSVC: optimise release builds
if(MSVC)
    target_compile_options(gpu_wf_bench PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Release>>:
            -O3 --use_fast_math
        >
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:
            /O2
        >
    )
endif()

# ─── Output directory (keep binary next to results/) ───
set_target_properties(gpu_wf_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin"
)
