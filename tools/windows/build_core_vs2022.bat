@echo off
setlocal
cd /d "%~dp0\..\.."

where cmake >nul 2>nul
if errorlevel 1 (
  echo [ERROR] cmake not found. Install CMake / Visual Studio C++ tools first.
  exit /b 1
)

echo [1/3] Configure VS2022 x64 shared build...
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DBUILD_SHARED_LIBS=ON -DGWENT_BUILD_TESTS=ON -DGWENT_BUILD_TRACE_TOOLS=OFF
if errorlevel 1 exit /b 1

echo [2/3] Build Release...
cmake --build build-vs --config Release -j
if errorlevel 1 exit /b 1

echo [3/3] Run C++ tests...
ctest --test-dir build-vs -C Release --output-on-failure
if errorlevel 1 exit /b 1

echo.
echo [OK] Core DLL: %CD%\build-vs\Release\gwent_core.dll
exit /b 0
