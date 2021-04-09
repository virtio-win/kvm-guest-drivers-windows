@echo off
if not "%EnterpriseWDK%"=="" goto ready
call :find_ewdk
rem Workaround for SDV
set path=%path%;%ewdk_drive%\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Redist\MSVC\14.20.27508\onecore\x86\Microsoft.VC141.OPENMP
call %ewdk_drive%\BuildEnv\SetupBuildEnv.cmd
goto :eof

:find_ewdk
set ewdk_drive=e:
goto :eof

:ready
echo We are already in EWDK version: %Version_Number%
goto :eof
