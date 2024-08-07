@echo off

setlocal
echo Loading Windows 10 build env
call "%~dp0\SetVsEnv.bat" Win10
for /r "%~dp0\..\" %%i in (*.sys) do call :sign_if_win10 "%%i"
for /r "%~dp0\..\" %%i in (*.cat) do call :sign_if_win10 "%%i"
endlocal

setlocal
echo Loading Windows 11 build env
call "%~dp0\SetVsEnv.bat" Win10
for /r "%~dp0\..\" %%i in (*.sys) do call :sign_if_win11 "%%i"
for /r "%~dp0\..\" %%i in (*.cat) do call :sign_if_win11 "%%i"
endlocal

exit /B 0

:sign_if_win10
echo "%~1" | findstr /i /c:win10
if errorlevel 1 goto :eof
"signtool.exe" sign /fd SHA256 /f "%~dp0\VirtIOTestCert.pfx" "%~1"
goto :eof

:sign_if_win11
echo "%~1" | findstr /i /c:win11
if errorlevel 1 goto :eof
"signtool.exe" sign /fd SHA256 /f "%~dp0\VirtIOTestCert.pfx" "%~1"
goto :eof