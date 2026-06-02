@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.." >nul
python "scripts\build_android_all_platforms_release.py" %*
set "RC=%ERRORLEVEL%"
popd >nul

exit /b %RC%
