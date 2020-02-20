@echo off

call :rmdir Install
call :rmdir Install_Debug
call :rmdir Release
call :rmfiles *.log
call :rmfiles *.err
call :cleandir

pushd pci
call :cleandir
for /D %%D IN (objchk_*) do call call :rmdir %%D
for /D %%D IN (objfre_*) do call call :rmdir %%D
popd

pushd svc
call :rmdir Release
call :cleandir
popd

pushd "VirtFS Package"
call :cleandir
popd

goto :eof

:rmdir
if exist "%~1" rmdir "%~1" /s /q
goto :eof

:rmfiles
if "%~1"=="" goto :eof
if exist "%~1" del /f "%~1"
shift
goto rmfiles

:cleandir
call :rmdir Win32
call :rmdir x64
goto :eof
