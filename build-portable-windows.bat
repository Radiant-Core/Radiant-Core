@echo off
REM Portable Windows Build Script for Radiant Core
REM This script automatically detects and configures dependencies

echo ========================================
echo Radiant Core Portable Windows Build
echo ========================================
echo.

REM Check for required tools
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake not found. Please install CMake 3.22+
    pause
    exit /b 1
)

where python >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Python not found. Please install Python 3.6+
    pause
    exit /b 1
)

REM Detect compiler
set COMPILER_FOUND=0

REM Check for Visual Studio
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found Visual Studio compiler
    set COMPILER_FOUND=1
    set GENERATOR="Visual Studio 16 2019"
    set ARCH=-A x64
    goto :check_vcpkg
)

REM Check for MinGW
where gcc >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found MinGW compiler
    set COMPILER_FOUND=1
    set GENERATOR="MinGW Makefiles"
    goto :check_msys2
)

echo ERROR: No supported compiler found. Please install Visual Studio or MinGW-w64
pause
exit /b 1

:check_vcpkg
REM Check for vcpkg
if defined VCPKG_ROOT (
    echo Found vcpkg at %VCPKG_ROOT%
    set CMAKE_TOOLCHAIN_FILE=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
) else (
    echo vcpkg not found. You may need to manually install dependencies.
    set CMAKE_TOOLCHAIN_FILE=
)
goto :configure

:check_msys2
REM Check for MSYS2
if exist "C:\msys64\mingw64" (
    echo Found MSYS2 at C:\msys64
) else (
    echo MSYS2 not found at default location. You may need to manually install dependencies.
)
goto :configure

:configure
echo.
echo Configuring build...
echo.

REM Create build directory
if not exist build mkdir build
cd build

REM Configure with portable settings
cmake .. -G %GENERATOR% %ARCH% %CMAKE_TOOLCHAIN_FILE% ^
    -DBUILD_RADIANT_WALLET=OFF ^
    -DBUILD_RADIANT_ZMQ=OFF ^
    -DENABLE_UPNP=OFF ^
    -DCMAKE_BUILD_TYPE=Release

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    pause
    exit /b 1
)

echo.
echo Building...
echo.

REM Build
if "%GENERATOR%"=="\"Visual Studio 16 2019\"" (
    cmake --build . --config Release
) else (
    mingw32-make -j%NUMBER_OF_PROCESSORS%
)

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Executables created:
if exist "src\Release\radiantd.exe" echo   - src\Release\radiantd.exe
if exist "src\radiantd.exe" echo   - src\radiantd.exe
if exist "src\Release\radiant-cli.exe" echo   - src\Release\radiant-cli.exe
if exist "src\radiant-cli.exe" echo   - src\radiant-cli.exe
if exist "src\Release\radiant-tx.exe" echo   - src\Release\radiant-tx.exe
if exist "src\radiant-tx.exe" echo   - src\radiant-tx.exe

echo.
echo To run the daemon:
if exist "src\Release\radiantd.exe" (
    echo   src\Release\radiantd.exe
) else (
    echo   src\radiantd.exe
)

echo.
echo To test the build:
if exist "src\Release\radiantd.exe" (
    src\Release\radiantd.exe --version
) else (
    src\radiantd.exe --version
)

pause
