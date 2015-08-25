@echo off
if /i "%1"=="Win7" goto %1_%2
if /i "%1"=="Win8" goto %1_%2
if /i "%1"=="Win10" goto %1_%2
echo error
goto :eof

:Win7_x86
if exist "Release\%~2" goto :eof
call :BuildWin8 "Release|Win32" buildfre_x86.log
goto :eof

:Win7_x64
if exist "Release\%~2" goto :eof
call :BuildWin8 "Release|x64" buildfre_amd64.log
goto :eof

:Win8_x86
if exist "Release\%~2" goto :eof
call :BuildWin8 "Release|x86" buildfre_x86.log
goto :eof

:Win8_x64
if exist "Release\%~2" goto :eof
call :BuildWin8 "Release|x64" buildfre_amd64.log
goto :eof

:Win10_x86
if exist "Release\%~2" goto :eof
call :BuildWin8 "Release|x86" buildfre_x86.log
goto :eof

:Win10_x64
if exist "Release\%~2" goto :eof
call :BuildWin8 "Release|x64" buildfre_amd64.log
goto :eof

:BuildWin8
setlocal
call ..\..\tools\callVisualStudio.bat 14 blnsvr.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof


:eof

