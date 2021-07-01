@echo off
call ..\tools\build.bat ivshmem.sln "Win7" %*
if errorlevel 1 goto :eof
call build_NoLegacy.bat
