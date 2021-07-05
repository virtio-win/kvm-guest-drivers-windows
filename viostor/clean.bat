@echo on

rmdir /S /Q .\Install

for /d %%x in (objfre_*) do rmdir /S /Q %%x
for /d %%x in (objchk_*) do rmdir /S /Q %%x
rmdir /S /Q .\sdv
rmdir /S /Q .\sdv.temp

del /F *.log *.wrn *.err *.sdf
del viostor.dvl.xml
del viostor.dvl-compat.xml
del sdv-map.h

