@echo off

rmdir /S /Q x86
rmdir /S /Q ARM64
rmdir /S /Q x64

del /F *.log *.wrn *.err
