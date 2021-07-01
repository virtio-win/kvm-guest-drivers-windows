@echo off
call ..\tools\build.bat pvpanic.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat pvpanic\pvpanic.vcxproj "Win10_SDV"
