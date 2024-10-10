@echo off
if "%VIRTIO_WIN_NO_ARM%"=="" call ..\build\build.bat viogpu.sln "Win10 Win11" ARM64
if errorlevel 1 goto :eof
call ..\build\build.bat viogpu.sln "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\build\build.bat viogpudo\viogpudo.vcxproj "Win11_SDV" %*
