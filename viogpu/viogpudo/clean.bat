@echo on

rmdir /S /Q .\Install

for /d %%x in (objfre_*) do rmdir /S /Q %%x
for /d %%x in (objchk_*) do rmdir /S /Q %%x
rmdir /S /Q .\sdv
rmdir /S /Q .\sdv.temp

del /F *.log *.wrn *.err *.sdf
del viogpudo.dvl.xml
del sdv-map.h

