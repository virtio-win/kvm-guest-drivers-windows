@echo off
call ..\build\build.bat fwcfg.sln "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\build\build.bat fwcfg.vcxproj "Win11_SDV" %*
