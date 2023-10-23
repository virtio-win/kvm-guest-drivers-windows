@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\tools\build.bat netkvm-vs2015.sln Win10 ARM64
if errorlevel 1 goto :eof
call ..\tools\build.bat netkvm-vs2015.sln "Win8.1 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat NetKVM-VS2015.vcxproj "Win10_SDV" %*
