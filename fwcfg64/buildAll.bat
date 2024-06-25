@echo off
call ..\tools\build.bat fwcfg.sln "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat fwcfg.vcxproj "Win11_SDV" %*
