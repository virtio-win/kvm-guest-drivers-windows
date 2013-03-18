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
for /d %%d in  (VirtIO\obj*) do call :rmdir %%d
for /d %%d in  (wxp\objfre*) do call :rmdir %%d
for /d %%d in  (common\objfre*) do call :rmdir %%d
for /d %%d in  (wxp\objchk*) do call :rmdir %%d
for /d %%d in  (common\objchk*) do call :rmdir %%d
call :rmdir Install
call :rmdir Install_Checked
call :rmfiles wxp\BuildLog.htm
call :rmfiles *.log *.err *.wrn 
