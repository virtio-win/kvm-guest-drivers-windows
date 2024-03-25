@echo off

call :rmdir Install
call :rmdir Install_Debug
call :rmfiles *.log
call :rmfiles *.err
call :cleandir

pushd cng\um
call :cleandir
popd

pushd coinstaller
call :cleandir
popd

pushd test
call :cleandir
popd

pushd viorng
call ..\..\Tools\clean.bat
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
