@echo off
if "%loglevel%"=="" (
  if "%~1"=="-quiet" (set loglevel=0)
  if "%~1"=="-debug" (set loglevel=2)
  if "%~1"=="" (set loglevel=1)
)
if "%loglevel%"=="0" (
  set _cln_verbs=quiet
  call ..\build\SetVsEnv.bat > nul
)
if "%loglevel%"=="1" (
  set _cln_verbs=normal
  call ..\build\SetVsEnv.bat
)
if "%loglevel%"=="2" (
  set _cln_verbs=detailed
  call ..\build\SetVsEnv.bat
)
call :rmdir .\sdv
msbuild.exe NetKVM-VS2015.vcxproj /t:clean /p:Configuration="Win10 Release" /P:Platform=x64  -Verbosity:%_cln_verbs%
msbuild.exe NetKVM-VS2015.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="Win10 Release" /P:platform=x64 -Verbosity:%_cln_verbs%
endlocal
goto :eof

:rmdir
if exist "%~1" (
  if %loglevel% NEQ 0 echo Removing %~1 directory tree...
  rmdir "%~1" /s /q
) else (
  if %loglevel% EQU 2 echo We did not find any %~1 directory tree to remove.
)
goto :eof
