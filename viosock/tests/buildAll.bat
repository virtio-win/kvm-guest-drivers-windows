@echo off
call ..\..\build\build.bat viosock-test\viosock-test.vcxproj "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\..\build\build.bat viosock-wsk-test\viosock-wsk-test.vcxproj "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\..\build\build.bat viosocklib-test\viosocklib-test.vcxproj "Win10 Win11" %*
if errorlevel 1 goto :eof
call ..\..\build\build.bat vsock_test\vsock_test.vcxproj "Win10 Win11" %*
