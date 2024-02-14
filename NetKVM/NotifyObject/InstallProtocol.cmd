@echo off
net session > nul
if not errorlevel 1 goto install
echo Run this batch as an administrator
pause
goto :eof
:install
cd /d "%~dp0"
netcfg -v -l vioprot.inf -c p -i VIOPROT
pause
