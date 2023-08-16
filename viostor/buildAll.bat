@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\tools\build.bat viostor.sln Win10 ARM64
if errorlevel 1 goto :eof
call ..\tools\build.bat viostor.sln "Win8 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat viostor.vcxproj "Win8_SDV Win10_SDV" %*
