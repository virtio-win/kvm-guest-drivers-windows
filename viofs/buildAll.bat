@echo off
call ..\tools\build.bat viofs.sln "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat pci\viofs.vcxproj "Win11_SDV" %*
