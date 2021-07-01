@echo off
call ..\tools\build.bat vioser.sln "Wxp Wnet Wlh Win7" %*
if errorlevel 1 goto :eof
call build_NoLegacy.bat
