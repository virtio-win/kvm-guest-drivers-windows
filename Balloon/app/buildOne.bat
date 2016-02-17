@echo off
if /i "%1"=="Win7" goto %1_%2
if /i "%1"=="Win8" goto %1_%2
if /i "%1"=="Win10" goto %1_%2

if "%DDKVER%"=="" set DDKVER=7600.16385.1
set BUILDROOT=C:\WINDDK\%DDKVER%

set DDKBUILDENV=
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %2 fre %1 no_oacr
popd
build -cZg

set DDKVER=
set BUILDROOT=

goto :eof

:Win7_x86
call :BuildProject "Win7 Release|x86" buildfre_win7_x86.log
goto :eof

:Win7_x64
call :BuildProject "Win7 Release|x64" buildfre_win7_amd64.log
goto :eof

:Win8_x86
call :BuildProject "Win8 Release|x86" buildfre_win8_x86.log
goto :eof

:Win8_x64
call :BuildProject "Win8 Release|x64" buildfre_win8_amd64.log
goto :eof

:Win10_x86
call :BuildProject "Win10 Release|x86" buildfre_win10_x86.log
goto :eof

:Win10_x64
call :BuildProject "Win10 Release|x64" buildfre_win10_amd64.log
goto :eof

:BuildProject
setlocal
call ..\..\tools\callVisualStudio.bat 14 blnsvr.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:eof

