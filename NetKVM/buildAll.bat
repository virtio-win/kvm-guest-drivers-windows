@echo off
call ..\tools\build.bat netkvm-vs2015.sln "Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat NetKVM-VS2015.vcxproj "Win10_SDV" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat netkvm-vs2015.sln "Win7 Win8 Win8.1" %*
if errorlevel 1 goto :eof
::call ..\tools\build.bat NDIS5\NetKVM-NDIS5.sln "WXp Wnet Wlh" %*
