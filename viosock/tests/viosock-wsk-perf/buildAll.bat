@echo off
rem Build the viosockwsk-perf kernel driver (x64, Win11 Release) via the
rem master build. The viosockwsk library is built through the project
rem reference. Args after "x64" are forwarded to build\build.bat.
rem Output: objfre_win11_amd64\amd64\viosockwsk-perf.sys
call ..\..\..\build\build.bat viosock-wsk-perf.vcxproj "Win11" x64 %*
