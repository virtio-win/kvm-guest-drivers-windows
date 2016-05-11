@echo off
:
: Set global parameters: 
: Build tools (DDK) - only tested is 6000 (XP/Vista)
: Version value: each module (at least in resources) has a form OSMajor.OsMinor.DriverMajor.DriverMinor
: For example for XP - 5.1.0.2805,  defined in _VERSION_ variable
: _MAJORVERSION_ = GENERATION * 100 + YEAR - 2000; 
: GENERATION = 2
: _MINORVERSION_ = MONTH * 100 + DAY
:
: Due to kernel.org restriction temporary security sertificates were removed
: from the source tree. To use the Vista (and up) 64bit driverm you will need 
: to sign the drivers with your certificate. Change tools\makeinstall.bat if needed
:
:


setlocal

if "%DDKVER%"=="" set DDKVER=7600.16385.1
set BUILDROOT=C:\WINDDK\%DDKVER%
set X64ENV=x64
if "%DDKVER%"=="6000" set X64ENV=amd64

if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=6

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%

if not "%1"=="" goto parameters_here
echo no parameters specified, rebuild all
call clean.bat
echo "clean done"
call "%0" XP XP64 XPCHK XP64CHK 
goto :eof
:parameters_here


:nextparam
if NOT "%1"=="" goto %1
endlocal
goto :eof
:continue
shift
goto nextparam


:preparebuild
set /a _NT_TARGET_MAJ_ARCH="%_NT_TARGET_VERSION% >> 8"
set /a _NT_TARGET_MIN_ARCH="%_NT_TARGET_VERSION% & 255"

set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

set _VERSION_=%_NT_TARGET_MAJ%.%_NT_TARGET_MIN%.%_MAJORVERSION_%.%_MINORVERSION_%
echo version set: %_VERSION_%
goto :eof

:XP
setlocal
set _BUILDARCH=x86
set _NT_TARGET_VERSION=0x0501
call :preparebuild
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "WinXP Release|x86" /Out buildfre_wxp_x86.log
call tools\makeinstall x86 objfre_wxp_x86\i386\netkvm.sys wxp\netkvm.inf %_VERSION_% XP Install
endlocal
goto continue

:XP64
setlocal
set _BUILDARCH=amd64
set _NT_TARGET_VERSION=0x0502
call :preparebuild
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "Win2k3 Release|x64" /Out buildfre_wnet_amd64.log
call tools\makeinstall amd64 objfre_wnet_amd64\amd64\netkvm.sys wxp\netkvm.inf %_VERSION_% XP Install
endlocal
goto continue

:XPCHK
setlocal
set _BUILDARCH=x86
set _NT_TARGET_VERSION=0x0501
call :preparebuild
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "WinXP Debug|x86" /Out buildchk_wxp_x86.log
call tools\makeinstall x86 objchk_wxp_x86\i386\netkvm.sys wxp\netkvm.inf %_VERSION_% XP Install_Checked
endlocal
goto continue

:XP64CHK
setlocal
set _BUILDARCH=amd64
set _NT_TARGET_VERSION=0x0502
call :preparebuild
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "Win2k3 Debug|x64" /Out buildchk_wnet_amd64.log
call tools\makeinstall amd64 objchk_wnet_amd64\amd64\netkvm.sys wxp\netkvm.inf %_VERSION_% XP Install_Checked
endlocal
goto continue


:goto :eof
