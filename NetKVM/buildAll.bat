@echo off
call ..\tools\build.bat netkvm-vs2015.sln "Win7" %*
if errorlevel 1 goto :eof
call build_NoLegacy.bat
if errorlevel 1 goto :eof
::call ..\tools\build.bat NDIS5\NetKVM-NDIS5.sln "WXp Wnet Wlh" %*
