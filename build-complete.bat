@echo off
REM Complete build and packaging script for Radiant Core Windows distribution

echo ========================================
echo Radiant Core Complete Build & Package
echo ========================================
echo.

REM Check if we're in the right directory
if not exist "CMakeLists.txt" (
    echo ERROR: Please run this script from the Radiant Core root directory
    pause
    exit /b 1
)

REM Step 1: Build
echo Step 1: Building Radiant Core...
call build-portable-windows.bat
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo Step 2: Creating portable distribution...
call create-portable-dist.bat
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Distribution creation failed
    pause
    exit /b 1
)

echo.
echo Step 3: Creating installer...
if exist "C:\Program Files\NSIS\makensis.exe" (
    "C:\Program Files\NSIS\makensis.exe" create-installer.nsi
    if %ERRORLEVEL% EQU 0 (
        echo Installer created successfully!
    ) else (
        echo WARNING: NSIS installer creation failed
    )
) else (
    echo NSIS not found. Skipping installer creation.
    echo To create an installer, install NSIS from https://nsis.sourceforge.io/
)

echo.
echo ========================================
echo Build and packaging completed!
echo ========================================
echo.
echo Created files:
if exist "dist" (
    echo   - dist\ folder with portable version
    dir /b dist
)
if exist "Radiant-Core-2.0.0-win64-setup.exe" (
    echo   - Radiant-Core-2.0.0-win64-setup.exe ^(installer^)
)

echo.
echo Distribution options:
echo 1. Portable: Copy the dist\ folder to any Windows machine
echo 2. Installer: Use the setup.exe for professional installation
echo.
echo Both options should work on any Windows 10/11 system
echo without requiring additional software installations.

pause
