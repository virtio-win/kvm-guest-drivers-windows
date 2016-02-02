@echo off

set SYS_FILE_NAME=balloon

for %%A in (Win7 Wnet Wlh WXp Win8) do for %%B in (32 64) do call :%%A_%%B
set SYS_FILE_NAME=
goto :eof 

:buildsys
cd sys
call buildOne.bat %1 %2
cd ..
goto :eof

:packsys
cd sys
call packOne.bat %1 %2 %SYS_FILE_NAME%
cd ..
goto :eof

:buildpack
call :buildsys %1 %2
call :packsys %1 %2
set BUILD_OS=
set BUILD_ARC=
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

:Win8_32
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
rem pause
goto :eof

:Win8_64
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
rem pause
goto :eof
