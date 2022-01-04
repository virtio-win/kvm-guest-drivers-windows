@echo off
goto start
:rmdir
if exist "%~1" rmdir "%~1" /s /q
goto :eof

:rmfiles
if "%~1"=="" goto :eof
if exist "%~1" del "%~1"
shift
goto rmfiles

:start

call :rmdir x64
call :rmdir Win8Debug
call :rmdir Win8Release
call :rmdir Win8.1Debug
call :rmdir Win8.1Release
call :rmdir Win10Release
call :rmdir Win10Debug
