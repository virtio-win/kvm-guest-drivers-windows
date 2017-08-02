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
call clean_dvl_log.cmd
call :rmdir Install
call :rmdir Install_Debug
call :rmdir x64
call :rmdir x86
call :rmfiles build.err build.log buildfre_*.log buildchk_*.log msbuild.log
call :rmfiles netkvm.DVL.XML SDV-default.xml sdv-user.sdv

pushd CoInstaller
call clean.bat
popd

pushd NDIS5
call clean.bat
popd

pushd Mof
call clean.bat
popd
