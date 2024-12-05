@echo off
if "%loglevel%"=="" (
  if "%~1"=="-quiet" (set loglevel=0)
  if "%~1"=="-debug" (set loglevel=2)
  if "%~1"=="" (set loglevel=1)
)

for /D %%D IN (objchk_*) do @call :rmdir %%D
for /D %%D IN (objfre_*) do @call :rmdir %%D

call :rmdir .\Install
call :rmdir .\Release
call :rmdir .\Debug
call :rmdir .\Install_Debug
call :rmdir .\Win10Release
call :rmdir .\Win10Debug
call :rmdir .\Win11Release
call :rmdir .\Win11Debug
call :rmdir .\ARM64
call :rmdir .\Win32
call :rmdir .\x64
call :rmdir .\x86
call :rmdir .\obj
call :rmdir .\sdv
call :rmdir .\sdv.temp
call :rmdir .\codeql_db
call :rmfiles *.dvl.xml *.dvl-compat.xml
call :rmfiles sdv-map.h sdv-user.sdv SDV-default.xml
call :rmfiles smvstats.txt smvbuild.log vc.nativecodeanalysis.all.xml
call :rmfiles *.sarif codeql.build.bat
call :rmfiles build.sdv.config
call :rmfiles *.log *.wrn *.err *.sdf
call :rmfiles qemufwcfg.cat
call :rmfiles ..\Common\netkvmmof.h netkvm.bmf
goto :eof

:rmdir
if exist "%~1" (
  if %loglevel% NEQ 0 echo Removing %~1 directory tree...
  rmdir "%~1" /s /q
) else (
  if %loglevel% EQU 2 echo We did not find any %~1 directory tree to remove.
)
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
