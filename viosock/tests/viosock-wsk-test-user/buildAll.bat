@echo off
rem Build wsk_test_user (x64, Win11 Release) via the master build.
rem The Linux side builds with the Makefile in this directory.
rem Args after "x64" are forwarded to build\build.bat (e.g. /rebuild).
rem Output: x64\Win11Release\wsk_test_user\wsk_test_user.exe
call ..\..\..\build\build.bat wsk-test-user.vcxproj "Win11" x64 %*
