@echo off
setlocal
cd /d "%~dp0"
if not exist "build\gpu" mkdir "build\gpu"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
"C:\Program Files\AMD\ROCm\7.2\bin\hipcc.exe" --offload-arch=gfx1100 -O3 -std=c++20 lidar_gpu_benchmark.cpp -o build\gpu\lidar_gpu_benchmark.exe
exit /b %errorlevel%
