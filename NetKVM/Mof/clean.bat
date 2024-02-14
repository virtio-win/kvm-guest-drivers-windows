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

call :rmdir obj
call :rmdir Win32
call :rmdir x64

call :rmfiles ..\Common\netkvmmof.h netkvm.bmf
