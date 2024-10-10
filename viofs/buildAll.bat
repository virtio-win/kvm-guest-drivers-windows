@echo off
call ..\build\build.bat viofs.sln "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\build\build.bat pci\viofs.vcxproj "Win11_SDV" %*
