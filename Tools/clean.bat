@echo off

for /D %%D IN (objchk_*) do call :rmdir %%D
for /D %%D IN (objfre_*) do call :rmdir %%D

call :rmdir .\ARM64
call :rmdir .\Win32
call :rmdir .\x64

call :rmdir .\sdv
call :rmdir .\sdv.temp
call :rmdir .\codeql_db
call :rmfiles *.dvl.xml *.dvl-compat.xml
call :rmfiles sdv-map.h sdv-user.sdv SDV-default.xml
call :rmfiles *.sarif codeql.build.bat
call :rmfiles *.log *.wrn *.err
goto :eof

:rmdir
if exist "%~1" rmdir "%~1" /s /q
goto :eof

:rmfiles
if "%~1"=="" goto :eof
if exist "%~1" del /f "%~1"
shift
goto rmfiles
