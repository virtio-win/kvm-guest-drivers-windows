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
call :rmdir codeql_db
call :rmfiles build.err build.log buildfre_*.log buildchk_*.log msbuild.log
call :rmfiles netkvm.DVL.XML netkvm.DVL-compat.XML SDV-default.xml sdv-user.sdv
call :rmfiles *.sarif codeql.build.bat
call :rmfiles *.inx

for %%d in (CoInstaller Mof NotifyObject ProtocolService) do call :subdir %%d
goto :eof

:subdir
pushd %1
call clean.bat
popd
goto :eof
