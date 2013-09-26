@echo off

if "%1_%2" neq "_" goto %1_%2
for %%A in (Win8 Win7 Wlh Wnet WXp) do for %%B in (32 64) do call :%%A_%%B
set SYS_FILE_NAME=
goto :eof 


:buildpack
mkdir .\Install\%1\%2
copy /Y qemupciserial.inf .\Install\%1\%2
set BUILD_OS=
set BUILD_ARC=
goto :eof

:WIN8_32
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WIN8_64
setlocal
set BUILD_OS=Win8
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WLH_64
setlocal
set BUILD_OS=Wlh
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WLH_32
setlocal
set BUILD_OS=Wlh
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WIN7_64
setlocal
set BUILD_OS=Win7
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WIN7_32
setlocal
set BUILD_OS=Win7
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WLH_64
setlocal
set BUILD_OS=Wlh
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WNET_32
setlocal
set BUILD_OS=Wnet
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WNET_64
setlocal
set BUILD_OS=Wnet
set BUILD_ARC=x64
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WXP_32
setlocal
set BUILD_OS=WXp
set BUILD_ARC=x86
call :buildpack %BUILD_OS% %BUILD_ARC%
endlocal
goto :eof

:WXP_64
goto :eof
