@echo off
rem clean_build_log.bat - Used to clean log files of ANSI colour palette artefacts
rem Usage: Accepts a single parameter - the path and filename of the build log file, e.g. build_log.txt.
rem        Also accepts filenames with spaces.
cd /d %~dp0
cd ..
if not exist "%~1" goto :eof
setlocal
Title Clean Build Log
echo Waiting 5 seconds for handles to close...
timeout 5 1> nul 2>&1
echo Cleaning Build Log of colour artefacts...
rem Get the ANSI ESC character: 0x27
for /f "tokens=2 usebackq delims=#" %%i in (`"prompt #$H#$E# & echo on & for %%i in (1) do rem"`) do @set z_esc=%%i
@PowerShell "(GC '%~1')|%%{$_ -Replace '%z_esc%\[0m',''}|SC '%~1'"
@PowerShell "(GC '%~1')|%%{$_ -Replace '%z_esc%\[40\;91m',''}|SC '%~1'"
@PowerShell "(GC '%~1')|%%{$_ -Replace '%z_esc%\[40\;92m',''}|SC '%~1'"
@PowerShell "(GC '%~1')|%%{$_ -Replace '%z_esc%\[40\;93m',''}|SC '%~1'"
@PowerShell "(GC '%~1')|%%{$_ -Replace '%z_esc%\[40\;96m',''}|SC '%~1'"
@PowerShell "(GC '%~1')|%%{$_ -Replace '%z_esc%\[40\;37m',''}|SC '%~1'"
@PowerShell "(GC '%~1')|%%{$_ -Replace '%z_esc%\[',''}|SC '%~1'"
echo Build Log successfully cleaned.
echo Removing scheduled task...
schtasks /delete /tn build_log_cleanup /f
echo All done.
rem If we are in SYSTEM user context, don't bother to flash cleaning log via stdout.
if not "%USERNAME:~0,-1%"=="%COMPUTERNAME%" (
  timeout 5
)
endlocal
