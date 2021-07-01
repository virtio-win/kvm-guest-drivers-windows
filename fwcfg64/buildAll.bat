@echo off
call ..\tools\build.bat fwcfg.sln "Win7" x64 %*
if errorlevel 1 goto :eof
call build_NoLegacy.bat
