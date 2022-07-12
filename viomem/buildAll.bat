@echo off
call ..\tools\build.bat viomem.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\viomem.vcxproj "Win10_SDV" %*
