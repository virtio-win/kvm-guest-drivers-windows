@echo off
call ..\..\build\clean.bat

rmdir /S /Q Release\x86
rmdir /S /Q Release\amd64
rmdir /S /Q Release
rmdir /S /Q Debug\x86
rmdir /S /Q Debug\amd64
rmdir /S /Q Debug
