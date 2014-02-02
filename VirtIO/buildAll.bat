@echo off
:
: Set global parameters: 
:

: Use Windows 7 DDK
if "%DDKVER%"=="" set DDKVER=7600.16385.1

: By default DDK is installed under C:\WINDDK, but it can be installed in different location
if "%DDKISNTALLROOT%"=="" set DDKISNTALLROOT=C:\WINDDK\
set BUILDROOT=%DDKISNTALLROOT%%DDKVER%
set X64ENV=x64
if "%DDKVER%"=="6000" set X64ENV=amd64

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
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre Wlh
popd
build -cZg
endlocal
goto continue

:Win7_64
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre WIN7
popd
build -cZg
endlocal
goto continue


:Vista
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre WIN7
popd
build -cZg
endlocal
goto continue

:Vista64
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre Wlh
popd
build -cZg
endlocal
goto continue

:Win2003
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre WNET
popd
build -cZg
endlocal
goto continue

:Win200364
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% %X64ENV% fre WNET
popd
build -cZg
endlocal
goto continue

:XP
set DDKBUILDENV=
setlocal
pushd %BUILDROOT%
call %BUILDROOT%\bin\setenv.bat %BUILDROOT% fre WXP
popd
build -cZg
endlocal
goto continue

:Win8
call :BuildWin8 "Win8 Release|Win32" buildfre_win8_x86.log
goto continue

:Win8_64
call :BuildWin8 "Win8 Release|x64" buildfre_win8_amd64.log
goto continue

:BuildWin8
call ..\tools\callVisualStudio.bat 12 VirtioLib-win8.vcxproj /Rebuild "%~1" /Out %2
goto :eof
