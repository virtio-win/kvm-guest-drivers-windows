@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\tools\build.bat viocrypt.sln Win10 ARM64
if errorlevel 1 goto :eof
call ..\tools\build.bat viocrypt.sln "Win8.1 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\viocrypt.vcxproj "Win10_SDV" %*
