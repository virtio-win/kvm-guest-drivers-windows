@echo off

if "%1_%2" neq "_" (

  call :%1_%2

  goto :eof
) else (

  for %%A in (Wxp Wlh Win7 Win8 Win8.1 Win10) do for %%B in (32 64) do (
    call :%%A_%%B

    if %ERRORLEVEL% NEQ 0 goto :eof
  )
)

if "%_BUILD_DISABLE_SDV%" neq "" goto :eof
call create_dvl_log_vs2015.cmd

goto :eof

:: Build Windows Vista
:Wlh_32
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Release|x86" /Out buildfre_win7_x86.log
endlocal
goto :eof

:Wlh_64
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Vista Release|x64" /Out buildfre_win7_amd64.log
endlocal
goto :eof

:: Build Windows 7
:Win7_32
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Release|x86" /Out buildfre_win7_x86.log
endlocal
goto :eof

:Win7_64
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win7 Release|x64" /Out buildfre_win7_amd64.log
endlocal
goto :eof

:: Build Windows 8
:Win8_32
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Release|x86" /Out buildfre_win8_x86.log
endlocal
goto :eof

:Win8_64
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8 Release|x64" /Out buildfre_win8_amd64.log
endlocal
goto :eof

:: Build Windows 8.1
:Win8.1_32
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Release|x86" /Out buildfre_win8_x86.log
endlocal
goto :eof

:Win8.1_64
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win8.1 Release|x64" /Out buildfre_win8_amd64.log
endlocal
goto :eof

:: Build Windows 10
:Win10_32
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Release|x86" /Out buildfre_win10_x86.log
endlocal
goto :eof

:Win10_64
setlocal
call ..\tools\callVisualStudio.bat 14 netkvm-vs2015.sln /Rebuild "Win10 Release|x64" /Out buildfre_win10_amd64.log
endlocal
goto :eof

:: XP
:Wxp_32
pushd NDIS5
call buildall.bat XP
popd
goto :eof

:: XP64
:Wxp_64
pushd NDIS5
call buildall.bat XP64
popd
goto :eof
