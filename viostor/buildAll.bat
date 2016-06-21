@echo off

set SYS_FILE_NAME=viostor

if "%1_%2" neq "_" goto %1_%2
for %%A in (Win10 Win8 Win7 Wnet Wlh WXp) do for %%B in (32 64) do call :%%A_%%B
set SYS_FILE_NAME=
goto :eof 

:buildpack
setlocal
set BUILD_OS=%1
set BUILD_ARC=%2

if exist Install\%BUILD_OS%\%BUILD_ARC% rmdir Install\%BUILD_OS%\%BUILD_ARC% /s /q
call :BuildProject %3 %4 %5
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%

endlocal
goto :eof

:create2012H
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ%
echo #define _NT_TARGET_MIN %_NT_TARGET_MIN%
echo #define _MAJORVERSION_ %_MAJORVERSION_%
echo #define _MINORVERSION_ %_MINORVERSION_%
goto :eof


:BuildProject
if exist viostor-2012.h del viostor-2012.h
setlocal
set _NT_TARGET_VERSION=%2
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%
call :create2012H  > viostor-2012.h
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_%
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\tools\callVisualStudio.bat 14 viostor.vcxproj /Rebuild "%~1" /Out %3
endlocal
goto :eof

:StaticDriverVerifier
setlocal
set _NT_TARGET_VERSION=%3
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" %2
msbuild.exe viostor.vcxproj /t:clean /p:Configuration="%~1" /P:Platform=%2 
msbuild.exe viostor.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="%~1" /P:platform=%2
msbuild.exe viostor.vcxproj /p:Configuration="%~1" /P:Platform=%2 /P:RunCodeAnalysisOnce=True
msbuild.exe viostor.vcxproj /t:sdv /p:inputs="/devenv /check" /p:Configuration="%~1" /P:platform=%2
msbuild.exe viostor.vcxproj /t:dvl /p:Configuration="%~1" /P:platform=%2
endlocal
goto :eof

:WIN10_32
call :buildpack Win10 x86 "Win10 Release|x86" 0xA00 buildfre_win10_x86.log
goto :eof

:WIN10_64
call :buildpack Win10 x64 "Win10 Release|x64" 0xA00 buildfre_win10_amd64.log
if "%_BUILD_DISABLE_SDV%"=="" goto :DO_SDV
goto :eof
:DO_SDV
call :StaticDriverVerifier "Win10 Release" x64 0xA00
call packOne.bat Win10 x64 %SYS_FILE_NAME%
goto :eof

:WIN8_32
call :buildpack Win8 x86 "Win8 Release|x86" 0x602 buildfre_win8_x86.log
goto :eof

:WIN8_64
call :buildpack Win8 x64 "Win8 Release|x64" 0x602 buildfre_win8_amd64.log
if "%_BUILD_DISABLE_SDV%"=="" goto :DO_SDV
goto :eof
:DO_SDV
call :StaticDriverVerifier "Win8 Release" x64 0x602
call packOne.bat Win8 x64 %SYS_FILE_NAME%
rmdir /S /Q .\sdv
goto :eof

:WIN7_32
call :buildpack Win7 x86 "Win7 Release|x86" 0x601 buildfre_win7_x86.log
goto :eof

:WIN7_64
call :buildpack Win7 x64 "Win7 Release|x64" 0x601 buildfre_win7_amd64.log
goto :eof

:WLH_32
call :buildpack Wlh x86 "Vista Release|x86" 0x600 buildfre_wlh_x86.log
goto :eof

:WLH_64
call :buildpack Wlh x64 "Vista Release|x64" 0x600 buildfre_wlh_amd64.log
goto :eof

:WNET_32
call :buildpack Wnet x86 "Win2k3 Release|x86" 0x502 buildfre_wnet_x86.log
goto :eof

:WNET_64
call :buildpack Wnet x64 "Win2k3 Release|x64" 0x502 buildfre_wnet_amd64.log
goto :eof

:WXP_32
call :buildpack Wxp x86 "WinXP Release|x86" 0x501 buildfre_wxp_x86.log
goto :eof

:WXP_64
goto :eof
