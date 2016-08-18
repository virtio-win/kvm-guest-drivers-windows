@echo off

if "%1_%2" neq "_" (
  call :%1_%2
) else (
  call clean.bat
  for %%A in (Wxp Wnet Wlh Win7 Win8 Win10) do for %%B in (32 64) do call :%%A_%%B
)

pushd WDF
call buildAll_Debug.bat %*
popd
goto :eof

:Wlh_32
call :BuildProject "Vista Debug|x86" buildchk_wlh_x86.log
goto :eof

:Wlh_64
call :BuildProject "Vista Debug|x64" buildchk_wlh_amd64.log
goto :eof

:Wnet_32
call :BuildProject "Win2k3 Debug|x86" buildchk_wnet_x86.log
goto :eof

:Wnet_64
call :BuildProject "Win2k3 Debug|x64" buildchk_wnet_amd64.log
goto :eof

:Wxp_32
call :BuildProject "WinXP Debug|x86" buildchk_wxp_x86.log
goto :eof

:Wxp_64
goto :eof

:Win7_32
call :BuildProject "Win7 Debug|x86" buildchk_win7_x86.log
goto :eof

:Win7_64
call :BuildProject "Win7 Debug|x64" buildchk_win7_amd64.log
goto :eof

:Win8_32
call :BuildProject "Win8 Debug|x86" buildchk_win8_x86.log
goto :eof

:Win8_64
call :BuildProject "Win8 Debug|x64" buildchk_win8_amd64.log
goto :eof

:Win10_32
call :BuildProject "Win10 Debug|x86" buildchk_win10_x86.log
goto :eof

:Win10_64
call :BuildProject "Win10 Debug|x64" buildchk_win10_amd64.log
goto :eof

:BuildProject
call ..\tools\callVisualStudio.bat 14 VirtioLib.vcxproj /Rebuild "%~1" /Out %2
goto :eof
