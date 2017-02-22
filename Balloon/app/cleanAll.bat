@echo off

for /d %%x in (objfre_*) do rmdir /S /Q %%x
for /d %%x in (objchk_*) do rmdir /S /Q %%x
rmdir /S /Q Release\x86
rmdir /S /Q Release\amd64
rmdir /S /Q Release
rmdir /S /Q Debug\x86
rmdir /S /Q Debug\amd64
rmdir /S /Q Debug

del /F *.log *.wrn *.err


