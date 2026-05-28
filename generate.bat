@echo off
setlocal

echo ============================================
echo  CloudEngine - Generate Project Files
echo ============================================
echo.

cd /d "%~dp0"

if not exist "src\CMakeLists.txt" (
    echo [ERROR] src\CMakeLists.txt not found!
    echo Please run this script from the project root.
    pause
    exit /b 1
)

if not exist "artifacts" mkdir "artifacts"

echo [INFO] Running CMake configure with preset: windows-msvc
echo [INFO] Source: src\
echo [INFO] Build dir: artifacts\build
echo.

cmake --preset windows-msvc -S src

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] CMake configure failed!
    pause
    exit /b 1
)

echo.
echo [DONE] Project files generated successfully.
echo        Build artifacts at: artifacts\build\
echo        Open artifacts\build\CloudEngine.sln in Visual Studio, or run build.bat

endlocal
