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

IF NOT DEFINED PSDK_INC_PATH (
SET PSDK_INC_PATH_NO_QUOTES=
) ELSE (
SET PSDK_INC_PATH_NO_QUOTES=%PSDK_INC_PATH:"=%
)

if NOT "%PSDK_INC_PATH_NO_QUOTES%" == "" goto sdk_set
pushd CoInstaller
call setbuildenv.bat
popd
:sdk_set

set BUILDROOT=C:\WINDDK\%DDKVER%
set X64ENV=x64
if "%DDKVER%"=="6000" set X64ENV=amd64

if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set _DRIVER_ISO_NAME=Install-%_MINORVERSION_%%_MAJORVERSION_%.iso

if not "%1"=="" goto parameters_here
echo no parameters specified, rebuild all
call clean.bat
call "%0" Win8 Win8_64 Vista Vista64 XP XP64 Win7 Win7_64
call :PackInstall
goto :eof
:parameters_here

if "%1"=="PackInstall" goto nextparam
if "%1"=="installer" goto nextparam
: this is for VirtIO
call :copyVirtIO


:nextparam
if NOT "%1"=="" goto %1
endlocal
echo Environment cleanup done
goto :eof
:continue
shift
goto nextparam

: VirtIO files are copied for build, deleted on clean.
: The reason is small difference in includes and makefile defines
: (NDIS is different from general-purpose kernel)
:
:copyVirtIO
for %%f in (..\VirtIO\VirtIO*.h ..\VirtIO\VirtIO*.c) do copy %%f VirtIO /Y
goto :eof


:preparebuild
echo DIRS=%* > dirs
for %%f in (%*) do echo !include $(NTMAKEENV)\makefile.def > %%f\makefile
set /a _NT_TARGET_MAJ_ARCH="%_NT_TARGET_VERSION% >> 8"
set /a _NT_TARGET_MIN_ARCH="%_NT_TARGET_VERSION% & 255"

set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%
rem set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 

set _VERSION_=%_NT_TARGET_MAJ%.%_NT_TARGET_MIN%.%_MAJORVERSION_%.%_MINORVERSION_%
echo version set: %_VERSION_%
goto :eof

:Win7 
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre Win7 no_oacr
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_win7_x86\i386\netkvm.sys call tools\makeinstall x86 wlh\objfre_win7_x86\i386\netkvm.sys wlh\netkvm.inf %_VERSION_% Win7 CoInstaller\objfre_win7_x86\i386\netkvmco.dll CoInstaller\readme.doc
endlocal
if not exist wlh\objfre_win7_x86\i386\netkvm.sys goto :eof
if not exist CoInstaller\objfre_win7_x86\i386\netkvmco.dll goto :eof
goto continue

:Win7_64 
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre Win7 no_oacr
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_win7_amd64\amd64\netkvm.sys call tools\makeinstall amd64 wlh\objfre_win7_amd64\amd64\netkvm.sys wlh\netkvm.inf %_VERSION_% Win7 CoInstaller\objfre_win7_amd64\amd64\netkvmco.dll CoInstaller\readme.doc
endlocal
if not exist wlh\objfre_win7_amd64\amd64\netkvm.sys goto :eof
if not exist CoInstaller\objfre_win7_amd64\amd64\netkvmco.dll goto :eof
goto continue

:Vista
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre Wlh no_oacr
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_wlh_x86\i386\netkvm.sys call tools\makeinstall x86 wlh\objfre_wlh_x86\i386\netkvm.sys wlh\netkvm.inf %_VERSION_% Vista CoInstaller\objfre_wlh_x86\i386\netkvmco.dll CoInstaller\readme.doc
endlocal
if not exist wlh\objfre_wlh_x86\i386\netkvm.sys goto :eof
if not exist CoInstaller\objfre_wlh_x86\i386\netkvmco.dll goto :eof
goto continue

:Vista64
rem echo Skipping Vista64 for now, comment to enable
rem goto :eof

set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre Wlh no_oacr
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_wlh_amd64\amd64\netkvm.sys call tools\makeinstall amd64 wlh\objfre_wlh_amd64\amd64\netkvm.sys wlh\netkvm.inf %_VERSION_% Vista CoInstaller\objfre_wlh_amd64\amd64\netkvmco.dll CoInstaller\readme.doc
endlocal
if not exist wlh\objfre_wlh_amd64\amd64\netkvm.sys goto :eof
if not exist CoInstaller\objfre_wlh_amd64\amd64\netkvmco.dll goto :eof
goto continue

:XP
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre WXP no_oacr
popd
call :preparebuild Common wxp VirtIO
build -cZg

