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

REM Detect and set OpenSSL path
set OPENSSL_ROOT_DIR=
if exist "C:\msys64\mingw64\include\openssl\ssl.h" (
    set OPENSSL_ROOT_DIR=C:\msys64\mingw64
    echo Found OpenSSL via MSYS2: %OPENSSL_ROOT_DIR%
) else if exist "C:\Strawberry\c\include\openssl\ssl.h" (
    set OPENSSL_ROOT_DIR=C:\Strawberry\c
    echo Found OpenSSL via Strawberry Perl: %OPENSSL_ROOT_DIR%
) else if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\include\openssl\ssl.h" (
        set OPENSSL_ROOT_DIR=%VCPKG_ROOT%\installed\x64-windows
        echo Found OpenSSL via vcpkg: %OPENSSL_ROOT_DIR%
    )
)

if not defined OPENSSL_ROOT_DIR (
    echo WARNING: OpenSSL not found in common locations
    echo You may need to manually set OPENSSL_ROOT_DIR
)

REM Detect and set Boost path
set BOOST_INCLUDEDIR=
set BOOST_LIBRARYDIR=
if exist "%~dp0boost-1.83.0" (
    set BOOST_INCLUDEDIR=%~dp0boost-1.83.0
    set BOOST_LIBRARYDIR=%~dp0boost-1.83.0\stage\lib
    echo Using bundled Boost: %BOOST_INCLUDEDIR%
) else if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\include\boost" (
        set BOOST_INCLUDEDIR=%VCPKG_ROOT%\installed\x64-windows\include
        set BOOST_LIBRARYDIR=%VCPKG_ROOT%\installed\x64-windows\lib
        echo Found Boost via vcpkg: %BOOST_INCLUDEDIR%
    )
) else if exist "C:\msys64\mingw64\include\boost" (
    set BOOST_INCLUDEDIR=C:\msys64\mingw64\include
    set BOOST_LIBRARYDIR=C:\msys64\mingw64\lib
    echo Found Boost via MSYS2: %BOOST_INCLUDEDIR%
)

REM Set libevent path (use bundled)
set CMAKE_INCLUDE_PATH=%~dp0libevent-install\include
set CMAKE_LIBRARY_PATH=%~dp0libevent-install\lib

REM Detect compiler
set COMPILER_FOUND=0

REM Check for Visual Studio
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found Visual Studio compiler
    set COMPILER_FOUND=1
    set "GENERATOR=Visual Studio 16 2019"
    set "ARCH=-A x64"
    goto :check_vcpkg
)

REM Check for MinGW
where gcc >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found MinGW compiler
    set COMPILER_FOUND=1
    set "GENERATOR=MinGW Makefiles"
    goto :configure
)

echo ERROR: No supported compiler found. Please install Visual Studio or MinGW-w64
pause
exit /b 1

:check_vcpkg
REM Check for vcpkg
if defined VCPKG_ROOT (
    echo Found vcpkg at %VCPKG_ROOT%
    set "CMAKE_TOOLCHAIN_FILE=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
) else (
    echo vcpkg not found. You may need to manually install dependencies.
    set CMAKE_TOOLCHAIN_FILE=
)
goto :configure

:configure
echo.
echo Configuring build with detected dependencies:
echo   OpenSSL: %OPENSSL_ROOT_DIR%
echo   Boost: %BOOST_INCLUDEDIR%
echo   libevent: %CMAKE_INCLUDE_PATH%
echo.

REM Create build directory
if not exist build mkdir build
cd build

REM Configure with detected settings
cmake .. -G "%GENERATOR%" %ARCH% %CMAKE_TOOLCHAIN_FILE% ^
    -DBUILD_RADIANT_WALLET=OFF ^
    -DBUILD_RADIANT_ZMQ=OFF ^
    -DBUILD_RADIANT_QT=OFF ^
    -DENABLE_UPNP=OFF ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT_DIR%" ^
    -DBOOST_INCLUDEDIR="%BOOST_INCLUDEDIR%" ^
    -DBOOST_LIBRARYDIR="%BOOST_LIBRARYDIR%" ^
    -DCMAKE_INCLUDE_PATH="%CMAKE_INCLUDE_PATH%" ^
    -DCMAKE_LIBRARY_PATH="%CMAKE_LIBRARY_PATH%"

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    pause
    exit /b 1
)

echo.
echo Building...
echo.

REM Build
if "%GENERATOR%"=="Visual Studio 16 2019" (
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

echo.
echo To create a portable distribution:
echo   ..\create-portable-dist.bat

pause
