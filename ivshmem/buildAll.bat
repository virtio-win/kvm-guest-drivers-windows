@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\tools\build.bat ivshmem.sln Win10 ARM64
if errorlevel 1 goto :eof
call ..\tools\build.bat ivshmem.sln "Win8.1 Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat ivshmem.vcxproj "Win10_SDV" %*
