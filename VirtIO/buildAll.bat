@echo off
::call ..\tools\build.bat VirtioLib.sln "Wxp Wnet Wlh" %*
call ..\tools\build.bat VirtioLib.sln "Win7" %*
if errorlevel 1 goto :eof
call build_NoLegacy.bat
