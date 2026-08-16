@echo off
REM ============================================================
REM  Ninja + clang-cl Build Script
REM  生成 compile_commands.json，供 clangd/IDE 使用
REM ============================================================
REM  Prerequisites:
REM    - Visual Studio 2022 with "Clang tools for Windows" workload
REM    - vcpkg at D:/openscad/vcpkg (for benchmark, spdlog, TBB)
REM    - Everything else (Ninja, clang-cl) is bundled in VS
REM
REM  Usage:
REM    build_ninja.bat              → Debug configure + build
REM    build_ninja.bat release      → Release configure + build
REM    build_ninja.bat debug nocfg  → Debug build only (skip cmake configure)
REM    build_ninja.bat release nocfg→ Release build only
REM ============================================================

setlocal

set BUILD_TYPE=Debug
set SKIP_CONFIGURE=0

if /i "%1"=="release" set BUILD_TYPE=Release
if /i "%2"=="nocfg"   set SKIP_CONFIGURE=1
if /i "%1"=="nocfg"   set SKIP_CONFIGURE=1

REM -- Auto-detect VS 2022 edition --
set VS_PATH=
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
)

if "%VS_PATH%"=="" (
    echo [ERROR] Visual Studio 2022 not found!
    echo         Install VS 2022 Community/Professional/Enterprise or Build Tools
    exit /b 1
)

echo [INFO] VS 2022 found: %VS_PATH%
echo [INFO] Build type: %BUILD_TYPE%
echo [INFO] Compiler: clang-cl (LLVM bundled with VS)
echo [INFO] Generator: Ninja (bundled with VS)

REM -- Set up VS x64 environment --
set VSCMD_SKIP_SENDTELEMETRY=1
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to set up VS x64 environment.
    exit /b 1
)

if "%BUILD_TYPE%"=="Debug" (
    set PRESET=clangcl-ninja-debug
) else (
    set PRESET=clangcl-ninja-release
)

REM -- CMake Configure --
if %SKIP_CONFIGURE%==0 (
    echo.
    echo === CMake Configure: %PRESET% ===
    cmake --preset %PRESET%
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] CMake configure failed!
        exit /b 1
    )
) else (
    echo [INFO] Skipping cmake configure (nocfg flag set)
)

REM -- CMake Build --
echo.
echo === CMake Build ===
cmake --build --preset %PRESET%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    exit /b 1
)

REM -- Done --
echo.
echo ============================================
echo  Build succeeded!
echo  compile_commands.json -^> build\%PRESET%\compile_commands.json
echo ============================================

endlocal
