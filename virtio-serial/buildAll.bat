@echo off

set SYS_FILE_NAME=vioser

for %%A in (Win7 Wnet Wlh WXp) do for %%B in (32 64) do call :%%A_%%B
goto :eof 

:buildsys
call buildOne.bat %1 %2
goto :eof

:packsys
call packOne.bat %1 %2 %SYS_FILE_NAME% %3
goto :eof

:buildpack
call :buildsys %1 %2
call :packsys %1 %2 %3
set BUILD_OS=
set BUILD_ARC=
set INF_FILE_NAME=
goto :eof

:WIN7_32
set BUILD_OS=Win7
set BUILD_ARC=x86
set INF_FILE_NAME=Wlh
call :buildpack %BUILD_OS% %BUILD_ARC% %INF_FILE_NAME%
goto :eof

:WIN7_64
set BUILD_OS=Win7
set BUILD_ARC=x64
set INF_FILE_NAME=Wlh
call :buildpack %BUILD_OS% %BUILD_ARC% %INF_FILE_NAME%
goto :eof

:WLH_32
set BUILD_OS=Wlh
set BUILD_ARC=x86
set INF_FILE_NAME=Wlh
call :buildpack %BUILD_OS% %BUILD_ARC% %INF_FILE_NAME%
goto :eof

:WLH_64
set BUILD_OS=Wlh
set BUILD_ARC=x64
set INF_FILE_NAME=Wlh
call :buildpack %BUILD_OS% %BUILD_ARC% %INF_FILE_NAME%
goto :eof

:WNET_32
set BUILD_OS=Wnet
set BUILD_ARC=x86
set INF_FILE_NAME=Wnet
call :buildpack %BUILD_OS% %BUILD_ARC% %INF_FILE_NAME%
goto :eof

:WNET_64
set BUILD_OS=Wnet
set BUILD_ARC=x64
set INF_FILE_NAME=Wnet
call :buildpack %BUILD_OS% %BUILD_ARC% %INF_FILE_NAME%
goto :eof

:WXP_32
set BUILD_OS=WXp
set BUILD_ARC=x86
set INF_FILE_NAME=Wxp
call :buildpack %BUILD_OS% %BUILD_ARC% %INF_FILE_NAME%
goto :eof

:WXP_64
goto :eof
