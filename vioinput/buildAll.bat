@echo off
call ..\tools\build.bat vioinput.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\vioinput.vcxproj "Win10_SDV" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat hidpassthrough\hidpassthrough.vcxproj "Win10_SDV" %*