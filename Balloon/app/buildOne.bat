@echo on
setlocal

call :%1_%2
goto :eof

:Win7_x86
call :BuildProject "Win7 Release|x86" buildfre_win7_x86.log
goto :eof

:Win7_x64
call :BuildProject "Win7 Release|x64" buildfre_win7_amd64.log
goto :eof

:Win8_x86
call :BuildProject "Win8 Release|x86" buildfre_win8_x86.log
goto :eof

:Win8_x64
call :BuildProject "Win8 Release|x64" buildfre_win8_amd64.log
goto :eof

:Win10_x86
call :BuildProject "Win10 Release|x86" buildfre_win10_x86.log
goto :eof

:Win10_x64
call :BuildProject "Win10 Release|x64" buildfre_win10_amd64.log
goto :eof

:Wlh_x86
call :BuildProject "Vista Release|x86" buildfre_wlh_x86.log
goto :eof

:Wlh_x64
call :BuildProject "Vista Release|x64" buildfre_wlh_amd64.log
goto :eof

:Wnet_x86
call :BuildProject "Win2k3 Release|x86" buildfre_wnet_x86.log
goto :eof

:Wnet_x64
call :BuildProject "Win2k3 Release|x64" buildfre_wnet_amd64.log
goto :eof

:WXp_x86
call :BuildProject "WinXP Release|x86" buildfre_wxp_x86.log
goto :eof

:BuildProject
setlocal
call ..\..\tools\callVisualStudio.bat 14 blnsvr.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof

:eof

