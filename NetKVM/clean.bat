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
for /d %%d in  (VirtIO\fre*) do call :rmdir %%d
for /d %%d in  (VirtIO\obj*) do call :rmdir %%d
for /d %%d in  (wlh\objfre*) do call :rmdir %%d
for /d %%d in  (wxp\objfre*) do call :rmdir %%d
for /d %%d in  (common\objfre*) do call :rmdir %%d
call :rmdir Install
call :rmdir win7
call :rmdir 2012Build
call :rmdir wlh\objfre_wlh_x86
call :rmdir wlh\objfre_wlh_amd64
call :rmdir wlh\objfre_win7_amd64
call :rmdir wlh\objfre_win7_x86
call :rmfiles dirs wlh\makefile wlh\BuildLog.htm wxp\makefile wxp\BuildLog.htm common\makefile VirtIO\makefile
for %%f in (VirtIO\*.c VirtIO\*.h *.log *.wrn) do call :rmfiles %%f


pushd CoInstaller
call clean.bat
popd

