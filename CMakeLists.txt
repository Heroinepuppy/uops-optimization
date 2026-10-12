cmake_minimum_required(VERSION 3.25)
project(uop_optimization LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LIDAR_REQUIRE_ARMV8 "Require a native ARMv8 build" OFF)
if(LIDAR_REQUIRE_ARMV8)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        message(FATAL_ERROR "Build ARMv8 requires an ARM64 compiler and host")
    endif()
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin/ARMv8")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
find_package(Threads REQUIRED)
add_executable(lidar_compute_cpu benchmarks/lidar_compute_cpu.cpp)
target_link_libraries(lidar_compute_cpu PRIVATE Threads::Threads)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    target_compile_definitions(lidar_compute_cpu PRIVATE LIDAR_ARM_NEON=1)
    if(MSVC)
        target_compile_options(lidar_compute_cpu PRIVATE /O2 /EHsc /W4)
    else()
        target_compile_options(lidar_compute_cpu PRIVATE -O3 -march=armv8-a+simd -Wall -Wextra)
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    target_compile_definitions(lidar_compute_cpu PRIVATE LIDAR_X86_SIMD=1)
    if(MSVC)
        target_compile_options(lidar_compute_cpu PRIVATE /O2 /arch:AVX2 /EHsc /W4)
    else()
        target_compile_options(lidar_compute_cpu PRIVATE -O3 -mavx2 -mfma -Wall -Wextra)
    endif()
else()
    message(FATAL_ERROR "Unsupported CPU architecture '${CMAKE_SYSTEM_PROCESSOR}'")
    target_compile_options(lidar_compute_cpu PRIVATE -O3 -Wall -Wextra)
endif()

set(LIDAR_BUILD_GPU "AUTO" CACHE STRING "Build HIP GPU benchmark: AUTO, ON or OFF")
set_property(CACHE LIDAR_BUILD_GPU PROPERTY STRINGS AUTO ON OFF)
if(LIDAR_BUILD_GPU STREQUAL "AUTO")
    find_program(HIPCC_EXECUTABLE NAMES hipcc
        HINTS "$ENV{HIP_PATH}/bin" "$ENV{ROCM_PATH}/bin")
    if(HIPCC_EXECUTABLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
        set(build_gpu ON)
    else()
        set(build_gpu OFF)
    endif()
elseif(LIDAR_BUILD_GPU STREQUAL "ON" OR LIDAR_BUILD_GPU STREQUAL "OFF")
    set(build_gpu "${LIDAR_BUILD_GPU}")
else()
    message(FATAL_ERROR "LIDAR_BUILD_GPU must be AUTO, ON or OFF")
endif()
message(STATUS "Native CPU architecture: ${CMAKE_SYSTEM_PROCESSOR}; HIP GPU build: ${build_gpu}")
if(build_gpu)
# HIP is invoked as a custom build rule so the Visual Studio generator can
# build both the MSVC CPU target and the HIP GPU target in one solution.
find_program(HIPCC_EXECUTABLE NAMES hipcc
    HINTS "$ENV{HIP_PATH}/bin" "$ENV{ROCM_PATH}/bin" REQUIRED)
set(LIDAR_GPU_ARCH "gfx1100" CACHE STRING "HIP GPU architecture")
set(LIDAR_HIP_MSVC_VERSION "14.44" CACHE STRING "MSVC toolset used by HIP on Windows")
set(LIDAR_GPU_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "GPU executable directory")
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

endif()

# Build native Vulkan compute when the SDK and shader compiler are available.
find_package(Vulkan QUIET)
find_program(GLSLANG_VALIDATOR NAMES glslangValidator)
if(Vulkan_FOUND AND GLSLANG_VALIDATOR)
    set(vulkan_shader "${CMAKE_CURRENT_BINARY_DIR}/lidar_vulkan_shader.h")
    add_custom_command(OUTPUT "${vulkan_shader}"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.1 --vn lidar_shader
            "${CMAKE_SOURCE_DIR}/benchmarks/lidar_compute_vulkan.comp" -o "${vulkan_shader}"
        DEPENDS benchmarks/lidar_compute_vulkan.comp VERBATIM)
    add_executable(lidar_compute_vulkan benchmarks/lidar_compute_vulkan.cpp "${vulkan_shader}")
    target_include_directories(lidar_compute_vulkan PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(lidar_compute_vulkan PRIVATE Vulkan::Vulkan)
    if(NOT MSVC)
        target_compile_options(lidar_compute_vulkan PRIVATE -O3 -Wall -Wextra -Wno-missing-field-initializers)
    endif()
    message(STATUS "Vulkan GPU benchmark enabled")
else()
    message(STATUS "Vulkan GPU benchmark disabled (requires Vulkan headers and glslangValidator)")
endif()
