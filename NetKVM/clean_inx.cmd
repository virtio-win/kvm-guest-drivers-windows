@echo off
if "%loglevel%"=="" (
  if "%~1"=="-quiet" (set loglevel=0)
  if "%~1"=="-debug" (set loglevel=2)
  if "%~1"=="" (set loglevel=1)
)
call :rmfiles *.inx
goto :eof

:rmfiles
if "%~1"=="" goto :eof
if exist "%~1" (
  if %loglevel% NEQ 0 echo Removing the %~1 file...
  del /f "%~1"
) else (
  if %loglevel% EQU 2 echo We did not find any %~1 file^(s^) to remove.
)
shift
goto rmfiles
