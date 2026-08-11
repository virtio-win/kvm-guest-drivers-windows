@echo off
for /d %%d in (objfre_*) do rmdir /s /q "%%d" 2>nul
exit /b 0
