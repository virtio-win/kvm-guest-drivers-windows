@echo off
set __VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2017
if "%CODEQL_HOME%"=="" set CODEQL_HOME=c:\codeql-home
set CODEQL_BIN=%CODEQL_HOME%\codeql\codeql.cmd

if not "%EnterpriseWDK%"=="" goto ewdk_ready
if "%EWDK11_DIR%"=="" goto vs_vars
call %EWDK11_DIR%\BuildEnv\SetupBuildEnv.cmd
::call :add_path "%VCToolsRedistDir%onecore\x86\Microsoft.VC142.OPENMP"
goto :eof

:vs_vars
if not "%VSFLAVOR%"=="" goto :knownVS
call :checkvs
echo USING %VSFLAVOR% Visual Studio

:knownVS
echo %0: Setting NATIVE ENV for %1 (VS %VSFLAVOR%)...
call "%__VS_PATH%\%VSFLAVOR%\VC\Auxiliary\Build\vcvarsall.bat" %1
goto :eof

:checkvs
set VSFLAVOR=Professional
if exist "%__VS_PATH%\Community\VC\Auxiliary\Build\vcvarsall.bat" set VSFLAVOR=Community
goto :eof

:ewdk_ready
echo We are already in EWDK version: %Version_Number%
goto :eof

:add_path
echo %path% | findstr /i /c:"%~dp1"
if not errorlevel 1 goto :eof
echo Adding path %~dp1
set path=%path%;%~dp1
goto :eof
