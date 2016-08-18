@echo off

if "%1_%2" neq "_" (
  call :%1_%2
) else (
  call clean.bat
  for %%A in (Wxp Wnet Wlh Win7 Win8 Win10) do for %%B in (32 64) do call :%%A_%%B
)

goto :eof

:Win7_32
call :BuildProject "Win7 Release|x86" buildfre_win7_x86.log
goto :eof

:Win7_64
call :BuildProject "Win7 Release|x64" buildfre_win7_amd64.log
goto :eof

:Wlh_32
call :BuildProject "Vista Release|x86" buildfre_wlh_x86.log
goto :eof

:Wlh_64
call :BuildProject "Vista Release|x64" buildfre_wlh_amd64.log
goto :eof

:Wnet_32
call :BuildProject "Win2k3 Release|x86" buildfre_wnet_x86.log
goto :eof

:Wnet_64
call :BuildProject "Win2k3 Release|x64" buildfre_wnet_amd64.log
goto :eof

:Wxp_32
call :BuildProject "WinXP Release|x86" buildfre_wxp_x86.log
goto :eof

:Wxp_64
goto :eof

:Win8_32
call :BuildProject "Win8 Release|x86" buildfre_win8_x86.log
goto :eof

:Win8_64
call :BuildProject "Win8 Release|x64" buildfre_win8_amd64.log
goto :eof

:Win10_32
call :BuildProject "Win10 Release|x86" buildfre_win10_x86.log
goto :eof

:Win10_64
call :BuildProject "Win10 Release|x64" buildfre_win10_amd64.log
goto :eof

:BuildProject
call ..\..\tools\callVisualStudio.bat 14 VirtioLib-WDF.vcxproj /Rebuild "%~1" /Out %2
goto :eof
