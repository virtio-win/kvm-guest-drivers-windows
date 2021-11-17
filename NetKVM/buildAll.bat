@echo off
call ..\tools\build.bat netkvm-vs2015.sln "Win8 Win8.1 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat NetKVM-VS2015.vcxproj "Win10_SDV" %*
