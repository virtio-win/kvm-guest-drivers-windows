@echo off
call ..\tools\build.bat viostor.sln "Wnet Wlh Win7 Win8 Win10" %*
rem call ..\tools\build.bat viostor.sln "Win8" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat viostor.vcxproj "Win8_SDV Win10_SDV" %*
