@echo off
call ..\tools\build.bat viogpu.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat viogpudo\viogpudo.vcxproj "Win10_SDV" %*
