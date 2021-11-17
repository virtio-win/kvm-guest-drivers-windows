@echo off
call ..\tools\build.bat ivshmem.sln "Win8 Win8.1 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat ivshmem.vcxproj "Win10_SDV" %*
