@echo off

for %%A in (Win7 Win8 Win10) do for %%B in (32 64) do call :buildpack %%A %%B

goto :eof

:buildsys
cd sys
call buildOne.bat %1 %2
cd ..
goto :eof

:packsys
cd sys
call packOne.bat %1 %2 vioinput
cd ..
goto :eof

:buildpack
setlocal
set BUILD_ARC=%2
set BUILD_ARC=x%BUILD_ARC:32=86%
call :buildsys %1 %BUILD_ARC%
call :packsys %1 %BUILD_ARC%
endlocal
goto :eof
