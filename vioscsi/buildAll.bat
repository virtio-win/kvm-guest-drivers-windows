rem @echo off

set SYS_FILE_NAME=vioscsi

if "%1_%2" neq "_" goto %1_%2
for %%A in (Wlh Win7 Win8 Win10) do for %%B in (32 64) do call :%%A_%%B
set SYS_FILE_NAME=
goto :eof 


:buildsys
call buildOne.bat %1 %2
goto :eof

:packsys
call packOne.bat %1 %2 %SYS_FILE_NAME%
goto :eof

:buildpack
call :buildsys %1 %2
call :packsys %1 %2
set BUILD_OS=
set BUILD_ARC=
goto :eof

:create2012H
echo #define _NT_TARGET_MAJ %_NT_TARGET_MAJ%
echo #define _NT_TARGET_MIN %_NT_TARGET_MIN%
echo #define _MAJORVERSION_ %_MAJORVERSION_%
echo #define _MINORVERSION_ %_MINORVERSION_%
goto :eof


:BuildUsingVS2015
if exist vioscsi-2012.h del vioscsi-2012.h
setlocal
if "%_NT_TARGET_VERSION%"=="" set _NT_TARGET_VERSION=0x602
if "%_BUILD_MAJOR_VERSION_%"=="" set _BUILD_MAJOR_VERSION_=101
if "%_BUILD_MINOR_VERSION_%"=="" set _BUILD_MINOR_VERSION_=58000
if "%_RHEL_RELEASE_VERSION_%"=="" set _RHEL_RELEASE_VERSION_=61

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set /a _NT_TARGET_MAJ="(%_NT_TARGET_VERSION% >> 8) * 10 + (%_NT_TARGET_VERSION% & 255)"
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%
call :create2012H  > vioscsi-2012.h
set STAMPINF_VERSION=%_NT_TARGET_MAJ%.%_RHEL_RELEASE_VERSION_%.%_BUILD_MAJOR_VERSION_%.%_BUILD_MINOR_VERSION_% 
call ..\tools\callVisualStudio.bat 14 vioscsi.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:StaticDriverVerifierVS2015
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
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" %2
msbuild.exe vioscsi.vcxproj /t:clean /p:Configuration="%~1" /P:Platform=%2 
msbuild.exe vioscsi.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="%~1" /P:platform=%2
msbuild.exe vioscsi.vcxproj /p:Configuration="%~1" /P:Platform=%2 /P:RunCodeAnalysisOnce=True
msbuild.exe vioscsi.vcxproj /t:sdv /p:inputs="/devenv /check" /p:Configuration="%~1" /P:platform=%2
msbuild.exe vioscsi.vcxproj /t:dvl /p:Configuration="%~1" /P:platform=%2
endlocal
goto :eof

:Win10_32
setlocal
set BUILD_OS=Win10
set BUILD_ARC=x86

if exist Install\win10\x86 rmdir Install\win10\x86 /s /q
call :BuildUsingVS2015 "Win10 Release|x86" buildfre_win10_x86.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
endlocal
goto :eof

:Win10_64
setlocal
set BUILD_OS=Win10
set BUILD_ARC=x64

if exist Install\win10\amd64 rmdir Install\win10\amd64 /s /q
call :BuildUsingVS2015 "Win10 Release|x64" buildfre_win10_amd64.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
call :StaticDriverVerifierVS2015 "Win10 Release" %BUILD_ARC%
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
rmdir /S /Q .\sdv
endlocal
goto :eof

:Win8_32
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x86

if exist Install\win8\x86 rmdir Install\win8\x86 /s /q
call :BuildUsingVS2015 "Win8 Release|x86" buildfre_win8_x86.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
endlocal
goto :eof

:Win8_64
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x64

if exist Install\win8\amd64 rmdir Install\win8\amd64 /s /q
call :BuildUsingVS2015 "Win8 Release|x64" buildfre_win8_amd64.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
call :StaticDriverVerifierVS2015 "Win8 Release" %BUILD_ARC%
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
rmdir /S /Q .\sdv
endlocal
goto :eof

:Win7_32
setlocal
set BUILD_OS=Win7
set BUILD_ARC=x86

if exist Install\win7\x86 rmdir Install\win7\x86 /s /q
call :BuildUsingVS2015 "Win7 Release|x86" buildfre_win7_x86.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
endlocal
goto :eof

:Win7_64
setlocal
set BUILD_OS=Win7
set BUILD_ARC=x64

if exist Install\win7\amd64 rmdir Install\win7\amd64 /s /q
call :BuildUsingVS2015 "Win7 Release|x64" buildfre_win7_amd64.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
endlocal
goto :eof

:WLH_64
setlocal
set BUILD_OS=Wlh
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WLH_32
setlocal
set BUILD_OS=Wlh
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof


:eof
