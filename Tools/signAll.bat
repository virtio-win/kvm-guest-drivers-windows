@echo off
call "%~dp0\SetVsEnv.bat" x86
for /r "%~dp0\..\" %%i in (*.sys) do "signtool.exe" sign /f "%~dp0\VirtIOTestCert.pfx" "%%i"
for /r "%~dp0\..\" %%i in (*.cat) do "signtool.exe" sign /f "%~dp0\VirtIOTestCert.pfx" "%%i"
