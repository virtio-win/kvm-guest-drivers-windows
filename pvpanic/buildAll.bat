@echo off

setlocal

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

if "%1_%2" neq "_" (
   call :build_driver %1 %2
) else (
  for %%O in (Wlh Win7 Win8 Win10) do for %%P in (32 64) do call :build_driver %%O %%P
)

endlocal

goto :eof

:set_os_and_platform
if "%1"=="Wlh" set OS=wlh
if "%1"=="Win7" set OS=win7
if "%1"=="Win8" set OS=win8
if "%1"=="Win10" set OS=win10
if "%2"=="32" set "PLAT=x86" & set BUILDPLAT=Win32
if "%2"=="64" set "PLAT=amd64" &set BUILDPLAT=x64
set BUILDCONFIG=%1
set BUILDCONFIG=%BUILDCONFIG:Wlh=Vista%
goto :eof

:set_out_filename
call :set_os_and_platform %1 %2
set OUT_FILENAME=buildfre_%OS%_%PLAT%.log
goto :eof

:build_driver
call :set_out_filename %1 %2
call ..\tools\callVisualStudio.bat 14 pvpanic.sln /Rebuild "%BUILDCONFIG% Release|%BUILDPLAT%" /Out %OUT_FILENAME%
goto :eof
