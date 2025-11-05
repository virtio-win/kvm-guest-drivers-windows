@echo off

if "%VIRTIO_WIN_NO_ARM%"=="" call build\build.bat virtio-win.sln "Win10 Win11" ARM64
if errorlevel 1 goto :fail

call build\build.bat virtio-win.sln "Win10 Win11" %*
if errorlevel 1 goto :fail

path %path%;C:\Program Files (x86)\Windows Kits\10\bin\x86\
for %%D in (pciserial fwcfg Q35) do @(
 call :bld_inf_drvr %%D
)
goto :bld_success

:bld_inf_drvr
set inf_drv=%~1
echo.
echo Building : %inf_drv%%
echo.
pushd %inf_drv%
call buildAll.bat
if not errorlevel==0 (
  goto :fail
)
echo Build for %inf_drv% succeeded.
popd
goto :eof

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
