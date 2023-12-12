@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\tools\build.bat vioser.sln "Win10 Win11" ARM64
if errorlevel 1 goto :eof
call ..\tools\build.bat vioser.sln "Win11" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\vioser.vcxproj "Win11_SDV" %*
