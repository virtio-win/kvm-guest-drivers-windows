@echo off

setlocal

if "%_NT_TARGET_VERSION%"=="" set _NT_TARGET_VERSION=0x602
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 

REM Create the include file for the driver's resource file.
call :create_version_file "viorng\2012-defines.h"
call :create_version_file "cng\um\2012-defines.h"

for %%O in (Win7 Win8) do for %%P in (Win32 x64) do call ..\tools\callVisualStudio.bat 11 viorng.sln /Rebuild "%%O Release|%%P"

for %%P in (Win32 x64) do call ..\tools\callVisualStudio.bat 11 viorngum.sln /Rebuild "Release|%%P"
for %%O in (Win7 Win8) do copy "Win32\Release\viorngum.dll" "Install\%%O\x86\"
for %%O in (Win7 Win8) do copy "x64\Release\viorngum.dll" "Install\%%O\x64\"

endlocal

goto :eof

:create_version_file
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ% >  "%~1"
echo #define _NT_TARGET_MIN %_NT_TARGET_MIN% >> "%~1"
echo #define _MAJORVERSION_ %_MAJORVERSION_% >> "%~1"
echo #define _MINORVERSION_ %_MINORVERSION_% >> "%~1"
goto :eof
