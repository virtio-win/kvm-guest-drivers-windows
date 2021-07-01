@echo off
call ..\tools\build.bat viorng.sln "Wlh Win7" %*
if errorlevel 1 goto :eof
call build_NoLegacy.bat
