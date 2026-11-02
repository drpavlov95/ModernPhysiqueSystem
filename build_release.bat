@echo off
setlocal
pushd "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 (
    set "EC=%ERRORLEVEL%"
    popd
    exit /b %EC%
)

cmake --preset build-release-msvc
if errorlevel 1 (
    set "EC=%ERRORLEVEL%"
    popd
    exit /b %EC%
)

cmake --build --preset release-msvc
set "EC=%ERRORLEVEL%"
popd
exit /b %EC%
