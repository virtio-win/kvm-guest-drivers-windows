@echo off
call ..\tools\build.bat fwcfg.sln "Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat fwcfg.vcxproj "Win10_SDV" %*
