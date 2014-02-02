@echo off

call %~dp0\checkWin8Tools.bat %1

for /f "tokens=*" %%a in ( 
'cscript.exe /nologo "%~dp0\getVisualStudioCmdLine.vbs" %*'
) do ( 
set vs_cmd=%%a 
) 

IF NOT DEFINED vs_cmd (
echo Visual Studio not found
EXIT /b 1
)

SET vs_cmd_no_quotes="%vs_cmd:"=%"
IF "vs_cmd_no_quotes" == "" (
echo Visual Studio not found
EXIT /b 2
)

call %vs_cmd%
if %ERRORLEVEL% GEQ 1 (
echo Build with Visual Studio FAILED
exit /b %ERRORLEVEL%
)

exit /b 0