if exist wxp\objfre_wxp_x86\i386\netkvm.sys call tools\makeinstall x86 wxp\objfre_wxp_x86\i386\netkvm.sys wxp\netkvm.inf %_VERSION_% XP
endlocal
if not exist wxp\objfre_wxp_x86\i386\netkvm.sys goto :eof
goto continue

:XP64
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre WNET no_oacr
popd
call :preparebuild Common wxp VirtIO
build -cZg

if exist wxp\objfre_wnet_amd64\amd64\netkvm.sys call tools\makeinstall amd64 wxp\objfre_wnet_amd64\amd64\netkvm.sys wxp\netkvm.inf %_VERSION_% XP
endlocal
if not exist wxp\objfre_wnet_amd64\amd64\netkvm.sys goto :eof
goto continue

:BuildUsing2012
setlocal
set PSDK_INC_PATH=
set PSDK_LIB_PATH=
call ..\tools\callVisualStudio.bat 11 CoInstaller\netkvmco.vcxproj /Rebuild "%~1" /Out %2
call ..\tools\callVisualStudio.bat 11 NetKVM-2012.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:Win8
set DDKBUILDENV=
if exist Install\win8\x86 rmdir Install\win8\x86 /s /q
call :BuildUsing2012 "Win8 Release|Win32" buildfre_win8_x86.log
goto continue

:Win8_64
set DDKBUILDENV=
if exist Install\win8\amd64 rmdir Install\win8\amd64 /s /q
call :BuildUsing2012 "Win8 Release|x64" buildfre_win8_amd64.log
goto continue

::
:: This part of the batch called from Win8 environment
:: (Start)

:set2012OS-wlh
set _NT_TARGET_VERSION=0x600
set OSName=Vista
goto :eof

:set2012OS-win7
set _NT_TARGET_VERSION=0x601
set OSName=Win7
goto :eof

:set2012OS-win8
set _NT_TARGET_VERSION=0x602
set OSName=Win8
goto :eof

:create2012H
echo #ifndef __DATE__ 
echo #define __DATE__ "%DATE%"
echo #endif
echo #ifndef __TIME__
echo #define __TIME__ "%TIME%"
echo #endif
echo #define PARANDIS_MAJOR_DRIVER_VERSION %_MAJORVERSION_%
echo #define PARANDIS_MINOR_DRIVER_VERSION %_MINORVERSION_%
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ%
echo #define _NT_TARGET_MIN %_NT_TARGET_MIN%
echo #define _MAJORVERSION_ %_MAJORVERSION_%
echo #define _MINORVERSION_ %_MINORVERSION_%
goto :eof

::%1=OS(wlh,win7,win8)
:Prepare2012
shift
echo Prepare2012 (1) %1
call :set2012OS-%1
call :preparebuild
call :create2012H  > NetKVM-2012.h
goto :eof

::%1=OS(wlh,win7,win8) %2=(x86,amd64) %3=targetdir 
:Finalize2012
shift
echo Finalize2012: (1) %1 (2) %2 (3) %3
call :set2012OS-%1
call :preparebuild
set _BUILDARCH=%2
set _COINSTBIN=CoInstaller\%OSName%Release\x86\netkvmco.dll
if /i "%2"=="amd64" set _COINSTBIN=CoInstaller\%OSName%Release\x64\netkvmco.dll
if exist "%~3netkvm.sys" call tools\makeinstall %2 "%~3netkvm.sys" wlh\netkvm.inf %_VERSION_% %OSName% %_COINSTBIN% CoInstaller\readme.doc
rem del NetKVM-2012.h
goto :eof

:: (End)
:: This part of the batch called from Win8 environment
:: (End)


:Win2K compilation is no longer supported by Windows 7 DDK
:Win2K
:setlocal
:set DDKBUILDENV=
:pushd %BUILDROOT%
:call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre W2K
:popd
:call :preparebuild Common wxp VirtIO
:build -cZg

:if exist wxp\objfre_w2k_x86\i386\netkvm.sys call tools\makeinstall x86 wxp\objfre_w2k_x86\i386\netkvm.sys wxp\netkvm2k.inf %_VERSION_% 2K
:endlocal
:if not exist wxp\objfre_w2k_x86\i386\netkvm.sys goto :eof
:goto continue


:installer
:"C:\Program Files\Microsoft Visual Studio 8\Common7\IDE\devenv" /Rebuild "Release" Installer\Package\Package.sln /Log Installer\Package\build.log
:goto continue

:PackInstall
:echo Packing to ISO image
:call tools\makecdimage.cmd %_DRIVER_ISO_NAME% Install

:goto :eof
