@echo off
setlocal

if not "%1"=="" goto parameters_here
echo no parameters specified, rebuild all
call clean.bat
echo "clean done"
call "%0" XP XP64 XPCHK XP64CHK 
goto :eof
:parameters_here


:nextparam
if NOT "%1"=="" goto %1
endlocal
goto :eof
:continue
shift
goto nextparam


:XP
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "WinXP Release|x86" /Out buildfre_wxp_x86.log
endlocal
goto continue

:XP64
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "Win2k3 Release|x64" /Out buildfre_wnet_amd64.log
endlocal
goto continue

:XPCHK
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "WinXP Debug|x86" /Out buildchk_wxp_x86.log
endlocal
goto continue

:XP64CHK
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
call ..\..\tools\callVisualStudio.bat 14 NetKVM-NDIS5.sln /Rebuild "Win2k3 Debug|x64" /Out buildchk_wnet_amd64.log
endlocal
goto continue


:goto :eof
