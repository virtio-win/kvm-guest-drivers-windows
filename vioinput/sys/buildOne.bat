@echo off
setlocal

call :%1_%2
goto :eof

:Win10_x86
call :BuildProject "Win10 Release|x86" buildfre_win10_x86.log
goto :eof

:Win10_x64
call :BuildProject "Win10 Release|x64" buildfre_win10_amd64.log
goto :eof

:Win8_x86
call :BuildProject "Win8 Release|x86" buildfre_win8_x86.log
goto :eof

:Win8_x64
call :BuildProject "Win8 Release|x64" buildfre_win8_amd64.log
goto :eof

:Win7_x86
call :BuildProject "Win7 Release|x86" buildfre_win7_x86.log
goto :eof

:Win7_x64
call :BuildProject "Win7 Release|x64" buildfre_win7_amd64.log
goto :eof

:BuildProject
setlocal

call ..\..\tools\callVisualStudio.bat 14 vioinput.vcxproj /Rebuild "%~1" /Out %2
endlocal
goto :eof
