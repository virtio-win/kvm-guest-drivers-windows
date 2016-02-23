@echo off
setlocal

if "%DDKINSTALLROOT%"=="" set DDKINSTALLROOT=C:\WINDDK\
if "%DDKVER%"=="" set DDKVER=7600.16385.1

call :%1_%2
goto :eof

:Win8_x86
call :BuildProject "Win8 Release|Win32" buildfre_win8_x86.log
goto :eof

:Win8_x64
call :BuildProject "Win8 Release|x64" buildfre_win8_amd64.log
goto :eof

:Win7_x86
call :BuildProject "Win7 Release|Win32" buildfre_win7_x86.log
goto :eof

:Win7_x64
call :BuildProject "Win7 Release|x64" buildfre_win7_amd64.log
goto :eof

:Wnet_x86
call :BuildProject "Win2k3 Release|Win32" buildfre_wnet_x86.log
goto :eof

:Wnet_x64
call :BuildProject "Win2k3 Release|x64" buildfre_wnet_amd64.log
goto :eof

:Wlh_x86
call :BuildProject "Vista Release|Win32" buildfre_wlh_x86.log
goto :eof

:Wlh_x64
call :BuildProject "Vista Release|x64" buildfre_wlh_amd64.log
goto :eof

:WXp_x86
call :BuildProject "WinXP Release|Win32" buildfre_wxp_x86.log
goto :eof

:BuildProject
call ..\..\tools\callVisualStudio.bat 12 vioser-test.vcxproj /Rebuild "%~1" /Out %2
goto :eof
