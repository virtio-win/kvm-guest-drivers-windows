@echo off
if "%CODEQL_HOME%"=="" set CODEQL_HOME=c:\codeql-home
set CODEQL_BIN=%CODEQL_HOME%\codeql\codeql.cmd
set CODEQL_DRIVER_SUITES=%CODEQL_HOME%\Windows-Driver-Developer-Supplemental-Tools\suites

if not "%EnterpriseWDK%"=="" goto ready
if "%1"=="Win11" (
    if "%EWDK11_24H2_DIR%"=="" set EWDK11_24H2_DIR=c:\ewdk11_24h2
    call %EWDK11_24H2_DIR%\BuildEnv\SetupBuildEnv.cmd
    @echo off
    goto :eof
) else (
    if "%EWDK11_DIR%"=="" set EWDK11_DIR=c:\ewdk11
    :: call :add_path "%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Redist\MSVC\14.28.29910\onecore\x86\Microsoft.VC142.OPENMP\vcomp140.dll"
    call %EWDK11_DIR%\BuildEnv\SetupBuildEnv.cmd
    @echo off
    goto :eof
)

:add_path
echo %path% | findstr /i /c:"%~dp1"
if not errorlevel 1 goto :eof
echo Adding path %~dp1
set path=%path%;%~dp1
goto :eof

:ready
for /f "tokens=4 usebackq delims=\'" %%i in (`echo %%VSINSTALLDIR%%`) do @set vs_year=%%i
echo **********************************************************************
echo ** We are already in an Enterprise WDK build environment
echo ** Version %BuildLab% ^| %Version_Number%
echo ** Visual Studio %vs_year% Developer Command Prompt v%VSCMD_VER%
echo **********************************************************************
goto :eof
