@echo off
if /i "%1"=="WXp" goto %2
if /i "%1"=="Wnet" goto %2
if /i "%1"=="Wlh" goto %2
if /i "%1"=="Win7" goto %2
if /i "%1"=="Win8" goto %2
if /i "%1"=="Win10" goto %2

goto :eof

:x86
if exist "Release\%~2" goto :eof
call :BuildProject "Release|x86" buildfre_x86.log
goto :eof

:x64
if exist "Release\%~2" goto :eof
call :BuildProject "Release|x64" buildfre_amd64.log
goto :eof

:BuildProject
setlocal
call ..\..\tools\callVisualStudio.bat 14 blnsvr.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:eof

