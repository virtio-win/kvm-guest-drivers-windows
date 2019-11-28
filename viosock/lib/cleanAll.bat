@echo off

rmdir /S /Q Win32
rmdir /S /Q ARM64
rmdir /S /Q x64

del /F *.log *.wrn *.err
