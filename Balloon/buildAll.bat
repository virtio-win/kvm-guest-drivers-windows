@echo off

set SYS_FILE_NAME=balloon
set APP_FILE_NAME=blnsvr

set OLD_PATH=%PATH%

for %%A in (Win7 Wnet Wlh WXp) do for %%B in (32 64) do call :%%A_%%B
goto :eof 

:buildsys
cd sys
call buildOne.bat %1 %2
cd ..
goto :eof

:buildapp
cd app
call buildOne.bat %1 %2 
cd ..
goto :eof

:packsys
cd sys
call packOne.bat %1 %2 %SYS_FILE_NAME%
cd ..
goto :eof

:packapp
cd app
call packOne.bat %1 %2 %APP_FILE_NAME%
cd ..
goto :eof

:buildpack
call :buildsys %1 %2
call :buildapp %1 %2
call :packsys %1 %2
call :packapp %1 %2
set BUILD_OS=
set BUILD_ARC=
goto :eof

:WIN7_32
set BUILD_OS=Win7
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
goto :eof

:WIN7_64
set BUILD_OS=Win7
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
goto :eof

:WLH_32
set BUILD_OS=Wlh
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
goto :eof

:WLH_64
set BUILD_OS=Wlh
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
goto :eof

:WNET_32
set BUILD_OS=Wnet
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
goto :eof

:WNET_64
set BUILD_OS=Wnet
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
goto :eof

:WXP_32
set BUILD_OS=WXp
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
goto :eof

:WXP_64
goto :eof

PATH=%OLD_PATH%
