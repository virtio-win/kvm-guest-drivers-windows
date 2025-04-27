@echo off

rem Uncomment the following to enable ETW or debug for the hot path too. Defaults to cold path ETW only.
rem set RUN_WPP_ALL_PATHS=Yes

rem Uncomment the following to compile without ETW or debug support.
rem set FORCE_RUN_UNCHECKED=Yes
rem Uncomment the following to enable debug instead of ETW.
rem set FORCE_RUN_DEBUG=Yes

if "%VIRTIO_WIN_NO_ARM%"=="" call ..\build\build.bat vioscsi.sln "Win10 Win11" ARM64
if errorlevel 1 goto :eof
call ..\build\build.bat vioscsi.sln "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\build\build.bat vioscsi.vcxproj "Win10_SDV Win11_SDV" %*
