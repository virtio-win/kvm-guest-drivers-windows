@echo off

if "%1_%2" neq "_" goto %1_%2
for %%A in (Wlh Win7 Win8 Win10) do for %%B in (32 64) do call :%%A_%%B
goto :eof 


:BuildProject
setlocal

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\tools\callVisualStudio.bat 14 vioscsi.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:BuildSDV
setlocal

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" %2
msbuild.exe vioscsi.vcxproj /t:clean /p:Configuration="%~1" /P:Platform=%2 
msbuild.exe vioscsi.vcxproj /t:sdv /p:inputs="/clean" /p:Configuration="%~1" /P:platform=%2
msbuild.exe vioscsi.vcxproj /p:Configuration="%~1" /P:Platform=%2 /P:RunCodeAnalysisOnce=True
msbuild.exe vioscsi.vcxproj /t:sdv /p:inputs="/devenv /check" /p:Configuration="%~1" /P:platform=%2
msbuild.exe vioscsi.vcxproj /t:dvl /p:Configuration="%~1" /P:platform=%2
endlocal
goto :eof

:build
setlocal
set BUILD_OS=%1
set BUILD_ARC=%2

if exist Install\%BUILD_OS%\%BUILD_ARC% rmdir Install\%BUILD_OS%\%BUILD_ARC% /s /q
call :BuildProject %3 %4

endlocal
goto :eof

:WLH_32
call :build Wlh x86 "Vista Release|x86" buildfre_wlh_x86.log
goto :eof

:WLH_64
call :build Wlh x64 "Vista Release|x64" buildfre_wlh_amd64.log
goto :eof

:WIN7_32
call :build Win7 x86 "Win7 Release|x86" buildfre_win7_x86.log
goto :eof

:WIN7_64
call :build Win7 x64 "Win7 Release|x64" buildfre_win7_amd64.log
goto :eof

:WIN8_32
call :build Win8 x86 "Win8 Release|x86" buildfre_win8_x86.log
goto :eof

:WIN8_64
call :build Win8 x64 "Win8 Release|x64" buildfre_win8_amd64.log
if "%_BUILD_DISABLE_SDV%"=="" goto :DO_SDV
goto :eof
:DO_SDV
call :BuildSDV "Win8 Release" x64
copy vioscsi.DVL.XML Install\Win8\amd64
rmdir /S /Q .\sdv
goto :eof

:WIN10_32
call :build Win10 x86 "Win10 Release|x86" buildfre_win10_x86.log
goto :eof

:WIN10_64
call :build Win10 x64 "Win10 Release|x64" buildfre_win10_amd64.log
if "%_BUILD_DISABLE_SDV%"=="" goto :DO_SDV
goto :eof
:DO_SDV
call :BuildSDV "Win10 Release" x64
copy vioscsi.DVL.XML Install\Win10\amd64
rmdir /S /Q .\sdv
goto :eof

:eof
