@echo off

goto start
:rmdir
if exist "%~1" rmdir "%~1" /s /q
goto :eof

:rmfiles
if "%~1"=="" goto :eof
if exist "%~1" del /f "%~1"
shift
goto rmfiles

:start
call :rmdir Install
call :rmdir Install_Debug
call :rmdir "NetKVM Package\VistaRelease"
call :rmdir "NetKVM Package\VistaDebug"
call :rmdir "NetKVM Package\Win8Release"
call :rmdir "NetKVM Package\Win8Debug"
call :rmdir "NetKVM Package\Win7Release"
call :rmdir "NetKVM Package\Win7Debug"
call :rmdir "NetKVM Package\x64"
call :rmdir x64
call :rmdir x86
del build.err
del build.log
del buildfre_*.log
del buildchk_*.log
del msbuild.log
del netkvm.DVL.XML
call clean_dvl_log.cmd

pushd CoInstaller
call clean.bat
popd

pushd NDIS5
call clean.bat
popd
