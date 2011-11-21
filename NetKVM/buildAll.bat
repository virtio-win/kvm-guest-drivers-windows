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
if "%DDKVER%"=="" set DDKVER=7600.16385.0

if "%PSDK_INC_PATH%" NEQ "" goto :sdk_set
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

set OLD_PATH=%PATH%

if not "%1"=="" goto parameters_here
echo no parameters specified, rebuild all
call clean.bat
call "%0" Vista Vista64 XP XP64 Win7 Win7_64
call :PackInstall
goto :eof
:parameters_here

if "%1"=="PackInstall" goto nextparam
if "%1"=="installer" goto nextparam
: this is for VirtIO
call :copyVirtIO


:nextparam
if "%1"=="" goto :eof
goto %1
:continue
shift
goto nextparam

: VirtIO files are copied for build, deleted on clean.
: The reason is small difference in includes and makefile defines
: (NDIS is different from general-purpose kernel)
:
:copyVirtIO
for %%f in (..\VirtIO\VirtIO*.h ..\VirtIO\VirtIO*.c ..\VirtIO\PVUtils.c ..\VirtIO\PVUtils.h ..\VirtIO\PVUtils.h) do copy %%f VirtIO /Y
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
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre Win7
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_win7_x86\i386\netkvm.sys call tools\makeinstall x86 wlh\objfre_win7_x86\i386\netkvm.sys wlh\netkvm.inf %_VERSION_% Win7 CoInstaller\objfre_win7_x86\i386\netkvmco.dll CoInstaller\readme.doc
if not exist wlh\objfre_win7_x86\i386\netkvm.sys goto :eof
if not exist CoInstaller\objfre_win7_x86\i386\netkvmco.dll goto :eof
goto continue

:Win7_64 
set DDKBUILDENV=
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre Win7
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_win7_amd64\amd64\netkvm.sys call tools\makeinstall amd64 wlh\objfre_win7_amd64\amd64\netkvm.sys wlh\netkvm.inf %_VERSION_% Win7 CoInstaller\objfre_win7_amd64\amd64\netkvmco.dll CoInstaller\readme.doc
if not exist wlh\objfre_win7_amd64\amd64\netkvm.sys goto :eof
if not exist CoInstaller\objfre_win7_amd64\amd64\netkvmco.dll goto :eof
goto continue

:Vista
set DDKBUILDENV=
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre Wlh
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_wlh_x86\i386\netkvm.sys call tools\makeinstall x86 wlh\objfre_wlh_x86\i386\netkvm.sys wlh\netkvm.inf %_VERSION_% Vista CoInstaller\objfre_wlh_x86\i386\netkvmco.dll CoInstaller\readme.doc
if not exist wlh\objfre_wlh_x86\i386\netkvm.sys goto :eof
if not exist CoInstaller\objfre_wlh_x86\i386\netkvmco.dll goto :eof
goto continue

:Vista64
rem echo Skipping Vista64 for now, comment to enable
rem goto :eof

set DDKBUILDENV=
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre Wlh
popd
call :preparebuild Common wlh VirtIO CoInstaller
build -cZg

if exist wlh\objfre_wlh_amd64\amd64\netkvm.sys call tools\makeinstall amd64 wlh\objfre_wlh_amd64\amd64\netkvm.sys wlh\netkvm.inf %_VERSION_% Vista CoInstaller\objfre_wlh_amd64\amd64\netkvmco.dll CoInstaller\readme.doc
if not exist wlh\objfre_wlh_amd64\amd64\netkvm.sys goto :eof
if not exist CoInstaller\objfre_wlh_amd64\amd64\netkvmco.dll goto :eof
goto continue

:XP
set DDKBUILDENV=
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre WXP
popd
call :preparebuild Common wxp VirtIO
build -cZg

if exist wxp\objfre_wxp_x86\i386\netkvm.sys call tools\makeinstall x86 wxp\objfre_wxp_x86\i386\netkvm.sys wxp\netkvm.inf %_VERSION_% XP
if not exist wxp\objfre_wxp_x86\i386\netkvm.sys goto :eof
goto continue

:XP64
set DDKBUILDENV=
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre WNET
popd
call :preparebuild Common wxp VirtIO
build -cZg

if exist wxp\objfre_wnet_amd64\amd64\netkvm.sys call tools\makeinstall amd64 wxp\objfre_wnet_amd64\amd64\netkvm.sys wxp\netkvm.inf %_VERSION_% XP
if not exist wxp\objfre_wnet_amd64\amd64\netkvm.sys goto :eof
goto continue


:Win2K compilation is no longer supported by Windows 7 DDK
:Win2K
:set DDKBUILDENV=
:pushd %BUILDROOT%
:call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre W2K
:popd
:call :preparebuild Common wxp VirtIO
:build -cZg

:if exist wxp\objfre_w2k_x86\i386\netkvm.sys call tools\makeinstall x86 wxp\objfre_w2k_x86\i386\netkvm.sys wxp\netkvm2k.inf %_VERSION_% 2K
:if not exist wxp\objfre_w2k_x86\i386\netkvm.sys goto :eof
:goto continue


:installer
:"C:\Program Files\Microsoft Visual Studio 8\Common7\IDE\devenv" /Rebuild "Release" Installer\Package\Package.sln /Log Installer\Package\build.log
:goto continue

:PackInstall
:echo Packing to ISO image
:call tools\makecdimage.cmd %_DRIVER_ISO_NAME% Install
goto :eof
echo "setting old path back"
set PATH=%OLD_PATH%