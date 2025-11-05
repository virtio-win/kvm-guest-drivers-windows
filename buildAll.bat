@echo off

call build_all_drivers.bat %*
if errorlevel 1 goto :fail

if "%_BUILD_DISABLE_SDV%"=="" call build_sdv.bat %*
if errorlevel 1 goto :fail

if "%VIRTIO_WIN_NO_CAB_CREATION%"=="" call build_cab.bat
if errorlevel 1 goto :fail
goto :bld_success

:bld_success
echo.
echo BUILD COMPLETED SUCCESSFULLY.
call :leave 0
goto :eof

:fail
echo BUILD FAILED.
set BUILD_FAILED=
call :leave 1
goto :eof

:leave
exit /B %1
