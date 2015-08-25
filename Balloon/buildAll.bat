@echo off

set SYS_FILE_NAME=balloon
set APP_FILE_NAME=blnsvr

for %%A in (Win7 Win8 Win10) do for %%B in (32 64) do call :%%A_%%B
set SYS_FILE_NAME=
set APP_FILE_NAME=
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

:Win7_32
setlocal
set BUILD_OS=Win7
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:Win7_64
setlocal
set BUILD_OS=Win7
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:Win8_32
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:Win8_64
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:Win10_32
setlocal
set BUILD_OS=Win10
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:Win10_64
setlocal
set BUILD_OS=Win10
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof
