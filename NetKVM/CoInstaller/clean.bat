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
call :rmdir VistaDebug
call :rmdir VistaRelease
call :rmdir Win7Debug
call :rmdir Win7Release
call :rmdir Win8Debug
call :rmdir Win8Release 
