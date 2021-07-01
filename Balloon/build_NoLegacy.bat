@echo off
call ..\tools\build.bat balloon.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\balloon.vcxproj "Win10_SDV" %*
