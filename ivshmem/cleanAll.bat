@echo on

rmdir /S /Q Install

for /d %%x in (objfre_*) do rmdir /S /Q %%x
for /d %%x in (objchk_*) do rmdir /S /Q %%x

rmdir /S /Q .\sdv
rmdir /S /Q .\sdv.temp

del /F *.log *.wrn *.err
del ivshmem.dvl.xml
del ivshmem.dvl-compat.xml
del sdv-map.h
del sdv-user.sdv
del SDV-default.xml

cd test
call cleanAll.bat
cd ..
