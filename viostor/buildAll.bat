@echo off

set SYS_FILE_NAME=viostor

if "%1_%2" neq "_" goto %1_%2
for %%A in (Win8 Win7 Wnet Wlh WXp) do for %%B in (32 64) do call :%%A_%%B
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


:BuildUsing2012
reg query "HKLM\Software\Wow6432Node\Microsoft\Windows Kits\WDK" /v WDKProductVersion > nul
if %ERRORLEVEL% EQU 0 goto BuildUsing2012_WDKOK
echo ERROR building Win8 drivers: Win8 WDK is not installed
cd .
goto :eof
:BuildUsing2012_WDKOK
reg query HKLM\Software\Wow6432Node\Microsoft\VisualStudio\11.0 /v InstallDir > nul
if %ERRORLEVEL% EQU 0 goto BuildUsing2012_VS11OK
echo ERROR building Win8 drivers: VS11 is not installed
cd .
goto :eof
:BuildUsing2012_VS11OK
cscript ..\tools\callVisuaStudio.vbs 11 viostor.vcxproj /Rebuild "%~1" /Out %2
if %ERRORLEVEL% GEQ 1 echo VS2011 Build of "%~1" FAILED
goto :eof


:WIN8_32
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x86
set INF2CAT_PATH=
if exist Install\win8\x86 rmdir Install\win8\x86 /s /q
call :BuildUsing2012 "Win8 Release|Win32" buildfre_win8_x86.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
endlocal
goto :eof

:WIN8_64
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x64
if exist Install\win8\amd64 rmdir Install\win8\amd64 /s /q
call :BuildUsing2012 "Win8 Release|x64" buildfre_win8_amd64.log
call packOne.bat %BUILD_OS% %BUILD_ARC% %SYS_FILE_NAME%
endlocal
goto :eof


:WIN7_32
setlocal
set BUILD_OS=Win7
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WIN7_64
setlocal
set BUILD_OS=Win7
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

:WLH_64
setlocal
set BUILD_OS=Wlh
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WNET_32
setlocal
set BUILD_OS=Wnet
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WNET_64
setlocal
set BUILD_OS=Wnet
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WXP_32
setlocal
set BUILD_OS=WXp
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WXP_64
goto :eof
