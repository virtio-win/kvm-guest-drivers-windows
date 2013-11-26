@echo off
REM Set NATIVE ENV for running SDV Tool.
set vsVer=%1
set arc=%2

set query='reg query "HKLM\Software\Microsoft\VisualStudio\%vsVer%.0" /v ShellFolder'
for /F "tokens=2*" %%A in (%query%) do (
set env=%%B
if not "%%B"=="" GOTO SET
)

set query='reg query "HKLM\Software\Wow6432Node\Microsoft\VisualStudio\%vsVer%.0" /v ShellFolder'

for /F "tokens=2*" %%A in (%query%) do (
set env=%%B
if not "%%B"=="" GOTO SET
)
ECHO ERROR Couldn't find VisualStudio installation.
exit /B 1

:SET
set cmdEnv="%env%VC\vcvarsall.bat"
call %cmdEnv% %arc%



