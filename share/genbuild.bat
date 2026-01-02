@echo off
setlocal enabledelayedexpansion
REM Windows version of genbuild.sh

if "%~2"=="" (
    if "%~1"=="" (
        echo Usage: %0 ^<filename^> ^<srcroot^>
        exit /b 1
    )
    set "FILE=%~1"
    set "SRCROOT=%~dp0"
) else (
    set "FILE=%~1"
    set "SRCROOT=%~2"
)

if exist "%FILE%" (
    set /p INFO=<"%FILE%"
) else (
    set INFO=
)

set "DESC="
set "SUFFIX="

REM Check if git is available and we're in a git repo
git rev-parse --is-inside-work-tree >nul 2>&1
if !ERRORLEVEL! EQU 0 (
    REM clean 'dirty' status of touched files that haven't been modified
    git diff >nul 2>&1
    
    REM if latest commit is tagged and not dirty, then override using the tag name
    for /f "delims=" %%i in ('git describe --abbrev=0 2^>nul') do set "RAWDESC=%%i"
    
    for /f "delims=" %%i in ('git rev-parse HEAD') do set "HEAD_COMMIT=%%i"
    for /f "delims=" %%i in ('git rev-list -1 "!RAWDESC!" 2^>nul') do set "TAG_COMMIT=%%i"
    
    if "!HEAD_COMMIT!"=="!TAG_COMMIT!" (
        git diff-index --quiet HEAD -- >nul 2>&1
        if !ERRORLEVEL! EQU 0 (
            set "DESC=!RAWDESC!"
        )
    )
    
    REM otherwise generate suffix from git, i.e. string like "59887e8-dirty"
    for /f "delims=" %%i in ('git rev-parse --short HEAD') do set "SUFFIX=%%i"
    git diff-index --quiet HEAD -- >nul 2>&1
    if !ERRORLEVEL! NEQ 0 (
        set "SUFFIX=!SUFFIX!-dirty"
    )
)

if defined DESC (
    set "NEWINFO=#define BUILD_DESC "!DESC!""
) else if defined SUFFIX (
    set "NEWINFO=#define BUILD_SUFFIX !SUFFIX!"
) else (
    set "NEWINFO=// No build information available"
)

REM only update build.h if necessary
if not "%INFO%"=="%NEWINFO%" (
    echo %NEWINFO% >"%FILE%"
)
