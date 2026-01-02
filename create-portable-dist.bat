@echo off
REM Copy required DLLs for portable distribution

echo ========================================
echo Copying DLLs for portable distribution
echo ========================================
echo.

REM Set source directories based on available installations
set OPENSSL_DIR=
set MSYS2_DIR=
set VCPKG_DIR=

REM Detect OpenSSL location
if exist "C:\Strawberry\c\bin\libcrypto-3-x64.dll" (
    set OPENSSL_DIR=C:\Strawberry\c\bin
) else if exist "C:\msys64\mingw64\bin\libcrypto-3-x64.dll" (
    set OPENSSL_DIR=C:\msys64\mingw64\bin
) else if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\bin\libcrypto-3-x64.dll" (
        set OPENSSL_DIR=%VCPKG_ROOT%\installed\x64-windows\bin
    )
)

REM Detect MSYS2
if exist "C:\msys64\mingw64\bin" (
    set MSYS2_DIR=C:\msys64\mingw64\bin
)

REM Detect vcpkg
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\bin" (
        set VCPKG_DIR=%VCPKG_ROOT%\installed\x64-windows\bin
    )
)

REM Create output directory
if not exist "dist" mkdir dist
if not exist "dist\dlls" mkdir dist\dlls

echo Copying DLLs to dist\dlls...

REM Copy OpenSSL DLLs
if defined OPENSSL_DIR (
    echo Found OpenSSL at %OPENSSL_DIR%
    if exist "%OPENSSL_DIR%\libcrypto-3-x64.dll" copy "%OPENSSL_DIR%\libcrypto-3-x64.dll" "dist\dlls\" >nul
    if exist "%OPENSSL_DIR%\libssl-3-x64.dll" copy "%OPENSSL_DIR%\libssl-3-x64.dll" "dist\dlls\" >nul
    if exist "%OPENSSL_DIR%\libcrypto-1_1-x64.dll" copy "%OPENSSL_DIR%\libcrypto-1_1-x64.dll" "dist\dlls\" >nul
    if exist "%OPENSSL_DIR%\libssl-1_1-x64.dll" copy "%OPENSSL_DIR%\libssl-1_1-x64.dll" "dist\dlls\" >nul
) else (
    echo WARNING: OpenSSL DLLs not found
)

REM Copy MinGW runtime DLLs
if defined MSYS2_DIR (
    echo Found MSYS2 at %MSYS2_DIR%
    if exist "%MSYS2_DIR%\libgcc_s_seh-1.dll" copy "%MSYS2_DIR%\libgcc_s_seh-1.dll" "dist\dlls\" >nul
    if exist "%MSYS2_DIR%\libstdc++-6.dll" copy "%MSYS2_DIR%\libstdc++-6.dll" "dist\dlls\" >nul
    if exist "%MSYS2_DIR%\libwinpthread-1.dll" copy "%MSYS2_DIR%\libwinpthread-1.dll" "dist\dlls\" >nul
) else (
    echo WARNING: MSYS2 runtime DLLs not found
)

REM Copy libevent DLLs
if defined MSYS2_DIR (
    if exist "%MSYS2_DIR%\libevent-2-1.dll" copy "%MSYS2_DIR%\libevent-2-1.dll" "dist\dlls\" >nul
    if exist "%MSYS2_DIR%\libevent_core-2-1.dll" copy "%MSYS2_DIR%\libevent_core-2-1.dll" "dist\dlls\" >nul
    if exist "%MSYS2_DIR%\libevent_extra-2-1.dll" copy "%MSYS2_DIR%\libevent_extra-2-1.dll" "dist\dlls\" >nul
)

if defined VCPKG_DIR (
    if exist "%VCPKG_DIR%\event.dll" copy "%VCPKG_DIR%\event.dll" "dist\dlls\" >nul
)

REM Copy executables
echo.
echo Copying executables...
if exist "build\src\Release\radiantd.exe" (
    copy "build\src\Release\radiantd.exe" "dist\" >nul
    copy "build\src\Release\radiant-cli.exe" "dist\" >nul
    copy "build\src\Release\radiant-tx.exe" "dist\" >nul
) else if exist "build\src\radiantd.exe" (
    copy "build\src\radiantd.exe" "dist\" >nul
    copy "build\src\radiant-cli.exe" "dist\" >nul
    copy "build\src\radiant-tx.exe" "dist\" >nul
) else (
    echo ERROR: No executables found in build directory
    pause
    exit /b 1
)

REM Copy configuration files
if exist "doc\bitcoin.conf" copy "doc\bitcoin.conf" "dist\" >nul
if exist "doc\radiant.conf" copy "doc\radiant.conf" "dist\" >nul

REM Create a README for the distribution
echo Creating README...
(
echo Radiant Core Portable Distribution
echo ===================================
echo.
echo This is a portable distribution of Radiant Core that should run on
echo any Windows 10/11 system without requiring additional installations.
echo.
echo To run Radiant Core:
echo 1. Run radiantd.exe to start the daemon
echo 2. Run radiant-cli.exe for command-line interface
echo 3. Run radiant-tx.exe for transaction utilities
echo.
echo Configuration:
echo - Edit radiant.conf to configure the daemon
echo - The blockchain data will be stored in %APPDATA%\Radiant
echo.
echo Requirements:
echo - Windows 10/11
echo - Internet connection for blockchain synchronization
echo.
echo For more information, visit: https://radiantblockchain.org
) > "dist\README.txt"

echo.
echo ========================================
echo Distribution created in dist\ folder
echo ========================================
echo.
echo Contents:
dir /b dist
echo.
echo DLLs copied:
dir /b dist\dlls
echo.
echo To test the portable version:
cd dist
radiantd.exe --version

pause
