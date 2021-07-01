@echo off
call ..\tools\build.bat viorng.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat viorng\viorng.vcxproj "Win10_SDV" %*
