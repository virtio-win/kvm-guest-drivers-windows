@echo off

:checkVS11
reg query HKLM\Software\Microsoft\VisualStudio\%1.0 /v InstallDir > nul 2>nul
if %ERRORLEVEL% EQU 0 exit /b 0
reg query HKLM\Software\Wow6432Node\Microsoft\VisualStudio\%1.0 /v InstallDir > nul 2>nul
if %ERRORLEVEL% EQU 0 exit /b 0
echo ERROR building Win8 drivers: VS11 is not installed
exit /b 2