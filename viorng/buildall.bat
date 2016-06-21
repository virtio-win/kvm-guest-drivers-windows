@echo off

setlocal

if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

for %%O in (Vista Win7 Win8 Win10) do for %%P in (Win32 x64) do call :build_driver %%O %%P

endlocal

goto :eof

:set_windows_version
if "%1"=="Vista" set _NT_TARGET_VERSION=0x600
if "%1"=="Wlh" set _NT_TARGET_VERSION=0x600
if "%1"=="Win7" set _NT_TARGET_VERSION=0x610
if "%1"=="Win8" set _NT_TARGET_VERSION=0x620
if "%1"=="Win10" set _NT_TARGET_VERSION=0xA00
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + ((%_NT_TARGET_VERSION% & 255) >> 4)"
goto :eof

:set_os_and_platform
if "%1"=="Vista" set OS=wlh
if "%1"=="Wlh" set OS=wlh
if "%1"=="Win7" set OS=win7
if "%1"=="Win8" set OS=win8
if "%1"=="Win10" set OS=win10
if "%2"=="Win32" set PLAT=x86
if "%2"=="x64" set PLAT=amd64
goto :eof

:set_out_filename
call :set_os_and_platform %1 %2
set OUT_FILENAME=buildfre_%OS%_%PLAT%.log
goto :eof

:fix_wdfcoinstaller_name
call :set_os_and_platform %1 %2
pushd Install\%OS%\%PLAT%\
for %%V in (01009 01011) do if exist WdfCoinstaller%%V.dll rename WdfCoinstaller%%V.dll WdfCoInstaller%%V.dll
popd
goto :eof

:prebuild_driver
call :set_windows_version %1
call :create_version_file "2012-defines.h"
call :set_out_filename %1 %2
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_%
goto :eof

:build_driver
call :prebuild_driver %1 %2
call ..\tools\callVisualStudio.bat 14 viorng.sln /Rebuild "%1 Release|%2" /Out %OUT_FILENAME%
call :fix_wdfcoinstaller_name %1 %2
goto :eof

:build_um_provider
set BUILDPLAT=%2
if "%2"=="Win32" set BUILDPLAT=x86
call :set_windows_version %1
call :set_out_filename %1 %2
call ..\tools\callVisualStudio.bat 14 cng\um\viorngum.vcxproj /Rebuild "Release|%BUILDPLAT%" /Out %OUT_FILENAME%
copy "cng\um\%2\Release\viorngum.dll" "Install\%OS%\%PLAT%\"
set BUILDPLAT=
goto :eof

:build_co_installer
set BUILDPLAT=%2
if "%2"=="Win32" set BUILDPLAT=x86
call :set_windows_version %1
call :set_out_filename %1 %2
call ..\tools\callVisualStudio.bat 14 coinstaller\viorngci.vcxproj /Rebuild "Release|%BUILDPLAT%" /Out %OUT_FILENAME%
copy "coinstaller\%2\Release\viorngci.dll" "Install\%OS%\%PLAT%\"
set BUILDPLAT=
goto :eof

:create_version_file
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ% >  "%~1"
echo #define _NT_TARGET_MIN %_NT_TARGET_MIN% >> "%~1"
echo #define _MAJORVERSION_ %_MAJORVERSION_% >> "%~1"
echo #define _MINORVERSION_ %_MINORVERSION_% >> "%~1"
goto :eof
