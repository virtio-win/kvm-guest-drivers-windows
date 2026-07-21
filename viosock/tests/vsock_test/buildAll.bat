@echo off
rem Build vsock_test for x64 + x86 (Win11 Release) via the master build.
rem Args (all optional) are forwarded to build\build.bat, so:
rem   buildAll.bat              -> both x64 and x86
rem   buildAll.bat x64          -> only x64
rem   buildAll.bat x86          -> only x86
rem Output:
rem   x64\Win11Release\vsock_test\vsock_test.exe
rem   x86\Win11Release\vsock_test\vsock_test_x86.exe
rem install-tests.sh knows how to pick up the x86 sibling from x86\...\.
call ..\..\..\build\build.bat vsock_test.vcxproj "Win11" %*
