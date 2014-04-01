@echo off

setlocal

if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

for %%O in (Vista Win7 Win8) do for %%P in (Win32 x64) do call :build_driver %%O %%P
for %%O in (Wlh Win7 Win8) do for %%P in (Win32 x64) do call :build_um_proivder %%O %%P

endlocal

goto :eof

:set_windows_version
if "%1"=="Vista" set _NT_TARGET_VERSION=0x600
if "%1"=="Wlh" set _NT_TARGET_VERSION=0x600
if "%1"=="Win7" set _NT_TARGET_VERSION=0x610
if "%1"=="Win8" set _NT_TARGET_VERSION=0x620
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + ((%_NT_TARGET_VERSION% & 255) >> 4)"
goto :eof

:build_driver
call :set_windows_version %1
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_%
call :create_version_file "viorng\2012-defines.h"
call ..\tools\callVisualStudio.bat 11 viorng.sln /Rebuild "%1 Release|%2"
goto :eof

:build_um_proivder
call :set_windows_version %1
call :create_version_file "cng\um\2012-defines.h"
call ..\tools\callVisualStudio.bat 11 viorngum.sln /Rebuild "Release|%2"
set DEST=%2
if "%DEST%"=="Win32" set DEST=x86
copy "%2\Release\viorngum.dll" "Install\%1\%DEST%\"
goto :eof

:create_version_file
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ% >  "%~1"
echo #define _NT_TARGET_MIN %_NT_TARGET_MIN% >> "%~1"
echo #define _MAJORVERSION_ %_MAJORVERSION_% >> "%~1"
echo #define _MINORVERSION_ %_MINORVERSION_% >> "%~1"
goto :eof
