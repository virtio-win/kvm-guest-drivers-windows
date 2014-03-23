@echo off

rmdir /S /Q objfre_win7_x86
rmdir /S /Q objfre_win7_amd64
rmdir /S /Q objfre_win8_x86
rmdir /S /Q objfre_win8_amd64

del /F *.log *.wrn *.err

del 2012-defines.h

