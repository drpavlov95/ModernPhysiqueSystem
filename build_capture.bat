@echo off
setlocal
pushd "%~dp0"

set "LOG_FILE=%TEMP%\rley_build_%RANDOM%_%RANDOM%.log"
set "LATEST_LOG=%~dp0build_out.txt"

call "%~dp0build_temp.bat" > "%LOG_FILE%" 2>&1
set "EC=%ERRORLEVEL%"
>> "%LOG_FILE%" echo EXIT_CODE=%EC%
copy /y "%LOG_FILE%" "%LATEST_LOG%" >nul 2>&1
echo BUILD_LOG=%LOG_FILE%

popd
exit /b %EC%
