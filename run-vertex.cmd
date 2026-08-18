@echo off
setlocal

set "REPOSITORY_ROOT=%~dp0"
set "DEMO_EXE=%REPOSITORY_ROOT%x64\Debug\my-engine-demo.exe"

if not exist "%DEMO_EXE%" set "DEMO_EXE=%REPOSITORY_ROOT%x64\Release\my-engine-demo.exe"

if not exist "%DEMO_EXE%" (
    echo my-engine-demo.exe was not found. Build Debug^|x64 or Release^|x64 first. 1>&2
    exit /b 1
)

"%DEMO_EXE%" --pipeline vertex %*
exit /b %ERRORLEVEL%

