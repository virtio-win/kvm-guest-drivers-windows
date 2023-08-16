@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\tools\build.bat pvpanic.sln Win10 ARM64
if errorlevel 1 goto :eof
call ..\tools\build.bat pvpanic.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat pvpanic\pvpanic.vcxproj "Win10_SDV"
