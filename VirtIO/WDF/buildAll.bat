@echo off
:
: Set global parameters: 
:

: Use Windows 7 DDK
if "%DDKVER%"=="" set DDKVER=7600.16385.1

: By default DDK is installed under C:\WINDDK, but it can be installed in different location
if "%DDKINSTALLROOT%"=="" set DDKINSTALLROOT=C:\WINDDK\

if not "%1"=="" goto parameters_here
echo no parameters specified, rebuild all
call clean.bat
call "%0" Win8 Win8_64 Win7 Win7_64 Vista Vista64 Win2003 Win200364 XP
goto :eof
:parameters_here

:nextparam
if "%1"=="" goto :eof
goto %1
:continue
shift
goto nextparam

:Win7
call :BuildProject "Win7 Release|Win32" buildfre_win7_x86.log
goto continue

:Win7_64
call :BuildProject "Win7 Release|x64" buildfre_win7_amd64.log
goto continue

:Vista
call :BuildProject "Vista Release|Win32" buildfre_wlh_x86.log
goto continue

:Vista64
call :BuildProject "Vista Release|x64" buildfre_wlh_amd64.log
goto continue

:Win2003
call :BuildProject "Win2k3 Release|Win32" buildfre_wnet_x86.log
goto continue

:Win200364
call :BuildProject "Win2k3 Release|x64" buildfre_wnet_amd64.log
goto continue

:XP
call :BuildProject "WinXP Release|Win32" buildfre_wxp_x86.log
goto continue

:Win8
call :BuildProject "Win8 Release|Win32" buildfre_win8_x86.log
goto continue

:Win8_64
call :BuildProject "Win8 Release|x64" buildfre_win8_amd64.log
goto continue

:BuildProject
call ..\..\tools\callVisualStudio.bat 12 VirtioLib-WDF.vcxproj /Rebuild "%~1" /Out %2
goto :eof
