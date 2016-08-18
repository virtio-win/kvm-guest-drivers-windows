@echo off
setlocal

call :%1_%2
goto :eof

:Win7_x86
call :BuildProject "Win7 Release|x86" 0x601 buildfre_win7_x86.log
goto :eof

:Win7_x64
call :BuildProject "Win7 Release|x64" 0x601 buildfre_win7_amd64.log
goto :eof

:Win8_x86
call :BuildProject "Win8 Release|x86" 0x602 buildfre_win8_x86.log
goto :eof

:Win8_x64
call :BuildProject "Win8 Release|x64" 0x602 buildfre_win8_amd64.log
goto :eof

:Win10_x86
call :BuildProject "Win10 Release|x86" 0xA00 buildfre_win10_x86.log
goto :eof

:Win10_x64
call :BuildProject "Win10 Release|x64" 0xA00 buildfre_win10_amd64.log
goto :eof

:Wlh_x86
call :BuildProject "Vista Release|x86" 0x600 buildfre_wlh_x86.log
goto :eof

:Wlh_x64
call :BuildProject "Vista Release|x64" 0x600 buildfre_wlh_amd64.log
goto :eof

:Wnet_x86
call :BuildProject "Win2k3 Release|x86" 0x502 buildfre_wnet_x86.log
goto :eof

:Wnet_x64
call :BuildProject "Win2k3 Release|x64" 0x502 buildfre_wnet_amd64.log
goto :eof

:WXp_x86
call :BuildProject "WinXP Release|x86" 0x501 buildfre_wxp_x86.log
goto :eof

:BuildProject
setlocal

set _MAJORVERSION_=%_BUILD_MAJOR_VERSION_%
set _MINORVERSION_=%_BUILD_MINOR_VERSION_%
set _NT_TARGET_MIN=%_RHEL_RELEASE_VERSION_%

call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 vioser.vcxproj /Rebuild "%~1" /Out %3
endlocal
goto :eof
