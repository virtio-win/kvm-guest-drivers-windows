@echo off

if "%DDKVER%"=="" set DDKVER=7600.16385.1
set BUILDROOT=C:\WINDDK\%DDKVER%

if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=105
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=109000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=72

if /i "%1"=="prepare" goto %1
if /i "%1"=="finalize" goto %1
if /i "%1"=="Win7" goto %1_%2
if /i "%1"=="Win8" goto %1_%2
if /i "%1"=="Win10" goto %1_%2

echo  Invalid OS version
goto :eof

:Win7_x86
rem set _NT_TARGET_VERSION=0x601
call :BuildWin8 "Win7 Release|x86" buildfre_win7_x86.log
goto :eof

:Win7_x64
rem set _NT_TARGET_VERSION=0x601
call :BuildWin8 "Win7 Release|x64" buildfre_win7_amd64.log
goto :eof

:Win8_x86
call :BuildWin8 "Win8 Release|x86" buildfre_win8_x86.log
goto :eof

:Win8_x64
call :BuildWin8 "Win8 Release|x64" buildfre_win8_amd64.log
goto :eof

:BuildWin8
setlocal
if "%_NT_TARGET_VERSION%"=="" set _NT_TARGET_VERSION=0x602
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=105
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=109000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=72

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_%
call ..\..\tools\callVisualStudio.bat 14 vioser.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:Win10_x86
call :BuildWin10 "Win10 Release|x86" buildfre_win10_x86.log
goto :eof

:Win10_x64
call :BuildWin10 "Win10 Release|x64" buildfre_win10_amd64.log
goto :eof

:BuildWin10
setlocal
if "%_NT_TARGET_VERSION%"=="" set _NT_TARGET_VERSION=0x603
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=105
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=109000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=72

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_%
call ..\..\tools\callVisualStudio.bat 14 vioser.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:prepare_version
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
goto :eof


:create2012H
echo #ifndef __DATE__ 
echo #define __DATE__ "%DATE%"
echo #endif
echo #ifndef __TIME__
echo #define __TIME__ "%TIME%"
echo #endif
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ%
echo #define _NT_TARGET_MIN %_RHEL_RELEASE_VERSION_%
echo #define _MAJORVERSION_ %_BUILD_MAJOR_VERSION_%
echo #define _MINORVERSION_ %_BUILD_MINOR_VERSION_%
goto :eof

:prepare
set _NT_TARGET_VERSION=0x0602
call :prepare_version
call :create2012H  > 2012-defines.h
goto :eof

:finalize
echo finalizing build (%2 %3)
rem del 2012-defines.h 
rem pushd ..
rem call packOne.bat %2 %3 vioser
rem popd
goto :eof

