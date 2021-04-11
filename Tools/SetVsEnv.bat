@echo off
if not "%EnterpriseWDK%"=="" goto ready
if "%EWDK1903_DIR%"=="" set EWDK1903_DIR=c:\ewdk1903
call :add_path "%EWDK1903_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Redist\MSVC\14.20.27508\onecore\x86\Microsoft.VC141.OPENMP\vcomp140.dll"
call %EWDK1903_DIR%\BuildEnv\SetupBuildEnv.cmd
goto :eof

:add_path
echo %path% | findstr /i /c:"%~dp1"
if not errorlevel 1 goto :eof
echo Adding path %~dp1
set path=%path%;%~dp1
goto :eof

:ready
echo We are already in EWDK version: %Version_Number%
goto :eof
