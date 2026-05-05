@echo off
setlocal
if not defined EWDK_PATH set EWDK_PATH=C:\ewdk11_24h2
call %EWDK_PATH%\BuildEnv\SetupBuildEnv.cmd
@echo off
pushd %~dp0
del /f /q build.log 2>nul

set CONFIG=Win11 Release
set PLATFORM=x64
if not "%1"=="" set CONFIG=%~1
if not "%2"=="" set PLATFORM=%~2

echo Building vsock_test [%CONFIG%|%PLATFORM%] ...
msbuild.exe vsock_test.vcxproj "/p:Configuration=%CONFIG%" "/p:Platform=%PLATFORM%" /v:m > build.log 2>&1
if errorlevel 1 (
    echo FAILED. See build.log for details.
    popd
    exit /b 1
)
echo Done: %PLATFORM%\%CONFIG%\vsock_test\vsock_test.exe
popd
