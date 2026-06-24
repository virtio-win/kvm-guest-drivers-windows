@echo off
cd /d %~dp0
cd ..
if not exist ".\build_log.txt" goto :eof
setlocal
Title Clean Build Log
echo Cleaning Build Log...
timeout 3 1> nul 2>&1
for /f "tokens=2 usebackq delims=#" %%i in (`"prompt #$H#$E# & echo on & for %%i in (1) do rem"`) do @set z_esc=%%i
@PowerShell "(GC .\build_log.txt)|%%{$_ -Replace '%z_esc%\[0m',''}|SC .\build_log.txt"
@PowerShell "(GC .\build_log.txt)|%%{$_ -Replace '%z_esc%\[40\;91m',''}|SC .\build_log.txt"
@PowerShell "(GC .\build_log.txt)|%%{$_ -Replace '%z_esc%\[40\;92m',''}|SC .\build_log.txt"
@PowerShell "(GC .\build_log.txt)|%%{$_ -Replace '%z_esc%\[40\;93m',''}|SC .\build_log.txt"
@PowerShell "(GC .\build_log.txt)|%%{$_ -Replace '%z_esc%\[40\;96m',''}|SC .\build_log.txt"
@PowerShell "(GC .\build_log.txt)|%%{$_ -Replace '%z_esc%\[40\;37m',''}|SC .\build_log.txt"
@PowerShell "(GC .\build_log.txt)|%%{$_ -Replace '%z_esc%\[',''}|SC .\build_log.txt"
echo Build Log successfully cleaned.
echo Removing scheduled task...
schtasks /delete /tn build_log_cleanup /f
timeout 5
endlocal
