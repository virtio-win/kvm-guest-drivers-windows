@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\tools\build.bat vioinput.sln Win10 ARM64
if errorlevel 1 goto :eof
call ..\tools\build.bat vioinput.sln "Win10" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat sys\vioinput.vcxproj "Win10_SDV" %*
if errorlevel 1 goto :eof
call ..\tools\build.bat hidpassthrough\hidpassthrough.vcxproj "Win10_SDV" %*