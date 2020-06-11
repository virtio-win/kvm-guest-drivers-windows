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
call :rmdir ARM64
call :rmfiles build.err build.log buildfre_*.log buildchk_*.log msbuild.log
call :rmfiles netkvm.DVL.XML SDV-default.xml sdv-user.sdv

for %%d in (CoInstaller NDIS5 Mof NotifyObject ProtocolService) do call :subdir %%d
goto :eof

:subdir
pushd %1
call clean.bat
popd
goto :eof
