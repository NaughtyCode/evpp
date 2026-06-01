@echo off
setlocal

echo ============================================
echo  CloudEngine - Android GameClient Build
echo ============================================
echo.

cd /d "%~dp0"

set "ANDROID_ABI=arm64-v8a"
set "ANDROID_PLATFORM=android-24"
set "BUILD_CONFIG=Release"
set "ANDROID_NDK=%~dp0artifacts\android-sdk\ndk\android-ndk-r29"

if not "%~1"=="" set "ANDROID_ABI=%~1"
if not "%~2"=="" set "ANDROID_PLATFORM=%~2"
if /i "%~3"=="debug" set "BUILD_CONFIG=Debug"
if /i "%~3"=="release" set "BUILD_CONFIG=Release"
if /i "%~3"=="relwithdebinfo" set "BUILD_CONFIG=RelWithDebInfo"
if not "%~4"=="" set "ANDROID_NDK=%~4"

set "ANDROID_TOOLCHAIN=%ANDROID_NDK%\build\cmake\android.toolchain.cmake"
set "BUILD_DIR=%~dp0artifacts\build-android\%ANDROID_ABI%"

if not exist "%ANDROID_TOOLCHAIN%" (
    echo [ERROR] Android NDK toolchain not found:
    echo         %ANDROID_TOOLCHAIN%
    echo.
    echo Install/extract Android NDK r29 there, or pass NDK dir as argument 4.
    exit /b 1
)

echo [INFO] ABI:       %ANDROID_ABI%
echo [INFO] Platform:  %ANDROID_PLATFORM%
echo [INFO] Config:    %BUILD_CONFIG%
echo [INFO] NDK:       %ANDROID_NDK%
echo [INFO] Build dir: %BUILD_DIR%
echo.

cmake -S src\client -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%ANDROID_TOOLCHAIN%" ^
    -DANDROID_ABI=%ANDROID_ABI% ^
    -DANDROID_PLATFORM=%ANDROID_PLATFORM% ^
    -DCMAKE_BUILD_TYPE=%BUILD_CONFIG% ^
    -DENGINE_MONGODB_ENABLED=OFF ^
    -DENGINE_PROFILER_ENABLED=OFF ^
    -DENGINE_PHYSICS_ENABLED=OFF ^
    -DHTTPS=OFF

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Android CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --target GameClient --config %BUILD_CONFIG%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Android GameClient build failed.
    exit /b 1
)

echo.
echo [DONE] Android GameClient build completed.
echo        SO: artifacts\android\%ANDROID_ABI%\bin\%BUILD_CONFIG%\libGameClient.so

endlocal
