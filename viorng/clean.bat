@echo off

call :rmdir Install
call :rmdir Install_Debug
call :cleandir

pushd cng\um
call :rmfiles 2012-defines.h
call :cleandir
popd

pushd viorng
call :rmfiles 2012-defines.h
call :cleandir
popd

pushd "VirtRNG Package"
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
