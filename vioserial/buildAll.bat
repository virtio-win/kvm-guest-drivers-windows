@echo off

for %%A in (Wnet Wlh WXp Win7 Win8 Win10) do for %%B in (32 64) do call :buildpack %%A_%%B

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
call packOne.bat %1 %2 vioser
cd ..
goto :eof

:packapp
cd app
call packOne.bat %1 %2 vioser-test
cd ..
goto :eof

:buildpack
if /i "%1"=="WXp" if "%2"=="64" goto :eof
set BUILD_ARC=%2
set BUILD_ARC=x%BUILD_ARC:32=86%
call :buildsys %1 %BUILD_ARC%
call :buildapp %1 %BUILD_ARC%
call :packsys %1 %BUILD_ARC%
call :packapp %1 %BUILD_ARC%
set BUILD_OS=
set BUILD_ARC=
goto :eof

