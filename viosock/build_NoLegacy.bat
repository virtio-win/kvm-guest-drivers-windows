@echo off
call ..\tools\build.bat viosock.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\viosock.vcxproj "Win10_SDV" %*
