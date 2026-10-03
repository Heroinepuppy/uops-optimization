cmake_minimum_required(VERSION 3.25)
project(uop_optimization LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(lidar_uop_benchmark lidar_uop_benchmark.cpp)
add_executable(lidar_thread_scaling lidar_thread_scaling.cpp)
add_executable(lidar_compute_cpu lidar_compute_cpu.cpp)

if(MSVC)
    target_compile_options(lidar_uop_benchmark PRIVATE /O2 /arch:AVX2 /EHsc /W4)
    target_compile_options(lidar_thread_scaling PRIVATE /O2 /arch:AVX2 /EHsc /W4)
    target_compile_options(lidar_compute_cpu PRIVATE /O2 /arch:AVX2 /EHsc /W4)
else()
    target_compile_options(lidar_uop_benchmark PRIVATE -O3 -mavx2 -mfma -Wall -Wextra)
    target_compile_options(lidar_thread_scaling PRIVATE -O3 -mavx2 -mfma -Wall -Wextra)
    target_compile_options(lidar_compute_cpu PRIVATE -O3 -mavx2 -mfma -Wall -Wextra)
endif()
