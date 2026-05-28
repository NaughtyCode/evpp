@echo off
setlocal enabledelayedexpansion

echo ============================================
echo  CloudEngine - Build
echo ============================================
echo.

cd /d "%~dp0"

if not exist "artifacts\build\CMakeCache.txt" (
    echo [WARN] Build directory not configured yet. Running generate first...
    echo.
    call "%~dp0generate.bat"
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Project generation failed. Aborting build.
        pause
        exit /b 1
    )
)

set BUILD_CONFIG=Release
if /i "%~1"=="debug"   set BUILD_CONFIG=Debug
if /i "%~1"=="release" set BUILD_CONFIG=Release
if /i "%~1"=="relwithdebinfo" set BUILD_CONFIG=RelWithDebInfo

echo [INFO] Building %BUILD_CONFIG% configuration...
echo.

cmake --build "artifacts\build" --config %BUILD_CONFIG%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo [DONE] Build completed successfully (%BUILD_CONFIG%).
echo        Binaries: artifacts\bin\%BUILD_CONFIG%\

endlocal
