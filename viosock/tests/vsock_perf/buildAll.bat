@echo off
rem Build vsock_perf (x64, Win11 Release) via the master build.
rem The vcxproj carries only x64 + ARM64; the master build script would
rem otherwise try the (missing) Win32 config too and log a harmless
rem MSB8013 — hardcoding x64 keeps the log clean.  ARM64 is not built
rem by default either; pass it explicitly if you need it.
rem Args after "x64" are forwarded to build\build.bat (e.g. /rebuild).
rem Output: x64\Win11Release\vsock_perf\vsock_perf.exe
call ..\..\..\build\build.bat vsock_perf.vcxproj "Win11" x64 %*
