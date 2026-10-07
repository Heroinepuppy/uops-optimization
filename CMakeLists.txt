cmake_minimum_required(VERSION 3.25)
project(uop_optimization LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lidar_compute_cpu benchmarks/lidar_compute_cpu.cpp)

if(MSVC)
    target_compile_options(lidar_compute_cpu PRIVATE /O2 /arch:AVX2 /EHsc /W4)
else()
    target_compile_options(lidar_compute_cpu PRIVATE -O3 -mavx2 -mfma -Wall -Wextra)
endif()

# HIP is invoked as a custom build rule so the Visual Studio generator can
# build both the MSVC CPU target and the HIP GPU target in one solution.
find_program(HIPCC_EXECUTABLE NAMES hipcc
    HINTS "$ENV{HIP_PATH}/bin" "$ENV{ROCM_PATH}/bin" REQUIRED)
set(LIDAR_GPU_ARCH "gfx1100" CACHE STRING "HIP GPU architecture")
set(LIDAR_HIP_MSVC_VERSION "14.44" CACHE STRING "MSVC toolset used by HIP on Windows")
set(LIDAR_GPU_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/gpu" CACHE PATH "GPU executable directory")
file(RELATIVE_PATH gpu_output "${CMAKE_SOURCE_DIR}"
    "${LIDAR_GPU_OUTPUT_DIR}/lidar_compute_gpu${CMAKE_EXECUTABLE_SUFFIX}")

set(hip_environment)
if(WIN32)
    # Use the Visual Studio instance selected by CMake, not a machine-specific path.
    string(REGEX REPLACE ",.*$" "" vs_instance "${CMAKE_GENERATOR_INSTANCE}")
    find_file(LIDAR_VCVARS64 NAMES vcvars64.bat
        HINTS "${vs_instance}/VC/Auxiliary/Build" "$ENV{VCINSTALLDIR}/Auxiliary/Build"
        REQUIRED)
    set(hip_environment COMMAND call "${LIDAR_VCVARS64}" "-vcvars_ver=${LIDAR_HIP_MSVC_VERSION}")
endif()

add_custom_command(
    OUTPUT "${LIDAR_GPU_OUTPUT_DIR}/lidar_compute_gpu${CMAKE_EXECUTABLE_SUFFIX}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIDAR_GPU_OUTPUT_DIR}"
    ${hip_environment}
    COMMAND "${HIPCC_EXECUTABLE}" "--offload-arch=${LIDAR_GPU_ARCH}" -O3 -std=c++20
        benchmarks/lidar_compute_gpu.cpp -o "${gpu_output}"
    DEPENDS benchmarks/lidar_compute_gpu.cpp benchmarks/lidar_compute_common.h
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building GPU benchmark with HIP (${LIDAR_GPU_ARCH})"
    VERBATIM)
add_custom_target(lidar_compute_gpu ALL
    DEPENDS "${LIDAR_GPU_OUTPUT_DIR}/lidar_compute_gpu${CMAKE_EXECUTABLE_SUFFIX}")
