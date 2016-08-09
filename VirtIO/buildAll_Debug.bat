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
for %%A in (WXp WNet WLH Win7 Win8 Win10) do for %%B in (32 64) do call :%%A_%%B

goto :wdf
:parameters_here

:nextparam
if "%1"=="" goto :eof
goto %1
:continue
shift
goto nextparam

:wdf
pushd WDF
call buildAll_Debug.bat %*
popd
goto :eof

:WLH_32
call :BuildProject "Vista Debug|x86" buildchk_wlh_x86.log
goto :continue

:WLH_64
call :BuildProject "Vista Debug|x64" buildchk_wlh_amd64.log
goto :continue

:WNet_32
call :BuildProject "Win2k3 Debug|x86" buildchk_wnet_x86.log
goto :continue

:WNet_64
call :BuildProject "Win2k3 Debug|x64" buildchk_wnet_amd64.log
goto :continue

:WXp_32
call :BuildProject "WinXP Debug|x86" buildchk_wxp_x86.log
goto :continue

:WXp_64
goto :continue

:Win7_32
call :BuildProject "Win7 Debug|x86" buildchk_win7_x86.log
goto :continue

:Win7_64
call :BuildProject "Win7 Debug|x64" buildchk_win7_amd64.log
goto :continue

:Win8_32
call :BuildProject "Win8 Debug|x86" buildchk_win8_x86.log
goto :continue

:Win8_64
call :BuildProject "Win8 Debug|x64" buildchk_win8_amd64.log
goto :continue

:Win10_32
call :BuildProject "Win10 Debug|x86" buildchk_win10_x86.log
goto :continue

:Win10_64
call :BuildProject "Win10 Debug|x64" buildchk_win10_amd64.log
goto :continue

:BuildProject
call ..\tools\callVisualStudio.bat 14 VirtioLib.vcxproj /Rebuild "%~1" /Out %2
goto :eof
