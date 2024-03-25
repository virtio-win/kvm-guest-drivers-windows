@echo off
net session > nul
if not errorlevel 1 goto uninstall
echo Run this batch as an administrator
pause
goto :eof

:uninstall
cd /d "%~dp0"
netcfg -v -u VIOPROT
timeout /t 3
for %%f in (%windir%\inf\oem*.inf) do call :checkinf %%f
echo Done
timeout /t 3
goto :eof

:checkinf
type %1 | findstr /i vioprot.cat
if not errorlevel 1 goto :removeinf
echo %1 is not VIOPROT inf file
goto :eof

:removeinf
echo This is VIOPROT inf file
pnputil /d "%~nx1"
timeout /t 2
goto :eof
