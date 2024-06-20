@echo off
if "%CODEQL_HOME%"=="" set CODEQL_HOME=c:\codeql-home
set CODEQL_BIN=%CODEQL_HOME%\codeql\codeql.cmd
set CODEQL_DRIVER_SUITES=%CODEQL_HOME%\Windows-Driver-Developer-Supplemental-Tools\suites

if not "%EnterpriseWDK%"=="" goto ready
if "%1"=="Win11" (
    if "%EWDK11_22H2_DIR%"=="" set EWDK11_22H2_DIR=c:\ewdk11_22h2
    call %EWDK11_22H2_DIR%\BuildEnv\SetupBuildEnv.cmd
    goto :eof
) else (
    if "%EWDK11_DIR%"=="" set EWDK11_DIR=c:\ewdk11
    :: call :add_path "%EWDK11_DIR%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Redist\MSVC\14.28.29910\onecore\x86\Microsoft.VC142.OPENMP\vcomp140.dll"
    call %EWDK11_DIR%\BuildEnv\SetupBuildEnv.cmd
    goto :eof
)

:add_path
echo %path% | findstr /i /c:"%~dp1"
if not errorlevel 1 goto :eof
echo Adding path %~dp1
set path=%path%;%~dp1
goto :eof

:ready
echo We are already in EWDK version: %Version_Number%
goto :eof
